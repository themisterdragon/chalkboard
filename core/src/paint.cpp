#include "paint.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

#include "fontlib.h"
#include "jpeg.h"
#include "text.h"
#include "zlib.h"

namespace paint {

namespace {

// Windows-1252's 0x80-0x9F (the PDF engine's one byte per character)
const char32_t CP1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                             0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                             0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

char32_t unicode_of(unsigned char b) { return b >= 0x80 && b < 0xA0 ? CP1252[b - 0x80] : (char32_t)b; }

std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::mutex g_mu;   // the font caches below (glyphs are cached inside Outlines too: draw from one thread)
std::map<std::pair<const std::string *, uint32_t>, std::shared_ptr<raster::Outlines>> g_data;
std::map<std::pair<std::string, int>, std::pair<std::shared_ptr<raster::Outlines>, std::pair<bool, bool>>> g_family;

std::shared_ptr<raster::Outlines> of_data(std::shared_ptr<const std::string> data, uint32_t off) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto key = std::make_pair(data.get(), off);
    auto hit = g_data.find(key);
    if (hit != g_data.end()) return hit->second;
    auto o = raster::Outlines::from_data(data, off);
    g_data[key] = o;
    return o;
}

raster::Color col(pdf::RGB c) { return {c.r, c.g, c.b}; }

}  // namespace

