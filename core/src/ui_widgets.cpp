// The controls (port of gui/widgets.py): buttons, check boxes, radio buttons, drop-downs, group
// boxes, lists, menus, title bars, and dialogs, drawn the same in every look on every OS. Every
// control works from the keyboard and shows a clear focus ring when it has the focus.
#include <algorithm>
#include <cmath>

#include "ui.h"

namespace ui {

namespace {

// a raised (or pressed) 3-D box: Bevel's buttons
void bevel_box(Ui &ui, const Rect &r, bool pressed, int t = 0) {
    Skin &k = ui.sk();
    int S = ui.S();
    if (!t) t = 2 * S;
    Color hi = pressed ? k["shadow"] : k["light"], lo = pressed ? k["face"] : k["shadow"];
    ui.fill(r, k["face"]);
    double x0 = r.x, y0 = r.y, x1 = r.r() - 1, y1 = r.b() - 1;
    for (int i = 0; i < (pressed ? S : t); i++) {
        ui.fill({x0 + i, y0 + i, x1 - x0 - 2 * i, 1}, hi);       // top
        ui.fill({x0 + i, y0 + i, 1, y1 - y0 - 2 * i + 1}, hi);   // left
        if (!pressed) {
            ui.fill({x0 + i, y1 - i, x1 - x0 - 2 * i + 1, 1}, lo);   // bottom
            ui.fill({x1 - i, y0 + i, 1, y1 - y0 - 2 * i + 1}, lo);   // right
        }
    }
}

// the keyboard focus mark: a solid ring in Modern, the classic dotted box in the retro looks
void focus_ring(Ui &ui, const Rect &r, bool round_ = false) {
    Skin &k = ui.sk();
    int S = ui.S();
    if (k.soft) {
        Rect o{r.x - 3 * S, r.y - 3 * S, r.w + 6 * S, r.h + 6 * S};
        if (round_) ui.oval(o, {}, k["focus"], 2 * S, false, true);
        else ui.rrect(o, 7 * S, {}, k["focus"], 2 * S, false, true);
    } else
        ui.dotted(r, k["focus"]);
}

Font &button_font(Skin &k, bool small) { return small ? *k.fsmall : (k.soft ? *k.f : *k.fb); }

}  // namespace

double Ui::btn_w(const std::string &label, bool small) {
    Skin &k = sk();
    Font &f = button_font(k, small);
    double w = f.width(label) + (small ? 16 : 26) * S();
    if (k.soft) w += 6 * S();
    return std::ceil(w);
}

// ----------------------------------------------------------------------------- labels

void label(Ui &ui, const std::string &text, bool bold, bool dim, bool small) {
    Skin &k = ui.sk();
    Font &f = small ? *k.fsmall : bold ? *k.fb : *k.f;
    Rect r = ui.place(std::ceil(f.width(text)) + 1, f.line);
    ui.text_in(f, r, text, dim ? k["dim"] : k["text"]);
}

double para(Ui &ui, const std::string &text, bool dim, double wrap, Font *font) {
    Skin &k = ui.sk();
    Font &f = font ? *font : *k.f;
    double w = ui.width();
    if (wrap > 0) w = std::min(w, wrap);
    auto lines = ui.wrap(f, text, w);
    double widest = 0;
    for (const auto &l : lines) widest = std::max(widest, f.width(l));
    Rect r = ui.place(std::ceil(widest) + 1, f.line * (double)lines.size());
    double y = r.y;
    for (const auto &l : lines) {
        ui.text(f, r.x, std::round(y + f.ascent), l, dim ? k["dim"] : k["text"]);
        y += f.line;
    }
    return r.h;
}

// ----------------------------------------------------------------------------- buttons

bool button_at(Ui &ui, Id id, const Rect &r, const std::string &text, bool enabled, bool is_default, bool small, bool latched, Font *font,
               bool takes_focus) {
    Skin &k = ui.sk();
    int S = ui.S();
    Font &f = font ? *font : button_font(k, small);
    bool clicked = ui.hit(id, r, takes_focus, enabled);
    bool focus = enabled && ui.focused(id);
    if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) clicked = true;
    if (focus && ui.has(plat::Event::TEXT) && ui.ev.text == " ") {
        ui.consume();
        clicked = true;
    }
    bool down = (ui.held(id) && ui.over(r)) || latched;
    if (!ui.drawing) return clicked;
    Color fg = enabled ? k["text"] : k["off"];
    double w = r.w, h = r.h;
    double tx = r.x + w / 2, ty = r.y + h / 2;
    if (k.soft) {
        double m = 3 * S;
        Rect in{r.x + m, r.y + m, w - 2 * m, h - 2 * m};
        double rad = k.glossy ? in.h / 2 : 8 * S;
        bool inside = ui.over(r);
        Color tcol;
        if (focus) ui.rrect({r.x, r.y, w, h}, rad + 3 * S, {}, k["focus"], 2 * S, false, true);
        if (k.glossy) {
            // a gel: shaded top to bottom, a shine on its upper half, a darker rim
            Color top = is_default ? k["gel_top"] : k["btn_top"], bot = is_default ? k["gel_bot"] : k["btn_bot"];
            if (!enabled) top = bot = mix(k["window"], k["btn_bot"], 0.5);
            else if (down) std::swap(top, bot);
            else if (inside) top = mix(top, {1, 1, 1}, 0.35), bot = mix(bot, {1, 1, 1}, 0.12);
            Color rim = enabled ? mix(bot, {0, 0, 0}, k.dark ? 0.35 : 0.3) : k["shadow"];
            ui.rrect(in, rad, rim, rim, 0, true, false);
            ui.gradient(in.in(S), rad - S, top, bot);
            if (enabled && !down) ui.rrect_a({in.x + 3 * S, in.y + 2 * S, in.w - 6 * S, in.h * 0.45}, rad - 2 * S, {1, 1, 1}, k.dark ? 0.16 : 0.45);
            tcol = !enabled ? k["off"] : is_default ? k["gel_text"] : k["text"];
        } else {
            Color fill;
            if (!enabled) {
                fill = mix(k["window"], k["face"], 0.5);
                tcol = k["off"];
            } else if (is_default) {
                fill = k["sel"];
                if (down || inside) fill = mix(fill, {0, 0, 0}, down ? 0.2 : 0.1);
                tcol = k["seltext"];
            } else {
                fill = k["face"];
                if (down || inside) fill = mix(fill, k["text"], down ? 0.14 : 0.06);
                tcol = k["text"];
            }
            ui.rrect(in, rad, fill, fill, 0, true, false);
        }
        Font &tf = is_default && !font && !small ? *k.fb : f;
        ui.text_in(tf, r, text, tcol, CENTER);
        return clicked;
    }
    if (k.bevel) {
        int edge = is_default ? 2 * S : S;
        ui.fill(r, k["dark"]);
        for (double cx : {r.x, r.r() - S})   // the corners, rounded off like the old buttons
            for (double cy : {r.y, r.b() - S}) ui.fill({cx, cy, (double)S, (double)S}, k["window"]);
        bevel_box(ui, r.in(edge), down);
        double off = down ? S : 0;
        Rect tr{r.x + off, r.y + off, w, h};
        if (!enabled) ui.text_in(f, {tr.x + S, tr.y + S, w, h}, text, k["light"], CENTER);
        ui.text_in(f, tr, text, fg, CENTER);
        tx += off;
        ty += off;
    } else {
        double pad = is_default ? 3 * S : 0;
        if (is_default) ui.rrect({r.x + S, r.y + S, w - 2 * S, h - 2 * S}, 9 * S, k["face"], k["dark"], 3 * S);
        Rect in{r.x + pad + S, r.y + pad + S, w - 2 * pad - 2 * S, h - 2 * pad - 2 * S};
        ui.rrect(in, 6 * S, down ? k["dark"] : k["face"], enabled ? k["dark"] : k["off"], S);
        ui.text_in(f, r, text, down ? k["light"] : fg, CENTER);
    }
    if (focus) {
        double tw = f.width(text) / 2 + 3 * S, th = std::floor(f.line / 2) + S;
        focus_ring(ui, {std::round(tx - tw), std::round(ty - th), std::round(2 * tw), std::round(2 * th)});
    }
    return clicked;
}

