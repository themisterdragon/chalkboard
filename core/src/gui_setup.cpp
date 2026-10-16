// The setup wizard (port of gui/setup.py): name, school, class, school colors, materials, look,
// window or terminal, and installing the app.
#include <algorithm>
#include <cmath>
#include <map>

#include "art.h"
#include "files.h"
#include "gui.h"
#include "install.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

const std::vector<std::pair<std::string, std::string>> PREVIEW = {
    {"primary_color", "#1F5A3A"}, {"secondary_color", "#F2E6A0"}, {"text_color", "#FFFFFF"}};
const std::vector<std::pair<std::string, std::string>> THEMES = {{"system", "Match my computer"}, {"light", "Light"}, {"dark", "Dark"}};

std::string lookup_preview(const std::string &key) {
    for (const auto &p : PREVIEW)
        if (p.first == key) return p.second;
    return "#000000";
}

}  // namespace

// A school color: a palette to click, or a HEX code. "" when cancelled.
std::string pick_color(App &app, const std::string &current) {
    static const char *SWATCHES[] = {
        "#7A0019", "#9E1B32", "#C8102E", "#E31837", "#F47321", "#FF8200", "#FFC72C", "#FFD100", "#F2E6A0", "#FFFFFF",
        "#00543C", "#1F5A3A", "#006747", "#00843D", "#4C9A2A", "#78BE20", "#003087", "#002D72", "#0033A0", "#0057B8",
        "#4B9CD3", "#00A3E0", "#330072", "#4F2683", "#582C83", "#782F40", "#5C2D0E", "#8B5B29", "#B3A369", "#C4B581",
        "#000000", "#1B1B1F", "#3A3A3A", "#53565A", "#75787B", "#A2AAAD", "#C8C9C7", "#D9D9D6", "#E8E8E8", "#F5F5F5"};
    std::string hexv = store::parse_hex(current);
    if (hexv.empty()) hexv = "#1F5A3A";
    std::string typed = hexv;
    bool first = true;
    int r = dialog(
        app.ui, {"School Color", 0, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            label(u, "Click a color, or type its HEX code:");
            u.space(6 * S);
            double sw = 30 * S;
            for (int row = 0; row < 4; row++) {
                Rect line = u.place(10 * (sw + 4 * S), sw);
                for (int c = 0; c < 10; c++) {
                    const char *code = SWATCHES[row * 10 + c];
                    Rect b{line.x + c * (sw + 4 * S), line.y, sw, sw};
                    Id id = u.id(code);
                    if (u.hit(id, b, false)) typed = hexv = code;
                    if (u.drawing) {
                        bool on = hexv == code;
                        u.fill(b, on ? k["sel"] : k["dark"]);
                        u.fill(b.in(on ? 3 * S : S), hex(code));
                    }
                }
                u.space(4 * S);
            }
            u.space(6 * S);
            u.flow();
            label_mid(u, "HEX:");
            if (first) {
                focus_at_end(u, u.id("entry:hex"));
                first = false;
            }
            if (entry(u, "hex", typed, 10 * k.f->width("0") + 20 * S) && !store::parse_hex(typed).empty()) hexv = store::parse_hex(typed);
            Rect b = u.place(60 * S, u.ctl_h());
            if (u.drawing) {
                u.fill(b, k["dark"]);
                u.fill(b.in(S), hex(hexv));
            }
            u.flow(false);
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    return r == 1 ? hexv : "";
}

bool setup_needed(App &app) {
    // a brand-new copy: setup never ran and there's no sign of earlier use
    Value &st = app.settings();
    if (st.get("setup_done").truthy()) return false;
    if (files::exists(app.store.path)) {
        st["setup_done"] = true;   // an upgrade: this computer already has Chalkboard data, so leave it be
        return false;
    }
    return true;
}

void run_setup(App &app) {
    Value &st = app.settings();
    std::map<std::string, std::string> v;
    for (const char *k : {"teacher", "school", "course", "subject", "grades", "primary_color", "secondary_color", "text_color"}) v[k] = store::text_of(st.get(k));
    if (v["grades"].empty()) v["grades"] = "9-10";
    std::string look = st.s("gui_skin").empty() ? "bevel" : st.s("gui_skin"), theme = app.theme(), mode = st.s("gui_mode").empty() ? "window" : st.s("gui_mode");
    std::string materials = st.s("default_materials");
    if (materials.empty()) materials = "- Notebook\n- Pencil or pen\n- Chromebook";
    install::Plan plan = install::plan();
    bool do_install = !plan.kind.empty(), desktop = true;
    std::vector<std::string> pages = {"welcome", "you", "class", "colors", "materials", "look", "mode"};
    if (!plan.kind.empty()) pages.push_back("install");
    pages.push_back("done");
    std::vector<std::string> names = {"Welcome", "About you", "Your class", "School colors", "Materials", "Look", "Window or terminal"};
    if (!plan.kind.empty()) names.push_back("Install");
    names.push_back("Done");
    size_t i = 0;
    bool focus_page = true;
    std::string installed;

    auto collect = [&] {   // what's on the pages into the settings (not saved to disk yet)
        for (const char *k : {"teacher", "school", "course", "grades", "subject"}) {
            std::string s = text::strip(v[k]);
            if (!s.empty() || std::string(k) == "teacher" || std::string(k) == "school") st[k] = s;
        }
        for (const auto &p : PREVIEW) {
            std::string raw = text::strip(v[p.first]);
            std::string code = raw.empty() ? "" : store::parse_hex(raw);
            if (raw.empty() || !code.empty()) st[p.first] = code;
        }
        bool any = false;
        for (const auto &p : PREVIEW) any |= !st.s(p.first).empty();
        if (any) st["board_style"] = "school";
        st["default_materials"] = text::strip(materials);
    };

    while (true) {
        int r = dialog(
            app.ui, {std::string(art::APP) + " Setup", 800, 0},
            [&](Ui &u) {
                Skin &k = u.sk();
                int S = u.S();
                Rect all{u.remaining().x, u.remaining().y, u.width(), 420.0 * S};
                u.place(0, all.h);
                // the art panel, with the steps
                double bar_top = all.b() - u.ctl_h() - 6 * S;
                Rect art{all.x, all.y, 190.0 * S, bar_top - 10 * S - 8 * S - all.y};
                if (u.drawing) {
                    u.fill(art, k["sel"]);
                    u.pixel_art(*icon("logo"), icon_color, std::round(art.x + (art.w - 96 * S) / 2), art.y + 34 * S, 6 * S);
                    u.text_in(*k.fbig, {art.x, art.y + 34 * S + 96 * S + 10 * S, art.w, k.fbig->line}, art::APP, k["seltext"], CENTER);
                    double y = art.y + 34 * S + 96 * S + 10 * S + k.fbig->line + 24 * S;
                    for (size_t j = 0; j < names.size(); j++) {
                        u.text(*k.fsmall, art.x + 14 * S, y + k.fsmall->ascent, (j == i ? "\xE2\x96\xBA " : "    ") + names[j], k["seltext"]);
                        y += k.fsmall->line;
                    }
                }
                // the page
                Rect page{art.r() + 18 * S, all.y, all.r() - art.r() - 18 * S, all.h - u.ctl_h() - 24 * S};
                u.begin(page, 0);
                auto head = [&](const std::string &title, const std::string &t = "") {
                    u.space(8 * S);
                    Rect h = u.place(0, k.fbig->line);
                    if (u.drawing) u.text_in(*k.fbig, h, title, k["text"]);
                    u.space(6 * S);
                    if (!t.empty()) {
                        para(u, t, false, 520 * S);
                        u.space(12 * S);
                    }
                };
                auto field = [&](const std::string &lab, const std::string &key, const std::string &hint = "", int width = 34) {
                    u.space(6 * S);
                    label(u, lab, true);
                    u.space(2 * S);
                    if (focus_page) {
                        focus_at_end(u, u.id("entry:" + key));
                        focus_page = false;
                    }
                    u.row({std::min(u.width(), width * k.f->width("0") + 20 * S)});
                    entry(u, key, v[key]);
                    u.row({});
                    if (!hint.empty()) {
                        u.space(2 * S);
                        label(u, hint, false, true, true);
                    }
                };
                const std::string &pg = pages[i];
                if (pg == "welcome") {
                    head(std::string("Welcome to ") + art::APP,
                         "This gets Chalkboard ready for your classroom. It takes about a minute, and you can change any of it later in Settings.");
                    label(u, "Click Next to begin.");
                } else if (pg == "you") {
                    head("About You", "Your name and school go in the footer of handouts and make-up sheets.");
                    field("Your name, the way students see it", "teacher", "Like Mrs. Rivera or Mr. Thompson");
                    field("School", "school");
                } else if (pg == "class") {
                    head("Your Class", "New lessons and quizzes start with these. You can still plan for any class.");
                    field("The class you teach most", "course", "Like English 10 or Biology", 26);
                    u.space(10 * S);
                    label(u, "Grades", true);
                    u.space(2 * S);
                    std::vector<std::pair<std::string, std::string>> gopts;
                    for (const auto &g : store::GRADE_CHOICES) gopts.push_back({g, grade_label(g)});
                    u.row({16 * k.f->width("0") + 40 * S});
                    dropdown(u, "grades", gopts, v["grades"], 16);
                    u.row({});
                    const auto &subjects = app.store.subjects;
                    if (!subjects.empty()) {
                        if (std::find(subjects.begin(), subjects.end(), v["subject"]) == subjects.end()) v["subject"] = subjects[0];
                        u.space(10 * S);
                        label(u, "Subject (for picking standards)", true);
                        u.space(2 * S);
                        std::vector<std::pair<std::string, std::string>> sopts;
                        for (const auto &s : subjects) sopts.push_back({s, s});
                        u.row({std::min(u.width(), 28 * k.f->width("0") + 40 * S)});
                        dropdown(u, "subject", sopts, v["subject"], 28);
                        u.row({});
                    }
                } else if (pg == "colors") {
                    head("School Colors", "Board slides can use your school's colors. Type HEX codes (like #7A0019), click Pick\xE2\x80\xA6 to choose, or leave "
                                          "them blank for a green chalkboard.");
                    double lw = k.f->width("Background:") + 8 * S;
                    std::vector<std::pair<std::string, std::string>> rows = {{"primary_color", "Background"}, {"secondary_color", "Headings"}, {"text_color", "Text"}};
                    for (const auto &p : rows) {
                        u.push_id(p.first);
                        u.row({lw, 10 * k.f->width("0") + 20 * S, 0}, u.ctl_h());
                        label_mid(u, p.second + ":");
                        if (focus_page) {
                            focus_at_end(u, u.id("entry:hex"));
                            focus_page = false;
                        }
                        entry(u, "hex", v[p.first]);
                        Rect b = u.place(u.btn_w("Pick\xE2\x80\xA6", true), u.ctl_h());
                        if (button_at(u, u.id("btn:pick"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Pick\xE2\x80\xA6", true, false, true)) {
                            std::string got = pick_color(app, v[p.first].empty() ? lookup_preview(p.first) : v[p.first]);
                            if (!got.empty()) v[p.first] = got;
                        }
                        u.pop_id();
                    }
                    u.row({});
                    u.space(12 * S);
                    Rect pv = u.place(320 * S, 150 * S);
                    if (u.drawing) {
                        auto col = [&](const std::string &key) {
                            std::string h = store::parse_hex(v[key]);
                            return hex(h.empty() ? lookup_preview(key) : h);
                        };
                        u.fill(pv, k["dark"]);
                        Rect in = pv.in(S);
                        u.fill(in, col("primary_color"));
                        u.text(*k.fb, in.x + 14 * S, in.y + 14 * S + k.fb->ascent, "I Can\xE2\x80\xA6", col("secondary_color"));
                        u.text(*k.f, in.x + 14 * S, in.y + 40 * S + k.f->ascent, "- Cite strong evidence from the text", col("text_color"));
                        u.text(*k.f, in.x + 14 * S, in.y + 40 * S + k.f->line + k.f->ascent, "- Explain what it shows", col("text_color"));
                        u.text(*k.fb, in.x + 14 * S, in.y + 92 * S + k.fb->ascent, "Bell Ringer", col("secondary_color"));
                        u.text(*k.f, in.x + 14 * S, in.y + 116 * S + k.f->ascent, "What's one thing that made you smile this week?", col("text_color"));
                    }
                } else if (pg == "materials") {
                    head("Materials", "What do students need every day? Every new lesson starts with this list. Put each item on its own line.");
                    if (focus_page) {
                        u.focus(u.id("text:materials"));
                        focus_page = false;
                    }
                    text_area(u, "materials", materials, 8, 8, false);
                } else if (pg == "look") {
                    head("Look", "Pick the look you like. You can switch any time from the View menu.");
                    for (const auto &s : skins())
                        if (radio(u, s[1] + ": " + s[2], look == s[0])) look = s[0];
                    u.space(12 * S);
                    label(u, "Light or dark:", true);
                    u.space(2 * S);
                    for (const auto &t : THEMES)
                        if (radio(u, t.second, theme == t.first)) theme = t.first;
                } else if (pg == "mode") {
#ifdef __APPLE__
                    const char *mod = "Cmd";
#else
                    const char *mod = "Ctrl";
#endif
                    head("Window or Terminal", std::string("Chalkboard works two ways, on the same lessons and assessments. You can switch any time from the View "
                                                           "menu or with ") + mod + "+Shift+W.");
                    for (auto m : std::vector<std::vector<std::string>>{
                             {"window", "Window view (recommended)", "Buttons, menus, and forms you click through."},
                             {"terminal", "Terminal view",
                              "A retro green screen you run from the keyboard: press a number or letter for each choice. Pick a school mascot in its Settings."}}) {
                        u.space(8 * S);
                        if (radio(u, m[1], mode == m[0])) mode = m[0];
                        u.begin({u.remaining().x + 24 * S, u.remaining().y, u.width() - 24 * S, 1e9}, 0);
                        para(u, m[2], true, 480 * S);
                        Rect used = u.end();
                        u.place(0, used.h);
                    }
                } else if (pg == "install") {
                    head("Install", "Put Chalkboard on this computer so it's easy to open next time?");
                    check(u, "Install Chalkboard on this computer", do_install);
                    u.begin({u.remaining().x + 24 * S, u.remaining().y + 2 * S, u.width() - 24 * S, 1e9}, 0);
                    para(u, plan.text, true, 500 * S);
                    Rect used = u.end();
                    u.place(0, used.h + 10 * S);
                    if (plan.kind == "windows") check(u, "Also put a shortcut on the desktop", desktop);
                } else {
                    std::string name = st.s("teacher").empty() ? "you" : st.s("teacher");
                    head("All Set!", "Chalkboard is ready for " + name + ". Click Finish to start planning.");
                    for (const char *t : {"Lesson Plans: plan a lesson, then export it or make a board slide.", "Assessments: build quizzes, tests, and worksheets.",
                                          "Back and Home are at the top of every window."}) {
                        para(u, std::string("\xE2\x80\xA2  ") + t, false, 500 * S);
                        u.space(2 * S);
                    }
                }
                u.end();
                // the buttons
                Rect bar{all.x, all.b() - u.ctl_h() - 6 * S, all.w, u.ctl_h() + 6 * S};
                if (u.drawing) u.fill({all.x, bar.y - 10 * S, all.w, (double)S}, k["dark"]);
                bool last = i + 1 == pages.size();
                if (focus_page) {   // a page with no box to type in: the keyboard starts on Next
                    u.focus(u.id("btn:next"));
                    focus_page = false;
                }
                u.begin(bar, 0);
                u.flow();
                if (button(u, "Skip Setup", !last, false, false, 90)) return 2;
                u.end();
                double nw = std::max(90.0 * S, u.btn_w(last ? "Finish" : "Next \xE2\x96\xBA") + 6 * S), bw = std::max(90.0 * S, u.btn_w("\xE2\x97\x84 Back"));
                double bh = u.ctl_h() + (k.modern || !k.bevel ? 6 * S : 0);
                if (button_at(u, u.id("btn:next"), {bar.r() - nw, bar.y, nw, bh}, last ? "Finish" : "Next \xE2\x96\xBA", true, true)) return 3;
                if (button_at(u, u.id("btn:back"), {bar.r() - nw - 8 * S - bw, bar.y, bw, bh}, "\xE2\x97\x84 Back", i > 0)) return 4;
                if (u.key(plat::K_RETURN)) return 3;   // Return: Next
                return 0;
            },
            {}, 0, 2);
        if (app.ui.quitting()) {   // the window is closing: keep what was typed
            collect();
            break;
        }
        if (r == 2) {   // Skip (or Esc, or the close box)
            if (i + 1 == pages.size()) break;
            if (confirm(app.ui, "Skip Setup",
                        "Skip the rest of setup? What you've typed so far is kept, and you can run setup again any time from Settings.", "Skip",
                        "Keep Going")) {
                collect();
                break;
            }
            continue;
        }
        collect();
        if (r == 4) {
            if (i > 0) i--;
            focus_page = true;
            continue;
        }
        if (r == 3) {
            if (i + 1 == pages.size()) break;
            if (pages[i] == "install" && do_install) {
                try {
                    installed = install::install(plan, desktop);
                } catch (install::Error &e) {
                    alert(app.ui, "Couldn't Install", "Chalkboard couldn't be installed:\n" + e.message + "\n\nYou can still use it from where it is now.", "warn");
                }
            }
            i++;
            focus_page = true;
        }
    }
    // save, and use what was picked
    st["setup_done"] = true;
    bool terminal = mode == "terminal";
    bool changed_look = look != app.ui.sk().kind || theme != app.theme();
    app.save();
    if (!installed.empty() && install::relaunch(plan)) {
        app.quit();   // the Mac copy in Applications takes over
        return;
    }
    if (changed_look) {
        st["gui_skin"] = look;
        st["gui_theme"] = theme;
        app.set_look();
    }
    if (!installed.empty()) app.status(installed);
    if (terminal) app.ui.after(100, [&app] { app.enter_terminal(true); });
}

}  // namespace gui
