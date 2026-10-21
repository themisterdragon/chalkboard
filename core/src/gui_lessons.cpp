// Lesson Plans (port of gui/lessons.py): the list and the lesson editor.
#include <algorithm>
#include <cmath>
#include <set>

#include "doc.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

const std::vector<std::pair<std::string, std::vector<std::string>>> GROUPS = {
    {"Lesson", {"title", "unit", "course", "date", "duration"}},
    {"Standards", {"standards"}},
    {"Goals", {"question", "targets", "success", "vocab"}},
    {"Getting Started", {"materials", "bell_ringer"}},
    {"Instruction", {"instruction", "guided", "independent"}},
    {"Wrapping Up", {"closure", "checks", "differentiation"}},
    {"Assessments & Worksheets", {"assessments"}},
    {"Homework & Notes", {"homework", "notes"}},
};

std::string hint(const std::string &key) {
    if (key == "question") return "The one question students can answer by the end.";
    if (key == "vocab") return std::string(store::VOCAB_HINT) + ".";
    if (key == "targets") return "One per line. \xE2\x80\x9C- \xE2\x80\x9D starts a bullet.";
    if (key == "bell_ringer") return "No warm-up planned? Click Random Good Thing.";
    if (key == "materials") return "Settings can pre-fill this on every new lesson.";
    return "";
}

std::string label_of(const std::string &key) {
    for (const auto &f : store::LESSON_FIELDS)
        if (f.key == key) return f.label;
    return key;
}

std::string q(const std::string &s) { return "\xE2\x80\x9C" + s + "\xE2\x80\x9D"; }
std::string or_untitled(const std::string &s) { return s.empty() ? "untitled" : s; }

// ----------------------------------------------------------------------------- the list

class LessonList : public ItemList {
public:
    explicit LessonList(App &a) : ItemList(a, "lesson", "lessons", "lesson_sort", "lesson") {
        columns_ = {{"Title", 380, true}, {"Unit", 110, false}, {"Course", 130, false}, {"Date(s)", 120, false}, {"Last Changed", 150, false}};
    }
    std::string title() override { return "Lesson Plans"; }

protected:
    void tools(Ui &ui) override {
        if (tool(ui, "New Lesson", true, true)) cmd_new();
        if (tool_sel(ui, "Open")) cmd_open();
        if (tool_sel(ui, "Copy")) cmd_duplicate();
        if (tool_sel(ui, "Rename\xE2\x80\xA6")) cmd_rename();
        if (tool_sel(ui, "Delete")) cmd_delete();
        tool_gap(ui);
        if (tool_sel(ui, "Preview")) cmd_preview();
        if (tool_sel(ui, "Export\xE2\x80\xA6")) cmd_export();
        if (tool_sel(ui, "Board Slide")) quick("PNG");
        if (tool_sel(ui, "Make-Up Sheet")) quick("MAKEUP");
        tool_gap(ui);
        if (tool(ui, "Day Slideshow\xE2\x80\xA6")) day_slideshow(app);
        if (tool(ui, "Curriculum Map\xE2\x80\xA6")) cmd_map();
    }
    std::vector<std::string> row(const Value &l) override {
        return {l.s("title").empty() ? "(untitled lesson)" : l.s("title"), l.s("unit"), l.s("course"), first_line(l.s("date")), l.s("updated")};
    }
    std::vector<std::pair<std::string, std::function<void()>>> menu_items() override {
        return {{"Open", [this] { cmd_open(); }},
                {"Preview", [this] { cmd_preview(); }},
                {"Export\xE2\x80\xA6", [this] { cmd_export(); }},
                {"Board Slide", [this] { quick("PNG"); }},
                {"Make-Up Sheet", [this] { quick("MAKEUP"); }},
                {"Shift Dates From Here\xE2\x80\xA6", [this] { shift_from_selected(); }},
                {"", nullptr},
                {"Copy", [this] { cmd_duplicate(); }},
                {"Rename\xE2\x80\xA6", [this] { cmd_rename(); }},
                {"Delete", [this] { cmd_delete(); }}};
    }
    // Shift Dates, from this lesson's first day, for its class
    void shift_from_selected() {
        Value *l = selected();
        if (!l) return;
        auto ds = store::lesson_dates(*l);
        Date from = ds.empty() ? today() : *std::min_element(ds.begin(), ds.end());
        shift_dates(app, store::class_of(*l), from);
    }
    void cmd_new() override {
        std::string t;
        if (!ask(app.ui, "New Lesson", "What's the lesson called?", t)) return;
        Value l = store::new_lesson(app.settings());
        l["title"] = text::strip(t);
        if (!unit_.empty()) l["unit"] = unit_;
        if (!new_class().empty()) l["course"] = new_class();
        std::string id = l.s("id");
        pool().push(std::move(l));
        app.save();
        app.show(lesson_editor(id));
    }
    void cmd_open() override {
        if (Value *l = selected()) app.show(lesson_editor(l->s("id")));
    }
    void cmd_delete() override {
        Value *l = selected();
        if (!l) return;
        std::string id = l->s("id");
        if (!confirm(app.ui, "Delete Lesson", "Delete the lesson " + q(or_untitled(l->s("title"))) + "?\n\nIts linked quizzes and worksheets are kept.",
                     "Delete", "Cancel", "warn"))
            return;
        auto &a = pool().arr_ref();
        a.erase(std::remove_if(a.begin(), a.end(), [&](const Value &x) { return x.s("id") == id; }), a.end());
        app.save();
        app.status("Deleted.");
    }
    void cmd_preview() override {
        if (Value *l = selected()) app.preview(doc::lesson_doc(*l, app.store));
    }
    void cmd_export() override {
        if (Value *l = selected()) export_dialog(app, "lesson", l->s("id"));
    }
    void cmd_map() {
        Value *l = selected();
        std::string c = !new_class().empty() ? new_class() : (l ? l->s("course") : "");
        if (!new_class().empty() || l) curriculum_map(app, &c);
        else curriculum_map(app);
    }
    void quick(const std::string &fmt) {
        if (Value *l = selected()) run_export(app, "lesson", l->s("id"), fmt);
    }
};

