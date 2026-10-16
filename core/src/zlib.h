// Deflate compression and decompression (RFC 1950/1951), CRC-32, and base64, with no outside
// library. Used for logos (PNG), PDF streams, and the zip files inside .docx and .pptx.
#pragma once
#include <cstdint>
#include <string>

namespace zlib {

struct Error {
    std::string message;
};

// zlib-wrapped data (what Python's zlib.compress / decompress use). decompress throws Error.
std::string compress(const std::string &data);
std::string decompress(const std::string &data, size_t limit = (size_t)1 << 31);

// raw deflate (inside zip files and PNG's ... no, PNG is zlib-wrapped; zip is raw)
std::string deflate_raw(const std::string &data);
std::string inflate_raw(const std::string &data, size_t limit = (size_t)1 << 31);

uint32_t crc32(const std::string &data, uint32_t crc = 0);
uint32_t adler32(const std::string &data, uint32_t a = 1);

std::string b64encode(const std::string &data);
// Like base64.b64decode: ignores characters outside the alphabet (non-strict); throws Error on bad padding.
std::string b64decode(const std::string &text);

}  // namespace zlib
