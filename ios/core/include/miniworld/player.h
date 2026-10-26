// Player movement, AABB collision and voxel interaction for the self-built engine.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace mw {

struct Vec3f {
    float x = 0, y = 0, z = 0;
};

struct WorldView {
    const std::uint8_t *voxels = nullptr;
    int nx = 0, ny = 0, nz = 0;
    // returns true when the block id is solid for collision
    std::function<bool(std::uint8_t)> solid;
    // true when the coordinate is inside the volume
    bool inside(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz;
    }
    std::uint8_t at(int x, int y, int z) const {
        if (!inside(x, y, z)) return 0;
        return voxels[((std::size_t)x * ny + y) * nz + z];
    }
    bool isSolid(int x, int y, int z) const {
        if (!inside(x, y, z)) return true;  // volume border acts as a wall
        const std::uint8_t id = at(x, y, z);
        return solid ? solid(id) : (id != 0);
    }
};

struct Player {
    Vec3f pos{0, 0, 0};   // feet position
    Vec3f vel{0, 0, 0};
    float yaw = 0;        // radians, 0 = +x
    float pitch = 0;
    bool onGround = false;

    static constexpr float kHalfWidth = 0.3f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kEyeHeight = 1.62f;
};

// Integrates one step. `wish` is the desired horizontal direction (already normalised or zero),
// `jump` requests a jump, `dt` is seconds.
void stepPlayer(const WorldView &world, Player &player, float wishX, float wishZ, bool jump,
                float dt, float speed = 4.5f);

struct RayHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;       // hit voxel
    int face = -1;                  // 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z (normal points back to ray)
    float distance = 0;
};

// Amanatides & Woo voxel traversal from `origin` along `dir`.
RayHit raycastVoxels(const WorldView &world, Vec3f origin, Vec3f dir, float maxDistance = 6.0f);

// Applies a dig (id = 0) or place at the face-adjacent empty voxel. Returns true when changed.
bool setBlock(std::vector<std::uint8_t> &voxels, int nx, int ny, int nz, int x, int y, int z,
              std::uint8_t id);

}  // namespace mw
