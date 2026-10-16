// Settings (port of gui/settings.py): you and your class, documents, board slides, the school
// mascot, how this window looks, and your data (backups).
#include <algorithm>
#include <cmath>
#include <set>

#include "art.h"
#include "files.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

std::string sections_text(App &app) {
    std::set<std::string> hide;
    for (const auto &h : app.settings().get("lesson_hide").arr()) hide.insert(store::text_of(h));
    std::vector<std::string> off;
    for (const auto &f : store::LESSON_FIELDS)
        if (hide.count(f.key) && std::find(store::FIXED_FIELDS.begin(), store::FIXED_FIELDS.end(), f.key) == store::FIXED_FIELDS.end()) off.push_back(f.label);
    return off.empty() ? "The lesson editor shows every section." : "Hidden from the lesson editor: " + text::join(off, ", ") + ".";
}

std::string last_backup_text(App &app) {
    const Value &st = app.settings();
    if (!st.get("last_backup").truthy()) return "No backups yet.";
    return "Last backup: " + store::text_of(st.get("last_backup")) + ", in " + app.store.backup_dir();
}

const std::vector<std::pair<std::string, std::string>> THEMES = {{"system", "Match my computer"}, {"light", "Light"}, {"dark", "Dark"}};

class SettingsView : public Screen {
public:
    explicit SettingsView(App &a) : Screen(a) { a.status("Changes save by themselves."); }
    std::string title() override { return "Settings"; }
    void frame(Ui &ui, const Rect &body) override {
        int S = ui.S();
        ui.begin_scroll(ui.id("settings"), body, 1000 * S);
        Rect in = ui.remaining();
        in = {in.x + 12 * S, in.y + 10 * S, ui.width() - 24 * S, 1e9};
        double cw = std::floor((in.w - 12 * S) / 2);
        ui.begin({in.x, in.y, cw, 1e9}, 0);
        left(ui);
        Rect lu = ui.end();
        ui.begin({in.x + cw + 12 * S, in.y, cw, 1e9}, 0);
        right(ui);
        Rect ru = ui.end();
        ui.place(0, 10 * S + std::max(lu.h, ru.h) + 12 * S);
        ui.end_scroll();
    }

private:
    Buddy pet_;

    Value &st() { return app.settings(); }
    void set(const std::string &key, const Value &v) {
        st()[key] = v;
        app.save_soon();
    }
    void look(const std::string &key, const Value &v) {
        app.later([this, key, v] { app.set_look(key, v); });
    }

