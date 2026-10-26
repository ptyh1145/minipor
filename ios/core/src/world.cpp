#include "miniworld/world.h"

#include <cmath>

namespace mw {
namespace {

constexpr float kFaceShade[6] = {0.80f, 0.80f, 1.00f, 0.55f, 0.90f, 0.70f};

// +X, -X, +Y, -Y, +Z, -Z — corner order chosen so the face is CCW when seen from outside.
constexpr int kFaceNormal[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

void faceCorners(int normalIndex, float x, float y, float z, float s, float out[4][3]) {
    switch (normalIndex) {
        case 0:  // +X
            out[0][0] = x + s; out[0][1] = y;     out[0][2] = z;
            out[1][0] = x + s; out[1][1] = y + s; out[1][2] = z;
            out[2][0] = x + s; out[2][1] = y + s; out[2][2] = z + s;
            out[3][0] = x + s; out[3][1] = y;     out[3][2] = z + s;
            break;
        case 1:  // -X
            out[0][0] = x;     out[0][1] = y;     out[0][2] = z + s;
            out[1][0] = x;     out[1][1] = y + s; out[1][2] = z + s;
            out[2][0] = x;     out[2][1] = y + s; out[2][2] = z;
            out[3][0] = x;     out[3][1] = y;     out[3][2] = z;
            break;
        case 2:  // +Y
            out[0][0] = x;     out[0][1] = y + s; out[0][2] = z + s;
            out[1][0] = x + s; out[1][1] = y + s; out[1][2] = z + s;
            out[2][0] = x + s; out[2][1] = y + s; out[2][2] = z;
            out[3][0] = x;     out[3][1] = y + s; out[3][2] = z;
            break;
        case 3:  // -Y
            out[0][0] = x;     out[0][1] = y;     out[0][2] = z;
            out[1][0] = x + s; out[1][1] = y;     out[1][2] = z;
            out[2][0] = x + s; out[2][1] = y;     out[2][2] = z + s;
            out[3][0] = x;     out[3][1] = y;     out[3][2] = z + s;
            break;
        case 4:  // +Z
            out[0][0] = x;     out[0][1] = y;     out[0][2] = z + s;
            out[1][0] = x + s; out[1][1] = y;     out[1][2] = z + s;
            out[2][0] = x + s; out[2][1] = y + s; out[2][2] = z + s;
            out[3][0] = x;     out[3][1] = y + s; out[3][2] = z + s;
            break;
        default:  // -Z
            out[0][0] = x + s; out[0][1] = y;     out[0][2] = z;
            out[1][0] = x;     out[1][1] = y;     out[1][2] = z;
            out[2][0] = x;     out[2][1] = y + s; out[2][2] = z;
            out[3][0] = x;     out[3][1] = y + s; out[3][2] = z;
            break;
    }
}

}  // namespace

bool BlockTable::load(const CsvTable &csv) {
    blocks_.clear();
    if (csv.rowCount() < 3) return false;

    CsvTable table = csv;
    table.useHeader(1);  // English field names

    const int cName = table.columnIndex("Name");
    const int cType = table.columnIndex("Type");
    const int cTex = table.columnIndex("TextureGroup");

    for (std::size_t r = 2; r < table.rowCount(); ++r) {
        int id = 0;
        if (!table.idAt(r, id)) continue;
        BlockInfo info;
        const std::string *name = (cName >= 0) ? table.cell(r, static_cast<std::size_t>(cName)) : nullptr;
        const std::string *type = (cType >= 0) ? table.cell(r, static_cast<std::size_t>(cType)) : nullptr;
        const std::string *tex = (cTex >= 0) ? table.cell(r, static_cast<std::size_t>(cTex)) : nullptr;
        if (name) info.name = *name;
        if (type) info.type = *type;
        if (tex && !tex->empty()) info.textureGroup = std::atoi(tex->c_str());
        blocks_[id] = std::move(info);
    }
    return !blocks_.empty();
}

const BlockInfo *BlockTable::find(int id) const {
    const auto it = blocks_.find(id);
    return it == blocks_.end() ? nullptr : &it->second;
}

std::uint32_t BlockTable::colorFor(int id) const {
    if (id == 0) return 0;
    const BlockInfo *info = find(id);
    // Mix the type string and the texture group into a stable hue, then keep it bright.
    std::uint32_t h = 2166136261u;
    const char *seed = info && !info->type.empty() ? info->type.c_str() : "block";
    for (const char *p = seed; *p; ++p) {
        h ^= static_cast<std::uint8_t>(*p);
        h *= 16777619u;
    }
    h ^= static_cast<std::uint32_t>(id) * 2654435761u;
    if (info) h ^= static_cast<std::uint32_t>(info->textureGroup) * 40503u;

    const float r = 0.35f + 0.60f * ((h >> 0) & 0xFF) / 255.0f;
    const float g = 0.35f + 0.60f * ((h >> 8) & 0xFF) / 255.0f;
    const float b = 0.35f + 0.60f * ((h >> 16) & 0xFF) / 255.0f;

    const auto pack = [](float v) {
        return static_cast<std::uint32_t>(std::lround(v * 255.0f)) & 0xFFu;
    };
    return pack(r) | (pack(g) << 8) | (pack(b) << 16) | (0xFFu << 24);
}

bool decodeChunk(const std::vector<std::uint8_t> &raw, std::vector<std::uint8_t> &voxels) {
    if (raw.size() != kChunkBytes) {
        voxels.clear();
        return false;
    }
    voxels = raw;
    return true;
}

void buildChunkMesh(const std::vector<std::uint8_t> &voxels, const BlockTable &table,
                    MeshBuffer &out, int originX, int originY, int originZ, float scale) {
    out.clear();
    if (voxels.size() != kChunkBytes) return;

    const auto solid = [&](int x, int y, int z) -> bool {
        if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) {
            return false;
        }
        return voxels[chunkIndex(x, y, z)] != 0;
    };

