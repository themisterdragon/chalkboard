#include "pdf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

#include "files.h"
#include "fontmetrics.h"
#include "markup.h"
#include "organizers.h"
#include "store.h"
#include "text.h"
#include "zlib.h"

namespace pdf {

using json::Value;
using store::text_of;

const RGB RED{0.72, 0.06, 0.06}, GRAY{0.45, 0.45, 0.45}, LINK{0.02, 0.39, 0.76}, BLACK{0, 0, 0};

// ----------------------------------------------------------------------------- text bytes

static std::string f2(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

static std::string f3(const RGB &c) {
    char b[80];
    std::snprintf(b, sizeof b, "%.3f %.3f %.3f", c.r, c.g, c.b);
    return b;
}

static int cp1252(char32_t c) {
    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) return (int)c;
    static const std::map<char32_t, int> HIGH = {
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
    auto it = HIGH.find(c);
    return it == HIGH.end() ? -1 : it->second;
}

std::string enc(const std::string &utf8) {
    // characters in the standards that the standard PDF fonts can't draw, spelled out
    static const std::map<char32_t, const char *> SPELL = {
        {0x2264, "<="}, {0x2265, ">="}, {0x2260, "=/="}, {0x2212, "-"}, {0x03B8, "theta"}, {0x03C0, "pi"},
        {0x207F, "^n"}, {0x2605, "*"}, {0x25CF, "\xE2\x80\xA2"}, {0x2192, "->"}, {0x2248, "~"}};
    std::string out;
    for (char32_t c : text::to32(utf8)) {
        if (c == '\t') c = ' ';
        auto sp = SPELL.find(c);
        if (sp != SPELL.end()) {
            for (char32_t d : text::to32(sp->second)) {
                int b = cp1252(d);
                out += (char)(b < 0 ? '?' : b);
            }
            continue;
        }
        int b = cp1252(c);
        out += (char)(b < 0 ? '?' : b);
    }
    return out;
}

std::string esc(const std::string &bytes) {
    std::string out;
    for (unsigned char c : bytes) {
        if (c == 40 || c == 41 || c == 92) {
            out += '\\';
            out += (char)c;
        } else if (c >= 32 && c < 127) out += (char)c;
        else {
            char b[8];
            std::snprintf(b, sizeof b, "\\%03o", c);
            out += b;
        }
    }
    return out;
}

int mixed(int base, bool bold, bool italic) {
    int face = base - base % 4, b = base % 4;
    return face + ((bold || b == B || b == BI) ? B : R) + ((italic || b == I || b == BI) ? I : 0);
}

// ----------------------------------------------------------------------------- fonts

Fonts::Fonts(const std::string &family) {
    if (family == "Helvetica") names = {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"};
    else names = {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"};
    for (const auto &n : names) {
        const short *w = fontmetrics::widths(n.c_str());
        tables.emplace_back(w, w + 224);
    }
}

double Fonts::width(const std::string &s, int style, double size) const {
    const std::vector<int> &w = tables[(size_t)style];
    long total = 0;
    bool ascii = true;   // most text: enc() would leave it as it is (tabs become spaces)
    for (unsigned char c : s) {
        if (c >= 0x80) {
            ascii = false;
            break;
        }
        if (c == '\t') c = ' ';
        if (c >= 32) total += w[c - 32];
    }
    if (!ascii) {
        total = 0;
        for (unsigned char c : enc(s))
            if (c >= 32) total += w[c - 32];
    }
    return (double)total * size / 1000;
}

std::vector<int> Fonts::pdf_fonts(const std::function<int(const std::string &)> &add) const {
    std::vector<int> ids;
    for (const auto &n : names) ids.push_back(add("<< /Type /Font /Subtype /Type1 /BaseFont /" + n + " /Encoding /WinAnsiEncoding >>"));
    return ids;
}

std::vector<std::string> Fonts::wrap(const std::string &t, int style, double size, double width, double first_width) const {
    std::vector<std::string> lines;
    std::string cur;
    double limit = first_width >= 0 ? first_width : width;
    for (std::string word : text::split_ws(t)) {
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (this->width(trial, style, size) <= limit) {
            cur = trial;
            continue;
        }
        if (!cur.empty()) {
            lines.push_back(cur);
            limit = width;
        }
        cur.clear();
        while (this->width(word, style, size) > limit && text::length(word) > 1) {
            long k = (long)text::length(word) - 1;
            while (k > 1 && this->width(text::slice(word, 0, k), style, size) > limit) k--;
            lines.push_back(text::slice(word, 0, k));
            word = text::slice(word, k);
            limit = width;
        }
        cur = word;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

namespace {
struct Ch {
    char32_t c;
    int style;
    bool under;
};
}  // namespace

std::vector<RichLine> Fonts::rich_wrap(const std::string &t, int style, double size, double width, double first_width) const {
    std::vector<std::vector<Ch>> words;
    std::vector<Ch> cur;
    for (const auto &run : markup::runs(t)) {
        int st = mixed(style, run.bold, run.italic);
        for (char32_t c : text::to32(run.text)) {
            if (text::isspace(c)) {
                if (!cur.empty()) {
                    words.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back({c, st, run.under});
        }
    }
    if (!cur.empty()) words.push_back(cur);
    auto w = [&](const std::vector<Ch> &chars, size_t n = (size_t)-1) {
        double total = 0;
        for (size_t i = 0; i < chars.size() && i < n; i++) total += this->width(text::to8(chars[i].c), chars[i].style, size);
        return total;
    };
    double space = this->width(" ", R, size);
    std::vector<std::vector<Ch>> lines;
    std::vector<Ch> line;
    double limit = first_width >= 0 ? first_width : width;
    for (auto word : words) {
        double trial = w(line) + (line.empty() ? 0 : space) + w(word);
        if (trial <= limit) {
            if (!line.empty()) line.push_back({' ', R, line.back().under && word[0].under});
            line.insert(line.end(), word.begin(), word.end());
            continue;
        }
        if (!line.empty()) {
            lines.push_back(line);
            limit = width;
        }
        line.clear();
        while (w(word) > limit && word.size() > 1) {
            size_t k = word.size() - 1;
            while (k > 1 && w(word, k) > limit) k--;
            lines.emplace_back(word.begin(), word.begin() + (long)k);
            word.erase(word.begin(), word.begin() + (long)k);
            limit = width;
        }
        line = word;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    std::vector<RichLine> out;
    for (const auto &chars : lines) {   // runs of one style back into pieces
        RichLine pieces;
        for (const Ch &c : chars) {
            if (!pieces.empty() && pieces.back().style == c.style && pieces.back().under == c.under) pieces.back().text += text::to8(c.c);
            else pieces.push_back({text::to8(c.c), c.style, c.under});
        }
        out.push_back(pieces);
    }
    return out;
}

// ----------------------------------------------------------------------------- the page

void Canvas::link(double x, double base, double w, double size, const std::string &url) {
    links.push_back({x, base - size * 0.25, x + w, base + size * 0.85, url});
}

void Canvas::image(const images::Image &img, double x, double y, double w, double h) {
    std::string name;
    for (const auto &e : images)
        if (e.second == &img) name = e.first;
    if (name.empty()) {
        name = "Im" + std::to_string(images.size() + 1);
        images.push_back({name, &img});
    }
    ops.push_back("q " + f2(w) + " 0 0 " + f2(h) + " " + f2(x) + " " + f2(y) + " cm /" + name + " Do Q");
}

void Canvas::text(double x, double y, const std::string &s, int style, double size, const RGB *color) {
    if (s.empty()) return;
    std::string col = color ? f3(*color) + " rg " : "";
    bool bold = false, italic = false;
    if (fakes && !fakes->empty()) {
        bold = (*fakes)[(size_t)style].first;
        italic = (*fakes)[(size_t)style].second;
    }
    if (bold) col += (color ? f3(*color) + " RG " : "") + f2(size * 0.035) + " w 2 Tr ";
    std::string at = italic ? "1 0 0.2 1 " + f2(x) + " " + f2(y) + " Tm" : f2(x) + " " + f2(y) + " Td";
    ops.push_back("q " + col + "BT /F" + std::to_string(style + 1) + " " + f2(size) + " Tf " + at + " (" + esc(enc(s)) + ") Tj ET Q");
}

void Canvas::line(double x1, double y1, double x2, double y2, double w, RGB color) {
    ops.push_back("q " + f2(w) + " w " + f3(color) + " RG " + f2(x1) + " " + f2(y1) + " m " + f2(x2) + " " + f2(y2) + " l S Q");
}

void Canvas::rect(double x, double y, double w, double h, double lw) {
    ops.push_back("q " + f2(lw) + " w " + f2(x) + " " + f2(y) + " " + f2(w) + " " + f2(h) + " re S Q");
}

void Canvas::fill(double x, double y, double w, double h, RGB color) {
    ops.push_back("q " + f3(color) + " rg " + f2(x) + " " + f2(y) + " " + f2(w) + " " + f2(h) + " re f Q");
}

void Canvas::clip(double x, double y, double w, double h) {
    ops.push_back("q " + f2(x) + " " + f2(y) + " " + f2(w) + " " + f2(h) + " re W n");
}

void Canvas::unclip() { ops.push_back("Q"); }

void Canvas::path(const std::vector<std::pair<double, double>> &pts, double w, RGB color) {
    std::string o = "q " + f2(w) + " w 1 j " + f3(color) + " RG " + f2(pts[0].first) + " " + f2(pts[0].second) + " m";
    for (size_t i = 1; i < pts.size(); i++) o += " " + f2(pts[i].first) + " " + f2(pts[i].second) + " l";
    ops.push_back(o + " S Q");
}

void Canvas::polygon(const std::vector<std::pair<double, double>> &pts, RGB color) {
    std::string o = "q " + f3(color) + " rg " + f2(pts[0].first) + " " + f2(pts[0].second) + " m";
    for (size_t i = 1; i < pts.size(); i++) o += " " + f2(pts[i].first) + " " + f2(pts[i].second) + " l";
    ops.push_back(o + " h f Q");
}

void Canvas::ellipse(double cx, double cy, double rx, double ry, RGB color, double lw, const RGB *fill) {
    const double k = 0.5523;
    std::string o = "q " + f2(lw) + " w " + f3(color) + " RG";
    if (fill) o += " " + f3(*fill) + " rg";
    o += " " + f2(cx + rx) + " " + f2(cy) + " m";
    double seg[4][6] = {{cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry},
                        {cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy},
                        {cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry},
                        {cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy}};
    for (auto &s : seg) o += " " + f2(s[0]) + " " + f2(s[1]) + " " + f2(s[2]) + " " + f2(s[3]) + " " + f2(s[4]) + " " + f2(s[5]) + " c";
    ops.push_back(o + (fill ? " B Q" : " S Q"));
}

static bool rgb_of(const Value &v, RGB &out) {
    if (!v.is_array() || v.size() < 3) return false;
    out = {v.at(0).as_double(), v.at(1).as_double(), v.at(2).as_double()};
    return true;
}

void draw_shapes(Canvas &c, const Fonts &f, const Value &items, double x0, double top) {
    for (const auto &s : items.arr()) {
        std::string k = s.s("k");
        if (k == "rect") {
            double y = top - s["y"].as_double() - s["h"].as_double();
            RGB fill;
            if (s.get("fill").truthy() && rgb_of(s.get("fill"), fill)) c.fill(x0 + s["x"].as_double(), y, s["w"].as_double(), s["h"].as_double(), fill);
            if (s.get("lw").truthy()) c.rect(x0 + s["x"].as_double(), y, s["w"].as_double(), s["h"].as_double(), s["lw"].as_double());
        } else if (k == "oval") {
            RGB fill;
            bool has = s.get("fill").truthy() && rgb_of(s.get("fill"), fill);
            c.ellipse(x0 + s["cx"].as_double(), top - s["cy"].as_double(), s["rx"].as_double(), s["ry"].as_double(), BLACK, s["lw"].as_double(),
                      has ? &fill : nullptr);
        } else if (k == "line") {
            std::vector<std::pair<double, double>> pts;
            for (const auto &p : s.get("pts").arr()) pts.push_back({x0 + p.at(0).as_double(), top - p.at(1).as_double()});
            if (s.get("arrow").truthy() && pts.size() >= 2) {
                double ax = pts[pts.size() - 2].first, ay = pts[pts.size() - 2].second, bx = pts.back().first, by = pts.back().second;
                double d = std::max(std::pow((bx - ax) * (bx - ax) + (by - ay) * (by - ay), 0.5), 0.01);
                double ux = (bx - ax) / d, uy = (by - ay) / d;
                double head = 4 + s["lw"].as_double() * 2.5;
                pts.back() = {bx - ux * head * 0.8, by - uy * head * 0.8};
                c.polygon({{bx, by},
                           {bx - ux * head - uy * head * 0.55, by - uy * head + ux * head * 0.55},
                           {bx - ux * head + uy * head * 0.55, by - uy * head - ux * head * 0.55}});
            }
            c.path(pts, s["lw"].as_double());
        } else if (k == "text") {
            int style = s.get("bold").truthy() ? B : R;
            double size = s["size"].as_double();
            auto lines = f.wrap(markup::plain(text_of(s.get("text"))), style, size, s["w"].as_double());
            for (size_t i = 0; i < lines.size() && i < 3; i++) {
                double x = x0 + s["x"].as_double();
                if (s.s("align") == "center") x += (s["w"].as_double() - f.width(lines[i], style, size)) / 2;
                c.text(x, top - s["y"].as_double() - size * (1 + 1.15 * (double)i), lines[i], style, size);
            }
        }
    }
}

void draw_rich(Canvas &c, const Fonts &f, double x, double base, const RichLine &line, double size, const RGB *color) {
    for (const auto &p : line) {
        double at = x;
        for (const auto &part : markup::links(p.text)) {   // web addresses: blue, underlined, and clickable
            double w = f.width(part.first, p.style, size);
            if (!part.second.empty()) {
                c.text(at, base, part.first, p.style, size, &LINK);
                c.line(at, base - size * 0.13, at + w, base - size * 0.13, std::max(0.5, size * 0.055), LINK);
                c.link(at, base, w, size, part.second);
            } else c.text(at, base, part.first, p.style, size, color);
            at += w;
        }
        double pw = f.width(p.text, p.style, size);
        if (p.under) c.line(x, base - size * 0.13, x + pw, base - size * 0.13, std::max(0.5, size * 0.055), color ? *color : BLACK);
        x += pw;
    }
}

// ----------------------------------------------------------------------------- laying out blocks

std::vector<Row> build_rows(const Value &blocks, const Fonts &f, double x0, double width, double body_h) {
    std::vector<Row> rows;
    const double lead = 1.3;
    const Fonts *F = &f;
    auto row = [](double h, std::function<void(Canvas &, double, double)> draw = nullptr, bool glue = false) {
        Row r;
        r.h = h;
        r.draw = draw;
        r.glue = glue;
        return r;
    };
    auto text_rows = [&](const std::vector<std::string> &lines, double x, int style, double size, const std::string &align, const RGB *color,
                         bool glue) {
        bool has = color != nullptr;
        RGB col = has ? *color : BLACK;
        for (const auto &ln : lines)
            rows.push_back(row(size * lead,
                               [=](Canvas &c, double top, double) {
                                   double xx = align == "center" ? x + (width - F->width(ln, style, size)) / 2 : x;
                                   c.text(xx, top - size, ln, style, size, has ? &col : nullptr);
                               },
                               glue));
    };
    auto space = [&](double h) {
        Row r;
        r.h = h;
        r.droptop = true;
        rows.push_back(r);
    };
    for (const auto &b : blocks.arr()) {
        std::string t = b.s("t");
        auto str = [&](const char *k) { return text_of(b.get(k)); };
        if (t == "title") {
            std::string al = b.has("align") ? text_of(b.get("align")) : "center";
            text_rows(f.wrap(markup::plain(str("text")), B, 17, width), x0, B, 17, al, nullptr, true);
            rows.push_back(row(2, nullptr, true));
        } else if (t == "subtitle") {
            std::string al = b.has("align") ? text_of(b.get("align")) : "center";
            text_rows(f.wrap(markup::plain(str("text")), I, 10.5, width), x0, I, 10.5, al, &GRAY, false);
            space(8);
        } else if (t == "fields") {
            std::vector<std::pair<std::string, double>> parts;
            std::vector<double> shares;
            if (b.has("shares")) for (const auto &s : b.get("shares").arr()) shares.push_back(s.as_double());
            else shares = {0.55, 0.27, 0.18};
            const Value &items = b.get("items");
            for (size_t i = 0; i < items.size() && i < shares.size(); i++) parts.push_back({text_of(items.at(i)), shares[i]});
            bool center = b.get("center").truthy();
            rows.push_back(row(24, [=](Canvas &c, double top, double) {
                double total = 0;
                for (const auto &p : parts) total += p.second;
                double x = center ? x0 + (width - width * total) / 2 : x0;
                double base = top - 15;
                for (const auto &p : parts) {
                    double seg = width * p.second;
                    double lw = F->width(p.first + ":", B, 10.5);
                    c.text(x, base, p.first + ":", B, 10.5);
                    c.line(x + lw + 4, base - 2, x + seg - 12, base - 2, 0.6);
                    x += seg;
                }
            }));
            space(6);
        } else if (t == "h1") {
            space(12);
            text_rows(f.wrap(markup::plain(str("text")), B, 12.5, width), x0, B, 12.5, "left", nullptr, true);
            bool rule = b.has("rule") ? b.get("rule").truthy() : true;
            rows.push_back(row(7, rule ? std::function<void(Canvas &, double, double)>([=](Canvas &c, double top, double) {
                                   c.line(x0, top - 2, x0 + width, top - 2, 0.7);
                               })
                                       : nullptr,
                               true));
        } else if (t == "check") {
            space(6);
            auto lines = f.wrap(markup::plain(str("text")), B, 11.5, width - 18);
            for (size_t i = 0; i < lines.size(); i++) {
                std::string ln = lines[i];
                rows.push_back(row(11.5 * lead + 2,
                                   [=](Canvas &c, double top, double) {
                                       double base = top - 11.5;
                                       if (i == 0) c.rect(x0, base - 1, 10, 10, 0.8);
                                       c.text(x0 + 18, base, ln, B, 11.5);
                                   },
                                   true));
            }
        } else if (t == "p" || t == "kv" || t == "bullet" || t == "answer") {
            int style = R;
            double size = SIZE, indent = 0;
            bool has_color = false;
            RGB color = BLACK;
            if (t == "p") {
                std::string st = b.has("style") ? text_of(b.get("style")) : "normal";
                style = st == "italic" ? I : st == "bold" ? B : st == "small" ? I : R;
                size = st == "small" ? 9.5 : SIZE;
                if (st == "small") { has_color = true; color = GRAY; }
            }
            if (t == "answer") {
                style = I;
                has_color = true;
                color = RED;
                indent = QIND;
            }
            if (t == "bullet") indent = 14 + 14 * b.get("indent").as_double(0);
            indent += b.get("pad").as_double(0);
            std::string label;
            if (t == "kv") label = str("label") + " ";
            else if (t == "answer") label = "Answer: ";
            int lstyle = t == "answer" ? BI : B;
            double lw = label.empty() ? 0 : f.width(label, lstyle, size);
            double avail = width - indent;
            std::vector<RichLine> lines;
            std::string body = str("text");
            if (b.get("text").truthy()) lines = f.rich_wrap(body, style, size, avail - 0, avail - lw);
            else lines.push_back(RichLine());
            std::string lab = text::strip(label);
            for (size_t i = 0; i < lines.size(); i++) {
                RichLine ln = lines[i];
                bool bullet = t == "bullet";
                rows.push_back(row(size * lead,
                                   [=](Canvas &c, double top, double) {
                                       double base = top - size, x = x0 + indent;
                                       if (i == 0 && bullet) c.text(x - 10, base, "\xE2\x80\xA2", R, size);
                                       if (i == 0 && !label.empty()) {
                                           c.text(x, base, lab, lstyle, size, has_color ? &color : nullptr);
                                           x += lw;
                                       }
                                       draw_rich(c, *F, x, base, ln, size, has_color ? &color : nullptr);
                                   },
                                   lines.size() <= 3 && i < lines.size() - 1));
            }
            space(t == "bullet" ? 3 : 5);
        } else if (t == "q") {
            std::string txt = str("text") + (b.get("points").truthy() ? "  " + str("points") : "");
            std::string num = str("num");
            auto lines = f.rich_wrap(txt, R, SIZE, width - QIND);
            for (size_t i = 0; i < lines.size(); i++) {
                RichLine ln = lines[i];
                rows.push_back(row(SIZE * lead,
                                   [=](Canvas &c, double top, double) {
                                       double base = top - SIZE;
                                       if (i == 0) c.text(x0, base, num, B, SIZE);
                                       draw_rich(c, *F, x0 + QIND, base, ln, SIZE);
                                   },
                                   true));
            }
            rows.push_back(row(3, nullptr, true));
        } else if (t == "choice") {
            double lx = x0 + QIND + 6, tx = x0 + QIND + 26;
            bool correct = b.get("correct").truthy();
            std::string label = str("label");
            auto lines = f.rich_wrap(str("text"), R, SIZE, x0 + width - tx);
            for (size_t i = 0; i < lines.size(); i++) {
                RichLine ln = lines[i];
                rows.push_back(row(SIZE * lead + 1,
                                   [=](Canvas &c, double top, double) {
                                       double base = top - SIZE;
                                       if (i == 0) {
                                           c.text(lx, base, label, R, SIZE, correct ? &RED : nullptr);
                                           if (correct) {
                                               double lw = F->width(label, R, SIZE);
                                               c.ellipse(lx + lw / 2 - 0.5, base + SIZE * 0.32, std::max(lw / 2 + 4, 8.0), SIZE * 0.68);
                                           }
                                       }
                                       draw_rich(c, *F, tx, base, ln, SIZE, correct ? &RED : nullptr);
                                   },
                                   true));
            }
            if (b.get("last").truthy()) rows.back().glue = false;
        } else if (t == "choice_inline") {
            std::vector<std::string> items;
            for (const auto &it : b.get("items").arr()) items.push_back(text_of(it));
            long correct = b.get("correct").is_number() ? (long)b.get("correct").as_int() : -1;
            rows.push_back(row(SIZE * lead + 4, [=](Canvas &c, double top, double) {
                double base = top - SIZE;
                for (size_t i = 0; i < items.size(); i++) {
                    double x = x0 + QIND + 6 + (double)i * 90;
                    bool hit = correct == (long)i;
                    c.text(x, base, items[i], R, SIZE, hit ? &RED : nullptr);
                    if (hit) {
                        double w = F->width(items[i], R, SIZE);
                        c.ellipse(x + w / 2, base + SIZE * 0.32, w / 2 + 7, SIZE * 0.72);
                    }
                }
            }));
        } else if (t == "lines") {
            for (long i = 0; i < b.get("n").as_int(); i++)
                rows.push_back(row(LINE_GAP,
                                   [=](Canvas &c, double top, double) {
                                       c.line(x0 + QIND, top - LINE_GAP + 3, x0 + width, top - LINE_GAP + 3, 0.5, RGB{0.3, 0.3, 0.3});
                                   },
                                   i == 0));
        } else if (t == "blank") {
            for (long i = 0; i < b.get("n").as_int(); i++) rows.push_back(row(LINE_GAP, nullptr, i == 0));
        } else if (t == "box") {
            double h = std::min((double)b.get("n").as_int() * LINE_GAP, body_h - 60);
            rows.push_back(row(h, [=](Canvas &c, double top, double) { c.rect(x0 + QIND, top - h + 2, width - QIND, h - 6); }));
        } else if (t == "match") {
            double lw = (width - QIND) * 0.42, rx = x0 + QIND + lw + 18, rw = x0 + width - rx;
            std::vector<std::string> L, Rt, K;
            for (const auto &x : b.get("left").arr()) L.push_back(text_of(x));
            for (const auto &x : b.get("right").arr()) Rt.push_back(text_of(x));
            bool has_key = b.get("key").truthy();
            for (const auto &x : b.get("key").arr()) K.push_back(text_of(x));
            size_t n = std::max(L.size(), Rt.size());
            for (size_t i = 0; i < n; i++) {
                std::string left = i < L.size() ? L[i] : "", right = i < Rt.size() ? Rt[i] : "";
                std::vector<RichLine> ll, rl;
                if (!left.empty()) ll = f.rich_wrap(left, R, SIZE, lw - 40);
                if (!right.empty()) rl = f.rich_wrap(right, R, SIZE, rw - 18);
                double h = (double)std::max(std::max(ll.size(), rl.size()), (size_t)1) * SIZE * lead + 5;
                std::string key = has_key && i < K.size() ? K[i] : "";
                rows.push_back(row(h,
                                   [=](Canvas &c, double top, double) {
                                       double base = top - SIZE;
                                       if (!left.empty()) {
                                           c.line(x0 + QIND, base - 2, x0 + QIND + 30, base - 2, 0.6);
                                           if (has_key) c.text(x0 + QIND + 11, base, key, B, SIZE, &RED);
                                       }
                                       for (size_t j = 0; j < ll.size(); j++) draw_rich(c, *F, x0 + QIND + 38, base - (double)j * SIZE * lead, ll[j], SIZE);
                                       if (!right.empty()) c.text(rx, base, (i < 10 ? std::string(1, "ABCDEFGHIJ"[i]) : std::string("?")) + ".", B, SIZE);
                                       for (size_t j = 0; j < rl.size(); j++) draw_rich(c, *F, rx + 18, base - (double)j * SIZE * lead, rl[j], SIZE);
                                   },
                                   i < n - 1));
            }
        } else if (t == "organizer") {
            double ow = width - QIND;
            double h = std::min((double)b.get("n").as_int() * LINE_GAP, body_h - 60);
            Value bb = b;
            Row r = row(h, [=](Canvas &c, double top, double hh) { draw_shapes(c, *F, organizers::shapes(bb, ow, hh - 8), x0 + QIND, top - 4); });
            r.fill = b.get("fill").truthy();
            rows.push_back(r);
        } else if (t == "passage") {
            space(10);
            if (b.get("title").truthy()) {
                text_rows(f.wrap(markup::plain(str("title")), B, 12, width), x0, B, 12, "center", nullptr, true);
                rows.push_back(row(4, nullptr, true));
            }
            double gutter = (b.has("numbered") ? b.get("numbered").truthy() : true) ? 30 : 0;
            int count = 0;
            std::string body = b.get("text").truthy() ? str("text") : "";
            for (const auto &p : text::split(body, "\n")) {
                std::string para = text::strip(p);
                if (para.empty()) continue;
                auto lines = f.rich_wrap(para, R, SIZE, width - gutter - 10, width - gutter - 28);
                for (size_t i = 0; i < lines.size(); i++) {
                    count++;
                    RichLine ln = lines[i];
                    int n = count;
                    rows.push_back(row(SIZE * 1.42, [=](Canvas &c, double top, double) {
                        double base = top - SIZE;
                        if (gutter && n % 5 == 0) c.text(x0 + 4, base, std::to_string(n), R, 8, &GRAY);
                        draw_rich(c, *F, x0 + gutter + (i == 0 ? 18 : 0), base, ln, SIZE);
                    }));
                }
            }
            space(8);
        } else if (t == "table") {
            // a filled-in table: a shaded heading row, then a row per line (each its own row, so a
            // long table goes on to the next page), cells cut to fit; "blank": empty rows to write in
            std::vector<std::pair<std::string, double>> cols;
            for (const auto &c : b.get("cols").arr()) cols.push_back({text_of(c.at(0)), c.at(1).as_double()});
            auto fit = [F](std::string s, int style, double size, double w) {
                if (F->width(s, style, size) <= w) return s;
                while (!s.empty() && F->width(s + "...", style, size) > w) {
                    s.pop_back();
                    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();   // a whole character
                    if (!s.empty() && (unsigned char)s.back() >= 0xC0) s.pop_back();
                }
                return s + "...";
            };
            auto line_row = [&](std::vector<std::string> cells, bool head, bool bold, double h, bool glue) {
                rows.push_back(row(h,
                                   [=](Canvas &c, double top, double) {
                                       if (head) c.fill(x0, top - h, width, h, RGB{0.86, 0.86, 0.86});
                                       double x = x0;
                                       for (size_t i = 0; i < cols.size(); i++) {
                                           double cw = width * cols[i].second;
                                           std::string v = i < cells.size() ? cells[i] : "";
                                           int st = head || bold ? B : R;
                                           v = fit(v, st, 10, cw - 8);
                                           double tx = i == 0 ? x + 4 : x + (cw - F->width(v, st, 10)) / 2;
                                           c.text(tx, top - h + (h - 10) / 2 + 2.5, v, st, 10);
                                           if (i) c.line(x, top, x, top - h, 0.6);
                                           x += cw;
                                       }
                                       c.line(x0, top, x0 + width, top, head ? 1.1 : 0.6);
                                       c.line(x0, top - h, x0 + width, top - h, 1.1);
                                       c.line(x0, top, x0, top - h, 1.1);
                                       c.line(x0 + width, top, x0 + width, top - h, 1.1);
                                   },
                                   glue));
            };
            std::vector<std::string> heads;
            for (const auto &c : cols) heads.push_back(c.first);
            line_row(heads, true, false, 20, true);
            for (const auto &r : b.get("rows").arr()) {
                std::vector<std::string> cells;
                for (const auto &c : r.arr()) cells.push_back(markup::plain(text_of(c)));
                line_row(cells, false, false, 20, false);
            }
            for (long i = 0; i < b.get("blank").as_int(); i++) line_row({}, false, false, 22, false);
            if (b.get("total").is_array()) {
                std::vector<std::string> cells;
                for (const auto &c : b.get("total").arr()) cells.push_back(text_of(c));
                line_row(cells, false, true, 20, false);
            }
            space(10);
        } else if (t == "grid") {
            std::vector<std::pair<std::string, double>> cols;
            for (const auto &c : b.get("cols").arr()) cols.push_back({text_of(c.at(0)), c.at(1).as_double()});
            long n = b.get("rows").as_int();
            double head = 22;
            Row r = row(head + (double)n * 28, [=](Canvas &c, double top, double h) {
                double rh = (h - head - 4) / (double)n;
                double bottom_y = top - head - (double)n * rh;
                c.fill(x0, top - head, width, head, RGB{0.86, 0.86, 0.86});
                double x = x0;
                for (size_t i = 0; i < cols.size(); i++) {
                    double cw = width * cols[i].second;
                    c.text(x + (cw - F->width(cols[i].first, B, 11)) / 2, top - head + 7, cols[i].first, B, 11);
                    if (i) c.line(x, top, x, bottom_y, 0.8);
                    x += cw;
                }
                for (long rr = 0; rr <= n; rr++) {
                    double y = top - head - (double)rr * rh;
                    c.line(x0, y, x0 + width, y, rr == 0 ? 0.8 : 0.6);
                }
                c.rect(x0, bottom_y, width, top - bottom_y, 1.1);
            });
            r.fill = true;
            rows.push_back(r);
        } else if (t == "days") {
            std::vector<std::pair<std::string, std::string>> days;
            for (const auto &d : b.get("days").arr()) days.push_back({text_of(d.at(0)), text_of(d.at(1))});
            double gap = 10;
            Row r = row((double)days.size() * 70, [=](Canvas &c, double top, double h) {
                double bh = (h - gap * (double)(days.size() - 1)) / (double)days.size();
                double date_w = F->width("Date:", B, 10);
                for (size_t i = 0; i < days.size(); i++) {
                    double bt = top - (double)i * (bh + gap);
                    c.rect(x0, bt - bh, width, bh, 0.9);
                    c.fill(x0, bt - 20, width, 20, RGB{0.90, 0.90, 0.90});
                    c.line(x0, bt - 20, x0 + width, bt - 20, 0.6);
                    c.text(x0 + 8, bt - 14.5, days[i].first, B, 12);
                    c.text(x0 + width - 150, bt - 14, "Date:", B, 10);
                    c.line(x0 + width - 146 + date_w, bt - 16, x0 + width - 10, bt - 16, 0.6);
                    double y = bt - 20;
                    if (!text::strip(days[i].second).empty()) {
                        auto lines = F->wrap(markup::plain(days[i].second), I, 10.5, width - 20);
                        for (size_t k = 0; k < lines.size() && k < 4; k++) {
                            y -= 10.5 * lead;
                            c.text(x0 + 10, y + 2, lines[k], I, 10.5);
                        }
                    }
                    y -= LINE_GAP - 3;
                    while (y > bt - bh + 8) {
                        c.line(x0 + 10, y, x0 + width - 10, y, 0.5, RGB{0.45, 0.45, 0.45});
                        y -= LINE_GAP - 3;
                    }
                }
            });
            r.fill = true;
            rows.push_back(r);
        } else if (t == "space") {
            space(b.has("h") ? b.get("h").as_double() : 8);
        } else if (t == "rule") {
            rows.push_back(row(8, [=](Canvas &c, double top, double) { c.line(x0, top - 4, x0 + width, top - 4, 0.5); }));
        } else if (t == "pagebreak") {
            Row r;
            r.brk = true;
            rows.push_back(r);
        }
    }
    return rows;
}

// ----------------------------------------------------------------------------- pages and the file

int render_pdf(const Value &doc, const std::string &path, const std::string &family, const std::string &page, std::string *err) {
    Fonts f(family);
    double W = page == "A4" ? 595 : 612, H = page == "A4" ? 842 : 792;
    double width = W - 2 * MARGIN, top0 = H - MARGIN, bottom = MARGIN + 12, body_h = top0 - bottom;
    std::vector<Row> rows = build_rows(doc.get("blocks"), f, MARGIN, width, body_h);
    std::vector<std::unique_ptr<Canvas>> pages;
    pages.emplace_back(new Canvas());
    double y = top0;
    for (size_t i = 0; i < rows.size(); i++) {
        Row &r = rows[i];
        bool at_top = y == top0;
        if (r.brk) {
            if (!at_top) {
                pages.emplace_back(new Canvas());
                y = top0;
            }
            continue;
        }
        if (r.droptop && at_top) continue;
        double run = r.h;
        size_t j = i;
        while (rows[j].glue && j + 1 < rows.size() && run <= body_h) {
            j++;
            run += rows[j].h;
        }
        if (run > body_h) run = r.h;   // can't keep it all together; only this row has to fit
        if (run > y - bottom && !at_top) {
            pages.emplace_back(new Canvas());
            y = top0;
            if (r.droptop) continue;
        }
        if (r.fill) {
            double h = std::max(r.h, y - bottom);
            r.draw(*pages.back(), y, h);
            y -= h;
            continue;
        }
        if (r.draw) r.draw(*pages.back(), y, r.h);
        y -= r.h;
    }
    size_t n = pages.size();
    for (size_t i = 0; i < n; i++) {
        Canvas &c = *pages[i];
        if (doc.get("footer").truthy()) c.text(MARGIN, MARGIN - 18, text_of(doc.get("footer")), I, 8.5, &GRAY);
        std::string label = "Page " + std::to_string(i + 1) + " of " + std::to_string(n);
        c.text(W - MARGIN - f.width(label, R, 8.5), MARGIN - 18, label, R, 8.5, &GRAY);
    }
    std::vector<Canvas *> ptrs;
    for (auto &p : pages) ptrs.push_back(p.get());
    std::string data = write(ptrs, f, W, H, doc.has("title") ? text_of(doc.get("title")) : "");
    if (!files::write(path, data, err)) return 0;
    return (int)n;
}

static std::string num_str(double v) {
    // whole numbers as Python prints ints (612), others as its floats (595.5)
    if (v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string((long long)v);
    return json::py_float(v);
}

std::string write(const std::vector<Canvas *> &pages, const Fonts &f, double W, double H, const std::string &title) {
    std::vector<std::string> objs(2);
    auto add = [&](const std::string &b) {
        objs.push_back(b);
        return (int)objs.size();
    };
    std::vector<int> font_ids = f.pdf_fonts(add);
    std::string fonts;
    for (size_t i = 0; i < font_ids.size(); i++) fonts += (i ? " " : "") + std::string("/F") + std::to_string(i + 1) + " " + std::to_string(font_ids[i]) + " 0 R";
    std::vector<int> kids;
    for (Canvas *c : pages) {
        std::string ops;
        for (size_t i = 0; i < c->ops.size(); i++) ops += (i ? "\n" : "") + c->ops[i];
        std::string data = zlib::compress(ops);
        int cid = add("<< /Length " + std::to_string(data.size()) + " /Filter /FlateDecode >>\nstream\n" + data + "\nendstream");
        std::vector<std::string> xobj;
        for (const auto &im : c->images) {
            const images::Image &img = *im.second;
            std::string space = img.colors == 1 ? "/DeviceGray" : img.colors == 4 ? "/DeviceCMYK" : "/DeviceRGB";
            std::string decode = img.inverted ? " /Decode [1 0 1 0 1 0 1 0]" : "";
            std::string head = "/Type /XObject /Subtype /Image /Width " + std::to_string(img.w) + " /Height " + std::to_string(img.h) +
                               " /ColorSpace " + space + " /BitsPerComponent 8 /Filter /" + img.filter + decode;
            std::string smask;
            if (img.has_alpha) {
                int mid = add("<< /Type /XObject /Subtype /Image /Width " + std::to_string(img.w) + " /Height " + std::to_string(img.h) +
                              " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length " + std::to_string(img.alpha.size()) +
                              " >>\nstream\n" + img.alpha + "\nendstream");
                smask = " /SMask " + std::to_string(mid) + " 0 R";
            }
            int iid = add("<< " + head + smask + " /Length " + std::to_string(img.data.size()) + " >>\nstream\n" + img.data + "\nendstream");
            xobj.push_back("/" + im.first + " " + std::to_string(iid) + " 0 R");
        }
        std::string res = "/Font << " + fonts + " >>";
        if (!xobj.empty()) res += " /XObject << " + text::join(xobj, " ") + " >>";
        std::vector<std::string> annots;
        for (const auto &l : c->links)
            annots.push_back(std::to_string(add("<< /Type /Annot /Subtype /Link /Rect [" + f2(l.x1) + " " + f2(l.y1) + " " + f2(l.x2) + " " + f2(l.y2) +
                                                "] /Border [0 0 0] /A << /S /URI /URI (" + esc(enc(l.url)) + ") >> >>")) +
                             " 0 R");
        std::string annot = annots.empty() ? "" : " /Annots [" + text::join(annots, " ") + "]";
        kids.push_back(add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num_str(W) + " " + num_str(H) + "] /Resources << " + res +
                           " >> /Contents " + std::to_string(cid) + " 0 R" + annot + " >>"));
    }
    objs[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    std::string kidlist;
    for (size_t i = 0; i < kids.size(); i++) kidlist += (i ? " " : "") + std::to_string(kids[i]) + " 0 R";
    objs[1] = "<< /Type /Pages /Kids [" + kidlist + "] /Count " + std::to_string(kids.size()) + " >>";
    int info = add("<< /Title (" + esc(enc(title)) + ") /Creator (Chalkboard) /Producer (Chalkboard) >>");
    std::string out = "%PDF-1.7\n%\xe2\xe3\xcf\xd3\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objs.size(); i++) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char b[32];
        std::snprintf(b, sizeof b, "%010zu 00000 n \n", off);
        out += b;
    }
    out += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root 1 0 R /Info " + std::to_string(info) + " 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return out;
}

}  // namespace pdf