    void left(Ui &ui) {
        Skin &k = ui.sk();
        int S = ui.S();
        begin_group(ui, "You and Your Class");
        double lw = k.f->width("Your name (for footers):") + 8 * S;
        for (auto p : std::vector<std::pair<std::string, std::string>>{{"teacher", "Your name (for footers)"}, {"school", "School"}, {"course", "Usual course"}}) {
            ui.row({lw, -1}, ui.ctl_h());
            label_mid(ui, p.second + ":");
            line_field(app, ui, p.first, st(), p.first);
        }
        ui.row({lw, 0}, ui.ctl_h());
        label_mid(ui, "Usual subject:");
        std::vector<std::pair<std::string, std::string>> sopts;
        for (const auto &s : app.store.subjects) sopts.push_back({s, s});
        if (sopts.empty()) sopts.push_back({"", "(import standards first)"});
        std::string subj = st().s("subject");
        if (dropdown(ui, "subject", sopts, subj, 22)) set("subject", subj);
        ui.row({lw, 0}, ui.ctl_h());
        label_mid(ui, "Usual grades:");
        std::vector<std::pair<std::string, std::string>> gopts;
        for (const auto &x : store::GRADE_CHOICES) gopts.push_back({x, grade_label(x)});
        std::string gr = st().s("grades").empty() ? "9-10" : st().s("grades");
        if (dropdown(ui, "grades", gopts, gr, 14)) set("grades", gr);
        ui.row({});
        ui.space(8 * S);
        label(ui, "Materials every new lesson starts with:");
        ui.space(2 * S);
        auto_text(app, ui, "materials", st(), "default_materials", 2);
        end_group(ui);

        ui.space(10 * S);
        begin_group(ui, "Printed Documents");
        std::string font = st().s("font").empty() ? "Times" : st().s("font"), page = st().s("page").empty() ? "Letter" : st().s("page");
        ui.flow();
        label_mid(ui, "Font:");
        for (auto p : std::vector<std::pair<std::string, std::string>>{{"Times", "Times (serif)"}, {"Helvetica", "Helvetica (sans)"}})
            if (radio(ui, p.second, font == p.first) && font != p.first) set("font", p.first);
        ui.flow(false);
        ui.flow();
        label_mid(ui, "Paper:");
        for (const char *p : {"Letter", "A4"})
            if (radio(ui, p, page == p) && page != p) set("page", p);
        ui.flow(false);
        ui.space(8 * S);
        label(ui, "Export folder:");
        ui.space(2 * S);
        double bw = ui.btn_w("Browse\xE2\x80\xA6", true);
        std::string &folder = ui.state<std::string>(ui.id("export-folder"));
        bool &loaded = ui.state<bool>(ui.id("export-folder-loaded"));
        if (!loaded) {
            folder = app.store.export_dir();
            loaded = true;
        }
        ui.row({-bw - ui.lay().gap - 6 * S, bw}, ui.ctl_h());
        if (entry(ui, "folder", folder)) set("export_dir", text::strip(folder));
        Rect b = ui.place(bw, ui.ctl_h());
        if (button_at(ui, ui.id("btn:browse"), {b.x + 6 * S, b.y + 3 * S, b.w, b.h - 6 * S}, "Browse\xE2\x80\xA6", true, false, true) && browse(app, folder))
            set("export_dir", text::strip(folder));
        ui.row({});
        end_group(ui);

        ui.space(10 * S);
        begin_group(ui, "Lesson Sections");
        para(ui, sections_text(app));
        ui.space(6 * S);
        if (button(ui, "Choose Sections\xE2\x80\xA6", true, false, true)) lesson_sections(app);
        end_group(ui);

        ui.space(10 * S);
        advanced(ui);
    }

    // Advanced Mode: the nitpicky layout options stay out of sight until a teacher asks for them
    void advanced(Ui &ui) {
        int S = ui.S();
        begin_group(ui, "Advanced Mode");
        bool on = st().get("advanced").truthy();
        if (check(ui, "Show advanced options", on)) set("advanced", on);
        auto changed = store::advanced_changes(st());
        if (!on) {
            std::string t = "Change how printed pages and board slides are laid out: name lines, titles, headings, footers, fonts, and more.";
            if (!changed.empty())
                t += "\n\nYou've changed: " + text::join(changed, ", ") + ". Those changes still apply. Turn this on to change them or reset them.";
            ui.space(4 * S);
            para(ui, t, true);
            end_group(ui);
            return;
        }
        ui.space(4 * S);
        para(ui, "Page layouts: pick a kind of page to change its name line, title, headings, and footer. Each one has a preview and a Reset to Default "
                 "button.",
             true);
        ui.space(6 * S);
        const Value &laid = st().get("page_layouts");
        ui.flow();
        for (const auto &pk : store::page_layouts()) {
            std::string t = pk.name + (laid.get(pk.kind).truthy() ? " (changed)" : "") + "\xE2\x80\xA6";
            ui.push_id(pk.kind);
            if (button(ui, t, true, false, true)) page_layout_dialog(app, pk.kind);
            ui.pop_id();
        }
        ui.flow(false);
        ui.space(6 * S);
        para(ui, "Board slide fonts and layout: Board Slides > Board Designer.", true);
        if (!changed.empty()) {
            ui.space(6 * S);
            para(ui, "Changed from the usual: " + text::join(changed, ", ") + ".", true);
            ui.space(6 * S);
            if (button(ui, "Reset Everything to Default\xE2\x80\xA6", true, false, true) &&
                confirm(app.ui, "Reset Everything?",
                        "Every page layout and the board design go back to how Chalkboard comes. Your lessons, assessments, colors, and logo stay the "
                        "same.",
                        "Reset", "Cancel", "warn")) {
                store::reset_advanced(st());
                app.save();
                app.status("Page layouts and the board design are back to the usual ones.");
            }
        }
        end_group(ui);
    }

