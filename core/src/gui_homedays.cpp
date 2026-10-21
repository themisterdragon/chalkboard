// Days of school at home (at-home days, or whatever the district calls them): the page that lists the
// days a district allows, planning a lesson for each one ahead of time, writing a lesson's at-home
// version (explained in writing, the work cut down), making each class's packet on the day, and
// the teacher log the district asks for afterward.
#include <algorithm>
#include <cmath>
#include <map>

#include "doc.h"
#include "exporting.h"
#include "gui.h"
#include "homedays.h"
#include "schoolcal.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

std::string day_name(App &app) { return store::home_day_name(app.settings()); }
std::string day_label(App &app, int n) { return day_name(app) + " " + std::to_string(n); }

// the classes lessons are in, and the ones the class periods name
std::vector<std::string> courses(App &app) {
    std::vector<const Value *> all;
    for (const auto &x : app.store.data["lessons"].arr())
        if (x.is_object()) all.push_back(&x);
    std::vector<std::string> out;
    for (const auto &c : store::classes(app.settings(), all))
        if (!c.first.empty()) out.push_back(c.first);
    return out;
}

// the lesson on the calendar for a class that day (not one planned for an at-home day), or ""
std::string lesson_on(App &app, const std::string &course, const Date &d) {
    std::string best, stamp;
    for (const auto &l : app.store.data["lessons"].arr()) {
        if (!l.is_object() || l.get("home_slot").as_int() > 0 || !store::in_class(l, course)) continue;
        auto ds = store::lesson_dates(l);
        if (std::find(ds.begin(), ds.end(), d) == ds.end()) continue;
        if (best.empty() || l.s("updated") > stamp) best = l.s("id"), stamp = l.s("updated");
    }
    return best;
}

bool explained(const Value &l) { return !text::strip(store::field(l, "nti_explain")).empty(); }

// asks how many days the district allows, the first time; false when cancelled
bool have_allotted(App &app) {
    if (homedays::allotted(app.settings()) > 0) return true;
    std::string n = "10";
    if (!ask(app.ui, day_name(app) + "s", "How many " + day_name(app) + "s does your district allow this year?", n)) return false;
    int v = std::max(0, std::min(60, std::atoi(text::strip(n).c_str())));
    if (v <= 0) return false;
    app.settings()["home_days_allotted"] = v;
    app.save();
    return true;
}

std::string now_time() {
    std::string t = strftime_now("%I:%M %p");
    return !t.empty() && t[0] == '0' ? t.substr(1) : t;
}

