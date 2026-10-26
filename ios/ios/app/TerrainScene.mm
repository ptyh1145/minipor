#import "TerrainScene.h"
#import "Log.h"

#include <cstdio>
#include <string>
#include <unordered_map>

#include "miniworld/assets_bin.h"

namespace {

const std::uint32_t kSlice = 128;  // texture array slice size

std::vector<std::uint8_t> readDocuments(const char *name) {
    std::vector<std::uint8_t> out;
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (dirs.count == 0) return out;
    NSString *path = [[dirs firstObject] stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
    FILE *fh = fopen(path.UTF8String, "rb");
    if (!fh) return out;
    fseek(fh, 0, SEEK_END);
    const long n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (n > 0) {
        out.resize((std::size_t)n);
        if (fread(out.data(), 1, out.size(), fh) != out.size()) out.clear();
    }
    fclose(fh);
    return out;
}

float faceShade(int face) {
    static const float s[6] = {0.80f, 0.70f, 1.00f, 0.42f, 0.90f, 0.62f};
    return (face >= 0 && face < 6) ? s[face] : 0.8f;
}

}  // namespace

@interface MWTerrain () {
    std::vector<MWVertex> _vertices;
    std::vector<std::uint32_t> _indices;
}
@end

@implementation MWTerrain
- (const MWVertex *)vertices { return _vertices.data(); }
- (const std::uint32_t *)indices { return _indices.data(); }
- (void)setGeometry:(std::vector<MWVertex> &&)v indices:(std::vector<std::uint32_t> &&)i {
    _vertices = std::move(v);
    _indices = std::move(i);
    self.vertexCount = _vertices.size();
    self.indexCount = _indices.size();
}
@end

MWTerrain *MWLoadTerrainFromDocuments(void) {
    std::vector<std::uint8_t> blob = readDocuments("assets.bin");
    if (blob.empty()) {
        MWLog(@"assets.bin not found in Documents");
        return nil;
    }
    MWLog(@"assets.bin: %.1f MB", blob.size() / 1048576.0);

    mw::PackedAssets assets;
    if (!mw::loadPackedAssets(blob.data(), blob.size(), assets)) {
        MWLog(@"assets.bin parse failed");
        return nil;
    }
    MWLog(@"packed: %zu textures, world %dx%dx%d", assets.textures.size(), assets.nx, assets.ny, assets.nz);

    mw::BlockTable table;
    if (!table.parse(assets.blockdef)) {
        MWLog(@"block table parse failed");
        return nil;
    }

    std::unordered_map<std::string, std::uint16_t> texIndex;
    for (std::size_t i = 0; i < assets.textures.size(); ++i) {
        texIndex[assets.textures[i].name] = (std::uint16_t)i;
    }

    std::vector<mw::Quad> quads = mw::buildTerrainQuads(
        assets.voxels.data(), assets.nx, assets.ny, assets.nz, table, texIndex);
    MWLog(@"terrain quads: %zu", quads.size());
    if (quads.empty()) return nil;

    std::vector<MWVertex> verts;
    std::vector<std::uint32_t> idx;
    verts.reserve(quads.size() * 4);
    idx.reserve(quads.size() * 6);
    for (const mw::Quad &q : quads) {
        const float shade = faceShade(q.face);
        const std::uint32_t base = (std::uint32_t)verts.size();
        for (int c = 0; c < 4; ++c) {
            MWVertex v{};
            v.pos[0] = q.pos[c][0];
            v.pos[1] = q.pos[c][1];
            v.pos[2] = q.pos[c][2];
            v.uv[0] = q.uv[c][0];
            v.uv[1] = q.uv[c][1];
            v.slice = (float)q.texture;
            v.shade = shade;
            verts.push_back(v);
        }
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }

    // pack every texture into one array so a single bind covers the whole world
    std::vector<std::uint8_t> array((std::size_t)assets.textures.size() * kSlice * kSlice * 4);
    std::vector<std::uint8_t> slice;
    for (std::size_t i = 0; i < assets.textures.size(); ++i) {
        mw::resizeToSquare(assets.textures[i], kSlice, slice);
        std::memcpy(&array[i * kSlice * kSlice * 4], slice.data(), slice.size());
    }

    MWTerrain *t = [[MWTerrain alloc] init];
    t.sliceCount = assets.textures.size();
    t.sliceSize = kSlice;
    t.textureArray = [NSData dataWithBytes:array.data() length:array.size()];
    t.label = [NSString stringWithFormat:@"terrain %dx%dx%d", assets.nx, assets.ny, assets.nz];
    [t setGeometry:std::move(verts) indices:std::move(idx)];
    MWLog(@"terrain ready: %lu verts, %lu indices, %lu slices",
          (unsigned long)t.vertexCount, (unsigned long)t.indexCount, (unsigned long)t.sliceCount);
    return t;
}