    void right(Ui &ui) {
        Skin &k = ui.sk();
        int S = ui.S();
        begin_group(ui, "ViewBoard Board Slides");
        std::string style = st().s("board_style").empty() ? "chalk" : st().s("board_style");
        for (const auto &p : STYLE_TEXT)
            if (radio(ui, p.second, style == p.first) && style != p.first) set("board_style", p.first);
        ui.space(6 * S);
        ui.flow();
        if (button(ui, "Sections & School Colors\xE2\x80\xA6", true, false, true)) board_options(app);
        if (st().get("advanced").truthy() && button(ui, "Board Designer\xE2\x80\xA6", true, false, true)) board_designer(app);
        ui.flow(false);
        ui.space(10 * S);
        if (button(ui, "School Logo\xE2\x80\xA6", true, false, true)) logo_options(app);
        ui.space(2 * S);
        para(ui, logo_text(app), true);
        ui.space(8 * S);
        if (button(ui, "Class Periods & Codes\xE2\x80\xA6", true, false, true)) class_periods(app);
        ui.space(2 * S);
        para(ui, periods_text(app), true);
        end_group(ui);

        ui.space(10 * S);
        begin_group(ui, "School Mascot");
        double pw = chibi::SIZE * 3 * S;
        Rect row = ui.place(0, pw + 30 * S);
        if (!st().s("mascot").empty()) pet_.draw(app, ui, row.x + pw, row.b(), 3 * S, false);
        double mx = row.x + (st().s("mascot").empty() ? 0 : pw + 10 * S);
        ui.begin({mx, row.y, row.r() - mx, row.h}, 0);
        std::vector<std::pair<std::string, std::string>> mopts = {{"", "None"}};
        for (const auto &m : art::mascots()) mopts.push_back({m.first, text::title(m.second)});
        std::string mas = st().s("mascot");
        ui.row({std::min(ui.width(), 16 * k.f->width("0") + 40 * S)});
        if (dropdown(ui, "mascot", mopts, mas, 16)) {
            set("mascot", mas);
            pet_.cheer(app);
        }
        ui.row({});
        ui.space(6 * S);
        para(ui, "Wears your school colors on the home screen. Click it for a cheer; it cheers your exports too.", true);
        ui.end();
        end_group(ui);

        ui.space(10 * S);
        begin_group(ui, "This Window");
        label(ui, "Look:");
        for (const auto &s : skins()) {
            ui.push_id(s[0]);
            ui.begin({ui.remaining().x + 12 * S, ui.remaining().y, ui.width() - 12 * S, 1e9}, 0);
            if (radio(ui, s[1] + " \xE2\x80\x94 " + s[2], k.kind == s[0], true, ui.width()) && k.kind != s[0]) look("gui_skin", s[0]);
            Rect used = ui.end();
            ui.place(0, used.h);
            ui.pop_id();
        }
        ui.space(8 * S);
        label(ui, "Light or dark (every look has both):");
        ui.flow();
        ui.place(6 * S, 1);
        for (const auto &t : THEMES)
            if (radio(ui, t.second, app.theme() == t.first) && app.theme() != t.first) look("gui_theme", t.first);
        ui.flow(false);
        ui.space(8 * S);
        ui.flow();
        label_mid(ui, "Text size:");
        std::vector<std::pair<std::string, std::string>> topts;
        for (int n : text_sizes()) topts.push_back({std::to_string(n), n == 13 ? std::to_string(n) + " (normal)" : std::to_string(n)});
        std::string size = std::to_string(app.text_px());
        if (dropdown(ui, "text", topts, size, 10)) look("gui_text", std::stoi(size));
        ui.place(8 * S, 1);
        label_mid(ui, "Size of everything:");
        int auto_s = std::max(1, std::min(3, (int)std::lround(app.win.scale())));
        std::vector<std::pair<std::string, std::string>> scopts = {
            {"0", "Automatic (" + std::to_string(auto_s) + "\xC3\x97)"}, {"1", "1\xC3\x97 (normal)"}, {"2", "2\xC3\x97 (sharp screens)"}, {"3", "3\xC3\x97"}};
        std::string sc = std::to_string(st().get("gui_scale").as_int(0));
        if (dropdown(ui, "scale", scopts, sc, 18)) look("gui_scale", std::stoi(sc));
        ui.flow(false);
        ui.space(8 * S);
        bool boot = st().has("boot") ? st().get("boot").truthy() : true;
        if (check(ui, "Show the welcome screen (retro looks)", boot)) set("boot", boot);
        ui.space(8 * S);
        if (button(ui, "Run Setup Again\xE2\x80\xA6", true, false, true)) app.later([this] { run_setup(app); });
        end_group(ui);

        ui.space(10 * S);
        begin_group(ui, "Your Data");
        para(ui, "Everything is saved to\n" + app.store.path + "\nThe terminal version (chalkboard) uses the same file.");
        ui.space(8 * S);
        para(ui, "A backup is one file with all your lessons, assessments, standards, and settings. Import it on another computer, or here to get your "
                 "work back.");
        ui.space(8 * S);
        ui.flow();
        if (button(ui, "Back Up Now\xE2\x80\xA6", true, false, true)) backup_now(app);
        if (button(ui, "Import Backup\xE2\x80\xA6", true, false, true)) import_backup(app);
        ui.flow(false);
        ui.space(6 * S);
        para(ui, last_backup_text(app), true);
        end_group(ui);
    }
};

}  // namespace

