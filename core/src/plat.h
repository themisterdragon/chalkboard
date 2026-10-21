// The window, from the operating system: one per OS (plat_gtk.cpp on Linux, plat_win.cpp on
// Windows, plat_mac.cpp on macOS). Chalkboard draws the whole window itself (ui.h) and hands the
// pixels over; this part only shows them and passes along the mouse, the keyboard, the clipboard,
// file pickers, and the computer's light/dark and accent-color settings. Each OS library is
// loaded when the program starts, so one program runs anywhere, and a computer without a desktop
// just gets the terminal app.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "raster.h"

namespace plat {

// keys that aren't characters (characters are their own Unicode values, lowercase)
enum Key : int {
    K_LEFT = 0x110000, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END, K_PGUP, K_PGDN, K_RETURN, K_ESCAPE, K_TAB, K_BACKSPACE,
    K_DELETE, K_INSERT, K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12, K_MENU
};
enum Mod : int { SHIFT = 1, CTRL = 2, ALT = 4, CMD = 8 };
// the shortcut key: Command on a Mac, Ctrl elsewhere
#ifdef __APPLE__
const int PRIMARY = CMD;
#else
const int PRIMARY = CTRL;
#endif

struct Event {
    enum Type { NONE, CLOSE, RESIZE, MOVE, DOWN, UP, WHEEL, KEY, TEXT, FOCUS_IN, FOCUS_OUT, LEAVE, WAKE, MENU, LOOK };
    Type type = NONE;
    double x = 0, y = 0;   // pixels (device pixels, not points)
    int button = 0;        // 1 left, 2 middle, 3 right
    int clicks = 1;        // 2 for a double-click
    double dx = 0, dy = 0; // wheel: lines (down/right positive), or device pixels when pixels is set
    bool pixels = false;   // wheel: a trackpad's exact distance (dx/dy are pixels, not lines)
    int key = 0;           // Key or a character
    int mods = 0;          // Mod bits
    std::string text;      // TEXT: what was typed (UTF-8)
    int menu = 0;          // MENU: the item's id
};

enum Cursor { ARROW, IBEAM, HAND, WAIT, RESIZE_NS, RESIZE_EW };

// A menu for the OS's own menu bar (macOS); elsewhere Chalkboard draws its menus in the window.
struct MenuItem {
    std::string label;     // "" = a separator
    int id = 0;            // sent back as a MENU event
    int key = 0, mods = 0; // the shortcut shown next to it (the window still gets the key itself)
    bool checked = false, enabled = true;
    std::vector<MenuItem> sub;
};

struct Filter {
    std::string name;                    // "Images"
    std::vector<std::string> patterns;   // {"*.png", "*.jpg"}
};

class Window {
public:
    virtual ~Window() = default;
    // w, h: the size in points; nullptr when this computer can't show a window
    static std::unique_ptr<Window> open(const std::string &title, int w, int h, const raster::Image *icon = nullptr);

    virtual double scale() = 0;                      // device pixels per point (2 on a Retina/HiDPI screen)
    virtual void size(int &w, int &h) = 0;            // in device pixels
    virtual void present(const raster::Image &img) = 0;   // show these pixels (the window's size)
    virtual bool next(Event &e, int timeout_ms) = 0;  // wait for something to happen; false: time ran out (-1 = wait forever)
    virtual void wake() = 0;                          // from another thread: next() returns a WAKE event
    virtual void title(const std::string &t) = 0;
    virtual void cursor(Cursor c) = 0;
    virtual std::string clipboard() = 0;
    virtual void set_clipboard(const std::string &s) = 0;
    virtual void minimize() = 0;
    virtual void maximize(bool on) = 0;
    virtual bool maximized() = 0;
    virtual void min_size(int w, int h) = 0;           // points
    virtual void dark_title(bool dark) = 0;            // the OS's own title bar, light or dark
    // the OS's file pickers; "" when cancelled
    virtual std::string open_file(const std::string &title, const std::vector<Filter> &filters) = 0;
    virtual std::string save_file(const std::string &title, const std::string &name) = 0;
    virtual std::string choose_folder(const std::string &title) = 0;
    // macOS: put this in the menu bar and return true; other OSes return false (Chalkboard draws its own)
    virtual bool menu_bar(const std::vector<MenuItem> &menus) { (void)menus; return false; }
    // where typing goes, for the OS's input method popup (accents, Chinese, Japanese...)
    virtual void text_spot(double x, double y, double h) { (void)x; (void)y; (void)h; }
};

// A window with no screen, for tests: events come from feed() (false = nothing more: the window
// closes), and each picture shown goes to shown().
std::unique_ptr<Window> headless(int w, int h, double scale, std::function<bool(Event &)> feed,
                                 std::function<void(const raster::Image &)> shown);

// A window run by a script, for tests (no screen needed). Each line is one step:
//   click X Y | dbl X Y | rclick X Y | move X Y | down X Y | up X Y | wheel X Y DY
//   key NAME [shift|ctrl|alt|cmd...] | type TEXT | wait MS | size W H
//   shot NAME (the window as OUTDIR/NAME.png) | pick PATH (the next file picker's answer)
// The window closes when the script ends.
std::unique_ptr<Window> scripted(int w, int h, double scale, const std::vector<std::string> &steps, const std::string &outdir);

// The computer's settings: dark mode, and its accent color ("" when it has none)
bool system_dark();
std::string system_accent();   // "#RRGGBB"
// the computer's own user-interface font: (family, size in points); ("", 0) when unknown
std::pair<std::string, double> system_font();

}  // namespace plat
