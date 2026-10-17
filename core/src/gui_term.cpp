// The terminal view (port of gui/term.py): the terminal app (the same code as `chalkboard` in a
// terminal) running inside the window, on the window app's data. Chalkboard draws the character
// grid itself, so it needs no terminal program and works the same on every OS.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "files.h"
#include "gui.h"
#include "proc.h"
#include "text.h"
#include "term.h"
#include "tui_app.h"

namespace gui {

using namespace ui;

namespace {

struct Leave {
    std::string why;   // "switch" (to the window view) or "quit"
};

Color xterm(int n) {
    static const uint32_t BASE[16] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
                                      0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    uint32_t v;
    if (n < 16) v = BASE[std::max(0, n)];
    else if (n < 232) {
        static const int L[6] = {0, 95, 135, 175, 215, 255};
        int m = n - 16;
        v = (uint32_t)(L[m / 36] << 16 | L[m / 6 % 6] << 8 | L[m % 6]);
    } else {
        int g = 8 + (n - 232) * 10;
        v = (uint32_t)(g << 16 | g << 8 | g);
    }
    return {(v >> 16 & 255) / 255.0, (v >> 8 & 255) / 255.0, (v & 255) / 255.0};
}

// the computer's own terminal font, or a common monospace one
std::vector<std::string> mono_families() {
#ifdef __APPLE__
    return {"SF Mono", "Menlo", "Monaco", "Courier New"};
#elif defined(_WIN32)
    return {"Cascadia Mono", "Consolas", "Lucida Console", "Courier New"};
#else
    return {"JetBrains Mono", "JetBrainsMono Nerd Font Mono", "JetBrainsMono Nerd Font", "Cascadia Mono", "Fira Mono", "Source Code Pro", "Hack",
            "Ubuntu Mono", "DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono", "Nimbus Mono PS", "Courier New"};
#endif
}

class CanvasScreen : public term::Screen {
public:
    explicit CanvasScreen(App &a) : app(a), win(a.win) { set_font(); }

    App &app;
    plat::Window &win;
    std::string leaving;   // set from a menu or a shortcut: unwind at the next key wait
    bool live = false;

    int colors() const override { return 256; }

    void set_font() {
        double px = app.text_px() * app.scale() + 3;
        for (int s = 0; s < 4; s++) fonts_[s].reset(new Font(mono_families(), px, s));
        cw_ = std::max(1.0, std::round(fonts_[0]->advance_of('0')));
        ch_ = std::max(1.0, std::round(fonts_[0]->line));
        shown_.clear();
        reflow(true);
    }

    void reflow(bool force = false) {
        int w, h;
        win.size(w, h);
        pad_ = std::max(2.0, std::floor(cw_ / 2));
        int r = std::max(1, (int)((h - pad_) / ch_)), c = std::max(1, (int)((w - pad_) / cw_));
        if (img_.w != w || img_.h != h) {
            img_ = raster::Image(w, h, {0, 0, 0});
            shown_.clear();
        }
        if (r == rows_ && c == cols_ && !force) return;
        resize(r, c);
        shown_.clear();
    }

    // ---- drawing
    void present() override {
        if (shown_.size() != cells_.size()) {
            shown_.assign(cells_.size(), term::Cell{U'\x01', {}});
            img_ = raster::Image(img_.w, img_.h, bg_of({}));
        }
        for (int y = 0; y < rows_; y++) {
            bool same = true;
            for (int x = 0; x < cols_ && same; x++) same = cells_[(size_t)(y * cols_ + x)] == shown_[(size_t)(y * cols_ + x)];
            if (same && !(cursor_row_ == y) && !(cy_ == y)) continue;
            draw_row(y);
            for (int x = 0; x < cols_; x++) shown_[(size_t)(y * cols_ + x)] = cells_[(size_t)(y * cols_ + x)];
        }
        // the cursor: a bar under the cell
        cursor_row_ = -1;
        if (cy_ >= 0 && cy_ < rows_ && cx_ >= 0 && cx_ < cols_) {
            Color fg = fg_of(cells_[(size_t)(cy_ * cols_ + cx_)].st);
            double t = std::max(2.0, std::floor(ch_ / 7));
            img_.rect(pad_ + cx_ * cw_, pad_ + (cy_ + 1) * ch_ - t, cw_, t, fg);
            cursor_row_ = cy_;
        }
        draw_selection();
        win.present(img_);
    }

