// First-person playable demo (Win32, no external dependencies).
//
//   play.exe <assets.bin>               window mode: WASD + mouse, LMB dig, RMB place, ESC quit
//   play.exe <assets.bin> --shot out.bmp [x y z yaw pitch]   render one frame headless
//
// Rendering is a software rasteriser with a z-buffer; terrain quads come from mw_core and
// are rebuilt after every block edit.

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniworld/assets_bin.h"
#include "miniworld/player.h"
#include "miniworld/terrain.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr float kFovY = 60.0f * 3.14159265f / 180.0f;

struct Assets {
    mw::PackedAssets packed;
    mw::BlockTable table;
    std::unordered_map<std::string, std::uint16_t> texIndex;
    std::vector<mw::Quad> quads;
};

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

bool loadAssets(const char *path, Assets &a) {
    std::vector<std::uint8_t> blob;
    if (!readFile(path, blob)) { printf("cannot read %s\n", path); return false; }
    if (!mw::loadPackedAssets(blob.data(), blob.size(), a.packed)) { printf("parse failed\n"); return false; }
    if (!a.table.parse(a.packed.blockdef)) { printf("block table failed\n"); return false; }
    for (std::size_t i = 0; i < a.packed.textures.size(); ++i) {
        a.texIndex[a.packed.textures[i].name] = (std::uint16_t)i;
    }
    a.quads = mw::buildTerrainQuads(a.packed.voxels.data(), a.packed.nx, a.packed.ny,
                                    a.packed.nz, a.table, a.texIndex);
    printf("loaded: %zu textures, world %dx%dx%d, %zu quads\n", a.packed.textures.size(),
           a.packed.nx, a.packed.ny, a.packed.nz, a.quads.size());
    return !a.quads.empty();
}

mw::WorldView worldOf(const Assets &a) {
    mw::WorldView w;
    w.voxels = a.packed.voxels.data();
    w.nx = a.packed.nx;
    w.ny = a.packed.ny;
    w.nz = a.packed.nz;
    w.solid = [](std::uint8_t id) { return id != 0; };
    return w;
}

struct Camera {
    float x, y, z;
    float yaw, pitch;
};

struct Screen {
    std::vector<std::uint8_t> rgba;  // kWidth*kHeight*4
    std::vector<float> depth;
};

void clearScreen(Screen &s) {
    s.rgba.assign((size_t)kWidth * kHeight * 4, 0);
    for (size_t i = 0; i < s.rgba.size(); i += 4) {
        s.rgba[i + 0] = 18; s.rgba[i + 1] = 20; s.rgba[i + 2] = 26; s.rgba[i + 3] = 255;
    }
    s.depth.assign((size_t)kWidth * kHeight, 1e30f);
}

