// Headless validation of player physics, voxel raycast and block editing.
// Runs on the packed asset file so it exercises the same data the app uses.
//
//   player_check <assets.bin>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniworld/assets_bin.h"
#include "miniworld/player.h"
#include "miniworld/terrain.h"

namespace {

bool readFile(const char *path, std::vector<std::uint8_t> &out) {
    FILE *fh = fopen(path, "rb");
    if (!fh) return false;
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), fh) == out.size();
    fclose(fh);
    return ok;
}

int failure = 0;

void check(bool cond, const char *what) {
    printf("%s  %s\n", cond ? "[ok]  " : "[FAIL]", what);
    if (!cond) failure++;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <assets.bin>\n", argv[0]);
        return 2;
    }
    std::vector<std::uint8_t> data;
    if (!readFile(argv[1], data)) { printf("cannot read\n"); return 1; }
    mw::PackedAssets assets;
    if (!mw::loadPackedAssets(data.data(), data.size(), assets)) { printf("parse failed\n"); return 1; }

    mw::BlockTable table;
    table.parse(assets.blockdef);

    mw::WorldView world;
    world.voxels = assets.voxels.data();
    world.nx = assets.nx;
    world.ny = assets.ny;
    world.nz = assets.nz;
    world.solid = [](std::uint8_t id) { return id != 0; };

    // find a spawn column near the middle
    int sx = assets.nx / 2, sz = assets.nz / 2;
    int surface = assets.ny - 1;
    while (surface > 0 && world.at(sx, surface, sz) == 0) surface--;

    mw::Player p;
    p.pos = {sx + 0.5f, (float)(surface + 1) + 0.01f, sz + 0.5f};
    printf("spawn at %.2f %.2f %.2f (surface %d)\n", p.pos.x, p.pos.y, p.pos.z, surface);

    // 1) settle on the ground
    for (int i = 0; i < 120; ++i) mw::stepPlayer(world, p, 0, 0, false, 1.0f / 60.0f);
    check(p.onGround, "player lands on the ground");
    check(std::fabs(p.pos.y - (surface + 1)) < 0.2f, "resting height matches the surface");

    // 2) walking forward moves the player
    const mw::Vec3f before = p.pos;
    for (int i = 0; i < 60; ++i) mw::stepPlayer(world, p, 1, 0, false, 1.0f / 60.0f);
    const float moved = std::fabs(p.pos.x - before.x) + std::fabs(p.pos.z - before.z);
    printf("walked %.2f blocks in 1s\n", moved);
    check(moved > 1.0f, "walking moves the player");

    // 3) jumping leaves the ground and comes back
    float peak = p.pos.y;
    for (int i = 0; i < 90; ++i) {
        mw::stepPlayer(world, p, 0, 0, i == 0, 1.0f / 60.0f);
        if (p.pos.y > peak) peak = p.pos.y;
    }
    printf("jump peak +%.2f\n", peak - (surface + 1));
    check(peak > (surface + 1) + 0.8f, "jump raises the player");
    check(p.onGround, "player lands again after jumping");

    // 4) raycast down hits the block under the feet
    mw::RayHit down = mw::raycastVoxels(world, {p.pos.x, p.pos.y + 1.0f, p.pos.z}, {0, -1, 0}, 6.0f);
    check(down.hit, "downward ray hits a block");
    if (down.hit) printf("hit voxel (%d,%d,%d) face %d dist %.2f\n", down.x, down.y, down.z,
                         down.face, down.distance);

    // 5) digging removes it, placing restores it
    if (down.hit) {
        const std::uint8_t original = world.at(down.x, down.y, down.z);
        check(mw::setBlock(assets.voxels, assets.nx, assets.ny, assets.nz, down.x, down.y, down.z, 0),
              "dig removes the block");
        check(world.at(down.x, down.y, down.z) == 0, "block is now air");
        check(mw::setBlock(assets.voxels, assets.nx, assets.ny, assets.nz, down.x, down.y, down.z,
                           original),
              "place restores the block");
        check(world.at(down.x, down.y, down.z) == original, "block restored");
    }

    // 6) the mesher still agrees with the world after edits
    std::unordered_map<std::string, std::uint16_t> texIndex;
    for (std::size_t i = 0; i < assets.textures.size(); ++i) texIndex[assets.textures[i].name] = (std::uint16_t)i;
    const std::vector<mw::Quad> quads = mw::buildTerrainQuads(
        assets.voxels.data(), assets.nx, assets.ny, assets.nz, table, texIndex);
    printf("quads after edits: %zu\n", quads.size());
    check(!quads.empty(), "mesher still produces geometry");

    printf(failure == 0 ? "\nALL PASS\n" : "\n%d CHECK(S) FAILED\n", failure);
    return failure == 0 ? 0 : 1;
}
