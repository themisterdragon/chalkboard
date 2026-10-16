// The window app's frame (port of gui/desktop.py): the desk, the window with its title and status
// line, the menus and shortcut keys, going between screens, and the Welcome and Home screens.
#include <algorithm>
#include <cmath>

#include "art.h"
#include "export_txt.h"
#include "files.h"
#include "gui.h"
#include "proc.h"
#include "text.h"
#include "version.h"

namespace gui {

using namespace ui;

#ifdef __APPLE__
static const char *MOD_LABEL = "Cmd";
#else
static const char *MOD_LABEL = "Ctrl";
#endif

// ----------------------------------------------------------------------------- the menus

enum MenuId {
    M_NEW_LESSON = 1, M_NEW_ASSESSMENT, M_RECENT, M_IMPORT, M_EXPORT_FOLDER, M_EXPORT_ALL, M_DAY, M_MAP, M_BACKUP, M_IMPORT_BACKUP,
    M_CLOSE, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_SELECT_ALL,
    M_HOME, M_SECTION,                  // + 0..3
    M_TERMINAL = 30, M_SKIN,            // + look index
    M_THEME = 40,                       // + 0..2
    M_BIGGER = 50, M_SMALLER, M_FILL,
    M_SHORTCUTS = 60, M_DATA_FILE, M_ABOUT,
    M_RECENT_ITEM = 1000,               // + index
};

static const std::vector<std::pair<std::string, std::string>> THEMES = {
    {"system", "Light or Dark: Match My Computer"}, {"light", "Light Mode"}, {"dark", "Dark Mode"}};

struct RecentItem {
    std::string kind, id, label;
};
static std::vector<RecentItem> recent_items(store::Store &st) {
    std::vector<std::pair<std::string, const Value *>> all;
    for (const auto &x : st.data["lessons"].arr())
        if (x.is_object()) all.push_back({"lesson", &x});
    for (const auto &x : st.data["assessments"].arr())
        if (x.is_object()) all.push_back({"assessment", &x});
    std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.second->s("updated") > b.second->s("updated"); });
    std::vector<RecentItem> out;
    for (size_t i = 0; i < all.size() && i < 10; i++) {
        const Value &x = *all[i].second;
        std::string what = all[i].first == "lesson" ? "Lesson" : (x.s("kind").empty() ? "Assessment" : x.s("kind"));
        std::string t = x.s("title").empty() ? "(untitled)" : x.s("title");
        out.push_back({all[i].first, x.s("id"), t + "   (" + what + ")"});
    }
    return out;
}

static std::vector<plat::MenuItem> menus(App &app) {
    using plat::MenuItem;
    auto item = [](const std::string &label, int id, int key = 0, int mods = 0) {
        MenuItem m;
        m.label = label;
        m.id = id;
        m.key = key;
        m.mods = mods;
        return m;
    };
    MenuItem sep;
    int P = plat::PRIMARY, SH = plat::SHIFT;
    std::vector<MenuItem> out;
    MenuItem f;
    f.label = "File";
    f.sub = {item("New Lesson", M_NEW_LESSON, 'n', P), item("New Assessment…", M_NEW_ASSESSMENT, 'n', P | SH)};
    MenuItem recent = item("Open Recent", M_RECENT);
    auto recents = recent_items(app.store);
    for (size_t i = 0; i < recents.size(); i++) recent.sub.push_back(item(recents[i].label, M_RECENT_ITEM + (int)i));
    if (recent.sub.empty()) {
        MenuItem none = item("(nothing yet)", 0);
        none.enabled = false;
        recent.sub.push_back(none);
    }
    f.sub.push_back(recent);
    f.sub.insert(f.sub.end(), {sep, item("Import Standards…", M_IMPORT), item("Open Export Folder", M_EXPORT_FOLDER),
                               item("Export Everything…", M_EXPORT_ALL), item("Day Slideshow…", M_DAY), item("Curriculum Map…", M_MAP), sep,
                               item("Back Up Everything…", M_BACKUP), item("Import Backup…", M_IMPORT_BACKUP), sep,
                               item("Close Window", M_CLOSE, 'w', P), item("Quit", M_QUIT, 'q', P)});
    out.push_back(f);
    MenuItem e;
    e.label = "Edit";
    e.sub = {item("Undo", M_UNDO, 'z', P), item("Redo", M_REDO, 'z', P | SH), sep, item("Cut", M_CUT, 'x', P), item("Copy", M_COPY, 'c', P),
             item("Paste", M_PASTE, 'v', P), item("Select All", M_SELECT_ALL, 'a', P)};
    out.push_back(e);
    MenuItem g;
    g.label = "Go";
    g.sub = {item("Today", M_HOME, '0', P)};
    for (size_t i = 0; i < SECTIONS.size(); i++) g.sub.push_back(item(SECTIONS[i].second, M_SECTION + (int)i, '1' + (int)i, P));
    out.push_back(g);
    MenuItem v;
    v.label = "View";
    v.sub = {item("Switch to Terminal View", M_TERMINAL, 'w', P | SH), sep};
    std::string kind = app.settings().s("gui_skin");
    if (kind.empty()) kind = "modern";
    for (size_t i = 0; i < skins().size(); i++) {
        MenuItem m = item(skins()[i][1] + " Look", M_SKIN + (int)i);
        m.checked = skins()[i][0] == kind;
        v.sub.push_back(m);
    }
    v.sub.push_back(sep);
    for (size_t i = 0; i < THEMES.size(); i++) {
        MenuItem m = item(THEMES[i].second, M_THEME + (int)i);
        m.checked = THEMES[i].first == app.theme();
        v.sub.push_back(m);
    }
    v.sub.insert(v.sub.end(), {sep, item("Bigger Text", M_BIGGER, '=', P), item("Smaller Text", M_SMALLER, '-', P), sep,
                               item("Fill the Screen / Restore", M_FILL)});
    out.push_back(v);
    MenuItem h;
    h.label = "Help";
    h.sub = {item("Keyboard Shortcuts", M_SHORTCUTS), item("Show Data File", M_DATA_FILE), sep, item(std::string("About ") + art::APP + "…", M_ABOUT)};
    out.push_back(h);
    return out;
}