bool button(Ui &ui, const std::string &text, bool enabled, bool is_default, bool small, double min_w) {
    Skin &k = ui.sk();
    int S = ui.S();
    Font &f = button_font(k, small);
    double h = f.line + (small ? 8 : 12) * S;
    double w = std::max(min_w * S, std::ceil(f.width(text)) + (small ? 16 : 26) * S);
    if (k.soft || (is_default && !k.bevel)) {
        w += 6 * S;
        h += 6 * S;
    }
    Rect r = ui.place(w, h);
    if (r.w > w) r.w = w;   // a button keeps its own width in a column
    return button_at(ui, ui.id("btn:" + text), r, text, enabled, is_default, small);
}

// ----------------------------------------------------------------------------- check boxes and radio buttons

static bool toggle(Ui &ui, const std::string &text, bool on, bool radio_, bool enabled, double wrap) {
    Skin &k = ui.sk();
    int S = ui.S();
    double b = k.line - 2 * S;
    double x0 = k.soft ? 4 * S : 0;
    double tw = k.f->width(text);
    std::vector<std::string> lines = {text};
    if (wrap > 0 && tw > wrap) {
        lines = ui.wrap(*k.f, text, wrap);
        tw = 0;
        for (const auto &l : lines) tw = std::max(tw, k.f->width(l));
    }
    double h = std::max(b, lines.size() * k.line) + (k.soft ? 8 : 6) * S;
    Rect r = ui.place(std::ceil(x0 + b + 8 * S + tw + 4 * S), h);
    r.w = std::min(r.w, std::ceil(x0 + b + 8 * S + tw + 4 * S));
    Id id = ui.id((radio_ ? "radio:" : "check:") + text);
    bool clicked = ui.hit(id, r, true, enabled);
    bool focus = enabled && ui.focused(id);
    if (focus && ui.key(' ')) clicked = true;
    if (focus && ui.has(plat::Event::TEXT) && ui.ev.text == " ") {
        ui.consume();
        clicked = true;
    }
    if (!ui.drawing) return clicked;
    double y0 = r.y + (k.soft ? 4 : 3) * S, x = r.x + x0;
    Rect ib{x, y0, b, b};
    if (radio_) {
        if (k.soft) {
            ui.oval(ib, on ? k["sel"] : k["field"], on ? k["sel"] : k["edge"], S);
            if (on) {
                double m = std::floor(b / 3);
                ui.oval(ib.in(m), k["seltext"], {}, 0, true, false);
            }
        } else {
            ui.oval(ib, k["field"], k["edge"], S);
            if (on) {
                double m = std::floor(b / 4) + S;
                ui.oval(ib.in(m), k["text"], {}, 0, true, false);
            }
        }
    } else if (k.soft) {
        Color fill = on && enabled ? k["sel"] : k["field"];
        ui.rrect(ib, 4 * S, fill, on ? k["sel"] : k["edge"], S);
        if (on)
            ui.polyline({{x + b * 0.24, y0 + b * 0.52}, {x + b * 0.43, y0 + b * 0.70}, {x + b * 0.78, y0 + b * 0.30}}, enabled ? k["seltext"] : k["off"],
                        2 * S);
    } else {
        ui.fill(ib, k["field"]);
        ui.box(ib, k["edge"], S);
        if (on) {
            double m = 3 * S;
            ui.line(x + m, y0 + m, x + b - m + S, y0 + b - m + S, k["text"], S + 1, false);
            ui.line(x + m, y0 + b - m, x + b - m + S, y0 + m - S, k["text"], S + 1, false);
        }
    }
    Color fg = enabled ? k["text"] : k["off"];
    double ty = y0 + b / 2 - k.line / 2;
    double tx = x + b + 6 * S;
    for (size_t i = 0; i < lines.size(); i++) ui.text(*k.f, tx, std::round(ty + i * k.line + k.f->ascent), lines[i], fg);
    if (focus) {
        if (k.soft) focus_ring(ui, {x + S, y0 + S, b - 2 * S, b - 2 * S}, radio_);
        else focus_ring(ui, {tx - S, std::round(ty), std::ceil(tw) + 2 * S, lines.size() * k.line});
    }
    return clicked;
}

bool check(Ui &ui, const std::string &text, bool &value, bool enabled, double wrap) {
    if (toggle(ui, text, value, false, enabled, wrap)) {
        value = !value;
        return true;
    }
    return false;
}

bool radio(Ui &ui, const std::string &text, bool on, bool enabled, double wrap) { return toggle(ui, text, on, true, enabled, wrap); }

// ----------------------------------------------------------------------------- drop-downs

bool dropdown(Ui &ui, const std::string &key, const std::vector<std::pair<std::string, std::string>> &options, std::string &value, double chars,
              bool enabled) {
    Skin &k = ui.sk();
    int S = ui.S();
    if (chars <= 0) {
        chars = 8;
        for (const auto &o : options) chars = std::max(chars, (double)o.second.size());
    }
    double w = std::min(k.f->width("0") * chars, 520.0 * S) + k.line + 16 * S, h = k.line + 8 * S;
    if (k.soft) {
        w += 6 * S;
        h += 10 * S;
    }
    Rect r = ui.place(std::ceil(w), h);
    // no wider than the room it has (a long option in a narrow column)
    double room_r = ui.lay().body.r();
    if (r.r() > room_r && room_r - r.x > 80 * S) r.w = room_r - r.x;
    w = r.w;
    Id id = ui.id("dd:" + key);
    bool open = ui.hit(id, r, true, enabled && !options.empty());
    bool focus = enabled && ui.focused(id);
    bool changed = false;
    auto pick = [&](size_t i) {
        if (i < options.size() && options[i].first != value) {
            value = options[i].first;
            changed = true;
        }
    };
    size_t cur = options.size();
    for (size_t i = 0; i < options.size(); i++)
        if (options[i].first == value) cur = i;
    if (focus) {
        if (ui.key(' ') || ui.key(plat::K_RETURN) || ui.key(plat::K_DOWN, plat::ALT)) open = true;
        else if (ui.has(plat::Event::TEXT) && ui.ev.text == " ") {
            ui.consume();
            open = true;
        } else if (ui.key(plat::K_DOWN)) pick(cur == options.size() ? 0 : std::min(options.size() - 1, cur + 1));
        else if (ui.key(plat::K_UP)) pick(cur == options.size() ? 0 : (cur ? cur - 1 : 0));
    }
    if (open && enabled && !options.empty()) {
        std::vector<MenuEntry> items;
        for (const auto &o : options) {
            MenuEntry e;
            e.label = o.second;
            e.checked = o.first == value;
            e.radio = true;
            items.push_back(e);
        }
        ui.focus(id);
        int got = popup_menu(ui, r.x, r.b(), items, r.w);
        if (got >= 0) pick((size_t)got);
        ui.again();
    }
    if (!ui.drawing) return changed;
    Color fg = enabled ? k["text"] : k["off"];
    std::string label = cur < options.size() ? options[cur].second : value;
    if (k.soft) {
        double m = 3 * S;
        if (focus) ui.rrect(r, 11 * S, {}, k["focus"], 2 * S, false, true);
        if (k.glossy) {
            Rect in = r.in(m);
            ui.rrect(in, 8 * S, k["edge"], k["edge"], 0, true, false);
            ui.gradient(in.in(S), 7 * S, k["btn_top"], k["btn_bot"]);
        } else
            ui.rrect(r.in(m), 8 * S, k["field"], k["edge"], S);
        double ax = r.r() - m - (h - 2 * m) / 2, ay = r.y + h / 2, a = std::max(3.0 * S, std::floor(h / 8));
        ui.polyline({{ax - a, ay - a / 2}, {ax, ay + a / 2}, {ax + a, ay - a / 2}}, fg, 2 * S);
        double room = w - (h - 2 * m) - 10 * S - m;
        ui.text_in(*k.f, {r.x + m + 8 * S, r.y, room, h}, ui.elide(*k.f, label, room), fg);
        return changed;
    }
    double bw = h, tx;
    if (k.bevel) {
        ui.fill({r.x, r.y, r.w - bw, h}, k["field"]);
        ui.box({r.x, r.y, r.w - bw + S, h}, k["edge"], S);
        ui.fill({r.r() - bw, r.y, bw, h}, k["dark"]);
        bevel_box(ui, {r.r() - bw + S, r.y + S, bw - 2 * S, h - 2 * S}, false);
        double ax = r.r() - bw / 2, ay = r.y + h / 2, a = std::max(3.0 * S, std::floor(h / 6));
        ui.polygon({{ax - a, ay - a / 2}, {ax + a, ay - a / 2}, {ax, ay + a / 2 + S}}, fg);
        tx = r.x + 5 * S;
    } else {
        ui.fill({r.x + S, r.y + S, r.w - S, h - S}, k["edge"]);
        ui.fill({r.x, r.y, r.w - S, h - S}, k["field"]);
        ui.box({r.x, r.y, r.w - S, h - S}, k["edge"], S);
        double ax = r.r() - h / 2 - S, ay = r.y + h / 2, a = std::max(3.0 * S, std::floor(h / 6));
        ui.polygon({{ax - a, ay - a / 2}, {ax + a, ay - a / 2}, {ax, ay + a / 2 + S}}, fg);
        tx = r.x + 6 * S;
    }
    double room = r.w - bw - (tx - r.x) - 4 * S;
    std::string shown = ui.elide(*k.f, label, room);
    Rect tr{tx, r.y, room, h};
    if (focus) {
        double tw = k.f->width(shown);
        double th = k.f->line;
        ui.fill({tx - 2 * S, r.y + (h - th) / 2, tw + 4 * S, th}, k["sel"]);
        ui.text_in(*k.f, tr, shown, k["seltext"]);
    } else
        ui.text_in(*k.f, tr, shown, fg);
    return changed;
}

