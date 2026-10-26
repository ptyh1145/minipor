// .omod / .ent / .emo container reader — recovered format, no third-party dependencies.
//
// Container: magic 0x23456789 (little endian "89 67 45 23"), u32, u32 childCount,
//            then nodes of: u16 type, u8 nameLen, name[nameLen], <type-specific payload>.
// SubMeshData payload:
//            u32 a | u32 b | u32 indexCount
//            u16 indices[indexCount]
//            <20-byte vertex-stream header>
//            vertices[N] of 40 bytes: pos(3f) normal(3f) uv(2f) unknown(2f)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mw {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Vec2 {
    float u = 0.0f, v = 0.0f;
};

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    float unknown[2] = {0.0f, 0.0f};
};

struct SubMesh {
    uint32_t a = 0;       // material / attribute flags (observed value 4)
    uint32_t b = 0;       // submesh ordinal
    uint32_t stride = 0;  // 32, 40 or 64 — record size recovered from the payload
    std::vector<uint16_t> indices;
    std::vector<Vertex> vertices;

    std::size_t triangleCount() const { return indices.size() / 3; }
};

struct NodeRef {
    uint16_t type = 0;
    std::string name;
    std::size_t offset = 0;
};

struct Model {
    std::vector<SubMesh> submeshes;
    std::vector<NodeRef> nodes;
    std::vector<std::string> texturePaths;
    bool truncated = false;  // some submeshes did not match the known layout
};

// The shipped asset trees wrap resources in an "UnConfuse" envelope:
//   FF D9 FF D8 | u32be headerBytes | confused(headerBytes) | rest
//   FF D9 FF D9 | confused(rest)
//   FF D9 FF D7 | u32be size        | confused(zlibStream)      <- needs inflate
// Android 0.7.5 ships .omod raw; iOS 0.12.1 wraps them with FF D9 FF D8.
// Returns false only when the payload needs zlib (FF D9 FF D7) and MW_HAVE_ZLIB is off.
bool unwrapConfuse(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out);

// Parses a whole .omod file (raw or wrapped). Returns false and fills `error` when the
// container magic or the node stream is invalid. Submeshes with an unknown layout are
// skipped and reported through `Model::truncated`.
bool parseOmod(const std::uint8_t* data, std::size_t size, Model& out, std::string& error);

}  // namespace mw
