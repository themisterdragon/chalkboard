// Text for the window app: a system font at a pixel size, drawn from cached glyph masks, with
// fallback fonts for characters the first one lacks.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "raster.h"

namespace ui {

class Font {
public:
    // families: tried in order (the first one this computer has wins); style: 0-3 (bold 1, italic 2)
    Font(const std::vector<std::string> &families, double px, int style = 0);
    double px;                      // the size: pixels per em
    double ascent, descent, line;   // pixels (descent positive)
    int style;
    std::string family;             // the family found ("" = the fallback)

    double width(const std::string &utf8);
    double advance_of(uint32_t codepoint);   // one character's width
    // draws on a baseline at y; returns the x after the text
    double draw(raster::Image &img, double x, double y, const std::string &utf8, raster::Color c);
    // the byte offset in utf8 closest to x (from the text's start)
    size_t hit(const std::string &utf8, double x);
    // each character's left edge (and the end), by byte offset: for text editing
    void edges(const std::string &utf8, std::vector<double> &x, std::vector<size_t> &at);

private:
    struct Face {
        std::shared_ptr<raster::Outlines> o;
        bool bold = false, italic = false;   // drawn thicker/slanted (the family has no such style)
    };
    std::vector<Face> faces_;   // the font, then the fallbacks
    struct Key {
        const raster::Outlines *o;
        uint32_t g;
        int sub;
        bool operator==(const Key &k) const { return o == k.o && g == k.g && sub == k.sub; }
    };
    struct KeyHash {
        size_t operator()(const Key &k) const { return std::hash<const void *>()(k.o) ^ (k.g * 2654435761u) ^ ((size_t)k.sub << 29); }
    };
    std::unordered_map<Key, raster::Mask, KeyHash> masks_;
    std::unordered_map<std::string, double> widths_;
    std::unordered_map<uint32_t, std::pair<int, uint32_t>> glyphs_;   // codepoint -> (face, glyph)
    std::pair<int, uint32_t> glyph_of(uint32_t c);
    double advance(int face, uint32_t g);
};

// the faces found for a family on this computer (cached): style -> outlines
std::shared_ptr<raster::Outlines> find_face(const std::string &family, int style, bool &fake_bold, bool &fake_italic);
bool has_family(const std::string &family);

}  // namespace ui