static std::string key_label(int key, int mods) {
    if (!key) return "";
    std::string s;
    if (mods & (plat::CTRL | plat::CMD)) s += std::string(MOD_LABEL) + "+";
    if (mods & plat::SHIFT) s += "Shift+";
    s += key >= 'a' && key <= 'z' ? std::string(1, (char)(key - 32)) : std::string(1, (char)key);
    return s;
}

// ----------------------------------------------------------------------------- the app

App::App(plat::Window &w, bool no_boot) : win(w), ui(w), no_boot_(no_boot) {
    maximized_ = settings().get("gui_maximized").truthy();
    mtime_ = file_mtime();
    apply_skin();
    win.min_size(760, 520);
}

int App::scale() {
    int v = (int)settings().get("gui_scale").as_int(0);
    if (v >= 1 && v <= 3) return v;
    return std::max(1, std::min(3, (int)std::lround(win.scale())));
}

int App::text_px() {
    int v = (int)settings().get("gui_text").as_int(13);
    const auto &sizes = text_sizes();
    return std::find(sizes.begin(), sizes.end(), v) != sizes.end() ? v : 13;
}

std::string App::theme() {
    std::string v = settings().s("gui_theme");
    for (const auto &t : THEMES)
        if (t.first == v) return v;
    return "system";
}

void App::apply_skin() {
    std::string kind = settings().s("gui_skin");
    bool known = false;
    for (const auto &s : skins()) known |= s[0] == kind;
    if (!known) kind = "modern";
    bool follow = theme() == "system";
    std::string acc = kind == "modern" ? accent() : "";
    seen_os_ = {follow ? (int)plat::system_dark() : -1, settings().s("gui_accent") == "system" ? acc : ""};
    bool dark = follow ? plat::system_dark() : theme() == "dark";
    ui.skin = make_skin(kind, scale(), text_px(), dark, acc);
    win.dark_title(dark);
    build_os_menu();
}

// Modern's highlight color: Chalkboard green, the school's colors, or the computer's accent
std::string App::accent() {
    std::string pick = settings().s("gui_accent");
    if (pick == "system") return plat::system_accent();
    if (pick != "school") return "";
    // the more colorful of the two school colors (a gray or a near-white doesn't make a highlight)
    std::string best;
    double most = 0.22;
    for (const char *key : {"primary_color", "secondary_color"}) {
        std::string h = settings().s(key);
        if (h.size() != 7 || h[0] != '#') continue;
        Color c = hex(h);
        double hi = std::max({c.r, c.g, c.b}), lo = std::min({c.r, c.g, c.b});
        double sat = hi <= 0 ? 0 : (hi - lo) / hi;
        if (sat > most) most = sat, best = h;
    }
    return best;
}