    int read(int timeout_ms) override {
        check();
        if (!pending_.empty()) {
            int k = pending_.front();
            pending_.erase(pending_.begin());
            return k;
        }
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
        while (true) {
            int wait = -1;
            if (timeout_ms >= 0) {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
                if (left <= 0) return term::K_NONE;
                wait = (int)left;
            }
            plat::Event e;
            if (!win.next(e, wait)) {
                if (timeout_ms >= 0) return term::K_NONE;
                continue;
            }
            handle(e);
            check();
            if (!pending_.empty()) {
                int k = pending_.front();
                pending_.erase(pending_.begin());
                return k;
            }
        }
    }

    void sleep_ms(int ms) override {
        // events keep coming while the boot screen animates: keys wait their turn
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (true) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
            if (left <= 0) break;
            plat::Event e;
            if (win.next(e, (int)left)) handle(e);
            check();
        }
    }

    void request(const std::string &why) {
        leaving = why;
        if (!live) throw Leave{why};
    }

private:
    std::unique_ptr<Font> fonts_[4];
    double cw_ = 8, ch_ = 16, pad_ = 4;
    raster::Image img_;
    std::vector<term::Cell> shown_;
    int cursor_row_ = -1;
    // a mouse selection: (row, col) to (row, col)
    bool selecting_ = false, has_sel_ = false;
    int sr0_ = 0, sc0_ = 0, sr1_ = 0, sc1_ = 0;

    void check() {
        if (!leaving.empty()) throw Leave{leaving};
    }

    Color fg_of(const term::Style &st) const {
        Color fg = xterm(st.fg >= 0 ? st.fg : 7), bg = xterm(st.bg >= 0 ? st.bg : 0);
        if (st.attr & term::A_REVERSE) std::swap(fg, bg);
        if (st.attr & term::A_DIM) fg = mix(fg, bg, 0.5);
        return fg;
    }
    Color bg_of(const term::Style &st) const {
        Color fg = xterm(st.fg >= 0 ? st.fg : 7), bg = xterm(st.bg >= 0 ? st.bg : 0);
        return st.attr & term::A_REVERSE ? fg : bg;
    }

    void draw_row(int y) {
        double top = pad_ + y * ch_;
        Color base = bg_of({});
        img_.rect(0, top, img_.w, ch_, base);
        if (y == 0) img_.rect(0, 0, img_.w, pad_, base);
        if (y == rows_ - 1) img_.rect(0, top + ch_, img_.w, img_.h - top - ch_, base);
        for (int x = 0; x < cols_; x++) {
            const term::Cell &c = cells_[(size_t)(y * cols_ + x)];
            double left = pad_ + x * cw_;
            Color fg = fg_of(c.st), bg = bg_of(c.st);
            if (hex(bg) != hex(base)) img_.rect(left, top, cw_, ch_, bg);
            // block characters as exact rectangles, so pixel art has no gaps
            if (c.ch == U'█') {
                img_.rect(left, top, cw_, ch_, fg);
                continue;
            }
            if (c.ch == U'▀' || c.ch == U'▄') {
                double half = std::floor(ch_ / 2);
                if (c.ch == U'▀') img_.rect(left, top, cw_, half, fg);
                else img_.rect(left, top + half, cw_, ch_ - half, fg);
                continue;
            }
            int style = (c.st.attr & term::A_BOLD ? 1 : 0) | (c.st.attr & term::A_ITALIC ? 2 : 0);
            if (c.ch != U' ') fonts_[style]->draw(img_, left, top + std::round((ch_ - fonts_[0]->line) / 2 + fonts_[0]->ascent), text::to8(c.ch), fg);
            if (c.st.attr & term::A_UNDER) img_.rect(left, top + ch_ - 2, cw_, 1, fg);
        }
    }

    void draw_selection() {
        if (!has_sel_) return;
        int r0 = sr0_, c0 = sc0_, r1 = sr1_, c1 = sc1_;
        if (std::make_pair(r1, c1) < std::make_pair(r0, c0)) std::swap(r0, r1), std::swap(c0, c1);
        Color fg = fg_of({});
        for (int r = r0; r <= r1 && r < rows_; r++) {
            int a = r == r0 ? c0 : 0, b = r == r1 ? c1 + 1 : cols_;
            img_.rect(pad_ + a * cw_, pad_ + r * ch_, (b - a) * cw_, ch_, fg, 0.35);
            for (int x = a; x < b; x++)   // drawn over: redraw these next time
                if (r * cols_ + x < (int)shown_.size()) shown_[(size_t)(r * cols_ + x)].ch = U'\x01';
        }
    }

    void cell_at(double px, double py, int &r, int &c) const {
        r = std::max(0, std::min(rows_ - 1, (int)((py - pad_) / ch_)));
        c = std::max(0, std::min(cols_ - 1, (int)((px - pad_) / cw_)));
    }

