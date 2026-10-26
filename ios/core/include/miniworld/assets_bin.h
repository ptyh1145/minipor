// Reader for the packed asset bundle produced by tools/pack_assets.py.
//
// Layout (little-endian):
//   "MWPREV1\0"
//   u32 textureCount, then per texture: u16 nameLen, name, u32 w, u32 h, RGBA bytes
//   u32 blockdefLen, blockdef CSV text
//   u32 nx, u32 ny, u32 nz, nx*ny*nz voxel bytes
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mw {

struct PackedTexture {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

struct PackedAssets {
    std::vector<PackedTexture> textures;
    std::string blockdef;
    int nx = 0, ny = 0, nz = 0;
    std::vector<std::uint8_t> voxels;

    bool empty() const { return voxels.empty(); }
};

// Returns false when the file is missing or malformed.
bool loadPackedAssets(const std::uint8_t *data, std::size_t size, PackedAssets &out);

// Nearest-neighbour resize into a fixed square, used to build a Metal texture array
// (array slices must share one size).
void resizeToSquare(const PackedTexture &tex, std::uint32_t side,
                    std::vector<std::uint8_t> &out);

}  // namespace mw
