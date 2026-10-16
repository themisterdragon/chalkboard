#include "ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ui {

Ui::Ui(plat::Window &w) : win(w) {
    on_close = [this] { quit(); };
    resize();
}

double Ui::now_ms() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Ui::resize() {
    int w = 0, h = 0;
    win.size(w, h);
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w != img.w || h != img.h) img = raster::Image(w, h);
}

// ----------------------------------------------------------------------------- ids

static Id mix_id(Id a, Id b) {
    // FNV-style mixing: different scopes give different ids for the same label
    Id h = a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    return h ? h : 1;
}

Id Ui::id(const std::string &label) {
    Id h = 1469598103934665603ULL;
    for (unsigned char c : label) h = (h ^ c) * 1099511628211ULL;
    return mix_id(ids_.empty() ? 0 : ids_.back(), h);
}
Id Ui::id(const void *p) { return mix_id(ids_.empty() ? 0 : ids_.back(), (Id)(uintptr_t)p * 31 + 7); }
Id Ui::id(Id n) { return mix_id(ids_.empty() ? 0 : ids_.back(), n * 0x100000001b3ULL + 3); }
void Ui::push_id(Id i) { ids_.push_back(i); }
void Ui::pop_id() {
    if (!ids_.empty()) ids_.pop_back();
}

// ----------------------------------------------------------------------------- input

bool Ui::over(const Rect &r) const { return r.has(mx, my) && clip_rect().has(mx, my); }

bool Ui::hit(Id id, const Rect &r, bool focusable, bool enabled) {
    last_ = id;
    last_rect_ = r;
    bool in = over(r);
    if (in && enabled) hot_next_ = id;
    if (focusable && enabled) {
        tab_.push_back(id);
        if (want_first_) {
            focus_ = id;
            want_first_ = false;
            again();
        }
        if (focus_ == id) {
            focus_seen_ = true;
            focus_rect_ = r;
        }
    }
    if (!enabled) return false;
    if (ev.type == plat::Event::DOWN && ev.button == 1 && in) {
        active_ = id;
        if (focusable && focus_ != id) {
            focus_ = id;
            again();
        }
        consume();
        return false;
    }
    if (ev.type == plat::Event::UP && ev.button == 1 && active_ == id) {
        active_ = 0;
        consume();
        return in;
    }
    return false;
}

void Ui::focus(Id id) {
    want_first_ = false;   // asked for: not the first control
    if (focus_ != id) {
        focus_ = id;
        see_focus_ = true;
        again();
    }
}

void Ui::focus_first() {
    want_first_ = true;
    again();
}

bool Ui::key(int k, int mods) {
    if (ev.type != plat::Event::KEY || ev.key != k) return false;
    if (ev.mods != mods) return false;
    consume();
    return true;
}

bool Ui::typed(std::string &text) {
    if (ev.type != plat::Event::TEXT) return false;
    text = ev.text;
    consume();
    return true;
}

// ----------------------------------------------------------------------------- drawing

static double rnd(double v) { return std::round(v); }

void Ui::fill(const Rect &r, Color c) {
    if (!drawing) return;
    double x0 = rnd(r.x), y0 = rnd(r.y), x1 = rnd(r.r()), y1 = rnd(r.b());
    if (x1 > x0 && y1 > y0) img.rect(x0, y0, x1 - x0, y1 - y0, c);
}

void Ui::box(const Rect &r, Color c, double w) {
    if (!drawing) return;
    if (w <= 0) w = S();
    fill({r.x, r.y, r.w, w}, c);
    fill({r.x, r.b() - w, r.w, w}, c);
    fill({r.x, r.y + w, w, r.h - 2 * w}, c);
    fill({r.r() - w, r.y + w, w, r.h - 2 * w}, c);
}

void Ui::hline(double x0, double x1, double y, Color c, double w) {
    if (w <= 0) w = S();
    fill({std::min(x0, x1), y, std::fabs(x1 - x0), w}, c);
}

