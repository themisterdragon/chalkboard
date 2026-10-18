// The standards library and the standards picker (port of gui/standards.py), both built on one
// browser: subjects on the left; search, grade, and the standards (parts tucked under each) on
// the right; the full text of the highlighted one underneath.
#include <algorithm>
#include <cmath>
#include <set>

#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

std::string sub_label(const Value &parent, const std::string &label) {
    return parent.get("sub_grades").truthy() ? "Grade " + label + ": " : label + ". ";
}

// a message from the core, as a sentence: "The file..." (Python's str(e)[:1].upper() + str(e)[1:].lower())
std::string sentence(const std::string &m) {
    if (m.empty()) return m;
    std::string low = text::lower(m);
    return text::upper(m.substr(0, 1)) + low.substr(1);
}

struct Browser {
    App &app;
    std::vector<std::string> *selected;   // the picker's checked codes (nullptr: the library)
    std::string grade, subject, q, sel_code;
    bool show_sel = false;
    std::set<std::string> open;           // standards with their parts showing
    // what's shown, worked out again only when something changes
    std::string cache_key;
    std::vector<size_t> items;
    struct Row {
        size_t i;
        bool part, kids;
    };
    std::vector<Row> rows;
    int tops = 0;
    bool focus_list = true;
    int search_timer = 0;
    std::string typed;   // the search box (applied a moment after typing stops)

    Browser(App &a, const std::string &grades, std::vector<std::string> *sel) : app(a), selected(sel) {
        const Value &st = a.settings();
        std::string g = !grades.empty() ? grades : st.s("grades");
        if (g.empty()) g = "9-10";
        grade = std::find(store::GRADE_CHOICES.begin(), store::GRADE_CHOICES.end(), g) != store::GRADE_CHOICES.end() ? g : store::ALL;
        std::string subj = !a.std_subject.empty() ? a.std_subject : st.s("subject");
        subject = std::find(a.store.subjects.begin(), a.store.subjects.end(), subj) != a.store.subjects.end() ? subj : store::ALL;
    }
    ~Browser() {
        if (search_timer) app.ui.cancel(search_timer);
    }

    std::vector<std::string> subject_keys() {
        std::vector<std::string> out = {store::ALL};
        out.insert(out.end(), app.store.subjects.begin(), app.store.subjects.end());
        for (const auto &s : app.store.standards)
            if (s.get("custom").truthy()) {
                out.push_back("Custom");
                break;
            }
        return out;
    }

    void compute() {
        std::string flat_sel = selected && show_sel ? text::join(*selected, "\x01") : "";
        std::string key = grade + "\x02" + subject + "\x02" + q + "\x02" + (show_sel ? "1" : "0") + flat_sel + "\x02" +
                          std::to_string(app.store.standards.size()) + "\x02";
        for (const auto &o : open) key += o + ",";
        if (key == cache_key) return;
        cache_key = key;
        items.clear();
        const auto &all = app.store.standards;
        if (selected && show_sel) {
            for (const auto &c : *selected) {
                auto it = app.store.std_index.find(c);
                if (it != app.store.std_index.end()) items.push_back(it->second);
            }
        } else {
            std::string ql = text::lower(text::strip(q));
            for (size_t i = 0; i < all.size(); i++) {
                const Value &s = all[i];
                if (subject == "Custom") {
                    if (!s.get("custom").truthy()) continue;
                } else if (subject != store::ALL && s.s("subject") != subject && !s.get("custom").truthy())
                    continue;
                if (grade != store::ALL && !store::grades_match(store::text_of(s.get("grades")), grade)) continue;
                if (!ql.empty() && !text::contains(text::lower(s.s("code")), ql) && !text::contains(text::lower(s.s("text")), ql) &&
                    !text::contains(text::lower(s.s("cluster")), ql) && !text::contains(text::lower(s.s("strand")), ql))
                    continue;
                items.push_back(i);
            }
        }
        bool flat = !text::strip(q).empty() || (selected && show_sel);
        std::set<std::string> shown_tops;
        std::set<std::string> has_kids;
        tops = 0;
        for (size_t i : items) {
            const Value &s = all[i];
            if (!s.get("parent").truthy()) {
                tops++;
                shown_tops.insert(s.s("code"));
            } else
                has_kids.insert(s.s("parent"));
        }
        rows.clear();
        for (size_t i : items) {
            const Value &s = all[i];
            std::string parent = s.s("parent");
            bool part = !flat && !parent.empty() && shown_tops.count(parent);
            if (part && !open.count(parent)) continue;
            rows.push_back({i, part, !flat && parent.empty() && has_kids.count(s.s("code")) > 0});
        }
    }

