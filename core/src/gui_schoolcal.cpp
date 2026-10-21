// The school calendar (Settings): the first and last day of school and the days off, typed in or
// read from the district's calendar file (.ics). And moving lessons by school days: Shift Dates
// and Snow Day, which skip weekends and days off, with Undo Shift to put them back.
#include <algorithm>
#include <cmath>
#include <set>

#include "files.h"
#include "gui.h"
#include "schoolcal.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

std::string range_text(const Date &a, const Date &b) { return dates::fmt_range(a, b); }

// one day off: its name, its days, and what kind it is
bool edit_day_off(App &app, schoolcal::DayOff &o, const std::string &title) {
    std::string name = o.name, kind = o.kind.empty() ? "holiday" : o.kind;
    Date from = o.from.valid() ? o.from : today(), to = o.to.valid() ? o.to : from;
    bool first = true;
    int r = dialog(
        app.ui, {title, 480, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double lw = k.f->width("Name:") + 8 * S;
            if (first) {
                focus_at_end(u, u.id("entry:name"));
                first = false;
            }
            u.row({lw, -1}, u.ctl_h());
            label_mid(u, "Name:");
            entry(u, "name", name);
            u.row({lw, 0, 0}, u.ctl_h());
            label_mid(u, "Days:");
            label_mid(u, range_text(from, to) + "   ", true);
            if (button(u, "Change\xE2\x80\xA6", true, false, true)) {
                Date a, b;
                if (pick_dates(app, "Days: " + (name.empty() ? std::string("Day Off") : name), &from, &to, a, b)) from = a, to = b;
            }
            u.row({});
            u.space(8 * S);
            label(u, "What kind of day is it?");
            u.space(2 * S);
            for (const auto &kd : schoolcal::KINDS)
                if (radio(u, kd.second, kind == kd.first)) kind = kd.first;
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    o.name = text::strip(name).empty() ? schoolcal::kind_label(kind) : text::strip(name);
    o.kind = kind;
    o.from = std::min(from, to);
    o.to = from < to ? to : from;
    return true;
}

// the all-day events in a district calendar file, to check the days off among them; true when
// the calendar changed
bool import_ics(App &app, schoolcal::Calendar &cal) {
    std::string path = app.win.open_file("Import a school calendar", {{"Calendar files", {"*.ics", "*.ICS"}}, {"All files", {"*"}}});
    if (path.empty()) return false;
    std::string raw, err;
    if (!files::read(path, raw, &err)) {
        alert(app.ui, "School Calendar", "Couldn't read that file.\n" + err, "warn");
        return false;
    }
    auto all = schoolcal::read_ics(raw);
    std::vector<schoolcal::Event> events;   // (the first and last day are school days: offered below instead)
    for (const auto &e : all)
        if (!schoolcal::is_first_or_last(e)) events.push_back(e);
    Date f, l;
    bool has_fl = schoolcal::ics_first_last(all, f, l);
    if (events.empty() && !has_fl) {
        alert(app.ui, "School Calendar",
              "That file has no all-day events in it. Calendar files from a school district usually list breaks and holidays as all-day "
              "events; this one may only have events with times.",
              "warn");
        return false;
    }
    std::vector<bool> on;
    for (const auto &e : events) on.push_back(e.looks_off);
    bool use_fl = has_fl;
    int sel = 0;
    bool first = true;
    int r = dialog(
        app.ui, {"Import School Calendar", 680, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Check the days with no class (or an early release). Chalkboard guessed from their names; Space or a click on the box checks "
                    "or unchecks one.");
            u.space(8 * S);
            std::vector<std::vector<std::string>> rows;
            for (size_t i = 0; i < events.size(); i++)
                rows.push_back({on[i] ? "x" : "o", range_text(events[i].day.from, events[i].day.to), events[i].day.name,
                                schoolcal::kind_short(events[i].day.kind)});
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 9 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:ics"));
                first = false;
            }
            ListResult res = list_view(u, "ics", lr, {{"", 26, false, MARK_CHECK}, {"Days", 170, false}, {"Name", 260, true}, {"Kind", 170, false}},
                                       rows, sel);
            if (res.open && res.col < 0) return 1;   // Return: Add Checked Days
            if (sel >= 0 && sel < (int)on.size() && (res.col == 0 || res.space || res.open)) {
                on[(size_t)sel] = !on[(size_t)sel];
                u.again();
            }
            u.space(8 * S);
            u.flow();
            if (button(u, "Check All", true, false, true)) std::fill(on.begin(), on.end(), true), u.again();
            if (button(u, "Check None", true, false, true)) std::fill(on.begin(), on.end(), false), u.again();
            u.flow(false);
            if (has_fl) {
                u.space(6 * S);
                std::string what = std::string(f.valid() ? "first day " + dates::fmt_date(f) : "") + (f.valid() && l.valid() ? " and " : "") +
                                   (l.valid() ? "last day " + dates::fmt_date(l) : "");
                check(u, "Use the " + what + " from this file", use_fl);
            }
            return 0;
        },
        {{"Add Checked Days", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    int added = 0;
    for (size_t i = 0; i < events.size(); i++) {
        if (!on[i]) continue;
        const auto &d = events[i].day;
        bool dup = false;
        for (const auto &o : cal.off) dup |= o.from == d.from && o.to == d.to && text::lower(o.name) == text::lower(d.name);
        if (dup) continue;
        cal.off.push_back(d);
        added++;
    }
    if (use_fl) {
        if (f.valid()) cal.first = f;
        if (l.valid()) cal.last = l;
    }
    schoolcal::sort(cal.off);
    app.status("Added " + plural(added, "day off", "days off") + " from the calendar file.");
    return added > 0 || use_fl;
}

// the classes lessons are in (for "which lessons")
std::vector<std::pair<std::string, std::string>> class_options(App &app) {
    std::vector<const Value *> all;
    for (const auto &x : app.store.data["lessons"].arr())
        if (x.is_object()) all.push_back(&x);
    std::vector<std::pair<std::string, std::string>> out = {{"", "All classes"}};
    for (const auto &c : store::classes(app.settings(), all))
        if (!c.first.empty()) out.push_back({c.first, c.first});
    return out;
}

}  // namespace

std::string school_calendar_text(App &app) {
    auto cal = schoolcal::read(app.settings());
    if (cal.empty())
        return "Not set up yet. Add your breaks and holidays, and Chalkboard shades them on the Calendar and skips them when it moves lessons.";
    std::string year = cal.first.valid() && cal.last.valid() ? "School year " + range_text(cal.first, cal.last)
                       : cal.first.valid()                   ? "First day " + dates::fmt_date(cal.first)
                       : cal.last.valid()                    ? "Last day " + dates::fmt_date(cal.last)
                                                             : "No first or last day yet";
    // weekdays with no class, and early releases (a five-day break counts five)
    long off = 0, early = 0, home = 0;
    for (const auto &o : cal.off)
        for (Date d = o.from; d <= o.to; d = d.plus(1))
            if (d.weekday() < 5) (o.kind == "early" ? early : o.kind == "athome" ? home : off)++;
    std::string hn = store::home_day_name(app.settings());
    return year + ": " + plural(off, "day off", "days off") + (early ? ", " + plural(early, "early release") : "") +
           (home ? ", " + plural(home, hn) : "") + ".";
}

void school_calendar_dialog(App &app) {
    auto cal = schoolcal::read(app.settings());
    int sel = -1;
    bool first = true;
    auto saved = [&] {
        schoolcal::sort(cal.off);
        schoolcal::write(app.settings(), cal);
        app.save();
        app.ui.again();
    };
    dialog(
        app.ui, {"School Calendar", 680, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Your district's first and last day, breaks, holidays, and teacher days. Chalkboard shades them on the Calendar and "
                    "Today, warns when a lesson lands on one, and skips them when it moves lessons (Shift Dates, Snow Day).");
            u.space(10 * S);
            double lw = k.f->width("First day of school:") + 8 * S, dw = k.fb->width("Wed, Sep 30, 2026") + 16 * S;
            for (int which = 0; which < 2; which++) {
                Date &d = which == 0 ? cal.first : cal.last;
                u.row({lw, dw, 0, 0}, u.ctl_h());
                label_mid(u, which == 0 ? "First day of school:" : "Last day of school:");
                label_mid(u, (d.valid() ? dates::fmt_date(d, true) : std::string("Not set")) + "   ", d.valid());
                u.push_id(which == 0 ? "first" : "last");
                if (button(u, "Change\xE2\x80\xA6", true, false, true)) {
                    Date a, b;
                    if (pick_dates(app, which == 0 ? "First Day of School" : "Last Day of School", d.valid() ? &d : nullptr, nullptr, a, b)) {
                        d = a;
                        saved();
                    }
                }
                if (d.valid() && button(u, "Clear", true, false, true)) {
                    d = Date{};
                    saved();
                }
                u.pop_id();
            }
            u.row({});
            u.space(10 * S);
            label(u, "Days off and early releases:", true);
            u.space(4 * S);
            std::vector<std::vector<std::string>> rows;
            for (const auto &o : cal.off) rows.push_back({range_text(o.from, o.to), o.name, schoolcal::kind_short(o.kind)});
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 8 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:days-off"));
                first = false;
            }
            ListResult res = list_view(u, "days-off", lr, {{"Days", 190, false}, {"Name", 250, true}, {"Kind", 200, false}}, rows, sel,
                                       "None yet. Click Add, or import your district's calendar file.");
            auto add = [&] {
                schoolcal::DayOff o;
                o.kind = "holiday";
                if (!edit_day_off(app, o, "Add a Day Off")) return;
                cal.off.push_back(o);
                saved();
                for (size_t i = 0; i < cal.off.size(); i++)
                    if (cal.off[i].from == o.from && cal.off[i].name == o.name) sel = (int)i;
            };
            auto edit = [&] {
                if (sel < 0 || sel >= (int)cal.off.size()) return;
                schoolcal::DayOff o = cal.off[(size_t)sel];
                if (!edit_day_off(app, o, "Day Off")) return;
                cal.off[(size_t)sel] = o;
                saved();
            };
            auto remove = [&] {
                if (sel < 0 || sel >= (int)cal.off.size()) return;
                if (!confirm(u, "Remove Day Off", "Remove " + cal.off[(size_t)sel].name + "?", "Remove")) return;
                cal.off.erase(cal.off.begin() + sel);
                sel = std::min(sel, (int)cal.off.size() - 1);
                saved();
            };
            if (res.open) edit();
            if (res.del) remove();
            u.space(8 * S);
            u.flow();
            if (button(u, "Add\xE2\x80\xA6", true, false, true)) add();
            if (button(u, "Edit\xE2\x80\xA6", sel >= 0, false, true)) edit();
            if (button(u, "Remove", sel >= 0, false, true)) remove();
            if (button(u, "Import Calendar File\xE2\x80\xA6", true, false, true) && import_ics(app, cal)) saved();
            if (button(u, "Copy to Next Year\xE2\x80\xA6", !cal.empty(), false, true) &&
                confirm(u, "Copy to Next Year",
                        "Move every day in your school calendar forward one year, for next year? This replaces this year's calendar. Holidays "
                        "that fall on a different date next year (like Labor Day) need fixing afterward.",
                        "Copy to Next Year")) {
                cal = schoolcal::next_year(cal);
                saved();
            }
            u.flow(false);
            return 0;
        },
        {{"Done", 1}}, 0, 1);
    app.save();
}