void renderFrame(const Assets &a, const Camera &cam, Screen &screen) {
    clearScreen(screen);

    const float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
    const float cp = cosf(cam.pitch), sp = sinf(cam.pitch);
    const float focal = (kHeight * 0.5f) / tanf(kFovY * 0.5f);

    // camera basis: forward from yaw/pitch, right = fwd x up, up = right x fwd
    float fx = cy * cp, fy = sp, fz = sy * cp;
    float rx = -sy, ry = 0.0f, rz = cy;
    float ux = -cy * sp, uy = cp, uz = -sy * sp;

    auto toCam = [&](float wx, float wy, float wz, float &cx, float &cyy, float &cz) {
        const float dx = wx - cam.x, dy = wy - cam.y, dz = wz - cam.z;
        cx = dx * rx + dy * ry + dz * rz;
        cyy = dx * ux + dy * uy + dz * uz;
        cz = dx * fx + dy * fy + dz * fz;  // positive = in front
    };

    static const float shade[6] = {0.80f, 0.70f, 1.00f, 0.42f, 0.90f, 0.62f};

    for (const mw::Quad &q : a.quads) {
        float cs[4][3];
        float sx[4], sy2[4], inv[4];
        bool anyFront = false, allBehind = true;
        for (int i = 0; i < 4; ++i) {
            toCam(q.pos[i][0], q.pos[i][1], q.pos[i][2], cs[i][0], cs[i][1], cs[i][2]);
            if (cs[i][2] > 0.05f) {
                allBehind = false;
                anyFront = true;
                const float iw = 1.0f / cs[i][2];
                sx[i] = kWidth * 0.5f + cs[i][0] * focal * iw;
                sy2[i] = kHeight * 0.5f - cs[i][1] * focal * iw;
                inv[i] = iw;
            } else {
                sx[i] = 0; sy2[i] = 0; inv[i] = 0;
            }
        }
        if (!anyFront || allBehind) continue;

        const mw::PackedTexture &tex = a.packed.textures[q.texture];
        const float sh = shade[q.face];

        const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
        const float uvs[2][3][2] = {{{0, 1}, {1, 1}, {1, 0}}, {{0, 1}, {1, 0}, {0, 0}}};
        for (int t = 0; t < 2; ++t) {
            const int i0 = tris[t][0], i1 = tris[t][1], i2 = tris[t][2];
            if (inv[i0] <= 0 || inv[i1] <= 0 || inv[i2] <= 0) continue;
            const float ax = sx[i0], ay = sy2[i0];
            const float bx = sx[i1], by = sy2[i1];
            const float cx2 = sx[i2], cy2 = sy2[i2];
            int x0 = (int)floorf(fminf(ax, fminf(bx, cx2)));
            int x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx2)));
            int y0 = (int)floorf(fminf(ay, fminf(by, cy2)));
            int y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy2)));
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 >= kWidth) x1 = kWidth - 1;
            if (y1 >= kHeight) y1 = kHeight - 1;
            if (x1 < x0 || y1 < y0) continue;

            const float den = (by - cy2) * (ax - cx2) + (cx2 - bx) * (ay - cy2);
            if (fabsf(den) < 1e-9f) continue;

            for (int py = y0; py <= y1; ++py) {
                for (int px = x0; px <= x1; ++px) {
                    const float gx = px + 0.5f, gy = py + 0.5f;
                    const float w0 = ((by - cy2) * (gx - cx2) + (cx2 - bx) * (gy - cy2)) / den;
                    const float w1 = ((cy2 - ay) * (gx - cx2) + (ax - cx2) * (gy - cy2)) / den;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < -0.002f || w1 < -0.002f || w2 < -0.002f) continue;

                    const float iw = w0 * inv[i0] + w1 * inv[i1] + w2 * inv[i2];
                    if (iw <= 0) continue;
                    const float depth = 1.0f / iw;
                    float *dz = &screen.depth[(size_t)py * kWidth + px];
                    if (depth >= *dz) continue;
                    *dz = depth;

                    const float u = w0 * uvs[t][0][0] + w1 * uvs[t][1][0] + w2 * uvs[t][2][0];
                    const float v = w0 * uvs[t][0][1] + w1 * uvs[t][1][1] + w2 * uvs[t][2][1];
                    int tx = (int)(u * tex.width); if (tx < 0) tx = 0; if (tx >= (int)tex.width) tx = (int)tex.width - 1;
                    int ty = (int)((1.0f - v) * tex.height); if (ty < 0) ty = 0; if (ty >= (int)tex.height) ty = (int)tex.height - 1;
                    const std::uint8_t *s = &tex.rgba[((size_t)ty * tex.width + tx) * 4];
                    std::uint8_t *d = &screen.rgba[((size_t)py * kWidth + px) * 4];
                    d[0] = (std::uint8_t)fminf(255.f, s[0] * sh);
                    d[1] = (std::uint8_t)fminf(255.f, s[1] * sh);
                    d[2] = (std::uint8_t)fminf(255.f, s[2] * sh);
                    d[3] = 255;
                }
            }
        }
    }
}

