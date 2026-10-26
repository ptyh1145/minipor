// preview: dependency-free software renderer for the packed asset file.
//
// Reads <assets.bin> produced by tools/pack_assets.py, resolves each block's texture the
// same way the runtime does (Type == "grass" -> top/side/bottom art, otherwise Texture1
// with optional _top/_side/_bottom variants), meshes visible faces and rasterises an
// isometric view into a BMP.
//
//   preview <assets.bin> <out.bmp> [size] [yaw] [pitch]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Texture {
    std::string name;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

struct Vec3 {
    float x, y, z;
};

bool readFile(const char *path, std::vector<uint8_t> &out) {
    FILE *fh = fopen(path, "rb");
    if (!fh) return false;
    fseek(fh, 0, SEEK_END);
    long n = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (n <= 0) { fclose(fh); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), fh) == out.size();
    fclose(fh);
    return ok;
}

uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

struct Assets {
    std::vector<Texture> textures;
    std::unordered_map<std::string, int> texIndex;
    std::unordered_map<int, std::vector<std::string>> blocks;  // id -> fields
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint8_t> voxels;

    const Texture *tex(const std::string &name) const {
        auto it = texIndex.find(name);
        if (it == texIndex.end()) return nullptr;
        return &textures[(size_t)it->second];
    }
};

