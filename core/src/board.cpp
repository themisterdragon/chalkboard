#include "board.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "export_pptx.h"
#include "files.h"
#include "markup.h"
#include "paint.h"
#include "pyrand.h"
#include "store.h"
#include "text.h"
#include "zlib.h"


namespace board {

using json::Value;
using pdf::RGB;
using store::text_of;

static const double PAD = 64, GAP = 40, INSET = 30, LOGO_H = 150, LOGO_W = 380;
static const int HB = 4 + pdf::B;   // the heading font, bold (styles 4-7 are the heading font)
static const int MIN_SIZE = 30, BIG_SIZE = 40;

static const Palette CHALK{{0.122, 0.231, 0.188}, {0.165, 0.290, 0.239}, {0.965, 0.953, 0.914}, {0.976, 0.824, 0.420}, {0.690, 0.773, 0.729}};
static const Palette WHITE{{1, 1, 1}, {0.937, 0.953, 0.945}, {0.098, 0.098, 0.098}, {0.075, 0.420, 0.298}, {0.400, 0.420, 0.410}};

static double weight(const std::string &key) {
    static const std::map<std::string, double> W_ = {{"targets", 1.12}, {"bell_ringer", 1.0}, {"success", 0.85}, {"materials", 0.85},
                                                      {"homework", 0.85}, {"closure", 0.85}, {"standards", 0.78}};
    auto it = W_.find(key);
    return it == W_.end() ? 0.9 : it->second;
}

// ----------------------------------------------------------------------------- colors

static bool rgb_of(const std::string &hexcode, RGB &out) {
    std::string h = text::lstrip_chars(hexcode, "#");
    if (text::length(h) != 6) return false;
    double v[3];
    for (int i = 0; i < 3; i++) {
        std::string pair = h.substr((size_t)i * 2, 2);
        char *end = nullptr;
        long x = std::strtol(pair.c_str(), &end, 16);
        if (!end || *end || pair.size() != 2) return false;
        v[i] = (double)x / 255;
    }
    out = {v[0], v[1], v[2]};
    return true;
}

static double lum(const RGB &c) {
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

static double contrast(const RGB &a, const RGB &b) {
    double x = lum(a), y = lum(b), hi = std::max(x, y), lo = std::min(x, y);
    return (hi + 0.05) / (lo + 0.05);
}

static RGB mix(const RGB &a, const RGB &b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

Palette school_palette(const std::string &primary, const std::string &secondary, const std::string &txt) {
    RGB bg, accent, ink;
    bool has_bg = rgb_of(primary, bg), has_accent = rgb_of(secondary, accent), has_ink = rgb_of(txt, ink);
    if (!has_bg) {
        Palette base = CHALK;
        if (has_ink) {
            base.ink = ink;
            base.dim = mix(base.bg, ink, 0.72);
        }
        if (has_accent) base.label = accent;
        return base;
    }
    RGB white{1, 1, 1}, dark{0.098, 0.098, 0.098};
    RGB autoc = contrast(dark, bg) > contrast(white, bg) ? dark : white;   // max(): the first wins a tie
    if (!has_ink) ink = autoc;
    return {bg, mix(bg, autoc, 0.10), ink, has_accent ? accent : ink, mix(bg, ink, 0.72)};
}

std::vector<std::string> low_contrast(const Value &st) {
    RGB bg = school_palette(store::field(st, "primary_color")).panel;
    std::vector<std::string> out;
    for (auto kn : std::vector<std::pair<const char *, const char *>>{{"secondary_color", "HEADINGS"}, {"text_color", "TEXT"}}) {
        RGB c;
        if (rgb_of(store::field(st, kn.first), c) && contrast(c, bg) < 3) out.push_back(kn.second);
    }
    return out;
}

Palette palette(const Value &doc) {
    std::string style = doc.s("style");
    if (style == "school") {
        const Value &c = doc.get("colors");
        return school_palette(text_of(c.at(0).is_null() ? Value("") : c.at(0)), text_of(c.at(1).is_null() ? Value("") : c.at(1)),
                              text_of(c.at(2).is_null() ? Value("") : c.at(2)));
    }
    return style == "white" ? WHITE : CHALK;
}

// ----------------------------------------------------------------------------- laying out panels

namespace {

struct Sec {
    std::string key, label, col;
    std::shared_ptr<const Value> blocks, shrt;
    bool codes = false;   // showing the standards as codes only
};

struct Item {
    double dx, dy;
    pdf::RichLine line;
};

struct Laid {
    double h;
    std::vector<Item> items;
    std::shared_ptr<const Value> keep;   // holds the blocks so their address isn't reused
};

struct Planned {
    const Sec *sec;
    double size, h;
    const std::vector<Item> *items;
};

struct Ctx {
    const pdf::Fonts &f;
    std::map<std::tuple<const Value *, double, double>, Laid> laid;
    explicit Ctx(const pdf::Fonts &fonts) : f(fonts) {}

    const Laid &lay(const std::shared_ptr<const Value> &blocks, double size, double width) {
        auto key = std::make_tuple(blocks.get(), size, width);
        auto it = laid.find(key);
        if (it != laid.end()) return it->second;
        Laid l;
        l.keep = blocks;
        double y = 0, lead = size * 1.24;
        std::string prev;
        for (const auto &b : blocks->arr()) {
            std::string t = b.s("t");
            if (!prev.empty()) y += size * (t == prev && t == "bullet" ? 0.22 : 0.55);
            prev = t;
            std::vector<pdf::RichLine> lines;
            if (t == "bullet") {
                double ind = size * (1.05 + 1.05 * b.get("indent").as_double(0));
                lines = f.rich_wrap(text_of(b.get("text")), pdf::R, size, width - ind);
                l.items.push_back({ind - size * 0.8, y + size, {{"\xE2\x80\xA2", pdf::R, false}}});
                for (size_t i = 0; i < lines.size(); i++) l.items.push_back({ind, y + size + (double)i * lead, lines[i]});
            } else if (t == "kv") {
                std::string label = text_of(b.get("label")) + "  ";
                double lw = f.width(label, pdf::B, size);
                if (b.get("text").truthy()) lines = f.rich_wrap(text_of(b.get("text")), pdf::R, size, width, width - lw);
                else lines.push_back({});
                l.items.push_back({0, y + size, {{markup::plain(text::strip(label)), pdf::B, false}}});
                for (size_t i = 0; i < lines.size(); i++) l.items.push_back({i == 0 ? lw : 0, y + size + (double)i * lead, lines[i]});
            } else {
                int base = b.get("style").is_string() && b.get("style").str() == "bold" ? pdf::B : pdf::R;
                lines = f.rich_wrap(text_of(b.get("text")), base, size, width);
                for (size_t i = 0; i < lines.size(); i++) l.items.push_back({0, y + size + (double)i * lead, lines[i]});
            }
            y += size + (double)(lines.size() - 1) * lead + size * 0.28;   // room for descenders
        }
        l.h = y;
        return laid.emplace(key, std::move(l)).first->second;
    }
};

double label_size(double s) { return std::max(20.0, std::min(34.0, s * 0.62)); }

std::pair<double, std::vector<Planned>> plan_column(Ctx &cx, const std::vector<Sec> &sections, double width, double s) {
    double inner = width - 2 * INSET;
    bool have = false;
    std::pair<double, std::vector<Planned>> best;
    for (double std_ : {1.0, 0.85}) {
        std::vector<Planned> plan;
        double total = GAP * (double)(sections.size() - 1);
        for (const auto &sec : sections) {
            double size = sec.codes ? s : s * weight(sec.key) * (sec.key == "standards" ? std_ : 1);
            const Laid &l = cx.lay(sec.blocks, size, inner);
            plan.push_back({&sec, size, l.h, &l.items});
            total += 2 * INSET + label_size(s) * 1.5 + l.h;
        }
        if (!have || total < best.first) {
            best = {total, plan};
            have = true;
        }
    }
    return best;
}

std::pair<int, std::vector<Planned>> fit_column(Ctx &cx, const std::vector<Sec> &sections, double width, double height, int cap = 64) {
    if (sections.empty()) return {cap, {}};
    int lo = 14, hi = cap;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (plan_column(cx, sections, width, mid).first <= height) lo = mid;
        else hi = mid - 1;
    }
    return {lo, plan_column(cx, sections, width, lo).second};
}

struct SlidePlan {
    int size;
    std::vector<Sec> left, right;
    double lw;
};

SlidePlan split_columns(Ctx &cx, const std::vector<Sec> &secs, double full, double height, const std::string &layout) {
    std::vector<int> home;
    for (const auto &x : secs) home.push_back(x.col == "right" ? 1 : 0);
    bool both = std::find(home.begin(), home.end(), 0) != home.end() && std::find(home.begin(), home.end(), 1) != home.end();
    if (secs.size() < 2 || layout == "one" || (layout == "sides" && !both)) return {fit_column(cx, secs, full, height).first, secs, {}, full};
    std::vector<long> masks;
    if (layout == "sides") {
        long m = 0;
        for (size_t i = 0; i < home.size(); i++) m += (long)home[i] << i;
        masks.push_back(m);
    } else
        for (long m = 1; m < (1L << secs.size()) - 1; m++) masks.push_back(m);
    bool have = false;
    std::tuple<int, int, bool> best_score;
    SlidePlan best;
    for (long mask : masks) {
        std::vector<Sec> a, b;
        int moved = 0;
        for (size_t i = 0; i < secs.size(); i++) {
            int k = (int)((mask >> i) & 1);
            (k ? b : a).push_back(secs[i]);
            moved += k != home[i];
        }
        for (double share : {0.56, 0.5, 0.62, 0.44}) {
            double lw = (full - GAP) * share;
            int size = std::min(fit_column(cx, a, lw, height).first, fit_column(cx, b, full - lw - GAP, height).first);
            auto score = std::make_tuple(size, -moved, share == 0.56);
            if (!have || score > best_score) {
                best_score = score;
                best = {size, a, b, lw};
                have = true;
            }
        }
    }
    return best;
}

std::vector<Sec> codes_only(const std::vector<Sec> &secs) {
    std::vector<Sec> out;
    for (auto x : secs) {
        if (x.shrt) {
            x.blocks = x.shrt;
            x.codes = true;
        }
        out.push_back(x);
    }
    return out;
}

std::vector<SlidePlan> plan_slides(Ctx &cx, std::vector<Sec> secs, double full, double first_h, double rest_h, const std::string &layout, int least) {
    if (secs.empty()) return {{0, {}, {}, full}};
    SlidePlan one = split_columns(cx, secs, full, first_h, layout);
    if (one.size >= least) return {one};
    secs = codes_only(secs);
    one = split_columns(cx, secs, full, first_h, layout);
    if (one.size >= least || secs.size() == 1) return {one};
    std::vector<SlidePlan> slides;
    double height = first_h;
    while (!secs.empty()) {
        SlidePlan plan;
        size_t k = secs.size();
        for (; k >= 1; k--) {
            std::vector<Sec> part(secs.begin(), secs.begin() + (long)k);
            plan = split_columns(cx, part, full, height, layout);
            if (plan.size >= least || k == 1) break;
        }
        slides.push_back(plan);
        secs.erase(secs.begin(), secs.begin() + (long)k);
        height = rest_h;
    }
    return slides;
}

// ----------------------------------------------------------------------------- drawing

void draw_panel(pdf::Canvas &c, double x, double y, double w, double h, const Palette &col, double ls, const std::string &look) {
    if (look == "outline") {
        double segs[4][4] = {{x, y, x + w, y}, {x, y + h, x + w, y + h}, {x, y, x, y + h}, {x + w, y, x + w, y + h}};
        for (auto &s : segs) c.line(s[0], s[1], s[2], s[3], 3, col.label);
    } else if (look == "chalk") {
        c.line(x + INSET, y + h - INSET - ls * 1.12, x + w - INSET, y + h - INSET - ls * 1.12, 2, col.dim);
    } else {
        c.fill(x, y, w, h, col.panel);
        c.fill(x, y, 8, h, col.label);
    }
}

void draw_column(Ctx &cx, pdf::Canvas &c, const std::vector<Sec> &sections, double x, double top, double width, double height, const Palette &col,
                 int cap, const std::string &look) {
    if (sections.empty()) return;
    auto fit = fit_column(cx, sections, width, height, cap);
    double ls = label_size(fit.first);
    std::vector<double> need;
    double sum = 0;
    for (const auto &p : fit.second) {
        need.push_back(2 * INSET + ls * 1.5 + p.h);
        sum += need.back();
    }
    double spare = std::max(0.0, height - GAP * (double)(fit.second.size() - 1) - sum) / (double)fit.second.size();
    double y = top;
    for (size_t i = 0; i < fit.second.size(); i++) {
        const Planned &p = fit.second[i];
        double ph = std::min(need[i] + spare, y - (top - height));
        draw_panel(c, x, y - ph, width, ph, col, ls, look);
        c.text(x + INSET, y - INSET - ls * 0.85, text::upper(p.sec->label), HB, ls, &col.label);
        double body = y - INSET - ls * 1.5;
        if (c.editable()) {   // the slideshow: one text box PowerPoint wraps itself
            c.blocks(x + INSET, body, width - 2 * INSET, body - (y - ph) - INSET / 2, *p.sec->blocks, p.size, col.ink, p.sec->label);
            y -= ph + GAP;
            continue;
        }
        c.clip(x, y - ph, width, ph);
        for (const auto &it : *p.items) {
            double xx = x + INSET + it.dx;
            for (const auto &piece : it.line) {
                c.text(xx, body - it.dy, piece.text, piece.style, p.size, &col.ink);
                double pw = cx.f.width(piece.text, piece.style, p.size);
                if (piece.under) c.line(xx, body - it.dy - p.size * 0.13, xx + pw, body - it.dy - p.size * 0.13, p.size * 0.06, col.ink);
                xx += pw;
            }
        }
        c.unclip();
        y -= ph + GAP;
    }
}

double draw_codes(Ctx &cx, pdf::Canvas &c, const Sec &sec, double x_right, double base, const Palette &col, double size = 34, double x_left = -1) {
    std::vector<std::pair<std::string, std::string>> parts;
    for (const auto &b : sec.blocks->arr()) {
        if (b.s("t") == "kv") parts.push_back({text_of(b.get("label")) + " ", text_of(b.get("text"))});
        else parts.push_back({"", text_of(b.get("text"))});
    }
    if (parts.size() == 1 && parts[0].first.empty()) parts = {{"Class Code: ", parts[0].second}};
    double w = 0;
    while (size > 20) {
        w = 0;
        for (const auto &p : parts) w += cx.f.width(p.first, pdf::B, size) + cx.f.width(p.second, pdf::B, size);
        w += 40 * (double)(parts.size() - 1);
        if (w <= W * 0.6) break;
        size -= 2;
    }
    double x = x_left < 0 ? x_right - w : x_left;
    if (c.editable()) {
        std::vector<pdf::Canvas::TextPiece> pieces;
        for (size_t k = 0; k < parts.size(); k++) {
            for (pdf::Canvas::TextPiece p : {pdf::Canvas::TextPiece{k ? "   " : "", pdf::R, size, col.dim}, pdf::Canvas::TextPiece{parts[k].first, pdf::R, size, col.dim},
                                             pdf::Canvas::TextPiece{parts[k].second, pdf::B, size, col.label}})
                if (!p.text.empty()) pieces.push_back(p);
        }
        c.textbox(x, base + size * 0.905, -1, size * 1.2, {pieces}, "l", "Class codes");
        return size;
    }
    for (const auto &p : parts) {
        c.text(x, base, p.first, pdf::R, size, &col.dim);
        x += cx.f.width(p.first, pdf::B, size);
        c.text(x, base, p.second, pdf::B, size, &col.label);
        x += cx.f.width(p.second, pdf::B, size) + 40;
    }
    return size;
}

double draw_banner(Ctx &cx, pdf::Canvas &c, const Sec &sec, double x, double top, double width, const Palette &col, const std::string &look) {
    std::vector<std::string> texts;
    for (const auto &b : sec.blocks->arr()) texts.push_back(text_of(b.get("text")));
    std::string txt = markup::plain(text::join(texts, " "));
    double inner = width - 2 * INSET;
    int size = 46;
    std::vector<std::string> lines;
    for (size = 46; size > 25; size -= 2) {
        lines = cx.f.wrap(txt, pdf::B, size, inner);
        if (lines.size() <= 2) break;
    }
    if (size <= 25) size = 26;   // the loop ran out on its last size, like range(46, 25, -2)
    if (lines.size() > 2) lines.resize(2);
    double ls = label_size(size), lead = size * 1.2;
    double ph = 2 * INSET + ls * 1.5 + size + (double)(lines.size() - 1) * lead;
    draw_panel(c, x, top - ph, width, ph, col, ls, look);
    c.text(x + INSET, top - INSET - ls * 0.85, text::upper(sec.label), HB, ls, &col.label);
    double body = top - INSET - ls * 1.5;
    if (c.editable()) {
        c.textbox(x + INSET, body - size * 0.9 + size * 0.905, inner, ph - 2 * INSET - ls * 1.5 + size * 0.3, {{{txt, pdf::B, (double)size, col.ink}}}, "l",
                  sec.label);
        return ph;
    }
    for (size_t i = 0; i < lines.size(); i++) c.text(x + INSET, body - size * 0.9 - (double)i * lead, lines[i], pdf::B, size, &col.ink);
    return ph;
}

double draw_header(Ctx &cx, pdf::Canvas &c, const Value &doc, const images::Image *logo, const Palette &col, const std::string &note) {
    c.fill(0, 0, W, H, col.bg);
    double lw = 0, lh = 0;
    if (logo) {
        double k = std::min(LOGO_H / logo->h, LOGO_W / logo->w);
        lw = logo->w * k;
        lh = logo->h * k;
    }
    bool on_left = !(doc.has("logo_place") && text_of(doc.get("logo_place")) == "right");
    bool center = doc.get("title_align").is_string() && doc.get("title_align").str() == "center";
    double x_title = PAD + (logo && (on_left || center) ? lw + GAP : 0);
    double x_right = W - PAD - (logo && (!on_left || center) ? lw + GAP : 0);
    std::vector<std::string> mparts;
    for (const auto &m : {text_of(doc.get("meta")), note}) if (!m.empty()) mparts.push_back(m);
    std::string meta = text::join(mparts, "  |  ");
    std::string title = markup::plain(text_of(doc.get("title")));
    std::string date = text_of(doc.get("date"));
    double y = H - PAD, tw;
    struct Part { std::string t; int st; double sz; RGB color; };
    std::vector<Part> right;
    if (center) tw = x_right - x_title;
    else {
        if (!date.empty()) right.push_back({date, HB, 46, col.label});
        if (!meta.empty()) right.push_back({meta, pdf::I, 28, col.dim});
        double rw = 0;
        for (const auto &r : right) rw = std::max(rw, cx.f.width(r.t, r.st, r.sz));
        tw = x_right - x_title - (rw ? rw + GAP : 0);
    }
    int ts = 76;
    std::vector<std::string> lines;
    for (ts = 76; ts > 39; ts -= 2) {
        lines = cx.f.wrap(title, HB, ts, tw);
        if (lines.size() == 1 || (ts <= 56 && lines.size() <= 2)) break;
    }
    if (ts <= 39) ts = 40;
    if (lines.size() > 2) lines.resize(2);
    double title_h = ts * 0.82 + (double)(lines.size() - 1) * ts * 1.1 + ts * 0.25;
    if (c.editable())
        c.textbox(x_title, y - ts * 0.82 + ts * 0.905, tw, title_h + ts * 0.3, {{{title, HB, (double)ts, col.ink}}}, center ? "ctr" : "l", "Title");
    else
        for (size_t i = 0; i < lines.size(); i++) {
            double lx = center ? x_title + (tw - cx.f.width(lines[i], HB, ts)) / 2 : x_title;
            c.text(lx, y - ts * 0.82 - (double)i * ts * 1.1, lines[i], HB, ts, &col.ink);
        }
    double ry = y, head_h;
    if (center) {
        std::vector<Part> sub;
        if (!date.empty()) sub.push_back({date, HB, 36, col.label});
        if (!meta.empty()) sub.push_back({meta, pdf::I, 28, col.dim});
        ry = y - title_h - 8;
        if (!sub.empty()) {
            double gap = 36, total = 0;
            for (const auto &s : sub) total += cx.f.width(s.t, s.st, s.sz);
            total += gap * (double)(sub.size() - 1);
            double sx = x_title + (tw - total) / 2;
            ry -= 36 * 0.95;
            for (const auto &s : sub) {
                c.text(sx, ry, s.t, s.st, s.sz, &s.color);
                sx += cx.f.width(s.t, s.st, s.sz) + gap;
            }
            ry -= 36 * 0.35;
        }
        head_h = std::max(y - ry, lh);
    } else {
        for (const auto &r : right) {
            ry -= r.sz * 0.95;
            c.text(x_right - cx.f.width(r.t, r.st, r.sz), ry, r.t, r.st, r.sz, &r.color);
            ry -= r.sz * 0.35;
        }
        head_h = std::max(std::max(title_h, y - ry), lh);
    }
    if (logo) c.image(*logo, on_left ? PAD : W - PAD - lw, y - (head_h + lh) / 2, lw, lh);
    double rule_y = y - head_h - 18;
    if (!(doc.get("panels").is_string() && doc.get("panels").str() == "outline")) c.line(PAD, rule_y, W - PAD, rule_y, 2.5, col.label);
    return rule_y;
}

double draw_footer(Ctx &cx, pdf::Canvas &c, const Value &doc, const Sec *codes, const Palette &col) {
    bool left = doc.get("codes_place").is_string() && doc.get("codes_place").str() == "left";
    std::string foot = store::field(doc, "footer");
    auto footer = [&](double base) {
        if (!foot.empty()) {
            double x = left && codes ? W - PAD - cx.f.width(foot, pdf::I, 22) : PAD;
            c.text(x, base, foot, pdf::I, 22, &col.dim);
        }
    };
    if (codes) {   // a strip along the bottom, big enough to read from the back row
        double size = draw_codes(cx, c, *codes, W - PAD, PAD - 8, col, 34, left ? PAD : -1);
        footer(PAD - 8);
        return PAD + size + 4;
    }
    footer(PAD - 30);
    return PAD;
}

std::vector<Sec> sections_of(const Value &list) {
    std::vector<Sec> out;
    for (const auto &x : list.arr()) {
        Sec s;
        s.key = x.s("key");
        s.label = text_of(x.get("label"));
        s.col = x.s("col");
        s.blocks = std::make_shared<const Value>(x.get("blocks"));
        if (x.get("short").truthy()) s.shrt = std::make_shared<const Value>(x.get("short"));
        // with "full standards text" off, a board's standards already are the codes (Python: blocks is short)
        if (s.shrt && *s.shrt == *s.blocks) {
            s.blocks = s.shrt;
            s.codes = true;
        }
        out.push_back(s);
    }
    return out;
}

}  // namespace

Slides board_slides(const Value &doc, const images::Image *logo_in, const std::function<pdf::Canvas *()> &make,
                    std::shared_ptr<fontlib::BoardFonts> fonts) {
    Slides out;
    out.fonts = fonts ? fonts : std::make_shared<fontlib::BoardFonts>(store::field(doc, "font"), store::field(doc, "head_font"));
    const images::Image *logo = doc.get("logo").truthy() ? logo_in : nullptr;
    Ctx cx(*out.fonts);
    Palette col = palette(doc);
    std::string look = doc.has("panels") ? text_of(doc.get("panels")) : "cards";
    std::string layout = doc.has("layout") ? text_of(doc.get("layout")) : "auto";
    int least = doc.get("big_text").truthy() ? BIG_SIZE : MIN_SIZE;
    std::vector<Sec> secs = sections_of(doc.get("left")), right = sections_of(doc.get("right"));
    secs.insert(secs.end(), right.begin(), right.end());
    std::unique_ptr<Sec> codes;
    for (size_t i = 0; i < secs.size(); i++)
        if (secs[i].key == "class_codes") {
            codes.reset(new Sec(secs[i]));
            secs.erase(secs.begin() + (long)i);
            break;
        }
    if (doc.get("order").truthy()) {   // the teacher's own order, across both sides
        std::map<std::string, size_t> rank;
        size_t r = 0;
        for (const auto &k : doc.get("order").arr()) rank[text_of(k)] = r++;   // a repeated key: its last place
        std::stable_sort(secs.begin(), secs.end(), [&](const Sec &a, const Sec &b) {
            size_t ra = rank.count(a.key) ? rank[a.key] : rank.size(), rb = rank.count(b.key) ? rank[b.key] : rank.size();
            return ra < rb;
        });
    }
    std::vector<Sec> top_secs = sections_of(doc.get("top"));
    double full = W - 2 * PAD;
    auto canvas = [&]() {
        pdf::Canvas *c = make ? make() : new pdf::Canvas();
        c->fakes = &out.fonts->fakes;
        c->fonts = out.fonts.get();
        return c;
    };
    // measure the header, footer, and Essential Question banner once on a scratch canvas
    std::unique_ptr<pdf::Canvas> scratch(canvas());
    double top = draw_header(cx, *scratch, doc, logo, col, "1 of 2") - 30;
    double bottom = draw_footer(cx, *scratch, doc, codes.get(), col);
    double rest_h = top - bottom;
    for (const auto &sec : top_secs) top -= draw_banner(cx, *scratch, sec, PAD, top, full, col, look) + GAP;
    auto slides = plan_slides(cx, secs, full, top - bottom, rest_h, layout, least);
    for (size_t n = 0; n < slides.size(); n++) {
        std::unique_ptr<pdf::Canvas> c(canvas());
        std::string note = slides.size() > 1 ? std::to_string(n + 1) + " of " + std::to_string(slides.size()) : "";
        double t = draw_header(cx, *c, doc, logo, col, note) - 30;
        double b = draw_footer(cx, *c, doc, codes.get(), col);
        if (n == 0)
            for (const auto &sec : top_secs) t -= draw_banner(cx, *c, sec, PAD, t, full, col, look) + GAP;
        double height = t - b;
        const SlidePlan &sp = slides[n];
        if (!sp.left.empty() && !sp.right.empty()) {
            draw_column(cx, *c, sp.left, PAD, t, sp.lw, height, col, sp.size, look);
            draw_column(cx, *c, sp.right, PAD + sp.lw + GAP, t, full - sp.lw - GAP, height, col, sp.size, look);
        } else if (!sp.left.empty() || !sp.right.empty()) {
            draw_column(cx, *c, sp.left.empty() ? sp.right : sp.left, PAD, t, full, height, col, 64, look);
        } else if (!doc.get("top").truthy()) {
            std::string msg = "Add a standard, I can statement, or bell ringer to fill this slide.";
            c->text((W - cx.f.width(msg, pdf::I, 34)) / 2, t - height / 2, msg, pdf::I, 34, &col.dim);
        }
        out.canvases.push_back(std::move(c));
    }
    return out;
}

// ----------------------------------------------------------------------------- PNG files

std::vector<raster::Image> pictures(const Value &doc, const images::Image *logo, double scale) {
    std::vector<std::pair<pdf::Canvas *, std::unique_ptr<raster::Image>>> made;
    int pw = std::max(1, (int)std::lround(W * scale)), ph = std::max(1, (int)std::lround(H * scale));
    Slides s = board_slides(doc, logo, [&]() -> pdf::Canvas * {
        std::unique_ptr<raster::Image> img(new raster::Image(pw, ph));
        auto *c = new paint::PixelCanvas(*img, H, scale);
        made.push_back({c, std::move(img)});
        return c;
    });
    std::vector<raster::Image> out;
    for (auto &c : s.canvases)
        for (auto &m : made)
            if (m.first == c.get()) out.push_back(std::move(*m.second));
    return out;
}

std::vector<std::string> render_png(const Value &doc, const images::Image *logo, const std::string &path) {
    std::vector<raster::Image> pics = pictures(doc, logo);
    std::string stem = path.substr(0, path.size() - 4);
    std::vector<std::string> paths = {path};
    for (size_t i = 2; i <= pics.size(); i++) paths.push_back(stem + " " + std::to_string(i) + ".png");
    for (size_t i = 0; i < pics.size(); i++) {
        std::string err;
        if (!files::write(paths[i], pics[i].png(), &err)) throw BoardError{"EXPORT FAILED: " + err};
    }
    for (size_t i = pics.size() + 1; i < 10; i++) {   // a slide left over from an earlier, fuller export
        std::string old = stem + " " + std::to_string(i) + ".png";
        if (files::exists(old)) files::remove(old);
    }
    // the same slides as an editable slideshow for PowerPoint, Keynote, or Google Slides
    std::string deck = stem + ".pptx", err;
    Slides shapes = board_slides(doc, logo, [] { return new export_pptx::SlideCanvas(); });
    std::vector<export_pptx::SlideCanvas *> list;
    for (auto &c : shapes.canvases) list.push_back(static_cast<export_pptx::SlideCanvas *>(c.get()));
    if (!export_pptx::render_pptx(list, deck, text_of(doc.get("title")), palette(doc).ink, &err)) throw BoardError{"EXPORT FAILED: " + err};
    paths.push_back(deck);
    return paths;
}

std::vector<std::string> render_day(const std::vector<Value> &docs, const images::Image *logo, const std::string &base, const std::string &title) {
    if (docs.empty()) throw BoardError{"NO LESSONS TO PUT IN THE SLIDESHOW"};
    std::shared_ptr<fontlib::BoardFonts> f;
    std::vector<Slides> pages, shapes;
    for (const auto &doc : docs) {
        pages.push_back(board_slides(doc, logo, nullptr, f));
        f = pages.back().fonts;
        shapes.push_back(board_slides(doc, logo, [] { return new export_pptx::SlideCanvas(); }));
    }
    std::vector<pdf::Canvas *> all;
    for (auto &s : pages) for (auto &c : s.canvases) all.push_back(c.get());
    std::string err;
    if (!files::write(base + ".pdf", pdf::write(all, *f, W, H, title), &err)) throw BoardError{"EXPORT FAILED: " + err};
    std::vector<export_pptx::SlideCanvas *> deck;
    for (auto &s : shapes) for (auto &c : s.canvases) deck.push_back(static_cast<export_pptx::SlideCanvas *>(c.get()));
    if (!export_pptx::render_pptx(deck, base + ".pptx", title, palette(docs[0]).ink, &err)) throw BoardError{"EXPORT FAILED: " + err};
    return {base + ".pptx", base + ".pdf"};
}

int render_preview(const Value &doc, const images::Image *logo, const std::string &path) {
    std::vector<raster::Image> pics = pictures(doc, logo);
    std::string err;
    if (pics.empty() || !files::write(path, pics[0].png(), &err)) throw BoardError{"EXPORT FAILED: " + err};
    return (int)pics.size();
}

}  // namespace board
