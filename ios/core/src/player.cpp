#include "miniworld/player.h"

#include <cmath>

namespace mw {
namespace {

constexpr float kGravity = 26.0f;
constexpr float kJumpSpeed = 8.2f;
constexpr float kEpsilon = 1e-4f;

// Does the player's AABB overlap any solid voxel?
bool collides(const WorldView &w, float x, float y, float z) {
    const float minX = x - Player::kHalfWidth;
    const float maxX = x + Player::kHalfWidth;
    const float minY = y;
    const float maxY = y + Player::kHeight;
    const float minZ = z - Player::kHalfWidth;
    const float maxZ = z + Player::kHalfWidth;

    const int x0 = (int)std::floor(minX + kEpsilon);
    const int x1 = (int)std::floor(maxX - kEpsilon);
    const int y0 = (int)std::floor(minY + kEpsilon);
    const int y1 = (int)std::floor(maxY - kEpsilon);
    const int z0 = (int)std::floor(minZ + kEpsilon);
    const int z1 = (int)std::floor(maxZ - kEpsilon);

    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                if (w.isSolid(bx, by, bz)) return true;
            }
        }
    }
    return false;
}

void moveAxis(const WorldView &w, Player &p, float delta, int axis) {
    if (std::fabs(delta) < 1e-6f) return;
    float next[3] = {p.pos.x, p.pos.y, p.pos.z};
    next[axis] += delta;
    if (!collides(w, next[0], next[1], next[2])) {
        p.pos.x = next[0];
        p.pos.y = next[1];
        p.pos.z = next[2];
        return;
    }
    // blocked: snap against the voxel face
    if (axis == 1) {
        if (delta < 0) {
            p.pos.y = std::floor(p.pos.y) + kEpsilon;  // landed on top of a block
            p.onGround = true;
        } else {
            p.pos.y = p.pos.y;  // hit a ceiling; stop rising
        }
        p.vel.y = 0;
    } else {
        if (axis == 0) p.vel.x = 0;
        else p.vel.z = 0;
    }
}

}  // namespace

void stepPlayer(const WorldView &world, Player &player, float wishX, float wishZ, bool jump,
                float dt, float speed) {
    const float len = std::sqrt(wishX * wishX + wishZ * wishZ);
    if (len > 1e-5f) {
        wishX /= len;
        wishZ /= len;
    } else {
        wishX = wishZ = 0;
    }

    // horizontal velocity directly follows input (arcade feel)
    player.vel.x = wishX * speed;
    player.vel.z = wishZ * speed;
    if (jump && player.onGround) {
        player.vel.y = kJumpSpeed;
        player.onGround = false;
    }
    player.vel.y -= kGravity * dt;
    if (player.vel.y < -55.f) player.vel.y = -55.f;

    const bool wasOnGround = player.onGround;
    player.onGround = false;

    moveAxis(world, player, player.vel.y * dt, 1);
    if (!player.onGround && wasOnGround && player.vel.y <= 0) {
        // walking off a ledge: keep falling, nothing special
    }
    moveAxis(world, player, player.vel.x * dt, 0);
    moveAxis(world, player, player.vel.z * dt, 2);

    // standing check: a solid block just below the feet
    if (!player.onGround) {
        const int bx = (int)std::floor(player.pos.x);
        const int by = (int)std::floor(player.pos.y - 0.05f);
        const int bz = (int)std::floor(player.pos.z);
        if (world.isSolid(bx, by, bz)) player.onGround = true;
    }
}

RayHit raycastVoxels(const WorldView &world, Vec3f origin, Vec3f dir, float maxDistance) {
    RayHit hit;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-6f) return hit;
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;

    int x = (int)std::floor(origin.x);
    int y = (int)std::floor(origin.y);
    int z = (int)std::floor(origin.z);

    const int stepX = dir.x > 0 ? 1 : -1;
    const int stepY = dir.y > 0 ? 1 : -1;
    const int stepZ = dir.z > 0 ? 1 : -1;

    const float invX = std::fabs(dir.x) < 1e-8f ? 1e30f : 1.0f / std::fabs(dir.x);
    const float invY = std::fabs(dir.y) < 1e-8f ? 1e30f : 1.0f / std::fabs(dir.y);
    const float invZ = std::fabs(dir.z) < 1e-8f ? 1e30f : 1.0f / std::fabs(dir.z);

    auto frac = [](float v) { return v - std::floor(v); };
    float tMaxX = invX * (dir.x > 0 ? (1.0f - frac(origin.x)) : frac(origin.x));
    float tMaxY = invY * (dir.y > 0 ? (1.0f - frac(origin.y)) : frac(origin.y));
    float tMaxZ = invZ * (dir.z > 0 ? (1.0f - frac(origin.z)) : frac(origin.z));

    float t = 0;
    for (int i = 0; i < 512 && t <= maxDistance; ++i) {
        if (world.isSolid(x, y, z)) {
            hit.hit = true;
            hit.x = x;
            hit.y = y;
            hit.z = z;
            hit.distance = t;
            if (tMaxX < tMaxY && tMaxX < tMaxZ) hit.face = stepX > 0 ? 1 : 0;
            else if (tMaxY < tMaxZ) hit.face = stepY > 0 ? 3 : 2;
            else hit.face = stepZ > 0 ? 5 : 4;
            return hit;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += invX;
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += invY;
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += invZ;
        }
    }
    return hit;
}

bool setBlock(std::vector<std::uint8_t> &voxels, int nx, int ny, int nz, int x, int y, int z,
              std::uint8_t id) {
    if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
    const std::size_t index = ((std::size_t)x * ny + y) * nz + z;
    if (index >= voxels.size()) return false;
    if (voxels[index] == id) return false;
    voxels[index] = id;
    return true;
}

}  // namespace mw