std::shared_ptr<raster::Outlines> family_outlines(const std::string &family, int style, bool *faked_bold, bool *faked_italic) {
    style &= 3;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto hit = g_family.find({family, style});
        if (hit != g_family.end()) {
            if (faked_bold) *faked_bold = hit->second.second.first;
            if (faked_italic) *faked_italic = hit->second.second.second;
            return hit->second.first;
        }
    }
    std::shared_ptr<raster::Outlines> out;
    bool fb = false, fi = false;
    const auto &all = fontlib::system_fonts();
    auto it = all.find(family);
    if (it == all.end())
        for (auto j = all.begin(); j != all.end(); ++j)
            if (lower(j->first) == lower(family)) it = j;
    if (it != all.end()) {
        // the style itself, or the nearest one (drawn thicker or slanted)
        for (int k : {style, style & 1, style & 2, 0, 1, 2, 3}) {
            auto f = it->second.find(k);
            if (f == it->second.end()) continue;
            out = raster::Outlines::load(f->second.first, f->second.second);
            if (!out) continue;
            fb = (style & 1) && !(k & 1);
            fi = (style & 2) && !(k & 2);
            break;
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_family[{family, style}] = {out, {fb, fi}};
    if (faked_bold) *faked_bold = fb;
    if (faked_italic) *faked_italic = fi;
    return out;
}

namespace {
// a standard PDF font: its style, and fonts with the same widths that computers usually have
std::shared_ptr<raster::Outlines> standard(const std::string &name, bool *fb, bool *fi) {
    std::string base = name.substr(0, name.find('-')), rest = name.find('-') == std::string::npos ? "" : name.substr(name.find('-') + 1);
    int style = (rest.find("Bold") != std::string::npos ? 1 : 0) | (rest.find("Italic") != std::string::npos || rest.find("Oblique") != std::string::npos ? 2 : 0);
    std::vector<std::string> fams;
    if (base == "Times")
        fams = {"Times", "Times New Roman", "Nimbus Roman", "Liberation Serif", "Tinos", "FreeSerif", "DejaVu Serif", "Georgia", "Cambria"};
    else if (base == "Courier")
        fams = {"Courier", "Courier New", "Nimbus Mono PS", "Liberation Mono", "Cousine", "FreeMono", "DejaVu Sans Mono", "Consolas", "Menlo"};
    fams.insert(fams.end(), {"Helvetica", "Arial", "Nimbus Sans", "Liberation Sans", "Arimo", "FreeSans", "Helvetica Neue", "DejaVu Sans",
                             "Segoe UI", "Noto Sans", "Verdana"});
    for (const auto &f : fams)
        if (auto o = family_outlines(f, style, fb, fi)) return o;
    for (const auto &f : fontlib::system_font_order())   // anything at all, so text still shows
        if (auto o = family_outlines(f, style, fb, fi)) return o;
    return nullptr;
}
}  // namespace

std::shared_ptr<raster::Outlines> outlines_for(const std::string &pdf_name) { return standard(pdf_name, nullptr, nullptr); }

bool decode(const images::Image &img, std::vector<uint8_t> &pix, std::vector<uint8_t> &alpha, int &channels) {
    pix.clear();
    alpha.clear();
    if (img.w <= 0 || img.h <= 0) return false;
    size_t n = (size_t)img.w * (size_t)img.h;
    if (img.filter == "DCTDecode") {
        int w = 0, h = 0;
        if (!jpeg::decode(img.data, w, h, pix) || w != img.w || h != img.h) return false;
        channels = 3;
        return true;
    }
    if (img.filter != "FlateDecode" || (img.colors != 1 && img.colors != 3)) return false;
    try {
        std::string raw = zlib::decompress(img.data, n * (size_t)img.colors + 1);
        if (raw.size() != n * (size_t)img.colors) return false;
        pix.assign(raw.begin(), raw.end());
        if (img.has_alpha) {
            std::string a = zlib::decompress(img.alpha, n + 1);
            if (a.size() == n) alpha.assign(a.begin(), a.end());
        }
    } catch (...) {
        return false;
    }
    channels = img.colors;
    return true;
}

PixelCanvas::PixelCanvas(raster::Image &img, double page_h, double scale) : img_(img), ph_(page_h), k_(scale) {}

const PixelCanvas::Face &PixelCanvas::face(int style) {
    if (style < 0) style = 0;
    if ((size_t)style >= faces_.size()) faces_.resize((size_t)style + 1);
    Face &f = faces_[(size_t)style];
    if (f.ready) return f;
    f.ready = true;
    auto board = dynamic_cast<const fontlib::BoardFonts *>(fonts);
    if (board && (size_t)style < board->entries.size() && board->entries[(size_t)style].face) {
        auto &face = board->entries[(size_t)style].face;
        f.o = of_data(face->data(), face->offset());
    } else {
        std::string name = board && (size_t)style < board->entries.size() ? board->entries[(size_t)style].std_name
                         : fonts && (size_t)style < fonts->names.size() ? fonts->names[(size_t)style]
                                                                       : std::vector<std::string>{"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"}[(size_t)(style & 3)];
        f.o = standard(name, &f.bold, &f.italic);
    }
    if (fakes && (size_t)style < fakes->size()) {
        f.bold = f.bold || (*fakes)[(size_t)style].first;
        f.italic = f.italic || (*fakes)[(size_t)style].second;
    }
    return f;
}

void PixelCanvas::text(double x, double y, const std::string &s, int style, double size, const pdf::RGB *color) {
    if (s.empty()) return;
    const Face &f = face(style);
    if (!f.o) return;
    raster::Color c = color ? col(*color) : raster::Color{0, 0, 0};
    double sc = size / f.o->units_per_em();
    const std::vector<int> *table = fonts && (size_t)style < fonts->tables.size() ? &fonts->tables[(size_t)style] : nullptr;
    double pen = x;
    for (unsigned char b : pdf::enc(s)) {
        const raster::Glyph &g = f.o->glyph(f.o->glyph_index(unicode_of(b)));
        if (!g.path.empty()) {
            double m[6] = {sc * k_, 0, (f.italic ? 0.2 : 0) * sc * k_, -sc * k_, pen * k_, (ph_ - y) * k_};
            img_.fill(g.path, c, 1, m);
            if (f.bold)   // like the PDF's fill-and-stroke text
                for (const auto &ct : g.path.contours_) img_.stroke(ct, size * 0.035 * k_, c, true, true, m);
        }
        if (table && b >= 32 && (size_t)(b - 32) < table->size()) pen += (*table)[(size_t)(b - 32)] * size / 1000;
        else pen += g.advance * sc;
    }
}

void PixelCanvas::image(const images::Image &img, double x, double y, double w, double h) {
    std::vector<uint8_t> pix, alpha;
    int ch = 3;
    if (!decode(img, pix, alpha, ch)) return;
    img_.picture(pix.data(), alpha.empty() ? nullptr : alpha.data(), img.w, img.h, ch, x * k_, (ph_ - y - h) * k_, w * k_, h * k_);
}

void PixelCanvas::line(double x1, double y1, double x2, double y2, double w, pdf::RGB color) {
    img_.stroke({to(x1, y1), to(x2, y2)}, w * k_, col(color));
}

void PixelCanvas::rect(double x, double y, double w, double h, double lw) {
    // a frame: the outer box less the inner one (square corners, like the PDF)
    double hw = std::max(lw * k_, 0.35) / 2;
    raster::Pt a = to(x, y + h), b = to(x + w, y);
    raster::Path p;
    p.move(a.x - hw, a.y - hw);
    p.line(b.x + hw, a.y - hw);
    p.line(b.x + hw, b.y + hw);
    p.line(a.x - hw, b.y + hw);
    p.close();
    if (b.x - a.x > 2 * hw && b.y - a.y > 2 * hw) {
        p.move(a.x + hw, a.y + hw);
        p.line(a.x + hw, b.y - hw);
        p.line(b.x - hw, b.y - hw);
        p.line(b.x - hw, a.y + hw);
        p.close();
    }
    img_.fill(p, {0, 0, 0});
}

void PixelCanvas::fill(double x, double y, double w, double h, pdf::RGB color) {
    raster::Pt a = to(x, y + h);
    img_.rect(a.x, a.y, w * k_, h * k_, col(color));
}

void PixelCanvas::clip(double x, double y, double w, double h) {
    raster::Pt a = to(x, y + h);
    img_.clip(a.x, a.y, w * k_, h * k_);
}

void PixelCanvas::unclip() { img_.unclip(); }

void PixelCanvas::path(const std::vector<std::pair<double, double>> &pts, double w, pdf::RGB color) {
    std::vector<raster::Pt> out;
    for (const auto &p : pts) out.push_back(to(p.first, p.second));
    img_.stroke(out, w * k_, col(color), true);
}

void PixelCanvas::polygon(const std::vector<std::pair<double, double>> &pts, pdf::RGB color) {
    if (pts.empty()) return;
    raster::Path p;
    raster::Pt a = to(pts[0].first, pts[0].second);
    p.move(a.x, a.y);
    for (size_t i = 1; i < pts.size(); i++) {
        raster::Pt b = to(pts[i].first, pts[i].second);
        p.line(b.x, b.y);
    }
    p.close();
    img_.fill(p, col(color));
}

void PixelCanvas::ellipse(double cx, double cy, double rx, double ry, pdf::RGB color, double lw, const pdf::RGB *fill) {
    raster::Pt c = to(cx, cy);
    raster::Path p;
    p.ellipse(c.x, c.y, rx * k_, ry * k_);
    if (fill) img_.fill(p, col(*fill));
    if (!p.contours_.empty()) img_.stroke(p.contours_[0], lw * k_, col(color), true, true);
}

}  // namespace paint
