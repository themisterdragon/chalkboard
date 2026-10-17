// The window's shell: the sidebar that's always there (Today, Lesson Plans, Assessments, Calendar,
// Standards, then My Exports, Settings, and the terminal), the header over each page, the line
// icons, and the two pages that live here: Today (the home page: this week at a glance, what you
// worked on lately) and Calendar (a month of lessons).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "art.h"
#include "dates.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

// ----------------------------------------------------------------------------- line icons

// Simple outline icons drawn on a 20 x 20 grid, so they're sharp at any size.
void line_icon(Ui &ui, const std::string &name, const Rect &box, Color c) {
    if (!ui.drawing) return;
    double s = box.w / 20.0, w = std::max(1.5, 1.6 * s);
    auto P = [&](double x, double y) { return raster::Pt{box.x + x * s, box.y + y * s}; };
    auto poly = [&](std::vector<raster::Pt> pts, bool closed) { ui.polyline(pts, c, w, closed); };
    auto rr = [&](double x, double y, double ww, double hh, double r) { ui.rrect({box.x + x * s, box.y + y * s, ww * s, hh * s}, r * s, {}, c, w, false, true); };
    auto ln = [&](double x0, double y0, double x1, double y1) { ui.line(box.x + x0 * s, box.y + y0 * s, box.x + x1 * s, box.y + y1 * s, c, w); };
    auto dot = [&](double x, double y, double r) { ui.oval({box.x + (x - r) * s, box.y + (y - r) * s, 2 * r * s, 2 * r * s}, c, {}, 0, true, false); };
    if (name == "today") {
        poly({P(2.5, 10), P(10, 3.2), P(17.5, 10)}, false);
        poly({P(4.8, 8.2), P(4.8, 17), P(15.2, 17), P(15.2, 8.2)}, false);
        poly({P(8.4, 17), P(8.4, 12.2), P(11.6, 12.2), P(11.6, 17)}, false);
    } else if (name == "lessons") {
        poly({P(10, 5.5), P(6.5, 4), P(2.5, 4.2), P(2.5, 15.5), P(6.5, 15.3), P(10, 16.8), P(13.5, 15.3), P(17.5, 15.5), P(17.5, 4.2), P(13.5, 4),
              P(10, 5.5)}, true);
        ln(10, 5.5, 10, 16.5);
    } else if (name == "assessments") {
        rr(4, 3.8, 12, 14, 2);
        rr(7.3, 2.2, 5.4, 3.2, 1.2);
        poly({P(7, 11), P(9.2, 13.2), P(13.2, 8.6)}, false);
    } else if (name == "calendar") {
        rr(2.8, 4.4, 14.4, 12.8, 2.2);
        ln(2.8, 8.4, 17.2, 8.4);
        ln(6.8, 2.6, 6.8, 6);
        ln(13.2, 2.6, 13.2, 6);
        for (double x : {7.0, 10.0, 13.0}) dot(x, 12.4, 1.0);
    } else if (name == "standards") {
        rr(2.8, 4, 3.6, 13, 0.8);
        rr(7.6, 4, 3.6, 13, 0.8);
        poly({P(12.6, 5.3), P(15.8, 4.5), P(18.6, 15.9), P(15.4, 16.7)}, true);
    } else if (name == "folder") {
        poly({P(2.5, 5), P(7.8, 5), P(9.4, 7), P(17.5, 7), P(17.5, 16), P(2.5, 16)}, true);
    } else if (name == "settings") {
        ui.oval({box.x + 7.2 * s, box.y + 7.2 * s, 5.6 * s, 5.6 * s}, {}, c, w, false, true);
        ui.oval({box.x + 4.2 * s, box.y + 4.2 * s, 11.6 * s, 11.6 * s}, {}, c, w, false, true);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4;
            ln(10 + std::cos(a) * 5.8, 10 + std::sin(a) * 5.8, 10 + std::cos(a) * 8.2, 10 + std::sin(a) * 8.2);
        }
    } else if (name == "terminal") {
        rr(2.5, 4, 15, 12, 2);
        poly({P(6, 8), P(8.6, 10), P(6, 12)}, false);
        ln(10.4, 12.2, 14, 12.2);
    } else if (name == "plus") {
        ln(10, 4, 10, 16);
        ln(4, 10, 16, 10);
    }
}

// ----------------------------------------------------------------------------- the sidebar

namespace {

struct NavItem {
    const char *key, *label, *icon;
};
// key "!x" is an action, not a page
const NavItem NAV_TOP[] = {{"", "Today", "today"},
                           {"lessons", "Lesson Plans", "lessons"},
                           {"assessments", "Assessments", "assessments"},
                           {"calendar", "Calendar", "calendar"},
                           {"standards", "Standards", "standards"}};
const NavItem NAV_BOTTOM[] = {{"!exports", "My Exports", "folder"}, {"settings", "Settings", "settings"}, {"!terminal", "Terminal", "terminal"}};

// the retro looks use their pixel-art icons where there's one
const char *pixel_icon(const char *icon) {
    for (const char *k : {"lessons", "assessments", "standards", "settings", "folder"})
        if (std::string(icon) == k) return k;
    return nullptr;
}

}  // namespace

