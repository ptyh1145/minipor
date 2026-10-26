#include "miniworld/zip.h"

#include <cstring>

#if defined(MW_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace mw {
namespace {

constexpr std::uint32_t kEocdSig = 0x06054b50u;
constexpr std::uint32_t kCentralSig = 0x02014b50u;
constexpr std::uint32_t kLocalSig = 0x04034b50u;

std::uint16_t rd16(const std::uint8_t *p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t rd32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool zipList(const std::uint8_t *data, std::size_t size, std::vector<ZipEntry> &out) {
    out.clear();
    if (size < 22) return false;

    // End-of-central-directory record: scan backwards over the (possibly long) comment.
    std::size_t eocd = 0;
    bool found = false;
    const std::size_t limit = (size > 66000) ? size - 66000 : 0;
    for (std::size_t i = size - 22;; --i) {
        if (rd32(data + i) == kEocdSig) {
            eocd = i;
            found = true;
            break;
        }
        if (i == limit) break;
    }
    if (!found) return false;

    const std::uint16_t count = rd16(data + eocd + 10);
    const std::uint32_t cdOffset = rd32(data + eocd + 16);
    if (cdOffset >= size) return false;

    std::size_t p = cdOffset;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (p + 46 > size || rd32(data + p) != kCentralSig) break;
        ZipEntry e;
        e.method = rd16(data + p + 10);
        e.compressedSize = rd32(data + p + 20);
        e.uncompressedSize = rd32(data + p + 24);
        const std::uint16_t nameLen = rd16(data + p + 28);
        const std::uint16_t extraLen = rd16(data + p + 30);
        const std::uint16_t commentLen = rd16(data + p + 32);
        e.localHeaderOffset = rd32(data + p + 42);
        if (p + 46 + nameLen > size) break;
        e.name.assign(reinterpret_cast<const char *>(data + p + 46), nameLen);
        out.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return !out.empty();
}

bool zipReadEntry(const std::uint8_t *data, std::size_t size, const std::string &name,
                  std::vector<std::uint8_t> &out) {
    out.clear();
    std::vector<ZipEntry> entries;
    if (!zipList(data, size, entries)) return false;

    const ZipEntry *hit = nullptr;
    for (const ZipEntry &e : entries) {
        if (e.name == name) {
            hit = &e;
            break;
        }
    }
    if (!hit) return false;

    const std::size_t lh = hit->localHeaderOffset;
    if (lh + 30 > size || rd32(data + lh) != kLocalSig) return false;
    const std::uint16_t nameLen = rd16(data + lh + 26);
    const std::uint16_t extraLen = rd16(data + lh + 28);
    const std::size_t dataStart = lh + 30 + nameLen + extraLen;
    if (dataStart + hit->compressedSize > size) return false;

    if (hit->method == 0) {
        out.assign(data + dataStart, data + dataStart + hit->compressedSize);
        return true;
    }
    if (hit->method == 8) {
#if defined(MW_HAVE_ZLIB)
        out.resize(hit->uncompressedSize);
        z_stream zs{};
        zs.next_in = const_cast<Bytef *>(data + dataStart);
        zs.avail_in = hit->compressedSize;
        zs.next_out = out.data();
        zs.avail_out = hit->uncompressedSize;
        if (inflateInit2(&zs, -15) != Z_OK) return false;
        const int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END) {
            out.clear();
            return false;
        }
        out.resize(zs.total_out);
        return true;
#else
        return false;
#endif
    }
    return false;
}

std::string zipFindBySuffix(const std::vector<ZipEntry> &entries, const std::string &suffix) {
    for (const ZipEntry &e : entries) {
        if (e.name.size() >= suffix.size() &&
            e.name.compare(e.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return e.name;
        }
    }
    return std::string();
}

}  // namespace mw
