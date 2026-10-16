#include "tui.h"

#include <algorithm>
#include <cmath>

#include "art.h"
#include "markup.h"
#include "text.h"

namespace tui {

using U32 = std::u32string;

static bool is_back(int k) { return k == term::K_ESC || k == term::K_BACKSPACE || k == term::K_LEFT; }

static bool printable(int k) {
    if (k < 0x20 || k >= term::K_ENTER) return false;
    if (k == 0x7f || (k >= 0x80 && k < 0xA0)) return false;
    if (k >= 0xD800 && k <= 0xDFFF) return false;
    return true;
}

static std::string ch8(int k) { return printable(k) ? text::to8((char32_t)k) : ""; }

std::string truncate(const std::string &s0, long n) {
    std::string s = text::replace(text::replace(s0, "\n", " "), "\t", " ");
    if (n <= 0) return "";
    long len = (long)text::length(s);
    return len <= n ? s : text::slice(s, 0, std::max(0L, n - 3)) + "...";
}

std::vector<std::pair<long, long>> wrap_rows(const U32 &buf, long width) {
    std::vector<std::pair<long, long>> rows;
    long start = 0, n = (long)buf.size();
    for (;;) {
        size_t f = buf.find(U'\n', (size_t)start);
        long nl = f == U32::npos ? -1 : (long)f;
        long end = nl != -1 ? nl : n;
        long s = start;
        if (s == end) rows.push_back({s, s});
        while (s < end) {
            if (end - s <= width) {
                rows.push_back({s, end});
                break;
            }
            long cut = -1;   // buf.rfind(" ", s, s + width)
            for (long i = std::min(s + width, end) - 1; i >= s; i--)
                if (buf[(size_t)i] == U' ') { cut = i; break; }
            if (cut <= s) {
                rows.push_back({s, s + width});
                s += width;
            } else {
                rows.push_back({s, cut + 1});
                s = cut + 1;
            }
        }
        if (nl == -1) break;
        start = nl + 1;
    }
    return rows;
}

// ----------------------------------------------------------------------------- the screen

UI::UI(term::Screen &screen, std::function<json::Value &()> st) : s(screen), settings(st) {
#ifdef _WIN32
    animate = false;   // the Windows console is slow at refreshing once per row
#endif
    apply_theme();
}

void UI::apply_theme() {
    struct Theme { const char *name; int fg, dim, hi, basic; };
    static const Theme THEMES[] = {{"green", 83, 28, 157, 2}, {"amber", 214, 130, 222, 3}, {"white", 252, 243, 231, 7}};
    Theme t = THEMES[0];
    for (const auto &x : THEMES) if (settings().s("theme") == x.name) t = x;
    int fg = t.fg, dim = t.dim, hi = t.hi, bg, extra_dim = 0, extra_hi = 0;
    if (s.colors() >= 256) bg = 16;
    else {
        fg = dim = hi = t.basic;
        bg = 0;
        extra_dim = term::A_DIM;
        extra_hi = term::A_BOLD;
    }
    N = {fg, bg, 0};
    INV = {bg, fg, 0};
    DIM = {dim, bg, extra_dim};
    HI = {hi, bg, term::A_BOLD | extra_hi};
    int tones[4] = {bg, dim, fg, hi};   // . d n h
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++) pix_[a][b] = {tones[a], tones[b], 0};
}

static int tone(char c) { return c == 'd' ? 1 : c == 'n' ? 2 : c == 'h' ? 3 : 0; }

Style UI::pix(char top, char bot) const { return pix_[tone(top)][tone(bot)]; }

void UI::dims(int &rows, int &w, int &x0) const {
    rows = s.rows();
    w = std::min(s.cols(), 110);
    x0 = 0;
}

std::string UI::tx(const std::string &t) const {
    const json::Value &st = settings();
    bool upper = st.has("uppercase") ? st.get("uppercase").truthy() : true;
    return upper ? text::upper(t) : t;
}

void UI::put(int y, int x, const std::string &t, const Style *st, bool raw) { buf_.push_back({y, x, raw ? t : tx(t), st ? *st : N}); }

void UI::draw(int y, int x, const std::string &t, Style st) {
    if (y < 0 || y >= s.rows() || x >= s.cols() || x < 0) return;
    s.put(y, x, text::slice(t, 0, s.cols() - x), st);
}

