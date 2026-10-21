// Chalkboard in a window (port of gui/): the same planner and data file as the terminal app, drawn
// by Chalkboard itself (ui.h) in a Bevel, Pinstripe, or Modern look, light or dark.
//
// The window shows one screen at a time (Home, Lesson Plans, a lesson...). A screen is an object
// whose frame() lays out and draws it from the data each time something happens. Screens hold
// lessons and assessments by id and look them up each time, never by pointer: a list growing,
// or a reload from disk, would leave a pointer dangling.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dates.h"
#include "json.h"
#include "plat.h"
#include "schoolcal.h"
#include "store.h"
#include "ui.h"

namespace gui {

using json::Value;
using ui::Color;
using ui::Rect;
using ui::Ui;

class App;

// One screen of the window. The App owns it; the screen holds the App.
class Screen {
public:
    explicit Screen(App &app) : app(app) {}
    virtual ~Screen() = default;
    App &app;
    virtual std::string title() = 0;
    virtual void frame(Ui &ui, const Rect &body) = 0;
    virtual bool escape() { return false; }   // Esc: true when it was used here (clearing a search)
    virtual void reloaded() {}                // the data file changed on disk
    // the menu and keyboard commands: "new", "open", "preview", "export", "find", "duplicate"
    virtual bool command(const std::string &name) { (void)name; return false; }
    virtual bool home() const { return false; }   // no page header (Today draws its own; Welcome)
    virtual bool bare() const { return false; }   // the whole window, no sidebar (Welcome)
};
using Factory = std::function<std::unique_ptr<Screen>(App &)>;

template <typename T, typename... A>
Factory make(A... args) {
    return [=](App &a) { return std::unique_ptr<Screen>(new T(a, args...)); };
}

class App {
public:
    App(plat::Window &w, bool no_boot);
    void run();

    plat::Window &win;
    Ui ui;
    store::Store store;
    Value &settings() { return store.settings(); }

    // ---- the look
    void apply_skin();
    std::string accent();   // Modern's highlight color ("" = Chalkboard green)
    void set_look(const std::string &key = "", const Value &v = Value());   // change a setting, then rebuild
    void zoom(int d);
    int text_px();
    std::string theme();   // "system", "light", "dark"
    int scale();

    // ---- saving
    bool save();
    void save_soon();      // typing autosaves a moment after the last key
    void touch(Value &obj);   // obj changed: mark it and save soon

    // ---- the status line
    void status(const std::string &left, const std::string &right = "\x01");   // "\x01" = leave the right side
    void progress(double frac);   // < 0 hides it

    // ---- screens. Changes happen between passes (a screen can't be replaced while it's running).
    void show(const Factory &f, bool push = true);
    void back();
    void go_section(const std::string &key);   // "" = Today
    const std::string &section() const { return section_; }
    void plan_lesson(const Date &d);           // a new lesson on that day (asks its name), opened
    void later(std::function<void()> fn);       // after this pass (a command that needs the new screen)
    Screen *view() { return view_.get(); }
    std::string current_title;
    void set_title(const std::string &t);

    // ---- lessons and assessments, by id
    Value *lesson(const std::string &id);
    Value *assessment(const std::string &id) { return store.assessment(id); }
    void open_item(const std::string &kind, const std::string &id);

    // ---- shared dialogs and commands
    void preview(const Value &doc, const std::string &title = "");
    void about();
    void shortcuts();
    void open_export_folder();
    void enter_terminal(bool boot = false);
    void quit();
    bool modal() const { return ui.modal_depth() > 0; }
    void cmd_new_lesson();
    void cmd_new_assessment();
    void sync_mtime() { mtime_ = file_mtime(); }   // the data file was just written here

    std::string std_subject;   // the standards subject last picked (this session)
    std::vector<schoolcal::Move> undo_moves;                 // the last Shift Dates (Edit > Undo Shift)

private:
    bool no_boot_;
    std::unique_ptr<Screen> view_;
    Factory current_;
    std::vector<Factory> stack_;
    std::vector<std::string> trail_;
    std::vector<std::function<void()>> pending_;
    std::string status_l_, status_r_;
    double progress_ = -1;
    int save_timer_ = 0;
    bool maximized_ = false;
    long long mtime_ = 0;
    std::pair<int, std::string> seen_os_;   // dark (-1 = not followed), accent
    std::string shown_hour_;                  // watch_file: the hour last drawn (a new one redraws)
    std::string section_;                   // the sidebar's page ("" = Today)
    int menu_open_ = -1;                    // the menu bar's open menu (drawn menus)
    bool in_terminal_ = false;              // the terminal view is showing

