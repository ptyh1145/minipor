// Host validation for mw::buildTerrainQuads: reads the packed asset file, extracts quads and
// prints statistics. Compiled locally with g++ (no iOS toolchain needed).
//
//   terrain_check <assets.bin>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

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

std::uint32_t rd32(const std::uint8_t *p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
std::uint16_t rd16(const std::uint8_t *p) {
    return (std::uint16_t)((std::uint16_t)p[0] | ((std::uint16_t)p[1] << 8));
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <assets.bin>\n", argv[0]);
        return 2;
    }
    std::vector<std::uint8_t> data;
    if (!readFile(argv[1], data)) { printf("cannot read %s\n", argv[1]); return 1; }
    if (data.size() < 12 || memcmp(data.data(), "MWPREV1", 7) != 0) { printf("bad magic\n"); return 1; }

    size_t off = 8;
    std::uint32_t tcount = rd32(&data[off]); off += 4;
    std::unordered_map<std::string, std::uint16_t> texIndex;
    for (std::uint32_t i = 0; i < tcount; ++i) {
        std::uint16_t nlen = rd16(&data[off]); off += 2;
        std::string name((const char *)&data[off], nlen); off += nlen;
        std::uint32_t w = rd32(&data[off]); off += 4;
        std::uint32_t h = rd32(&data[off]); off += 4;
        off += (size_t)w * h * 4;
        texIndex[name] = (std::uint16_t)i;
    }
    std::uint32_t blen = rd32(&data[off]); off += 4;
    std::string csv((const char *)&data[off], blen); off += blen;
    int nx = (int)rd32(&data[off]); off += 4;
    int ny = (int)rd32(&data[off]); off += 4;
    int nz = (int)rd32(&data[off]); off += 4;
    const std::uint8_t *vox = data.data() + off;

    mw::BlockTable table;
    if (!table.parse(csv)) { printf("block table parse failed\n"); return 1; }

    std::vector<mw::Quad> quads = mw::buildTerrainQuads(vox, nx, ny, nz, table, texIndex);

    int perFace[6] = {0, 0, 0, 0, 0, 0};
    int usedTex = 0;
    std::unordered_map<int, int> byTex;
    for (const mw::Quad &q : quads) {
        perFace[q.face]++;
        byTex[q.texture]++;
    }
    usedTex = (int)byTex.size();

    printf("world %dx%dx%d, textures %u\n", nx, ny, nz, (unsigned)tcount);
    printf("quads %zu, distinct textures used %d\n", quads.size(), usedTex);
    printf("faces: +x %d  -x %d  +y %d  -y %d  +z %d  -z %d\n",
           perFace[0], perFace[1], perFace[2], perFace[3], perFace[4], perFace[5]);
    return quads.empty() ? 1 : 0;
}
