#include "miniworld/assets_bin.h"

#include <cstring>

namespace mw {
namespace {

std::uint32_t rd32(const std::uint8_t *p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

std::uint16_t rd16(const std::uint8_t *p) {
    return (std::uint16_t)((std::uint16_t)p[0] | ((std::uint16_t)p[1] << 8));
}

bool need(std::size_t off, std::size_t want, std::size_t size) {
    return off <= size && want <= size - off;
}

}  // namespace

bool loadPackedAssets(const std::uint8_t *data, std::size_t size, PackedAssets &out) {
    out = PackedAssets{};
    if (!data || size < 12) return false;
    if (std::memcmp(data, "MWPREV1", 7) != 0) return false;

    std::size_t off = 8;
    if (!need(off, 4, size)) return false;
    const std::uint32_t texCount = rd32(&data[off]);
    off += 4;
    if (texCount > 100000) return false;

    out.textures.reserve(texCount);
    for (std::uint32_t i = 0; i < texCount; ++i) {
        if (!need(off, 2, size)) return false;
        const std::uint16_t nameLen = rd16(&data[off]);
        off += 2;
        if (!need(off, nameLen, size)) return false;
        PackedTexture tex;
        tex.name.assign((const char *)&data[off], nameLen);
        off += nameLen;
        if (!need(off, 8, size)) return false;
        tex.width = rd32(&data[off]);
        tex.height = rd32(&data[off + 4]);
        off += 8;
        const std::size_t bytes = (std::size_t)tex.width * tex.height * 4;
        if (tex.width == 0 || tex.height == 0 || !need(off, bytes, size)) return false;
        tex.rgba.assign(data + off, data + off + bytes);
        off += bytes;
        out.textures.push_back(std::move(tex));
    }

    if (!need(off, 4, size)) return false;
    const std::uint32_t blen = rd32(&data[off]);
    off += 4;
    if (!need(off, blen, size)) return false;
    out.blockdef.assign((const char *)&data[off], blen);
    off += blen;

    if (!need(off, 12, size)) return false;
    out.nx = (int)rd32(&data[off]);
    out.ny = (int)rd32(&data[off + 4]);
    out.nz = (int)rd32(&data[off + 8]);
    off += 12;
    const std::size_t vox = (std::size_t)out.nx * out.ny * out.nz;
    if (out.nx <= 0 || out.ny <= 0 || out.nz <= 0 || !need(off, vox, size)) return false;
    out.voxels.assign(data + off, data + off + vox);
    return true;
}

void resizeToSquare(const PackedTexture &tex, std::uint32_t side,
                    std::vector<std::uint8_t> &out) {
    out.resize((std::size_t)side * side * 4);
    if (tex.width == 0 || tex.height == 0 || tex.rgba.empty()) {
        std::memset(out.data(), 0, out.size());
        return;
    }
    for (std::uint32_t y = 0; y < side; ++y) {
        const std::uint32_t sy = (std::uint32_t)(((std::uint64_t)y * tex.height) / side);
        for (std::uint32_t x = 0; x < side; ++x) {
            const std::uint32_t sx = (std::uint32_t)(((std::uint64_t)x * tex.width) / side);
            const std::uint8_t *src = &tex.rgba[((std::size_t)sy * tex.width + sx) * 4];
            std::uint8_t *dst = &out[((std::size_t)y * side + x) * 4];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
}

}  // namespace mw