void App::set_look(const std::string &key, const Value &v) {
    save();
    if (!key.empty()) settings()[key] = v;
    save();
    apply_skin();
    if (current_) view_ = current_(*this);   // rebuilt in the new look
    set_title(view_ ? view_->title() : art::APP);
    ui.again();
}

void App::zoom(int d) {
    const auto &sizes = text_sizes();
    int i = (int)(std::find(sizes.begin(), sizes.end(), text_px()) - sizes.begin()) + d;
    if (i >= 0 && i < (int)sizes.size()) set_look("gui_text", sizes[(size_t)i]);
}

void App::toggle_zoom() {
    if (ui.sk().soft) {   // the real OS window
        win.maximize(!win.maximized());
        return;
    }
    maximized_ = !maximized_;
    settings()["gui_maximized"] = maximized_;
    save();
    ui.again();
}

// ---- saving

long long App::file_mtime() { return (long long)(files::mtime(store.path) * 1000); }

bool App::save() {
    if (save_timer_) {
        ui.cancel(save_timer_);
        save_timer_ = 0;
    }
    std::string err;
    if (store.save(&err)) {
        mtime_ = file_mtime();
        return true;
    }
    alert(ui, "Couldn't Save", "Chalkboard couldn't save your work:\n" + err, "warn");
    return false;
}

void App::save_soon() {
    if (save_timer_) ui.cancel(save_timer_);
    save_timer_ = ui.after(700, [this] { autosave(); });
}

void App::autosave() {
    save_timer_ = 0;
    if (save()) status("Saved at " + store::now().substr(store::now().size() - 5));
}

void App::touch(Value &obj) {
    obj["updated"] = store::now();
    save_soon();
}

void App::watch_file() {
    long long m = file_mtime();
    if (m != mtime_ && !save_timer_ && !modal()) {
        mtime_ = m;
        store.load();
        store.reload_standards();
        if (view_) view_->reloaded();
        status("Loaded changes saved by another Chalkboard window.");
    }
    ui.after(2000, [this] { watch_file(); });
}

void App::watch_os_look() {
    bool os_accent = ui.sk().modern && settings().s("gui_accent") == "system";
    if (theme() == "system" || os_accent) {
        std::pair<int, std::string> now = {theme() == "system" ? (int)plat::system_dark() : -1, os_accent ? plat::system_accent() : ""};
        if (now != seen_os_ && !modal() && !save_timer_) set_look();
    }
    ui.after(3000, [this] { watch_os_look(); });
}

void App::quit() {
    save();
    ui.quit();
}

void App::status(const std::string &left, const std::string &right) {
    if (left == status_l_ && (right == "\x01" || right == status_r_)) return;
    status_l_ = left;
    if (right != "\x01") status_r_ = right;
    ui.again();
}

void App::progress(double frac) {
    if (frac == progress_) return;
    progress_ = frac;
    ui.again();
}

// ---- screens

void App::later(std::function<void()> fn) {
    pending_.push_back(std::move(fn));
    ui.again();
}

void App::show(const Factory &f, bool push) {
    later([this, f, push] { do_show(f, push); });
}

void App::do_show(const Factory &f, bool push) {
    if (view_) {
        save();
        if (push && current_) {
            stack_.push_back(current_);
            trail_.push_back(current_title);
        }
    }
    current_ = f;
    view_.reset();
    status_l_.clear();
    status_r_.clear();
    progress_ = -1;
    view_ = f(*this);
    set_title(view_->title());
    ui.focus(0);   // each screen puts the keyboard where it makes sense
}

void App::back() {
    later([this] {
        if (stack_.empty()) return;
        Factory f = stack_.back();
        stack_.pop_back();
        trail_.pop_back();
        do_show(f, false);
    });
}

void App::go_section(const std::string &key) {
    later([this, key] {
        save();
        stack_.clear();
        trail_.clear();
        view_.reset();
        current_ = nullptr;
        section_ = key;
        do_show(section_factory(key), false);
    });
}

void App::set_title(const std::string &t) {
    current_title = t;
    // the OS title bar names the window in Modern (and screen magnifiers and task switchers show it)
    win.title(ui.sk().soft && t != art::APP ? t + " \xE2\x80\x93 " + art::APP : art::APP);
}