bool saveBmp(const char *path, const Screen &s) {
    const int rowSize = (kWidth * 3 + 3) & ~3;
    const int dataSize = rowSize * kHeight;
    std::uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    const std::uint32_t fileSize = 54 + (std::uint32_t)dataSize;
    memcpy(&header[2], &fileSize, 4);
    const std::uint32_t off = 54; memcpy(&header[10], &off, 4);
    const std::uint32_t hdr = 40; memcpy(&header[14], &hdr, 4);
    const std::int32_t w = kWidth, h = kHeight;
    memcpy(&header[18], &w, 4); memcpy(&header[22], &h, 4);
    const std::uint16_t planes = 1, bpp = 24;
    memcpy(&header[26], &planes, 2); memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &dataSize, 4);

    FILE *fh = fopen(path, "wb");
    if (!fh) return false;
    fwrite(header, 1, 54, fh);
    std::vector<std::uint8_t> row((size_t)rowSize, 0);
    for (int y = kHeight - 1; y >= 0; --y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::uint8_t *s2 = &s.rgba[((size_t)y * kWidth + x) * 4];
            row[x * 3 + 0] = s2[2];
            row[x * 3 + 1] = s2[1];
            row[x * 3 + 2] = s2[0];
        }
        fwrite(row.data(), 1, row.size(), fh);
    }
    fclose(fh);
    return true;
}

Camera spawnCamera(const Assets &a) {
    Camera c{};
    const int sx = a.packed.nx / 2, sz = a.packed.nz / 2;
    int surface = a.packed.ny - 1;
    while (surface > 0) {
        const std::size_t idx = ((std::size_t)sx * a.packed.ny + surface) * a.packed.nz + sz;
        if (a.packed.voxels[idx] != 0) break;
        surface--;
    }
    c.x = sx + 0.5f;
    c.z = sz + 0.5f;
    c.y = (float)(surface + 2) + 1.62f;
    c.yaw = -1.1f;
    c.pitch = -0.25f;
    return c;
}

}  // namespace

#ifdef _WIN32
namespace {

struct WindowState {
    Assets *assets = nullptr;
    Screen *screen = nullptr;
    mw::Player player;
    bool running = true;
    bool mouseCaptured = false;
    int lastMouseX = 0, lastMouseY = 0;
    bool keys[256] = {false};
};

WindowState *gState = nullptr;

void rebuild(WindowState &st) {
    Assets &a = *st.assets;
    a.quads = mw::buildTerrainQuads(a.packed.voxels.data(), a.packed.nx, a.packed.ny,
                                    a.packed.nz, a.table, a.texIndex);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowState *st = gState;
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            if (st) st->running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { if (st) st->running = false; PostQuitMessage(0); return 0; }
            if (st && wParam < 256) st->keys[wParam] = true;
            return 0;
        case WM_KEYUP:
            if (st && wParam < 256) st->keys[wParam] = false;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            if (!st) return 0;
            mw::WorldView world = worldOf(*st->assets);
            const float cy = cosf(st->player.yaw), sy = sinf(st->player.yaw);
            const float cp = cosf(st->player.pitch), sp = sinf(st->player.pitch);
            const mw::Vec3f eye{st->player.pos.x, st->player.pos.y + 1.62f, st->player.pos.z};
            const mw::Vec3f dir{cy * cp, sp, sy * cp};
            const mw::RayHit hit = mw::raycastVoxels(world, eye, dir, 6.0f);
            if (hit.hit) {
                if (msg == WM_LBUTTONDOWN) {
                    mw::setBlock(st->assets->packed.voxels, st->assets->packed.nx, st->assets->packed.ny,
                                 st->assets->packed.nz, hit.x, hit.y, hit.z, 0);
                } else {
                    static const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    const int f = (hit.face >= 0 && hit.face < 6) ? hit.face : 2;
                    mw::setBlock(st->assets->packed.voxels, st->assets->packed.nx, st->assets->packed.ny,
                                 st->assets->packed.nz, hit.x + off[f][0], hit.y + off[f][1],
                                 hit.z + off[f][2], 1);
                }
                rebuild(*st);
            }
            return 0;
        }
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <assets.bin> [--shot out.bmp [x y z yaw pitch]]\n", argv[0]);
        return 2;
    }
    Assets assets;
    if (!loadAssets(argv[1], assets)) return 1;

    Screen screen;
    screen.rgba.resize((size_t)kWidth * kHeight * 4);
    screen.depth.resize((size_t)kWidth * kHeight);

    if (argc >= 4 && std::string(argv[2]) == "--shot") {
        Camera cam = spawnCamera(assets);
        if (argc >= 9) {
            cam.x = (float)atof(argv[4]);
            cam.y = (float)atof(argv[5]);
            cam.z = (float)atof(argv[6]);
            cam.yaw = (float)atof(argv[7]);
            cam.pitch = (float)atof(argv[8]);
        }
        renderFrame(assets, cam, screen);
        if (!saveBmp(argv[3], screen)) { printf("cannot write bmp\n"); return 1; }
        printf("wrote %s (camera %.1f %.1f %.1f yaw %.2f pitch %.2f)\n", argv[3], cam.x, cam.y, cam.z,
               cam.yaw, cam.pitch);
        return 0;
    }