void Ui::vline(double x, double y0, double y1, Color c, double w) {
    if (w <= 0) w = S();
    fill({x, std::min(y0, y1), w, std::fabs(y1 - y0)}, c);
}

void Ui::line(double x0, double y0, double x1, double y1, Color c, double w, bool round) {
    if (!drawing) return;
    img.stroke({{x0, y0}, {x1, y1}}, w, c, round);
}

void Ui::polyline(const std::vector<raster::Pt> &pts, Color c, double w, bool closed) {
    if (!drawing) return;
    img.stroke(pts, w, c, true, closed);
}

void Ui::polygon(const std::vector<raster::Pt> &pts, Color c) {
    if (!drawing || pts.size() < 3) return;
    raster::Path p;
    p.move(pts[0].x, pts[0].y);
    for (size_t i = 1; i < pts.size(); i++) p.line(pts[i].x, pts[i].y);
    p.close();
    img.fill(p, c);
}

static void round_path(raster::Path &p, const Rect &r, double rad) {
    rad = std::max(0.0, std::min(rad, std::min(r.w, r.h) / 2));
    const double k = 0.5523 * rad;
    double x0 = r.x, y0 = r.y, x1 = r.r(), y1 = r.b();
    p.move(x0 + rad, y0);
    p.line(x1 - rad, y0);
    p.cubic(x1 - rad + k, y0, x1, y0 + rad - k, x1, y0 + rad);
    p.line(x1, y1 - rad);
    p.cubic(x1, y1 - rad + k, x1 - rad + k, y1, x1 - rad, y1);
    p.line(x0 + rad, y1);
    p.cubic(x0 + rad - k, y1, x0, y1 - rad + k, x0, y1 - rad);
    p.line(x0, y0 + rad);
    p.cubic(x0, y0 + rad - k, x0 + rad - k, y0, x0 + rad, y0);
    p.close();
}

void Ui::rrect(const Rect &r, double radius, Color fillc, Color edge, double edge_w, bool has_fill, bool has_edge) {
    if (!drawing || r.w <= 0 || r.h <= 0) return;
    if (has_edge && edge_w > 0) {
        // the edge: the outer shape less the inner one, so it stays sharp
        raster::Path p;
        round_path(p, r, radius);
        Rect in = r.in(edge_w);
        if (in.w > 0 && in.h > 0) {
            raster::Path q;
            round_path(q, in, std::max(0.0, radius - edge_w));
            // the inner contour, the other way round, cuts the hole
            auto ct = q.contours_[0];
            std::reverse(ct.begin(), ct.end());
            p.contours_.push_back(ct);
            if (has_fill) {
                raster::Path f;
                round_path(f, in, std::max(0.0, radius - edge_w));
                img.fill(f, fillc);
            }
        }
        img.fill(p, edge);
    } else if (has_fill) {
        raster::Path p;
        round_path(p, r, radius);
        img.fill(p, fillc);
    }
}

void Ui::rrect_a(const Rect &r, double radius, Color c, double alpha) {
    if (!drawing || r.w <= 0 || r.h <= 0 || alpha <= 0) return;
    raster::Path p;
    round_path(p, r, radius);
    img.fill(p, c, alpha);
}

void Ui::gradient(const Rect &r, double radius, Color top, Color bottom, double alpha) {
    if (!drawing || r.w <= 0 || r.h <= 0) return;
    raster::Path p;
    round_path(p, r, radius);
    raster::Mask m = raster::coverage(p);
    img.mask_gradient(m, 0, 0, top, bottom, r.y, r.b(), alpha);
}