// ----------------------------------------------------------------------------- groups, separators, progress

namespace {
struct GroupState {
    double h = 0;
};
struct GroupFrame {
    Id id;
    Rect r;
    double top;
};
std::vector<GroupFrame> g_groups;
}  // namespace

void begin_group(Ui &ui, const std::string &title) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("group:" + title);
    GroupState &st = ui.state<GroupState>(id);
    Font &f = *k.fb;
    Rect r = ui.place(0, std::max(st.h, f.line));
    // the box first (its height is from last time), the contents over it
    double padx = k.soft ? 18 * S : 8 * S, pady = k.soft ? 14 * S : 6 * S;
    if (k.soft) {
        // a card: rounded, a hairline edge, a soft shadow; its heading inside
        ui.shadow(r, 12 * S, 6 * S, k.dark ? 0.35 : 0.07);
        ui.rrect(r, 12 * S, k["card"], k["line"], S);
        ui.text(*k.fhead, r.x + S + padx, std::round(r.y + S + pady + k.fhead->ascent), title, k["text"]);
    } else {
        double bd = k.bevel ? 2 * S : S;
        double ty = r.y + f.line / 2;
        Rect fr{r.x, std::round(ty - bd / 2), r.w, r.b() - std::round(ty - bd / 2)};
        if (k.bevel) {   // an etched (groove) line
            ui.box(fr, k["shadow"], S);
            ui.box({fr.x + S, fr.y + S, fr.w - S, fr.h - S}, k["light"], S);
            ui.fill({fr.x + S, fr.y + S, fr.w - 2 * S, (double)S}, k["light"]);
            ui.box({fr.x, fr.y, fr.w - S, fr.h - S}, k["shadow"], S);
        } else
            ui.box(fr, k["dark"], S);
        std::string t = " " + title + " ";
        double tw = f.width(t);
        ui.fill({r.x + 7 * S, r.y, tw, f.line}, k["window"]);
        ui.text(f, r.x + 7 * S, std::round(r.y + f.ascent), t, k["text"]);
    }
    double top = k.soft ? r.y + S + pady + k.fhead->line + 8 * S : r.y + f.line + pady;
    ui.begin({r.x + (k.soft ? S : 2 * S) + padx, top, r.w - 2 * ((k.soft ? S : 2 * S) + padx), 1e9}, -1);
    g_groups.push_back({id, r, top});
}

void end_group(Ui &ui) {
    if (g_groups.empty()) return;
    GroupFrame g = g_groups.back();
    g_groups.pop_back();
    Rect used = ui.end();
    Skin &k = ui.sk();
    int S = ui.S();
    double pady = k.soft ? 16 * S : 6 * S;
    double h = (g.top - g.r.y) + used.h + pady + (k.soft ? S : 2 * S);
    GroupState &st = ui.state<GroupState>(g.id);
    if (std::fabs(st.h - h) > 0.5) {
        // a different height than last time: what's below moves, so lay out again
        Ui::Layout &L = ui.lay();
        L.y += h - std::max(st.h, k.fb->line);
        st.h = h;
        ui.again();
    }
}

void separator(Ui &ui) {
    Skin &k = ui.sk();
    Rect r = ui.place(0, ui.S());
    ui.fill(r, k.soft ? k["line"] : k["dark"]);
}

void progress(Ui &ui, const Rect &r, double frac) {
    Skin &k = ui.sk();
    Color fill = fit(k["sel"], {k["trough"]}, 3.0);   // 3:1 from its track (WCAG 1.4.11), in every look
    if (k.soft) {
        ui.rrect(r, r.h / 2, k["trough"], k["edge"], ui.S());
        double w = std::round(r.w * std::max(0.0, std::min(1.0, frac)));
        if (w >= r.h) {
            if (k.glossy) ui.gradient({r.x, r.y, w, r.h}, r.h / 2, k["gel_top"], k["gel_bot"]);
            else ui.rrect({r.x, r.y, w, r.h}, r.h / 2, fill, fill, 0, true, false);
        }
        return;
    }
    ui.fill(r, k["trough"]);
    ui.fill({r.x, r.y, std::round(r.w * std::max(0.0, std::min(1.0, frac))), r.h}, fill);
    ui.box(r, k["edge"], ui.S());
}

// ----------------------------------------------------------------------------- toolbars

// One row of buttons. Buttons that don't fit go into a "More" menu at the end instead of wrapping
// onto a second row; a pick from that menu comes back from tool() on the next pass.
namespace {
struct ToolbarState {
    std::string picked;   // picked from More last time: tool() with this label returns true
};
struct ToolbarFrame {
    Id id;
    Rect r;
    std::vector<std::pair<std::string, bool>> spill;   // (label, enabled) that didn't fit
    double reserve = 0;                                // room kept for the More button
};
std::vector<ToolbarFrame> g_toolbars;
}  // namespace

void begin_toolbar(Ui &ui) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("toolbar");
    double pad = k.soft ? 10 : 6;
    Rect r = ui.cut_top(ui.ctl_h() + (k.soft ? 6 : 0) * S + 2 * pad * S);
    if (!k.soft) ui.fill(r, k["window"]);
    ui.begin({r.x + (k.soft ? 0 : 8) * S, r.y + pad * S, r.w - (k.soft ? 0 : 16) * S, 1e9}, 6 * S);
    ui.flow(true);
    g_toolbars.push_back({id, r, {}, ui.btn_w("More") + 34 * S});
}

