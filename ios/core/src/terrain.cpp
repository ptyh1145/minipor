#include "miniworld/terrain.h"

#include <cstdlib>

namespace mw {
namespace {

std::vector<std::string> splitCsv(const std::string &line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (quoted) {
            if (c == '"') quoted = false;
            else cur.push_back(c);
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

const int kDirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

const int kCorners[6][4][3] = {
    {{1, 0, 1}, {1, 1, 1}, {1, 1, 0}, {1, 0, 0}},
    {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}},
    {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}},
    {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}},
};

const float kUv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

}  // namespace

bool BlockTable::parse(const std::string &csv) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : csv) {
        if (c == '\n') {
            if (!cur.empty()) lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() < 3) return false;

    const std::vector<std::string> fields = splitCsv(lines[1]);
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < (int)fields.size(); ++i) index[fields[i]] = i;

    auto field = [&](const std::vector<std::string> &row, const char *name) -> std::string {
        auto it = index.find(name);
        if (it == index.end()) return std::string();
        const int i = it->second;
        return i < (int)row.size() ? row[(size_t)i] : std::string();
    };

    blocks_.clear();
    for (size_t i = 2; i < lines.size(); ++i) {
        const std::vector<std::string> row = splitCsv(lines[i]);
        if (row.empty() || row[0].empty()) continue;
        const int id = std::atoi(row[0].c_str());
        BlockInfo info;
        info.name = field(row, "Name");
        info.type = field(row, "Type");
        info.texture = field(row, "Texture1");
        blocks_[id] = std::move(info);
    }
    return !blocks_.empty();
}

const BlockInfo *BlockTable::find(int id) const {
    auto it = blocks_.find(id);
    return it == blocks_.end() ? nullptr : &it->second;
}

std::string BlockTable::textureFor(const BlockInfo &block, int face, bool hasTexture) const {
    if (block.type == "grass") {
        if (face == 2) return "grass_top";
        if (face == 3) return "dirt";
        return "grass_side";
    }
    if (!block.texture.empty()) {
        if (hasTexture) {
            const char *suffix = (face == 2) ? "_top" : (face == 3 ? "_bottom" : "_side");
            return block.texture + suffix;
        }
        return block.texture;
    }
    return block.type;
}

std::vector<Quad> buildTerrainQuads(
    const std::uint8_t *voxels, int nx, int ny, int nz, const BlockTable &table,
    const std::unordered_map<std::string, std::uint16_t> &textureIndex) {
    std::vector<Quad> quads;
    if (!voxels || nx <= 0 || ny <= 0 || nz <= 0) return quads;

    auto at = [&](int x, int y, int z) -> std::uint8_t {
        return voxels[((size_t)x * ny + y) * nz + z];
    };
    auto slot = [&](const std::string &name) -> int {
        auto it = textureIndex.find(name);
        return it == textureIndex.end() ? -1 : (int)it->second;
    };

    for (int x = 0; x < nx; ++x) {
        for (int y = 0; y < ny; ++y) {
            for (int z = 0; z < nz; ++z) {
                const int id = at(x, y, z);
                if (id == 0) continue;
                const BlockInfo *info = table.find(id);
                if (!info) continue;
                for (int f = 0; f < 6; ++f) {
                    const int ax = x + kDirs[f][0];
                    const int ay = y + kDirs[f][1];
                    const int az = z + kDirs[f][2];
                    if (ax >= 0 && ay >= 0 && az >= 0 && ax < nx && ay < ny && az < nz &&
                        at(ax, ay, az) != 0) {
                        continue;
                    }
                    // prefer the per-face variant when the pack ships one
                    int idx = slot(table.textureFor(*info, f, true));
                    if (idx < 0) idx = slot(table.textureFor(*info, f, false));
                    if (idx < 0) idx = slot("default");
                    if (idx < 0) continue;

                    Quad q;
                    q.face = f;
                    q.texture = (std::uint16_t)idx;
                    for (int c = 0; c < 4; ++c) {
                        q.pos[c][0] = (float)(x + kCorners[f][c][0]);
                        q.pos[c][1] = (float)(y + kCorners[f][c][1]);
                        q.pos[c][2] = (float)(z + kCorners[f][c][2]);
                        q.uv[c][0] = kUv[c][0];
                        q.uv[c][1] = kUv[c][1];
                    }
                    quads.push_back(q);
                }
            }
        }
    }
    return quads;
}

}  // namespace mw