void Ui::shadow(const Rect &r, double radius, double depth, double strength) {
    if (!drawing || depth <= 0) return;
    // rings of fading darkness, a little lower than the box (light from above)
    int steps = std::max(2, (int)std::lround(depth / std::max(1, S())));
    double each = strength / steps;
    for (int i = steps; i >= 1; i--) {
        double d = depth * i / steps;
        Rect o{r.x - d * 0.6, r.y - d * 0.35 + depth * 0.35, r.w + d * 1.2, r.h + d * 1.0};
        rrect_a(o, radius + d, {0, 0, 0}, each * (1.0 - (double)(i - 1) / steps) + each * 0.15);
    }
}

void Ui::oval(const Rect &r, Color fillc, Color edge, double edge_w, bool has_fill, bool has_edge) {
    if (!drawing || r.w <= 0 || r.h <= 0) return;
    raster::Path p;
    p.ellipse(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, r.h / 2);
    if (has_edge && edge_w > 0) {
        raster::Path in;
        in.ellipse(r.x + r.w / 2, r.y + r.h / 2, std::max(0.0, r.w / 2 - edge_w), std::max(0.0, r.h / 2 - edge_w));
        if (has_fill) img.fill(in, fillc);
        auto ct = in.contours_[0];
        std::reverse(ct.begin(), ct.end());
        p.contours_.push_back(ct);
        img.fill(p, edge);
    } else if (has_fill)
        img.fill(p, fillc);
}

void Ui::dotted(const Rect &r, Color c) {
    if (!drawing) return;
    int s = S();
    int x0 = (int)rnd(r.x), y0 = (int)rnd(r.y), x1 = (int)rnd(r.r()) - s, y1 = (int)rnd(r.b()) - s;
    for (int x = x0; x <= x1; x += 2 * s) {
        img.rect(x, y0, s, s, c);
        img.rect(x, y1, s, s, c);
    }
    for (int y = y0; y <= y1; y += 2 * s) {
        img.rect(x0, y, s, s, c);
        img.rect(x1, y, s, s, c);
    }
}

double Ui::text(Font &f, double x, double baseline, const std::string &s, Color c) {
    if (!drawing) return x + f.width(s);
    return f.draw(img, x, baseline, s, c);
}

void Ui::text_in(Font &f, const Rect &r, const std::string &s, Color c, Align a) {
    if (!drawing) return;
    double w = f.width(s);
    double x = a == LEFT ? r.x : a == CENTER ? r.x + (r.w - w) / 2 : r.r() - w;
    double base = std::round(r.y + (r.h - f.line) / 2 + f.ascent);
    f.draw(img, std::round(x), base, s, c);
}

std::string Ui::elide(Font &f, const std::string &s, double w) {
    if (f.width(s) <= w) return s;
    std::string t = s;
    while (!t.empty()) {
        // drop one character (UTF-8 aware)
        size_t i = t.size() - 1;
        while (i > 0 && ((unsigned char)t[i] & 0xC0) == 0x80) i--;
        t.erase(i);
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (f.width(t + "\xE2\x80\xA6") <= w) return t + "\xE2\x80\xA6";
    }
    return "";
}

