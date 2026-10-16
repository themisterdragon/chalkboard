#include "ui_font.h"

#include <algorithm>
#include <cmath>

#include "fontlib.h"
#include "paint.h"
#include "text.h"

namespace ui {

namespace {

// fonts with lots of symbols, for the characters a UI font lacks
const char *FALLBACKS[] = {"DejaVu Sans", "Noto Sans", "Segoe UI Symbol", "Segoe UI", "Arial Unicode MS", "Apple Symbols", "Lucida Grande",
                           "Liberation Sans", "Noto Sans Symbols", "Noto Sans Symbols2", "FreeSans", "Arial", "Helvetica"};

uint32_t next_cp(const std::string &s, size_t &i) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) {
        cp = c;
        n = 1;
    } else if ((c >> 5) == 6) {
        cp = c & 0x1F;
        n = 2;
    } else if ((c >> 4) == 14) {
        cp = c & 0x0F;
        n = 3;
    } else if ((c >> 3) == 30) {
        cp = c & 0x07;
        n = 4;
    } else {
        i++;
        return 0xFFFD;
    }
    if (i + (size_t)n > s.size()) {
        i = s.size();
        return 0xFFFD;
    }
    for (int k = 1; k < n; k++) {
        unsigned char d = (unsigned char)s[i + (size_t)k];
        if ((d >> 6) != 2) {
            i += (size_t)k;
            return 0xFFFD;
        }
        cp = (cp << 6) | (d & 0x3F);
    }
    i += (size_t)n;
    return cp;
}

}  // namespace

bool has_family(const std::string &family) {
    bool fb, fi;
    return find_face(family, 0, fb, fi) != nullptr;
}

std::shared_ptr<raster::Outlines> find_face(const std::string &family, int style, bool &fake_bold, bool &fake_italic) {
    return paint::family_outlines(family, style, &fake_bold, &fake_italic);
}

Font::Font(const std::vector<std::string> &families, double px_, int style_) : px(px_), style(style_) {
    for (const auto &f : families) {
        Face face;
        face.o = find_face(f, style, face.bold, face.italic);
        if (face.o) {
            family = f;
            faces_.push_back(face);
            break;
        }
    }
    for (const char *f : FALLBACKS) {
        if (f == family) continue;
        Face face;
        face.o = find_face(f, style, face.bold, face.italic);
        if (face.o) faces_.push_back(face);
        if (faces_.size() >= 4) break;
    }
    if (faces_.empty()) {   // anything at all
        for (const auto &f : fontlib::system_font_order()) {
            Face face;
            face.o = find_face(f, style, face.bold, face.italic);
            if (face.o) {
                faces_.push_back(face);
                break;
            }
        }
    }
    if (!faces_.empty()) {
        const auto &o = faces_[0].o;
        double k = px / o->units_per_em();
        ascent = std::ceil(o->ascent() * k);
        descent = std::ceil(-o->descent() * k);
    } else {
        ascent = std::ceil(px * 0.8);
        descent = std::ceil(px * 0.2);
    }
    line = ascent + descent;
}

std::pair<int, uint32_t> Font::glyph_of(uint32_t c) {
    auto hit = glyphs_.find(c);
    if (hit != glyphs_.end()) return hit->second;
    std::pair<int, uint32_t> out{0, 0};
    for (size_t i = 0; i < faces_.size(); i++) {
        uint32_t g = faces_[i].o->glyph_index(c);
        if (g) {
            out = {(int)i, g};
            break;
        }
    }
    glyphs_[c] = out;
    return out;
}

double Font::advance(int face, uint32_t g) {
    if (faces_.empty()) return px * 0.5;
    auto &o = faces_[(size_t)face].o;
    double a = o->glyph(g).advance * px / o->units_per_em();
    if (faces_[(size_t)face].bold) a += px * 0.04;
    return a;
}

double Font::advance_of(uint32_t c) {
    if (c == '\t') c = ' ';
    auto g = glyph_of(c);
    return advance(g.first, g.second);
}

double Font::width(const std::string &s) {
    auto hit = widths_.find(s);
    if (hit != widths_.end()) return hit->second;
    double w = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = next_cp(s, i);
        if (c == '\t') c = ' ';
        auto g = glyph_of(c);
        w += advance(g.first, g.second);
    }
    if (widths_.size() > 20000) widths_.clear();
    widths_[s] = w;
    return w;
}

double Font::draw(raster::Image &img, double x, double y, const std::string &s, raster::Color c) {
    if (faces_.empty()) return x;
    int by = (int)std::lround(y);
    int cx0, cy0, cx1, cy1;
    bool visible = img.clipped(cx0, cy0, cx1, cy1) && by + descent >= cy0 && by - ascent < cy1;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = next_cp(s, i);
        if (cp == '\t') cp = ' ';
        auto g = glyph_of(cp);
        double adv = advance(g.first, g.second);
        if (visible && cp != ' ' && x + adv >= cx0 && x < cx1) {
            // a quarter-pixel position, so spacing stays even without blurring every glyph alike
            double fx = std::floor(x);
            int sub = (int)std::lround((x - fx) * 4);
            if (sub == 4) {
                sub = 0;
                fx += 1;
            }
            const Face &f = faces_[(size_t)g.first];
            Key key{f.o.get(), g.second, sub};
            auto m = masks_.find(key);
            if (m == masks_.end()) {
                const raster::Glyph &gl = f.o->glyph(g.second);
                double k = px / f.o->units_per_em();
                double mt[6] = {k, 0, f.italic ? 0.2 * k : 0, -k, sub / 4.0, 0};
                raster::Mask mk = raster::coverage(gl.path, mt);
                if (f.bold && mk.w > 0) {   // thicker: the mask, smeared a little to the right
                    int extra = std::max(1, (int)std::lround(px * 0.04));
                    raster::Mask b = mk;
                    b.w += extra;
                    b.a.assign((size_t)b.w * (size_t)b.h, 0);
                    for (int yy = 0; yy < mk.h; yy++)
                        for (int xx = 0; xx < b.w; xx++) {
                            int v = 0;
                            for (int d = 0; d <= extra; d++) {
                                int sx = xx - d;
                                if (sx >= 0 && sx < mk.w) v = std::max(v, (int)mk.a[(size_t)yy * (size_t)mk.w + (size_t)sx]);
                            }
                            b.a[(size_t)yy * (size_t)b.w + (size_t)xx] = (uint8_t)v;
                        }
                    mk = b;
                }
                if (masks_.size() > 6000) masks_.clear();
                m = masks_.emplace(key, std::move(mk)).first;
            }
            if (m->second.w > 0) img.mask(m->second, (int)fx, by, c);
        }
        x += adv;
    }
    return x;
}

void Font::edges(const std::string &s, std::vector<double> &xs, std::vector<size_t> &at) {
    xs.clear();
    at.clear();
    double x = 0;
    for (size_t i = 0; i < s.size();) {
        xs.push_back(x);
        at.push_back(i);
        uint32_t c = next_cp(s, i);
        if (c == '\t') c = ' ';
        auto g = glyph_of(c);
        x += advance(g.first, g.second);
    }
    xs.push_back(x);
    at.push_back(s.size());
}

size_t Font::hit(const std::string &s, double x) {
    std::vector<double> xs;
    std::vector<size_t> at;
    edges(s, xs, at);
    for (size_t k = 0; k + 1 < xs.size(); k++)
        if (x < (xs[k] + xs[k + 1]) / 2) return at[k];
    return s.size();
}

}  // namespace ui
