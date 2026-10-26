#import "AssetModel.h"
#import "Log.h"

#import <UIKit/UIKit.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "miniworld/csv.h"
#include "miniworld/omod.h"
#include "miniworld/world.h"
#include "miniworld/zip.h"

namespace {

// ------------------------------------------------------------------ pack source
// Either a zip (the shipped data pack) or a plain folder (handy when pushing the
// decrypted assets to Documents over a jailbroken device).
struct Pack {
    std::vector<std::uint8_t> zip;
    std::string dir;
    std::string origin;
    bool valid() const { return !zip.empty() || !dir.empty(); }
};

std::vector<std::uint8_t> readFile(const std::string &path) {
    std::vector<std::uint8_t> out;
    FILE *fh = std::fopen(path.c_str(), "rb");
    if (!fh) return out;
    std::fseek(fh, 0, SEEK_END);
    const long n = std::ftell(fh);
    std::fseek(fh, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        if (std::fread(out.data(), 1, out.size(), fh) != out.size()) out.clear();
    }
    std::fclose(fh);
    return out;
}

std::string documentsDir() {
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (dirs.count == 0) return std::string();
    return std::string([dirs[0] UTF8String]);
}

bool isDirectory(const std::string &path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

void listDir(const std::string &root, const std::string &prefix, std::vector<std::string> &out,
             int depth) {
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(root, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const auto &entry = *it;
        const std::string name = prefix + entry.path().filename().string();
        if (entry.is_directory(ec)) {
            if (depth < 8) listDir(entry.path().string(), name + "/", out, depth + 1);
        } else {
            out.push_back(name);
        }
    }
}

bool packRead(const Pack &pack, const std::string &name, std::vector<std::uint8_t> &out) {
    if (!pack.zip.empty()) return mw::zipReadEntry(pack.zip.data(), pack.zip.size(), name, out);
    out = readFile(pack.dir + "/" + name);
    return !out.empty();
}

bool packList(const Pack &pack, std::vector<std::string> &out) {
    if (!pack.zip.empty()) {
        std::vector<mw::ZipEntry> entries;
        if (!mw::zipList(pack.zip.data(), pack.zip.size(), entries)) return false;
        for (const mw::ZipEntry &e : entries) out.push_back(e.name);
        return true;
    }
    listDir(pack.dir, "", out, 0);
    return !out.empty();
}

std::string findBySuffix(const std::vector<std::string> &names, const std::string &suffix) {
    for (const std::string &n : names) {
        if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return n;
        }
    }
    return std::string();
}

std::string findContains(const std::vector<std::string> &names, const std::string &needle) {
    for (const std::string &n : names) {
        if (n.find(needle) != std::string::npos) return n;
    }
    return std::string();
}

// ------------------------------------------------------------------ pack discovery
Pack loadPack() {
    Pack pack;
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:@"miniworldLeshi" ofType:@"apk"];
    if (bundlePath) {
        pack.zip = readFile(bundlePath.UTF8String);
        if (!pack.zip.empty()) {
            pack.origin = std::string("bundle:") + bundlePath.UTF8String;
            return pack;
        }
    }
    MWLog(@"bundle pack missing at %@", bundlePath ?: @"(nil)");

    const std::string docs = documentsDir();
    if (!docs.empty()) {
        const char *candidates[] = {"miniworldLeshi.apk", "apk-075/assets", "assets"};
        for (const char *c : candidates) {
            const std::string p = docs + "/" + c;
            if (isDirectory(p)) {
                pack.dir = p;
                pack.origin = "dir:" + p;
                MWLog(@"pack candidate %s -> directory", p.c_str());
                return pack;
            }
            std::vector<std::uint8_t> data = readFile(p);
            if (!data.empty()) {
                pack.zip = std::move(data);
                pack.origin = "file:" + p;
                MWLog(@"pack candidate %s -> %zu bytes", p.c_str(), pack.zip.size());
                return pack;
            }
            MWLog(@"pack candidate %s -> missing", p.c_str());
        }
    }
    return pack;
}

