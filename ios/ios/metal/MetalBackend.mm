// Metal backend — iOS 11+, arm64.
//
// Draw order of preference:
//   1. terrain from Documents/assets.bin  (packed textures + voxels + block table)
//   2. real geometry from the data pack    (AssetModel)
//   3. procedural fallback                 (cube + voxel cluster)
#import "Backend.h"
#import "AssetModel.h"
#import "TerrainScene.h"
#import "Log.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <vector>

#include "geometry.h"
#include "miniworld/assets_bin.h"

static NSString *const kShaderSource = @R"METAL(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    packed_float3 pos;
    packed_float3 nrm;
    packed_float2 uv;
    packed_float3 col;
};

struct TVertex {
    packed_float3 pos;
    packed_float2 uv;
    float slice;
    float shade;
};

struct Uniforms {
    float4x4 mvp;
    float useTex;
};

struct VOut {
    float4 position [[position]];
    float3 nrm;
    float2 uv;
    float3 col;
};

struct TVOut {
    float4 position [[position]];
    float2 uv;
    float slice;
    float shade;
};

vertex VOut v_main(uint vid [[vertex_id]],
                   device const Vertex *verts [[buffer(0)]],
                   constant Uniforms &u [[buffer(1)]]) {
    VOut out;
    Vertex v = verts[vid];
    out.position = u.mvp * float4(v.pos, 1.0);
    out.nrm = v.nrm;
    out.uv = v.uv;
    out.col = v.col;
    return out;
}

fragment float4 f_main(VOut in [[stage_in]],
                       constant Uniforms &u [[buffer(1)]],
                       texture2d<float> tex [[texture(0)]],
                       sampler samp [[sampler(0)]]) {
    float3 base = in.col;
    if (u.useTex > 0.5) {
        base *= tex.sample(samp, in.uv).rgb;
    }
    float3 n = normalize(in.nrm);
    float3 l = normalize(float3(0.45, 0.85, 0.55));
    float diff = max(dot(n, l), 0.0);
    float3 c = base * (0.28 + 0.72 * diff);
    float rim = pow(1.0 - max(abs(n.z), 0.0), 3.0) * 0.18;
    return float4(c + rim, 1.0);
}

vertex TVOut tv_main(uint vid [[vertex_id]],
                     device const TVertex *verts [[buffer(0)]],
                     constant Uniforms &u [[buffer(1)]]) {
    TVOut out;
    TVertex v = verts[vid];
    out.position = u.mvp * float4(v.pos, 1.0);
    out.uv = v.uv;
    out.slice = v.slice;
    out.shade = v.shade;
    return out;
}

fragment float4 tf_main(TVOut in [[stage_in]],
                        texture2d_array<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    float4 c = tex.sample(samp, in.uv, uint(in.slice + 0.5));
    return float4(c.rgb * in.shade, 1.0);
}
)METAL";

struct UniformsC {
    float mvp[16];
    float useTex;
};

@interface MWBackendMetal () <MTKViewDelegate>
@property(nonatomic, strong) MTKView *view;
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> terrainPipeline;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLSamplerState> sampler;
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic, strong) id<MTLBuffer> sceneBuffer;
@property(nonatomic, strong) id<MTLBuffer> sceneIndexBuffer;
@property(nonatomic, assign) NSUInteger sceneIndexCount;
@property(nonatomic, assign) BOOL sceneIsWorld;
@property(nonatomic, strong) id<MTLBuffer> terrainVertexBuffer;
@property(nonatomic, strong) id<MTLBuffer> terrainIndexBuffer;
@property(nonatomic, strong) id<MTLTexture> terrainTexture;
@property(nonatomic, assign) NSUInteger terrainIndexCount;
@property(nonatomic, assign) float terrainExtent;
@property(nonatomic, assign) float terrainHeight;
@property(nonatomic, assign) double startTime;
@end

@implementation MWBackendMetal