bool tool(Ui &ui, const std::string &text, bool enabled, bool primary) {
    if (g_toolbars.empty()) return button(ui, text, enabled, primary);
    ToolbarFrame &tb = g_toolbars.back();
    Ui::Layout &L = ui.lay();
    Skin &k = ui.sk();
    double w = ui.btn_w(text) + (k.soft ? 6 * ui.S() : 0);
    if (!tb.spill.empty() || L.x + w > L.body.r() - tb.reserve) {
        tb.spill.push_back({text, enabled});
        ToolbarState &st = ui.state<ToolbarState>(tb.id);
        if (enabled && st.picked == text) {
            st.picked.clear();
            return true;
        }
        return false;
    }
    return button(ui, text, enabled, primary);
}

bool toolbar_room(Ui &ui, double w) {
    if (g_toolbars.empty()) return true;
    ToolbarFrame &tb = g_toolbars.back();
    Ui::Layout &L = ui.lay();
    if (!tb.spill.empty() || L.x + w > L.body.r() - tb.reserve) {
        tb.spill.push_back({"", false});   // what comes after goes in More too (a line in the menu)
        return false;
    }
    return true;
}

void tool_gap(Ui &ui) {
    if (!g_toolbars.empty() && !g_toolbars.back().spill.empty()) return;
    ui.place((ui.sk().soft ? 10 : 4) * ui.S(), 1);
}

void end_toolbar(Ui &ui) {
    if (g_toolbars.empty()) return;
    ToolbarFrame tb = g_toolbars.back();
    g_toolbars.pop_back();
    if (!tb.spill.empty()) {
        Skin &k = ui.sk();
        int S = ui.S();
        Id more = ui.id("toolbar-more");
        ui.place(4 * S, 1);
        // the same size as the toolbar's other buttons, with a drawn down-arrow
        Font &f = k.soft ? *k.f : *k.fb;
        double h = f.line + 12 * S + (k.soft ? 6 * S : 0);
        Rect r = ui.place(ui.btn_w("More") + 14 * S + (k.soft ? 6 * S : 0), h);
        if (button_at(ui, more, {r.x, r.y, r.w, h}, "More      ")) {
            std::vector<MenuEntry> items;
            for (const auto &it : tb.spill) {
                if (it.first.empty() && (items.empty() || items.back().label.empty())) continue;
                MenuEntry e;
                e.label = it.first;
                e.enabled = it.second;
                items.push_back(e);
            }
            while (!items.empty() && items.back().label.empty()) items.pop_back();
            int got = popup_menu(ui, r.x, r.y + h + 2 * S, items);
            if (got >= 0) {
                ui.state<ToolbarState>(tb.id).picked = items[(size_t)got].label;
                ui.again();
            }
        }
        if (ui.drawing) {
            double tw = f.width("More      "), ax = r.x + r.w / 2 + tw / 2 - 6 * S, ay = r.y + h / 2, a = 3.5 * S;
            ui.polyline({{ax - a, ay - a / 2}, {ax, ay + a / 2}, {ax + a, ay - a / 2}}, k["text"], 1.8 * S);
        }
    }
    ui.end();
}

// ----------------------------------------------------------------------------- menus

int popup_menu(Ui &ui, double x, double y, const std::vector<MenuEntry> &items, double min_w) {
    Skin &k = ui.sk();
    int S = ui.S();
    Font &f = k.pinstripe ? *k.fb : *k.f;
    double ih = f.line + (k.soft ? 12 : 6) * S, sh = 7 * S, bd = k.soft ? 6 * S : S;
    double left = 22 * S, right = 14 * S, keys_w = 0, w = 0;
    for (const auto &it : items) {
        w = std::max(w, f.width(it.label));
        if (!it.keys.empty()) keys_w = std::max(keys_w, f.width(it.keys) + 24 * S);
    }
    w = std::max(min_w, std::ceil(left + w + keys_w + right + 2 * bd));
    double h = 2 * bd;
    for (const auto &it : items) h += it.label.empty() ? sh : ih;
    // inside the window
    if (x + w > ui.W()) x = std::max(0.0, ui.W() - w);
    if (y + h > ui.H()) y = std::max(0.0, std::min(y - h, ui.H() - h));
    Rect box{std::round(x), std::round(y), w, h};
    int sel = -1, result = -1;
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].checked) sel = (int)i;
    bool pressed_inside = false;
    auto row_at = [&](double py) {
        double yy = box.y + bd;
        for (size_t i = 0; i < items.size(); i++) {
            double rh = items[i].label.empty() ? sh : ih;
            if (py >= yy && py < yy + rh) return items[i].label.empty() ? -1 : (int)i;
            yy += rh;
        }
        return -1;
    };
    auto step = [&](int d) {
        int n = (int)items.size();
        for (int t = 0; t < n; t++) {
            sel = ((sel < 0 ? (d > 0 ? -1 : n) : sel) + d + n) % n;
            if (!items[(size_t)sel].label.empty() && items[(size_t)sel].enabled) return;
        }
    };
    ui.modal([&](bool &done) {
        // input
        if (ui.has(plat::Event::MOVE)) {
            if (box.has(ui.mx, ui.my)) {
                int r = row_at(ui.my);
                if (r != sel) {
                    sel = r;
                    ui.again();
                }
            }
        } else if (ui.has(plat::Event::DOWN)) {
            if (!box.has(ui.mx, ui.my)) {
                result = -1;
                done = true;
                return;
            }
            pressed_inside = true;
            ui.consume();
        } else if (ui.has(plat::Event::UP)) {
            if (box.has(ui.mx, ui.my)) {
                int r = row_at(ui.my);
                if (r >= 0 && items[(size_t)r].enabled) {
                    result = r;
                    done = true;
                    return;
                }
            } else if (pressed_inside) {
                result = -1;
                done = true;
                return;
            }
            ui.consume();
        } else if (ui.key(plat::K_ESCAPE)) {
            done = true;
            return;
        } else if (ui.key(plat::K_DOWN)) {
            step(1);
            ui.again();
        } else if (ui.key(plat::K_UP)) {
            step(-1);
            ui.again();
        } else if (ui.key(plat::K_LEFT)) {
            result = -2;
            done = true;
            return;
        } else if (ui.key(plat::K_RIGHT)) {
            result = -3;
            done = true;
            return;
        } else if (ui.key(plat::K_RETURN) || ui.key(' ')) {
            if (sel >= 0 && items[(size_t)sel].enabled) {
                result = sel;
                done = true;
                return;
            }
        } else if (ui.has(plat::Event::TEXT) || ui.has(plat::Event::KEY)) {
            // a letter jumps to the next item starting with it
            std::string t = ui.has(plat::Event::TEXT) ? ui.ev.text : std::string(1, (char)ui.ev.key);
            ui.consume();
            if (t.size() == 1) {
                char c = (char)std::tolower((unsigned char)t[0]);
                int n = (int)items.size();
                for (int d = 1; d <= n; d++) {
                    int i = ((sel < 0 ? -1 : sel) + d + n) % n;
                    const auto &l = items[(size_t)i].label;
                    if (!l.empty() && items[(size_t)i].enabled && std::tolower((unsigned char)l[0]) == c) {
                        sel = i;
                        break;
                    }
                }
                ui.again();
            }
        }
        if (!ui.drawing) return;
        // drawing
        if (k.pinstripe) ui.fill({box.x + 2 * S, box.y + 2 * S, box.w, box.h}, k["dark"]);   // a drop shadow
        if (k.soft) {
            ui.shadow(box, 10 * S, 10 * S, k.dark ? 0.5 : 0.14);
            ui.rrect(box, 10 * S, k["menu"], k.dark ? k["edge"] : k["line"], S);
        } else {
            ui.fill(box, k["menu"]);
            ui.box(box, k["edge"], bd);
        }
        double yy = box.y + bd;
        for (size_t i = 0; i < items.size(); i++) {
            const auto &it = items[i];
            if (it.label.empty()) {
                ui.fill({box.x + bd + 2 * S, yy + sh / 2, box.w - 2 * bd - 4 * S, (double)S}, k.soft ? k["line"] : k["shadow"]);
                yy += sh;
                continue;
            }
            Rect rr{box.x + bd, yy, box.w - 2 * bd, ih};
            bool on = (int)i == sel && it.enabled;
            Color fg = !it.enabled ? k["off"] : on ? k["seltext"] : k["text"];
            if (on && k.soft) {
                ui.rrect(rr.in(5 * S, S), 6 * S, k["sel"], {}, 0, true, false);
            } else if (on)
                ui.fill(rr, k["sel"]);
            if (it.checked) {
                double cx = rr.x + 11 * S, cy = rr.y + ih / 2;
                if (it.radio) ui.oval({cx - 3 * S, cy - 3 * S, 6.0 * S, 6.0 * S}, fg, {}, 0, true, false);
                else ui.polyline({{cx - 4 * S, cy}, {cx - S, cy + 3 * S}, {cx + 4 * S, cy - 4 * S}}, fg, 1.5 * S);
            }
            ui.text_in(f, {rr.x + left, rr.y, rr.w - left - right, ih}, it.label, fg);
            if (!it.keys.empty()) ui.text_in(f, {rr.x, rr.y, rr.w - right, ih}, it.keys, fg, RIGHT);
            yy += ih;
        }
    });
    return result;
}

