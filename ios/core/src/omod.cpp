#include "miniworld/omod.h"

#include <cstring>

#ifdef MW_HAVE_ZLIB
#include <zlib.h>
#endif

namespace mw {
namespace {

constexpr std::uint32_t kMagic = 0x23456789u;
constexpr std::size_t kVertexHeader = 20;
constexpr std::size_t kStride32 = 32;   // pos3f + normal3f + uv2f
constexpr std::size_t kStride40 = 40;   // same + 2 unknown floats
constexpr std::size_t kStride64 = 64;   // skinned variant, field order not recovered

std::uint16_t readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t readU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

float readF32(const std::uint8_t* p) {
    const std::uint32_t raw = readU32(p);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

bool printable(const std::uint8_t* p, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7e) return false;
    }
    return true;
}

struct Node {
    std::size_t offset;
    std::size_t nameEnd;
    std::uint16_t type;
    std::string name;
};

std::vector<Node> scanNodes(const std::uint8_t* data, std::size_t size) {
    std::vector<Node> nodes;
    std::size_t i = 4;
    while (i + 3 < size) {
        const std::uint16_t type = readU16(data + i);
        const std::uint8_t len = data[i + 2];
        if (type >= 0x60 && type <= 0x90 && len >= 2 && len <= 48 && i + 3 + len <= size &&
            printable(data + i + 3, len)) {
            nodes.push_back({i, i + 3 + len, type, std::string(reinterpret_cast<const char*>(data + i + 3), len)});
            i += 3 + len;
            continue;
        }
        ++i;
    }
    return nodes;
}

void collectTextures(const std::uint8_t* payload, std::size_t len, std::vector<std::string>& out) {
    static const char* kExts[] = {".png", ".jpg", ".jpeg", ".tga", ".dds"};
    std::size_t start = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint8_t c = payload[i];
        if (c < 0x20 || c > 0x7e) {
            start = i + 1;
            continue;
        }
        if (i + 1 - start < 4) continue;
        for (const char* ext : kExts) {
            const std::size_t elen = std::strlen(ext);
            if (i + 1 >= elen && (i + 1 - start) >= elen) {
                bool match = true;
                for (std::size_t k = 0; k < elen; ++k) {
                    char ch = static_cast<char>(payload[i + 1 - elen + k]);
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                    if (ch != ext[k]) {
                        match = false;
                        break;
                    }
                }
                if (match) out.emplace_back(reinterpret_cast<const char*>(payload + start), i + 1 - start);
            }
        }
    }
}

}  // namespace

bool unwrapConfuse(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out) {
    static const std::uint8_t kKey[16] = {
        0xD6, 0x02, 0x08, 0x00, 0xF4, 0xFE, 0xFF, 0x3F,
        0x01, 0x00, 0x00, 0x00, 0xD0, 0xCA, 0x01, 0x00,
    };
    out.clear();
    if (size < 8) {
        out.assign(data, data + size);
        return true;
    }
    const std::uint32_t beMagic = (std::uint32_t(data[0]) << 24) | (std::uint32_t(data[1]) << 16) |
                                  (std::uint32_t(data[2]) << 8) | std::uint32_t(data[3]);
    const std::uint32_t field = (std::uint32_t(data[4]) << 24) | (std::uint32_t(data[5]) << 16) |
                                (std::uint32_t(data[6]) << 8) | std::uint32_t(data[7]);

    auto confuse = [&](const std::uint8_t* src, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) out.push_back(src[i] ^ kKey[i & 0x0F]);
    };

    if (beMagic == 0xFFD9FFD8u) {
        // header-only confusion: `field` bytes after the 8-byte preamble
        const std::size_t head = field;
        if (8 + head > size) return false;
        confuse(data + 8, head);
        out.insert(out.end(), data + 8 + head, data + size);
        return true;
    }
    if (beMagic == 0xFFD9FFD9u) {
        confuse(data + 4, size - 4);
        return true;
    }
    if (beMagic == 0xFFD9FFD7u) {
#ifdef MW_HAVE_ZLIB
        std::vector<std::uint8_t> tmp;
        tmp.reserve(size - 8);
        for (std::size_t i = 8; i < size; ++i) tmp.push_back(data[i] ^ kKey[(i - 8) & 0x0F]);
        out.resize(field);
        uLongf destLen = static_cast<uLongf>(out.size());
        if (uncompress(out.data(), &destLen, tmp.data(), static_cast<uLong>(tmp.size())) != Z_OK) {
            return false;
        }
        out.resize(destLen);
        return true;
#else
        return false;  // caller must inflate this variant externally
#endif
    }
    out.assign(data, data + size);
    return true;
}

