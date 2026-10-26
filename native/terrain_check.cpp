// Host validation for the packed-asset loader and the terrain mesher.
// Compiled locally with g++ and in CI (no iOS toolchain needed).
//
//   terrain_check <assets.bin>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniworld/assets_bin.h"
#include "miniworld/terrain.h"

namespace {

bool readFile(const char *path, std::vector<std::uint8_t> &out) {
    FILE *fh = fopen(path, "rb");
    if (!fh) return false;
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), fh) == out.size();
    fclose(fh);
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <assets.bin>\n", argv[0]);
        return 2;
    }
    std::vector<std::uint8_t> data;
    if (!readFile(argv[1], data)) { printf("cannot read %s\n", argv[1]); return 1; }

    mw::PackedAssets assets;
    if (!mw::loadPackedAssets(data.data(), data.size(), assets)) {
        printf("packed asset parse failed\n");
        return 1;
    }
    printf("textures %zu, world %dx%dx%d\n", assets.textures.size(),
           assets.nx, assets.ny, assets.nz);

    mw::BlockTable table;
    if (!table.parse(assets.blockdef)) { printf("block table parse failed\n"); return 1; }

    std::unordered_map<std::string, std::uint16_t> texIndex;
    for (std::size_t i = 0; i < assets.textures.size(); ++i) {
        texIndex[assets.textures[i].name] = (std::uint16_t)i;
    }

    std::vector<mw::Quad> quads = mw::buildTerrainQuads(
        assets.voxels.data(), assets.nx, assets.ny, assets.nz, table, texIndex);

    int perFace[6] = {0, 0, 0, 0, 0, 0};
    for (const mw::Quad &q : quads) perFace[q.face]++;

    printf("quads %zu\n", quads.size());
    printf("faces: +x %d  -x %d  +y %d  -y %d  +z %d  -z %d\n",
           perFace[0], perFace[1], perFace[2], perFace[3], perFace[4], perFace[5]);

    // exercise the resize path used to build the Metal texture array
    std::vector<std::uint8_t> slice;
    mw::resizeToSquare(assets.textures.front(), 128, slice);
    printf("slice bytes %zu\n", slice.size());

    return quads.empty() ? 1 : 0;
}