void UI::show(bool anim, int cy, int cx) {
    anim = anim && animate;
    s.clear(N);
    if (s.rows() < 16 || s.cols() < 60) {
        s.cursor(-1, -1);
        draw(0, 0, "TERMINAL TOO SMALL (NEED 60X16)", HI);
        s.present();
        return;
    }
    std::vector<Item> items = buf_;
    if (anim) std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.y < b.y; });
    s.cursor(-1, -1);
    int last = -1000000;
    for (const auto &it : items) {
        if (anim && last != -1000000 && it.y != last) {
            s.present();
            s.sleep_ms(8);
            int k = s.read(0);
            if (k != term::K_NONE) {
                s.unread(k);
                anim = false;
            }
        }
        last = it.y;
        draw(it.y, it.x, it.text, it.st);
    }
    s.cursor(cy, cx);
    s.present();
}

std::vector<std::pair<int, std::pair<std::string, Style>>> UI::sprite_runs_(const std::vector<std::string> &rows, int &) const {
    std::vector<std::pair<int, std::pair<std::string, Style>>> out;   // (row * 1000 + col, (text, style))
    for (size_t r = 0; r + 1 < rows.size(); r += 2) {
        const std::string &top = rows[r], &bot = rows[r + 1];
        for (size_t c = 0; c < top.size() && c < bot.size(); c++) {
            char a = top[c], b = bot[c];
            if (a == '.' && b == '.') continue;
            if (a == b) out.push_back({(int)(r / 2) * 1000 + (int)c, {"\xE2\x96\x88", pix(a, '.')}});
            else out.push_back({(int)(r / 2) * 1000 + (int)c, {"\xE2\x96\x80", pix(a, b)}});
        }
    }
    return out;
}

void UI::put_sprite(int y, int x, const std::vector<std::string> &rows) {
    int w;
    for (const auto &r : sprite_runs_(rows, w)) put(y + r.first / 1000, x + r.first % 1000, r.second.first, &r.second.second, true);
}

void UI::draw_sprite(int y, int x, const std::vector<std::string> &rows, bool clear) {
    if (clear && !rows.empty())
        for (size_t dy = 0; dy < rows.size() / 2; dy++) draw(y + (int)dy, x, std::string(rows[0].size(), ' '), N);
    int w;
    for (const auto &r : sprite_runs_(rows, w)) draw(y + r.first / 1000, x + r.first % 1000, r.second.first, r.second.second);
}

void UI::header(const std::string &title, bool raw) {
    int cols = s.cols();
    put(0, 0, std::string((size_t)std::max(0, cols), ' '), &INV);
    put(0, 1, art::APP, &INV);
    std::string t = truncate(title, cols - 34);
    put(0, (cols - (int)text::length(t)) / 2, t, &INV, raw);
    std::string clock = strftime_now("%a %b %d %H:%M");
    put(0, cols - (int)text::length(clock) - 1, clock, &INV);
}

void UI::footer(const std::string &hints) {
    int rows, w, x0;
    dims(rows, w, x0);
    if (!msg.empty()) {
        put(rows - 2, x0, truncate(msg, w), &HI, true);
        msg.clear();
    } else put(rows - 2, x0, truncate(hints, w), &DIM);
    put(rows - 1, x0, "]", &N);
}

void UI::badge(int y, int x, const std::string &label) { put(y, x, " " + label + " ", &INV); }

int UI::key() {
    bool on = true;
    for (;;) {
        int rows, w, x0;
        dims(rows, w, x0);
        draw(rows - 1, x0 + 1, on ? "\xE2\x96\x88" : " ", N);
        s.present();
        int k = s.read(450);
        if (k == term::K_NONE) {
            on = !on;
            continue;
        }
        return k;
    }
}

void UI::echo(const std::string &c) {
    int rows, w, x0;
    dims(rows, w, x0);
    draw(rows - 1, x0 + 1, tx(c) + "\xE2\x96\x88", N);
    s.present();
    s.sleep_ms(80);
}

