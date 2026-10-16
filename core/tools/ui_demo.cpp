// The window toolkit on its own: every control, in every look (dev tool).
//   build/ui-demo [bevel|pinstripe|modern] [light|dark]
//   build/ui-demo KIND MODE OUTDIR SCRIPT   (no window: scripted clicks and keys, screenshots to OUTDIR)
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "files.h"
#include "ui.h"
using namespace ui;

// a scripted window: "click x y", "dbl x y", "rclick x y", "key NAME [shift|ctrl]", "type TEXT", "wheel x y dy", "shot FILE"
struct Script {
    std::vector<std::string> steps;
    size_t at = 0;
    raster::Image last;
    std::string dir;
    bool next(plat::Event &e) {
        while (at < steps.size()) {
            std::istringstream in(steps[at++]);
            std::string op;
            in >> op;
            e = plat::Event();
            if (op == "shot") {
                std::string name;
                in >> name;
                files::write(dir + "/" + name + ".png", last.png());
                continue;
            }
            if (op == "click" || op == "dbl" || op == "rclick" || op == "up" || op == "down" || op == "move") {
                in >> e.x >> e.y;
                // a click: press now, release next time
                if (op == "move") { e.type = plat::Event::MOVE; return true; }
                e.type = op == "up" ? plat::Event::UP : plat::Event::DOWN;
                e.button = op == "rclick" ? 3 : 1;
                e.clicks = op == "dbl" ? 2 : 1;
                if (op == "click" || op == "rclick" || op == "dbl") steps.insert(steps.begin() + (long)at, "up " + std::to_string(e.x) + " " + std::to_string(e.y) + (op == "rclick" ? " r" : ""));
                if (op == "up") { std::string r; in >> r; if (r == "r") e.button = 3; }
                return true;
            }
            if (op == "wheel") { e.type = plat::Event::WHEEL; in >> e.x >> e.y >> e.dy; return true; }
            if (op == "type") { e.type = plat::Event::TEXT; std::getline(in, e.text); if (!e.text.empty() && e.text[0] == ' ') e.text = e.text.substr(1); return true; }
            if (op == "key") {
                std::string name, mod;
                in >> name >> mod;
                e.type = plat::Event::KEY;
                static const std::vector<std::pair<std::string, int>> K = {{"left", plat::K_LEFT}, {"right", plat::K_RIGHT}, {"up", plat::K_UP}, {"down", plat::K_DOWN}, {"home", plat::K_HOME}, {"end", plat::K_END}, {"pgup", plat::K_PGUP}, {"pgdn", plat::K_PGDN}, {"return", plat::K_RETURN}, {"escape", plat::K_ESCAPE}, {"tab", plat::K_TAB}, {"backspace", plat::K_BACKSPACE}, {"delete", plat::K_DELETE}, {"space", ' '}};
                e.key = name.size() == 1 ? name[0] : 0;
                for (auto &k : K) if (k.first == name) e.key = k.second;
                if (mod.find("shift") != std::string::npos) e.mods |= plat::SHIFT;
                if (mod.find("ctrl") != std::string::npos) e.mods |= plat::CTRL;
                return true;
            }
        }
        return false;
    }
};
int main(int argc, char **argv) {
    std::string kind = argc > 1 ? argv[1] : "bevel";
    bool dark = argc > 2 && std::string(argv[2]) == "dark";
    Script script;
    std::unique_ptr<plat::Window> win;
    if (argc > 4) {   // demo KIND MODE OUTDIR SCRIPTFILE
        script.dir = argv[3];
        std::ifstream f(argv[4]);
        for (std::string l; std::getline(f, l);) if (!l.empty() && l[0] != '#') script.steps.push_back(l);
        win = plat::headless(1000, 700, 1, [&](plat::Event &e) { return script.next(e); }, [&](const raster::Image &img) { script.last = img; });
    } else
        win = plat::Window::open("Toolkit demo", 1000, 700);
    if (!win) { std::fprintf(stderr, "no window\n"); return 1; }
    Ui ui(*win);
    ui.skin = make_skin(kind, (int)win->scale(), 13, dark, plat::system_accent());
    std::string name = "Hamlet, Act 3", notes = "Some **bold** and *italic* and __underlined__ text that wraps around when it gets long enough to need another line.\nSecond paragraph.";
    bool chk = true; int rad = 1; std::string dd = "b";
    int sel = 2;
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 30; i++) rows.push_back({"Lesson " + std::to_string(i), "Unit " + std::to_string(i % 4), "English 4", "Oct " + std::to_string(i + 1)});
    std::string status = "Ready.";
    ui.on_key = [&](const plat::Event &e) {
        if (e.key == 'q' && e.mods == plat::PRIMARY) { ui.quit(); return true; }
        return false;
    };
    ui.run([&] {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.fill({0, 0, (double)ui.W(), (double)ui.H()}, k["window"]);
        ui.begin({0, 0, (double)ui.W(), (double)ui.H()}, 0);
        begin_toolbar(ui);
        if (tool(ui, "Alert")) alert(ui, "Chalkboard", "This is a message that wraps onto a second line because it's long enough to need one.");
        if (tool(ui, "Confirm")) status = confirm(ui, "Delete Lesson", "Delete the lesson “Hamlet”?\n\nIts linked quizzes are kept.", "Delete", "Cancel", "warn") ? "Deleted" : "Kept";
        if (tool(ui, "Ask")) { std::string v = name; if (ask(ui, "Rename", "What's the lesson called?", v)) name = v; }
        if (tool(ui, "Choose")) { auto c = choose(ui, "Pick", "Pick a look:", {{"bevel", "Bevel"}, {"pinstripe", "Pinstripe"}, {"modern", "Modern"}}, k.kind); if (!c.empty()) { ui.skin = make_skin(c, S, 13, dark, plat::system_accent()); } }
        tool_gap(ui);
        if (tool(ui, dark ? "Light" : "Dark")) { dark = !dark; ui.skin = make_skin(k.kind, S, 13, dark, plat::system_accent()); }
        tool(ui, "Off", false);
        format_bar(ui);
        end_toolbar(ui);
        ui.fill(ui.cut_top(S), k["dark"]);
        Rect st = ui.cut_bottom(k.line + 8 * S);
        ui.fill(st, k["face"]);
        ui.text_in(*k.f, {st.x + 8 * S, st.y, st.w, st.h}, status, k["text"]);
        Rect rest = ui.remaining();
        ui.end();
        // left: a list; right: a form
        Rect left{rest.x + 12 * S, rest.y + 10 * S, rest.w * 0.45, rest.h - 20 * S};
        ListResult lr = list_view(ui, "lessons", left, {{"Title", 160, true}, {"Unit", 70, false}, {"Course", 90, false}, {"Date", 60, false}}, rows, sel, "No lessons yet.");
        if (lr.open) status = "Open " + rows[sel][0];
        if (lr.menu) { int m = popup_menu(ui, lr.menu_x, lr.menu_y, {{"Open"}, {"Preview"}, {""}, {"Delete", false}}); status = "menu " + std::to_string(m); }
        Rect right{left.r() + 14 * S, rest.y, rest.r() - left.r() - 14 * S, rest.h};
        ui.begin_scroll(ui.id("form"), right);
        ui.space(10 * S);
        begin_group(ui, "Lesson");
        ui.row({70.0 * S, -1}, ui.ctl_h());
        label(ui, "Title:");
        if (entry(ui, "title", name)) status = "Title: " + name;
        ui.row({70.0 * S, 0});
        label(ui, "Look:");
        if (dropdown(ui, "look", {{"a", "Bevel"}, {"b", "Pinstripe"}, {"c", "Modern (your computer's style)"}}, dd)) status = "picked " + dd;
        ui.row({});
        check(ui, "Show the board slide", chk);
        if (radio(ui, "One column", rad == 0)) rad = 0;
        if (radio(ui, "Two columns", rad == 1)) rad = 1;
        end_group(ui);
        ui.space(10 * S);
        begin_group(ui, "Notes");
        text_area(ui, "notes", notes, 3, 8);
        para(ui, "Changes save by themselves. This paragraph wraps to the width of the group box it's in.", true);
        end_group(ui);
        ui.space(10 * S);
        if (button(ui, "Save", true, true)) status = "Saved";
        Rect pr = ui.place(160 * S, 12 * S);
        progress(ui, pr, 0.4);
        ui.space(200 * S);
        ui.end_scroll();
    });
    return 0;
}
