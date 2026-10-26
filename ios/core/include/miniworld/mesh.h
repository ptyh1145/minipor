// Shared mesh representation used by every render backend and by the world builder.
#pragma once

#include <cstdint>
#include <vector>

namespace mw {

// 44 bytes: position, normal, uv, colour. Matches what both Metal and GL ES 2 upload.
struct MeshVertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float col[3];
};

struct MeshBuffer {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    void clear() {
        vertices.clear();
        indices.clear();
    }
    bool empty() const { return indices.empty(); }
};

}  // namespace mw
