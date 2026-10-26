// Metal backend — iOS 11+, arm64.
// Draws either the procedural fallback or real geometry from the data pack, with an
// optional texture sampled through the vertex UVs.
#import "Backend.h"
#import "AssetModel.h"
#import "Log.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <vector>

#include "geometry.h"

static NSString *const kShaderSource = @R"METAL(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    packed_float3 pos;
    packed_float3 nrm;
    packed_float2 uv;
    packed_float3 col;
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
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLSamplerState> sampler;
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic, strong) id<MTLBuffer> sceneBuffer;
@property(nonatomic, strong) id<MTLBuffer> sceneIndexBuffer;
@property(nonatomic, assign) NSUInteger sceneIndexCount;
@property(nonatomic, assign) BOOL sceneIsWorld;
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

    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    self.depthState = [self.device newDepthStencilStateWithDescriptor:dd];

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = MTLSamplerAddressModeRepeat;
    sd.tAddressMode = MTLSamplerAddressModeRepeat;
    self.sampler = [self.device newSamplerStateWithDescriptor:sd];

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

    if (scene && scene.textureRGBA.length > 0) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:scene.texWidth
                                        height:scene.texHeight
                                     mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        self.texture = [self.device newTextureWithDescriptor:td];
        [self.texture replaceRegion:MTLRegionMake2D(0, 0, scene.texWidth, scene.texHeight)
                        mipmapLevel:0
                          withBytes:scene.textureRGBA.bytes
                        bytesPerRow:scene.texWidth * 4];
        MWLog(@"texture bound: %lux%lu", (unsigned long)scene.texWidth,
              (unsigned long)scene.texHeight);
    }

    MWLog(@"MWBackendMetal ready: world=%d indices=%lu verts=%lu tex=%d",
          self.sceneIsWorld ? 1 : 0, (unsigned long)self.sceneIndexCount,
          (unsigned long)mesh->vertices.size(), self.texture ? 1 : 0);
    return view;
}

- (void)start {
    [self.view setPaused:NO];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(MTKView *)view {
    if (!self.pipeline || self.sceneIndexCount == 0) return;
    id<MTLCommandBuffer> cb = [self.queue commandBuffer];
    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
    if (!rp) {
        [cb commit];
        return;
    }
    const float t = (float)(CACurrentMediaTime() - self.startTime);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:self.pipeline];
    [enc setDepthStencilState:self.depthState];
    [enc setCullMode:MTLCullModeBack];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];

    const float aspect = (float)view.drawableSize.width / (float)view.drawableSize.height;
    mw::Mat4 proj = mw::matPerspective(60.0f * (float)M_PI / 180.0f, aspect, 0.1f, 400.0f);

    // World: look at the chunk from a fixed isometric-ish angle and rotate slowly.
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
