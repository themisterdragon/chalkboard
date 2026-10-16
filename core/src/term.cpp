#include "term.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <thread>

#include "files.h"
#include "text.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace term {

// ----------------------------------------------------------------------------- the cell grid

void Screen::resize(int rows, int cols) {
    rows_ = rows < 1 ? 1 : rows;
    cols_ = cols < 1 ? 1 : cols;
    cells_.assign((size_t)rows_ * (size_t)cols_, Cell());
}

void Screen::clear(Style st) {
    for (auto &c : cells_) c = Cell{U' ', st};
}

void Screen::put(int y, int x, const std::u32string &t, Style st) {
    if (y < 0 || y >= rows_) return;
    for (char32_t c : t) {
        if (x >= cols_) break;
        if (x >= 0) {
            if (c < 0x20 || (c >= 0x7F && c < 0xA0)) c = U' ';   // never send a control character to the terminal
            cells_[(size_t)y * (size_t)cols_ + (size_t)x] = Cell{c, st};
        }
        x++;
    }
}

void Screen::put(int y, int x, const std::string &utf8, Style st) { put(y, x, text::to32(utf8), st); }

namespace {

std::string sgr(const Style &s, int colors) {
    std::string o = "\x1b[0";
    if (s.attr & A_BOLD) o += ";1";
    if (s.attr & A_DIM) o += ";2";
    if (s.attr & A_ITALIC) o += ";3";
    if (s.attr & A_UNDER) o += ";4";
    if (s.attr & A_REVERSE) o += ";7";
    auto color = [&](int c, bool fg) {
        if (c < 0) return;
        if (colors >= 256) o += (fg ? ";38;5;" : ";48;5;") + std::to_string(c);
        else if (c < 8) o += ";" + std::to_string((fg ? 30 : 40) + c);
        else if (c < 16) o += ";" + std::to_string((fg ? 90 : 100) + c - 8);
    };
    color(s.fg, true);
    color(s.bg, false);
    return o + "m";
}

// ----------------------------------------------------------------------------- a real terminal

class Terminal : public Screen {
public:
    Terminal();
    ~Terminal() override;
    int colors() const override { return colors_; }
    void present() override;
    int read(int timeout_ms) override;
    void sleep_ms(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

private:
    std::vector<Cell> shown_;
    bool full_ = true;
    int colors_ = 256;
    std::string inbuf_;
    void write(const std::string &s);
    bool size(int &rows, int &cols);
    int byte(int timeout_ms);   // -1 none, -2 closed
#ifdef _WIN32
    HANDLE in_ = nullptr, out_ = nullptr;
    DWORD in_mode_ = 0, out_mode_ = 0;
    UINT cp_ = 0;
    std::u32string keys_;   // decoded key presses waiting
#else
    termios saved_{};
#endif
};

#ifndef _WIN32
volatile std::sig_atomic_t g_winch = 0;
Terminal *g_term = nullptr;
termios g_saved;
bool g_raw = false;
void on_winch(int) { g_winch = 1; }
void restore_tty() {
    if (g_raw) {
        tcsetattr(0, TCSANOW, &g_saved);
        const char *bye = "\x1b[0m\x1b[?25h\x1b[?1049l";
        ssize_t r = ::write(1, bye, std::char_traits<char>::length(bye));
        (void)r;
        g_raw = false;
    }
}
void on_fatal(int sig) {
    restore_tty();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

Terminal::Terminal() {
#ifdef _WIN32
    in_ = GetStdHandle(STD_INPUT_HANDLE);
    out_ = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(in_, &in_mode_) || !GetConsoleMode(out_, &out_mode_)) throw std::runtime_error("not a console");
    SetConsoleMode(in_, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
    SetConsoleMode(out_, out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    cp_ = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    colors_ = 256;
#else
    if (!isatty(0) || !isatty(1)) throw std::runtime_error("not a terminal");
    tcgetattr(0, &saved_);
    g_saved = saved_;
    termios raw = saved_;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    g_raw = true;
    g_term = this;
    std::signal(SIGWINCH, on_winch);
    for (int s : {SIGTERM, SIGHUP, SIGSEGV, SIGABRT}) std::signal(s, on_fatal);
    std::atexit(restore_tty);
    std::string t = files::env("TERM"), ct = files::env("COLORTERM");
    colors_ = (text::contains(t, "256") || ct == "truecolor" || ct == "24bit" || files::env("TERM_PROGRAM") == "Apple_Terminal" ||
               files::env("TERM_PROGRAM") == "iTerm.app")
                  ? 256
                  : (t == "linux" || t == "vt100" || t == "dumb") ? 8 : 256;
#endif
    write("\x1b[?1049h\x1b[?25l\x1b[2J");   // the alternate screen, cursor hidden
    int r = 24, c = 80;
    size(r, c);
    resize(r, c);
    shown_ = cells_;
}

Terminal::~Terminal() {
#ifdef _WIN32
    write("\x1b[0m\x1b[?25h\x1b[?1049l");
    SetConsoleMode(in_, in_mode_);
    SetConsoleMode(out_, out_mode_);
    SetConsoleOutputCP(cp_);
#else
    restore_tty();
    g_term = nullptr;
#endif
}

void Terminal::write(const std::string &s) {
#ifdef _WIN32
    DWORD n;
    WriteFile(out_, s.data(), (DWORD)s.size(), &n, nullptr);
#else
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(1, s.data() + off, s.size() - off);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return;
        }
        off += (size_t)n;
    }
#endif
}

bool Terminal::size(int &rows, int &cols) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out_, &info)) return false;
    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return true;
