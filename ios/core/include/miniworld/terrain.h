// Block table + textured quad extraction, shared by the previewer and the iOS renderer.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mw {

struct BlockInfo {
    std::string name;
    std::string type;
    std::string texture;
};

// Parses the decrypted blockdef.csv (two header rows; row 1 carries the field names).
class BlockTable {
public:
    bool parse(const std::string &csv);
    const BlockInfo *find(int id) const;

    // Texture name for one face of a block. face: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z.
    // Mirrors the runtime rules: Type == "grass" uses dedicated top/side/bottom art,
    // otherwise Texture1 with an optional _top/_side/_bottom variant.
    std::string textureFor(const BlockInfo &block, int face, bool hasTexture) const;

private:
    std::unordered_map<int, BlockInfo> blocks_;
};

struct Quad {
    float pos[4][3];
    float uv[4][2];
    int face = 0;
    std::uint16_t texture = 0;  // index supplied by the caller
};

// Extracts every visible face (a face touching air or the volume border).
// `textureIndex` maps a texture name to a slot; return -1 to drop the face.
std::vector<Quad> buildTerrainQuads(
    const std::uint8_t *voxels, int nx, int ny, int nz, const BlockTable &table,
    const std::unordered_map<std::string, std::uint16_t> &textureIndex);

}  // namespace mw