    const Value *current() {
        if (sel_code.empty()) return nullptr;
        for (const auto &r : rows)
            if (app.store.standards[r.i].s("code") == sel_code) return &app.store.standards[r.i];
        return nullptr;
    }

    void toggle(const std::string &code) {
        if (!selected) return;
        auto it = std::find(selected->begin(), selected->end(), code);
        if (it != selected->end()) selected->erase(it);
        else selected->push_back(code);
        app.ui.again();
    }

    void frame(Ui &ui, const Rect &r) {
        Skin &k = ui.sk();
        int S = ui.S();
        // the subjects
        double lw = std::min(30 * k.f->width("0") + 30 * S, r.w * 0.3);
        Rect left{r.x, r.y, lw, r.h};
        ui.begin(left, 0);
        label(ui, "Subjects", true);
        ui.space(2 * S);
        Rect lr = ui.remaining();
        ui.end();
        auto keys = subject_keys();
        if (std::find(keys.begin(), keys.end(), subject) == keys.end()) subject = store::ALL;
        std::vector<std::vector<std::string>> srows;
        int ssel = 0;
        for (size_t i = 0; i < keys.size(); i++) {
            srows.push_back({keys[i] == store::ALL ? "All subjects" : keys[i]});
            if (keys[i] == subject) ssel = (int)i;
        }
        int before = ssel;
        list_view(ui, "subjects", lr, {{"", 300, true}}, srows, ssel, "", false);
        if (ssel != before && ssel >= 0) {
            subject = app.std_subject = keys[(size_t)ssel];
            ui.again();
        }
        // the filters
        Rect right{r.x + lw + 8 * S, r.y, r.w - lw - 8 * S, r.h};
        ui.begin(right, 0);
        Rect bar = ui.cut_top(ui.ctl_h() + 6 * S);
        ui.begin({bar.x, bar.y, bar.w, ui.ctl_h()}, 6 * S);
        ui.flow();
        label_mid(ui, "Search:");
        Id sid = ui.id("entry:search");
        if (ui.focused(sid) && ui.key(plat::K_DOWN)) focus_list = true;
        if (entry(ui, "search", typed, 24 * k.f->width("0") + 20 * S)) {   // a moment after typing stops
            if (search_timer) ui.cancel(search_timer);
            search_timer = ui.after(250, [this] {
                search_timer = 0;
                q = typed;
            });
        }
        ui.place(8 * S, 1);
        label_mid(ui, "Grade:");
        std::vector<std::pair<std::string, std::string>> gopts;
        for (const auto &g : store::GRADE_CHOICES) gopts.push_back({g, grade_label(g)});
        dropdown(ui, "grade", gopts, grade, 13);
        if (selected) {
            ui.place(8 * S, 1);
            Rect c = ui.place(k.f->width("Show only checked") + k.line + 16 * S, ui.ctl_h());
            ui.begin({c.x, c.y + (c.h - k.line) / 2, c.w, k.line + 4 * S}, 0);
            check(ui, "Show only checked", show_sel);
            ui.end();
        }
        ui.end();
        // the detail underneath
        double dh = 6 * k.line + 10 * S;
        Rect det = ui.cut_bottom(dh);
        Rect list_r = ui.remaining();
        list_r.h -= 6 * S;
        ui.end();
        compute();
        // the list
        std::vector<Column> cols;
        if (selected) cols.push_back({"", 26, false, MARK_CHECK});
        cols.push_back({"", 22, false, MARK_TREE});
        cols.push_back({"Code", 150, false});
        cols.push_back({"Grades", 80, false});
        cols.push_back({"Standard", 520, true});
        std::vector<std::vector<std::string>> lrows;
        int sel = -1;
        for (size_t j = 0; j < rows.size(); j++) {
            const Value &s = app.store.standards[rows[j].i];
            std::vector<std::string> cells;
            std::string code = s.s("code");
            if (selected) cells.push_back(std::find(selected->begin(), selected->end(), code) != selected->end() ? "x" : "o");
            cells.push_back(rows[j].kids ? (open.count(code) ? "-" : "+") : "");
            cells.push_back(rows[j].part ? "    " + code : code);
            cells.push_back(store::text_of(s.get("grades")));
            cells.push_back(text::squash(s.s("text")));
            lrows.push_back(cells);
            if (code == sel_code) sel = (int)j;
        }
        if (focus_list) {
            ui.focus(ui.id("list:standards"));
            focus_list = false;
        }
        std::string empty = app.store.standards.empty() ? "No standards yet. Use Import File\xE2\x80\xA6 to bring in a CSV or JSON file."
                                                        : "No standards match. Try another subject or grade, or clear the search.";
        ListResult res = list_view(ui, "standards", list_r, cols, lrows, sel, empty);
        if (sel >= 0 && sel < (int)rows.size()) {
            std::string code = app.store.standards[rows[(size_t)sel].i].s("code");
            if (code != sel_code) {
                sel_code = code;
                ui.again();
            }
        }
        int tree_col = selected ? 1 : 0;
        if (sel >= 0 && sel < (int)rows.size()) {
            std::string code = app.store.standards[rows[(size_t)sel].i].s("code");
            bool kids = rows[(size_t)sel].kids;
            if (res.col == tree_col && kids) {
                if (open.count(code)) open.erase(code);
                else open.insert(code);
                ui.again();
            } else if (selected && res.col == 0 && !lrows[(size_t)sel][0].empty())
                toggle(code);
            else if (selected && (res.space || res.open))
                toggle(code);
            else if (!selected && res.open && kids) {
                if (open.count(code)) open.erase(code);
                else open.insert(code);
                ui.again();
            }
            // arrows open and close a standard's parts
            if (ui.focused(ui.id("list:standards"))) {
                if (kids && !open.count(code) && ui.key(plat::K_RIGHT)) {
                    open.insert(code);
                    ui.again();
                } else if (kids && open.count(code) && ui.key(plat::K_LEFT)) {
                    open.erase(code);
                    ui.again();
                } else if (rows[(size_t)sel].part && ui.key(plat::K_LEFT)) {
                    sel_code = app.store.standards[rows[(size_t)sel].i].s("parent");
                    ui.again();
                }
            }
        }
        // the status line
        std::string subj = subject == store::ALL ? "All subjects" : subject;
        std::string note = commas(tops) + " standard" + (tops != 1 ? "s" : "") +
                           ((int)items.size() > tops ? " + " + commas((long long)items.size() - tops) + " parts" : "");
        app.status(subj + " \xC2\xB7 " + grade_label(grade) + " \xC2\xB7 " + note,
                   selected ? std::to_string(selected->size()) + " checked" : std::string("\x01"));
        detail(ui, det);
    }

