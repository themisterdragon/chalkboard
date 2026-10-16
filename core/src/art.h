// Chalkboard's pixel art: the block-letter banner (banner.py), the logo (logo.py), and the school
// mascots (mascots.py, art in data/mascots.json). Sprites are 16 rows of 16 pixels in four
// phosphor tones: '.' background, 'd' dim, 'n' normal, 'h' bright.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace art {

extern const char *APP;   // "Chalkboard"
std::vector<std::string> big(const std::string &text, const std::string &px = "\xE2\x96\x88");

extern const std::vector<std::string> LOGO;          // the 16x16 logo in its own colors (k w n t T)
const std::vector<std::string> &logo_sprite();       // the logo in phosphor tones
// the logo scaled up with hard edges, centered on a clear square: a PNG file (the app's icon)
std::string logo_png(int size = 1024, int margin = 96);
// the logo as an icon file for Windows (.ico) or macOS (.icns): PNGs at the usual sizes
std::string logo_ico();
std::string logo_icns();
// the logo's pixels at a size, RGB, with alpha alongside (for the window's icon)
void logo_pixels(int size, int margin, std::vector<uint8_t> &rgb, std::vector<uint8_t> &alpha);

// mascots in menu order: (key, name)
const std::vector<std::pair<std::string, std::string>> &mascots();
bool has_mascot(const std::string &key);
std::vector<std::string> sprite(const std::string &key);   // the logo for "" or an unknown key
std::string name(const std::string &key);                   // "" for none
std::vector<std::string> hop(const std::vector<std::string> &rows);
bool valid(const std::vector<std::string> &rows);

}  // namespace art

// The window app's chibi mascots (chibi.py, art in data/chibis.json): 24x24 in full color,
// wearing the school's colors.
namespace chibi {
const int SIZE = 24;
std::vector<std::string> keys();
// 24 rows of 24 color letters ('.' is clear), standing or cheering; empty when unknown
std::vector<std::string> frame(const std::string &key, bool cheer = false);
// letter -> "#rrggbb"; the jersey in the school's colors when they're set
std::vector<std::pair<char, std::string>> colors(const std::string &key, const std::string &primary = "", const std::string &secondary = "");
}  // namespace chibi
