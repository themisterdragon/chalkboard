// Chalkboard's own window toolkit. The whole window is drawn by Chalkboard (with raster.h), so
// every look (Bevel, Pinstripe, Modern; light or dark) is the same on Windows, macOS, and Linux.
//
// It works "immediate mode": a screen is a function that lays out and draws its controls from the
// data every time something happens, and each control says what happened to it as it goes
// (if (ui.button("Save")) save();). Only small things are remembered between times: which control
// has the keyboard focus, scroll positions, and text being edited. Dialogs block until they close
// (if (confirm(...)) ...), like the Tk version's, by running their own loop over a picture of
// the window behind them.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "plat.h"
#include "raster.h"
#include "ui_font.h"

namespace ui {

using raster::Color;
using Id = uint64_t;

// ----------------------------------------------------------------------------- colors

Color hex(const std::string &h);   // "#RRGGBB"
std::string hex(Color c);
Color mix(Color a, Color b, double t);
double luminance(Color c);
double contrast(Color a, Color b);
// c, darkened or lightened just enough for `ratio` contrast with every color in `against` (WCAG)
Color fit(Color c, const std::vector<Color> &against, double ratio);

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    double r() const { return x + w; }
    double b() const { return y + h; }
    bool has(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect in(double d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect in(double dx, double dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

// ----------------------------------------------------------------------------- looks

// The palettes (skin.py): every pair of colors meets WCAG 2.1 AA, checked by check_contrast.
struct Skin {
    std::string kind;   // "modern", "glossy", "bevel", "pinstripe"
    std::string name;   // "Modern"
    bool dark = false, bevel = false, modern = false, pinstripe = false, glossy = false;
    bool soft = false;  // Modern or Glossy: rounded controls, cards, and roomy lists
    int S = 1;          // the scale: pixels per point of the design
    std::unordered_map<std::string, Color> c;
    bool desk2 = false;   // Pinstripe's dithered desktop
    bool status_top = false;
    std::unique_ptr<Font> f, fb, fi, fbi, fsmall, ftitle, fbig, fhuge, fmono;
    std::unique_ptr<Font> fhead, fpage, fsmallb;   // a card's heading, a page's title, small bold
    double line = 16;   // the text font's line height
    Color operator[](const std::string &k) const;
    int px(double n) const { return (int)(n * S); }
    Font &font(int style);   // 0-3: regular, bold, italic, bold italic
};
// kind: "modern"/"glossy"/"bevel"/"pinstripe"; accent: a color for Modern's highlights ("" = Chalkboard green); text_px: the text size
std::unique_ptr<Skin> make_skin(const std::string &kind, int S, int text_px, bool dark, const std::string &accent);
// (key, name, about) for each look
const std::vector<std::vector<std::string>> &skins();
const std::vector<int> &text_sizes();   // 11 ... 24
// the WCAG problems of one palette (empty = it passes); for scripts/check_contrast
std::vector<std::string> contrast_problems(const std::string &kind, bool dark, const std::string &accent);

// 16x16 pixel art: icons by name ("lessons", "assessments", "standards", "settings", "folder",
// "info", "ask", "warn", "logo")
const std::vector<std::string> *icon(const std::string &name);
// draws rows of pixel art; colors maps each letter to a color ('.' is clear)
void sprite(raster::Image &img, const std::vector<std::string> &rows, const std::function<bool(char, Color &)> &colors, double x, double y,
            int zoom);
bool icon_color(char ch, Color &out);   // the icons' 16-color palette

// ----------------------------------------------------------------------------- the toolkit

enum Align { LEFT, CENTER, RIGHT };

class Ui {
public:
    explicit Ui(plat::Window &w);
    plat::Window &win;
    raster::Image img;           // the window's pixels
    raster::Image base_;         // the window as last drawn with no dialog open
    std::unique_ptr<Skin> skin;
    Skin &sk() { return *skin; }
    int S() const { return skin->S; }

    // ---- running
    // The main loop: frame() lays out and draws the window; runs until quit() is called.
    void run(const std::function<void()> &frame);
    void quit() { quitting_ = true; }
    bool quitting() const { return quitting_; }
    // A dialog: runs body (which lays out its own box) over a picture of the window until it sets done.
    void modal(const std::function<void(bool &done)> &body);
    int modal_depth() const { return modal_depth_; }
    void dim_behind(double alpha) { dim_next_ = alpha; }   // the next modal() dims the window behind it
    // a function to run in ms milliseconds (returns an id for cancel())
    int after(int ms, std::function<void()> fn);
    void cancel(int id);
    void again() { again_ = true; }   // something changed: lay out and draw once more
    void send(const plat::Event &e) { injected_.push_back(e); }   // handle e as if it had happened (menu commands)
    // what happens when the window's close box is clicked (the default quits)
    std::function<void()> on_close;
    // called before each frame, and for keys nothing else used (shortcuts)
    std::function<bool(const plat::Event &)> on_key;
    std::function<void(int)> on_menu;   // the OS's own menu bar (macOS): an item was picked

    // ---- this pass
    bool drawing = false;   // false while only handling input: drawing calls do nothing
    plat::Event ev;         // what's being handled (type NONE when nothing is)
    bool has(plat::Event::Type t) const { return ev.type == t; }
    void consume() { ev.type = plat::Event::NONE; }
    double mx = -1, my = -1;   // the mouse
    int W() const { return img.w; }
    int H() const { return img.h; }

    // ---- ids: from a label, inside push_id()/pop_id() scopes (lists push each row's index)
    Id id(const std::string &label);
    Id id(const char *label) { return id(std::string(label)); }   // by its text (not where it is in memory)
    Id id(const void *p);
    Id id(Id n);
    void push_id(Id i);
    void push_id(const std::string &label) { push_id(id(label)); }
    void pop_id();

    // ---- input
    bool over(const Rect &r) const;   // the mouse is over r (and inside the clip)
    // the control's part in this pass: hover, press, focus. Returns true when it was clicked (a press
    // and release over it). Registers it for Tab when focusable.
    bool hit(Id id, const Rect &r, bool focusable = true, bool enabled = true);
    bool hovered(Id id) const { return hot_ == id; }
    bool held(Id id) const { return active_ == id; }   // pressed and not yet let go
    bool focused(Id id) const { return focus_ == id && id != 0; }
    void focus(Id id);
    Id focus_id() const { return focus_; }
    void focus_first();   // the first control that takes the focus (for a new screen or dialog)
    bool key(int k, int mods = 0);   // a key press with exactly these modifier keys (then it's used up)
    bool typed(std::string &text);   // text typed (when the focused control takes text)
    void capture(Id id) { active_ = id; }   // the mouse goes to this control until the button is let go
    void cursor(plat::Cursor c) { cursor_ = c; }
    Id last_id() const { return last_; }   // the control just placed
    Rect last_rect() const { return last_rect_; }

    // ---- drawing (in pixels; nothing happens while !drawing)
    void fill(const Rect &r, Color c);
    void box(const Rect &r, Color c, double w = 0);   // an outline (w = 0: one design pixel)
    void hline(double x0, double x1, double y, Color c, double w = 0);
    void vline(double x, double y0, double y1, Color c, double w = 0);
    void line(double x0, double y0, double x1, double y1, Color c, double w, bool round = true);
    void polyline(const std::vector<raster::Pt> &pts, Color c, double w, bool closed = false);
    void polygon(const std::vector<raster::Pt> &pts, Color c);
    void rrect(const Rect &r, double radius, Color fill, Color edge, double edge_w, bool has_fill = true, bool has_edge = true);
    void oval(const Rect &r, Color fill, Color edge, double edge_w, bool has_fill = true, bool has_edge = true);
    // a rounded box see-through by alpha (0-1), and one shaded from top to bottom (Glossy, cards)
    void rrect_a(const Rect &r, double radius, Color c, double alpha);
    void gradient(const Rect &r, double radius, Color top, Color bottom, double alpha = 1);
    // a soft shadow under a rounded box (cards, dialogs, menus): depth pixels of blur, strength 0-1
    void shadow(const Rect &r, double radius, double depth, double strength);
    void dotted(const Rect &r, Color c);   // the classic dotted focus box
    // text on a baseline; returns the x after it
    double text(Font &f, double x, double baseline, const std::string &s, Color c);
    // text in a box, vertically centered
    void text_in(Font &f, const Rect &r, const std::string &s, Color c, Align a = LEFT);
    // text cut short with "…" to fit w
    std::string elide(Font &f, const std::string &s, double w);
    // word-wrapped lines of s within width w (explicit newlines kept)
    std::vector<std::string> wrap(Font &f, const std::string &s, double w);
    void pixel_art(const std::vector<std::string> &rows, const std::function<bool(char, Color &)> &colors, double x, double y, int zoom);
    void picture(const raster::Image &pic, double x, double y);   // copied as-is
    void clip(const Rect &r);
    void unclip();
    Rect clip_rect() const;

    // ---- layout: a column of rows. place() hands out the next spot.
    struct Layout {
        Rect body;              // the area (its top moves down as things are placed)
        double y = 0;           // where the next row starts
        std::vector<double> widths;   // the current row's columns (empty = one item per row, full width)
        size_t col = 0;
        double row_y = 0, row_h = 0, fixed_h = 0;
        double x = 0;           // in a row: where the next item goes
        double gap = 0;         // between rows and items
        bool flow = false;      // items left to right at their own widths, wrapping
        double flow_h = 0;
        double max_x = 0;       // the widest thing placed (for sizing dialogs)
    };
    void begin(const Rect &body, double gap = -1);   // a new layout in body
    Rect end();                                       // ...returns the area that was used
    Layout &lay() { return layouts_.back(); }
    // columns for the next items: >0 pixels, 0 = the item's own width, <0 = what's left (all but -w)
    void row(const std::vector<double> &widths, double h = 0);
    void flow(bool on = true);           // items left to right, wrapping
    void space(double h);                 // a vertical gap (ends a row)
    Rect place(double w, double h);       // the next spot for something w x h (w <= 0: full width)
    Rect remaining() const;               // what's left of the area below
    Rect cut_bottom(double h);            // take a strip off the bottom of the area
    Rect cut_right(double w);
    Rect cut_left(double w);
    Rect cut_top(double h);
    double width() const;                 // the area's width

    // ---- scrolling
    // a scrolling area in r; things placed inside scroll. Returns the inner layout's body.
    void begin_scroll(Id id, const Rect &r, double max_width = 0);
    void end_scroll();
    double &scroll_of(Id id) { return scrolls_[id].y; }
    void scroll_to_focus() { see_focus_ = true; }

    // ---- remembered state of a control (text being edited, a list's selection...)
    template <typename T>
    T &state(Id id) {
        auto &p = states_[id];
        if (!p.second) p.second = std::make_shared<Holder<T>>();
        p.first = pass_;
        return static_cast<Holder<T> *>(p.second.get())->v;
    }
    void forget(Id id) { states_.erase(id); }

    // the parts of the window outside widgets (title bar, menus) report their size here
    double ctl_h() const;      // a standard control's height (entry, dropdown, button)
    double btn_w(const std::string &label, bool small = false);

private:
    struct HolderBase {
        virtual ~HolderBase() = default;
    };
    template <typename T>
    struct Holder : HolderBase {
        T v{};
    };
    struct Scroll {
        double y = 0, content = 0, view = 0;
        Rect r, bar;
        bool drag = false;
        double grab = 0;
    };
    struct ScrollFrame {
        Id id;
        Rect r;
        double start;
        double bottom;  // the lowest any layout inside it reached
    };
    struct Timer {
        int id;
        double at;
        std::function<void()> fn;
    };

    bool quitting_ = false, again_ = false, see_focus_ = false;
    double dim_next_ = 0;
    int modal_depth_ = 0;
    uint64_t pass_ = 0;
    std::vector<Id> ids_;
    std::vector<Layout> layouts_;
    std::vector<Rect> clips_;
    std::vector<ScrollFrame> scroll_stack_;
    std::unordered_map<Id, Scroll> scrolls_;
    std::unordered_map<Id, std::pair<uint64_t, std::shared_ptr<HolderBase>>> states_;
    Id hot_ = 0, hot_next_ = 0, active_ = 0, focus_ = 0, last_ = 0;
    Rect last_rect_;
    bool want_first_ = false;
    std::vector<Id> tab_;            // focusable controls this pass, in order
    Rect focus_rect_;
    bool focus_seen_ = false;
    plat::Cursor cursor_ = plat::ARROW;
    std::vector<Timer> timers_;
    std::vector<plat::Event> injected_;
    int next_timer_ = 1;
    double now_ms() const;
    void pass(const std::function<void()> &frame, bool draw);
    void after_pass();
    void fire_timers();
    int wait_ms() const;
    bool next_event(int timeout);
    void resize();
    friend struct ModalScope;
};

// ----------------------------------------------------------------------------- controls (ui_widgets.cpp)

void label(Ui &ui, const std::string &text, bool bold = false, bool dim = false, bool small = false);
// word-wrapped text, as wide as the area (or wrap pixels); returns its height
double para(Ui &ui, const std::string &text, bool dim = false, double wrap = 0, Font *font = nullptr);
bool button(Ui &ui, const std::string &text, bool enabled = true, bool is_default = false, bool small = false, double min_w = 0);
// a button already given a spot
bool button_at(Ui &ui, Id id, const Rect &r, const std::string &text, bool enabled = true, bool is_default = false, bool small = false,
               bool latched = false, Font *font = nullptr, bool takes_focus = true);
bool check(Ui &ui, const std::string &text, bool &value, bool enabled = true, double wrap = 0);
bool radio(Ui &ui, const std::string &text, bool on, bool enabled = true, double wrap = 0);   // true when picked
// a drop-down: options (value, label); returns true when the value changed
bool dropdown(Ui &ui, const std::string &key, const std::vector<std::pair<std::string, std::string>> &options, std::string &value,
              double width_chars = 0, bool enabled = true);
// a group box with a title: begin_group() ... end_group()
void begin_group(Ui &ui, const std::string &title);
void end_group(Ui &ui);
void separator(Ui &ui);
void progress(Ui &ui, const Rect &r, double frac);
// the toolbar strip at the top of a screen (one row): begin_toolbar() ... tool() ... end_toolbar()
void begin_toolbar(Ui &ui);
// a toolbar button (primary: the screen's main action, in the accent color); buttons that don't fit go in "More"
bool tool(Ui &ui, const std::string &text, bool enabled = true, bool primary = false);
void tool_gap(Ui &ui);
// something else in a toolbar, w wide: true when it fits on the row (false: leave it out)
bool toolbar_room(Ui &ui, double w);
void end_toolbar(Ui &ui);

// a pop-up menu at (x, y): (label, enabled) ("" = a separator); returns the index picked or -1
struct MenuEntry {
    std::string label;
    bool enabled = true;
    bool checked = false;
    std::string keys;   // "Ctrl+N", shown at the right
    bool radio = false;
};
// A menu opened from a menu bar: the bar's titles, so moving onto another title opens that menu
// instead (popup_menu returns -(10 + its index)), and drawing the open title highlighted.
struct MenuBarHook {
    std::vector<Rect> cells;
    int current = -1;
    std::function<void(int)> draw;   // draws the bar with title i highlighted
};
int popup_menu(Ui &ui, double x, double y, const std::vector<MenuEntry> &items, double min_w = 0, const MenuBarHook *bar = nullptr);

// A list with columns (a table). The caller keeps the rows; this keeps the selection and scroll.
struct Column {
    std::string head;
    double width;    // design pixels
    bool stretch;    // takes the extra width
    int mark = 0;    // MARK_CHECK: cells "x" / "o" draw a check box; MARK_TREE: "+" / "-" draw an open/close arrow
};
enum { MARK_NONE = 0, MARK_CHECK = 1, MARK_TREE = 2 };
struct ListResult {
    bool changed = false;    // the selection moved
    bool open = false;       // double-click or Return
    bool menu = false;       // right-click (menu_x, menu_y)
    bool del = false;        // Delete
    int sorted = -1;         // a heading was clicked
    int col = -1;            // the column a row was pressed in (this pass)
    bool space = false;      // Space on the selected row
    double menu_x = 0, menu_y = 0;
};
// rows: the text in each column; sel: the selected row (-1 none), kept by the caller
ListResult list_view(Ui &ui, const std::string &key, const Rect &r, const std::vector<Column> &cols,
                     const std::vector<std::vector<std::string>> &rows, int &sel, const std::string &empty_text = "",
                     bool headings = true);

// ---- text editing (ui_edit.cpp)
// one line; returns true when the text changed
bool entry(Ui &ui, const std::string &key, std::string &value, double width = 0, bool enabled = true, bool select_all_on_focus = false);
bool entry_at(Ui &ui, Id id, const Rect &r, std::string &value, bool enabled = true);
// put the keyboard in a one-line box with the cursor at the end (nothing selected); id: ui.id("entry:" + key)
void focus_at_end(Ui &ui, Id id);
// several lines that grow with the text (min..max lines, then it scrolls), showing **bold** *italic*
// __underline__ as formatting; returns true when the text changed
bool text_area(Ui &ui, const std::string &key, std::string &value, int min_lines = 2, int max_lines = 16, bool rich = true,
               bool enabled = true);
// the formatting buttons (B, I, U) for the text box last typed in
void format_bar(Ui &ui);
// read-only text in a scrolling box (fixed-width font): the preview
void text_view(Ui &ui, const std::string &key, const Rect &r, const std::vector<std::string> &lines);

// ---- window parts (ui_widgets.cpp)
// a title bar (the retro looks, and Modern's dialogs); returns which box was clicked: 1 close, 2 zoom, 3 minimize
int title_bar(Ui &ui, const Rect &r, const std::string &title, bool close, bool zoom, bool minimize, bool active = true);
double title_h(Ui &ui);

// ---- dialogs (ui_widgets.cpp): blocking, like the Tk version
struct DialogBox {
    std::string title;
    double w = 0, h = 0;          // design pixels (0 = as needed)
    double fw = 0, fh = 0;        // or fractions of the window
};
// body lays out the dialog's contents and returns: 0 to keep going, or a result to close with.
// buttons: (label, result); Return picks default_idx, Esc gives cancel.
int dialog(Ui &ui, const DialogBox &box, const std::function<int(Ui &)> &body, const std::vector<std::pair<std::string, int>> &buttons,
           int default_idx = 0, int cancel = 0);
void alert(Ui &ui, const std::string &title, const std::string &message, const std::string &icon = "info");
bool confirm(Ui &ui, const std::string &title, const std::string &message, const std::string &yes = "OK", const std::string &no = "Cancel",
             const std::string &icon = "ask");
// a line of text; false when cancelled
bool ask(Ui &ui, const std::string &title, const std::string &message, std::string &value);
// pick one of options (value, label); "" when cancelled
std::string choose(Ui &ui, const std::string &title, const std::string &message, const std::vector<std::pair<std::string, std::string>> &options,
                   const std::string &current = "");

}  // namespace ui
