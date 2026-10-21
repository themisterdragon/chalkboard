// Assessments & assignments (port of gui/assessments.py): the list, the quiz/test editor, its
// question dialog, and the fixed sheets (annotation and bell ringer).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "doc.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

const char *LETTERS = "ABCDEFGHIJ";

const std::vector<std::pair<std::string, std::string>> TYPE_TEXT = {
    {"mc", "Multiple choice"}, {"tf", "True / false"}, {"short", "Short answer"}, {"essay", "Extended response"},
    {"fill", "Fill in the blank"}, {"match", "Matching"}, {"chart", "Chart / organizer"}, {"passage", "Reading passage"},
    {"section", "Section header"}};
const std::vector<std::pair<std::string, std::string>> TYPE_HELP = {
    {"mc", "A prompt and lettered choices."},
    {"tf", "A statement students mark True or False."},
    {"short", "A prompt and a few writing lines."},
    {"essay", "A prompt and lined, blank, or boxed space."},
    {"fill", "Type ___ (three underscores) wherever a blank goes."},
    {"match", "Terms and their matches. Matches are shuffled on the student copy."},
    {"chart", "A chart, Venn diagram, idea web, or other organizer for students to fill in."},
    {"passage", "A text block with line numbers. Not scored."},
    {"section", "A titled part with its own directions. Not scored."}};
const std::vector<std::pair<std::string, std::string>> SPACE_TEXT = {{"lines", "Lined"}, {"blank", "Blank space"}, {"box", "Bordered box"}};

std::string lookup(const std::vector<std::pair<std::string, std::string>> &m, const std::string &k, const std::string &dflt = "") {
    for (const auto &p : m)
        if (p.first == k) return p.second;
    return dflt;
}

std::string kind_help(const std::string &k) {
    if (k == store::ANNOTATION) return "Name/date, a heading, and a Line / Symbol / Reason chart on one page.";
    if (k == store::BELL_SHEET) return "Monday\xE2\x80\x93" "Friday boxes, one week per side. Print double-sided.";
    return "";
}

bool is_sheet(const std::string &kind) {
    return std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), kind) != store::SHEET_KINDS.end();
}

std::string q(const std::string &s) { return "\xE2\x80\x9C" + s + "\xE2\x80\x9D"; }
std::string or_untitled(const std::string &s) { return s.empty() ? "untitled" : s; }

std::string list_text(const Value &v) {
    std::vector<std::string> out;
    for (const auto &x : v.arr()) out.push_back(store::text_of(x));
    return text::join(out, "\n");
}

Value text_list(const std::string &s0) {
    std::string s = text::strip(s0);
    Value out = Value::array();
    if (s.empty()) return out;
    for (const auto &x : text::split(s, "\n")) out.push(text::strip(x));
    return out;
}

std::vector<std::pair<std::string, std::string>> numbers(int lo, int hi) {
    std::vector<std::pair<std::string, std::string>> out;
    for (int n = lo; n <= hi; n++) out.push_back({std::to_string(n), std::to_string(n)});
    return out;
}

// a label over a control, bold
void head(Ui &ui, const std::string &t, double above = 6) {
    ui.space(above * ui.S());
    label(ui, t, true);
    ui.space(2 * ui.S());
}

// ----------------------------------------------------------------------------- the question dialog