// ----------------------------------------------------------------------------- shifting lessons

bool shift_dates(App &app, const std::string &course0, const Date &from0, bool snow) {
    std::string course = course0, n_text = "1";
    Date from = from0.valid() ? from0 : today();
    bool later = true, mark_off = snow, first = true;
    auto cal_saved = schoolcal::read(app.settings());
    std::string key;   // what the preview was worked out for
    std::vector<schoolcal::Move> plan;
    int sel = -1;
    auto classes = class_options(app);
    auto cal_for = [&] {   // with today as a day off, when Snow Day says so
        auto c = cal_saved;
        if (snow && mark_off && !c.day_off(from)) c.off.push_back({from, from, "Snow day", "holiday"});
        return c;
    };
    int r = dialog(
        app.ui, {snow ? "Snow Day" : "Shift Lesson Dates", 860, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            if (snow)
                para(u, "No school today? Every lesson from today on moves back one school day, past weekends and days off, so nothing is "
                        "lost. Edit \xE2\x80\xBA Undo Shift puts them back.");
            else
                para(u, "Move lessons later or earlier by school days: weekends and the days off in your school calendar are skipped, and a "
                        "lesson over several days keeps its length. Edit \xE2\x80\xBA Undo Shift puts them back.");
            u.space(10 * S);
            double lw = k.f->width("Starting from:") + 8 * S;
            u.row({lw, k.f->width("000") + 24 * S, 0, 0, 0}, u.ctl_h());
            label_mid(u, "Move by:");
            entry(u, "n", n_text);
            label_mid(u, " school day(s)   ");
            if (radio(u, "Later", later)) later = true;
            if (radio(u, "Earlier", !later)) later = false;
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Which lessons:");
            dropdown(u, "class", classes, course, 24);
            u.row({lw, 0, 0}, u.ctl_h());
            label_mid(u, "Starting from:");
            label_mid(u, dates::fmt_date(from, true) + " (and after)   ", true);
            if (button(u, "Change\xE2\x80\xA6", true, false, true)) {
                Date a, b;
                if (pick_dates(app, "Shift Lessons Starting From", &from, nullptr, a, b)) from = a;
            }
            u.row({});
            if (snow) {
                u.space(4 * S);
                check(u, "Also mark " + dates::fmt_date(from, true, false) + " as a day off (a snow day) in the school calendar", mark_off);
            }
            // the preview
            int n = std::max(0, std::min(400, std::atoi(text::strip(n_text).c_str())));
            int signed_n = later ? n : -n;
            std::string now_key = std::to_string(signed_n) + "|" + course + "|" + from.iso() + "|" + (mark_off ? "1" : "0");
            if (now_key != key) {
                key = now_key;
                plan = schoolcal::plan_shift(app.store.data["lessons"], cal_for(), from, signed_n, course);
                sel = plan.empty() ? -1 : 0;
            }
            u.space(10 * S);
            int past = 0;
            for (const auto &m : plan) past += m.past_last;
            std::string head = plan.empty()        ? (n == 0 ? "Type how many school days to move them." : "No lessons on or after that day to move.")
                               : plan.size() == 1 ? "This lesson moves:"
                                                  : std::to_string(plan.size()) + " lessons move:";
            label(u, head, true);
            u.space(4 * S);
            std::vector<std::vector<std::string>> rows;
            for (const auto &m : plan)
                rows.push_back({m.title.empty() ? "(untitled lesson)" : m.title, m.course, first_line(m.before),
                                first_line(m.after) + (m.past_last ? "  (after the last day!)" : "")});
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 8 * rh + hh + 2 * S);
            if (first) {
                focus_at_end(u, u.id("entry:n"));
                first = false;
            }
            list_view(u, "shift", lr, {{"Lesson", 260, true}, {"Class", 120, false}, {"Now", 160, false}, {"Moves to", 220, false}}, rows, sel);
            if (past) {
                u.space(6 * S);
                para(u, "Heads up: " + plural(past, "lesson lands", "lessons land") + " after the last day of school (" +
                            dates::fmt_date(cal_saved.last) + "). You can move or cut " + (past == 1 ? "it" : "them") + " afterward.");
            }
            if (cal_saved.empty()) {
                u.space(6 * S);
                para(u, "Tip: add your breaks and holidays in Settings \xE2\x80\xBA School Calendar, and shifts skip them too. For now only weekends "
                        "are skipped.",
                     true, 0, k.fsmall.get());
            }
            return 0;
        },
        {{snow ? "Move Lessons" : "Shift", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    if (snow && mark_off) {
        auto c = cal_for();
        schoolcal::write(app.settings(), c);
    }
    if (plan.empty()) {
        app.save();
        return false;
    }
    // a backup first (in the backups folder), then the move
    try {
        app.store.backup("", false);
    } catch (const store::UserError &e) {
        if (!confirm(app.ui, "Shift Lesson Dates", "Chalkboard couldn't make a backup first:\n" + e.message + "\n\nMove the lessons anyway?", "Move Them",
                     "Cancel", "warn"))
            return false;
    }
    for (const auto &m : plan) {
        Value *l = app.lesson(m.id);
        if (!l) continue;
        (*l)["date"] = m.after;   // (not "updated": moving a day isn't editing it, and Recent stays useful)
    }
    app.undo_moves = plan;
    app.save();
    app.status("Moved " + plural((long long)plan.size(), "lesson") + ". Edit \xE2\x80\xBA Undo Shift puts " + (plan.size() == 1 ? "it" : "them") +
               " back.");
    return true;
}

void undo_shift(App &app) {
    if (app.undo_moves.empty()) return;
    int back = 0, kept = 0;
    for (const auto &m : app.undo_moves) {
        Value *l = app.lesson(m.id);
        if (!l) continue;
        if (store::field(*l, "date") != m.after) {   // changed by hand since: left alone
            kept++;
            continue;
        }
        (*l)["date"] = m.before;
        back++;
    }
    app.undo_moves.clear();
    app.save();
    app.status("Put " + plural(back, "lesson") + " back on " + (back == 1 ? "its day" : "their days") + "." +
               (kept ? " " + plural(kept, "lesson") + " you changed since stayed as " + (kept == 1 ? "it is." : "they are.") : ""));
}

std::string no_school(App &app, const Date &d) { return schoolcal::read(app.settings()).no_school(d); }

}  // namespace gui