std::vector<std::string> Ui::wrap(Font &f, const std::string &s, double w) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (para.empty()) out.push_back("");
        std::string cur;
        size_t i = 0;
        while (i < para.size()) {
            // the next word, with the spaces after it
            size_t j = i;
            while (j < para.size() && para[j] != ' ') j++;
            while (j < para.size() && para[j] == ' ') j++;
            std::string word = para.substr(i, j - i);
            std::string trial = cur + word;
            std::string trimmed = trial;
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
            if (cur.empty() || f.width(trimmed) <= w) {
                if (cur.empty() && f.width(trimmed) > w) {
                    // a word longer than the line: break it by characters
                    std::string part;
                    for (size_t k = 0; k < word.size();) {
                        size_t n = 1;
                        unsigned char c = (unsigned char)word[k];
                        if (c >= 0xF0) n = 4;
                        else if (c >= 0xE0) n = 3;
                        else if (c >= 0xC0) n = 2;
                        std::string ch = word.substr(k, n);
                        if (!part.empty() && f.width(part + ch) > w) {
                            out.push_back(part);
                            part.clear();
                        }
                        part += ch;
                        k += n;
                    }
                    cur = part;
                } else
                    cur = trial;
            } else {   // the line is full: this word starts the next one (and is broken there if it's too long)
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                out.push_back(cur);
                cur.clear();
                continue;
            }
            i = j;
        }
        if (!para.empty()) {
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            out.push_back(cur);
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

void Ui::pixel_art(const std::vector<std::string> &rows, const std::function<bool(char, Color &)> &colors, double x, double y, int zoom) {
    if (drawing) sprite(img, rows, colors, x, y, zoom);
}

void Ui::picture(const raster::Image &pic, double x, double y) {
    if (drawing) img.blit(pic, (int)rnd(x), (int)rnd(y));
}

void Ui::clip(const Rect &r) {
    Rect c = r;
    if (!clips_.empty()) {
        const Rect &p = clips_.back();
        double x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y), x1 = std::min(c.r(), p.r()), y1 = std::min(c.b(), p.b());
        c = {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
    }
    clips_.push_back(c);
    img.clip(rnd(c.x), rnd(c.y), rnd(c.r()) - rnd(c.x), rnd(c.b()) - rnd(c.y));
}

void Ui::unclip() {
    if (clips_.empty()) return;
    clips_.pop_back();
    img.unclip();
}

Rect Ui::clip_rect() const { return clips_.empty() ? Rect{0, 0, (double)img.w, (double)img.h} : clips_.back(); }

// ----------------------------------------------------------------------------- layout

void Ui::begin(const Rect &body, double gap) {
    Layout L;
    L.body = body;
    L.y = body.y;
    L.x = body.x;
    L.gap = gap < 0 ? 6 * S() : gap;
    L.max_x = body.x;
    layouts_.push_back(L);
}

static void finish_row(Ui::Layout &L) {
    if (!L.widths.empty() && L.col > 0) {
        L.y = L.row_y + L.row_h + L.gap;
        L.col = 0;
    }
    if (L.flow && L.x > L.body.x) {
        L.y += L.flow_h + L.gap;
        L.x = L.body.x;
        L.flow_h = 0;
    }
}

Rect Ui::end() {
    if (layouts_.empty()) return {};
    Layout L = layouts_.back();
    finish_row(L);
    layouts_.pop_back();
    double used = std::max(0.0, L.y - L.gap - L.body.y);
    return {L.body.x, L.body.y, std::max(0.0, L.max_x - L.body.x), used};
}

void Ui::row(const std::vector<double> &widths, double h) {
    Layout &L = lay();
    finish_row(L);
    L.flow = false;
    L.widths = widths;
    L.fixed_h = h;
    L.col = 0;
}

void Ui::flow(bool on) {
    Layout &L = lay();
    finish_row(L);
    L.widths.clear();
    L.flow = on;
    L.x = L.body.x;
    L.flow_h = 0;
}

void Ui::space(double h) {
    Layout &L = lay();
    finish_row(L);
    L.y += h;
}

Rect Ui::place(double w, double h) {
    Layout &L = lay();
    Rect r;
    if (L.flow) {
        if (w > L.body.w) w = L.body.w;
        if (L.x > L.body.x && L.x + w > L.body.r() + 0.5) {
            L.y += L.flow_h + L.gap;
            L.x = L.body.x;
            L.flow_h = 0;
        }
        r = {std::round(L.x), std::round(L.y), w, h};
        L.x += w + L.gap;
        L.flow_h = std::max(L.flow_h, h);
    } else if (L.widths.empty()) {
        r = {std::round(L.body.x), std::round(L.y), w > 0 ? std::min(w, L.body.w) : L.body.w, h};
        L.y += h + L.gap;
    } else {
        if (L.col == 0) {
            L.row_y = L.y;
            L.row_h = 0;
            L.x = L.body.x;
        }
        double spec = L.widths[L.col];
        double ww;
        if (spec > 0) ww = spec;
        else if (spec == 0) ww = w;
        else {
            double later = 0;
            for (size_t k = L.col + 1; k < L.widths.size(); k++) later += std::max(0.0, L.widths[k]) + L.gap;
            ww = std::max(0.0, L.body.r() - L.x - later + spec);
        }
        double hh = L.fixed_h > 0 ? L.fixed_h : h;
        r = {std::round(L.x), std::round(L.row_y), std::round(ww), hh};
        L.x += ww + L.gap;
        L.row_h = std::max(L.row_h, hh);
        if (++L.col == L.widths.size()) {
            L.col = 0;
            L.y = L.row_y + L.row_h + L.gap;
        }
    }
    L.max_x = std::max(L.max_x, r.r());
    return r;
}

Rect Ui::remaining() const {
    const Layout &L = layouts_.back();
    return {L.body.x, L.y, L.body.w, std::max(0.0, L.body.b() - L.y)};
}

Rect Ui::cut_bottom(double h) {
    Layout &L = lay();
    Rect r{L.body.x, L.body.b() - h, L.body.w, h};
    L.body.h -= h;
    return r;
}

Rect Ui::cut_top(double h) {
    Layout &L = lay();
    finish_row(L);
    Rect r{L.body.x, L.y, L.body.w, h};
    L.y += h;
    return r;
}

Rect Ui::cut_left(double w) {
    Layout &L = lay();
    Rect r{L.body.x, L.y, w, std::max(0.0, L.body.b() - L.y)};
    L.body.x += w;
    L.body.w -= w;
    L.x = L.body.x;
    return r;
}

Rect Ui::cut_right(double w) {
    Layout &L = lay();
    Rect r{L.body.r() - w, L.y, w, std::max(0.0, L.body.b() - L.y)};
    L.body.w -= w;
    return r;
}

double Ui::width() const { return layouts_.empty() ? img.w : layouts_.back().body.w; }

double Ui::ctl_h() const { return skin->line + (skin->soft ? 18 : 8) * S(); }

// ----------------------------------------------------------------------------- scrolling

void Ui::begin_scroll(Id id, const Rect &r, double max_width) {
    Scroll &s = scrolls_[id];
    s.r = r;
    s.view = r.h;
    double sbw = (skin->soft ? 14 : 16) * S();
    bool bar = s.content > s.view + 0.5;
    double inner_w = r.w - (bar ? sbw : 0);
    s.y = std::max(0.0, std::min(s.y, std::max(0.0, s.content - s.view)));
    clip({r.x, r.y, inner_w, r.h});
    double w = max_width > 0 ? std::min(inner_w, max_width) : inner_w;
    begin({r.x, r.y - s.y, w, 1e9}, -1);
    lay().gap = 0;
    scroll_stack_.push_back({id, r, r.y - s.y});
    s.bar = bar ? Rect{r.r() - sbw, r.y, sbw, r.h} : Rect{};
}

void Ui::end_scroll() {
    if (scroll_stack_.empty()) return;
    ScrollFrame fr = scroll_stack_.back();
    scroll_stack_.pop_back();
    Layout L = lay();
    finish_row(L);
    layouts_.pop_back();
    unclip();
    Scroll &s = scrolls_[fr.id];
    double content = std::max(0.0, L.y - fr.start);
    bool had_bar = s.content > s.view + 0.5;
    if (std::fabs(content - s.content) > 0.5) {
        s.content = content;
        if ((s.content > s.view + 0.5) != had_bar) again();
    }
    double maxy = std::max(0.0, s.content - s.view);
    Rect r = fr.r;
    // keep the focused control in view when it moved by keyboard
    if (see_focus_ && focus_seen_ && focus_rect_.y >= fr.start - 1 && focus_rect_.b() <= fr.start + s.content + 1) {
        double pad = 8 * S();
        if (focus_rect_.y < r.y) s.y -= r.y - focus_rect_.y + pad;
        else if (focus_rect_.b() > r.b()) s.y += focus_rect_.b() - r.b() + pad;
        s.y = std::max(0.0, std::min(s.y, maxy));
        again();
    }
    // the mouse wheel
    if (ev.type == plat::Event::WHEEL && over(r) && maxy > 0) {
        double before = s.y;
        s.y = std::max(0.0, std::min(maxy, s.y + ev.dy * skin->line * 3));
        if (s.y != before) {
            consume();
            again();
        }
    }
    if (maxy <= 0) {
        s.y = 0;
        return;
    }
    // the scroll bar
    Skin &k = *skin;
    Rect bar = s.bar;
    if (bar.w <= 0) return;
    Id bid = id(fr.id + 77);
    double arrow = k.soft ? 0 : bar.w;
    Rect track{bar.x, bar.y + arrow, bar.w, bar.h - 2 * arrow};
    double th = std::max(20.0 * S(), track.h * s.view / std::max(1.0, s.content));
    double ty = track.y + (track.h - th) * (s.y / maxy);
    Rect thumb{bar.x, ty, bar.w, th};
    bool in = over(bar);
    if (ev.type == plat::Event::DOWN && ev.button == 1 && in) {
        active_ = bid;
        consume();
        if (thumb.has(mx, my)) s.grab = my - ty;
        else if (!k.soft && my < track.y) s.y = std::max(0.0, s.y - k.line * 3), s.grab = -1;
        else if (!k.soft && my >= track.b()) s.y = std::min(maxy, s.y + k.line * 3), s.grab = -1;
        else {   // a page at a time toward the click
            s.y = my < ty ? std::max(0.0, s.y - s.view * 0.9) : std::min(maxy, s.y + s.view * 0.9);
            s.grab = -1;
        }
        again();
    } else if (ev.type == plat::Event::MOVE && active_ == bid && s.grab >= 0) {
        double t = (my - s.grab - track.y) / std::max(1.0, track.h - th);
        s.y = std::max(0.0, std::min(maxy, t * maxy));
        again();
    } else if (ev.type == plat::Event::UP && active_ == bid) {
        active_ = 0;
        consume();
    }
    if (!drawing) return;
    fill(bar, k["trough"]);
    if (k.soft) {
        Rect t = thumb.in(3 * S());
        bool hot = (active_ == bid) || thumb.has(mx, my);
        rrect(t, t.w / 2, hot ? k["text"] : k["thumb"], k["thumb"], 0, true, false);
        return;
    }
    // the retro bar: arrows at both ends and a raised thumb
    auto raised = [&](const Rect &b) {
        fill(b, k["thumb"]);
        hline(b.x, b.r(), b.y, k["light"]);
        vline(b.x, b.y, b.b(), k["light"]);
        hline(b.x, b.r(), b.b() - S(), k["shadow"]);
        vline(b.r() - S(), b.y, b.b(), k["shadow"]);
        box(b, k["dark"], S());
    };
    vline(bar.x, bar.y, bar.b(), k["dark"]);
    Rect up{bar.x, bar.y, bar.w, arrow}, dn{bar.x, bar.b() - arrow, bar.w, arrow};
    raised(up);
    raised(dn);
    double a = std::max(3.0 * S(), bar.w / 5), cx = bar.x + bar.w / 2;
    polygon({{cx - a, up.y + up.h / 2 + a / 2}, {cx + a, up.y + up.h / 2 + a / 2}, {cx, up.y + up.h / 2 - a / 2 - S()}}, k["text"]);
    polygon({{cx - a, dn.y + dn.h / 2 - a / 2}, {cx + a, dn.y + dn.h / 2 - a / 2}, {cx, dn.y + dn.h / 2 + a / 2 + S()}}, k["text"]);
    raised(thumb);
}

// ----------------------------------------------------------------------------- running

int Ui::after(int ms, std::function<void()> fn) {
    int id = next_timer_++;
    timers_.push_back({id, now_ms() + ms, std::move(fn)});
    return id;
}

void Ui::cancel(int id) {
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer &t) { return t.id == id; }), timers_.end());
}