#else
    winsize ws{};
    if (ioctl(1, TIOCGWINSZ, &ws) != 0 || !ws.ws_col) return false;
    rows = ws.ws_row;
    cols = ws.ws_col;
    return true;
#endif
}

void Terminal::present() {
    if ((int)shown_.size() != rows_ * cols_) full_ = true;
    std::string o;
    Style cur;
    bool have = false;
    int at_y = -1, at_x = -1;
    for (int y = 0; y < rows_; y++)
        for (int x = 0; x < cols_; x++) {
            size_t i = (size_t)y * (size_t)cols_ + (size_t)x;
            const Cell &c = cells_[i];
            if (!full_ && shown_[i] == c) continue;
            if (at_y != y || at_x != x) o += "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
            if (!have || c.st != cur) {
                o += sgr(c.st, colors_);
                cur = c.st;
                have = true;
            }
            o += text::to8(c.ch);
            at_y = y;
            at_x = x + 1;
        }
    shown_ = cells_;
    full_ = false;
    if (cy_ >= 0 && cx_ >= 0) o += "\x1b[" + std::to_string(cy_ + 1) + ";" + std::to_string(cx_ + 1) + "H\x1b[?25h";
    else o += "\x1b[?25l";
    o += "\x1b[0m";
    write(o);
}

#ifdef _WIN32
int Terminal::byte(int) { return -1; }