std::vector<std::string> splitCsv(const std::string &line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

bool loadAssets(const char *path, Assets &a) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) return false;
    if (data.size() < 12 || memcmp(data.data(), "MWPREV1", 7) != 0) return false;

    size_t off = 8;
    uint32_t tcount = rd32(&data[off]); off += 4;
    a.textures.resize(tcount);
    for (uint32_t i = 0; i < tcount; ++i) {
        uint16_t nlen = rd16(&data[off]); off += 2;
        std::string name((const char *)&data[off], nlen); off += nlen;
        uint32_t w = rd32(&data[off]); off += 4;
        uint32_t h = rd32(&data[off]); off += 4;
        size_t bytes = (size_t)w * h * 4;
        Texture t;
        t.name = name; t.w = w; t.h = h;
        t.rgba.assign(data.begin() + off, data.begin() + off + bytes);
        off += bytes;
        a.texIndex[name] = (int)i;
        a.textures[i] = std::move(t);
    }

    uint32_t blen = rd32(&data[off]); off += 4;
    std::string csv((const char *)&data[off], blen); off += blen;
    a.nx = (int)rd32(&data[off]); off += 4;
    a.ny = (int)rd32(&data[off]); off += 4;
    a.nz = (int)rd32(&data[off]); off += 4;
    size_t vox = (size_t)a.nx * a.ny * a.nz;
    a.voxels.assign(data.begin() + off, data.begin() + off + vox);

    // parse blockdef: row 0 and 1 are headers, row 1 carries the field names
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : csv) {
            if (c == '\n') { if (!cur.empty()) lines.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    if (lines.size() < 3) return false;
    std::vector<std::string> fields = splitCsv(lines[1]);
    std::unordered_map<std::string, int> fi;
    for (int i = 0; i < (int)fields.size(); ++i) fi[fields[i]] = i;
    a.blocks[0];
    for (size_t i = 2; i < lines.size(); ++i) {
        std::vector<std::string> f = splitCsv(lines[i]);
        if (f.empty() || f[0].empty()) continue;
        int id = atoi(f[0].c_str());
        // store fields in a fixed order: name, type, texture1
        std::vector<std::string> keep(3);
        auto get = [&](const char *k) -> std::string {
            auto it = fi.find(k);
            if (it == fi.end()) return "";
            int idx = it->second;
            return idx < (int)f.size() ? f[idx] : "";
        };
        keep[0] = get("Name");
        keep[1] = get("Type");
        keep[2] = get("Texture1");
        a.blocks[id] = keep;
    }
    return true;
}

struct Quad {
    Vec3 p[4];
    unsigned tex;
    float shade;
    float depth;
};

const Texture *pickTexture(const Assets &a, const std::vector<std::string> &row, int face) {
    // face: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z
    const std::string &type = row.size() > 1 ? row[1] : "";
    const std::string &t1 = row.size() > 2 ? row[2] : "";
    if (type == "grass") {
        if (face == 2) return a.tex("grass_top");
        if (face == 3) return a.tex("dirt");
        return a.tex("grass_side");
    }
    if (!t1.empty()) {
        const char *suffix = (face == 2) ? "_top" : (face == 3 ? "_bottom" : "_side");
        const Texture *t = a.tex(t1 + suffix);
        if (t) return t;
        t = a.tex(t1);
        if (t) return t;
    }
    if (!type.empty()) {
        const Texture *t = a.tex(type);
        if (t) return t;
    }
    return a.tex("default");
}

inline uint8_t at(const Assets &a, int x, int y, int z) {
    return a.voxels[((size_t)x * a.ny + y) * a.nz + z];
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s <assets.bin> <out.bmp> [size] [yaw] [pitch]\n", argv[0]);
        return 2;
    }
    Assets a;
    if (!loadAssets(argv[1], a)) { printf("failed to load assets\n"); return 1; }
    printf("loaded: %d textures, world %dx%dx%d\n", (int)a.textures.size(), a.nx, a.ny, a.nz);

    int size = argc > 3 ? atoi(argv[3]) : 640;
    float yaw = argc > 4 ? (float)atof(argv[4]) : 32.0f;
    float pitch = argc > 5 ? (float)atof(argv[5]) : 22.0f;

    const float light[6] = {0.80f, 0.70f, 1.00f, 0.42f, 0.90f, 0.62f};
    const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int corners[6][4][3] = {
        {{1, 0, 1}, {1, 1, 1}, {1, 1, 0}, {1, 0, 0}},
        {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}},
        {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}},
        {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}},
    };

    float cy = cosf(yaw * 3.14159265f / 180.f), sy = sinf(yaw * 3.14159265f / 180.f);
    float cp = cosf(pitch * 3.14159265f / 180.f), sp = sinf(pitch * 3.14159265f / 180.f);
    float cxw = a.nx / 2.f, czw = a.nz / 2.f;
    float span = (float)(a.nx > a.nz ? a.nx : a.nz);
    float scale = size * 0.82f / span;

    auto project = [&](float px, float py, float pz, float &sx, float &sy2, float &depth) {
        float x = (px - cxw) * scale;
        float y = (py - (float)a.ny) * scale;
        float z = (pz - czw) * scale;
        float xr = x * cy - z * sy;
        float zr = x * sy + z * cy;
        float yr = y * cp - zr * sp;
        float zr2 = y * sp + zr * cp;
        sx = size / 2.f + xr;
        sy2 = size / 2.f - yr;
        depth = zr2;
    };

    std::vector<Quad> quads;
    for (int x = 0; x < a.nx; ++x) {
        for (int y = 0; y < a.ny; ++y) {
            for (int z = 0; z < a.nz; ++z) {
                int id = at(a, x, y, z);
                if (id == 0) continue;
                auto rit = a.blocks.find(id);
                if (rit == a.blocks.end()) continue;
                for (int f = 0; f < 6; ++f) {
                    int nx = x + dirs[f][0], ny = y + dirs[f][1], nz = z + dirs[f][2];
                    if (nx >= 0 && ny >= 0 && nz >= 0 && nx < a.nx && ny < a.ny && nz < a.nz) {
                        if (at(a, nx, ny, nz) != 0) continue;
                    }
                    const Texture *t = pickTexture(a, rit->second, f);
                    if (!t) continue;
                    Quad q;
                    q.tex = (unsigned)(t - a.textures.data());
                    q.shade = light[f];
                    float sum = 0;
                    for (int c = 0; c < 4; ++c) {
                        float px = (float)(x + corners[f][c][0]);
                        float py = (float)(y + corners[f][c][1]);
                        float pz = (float)(z + corners[f][c][2]);
                        q.p[c] = {px, py, pz};
                        float sx, sy2, d;
                        project(px, py, pz, sx, sy2, d);
                        sum += d;
                    }
                    q.depth = sum / 4.f;
                    quads.push_back(q);
                }
            }
        }
    }
    printf("quads: %d\n", (int)quads.size());

    std::sort(quads.begin(), quads.end(),
              [](const Quad &a1, const Quad &b1) { return a1.depth < b1.depth; });

    std::vector<uint8_t> pix((size_t)size * size * 3);
    for (size_t i = 0; i < pix.size(); i += 3) { pix[i] = 16; pix[i + 1] = 18; pix[i + 2] = 24; }

    for (const Quad &q : quads) {
        const Texture &t = a.textures[q.tex];
        float sx[4], sy2[4];
        for (int c = 0; c < 4; ++c) {
            float d;
            project(q.p[c].x, q.p[c].y, q.p[c].z, sx[c], sy2[c], d);
        }
        const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
        const float uvs[2][3][2] = {{{0, 1}, {1, 1}, {1, 0}}, {{0, 1}, {1, 0}, {0, 0}}};
        for (int ti = 0; ti < 2; ++ti) {
            int i0 = tris[ti][0], i1 = tris[ti][1], i2 = tris[ti][2];
            float ax = sx[i0], ay = sy2[i0];
            float bx = sx[i1], by = sy2[i1];
            float cxx = sx[i2], cyy = sy2[i2];
            int x0 = (int)floorf(fminf(ax, fminf(bx, cxx)));
            int x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cxx)));
            int y0 = (int)floorf(fminf(ay, fminf(by, cyy)));
            int y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cyy)));
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 >= size) x1 = size - 1; if (y1 >= size) y1 = size - 1;
            float den = (by - cyy) * (ax - cxx) + (cxx - bx) * (ay - cyy);
            if (fabsf(den) < 1e-9f) continue;
            for (int py = y0; py <= y1; ++py) {
                for (int px = x0; px <= x1; ++px) {
                    float gxp = px + 0.5f, gyp = py + 0.5f;
                    float w0 = ((by - cyy) * (gxp - cxx) + (cxx - bx) * (gyp - cyy)) / den;
                    float w1 = ((cyy - ay) * (gxp - cxx) + (ax - cxx) * (gyp - cyy)) / den;
                    float w2 = 1.f - w0 - w1;
                    if (w0 < -0.002f || w1 < -0.002f || w2 < -0.002f) continue;
                    float u = w0 * uvs[ti][0][0] + w1 * uvs[ti][1][0] + w2 * uvs[ti][2][0];
                    float v = w0 * uvs[ti][0][1] + w1 * uvs[ti][1][1] + w2 * uvs[ti][2][1];
                    int tx = (int)(u * t.w); if (tx < 0) tx = 0; if (tx >= (int)t.w) tx = (int)t.w - 1;
                    int ty = (int)((1.f - v) * t.h); if (ty < 0) ty = 0; if (ty >= (int)t.h) ty = (int)t.h - 1;
                    const uint8_t *s = &t.rgba[((size_t)ty * t.w + tx) * 4];
                    uint8_t *d = &pix[((size_t)py * size + px) * 3];
                    d[0] = (uint8_t)fminf(255.f, s[0] * q.shade);
                    d[1] = (uint8_t)fminf(255.f, s[1] * q.shade);
                    d[2] = (uint8_t)fminf(255.f, s[2] * q.shade);
                }
            }
        }
    }

    // BMP (bottom-up, 24bpp)
    int rowSize = (size * 3 + 3) & ~3;
    int dataSize = rowSize * size;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    uint32_t fileSize = 54 + (uint32_t)dataSize;
    memcpy(&header[2], &fileSize, 4);
    uint32_t off = 54; memcpy(&header[10], &off, 4);
    uint32_t hdrSize = 40; memcpy(&header[14], &hdrSize, 4);
    memcpy(&header[18], &size, 4);
    memcpy(&header[22], &size, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(&header[26], &planes, 2);
    memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &dataSize, 4);

    FILE *out = fopen(argv[2], "wb");
    if (!out) { printf("cannot write %s\n", argv[2]); return 1; }
    fwrite(header, 1, 54, out);
    std::vector<uint8_t> row((size_t)rowSize, 0);
    for (int y = size - 1; y >= 0; --y) {
        for (int x = 0; x < size; ++x) {
            const uint8_t *s = &pix[((size_t)y * size + x) * 3];
            row[x * 3 + 0] = s[2];
            row[x * 3 + 1] = s[1];
            row[x * 3 + 2] = s[0];
        }
        fwrite(row.data(), 1, row.size(), out);
    }
    fclose(out);
    printf("wrote %s\n", argv[2]);
    return 0;
}