// Edits a copy of q. Returns false when cancelled.
bool question_dialog(App &app, const std::string &aid, Value &qq, bool is_new) {
    Value q = qq;
    std::string t = q.s("type");
    std::string name = (is_new ? "New " : "Edit ") + lookup(TYPE_TEXT, t) + (t != "passage" && t != "section" ? " Question" : "");
    std::map<std::string, std::string> strs;
    for (const char *k : {"prompt", "title", "text", "answer", "center"}) strs[k] = store::text_of(q.get(k));
    strs["heads"] = list_text(q.get("heads"));
    strs["side"] = list_text(q.get("side"));
    strs["points"] = store::fmt_points(q.get("points").truthy() ? q.get("points").as_double() : 0);
    strs["standard"] = q.s("standard");
    if (t == "mc" || t == "tf") strs["answer"] = "";
    // multiple choice
    std::vector<std::string> choices;
    for (const auto &c : q.get("choices").arr()) choices.push_back(store::text_of(c));
    if (t == "mc" && choices.empty()) choices.assign(4, "");
    int ans = q.get("answer").is_number() ? (int)q.get("answer").as_int() : -1;
    // true / false
    std::string tf = q.get("answer").is_bool() ? (q.get("answer").as_bool() ? "T" : "F") : "";
    // matching
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto &p : q.get("pairs").arr()) pairs.push_back({store::text_of(p.at(0)), store::text_of(p.at(1))});
    if (t == "match" && pairs.empty()) pairs.assign(3, {"", ""});
    bool numbered = q.has("numbered") ? q.get("numbered").truthy() : true;
    std::set<std::string> texts;   // the keys this question's fields own
    bool first = true;
    Value *a = app.assessment(aid);
    std::vector<std::string> pool;
    if (a)
        for (const auto &c : a->get("standards").arr()) pool.push_back(c.str());
    bool has_stds = !pool.empty();
    if (!q.s("standard").empty() && std::find(pool.begin(), pool.end(), q.s("standard")) == pool.end()) pool.push_back(q.s("standard"));
    std::vector<std::pair<std::string, std::string>> std_opts = {{"", "(none)"}};
    for (const auto &c : pool) std_opts.push_back({c, c + "  " + text::slice(app.store.std_text(c), 0, 60)});

    int r = dialog(
        app.ui, DialogBox{name, 680, 0, 0, 0.86},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Rect area = u.clip_rect();
            area.h = area.b() - u.remaining().y;
            area.y = u.remaining().y;
            Rect top = u.place(0, k.fsmall->line + 14 * S);
            if (u.drawing) u.text_in(*k.f, {top.x, top.y, top.w, top.h}, lookup(TYPE_HELP, t), k["dim"]);
            u.begin({top.r() - 140.0 * S, top.y, 140.0 * S, top.h}, 0);
            u.flow();
            format_bar(u);
            u.end();
            area.y = top.b();
            area.h -= top.h;
            u.begin_scroll(u.id("qscroll"), area);
            u.begin({u.remaining().x, u.remaining().y, u.width() - 4 * S, 1e9}, 4 * S);
            texts.clear();
            auto line = [&](const std::string &lab, const std::string &key) {
                head(u, lab);
                texts.insert("line:" + key);
                if (first) {
                    focus_at_end(u, u.id("entry:" + key));
                    first = false;
                }
                entry(u, key, strs[key]);
            };
            auto box = [&](const std::string &lab, const std::string &key, int h) {
                head(u, lab);
                texts.insert(key);
                if (first) {
                    u.focus(u.id("text:" + key));
                    first = false;
                }
                text_area(u, key, strs[key], h, std::max(h, 14));
            };
            if (t == "section") {
                line("Section title (like Part II: Vocabulary)", "title");
                box("Directions for this section (optional)", "prompt", 3);
            } else if (t == "passage") {
                line("Passage title (optional)", "title");
                box("Passage text (paste or type; each new line starts a paragraph)", "text", 12);
                u.space(6 * S);
                check(u, "Number the lines on the PDF", numbered);
            } else if (t == "chart")
                box("Directions (optional; left blank, a short default prints)", "prompt", 2);
            else
                box(t != "match" ? "Question" : "Directions for the matching set", "prompt", t != "fill" ? 4 : 3);

            if (t == "mc") {
                head(u, "Choices (click the circle next to the correct answer)", 8);
                double rm = u.btn_w("Remove", true);
                for (size_t i = 0; i < choices.size(); i++) {
                    u.push_id((Id)i + 1);
                    u.row({k.line + k.f->width("M)") + 12 * S, -1, rm}, u.ctl_h());
                    if (radio(u, std::string(1, LETTERS[i]) + ")", ans == (int)i)) ans = (int)i;
                    entry(u, "choice", choices[i]);
                    Rect b = u.place(rm, u.ctl_h());
                    if (button_at(u, u.id("remove"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Remove", true, false, true)) {
                        choices.erase(choices.begin() + (long)i);
                        if (ans == (int)i) ans = -1;
                        else if (ans > (int)i) ans--;
                        u.pop_id();
                        u.again();
                        break;
                    }
                    u.pop_id();
                }
                u.row({});
                u.space(4 * S);
                if (button(u, "Add Choice", choices.size() < 6, false, true) && choices.size() < 6) {
                    choices.push_back("");
                    u.push_id((Id)choices.size());
                    u.focus(u.id("entry:choice"));
                    u.pop_id();
                }
            } else if (t == "tf") {
                head(u, "Answer", 8);
                u.flow();
                for (auto p : std::vector<std::pair<std::string, std::string>>{{"T", "True"}, {"F", "False"}, {"", "Not set"}}) {
                    if (radio(u, p.second, tf == p.first)) tf = p.first;
                    u.place(10 * S, 1);
                }
                u.flow(false);
            } else if (t == "short") {
                u.space(8 * S);
                u.flow();
                label_mid(u, "Writing lines:");
                std::string lines = std::to_string(q.get("lines").truthy() ? q.get("lines").as_int() : 3);
                if (dropdown(u, "lines", numbers(1, 12), lines, 4)) q["lines"] = std::stoi(lines);
                u.flow(false);
                box("Answer key / exemplar (only on the key)", "answer", 2);
            } else if (t == "essay") {
                u.space(8 * S);
                u.flow();
                label_mid(u, "Writing space:");
                std::string space = q.s("space").empty() ? "lines" : q.s("space");
                for (const auto &p : SPACE_TEXT)
                    if (radio(u, p.second, space == p.first)) q["space"] = p.first;
                u.flow(false);
                u.space(4 * S);
                u.flow();
                label_mid(u, "Size (lines, about 1/3 inch each):");
                std::string lines = std::to_string(q.get("lines").truthy() ? q.get("lines").as_int() : 12);
                std::vector<std::pair<std::string, std::string>> opts;
                bool found = false;
                for (int n : {4, 6, 8, 10, 12, 15, 18, 20, 24, 30}) {
                    opts.push_back({std::to_string(n), std::to_string(n)});
                    found |= std::to_string(n) == lines;
                }
                if (!found) opts.push_back({lines, lines});
                if (dropdown(u, "lines", opts, lines, 4)) q["lines"] = std::stoi(lines);
                u.flow(false);
                box("Rubric / key notes (only on the key)", "answer", 3);
            } else if (t == "fill") {
                line("Answer(s) for the key", "answer");
            } else if (t == "chart") {
                // the organizer: picking a different kind fills in its usual labels
                u.space(8 * S);
                u.flow();
                label_mid(u, "Kind:", true);
                std::string preset = q.s("preset").empty() ? "chart" : q.s("preset");
                std::vector<std::pair<std::string, std::string>> popts;
                for (const auto &p : store::chart_presets()) popts.push_back({p.key, p.label});
                if (dropdown(u, "preset", popts, preset, 30)) {
                    // keep what was typed for the fields the new kind shares
                    q["heads"] = text_list(strs["heads"]);
                    q["side"] = text_list(strs["side"]);
                    q["center"] = strs["center"];
                    store::chart_preset(q, preset);
                    strs["heads"] = list_text(q.get("heads"));
                    strs["side"] = list_text(q.get("side"));
                    strs["center"] = q.s("center");
                }
                u.flow(false);
                label(u, "Picking a kind fills in its usual labels; change any of them below.", false, true, true);
                std::string lay = q.s("layout").empty() ? "table" : q.s("layout");
                auto num = [&](const std::string &lab, const std::string &key, int lo, int hi) {
                    label_mid(u, lab);
                    std::string v = std::to_string(q.get(key).truthy() ? q.get(key).as_int() : lo);
                    if (dropdown(u, key, numbers(lo, hi), v, 3)) q[key] = std::stoi(v);
                    u.place(10 * S, 1);
                };
                auto lines_box = [&](const std::string &lab, const std::string &key, int h = 3) {
                    head(u, lab);
                    texts.insert(key);
                    text_area(u, key, strs[key], h, std::max(h, 10), false);
                };
                auto center = [&](const std::string &lab) {
                    head(u, lab);
                    texts.insert("center");
                    u.row({std::min(u.width(), 40 * k.f->width("0") + 20 * S)});
                    entry(u, "center", strs["center"]);
                    u.row({});
                };
                u.space(4 * S);
                u.flow();
                if (lay == "table") {
                    num("Columns:", "cols", 1, 8);
                    num("Rows:", "rows", 1, 20);
                    u.flow(false);
                    lines_box("Column headings (one per line; leave blank for none)", "heads");
                    bool side = q.get("sidecol").truthy();
                    u.space(6 * S);
                    if (check(u, "Label each row too (a matrix)", side)) q["sidecol"] = side;
                    lines_box("Row labels (one per line; optional)", "side");
                } else if (lay == "venn") {
                    label_mid(u, "Circles:");
                    int c = (int)(q.get("circles").truthy() ? q.get("circles").as_int() : 2);
                    for (int n : {2, 3})
                        if (radio(u, std::to_string(n), c == n)) q["circles"] = n;
                    u.flow(false);
                    lines_box("Circle labels (one per line; a line after the last circle labels the middle)", "heads");
                } else if (lay == "web") {
                    num("Bubbles:", "rows", 3, 8);
                    u.flow(false);
                    center("Center topic (optional)");
                    lines_box("Bubble labels (one per line; optional)", "heads");
                } else if (lay == "sequence") {
                    num("Boxes:", "rows", 2, 8);
                    u.flow(false);
                    lines_box("Box labels (one per line; blank boxes are numbered)", "heads");
                } else if (lay == "frayer") {
                    u.flow(false);
                    center("Word in the middle (optional)");
                    lines_box("Corner labels (four lines)", "heads", 4);
                } else if (lay == "plot") {
                    u.flow(false);
                    lines_box("Stage labels (five lines)", "heads", 5);
                }
                u.flow(false);
                u.space(8 * S);
                u.flow();
                label_mid(u, "Size:");
                int size = (int)q.get("lines").as_int(0);
                std::vector<int> sizes = store::CHART_SIZES;
                if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
                std::vector<std::pair<std::string, std::string>> sopts;
                for (int n : sizes) sopts.push_back({std::to_string(n), store::chart_size_label(n)});
                std::string sv = std::to_string(size);
                if (dropdown(u, "size", sopts, sv, 26)) q["lines"] = std::stoi(sv);
                u.flow(false);
                box("Key notes (only on the key)", "answer", 2);
            }
            if (t == "match") {
                head(u, "Terms and matches (up to 10)", 8);
                double rm = u.btn_w("Remove", true);
                double tw = std::floor((u.width() - rm - 2 * u.lay().gap) * 0.38);
                u.row({tw, -1, rm});
                label(u, "Term", false, true);
                label(u, "Matches", false, true);
                u.place(rm, 1);
                for (size_t i = 0; i < pairs.size(); i++) {
                    u.push_id((Id)i + 1);
                    u.row({tw, -1, rm}, u.ctl_h());
                    entry(u, "term", pairs[i].first);
                    entry(u, "match", pairs[i].second);
                    Rect b = u.place(rm, u.ctl_h());
                    if (button_at(u, u.id("remove"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Remove", true, false, true)) {
                        pairs.erase(pairs.begin() + (long)i);
                        u.pop_id();
                        u.again();
                        break;
                    }
                    u.pop_id();
                }
                u.row({});
                u.space(4 * S);
                if (button(u, "Add Pair", pairs.size() < 10, false, true) && pairs.size() < 10) {
                    pairs.push_back({"", ""});
                    u.push_id((Id)pairs.size());
                    u.focus(u.id("entry:term"));
                    u.pop_id();
                }
            }
            if (t != "passage" && t != "section") {
                u.space(10 * S);
                u.flow();
                label_mid(u, "Points:");
                entry(u, "points", strs["points"], 5 * k.f->width("0") + 20 * S);
                if (t == "match") label_mid(u, "(0 = one per pair)", false, true);
                if (t == "chart") label_mid(u, "(0 = not scored)", false, true);
                u.place(14 * S, 1);
                label_mid(u, "Standard:");
                dropdown(u, "standard", std_opts, strs["standard"], 26);
                u.flow(false);
                if (!has_stds) {
                    u.space(2 * S);
                    label(u, "Add standards to this assessment (on the left) to tag questions with them.", false, true, true);
                }
            }
            u.end();
            u.end_scroll();
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    // what was typed
    for (const auto &key : texts) {
        if (text::starts_with(key, "line:")) {
            std::string kk = key.substr(5);
            q[kk] = strs[kk];
        } else if (key == "heads" || key == "side")
            q[key] = text_list(strs[key]);
        else if (key == "center")
            q[key] = strs[key];
        else
            q[key] = text::strip(strs[key]);
    }
    if (t == "passage") q["numbered"] = numbered;
    if (t == "tf") q["answer"] = tf == "T" ? Value(true) : tf == "F" ? Value(false) : Value();
    if (t != "passage" && t != "section") {
        q["points"] = store::parse_points(strs["points"], q.get("points"));
        q["standard"] = strs["standard"];
    }
    if (t == "mc") {
        Value cs = Value::array();
        std::vector<int> kept;
        for (size_t i = 0; i < choices.size(); i++)
            if (!text::strip(choices[i]).empty()) {
                cs.push(text::strip(choices[i]));
                kept.push_back((int)i);
            }
        q["choices"] = cs;
        auto it = std::find(kept.begin(), kept.end(), ans);
        q["answer"] = ans >= 0 && it != kept.end() ? Value((int)(it - kept.begin())) : Value();
    }
    if (t == "match") {
        Value ps = Value::array();
        for (const auto &p : pairs)
            if (!text::strip(p.first).empty()) ps.push(Value(json::Array{text::strip(p.first), text::strip(p.second)}));
        q["pairs"] = ps;
    }
    qq = q;
    return true;
}

// ----------------------------------------------------------------------------- the list

class AssessmentList : public ItemList {
public:
    explicit AssessmentList(App &a) : ItemList(a, "assessment", "assessments", "assess_sort", "item") {
        search_fields_ = {"title", "kind", "course", "unit", "standards"};
        columns_ = {{"Type", 140, false}, {"Title", 340, true}, {"Unit", 110, false}, {"Size", 170, false}, {"Last Changed", 150, false}};
    }
    std::string title() override { return "Assessments & Assignments"; }

protected:
    std::string type_;
    void tools(Ui &ui) override {
        if (tool(ui, "New\xE2\x80\xA6", true, true)) cmd_new();
        if (tool_sel(ui, "Open")) cmd_open();
        if (tool_sel(ui, "Copy")) cmd_duplicate();
        if (tool_sel(ui, "Rename\xE2\x80\xA6")) cmd_rename();
        if (tool_sel(ui, "Delete")) cmd_delete();
        tool_gap(ui);
        if (tool_sel(ui, "Preview")) cmd_preview();
        Value *a = selected();
        if (tool(ui, "Answer Key", a && !is_sheet(a->s("kind")))) cmd_key();
        if (tool_sel(ui, "Export\xE2\x80\xA6")) cmd_export();
    }
    void filters(Ui &ui) override {
        int S = ui.S();
        label_mid(ui, "Type:");
        ui.place(6 * S, 1);
        std::vector<std::pair<std::string, std::string>> opts = {{"", "All types"}};
        for (const auto &k : store::ASSESSMENT_KINDS) opts.push_back({k, k});
        for (const auto &k : store::SHEET_KINDS) opts.push_back({k, k});
        dropdown(ui, "type", opts, type_, 14);
        ui.place(14 * S, 1);
    }
    double filters_w(Ui &ui) override {
        Skin &k = ui.sk();
        int S = ui.S();
        return k.f->width("Type:") + 6 * S + std::ceil(std::min(k.f->width("0") * 14, 520.0 * S) + k.line + 16 * S + (k.soft ? 6 * S : 0)) + 14 * S;
    }
    bool match(const Value &a) override { return type_.empty() || a.s("kind") == type_; }
    std::vector<std::string> row(const Value &a) override {
        return {a.s("kind"), a.s("title").empty() ? "(untitled)" : a.s("title"), a.s("unit"), size_text(a), a.s("updated")};
    }
    std::vector<std::pair<std::string, std::function<void()>>> menu_items() override {
        return {{"Open", [this] { cmd_open(); }},       {"Preview", [this] { cmd_preview(); }},
                {"Answer Key", [this] { cmd_key(); }},  {"Export\xE2\x80\xA6", [this] { cmd_export(); }},
                {"", nullptr},                          {"Copy", [this] { cmd_duplicate(); }},
                {"Rename\xE2\x80\xA6", [this] { cmd_rename(); }}, {"Delete", [this] { cmd_delete(); }}};
    }
    void cmd_new() override {
        std::string id = new_assessment_dialog(app, "", unit_, new_class());
        if (!id.empty()) app.show(open_assessment(id));
    }
    void cmd_open() override {
        if (Value *a = selected()) app.show(open_assessment(a->s("id")));
    }
    void cmd_delete() override {
        Value *a = selected();
        if (!a) return;
        std::string id = a->s("id");
        size_t used = 0;
        for (const auto &l : app.store.data["lessons"].arr())
            for (const auto &x : l.get("assessments").arr())
                if (x.is_string() && x.str() == id) {
                    used++;
                    break;
                }
        std::string s = used != 1 ? "s" : "";
        std::string also = used ? "\n\nIt's linked to " + std::to_string(used) + " lesson" + s + "; the link" + s + " will be removed." : "";
        if (!confirm(app.ui, "Delete", "Delete " + q(or_untitled(a->s("title"))) + "?" + also, "Delete", "Cancel", "warn")) return;
        auto &arr = pool().arr_ref();
        arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const Value &x) { return x.s("id") == id; }), arr.end());
        for (auto &l : app.store.data["lessons"].arr_ref()) {
            if (!l.get("assessments").is_array()) continue;
            auto &ids = l["assessments"].arr_ref();
            for (size_t i = 0; i < ids.size(); i++)
                if (ids[i].is_string() && ids[i].str() == id) {   // the first, like list.remove
                    ids.erase(ids.begin() + (long)i);
                    break;
                }
        }
        app.save();
        app.status("Deleted.");
    }
    void cmd_preview() override {
        if (Value *a = selected()) app.preview(doc::assessment_doc(*a, app.store));
    }
    void cmd_key() {
        Value *a = selected();
        if (a && !is_sheet(a->s("kind")))
            app.preview(doc::assessment_doc(*a, app.store, 0, 1, true), (a->s("title").empty() ? "Untitled" : a->s("title")) + " (Key)");
    }
    void cmd_export() override {
        if (Value *a = selected()) export_dialog(app, "assessment", a->s("id"));
    }
};

// ----------------------------------------------------------------------------- the quiz / test editor

class AssessmentEditor : public Screen {
public:
    AssessmentEditor(App &a, std::string id) : Screen(a), id_(std::move(id)) {}
    std::string title() override {
        Value *a = app.assessment(id_);
        if (!a) return "Assessment";
        return (a->s("kind").empty() ? "Assessment" : a->s("kind")) + ": " + or_untitled(a->s("title"));
    }
    bool command(const std::string &name) override {
        if (name == "preview") cmd_preview();
        else if (name == "export") export_dialog(app, "assessment", id_);
        else if (name == "open") edit();
        else return false;
        return true;
    }
    void frame(Ui &ui, const Rect &body) override {
        Skin &k = ui.sk();
        int S = ui.S();
        if (!app.assessment(id_)) {
            app.back();
            return;
        }
        ui.push_id(id_);
        ui.begin(body, 0);
        begin_toolbar(ui);
        if (tool(ui, "Preview")) cmd_preview();
        if (tool(ui, "Answer Key")) cmd_key();
        if (tool(ui, "Export\xE2\x80\xA6")) export_dialog(app, "assessment", id_);
        tool_gap(ui);
        format_bar(ui);
        tool_gap(ui);
        if (toolbar_room(ui, k.f->width("Changes save by themselves.") + 4 * S)) label_mid(ui, "Changes save by themselves.", false, true);
        end_toolbar(ui);
        ui.fill(ui.cut_top(S), k.soft ? k["line"] : k["dark"]);
        Rect rest = ui.remaining();
        ui.end();
        double lw = std::max(320.0 * S, std::min(440.0 * S, rest.w * 0.4));
        Rect left{rest.x, rest.y, lw, rest.h}, right{rest.x + lw + 6 * S, rest.y, rest.w - lw - 6 * S, rest.h};
        ui.begin_scroll(ui.id("about"), left);
        ui.begin({ui.remaining().x + 10 * S, ui.remaining().y, ui.width() - 20 * S, 1e9}, -1);
        settings(ui);
        ui.end();
        ui.end_scroll();
        questions(ui, right);
        ui.pop_id();
        std::string t = title();
        if (t != app.current_title) app.set_title(t);
    }

private:
    std::string id_;
    int sel_i_ = -1;   // the selected question, by its place (questions have no ids)
    bool first_ = true;

    Value &a() { return *app.assessment(id_); }
    void set(const std::string &key, const Value &v) {
        a()[key] = v;
        app.touch(a());
    }

    void settings(Ui &ui) {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.space(10 * S);
        begin_group(ui, "About This " + (a().s("kind").empty() ? std::string("Assessment") : a().s("kind")));
        double lw = 0;
        for (const char *l : {"Title:", "Unit:", "Course:", "Type:", "Grades:"}) lw = std::max(lw, k.f->width(l));
        lw += 8 * S;
        for (auto p : std::vector<std::pair<std::string, std::string>>{{"title", "Title"}, {"unit", "Unit"}, {"course", "Course"}}) {
            ui.row({lw, -1}, ui.ctl_h());
            label_mid(ui, p.second + ":");
            line_field(app, ui, p.first, a(), p.first);
        }
        ui.row({lw, 0}, ui.ctl_h());
        label_mid(ui, "Type:");
        std::string kind = a().s("kind");
        if (std::find(store::ASSESSMENT_KINDS.begin(), store::ASSESSMENT_KINDS.end(), kind) == store::ASSESSMENT_KINDS.end()) kind = "Quiz";
        std::vector<std::pair<std::string, std::string>> kopts;
        for (const auto &x : store::ASSESSMENT_KINDS) kopts.push_back({x, x});
        if (dropdown(ui, "kind", kopts, kind, 14)) set("kind", kind);
        ui.row({lw, 0}, ui.ctl_h());
        label_mid(ui, "Grades:");
        std::string gr = a().s("grades");
        std::vector<std::pair<std::string, std::string>> gopts;
        for (const auto &x : store::GRADE_CHOICES) gopts.push_back({x, grade_label(x)});
        if (std::find(store::GRADE_CHOICES.begin(), store::GRADE_CHOICES.end(), gr) == store::GRADE_CHOICES.end()) gopts.insert(gopts.begin(), {gr, grade_label(gr)});
        if (dropdown(ui, "grades", gopts, gr, 14)) set("grades", gr);
        ui.row({});
        end_group(ui);
        ui.space(10 * S);
        begin_group(ui, "Standards");
        standards_field(app, ui, "standards", [this] { return app.assessment(id_); });
        end_group(ui);
        ui.space(10 * S);
        begin_group(ui, "Directions (Printed at the Top)");
        auto_text(app, ui, "instructions", a(), "instructions", 3);
        end_group(ui);
        ui.space(10 * S);
        begin_group(ui, "On the Student Copy");
        for (auto p : std::vector<std::pair<std::string, std::string>>{
                 {"show_name", "Name / date / period line"}, {"show_points", "Point values"}, {"show_standards", "The standards"}}) {
            bool v = a().get(p.first).truthy();
            if (check(ui, p.second, v)) set(p.first, v);
        }
        end_group(ui);
        ui.space(12 * S);
    }

    void questions(Ui &ui, const Rect &r) {
        Skin &k = ui.sk();
        int S = ui.S();
        ui.begin({r.x, r.y + 10 * S, r.w - 10 * S, r.h - 20 * S}, 0);
        // a group box by hand: it fills the height
        Rect g = ui.remaining();
        ui.end();
        if (ui.drawing) {
            Font &f = *k.fb;
            if (k.soft) {
                ui.shadow(g, 12 * S, 6 * S, k.dark ? 0.35 : 0.07);
                ui.rrect(g, 12 * S, k["card"], k["line"], S);
                ui.text(*k.fhead, g.x + 18 * S, g.y + 14 * S + k.fhead->ascent, "Questions", k["text"]);
            } else {
                double ty = std::round(g.y + f.line / 2);
                Rect fr{g.x, ty, g.w, g.b() - ty};
                if (k.bevel) {
                    ui.box(fr, k["shadow"], S);
                    ui.box({fr.x + S, fr.y + S, fr.w - S, fr.h - S}, k["light"], S);
                    ui.box({fr.x, fr.y, fr.w - S, fr.h - S}, k["shadow"], S);
                } else
                    ui.box(fr, k["dark"], S);
                ui.fill({g.x + 7 * S, g.y, f.width(" Questions "), f.line}, k["window"]);
                ui.text(f, g.x + 7 * S, g.y + f.ascent, " Questions ", k["text"]);
            }
        }
        double padx = k.soft ? 18 * S : 10 * S;
        Rect in{g.x + padx, g.y + (k.soft ? k.fhead->line + 24 * S : k.fb->line + 6 * S), g.w - 2 * padx, 0};
        in.h = g.b() - in.y - (k.soft ? 16 * S : 8 * S);
        ui.begin(in, 0);
        Value &qs = a()["questions"];
        int n = (int)qs.size();
        if (sel_i_ >= n) sel_i_ = n - 1;
        bool has = sel_i_ >= 0 && sel_i_ < n;
        // the buttons
        Rect bar = ui.cut_top(ui.ctl_h() + 6 * S);
        ui.begin(bar, 6 * S);
        ui.flow();
        if (button(ui, "Add Question \xE2\x96\xBC")) {
            Rect b = ui.last_rect();
            std::vector<MenuEntry> items;
            for (const auto &qt : store::QUESTION_TYPES) {
                MenuEntry e;
                e.label = lookup(TYPE_TEXT, qt.type);
                items.push_back(e);
            }
            int got = popup_menu(ui, b.x, b.b(), items);
            if (got >= 0) add(store::QUESTION_TYPES[(size_t)got].type);
        }
        if (button(ui, "Edit\xE2\x80\xA6", has)) edit();
        if (button(ui, "Copy", has)) duplicate();
        if (button(ui, "Delete", has)) del();
        if (button(ui, "Move Up", has)) move(-1);
        if (button(ui, "Move Down", has)) move(1);
        ui.end();
        // the total
        Rect tot = ui.cut_bottom(k.fb->line + 6 * S);
        // the list
        std::vector<std::vector<std::string>> rows;
        int num = 0;
        double pts = 0;
        for (const auto &qq : qs.arr()) {
            std::string t = qq.s("type");
            pts += store::points_of(qq);
            if (t == "section") {
                rows.push_back({"", "SECT", "", "\xE2\x94\x80\xE2\x94\x80 " + (qq.s("title").empty() ? std::string("Section") : qq.s("title")) + " \xE2\x94\x80\xE2\x94\x80", ""});
                continue;
            }
            if (t == "passage") {
                size_t words = text::split_ws(qq.s("text")).size();
                rows.push_back({"", "TEXT", "", (qq.s("title").empty() ? std::string("Passage") : qq.s("title")) + "  (" + std::to_string(words) + " words)", ""});
                continue;
            }
            num++;
            std::string warn;
            if ((t == "mc" && (qq.get("answer").is_null() || !qq.get("choices").truthy())) || (t == "tf" && qq.get("answer").is_null()))
                warn = "   (no answer key yet)";
            std::string prompt = first_line(qq.s("prompt"));
            if (t == "chart") {
                std::string kind = "Chart";
                for (const auto &p : store::chart_presets())
                    if (p.key == qq.s("preset")) kind = p.label;
                prompt = "[" + kind + "]  " + (prompt.empty() ? lookup(store::CHART_DIRECTIONS, qq.s("layout")) : prompt);
            }
            rows.push_back({std::to_string(num) + ".", store::type_tag(t), store::fmt_points(store::points_of(qq)),
                            (prompt.empty() ? "(no prompt)" : prompt) + warn, qq.s("standard")});
        }
        Rect lr = ui.remaining();
        ui.end();
        int sel = sel_i_;
        if (first_) {
            ui.focus(ui.id("list:questions"));
            first_ = false;
        }
        ListResult res = list_view(ui, "questions", lr, {{"#", 44, false}, {"Type", 70, false}, {"Pts", 50, false}, {"Question", 380, true}, {"Standard", 120, false}},
                                   rows, sel, "No questions yet. Click Add Question to write the first one.");
        if (sel != sel_i_) {
            sel_i_ = sel;
            ui.again();
        }
        std::string total = std::to_string(num) + " question" + (num != 1 ? "s" : "") + ", " + store::fmt_points(pts) + " point" + (pts != 1 ? "s" : "");
        if (ui.drawing) ui.text_in(*k.fb, tot, total, k["text"], RIGHT);
        std::vector<std::string> stds;
        for (const auto &c : a().get("standards").arr()) stds.push_back(store::text_of(c));
        app.status(total, "Standards: " + (stds.empty() ? std::string("none") : text::join(stds, ", ")));
        if (res.open) edit();
        if (res.del) del();
        if (res.menu) {
            int got = popup_menu(ui, res.menu_x, res.menu_y, entries({"Edit\xE2\x80\xA6", "Copy", "Delete", "Move Up", "Move Down"}));
            if (got == 0) edit();
            else if (got == 1) duplicate();
            else if (got == 2) del();
            else if (got == 3) move(-1);
            else if (got == 4) move(1);
        }
    }

    void changed(int select) {
        sel_i_ = select;
        app.touch(a());
        app.ui.again();
    }

    std::string default_std() {
        const Value &s = a().get("standards");
        return s.size() == 1 ? store::text_of(s.at(0)) : "";
    }

    void add(const std::string &t) {
        Value qq = store::new_question(t, default_std());
        if (t == "match") qq["prompt"] = "Match each term with its definition.";
        if (!question_dialog(app, id_, qq, true)) return;
        auto &qs = a()["questions"].arr_ref();
        int at = sel_i_ < 0 || sel_i_ >= (int)qs.size() ? (int)qs.size() : sel_i_ + 1;
        qs.insert(qs.begin() + at, qq);
        changed(at);
    }

    void edit() {
        auto &qs0 = a()["questions"].arr_ref();
        if (sel_i_ < 0 || sel_i_ >= (int)qs0.size()) return;
        Value qq = qs0[(size_t)sel_i_];
        int at = sel_i_;
        if (!question_dialog(app, id_, qq, false)) return;
        auto &qs = a()["questions"].arr_ref();
        if (at < (int)qs.size()) qs[(size_t)at] = qq;
        changed(at);
    }

    void duplicate() {
        auto &qs = a()["questions"].arr_ref();
        if (sel_i_ < 0 || sel_i_ >= (int)qs.size()) return;
        Value dup = qs[(size_t)sel_i_];
        qs.insert(qs.begin() + sel_i_ + 1, dup);
        changed(sel_i_ + 1);
    }

    void del() {
        auto &qs0 = a()["questions"].arr_ref();
        if (sel_i_ < 0 || sel_i_ >= (int)qs0.size()) return;
        const Value &qq = qs0[(size_t)sel_i_];
        std::string what = first_line(!qq.s("prompt").empty() ? qq.s("prompt") : qq.s("title"));
        int at = sel_i_;
        if (!confirm(app.ui, "Delete Question",
                     "Delete this " + text::lower(lookup(TYPE_TEXT, qq.s("type"))) + " question?\n\n" + q(what.empty() ? "(blank)" : what), "Delete", "Cancel",
                     "warn"))
            return;
        auto &qs = a()["questions"].arr_ref();
        if (at >= (int)qs.size()) return;
        qs.erase(qs.begin() + at);
        changed(qs.empty() ? -1 : std::min(at, (int)qs.size() - 1));
    }

    void move(int d) {
        auto &qs = a()["questions"].arr_ref();
        int i = sel_i_, j = i + d;
        if (i < 0 || i >= (int)qs.size() || j < 0 || j >= (int)qs.size()) return;
        std::swap(qs[(size_t)i], qs[(size_t)j]);
        changed(j);
    }

    void cmd_preview() {
        app.save();
        app.preview(doc::assessment_doc(a(), app.store));
    }
    void cmd_key() {
        app.save();
        app.preview(doc::assessment_doc(a(), app.store, 0, 1, true), (a().s("title").empty() ? "Untitled" : a().s("title")) + " (Key)");
    }
};

// ----------------------------------------------------------------------------- annotation & bell ringer sheets

class SheetEditor : public Screen {
public:
    SheetEditor(App &a, std::string id) : Screen(a), id_(std::move(id)) {
        Value *x = a.assessment(id_);
        if (x && x->s("kind") == store::BELL_SHEET) {
            if (!x->has("weeks")) (*x)["weeks"] = Value(json::Array{"", ""});
            if (!x->has("days")) (*x)["days"] = Value(json::Array(10, Value("")));
        }
        a.status("Last saved " + (x && !x->s("updated").empty() ? x->s("updated") : std::string("\xE2\x80\x93")));
    }
    std::string title() override {
        Value *a = app.assessment(id_);
        return a ? a->s("kind") + ": " + or_untitled(a->s("title")) : "Sheet";
    }
    bool command(const std::string &name) override {
        if (name == "preview") cmd_preview();
        else if (name == "export") export_dialog(app, "assessment", id_);
        else return false;
        return true;
    }
    void frame(Ui &ui, const Rect &body) override {
        Skin &k = ui.sk();
        int S = ui.S();
        if (!app.assessment(id_)) {
            app.back();
            return;
        }
        bool bell = a().s("kind") == store::BELL_SHEET;
        ui.push_id(id_);
        ui.begin(body, 0);
        begin_toolbar(ui);
        if (tool(ui, "Preview")) cmd_preview();
        if (tool(ui, "Export\xE2\x80\xA6")) export_dialog(app, "assessment", id_);
        tool_gap(ui);
        format_bar(ui);
        tool_gap(ui);
        label_mid(ui, kind_help(a().s("kind")), false, true);
        end_toolbar(ui);
        ui.fill(ui.cut_top(S), k.soft ? k["line"] : k["dark"]);
        Rect rest = ui.remaining();
        ui.end();
        if (first_) {
            focus_at_end(ui, ui.id("entry:title"));
            first_ = false;
        }
        ui.begin_scroll(ui.id("form"), rest, 1000 * S);
        ui.begin({ui.remaining().x + 12 * S, ui.remaining().y, ui.width() - 24 * S, 1e9}, -1);
        ui.space(10 * S);
        begin_group(ui, "Sheet");
        double lw = k.f->width("Rows in the chart:") + 8 * S;
        for (auto p : std::vector<std::pair<std::string, std::string>>{{"title", bell ? "Title" : "Heading"}, {"unit", "Unit"}, {"course", "Course"}}) {
            ui.row({lw, -1}, ui.ctl_h());
            label_mid(ui, p.second + ":");
            line_field(app, ui, p.first, a(), p.first);
        }
        if (!bell) {
            ui.row({lw, 0}, ui.ctl_h());
            label_mid(ui, "Rows in the chart:");
            std::vector<std::pair<std::string, std::string>> opts;
            for (int n = 1; n <= 20; n++) opts.push_back({std::to_string(n), std::to_string(n) + (n == 10 ? " (fills one page)" : "")});
            std::string rows = a().get("rows").truthy() ? store::text_of(a().get("rows")) : "10";
            if (dropdown(ui, "rows", opts, rows, 16)) {
                a()["rows"] = std::stoi(rows);
                app.touch(a());
            }
        }
        ui.row({});
        end_group(ui);
        ui.space(10 * S);
        begin_group(ui, "Directions (Optional)");
        auto_text(app, ui, "instructions", a(), "instructions", 2);
        end_group(ui);
        if (bell) {
            for (int w = 0; w < 2; w++) {
                ui.push_id((Id)w + 1);
                ui.space(10 * S);
                begin_group(ui, "Week " + std::to_string(w + 1) + " (" + (w == 0 ? "front" : "back") + " of the page)");
                ui.flow();
                label_mid(ui, "Week of:");
                std::string wk = store::text_of(a()["weeks"].at((size_t)w));
                if (entry(ui, "week", wk, 18 * k.f->width("0") + 20 * S)) {
                    while (a()["weeks"].size() <= (size_t)w) a()["weeks"].push("");
                    a()["weeks"].at((size_t)w) = wk;
                    app.touch(a());
                }
                label_mid(ui, "(like Oct 5\xE2\x80\x93" "9; blank prints a line to fill in)", false, true);
                ui.flow(false);
                for (int dd = 0; dd < 5; dd++) {
                    int i = w * 5 + dd;
                    ui.push_id((Id)dd + 1);
                    ui.space(6 * S);
                    double bw = ui.btn_w("Random Good Thing", true);
                    Rect row = ui.place(0, k.fsmall->line + 8 * S);
                    if (ui.drawing) {
                        double x = ui.text(*k.fb, row.x, row.y + (row.h - k.fb->line) / 2 + k.fb->ascent, store::WEEKDAYS[(size_t)dd], k["text"]);
                        ui.text(*k.fsmall, x, row.y + (row.h - k.fb->line) / 2 + k.fb->ascent, "  blank: students write the prompt", k["dim"]);
                    }
                    if (button_at(ui, ui.id("btn:good"), {row.r() - bw, row.y, bw, row.h}, "Random Good Thing", true, false, true)) good_thing(i);
                    while (a()["days"].size() <= (size_t)i) a()["days"].push("");
                    std::string v = store::text_of(a()["days"].at((size_t)i));
                    if (text_area(ui, "day", v, 1, 6)) {
                        a()["days"].at((size_t)i) = v;
                        app.touch(a());
                    }
                    ui.pop_id();
                }
                end_group(ui);
                ui.pop_id();
            }
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
    Value &a() { return *app.assessment(id_); }

    void good_thing(int i) {
        std::string cur = text::strip(store::text_of(a()["days"].at((size_t)i)));
        if (!cur.empty() && !text::starts_with(cur, store::GOOD_THINGS_PREFIX) &&
            !confirm(app.ui, "Bell Ringer", "Replace " + store::WEEKDAYS[(size_t)(i % 5)] + "'s prompt with a random Good Things prompt?", "Replace"))
            return;
        std::string t = app.store.good_thing(cur);
        a()["days"].at((size_t)i) = t;
        app.touch(a());
    }
    void cmd_preview() {
        app.save();
        app.preview(doc::assessment_doc(a(), app.store));
    }
};

}  // namespace

Factory assessment_list() { return make<AssessmentList>(); }

Factory open_assessment(const std::string &id) {
    return [id](App &app) -> std::unique_ptr<Screen> {
        Value *x = app.assessment(id);
        if (!x) return make<AssessmentList>()(app);
        if (is_sheet(x->s("kind"))) return std::unique_ptr<Screen>(new SheetEditor(app, id));
        return std::unique_ptr<Screen>(new AssessmentEditor(app, id));
    };
}

std::string new_assessment_dialog(App &app, const std::string &lesson_id, const std::string &unit, const std::string &cls) {
    std::string kind = "Quiz", title;
    bool first = true;
    auto lesson_title = [&]() -> std::string {
        Value *l = lesson_id.empty() ? nullptr : app.lesson(lesson_id);
        return l ? l->s("title") : "";
    };
    int r = dialog(
        app.ui, {"New Assessment or Worksheet", 600, 0},
        [&](Ui &u) {
            int S = u.S();
            label(u, "What are you making?", true);
            u.space(4 * S);
            std::string before = kind;
            const auto &ks = store::ASSESSMENT_KINDS;
            size_t rows = 4, cols = (ks.size() + rows - 1) / rows;
            double cw = std::floor((std::min(u.width(), 520.0 * S) - (double)(cols - 1) * u.lay().gap) / (double)cols);
            for (size_t rr = 0; rr < rows; rr++) {
                u.row(std::vector<double>(cols, cw));
                for (size_t c = 0; c < cols; c++) {
                    size_t i = c * rows + rr;
                    if (i >= ks.size()) {
                        u.place(1, 1);
                        continue;
                    }
                    if (radio(u, ks[i], kind == ks[i])) kind = ks[i];
                }
            }
            u.row({});
            for (const auto &k : store::SHEET_KINDS)
                if (radio(u, k + " \xE2\x80\x94 " + kind_help(k), kind == k, true, 520 * S)) kind = k;
            if (kind != before) {   // a fresh guess at the title for the new kind
                std::string lt = lesson_title();
                std::set<std::string> guesses = {"", "Bell Ringers"};
                if (!lt.empty()) guesses.insert(lt + " Annotation");
                if (guesses.count(title)) title = kind == store::ANNOTATION ? (lt.empty() ? "" : lt + " Annotation") : kind == store::BELL_SHEET ? "Bell Ringers" : "";
            }
            u.space(8 * S);
            label(u, "Title:");
            u.space(2 * S);
            if (first) {
                u.focus(u.id("entry:title"));
                first = false;
            }
            entry(u, "title", title);
            return 0;
        },
        {{"Create", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return "";
    Value a = store::new_assessment(app.settings(), kind);
    a["title"] = text::strip(title);
    if (Value *l = lesson_id.empty() ? nullptr : app.lesson(lesson_id)) {
        a["unit"] = l->s("unit");
        if (l->get("course").truthy()) a["course"] = l->get("course");
        if (l->get("grades").truthy()) a["grades"] = l->get("grades");
        Value stds = Value::array();
        for (const auto &s : l->get("standards").arr()) stds.push(s);
        a["standards"] = stds;
        if (!l->get("assessments").is_array()) (*l)["assessments"] = Value::array();
        (*l)["assessments"].push(a.s("id"));
    } else if (!unit.empty())
        a["unit"] = unit;
    if (!cls.empty()) a["course"] = cls;
    std::string id = a.s("id");
    app.store.data["assessments"].push(std::move(a));
    app.save();
    return id;
}

}  // namespace gui