// ----------------------------------------------------------------------------- linked quizzes and worksheets

void attachments(App &app, Ui &ui, const std::string &lesson_id) {
    Skin &k = ui.sk();
    int S = ui.S();
    auto lesson = [&] { return app.lesson(lesson_id); };
    Value *l = lesson();
    if (!l) return;
    // drop links to deleted ones
    auto ids = [&]() -> Value & {
        Value *x = lesson();
        Value a = Value::array();
        for (const Value *t : app.store.attached(*x)) a.push(t->s("id"));
        if (a != x->get("assessments")) (*x)["assessments"] = a;
        return (*x)["assessments"];
    };
    auto shown = app.store.attached(*l);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row_ids;
    for (const Value *a : shown) {
        rows.push_back({a->s("kind"), a->s("title").empty() ? "(untitled)" : a->s("title"), size_text(*a)});
        row_ids.push_back(a->s("id"));
    }
    std::string &sel_id = ui.state<std::string>(ui.id("attach-sel"));
    int sel = -1;
    for (size_t i = 0; i < row_ids.size(); i++)
        if (row_ids[i] == sel_id) sel = (int)i;
    int nrows = std::max(3, std::min(8, (int)rows.size()));
    double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
    Rect r = ui.place(0, nrows * rh + hh + 2 * S);
    ListResult lr = list_view(ui, "attached", r, {{"Type", 140, false}, {"Title", 360, true}, {"Size", 140, false}}, rows, sel,
                              "Nothing linked yet. Click New\xE2\x80\xA6 to build a quiz or worksheet.");
    if (sel >= 0 && sel < (int)row_ids.size()) sel_id = row_ids[(size_t)sel];
    auto changed = [&] {
        if (Value *x = lesson()) app.touch(*x);
        ui.again();
    };
    auto open = [&] {
        if (!sel_id.empty() && sel >= 0) app.show(open_assessment(sel_id));
    };
    auto remove = [&] {
        if (sel < 0) return;
        const Value *a = app.assessment(sel_id);
        if (!a) return;
        if (!confirm(ui, "Remove from Lesson", "Take " + q(or_untitled(a->s("title"))) + " off this lesson? It isn't deleted.", "Remove")) return;
        auto &arr = ids().arr_ref();
        for (size_t i = 0; i < arr.size(); i++)
            if (arr[i].str() == sel_id) {
                arr.erase(arr.begin() + (long)i);
                break;
            }
        changed();
    };
    auto move = [&](int d) {
        if (sel < 0) return;
        auto &arr = ids().arr_ref();
        for (size_t i = 0; i < arr.size(); i++)
            if (arr[i].str() == sel_id) {
                long j = (long)i + d;
                if (j >= 0 && j < (long)arr.size()) {
                    std::swap(arr[i], arr[(size_t)j]);
                    changed();
                }
                break;
            }
    };
    if (lr.open) open();
    if (lr.del) remove();
    ui.space(6 * S);
    ui.flow();
    if (button(ui, "New\xE2\x80\xA6", true, false, true)) {
        Value *x = lesson();
        std::string id = new_assessment_dialog(app, lesson_id, x ? text::strip(x->s("unit")) : "");
        if (!id.empty()) {
            app.save();
            app.show(open_assessment(id));
        }
    }
    if (button(ui, "Link Existing\xE2\x80\xA6", true, false, true)) {
        Value &have = ids();
        std::set<std::string> linked;
        for (const auto &v : have.arr()) linked.insert(v.str());
        Value *x = lesson();
        std::string unit = text::strip(x->s("unit"));
        std::vector<const Value *> all;
        for (const auto &a : app.store.data["assessments"].arr())
            if (a.is_object()) all.push_back(&a);
        std::vector<const Value *> pool;
        for (const Value *a : store::sort_items(all, "updated"))
            if (!linked.count(a->s("id"))) pool.push_back(a);
        std::stable_sort(pool.begin(), pool.end(), [&](const Value *a, const Value *b) {   // this unit first
            auto other = [&](const Value *v) { return unit.empty() || text::strip(v->s("unit")) != unit; };
            return !other(a) && other(b);
        });
        if (pool.empty()) {
            alert(ui, "Link Existing", "There's nothing else to link. Click New\xE2\x80\xA6 to build one.");
        } else {
            std::vector<std::pair<std::string, std::string>> opts;
            for (const Value *a : pool) {
                std::string t = a->s("kind") + ":  " + (a->s("title").empty() ? "(untitled)" : a->s("title"));
                if (!text::strip(a->s("unit")).empty()) t += "   [" + a->s("unit") + "]";
                opts.push_back({a->s("id"), t});
            }
            std::string got = choose(ui, "Link to This Lesson", "Pick a quiz, worksheet, or sheet to go with this lesson:", opts);
            if (!got.empty()) {
                ids().push(got);
                sel_id = got;
                changed();
            }
        }
    }
    if (button(ui, "Open", true, false, true)) open();
    if (button(ui, "Remove", true, false, true)) remove();
    if (button(ui, "Move Up", true, false, true)) move(-1);
    if (button(ui, "Move Down", true, false, true)) move(1);
    ui.flow(false);
    label(ui, "Export \xE2\x86\x92 All formats puts these in the lesson's folder.", false, true, true);
}

