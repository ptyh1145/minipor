// 48^3 voxel chunks and the block table that colours them.
//
// Chunk files (`worlds/<id>/m0/x*z*.r`) are exactly 110,592 bytes = 48^3, one byte per
// voxel. Block ids are resolved through `csvdef/blockdef.csv`.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniworld/csv.h"
#include "miniworld/mesh.h"

namespace mw {

constexpr int kChunkSize = 48;
constexpr std::size_t kChunkBytes = static_cast<std::size_t>(kChunkSize) * kChunkSize * kChunkSize;

struct BlockInfo {
    std::string name;
    std::string type;
    int textureGroup = 0;
};

class BlockTable {
public:
    // Expects blockdef.csv already parsed; uses row 1 (English field names) as the header.
    bool load(const CsvTable &csv);
    const BlockInfo *find(int id) const;
    // Stable per-block colour derived from the texture group / type so a chunk is readable
    // even without textures.
    std::uint32_t colorFor(int id) const;
    std::size_t size() const { return blocks_.size(); }

private:
    std::unordered_map<int, BlockInfo> blocks_;
};

// Verifies the byte count and returns the voxel array (empty on mismatch).
bool decodeChunk(const std::vector<std::uint8_t> &raw, std::vector<std::uint8_t> &voxels);

// Emits only faces that touch air or the chunk border.
void buildChunkMesh(const std::vector<std::uint8_t> &voxels, const BlockTable &table,
                    MeshBuffer &out, int originX, int originY, int originZ, float scale = 1.0f);

// Convenience for the demo: index inside the 48^3 array.
inline std::size_t chunkIndex(int x, int y, int z) {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(z) * kChunkSize +
           static_cast<std::size_t>(y) * kChunkSize * kChunkSize;
}

}  // namespace mw
