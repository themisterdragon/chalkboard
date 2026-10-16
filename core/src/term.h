// The terminal app's screen: a grid of character cells drawn with colors and attributes, and keys
// read one at a time. A real terminal (escape codes on Linux and macOS, the console on Windows)
// is one back end; the window app hosts the same screens on a grid of its own.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace term {

// Keys are Unicode code points (letters, and control characters like Ctrl-B = 2), or these:
enum Key : int {
    K_NONE = 0,   // nothing yet (a read that timed out)
    K_ENTER = 0x110000,
    K_BACKSPACE,
    K_UP,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_HOME,
    K_END,
    K_PGUP,
    K_PGDN,
    K_DELETE,
    K_RESIZE,
    K_CLOSED,   // the terminal went away: save and quit
};
const int K_ESC = 0x1b;

enum Attr { A_BOLD = 1, A_DIM = 2, A_UNDER = 4, A_ITALIC = 8, A_REVERSE = 16 };

struct Style {
    int fg = -1, bg = -1;   // xterm-256 colors; -1 = the terminal's own
    int attr = 0;
    bool operator==(const Style &o) const { return fg == o.fg && bg == o.bg && attr == o.attr; }
    bool operator!=(const Style &o) const { return !(*this == o); }
};

struct Cell {
    char32_t ch = U' ';
    Style st;
    bool operator==(const Cell &o) const { return ch == o.ch && st == o.st; }
};

class Screen {
public:
    virtual ~Screen() = default;
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    virtual int colors() const { return 256; }   // 256, 16, or 8

    void clear(Style st);
    // text clipped at the right edge (one cell per character)
    void put(int y, int x, const std::u32string &text, Style st);
    void put(int y, int x, const std::string &utf8, Style st);
    void cursor(int y, int x) { cy_ = y; cx_ = x; }   // -1: hidden
    virtual void present() = 0;
    virtual int read(int timeout_ms) = 0;            // a key, K_NONE on timeout
    virtual void unread(int key) { pending_.push_back(key); }
    virtual void sleep_ms(int ms) = 0;

protected:
    int rows_ = 24, cols_ = 80, cy_ = -1, cx_ = -1;
    std::vector<Cell> cells_;
    std::vector<int> pending_;
    void resize(int rows, int cols);
};

// true when this program's input and output are a terminal (or a Windows console)
bool in_terminal();
// The real terminal. Throws std::runtime_error when the program isn't in one.
std::unique_ptr<Screen> open_terminal();

}  // namespace term