bool readText(const Pack &pack, const std::string &name, std::string &out) {
    std::vector<std::uint8_t> raw;
    if (!packRead(pack, name, raw)) return false;
    std::vector<std::uint8_t> plain;
    if (!mw::unwrapConfuse(raw.data(), raw.size(), plain)) return false;
    out.assign(reinterpret_cast<const char *>(plain.data()), plain.size());
    return true;
}

bool decodePngToRGBA(NSData *png, NSData **rgba, NSUInteger *w, NSUInteger *h) {
    UIImage *image = [UIImage imageWithData:png];
    CGImageRef cg = image.CGImage;
    if (!cg) return false;
    const NSUInteger width = CGImageGetWidth(cg);
    const NSUInteger height = CGImageGetHeight(cg);
    if (width == 0 || height == 0) return false;

    std::vector<std::uint8_t> buf(width * height * 4);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(buf.data(), width, height, 8, width * 4, space,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!ctx) return false;
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), cg);
    CGContextRelease(ctx);

    *rgba = [NSData dataWithBytes:buf.data() length:buf.size()];
    *w = width;
    *h = height;
    return true;
}

}  // namespace

@interface MWModelData () {
    mw::MeshBuffer _buffer;
}
@end

@implementation MWModelData
- (const mw::MeshBuffer *)buffer { return &_buffer; }
- (void)adoptBuffer:(mw::MeshBuffer &&)buffer {
    _buffer = std::move(buffer);
    self.vertexCount = _buffer.vertices.size();
    self.indexCount = _buffer.indices.size();
}
@end