Value *App::lesson(const std::string &id) {
    for (auto &l : store.data["lessons"].arr_ref())
        if (l.is_object() && l.s("id") == id) return &l;
    return nullptr;
}

void App::open_item(const std::string &kind, const std::string &id) {
    if (kind == "lesson") {
        go_section("lessons");
        show(lesson_editor(id));
    } else {
        go_section("assessments");
        show(open_assessment(id));
    }
}

void App::cmd_new_lesson() {
    go_section("lessons");
    later([this] { if (view_) view_->command("new"); });
}

void App::cmd_new_assessment() {
    go_section("assessments");
    later([this] { if (view_) view_->command("new"); });
}

void App::open_export_folder() {
    files::make_dirs(store.export_dir());
    proc::open_path(store.export_dir());
}

// ----------------------------------------------------------------------------- dialogs

void App::about() {
    const Value &d = store.data;
    std::string info = plural((long long)d.get("lessons").size(), "lesson plan") + ", " + plural((long long)d.get("assessments").size(), "assessment") +
                       "\n" + commas(store.kas_count) + " standards in " + plural((long long)store.subjects.size(), "subject") +
                       "\n\nYour work saves by itself to:\n" + store.path +
                       "\n\nThe terminal version (chalkboard) uses the same file, so you can switch any time.";
    dialog(ui, {std::string("About ") + art::APP, 520, 0},
           [&](Ui &u) {
               Skin &k = u.sk();
               int S = u.S();
               Rect top = u.place(0, 64 * S);
               if (u.drawing) {
                   u.pixel_art(*icon("logo"), icon_color, top.x, top.y, 4 * S);
                   double x = top.x + 64 * S + 16 * S;
                   u.text(*k.fbig, x, top.y + k.fbig->ascent, art::APP, k["text"]);
                   u.text(*k.f, x, top.y + k.fbig->line + k.f->ascent, std::string("Lesson planning system, version ") + CHALKBOARD_VERSION, k["text"]);
                   u.text(*k.f, x, top.y + k.fbig->line + k.f->line + 2 * S + k.f->ascent, "Free software under the GNU GPL v3.", k["dim"]);
               }
               u.space(8 * S);
               para(u, info);
               return 0;
           },
           {{"OK", 1}});
}

void App::shortcuts() {
    std::vector<std::pair<std::string, std::string>> rows = {
        {"Ctrl+N", "New lesson (or new item in the open list)"}, {"Ctrl+Shift+N", "New assessment"},
        {"Ctrl+O / Return", "Open the selected item"}, {"Ctrl+D", "Duplicate the selected item"},
        {"Delete", "Delete the selected item"}, {"Ctrl+P", "Preview"}, {"Ctrl+E", "Export…"},
        {"Ctrl+F", "Search"}, {"Ctrl+W / Esc", "Close this window (go back)"},
        {"Ctrl+0 … 5", "Today, Lessons, Assessments, Standards, Settings, Calendar"},
        {"Ctrl+= / Ctrl+-", "Bigger / smaller text"}, {"Tab", "Next field"},
        {"Ctrl+B / Ctrl+I / Ctrl+U", "Bold, italic, or underline the word (or what's selected)"},
        {"Ctrl+Shift+W", "Switch between the window view and the terminal view"},
        {"Ctrl+Q", "Quit (everything is already saved)"}};
    for (auto &r : rows) r.first = text::replace(r.first, "Ctrl", MOD_LABEL);
    dialog(ui, {"Keyboard Shortcuts", 0, 0},
           [&](Ui &u) {
               Skin &k = u.sk();
               int S = u.S();
               double kw = 0;
               for (auto &r : rows) kw = std::max(kw, k.fb->width(r.first));
               for (auto &r : rows) {
                   u.row({kw + 18 * S, 0});
                   label(u, r.first, true);
                   label(u, r.second);
               }
               return 0;
           },
           {{"OK", 1}});
}

void App::preview(const Value &doc, const std::string &title) {
    std::string t = !title.empty() ? title : (doc.s("title").empty() ? "Untitled" : doc.s("title"));
    std::vector<std::string> lines = export_txt::render_lines(doc, 78);
    dialog(ui, DialogBox{"Preview: " + t, 0, 0, 0.78, 0.86},
           [&](Ui &u) {
               Rect r = u.place(0, std::max(100.0, u.remaining().h - 4 * u.S()));
               text_view(u, "preview", r, lines);
               return 0;
           },
           {{"Close", 1}});
}