int Ui::wait_ms() const {
    if (timers_.empty()) return -1;
    double soonest = 1e300;
    for (const auto &t : timers_) soonest = std::min(soonest, t.at);
    return std::max(0, (int)std::ceil(soonest - now_ms()));
}

void Ui::fire_timers() {
    double now = now_ms();
    std::vector<Timer> due;
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->at <= now) {
            due.push_back(std::move(*it));
            it = timers_.erase(it);
        } else
            ++it;
    }
    std::sort(due.begin(), due.end(), [](const Timer &a, const Timer &b) { return a.at < b.at; });
    for (auto &t : due) {
        if (quitting_) break;
        t.fn();
        again();
    }
}

void Ui::pass(const std::function<void()> &frame, bool draw) {
    drawing = draw;
    pass_++;
    tab_.clear();
    hot_next_ = 0;
    focus_seen_ = false;
    cursor_ = plat::ARROW;
    ids_.clear();
    layouts_.clear();
    while (!clips_.empty()) unclip();
    scroll_stack_.clear();
    frame();
    while (!clips_.empty()) unclip();
    after_pass();
    drawing = false;
}

void Ui::after_pass() {
    hot_ = hot_next_;
    // Tab and Shift+Tab move the focus along the controls, in the order they were placed
    if (ev.type == plat::Event::KEY && ev.key == plat::K_TAB && !(ev.mods & (plat::CTRL | plat::ALT | plat::CMD)) && !tab_.empty()) {
        auto it = std::find(tab_.begin(), tab_.end(), focus_);
        bool back = ev.mods & plat::SHIFT;
        if (it == tab_.end()) focus_ = back ? tab_.back() : tab_.front();
        else if (back) focus_ = it == tab_.begin() ? tab_.back() : *(it - 1);
        else focus_ = (it + 1 == tab_.end()) ? tab_.front() : *(it + 1);
        see_focus_ = true;
        consume();
        again();
    } else if (want_first_ && !tab_.empty()) {
        focus_ = tab_.front();
        want_first_ = false;
        again();
    }
    if (ev.type == plat::Event::UP && ev.button == 1) active_ = 0;
    if (ev.type == plat::Event::KEY && on_key) {
        if (on_key(ev)) consume();
    }
    // a focused control that wasn't placed this time (its screen changed) lets go of the focus
    if (!focus_seen_ && focus_ && !again_ && !want_first_ && std::find(tab_.begin(), tab_.end(), focus_) == tab_.end()) focus_ = 0;
    // forget what controls no longer shown kept (after a while)
    if (pass_ % 500 == 0)
        for (auto it = states_.begin(); it != states_.end();)
            it = pass_ - it->second.first > 2000 ? states_.erase(it) : std::next(it);
    win.cursor(cursor_);
}