    void detail(Ui &ui, const Rect &r) {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.fill({r.x, r.y, r.w, (double)S}, k["dark"]);
        Rect in{r.x, r.y + S + 4 * S, r.w, r.h - S - 4 * S};
        const Value *s = current();
        ui.begin_scroll(ui.id("detail"), in);
        if (s) {
            Rect top = ui.place(0, k.fb->line);
            if (ui.drawing) {
                double x = ui.text(*k.fb, top.x, top.y + k.fb->ascent, s->s("code"), k["text"]);
                std::vector<std::string> parts;
                for (const std::string &p : {s->s("subject"), s->s("strand"), s->s("cluster"), grade_label(store::text_of(s->get("grades")))})
                    if (!p.empty()) parts.push_back(p);
                ui.text(*k.f, x, top.y + k.fb->ascent, ui.elide(*k.f, "   " + text::join(parts, "  /  "), top.r() - x), k["dim"]);
            }
            if (s->get("parent").truthy()) {
                const Value *p = app.store.standard(s->s("parent"));
                if (p) {
                    para(ui, "Part of " + p->s("code") + ": " + p->s("text"), true);
                    para(ui, sub_label(*p, store::text_of(s->get("label"))) + s->s("text"));
                }
            } else {
                para(ui, s->s("text"));
                for (const auto &kv : s->get("subs").arr()) para(ui, "   " + sub_label(*s, store::text_of(kv.at(0))) + store::text_of(kv.at(1)));
            }
            para(ui, "Source: " + s->s("source"), true);
        }
        ui.end_scroll();
    }
};

