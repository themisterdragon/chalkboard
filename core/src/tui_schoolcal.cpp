// The terminal app's school calendar (the first and last day, days off, a district .ics file) and
// moving lessons by school days, with a preview first and Undo after.
#include <algorithm>

#include "exporting.h"
#include "files.h"
#include "homedays.h"
#include "text.h"
#include "tui_app.h"

namespace tui {

using json::Value;
using store::field;

static std::string up(const std::string &s) { return text::upper(s); }

void App::school_calendar() {
    auto cal = schoolcal::read(st());
    auto saved = [&] {
        schoolcal::sort(cal.off);
        schoolcal::write(st(), cal);
        save();
    };
    auto pick_day = [&](const std::string &title, Date &d) {
        auto p = ui.calendar(title, d.valid() ? &d : nullptr);
        if (p.kind != UI::Picked::DAYS) return false;
        d = p.first;
        return true;
    };
    ui.list_screen(
        "SCHOOL CALENDAR", [&] { return 2 + (long)cal.off.size(); },
        [&](long i, int) {
            UI::Row r;
            if (i < 2) {
                const Date &d = i == 0 ? cal.first : cal.last;
                r.text = std::string(i == 0 ? "FIRST DAY OF SCHOOL ...... " : "LAST DAY OF SCHOOL ....... ") +
                         (d.valid() ? up(dates::fmt_date(d, true)) : "NOT SET");
                r.has_style = true;
                r.st = ui.HI;
                return r;
            }
            const auto &o = cal.off[(size_t)i - 2];
            r.text = text::ljust(up(dates::fmt_range(o.from, o.to)), 26) + " " + text::ljust(up(o.name), 34) + " " + up(schoolcal::kind_short(o.kind));
            return r;
        },
        [&](long i) {
            if (i < 2) {
                Date &d = i == 0 ? cal.first : cal.last;
                if (pick_day(i == 0 ? "FIRST DAY OF SCHOOL" : "LAST DAY OF SCHOOL", d)) saved();
            } else if (edit_day_off(cal.off[(size_t)i - 2]))
                saved();
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(k >= 0x20 && k < 0x7f ? std::string(1, (char)k) : "");
            if (c == "n") {
                schoolcal::DayOff o;
                o.from = o.to = today();
                o.kind = "holiday";
                if (edit_day_off(o)) {
                    cal.off.push_back(o);
                    saved();
                }
                return Result::none();
            }
            if (c == "d" && i >= 2 && i - 2 < (long)cal.off.size() && ui.confirm("REMOVE " + up(cal.off[(size_t)i - 2].name))) {
                cal.off.erase(cal.off.begin() + (i - 2));
                saved();
                return Result::move(std::max(0L, i - 1));
            }
            if (c == "d" && i < 2) {
                (i == 0 ? cal.first : cal.last) = Date{};
                saved();
                return Result::move(i);
            }
            if (c == "i") {
                std::string path;
                if (!ui.prompt("CALENDAR FILE (.ICS) TO IMPORT", path) || text::strip(path).empty()) return Result::none();
                std::string raw, err;
                if (!files::read(text::strip(path), raw, &err)) {
                    ui.msg = "?" + up(err);
                    return Result::none();
                }
                auto events = schoolcal::read_ics(raw);
                Date f, l;
                bool fl = schoolcal::ics_first_last(events, f, l);
                int added = 0;
                for (const auto &e : events) {
                    if (!e.looks_off || schoolcal::is_first_or_last(e)) continue;
                    bool dup = false;
                    for (const auto &o : cal.off) dup |= o.from == e.day.from && o.to == e.day.to && text::lower(o.name) == text::lower(e.day.name);
                    if (!dup) cal.off.push_back(e.day), added++;
                }
                if (fl) {
                    if (f.valid()) cal.first = f;
                    if (l.valid()) cal.last = l;
                }
                saved();
                ui.msg = "ADDED " + std::to_string(added) + " DAYS OFF" + (fl ? " AND THE FIRST/LAST DAY" : "") + ". D REMOVES ANY THAT AREN'T.";
                return Result::none();
            }
            if (c == "y" && !cal.empty() &&
                ui.confirm("MOVE THE WHOLE CALENDAR FORWARD A YEAR (REPLACES THIS YEAR'S)")) {
                cal = schoolcal::next_year(cal);
                saved();
                ui.msg = "MOVED A YEAR. FIX HOLIDAYS THAT CHANGE DATE (LIKE LABOR DAY).";
                return Result::none();
            }
            if (c == "s") {
                shift_lessons();
                cal = schoolcal::read(st());
                return Result::none();
            }
            if (c == "u") {
                undo_shift();
                return Result::none();
            }
            return Result::none();
        },
        "1-0 CHANGE  N NEW  D REMOVE  I IMPORT .ICS  Y NEXT YEAR  S SHIFT LESSONS  U UNDO SHIFT  ESC BACK",
        [] { return std::string("SHIFTS SKIP WEEKENDS AND THESE DAYS. EARLY RELEASES STILL COUNT AS SCHOOL DAYS."); });
}

bool App::edit_day_off(schoolcal::DayOff &o) {
    std::string name;
    if (!ui.prompt("NAME (E.G. FALL BREAK)", name, o.name)) return false;
    auto p = ui.calendar("DAYS OFF: " + up(name), o.from.valid() ? &o.from : nullptr, o.to.valid() ? &o.to : nullptr, nullptr, true);
    if (p.kind != UI::Picked::DAYS) return false;
    std::vector<std::string> kinds;
    long cur = 0;
    for (size_t i = 0; i < schoolcal::KINDS.size(); i++) {
        kinds.push_back(up(schoolcal::KINDS[i].second));
        if (schoolcal::KINDS[i].first == o.kind) cur = (long)i;
    }
    long k = ui.choose("WHAT KIND OF DAY", kinds, cur);
    if (k < 0) return false;
    o.name = text::strip(name).empty() ? schoolcal::kind_label(schoolcal::KINDS[(size_t)k].first) : text::strip(name);
    o.from = p.first;
    o.to = p.last.valid() ? p.last : p.first;
    o.kind = schoolcal::KINDS[(size_t)k].first;
    return true;
}

void App::shift_lessons(const Date *from0) {
    std::string n_text;
    if (!ui.prompt("MOVE BY HOW MANY SCHOOL DAYS? (-1 = A DAY EARLIER)", n_text, "1")) return;
    int n = std::max(-400, std::min(400, std::atoi(text::strip(n_text).c_str())));
    if (n == 0) return;
    std::vector<const Value *> all;
    for (const auto &x : store_.data.get("lessons").arr())
        if (x.is_object()) all.push_back(&x);
    std::vector<std::string> classes = {"ALL CLASSES"}, keys = {""};
    for (const auto &c : store::classes(st(), all))
        if (!c.first.empty()) classes.push_back(up(c.first)), keys.push_back(c.first);
    long ci = ui.choose("WHICH LESSONS?", classes, 0);
    if (ci < 0) return;
    Date start = from0 ? *from0 : today();
    auto p = ui.calendar("MOVE LESSONS FROM WHICH DAY ON?", &start);
    if (p.kind != UI::Picked::DAYS) return;
    auto cal = schoolcal::read(st());
    auto plan = schoolcal::plan_shift(store_.data.get("lessons"), cal, p.first, n, keys[(size_t)ci]);
    if (plan.empty()) {
        ui.msg = "?NO LESSONS ON OR AFTER " + up(dates::fmt_date(p.first)) + " TO MOVE.";
        return;
    }
    bool go = false;
    ui.list_screen(
        "SHIFT " + std::to_string(plan.size()) + " LESSON" + (plan.size() == 1 ? "" : "S") + (n > 0 ? " LATER" : " EARLIER"),
        [&] { return (long)plan.size(); },
        [&](long i, int) {
            const auto &m = plan[(size_t)i];
            UI::Row r;
            r.text = text::ljust(up(first_line(m.before)), 24) + " > " + text::ljust(up(first_line(m.after)), 24) + " " + up(m.title) +
                     (m.past_last ? "  (AFTER THE LAST DAY!)" : "");
            if (m.past_last) {
                r.has_style = true;
                r.st = ui.HI;
            }
            return r;
        },
        [&](long) { return Result::none(); },
        [&](int k, long) -> Result {
            if (k == 'y' || k == 'Y') {
                go = true;
                return Result::back();
            }
            return Result::none();
        },
        "Y MOVE THEM  ESC CANCEL",
        [&] { return cal.empty() ? std::string("ONLY WEEKENDS ARE SKIPPED: ADD DAYS OFF IN THE SCHOOL CALENDAR.") : std::string("WEEKENDS AND DAYS OFF ARE SKIPPED."); });
    if (!go) return;
    try {
        store_.backup("", false);
    } catch (const store::UserError &e) {
        if (!ui.confirm("NO BACKUP (" + up(e.message) + "). MOVE THEM ANYWAY")) return;
    }
    for (const auto &m : plan)
        if (Value *l = lesson(m.id)) (*l)["date"] = m.after;
    undo_moves_ = plan;
    save();
    ui.msg = "MOVED " + std::to_string(plan.size()) + " LESSONS. SETTINGS > SCHOOL CALENDAR > U PUTS THEM BACK.";
}

void App::undo_shift() {
    if (undo_moves_.empty()) {
        ui.msg = "?NOTHING TO UNDO (UNDO WORKS UNTIL YOU QUIT).";
        return;
    }
    int back = 0;
    for (const auto &m : undo_moves_)
        if (Value *l = lesson(m.id))
            if (field(*l, "date") == m.after) (*l)["date"] = m.before, back++;
    undo_moves_.clear();
    save();
    ui.msg = "PUT " + std::to_string(back) + " LESSONS BACK.";
}

// ----------------------------------------------------------------------------- at-home (NTI) days

void App::home_days() {
    auto name = [&] { return up(store::home_day_name(st())); };
    if (homedays::allotted(st()) <= 0) {
        std::string v;
        if (!ui.prompt("HOW MANY AT-HOME (NTI) DAYS DOES YOUR DISTRICT ALLOW?", v, "10")) return;
        int n = std::max(0, std::min(60, std::atoi(text::strip(v).c_str())));
        if (n <= 0) return;
        st()["home_days_allotted"] = n;
        if (ui.prompt("WHAT DOES YOUR DISTRICT CALL THEM? (E.G. NTI DAY)", v, store::home_day_name(st()))) st()["home_day_name"] = text::strip(v);
        save();
    }
    ui.list_screen(
        name() + "S", [&] { return (long)homedays::allotted(st()); },
        [&](long i, int) {
            const Value *r = homedays::find(store_.data, (int)i + 1);
            Date d = r ? homedays::date_of(*r) : Date{};
            UI::Row row;
            row.text = text::ljust(name() + " " + std::to_string(i + 1), 16) + " " + (d.valid() ? up(dates::fmt_date(d, true)) : "NOT USED YET");
            return row;
        },
        [&](long i) {
            home_log((int)i + 1);
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(k >= 0x20 && k < 0x7f ? std::string(1, (char)k) : "");
            int n = (int)i + 1;
            if (c == "p") {   // the packets for a day: each class's planned lesson, or its lesson that day
                Date start = today();
                auto pk = ui.calendar("PACKETS FOR WHICH DAY?", &start);
                if (pk.kind != UI::Picked::DAYS) return Result::none();
                std::vector<const Value *> all;
                for (const auto &x : store_.data.get("lessons").arr())
                    if (x.is_object()) all.push_back(&x);
                std::vector<const Value *> ls;
                for (const auto &cl : store::classes(st(), all)) {
                    if (cl.first.empty()) continue;
                    const Value *l = homedays::planned(store_.data.get("lessons"), cl.first, n);
                    if (!l)
                        for (const Value *x : all) {
                            auto ds = store::lesson_dates(*x);
                            if (x->get("home_slot").as_int() == 0 && store::in_class(*x, cl.first) && std::find(ds.begin(), ds.end(), pk.first) != ds.end()) l = x;
                        }
                    if (l) ls.push_back(l);
                }
                try {
                    auto files = exporting::export_home_day(store_, pk.first, ls);
                    Value &rec = homedays::make(store_.data, n);
                    rec["date"] = pk.first.iso();
                    save();
                    export_done(files);
                } catch (const exporting::ExportError &e) {
                    ui.msg = "?" + e.message;
                }
                return Result::move(i);
            }
            if (c == "e") {
                const Value *r = homedays::find(store_.data, n);
                if (!r) {
                    ui.msg = "?NOTHING LOGGED FOR THAT DAY YET.";
                    return Result::none();
                }
                try {
                    export_done(exporting::export_home_log(store_, *r));
                } catch (const exporting::ExportError &e) {
                    ui.msg = "?" + e.message;
                }
                return Result::move(i);
            }
            return Result::none();
        },
        "1-0 TEACHER LOG  P MAKE PACKETS  E EXPORT LOG  ESC BACK",
        [&] { return std::string("PLAN EACH DAY'S LESSONS AHEAD IN THE WINDOW APP (") + name() + "S PAGE)."; });
}

void App::home_log(int n) {
    Value &rec0 = homedays::make(store_.data, n);
    if (rec0.get("classes").size() == 0)
        for (const auto &c : homedays::log_classes(st(), store_.data.get("lessons"))) {
            Value row = Value::object();
            row["period"] = c.first;
            row["course"] = c.second;
            for (const char *k : {"enrolled", "participating", "technology", "paper"}) row[k] = "";
            rec0["classes"].push(row);
        }
    auto rec = [&]() -> Value & { return homedays::make(store_.data, n); };
    ui.list_screen(
        up(store::home_day_name(st())) + " " + std::to_string(n) + " TEACHER LOG",
        [&] { return (long)(rec()["classes"].size() + rec()["contacts"].size()); },
        [&](long i, int) {
            UI::Row row;
            size_t nc = rec()["classes"].size();
            if ((size_t)i < nc) {
                const Value &c = rec()["classes"].at((size_t)i);
                long a = homedays::count(c, "enrolled"), b = homedays::count(c, "participating");
                row.text = text::ljust(up(c.s("period")), 14) + " A " + text::ljust(c.s("enrolled"), 4) + " B " + text::ljust(c.s("participating"), 4) +
                           " C " + text::ljust(c.s("participating").empty() ? "" : homedays::percent(b, a), 5) + " D " + text::ljust(c.s("technology"), 4) +
                           " E " + c.s("paper");
            } else {
                const Value &c = rec()["contacts"].at((size_t)i - nc);
                row.text = "CONTACT: " + text::ljust(up(c.s("name")), 26) + " " + text::ljust(up(c.s("time")), 9) + " " + up(c.s("mode"));
            }
            return row;
        },
        [&](long i) {
            size_t nc = rec()["classes"].size();
            if ((size_t)i < nc) {
                for (auto kv : std::vector<std::pair<const char *, const char *>>{{"enrolled", "A. STUDENTS IN CLASS"},
                                                                                   {"participating", "B. STUDENTS WHO TOOK PART"},
                                                                                   {"technology", "D. USED TECHNOLOGY"},
                                                                                   {"paper", "E. USED A BOOK OR PAPER PACKET"}}) {
                    std::string v;
                    Value &c = rec()["classes"].at((size_t)i);
                    if (!ui.prompt(up(c.s("period")) + ": " + kv.second, v, c.s(kv.first))) break;
                    std::string digits;
                    for (char ch : v)
                        if (ch >= '0' && ch <= '9') digits += ch;
                    rec()["classes"].at((size_t)i)[kv.first] = digits.substr(0, 5);
                }
                save();
            }
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(k >= 0x20 && k < 0x7f ? std::string(1, (char)k) : "");
            size_t nc = rec()["classes"].size();
            if (c == "n") {
                Value x = Value::object();
                std::string v;
                if (!ui.prompt("NAME", v)) return Result::none();
                x["name"] = text::strip(v);
                std::string t = strftime_now("%I:%M %p");
                if (!t.empty() && t[0] == '0') t = t.substr(1);
                if (!ui.prompt("TIME", v, t)) return Result::none();
                x["time"] = text::strip(v);
                std::vector<std::string> modes;
                for (const auto &m : homedays::MODES) modes.push_back(up(m));
                long j = ui.choose("MODE OF COMMUNICATION", modes, 0);
                if (j < 0) return Result::none();
                x["mode"] = homedays::MODES[(size_t)j];
                if (!rec()["contacts"].is_array()) rec()["contacts"] = Value::array();
                rec()["contacts"].push(x);
                save();
                return Result::move((long)(nc + rec()["contacts"].size() - 1));
            }
            if (c == "d" && (size_t)i >= nc && ui.confirm("REMOVE " + up(rec()["contacts"].at((size_t)i - nc).s("name")))) {
                auto &a = rec()["contacts"].arr_ref();
                a.erase(a.begin() + (i - (long)nc));
                save();
                return Result::move(std::max(0L, i - 1));
            }
            if (c == "e") {
                try {
                    export_done(exporting::export_home_log(store_, rec()));
                } catch (const exporting::ExportError &e) {
                    ui.msg = "?" + e.message;
                }
            }
            return Result::none();
        },
        "1-0 TYPE A/B/D/E  N ADD CONTACT  D REMOVE CONTACT  E EXPORT LOG  ESC BACK",
        [] { return std::string("A IN CLASS  B TOOK PART  C = B / A x 100  D TECHNOLOGY  E BOOK OR PAPER"); });
}

}  // namespace tui