    void frame();
    void desk(const Rect &r);
    double sidebar_w(double body_w);
    void sidebar(const Rect &r);        // gui_shell.cpp
    Rect page_header(const Rect &r);    // returns the page's area under it
    Rect window_frame(const Rect &r);   // draws the frame, title, and status; returns the body
    double menu_bar(const Rect &r);     // the drawn menu bar (not on a Mac); returns its height
    void open_menu(int i, double x, double y);
    void build_os_menu();
    void menu_command(int id);
    bool keys(const plat::Event &e);
    void watch_file();
    void watch_os_look();
    long long file_mtime();
    void toggle_zoom();
    void autosave();
    void do_show(const Factory &f, bool push);
    void open_recent(double x, double y);
};

// ---- shared pieces (gui_common.cpp)
std::string grade_label(const std::string &g);
std::string first_line(const std::string &s);
std::string commas(long long n);   // 12,345
std::string plural(long long n, const std::string &word, const std::string &many = "");
extern const std::vector<std::pair<std::string, std::string>> SECTIONS;   // (key, label)
Factory section_factory(const std::string &key);
Factory make_home();      // Today (gui_shell.cpp)
Factory calendar_page();  // the month of lessons (gui_shell.cpp)
ui::Color class_color(ui::Skin &k, const std::string &course);   // each class's own color (gui_shell.cpp)
void line_icon(Ui &ui, const std::string &name, const Rect &box, Color c);   // "today", "lessons", ... (gui_shell.cpp)
extern const std::vector<std::pair<std::string, std::string>> SORT_LABEL;

// menu items from their labels ("" = a separator)
std::vector<ui::MenuEntry> entries(const std::vector<std::string> &labels);
// a label as tall as a control, its text centered up and down (for rows of controls)
void label_mid(Ui &ui, const std::string &text, bool bold = false, bool dim = false);
// A one-line box bound to obj[key] (by a function that finds obj): saves as you type. Returns true when it changed.
bool line_field(App &app, Ui &ui, const std::string &key, Value &obj, const std::string &field, double width = 0);
// A text box bound to obj[field] that grows with what's in it.
bool auto_text(App &app, Ui &ui, const std::string &key, Value &obj, const std::string &field, int min_lines = 2, int max_lines = 16);
// The standards on a lesson or assessment: code + text rows, Choose…, and add-by-code.
// find: looks the object up each time (a dialog can open in the middle)
void standards_field(App &app, Ui &ui, const std::string &key, const std::function<Value *()> &find, const std::string &field = "standards",
                     const std::function<std::string()> &grades = nullptr);

// A list window for lessons or assessments: toolbar, search/unit/sort filters, and the list.
class ItemList : public Screen {
public:
    ItemList(App &app, const std::string &kind, const std::string &pool_key, const std::string &sort_key, const std::string &noun);
    void frame(Ui &ui, const Rect &body) override;
    bool escape() override;
    void reloaded() override {}
    bool command(const std::string &name) override;

protected:
    std::string kind_, pool_key_, sort_key_, noun_;
    std::vector<ui::Column> columns_;
    std::vector<std::string> search_fields_ = {"title", "unit", "course", "date", "standards"};
    std::string q_, unit_, sort_;
    std::string sel_id_;   // the selected item
    bool has_cls_ = false;   // showing one class (cls_; "" = the ones with no class yet)
    std::string cls_;
    int sel_ = -1;
    std::vector<std::string> ids_;   // the items shown, in order

    Value &pool();
    Value *selected();
    std::string class_key() const { return kind_ == "lesson" ? "lesson_class" : "assess_class"; }   // the setting it's kept in
    void open_class(bool has, const std::string &name);
    std::vector<std::string> items();
    std::string new_class();   // the class a new item goes in ("" = none)
    void select(const std::string &id) { sel_id_ = id; }