class Library : public Screen {
public:
    explicit Library(App &a) : Screen(a), b_(a, "", nullptr) {}
    std::string title() override { return "Standards Library"; }
    bool escape() override {
        if (!b_.typed.empty()) {
            b_.typed.clear();
            b_.q.clear();
            app.ui.again();
            return true;
        }
        return false;
    }
    bool command(const std::string &name) override {
        if (name != "find") return false;
        app.ui.focus(find_id_);
        return true;
    }
    void reloaded() override { b_.cache_key.clear(); }
    void frame(Ui &ui, const Rect &body) override {
        int S = ui.S();
        ui.begin(body, 0);
        begin_toolbar(ui);
        if (tool(ui, "Import File\xE2\x80\xA6")) import_standards(app);
        if (tool(ui, "Export to Share\xE2\x80\xA6")) export_file();
        if (tool(ui, "Remove Imported Subject\xE2\x80\xA6")) remove_subject();
        if (tool(ui, "Add My Own\xE2\x80\xA6")) add_custom();
        const Value *cur = b_.current();
        if (tool(ui, "Delete Mine", cur && cur->get("custom").truthy())) delete_custom();
        end_toolbar(ui);
        Rect rest = ui.remaining();
        ui.end();
        ui.push_id("browser");
        find_id_ = ui.id("entry:search");
        b_.frame(ui, {rest.x + 8 * S, rest.y, rest.w - 16 * S, rest.h - 8 * S});
        ui.pop_id();
    }

private:
    Browser b_;
    Id find_id_ = 0;

    void export_file() {
        auto opts = app.store.shareable_subjects();
        if (opts.empty()) {
            alert(app.ui, "Export to Share", "Only standards you imported or added yourself can be exported, and there aren't any yet.");
            return;
        }
        std::vector<std::pair<std::string, std::string>> choices;
        if (opts.size() > 1) choices.push_back({std::string("\x01" "all"), "All of them"});
        for (const auto &o : opts) choices.push_back({o, o});
        std::string s = choose(app.ui, "Export to Share", "Save which standards to a file another teacher can import?", choices, choices[0].first);
        if (s.empty()) return;
        std::string folder = app.win.choose_folder("Save the standards file in which folder?");
        if (folder.empty()) return;
        try {
            std::string path = app.store.export_standards(folder, s == "\x01" "all" ? opts : std::vector<std::string>{s});
            alert(app.ui, "Exported", "Saved\n" + path + "\n\nAnother teacher can bring it in with Standards Library > Import File.");
        } catch (store::UserError &e) {
            alert(app.ui, "Export Didn't Work", sentence(e.message), "warn");
        }
    }

    void remove_subject() {
        auto opts = app.store.imported_subjects();
        if (opts.empty()) {
            alert(app.ui, "Remove Subject", "You haven't imported any subjects yet. Use Import File\xE2\x80\xA6 to bring in a CSV or JSON file of standards.");
            return;
        }
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto &o : opts) choices.push_back({o, o});
        std::string s = choose(app.ui, "Remove Imported Subject", "Which subject?", choices);
        if (!s.empty() && confirm(app.ui, "Remove Subject", "Remove " + s + "? Lessons keep the codes they already use.", "Remove", "Cancel", "warn")) {
            app.store.remove_subject(s);
            b_.cache_key.clear();
        }
    }

    void add_custom() {
        std::string code, body;
        bool first = true;
        int r = dialog(
            app.ui, {"Add My Own Standard", 560, 0},
            [&](Ui &u) {
                int S = u.S();
                label(u, "Code (like SL.9-10.1 or DISTRICT-1):");
                u.space(2 * S);
                if (first) {
                    u.focus(u.id("entry:code"));
                    first = false;
                }
                u.row({24 * u.sk().f->width("0") + 20 * S});
                entry(u, "code", code);
                u.row({});
                u.space(8 * S);
                label(u, "Full text of the standard:");
                u.space(2 * S);
                text_area(u, "text", body, 5, 10, false);
                return 0;
            },
            {{"Add", 1}, {"Cancel", 2}}, 0, 2);
        if (r != 1) return;
        std::string c = text::strip(code), t = text::squash(body);
        if (c.empty() || t.empty()) return;
        if (app.store.std_index.count(c)) {
            alert(app.ui, "Add My Own Standard", "There's already a standard with the code " + c + ".", "warn");
            return;
        }
        Value x = Value::object();
        x["code"] = c;
        x["text"] = t;
        x["grades"] = "Custom";
        if (!app.store.data["custom_standards"].is_array()) app.store.data["custom_standards"] = Value::array();
        app.store.data["custom_standards"].push(x);
        app.store.reload_standards();
        app.save();
        b_.cache_key.clear();
        app.status("Added " + c + ".");
    }

    void delete_custom() {
        const Value *s = b_.current();
        if (!s || !s->get("custom").truthy()) return;
        std::string code = s->s("code");
        if (!confirm(app.ui, "Delete Standard", "Delete your standard " + code + "?", "Delete", "Cancel", "warn")) return;
        auto &arr = app.store.data["custom_standards"].arr_ref();
        arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const Value &x) { return x.s("code") == code; }), arr.end());
        app.store.reload_standards();
        app.save();
        b_.cache_key.clear();
    }
};

}  // namespace

