// Fonts for board slides: the standard PDF fonts and the fonts installed on this computer
// (port of fontlib.py). A font setting is just a family name. "" means the board's usual
// Helvetica (Arial in the slideshow); a name this computer doesn't have falls back to it too,
// so a board slide always comes out.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pdf.h"
#include "ttf.h"

namespace fontlib {

std::vector<std::string> font_dirs();
// {family: {style 0-3: (path, index)}}, scanned once per run
const std::map<std::string, std::map<int, std::pair<std::string, int>>> &system_fonts(bool refresh = false);
// families in menu order (case-insensitive A-Z)
const std::vector<std::string> &system_font_order();
std::vector<std::pair<std::string, std::string>> families();   // (name, "standard" or "system")
std::string label(const std::string &name);
bool available(const std::string &name);
std::string office_name(const std::string &name);

struct Entry {
    std::string std_name;               // a standard PDF font, or
    std::shared_ptr<ttf::Face> face;    // a font from this computer
};
// the four styles of a family, and which are faked from another style: (bold, italic)
std::pair<std::vector<Entry>, std::vector<std::pair<bool, bool>>> faces(const std::string &name);

// Styles 0-3 are the text font (R, B, I, BI), 4-7 the heading font. Fonts that aren't standard
// PDF fonts are embedded in the PDF.
class BoardFonts : public pdf::Fonts {
public:
    BoardFonts(const std::string &text = "", const std::string &head = "");
    std::vector<Entry> entries;
    std::vector<int> pdf_fonts(const std::function<int(const std::string &)> &add) const override;
};

}  // namespace fontlib