bool parseOmod(const std::uint8_t* data, std::size_t size, Model& out, std::string& error) {
    out = Model{};
    error.clear();
    if (data == nullptr || size < 16) {
        error = "file too small";
        return false;
    }

    // Accept both a raw container and an UnConfuse-wrapped one (iOS asset trees wrap).
    std::vector<std::uint8_t> unwrapped;
    const std::uint8_t* bytes = data;
    std::size_t length = size;
    if (readU32(data) != kMagic) {
        if (!unwrapConfuse(data, size, unwrapped) || unwrapped.size() < 16) {
            error = "bad container magic";
            return false;
        }
        bytes = unwrapped.data();
        length = unwrapped.size();
    }
    if (readU32(bytes) != kMagic) {
        error = "bad container magic after unwrap";
        return false;
    }

    const std::vector<Node> nodes = scanNodes(bytes, length);
    if (nodes.empty()) {
        error = "no nodes found";
        return false;
    }

    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const Node& node = nodes[n];
        const std::size_t payloadEnd = (n + 1 < nodes.size()) ? nodes[n + 1].offset : length;
        if (payloadEnd < node.nameEnd) continue;
        const std::uint8_t* payload = bytes + node.nameEnd;
        const std::size_t len = payloadEnd - node.nameEnd;

        out.nodes.push_back({node.type, node.name, node.offset});

        if (node.name == "Material") {
            collectTextures(payload, len, out.texturePaths);
            continue;
        }
        if (node.name != "SubMeshData" || len < 12 + kVertexHeader + kStride32) continue;

        const std::uint32_t a = readU32(payload);
        const std::uint32_t b = readU32(payload + 4);
        const std::uint32_t indexCount = readU32(payload + 8);
        const std::size_t vbase = 12 + static_cast<std::size_t>(indexCount) * 2;
        if (vbase + kVertexHeader > len) {
            out.truncated = true;
            continue;
        }
        const std::size_t body = len - vbase - kVertexHeader;

        // Three vertex layouts are observed in the shipped assets. Pick the one whose
        // record size divides the vertex body exactly (longest match wins).
        std::size_t stride = 0;
        for (const std::size_t candidate : {kStride40, kStride64, kStride32}) {
            if (body % candidate == 0 && body / candidate > 0) {
                stride = candidate;
                break;
            }
        }
        if (stride == 0) {
            out.truncated = true;
            continue;
        }
        const std::size_t vertexCount = body / stride;
        if (vertexCount == 0) {
            out.truncated = true;
            continue;
        }

        SubMesh mesh;
        mesh.a = a;
        mesh.b = b;
        mesh.stride = static_cast<std::uint32_t>(stride);
        mesh.indices.resize(indexCount);
        for (std::uint32_t i = 0; i < indexCount; ++i) {
            mesh.indices[i] = readU16(payload + 12 + i * 2);
        }
        const std::uint8_t* vb = payload + vbase + kVertexHeader;
        mesh.vertices.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const std::uint8_t* v = vb + i * stride;
            Vertex& out_v = mesh.vertices[i];
            out_v.position = {readF32(v + 0), readF32(v + 4), readF32(v + 8)};
            out_v.normal = {readF32(v + 12), readF32(v + 16), readF32(v + 20)};
            out_v.uv = {readF32(v + 24), readF32(v + 28)};
            if (stride == kStride40) {
                out_v.unknown[0] = readF32(v + 32);
                out_v.unknown[1] = readF32(v + 36);
            }
        }
        out.submeshes.push_back(std::move(mesh));
    }

    if (out.submeshes.empty()) {
        error = "no SubMeshData parsed";
        return false;
    }
    return true;
}

}  // namespace mw