- (UIView *)makeViewWithFrame:(CGRect)frame {
    self.device = MTLCreateSystemDefaultDevice();
    if (!self.device) {
        MWLog(@"Metal unavailable on this device");
        return nil;
    }
    self.queue = [self.device newCommandQueue];
    self.startTime = CACurrentMediaTime();

    MTKView *view = [[MTKView alloc] initWithFrame:frame device:self.device];
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    view.clearColor = MTLClearColorMake(0.05, 0.06, 0.09, 1.0);
    view.preferredFramesPerSecond = 60;
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    view.delegate = self;
    self.view = view;

    NSError *error = nil;
    id<MTLLibrary> lib = [self.device newLibraryWithSource:kShaderSource options:nil error:&error];
    if (!lib) {
        MWLog(@"shader compile failed: %@", error);
        return view;
    }

    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"v_main"];
    pd.fragmentFunction = [lib newFunctionWithName:@"f_main"];
    pd.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    pd.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    self.pipeline = [self.device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!self.pipeline) MWLog(@"pipeline failed: %@", error);

    MTLRenderPipelineDescriptor *td = [MTLRenderPipelineDescriptor new];
    td.vertexFunction = [lib newFunctionWithName:@"tv_main"];
    td.fragmentFunction = [lib newFunctionWithName:@"tf_main"];
    td.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    td.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    self.terrainPipeline = [self.device newRenderPipelineStateWithDescriptor:td error:&error];
    if (!self.terrainPipeline) MWLog(@"terrain pipeline failed: %@", error);

    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    self.depthState = [self.device newDepthStencilStateWithDescriptor:dd];

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    self.sampler = [self.device newSamplerStateWithDescriptor:sd];

    // ---- 1. terrain ------------------------------------------------------
    [self prepareTerrain];

    // ---- 2/3. packed model or procedural fallback ------------------------
    if (self.terrainIndexCount == 0) {
        MWModelData *scene = MWLoadScene();
        mw::MeshBuffer fallback;
        const mw::MeshBuffer *mesh = nullptr;
        if (scene && scene.vertexCount > 0) {
            mesh = [scene buffer];
            self.sceneIsWorld = scene.isWorld;
        } else {
            auto cube = mw::buildCube(0, 0, 0, 0.62f);
            auto voxels = mw::buildVoxelCluster();
            fallback.vertices = cube;
            fallback.vertices.insert(fallback.vertices.end(), voxels.begin(), voxels.end());
            for (std::uint32_t i = 0; i < fallback.vertices.size(); ++i) fallback.indices.push_back(i);
            mesh = &fallback;
        }
        self.sceneIndexCount = mesh->indices.size();
        self.sceneBuffer = [self.device newBufferWithBytes:mesh->vertices.data()
                                                    length:mesh->vertices.size() * sizeof(mw::MeshVertex)
                                                   options:MTLResourceStorageModeShared];
        self.sceneIndexBuffer = [self.device newBufferWithBytes:mesh->indices.data()
                                                         length:mesh->indices.size() * sizeof(std::uint32_t)
                                                        options:MTLResourceStorageModeShared];
        MWLog(@"fallback scene: %lu indices", (unsigned long)self.sceneIndexCount);
    }

    MWLog(@"MWBackendMetal ready: terrain=%d indices=%lu pipeline=%d",
          self.terrainIndexCount > 0 ? 1 : 0, (unsigned long)self.terrainIndexCount,
          self.terrainPipeline ? 1 : 0);
    return view;
}

- (void)prepareTerrain {
    MWTerrain *terrain = MWLoadTerrainFromDocuments();
    if (!terrain || terrain.indexCount == 0 || !terrain.textureArray.length) return;
    if (!self.terrainPipeline) {
        MWLog(@"terrain pipeline unavailable, keeping fallback");
        return;
    }

    self.terrainIndexCount = terrain.indexCount;
    self.terrainVertexBuffer = [self.device newBufferWithBytes:[terrain vertices]
                                                       length:terrain.vertexCount * sizeof(MWVertex)
                                                      options:MTLResourceStorageModeShared];
    self.terrainIndexBuffer = [self.device newBufferWithBytes:[terrain indices]
                                                      length:terrain.indexCount * sizeof(std::uint32_t)
                                                     options:MTLResourceStorageModeShared];

    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:terrain.sliceSize
                                    height:terrain.sliceSize
                                 mipmapped:NO];
    desc.textureType = MTLTextureType2DArray;
    desc.arrayLength = terrain.sliceCount;
    desc.usage = MTLTextureUsageShaderRead;
    self.terrainTexture = [self.device newTextureWithDescriptor:desc];

    const NSUInteger sliceBytes = terrain.sliceSize * terrain.sliceSize * 4;
    const std::uint8_t *base = (const std::uint8_t *)terrain.textureArray.bytes;
    for (NSUInteger i = 0; i < terrain.sliceCount; ++i) {
        [self.terrainTexture replaceRegion:MTLRegionMake3D(0, 0, i, terrain.sliceSize,
                                                          terrain.sliceSize, 1)
                               mipmapLevel:0
                                     slice:i
                                 withBytes:base + i * sliceBytes
                               bytesPerRow:terrain.sliceSize * 4
                             bytesPerImage:sliceBytes];
    }

    // camera framing from the label's world size
    NSArray<NSString *> *parts = [terrain.label componentsSeparatedByString:@" "];
    float extent = 96.f, height = 48.f;
    if (parts.count >= 2) {
        NSArray<NSString *> *dims = [parts.lastObject componentsSeparatedByString:@"x"];
        if (dims.count == 3) {
            extent = (float)[dims[0] floatValue];
            height = (float)[dims[1] floatValue];
        }
    }
    self.terrainExtent = extent;
    self.terrainHeight = height;
    MWLog(@"terrain uploaded: %@ slices=%lu extent=%.0f", terrain.label,
          (unsigned long)terrain.sliceCount, extent);
}