// ----------------------------------------------------------------------------- Welcome and Home

namespace {

class Welcome : public Screen {
public:
    explicit Welcome(App &a) : Screen(a) {
        timer_ = a.ui.after(2800, [this] { go(); });
    }
    ~Welcome() override { app.ui.cancel(timer_); }
    std::string title() override { return art::APP; }
    bool home() const override { return true; }
    bool bare() const override { return true; }
    bool escape() override {
        go();
        return true;
    }
    void frame(Ui &ui, const Rect &r) override {
        Skin &k = ui.sk();
        int S = ui.S();
        Color bg = k.bevel ? k["field"] : k["window"];
        ui.fill(r, bg);
        if ((ui.has(plat::Event::DOWN) && ui.over(r)) || ui.has(plat::Event::KEY) || ui.has(plat::Event::TEXT)) {
            ui.consume();
            go();
        }
        if (!ui.drawing) return;
        Font &huge = *k.fhuge;
        double logo = 16 * 10 * S;
        double h = logo + 10 * S + huge.line + k.f->line + 22 * S + k.fsmall->line;
        double y = r.y + r.h * 0.44 - h / 2, cx = r.x + r.w / 2;
        ui.pixel_art(*icon("logo"), icon_color, std::round(cx - logo / 2), std::round(y), 10 * S);
        y += logo + 10 * S;
        ui.text_in(huge, {r.x, y, r.w, huge.line}, art::APP, k["text"], CENTER);
        y += huge.line;
        ui.text_in(*k.f, {r.x, y, r.w, k.f->line}, std::string("Lesson Planning System   \xC2\xB7   Version ") + CHALKBOARD_VERSION, k["text"], CENTER);
        y += k.f->line + 22 * S;
        ui.text_in(*k.fsmall, {r.x, y, r.w, k.fsmall->line}, "Click anywhere or press any key to begin.", k["dim"], CENTER);
    }

private:
    int timer_ = 0;
    bool done_ = false;
    void go() {
        if (done_ || app.modal()) return;
        done_ = true;
        app.show(make_home(), false);
    }
};


}  // namespace

static Factory make_welcome() { return make<Welcome>(); }

// ----------------------------------------------------------------------------- the window

void App::desk(const Rect &r) {
    Skin &k = ui.sk();
    if (!ui.drawing) return;
    ui.fill(r, k["desk"]);
    if (!k.desk2) return;
    // Pinstripe's dither: every other pixel on every other row
    int S = k.S;
    Color c = k["desk2"];
    for (int y = (int)r.y; y < (int)r.b(); y += 2 * S)
        for (int x = (int)r.x + S; x < (int)r.r(); x += 2 * S) ui.fill({(double)x, (double)y, (double)S, (double)S}, c);
}