// ----------------------------------------------------------------------------- lists

namespace {
struct ListState {
    double scroll = 0;
    int last_rows = -1;
};
}  // namespace

ListResult list_view(Ui &ui, const std::string &key, const Rect &r, const std::vector<Column> &cols, const std::vector<std::vector<std::string>> &rows,
                     int &sel, const std::string &empty_text, bool headings) {
    Skin &k = ui.sk();
    int S = ui.S();
    ListResult out;
    Id id = ui.id("list:" + key);
    ListState &st = ui.state<ListState>(id);
    int n = (int)rows.size();
    if (sel >= n) sel = n - 1;
    if (sel < 0 && n > 0) sel = 0;
    double rh = k.line + (k.soft ? 16 : 6) * S;
    double hh = headings ? (k.soft ? k.fsmallb->line + 14 * S : k.fb->line + 2 * 2 * S + (k.bevel ? 2 * S : S)) : 0;
    double sbw = (k.soft ? 12 : 16) * S;
    Rect inner = k.soft ? r.in(6 * S, S) : r.in(S);
    Rect head{inner.x, inner.y, inner.w - sbw, hh};
    Rect body{inner.x, inner.y + hh, inner.w - sbw, inner.h - hh};
    double content = n * rh, maxs = std::max(0.0, content - body.h);
    st.scroll = std::max(0.0, std::min(st.scroll, maxs));
    // columns: their widths, the stretchy ones sharing what's left (or giving up room when there's too little)
    std::vector<double> xs;
    {
        double fixed = 0, stretch_n = 0, stretch_w = 0;
        for (const auto &c : cols) {
            fixed += c.width * S;
            if (c.stretch) {
                stretch_n++;
                stretch_w += c.width * S;
            }
        }
        double extra = body.w - fixed;
        double cx = body.x;
        for (const auto &c : cols) {
            xs.push_back(cx);
            double w = c.width * S;
            if (c.stretch && stretch_n) w = extra >= 0 ? w + extra / stretch_n : std::max(40.0 * S, w + extra * (w / stretch_w));
            cx += w;
        }
        xs.push_back(cx);
    }
    // the focus and the keyboard
    // a press on a row (noted before hit() uses it up)
    bool press = ui.has(plat::Event::DOWN) && ui.over(body);
    int press_button = ui.ev.button, press_clicks = ui.ev.clicks, press_mods = ui.ev.mods;
    ui.hit(id, body, true, true);
    bool focus = ui.focused(id);
    auto see = [&] {
        if (sel < 0) return;
        double top = sel * rh;
        if (top < st.scroll) st.scroll = top;
        if (top + rh > st.scroll + body.h) st.scroll = top + rh - body.h;
        st.scroll = std::max(0.0, std::min(st.scroll, maxs));
    };
    auto move = [&](int to) {
        if (n == 0) return;
        to = std::max(0, std::min(n - 1, to));
        if (to != sel) {
            sel = to;
            out.changed = true;
        }
        see();
        ui.again();
    };
    if (focus) {
        int page = std::max(1, (int)(body.h / rh) - 1);
        if (ui.key(plat::K_DOWN)) move(sel + 1);
        else if (ui.key(plat::K_UP)) move(sel - 1);
        else if (ui.key(plat::K_HOME)) move(0);
        else if (ui.key(plat::K_END)) move(n - 1);
        else if (ui.key(plat::K_PGDN)) move(sel + page);
        else if (ui.key(plat::K_PGUP)) move(sel - page);
        else if (n > 0 && ui.key(plat::K_RETURN)) out.open = true;
        else if (n > 0 && (ui.key(' ') || (ui.has(plat::Event::TEXT) && ui.ev.text == " "))) {
            ui.consume();
            out.space = true;
        }
        else if (n > 0 && (ui.key(plat::K_DELETE)
#ifdef __APPLE__
                           || ui.key(plat::K_BACKSPACE)
#endif
                               ))
            out.del = true;
        else if (n > 0 && (ui.key(plat::K_MENU) || ui.key(plat::K_F10, plat::SHIFT))) {
            out.menu = true;
            out.menu_x = body.x + 40 * S;
            out.menu_y = body.y + (sel + 1) * rh - st.scroll;
        }
    }
    // the mouse: a press picks a row (and a double-click opens it); right-click asks for a menu
    if (press) {
        int row = (int)std::floor((ui.my - body.y + st.scroll) / rh);
        for (size_t c = 0; c < cols.size(); c++)   // which column
            if (ui.mx >= xs[c] && ui.mx < xs[c + 1]) out.col = (int)c;
        if (row >= 0 && row < n) {
            if (row != sel) out.changed = true;
            sel = row;
            if (press_button == 1 && press_clicks >= 2) out.open = true;
            if (press_button == 3 || (press_button == 1 && (press_mods & plat::CTRL) && plat::PRIMARY == plat::CMD)) {
                out.menu = true;
                out.menu_x = ui.mx;
                out.menu_y = ui.my;
            }
        }
        ui.focus(id);
        ui.consume();
        ui.again();
    }
    if (ui.has(plat::Event::WHEEL) && ui.over(r) && maxs > 0) {
        st.scroll = std::max(0.0, std::min(maxs, st.scroll + ui.ev.dy * rh * 3));
        ui.consume();
        ui.again();
    }
    if (st.last_rows != n) {
        st.last_rows = n;
        see();
    }
    // the scroll bar
    Rect bar{inner.r() - sbw, inner.y, sbw, inner.h};
    Id bid = ui.id("list:" + key + ":bar");
    double arrow = k.soft ? 0 : sbw;
    Rect track{bar.x, bar.y + arrow, bar.w, bar.h - 2 * arrow};
    double th = maxs > 0 ? std::max(20.0 * S, track.h * body.h / content) : track.h;
    double ty = track.y + (maxs > 0 ? (track.h - th) * st.scroll / maxs : 0);
    struct Drag {
        double grab = -1;
    };
    Drag &dr = ui.state<Drag>(bid);
    if (ui.has(plat::Event::DOWN) && ui.ev.button == 1 && ui.over(bar) && maxs > 0) {
        ui.capture(bid);
        if (ui.my >= ty && ui.my < ty + th) dr.grab = ui.my - ty;   // the thumb: drag it
        else {   // the arrows step a row, the track a page
            dr.grab = -1;
            if (!k.soft && ui.my < track.y) st.scroll -= rh;
            else if (!k.soft && ui.my >= track.b()) st.scroll += rh;
            else st.scroll += ui.my < ty ? -body.h : body.h;
            st.scroll = std::max(0.0, std::min(maxs, st.scroll));
        }
        ui.consume();
        ui.again();
    } else if (ui.held(bid) && ui.has(plat::Event::MOVE) && dr.grab >= 0 && maxs > 0) {
        st.scroll = std::max(0.0, std::min(maxs, (ui.my - dr.grab - track.y) / std::max(1.0, track.h - th) * maxs));
        ui.again();
    }
    if (!ui.drawing) return out;

    // ---- drawing
    double padx = (k.soft ? 8 : 4) * S;
    if (k.soft) {
        ui.rrect(r, 10 * S, k["field"], k["edge"], S);
        if (headings) {
            for (size_t i = 0; i < cols.size(); i++) {
                Rect hr{xs[i], head.y, xs[i + 1] - xs[i], head.h};
                ui.clip(hr);
                ui.text_in(*k.fsmallb, {hr.x + padx, hr.y, hr.w - 2 * padx, hr.h}, ui.elide(*k.fsmallb, cols[i].head, hr.w - 2 * padx), k["dim"]);
                ui.unclip();
            }
            ui.fill({head.x, head.b() - S, head.w, (double)S}, k["line"]);
        }
    } else {
        ui.fill(r, k["edge"]);
        ui.fill(inner, k["field"]);
    }
    if (headings && !k.soft) {
        ui.fill(head, k["head"]);
        for (size_t i = 0; i < cols.size(); i++) {
            Rect hr{xs[i], head.y, xs[i + 1] - xs[i], head.h};
            if (k.bevel) {
                ui.fill({hr.x, hr.y, hr.w, (double)S}, k["light"]);
                ui.fill({hr.x, hr.y, (double)S, hr.h}, k["light"]);
                ui.fill({hr.x, hr.b() - S, hr.w, (double)S}, k["shadow"]);
                ui.fill({hr.r() - S, hr.y, (double)S, hr.h}, k["shadow"]);
            } else if (i + 1 < cols.size())
                ui.fill({hr.r() - S, hr.y + 3 * S, (double)S, hr.h - 6 * S}, k["shadow"]);
            ui.clip(hr);
            ui.text_in(*k.fb, {hr.x + padx, hr.y, hr.w - 2 * padx, hr.h}, ui.elide(*k.fb, cols[i].head, hr.w - 2 * padx), k["text"]);
            ui.unclip();
        }
        if (!k.bevel) ui.fill({head.x, head.b() - S, head.w, (double)S}, k["shadow"]);
    }
    ui.clip(body);
    int first = (int)(st.scroll / rh), last = std::min(n, (int)((st.scroll + body.h) / rh) + 1);
    // a list without the keyboard focus shows its selection more softly
    Color soft_text = hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["text"];
    int hover = ui.over(body) && !ui.held(bid) ? (int)std::floor((ui.my - body.y + st.scroll) / rh) : -1;
    for (int i = std::max(0, first); i < last; i++) {
        double y = body.y + i * rh - st.scroll;
        bool on = i == sel;
        Color fg = k["text"];
        if (k.soft) {
            if (i + 1 < n && !on && i + 1 != sel) ui.fill({body.x + padx, y + rh - S, body.w - 2 * padx, (double)S}, k["line"]);
            if (on) ui.rrect({body.x, y + S, body.w, rh - 2 * S}, 7 * S, focus ? k["sel"] : k["selsoft"], {}, 0, true, false);
            else if (i == hover) ui.rrect({body.x, y + S, body.w, rh - 2 * S}, 7 * S, mix(k["field"], k["text"], 0.05), {}, 0, true, false);
            if (on) fg = focus ? k["seltext"] : soft_text;
        } else if (on) {
            ui.fill({body.x, y, body.w, rh}, focus ? k["sel"] : k["selsoft"]);
            fg = focus ? k["seltext"] : soft_text;
        }
        const auto &cells = rows[(size_t)i];
        for (size_t c = 0; c < cols.size() && c < cells.size(); c++) {
            double cw = xs[c + 1] - xs[c] - 2 * padx;
            if (cols[c].mark == MARK_CHECK) {
                if (cells[c].empty()) continue;
                double b = std::min(k.line - 2 * S, rh - 4 * S);
                Rect bx{std::round(xs[c] + padx), std::round(y + (rh - b) / 2), std::round(b), std::round(b)};
                ui.fill(bx, k["field"]);
                ui.box(bx, on ? fg : k["text"], S);
                if (cells[c] == "x") {
                    double m = std::max(2.0 * S, std::round(b / 5));
                    ui.line(bx.x + m, bx.y + m, bx.r() - m, bx.b() - m, k["text"], 1.5 * S, false);
                    ui.line(bx.x + m, bx.b() - m, bx.r() - m, bx.y + m, k["text"], 1.5 * S, false);
                }
                continue;
            }
            if (cols[c].mark == MARK_TREE) {
                if (cells[c].empty()) continue;
                double a = std::max(3.0 * S, std::round(k.line / 4)), cx0 = xs[c] + padx + a + S, cy = y + rh / 2;
                if (cells[c] == "+")   // closed: pointing right
                    ui.polygon({{cx0 - a / 2, cy - a}, {cx0 + a / 2 + S, cy}, {cx0 - a / 2, cy + a}}, fg);
                else if (cells[c] == "-")   // open: pointing down
                    ui.polygon({{cx0 - a, cy - a / 2}, {cx0 + a, cy - a / 2}, {cx0, cy + a / 2 + S}}, fg);
                continue;
            }
            ui.text_in(*k.f, {xs[c] + padx, y, cw, rh}, ui.elide(*k.f, cells[c], cw), fg);
        }
    }
    if (n == 0 && !empty_text.empty()) {
        auto lines = ui.wrap(*k.f, empty_text, std::max(200.0 * S, body.w - 40 * S));
        double rel = body.h / rh < 6 ? 0.55 : 0.35;
        double y = body.y + body.h * rel - lines.size() * k.line / 2;
        for (const auto &l : lines) {
            ui.text_in(*k.f, {body.x, y, body.w, k.line}, l, k["dim"], CENTER);
            y += k.line;
        }
    }
    ui.unclip();
    // the scroll bar
    if (!k.soft) ui.fill(bar, k["trough"]);
    if (k.soft) {
        if (maxs > 0) {
            Rect t = Rect{bar.x, ty, bar.w, th}.in(3 * S);
            ui.rrect(t, t.w / 2, ui.held(bid) ? k["text"] : k["thumb"], {}, 0, true, false);
        }
    } else {
        auto raised = [&](const Rect &b) {
            ui.fill(b, k["thumb"]);
            ui.hline(b.x, b.r(), b.y, k["light"]);
            ui.vline(b.x, b.y, b.b(), k["light"]);
            ui.hline(b.x, b.r(), b.b() - S, k["shadow"]);
            ui.vline(b.r() - S, b.y, b.b(), k["shadow"]);
            ui.box(b, k["dark"], S);
        };
        Rect up{bar.x, bar.y, bar.w, arrow}, dn{bar.x, bar.b() - arrow, bar.w, arrow};
        raised(up);
        raised(dn);
        double a = std::max(3.0 * S, std::floor(bar.w / 5)), ax = bar.x + bar.w / 2;
        Color ac = maxs > 0 ? k["text"] : k["off"];
        ui.polygon({{ax - a, up.y + up.h / 2 + a / 2}, {ax + a, up.y + up.h / 2 + a / 2}, {ax, up.y + up.h / 2 - a / 2 - S}}, ac);
        ui.polygon({{ax - a, dn.y + dn.h / 2 - a / 2}, {ax + a, dn.y + dn.h / 2 - a / 2}, {ax, dn.y + dn.h / 2 + a / 2 + S}}, ac);
        if (maxs > 0) raised({bar.x, ty, bar.w, th});
    }
    return out;
}