    // subclasses
    virtual void tools(Ui &ui) = 0;               // the toolbar's buttons
    virtual void filters(Ui &ui) { (void)ui; }    // extra filters
    virtual double filters_w(Ui &ui) { (void)ui; return 0; }   // ... and how wide they are
    virtual std::vector<std::string> row(const Value &x) = 0;
    virtual bool match(const Value &x) { (void)x; return true; }
    virtual std::vector<std::pair<std::string, std::function<void()>>> menu_items() = 0;
    virtual void cmd_new() = 0;
    virtual void cmd_open() = 0;
    virtual void cmd_delete() = 0;
    virtual void cmd_preview() {}
    virtual void cmd_export() {}
    void cmd_rename();
    void cmd_duplicate();
    bool tool_sel(Ui &ui, const std::string &text);   // a toolbar button that needs a selection

private:
    bool focus_list_ = true;
    bool find_ = false;
};

// ---- the screens and dialogs, by file
// gui_lessons.cpp
Factory lesson_list();
Factory lesson_editor(const std::string &id);
bool lesson_sections(App &app);
std::string size_text(const Value &a);
// gui_assess.cpp
Factory assessment_list();
Factory open_assessment(const std::string &id);
std::string new_assessment_dialog(App &app, const std::string &lesson_id = "", const std::string &unit = "", const std::string &cls = "");
// gui_standards.cpp
Factory standards_library();
// returns false when cancelled
bool pick_standards(App &app, const std::vector<std::string> &selected, const std::string &grades, std::vector<std::string> &out);
void import_standards(App &app);
// gui_settings.cpp
Factory settings_view();
void backup_now(App &app);
void import_backup(App &app);
// gui_export.cpp
extern const std::vector<std::pair<std::string, std::string>> STYLE_TEXT;   // board styles
// runs job on its own thread with a progress box (the window keeps drawing); rethrows what it throws
void busy(App &app, const std::string &title, const std::function<void(const std::function<void(int, int, const std::string &)> &)> &job);
void export_done(App &app, const std::vector<std::string> &files);
bool browse(App &app, std::string &folder);   // pick a folder; false when cancelled
void board_options(App &app);
std::string logo_text(App &app);
void logo_options(App &app);
std::string periods_text(App &app);
void class_periods(App &app);
void export_dialog(App &app, const std::string &kind, const std::string &id);
void run_export(App &app, const std::string &kind, const std::string &id, const std::string &fmt);
void export_everything_dialog(App &app);
void day_slideshow(App &app);
void curriculum_map(App &app, const std::string *course = nullptr);
// gui_calendar.cpp: a month calendar for picking days. Arrows move a day or a week, Page Up/Down
// change the month, Home goes to today, Return or Space picks the day (Shift picks through it).
struct Calendar {
    Date start, end, cursor, month;   // start.valid() == false: nothing picked yet
    bool allow_range = false;
    std::function<bool(const Date &)> marks;   // a dot under days that already have something
    void init(const Date *first, const Date *last);
    // lays out and draws it; true when a day (or a range) was just picked
    bool frame(Ui &ui, const std::string &key);
};
std::function<bool(const Date &)> lesson_marks(App &app);
bool pick_dates(App &app, const std::string &title, const Date *first, const Date *last, Date &a, Date &b);
// gui_schoolcal.cpp: the school calendar (Settings), and moving lessons by school days
std::string school_calendar_text(App &app);   // a line about it for Settings
void school_calendar_dialog(App &app);
// Shift Dates (snow: the Snow Day version, from today); true when lessons moved
bool shift_dates(App &app, const std::string &course, const Date &from, bool snow = false);
void undo_shift(App &app);
std::string no_school(App &app, const Date &d);   // why there's no class that day, or ""
// gui_homedays.cpp: days of school at home
Factory home_days_page();
bool home_version_dialog(App &app, const std::string &lesson_id);   // a lesson's at-home version
void home_lessons_dialog(App &app, const std::string &course);   // the lessons kept for at-home days
bool home_day_now(App &app, const Date &day);   // make the day's packets
void home_day_log(App &app, int n);
void snow_day(App &app);   // no school today: move lessons, or an at-home day
// gui_board.cpp
void board_designer(App &app);
// gui_layouts.cpp: Advanced Mode > Page Layouts; true when something changed
bool page_layout_dialog(App &app, const std::string &kind);
// gui_setup.cpp
bool setup_needed(App &app);
std::string pick_color(App &app, const std::string &current);   // a school color; "" when cancelled
void run_setup(App &app);
// gui_buddy.cpp: the school mascot that cheers
struct Buddy {
    int hop = 0;          // pixels up (in art pixels)
    bool arms = false;    // cheering
    bool talking = false;
    std::vector<int> jobs;
    App *app = nullptr;
    ~Buddy();
    void cheer(App &app);
    // draws it with its bottom right at (r, b); returns its box
    Rect draw(App &app, Ui &ui, double r, double b, int scale, bool bubble = true);
};

// The window app. Returns the exit code; -1 when this computer can't show a window.
int run_window(bool no_boot, bool terminal_view);
// The window app run by a script (see plat::scripted); screenshots go to outdir
int run_script(const std::string &script, const std::string &outdir, bool no_boot);

}  // namespace gui