bool UI::prompt(const std::string &label, std::string &out, const std::string &initial, bool replace, bool raw) {
    U32 buf = text::to32(initial);
    long pos = (long)buf.size();
    bool fresh = replace && !buf.empty();
    for (;;) {
        int rows, w, x0;
        dims(rows, w, x0);
        std::string head = "]" + (raw ? label : tx(label)) + "? ";
        long hl = (long)text::length(head);
        long room = std::max(10L, (long)w - hl - 1);
        long off = std::max(0L, pos - room + 1);
        U32 shown = buf.substr((size_t)std::min(off, (long)buf.size()), (size_t)room);
        draw(rows - 1, x0, std::string((size_t)w, ' '), N);
        draw(rows - 1, x0, head, HI);
        draw(rows - 1, x0 + (int)hl, text::to8(shown), N);
        s.cursor(rows - 1, (int)std::min((long)x0 + hl + pos - off, (long)x0 + w - 1));
        s.present();
        int k = s.read(-1);
        if (k == term::K_CLOSED) return false;
        if (k == term::K_RESIZE || k == term::K_NONE) continue;
        if (fresh && printable(k)) {
            buf.clear();
            pos = 0;
        }
        fresh = false;
        if (k == term::K_ENTER) {
            s.cursor(-1, -1);
            out = text::strip(text::to8(buf));
            return true;
        }
        if (k == term::K_ESC) {
            s.cursor(-1, -1);
            return false;
        }
        if (k == term::K_BACKSPACE) {
            if (pos) buf.erase((size_t)--pos, 1);
        } else if (k == term::K_DELETE) {
            if (pos < (long)buf.size()) buf.erase((size_t)pos, 1);
        } else if (k == term::K_LEFT) pos = std::max(0L, pos - 1);
        else if (k == term::K_RIGHT) pos = std::min((long)buf.size(), pos + 1);
        else if (k == term::K_HOME || k == 0x01) pos = 0;
        else if (k == term::K_END || k == 0x05) pos = (long)buf.size();
        else if (k == 0x15) {
            buf.clear();
            pos = 0;
        } else if (printable(k)) buf.insert((size_t)pos++, 1, (char32_t)k);
    }
}

bool UI::confirm(const std::string &question) {
    std::string ans;
    if (!prompt(tx(question) + " (Y/n)", ans, "", false, true)) return false;
    return ans.empty() || text::starts_with(text::lower(ans), "y");
}