// ----------------------------------------------------------------------------- title bars

double title_h(Ui &ui) {
    Skin &k = ui.sk();
    return k.soft ? k.fhead->line + 26 * ui.S() : k.line + (k.bevel ? 8 : 6) * ui.S();
}

int title_bar(Ui &ui, const Rect &r, const std::string &title, bool close, bool zoom, bool minimize, bool active) {
    Skin &k = ui.sk();
    int S = ui.S();
    double w = r.w, h = r.h;
    int clicked = 0;
    struct Box {
        double x0, x1;
        int what;
    };
    std::vector<Box> boxes;
    Font &f = *k.ftitle;
    if (k.soft) {
        // a heading at the top of the card, and a round close button
        ui.text_in(*k.fhead, {r.x + 22 * S, r.y + 4 * S, w - 70 * S, h - 4 * S}, ui.elide(*k.fhead, title, w - 70 * S), k["title_text"]);
        if (close) {
            double d = 28 * S, cx = r.r() - 14 * S - d / 2, cy = r.y + 4 * S + (h - 4 * S) / 2, a = 5.0 * S;
            Rect cb{cx - d / 2, cy - d / 2, d, d};
            if (ui.over(cb)) ui.oval(cb, mix(k["title"], k["text"], 0.1), {}, 0, true, false);
            ui.line(cx - a, cy - a, cx + a, cy + a, k["dim"], 2 * S);
            ui.line(cx - a, cy + a, cx + a, cy - a, k["dim"], 2 * S);
            boxes.push_back({cb.x - r.x, cb.r() - r.x, 1});
        }
    } else if (k.bevel) {
        ui.fill(r, active ? k["title"] : k["title_off"]);
        double left = 0;
        if (close) {
            bevel_box(ui, {r.x, r.y, h - S, h}, false);
            double q = std::floor(h / 4);
            Rect bar{r.x + q, r.y + std::floor(h / 2) - 2 * S, h - 2 * q - S, 3.0 * S};
            ui.fill(bar, k["light"]);
            ui.box(bar, k["dark"], S);
            ui.fill({r.x + h - S, r.y, (double)S, h}, k["dark"]);
            boxes.push_back({0, h, 1});
            left = h;
        }
        double right = w;
        for (int which : {2, 3}) {
            bool on = which == 2 ? zoom : minimize;
            if (!on) continue;
            double x1 = right, x0 = right - h;
            ui.fill({r.x + x0, r.y, (double)S, h}, k["dark"]);
            bevel_box(ui, {r.x + x0 + S, r.y, x1 - x0 - S, h}, false);
            double cx = r.x + std::floor((x0 + x1) / 2) + std::floor(S / 2.0), cy = r.y + std::floor(h / 2), a = std::floor(h / 5);
            if (which == 2) ui.polygon({{cx - a, cy + a / 2}, {cx + a, cy + a / 2}, {cx, cy - a / 2 - S}}, k["text"]);
            else ui.polygon({{cx - a, cy - a / 2}, {cx + a, cy - a / 2}, {cx, cy + a / 2 + S}}, k["text"]);
            boxes.push_back({x0, x1, which});
            right = x0;
        }
        ui.text_in(f, {r.x + left, r.y, right - left, h}, ui.elide(f, title, right - left - 8 * S), active ? k["title_text"] : k["title_off_text"],
                   CENTER);
    } else {
        ui.fill(r, k["title"]);
        if (active)
            for (double y = 3 * S; y < h - 3 * S; y += 2 * S) ui.fill({r.x + S, r.y + y + std::floor(S / 2.0), w - 2 * S, (double)S}, k["dark"]);
        double b = h - 8 * S, y0 = std::floor((h - b) / 2);
        if (close && active) {
            double x0 = 8 * S;
            ui.fill({r.x + x0 - 2 * S, r.y + y0 - S, b + 4 * S, b + 2 * S}, k["title"]);
            ui.box({r.x + x0, r.y + y0, b, b}, k["dark"], S);
            boxes.push_back({0, x0 + b + 4 * S, 1});
        }
        if (zoom && active) {
            double x1 = w - 8 * S, x0 = x1 - b;
            ui.fill({r.x + x0 - 2 * S, r.y + y0 - S, b + 4 * S, b + 2 * S}, k["title"]);
            ui.box({r.x + x0, r.y + y0, b, b}, k["dark"], S);
            ui.box({r.x + x0, r.y + y0, std::floor(b * 6 / 10), std::floor(b * 6 / 10)}, k["dark"], S);
            boxes.push_back({x0 - 4 * S, w, 2});
        }
        std::string t = ui.elide(f, title, w - 2 * (b + 24 * S));
        double tw = f.width(t);
        ui.fill({r.x + std::floor(w / 2 - tw / 2) - 6 * S, r.y, tw + 12 * S, h}, k["title"]);
        ui.text_in(f, r, t, active ? k["title_text"] : k["title_off_text"], CENTER);
    }
    for (const auto &bx : boxes) {
        Rect br{r.x + bx.x0, r.y, bx.x1 - bx.x0, h};
        if (ui.hit(ui.id("titlebox:" + std::to_string(bx.what)), br, false, true)) clicked = bx.what;
    }
    return clicked;
}