Rect App::window_frame(const Rect &area) {
    Skin &k = ui.sk();
    int S = k.S;
    bool modern = k.soft;
    double m = maximized_ || modern ? 0 : 14 * S;
    double shadow = k.pinstripe ? 2 * S : 0;
    Rect w{area.x + m, area.y + m, area.w - 2 * m - shadow, area.h - 2 * m - shadow};
    if (k.pinstripe) ui.fill({w.x + shadow, w.y + shadow, w.w, w.h}, k["dark"]);
    Rect in = w;
    if (!modern) {
        ui.fill(w, k["dark"]);
        in = w.in(S);
        if (k.bevel) {   // the thick gray sizing border, then a black line
            ui.fill(in, k["face"]);
            in = in.in(3 * S);
            ui.fill(in, k["dark"]);
            in = in.in(S);
        }
        double th = title_h(ui);
        int clicked = title_bar(ui, {in.x, in.y, in.w, th}, current_title, !stack_.empty(), true, k.bevel);
        if (clicked == 1) back();
        else if (clicked == 2) toggle_zoom();
        else if (clicked == 3) win.minimize();
        in.y += th;
        in.h -= th;
        ui.fill({in.x, in.y, in.w, (double)S}, k["dark"]);
        in.y += S;
        in.h -= S;
    }
    // the status line
    Font &sf = k.bevel ? *k.f : *k.fsmall;
    auto status_text = [&](const Rect &r, Color bg, Color fg, bool sunken) {
        if (!ui.drawing) return;
        if (sunken) {
            ui.fill(r, k["shadow"]);
            ui.fill({r.x + S, r.y + S, r.w - S, r.h - S}, k["light"]);
            ui.fill({r.x + S, r.y + S, r.w - 2 * S, r.h - 2 * S}, bg);
        } else
            ui.fill(r, bg);
        ui.clip(r);
        ui.text_in(sf, {r.x + 6 * S, r.y, r.w - 12 * S, r.h}, status_l_, fg);
        ui.unclip();
    };
    double sh = sf.line + 2 * S + (k.bevel ? 2 * S : 0);
    Rect bar;
    if (modern) {
        // Modern and Glossy show the status at the foot of the sidebar
        ui.fill(in, k["window"]);
        return in;
    } else if (k.status_top) {
        bar = {in.x, in.y, in.w, sh};
        ui.fill(bar, k["window"]);
        in.y += sh;
        in.h -= sh;
        ui.fill({in.x, in.y, in.w, (double)S}, k["dark"]);
        ui.fill({in.x, in.y + 2 * S, in.w, (double)S}, k["dark"]);
        in.y += 3 * S;
        in.h -= 3 * S;
    } else {
        bar = {in.x, in.b() - sh - 4 * S, in.w, sh + 4 * S};
        ui.fill(bar, k["face"]);
        in.h -= bar.h;
        bar = bar.in(2 * S);
    }
    if (ui.drawing) {
        Color bg = k.bevel ? k["face"] : k["window"], fg = modern ? k["dim"] : k["text"];
        double rw = status_r_.empty() ? 0 : std::ceil(sf.width(status_r_)) + 14 * S;
        double pw = progress_ >= 0 ? 160 * S + 6 * S : 0;
        Rect left{bar.x, bar.y, bar.w - rw - pw - (rw > 0 ? 2 * S : 0), bar.h};
        status_text(left, bg, fg, k.bevel);
        if (pw > 0) {
            Rect p{left.r() + 3 * S, bar.y + (bar.h - 12.0 * S) / 2, 160.0 * S, 12.0 * S};
            ui::progress(ui, p, progress_);
        }
        if (rw > 0) {
            Rect rr{bar.r() - rw, bar.y, rw, bar.h};
            if (k.bevel) {
                ui.fill(rr, k["shadow"]);
                ui.fill({rr.x + S, rr.y + S, rr.w - S, rr.h - S}, k["light"]);
                ui.fill({rr.x + S, rr.y + S, rr.w - 2 * S, rr.h - 2 * S}, bg);
            }
            ui.text_in(sf, {rr.x + 7 * S, rr.y, rr.w - 7 * S, rr.h}, status_r_, fg);
        }
    }
    ui.fill(in, k["window"]);
    return in;
}

double App::menu_bar(const Rect &r) {
    Skin &k = ui.sk();
    int S = k.S;
    Font &f = k.pinstripe ? *k.fb : *k.f;
    double h = f.line + 8 * S;
    Rect bar{r.x, r.y, r.w, h};
    ui.fill(bar, k.bevel ? k["face"] : k.soft ? k["sidebar"] : k["window"]);
    ui.fill({r.x, r.y + h - S, r.w, (double)S}, k.soft ? k["line"] : k["dark"]);
    auto ms = menus(*this);
    double x = r.x + 4 * S;
    for (size_t i = 0; i < ms.size(); i++) {
        double w = f.width(ms[i].label) + 16 * S;
        Rect cell{x, r.y, w, h - S};
        bool open = menu_open_ == (int)i;
        if (ui.has(plat::Event::DOWN) && ui.ev.button == 1 && ui.over(cell) && !modal()) {
            ui.consume();
            open_menu((int)i, cell.x, cell.b());
        }
        if (ui.drawing) {
            if (open) ui.fill(cell, k["sel"]);
            ui.text_in(f, cell, ms[i].label, open ? k["seltext"] : k["text"], CENTER);
        }
        x += w;
    }
    return h;
}