bool UI::edit_text(const std::string &title, std::string &textv, const std::string &help, std::function<std::string(const std::string &)> fill,
                   const std::string &fill_hint, const std::string &fill_prefix) {
    U32 buf = text::to32(textv);
    long pos = (long)buf.size(), top = 0;
    U32 original = buf;
    U32 filled;
    bool has_filled = false;
    if (fill && text::starts_with(textv, fill_prefix) && text::strip(textv).find('\n') == std::string::npos) {
        filled = buf;
        has_filled = true;
    }
    long goal_col = -1;
    for (;;) {
        int rows, w, x0;
        dims(rows, w, x0);
        long width = w - 4;
        auto vis = wrap_rows(buf, width);
        long r = 0;
        for (long i = 0; i < (long)vis.size(); i++)
            if (vis[(size_t)i].first <= pos) r = i;
        long col = pos - vis[(size_t)r].first;
        long body = rows - 5;
        if (r < top) top = r;
        else if (r >= top + body) top = r - body + 1;
        begin();
        header("EDIT: " + title);
        put(1, x0 + 1, help.empty() ? "TYPE FREELY.  START A LINE WITH '- ' FOR A BULLET." : help, &DIM);
        put(2, x0, std::string((size_t)w, '-'), &DIM);
        // a look for every character: formatted text bold/italic/underlined, the marks dimmed
        std::string b8 = text::to8(buf);
        std::vector<Style> looks(buf.size(), N);
        for (const auto &sp : markup::spans(b8)) {
            Style st = sp.mark ? DIM : N;
            if (!sp.mark) {
                if (sp.bits & markup::B) st.attr |= term::A_BOLD;
                if (sp.bits & markup::I) st.attr |= term::A_ITALIC;
                if (sp.bits & markup::U) st.attr |= term::A_UNDER;
            }
            for (long i = sp.start; i < sp.end && i < (long)looks.size(); i++) looks[(size_t)i] = st;
        }
        for (long i = 0; i < body && top + i < (long)vis.size(); i++) {
            long a = vis[(size_t)(top + i)].first, b = vis[(size_t)(top + i)].second;
            U32 ln = buf.substr((size_t)a, (size_t)(b - a));
            while (!ln.empty() && ln.back() == U'\n') ln.pop_back();
            long j = 0;
            while (j < (long)ln.size()) {   // one put per run of the same look
                long k = j;
                while (k < (long)ln.size() && looks[(size_t)(a + k)] == looks[(size_t)(a + j)]) k++;
                put(3 + (int)i, x0 + 2 + (int)j, text::to8(ln.substr((size_t)j, (size_t)(k - j))), &looks[(size_t)(a + j)], true);
                j = k;
            }
        }
        long words = (long)text::split_ws(b8).size();
        put(rows - 2, x0,
            "ESC SAVE+EXIT   CTRL-X CANCEL   " + (fill ? fill_hint + "   " : std::string()) + "^B BOLD  ^T ITALIC  ^U UNDERLINE   LINE " +
                std::to_string(r + 1) + "/" + std::to_string(vis.size()) + "   " + std::to_string(words) + " WORDS",
            &DIM);
        show(false, 3 + (int)(r - top), x0 + 2 + (int)col);
        int k = s.read(-1);
        if (k == term::K_CLOSED) {
            textv = text::to8(buf);   // keep what was typed
            return true;
        }
        if (k == term::K_RESIZE || k == term::K_NONE) continue;
        if (k == term::K_ESC) {
            s.cursor(-1, -1);
            textv = text::to8(buf);
            return true;
        }
        if (k == 0x18) {
            if (buf == original || confirm("DISCARD CHANGES")) {
                s.cursor(-1, -1);
                return false;
            }
            continue;
        }
        if (k == 0x02 || k == 0x14 || k == 0x15) {   // Ctrl-B / Ctrl-T / Ctrl-U (Ctrl-I is Tab in a terminal)
            auto got = markup::toggle(text::to8(buf), pos, k == 0x02 ? 'b' : k == 0x14 ? 'i' : 'u');
            buf = text::to32(got.first);
            pos = got.second;
            continue;
        }
        if (k == 0x07 && fill) {
            if (text::strip(text::to8(buf)).empty() || (has_filled && buf == filled)) {
                buf = filled = text::to32(fill(text::to8(buf)));
                has_filled = true;
                pos = (long)buf.size();
            } else {
                U32 add = text::to32(fill(text::to8(buf)));
                buf.insert((size_t)pos, add);
                pos += (long)add.size();
            }
            continue;
        }
        bool vertical = k == term::K_UP || k == term::K_DOWN || k == term::K_PGUP || k == term::K_PGDN;
        if (!vertical) goal_col = -1;
        if (k == term::K_ENTER) buf.insert((size_t)pos++, 1, U'\n');
        else if (k == term::K_BACKSPACE) {
            if (pos) buf.erase((size_t)--pos, 1);
        } else if (k == term::K_DELETE) {
            if (pos < (long)buf.size()) buf.erase((size_t)pos, 1);
        } else if (k == term::K_LEFT) pos = std::max(0L, pos - 1);
        else if (k == term::K_RIGHT) pos = std::min((long)buf.size(), pos + 1);
        else if (vertical) {
            long step = k == term::K_UP ? -1 : k == term::K_DOWN ? 1 : k == term::K_PGUP ? -(body - 1) : body - 1;
            if (goal_col < 0) goal_col = col;
            long nr = std::max(0L, std::min((long)vis.size() - 1, r + step));
            if (nr == r) pos = step < 0 ? 0 : (long)buf.size();
            else {
                long a = vis[(size_t)nr].first, b = vis[(size_t)nr].second, limit = b - a;
                if (nr + 1 < (long)vis.size() && vis[(size_t)nr + 1].first == b && b > a) limit--;   // a soft-wrapped row: not past the wrap
                pos = a + std::min(goal_col, limit);
            }
        } else if (k == term::K_HOME || k == 0x01) pos = vis[(size_t)r].first;
        else if (k == term::K_END || k == 0x05) {
            long a = vis[(size_t)r].first, b = vis[(size_t)r].second;
            pos = (r + 1 < (long)vis.size() && vis[(size_t)r + 1].first == b && b > a) ? b - 1 : b;
        } else if (k == '\t') {
            buf.insert((size_t)pos, U"  ");
            pos += 2;
        } else if (printable(k)) buf.insert((size_t)pos++, 1, (char32_t)k);
    }
}