bool edit_contact(App &app, Value &c, const std::string &title) {
    std::string name = c.s("name"), time = c.s("time").empty() ? now_time() : c.s("time"), mode = c.s("mode");
    std::vector<std::pair<std::string, std::string>> modes;
    for (const auto &m : homedays::MODES) modes.push_back({m, m});
    if (mode.empty()) mode = homedays::MODES[0];
    bool first = true;
    int r = dialog(
        app.ui, {title, 460, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double lw = k.f->width("Mode of communication:") + 8 * S;
            if (first) {
                focus_at_end(u, u.id("entry:name"));
                first = false;
            }
            u.row({lw, -1}, u.ctl_h());
            label_mid(u, "Name:");
            entry(u, "name", name);
            u.row({lw, k.f->width("12:00 PM") + 30 * S}, u.ctl_h());
            label_mid(u, "Time:");
            entry(u, "time", time);
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Mode of communication:");
            dropdown(u, "mode", modes, mode, 18);
            u.row({});
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    c["name"] = text::strip(name);
    c["time"] = text::strip(time);
    c["mode"] = mode;
    return true;
}

}  // namespace

// ----------------------------------------------------------------------------- a lesson's at-home version

namespace {

// how ready an at-home lesson is, in a few words
std::string readiness(App &app, const Value &l) {
    if (homedays::used(l)) {
        Date d;
        return dates::stamp_day(store::field(l, "home_used"), d) ? "Used " + dates::fmt_date(d, false, false) : "Used";
    }
    bool qs = false;
    for (const Value *a : app.store.attached(l)) qs |= a->get("questions").size() > 0;
    if (!explained(l)) return "Needs a written explanation";
    return qs ? "Ready" : "Ready (no questions)";
}

std::string lesson_name(const Value &l) {
    std::string t = store::field(l, "title");
    return t.empty() ? "(untitled lesson)" : t;
}

void copy_for_classroom(App &app, const std::string &id) {
    const Value *l = app.lesson(id);
    if (!l) return;
    app.win.set_clipboard(doc::classroom_text(*l, app.store));
    app.status("Copied \"" + lesson_name(*l) + "\" for Google Classroom: paste it into a question or an assignment.");
}

}  // namespace

bool home_version_dialog(App &app, const std::string &id) {
    Value *l = app.lesson(id);
    if (!l) return false;
    std::string explain = store::field(*l, "nti_explain"), work = store::field(*l, "nti_work");
    std::string title = store::field(*l, "title"), unit = text::strip(store::field(*l, "unit"));
    bool ready = homedays::is_home(*l);
    bool first = true;
    int r = dialog(
        app.ui, {day_name(app) + " Lesson: " + (title.empty() ? std::string("Untitled Lesson") : title), 720, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Value *x = app.lesson(id);
            if (!x) return 2;
            if (first) {
                u.focus(u.id("text:explain"));
                first = false;
            }
            check(u, unit.empty() ? "Keep this in the " + day_name(app) + " lessons for any unit, ready for a day at home"
                                 : "Keep this in " + unit + "'s " + day_name(app) + " lessons, ready for a day at home",
                  ready);
            u.space(10 * S);
            label(u, "Explain it in writing", true);
            para(u, "Students can't ask you that day, so teach it here: what we're learning and why, with one short example. A few sentences.", true, 0,
                 k.fsmall.get());
            u.space(4 * S);
            text_area(u, "explain", explain, 4, 10);
            if (text::strip(explain).empty() && !text::strip(store::field(*x, "instruction")).empty()) {
                u.space(4 * S);
                if (button(u, "Start From the Lesson's I Do", true, false, true)) explain = store::field(*x, "instruction");
            }
            u.space(12 * S);
            label(u, "What students do", true);
            para(u, "One step per line, in order. Keep it to about half an hour. Students won't see a time.", true, 0, k.fsmall.get());
            u.space(4 * S);
            text_area(u, "work", work, 4, 10, false);
            if (text::strip(work).empty() && (!text::strip(store::field(*x, "guided")).empty() || !text::strip(store::field(*x, "independent")).empty())) {
                u.space(4 * S);
                if (button(u, "Start From We Do and You Do", true, false, true))
                    work = text::strip(store::field(*x, "guided") + "\n" + store::field(*x, "independent"));
            }
            u.space(8 * S);
            para(u, "Worksheets linked to this lesson print with the packet, and their questions go in the Google Classroom copy. Quizzes and tests "
                    "wait until you're back.",
                 true, 0, k.fsmall.get());
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    l = app.lesson(id);
    if (!l) return false;
    (*l)["nti_explain"] = explain;
    (*l)["nti_work"] = work;
    if (ready) (*l)["home_ready"] = true;
    else {
        l->erase("home_ready");
        l->erase("home_slot");
    }
    app.touch(*l);
    return true;
}

// ----------------------------------------------------------------------------- the lessons kept for at-home days

namespace {

// a new at-home lesson: its class, its unit, its name; "" when cancelled
std::string new_home_lesson(App &app, const std::string &course0, const Value *from) {
    auto cs = courses(app);
    if (cs.empty()) {
        alert(app.ui, day_name(app) + " Lessons", "Plan a lesson with a class (its Course) first.");
        return "";
    }
    std::string course = course0.empty() ? cs[0] : course0, title = from ? store::field(*from, "title") : "", unit;
    if (from) unit = text::strip(store::field(*from, "unit"));
    bool first = true;
    int r = dialog(
        app.ui, {from ? "Copy a Lesson" : "New " + day_name(app) + " Lesson", 520, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double lw = k.f->width("Class:") + 8 * S;
            std::vector<std::pair<std::string, std::string>> copts;
            for (const auto &c : cs) copts.push_back({c, c});
            std::vector<const Value *> mine;
            for (const auto &x : app.store.data["lessons"].arr())
                if (x.is_object() && store::in_class(x, course)) mine.push_back(&x);
            std::vector<std::pair<std::string, std::string>> uopts;
            for (const auto &un : app.store.units(mine)) uopts.push_back({un, un});
            if (first && unit.empty() && !uopts.empty()) unit = homedays::unit_on(app.store.data["lessons"], course, today());
            if (first && unit.empty() && !uopts.empty()) unit = uopts.front().first;
            if (!uopts.empty()) uopts.push_back({"", "Any unit"});
            if (!unit.empty() && std::find_if(uopts.begin(), uopts.end(), [&](const auto &o) { return o.first == unit; }) == uopts.end())
                uopts.push_back({unit, unit});
            if (first) {
                focus_at_end(u, u.id("entry:title"));
                first = false;
            }
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Class:");
            dropdown(u, "class", copts, course, 20);
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Unit:");
            if (uopts.empty()) entry(u, "unit", unit, 14 * k.f->width("0"));
            else dropdown(u, "unit", uopts, unit, 20);
            u.row({lw, -1}, u.ctl_h());
            label_mid(u, "Name:");
            entry(u, "title", title);
            u.row({});
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return "";
    Value x = from ? *from : store::new_lesson(app.settings());
    x["id"] = store::new_id();
    x["title"] = text::strip(title).empty() ? day_name(app) + " Lesson" : text::strip(title);
    x["course"] = course;
    x["unit"] = unit;
    x["date"] = "";
    x["home_ready"] = true;
    x.erase("home_slot");
    x.erase("home_used");
    x["created"] = store::now();
    x["updated"] = store::now();
    std::string id = x.s("id");
    app.store.data["lessons"].push(std::move(x));
    app.save();
    return id;
}

}  // namespace

void home_lessons_dialog(App &app, const std::string &course0) {
    std::string course = course0, open_id;
    int sel = 0;
    bool first = true;
    dialog(
        app.ui, {day_name(app) + " Lessons", 880, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Lessons kept ready for a " + day_name(app) + ", with their unit. On the day, each class gets an unused one from the unit it's "
                    "in. Keep each one simple, about half an hour, explained in writing, with questions you can paste into Google Classroom.");
            u.space(8 * S);
            auto cs = courses(app);
            std::vector<std::pair<std::string, std::string>> copts = {{"", "All classes"}};
            for (const auto &c : cs) copts.push_back({c, c});
            u.row({k.f->width("Class:") + 8 * S, 0}, u.ctl_h());
            label_mid(u, "Class:");
            dropdown(u, "class", copts, course, 20);
            u.row({});
            u.space(6 * S);
            std::vector<const Value *> ls;
            for (const auto &c : cs)
                if (course.empty() || c == course)
                    for (const Value *l : homedays::bank(app.store.data["lessons"], c)) ls.push_back(l);
            std::vector<std::vector<std::string>> rows;
            for (const Value *l : ls) rows.push_back({store::class_of(*l), homedays::unit_label(*l), lesson_name(*l), readiness(app, *l)});
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 9 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:bank"));
                first = false;
            }
            ListResult res = list_view(u, "bank", lr, {{"Class", 140, false}, {"Unit", 90, false}, {"Lesson", 380, true}, {"Ready?", 210, false}}, rows,
                                       sel, "None yet. Click New Lesson, or turn one on from a lesson (" + day_name(app) + " Lesson\xE2\x80\xA6).");
            std::string lid = sel >= 0 && sel < (int)ls.size() ? ls[(size_t)sel]->s("id") : "";
            if (res.open && !lid.empty()) home_version_dialog(app, lid);
            u.space(8 * S);
            u.flow();
            if (button(u, "New Lesson\xE2\x80\xA6", true, false, true)) {
                std::string id = new_home_lesson(app, course, nullptr);
                if (!id.empty()) home_version_dialog(app, id);
            }
            if (button(u, "Copy a Lesson\xE2\x80\xA6", true, false, true)) {
                std::vector<std::pair<std::string, std::string>> opts;
                for (const auto &x : app.store.data["lessons"].arr())
                    if (x.is_object() && !homedays::is_home(x) && (course.empty() || store::in_class(x, course)))
                        opts.push_back({x.s("id"), lesson_name(x) + "   (" + store::class_of(x) + ", " + store::field(x, "unit") + ")"});
                std::string pick = opts.empty() ? "" : choose(u, "Copy a Lesson", "Start a " + day_name(app) + " lesson from a copy of which lesson?", opts);
                if (const Value *from = app.lesson(pick)) {
                    Value copy = *from;
                    std::string id = new_home_lesson(app, store::class_of(copy), &copy);
                    if (!id.empty()) home_version_dialog(app, id);
                }
            }
            if (button(u, "Write It\xE2\x80\xA6", !lid.empty(), false, true)) home_version_dialog(app, lid);
            if (button(u, "Copy for Google Classroom", !lid.empty(), false, true)) copy_for_classroom(app, lid);
            if (button(u, "Open Lesson", !lid.empty(), false, true)) {
                open_id = lid;
                return 1;
            }
            if (button(u, "Remove", !lid.empty(), false, true) &&
                confirm(u, "Remove", "Take \"" + lesson_name(*app.lesson(lid)) + "\" out of the " + day_name(app) +
                                         " lessons? The lesson itself stays in your Lesson Plans.",
                        "Remove")) {
                if (Value *x = app.lesson(lid)) {
                    x->erase("home_ready");
                    x->erase("home_slot");
                    app.save();
                }
            }
            u.flow(false);
            return 0;
        },
        {{"Done", 1}}, 0, 1);
    app.save();
    if (!open_id.empty()) app.later([&app, open_id] { app.open_item("lesson", open_id); });
}

// ----------------------------------------------------------------------------- on the day

bool home_day_now(App &app, const Date &day0) {
    if (!have_allotted(app)) return false;
    Date day = day0.valid() ? day0 : today();
    int allot = homedays::allotted(app.settings());
    const Value *was = homedays::on_date(app.store.data, day);
    int n = was ? (int)was->get("number").as_int() : homedays::next_unused(app.store.data, app.settings());
    if (n <= 0) n = allot;
    std::string n_key = std::to_string(n);
    std::vector<std::pair<std::string, std::string>> numbers;
    for (int i = 1; i <= allot; i++) {
        const Value *r = homedays::find(app.store.data, i);
        Date d = r ? homedays::date_of(*r) : Date{};
        numbers.push_back({std::to_string(i), day_label(app, i) + (d.valid() && d != day ? "  (used " + dates::fmt_date(d, false, false) + ")" : "")});
    }
    bool move_regular = true;
    int sel = 0;
    bool first = true;
    std::map<std::string, std::string> chosen;   // class -> the lesson picked by hand ("" = no packet)
    struct Pick {
        std::string course, lesson_id, regular;
        bool home = false;
    };
    std::vector<Pick> picks;
    auto work_out = [&] {
        picks.clear();
        for (const auto &c : courses(app)) {
            Pick p;
            p.course = c;
            p.regular = lesson_on(app, c, day);
            auto it = chosen.find(c);
            if (it != chosen.end()) p.lesson_id = it->second;
            else if (const Value *l = homedays::pick(app.store.data["lessons"], c, day)) p.lesson_id = l->s("id");
            else p.lesson_id = p.regular;
            const Value *l = app.lesson(p.lesson_id);
            p.home = l && homedays::is_home(*l);
            picks.push_back(p);
        }
    };
    int r = dialog(
        app.ui, {day_name(app) + " Packets", 860, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            work_out();
            u.row({0, 0, 0, 0}, u.ctl_h());
            label_mid(u, dates::fmt_date(day, true) + " is ", true);
            dropdown(u, "number", numbers, n_key, 26);
            label_mid(u, "   ");
            if (button(u, "Change Day\xE2\x80\xA6", true, false, true)) {
                Date a, b;
                if (pick_dates(app, "Which Day?", &day, nullptr, a, b)) day = a;
            }
            u.row({});
            u.space(8 * S);
            para(u, "A packet for each class, from a " + day_name(app) + " lesson in the unit it's in (or that day's lesson): explained in writing, "
                    "a short list of what to do, the questions, and how to reach you. Each one also comes as a Google Classroom copy.");
            u.space(8 * S);
            std::vector<std::vector<std::string>> rows;
            int missing = 0, unexplained = 0;
            for (const auto &p : picks) {
                const Value *l = app.lesson(p.lesson_id);
                std::string from = !l ? "Nothing ready: no packet"
                                       : (p.home ? homedays::unit_label(*l) + ", " + day_name(app) + " lesson: " : "That day's lesson: ") + lesson_name(*l);
                missing += !l;
                unexplained += l && !explained(*l);
                rows.push_back({p.course, from, !l ? "" : readiness(app, *l)});
            }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 6 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:packets"));
                first = false;
            }
            ListResult res = list_view(u, "packets", lr, {{"Class", 150, false}, {"Packet from", 450, true}, {"Ready?", 200, false}}, rows, sel);
            const Pick *p = sel >= 0 && sel < (int)picks.size() ? &picks[(size_t)sel] : nullptr;
            if (res.open && res.col < 0) return 1;   // Return: Make Packets (a double-click edits the lesson)
            if (res.open && p && !p->lesson_id.empty()) home_version_dialog(app, p->lesson_id);
            u.space(6 * S);
            u.flow();
            if (button(u, "Change Lesson\xE2\x80\xA6", p != nullptr, false, true)) {
                std::vector<std::pair<std::string, std::string>> opts;
                for (const Value *l : homedays::bank(app.store.data["lessons"], p->course))
                    opts.push_back({l->s("id"), homedays::unit_label(*l) + ": " + lesson_name(*l) + (homedays::used(*l) ? "  (" + readiness(app, *l) + ")" : "")});
                if (const Value *reg = app.lesson(p->regular)) opts.push_back({reg->s("id"), "That day's lesson: " + lesson_name(*reg)});
                opts.push_back({"\x01none", "No packet for this class"});
                std::string c = p->course;
                std::string got = choose(u, "Change Lesson", "The " + c + " packet comes from:", opts);
                if (!got.empty()) chosen[c] = got == "\x01none" ? "" : got;
            }
            if (button(u, "Write It\xE2\x80\xA6", p && !p->lesson_id.empty(), false, true)) home_version_dialog(app, p->lesson_id);
            if (button(u, "Copy for Google Classroom", p && !p->lesson_id.empty(), false, true)) copy_for_classroom(app, p->lesson_id);
            if (button(u, day_name(app) + " Lessons\xE2\x80\xA6", true, false, true)) home_lessons_dialog(app, p ? p->course : "");
            u.flow(false);
            if (unexplained || missing) {
                u.space(6 * S);
                std::string t = unexplained ? plural(unexplained, "packet has", "packets have") +
                                                  " no written explanation, so students get the steps only. A few sentences make a big difference."
                                            : "";
                if (missing) t += std::string(t.empty() ? "" : " ") + plural(missing, "class has", "classes have") + " nothing ready for this day.";
                para(u, "Heads up: " + t);
            }
            bool replacing = false;
            for (const auto &q : picks) replacing |= q.home && !q.regular.empty();
            if (replacing) {
                u.space(6 * S);
                check(u, "Move the regular lessons planned for this day (and after) back one school day, in the classes using a " + day_name(app) +
                             " lesson",
                      move_regular, true, u.width() - 30 * S);
            }
            return 0;
        },
        {{"Make Packets", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    int num = std::atoi(n_key.c_str());
    work_out();
    std::vector<const Value *> lessons;
    for (const auto &p : picks)
        if (const Value *l = app.lesson(p.lesson_id)) lessons.push_back(l);
    if (lessons.empty()) {
        alert(app.ui, day_name(app) + " Packets", "No class has a lesson ready for this day yet.");
        return false;
    }
    // the day on the school calendar (a school day), the regular lessons moved, the at-home ones marked used
    auto cal = schoolcal::read(app.settings());
    if (!cal.day_off(day)) {
        cal.off.push_back({day, day, day_label(app, num), "athome"});
        schoolcal::write(app.settings(), cal);
    }
    std::vector<schoolcal::Move> moved;
    for (const auto &p : picks)
        if (p.home && !p.regular.empty() && move_regular) {
            auto plan = schoolcal::plan_shift(app.store.data["lessons"], cal, day, 1, p.course);
            for (const auto &m : plan)
                if (Value *l = app.lesson(m.id)) (*l)["date"] = m.after;
            moved.insert(moved.end(), plan.begin(), plan.end());
        }
    if (!moved.empty()) app.undo_moves = moved;
    for (const auto &p : picks)
        if (p.home)
            if (Value *l = app.lesson(p.lesson_id)) {
                (*l)["home_used"] = day.iso();
                if (store::lesson_dates(*l).empty()) (*l)["date"] = dates::fmt_date(day, true);   // on the Calendar that day
            }
    Value &rec = homedays::make(app.store.data, num);
    rec["date"] = day.iso();
    if (rec.get("classes").size() == 0)
        for (const auto &c : homedays::log_classes(app.settings(), app.store.data["lessons"])) {
            Value row = Value::object();
            row["period"] = c.first;
            row["course"] = c.second;
            for (const char *k : {"enrolled", "participating", "technology", "paper"}) row[k] = "";
            rec["classes"].push(row);
        }
    app.save();
    lessons.clear();   // (looked up again: the list may have moved)
    for (const auto &p : picks)
        if (const Value *l = app.lesson(p.lesson_id)) lessons.push_back(l);
    std::vector<std::string> files;
    try {
        busy(app, "Making Packets", [&](const exporting::Progress &prog) { files = exporting::export_home_day(app.store, day, lessons, prog); });
    } catch (const exporting::ExportError &e) {
        alert(app.ui, day_name(app) + " Packets", e.message, "warn");
        return false;
    }
    export_done(app, files);
    app.status("Packets made, with a Google Classroom copy of each. After the day, fill in the teacher log on the " + day_name(app) + "s page." +
               (moved.empty() ? "" : " Moved " + plural((long long)moved.size(), "regular lesson") + " back a day (Edit \xE2\x80\xBA Undo Shift)."));
    return true;
}

// ----------------------------------------------------------------------------- the teacher log

void home_day_log(App &app, int n) {
    Value &rec0 = homedays::make(app.store.data, n);
    if (rec0.get("classes").size() == 0)
        for (const auto &c : homedays::log_classes(app.settings(), app.store.data["lessons"])) {
            Value row = Value::object();
            row["period"] = c.first;
            row["course"] = c.second;
            for (const char *k : {"enrolled", "participating", "technology", "paper"}) row[k] = "";
            rec0["classes"].push(row);
        }
    int csel = -1;
    auto rec = [&]() -> Value & { return homedays::make(app.store.data, n); };   // (looked up each time)
    int r = dialog(
        app.ui, {day_label(app, n) + " Teacher Log", 820, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Date d = homedays::date_of(rec());
            std::string who = app.settings().s("teacher"), school = app.settings().s("school");
            u.row({0, 0}, u.ctl_h());
            label_mid(u, "Teacher: " + (who.empty() ? std::string("(Settings)") : who) + "     School: " + (school.empty() ? std::string("(Settings)") : school) +
                             "     Date: " + (d.valid() ? dates::fmt_date(d, true) : std::string("not set")) + "   ");
            if (button(u, "Change Day\xE2\x80\xA6", true, false, true)) {
                Date a, b;
                if (pick_dates(app, "Date of " + day_label(app, n), d.valid() ? &d : nullptr, nullptr, a, b)) {
                    rec()["date"] = a.iso();
                    app.save_soon();
                }
            }
            u.row({});
            u.space(10 * S);
            label(u, "Participation", true);
            u.space(4 * S);
            // a row per class: A, B, D, E typed; C worked out
            // class, then A _ B _ C D _ E _ (the _ cells keep the boxes apart)
            double g = 10.0 * S, cw = 200 * S, nw = std::floor((u.width() - cw - 9 * u.lay().gap) / 5);
            std::vector<double> cells = {cw, nw - g, g, nw - g, g, nw, nw - g, g, nw - g, g};
            u.row(cells, k.fsmallb->line * 2 + 6 * S);
            int hcol = 0;
            for (const char *h : {"Class", "A. Students\nin class", "", "B. Took part", "", "C. Percent\n(B / A x 100)", "D. Used\ntechnology", "",
                                  "E. Used book\nor paper", ""}) {
                Rect hr = u.place(0, 0);
                hr.w = hcol == 0 || hcol == 5 ? hr.w : nw;   // (a heading may run over its gap)
                hcol++;
                if (u.drawing) {
                    double y = hr.y;
                    for (const auto &line : text::split(h, "\n")) {
                        u.text(*k.fsmallb, hr.x, std::round(y + k.fsmallb->ascent), line, k["dim"]);
                        y += k.fsmallb->line;
                    }
                }
            }
            Value &classes = rec()["classes"];
            std::vector<std::string> warn;
            for (size_t i = 0; i < classes.size(); i++) {
                Value &c = classes.at(i);
                u.push_id("class" + std::to_string(i));
                u.row(cells, u.ctl_h());
                std::string nm = c.s("period"), course = c.s("course");
                label_mid(u, nm + (!course.empty() && text::lower(course) != text::lower(nm) ? " (" + course + ")" : ""));
                bool changed = false;
                auto num = [&](const char *key) {
                    if (!c[key].is_string()) c[key] = c.get(key).is_number() ? std::to_string(c.get(key).as_int()) : std::string();
                    std::string &v = c[key].str_ref();
                    if (entry(u, key, v)) {
                        std::string digits;
                        for (char ch : v)
                            if (ch >= '0' && ch <= '9') digits += ch;
                        v = digits.substr(0, 5);
                        changed = true;
                    }
                    u.place(0, 0);
                };
                num("enrolled");
                num("participating");
                long a = homedays::count(c, "enrolled"), b = homedays::count(c, "participating");
                label_mid(u, text::strip(c.s("participating")).empty() ? "" : homedays::percent(b, a), true);
                num("technology");
                num("paper");
                if (changed) app.save_soon();
                if (a > 0 && b > a) warn.push_back(nm + " has more taking part than in class");
                u.pop_id();
            }
            u.row({});
            if (button(u, "Add a Class\xE2\x80\xA6", true, false, true)) {
                std::string nm;
                if (ask(u, "Add a Class", "What's the class called (like Period 5)?", nm) && !text::strip(nm).empty()) {
                    Value row = Value::object();
                    row["period"] = text::strip(nm);
                    row["course"] = "";
                    for (const char *key : {"enrolled", "participating", "technology", "paper"}) row[key] = "";
                    rec()["classes"].push(row);
                    app.save_soon();
                }
            }
            if (!warn.empty()) {
                u.space(4 * S);
                para(u, "Check: " + text::join(warn, "; ") + ".");
            }
            u.space(12 * S);
            label(u, "Communication log", true);
            u.space(4 * S);
            Value &contacts = rec()["contacts"];
            if (!contacts.is_array()) contacts = Value::array();
            std::vector<std::vector<std::string>> rows;
            for (const auto &c : contacts.arr()) rows.push_back({c.s("name"), c.s("time"), c.s("mode")});
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 5 * rh + hh + 2 * S);
            ListResult res = list_view(u, "contacts", lr, {{"Name", 320, true}, {"Time", 130, false}, {"Mode of Communication", 230, false}}, rows, csel,
                                       "No one yet. Add each student or family you talked to.");
            auto edit_one = [&] {
                if (csel < 0 || csel >= (int)rec()["contacts"].size()) return;
                Value c = rec()["contacts"].at((size_t)csel);
                if (edit_contact(app, c, "Communication")) {
                    rec()["contacts"].at((size_t)csel) = c;
                    app.save_soon();
                }
            };
            if (res.open) edit_one();
            u.space(6 * S);
            u.flow();
            if (button(u, "Add\xE2\x80\xA6", true, false, true)) {
                Value c = Value::object();
                if (edit_contact(app, c, "Add to the Communication Log")) {
                    rec()["contacts"].push(c);
                    csel = (int)rec()["contacts"].size() - 1;
                    app.save_soon();
                }
            }
            if (button(u, "Edit\xE2\x80\xA6", csel >= 0, false, true)) edit_one();
            if ((button(u, "Remove", csel >= 0, false, true) || res.del) && csel >= 0 && csel < (int)rec()["contacts"].size() &&
                confirm(u, "Remove", "Take " + rec()["contacts"].at((size_t)csel).s("name") + " off the log?", "Remove")) {
                auto &a = rec()["contacts"].arr_ref();
                a.erase(a.begin() + csel);
                csel = std::min(csel, (int)a.size() - 1);
                app.save_soon();
            }
            u.flow(false);
            return 0;
        },
        {{"Export Log", 2}, {"Done", 1}}, 1, 1);
    app.save();
    if (r != 2) return;
    std::vector<std::string> files;
    try {
        files = exporting::export_home_log(app.store, rec());
    } catch (const exporting::ExportError &e) {
        alert(app.ui, "Teacher Log", e.message, "warn");
        return;
    }
    export_done(app, files);
}

// ----------------------------------------------------------------------------- the page

namespace {

class HomeDaysPage : public Screen {
public:
    explicit HomeDaysPage(App &a) : Screen(a) { a.status("Keep a few lessons ready for each unit, make packets on the day, then fill in the teacher log."); }
    std::string title() override { return day_name(app) + "s"; }
    void frame(Ui &ui, const Rect &body) override {
        Skin &k = ui.sk();
        int S = ui.S();
        Value &st = app.settings();
        int allot = homedays::allotted(st);
        ui.begin_scroll(ui.id("homedays"), body, 900 * S);
        Rect in = ui.remaining();
        ui.begin({in.x + 16 * S, in.y + 12 * S, ui.width() - 32 * S, 1e9}, 0);
        ui.flow();
        if (button(ui, "Make Today's Packets\xE2\x80\xA6", true, true))
            app.later([this] { home_day_now(app, today()); });
        if (button(ui, day_name(app) + " Lessons\xE2\x80\xA6")) app.later([this] { home_lessons_dialog(app, ""); });
        if (button(ui, "Teacher Log\xE2\x80\xA6", sel_ >= 0 && sel_ < allot)) app.later([this] { home_day_log(app, sel_ + 1); });
        ui.flow(false);
        ui.space(12 * S);
        // how many lessons each class has ready, unit by unit
        {
            std::vector<std::string> bits;
            for (const auto &c : courses(app)) {
                std::map<std::string, int> per;
                std::vector<std::string> order;
                for (const Value *l : homedays::bank(app.store.data["lessons"], c))
                    if (!homedays::used(*l)) {
                        std::string un = homedays::unit_label(*l);
                        if (!per.count(un)) order.push_back(un);
                        per[un]++;
                    }
                if (order.empty()) continue;
                std::vector<std::string> us;
                for (const auto &un : order) us.push_back(un + " (" + std::to_string(per[un]) + ")");
                bits.push_back(c + ": " + text::join(us, ", "));
            }
            label(ui, "Lessons ready", true);
            ui.space(2 * S);
            para(ui, bits.empty() ? "None yet. Click " + day_name(app) + " Lessons to make some, a few for each unit." : text::join(bits, "\n"));
            ui.space(12 * S);
        }
        // the days used, and their teacher logs
        std::vector<std::vector<std::string>> rows;
        for (int n = 1; n <= allot; n++) {
            const Value *r = homedays::find(app.store.data, n);
            Date d = r ? homedays::date_of(*r) : Date{};
            std::string log;
            if (r) {
                long filled = 0;
                for (const auto &c : r->get("classes").arr()) filled += !text::strip(c.s("participating")).empty();
                log = filled == 0 ? "Not started" : filled == (long)r->get("classes").size() ? "Filled in" : "Started";
            }
            rows.push_back({day_label(app, n), d.valid() ? dates::fmt_date(d, true) : "Not used yet", d.valid() ? log : ""});
        }
        double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
        Rect lr = ui.place(0, std::max(4, std::min(allot, 12)) * rh + hh + 2 * S);
        ListResult res = list_view(ui, "days", lr, {{"Day", 200, false}, {"Date", 240, true}, {"Teacher log", 180, false}}, rows, sel_,
                                   "Type how many days your district allows below.");
        if (res.open && sel_ >= 0 && sel_ < allot) app.later([this] { home_day_log(app, sel_ + 1); });
        ui.space(16 * S);
        begin_group(ui, "Setup");
        double lw = k.f->width("What your district calls them:") + 8 * S;
        ui.row({lw, k.f->width("000") + 30 * S}, ui.ctl_h());
        label_mid(ui, "How many your district allows:");
        std::string &n = ui.state<std::string>(ui.id("allot-text"));
        bool &loaded = ui.state<bool>(ui.id("allot-loaded"));
        if (!loaded) n = std::to_string(allot), loaded = true;
        if (entry(ui, "allot", n)) {
            st["home_days_allotted"] = std::max(0, std::min(60, std::atoi(text::strip(n).c_str())));
            app.save_soon();
        }
        ui.row({lw, k.f->width("Non-Traditional Instruction Day") + 20 * S}, ui.ctl_h());
        label_mid(ui, "What your district calls them:");
        if (!st.get("home_day_name").is_string()) st["home_day_name"] = "";
        if (entry(ui, "name", st["home_day_name"].str_ref())) app.save_soon();
        ui.row({});
        ui.space(6 * S);
        label(ui, "How students reach you (printed on every packet):");
        ui.space(2 * S);
        auto_text(app, ui, "contact", st, "home_day_contact", 2, 6);
        ui.space(2 * S);
        para(ui, "Like: I'm on Google Classroom and email from 9 to 11 and 1 to 2. Turn your work in on Google Classroom, or bring the packet "
                 "the day we're back.",
             true, 0, k.fsmall.get());
        end_group(ui);
        ui.space(16 * S);
        ui.end();
        ui.end_scroll();
    }

private:
    int sel_ = 0;
};

}  // namespace

Factory home_days_page() { return make<HomeDaysPage>(); }

// No school today: move lessons back, or make it an at-home day
void snow_day(App &app) {
    std::string hn = day_name(app);
    int pick = message(app.ui, "Snow Day", "No school today? What kind of day is it?", "ask",
                       {{"No School: Move Lessons", 1}, {hn + ": Make Packets", 2}, {"Cancel", 3}}, 0, 3);
    if (pick == 1) shift_dates(app, "", today(), true);
    else if (pick == 2) home_day_now(app, today());
}

}  // namespace gui