Factory standards_library() { return make<Library>(); }

void import_standards(App &app) {
    std::string path = app.win.open_file("Import standards", {{"Standards files", {"*.csv", "*.json", "*.CSV", "*.JSON"}}, {"All files", {"*"}}});
    if (path.empty()) return;
    Value done;
    try {
        done = app.store.import_standards(path);
    } catch (store::UserError &e) {
        alert(app.ui, "Import Didn't Work", sentence(e.message), "warn");
        return;
    }
    if (!app.store.warning.empty()) {
        std::string w = app.store.warning;
        app.store.warning.clear();
        if (w[0] == '?') w = w.substr(1);
        alert(app.ui, "Import", w, "warn");
        return;
    }
    std::vector<std::string> lines;
    for (const auto &d : done.arr()) {
        long long skip = d.at(2).as_int();
        lines.push_back(store::text_of(d.at(0)) + ": " + commas(d.at(1).as_int()) + " standards" +
                        (skip ? " (" + std::to_string(skip) + " skipped: code already used)" : ""));
    }
    if (done.size()) app.std_subject = store::text_of(done.at(0).at(0));
    app.go_section("standards");   // shows the subject just brought in
    app.later([&app, lines] { alert(app.ui, "Imported", text::join(lines, "\n")); });
}

bool pick_standards(App &app, const std::vector<std::string> &current, const std::string &grades, std::vector<std::string> &out) {
    std::vector<std::string> sel = current;
    Browser b(app, grades, &sel);
    int r = dialog(
        app.ui, DialogBox{"Choose Standards", 0, 0, 0.9, 0.86},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Rect area = u.clip_rect();
            area.h = area.b() - u.remaining().y;
            area.y = u.remaining().y;
            u.begin(area, 0);
            label(u, "Click the box (or press Space) to check a standard. Click the arrow to see its parts.", false, true);
            u.space(6 * S);
            std::string info = "Checked (" + std::to_string(sel.size()) + "): " + (sel.empty() ? std::string("none yet") : text::join(sel, ", "));
            double uw = u.btn_w("Uncheck All", true);
            auto lines = u.wrap(*k.fb, info, u.width() - uw - 12 * S);
            if (lines.size() > 2) lines.resize(2), lines[1] = u.elide(*k.fb, lines[1] + "\xE2\x80\xA6", u.width() - uw - 12 * S);
            Rect bottom = u.cut_bottom(std::max(u.ctl_h(), lines.size() * k.fb->line) + 8 * S);
            Rect mid = u.remaining();
            u.end();
            if (u.drawing)
                for (size_t i = 0; i < lines.size(); i++) u.text(*k.fb, bottom.x, bottom.y + 8 * S + i * k.fb->line + k.fb->ascent, lines[i], k["text"]);
            if (button_at(u, u.id("btn:uncheck"), {bottom.r() - uw, bottom.y + 8 * S, uw, k.fsmall->line + 8 * S}, "Uncheck All", true, false, true)) {
                sel.clear();
                u.again();
            }
            u.push_id("browser");
            b.frame(u, mid);
            u.pop_id();
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    out = sel;
    return true;
}

}  // namespace gui
