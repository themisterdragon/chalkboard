// Writing and reading .zip files (the inside of .docx and .pptx), deflate-compressed.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zipfile {

class Writer {
public:
    void add(const std::string &name, const std::string &data, bool compress = true);
    std::string finish();   // the whole file

private:
    struct Entry { std::string name; uint32_t crc, csize, usize, offset; uint16_t method; };
    std::string out_;
    std::vector<Entry> entries_;
};

// [(name, contents)] from a zip file's bytes; throws zlib::Error or returns false on a damaged file
bool read(const std::string &zip, std::vector<std::pair<std::string, std::string>> &out, std::string *err = nullptr);

}  // namespace zipfile
