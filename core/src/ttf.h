// Reading TrueType / OpenType fonts (.ttf, .otf, .ttc) just enough to lay out and embed them
// (port of ttf.py). The PDF engine draws text one byte per character (Windows-1252), so a font
// only needs its name, style, glyph widths for those 224 characters, and the file to embed.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ttf {

struct FontError {
    std::string message;
};

struct Found {
    int index;
    std::string family;
    bool bold, italic;
};

std::vector<uint32_t> faces_in(const std::string &data);
std::map<int, std::string> names(const std::string &data, uint32_t off = 0);
std::vector<Found> scan(const std::string &path);   // the usable fonts in a file; throws FontError when damaged
std::map<uint32_t, uint32_t> cmap_lookup(const std::string &d, size_t base, const std::vector<uint32_t> &want);

class Face {
public:
    Face(std::shared_ptr<const std::string> data, uint32_t off = 0, const std::string &name = "");   // throws FontError
    int upm = 1000;
    std::vector<int> bbox;          // 4 numbers, 1/1000 em
    int ascent = 0, descent = 0, cap = 0, italic_angle = 0;
    bool cff = false;
    std::string ps, family;
    std::vector<int> widths;         // cp1252 32-255
    std::string file() const;        // the font as a file of its own (one out of a .ttc becomes a .ttf)
    std::shared_ptr<const std::string> data() const { return data_; }
    uint32_t offset() const { return off_; }

private:
    std::shared_ptr<const std::string> data_;
    uint32_t off_;
    std::map<std::string, std::pair<uint32_t, uint32_t>> t_;   // tag -> (offset, length)
};

std::shared_ptr<Face> load(const std::string &path, int index = 0);   // throws FontError

}  // namespace ttf