    std::string selected() const {
        if (!has_sel_ || (sr0_ == sr1_ && sc0_ == sc1_)) return "";
        int r0 = sr0_, c0 = sc0_, r1 = sr1_, c1 = sc1_;
        if (std::make_pair(r1, c1) < std::make_pair(r0, c0)) std::swap(r0, r1), std::swap(c0, c1);
        std::vector<std::string> out;
        for (int r = r0; r <= r1; r++) {
            int a = r == r0 ? c0 : 0, b = r == r1 ? c1 + 1 : cols_;
            std::u32string line;
            for (int x = a; x < b; x++) {
                char32_t ch = cells_[(size_t)(r * cols_ + x)].ch;
                if (ch != U'█') line += ch;
            }
            while (!line.empty() && line.back() == U' ') line.pop_back();
            out.push_back(text::to8(line));
        }
        std::string s = text::join(out, "\n");
        return text::strip_chars(s, "\n");
    }

    void clear_sel() {
        if (has_sel_) {
            has_sel_ = false;
            shown_.clear();
        }
    }

    void paste(const std::string &t0) {
        clear_sel();
        std::string t = text::replace(text::replace(text::replace(t0, "\r\n", "\n"), "\r", "\n"), "\t", "    ");
        for (char32_t c : text::to32(t))
            if (c == U'\n') pending_.push_back(term::K_ENTER);
            else if (c >= 0x20 && c != 0x7f) pending_.push_back((int)c);
    }

    void zoom(int d) {
        const auto &sizes = text_sizes();
        int i = (int)(std::find(sizes.begin(), sizes.end(), app.text_px()) - sizes.begin()) + d;
        if (i < 0 || i >= (int)sizes.size()) return;
        app.settings()["gui_text"] = sizes[(size_t)i];
        set_font();
        pending_.push_back(term::K_RESIZE);
    }

    void menu(double x, double y) {
#ifdef __APPLE__
        const char *mod = "Cmd";
#else
        const char *mod = "Ctrl";
#endif
        std::vector<MenuEntry> items = entries({"Copy", "Paste", "", "Switch to Window View", "Bigger Text", "Smaller Text", "", "Quit"});
        items[0].keys = std::string(mod) + "+C";
        items[1].keys = std::string(mod) + "+V";
        items[3].keys = std::string(mod) + "+Shift+W";
        items[4].keys = std::string(mod) + "+=";
        items[5].keys = std::string(mod) + "+-";
        items[7].keys = std::string(mod) + "+Q";
        items[0].enabled = !selected().empty();
        // the menu draws over the terminal's own picture
        app.ui.img = img_;
        int got = popup_menu(app.ui, x, y, items);
        shown_.clear();
        if (got == 0) win.set_clipboard(selected());
        else if (got == 1) paste(win.clipboard());
        else if (got == 3) request("switch");
        else if (got == 4) zoom(1);
        else if (got == 5) zoom(-1);
        else if (got == 7) request("quit");
        present();
    }

    void handle(const plat::Event &e) {
        using plat::Event;
        int P = plat::PRIMARY;
        switch (e.type) {
        case Event::CLOSE:
            request("quit");
            return;
        case Event::RESIZE:
            reflow();
            pending_.push_back(term::K_RESIZE);
            return;
        case Event::LOOK:
        case Event::FOCUS_IN:
            shown_.clear();
            present();
            return;
        case Event::DOWN:
            if (e.button == 3 || (e.button == 1 && (e.mods & plat::CTRL) && P == plat::CMD)) {
                menu(e.x, e.y);
                return;
            }
            if (e.button == 2) {   // middle click pastes, like a Linux terminal
                paste(win.clipboard());
                return;
            }
            if (e.button == 1) {
                clear_sel();
                cell_at(e.x, e.y, sr0_, sc0_);
                sr1_ = sr0_;
                sc1_ = sc0_;
                selecting_ = true;
                has_sel_ = true;
                present();
            }
            return;
        case Event::MOVE:
            if (selecting_) {
                cell_at(e.x, e.y, sr1_, sc1_);
                shown_.clear();
                present();
            }
            return;
        case Event::UP:
            if (e.button == 1 && selecting_) {
                selecting_ = false;
                std::string t = selected();
                if (t.empty()) clear_sel();
            }
            return;
        case Event::TEXT:
            if (!e.text.empty()) {
                clear_sel();
                for (char32_t c : text::to32(e.text))
                    if (c >= 0x20 && c != 0x7f) pending_.push_back((int)c);
            }
            return;
        case Event::KEY:
            break;
        default:
            return;
        }
        // keys
        int k = e.key, m = e.mods;
        if (m == (P | plat::SHIFT) && k == 'w') return request("switch");
        if (m == P && k == 'q') return request("quit");
        if (m == P && (k == '=' || k == '+')) return zoom(1);
        if (m == (P | plat::SHIFT) && (k == '=' || k == '+')) return zoom(1);
        if (m == P && k == '-') return zoom(-1);
        if (m == P && k == 'v') return paste(win.clipboard());
        if (m == P && k == 'c' && !selected().empty()) {   // with nothing selected, Ctrl+C goes to the app as usual
            win.set_clipboard(selected());
            return;
        }
        int out = 0;
        switch (k) {
        case plat::K_LEFT: out = term::K_LEFT; break;
        case plat::K_RIGHT: out = term::K_RIGHT; break;
        case plat::K_UP: out = term::K_UP; break;
        case plat::K_DOWN: out = term::K_DOWN; break;
        case plat::K_HOME: out = term::K_HOME; break;
        case plat::K_END: out = term::K_END; break;
        case plat::K_PGUP: out = term::K_PGUP; break;
        case plat::K_PGDN: out = term::K_PGDN; break;
        case plat::K_RETURN: out = term::K_ENTER; break;
        case plat::K_ESCAPE: out = term::K_ESC; break;
        case plat::K_TAB: out = '\t'; break;
        case plat::K_BACKSPACE: out = term::K_BACKSPACE; break;
        case plat::K_DELETE: out = term::K_DELETE; break;
        default:
            if ((m & plat::CTRL) && !(m & (plat::ALT | plat::CMD)) && k >= 'a' && k <= 'z') out = k - 'a' + 1;   // Ctrl-B is 2
            break;
        }
        if (out) {
            clear_sel();
            pending_.push_back(out);
        }
    }
};

}  // namespace