#ifdef _WIN32
    const int sx = assets.packed.nx / 2, sz = assets.packed.nz / 2;
    int surface = assets.packed.ny - 1;
    while (surface > 0) {
        const std::size_t idx = ((std::size_t)sx * assets.packed.ny + surface) * assets.packed.nz + sz;
        if (assets.packed.voxels[idx] != 0) break;
        surface--;
    }

    WindowState st;
    st.assets = &assets;
    st.screen = &screen;
    st.player.pos = {sx + 0.5f, (float)(surface + 1) + 0.02f, sz + 0.5f};
    st.player.yaw = -1.1f;
    st.player.pitch = -0.25f;
    gState = &st;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "mwplay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rect = {0, 0, kWidth, kHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("mwplay", "voxel preview - WASD, LMB dig, RMB place, ESC quit",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                              wc.hInstance, nullptr);
    if (!hwnd) { printf("CreateWindow failed\n"); return 1; }
    ShowWindow(hwnd, SW_SHOW);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kWidth;
    bmi.bmiHeader.biHeight = -kHeight;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);
    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (st.running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!st.running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        // mouse look while the left button is held
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
            POINT pt;
            GetCursorPos(&pt);
            if (!st.mouseCaptured) { st.lastMouseX = pt.x; st.lastMouseY = pt.y; st.mouseCaptured = true; }
            const int dx = pt.x - st.lastMouseX, dy = pt.y - st.lastMouseY;
            st.lastMouseX = pt.x; st.lastMouseY = pt.y;
            st.player.yaw += dx * 0.004f;
            st.player.pitch -= dy * 0.004f;
            if (st.player.pitch > 1.5f) st.player.pitch = 1.5f;
            if (st.player.pitch < -1.5f) st.player.pitch = -1.5f;
        } else {
            st.mouseCaptured = false;
            if (st.keys[VK_LEFT]) st.player.yaw -= 1.8f * dt;
            if (st.keys[VK_RIGHT]) st.player.yaw += 1.8f * dt;
            if (st.keys[VK_UP]) st.player.pitch += 1.2f * dt;
            if (st.keys[VK_DOWN]) st.player.pitch -= 1.2f * dt;
        }

        float wishX = 0, wishZ = 0;
        const float cy = cosf(st.player.yaw), sy = sinf(st.player.yaw);
        if (st.keys['W']) { wishX += cy; wishZ += sy; }
        if (st.keys['S']) { wishX -= cy; wishZ -= sy; }
        if (st.keys['A']) { wishX += sy; wishZ -= cy; }
        if (st.keys['D']) { wishX -= sy; wishZ += cy; }
        const bool jump = st.keys[VK_SPACE];

        mw::WorldView world = worldOf(assets);
        mw::stepPlayer(world, st.player, wishX, wishZ, jump, dt, 5.0f);
        if (st.player.pos.y < -20.f) st.player.pos.y = (float)assets.packed.ny;

        Camera cam;
        cam.x = st.player.pos.x;
        cam.y = st.player.pos.y + 1.62f;
        cam.z = st.player.pos.z;
        cam.yaw = st.player.yaw;
        cam.pitch = st.player.pitch;
        renderFrame(assets, cam, screen);

        StretchDIBits(hdc, 0, 0, kWidth, kHeight, 0, 0, kWidth, kHeight, screen.rgba.data(), &bmi,
                      DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(hwnd, hdc);
    return 0;
#else
    printf("window mode requires Windows; use --shot to render a frame\n");
    return 1;
#endif
}