int Terminal::read(int timeout_ms) {
    if (!pending_.empty()) {
        int k = pending_.front();
        pending_.erase(pending_.begin());
        return k;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        DWORD wait = timeout_ms < 0 ? INFINITE
                                    : (DWORD)std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (WaitForSingleObject(in_, wait) != WAIT_OBJECT_0) return K_NONE;
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(in_, &rec, 1, &n) || !n) return K_CLOSED;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            int r, c;
            if (size(r, c)) {
                resize(r, c);
                full_ = true;
            }
            return K_RESIZE;
        }
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        const KEY_EVENT_RECORD &k = rec.Event.KeyEvent;
        switch (k.wVirtualKeyCode) {
            case VK_UP: return K_UP;
            case VK_DOWN: return K_DOWN;
            case VK_LEFT: return K_LEFT;
            case VK_RIGHT: return K_RIGHT;
            case VK_HOME: return K_HOME;
            case VK_END: return K_END;
            case VK_PRIOR: return K_PGUP;
            case VK_NEXT: return K_PGDN;
            case VK_DELETE: return K_DELETE;
            case VK_RETURN: return K_ENTER;
            case VK_BACK: return K_BACKSPACE;
            case VK_ESCAPE: return K_ESC;
            default: break;
        }
        wchar_t ch = k.uChar.UnicodeChar;
        if (!ch) continue;
        if (ch >= 0xD800 && ch <= 0xDBFF) {   // the first half of a character past U+FFFF
            INPUT_RECORD r2;
            if (ReadConsoleInputW(in_, &r2, 1, &n) && n && r2.EventType == KEY_EVENT) {
                wchar_t lo = r2.Event.KeyEvent.uChar.UnicodeChar;
                return 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
            }
            continue;
        }
        return (int)ch;
    }
}
#else
int Terminal::byte(int timeout_ms) {
    if (!inbuf_.empty()) {
        int b = (unsigned char)inbuf_[0];
        inbuf_.erase(0, 1);
        return b;
    }
    pollfd p{0, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return -1;
    if (p.revents & (POLLHUP | POLLERR)) return -2;
    char buf[256];
    ssize_t n = ::read(0, buf, sizeof buf);
    if (n == 0) return -2;
    if (n < 0) return -1;
    inbuf_.append(buf, (size_t)n);
    int b = (unsigned char)inbuf_[0];
    inbuf_.erase(0, 1);
    return b;
}

int Terminal::read(int timeout_ms) {
    if (!pending_.empty()) {
        int k = pending_.front();
        pending_.erase(pending_.begin());
        return k;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (g_winch) {
            g_winch = 0;
            int r, c;
            if (size(r, c) && (r != rows_ || c != cols_)) {
                resize(r, c);
                full_ = true;
                return K_RESIZE;
            }
        }
        int wait = 50;
        if (timeout_ms >= 0) {
            long long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return K_NONE;
            wait = (int)std::min<long long>(left, 50);
        }
        int b = byte(wait);
        if (b == -2) return K_CLOSED;
        if (b < 0) continue;
        if (b == 0x1b) {
            int b2 = byte(30);
            if (b2 < 0) return K_ESC;   // Esc on its own
            if (b2 == '[' || b2 == 'O') {
                std::string seq;
                for (;;) {
                    int c = byte(30);
                    if (c < 0) break;
                    seq += (char)c;
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') break;
                }
                char last = seq.empty() ? 0 : seq.back();
                if (last == 'A') return K_UP;
                if (last == 'B') return K_DOWN;
                if (last == 'C') return K_RIGHT;
                if (last == 'D') return K_LEFT;
                if (last == 'H') return K_HOME;
                if (last == 'F') return K_END;
                if (last == '~') {
                    int n = std::atoi(seq.c_str());
                    if (n == 1 || n == 7) return K_HOME;
                    if (n == 4 || n == 8) return K_END;
                    if (n == 3) return K_DELETE;
                    if (n == 5) return K_PGUP;
                    if (n == 6) return K_PGDN;
                }
                continue;   // a key Chalkboard doesn't use
            }
            inbuf_.insert(inbuf_.begin(), (char)b2);   // Esc then a key: Esc first
            return K_ESC;
        }
        if (b == '\r' || b == '\n') return K_ENTER;
        if (b == 0x7f || b == 0x08) return K_BACKSPACE;
        if (b < 0x80) return b;
        // the rest of a UTF-8 character
        int len = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
        std::string u(1, (char)b);
        for (int i = 1; i < len; i++) {
            int c = byte(30);
            if (c < 0) break;
            u += (char)c;
        }
        std::u32string d = text::to32(u);
        if (!d.empty()) return (int)d[0];
    }
}
#endif

}  // namespace

std::unique_ptr<Screen> open_terminal() { return std::unique_ptr<Screen>(new Terminal()); }

bool in_terminal() {
#ifdef _WIN32
    DWORD m;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &m) && GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &m);
#else
    return isatty(0) && isatty(1);
#endif
}

}  // namespace term