double App::sidebar_w(double body_w) {
    Skin &k = ui.sk();
    int S = k.S;
    bool narrow = body_w < 940 * S;
    if (narrow) return 64 * S;
    return (k.soft ? 224 : 196) * S;
}

void App::sidebar(const Rect &r) {
    Skin &k = ui.sk();
    int S = k.S;
    bool narrow = r.w < 100 * S;
    if (ui.drawing) {
        if (k.glossy) ui.gradient(r, 0, k["side_top"], k["side_bot"]);
        else ui.fill(r, k["sidebar"]);
        if (k.soft) ui.fill({r.r() - S, r.y, (double)S, r.h}, k["line"]);
        else ui.fill({r.r() - S, r.y, (double)S, r.h}, k["dark"]);
        if (k.bevel) ui.fill({r.r() - 2 * S, r.y, (double)S, r.h}, k["light"]);
    }
    double pad = (narrow ? 10 : 14) * S;
    double y = r.y + 16 * S;
    // the name and the logo
    if (ui.drawing) {
        double lz = 2 * S;
        double lx = narrow ? r.x + (r.w - 16 * lz) / 2 : r.x + pad + 2 * S;
        ui.pixel_art(*icon("logo"), icon_color, std::round(lx), std::round(y), (int)lz);
        if (!narrow) ui.text_in(*k.fhead, {lx + 16 * lz + 10 * S, y, r.w, 16 * lz}, art::APP, k["sidebar_text"]);
    }
    y += 32 * S + 18 * S;
    // New Lesson
    double bh = ui.ctl_h() + (k.soft ? 6 * S : 4 * S);
    Rect nb{r.x + pad - 3 * S, y, r.w - 2 * pad + 6 * S, bh};
    std::string nl = narrow ? "+" : "+  New Lesson";
    if (button_at(ui, ui.id("nav:new"), nb, nl, true, true)) cmd_new_lesson();
    y += bh + 16 * S;
    // the pages
    double ih = ui.ctl_h() + 4 * S;
    auto item = [&](const NavItem &it, double iy) {
        Rect ir{r.x + pad - 4 * S, iy, r.w - 2 * pad + 8 * S, ih};
        std::string key = it.key;
        bool on = key[0] != '!' && key == section_ && view_ && !view_->bare();
        Id id = ui.id(std::string("nav:") + it.label);
        bool go = ui.hit(id, ir, true, true);
        bool focus = ui.focused(id);
        if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
        if (ui.over(ir)) ui.cursor(plat::HAND);
        if (ui.drawing) {
            Color fg = on ? k["navsel_text"] : k["sidebar_text"];
            if (on) {
                if (k.glossy) {
                    ui.rrect(ir, ih / 2, mix(k["navsel"], {0, 0, 0}, k.dark ? 0.3 : 0.25), {}, 0, true, false);
                    ui.gradient(ir.in(S), ih / 2 - S, mix(k["navsel"], {1, 1, 1}, k.dark ? 0.2 : 0.5), k["navsel"]);
                    ui.rrect_a({ir.x + 4 * S, ir.y + 2 * S, ir.w - 8 * S, ir.h * 0.45}, ih / 2 - 2 * S, {1, 1, 1}, k.dark ? 0.12 : 0.4);
                } else if (k.soft) {
                    if (!k.dark) ui.shadow(ir, 8 * S, 3 * S, 0.06);
                    ui.rrect(ir, 8 * S, k["navsel"], {}, 0, true, false);
                } else
                    ui.fill(ir, k["navsel"]);
            } else if (ui.over(ir) && k.soft)
                ui.rrect(ir, k.glossy ? ih / 2 : 8 * S, mix(k["sidebar"], k["sidebar_text"], 0.07), {}, 0, true, false);
            double is = 20 * S;
            double ix = narrow ? ir.x + (ir.w - is) / 2 : ir.x + 12 * S;
            Rect ib{ix, ir.y + (ih - is) / 2, is, is};
            const char *pix = k.soft ? nullptr : pixel_icon(it.icon);
            if (pix) ui.pixel_art(*icon(pix), icon_color, std::round(ix + 2 * S), std::round(ir.y + (ih - 16 * S) / 2), S);
            else line_icon(ui, it.icon, ib, fg);
            if (!narrow) {
                Font &f = on && k.soft ? *k.fb : *k.f;
                ui.text_in(f, {ix + is + 12 * S, ir.y, ir.r() - ix - is - 16 * S, ih}, ui.elide(f, it.label, ir.r() - ix - is - 16 * S), fg);
            }
            if (focus) {
                if (k.soft) ui.rrect(ir.in(-2 * S), 10 * S, {}, k["focus"], 2 * S, false, true);
                else ui.dotted(ir.in(2 * S), on ? k["navsel_text"] : k["focus"]);
            }
        }
        if (go) {
            if (key == "!exports") {
                open_export_folder();
                status("Opened " + store.export_dir());
            } else if (key == "!terminal")
                enter_terminal();
            else
                go_section(key);
        }
    };
    for (const auto &it : NAV_TOP) {
        item(it, y);
        y += ih + 2 * S;
    }
    // the bottom: the status line, and above it My Exports, Settings, Terminal
    Font &sf = *k.fsmall;
    double foot = narrow ? 8 * S : sf.line * 2 + 22 * S;
    double by = r.b() - foot - (ih + 2 * S) * 3;
    by = std::max(by, y + 12 * S);
    if (ui.drawing && k.soft) ui.fill({r.x + pad, by - 8 * S, r.w - 2 * pad, (double)S}, k["line"]);
    for (const auto &it : NAV_BOTTOM) {
        item(it, by);
        by += ih + 2 * S;
    }
    if (!narrow && ui.drawing && k.soft) {
        Rect fr{r.x + pad, r.b() - foot + 8 * S, r.w - 2 * pad, foot - 12 * S};
        if (progress_ >= 0) {
            ui::progress(ui, {fr.x, fr.y + 2 * S, fr.w, 8.0 * S}, progress_);
            fr.y += 14 * S;
        }
        auto lines = ui.wrap(sf, status_l_, fr.w);
        double ly = fr.y;
        for (size_t i = 0; i < lines.size() && i < 2; i++) {
            ui.text(sf, fr.x, std::round(ly + sf.ascent), ui.elide(sf, lines[i], fr.w), k["dim"]);
            ly += sf.line;
        }
    }
}