void App::enter_terminal(bool boot) {
    if (modal() || in_terminal_) return;
    save();
    in_terminal_ = true;
    std::string why = "quit", crash;
    int text_before = text_px();
    // "terminal" is remembered only when you quit from it, so a terminal view that goes wrong
    // can't come back on every launch
    try {
        CanvasScreen screen(*this);
        win.title("Chalkboard");
        tui::App t(screen, store, !boot);
        t.leave = [] { throw Leave{"switch"}; };
        screen.live = true;
        try {
            t.run();
        } catch (Leave &l) {
            why = l.why;
        }
    } catch (Leave &l) {
        why = l.why;
    } catch (std::exception &e) {
        why = "switch";
        crash = e.what();
    }
    std::string err;
    store.save(&err);
    sync_mtime();
    in_terminal_ = false;
    if (why == "quit") {
        settings()["gui_mode"] = "terminal";
        quit();
        return;
    }
    settings()["gui_mode"] = "window";
    save();
    // what was open may have changed in the terminal view
    stack_.clear();
    trail_.clear();
    view_.reset();
    section_.clear();
    do_show(make_home(), false);
    if (text_px() != text_before) set_look();   // the text size changed in the terminal view
    else ui.again();
    if (!crash.empty()) ui.after(1, [this, crash] { alert(ui, "Terminal View", "The terminal view stopped because of an error:\n" + crash, "warn"); });
}

// ----------------------------------------------------------------------------- starting

int run_window(bool no_boot, bool terminal_view) {
    auto win = plat::Window::open("Chalkboard", 1180, 800, nullptr);
    if (!win) return -1;
    App app(*win, no_boot);
    if (terminal_view) app.settings()["gui_mode"] = "terminal";
    // a check that the real window works here: save what it drew after a moment, then close
    std::string smoke = files::env("CHALKBOARD_SMOKE");
    if (!smoke.empty())
        app.ui.after(1500, [&app, smoke] {
            files::write(smoke, app.ui.img.png());
            app.quit();
        });
    app.run();
    return 0;
}

int run_script(const std::string &script, const std::string &outdir, bool no_boot) {
    std::string raw;
    if (!files::read(script, raw)) return 2;
    std::vector<std::string> steps;
    int w = 1180, h = 800, scale = 1;
    for (auto &l : text::split(raw, "\n")) {
        l = text::strip(l);
        if (l.empty() || l[0] == '#') continue;
        if (text::starts_with(l, "window ")) {   // window W H [SCALE]: the size, first
            auto p = text::split_ws(l);
            w = std::stoi(p[1]);
            h = std::stoi(p[2]);
            if (p.size() > 3) scale = std::stoi(p[3]);
            continue;
        }
        steps.push_back(l);
    }
    files::make_dirs(outdir);
    proc::dry_run = true;   // a test never opens files in other programs
#ifndef _WIN32
    // and its app-menu entries, settings, and caches follow HOME (a test sets HOME to a scratch folder)
    for (const char *v : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_DOCUMENTS_DIR"}) unsetenv(v);
#endif
    auto win = plat::scripted(w, h, scale, steps, outdir);
    App app(*win, no_boot);
    app.run();
    return 0;
}

}  // namespace gui
