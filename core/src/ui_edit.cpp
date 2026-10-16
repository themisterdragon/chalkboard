// Text editing (port of the Tk entry and gui/widgets.py RichText): one-line boxes, and text boxes
// that show **bold**, *italic*, and __underline__ as formatting without the marks. What's saved is
// the same marked text as everywhere else. Ctrl-B/I/U (Ctrl-T too; Cmd on a Mac) or the format bar
// format the selection, the word at the cursor, or what's typed next. Undo works in whole words,
// formatting included.
#include <algorithm>
#include <cmath>
#include <cwctype>

#include "markup.h"
#include "text.h"
#include "ui.h"

namespace ui {

namespace {

using U32 = std::u32string;

struct Edit {
    U32 t;
    std::vector<int> fl;   // B/I/U bits per character
    size_t cur = 0, anc = 0;
    std::string synced;    // the value last read or written
    bool init = false;
    int style = -1;        // bits for what's typed next; -1 = like the text at the cursor
    std::vector<std::pair<std::string, size_t>> undo, redo;
    std::string last;      // the kind of the last change ("type", "delete", ...)
    double sx = 0, sy = 0; // scrolled (one line across, a text box down)
    double goal = -1;      // the x Up/Down keep to
    bool rich = false, single = false;
    int drag = 0;          // 1 characters, 2 words, 3 lines
    size_t drag_a = 0, drag_b = 0;
    // the wrapped lines
    struct Line {
        size_t a, b;
        double end;   // the x where the line ends
    };
    std::vector<Line> lines;
    std::vector<double> xs;   // each character's x within its line (and one past the end)
    double lay_w = -1;
    uint64_t lay_ver = (uint64_t)-1, ver = 0;
    int lay_S = 0;
    double lay_px = 0;
    bool want_select_all = false;
    bool focused_before = false;
    bool want_end = false;   // focused by the program: the cursor at the end, nothing selected
};

struct Clip {
    std::string plain, marked;   // the last thing copied from a text box
} g_clip;

Id g_rich = 0;        // the text box last typed in (the format bar works on it)
char g_toggle = 0;    // a format bar press for it, done when it's laid out

bool word_char(char32_t c) { return c == '_' || (c < 0x80 ? std::isalnum((int)c) != 0 : c > 0xBF); }

std::string to8(const U32 &s) { return text::to8(s); }
U32 to32(const std::string &s) { return text::to32(s); }

Font &font_for(Skin &k, int bits) { return k.font(bits & 3); }

void load(Edit &e, const std::string &value) {
    if (e.rich) {
        auto got = markup::flags_of(value);
        e.t = to32(got.first);
        e.fl = got.second;
    } else {
        e.t = to32(value);
        e.fl.assign(e.t.size(), 0);
    }
    if (e.single) {
        for (auto &c : e.t)
            if (c == '\n' || c == '\r') c = ' ';
    }
    e.fl.resize(e.t.size(), 0);
    e.synced = value;
    e.cur = std::min(e.cur, e.t.size());
    e.anc = std::min(e.anc, e.t.size());
    e.ver++;
    e.init = true;
}

std::string value_of(const Edit &e) { return e.rich ? markup::marked(to8(e.t), e.fl) : to8(e.t); }

size_t sel_a(const Edit &e) { return std::min(e.cur, e.anc); }
size_t sel_b(const Edit &e) { return std::max(e.cur, e.anc); }
bool has_sel(const Edit &e) { return e.cur != e.anc; }

// the bits of the text at the cursor: the character before it, or after it at a line's start
int context(const Edit &e) {
    size_t p = e.cur;
    if (p > 0 && e.t[p - 1] != '\n') return e.fl[p - 1];
    return p < e.t.size() && e.t[p] != '\n' ? e.fl[p] : 0;
}

void snapshot(Edit &e, const std::string &kind, bool boundary = false) {
    if (kind == "type" && e.last == "type" && !boundary) return;
    std::string snap = value_of(e);
    if (e.undo.empty() || e.undo.back().first != snap) {
        e.undo.push_back({snap, e.cur});
        if (e.undo.size() > 200) e.undo.erase(e.undo.begin());
    }
    e.redo.clear();
    e.last = kind;
}

// marks typed into the text turn into formatting; the cursor stays with its character
void tidy_up(Edit &e) {
    if (!e.rich) return;
    auto got = markup::tidy(to8(e.t), e.fl);
    U32 nt = to32(got.first);
    if (nt != e.t) {
        // which characters went: walk both texts
        std::vector<bool> gone(e.t.size(), false);
        size_t k = 0;
        for (char32_t ch : nt) {
            while (k < e.t.size() && e.t[k] != ch) gone[k++] = true;
            k++;
        }
        while (k < e.t.size()) gone[k++] = true;
        auto shift = [&](size_t p) {
            size_t n = 0;
            for (size_t i = 0; i < p && i < gone.size(); i++)
                if (gone[i]) n++;
            return p - n;
        };
        e.cur = shift(e.cur);
        e.anc = shift(e.anc);
    }
    e.t = nt;
    e.fl = got.second;
    e.fl.resize(e.t.size(), 0);
}

void replace(Edit &e, size_t a, size_t b, const U32 &ins, const std::vector<int> &ins_fl) {
    e.t.replace(a, b - a, ins);
    e.fl.erase(e.fl.begin() + (long)a, e.fl.begin() + (long)b);
    e.fl.insert(e.fl.begin() + (long)a, ins_fl.begin(), ins_fl.end());
    e.cur = e.anc = a + ins.size();
    e.ver++;
}

void type_text(Edit &e, const std::string &s8) {
    U32 s = to32(s8);
    U32 clean;
    for (char32_t c : s) {
        if (c == '\r') continue;
        if (c == '\n' && e.single) continue;
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        clean += c;
    }
    if (clean.empty()) return;
    // typed over a selection: the style of where it starts
    if (e.style < 0) {
        if (has_sel(e)) {
            size_t a = sel_a(e);
            e.style = a > 0 && e.t[a - 1] != '\n' ? e.fl[a - 1] : (a < e.t.size() && e.t[a] != '\n' ? e.fl[a] : 0);
        } else
            e.style = context(e);
    }
    bool boundary = !clean.empty() && (clean.back() == ' ' || clean.back() == '\n');
    snapshot(e, "type", boundary);
    int style = e.rich ? e.style : 0;
    size_t at = sel_a(e);
    replace(e, at, sel_b(e), clean, std::vector<int>(clean.size(), style));
    // keep an underline going across a typed space
    if (e.rich && (style & markup::U)) {
        size_t k = at;
        while (k > 0 && (e.t[k - 1] == ' ' || e.t[k - 1] == '\t')) k--;
        if (k < at && k > 0 && (e.fl[k - 1] & markup::U))
            for (size_t x = k; x < at; x++) e.fl[x] |= markup::U;
    }
    tidy_up(e);
    e.goal = -1;
}

void erase(Edit &e, size_t a, size_t b) {
    if (a >= b) return;
    snapshot(e, "delete");
    replace(e, a, b, U32(), {});
    e.style = -1;
    tidy_up(e);
    e.goal = -1;
}

void insert_marked(Edit &e, const std::string &s) {
    snapshot(e, "other");
    U32 t;
    std::vector<int> fl;
    if (e.rich) {
        auto got = markup::flags_of(s);
        t = to32(got.first);
        fl = got.second;
    } else {
        t = to32(s);
        fl.assign(t.size(), 0);
    }
    U32 ct;
    std::vector<int> cf;
    for (size_t i = 0; i < t.size(); i++) {
        char32_t c = t[i];
        if (c == '\r') continue;
        if (c == '\n' && e.single) c = ' ';
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        ct += c;
        cf.push_back(i < fl.size() ? fl[i] : 0);
    }
    replace(e, sel_a(e), sel_b(e), ct, cf);
    e.style = -1;
    tidy_up(e);
}

// Ctrl-B/I/U: the selection, the word at the cursor, or what's typed next
void toggle(Edit &e, char kind) {
    if (!e.rich) return;
    int bit = kind == 'b' ? markup::B : kind == 'i' ? markup::I : markup::U;
    size_t a, b;
    size_t p = e.cur;
    if (has_sel(e)) {
        a = sel_a(e);
        b = sel_b(e);
    } else if (p > 0 && p < e.t.size() && word_char(e.t[p - 1]) && word_char(e.t[p])) {
        a = b = p;
        while (a > 0 && word_char(e.t[a - 1])) a--;
        while (b < e.t.size() && word_char(e.t[b])) b++;
    } else {
        e.style = (e.style >= 0 ? e.style : context(e)) ^ bit;
        e.last.clear();
        return;
    }
    bool all = true, any = false;
    for (size_t k = a; k < b; k++)
        if (!std::iswspace((wint_t)e.t[k])) {
            any = true;
            if (!(e.fl[k] & bit)) all = false;
        }
    bool on = any ? !all : true;
    std::vector<int> nf = e.fl;
    for (size_t k = a; k < b; k++) nf[k] = on ? nf[k] | bit : nf[k] & ~bit;
    auto before = markup::tidy(to8(e.t), e.fl), after = markup::tidy(to8(e.t), nf);
    if (before != after) {
        snapshot(e, "format");
        e.fl = nf;
        tidy_up(e);
        e.ver++;
    }
    e.style = -1;
}

// the bits the B, I, U buttons show: all of the selection, or what typing would get
int formats(const Edit &e) {
    if (has_sel(e)) {
        int out = 0;
        for (int bit : {markup::B, markup::I, markup::U}) {
            bool all = true, any = false;
            for (size_t k = sel_a(e); k < sel_b(e); k++)
                if (!std::iswspace((wint_t)e.t[k])) {
                    any = true;
                    if (!(e.fl[k] & bit)) all = false;
                }
            if (any && all) out |= bit;
        }
        return out;
    }
    return e.style >= 0 ? e.style : context(e);
}

void restore(Edit &e, const std::pair<std::string, size_t> &snap) {
    std::string keep = e.synced;
    load(e, snap.first);
    e.synced = keep;   // so the change is noticed and saved
    e.cur = e.anc = std::min(snap.second, e.t.size());
    e.style = -1;
    e.last.clear();
}

void do_undo(Edit &e) {
    std::string cur = value_of(e);
    while (!e.undo.empty() && e.undo.back().first == cur) e.undo.pop_back();
    if (e.undo.empty()) return;
    e.redo.push_back({cur, e.cur});
    auto s = e.undo.back();
    e.undo.pop_back();
    restore(e, s);
}

void do_redo(Edit &e) {
    if (e.redo.empty()) return;
    e.undo.push_back({value_of(e), e.cur});
    auto s = e.redo.back();
    e.redo.pop_back();
    restore(e, s);
}

// ---- wrapping into lines
void layout(Edit &e, Skin &k, double width) {
    if (e.lay_ver == e.ver && std::fabs(e.lay_w - width) < 0.5 && e.lay_S == k.S && e.lay_px == k.f->px) return;
    e.lay_ver = e.ver;
    e.lay_w = width;
    e.lay_S = k.S;
    e.lay_px = k.f->px;
    size_t n = e.t.size();
    e.lines.clear();
    e.xs.assign(n + 1, 0);
    auto adv = [&](size_t i) { return e.t[i] == '\n' ? 0.0 : font_for(k, e.fl[i]).advance_of((uint32_t)e.t[i]); };
    size_t i = 0;
    while (true) {
        size_t b = i;
        while (b < n && e.t[b] != '\n') b++;
        size_t start = i;
        double x = 0;
        long last_space = -1;
        for (size_t j = i; j < b; j++) {
            double w = adv(j);
            if (!e.single && x + w > width && j > start && e.t[j] != ' ') {
                size_t brk = last_space >= (long)start ? (size_t)last_space + 1 : j;
                double base = brk == j ? x : e.xs[brk];
                e.lines.push_back({start, brk, base});
                for (size_t q = brk; q < j; q++) e.xs[q] -= base;
                x -= base;
                if (brk == j) x = 0;
                start = brk;
                last_space = -1;
                for (size_t q = brk; q < j; q++)
                    if (e.t[q] == ' ') last_space = (long)q;
            }
            e.xs[j] = x;
            x += w;
            if (e.t[j] == ' ') last_space = (long)j;
        }
        e.xs[b] = x;
        e.lines.push_back({start, b, x});
        if (b >= n) break;
        i = b + 1;
    }
}

size_t line_of(const Edit &e, size_t p) {
    for (size_t l = 0; l < e.lines.size(); l++) {
        const auto &L = e.lines[l];
        if (p >= L.a && p < L.b) return l;
        if (p == L.b && (L.b == e.t.size() || e.t[L.b] == '\n')) return l;
    }
    return e.lines.empty() ? 0 : e.lines.size() - 1;
}

// the x of position p on line l
double x_of(const Edit &e, size_t l, size_t p) { return p >= e.lines[l].b ? e.lines[l].end : e.xs[p]; }

bool soft_end(const Edit &e, size_t l) { return e.lines[l].b < e.t.size() && e.t[e.lines[l].b] != '\n'; }

size_t index_at(const Edit &e, size_t line, double x) {
    if (e.lines.empty()) return 0;
    line = std::min(line, e.lines.size() - 1);
    const auto &L = e.lines[line];
    for (size_t p = L.a; p < L.b; p++)
        if (x < (x_of(e, line, p) + x_of(e, line, p + 1)) / 2) return p;
    // past the end: a wrapped line's cursor goes before its last space (the next line starts at b)
    return soft_end(e, line) && L.b > L.a ? L.b - 1 : L.b;
}

size_t word_left(const Edit &e, size_t p) {
    while (p > 0 && !word_char(e.t[p - 1])) p--;
    while (p > 0 && word_char(e.t[p - 1])) p--;
    return p;
}
size_t word_right(const Edit &e, size_t p) {
    size_t n = e.t.size();
    while (p < n && !word_char(e.t[p])) p++;
    while (p < n && word_char(e.t[p])) p++;
    return p;
}

// ---- the keys: true when one was used
bool keys(Ui &ui, Edit &e, bool enabled, double page_lines) {
    const int P = plat::PRIMARY;
    const plat::Event &ev = ui.ev;
    if (ui.has(plat::Event::TEXT)) {
        if (!enabled) return false;
        std::string t = ev.text;
        ui.consume();
        type_text(e, t);
        return true;
    }
    if (!ui.has(plat::Event::KEY)) return false;
    int key = ev.key, m = ev.mods;
    bool shift = m & plat::SHIFT;
    int m2 = m & ~plat::SHIFT;
    size_t n = e.t.size();
    auto move = [&](size_t to) {
        e.cur = std::min(to, n);
        if (!shift) e.anc = e.cur;
        e.style = -1;
        e.last.clear();
        ui.consume();
        return true;
    };
#ifdef __APPLE__
    const int WORD = plat::ALT, LINE = plat::CMD;
#else
    const int WORD = plat::CTRL, LINE = -1;
#endif
    if (key == plat::K_LEFT && (m2 == 0 || m2 == WORD || m2 == LINE)) {
        e.goal = -1;
        if (m2 == 0 && has_sel(e) && !shift) return move(sel_a(e));
        if (m2 == WORD) return move(word_left(e, e.cur));
        if (m2 == LINE) return move(e.lines.empty() ? 0 : e.lines[line_of(e, e.cur)].a);
        return move(e.cur ? e.cur - 1 : 0);
    }
    if (key == plat::K_RIGHT && (m2 == 0 || m2 == WORD || m2 == LINE)) {
        e.goal = -1;
        if (m2 == 0 && has_sel(e) && !shift) return move(sel_b(e));
        if (m2 == WORD) return move(word_right(e, e.cur));
        if (m2 == LINE) return move(e.lines.empty() ? n : e.lines[line_of(e, e.cur)].b);
        return move(e.cur + 1);
    }
    if ((key == plat::K_UP || key == plat::K_DOWN || key == plat::K_PGUP || key == plat::K_PGDN) && m2 == 0) {
        if (e.single) {
            if (key == plat::K_UP || key == plat::K_DOWN) return false;   // (a drop-down's arrows elsewhere)
            return false;
        }
        size_t l = line_of(e, e.cur);
        if (e.goal < 0) e.goal = x_of(e, l, e.cur);
        long d = key == plat::K_UP ? -1 : key == plat::K_DOWN ? 1 : key == plat::K_PGUP ? -(long)page_lines : (long)page_lines;
        long to = (long)l + d;
        if (to < 0) return move(0), e.goal = -1, true;
        if (to >= (long)e.lines.size()) return move(n), e.goal = -1, true;
        double g = e.goal;
        move(index_at(e, (size_t)to, g));
        e.goal = g;
        return true;
    }
    if (key == plat::K_HOME) {
        e.goal = -1;
        if (m2 == P || e.single) return move(0);
        if (m2 == 0) return move(e.lines.empty() ? 0 : e.lines[line_of(e, e.cur)].a);
    }
    if (key == plat::K_END) {
        e.goal = -1;
        if (m2 == P || e.single) return move(n);
        if (m2 == 0) {
            if (e.lines.empty()) return move(n);
            size_t l = line_of(e, e.cur);
            size_t b = e.lines[l].b;
            if (soft_end(e, l) && b > e.lines[l].a) b--;   // a wrapped line: before the next line's start
            return move(b);
        }
    }
    if (m == P && key == 'a') {
        e.anc = 0;
        e.cur = n;
        ui.consume();
        return true;
    }
    if (m == P && (key == 'c' || key == 'x')) {
        if (has_sel(e)) {
            U32 part = e.t.substr(sel_a(e), sel_b(e) - sel_a(e));
            std::vector<int> pf(e.fl.begin() + (long)sel_a(e), e.fl.begin() + (long)sel_b(e));
            g_clip.plain = to8(part);
            g_clip.marked = e.rich ? markup::marked(g_clip.plain, pf) : g_clip.plain;
            ui.win.set_clipboard(g_clip.plain);
            if (key == 'x' && enabled) erase(e, sel_a(e), sel_b(e));
        }
        ui.consume();
        return true;
    }
    if (m == P && key == 'v') {
        ui.consume();
        if (!enabled) return true;
        std::string clip = ui.win.clipboard();
        if (clip.empty()) return true;
        insert_marked(e, clip == g_clip.plain && !g_clip.marked.empty() ? g_clip.marked : clip);
        return true;
    }
    if (m == P && key == 'z') {
        ui.consume();
        if (enabled) do_undo(e);
        return true;
    }
    if ((m == (P | plat::SHIFT) && key == 'z') || (m == P && key == 'y')) {
        ui.consume();
        if (enabled) do_redo(e);
        return true;
    }
    if (e.rich && (m == P || (plat::PRIMARY == plat::CMD && m == plat::CTRL)) && (key == 'b' || key == 'i' || key == 'u' || key == 't')) {
        ui.consume();
        if (enabled) toggle(e, key == 't' ? 'i' : (char)key);
        return true;
    }
    if (!enabled) return false;
    if (key == plat::K_BACKSPACE && (m2 == 0 || m2 == WORD)) {
        ui.consume();
        if (has_sel(e)) erase(e, sel_a(e), sel_b(e));
        else if (e.cur > 0) erase(e, m2 == WORD ? word_left(e, e.cur) : e.cur - 1, e.cur);
        return true;
    }
    if (key == plat::K_DELETE && (m2 == 0 || m2 == WORD)) {
        ui.consume();
        if (has_sel(e)) erase(e, sel_a(e), sel_b(e));
        else if (e.cur < n) erase(e, e.cur, m2 == WORD ? word_right(e, e.cur) : e.cur + 1);
        return true;
    }
    if (key == plat::K_RETURN && m == 0 && !e.single) {
        ui.consume();
        type_text(e, "\n");
        return true;
    }
    // printable keys arrive as TEXT too: let them through without using them here
    return false;
}

// ---- the mouse
void mouse(Ui &ui, Edit &e, Id id, double ox, double oy, double lh, bool focus) {
    auto at = [&](double x, double y) {
        if (e.lines.empty()) return (size_t)0;
        long l = (long)std::floor((y - oy) / lh);
        l = std::max(0L, std::min((long)e.lines.size() - 1, l));
        return index_at(e, (size_t)l, x - ox);
    };
    if (ui.has(plat::Event::DOWN) && ui.ev.button == 1 && ui.held(id)) {
        // hit() made this control active; work out where
        size_t p = at(ui.mx, ui.my);
        int clicks = ui.ev.clicks;
        bool shift = ui.ev.mods & plat::SHIFT;
        e.style = -1;
        e.last.clear();
        e.goal = -1;
        if (clicks >= 3) {
            size_t l = line_of(e, p);
            e.drag = 3;
            e.drag_a = e.anc = e.lines[l].a;
            e.drag_b = e.cur = e.lines[l].b;
        } else if (clicks == 2) {
            size_t a = p, b = p;
            while (a > 0 && word_char(e.t[a - 1])) a--;
            while (b < e.t.size() && word_char(e.t[b])) b++;
            if (a == b && b < e.t.size()) b++;
            e.drag = 2;
            e.drag_a = e.anc = a;
            e.drag_b = e.cur = b;
        } else {
            e.drag = 1;
            e.cur = p;
            if (!shift || !focus) e.anc = p;
        }
        ui.consume();
        ui.again();
    } else if (ui.has(plat::Event::MOVE) && ui.held(id) && e.drag) {
        size_t p = at(ui.mx, ui.my);
        if (e.drag == 1) e.cur = p;
        else if (p < e.drag_a) {
            e.anc = e.drag_b;
            e.cur = e.drag == 2 ? word_left(e, p) : e.lines[line_of(e, p)].a;
        } else {
            e.anc = e.drag_a;
            e.cur = e.drag == 2 ? std::max(e.drag_b, word_right(e, p)) : e.lines[line_of(e, p)].b;
        }
        ui.again();
    } else if (ui.has(plat::Event::UP))
        e.drag = 0;
}

void draw_text(Ui &ui, Edit &e, double ox, double oy, double lh, bool focus, bool enabled, const Rect &clipr) {
    Skin &k = ui.sk();
    int S = ui.S();
    size_t a = sel_a(e), b = sel_b(e);
    for (size_t l = 0; l < e.lines.size(); l++) {
        double y = oy + l * lh;
        if (y + lh < clipr.y || y > clipr.b()) continue;
        const auto &L = e.lines[l];
        double base = std::round(y + (lh - k.line) / 2 + k.f->ascent);
        // the selection's background
        if (focus && a < b) {
            size_t s0 = std::max(a, L.a), s1 = std::min(b, L.b);
            bool nl = L.b < e.t.size() && e.t[L.b] == '\n' && b > L.b && a <= L.b;
            if (s0 < s1 || nl) {
                double x0 = s0 < s1 ? x_of(e, l, s0) : L.end;
                double x1 = s0 < s1 ? x_of(e, l, s1) : L.end;
                if (nl) x1 += 6 * S;
                ui.fill({ox + x0, y, x1 - x0, lh}, k["sel"]);
            }
        }
        // the text, in runs of one style
        size_t p = L.a;
        while (p < L.b) {
            size_t q = p;
            bool insel = focus && p >= a && p < b;
            while (q < L.b && e.fl[q] == e.fl[p] && ((focus && q >= a && q < b) == insel)) q++;
            int bits = e.fl[p];
            Font &f = font_for(k, bits);
            Color fg = !enabled ? k["off"] : insel ? k["seltext"] : k["text"];
            std::string run = to8(e.t.substr(p, q - p));
            double x = ox + e.xs[p];
            double x2 = ui.text(f, x, base, run, fg);
            if (bits & markup::U) ui.fill({x, base + std::max(1, S), x2 - x, (double)std::max(1, S)}, fg);
            p = q;
        }
    }
    // the cursor
    if (focus && enabled) {
        size_t l = line_of(e, e.cur);
        double x = ox + x_of(e, l, e.cur);
        double y = oy + l * lh;
        ui.fill({std::round(x), y + (lh - k.line) / 2, (double)std::max(2, S + 1), k.line}, k["text"]);
        ui.win.text_spot(x, y, lh);
    }
}

}  // namespace

// ----------------------------------------------------------------------------- one line

bool entry_at(Ui &ui, Id id, const Rect &r, std::string &value, bool enabled) {
    Skin &k = ui.sk();
    int S = ui.S();
    Edit &e = ui.state<Edit>(id);
    e.single = true;
    e.rich = false;
    if (!e.init || value != e.synced) load(e, value);
    ui.hit(id, r, true, enabled);
    bool focus = ui.focused(id);
    if (e.want_end) {
        e.anc = e.cur = e.t.size();
        e.want_end = false;
    } else if (focus && !e.focused_before && !ui.held(id)) {   // tabbed in (not clicked): everything selected, like Tk
        e.anc = 0;
        e.cur = e.t.size();
    }
    e.focused_before = focus;
    if (ui.over(r)) ui.cursor(plat::IBEAM);
    double ring = k.modern ? 2 * S : S, bd = (k.modern ? 5 : 3) * S;
    Rect in{r.x + ring + bd, r.y + ring + bd, r.w - 2 * (ring + bd), r.h - 2 * (ring + bd)};
    layout(e, k, 1e9);
    double ox = in.x - e.sx, oy = in.y;
    double lh = in.h;
    mouse(ui, e, id, ox, oy, lh, focus);
    if (focus) keys(ui, e, enabled, 1);
    layout(e, k, 1e9);
    // keep the cursor in view
    double cx = e.lines.empty() ? 0 : x_of(e, 0, e.cur);
    if (cx - e.sx > in.w - 2 * S) e.sx = cx - in.w + 2 * S;
    if (cx - e.sx < 0) e.sx = cx;
    double total = e.lines.empty() ? 0 : e.lines[0].end;
    e.sx = std::max(0.0, std::min(e.sx, std::max(0.0, total - in.w + 2 * S)));
    ox = in.x - e.sx;
    bool changed = false;
    std::string now = value_of(e);
    if (now != e.synced) {
        e.synced = now;
        value = now;
        changed = true;
    }
    if (!ui.drawing) return changed;
    ui.fill(r, enabled ? k["field"] : k["window"]);
    ui.box(r, focus ? k["focus"] : k["edge"], ring);
    ui.clip(in.in(-bd + S, 0));
    draw_text(ui, e, ox, oy, lh, focus, enabled, r);
    ui.unclip();
    return changed;
}

void focus_at_end(Ui &ui, Id id) {
    Edit &e = ui.state<Edit>(id);
    e.want_end = true;
    e.focused_before = true;
    ui.focus(id);
}

bool entry(Ui &ui, const std::string &key, std::string &value, double width, bool enabled, bool select_all) {
    (void)select_all;
    Skin &k = ui.sk();
    int S = ui.S();
    double h = k.line + 2 * ((k.modern ? 5 : 3) * S + (k.modern ? 2 : 1) * S);
    Rect r = ui.place(width > 0 ? width : 0, h);
    return entry_at(ui, ui.id("entry:" + key), r, value, enabled);
}

// ----------------------------------------------------------------------------- several lines

bool text_area(Ui &ui, const std::string &key, std::string &value, int min_lines, int max_lines, bool rich, bool enabled) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("text:" + key);
    Edit &e = ui.state<Edit>(id);
    e.single = false;
    e.rich = rich;
    if (!e.init || value != e.synced) load(e, value);
    double ring = k.modern ? 2 * S : S;
    double padx = (k.modern ? 6 : 4) * S, pady = (k.modern ? 4 : 3) * S;
    double lh = k.line + 2 * S;   // Tk's spacing above and below each line
    double w = ui.width();
    double inner_w = w - 2 * (ring + padx) - 2 * S;
    layout(e, k, inner_w);
    int shown = std::max(min_lines, std::min(max_lines, (int)e.lines.size()));
    double h = shown * lh + 2 * (ring + pady);
    Rect r = ui.place(0, h);
    ui.hit(id, r, true, enabled);
    bool focus = ui.focused(id);
    if (focus) g_rich = rich ? id : 0;
    if (ui.over(r)) ui.cursor(plat::IBEAM);
    Rect in{r.x + ring + padx, r.y + ring + pady, r.w - 2 * (ring + padx), r.h - 2 * (ring + pady)};
    double oy = in.y - e.sy;
    mouse(ui, e, id, in.x, oy, lh, focus);
    // the wheel scrolls a box that's full
    double content = e.lines.size() * lh;
    if (ui.has(plat::Event::WHEEL) && ui.over(r) && content > in.h + 0.5) {
        double before = e.sy;
        e.sy = std::max(0.0, std::min(content - in.h, e.sy + ui.ev.dy * lh * 3));
        if (e.sy != before) {
            ui.consume();
            ui.again();
        }
    }
    if (g_toggle && g_rich == id) {   // the format bar was pressed
        if (enabled) toggle(e, g_toggle);
        g_toggle = 0;
        ui.focus(id);
    }
    if (focus) keys(ui, e, enabled, std::max(1.0, std::floor(in.h / lh) - 1));
    layout(e, k, inner_w);
    // keep the cursor in view
    content = e.lines.size() * lh;
    if (focus && !e.lines.empty()) {
        double cy = line_of(e, e.cur) * lh;
        if (cy < e.sy) e.sy = cy;
        if (cy + lh > e.sy + in.h) e.sy = cy + lh - in.h;
    }
    e.sy = std::max(0.0, std::min(e.sy, std::max(0.0, content - in.h)));
    bool changed = false;
    std::string now = value_of(e);
    if (now != e.synced) {
        e.synced = now;
        value = now;
        changed = true;
    }
    int lines_now = std::max(min_lines, std::min(max_lines, (int)e.lines.size()));
    if (lines_now != shown) ui.again();   // it grew or shrank
    if (!ui.drawing) return changed;
    ui.fill(r, enabled ? k["field"] : k["window"]);
    ui.box(r, focus ? k["focus"] : k["edge"], ring);
    ui.clip(r.in(ring));
    draw_text(ui, e, in.x, in.y - e.sy, lh, focus, enabled, r);
    ui.unclip();
    return changed;
}