// The header over a page: a link back to where you were, and the page's name.
Rect App::page_header(const Rect &r) {
    Skin &k = ui.sk();
    int S = k.S;
    if (!k.soft) {
        // the retro looks: a Back button and the name in bold, on a rule
        double h = ui.ctl_h() + 12 * S;
        Rect bar{r.x, r.y, r.w, h};
        double x = r.x + 10 * S;
        if (!stack_.empty()) {
            double bw = ui.btn_w("\xE2\x97\x84 Back");
            if (button_at(ui, ui.id("hdr:back"), {x, r.y + 6 * S, bw, ui.ctl_h()}, "\xE2\x97\x84 Back")) back();
            x += bw + 12 * S;
        }
        if (ui.drawing) {
            ui.text_in(*k.fhead, {x, bar.y, bar.r() - x - 10 * S, h}, ui.elide(*k.fhead, current_title, bar.r() - x - 10 * S), k["text"]);
            ui.fill({r.x, bar.b() - S, r.w, (double)S}, k["dark"]);
        }
        return {r.x, bar.b(), r.w, r.h - h};
    }
    double padx = 28 * S, top = 18 * S;
    double y = r.y + top;
    if (!stack_.empty()) {
        std::string t = "\xE2\x80\xB9  " + trail_.back();
        Font &f = *k.f;
        Rect lr{r.x + padx - 6 * S, y - 3 * S, f.width(t) + 12 * S, f.line + 6 * S};
        Id id = ui.id("hdr:back");
        bool go = ui.hit(id, lr, true, true);
        if (ui.focused(id) && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
        if (ui.over(lr)) ui.cursor(plat::HAND);
        if (ui.drawing) {
            if (ui.over(lr)) ui.rrect(lr, 6 * S, mix(k["window"], k["link"], 0.1), {}, 0, true, false);
            ui.text(f, lr.x + 6 * S, std::round(y + f.ascent), t, k["link"]);
            if (ui.focused(id)) ui.rrect(lr.in(-2 * S), 8 * S, {}, k["focus"], 2 * S, false, true);
        }
        if (go) back();
        y += f.line + 6 * S;
    }
    Font &pf = *k.fpage;
    if (ui.drawing) ui.text(pf, r.x + padx, std::round(y + pf.ascent), ui.elide(pf, current_title, r.w - 2 * padx), k["text"]);
    y += pf.line + 6 * S;
    return {r.x + padx - 8 * S, y, r.w - 2 * padx + 16 * S, r.b() - y - 12 * S};
}

// ----------------------------------------------------------------------------- shared by Today and Calendar

namespace {

}  // namespace

// each class gets its own color (a stripe beside its lessons), the same everywhere
Color class_color(Skin &k, const std::string &course) {
    static const char *HUES[] = {"#2F8F5B", "#2C6FD1", "#C2410C", "#8B4CC9", "#B8860B", "#C2185B", "#0E8A8A", "#5B6B7A"};
    unsigned h = 0;
    for (unsigned char ch : course) h = h * 31 + ch;
    Color c = hex(HUES[course.empty() ? 7 : h % 8]);
    return fit(c, {k["card"], k["window"]}, 3.0);
}

namespace {

struct DayLesson {
    std::string id, title, course;
};

std::map<long, std::vector<DayLesson>> lessons_by_day(App &app, const Date &from, const Date &to) {
    std::map<long, std::vector<DayLesson>> out;
    long a = from.ordinal(), b = to.ordinal();
    for (const auto &l : app.store.data["lessons"].arr()) {
        if (!l.is_object()) continue;
        for (const auto &d : store::lesson_dates(l)) {
            long o = d.ordinal();
            if (o >= a && o <= b) out[o].push_back({l.s("id"), l.s("title").empty() ? "(untitled lesson)" : l.s("title"), l.s("course")});
        }
    }
    for (auto &kv : out)
        std::stable_sort(kv.second.begin(), kv.second.end(), [](const DayLesson &x, const DayLesson &y) {
            return x.course != y.course ? x.course < y.course : x.title < y.title;
        });
    return out;
}

const char *DAY_NAMES[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char *DAY_SHORT[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char *MONTH_NAMES[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// a lesson on the week or the month: a stripe in its class's color, its title, and its class
bool lesson_chip(Ui &ui, const std::string &key, const Rect &r, const DayLesson &l, bool show_course) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("chip:" + key);
    bool go = ui.hit(id, r, true, true);
    bool focus = ui.focused(id);
    if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
    if (ui.over(r)) ui.cursor(plat::HAND);
    if (!ui.drawing) return go;
    bool hot = ui.over(r);
    Color bg = k.soft ? (hot ? mix(k["window"], k["text"], 0.06) : k["window"]) : (hot ? k["selsoft"] : k["field"]);
    Color fg = !k.soft && hot ? (hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["text"]) : k["text"];
    if (k.soft) ui.rrect(r, 6 * S, bg, {}, 0, true, false);
    else {
        ui.fill(r, bg);
        ui.box(r, k["edge"], S);
    }
    Color stripe = class_color(k, l.course);
    if (k.soft) ui.rrect({r.x, r.y, 4.0 * S, r.h}, 2 * S, stripe, {}, 0, true, false);
    else ui.fill({r.x + S, r.y + S, 3.0 * S, r.h - 2 * S}, stripe);
    double tx = r.x + 10 * S, tw = r.w - 14 * S;
    ui.clip(r);
    if (show_course && !l.course.empty()) {
        double y = r.y + (r.h - k.f->line - k.fsmall->line) / 2;
        ui.text(*k.f, tx, std::round(y + k.f->ascent), ui.elide(*k.f, l.title, tw), fg);
        ui.text(*k.fsmall, tx, std::round(y + k.f->line + k.fsmall->ascent), ui.elide(*k.fsmall, l.course, tw), hot && !k.soft ? fg : k["dim"]);
    } else
        ui.text_in(*k.fsmall, {tx, r.y, tw, r.h}, ui.elide(*k.fsmall, l.title, tw), fg);
    ui.unclip();
    if (focus) {
        if (k.soft) ui.rrect(r.in(-2 * S), 8 * S, {}, k["focus"], 2 * S, false, true);
        else ui.dotted(r.in(2 * S), k["focus"]);
    }
    return go;
}

// a small text link ("+ Plan"), true when clicked
bool link(Ui &ui, const std::string &key, const Rect &r, const std::string &text, Font &f, Align a = LEFT) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("link:" + key);
    bool go = ui.hit(id, r, true, true);
    bool focus = ui.focused(id);
    if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
    bool hot = ui.over(r);
    if (hot) ui.cursor(plat::HAND);
    if (!ui.drawing) return go;
    if (hot && k.soft) ui.rrect(r, 6 * S, mix(k["card"], k["link"], 0.1), {}, 0, true, false);
    ui.text_in(f, r.in(6 * S, 0), text, k["link"], a);
    if (hot && !k.soft) {
        double tw = f.width(text);
        double x = a == CENTER ? r.x + (r.w - tw) / 2 : r.x + 6 * S;
        ui.fill({x, r.y + r.h / 2 + f.line / 2 - 2 * S, tw, (double)S}, k["link"]);
    }
    if (focus) {
        if (k.soft) ui.rrect(r.in(-2 * S), 8 * S, {}, k["focus"], 2 * S, false, true);
        else ui.dotted(r, k["focus"]);
    }
    return go;
}

// a box for a part of a page: a rounded card in Modern and Glossy, a plain framed panel in the others
void panel(Ui &ui, const Rect &r) {
    Skin &k = ui.sk();
    int S = ui.S();
    if (!ui.drawing) return;
    if (k.soft) {
        ui.shadow(r, 12 * S, 6 * S, k.dark ? 0.35 : 0.07);
        ui.rrect(r, 12 * S, k["card"], k["line"], S);
    } else if (k.bevel) {
        ui.fill(r, k["shadow"]);
        ui.fill({r.x + S, r.y + S, r.w - S, r.h - S}, k["light"]);
        ui.fill({r.x + S, r.y + S, r.w - 2 * S, r.h - 2 * S}, k["card"]);
    } else {
        ui.fill(r, k["card"]);
        ui.box(r, k["dark"], S);
    }
}

std::string greeting() {
    std::string hh = strftime_now("%H");
    int h = std::atoi(hh.c_str());
    return h < 12 ? "Good morning" : h < 17 ? "Good afternoon" : "Good evening";
}

}  // namespace

// a new lesson on a day (asks what it's called), then opens it
void App::plan_lesson(const Date &d) {
    std::string t;
    if (!ask(ui, "Plan a Lesson", "What's the lesson on " + dates::fmt_date(d, true) + " called?", t)) return;
    Value l = store::new_lesson(settings());
    l["title"] = text::strip(t);
    l["date"] = dates::fmt_date(d);
    std::string id = l.s("id");
    store.data["lessons"].push(std::move(l));
    save();
    open_item("lesson", id);
}

// ----------------------------------------------------------------------------- Today

namespace {

class Today : public Screen {
public:
    explicit Today(App &a) : Screen(a) { reloaded(); }
    std::string title() override { return "Today"; }
    bool home() const override { return true; }
    void reloaded() override { app.status("Your work saves by itself."); }
    void frame(Ui &ui, const Rect &r) override {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.fill(r, k["window"]);
        Id sid = ui.id("today-scroll");
        ui.begin_scroll(sid, r);
        double padx = 28 * S;
        ui.begin({r.x + padx, ui.lay().y + 22 * S, std::min(ui.width() - 2 * padx, 1180.0 * S), 1e9}, 0);
        double W = ui.width();
        // hello
        std::string name = app.settings().s("teacher");
        std::string hello = greeting() + (name.empty() ? "" : ", " + name);
        Date t = today();
        Rect hr = ui.place(0, k.fpage->line + k.f->line + 6 * S);
        const Value &d = app.store.data;
        std::string sub = std::string(DAY_NAMES[t.weekday()]) + ", " + MONTH_NAMES[t.m - 1] + " " + std::to_string(t.d) + "   \xC2\xB7   " +
                          plural((long long)d.get("lessons").size(), "lesson plan") + "   \xC2\xB7   " +
                          plural((long long)d.get("assessments").size(), "assessment");
        if (ui.drawing) {
            ui.text(*k.fpage, hr.x, std::round(hr.y + k.fpage->ascent), ui.elide(*k.fpage, hello, W - 120 * S), k["text"]);
            ui.text(*k.f, hr.x, std::round(hr.y + k.fpage->line + 4 * S + k.f->ascent), sub, k["dim"]);
        }
        // the school mascot waves from the top right (click it for a cheer)
        buddy_.draw(app, ui, hr.r(), hr.b() + 8 * S, 3 * S);
        ui.space(18 * S);
        // what you can do from here
        ui.flow(true);
        ui.lay().gap = 8 * S;
        if (button(ui, "+  New Assessment", true, true)) app.cmd_new_assessment();
        if (button(ui, "Day Slideshow\xE2\x80\xA6")) day_slideshow(app);
        if (button(ui, "Curriculum Map\xE2\x80\xA6")) curriculum_map(app);
        if (button(ui, "Export Everything\xE2\x80\xA6")) export_everything_dialog(app);
        ui.flow(false);
        ui.lay().gap = 0;
        ui.space(22 * S);
        week(ui, W);
        ui.space(22 * S);
        recent(ui, W);
        ui.space(28 * S);
        ui.end();
        ui.end_scroll();
    }

private:
    Buddy buddy_;
    int week_ = 0;   // weeks from this one

    void week(Ui &ui, double W) {
        Skin &k = ui.sk();
        int S = ui.S();
        Date t = today();
        // this school week; on a weekend, the one coming up
        Date mon = t.plus(-t.weekday());
        if (t.weekday() >= 5) mon = mon.plus(7);
        mon = mon.plus(7L * week_);
        Date fri = mon.plus(4);
        auto days = lessons_by_day(app, mon, fri);
        size_t most = 0;
        for (const auto &kv : days) most = std::max(most, kv.second.size());
        double chip_h = k.f->line + k.fsmall->line + 12 * S, gap = 6 * S;
        double head_h = k.fhead->line + 30 * S, day_h = k.f->line + k.fsmall->line + 16 * S;
        double plan_h = k.fsmall->line + 10 * S;
        double body_h = std::max<double>(1, (double)most) * (chip_h + gap) + plan_h + 6 * S;
        Rect card = ui.place(W, head_h + day_h + body_h + 18 * S);
        panel(ui, card);
        double cx = card.x + 18 * S, cy = card.y + 14 * S;
        std::string range = std::string(MONTH_NAMES[mon.m - 1]) + " " + std::to_string(mon.d) + " \xE2\x80\x93 " +
                            (fri.m != mon.m ? std::string(MONTH_NAMES[fri.m - 1]) + " " : "") + std::to_string(fri.d);
        std::string heading = week_ == 0 ? "This Week" : week_ == 1 ? "Next Week" : week_ == -1 ? "Last Week" : "Week of " + range;
        if (ui.drawing) {
            ui.text(*k.fhead, cx, std::round(cy + k.fhead->ascent), heading, k["text"]);
            double hw = k.fhead->width(heading);
            ui.text(*k.f, cx + hw + 12 * S, std::round(cy + (k.fhead->line - k.f->line) / 2 + k.f->ascent), range, k["dim"]);
        }
        // previous / this week / next
        double bw = ui.btn_w("\xE2\x80\xB9", true) + 6 * S, bh = k.fsmall->line + 14 * S;
        double tw = ui.btn_w("This Week", true) + 6 * S;
        double bx = card.r() - 18 * S - bw;
        if (button_at(ui, ui.id("week-next"), {bx, cy - 2 * S, bw, bh}, "\xE2\x80\xBA", true, false, true)) week_++, ui.again();
        bx -= tw + 6 * S;
        if (button_at(ui, ui.id("week-now"), {bx, cy - 2 * S, tw, bh}, "This Week", week_ != 0, false, true)) week_ = 0, ui.again();
        bx -= bw + 6 * S;
        if (button_at(ui, ui.id("week-prev"), {bx, cy - 2 * S, bw, bh}, "\xE2\x80\xB9", true, false, true)) week_--, ui.again();
        // five columns, Monday to Friday
        double gx = card.x + 12 * S, gw = card.w - 24 * S, colw = (gw - 4 * 10 * S) / 5;
        double y0 = card.y + head_h;
        for (int i = 0; i < 5; i++) {
            Date d = mon.plus(i);
            double x = gx + i * (colw + 10 * S);
            bool is_today = d == t;
            Rect dh{x, y0, colw, day_h};
            if (ui.drawing) {
                if (is_today) {
                    if (k.soft) ui.rrect(dh, 8 * S, k["selsoft"], {}, 0, true, false);
                    else ui.fill(dh, k["sel"]);
                }
                Color fg = is_today && !k.soft ? k["seltext"] : k["text"];
                Color dim = is_today && !k.soft ? k["seltext"] : k["dim"];
                ui.text(*k.fb, dh.x + 8 * S, std::round(dh.y + 7 * S + k.fb->ascent), DAY_SHORT[i] + std::string(" ") + std::to_string(d.d), fg);
                ui.text(*k.fsmall, dh.x + 8 * S, std::round(dh.y + 9 * S + k.fb->line + k.fsmall->ascent),
                        is_today ? "Today" : MONTH_NAMES[d.m - 1], dim);
            }
            double y = dh.b() + 6 * S;
            auto it = days.find(d.ordinal());
            if (it != days.end())
                for (size_t j = 0; j < it->second.size(); j++) {
                    const auto &l = it->second[j];
                    if (lesson_chip(ui, std::to_string(d.ordinal()) + ":" + l.id, {x, y, colw, chip_h}, l, true)) app.open_item("lesson", l.id);
                    y += chip_h + gap;
                }
            if (link(ui, "plan:" + std::to_string(d.ordinal()), {x, y, colw, plan_h}, "+ Plan", *k.fsmall)) app.plan_lesson(d);
        }
    }

    void recent(Ui &ui, double W) {
        Skin &k = ui.sk();
        int S = ui.S();
        std::vector<std::pair<std::string, const Value *>> all;
        for (const auto &x : app.store.data["lessons"].arr())
            if (x.is_object()) all.push_back({"lesson", &x});
        for (const auto &x : app.store.data["assessments"].arr())
            if (x.is_object()) all.push_back({"assessment", &x});
        std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.second->s("updated") > b.second->s("updated"); });
        if (all.size() > 6) all.resize(6);
        double rh = k.f->line + 18 * S, head_h = k.fhead->line + 26 * S;
        Rect card = ui.place(W, head_h + std::max<size_t>(1, all.size()) * rh + 14 * S);
        panel(ui, card);
        if (ui.drawing) ui.text(*k.fhead, card.x + 18 * S, std::round(card.y + 14 * S + k.fhead->ascent), "Pick Up Where You Left Off", k["text"]);
        double y = card.y + head_h;
        if (all.empty() && ui.drawing)
            ui.text_in(*k.f, {card.x + 18 * S, y, card.w - 36 * S, rh}, "Nothing yet. Make a lesson and it shows up here.", k["dim"]);
        for (size_t i = 0; i < all.size(); i++) {
            const Value &x = *all[i].second;
            std::string kind = all[i].first;
            std::string what = kind == "lesson" ? "Lesson" : (x.s("kind").empty() ? "Assessment" : x.s("kind"));
            std::string t = x.s("title").empty() ? "(untitled)" : x.s("title");
            Rect rr{card.x + 8 * S, y, card.w - 16 * S, rh};
            Id id = ui.id("recent:" + x.s("id"));
            bool go = ui.hit(id, rr, true, true);
            bool focus = ui.focused(id);
            if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
            bool hot = ui.over(rr);
            if (hot) ui.cursor(plat::HAND);
            if (ui.drawing) {
                if (hot) {
                    if (k.soft) ui.rrect(rr, 8 * S, mix(k["card"], k["text"], 0.05), {}, 0, true, false);
                    else ui.fill(rr, k["selsoft"]);
                }
                Color fg = hot && !k.soft && hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["text"];
                Color dim = hot && !k.soft && hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["dim"];
                double is = 18 * S;
                line_icon(ui, kind == "lesson" ? "lessons" : "assessments", {rr.x + 10 * S, rr.y + (rh - is) / 2, is, is}, dim);
                std::string when = x.s("updated");
                Date wd;
                if (dates::stamp_day(when, wd)) {
                    long ago = today().ordinal() - wd.ordinal();
                    when = ago == 0 ? "Today" : ago == 1 ? "Yesterday" : ago < 7 && ago > 0 ? DAY_NAMES[wd.weekday()]
                                                                                            : dates::fmt_date(wd, false, wd.y != today().y);
                }
                double ww = k.fsmall->width(when) + 10 * S, kw = k.fsmall->width(what) + 18 * S;
                double tx = rr.x + 10 * S + is + 12 * S, room = rr.w - (tx - rr.x) - ww - kw - 10 * S;
                ui.text_in(*k.f, {tx, rr.y, room, rh}, ui.elide(*k.f, t, room), fg);
                ui.text_in(*k.fsmall, {rr.r() - ww - kw, rr.y, kw, rh}, what, dim);
                ui.text_in(*k.fsmall, {rr.r() - ww, rr.y, ww - 10 * S, rh}, when, dim, RIGHT);
                if (focus) {
                    if (k.soft) ui.rrect(rr.in(-2 * S), 10 * S, {}, k["focus"], 2 * S, false, true);
                    else ui.dotted(rr.in(S), k["focus"]);
                }
            }
            if (go) app.open_item(kind, x.s("id"));
            y += rh;
        }
    }
};

// ----------------------------------------------------------------------------- Calendar

class CalendarPage : public Screen {
public:
    explicit CalendarPage(App &a) : Screen(a) {
        Date t = today();
        y_ = t.y;
        m_ = t.m;
        app.status("Click a lesson to open it, or + to plan one on that day.");
    }
    std::string title() override { return "Calendar"; }
    bool escape() override { return false; }
    void frame(Ui &ui, const Rect &r) override {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.fill(r, k["window"]);
        // the month and the buttons to change it
        double bar_h = std::max(k.fhead->line, ui.ctl_h()) + 8 * S;
        Rect bar{r.x + 8 * S, r.y + 4 * S, r.w - 16 * S, bar_h};
        std::string month = std::string(MONTH_NAMES[m_ - 1]) + " " + std::to_string(y_);
        if (ui.drawing) ui.text_in(*k.fhead, bar, month, k["text"]);
        double bw = ui.btn_w("\xE2\x80\xB9") + 6 * S, tw = ui.btn_w("This Month") + 6 * S, bh = ui.ctl_h() + (k.soft ? 6 * S : 0);
        double bx = bar.r() - bw;
        if (button_at(ui, ui.id("cal-next"), {bx, bar.y, bw, bh}, "\xE2\x80\xBA") || ui.key(plat::K_PGDN)) step(1);
        bx -= tw + 6 * S;
        Date t = today();
        if (button_at(ui, ui.id("cal-now"), {bx, bar.y, tw, bh}, "This Month", !(t.y == y_ && t.m == m_)) || ui.key(plat::K_HOME)) y_ = t.y, m_ = t.m,
                                                                                                                              ui.again();
        bx -= bw + 6 * S;
        if (button_at(ui, ui.id("cal-prev"), {bx, bar.y, bw, bh}, "\xE2\x80\xB9") || ui.key(plat::K_PGUP)) step(-1);
        // the grid: weeks down, Monday to Sunday across (the weekend narrower)
        Date first;
        make_date(y_, m_, 1, first);
        Date start = first.plus(-first.weekday());
        int dim_ = days_in_month(y_, m_);
        Date last;
        make_date(y_, m_, dim_, last);
        int weeks = (int)((last.ordinal() - start.ordinal()) / 7) + 1;
        Rect grid{r.x + 8 * S, bar.b() + 10 * S, r.w - 16 * S, r.b() - bar.b() - 18 * S};
        panel(ui, grid);
        Rect in = grid.in(8 * S);
        double head = k.fsmallb->line + 10 * S;
        double unit = in.w / 6.0;   // weekdays get 1, the weekend days 0.5 each
        auto col_x = [&](int c) { return in.x + (c < 5 ? c * unit : 5 * unit + (c - 5) * unit / 2); };
        auto col_w = [&](int c) { return c < 5 ? unit : unit / 2; };
        if (ui.drawing)
            for (int c = 0; c < 7; c++)
                ui.text_in(*k.fsmallb, {col_x(c) + 6 * S, in.y, col_w(c) - 6 * S, head}, c < 5 ? DAY_NAMES[c] : DAY_SHORT[c], k["dim"]);
        double rowh = (in.h - head) / weeks;
        auto days = lessons_by_day(app, start, start.plus(7L * weeks - 1));
        for (int w = 0; w < weeks; w++)
            for (int c = 0; c < 7; c++) {
                Date d = start.plus(7L * w + c);
                Rect cell{col_x(c), in.y + head + w * rowh, col_w(c), rowh};
                bool in_month = d.m == m_;
                bool is_today = d == t;
                if (ui.drawing) {
                    Color lc = k.soft ? k["line"] : k["shadow"];
                    ui.fill({cell.x, cell.y, cell.w, (double)S}, lc);
                    if (c > 0) ui.fill({cell.x, cell.y, (double)S, cell.h}, lc);
                    if (c >= 5 && k.soft) ui.fill({cell.x + S, cell.y + S, cell.w - S, cell.h - S}, mix(k["card"], k["window"], 0.6));
                    std::string num = std::to_string(d.d);
                    Font &nf = *k.fb;
                    double nw = std::max(nf.width(num) + 12 * S, nf.line + 4 * S);
                    Rect nr{cell.x + 4 * S, cell.y + 4 * S, nw, nf.line + 4 * S};
                    if (is_today) {
                        if (k.soft) ui.rrect(nr, nr.h / 2, k["sel"], {}, 0, true, false);
                        else ui.fill(nr, k["sel"]);
                    }
                    ui.text_in(nf, nr, num, is_today ? k["seltext"] : in_month ? k["text"] : k["off"], CENTER);
                }
                double top = cell.y + k.fb->line + 12 * S;
                double ch = k.fsmall->line + 8 * S;
                auto it = days.find(d.ordinal());
                int fits = std::max(0, (int)((cell.b() - top - 4 * S) / (ch + 3 * S)));
                if (it != days.end()) {
                    int n = (int)it->second.size();
                    int show = n > fits ? std::max(0, fits - 1) : n;
                    for (int j = 0; j < show; j++) {
                        const auto &l = it->second[(size_t)j];
                        if (lesson_chip(ui, "m" + std::to_string(d.ordinal()) + ":" + l.id, {cell.x + 4 * S, top + j * (ch + 3 * S), cell.w - 8 * S, ch}, l,
                                        false))
                            app.open_item("lesson", l.id);
                    }
                    if (show < n && link(ui, "more:" + std::to_string(d.ordinal()), {cell.x + 2 * S, top + show * (ch + 3 * S), cell.w - 4 * S, ch},
                                         "+" + std::to_string(n - show) + " more", *k.fsmall))
                        day_list(d, it->second);
                }
                // + on the day's corner: plan a lesson then
                double pw = k.fb->line + 4 * S;
                Rect pr{cell.r() - pw - 4 * S, cell.y + 4 * S, pw, pw};
                if (ui.over(cell) || ui.focused(ui.id("link:plan:" + std::to_string(d.ordinal()))))
                    if (link(ui, "plan:" + std::to_string(d.ordinal()), pr, "+", *k.fb, CENTER)) app.plan_lesson(d);
            }
    }

private:
    int y_, m_;
    void step(int d) {
        m_ += d;
        if (m_ < 1) m_ = 12, y_--;
        if (m_ > 12) m_ = 1, y_++;
        app.ui.again();
    }
    void day_list(const Date &d, const std::vector<DayLesson> &ls) {
        std::vector<std::pair<std::string, std::string>> opts;
        for (const auto &l : ls) opts.push_back({l.id, l.title + (l.course.empty() ? "" : "   (" + l.course + ")")});
        std::string id = choose(app.ui, dates::fmt_date(d, true), "Lessons on this day:", opts);
        if (!id.empty()) app.open_item("lesson", id);
    }
};

}  // namespace

Factory make_home() { return make<Today>(); }
Factory calendar_page() { return make<CalendarPage>(); }

}  // namespace gui