// ----------------------------------------------------------------------------- dialogs

namespace {
struct DialogState {
    double w = 0, h = 0;          // the contents' size last time
    double dx = 0, dy = 0;        // moved by its title bar
    bool dragging = false;
    double gx = 0, gy = 0;
};
}  // namespace

int dialog(Ui &ui, const DialogBox &box, const std::function<int(Ui &)> &body, const std::vector<std::pair<std::string, int>> &buttons,
           int default_idx, int cancel) {
    int result = cancel;
    DialogState st;
    if (ui.sk().soft) ui.dim_behind(ui.sk().dark ? 0.45 : 0.28);   // the dialog floats over the dimmed window
    ui.modal([&](bool &done) {
        Skin &k = ui.sk();
        int S = ui.S();
        double tb = title_h(ui);
        double frame = k.bevel ? 5 * S : S;   // the border around: Bevel's thick sizing frame
        double padx = (k.soft ? 22 : 14) * S, pady = (k.soft ? 4 : 12) * S;
        double W = ui.W(), H = ui.H();
        // the size: asked for, or what the contents needed last time
        double cw = box.fw > 0 ? W * box.fw - 2 * (frame + padx) : box.w > 0 ? box.w * S : (st.w > 0 ? st.w : std::min(W - 40 * S, 560.0 * S));
        double ch_fixed = box.fh > 0 ? H * box.fh - 2 * frame - tb - S - 2 * pady : box.h > 0 ? box.h * S : 0;
        double btn_h = 0;
        if (!buttons.empty()) btn_h = ui.btn_w("OK") > 0 ? (k.soft ? k.f->line + 18 * S : k.fb->line + 12 * S + (k.bevel ? 0 : 6 * S)) + 14 * S : 0;
        double ch = ch_fixed > 0 ? ch_fixed : (st.h > 0 ? st.h : 100 * S);
        double dw = std::min(W - 20 * S, cw + 2 * (frame + padx)),
               dh = std::min(H - 20 * S, ch + btn_h + 2 * frame + tb + S + 2 * pady + (k.soft ? 14 * S : 0));
        double x = std::max(0.0, std::floor((W - dw) / 2) + st.dx), y = std::max(0.0, std::floor((H - dh) / 3) + st.dy);
        Rect outer{x, y, dw, dh};
        // the frame
        if (k.pinstripe) ui.fill({x + 2 * S, y + 2 * S, dw, dh}, k["dark"]);
        if (k.soft) {
            // the window behind dims; the dialog is a card floating over it
            ui.shadow(outer, 16 * S, 18 * S, k.dark ? 0.6 : 0.22);
            ui.rrect(outer, 16 * S, k["card"], k.dark ? k["edge"] : k["line"], S);
        } else
            ui.fill(outer, k["dark"]);
        Rect in = outer.in(S);
        if (k.bevel) {
            ui.fill(in, k["title"]);
            in = in.in(3 * S);
            ui.fill(in, k["dark"]);
            in = in.in(S);
        }
        Rect bar{in.x, in.y, in.w, tb};
        bool closable = k.bevel || k.soft;
        int clicked = title_bar(ui, bar, box.title, closable, false, false);
        if (clicked == 1) {
            result = cancel;
            done = true;
            return;
        }
        // dragging by the title bar
        Id drag = ui.id("dialog-drag");
        if (ui.has(plat::Event::DOWN) && ui.ev.button == 1 && ui.over(bar)) {
            st.dragging = true;
            st.gx = ui.mx - st.dx;
            st.gy = ui.my - st.dy;
            ui.capture(drag);
            ui.consume();
        } else if (st.dragging && ui.has(plat::Event::MOVE)) {
            st.dx = ui.mx - st.gx;
            st.dy = ui.my - st.gy;
            ui.again();
        } else if (st.dragging && ui.has(plat::Event::UP)) {
            st.dragging = false;
        }
        if (!k.soft) ui.fill({in.x, bar.b(), in.w, (double)S}, k["dark"]);
        Rect content{in.x, bar.b() + S, in.w, in.b() - bar.b() - S};
        if (k.soft) content.h -= 14 * S;   // room under the buttons, inside the rounded corners
        else ui.fill(content, k["window"]);
        Rect inner{content.x + padx, content.y + pady, content.w - 2 * padx, content.h - 2 * pady};
        // the buttons, bottom right
        ui.begin(inner, 0);
        Rect row = buttons.empty() ? Rect{} : ui.cut_bottom(btn_h);
        int got = 0;
        if (!buttons.empty()) {
            double bx = row.r();
            std::vector<std::pair<Rect, size_t>> spots;
            for (size_t i = buttons.size(); i-- > 0;) {
                bool def = (int)i == default_idx;
                Font &f = k.soft ? *k.f : *k.fb;
                double bh = f.line + 12 * S, bw = std::max(76.0 * S, std::ceil(f.width(buttons[i].first)) + 26 * S);
                if (k.soft || (def && !k.bevel)) {
                    bw += 6 * S;
                    bh += 6 * S;
                }
                bx -= bw;
                spots.push_back({Rect{bx, row.b() - bh, bw, bh}, i});
                bx -= 8 * S;
            }
            std::reverse(spots.begin(), spots.end());
            for (auto &sp : spots) {
                size_t i = sp.second;
                if (button_at(ui, ui.id("dlgbtn:" + buttons[i].first), sp.first, buttons[i].first, true, (int)i == default_idx)) got = buttons[i].second;
            }
        }
        // the contents
        Rect area = ui.remaining();
        ui.clip(area);
        ui.begin({area.x, area.y, area.w, 1e9}, 6 * S);
        int r = body(ui);
        Rect used = ui.end();
        ui.unclip();
        ui.end();
        if (r) {
            result = r;
            done = true;
            return;
        }
        if (got) {
            result = got;
            done = true;
            return;
        }
        // its size for next time
        double need_w = box.w > 0 || box.fw > 0 ? cw : std::max(used.w, 200.0 * S);
        double need_h = used.h;
        if (box.w <= 0 && box.fw <= 0 && !buttons.empty()) {
            double bsum = 0;
            for (const auto &b : buttons) bsum += std::max(76.0 * S, ui.sk().fb->width(b.first) + 26 * S) + 14 * S;
            need_w = std::max(need_w, bsum);
        }
        if (std::fabs(need_w - st.w) > 0.5 || std::fabs(need_h - st.h) > 0.5) {
            st.w = need_w;
            st.h = need_h;
            ui.again();
        }
        // Return picks the default button; Esc cancels
        if (ui.key(plat::K_ESCAPE)) {
            result = cancel;
            done = true;
            return;
        }
        if (!buttons.empty() && ui.key(plat::K_RETURN)) {
            result = buttons[(size_t)std::max(0, std::min((int)buttons.size() - 1, default_idx))].second;
            done = true;
            return;
        }
    });
    return result;
}

