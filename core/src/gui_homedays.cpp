// Days of school at home (NTI days, or whatever the district calls them): the page that lists the
// days a district allows, planning a lesson for each one ahead of time, writing a lesson's at-home
// version (explained in writing, the work cut down), making each class's packet on the day, and
// the teacher log the district asks for afterward.
#include <algorithm>
#include <cmath>

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

bool home_version_dialog(App &app, const std::string &id) {
    Value *l = app.lesson(id);
    if (!l) return false;
    std::string explain = store::field(*l, "nti_explain"), work = store::field(*l, "nti_work");
    std::string title = store::field(*l, "title");
    bool first = true;
    int r = dialog(
        app.ui, {day_name(app) + " Version: " + (title.empty() ? std::string("Untitled Lesson") : title), 720, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Value *x = app.lesson(id);
            if (!x) return 2;
            if (first) {
                u.focus(u.id("text:explain"));
                first = false;
            }
            label(u, "Explain it in writing", true);
            para(u, "Students can't ask you today, so teach it here: what we're learning and why, with one short example. A few sentences.", true, 0,
                 k.fsmall.get());
            u.space(4 * S);
            text_area(u, "explain", explain, 4, 10);
            if (text::strip(explain).empty() && !text::strip(store::field(*x, "instruction")).empty()) {
                u.space(4 * S);
                if (button(u, "Start From the Lesson's I Do", true, false, true)) explain = store::field(*x, "instruction");
            }
            u.space(12 * S);
            label(u, "What students do", true);
            para(u, "One step per line, in order. Cut it down to what matters most: less than a class period's worth (about 20 to 30 minutes). "
                    "Students won't see a time.",
                 true, 0, k.fsmall.get());
            u.space(4 * S);
            text_area(u, "work", work, 4, 10, false);
            if (text::strip(work).empty() && (!text::strip(store::field(*x, "guided")).empty() || !text::strip(store::field(*x, "independent")).empty())) {
                u.space(4 * S);
                if (button(u, "Start From We Do and You Do", true, false, true))
                    work = text::strip(store::field(*x, "guided") + "\n" + store::field(*x, "independent"));
            }
            u.space(8 * S);
            para(u, "Worksheets linked to this lesson print with the packet. Quizzes and tests wait until you're back.", true, 0, k.fsmall.get());
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    l = app.lesson(id);
    if (!l) return false;
    (*l)["nti_explain"] = explain;
    (*l)["nti_work"] = work;
    app.touch(*l);
    return true;
}

// ----------------------------------------------------------------------------- planning ahead

void plan_home_day(App &app, int n) {
    int sel = 0;
    bool first = true;
    std::string open_id;
    dialog(
        app.ui, {"Plan " + day_label(app, n), 720, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "A lesson for each class, ready before the day comes. When " + day_label(app, n) +
                        " happens, each class's packet is made from its lesson here. Keep it short, and explain it in writing.");
            u.space(10 * S);
            auto cs = courses(app);
            std::vector<std::vector<std::string>> rows;
            for (const auto &c : cs) {
                const Value *l = homedays::planned(app.store.data["lessons"], c, n);
                rows.push_back({c, l ? (store::field(*l, "title").empty() ? "(untitled lesson)" : store::field(*l, "title")) : "Not planned yet",
                                !l ? "" : explained(*l) ? "Explained" : "Needs a written explanation"});
            }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 7 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:plan"));
                first = false;
            }
            ListResult res = list_view(u, "plan", lr, {{"Class", 170, false}, {"Lesson", 300, true}, {"Ready?", 210, false}}, rows, sel,
                                       "No classes yet. Plan a lesson with a course first.");
            std::string course = sel >= 0 && sel < (int)cs.size() ? cs[(size_t)sel] : "";
            Value *l = course.empty() ? nullptr : homedays::planned(app.store.data["lessons"], course, n);
            std::string lid = l ? l->s("id") : "";
            auto make_new = [&](const Value *from) {
                Value x = from ? *from : store::new_lesson(app.settings());
                x["id"] = store::new_id();
                x["title"] = from ? store::field(*from, "title") : day_label(app, n);
                x["course"] = course;
                x["date"] = "";
                x["home_slot"] = n;
                x["created"] = store::now();
                x["updated"] = store::now();
                x["assessments"] = from ? from->get("assessments") : Value::array();
                std::string id = x.s("id");
                app.store.data["lessons"].push(std::move(x));
                app.save();
                home_version_dialog(app, id);
                u.again();
            };
            if (res.open) {
                if (l) home_version_dialog(app, lid);
                else if (!course.empty()) make_new(nullptr);
            }
            u.space(8 * S);
            u.flow();
            if (button(u, "New Lesson", !course.empty() && !l, false, true)) make_new(nullptr);
            if (button(u, "Copy a Lesson\xE2\x80\xA6", !course.empty() && !l, false, true)) {
                std::vector<std::pair<std::string, std::string>> opts;
                for (const auto &x : app.store.data["lessons"].arr())
                    if (x.is_object() && store::in_class(x, course) && x.get("home_slot").as_int() == 0)
                        opts.push_back({x.s("id"), store::field(x, "title") + "   (" + store::field(x, "date") + ")"});
                std::string pick = opts.empty() ? "" : choose(u, "Copy a Lesson", "Start " + day_label(app, n) + " from a copy of which lesson?", opts);
                if (opts.empty()) alert(u, "Copy a Lesson", "No " + course + " lessons to copy yet.");
                if (Value *from = app.lesson(pick)) {
                    Value copy = *from;
                    make_new(&copy);
                }
            }
            if (button(u, "Write the " + day_name(app) + " Version\xE2\x80\xA6", l != nullptr, false, true)) home_version_dialog(app, lid);
            if (button(u, "Open Lesson", l != nullptr, false, true)) {
                open_id = lid;
                return 1;
            }
            if (button(u, "Remove", l != nullptr, false, true) &&
                confirm(u, "Remove", "Take this lesson off " + day_label(app, n) + "? The lesson itself stays in your Lesson Plans.", "Remove")) {
                if (Value *x = app.lesson(lid)) {
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
    const Value *used = homedays::on_date(app.store.data, day);
    int n = used ? (int)used->get("number").as_int() : homedays::next_unused(app.store.data, app.settings());
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
    struct Pick {
        std::string course, lesson_id;
        bool planned = false, replaces = false;
    };
    std::vector<Pick> picks;
    auto work_out = [&] {
        picks.clear();
        int num = std::atoi(n_key.c_str());
        for (const auto &c : courses(app)) {
            Pick p;
            p.course = c;
            std::string regular = lesson_on(app, c, day);
            if (const Value *l = homedays::planned(app.store.data["lessons"], c, num)) {
                p.lesson_id = l->s("id");
                p.planned = true;
                p.replaces = !regular.empty();
            } else
                p.lesson_id = regular;
            picks.push_back(p);
        }
    };
    int r = dialog(
        app.ui, {day_name(app) + " Packets", 760, 0},
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
            para(u, "A packet for each class: its lesson explained in writing, a short list of what to do, the worksheets, and how to reach you. "
                    "Double-click a class to write or fix its at-home version.");
            u.space(8 * S);
            std::vector<std::vector<std::string>> rows;
            int missing = 0, unexplained = 0;
            for (const auto &p : picks) {
                const Value *l = app.lesson(p.lesson_id);
                std::string t = l ? store::field(*l, "title") : "";
                std::string from = !l                                                      ? "Nothing planned: no packet"
                                   : p.planned && (t.empty() || t == day_label(app, std::atoi(n_key.c_str()))) ? "Planned lesson"
                                   : p.planned                                                ? "Planned lesson: " + t
                                                                                              : "Today's lesson: " + (t.empty() ? "(untitled)" : t);
                missing += !l;
                unexplained += l && !explained(*l);
                rows.push_back({p.course, from, !l ? "" : explained(*l) ? "Explained" : "No written explanation yet"});
            }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 6 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:packets"));
                first = false;
            }
            ListResult res = list_view(u, "packets", lr, {{"Class", 160, false}, {"Packet from", 360, true}, {"Ready?", 200, false}}, rows, sel);
            const Pick *p = sel >= 0 && sel < (int)picks.size() ? &picks[(size_t)sel] : nullptr;
            if (res.open && res.col < 0) return 1;   // Return: Make Packets (a double-click edits the class's version)
            if (res.open && p && !p->lesson_id.empty()) home_version_dialog(app, p->lesson_id);
            u.space(6 * S);
            u.flow();
            if (button(u, "Write the " + day_name(app) + " Version\xE2\x80\xA6", p && !p->lesson_id.empty(), false, true)) home_version_dialog(app, p->lesson_id);
            if (button(u, "Plan " + day_label(app, std::atoi(n_key.c_str())) + "\xE2\x80\xA6", true, false, true)) plan_home_day(app, std::atoi(n_key.c_str()));
            u.flow(false);
            if (unexplained || missing) {
                u.space(6 * S);
                std::string t = unexplained ? plural(unexplained, "packet has", "packets have") +
                                                  " no written explanation, so students get the steps only. A few sentences make a big difference."
                                            : "";
                if (missing) t += std::string(t.empty() ? "" : " ") + plural(missing, "class has", "classes have") + " no lesson for this day.";
                para(u, "Heads up: " + t);
            }
            bool replacing = false;
            for (const auto &q : picks) replacing |= q.replaces;
            if (replacing) {
                u.space(6 * S);
                check(u, "Move the regular lessons planned for this day (and after) back one school day, in the classes using a planned " +
                             day_name(app) + " lesson",
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
        alert(app.ui, day_name(app) + " Packets", "No class has a lesson for this day yet. Plan one first.");
        return false;
    }
    // the day on the school calendar (a school day), the regular lessons moved, the planned ones dated
    auto cal = schoolcal::read(app.settings());
    if (!cal.day_off(day)) {
        cal.off.push_back({day, day, day_label(app, num), "athome"});
        schoolcal::write(app.settings(), cal);
    }
    std::vector<schoolcal::Move> moved;
    for (const auto &p : picks)
        if (p.planned && p.replaces && move_regular) {
            auto plan = schoolcal::plan_shift(app.store.data["lessons"], cal, day, 1, p.course);
            for (const auto &m : plan)
                if (Value *l = app.lesson(m.id)) (*l)["date"] = m.after;
            moved.insert(moved.end(), plan.begin(), plan.end());
        }
    if (!moved.empty()) app.undo_moves = moved;
    for (const auto &p : picks)
        if (p.planned)
            if (Value *l = app.lesson(p.lesson_id)) (*l)["date"] = dates::fmt_date(day, true);
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
    app.status("Packets made. After the day, fill in the teacher log on the " + day_name(app) + "s page." +
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
    explicit HomeDaysPage(App &a) : Screen(a) { a.status("Plan ahead, make packets on the day, then fill in the teacher log."); }
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
        if (button(ui, "Plan Lessons\xE2\x80\xA6", sel_ >= 0 && sel_ < allot)) app.later([this] { plan_home_day(app, sel_ + 1); });
        if (button(ui, "Teacher Log\xE2\x80\xA6", sel_ >= 0 && sel_ < allot)) app.later([this] { home_day_log(app, sel_ + 1); });
        ui.flow(false);
        ui.space(12 * S);
        // the days, and how far along each one is
        auto cs = courses(app);
        std::vector<std::vector<std::string>> rows;
        for (int n = 1; n <= allot; n++) {
            int planned = 0, ready = 0;
            for (const auto &c : cs)
                if (const Value *l = homedays::planned(app.store.data["lessons"], c, n)) planned++, ready += explained(*l);
            const Value *r = homedays::find(app.store.data, n);
            Date d = r ? homedays::date_of(*r) : Date{};
            std::string log;
            if (r) {
                long filled = 0;
                for (const auto &c : r->get("classes").arr()) filled += !text::strip(c.s("participating")).empty();
                log = filled == 0 ? "Not started" : filled == (long)r->get("classes").size() ? "Filled in" : "Started";
            }
            rows.push_back({day_label(app, n), d.valid() ? dates::fmt_date(d, true) : "Not used yet",
                            std::to_string(planned) + " of " + std::to_string(cs.size()) + " classes" +
                                (planned ? ", " + std::to_string(ready) + " explained" : ""),
                            d.valid() ? log : ""});
        }
        double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
        Rect lr = ui.place(0, std::max(4, std::min(allot, 12)) * rh + hh + 2 * S);
        ListResult res = list_view(ui, "days", lr, {{"Day", 170, false}, {"Date", 200, false}, {"Lessons planned", 300, true}, {"Teacher log", 150, false}},
                                   rows, sel_, "Type how many days your district allows below.");
        if (res.open && sel_ >= 0 && sel_ < allot) {
            const Value *r = homedays::find(app.store.data, sel_ + 1);
            bool used = r && homedays::date_of(*r).valid();
            app.later([this, used] { used ? home_day_log(app, sel_ + 1) : plan_home_day(app, sel_ + 1); });
        }
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