void UI::view_lines(const std::string &title, const std::vector<std::pair<std::string, Style>> &lines, const std::string &hints, bool raw_title) {
    long top = 0;
    bool first = true;
    for (;;) {
        int rows, w, x0;
        dims(rows, w, x0);
        long body = rows - 4, maxtop = std::max(0L, (long)lines.size() - body);
        top = std::max(0L, std::min(top, maxtop));
        begin();
        header(title, raw_title);
        for (long i = 0; i < body && top + i < (long)lines.size(); i++) put(2 + (int)i, x0 + 2, lines[(size_t)(top + i)].first, &lines[(size_t)(top + i)].second, true);
        long pct = maxtop == 0 ? 100 : (long)std::nearbyint((double)top / (double)maxtop * 100);
        std::string p = std::to_string(pct);
        footer(std::string(3 - std::min<size_t>(3, p.size()), ' ') + p + "%  " + hints);
        show(first);
        first = false;
        int k = key();
        std::string c = text::lower(ch8(k));
        if (k == term::K_CLOSED || is_back(k) || c == "q") return;
        if (k == term::K_DOWN || c == "j") top++;
        else if (k == term::K_UP || c == "k") top--;
        else if (c == " " || k == term::K_PGDN) top += body - 2;
        else if (c == "b" || k == term::K_PGUP) top -= body - 2;
        else if (k == term::K_HOME) top = 0;
        else if (k == term::K_END) top = maxtop;
    }
}

void UI::list_screen(const std::string &title, const std::function<long()> &count, const std::function<Row(long, int)> &row,
                     const std::function<Result(long)> &on_open, const std::function<Result(int, long)> &on_key, const std::string &hints,
                     const std::function<std::string()> &info_fn, const std::string &empty, long start) {
    long page = 0, sel = 0, pending = start;
    bool has_pending = true, first = true;
    for (;;) {
        long n = count();
        int rows, w, x0;
        dims(rows, w, x0);
        long per = std::max(1, std::min(10, rows - 6));
        if (has_pending) {
            long p = std::max(0L, pending);
            page = p / per;
            sel = p % per;
            has_pending = false;
        }
        long pages = std::max(1L, (n + per - 1) / per);
        page = std::max(0L, std::min(page, pages - 1));
        long lo = page * per, hi = std::min(n, lo + per), chunk = std::max(0L, hi - lo);
        sel = std::max(0L, std::min(sel, chunk - 1));
        long cur = chunk ? lo + sel : -1;
        begin();
        header(title);
        std::string info = info_fn ? info_fn() : std::to_string(n) + " ITEMS";
        std::string pg = pages > 1 ? "PAGE " + std::to_string(page + 1) + "/" + std::to_string(pages) : "";
        put(1, x0 + 1, truncate(info, w - (long)pg.size() - 3), &DIM, true);
        if (!pg.empty()) put(1, x0 + w - (int)pg.size() - 1, pg, &DIM);
        for (long i = 0; i < chunk; i++) {
            int y = 3 + (int)i;
            badge(y, x0 + 1, std::to_string((i + 1) % 10));
            Row rw = row(lo + i, w - 7);
            std::string t = (long)text::length(rw.text) > w - 7 ? truncate(rw.text, w - 7) : rw.text;
            if (i == sel) put(y, x0 + 5, " " + text::ljust(t, w - 7), &INV, true);
            else put(y, x0 + 6, t, rw.has_style ? &rw.st : &N, true);
        }
        if (!n) put(3, x0 + 2, empty, &N);
        footer(hints);
        show(first);
        first = false;
        int k = key();
        if (k == term::K_CLOSED) return;
        std::string c = ch8(k);
        Result res;
        if (c.size() == 1 && c[0] >= '0' && c[0] <= '9' && on_open) {
            long i = ((c[0] - '0') + 9) % 10;
            if (i < chunk) {
                sel = i;
                echo(c);
                res = on_open(lo + i);
                first = true;
            }
        } else if (k == term::K_ENTER && chunk && on_open) {
            res = on_open(cur);
            first = true;
        } else if (k == term::K_DOWN) {
            if (sel < chunk - 1) sel++;
            else if (page < pages - 1) { page++; sel = 0; }
        } else if (k == term::K_UP) {
            if (sel > 0) sel--;
            else if (page > 0) { page--; sel = per - 1; }
        } else if (k == term::K_PGDN || k == term::K_RIGHT || c == ">") {
            page++;
            sel = 0;
        } else if (k == term::K_PGUP || c == "<") {
            page--;
            sel = 0;
        } else if (is_back(k)) return;
        else if (on_key) res = on_key(k, cur);
        if (res.kind == Result::BACK) return;
        if (res.kind == Result::MOVE) {
            pending = res.to;
            has_pending = true;
        }
    }
}