- (void)start {
    [self.view setPaused:NO];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(MTKView *)view {
    const BOOL useTerrain = self.terrainIndexCount > 0 && self.terrainTexture;
    if (!useTerrain && (!self.pipeline || self.sceneIndexCount == 0)) return;

    id<MTLCommandBuffer> cb = [self.queue commandBuffer];
    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
    if (!rp) {
        [cb commit];
        return;
    }
    const float t = (float)(CACurrentMediaTime() - self.startTime);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setDepthStencilState:self.depthState];
    [enc setCullMode:MTLCullModeBack];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];

    const float aspect = (float)view.drawableSize.width / (float)view.drawableSize.height;
    mw::Mat4 proj = mw::matPerspective(60.0f * (float)M_PI / 180.0f, aspect, 0.1f, 600.0f);

    if (useTerrain) {
        const float extent = self.terrainExtent > 1.f ? self.terrainExtent : 96.f;
        const float height = self.terrainHeight > 1.f ? self.terrainHeight : 48.f;
        mw::Mat4 view4 = mw::matTranslation(0, -height * 0.55f, -extent * 1.35f);
        mw::Mat4 rot = mw::matMul(mw::matRotationY(0.6f + t * 0.10f), mw::matRotationX(0.55f));

        UniformsC u{};
        const mw::Mat4 mvp = mw::matMul(proj, mw::matMul(view4, rot));
        for (int i = 0; i < 16; ++i) u.mvp[i] = mvp.m[i];

        [enc setRenderPipelineState:self.terrainPipeline];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:1];
        [enc setVertexBuffer:self.terrainVertexBuffer offset:0 atIndex:0];
        [enc setFragmentTexture:self.terrainTexture atIndex:0];
        [enc setFragmentSamplerState:self.sampler atIndex:0];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:self.terrainIndexCount
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:self.terrainIndexBuffer
                 indexBufferOffset:0];
        [enc endEncoding];
        [cb presentDrawable:view.currentDrawable];
        [cb commit];
        return;
    }

    [enc setRenderPipelineState:self.pipeline];
    const float distance = self.sceneIsWorld ? 78.0f : 3.0f;
    const float height = self.sceneIsWorld ? -30.0f : 0.0f;
    mw::Mat4 view4 = mw::matTranslation(0, height, -distance);
    mw::Mat4 rot = mw::matRotationY(self.sceneIsWorld ? (0.6f + t * 0.12f) : (t * 0.5f));
    if (self.sceneIsWorld) rot = mw::matMul(rot, mw::matRotationX(0.55f));

    UniformsC u{};
    const mw::Mat4 mvp = mw::matMul(proj, mw::matMul(view4, rot));
    for (int i = 0; i < 16; ++i) u.mvp[i] = mvp.m[i];
    u.useTex = self.texture ? 1.0f : 0.0f;

    [enc setVertexBytes:&u length:sizeof(u) atIndex:1];
    [enc setFragmentBytes:&u length:sizeof(u) atIndex:1];
    if (self.texture) {
        [enc setFragmentTexture:self.texture atIndex:0];
        [enc setFragmentSamplerState:self.sampler atIndex:0];
    }
    [enc setVertexBuffer:self.sceneBuffer offset:0 atIndex:0];
    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:self.sceneIndexCount
                     indexType:MTLIndexTypeUInt32
                   indexBuffer:self.sceneIndexBuffer
             indexBufferOffset:0];

    [enc endEncoding];
    [cb presentDrawable:view.currentDrawable];
    [cb commit];
}

@end
