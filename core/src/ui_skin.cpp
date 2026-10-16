// The window looks (port of gui/skin.py): Bevel, Pinstripe, and Modern, each light and dark; their
// fonts; and Chalkboard's own pixel-art icons. Every palette meets WCAG 2.1 AA: 4.5:1 for text (dim
// text too), 3:1 for control edges and the keyboard focus ring.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

#include "art.h"
#include "ui.h"

namespace ui {

namespace {

struct SkinSpec {
    const char *key, *name, *about;
    bool status_top;
    std::vector<std::string> families;
    std::vector<std::pair<const char *, const char *>> light, dark;
};

// Keys every palette has:
//   desk/desk2   the desktop behind the window (desk2: the Pinstripe dither)
//   window       window background      card   group boxes      field  text boxes and lists
//   face/light/shadow/dark   button face and its 3-D edges (dark: outlines and rules)
//   edge         the outline that shows where a text box, list, or drop-down is (3:1)
//   text, dim    text and secondary text (both 4.5:1)      off   disabled text (exempt)
//   sel/seltext  selection; selsoft: a selection in a list without the keyboard focus
//   focus        the keyboard focus ring (3:1)       title*  title bars    menu  menus
//   trough/thumb/head   scroll bars and list headings
const std::vector<SkinSpec> &specs() {
    static const std::vector<SkinSpec> S = {
        {"bevel", "Bevel", "gray 3-D buttons, blue title bars", false,
         {"Tahoma", "Verdana", "Arial", "Helvetica", "Liberation Sans", "Nimbus Sans", "DejaVu Sans", "Noto Sans", "Adwaita Sans"},
         {{"desk", "#008080"}, {"face", "#C0C0C0"}, {"light", "#FFFFFF"}, {"shadow", "#808080"}, {"dark", "#000000"}, {"edge", "#000000"},
          {"text", "#000000"}, {"dim", "#3C3C3C"}, {"off", "#808080"}, {"field", "#FFFFFF"}, {"card", "#C0C0C0"}, {"sel", "#000080"},
          {"seltext", "#FFFFFF"}, {"selsoft", "#000080"}, {"focus", "#000000"}, {"title", "#000080"}, {"title_text", "#FFFFFF"},
          {"title_off", "#FFFFFF"}, {"title_off_text", "#000000"}, {"menu", "#FFFFFF"}, {"window", "#C0C0C0"}, {"trough", "#E0E0E0"},
          {"thumb", "#C0C0C0"}, {"head", "#C0C0C0"}},
         {{"desk", "#0B3B3B"}, {"face", "#3C3C3C"}, {"light", "#8C8C8C"}, {"shadow", "#1A1A1A"}, {"dark", "#000000"}, {"edge", "#9A9A9A"},
          {"text", "#F0F0F0"}, {"dim", "#C4C4C4"}, {"off", "#7A7A7A"}, {"field", "#1E1E1E"}, {"card", "#3C3C3C"}, {"sel", "#3557C8"},
          {"seltext", "#FFFFFF"}, {"selsoft", "#3557C8"}, {"focus", "#F0F0F0"}, {"title", "#22408F"}, {"title_text", "#FFFFFF"},
          {"title_off", "#2A2A2A"}, {"title_off_text", "#C4C4C4"}, {"menu", "#2A2A2A"}, {"window", "#3C3C3C"}, {"trough", "#262626"},
          {"thumb", "#4A4A4A"}, {"head", "#3C3C3C"}}},
        {"pinstripe", "Pinstripe", "black-and-white, striped title bars", true,
         {"Geneva", "Verdana", "Lucida Grande", "Helvetica", "Arial", "Liberation Sans", "Nimbus Sans", "DejaVu Sans", "Noto Sans", "Adwaita Sans"},
         {{"desk", "#A8A8A8"}, {"desk2", "#888888"}, {"face", "#FFFFFF"}, {"light", "#FFFFFF"}, {"shadow", "#A0A0A0"}, {"dark", "#000000"},
          {"edge", "#000000"}, {"text", "#000000"}, {"dim", "#5A5A5A"}, {"off", "#909090"}, {"field", "#FFFFFF"}, {"card", "#FFFFFF"},
          {"sel", "#000000"}, {"seltext", "#FFFFFF"}, {"selsoft", "#000000"}, {"focus", "#000000"}, {"title", "#FFFFFF"}, {"title_text", "#000000"},
          {"title_off", "#FFFFFF"}, {"title_off_text", "#5A5A5A"}, {"menu", "#FFFFFF"}, {"window", "#FFFFFF"}, {"trough", "#D8D8D8"},
          {"thumb", "#FFFFFF"}, {"head", "#FFFFFF"}},
         {{"desk", "#3A3A3A"}, {"desk2", "#262626"}, {"face", "#141414"}, {"light", "#141414"}, {"shadow", "#6A6A6A"}, {"dark", "#E8E8E8"},
          {"edge", "#E8E8E8"}, {"text", "#F2F2F2"}, {"dim", "#B0B0B0"}, {"off", "#6E6E6E"}, {"field", "#141414"}, {"card", "#141414"},
          {"sel", "#E8E8E8"}, {"seltext", "#000000"}, {"selsoft", "#E8E8E8"}, {"focus", "#F2F2F2"}, {"title", "#141414"}, {"title_text", "#F2F2F2"},
          {"title_off", "#141414"}, {"title_off_text", "#B0B0B0"}, {"menu", "#141414"}, {"window", "#141414"}, {"trough", "#2E2E2E"},
          {"thumb", "#141414"}, {"head", "#141414"}}},
        {"modern", "Modern", "your computer's own style, follows light or dark mode", false,
         {},
         {{"desk", "#F3F3F5"}, {"face", "#FFFFFF"}, {"light", "#FFFFFF"}, {"shadow", "#D0D0D5"}, {"dark", "#76767C"}, {"edge", "#76767C"},
          {"text", "#1C1C1E"}, {"dim", "#55555A"}, {"off", "#A0A0A6"}, {"field", "#FFFFFF"}, {"card", "#FFFFFF"}, {"sel", "#0A5FD0"},
          {"seltext", "#FFFFFF"}, {"selsoft", "#DCDCE2"}, {"focus", "#0A5FD0"}, {"title", "#FFFFFF"}, {"title_text", "#1C1C1E"},
          {"title_off", "#FFFFFF"}, {"title_off_text", "#55555A"}, {"menu", "#FFFFFF"}, {"window", "#F3F3F5"}, {"trough", "#F3F3F5"},
          {"thumb", "#8A8A90"}, {"head", "#FFFFFF"}},
         {{"desk", "#1E1E20"}, {"face", "#3A3A3E"}, {"light", "#3A3A3E"}, {"shadow", "#3E3E43"}, {"dark", "#8E8E94"}, {"edge", "#8E8E94"},
          {"text", "#F2F2F4"}, {"dim", "#B4B4BA"}, {"off", "#6E6E74"}, {"field", "#161618"}, {"card", "#2A2A2D"}, {"sel", "#2F6FD8"},
          {"seltext", "#FFFFFF"}, {"selsoft", "#4A4A50"}, {"focus", "#5B9BFF"}, {"title", "#2A2A2D"}, {"title_text", "#F2F2F4"},
          {"title_off", "#2A2A2D"}, {"title_off_text", "#B4B4BA"}, {"menu", "#2A2A2D"}, {"window", "#1E1E20"}, {"trough", "#1E1E20"},
          {"thumb", "#8E8E94"}, {"head", "#2A2A2D"}}},
    };
    return S;
}

const std::vector<std::string> MONO = {"Courier New", "Courier", "Nimbus Mono PS", "Liberation Mono", "DejaVu Sans Mono", "Menlo", "Consolas"};

// Modern uses the computer's own interface font
std::vector<std::string> os_families() {
    std::vector<std::string> out;
    auto sys = plat::system_font();
    if (!sys.first.empty()) out.push_back(sys.first);
#if defined(__APPLE__)
    out.insert(out.end(), {".AppleSystemUIFont", "SF Pro Text", "SF Pro", "Helvetica Neue", "Helvetica"});
#elif defined(_WIN32)
    out.insert(out.end(), {"Segoe UI Variable Text", "Segoe UI", "Tahoma"});
#else
    out.insert(out.end(), {"Adwaita Sans", "Cantarell", "Noto Sans", "Ubuntu", "Inter", "DejaVu Sans", "Liberation Sans"});
#endif
    return out;
}

int py_round(double v) { return (int)std::nearbyint(v); }   // Python's round(): halves go to even

const std::map<std::string, std::vector<std::string>> &icon_table() {
    static const std::map<std::string, std::vector<std::string>> T = {
        {"lessons", {"..kkkkkkkkkkkk..", ".kwwwwwwwwwwwwk.", "kdkwrwwwwwwwwwk.", ".kwrbbbbbbbbbwk.", "kdkwrwwwwwwwwwk.", ".kwrbbbbbbbwwwk.",
                     "kdkwrwwwwwwwwwk.", ".kwrbbbbbbbbbwk.", "kdkwrwwwwwwwwwk.", ".kwrbbbbbwwwwwk.", "kdkwrwwwwwwwwwk.", ".kwrbbbbbbbbwwk.",
                     "kdkwrwwwwwwwwwk.", ".kwwwwwwwwwwwwk.", "..kkkkkkkkkkrkk.", "............r..."}},
        {"assessments", {"..kkkkkkkkk.....", "..kwwwwwwwkk....", "..kwrrwwwwkwk...", "..krwwrwwwkkkk..", "..krrrrwrwwwwk..", "..krwwrrrrwwwk..",
                         "..krwwrwrwwwwk..", "..kwwwwwwwwwwk..", "..kwwwNwdddddk..", "..kNwNwwwwwwwk..", "..kwNwwwdddddk..", "..kwwwwwwwwwwk..",
                         "..kwwwNwdddddk..", "..kNwNwwwwwwwk..", "..kwNwwwdddddk..", "..kkkkkkkkkkkk.."}},
        {"standards", {"................", ".kkkk...........", ".krrk.kkkk......", ".krrk.kbbk......", ".kwwk.kbbk.kkkk.", ".krrk.kwwk.kNNk.",
                       ".krrk.kbbk.kNNk.", ".krrk.kbbk.kwwk.", ".krrk.kbbk.kNNk.", ".krrk.kbbk.kNNk.", ".kwwk.kwwk.kNNk.", ".krrk.kbbk.kNNk.",
                       ".krrk.kbbk.kwwk.", ".kkkk.kkkk.kkkk.", "tttttttttttttttt", "TTTTTTTTTTTTTTTT"}},
        {"settings", {"kkkkkkkkkkkkkkkk", "kwwwwwwwwwwwwwdk", "kwggggggggggggdk", "kwggkgggkgggkgdk", "kwgbbbggkgggkgdk", "kwgbbbggkgggkgdk",
                      "kwggkgggkggbbbdk", "kwggkgggkggbbbdk", "kwggkggbbbggkgdk", "kwggkggbbbggkgdk", "kwggkgggkgggkgdk", "kwggkgggkgggkgdk",
                      "kwggggggggggggdk", "kddddddddddddddk", "kkkkkkkkkkkkkkkk", "................"}},
        {"folder", {"................", "................", ".kkkkk..........", "kyyyyyk.........", "kyyyyyykkkkkkkk.", "kyyyyyyyyyyyyyyk",
                    "kYYYYYYYYYYYYYYk", "kyyyyyyyyyyyyyyk", "kyyyyyyyyyyyyyyk", "kyyyyyyyyyyyyyyk", "kyyyyyyyyyyyyyyk", "kyyyyyyyyyyyyyyk",
                    "kyyyyyyyyyyyyyyk", "kYYYYYYYYYYYYYYk", "kkkkkkkkkkkkkkkk", "................"}},
        {"info", {".....kkkkkk.....", "...kkBBBBBBkk...", "..kBBBBwwBBBBk..", ".kBBBBBwwBBBBBk.", ".kBBBBBBBBBBBBk.", "kBBBBBwwwBBBBBBk",
                  "kBBBBBBwwBBBBBBk", "kBBBBBBwwBBBBBBk", "kBBBBBBwwBBBBBBk", "kBBBBBBwwBBBBBBk", "kBBBBBwwwwBBBBBk", ".kBBBBBBBBBBBBk.",
                  ".kBBBBBBBBBBBBk.", "..kBBBBBBBBBBk..", "...kkBBBBBBkk...", ".....kkkkkk....."}},
        {"ask", {".....kkkkkk.....", "...kkBBBBBBkk...", "..kBBBwwwwBBBk..", ".kBBBwwBBwwBBBk.", ".kBBBBBBBwwBBBk.", "kBBBBBBBwwBBBBBk",
                 "kBBBBBBwwBBBBBBk", "kBBBBBBwwBBBBBBk", "kBBBBBBBBBBBBBBk", "kBBBBBBwwBBBBBBk", "kBBBBBBwwBBBBBBk", ".kBBBBBBBBBBBBk.",
                 ".kBBBBBBBBBBBBk.", "..kBBBBBBBBBBk..", "...kkBBBBBBkk...", ".....kkkkkk....."}},
        {"warn", {".......kk.......", "......kyyk......", "......kyyk......", ".....kyyyyk.....", ".....kykkyk.....", "....kyykkyyk....",
                  "....kyykkyyk....", "...kyyykkyyyk...", "...kyyykkyyyk...", "..kyyyykkyyyyk..", "..kyyyyyyyyyyk..", ".kyyyyykkyyyyyk.",
                  ".kyyyyykkyyyyyk.", "kyyyyyyyyyyyyyyk", "kkkkkkkkkkkkkkkk", "................"}},
        {"logo", art::LOGO},
    };
    return T;
}

}  // namespace

// ----------------------------------------------------------------------------- colors

Color hex(const std::string &h) {
    std::string s = h;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    unsigned v = 0;
    if (s.size() != 6 || std::sscanf(s.c_str(), "%6x", &v) != 1) return {0, 0, 0};
    return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
}

std::string hex(Color c) {
    char out[8];
    auto ch = [](double v) { return std::max(0, std::min(255, py_round(v * 255))); };
    std::snprintf(out, sizeof out, "#%02X%02X%02X", ch(c.r), ch(c.g), ch(c.b));
    return out;
}

Color mix(Color a, Color b, double t) {
    auto ch = [&](double x, double y) {
        double X = std::round(x * 255), Y = std::round(y * 255);
        return std::max(0, std::min(255, py_round(X + (Y - X) * t))) / 255.0;
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

double luminance(Color c) {
    auto ch = [](double v) {
        v = std::round(v * 255) / 255;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
}

double contrast(Color a, Color b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

Color fit(Color c, const std::vector<Color> &against, double ratio) {
    double avg = 0;
    for (const auto &a : against) avg += luminance(a);
    avg /= std::max<size_t>(1, against.size());
    Color toward = avg > 0.18 ? Color{0, 0, 0} : Color{1, 1, 1};
    for (int i = 0; i < 21; i++) {
        Color m = mix(c, toward, i / 20.0);
        bool ok = true;
        for (const auto &a : against)
            if (contrast(m, a) < ratio) ok = false;
        if (ok) return m;
    }
    return toward;
}

// ----------------------------------------------------------------------------- skins

Color Skin::operator[](const std::string &k) const {
    auto it = c.find(k);
    return it == c.end() ? Color{1, 0, 1} : it->second;
}

Font &Skin::font(int style) {
    switch (style & 3) {
    case 1: return *fb;
    case 2: return *fi;
    case 3: return *fbi;
    }
    return *f;
}

const std::vector<std::vector<std::string>> &skins() {
    static std::vector<std::vector<std::string>> out;
    if (out.empty())
        for (const auto &s : specs()) out.push_back({s.key, s.name, s.about});
    return out;
}

const std::vector<int> &text_sizes() {
    static const std::vector<int> T = {11, 12, 13, 14, 16, 18, 20, 24};
    return T;
}

static std::unordered_map<std::string, Color> palette(const SkinSpec &sp, bool dark, const std::string &accent) {
    std::unordered_map<std::string, Color> c;
    for (const auto &kv : sp.light) c[kv.first] = hex(kv.second);
    if (dark)
        for (const auto &kv : sp.dark) c[kv.first] = hex(kv.second);
    if (std::string(sp.key) == "modern" && !accent.empty()) {
        c["sel"] = fit(hex(accent), {Color{1, 1, 1}}, 4.6);
        c["seltext"] = {1, 1, 1};
        c["focus"] = fit(hex(accent), {c["window"], c["card"], c["field"]}, 3.2);
    }
    return c;
}

std::unique_ptr<Skin> make_skin(const std::string &kind, int S, int text_px, bool dark, const std::string &accent) {
    const SkinSpec *sp = &specs()[0];
    for (const auto &s : specs())
        if (kind == s.key) sp = &s;
    std::unique_ptr<Skin> k(new Skin());
    k->kind = sp->key;
    k->name = sp->name;
    k->dark = dark;
    k->bevel = k->kind == "bevel";
    k->modern = k->kind == "modern";
    k->pinstripe = k->kind == "pinstripe";
    k->S = std::max(1, S);
    k->status_top = sp->status_top;
    k->c = palette(*sp, dark, accent);
    k->desk2 = k->c.count("desk2") > 0;
    std::vector<std::string> fams = sp->families.empty() ? os_families() : sp->families;
    double px = text_px * k->S;
    k->f.reset(new Font(fams, px, 0));
    k->fb.reset(new Font(fams, px, 1));
    k->fi.reset(new Font(fams, px, 2));
    k->fbi.reset(new Font(fams, px, 3));
    k->fsmall.reset(new Font(fams, std::max(9.0, px - 2 * k->S), 0));
    k->ftitle.reset(new Font(fams, px, 1));
    k->fbig.reset(new Font(fams, px + 6 * k->S, 1));
    k->fhuge.reset(new Font(fams, px * 3, 1));
    k->fmono.reset(new Font(MONO, px, 0));
    k->line = k->f->line;
    return k;
}

std::vector<std::string> contrast_problems(const std::string &kind, bool dark, const std::string &accent) {
    const SkinSpec *sp = &specs()[0];
    for (const auto &s : specs())
        if (kind == s.key) sp = &s;
    auto c = palette(*sp, dark, accent);
    std::vector<std::pair<std::string, std::string>> text = {{"text", "window"}, {"text", "card"},   {"text", "field"},  {"text", "face"},
                                                             {"text", "menu"},   {"text", "head"},   {"dim", "window"},  {"dim", "card"},
                                                             {"dim", "field"},   {"seltext", "sel"}, {"title_text", "title"},
                                                             {"title_off_text", "title_off"}};
    text.push_back({hex(c["selsoft"]) == hex(c["sel"]) ? "seltext" : "text", "selsoft"});
    std::vector<std::pair<std::string, std::string>> edges = {{"edge", "window"}, {"edge", "card"}, {"focus", "window"}, {"focus", "card"},
                                                              {"focus", "field"}};
    if (kind == "modern") edges.push_back({"thumb", "trough"});
    std::vector<std::string> out;
    const std::pair<const std::vector<std::pair<std::string, std::string>> *, double> checks[] = {{&text, 4.5}, {&edges, 3.0}};
    for (const auto &pr : checks)
        for (const auto &ab : *pr.first) {
            double r = contrast(c[ab.first], c[ab.second]);
            if (r < pr.second) {
                char buf[200];
                std::snprintf(buf, sizeof buf, "%s %s on %s %s: %.2f:1, needs %g:1", ab.first.c_str(), hex(c[ab.first]).c_str(), ab.second.c_str(),
                              hex(c[ab.second]).c_str(), r, pr.second);
                out.push_back(buf);
            }
        }
    Color bar = fit(c["sel"], {c["trough"]}, 3.0);   // the progress bar's fill
    if (contrast(bar, c["trough"]) < 3.0) out.push_back("progress fill " + hex(bar) + " on trough " + hex(c["trough"]) + ": needs 3:1");
    return out;
}

// ----------------------------------------------------------------------------- pixel art

const std::vector<std::string> *icon(const std::string &name) {
    auto it = icon_table().find(name);
    return it == icon_table().end() ? nullptr : &it->second;
}

bool icon_color(char ch, Color &out) {
    static const std::map<char, const char *> INK = {{'k', "#000000"}, {'w', "#FFFFFF"}, {'g', "#C0C0C0"}, {'d', "#808080"}, {'r', "#C00000"},
                                                     {'y', "#FFE000"}, {'Y', "#C8A000"}, {'b', "#2040C0"}, {'B', "#3060E0"}, {'N', "#108040"},
                                                     {'n', "#1F5A3A"}, {'t', "#6B4423"}, {'T', "#A87040"}};
    if (ch == '.') return false;
    auto it = INK.find(ch);
    out = hex(it == INK.end() ? "#000000" : it->second);
    return true;
}

void sprite(raster::Image &img, const std::vector<std::string> &rows, const std::function<bool(char, Color &)> &colors, double x, double y,
            int zoom) {
    int cx0, cy0, cx1, cy1;
    if (!img.clipped(cx0, cy0, cx1, cy1)) return;
    int ox = (int)std::lround(x), oy = (int)std::lround(y);
    std::map<char, std::pair<bool, std::array<uint8_t, 3>>> seen;
    for (size_t r = 0; r < rows.size(); r++)
        for (size_t c = 0; c < rows[r].size(); c++) {
            char ch = rows[r][c];
            auto it = seen.find(ch);
            if (it == seen.end()) {
                Color col;
                bool on = colors(ch, col);
                std::array<uint8_t, 3> b = {(uint8_t)std::lround(col.r * 255), (uint8_t)std::lround(col.g * 255), (uint8_t)std::lround(col.b * 255)};
                it = seen.emplace(ch, std::make_pair(on, b)).first;
            }
            if (!it->second.first) continue;
            int x0 = std::max(cx0, ox + (int)c * zoom), x1 = std::min(cx1, ox + ((int)c + 1) * zoom);
            int y0 = std::max(cy0, oy + (int)r * zoom), y1 = std::min(cy1, oy + ((int)r + 1) * zoom);
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    uint8_t *p = &img.px[((size_t)yy * (size_t)img.w + (size_t)xx) * 3];
                    p[0] = it->second.second[0];
                    p[1] = it->second.second[1];
                    p[2] = it->second.second[2];
                }
        }
}

}  // namespace ui
