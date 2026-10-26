#include "geometry.h"

#include <cmath>
#include <cstring>

namespace mw {
namespace {

void pushFace(std::vector<RenderVertex> &out,
              const float (&a)[3], const float (&b)[3], const float (&c)[3],
              const float (&d)[3], const float (&n)[3], const float (&col)[3]) {
    const float *quad[4] = {a, b, c, d};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        const float *p = quad[order[i]];
        RenderVertex v{};
        std::memcpy(v.pos, p, sizeof(v.pos));
        std::memcpy(v.nrm, n, sizeof(v.nrm));
        std::memcpy(v.col, col, sizeof(v.col));
        v.uv[0] = (p == a) ? 0.0f : 1.0f;
        v.uv[1] = (p == d) ? 0.0f : 1.0f;
        out.push_back(v);
    }
}

}  // namespace

std::vector<RenderVertex> buildCube(float cx, float cy, float cz, float half) {
    const float s = half;
    std::vector<RenderVertex> v;
    const float col[6][3] = {
        {0.95f, 0.42f, 0.32f}, {0.36f, 0.78f, 0.92f}, {0.55f, 0.88f, 0.45f},
        {0.95f, 0.82f, 0.35f}, {0.78f, 0.55f, 0.95f}, {0.92f, 0.92f, 0.95f},
    };
    pushFace(v, {cx + s, cy - s, cz - s}, {cx + s, cy + s, cz - s}, {cx + s, cy + s, cz + s},
             {cx + s, cy - s, cz + s}, {1, 0, 0}, col[0]);
    pushFace(v, {cx - s, cy - s, cz + s}, {cx - s, cy + s, cz + s}, {cx - s, cy + s, cz - s},
             {cx - s, cy - s, cz - s}, {-1, 0, 0}, col[1]);
    pushFace(v, {cx - s, cy + s, cz - s}, {cx - s, cy + s, cz + s}, {cx + s, cy + s, cz + s},
             {cx + s, cy + s, cz - s}, {0, 1, 0}, col[2]);
    pushFace(v, {cx - s, cy - s, cz + s}, {cx - s, cy - s, cz - s}, {cx + s, cy - s, cz - s},
             {cx + s, cy - s, cz + s}, {0, -1, 0}, col[3]);
    pushFace(v, {cx - s, cy - s, cz + s}, {cx + s, cy - s, cz + s}, {cx + s, cy + s, cz + s},
             {cx - s, cy + s, cz + s}, {0, 0, 1}, col[4]);
    pushFace(v, {cx + s, cy - s, cz - s}, {cx - s, cy - s, cz - s}, {cx - s, cy + s, cz - s},
             {cx + s, cy + s, cz - s}, {0, 0, -1}, col[5]);
    return v;
}

std::vector<RenderVertex> buildVoxelCluster() {
    std::vector<RenderVertex> all;
    for (int x = 0; x < 3; ++x) {
        for (int z = 0; z < 3; ++z) {
            const int h = ((x * 7 + z * 5) % 3) + 1;
            for (int y = 0; y < h; ++y) {
                std::vector<RenderVertex> cube = buildCube((x - 1) * 0.55f,
                                                           (y - 1) * 0.55f - 0.9f,
                                                           (z - 1) * 0.55f, 0.24f);
                all.insert(all.end(), cube.begin(), cube.end());
            }
        }
    }
    return all;
}

Mat4 matIdentity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 matMul(const Mat4 &a, const Mat4 &b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 matPerspective(float fovyRadians, float aspect, float nearZ, float farZ) {
    const float ys = 1.0f / std::tan(fovyRadians * 0.5f);
    const float xs = ys / aspect;
    const float zs = farZ / (nearZ - farZ);
    Mat4 r{};
    r.m[0] = xs;
    r.m[5] = ys;
    r.m[10] = zs;
    r.m[11] = -1.0f;
    r.m[14] = zs * nearZ;
    return r;
}

Mat4 matTranslation(float x, float y, float z) {
    Mat4 r = matIdentity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 matRotationX(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r = matIdentity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

Mat4 matRotationY(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r = matIdentity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

}  // namespace mw
