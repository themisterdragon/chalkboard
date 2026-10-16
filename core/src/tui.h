// The terminal app's toolkit (port of ui.py): green-screen chrome, numbered lists, line and text
// editors, a calendar, and the mascot export bar. Drawn on a term::Screen.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "dates.h"
#include "json.h"
#include "term.h"

namespace tui {

using term::Style;

std::string truncate(const std::string &s, long n);
std::vector<std::pair<long, long>> wrap_rows(const std::u32string &buf, long width);

// what a list's callbacks ask for: nothing, leave the list, or move the selection
struct Result {
    enum Kind { NONE, BACK, MOVE } kind = NONE;
    long to = 0;
    static Result none() { return {}; }
    static Result back() { return {BACK, 0}; }
    static Result move(long i) { return {MOVE, i}; }
};

class UI {
public:
    UI(term::Screen &screen, std::function<json::Value &()> settings);
    term::Screen &s;
    std::function<json::Value &()> settings;   // looked up each time: the store's settings can move
    std::string msg;   // shown once in the footer instead of the hints
    bool animate = true;
    Style N, INV, DIM, HI;
    Style pix(char top, char bot) const;

    void apply_theme();
    void dims(int &rows, int &w, int &x0) const;
    std::string tx(const std::string &s) const;   // all caps when the setting says so

    // buffered drawing, shown with one refresh
    void begin() { buf_.clear(); }
    void put(int y, int x, const std::string &t, const Style *st = nullptr, bool raw = false);
    void put(int y, int x, const std::string &t, Style st, bool raw = false) { put(y, x, t, &st, raw); }
    void line(int y, const std::string &t, const Style *st = nullptr, bool raw = false) { put(y, 1, t, st, raw); }
    void draw(int y, int x, const std::string &t, Style st);   // right away
    void show(bool animate = false, int cy = -1, int cx = -1);
    void put_sprite(int y, int x, const std::vector<std::string> &rows);
    void draw_sprite(int y, int x, const std::vector<std::string> &rows, bool clear = true);
    void header(const std::string &title, bool raw = false);
    void footer(const std::string &hints);
    void badge(int y, int x, const std::string &label);
    void refresh() { s.present(); }
    void napms(int ms) { s.sleep_ms(ms); }

    // input
    int key();
    void echo(const std::string &c);
    // one-line editor at the ] prompt; false on Esc
    bool prompt(const std::string &label, std::string &out, const std::string &initial = "", bool replace = false, bool raw = false);
    bool confirm(const std::string &question);
    // full-screen editor; false when cancelled. fill(buf) -> text for Ctrl-G
    bool edit_text(const std::string &title, std::string &text, const std::string &help = "", std::function<std::string(const std::string &)> fill = nullptr,
                   const std::string &fill_hint = "", const std::string &fill_prefix = "");
    void view_lines(const std::string &title, const std::vector<std::pair<std::string, Style>> &lines,
                    const std::string &hints = "ARROWS/SPACE SCROLL   ESC BACK", bool raw_title = false);

    // A numbered, paged list. Number keys, or the arrows and Return, open an item.
    // row(i, width) -> (text, style or null); on_open(i) and on_key(key, i or -1)
    struct Row { std::string text; bool has_style = false; Style st; };
    void list_screen(const std::string &title, const std::function<long()> &count, const std::function<Row(long, int)> &row,
                     const std::function<Result(long)> &on_open, const std::function<Result(int, long)> &on_key, const std::string &hints = "",
                     const std::function<std::string()> &info = nullptr, const std::string &empty = "NOTHING HERE YET.", long start = 0);

    struct Picked {
        enum Kind { CANCEL, DAYS, TYPE } kind = CANCEL;
        Date first, last;
    };
    Picked calendar(const std::string &title, const Date *start = nullptr, const Date *end = nullptr,
                    const std::function<bool(const Date &)> &marks = nullptr, bool allow_range = false, bool allow_type = false);
    long choose(const std::string &title, const std::vector<std::string> &options, long current = -1,
                const std::string &hints = "PRESS A NUMBER   ESC CANCEL");

private:
    struct Item { int y, x; std::string text; Style st; };
    std::vector<Item> buf_;
    Style pix_[4][4];
    std::vector<std::pair<int, std::pair<std::string, Style>>> sprite_runs_(const std::vector<std::string> &rows, int &cols_out) const;
};

// progress for exporting: the mascot (or the logo) runs along a bar
class ExportBar {
public:
    ExportBar(UI &ui, const std::string &mascot);
    void operator()(int done, int total, const std::string &label);

private:
    UI &ui_;
    std::string mascot_;
    std::vector<std::string> art_;
    int x_ = -1, frame_ = 0;
    void draw(int y, int x, int left, int span, int done, int total);
};

}  // namespace tui