void format_bar(Ui &ui) {
    Skin &k = ui.sk();
    int S = ui.S();
    int bits = 0;
    bool live = false;
    if (g_rich) {
        Edit &e = ui.state<Edit>(g_rich);
        if (e.init && e.rich) {
            bits = formats(e);
            live = true;
        }
    }
    struct B {
        const char *label;
        char kind;
        int bit;
        int style;
    };
    for (const B &b : {B{"B", 'b', markup::B, 1}, B{"I", 'i', markup::I, 2}, B{"U", 'u', markup::U, 0}}) {
        Font &f = b.style ? k.font(b.style) : *k.fsmall;
        double w = std::max(26.0 * S, f.width(b.label) + 16 * S), h = k.fsmall->line + 8 * S;
        if (k.modern) {
            w += 6 * S;
            h += 6 * S;
        }
        Rect r = ui.place(w, h);
        if (button_at(ui, ui.id(std::string("fmt:") + b.label), r, b.label, live, false, true, live && (bits & b.bit), &f, false)) g_toggle = b.kind;
        if (b.kind == 'u' && ui.drawing) {   // the U is underlined
            double tw = f.width("U");
            ui.fill({std::round(r.x + r.w / 2 - tw / 2), std::round(r.y + r.h / 2 + f.line / 2 - f.descent + S), tw, (double)S},
                    live ? (bits & b.bit && !k.bevel ? k["light"] : k["text"]) : k["off"]);
        }
    }
}

