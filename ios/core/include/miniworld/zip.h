// Minimal ZIP reader for the game's data pack (`miniworldLeshi.apk` / `assets/` trees).
// Supports STORED and DEFLATE entries; DEFLATE needs MW_HAVE_ZLIB (zlib is present on iOS).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mw {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
};

// Lists entries. Returns false when the archive structure is not a plain ZIP.
bool zipList(const std::uint8_t* data, std::size_t size, std::vector<ZipEntry>& out);

// Reads one entry (STORED or DEFLATE). `name` is the full archive path, e.g.
// "assets/entity/100003/body.omod". Returns false when missing or unsupported.
bool zipReadEntry(const std::uint8_t* data, std::size_t size, const std::string& name,
                  std::vector<std::uint8_t>& out);

// Convenience: locate an entry by suffix, e.g. ".omod" and "100003/body". Returns "" if none.
std::string zipFindBySuffix(const std::vector<ZipEntry>& entries, const std::string& suffix);

}  // namespace mw