MWModelData *MWLoadScene(void) {
    Pack pack = loadPack();
    if (!pack.valid()) {
        MWLog(@"no data pack found; procedural fallback");
        return nil;
    }
    MWLog(@"pack: %s", pack.origin.c_str());

    std::vector<std::string> names;
    if (!packList(pack, names)) {
        MWLog(@"pack listing failed");
        return nil;
    }
    MWLog(@"pack entries: %zu", names.size());

    // ---- block table ------------------------------------------------------
    mw::BlockTable blocks;
    const std::string blockdef = findBySuffix(names, "csvdef/blockdef.csv");
    if (!blockdef.empty()) {
        std::string text;
        if (readText(pack, blockdef, text)) {
            mw::CsvTable csv;
            if (csv.parse(text) && blocks.load(csv)) MWLog(@"blockdef: %zu blocks", blocks.size());
            else MWLog(@"blockdef parse failed");
        } else {
            MWLog(@"blockdef decrypt failed");
        }
    } else {
        MWLog(@"blockdef.csv not found in pack");
    }

    // ---- script probe -----------------------------------------------------
    const std::string lua = findBySuffix(names, "luascript/common.lua");
    if (!lua.empty()) {
        std::string text;
        if (readText(pack, lua, text)) {
            MWLog(@"lua ok (%zu bytes): %s", text.size(), text.substr(0, text.find('\n')).c_str());
        } else {
            MWLog(@"lua decrypt failed");
        }
    }

    MWModelData *out = [[MWModelData alloc] init];

    // ---- world chunk ------------------------------------------------------
    std::string chunkName = findBySuffix(names, ".r");
    if (!chunkName.empty()) {
        std::vector<std::uint8_t> raw;
        std::vector<std::uint8_t> voxels;
        if (packRead(pack, chunkName, raw) && mw::decodeChunk(raw, voxels)) {
            mw::MeshBuffer mesh;
            mw::buildChunkMesh(voxels, blocks, mesh, -mw::kChunkSize / 2, -mw::kChunkSize / 2,
                               -mw::kChunkSize / 2, 1.0f);
            if (!mesh.empty()) {
                MWLog(@"world %s: %zu verts, %zu indices", chunkName.c_str(), mesh.vertices.size(),
                      mesh.indices.size());
                [out adoptBuffer:std::move(mesh)];
                out.isWorld = YES;
                out.label = [NSString stringWithFormat:@"world %s", chunkName.c_str()];
            }
        }
    }

    // ---- model fallback ---------------------------------------------------
    if (!out.isWorld) {
        std::string entryName = findBySuffix(names, "entity/100003/body.omod");
        if (entryName.empty()) entryName = findBySuffix(names, ".omod");
        std::vector<std::uint8_t> raw;
        mw::Model model;
        std::string error;
        if (!entryName.empty() && packRead(pack, entryName, raw) &&
            mw::parseOmod(raw.data(), raw.size(), model, error)) {
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const mw::SubMesh &sm : model.submeshes) {
                for (const mw::Vertex &v : sm.vertices) {
                    const float p[3] = {v.position.x, v.position.y, v.position.z};
                    for (int i = 0; i < 3; ++i) {
                        lo[i] = std::fmin(lo[i], p[i]);
                        hi[i] = std::fmax(hi[i], p[i]);
                    }
                }
            }
            const float extent = std::fmax(hi[0] - lo[0], std::fmax(hi[1] - lo[1], hi[2] - lo[2]));
            const float scale = (extent > 1e-4f) ? 2.0f / extent : 1.0f;
            const float cx = (lo[0] + hi[0]) * 0.5f, cy = (lo[1] + hi[1]) * 0.5f,
                        cz = (lo[2] + hi[2]) * 0.5f;

            mw::MeshBuffer mesh;
            for (const mw::SubMesh &sm : model.submeshes) {
                const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const mw::Vertex &v : sm.vertices) {
                    mw::MeshVertex mv{};
                    mv.pos[0] = (v.position.x - cx) * scale;
                    mv.pos[1] = (v.position.y - cy) * scale;
                    mv.pos[2] = (v.position.z - cz) * scale;
                    mv.nrm[0] = v.normal.x;
                    mv.nrm[1] = v.normal.y;
                    mv.nrm[2] = v.normal.z;
                    mv.uv[0] = v.uv.u;
                    mv.uv[1] = v.uv.v;
                    mv.col[0] = 0.85f;
                    mv.col[1] = 0.80f;
                    mv.col[2] = 0.72f;
                    mesh.vertices.push_back(mv);
                }
                for (std::uint16_t i : sm.indices) mesh.indices.push_back(base + i);
            }
            if (!mesh.empty()) {
                MWLog(@"model %s: %zu verts, %zu indices", entryName.c_str(), mesh.vertices.size(),
                      mesh.indices.size());
                const std::size_t n = mesh.vertices.size();
                [out adoptBuffer:std::move(mesh)];
                out.label = [NSString stringWithFormat:@"%s", entryName.c_str()];
                (void)n;
            }
        } else if (!entryName.empty()) {
            MWLog(@"model %s parse failed: %s", entryName.c_str(), error.c_str());
        }
    }

    if (out.vertexCount == 0) {
        MWLog(@"scene build produced no geometry");
        return nil;
    }

    // ---- texture ----------------------------------------------------------
    const std::string tex = findContains(names, "blocks/");
    std::string texName = tex;
    if (texName.empty() || texName.find(".png") == std::string::npos) {
        texName = findBySuffix(names, ".png");
    }
    if (!texName.empty()) {
        std::vector<std::uint8_t> png;
        if (packRead(pack, texName, png)) {
            NSData *rgba = nil;
            NSUInteger w = 0, h = 0;
            NSData *raw = [NSData dataWithBytes:png.data() length:png.size()];
            if (decodePngToRGBA(raw, &rgba, &w, &h)) {
                out.textureRGBA = rgba;
                out.texWidth = w;
                out.texHeight = h;
                MWLog(@"texture %s: %lux%lu", texName.c_str(), (unsigned long)w, (unsigned long)h);
            } else {
                MWLog(@"texture %s decode failed", texName.c_str());
            }
        }
    }

    MWLog(@"scene ready: world=%d verts=%zu indices=%zu", out.isWorld ? 1 : 0,
          (size_t)out.vertexCount, (size_t)out.indexCount);
    return out;
}