void App::open_menu(int i, double x, double y) {
    auto ms = menus(*this);
    Skin &k = ui.sk();
    Font &f = k.pinstripe ? *k.fb : *k.f;
    while (i >= 0 && i < (int)ms.size()) {
        // where this menu's title sits in the bar
        double mx = 4 * k.S;
        for (int j = 0; j < i; j++) mx += f.width(ms[(size_t)j].label) + 16 * k.S;
        if (x < 0) x = mx;
        menu_open_ = i;
        std::vector<MenuEntry> items;
        for (const auto &it : ms[(size_t)i].sub) {
            MenuEntry e;
            e.label = it.sub.empty() || it.label.empty() ? it.label : it.label + "  \xE2\x96\xB8";
            e.enabled = it.enabled;
            e.checked = it.checked;
            e.radio = it.checked || (it.id >= M_SKIN && it.id < M_SKIN + 10) || (it.id >= M_THEME && it.id < M_THEME + 10);
            e.keys = key_label(it.key, it.mods);
            items.push_back(e);
        }
        int got = popup_menu(ui, mx, y, items, 0);
        menu_open_ = -1;
        if (got == -2 || got == -3) {   // Left/Right: the next menu over
            i = (i + (got == -2 ? -1 : 1) + (int)ms.size()) % (int)ms.size();
            x = -1;
            continue;
        }
        if (got >= 0) {
            const plat::MenuItem &it = ms[(size_t)i].sub[(size_t)got];
            if (!it.sub.empty()) {   // Open Recent: its items in a menu of their own
                std::vector<MenuEntry> sub;
                for (const auto &s : it.sub) {
                    MenuEntry e;
                    e.label = s.label;
                    e.enabled = s.enabled;
                    sub.push_back(e);
                }
                int r = popup_menu(ui, mx + 24 * k.S, y + 24 * k.S, sub, 0);
                if (r >= 0 && it.sub[(size_t)r].id) menu_command(it.sub[(size_t)r].id);
            } else
                menu_command(it.id);
        }
        break;
    }
    ui.again();
}

void App::build_os_menu() {
    win.menu_bar(menus(*this));
}

void App::menu_command(int id) {
    auto key = [&](int k, int mods) {
        plat::Event e;
        e.type = plat::Event::KEY;
        e.key = k;
        e.mods = mods;
        ui.send(e);
    };
    int P = plat::PRIMARY;
    if (id >= M_RECENT_ITEM) {
        auto items = recent_items(store);
        size_t i = (size_t)(id - M_RECENT_ITEM);
        if (i < items.size()) open_item(items[i].kind, items[i].id);
        return;
    }
    if (id >= M_SKIN && id < M_SKIN + 10) {
        size_t i = (size_t)(id - M_SKIN);
        if (i < skins().size()) set_look("gui_skin", skins()[i][0]);
        return;
    }
    if (id >= M_THEME && id < M_THEME + 10) {
        size_t i = (size_t)(id - M_THEME);
        if (i < THEMES.size()) set_look("gui_theme", THEMES[i].first);
        return;
    }
    if (id >= M_SECTION && id < M_SECTION + (int)SECTIONS.size()) {
        go_section(SECTIONS[(size_t)(id - M_SECTION)].first);
        return;
    }
    switch (id) {
    case M_NEW_LESSON: cmd_new_lesson(); break;
    case M_NEW_ASSESSMENT: cmd_new_assessment(); break;
    case M_IMPORT:
        go_section("standards");
        later([this] { import_standards(*this); });
        break;
    case M_EXPORT_FOLDER: open_export_folder(); break;
    case M_EXPORT_ALL: export_everything_dialog(*this); break;
    case M_DAY: day_slideshow(*this); break;
    case M_MAP: curriculum_map(*this); break;
    case M_BACKUP: backup_now(*this); break;
    case M_IMPORT_BACKUP: import_backup(*this); break;
    case M_CLOSE: back(); break;
    case M_QUIT: quit(); break;
    case M_UNDO: key('z', P); break;
    case M_REDO: key('z', P | plat::SHIFT); break;
    case M_CUT: key('x', P); break;
    case M_COPY: key('c', P); break;
    case M_PASTE: key('v', P); break;
    case M_SELECT_ALL: key('a', P); break;
    case M_HOME: go_section(""); break;
    case M_TERMINAL: enter_terminal(); break;
    case M_BIGGER: zoom(1); break;
    case M_SMALLER: zoom(-1); break;
    case M_FILL: toggle_zoom(); break;
    case M_SHORTCUTS: shortcuts(); break;
    case M_DATA_FILE: proc::open_path(files::dirname(store.path)); break;
    case M_ABOUT: about(); break;
    }
}