static const char *MONTH_ABBR[13] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *MONTH_FULL[13] = {"", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
static const char *WEEKDAY_FULL[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

UI::Picked UI::calendar(const std::string &title, const Date *start, const Date *end, const std::function<bool(const Date &)> &marks, bool allow_range,
                        bool allow_type) {
    Date today_ = today();
    Date cur = start ? *start : today_;
    bool anchored = false;
    Date anchor;
    for (;;) {
        int rows, w, x0;
        dims(rows, w, x0);
        bool has_span = false;
        Date lo, hi;
        if (anchored) {
            lo = std::min(anchor, cur);
            hi = anchor < cur ? cur : anchor;
            has_span = true;
        } else if (start) {
            lo = *start;
            hi = end ? *end : *start;
            has_span = true;
        }
        begin();
        header(title);
        std::string label = std::string(MONTH_ABBR[cur.m]) + " " + std::to_string(cur.y);
        std::vector<std::string> wide = art::big(label, "\xE2\x96\x88\xE2\x96\x88");
        std::vector<std::string> pic;
        if ((long)text::length(wide[0]) <= w - 2 && rows >= 26) pic = wide;
        else if (rows >= 22) pic = art::big(label);
        else pic = {text::upper(label)};
        for (size_t i = 0; i < pic.size(); i++) put(2 + (int)i, x0 + 2, pic[i], &HI, true);
        int y = 3 + (int)pic.size();
        std::vector<std::string> wd;
        for (const char *d : {"SU", "MO", "TU", "WE", "TH", "FR", "SA"}) wd.push_back(std::string(" ") + d);
        put(y, x0 + 2, text::join(wd, "  "), &DIM);
        // the month's weeks, Sunday first, with the days of the months around it
        Date first{cur.y, cur.m, 1};
        Date day = first.plus(-((first.weekday() + 1) % 7));
        Date last{cur.y, cur.m, days_in_month(cur.y, cur.m)};
        for (int r = 0; day <= last; r++)
            for (int i = 0; i < 7; i++, day = day.plus(1)) {
                int x = x0 + 2 + i * 5;
                bool is_today = day == today_;
                std::string num = std::to_string(day.d);
                if (num.size() < 2) num = " " + num;
                std::string cell = std::string(is_today ? "[" : " ") + num + (is_today ? "]" : " ") + (marks && marks(day) ? "\xE2\x80\xA2" : " ");
                const Style *st = &N;
                if (day == cur) st = &INV;
                else if (has_span && lo <= day && day <= hi) st = &HI;
                else if (day.m != cur.m) st = &DIM;
                put(y + 1 + r, x, cell, st, true);
            }
        std::string info = std::string(WEEKDAY_FULL[cur.weekday()]) + ", " + MONTH_FULL[cur.m] + " " + std::to_string(cur.d) + ", " + std::to_string(cur.y);
        if (anchored)
            info = std::string("FROM ") + MONTH_ABBR[anchor.m] + " " + std::to_string(anchor.d) + " THROUGH " + MONTH_ABBR[cur.m] + " " + std::to_string(cur.d) +
                   " (RETURN PICKS)";
        put(y + 8, x0 + 2, text::upper(info), &HI, true);
        put(y + 9, x0 + 2, "[ ] TODAY   \xE2\x80\xA2 HAS A LESSON", &DIM, true);
        std::string hints = std::string("ARROWS MOVE  < > MONTH  T TODAY  ") + (allow_range ? "SPACE STARTS A RANGE  " : "") + "RETURN PICKS  " +
                            (allow_type ? "/ TYPE IT  " : "") + "ESC CANCEL";
        footer(hints);
        show();
        int k = key();
        std::string c = ch8(k);
        int step = k == term::K_LEFT ? -1 : k == term::K_RIGHT ? 1 : k == term::K_UP ? -7 : k == term::K_DOWN ? 7 : 0;
        if (k == term::K_CLOSED) return {};
        if (step) cur = cur.plus(step);
        else if (k == term::K_PGUP || k == term::K_PGDN || c == "<" || c == ">" || c == "," || c == ".") {
            int n = (k == term::K_PGUP || c == "<" || c == ",") ? -1 : 1;
            int m = cur.m - 1 + n;
            int y2 = cur.y + (m >= 0 ? m / 12 : (m - 11) / 12), m2 = ((m % 12) + 12) % 12 + 1;
            cur = Date{y2, m2, std::min(cur.d, days_in_month(y2, m2))};
        } else if (text::lower(c) == "t" || k == term::K_HOME) cur = today_;
        else if (c == " " && allow_range) {
            if (anchored && anchor == cur) anchored = false;
            else {
                anchor = cur;
                anchored = true;
            }
        } else if (c == "/" && allow_type) {
            Picked p;
            p.kind = Picked::TYPE;
            return p;
        } else if (k == term::K_ENTER) {
            Picked p;
            p.kind = Picked::DAYS;
            p.first = anchored ? std::min(anchor, cur) : cur;
            p.last = anchored ? (anchor < cur ? cur : anchor) : cur;
            return p;
        } else if (is_back(k) && k != term::K_LEFT) return {};
    }
}

long UI::choose(const std::string &title, const std::vector<std::string> &options, long current, const std::string &hints) {
    long picked = -1;
    list_screen(
        title, [&] { return (long)options.size(); },
        [&](long i, int) {
            Row r;
            r.text = std::string(i == current ? "* " : "  ") + options[(size_t)i];
            return r;
        },
        [&](long i) {
            picked = i;
            return Result::back();
        },
        nullptr, hints, [] { return std::string(); }, "NOTHING HERE YET.", current < 0 ? 0 : current);
    return picked;
}

// ----------------------------------------------------------------------------- the export bar

ExportBar::ExportBar(UI &ui, const std::string &mascot) : ui_(ui), mascot_(mascot), art_(art::sprite(mascot)) {}

void ExportBar::operator()(int done, int total, const std::string &label) {
    if (ui_.s.rows() < 16 || ui_.s.cols() < 60) return;
    int rows, w, x0;
    ui_.dims(rows, w, x0);
    if (x_ < 0) {   // first call: the screen frame
        ui_.begin();
        ui_.header("EXPORTING");
        ui_.show();
    }
    int y = 2, left = x0 + 1, span = w - 2 - 16;
    int goal = left + span * done / std::max(1, total);
    if (x_ < 0) x_ = left;
    // glide to the new spot in a few hops (skipped where the console is slow)
    int steps = ui_.animate ? std::min(6, goal - x_) : 0;
    for (int i = 1; i <= steps; i++) {
        draw(y, x_ + (goal - x_) * i / steps, left, span, done, total);
        ui_.napms(12);
    }
    x_ = goal;
    draw(y, goal, left, span, done, total);
    std::string t;
    if (done < total) t = std::to_string(done + 1) + " OF " + std::to_string(total) + ": " + label;
    else t = art::name(mascot_).empty() ? "DONE!" : "DONE! GO " + art::name(mascot_) + "!";
    ui_.draw(y + 11, x0, std::string((size_t)w, ' '), ui_.N);
    ui_.draw(y + 11, left, truncate(t, w - 2), done == total ? ui_.HI : ui_.N);
    ui_.refresh();
    if (done == total) ui_.napms(350);   // a beat to see it finish
}

void ExportBar::draw(int y, int x, int left, int span, int done, int total) {
    frame_++;
    for (int dy = 0; dy < 8; dy++) ui_.draw(y + dy, left, std::string((size_t)(span + 16), ' '), ui_.N);
    ui_.draw_sprite(y, x, frame_ % 2 ? art::hop(art_) : art_, false);
    // the bar under its feet: filled up to the mascot, dim after it
    int filled = (span + 16) * (x - left) / std::max(1, span);
    ui_.draw(y + 8, left, text::repeat("\xE2\x96\x80", (size_t)std::max(0, filled)), ui_.pix('n', '.'));
    ui_.draw(y + 8, left + filled, text::repeat("\xE2\x96\x80", (size_t)std::max(0, span + 16 - filled)), ui_.pix('d', '.'));
    std::string pct = std::to_string(100 * done / std::max(1, total)) + "%";
    while (pct.size() < 4) pct = " " + pct;
    ui_.draw(y + 9, left + span + 16 - (int)pct.size(), pct, ui_.DIM);
    ui_.refresh();
}

}  // namespace tui