static int message(Ui &ui, const std::string &title, const std::string &msg, const std::string &icon_name,
                   const std::vector<std::pair<std::string, int>> &specs, int default_idx, int cancel) {
    return dialog(
        ui, {title},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double iz = 2 * S, is = 16 * iz;
            double wrap = 420 * S;
            auto lines = u.wrap(*k.f, msg, wrap);
            double tw = 0;
            for (const auto &l : lines) tw = std::max(tw, k.f->width(l));
            Rect r = u.place(is + 14 * S + std::ceil(tw) + 2, std::max(is, lines.size() * k.line));
            if (k.soft) {
                // a round badge: i, ?, or ! (a warning in amber, the others in the highlight color)
                Rect b{r.x, r.y, is, is};
                bool warn = icon_name == "warn";
                Color bg = warn ? fit(hex("#D98A00"), {k["card"]}, 3.0) : k["sel"];
                Color fg = warn ? (luminance(bg) > 0.4 ? Color{0, 0, 0} : Color{1, 1, 1}) : k["seltext"];
                u.oval(b, bg, {}, 0, true, false);
                std::string mark = warn ? "!" : icon_name == "ask" ? "?" : "i";
                u.text_in(*k.fhead, b, mark, fg, CENTER);
            } else if (const auto *rows = icon(icon_name))
                u.pixel_art(*rows, icon_color, r.x, r.y, (int)iz);
            double y = r.y;
            for (const auto &l : lines) {
                u.text(*k.f, r.x + is + 14 * S, std::round(y + k.f->ascent), l, k["text"]);
                y += k.line;
            }
            return 0;
        },
        specs, default_idx, cancel);
}

void alert(Ui &ui, const std::string &title, const std::string &msg, const std::string &icon_name) {
    message(ui, title, msg, icon_name, {{"OK", 1}}, 0, 1);
}

bool confirm(Ui &ui, const std::string &title, const std::string &msg, const std::string &yes, const std::string &no, const std::string &icon_name) {
    return message(ui, title, msg, icon_name, {{yes, 1}, {no, 2}}, 0, 2) == 1;
}

bool ask(Ui &ui, const std::string &title, const std::string &msg, std::string &value) {
    std::string v = value;
    bool first = true;
    int r = dialog(
        ui, {title},
        [&](Ui &u) {
            para(u, msg, false, 460 * u.S());
            Rect er = u.place(std::max(u.width(), 44 * u.sk().f->width("0")), u.sk().line + (u.sk().soft ? 14 : 8) * u.S());
            if (first) {   // the box starts with the focus, its text selected
                u.focus(u.id("ask-entry"));
                first = false;
            }
            entry_at(u, u.id("ask-entry"), er, v);
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    value = v;
    return true;
}

std::string choose(Ui &ui, const std::string &title, const std::string &msg, const std::vector<std::pair<std::string, std::string>> &options,
                   const std::string &current) {
    int sel = 0;
    for (size_t i = 0; i < options.size(); i++)
        if (options[i].first == current) sel = (int)i;
    std::vector<std::vector<std::string>> rows;
    for (const auto &o : options) rows.push_back({o.second});
    bool first = true;
    int r = dialog(
        ui, {title},
        [&](Ui &u) {
            if (!msg.empty()) para(u, msg, false, 520 * u.S());
            Skin &k = u.sk();
            double rh = k.line + (k.soft ? 10 : 6) * u.S();
            int shown = std::min(12, std::max(4, (int)options.size()));
            Rect lr = u.place(520 * u.S(), shown * rh + 2 * u.S());
            if (first) {
                u.focus(u.id("list:choose"));
                first = false;
            }
            ListResult res = list_view(u, "choose", lr, {{"", 520, true}}, rows, sel, "", false);
            if (res.open && sel >= 0) return 1;
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1 || sel < 0 || sel >= (int)options.size()) return "";
    return options[(size_t)sel].first;
}

}  // namespace ui