// keys nothing else used: the shortcuts
bool App::keys(const plat::Event &e) {
    if (modal()) return false;
    int P = plat::PRIMARY, m = e.mods & ~0;
    auto dispatch = [&](const std::string &name) {
        if (!view_ || !view_->command(name)) {
            if (name == "new") cmd_new_lesson();
        }
    };
    if (m == P) {
        switch (e.key) {
        case 'n': dispatch("new"); return true;
        case 'o': dispatch("open"); return true;
        case 'p': dispatch("preview"); return true;
        case 'e': dispatch("export"); return true;
        case 'f': dispatch("find"); return true;
        case 'd': dispatch("duplicate"); return true;
        case 'w': back(); return true;
        case 'q': quit(); return true;
        case '=': case '+': zoom(1); return true;
        case '-': zoom(-1); return true;
        case '0': go_section(""); return true;
        default: break;
        }
        if (e.key >= '1' && e.key < '1' + (int)SECTIONS.size()) {
            go_section(SECTIONS[(size_t)(e.key - '1')].first);
            return true;
        }
    }
    if (m == (P | plat::SHIFT)) {
        if (e.key == 'n') { cmd_new_assessment(); return true; }
        if (e.key == 'w') { enter_terminal(); return true; }
        if (e.key == '+' || e.key == '=') { zoom(1); return true; }
    }
#ifndef __APPLE__
    // Alt+letter opens that menu, F10 the first one
    if (m == plat::ALT || (e.key == plat::K_F10 && !m)) {
        auto ms = menus(*this);
        for (size_t i = 0; i < ms.size(); i++)
            if (e.key == plat::K_F10 ? i == 0 : std::tolower((unsigned char)ms[i].label[0]) == e.key) {
                Skin &k = ui.sk();
                open_menu((int)i, -1, (k.pinstripe ? k.fb : k.f)->line + 8 * k.S);
                return true;
            }
    }
#endif
    if (e.key == plat::K_ESCAPE && !m) {
        if (view_ && view_->escape()) return true;
        if (!stack_.empty()) back();
        return true;
    }
    return false;
}

void App::frame() {
    Skin &k = ui.sk();
    Rect all{0, 0, (double)ui.W(), (double)ui.H()};
    double top = 0;
#ifndef __APPLE__
    top = menu_bar(all);
#endif
    Rect area{0, top, all.w, all.h - top};
    desk(area);
    if (!view_) return;
    Rect body = window_frame(area);
    if (view_->bare()) {
        ui.push_id("view");
        view_->frame(ui, body);
        ui.pop_id();
        return;
    }
    // the sidebar on the left, the page on the right
    double sw = sidebar_w(body.w);
    Rect side{body.x, body.y, sw, body.h}, page{body.x + sw, body.y, body.w - sw, body.h};
    // the page first, so it gets the keyboard first (Tab goes through the page, then the sidebar)
    if (ui.drawing) ui.fill(page, k["window"]);
    if (!view_->home()) page = page_header(page);
    ui.push_id("view");
    view_->frame(ui, page);
    ui.pop_id();
    ui.push_id("sidebar");
    sidebar(side);
    ui.pop_id();
}

void App::run() {
    ui.on_close = [this] { quit(); };
    ui.on_key = [this](const plat::Event &e) { return keys(e); };
    ui.on_menu = [this](int id) {
        if (!modal()) menu_command(id);   // (like the drawn menus: not while a dialog is open)
    };
    bool first_time = store.warning.empty() && setup_needed(*this);
    bool welcome = (settings().has("boot") ? settings().get("boot").truthy() : true) && !no_boot_ && !first_time && !ui.sk().soft;
    current_ = welcome ? make_welcome() : make_home();
    view_ = current_(*this);
    set_title(view_->title());
    if (!store.warning.empty()) {
        std::string w = store.warning;
        store.warning.clear();
        if (!w.empty() && w[0] == '?') w = w.substr(1);
        ui.after(300, [this, w] { alert(ui, "Chalkboard", w, "warn"); });
    } else if (first_time)
        ui.after(300, [this] { run_setup(*this); });
    if (settings().s("gui_mode") == "terminal" && !first_time) ui.after(50, [this] { enter_terminal(!no_boot_); });
    ui.after(2000, [this] { watch_file(); });
    ui.after(3000, [this] { watch_os_look(); });
    ui.run([this] {
        frame();
        // screen changes asked for during this pass
        while (!pending_.empty()) {
            auto todo = std::move(pending_);
            pending_.clear();
            for (auto &fn : todo) fn();
            ui.again();
        }
    });
    save();
}

}  // namespace gui