void text_view(Ui &ui, const std::string &key, const Rect &r, const std::vector<std::string> &lines) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("view:" + key);
    ui.fill(r, k["dark"]);
    Rect page = r.in(S);
    ui.fill(page, k["field"]);
    ui.begin_scroll(id, page);
    Font &f = *k.fmono;
    double padx = 24 * S, pady = 18 * S;
    ui.space(pady);
    Rect area = ui.place(0, f.line * (double)lines.size() + pady);
    if (ui.drawing) {
        Rect c = ui.clip_rect();
        for (size_t i = 0; i < lines.size(); i++) {
            double y = area.y + i * f.line;
            if (y + f.line < c.y || y > c.b()) continue;
            ui.text(f, area.x + padx, std::round(y + f.ascent), lines[i], k["text"]);
        }
    }
    ui.hit(id, page, true, true);
    if (ui.focused(id)) {
        double &sy = ui.scroll_of(id);
        double step = f.line;
        if (ui.key(plat::K_DOWN)) sy += step, ui.again();
        else if (ui.key(plat::K_UP)) sy -= step, ui.again();
        else if (ui.key(plat::K_PGDN) || ui.key(' ')) sy += page.h - step, ui.again();
        else if (ui.key(plat::K_PGUP)) sy -= page.h - step, ui.again();
        else if (ui.key(plat::K_HOME)) sy = 0, ui.again();
        else if (ui.key(plat::K_END)) sy = 1e12, ui.again();
    }
    ui.end_scroll();
}

}  // namespace ui