    for (int y = 0; y < kChunkSize; ++y) {
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const int id = voxels[chunkIndex(x, y, z)];
                if (id == 0) continue;

                const std::uint32_t packed = table.colorFor(id);
                const float r = static_cast<float>(packed & 0xFF) / 255.0f;
                const float g = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
                const float b = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;

                for (int f = 0; f < 6; ++f) {
                    const int nx = x + kFaceNormal[f][0];
                    const int ny = y + kFaceNormal[f][1];
                    const int nz = z + kFaceNormal[f][2];
                    if (solid(nx, ny, nz)) continue;

                    float corners[4][3];
                    const float px = static_cast<float>(x + originX) * scale;
                    const float py = static_cast<float>(y + originY) * scale;
                    const float pz = static_cast<float>(z + originZ) * scale;
                    faceCorners(f, px, py, pz, scale, corners);

                    const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size());
                    const float shade = kFaceShade[f];
                    const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                    for (int c = 0; c < 4; ++c) {
                        MeshVertex v{};
                        v.pos[0] = corners[c][0];
                        v.pos[1] = corners[c][1];
                        v.pos[2] = corners[c][2];
                        v.nrm[0] = static_cast<float>(kFaceNormal[f][0]);
                        v.nrm[1] = static_cast<float>(kFaceNormal[f][1]);
                        v.nrm[2] = static_cast<float>(kFaceNormal[f][2]);
                        v.uv[0] = uvs[c][0];
                        v.uv[1] = uvs[c][1];
                        v.col[0] = r * shade;
                        v.col[1] = g * shade;
                        v.col[2] = b * shade;
                        out.vertices.push_back(v);
                    }
                    out.indices.push_back(base + 0);
                    out.indices.push_back(base + 1);
                    out.indices.push_back(base + 2);
                    out.indices.push_back(base + 0);
                    out.indices.push_back(base + 2);
                    out.indices.push_back(base + 3);
                }
            }
        }
    }
}

}  // namespace mw