// ----------------------------------------------------------------------------- the editor

class LessonEditor : public Screen {
public:
    LessonEditor(App &a, std::string id) : Screen(a), id_(std::move(id)) {
        Value *l = a.lesson(id_);
        a.status("Last saved " + (l && !l->s("updated").empty() ? l->s("updated") : std::string("\xE2\x80\x93")));
    }
    std::string title() override {
        Value *l = app.lesson(id_);
        return "Lesson: " + or_untitled(l ? l->s("title") : "");
    }
    bool command(const std::string &name) override {
        if (name == "preview") cmd_preview();
        else if (name == "export") export_dialog(app, "lesson", id_);
        else return false;
        return true;
    }
    void frame(Ui &ui, const Rect &body) override {
        Skin &k = ui.sk();
        int S = ui.S();
        Value *l = app.lesson(id_);
        if (!l) {   // deleted somewhere else
            app.back();
            return;
        }
        ui.push_id(id_);
        ui.begin(body, 0);
        begin_toolbar(ui);
        if (tool(ui, "Preview")) cmd_preview();
        if (tool(ui, "Export\xE2\x80\xA6")) export_dialog(app, "lesson", id_);
        if (tool(ui, "Board Slide")) run_export(app, "lesson", id_, "PNG");
        if (tool(ui, "Make-Up Sheet")) run_export(app, "lesson", id_, "MAKEUP");
        tool_gap(ui);
        if (tool(ui, "Sections\xE2\x80\xA6")) lesson_sections(app);
        tool_gap(ui);
        format_bar(ui);
        tool_gap(ui);
        if (toolbar_room(ui, k.f->width("Changes save by themselves.") + 4 * S)) label_mid(ui, "Changes save by themselves.", false, true);
        end_toolbar(ui);
        ui.fill(ui.cut_top(S), k.soft ? k["line"] : k["dark"]);
        Rect rest = ui.remaining();
        ui.end();
        if (first_) {
            focus_at_end(ui, ui.id("entry:title"));
            first_ = false;
        }
        ui.begin_scroll(ui.id("form"), rest, 1100 * S);
        ui.begin({ui.remaining().x + 12 * S, ui.remaining().y, ui.width() - 24 * S, 1e9}, -1);
        std::set<std::string> shown;
        for (const auto &f : store::shown_fields(app.settings(), *app.lesson(id_))) shown.insert(f.key);
        for (const auto &g : GROUPS) {
            std::vector<std::string> keys;
            for (const auto &key : g.second)
                if (shown.count(key)) keys.push_back(key);
            if (keys.empty()) continue;
            ui.space(10 * S);
            begin_group(ui, g.first);
            if (g.first == "Lesson") basics(ui);
            else
                for (const auto &key : keys) field(ui, key);
            end_group(ui);
        }
        ui.space(16 * S);
        ui.end();
        ui.end_scroll();
        ui.pop_id();
        std::string t = title();
        if (t != app.current_title) app.set_title(t);
    }

private:
    std::string id_;
    bool first_ = true;