bool Ui::next_event(int timeout) {
    plat::Event e;
    if (!injected_.empty()) {
        e = injected_.front();
        injected_.erase(injected_.begin());
    } else if (!win.next(e, timeout))
        return false;
    ev = e;
    switch (e.type) {
    case plat::Event::MOVE:
    case plat::Event::DOWN:
    case plat::Event::UP:
    case plat::Event::WHEEL:
        mx = e.x;
        my = e.y;
        break;
    case plat::Event::LEAVE:
        if (!active_) mx = my = -1;
        break;
    case plat::Event::RESIZE:
        resize();
        again();
        break;
    case plat::Event::CLOSE:
        ev.type = plat::Event::NONE;
        if (on_close) on_close();
        again();
        break;
    case plat::Event::FOCUS_OUT:
        active_ = 0;
        again();
        break;
    case plat::Event::MENU:
        ev.type = plat::Event::NONE;
        if (on_menu) on_menu(e.menu);
        again();
        break;
    default:
        break;
    }
    return true;
}

void Ui::run(const std::function<void()> &frame) {
    resize();
    auto draw = [&] {
        for (int i = 0; i < 4; i++) {
            again_ = false;
            ev = plat::Event();
            pass(frame, true);
            if (!again_) break;
        }
        win.present(img);
    };
    draw();
    while (!quitting_) {
        bool got = next_event(wait_ms());
        fire_timers();
        if (quitting_) break;
        if (got) {
            pass(frame, false);
            // take whatever else is waiting before drawing (a burst of mouse moves)
            plat::Event e;
            int n = 0;
            while (!quitting_ && n++ < 200 && next_event(0)) {
                pass(frame, false);
                for (int i = 0; i < 3 && again_; i++) {
                    again_ = false;
                    ev = plat::Event();
                    pass(frame, false);
                }
            }
        }
        for (int i = 0; i < 3 && again_ && !quitting_; i++) {
            again_ = false;
            ev = plat::Event();
            pass(frame, false);
        }
        if (!quitting_) draw();
    }
}