Factory settings_view() { return make<SettingsView>(); }

void backup_now(App &app) {
    std::string folder = app.win.choose_folder("Save a backup in which folder?");
    if (folder.empty() || !app.save()) return;
    std::string path;
    try {
        path = app.store.backup(folder);
    } catch (store::UserError &e) {
        alert(app.ui, "Backup Didn't Work", "Chalkboard couldn't save the backup:\n" + e.message, "warn");
        return;
    }
    app.sync_mtime();
    alert(app.ui, "Backed Up", "Everything is saved in\n" + path + "\n\nKeep it somewhere safe, like a flash drive or cloud folder.");
}

void import_backup(App &app) {
    std::string path = app.win.open_file("Import a backup", {{"Chalkboard backups", {"*.json", "*.JSON"}}, {"All files", {"*"}}});
    if (path.empty()) return;
    std::string how = choose(app.ui, "Import Backup", "How should " + files::basename(path) + " come in?",
                             {{"add", "Add what I don't have (keeps all my work)"}, {"replace", "Replace everything with the backup"}}, "add");
    if (how.empty()) return;
    if (how == "replace" &&
        !confirm(app.ui, "Replace Everything?",
                 "Your lessons, assessments, standards, and settings will be swapped for the backup's. (Chalkboard saves a copy of them first.)", "Replace",
                 "Cancel", "warn"))
        return;
    if (!app.save()) return;
    Value got;
    try {
        got = app.store.import_backup(path, how == "replace");
    } catch (store::UserError &e) {
        std::string m = e.message;
        alert(app.ui, "Import Didn't Work", m.empty() ? m : text::upper(m.substr(0, 1)) + text::lower(m).substr(1), "warn");
        return;
    }
    app.sync_mtime();
    if (app.view()) app.view()->reloaded();
    auto n = [&](const std::string &key, const std::string &word, const std::string &many = "") {
        long long c = got.get(key).as_int();
        return std::to_string(c) + " " + (c == 1 ? word : (many.empty() ? word + "s" : many));
    };
    std::string msg;
    if (how == "replace")
        msg = "Chalkboard now has the backup's " + n("lessons", "lesson") + " and " + n("assessments", "assessment") + ".";
    else {
        msg = "Added " + n("lessons", "lesson") + ", " + n("assessments", "assessment") + ", " + n("subjects", "standards subject") + ", and " +
              n("standards", "custom standard") + ".";
        if (got.get("updated").as_int()) msg += " " + n("updated", "lesson or assessment", "lessons or assessments") + " got the backup's newer copy.";
    }
    alert(app.ui, "Backup Imported", msg + "\n\nWhat you had before is saved in\n" + store::text_of(got.get("safety")));
}

}  // namespace gui