    void basics(Ui &ui) {
        Skin &k = ui.sk();
        int S = ui.S();
        double lw = 0;
        for (const char *key : {"title", "unit", "course", "date", "duration"}) lw = std::max(lw, k.f->width(label_of(key) + ":"));
        lw += 8 * S;
        double avail = ui.width(), col = std::floor((avail - 2 * lw - 16 * S - 3 * ui.lay().gap) / 2);
        auto lab = [&](const std::string &key) { label_mid(ui, label_of(key) + ":"); };
        ui.row({lw, -1});
        lab("title");
        line_field(app, ui, "title", *app.lesson(id_), "title");
        ui.row({lw, col, lw + 16 * S, -1});
        lab("unit");
        line_field(app, ui, "unit", *app.lesson(id_), "unit");
        Rect r = ui.place(0, ui.ctl_h());
        if (ui.drawing) ui.text_in(*k.f, {r.x + 16 * S, r.y, r.w, r.h}, label_of("course") + ":", k["text"]);
        line_field(app, ui, "course", *app.lesson(id_), "course");
        ui.row({lw, col - ui.btn_w("Calendar\xE2\x80\xA6", true) - ui.lay().gap, 0, lw + 16 * S, -1}, ui.ctl_h());
        lab("date");
        line_field(app, ui, "date", *app.lesson(id_), "date");
        Rect b = ui.place(ui.btn_w("Calendar\xE2\x80\xA6", true), ui.ctl_h());
        if (button_at(ui, ui.id("btn:calendar"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Calendar\xE2\x80\xA6", true, false, true)) pick_date();
        r = ui.place(0, ui.ctl_h());
        if (ui.drawing) ui.text_in(*k.f, {r.x + 16 * S, r.y, r.w, r.h}, label_of("duration") + ":", k["text"]);
        line_field(app, ui, "duration", *app.lesson(id_), "duration");
        ui.row({});
        // a day with no class in the Date(s): say so under it
        std::string warn = day_warning();
        if (!warn.empty()) {
            ui.row({lw, -1});
            ui.place(0, 1);
            para(ui, warn, false, 0, k.fsmall.get());
            ui.row({});
        }
    }

    std::string day_warning() {
        const Value *l = app.lesson(id_);
        if (!l) return "";
        auto cal = schoolcal::read(app.settings());
        std::vector<std::string> bits;
        for (const auto &d : store::lesson_dates(*l)) {
            std::string why = cal.no_school(d);
            if (why.empty()) continue;
            std::string day = dates::fmt_date(d, false, false);
            if (d.weekday() >= 5) bits.push_back(day + " is a " + why);
            else if (const schoolcal::DayOff *o = cal.day_off(d)) bits.push_back(day + " is " + o->name);
            else bits.push_back(day + " is " + text::lower(why.substr(0, 1)) + why.substr(1));
            if (bits.size() == 3) break;
        }
        return bits.empty() ? "" : "Heads up: " + text::join(bits, "; ") + ".";
    }

    void pick_date() {
        // the calendar writes the year, so next year's copy of a lesson never lands on this year's days
        Value *l = app.lesson(id_);
        auto days = store::lesson_dates(*l);
        Date a, b;
        if (pick_dates(app, "Date(s): " + or_untitled(l->s("title")), days.empty() ? nullptr : &days.front(), days.empty() ? nullptr : &days.back(), a,
                       b)) {
            Value *x = app.lesson(id_);
            if (!x) return;
            (*x)["date"] = dates::fmt_range(a, b);
            app.touch(*x);
        }
    }

    void field(Ui &ui, const std::string &key) {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.space(6 * S);
        std::string h = hint(key);
        std::string btn = key == "bell_ringer" ? "Random Good Thing" : key == "vocab" ? "Make Vocab Quiz" : "";
        if (key != "standards" && key != "assessments") {
            double bw = btn.empty() ? 0 : ui.btn_w(btn, true);
            Rect row = ui.place(0, std::max(k.fb->line, btn.empty() ? 0.0 : k.fsmall->line + 8 * S));
            if (ui.drawing) {
                double x = ui.text(*k.fb, row.x, row.y + (row.h - k.fb->line) / 2 + k.fb->ascent, label_of(key), k["text"]);
                if (!h.empty()) {
                    std::string t = ui.elide(*k.fsmall, "  " + h, std::max(0.0, row.r() - bw - 8 * S - x));
                    ui.text(*k.fsmall, x, row.y + (row.h - k.fb->line) / 2 + k.fb->ascent, t, k["dim"]);
                }
            }
            if (!btn.empty() && button_at(ui, ui.id("btn:" + btn), {row.r() - bw, row.y, bw, row.h}, btn, true, false, true)) {
                if (key == "bell_ringer") good_thing();
                else vocab_quiz();
            }
        }
        if (key == "standards") standards_field(app, ui, "standards", [this] { return app.lesson(id_); });
        else if (key == "assessments") attachments(app, ui, id_);
        else auto_text(app, ui, key, *app.lesson(id_), key, key == "question" ? 1 : 2);
    }

    void good_thing() {
        Value *l = app.lesson(id_);
        std::string cur = text::strip(l->s("bell_ringer"));
        if (!cur.empty() && !text::starts_with(cur, store::GOOD_THINGS_PREFIX) &&
            !confirm(app.ui, "Bell Ringer", "Replace this bell ringer with a random Good Things prompt?", "Replace"))
            return;
        std::string t = app.store.good_thing(cur);
        l = app.lesson(id_);
        (*l)["bell_ringer"] = t;
        app.touch(*l);
        app.status("Good Things bell ringer added. Click again for another.");
    }

    void vocab_quiz() {
        app.save();
        Value *l = app.lesson(id_);
        Value *a = app.store.vocab_quiz(*l);
        if (!a) {
            alert(app.ui, "Make Vocab Quiz",
                  "Type at least two words with their definitions first, one per line, like this:\n\nethos: an appeal to the speaker's credibility");
            return;
        }
        std::string aid = a->s("id"), t = a->s("title");
        size_t n = a->get("questions").at(0).get("pairs").size();
        app.save();
        if (confirm(app.ui, "Make Vocab Quiz",
                    "Made " + q(t) + ", a matching quiz with " + std::to_string(n) + " words, and linked it to this lesson. Open it now?", "Open Quiz",
                    "Not Now"))
            app.show(open_assessment(aid));
        else
            app.status("Made " + q(t) + ". It's under Assessments & Worksheets.");
    }

    void cmd_preview() {
        app.save();
        if (Value *l = app.lesson(id_)) app.preview(doc::lesson_doc(*l, app.store));
    }
};

}  // namespace

Factory lesson_list() { return make<LessonList>(); }

Factory lesson_editor(const std::string &id) {
    return [id](App &a) -> std::unique_ptr<Screen> {
        if (!a.lesson(id)) return make<LessonList>()(a);
        return std::unique_ptr<Screen>(new LessonEditor(a, id));
    };
}

bool lesson_sections(App &app) {
    Value &st = app.settings();
    std::set<std::string> hide;
    for (const auto &h : st.get("lesson_hide").arr())
        if (h.is_string()) hide.insert(h.str());
    std::vector<std::pair<std::string, std::string>> keys;
    for (const auto &f : store::LESSON_FIELDS)
        if (std::find(store::FIXED_FIELDS.begin(), store::FIXED_FIELDS.end(), f.key) == store::FIXED_FIELDS.end()) keys.push_back({f.key, f.label});
    std::vector<char> on;
    for (const auto &kk : keys) on.push_back(!hide.count(kk.first));
    int r = dialog(app.ui, {"Lesson Sections", 560, 0},
                   [&](Ui &u) {
                       int S = u.S();
                       para(u, "Check the sections you use. Unchecked ones are hidden from the lesson editor, so it's shorter. Nothing is deleted: a "
                               "lesson that already has something in a hidden section still shows it, and it still prints.");
                       u.space(8 * S);
                       size_t half = (keys.size() + 1) / 2;
                       for (size_t i = 0; i < half; i++) {
                           u.row({(u.width() - 18 * S) / 2, -1});
                           for (size_t j : {i, i + half}) {
                               if (j >= keys.size()) {
                                   u.place(1, 1);
                                   continue;
                               }
                               bool v = on[j];
                               u.push_id((Id)j + 1);
                               if (check(u, keys[j].second, v)) on[j] = v;
                               u.pop_id();
                           }
                       }
                       u.row({});
                       u.space(8 * S);
                       para(u, "Title, unit, course, dates, duration, and standards are always there.", true);
                       return 0;
                   },
                   {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    std::set<std::string> now;
    Value a = Value::array();
    for (size_t i = 0; i < keys.size(); i++)
        if (!on[i]) {
            now.insert(keys[i].first);
            a.push(keys[i].first);
        }
    if (now == hide) return false;
    st["lesson_hide"] = a;
    app.save();
    return true;
}

std::string size_text(const Value &a) {
    std::string kind = a.s("kind");
    if (std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), kind) != store::SHEET_KINDS.end()) {
        if (kind == store::ANNOTATION) {
            const Value &r = a.get("rows");
            return (r.truthy() ? store::text_of(r) : "10") + " rows";
        }
        return "2 weeks";
    }
    int n = 0;
    double pts = 0;
    for (const auto &qq : a.get("questions").arr()) {
        std::string t = qq.s("type");
        if (t != "passage" && t != "section") n++;
        pts += store::points_of(qq);
    }
    return std::to_string(n) + " question" + (n != 1 ? "s" : "") + ", " + store::fmt_points(pts) + " pts";
}

}  // namespace gui