void Ui::modal(const std::function<void(bool &done)> &body) {
    raster::Image under = img;
    // everything the pass that opened the dialog is in the middle of, to carry on with afterward
    auto ids = ids_;
    auto layouts = layouts_;
    auto clips = clips_;
    auto scroll_stack = scroll_stack_;
    auto tab = tab_;
    Id hot_next = hot_next_, last = last_;
    Rect last_rect = last_rect_, focus_rect = focus_rect_;
    bool focus_seen = focus_seen_, was_drawing = drawing;
    plat::Cursor cur = cursor_;
    // and what the window behind keeps for when the dialog closes
    Id old_focus = focus_, old_active = active_, old_hot = hot_;
    bool old_want = want_first_;
    plat::Event old_ev = ev;
    focus_ = 0;
    active_ = 0;
    hot_ = 0;
    want_first_ = true;
    modal_depth_++;
    bool done = false;
    auto frame = [&] {
        if (drawing) {
            if (under.w != img.w || under.h != img.h) fill({0, 0, (double)img.w, (double)img.h}, skin->c["desk"]);
            img.blit(under, 0, 0);
        }
        body(done);
    };
    auto draw = [&] {
        for (int i = 0; i < 4 && !done; i++) {
            again_ = false;
            ev = plat::Event();
            pass(frame, true);
            if (!again_) break;
        }
        if (!done) win.present(img);
    };
    draw();
    while (!done && !quitting_) {
        bool got = next_event(wait_ms());
        fire_timers();
        if (done || quitting_) break;
        if (got) {
            pass(frame, false);
            int n = 0;
            while (!done && !quitting_ && n++ < 200 && next_event(0)) pass(frame, false);
        }
        for (int i = 0; i < 3 && again_ && !done && !quitting_; i++) {
            again_ = false;
            ev = plat::Event();
            pass(frame, false);
        }
        if (!done && !quitting_) draw();
    }
    modal_depth_--;
    focus_ = old_focus;
    active_ = 0;   // the press that opened it ended inside the dialog
    (void)old_active;
    hot_ = old_hot;
    want_first_ = old_want;
    ev = old_ev;
    ev.type = plat::Event::NONE;   // what opened the dialog is used up
    ids_ = ids;
    layouts_ = layouts;
    scroll_stack_ = scroll_stack;
    tab_ = tab;
    hot_next_ = hot_next;
    last_ = last;
    last_rect_ = last_rect;
    focus_rect_ = focus_rect;
    focus_seen_ = focus_seen;
    drawing = was_drawing;
    cursor_ = cur;
    while (!clips_.empty()) unclip();
    for (const auto &c : clips) {   // the clip boxes again, already cut to each other
        clips_.push_back(c);
        img.clip(std::round(c.x), std::round(c.y), std::round(c.r()) - std::round(c.x), std::round(c.b()) - std::round(c.y));
    }
    again();
}

}  // namespace ui
