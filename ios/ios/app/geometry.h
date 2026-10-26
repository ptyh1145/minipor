// Shared geometry helpers for both render backends.
// Vertex layout is mw::MeshVertex (pos, normal, uv, colour) so the world builder and the
// model loader feed the same buffers.
#pragma once

#include <vector>

#include "miniworld/mesh.h"

namespace mw {

using RenderVertex = MeshVertex;

std::vector<RenderVertex> buildCube(float cx, float cy, float cz, float half);
std::vector<RenderVertex> buildVoxelCluster();

// Column-major 4x4 helpers shared by both backends.
struct Mat4 {
    float m[16];
};

Mat4 matIdentity();
Mat4 matMul(const Mat4 &a, const Mat4 &b);
Mat4 matPerspective(float fovyRadians, float aspect, float nearZ, float farZ);
Mat4 matTranslation(float x, float y, float z);
Mat4 matRotationX(float radians);
Mat4 matRotationY(float radians);

}  // namespace mw
