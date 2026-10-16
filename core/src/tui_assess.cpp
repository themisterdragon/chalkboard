// The terminal app's assessment screens: the list, sheets, quizzes and tests, and their questions.
#include <algorithm>

#include "doc.h"
#include "markup.h"
#include "text.h"
#include "tui_app.h"

namespace tui {

using json::Value;
using store::field;
using store::text_of;

static const char *LETTERS = "ABCDEFGHIJ";

static std::string key_text(int k) {
    if (k < 0x20 || k >= term::K_ENTER || k == 0x7f) return "";
    return text::to8((char32_t)k);
}

static std::string letter(long i) { return i >= 0 && i < 10 ? std::string(1, LETTERS[i]) : "?"; }

static std::string lpad(const std::string &s, size_t n) { return text::ljust(s, (long)n); }
static std::string rpad(const std::string &s, size_t n) { return text::rjust(s, (long)n); }

static std::string preset_label(const std::string &key, const std::string &dflt) {
    for (const auto &p : store::chart_presets()) if (p.key == key) return p.label;
    return dflt;
}

static std::string space_label(const std::string &v) {
    if (v == "lines") return "LINED";
    if (v == "blank") return "BLANK SPACE";
    if (v == "box") return "BORDERED BOX";
    return v;
}

static bool all_digits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

UI::Row App::assessment_row(const Value &a, int w) {
    std::string kind = a.s("kind"), stat;
    if (kind == store::ANNOTATION) stat = std::to_string(a.get("rows").truthy() ? a.get("rows").as_int() : 10) + " ROWS";
    else if (kind == store::BELL_SHEET) stat = "2 WEEKS";
    else {
        double pts = 0;
        long n = 0;
        for (const auto &q : a.get("questions").arr()) {
            pts += store::points_of(q);
            std::string t = q.s("type");
            if (t != "passage" && t != "section") n++;
        }
        stat = rpad(std::to_string(n), 3) + " Q  " + rpad(store::fmt_points(pts), 4) + " PTS";
    }
    std::string unit = truncate(text::strip(field(a, "unit")), 16);
    std::string right = lpad(unit, 16) + "  " + rpad(stat, 13);
    std::string t = field(a, "title");
    std::string left = lpad(text::slice(ui.tx(kind), 0, 17), 18) + (t.empty() ? "(untitled)" : t);
    UI::Row r;
    r.text = text::ljust(truncate(left, w - (long)text::length(right) - 2), w - (long)text::length(right)) + right;
    r.has_style = true;
    r.st = ui.HI;
    return r;
}

void App::assessments_screen() {
    Filter f;
    f.sort = "assess_sort";
    f.has_kind = true;
    auto items = [&] { return filtered("assessments", f, {"title", "kind", "course", "unit", "standards"}); };
    auto on_key = [&](int k, const std::string &id, long) -> Result {
        std::string c = text::lower(key_text(k));
        if (c == "n") {
            std::string aid = new_assessment_flow("", f.unit);
            if (!aid.empty() && field(st(), "assess_sort") == "class" && f.has_cls && !f.cls.empty()) {
                if (Value *a = assessment(aid)) (*a)["course"] = f.cls;
                save();
            }
            if (!aid.empty()) {
                open_assessment(aid);
                return Result::move(0);
            }
            return Result::none();
        }
        if (filter_key(c, f, "assessments")) return Result::move(0);
        Value *a = id.empty() ? nullptr : assessment(id);
        if (!a) return Result::none();
        std::vector<std::string> used;
        for (const auto &l : store_.data.get("lessons").arr())
            for (const auto &x : l.get("assessments").arr())
                if (x.is_string() && x.str() == id) { used.push_back(l.s("id")); break; }
        std::string also = used.empty() ? "" : " (LINKED TO " + std::to_string(used.size()) + " LESSON" + (used.size() != 1 ? "S" : "") + ")";
        std::string t = field(*a, "title");
        if (c == "d" && ui.confirm("DELETE '" + (t.empty() ? "untitled" : t) + "'" + also)) {
            auto &arr = store_.data["assessments"].arr_ref();
            for (size_t i = 0; i < arr.size(); i++)
                if (arr[i].s("id") == id) { arr.erase(arr.begin() + (long)i); break; }
            for (const auto &lid : used)
                if (Value *l = lesson(lid)) {
                    auto &links = (*l)["assessments"].arr_ref();
                    for (size_t i = 0; i < links.size(); i++)
                        if (links[i].is_string() && links[i].str() == id) { links.erase(links.begin() + (long)i); break; }
                }
            save();
            ui.msg = "DELETED.";
        } else if (c == "r") {
            std::string kind = field(*a, "kind");
            return rename("assessments", id, text::upper(kind.empty() ? "item" : kind), items);
        } else if (c == "c") {
            Value dup = *a;
            dup["id"] = store::new_id();
            dup["title"] = (t.empty() ? "Assessment" : t) + " (copy)";
            dup["created"] = store::now();
            dup["updated"] = store::now();
            store_.data["assessments"].push(dup);
            save();
            ui.msg = "DUPLICATED. THE COPY IS AT THE TOP.";
            return Result::move(0);
        } else if (c == "x") export_screen("assessment", id);
        else if (c == "p") preview(doc::assessment_doc(*a, store_));
        return Result::none();
    };
    bool any = store_.data.get("assessments").size() > 0;
    class_list(
        "ASSESSMENTS & ASSIGNMENTS", f, "assessments", items,
        [&](const std::string &id, int w) {
            Value *a = assessment(id);
            return a ? assessment_row(*a, w) : UI::Row();
        },
        [&](const std::string &id, long) {
            open_assessment(id);
            return Result::none();
        },
        on_key, "ITEM", [&] { return filter_info(f, items().size(), "ITEMS"); },
        "1-0 OPEN  N NEW  R RENAME  C COPY  D DEL  X EXPORT  P PREVIEW  / SEARCH  U UNIT  T TYPE  O SORT  ESC BACK",
        any ? "NOTHING MATCHES. PRESS N TO BUILD SOMETHING." : "NOTHING YET. PRESS N TO BUILD A QUIZ, TEST OR ASSIGNMENT.");
}

std::string App::new_assessment_flow(const std::string &lesson_id, const std::string &unit) {
    std::vector<std::string> kinds = store::ASSESSMENT_KINDS;
    kinds.insert(kinds.end(), store::SHEET_KINDS.begin(), store::SHEET_KINDS.end());
    std::vector<std::string> labels;
    for (const auto &k : store::ASSESSMENT_KINDS) labels.push_back(ui.tx(k));
    labels.push_back(ui.tx(std::string(store::ANNOTATION) + "  (line / symbol / reason chart)"));
    labels.push_back(ui.tx(std::string(store::BELL_SHEET) + "  (Mon-Fri, one week per side)"));
    long j = ui.choose("WHAT ARE YOU MAKING?", labels);
    if (j < 0) return "";
    std::string kind = kinds[(size_t)j], dflt;
    Value *l = lesson_id.empty() ? nullptr : lesson(lesson_id);
    if (kind == store::ANNOTATION) dflt = l && l->get("title").truthy() ? field(*l, "title") + " Annotation" : "";
    else if (kind == store::BELL_SHEET) dflt = "Bell Ringers";
    std::string t;
    if (!ui.prompt("TITLE FOR THIS " + kind + (kind == store::ANNOTATION ? " (PRINTS AS THE HEADING)" : ""), t, dflt)) return "";
    std::string aid = add_assessment(kind, t, lesson_id);
    if (!unit.empty() && lesson_id.empty())
        if (Value *a = assessment(aid)) (*a)["unit"] = unit;
    return aid;
}

std::string App::add_assessment(const std::string &kind, const std::string &title, const std::string &lesson_id) {
    Value a = store::new_assessment(st(), kind);
    a["title"] = title;
    if (Value *l = lesson_id.empty() ? nullptr : lesson(lesson_id)) {
        a["unit"] = field(*l, "unit");
        if (l->get("course").truthy()) a["course"] = l->get("course");
        if (l->get("grades").truthy()) a["grades"] = l->get("grades");
        Value cs = Value::array();
        for (const auto &c : l->get("standards").arr()) cs.push(c);
        a["standards"] = cs;
        l->setdefault("assessments", Value::array()).push(a.s("id"));
    }
    std::string id = a.s("id");
    store_.data["assessments"].push(a);
    save();
    return id;
}

void App::open_assessment(const std::string &id) {
    Value *a = assessment(id);
    if (!a) return;
    std::string kind = a->s("kind");
    if (std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), kind) != store::SHEET_KINDS.end()) sheet_editor(id);
    else assessment_editor(id);
}

void App::sheet_editor(const std::string &id) {
    Value *a0 = assessment(id);
    if (!a0) return;
    bool bell = a0->s("kind") == store::BELL_SHEET;
    struct F { std::string key, label, kind; long extra; };
    std::vector<F> fields;
    if (bell) {
        // a bell ringer sheet always has two weeks and ten days
        if (!a0->get("weeks").is_array()) (*a0)["weeks"] = Value::array();
        if (!a0->get("days").is_array()) (*a0)["days"] = Value::array();
        while ((*a0)["weeks"].size() < 2) (*a0)["weeks"].push("");
        while ((*a0)["days"].size() < 10) (*a0)["days"].push("");
        fields = {{"title", "TITLE", "line", 0}, {"unit", "UNIT", "line", 0}, {"course", "COURSE", "line", 0}, {"instructions", "DIRECTIONS (OPTIONAL)", "text", 0}};
        for (long w = 0; w < 2; w++) {
            fields.push_back({"week", "WEEK " + std::to_string(w + 1) + " (" + (w == 0 ? "FRONT" : "BACK") + ") WEEK OF", "", w});
            for (long d = 0; d < 5; d++) fields.push_back({"day", "  " + text::upper(store::WEEKDAYS[(size_t)d]) + " PROMPT", "", w * 5 + d});
        }
    } else
        fields = {{"title", "HEADING (UNDER NAME/DATE)", "line", 0}, {"unit", "UNIT", "line", 0}, {"course", "COURSE", "line", 0},
                  {"rows", "ROWS IN THE CHART", "rows", 0},          {"instructions", "DIRECTIONS (OPTIONAL)", "text", 0}};
    auto value = [&](const F &f) -> std::string {
        Value *a = assessment(id);
        if (!a) return "-";
        if (f.key == "week") {
            std::string w = text_of(a->get("weeks").at((size_t)f.extra));
            return w.empty() ? "(blank line to fill in)" : w;
        }
        if (f.key == "day") {
            std::string d = first_line(text_of(a->get("days").at((size_t)f.extra)));
            return d.empty() ? "(blank: students write the prompt)" : d;
        }
        std::string v = first_line(a->get(f.key).truthy() ? text_of(a->get(f.key)) : "");
        return v.empty() ? "-" : v;
    };
    auto changed = [&] {
        if (Value *a = assessment(id)) {
            (*a)["updated"] = store::now();
            save();
        }
    };
    ui.list_screen(
        a0->get("title").truthy() ? field(*a0, "title") : "UNTITLED", [&] { return (long)fields.size(); },
        [&](long i, int) {
            const F &f = fields[(size_t)i];
            std::string v = value(f);
            UI::Row r;
            r.text = lpad(f.label, 30) + " " + v;
            r.has_style = true;
            r.st = (f.key != "week" && f.key != "day") || v[0] != '(' ? ui.HI : ui.DIM;
            return r;
        },
        [&](long i) -> Result {
            const F f = fields[(size_t)i];
            Value *a = assessment(id);
            if (!a) return Result::back();
            std::string v;
            if (f.key == "week") {
                if (ui.prompt("WEEK OF (E.G. OCT 5-9; BLANK = LINE TO FILL IN)", v, text_of(a->get("weeks").at((size_t)f.extra)))) {
                    if ((a = assessment(id))) (*a)["weeks"].at((size_t)f.extra) = v;
                    changed();
                }
                return Result::none();
            }
            if (f.key == "day") {
                v = text_of(a->get("days").at((size_t)f.extra));
                bool got = ui.edit_text(text::strip(f.label), v, "THE PROMPT PRINTED IN THIS DAY'S BOX. ESC SAVES.",
                                        [this](const std::string &cur) { return store_.good_thing(cur); }, "CTRL-G RANDOM GOOD THING",
                                        store::GOOD_THINGS_PREFIX);
                if (got) {
                    if ((a = assessment(id))) (*a)["days"].at((size_t)f.extra) = text::strip(v);
                    changed();
                    return Result::move(std::min(i + 1, (long)fields.size() - 1));
                }
                return Result::none();
            }
            bool got = false;
            Value nv;
            if (f.kind == "line") {
                got = ui.prompt(f.label, v, field(*a, f.key));
                nv = v;
            } else if (f.kind == "text") {
                v = field(*a, f.key);
                got = ui.edit_text(f.label, v, "PRINTED UNDER THE HEADING. ESC SAVES.");
                nv = v;
            } else if (f.kind == "rows") {
                std::string cur = a->get("rows").truthy() ? text_of(a->get("rows")) : "10";
                if (ui.prompt("ROWS (1-20; 10 FILLS ONE PAGE NICELY)", v, cur, true) && all_digits(v) && v.size() < 4) {
                    int n = std::atoi(v.c_str());
                    if (n >= 1 && n <= 20) {
                        nv = n;
                        got = true;
                    }
                }
            }
            if (got && (a = assessment(id))) {
                (*a)[f.key] = nv;
                changed();
            }
            return Result::none();
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(key_text(k));
            Value *a = assessment(id);
            if (!a) return Result::back();
            if (c == "p") preview(doc::assessment_doc(*a, store_));
            else if (c == "x") export_screen("assessment", id);
            else if (c == "g" && i >= 0 && fields[(size_t)i].key == "day") {
                Value &d = (*a)["days"].at((size_t)fields[(size_t)i].extra);
                d = store_.good_thing(text_of(d));
                changed();
            }
            return Result::none();
        },
        std::string("1-0 CHANGE  ") + (bell ? "G GOOD THING ON A DAY  " : "") + "P PREVIEW  X EXPORT  ESC BACK", [&] {
            return std::string(bell ? "BELL RINGER SHEET: 2 PAGES, PRINT DOUBLE-SIDED" : "ANNOTATION SHEET: NAME/DATE, HEADING, LINE / SYMBOL / REASON CHART ON ONE PAGE");
        });
}

void App::assessment_editor(const std::string &id) {
    auto qs = [&]() -> Value * {
        Value *a = assessment(id);
        if (!a) return nullptr;
        if (!a->get("questions").is_array()) (*a)["questions"] = Value::array();
        return &(*a)["questions"];
    };
    auto touch = [&] {
        if (Value *a = assessment(id)) {
            (*a)["updated"] = store::now();
            save();
        }
    };
    auto row = [&](long i, int) {
        UI::Row r;
        Value *list = qs();
        if (!list) return r;
        const Value &q = list->at((size_t)i);
        std::string t = q.s("type");
        long num = 0;
        for (long k = 0; k <= i; k++) {
            std::string tk = list->at((size_t)k).s("type");
            if (tk != "passage" && tk != "section") num++;
        }
        if (t == "section") {
            std::string title = field(q, "title");
            r.text = "     == " + (title.empty() ? "SECTION" : title) + " ==";
            r.has_style = true;
            r.st = ui.HI;
            return r;
        }
        if (t == "passage") {
            std::string title = field(q, "title");
            r.text = "     [TEXT] " + (title.empty() ? "Passage" : title) + "  (" + std::to_string(text::split_ws(field(q, "text")).size()) + " words)";
            r.has_style = true;
            r.st = ui.DIM;
            return r;
        }
        std::string warn;
        if (t == "mc" && (q.get("answer").is_null() || !q.get("choices").truthy())) warn = " (!NO KEY)";
        if (t == "tf" && q.get("answer").is_null()) warn = " (!NO KEY)";
        std::string pts = store::fmt_points(store::points_of(q));
        std::string prompt = first_line(field(q, "prompt"));
        if (t == "chart") {
            std::string pr = prompt.empty() ? store::chart_direction(q.s("layout")) : prompt;
            if (prompt.empty()) {
                pr = "";
                for (const auto &d : store::CHART_DIRECTIONS) if (d.first == q.s("layout")) pr = d.second;
            }
            prompt = "[" + text::upper(preset_label(q.s("preset"), "Chart")) + "] " + pr;
        }
        r.text = rpad(std::to_string(num), 3) + ". " + lpad(store::type_tag(t), 5) + rpad(pts, 3) + "p  " + (prompt.empty() ? "(no prompt)" : prompt) + warn;
        return r;
    };
    auto info = [&] {
        Value *a = assessment(id);
        if (!a) return std::string();
        double pts = 0;
        long n = 0;
        for (const auto &q : a->get("questions").arr()) {
            pts += store::points_of(q);
            std::string t = q.s("type");
            if (t != "passage" && t != "section") n++;
        }
        std::vector<std::string> cs;
        for (const auto &c : a->get("standards").arr()) cs.push_back(text_of(c));
        return text::upper(field(*a, "kind")) + "  |  " + std::to_string(n) + " QUESTIONS  |  " + store::fmt_points(pts) + " POINTS  |  STANDARDS: " +
               (cs.empty() ? "NONE" : text::join(cs, ", "));
    };
    auto on_key = [&](int k, long i) -> Result {
        std::string c = text::lower(key_text(k));
        Value *a = assessment(id);
        if (!a) return Result::back();
        if (c == "a") {
            long at = i < 0 ? (long)qs()->size() : i + 1;
            Value nq;
            if (add_question(id, nq)) {
                Value *list = qs();
                if (!list) return Result::back();
                at = std::min(at, (long)list->size());
                list->arr_ref().insert(list->arr_ref().begin() + at, nq);
                touch();
                return Result::move(at);
            }
            return Result::none();
        }
        if (c == "s") {
            assessment_settings(id);
            return Result::none();
        }
        if (c == "x") {
            export_screen("assessment", id);
            return Result::none();
        }
        if (c == "p") {
            preview(doc::assessment_doc(*a, store_));
            return Result::none();
        }
        if (c == "k") {
            preview(doc::assessment_doc(*a, store_, 0, 1, true));
            return Result::none();
        }
        Value *list = qs();
        if (i < 0 || !list || i >= (long)list->size()) return Result::none();
        auto &arr = list->arr_ref();
        if (c == "d" && ui.confirm("DELETE THIS " + store::type_label(arr[(size_t)i].s("type")))) {
            if ((list = qs()) && i < (long)list->size()) list->arr_ref().erase(list->arr_ref().begin() + i);
            touch();
            return Result::move(std::max(0L, i - 1));
        }
        if (c == "c") {
            Value copy = arr[(size_t)i];
            arr.insert(arr.begin() + i + 1, copy);
            touch();
            return Result::move(i + 1);
        }
        if ((c == "+" || c == "=") && i < (long)arr.size() - 1) {
            std::swap(arr[(size_t)i], arr[(size_t)i + 1]);
            touch();
            return Result::move(i + 1);
        }
        if ((c == "-" || c == "_") && i > 0) {
            std::swap(arr[(size_t)i], arr[(size_t)i - 1]);
            touch();
            return Result::move(i - 1);
        }
        if (c == "m") {
            std::string to;
            if (ui.prompt("MOVE TO POSITION (1-" + std::to_string(arr.size()) + ")", to) && all_digits(to) && to.size() < 6 && (list = qs())) {
                auto &a2 = list->arr_ref();
                long j = std::max(0L, std::min((long)a2.size() - 1, (long)std::atol(to.c_str()) - 1));
                Value moving = a2[(size_t)i];
                a2.erase(a2.begin() + i);
                a2.insert(a2.begin() + j, moving);
                touch();
                return Result::move(j);
            }
        }
        return Result::none();
    };
    Value *a0 = assessment(id);
    ui.list_screen(
        a0 && a0->get("title").truthy() ? field(*a0, "title") : "UNTITLED", [&] { return qs() ? (long)qs()->size() : 0L; }, row,
        [&](long i) {
            question_editor(id, [this, id, i]() -> Value * {
                Value *a = assessment(id);
                if (!a || !a->get("questions").is_array() || i >= (long)a->get("questions").size()) return nullptr;
                return &(*a)["questions"].at((size_t)i);
            });
            touch();
            return Result::none();
        },
        on_key, "A ADD  1-0 EDIT  D DEL  C COPY  +/- MOVE  S SETTINGS  P PREVIEW  K KEY  X EXPORT  ESC BACK", info,
        "NO QUESTIONS YET. PRESS A TO ADD ONE, OR S FOR TITLE/DIRECTIONS/STANDARDS.");
}

void App::assessment_settings(const std::string &id) {
    struct F { std::string key, label, kind; };
    std::vector<F> fields = {{"title", "TITLE", "line"},
                             {"kind", "TYPE", "kind"},
                             {"unit", "UNIT", "line"},
                             {"course", "COURSE", "line"},
                             {"grades", "GRADE BAND", "grades"},
                             {"standards", "STANDARDS", "standards"},
                             {"instructions", "DIRECTIONS", "text"},
                             {"show_name", "NAME / DATE / PERIOD LINE", "bool"},
                             {"show_points", "SHOW POINT VALUES", "bool"},
                             {"show_standards", "PRINT STANDARDS ON STUDENT COPY", "bool"}};
    Value *a0 = assessment(id);
    std::string t0 = a0 ? field(*a0, "title") : "";
    ui.list_screen(
        "SETTINGS: " + (t0.empty() ? "untitled" : t0), [&] { return (long)fields.size(); },
        [&](long i, int) {
            UI::Row r;
            Value *a = assessment(id);
            if (!a) return r;
            const F &f = fields[(size_t)i];
            std::string v;
            if (f.kind == "bool") v = a->get(f.key).truthy() ? "YES" : "NO";
            else if (f.kind == "standards") {
                std::vector<std::string> cs;
                for (const auto &c : a->get(f.key).arr()) cs.push_back(text_of(c));
                v = cs.empty() ? "-" : text::join(cs, ", ");
            } else {
                v = first_line(a->get(f.key).truthy() ? text_of(a->get(f.key)) : "");
                if (v.empty()) v = "-";
            }
            r.text = lpad(f.label, 34) + " " + v;
            r.has_style = true;
            r.st = ui.HI;
            return r;
        },
        [&](long i) -> Result {
            Value *a = assessment(id);
            if (!a) return Result::back();
            const F f = fields[(size_t)i];
            Value nv;
            bool got = false;
            std::string v;
            if (f.kind == "line") {
                got = ui.prompt(f.label, v, field(*a, f.key));
                nv = v;
            } else if (f.kind == "text") {
                v = field(*a, f.key);
                got = ui.edit_text(f.label, v, "DIRECTIONS PRINTED AT THE TOP. ESC SAVES.");
                nv = v;
            } else if (f.kind == "kind") {
                std::vector<std::string> labels;
                for (const auto &k : store::ASSESSMENT_KINDS) labels.push_back(ui.tx(k));
                auto it = std::find(store::ASSESSMENT_KINDS.begin(), store::ASSESSMENT_KINDS.end(), a->s("kind"));
                long j = ui.choose("TYPE", labels, it == store::ASSESSMENT_KINDS.end() ? -1 : (long)(it - store::ASSESSMENT_KINDS.begin()));
                if (j >= 0) {
                    nv = store::ASSESSMENT_KINDS[(size_t)j];
                    got = true;
                }
            } else if (f.kind == "grades") {
                if (pick_grade(field(*a, "grades"), v)) {
                    nv = v;
                    got = true;
                }
            } else if (f.kind == "bool") {
                nv = !a->get(f.key).truthy();
                got = true;
            } else {
                std::vector<std::string> cur;
                for (const auto &c : a->get("standards").arr()) cur.push_back(text_of(c));
                std::string g = a->get("grades").truthy() ? field(*a, "grades") : field(st(), "grades");
                auto picked = standards_picker(cur, g);
                nv = Value::array();
                for (const auto &c : picked) nv.push(c);
                got = true;
            }
            if (got && (a = assessment(id))) {
                (*a)[f.key] = nv;
                (*a)["updated"] = store::now();
                save();
            }
            return Result::none();
        },
        nullptr, "1-0 CHANGE  ESC BACK (AUTOSAVED)", [] { return std::string("THESE PRINT AT THE TOP OF THE STUDENT COPY"); });
}

std::string App::default_std(const Value &a) { return a.get("standards").size() == 1 ? text_of(a.get("standards").at(0)) : ""; }

bool App::add_question(const std::string &aid, Value &q) {
    std::vector<std::string> opts;
    for (const auto &t : store::QUESTION_TYPES) opts.push_back(lpad(t.label, 20) + " " + t.desc);
    long j = ui.choose("ADD WHAT KIND OF QUESTION?", opts);
    if (j < 0) return false;
    std::string t = store::QUESTION_TYPES[(size_t)j].type;
    Value *a = assessment(aid);
    q = store::new_question(t, a ? default_std(*a) : "");
    std::string v;
    if (t == "section") {
        if (!ui.prompt("SECTION TITLE (E.G. PART II: VOCABULARY)", v)) return false;
        q["title"] = v;
        std::string d;
        ui.prompt("DIRECTIONS FOR THIS SECTION (OPTIONAL)", d);
        q["prompt"] = d;
        return true;
    }
    if (t == "passage") {
        std::string title;
        ui.prompt("PASSAGE TITLE (OPTIONAL)", title);
        q["title"] = title;
        std::string body;
        if (!ui.edit_text("PASSAGE TEXT", body, "PASTE OR TYPE THE TEXT. BLANK LINE OR NEW LINE = NEW PARAGRAPH. ESC SAVES.") || body.empty()) return false;
        q["text"] = body;
        return true;
    }
    if (t == "chart") {
        std::vector<std::string> labels;
        for (const auto &p : store::chart_presets()) labels.push_back(text::upper(p.label));
        long k = ui.choose("WHAT KIND OF CHART?", labels, 0);
        if (k < 0) return false;
        store::chart_preset(q, store::chart_presets()[(size_t)k].key);
        std::string d;
        if (!ui.edit_text("CHART - DIRECTIONS", d, "DIRECTIONS FOR STUDENTS (OPTIONAL; BLANK PRINTS A SHORT DEFAULT). ESC SAVES. NEXT YOU CAN SET LABELS AND SIZE."))
            return false;
        q["prompt"] = text::strip(d);
        question_editor(aid, [&q]() -> Value * { return &q; });
        return true;
    }
    std::string help = t == "fill" ? "TYPE THE SENTENCE. USE ___ (3+ UNDERSCORES) FOR EACH BLANK. ESC SAVES."
                       : t == "match" ? "DIRECTIONS FOR THE MATCHING SET (E.G. MATCH EACH TERM TO ITS DEFINITION). ESC SAVES."
                                      : "TYPE THE QUESTION. ESC SAVES, CTRL-X CANCELS.";
    std::string prompt = t == "match" ? "Match each term with its definition." : "";
    if (!ui.edit_text(store::type_label(t) + " - PROMPT", prompt, help) || text::strip(prompt).empty()) return false;
    q["prompt"] = text::strip(prompt);
    if (t == "mc") {
        while (q["choices"].size() < 6) {
            std::string c;
            if (!ui.prompt("CHOICE " + letter((long)q["choices"].size()) + " (BLANK WHEN DONE)", c) || c.empty()) break;
            q["choices"].push(c);
        }
        long n = (long)q["choices"].size();
        if (n) {
            std::string ans;
            if (ui.prompt("CORRECT ANSWER (A-" + letter(n - 1) + ")", ans) && !text::strip(ans).empty()) {
                char c = (char)toupper((unsigned char)text::strip(ans)[0]);
                for (long i = 0; i < n; i++) if (LETTERS[i] == c) q["answer"] = i;
            }
        }
    } else if (t == "tf") {
        std::string ans;
        if (ui.prompt("ANSWER (T/F)", ans) && !text::strip(ans).empty()) {
            char c = (char)tolower((unsigned char)text::strip(ans)[0]);
            if (c == 't' || c == 'f') q["answer"] = c == 't';
        }
    } else if (t == "fill") {
        std::string ans;
        ui.prompt("ANSWER(S) FOR THE KEY", ans);
        q["answer"] = ans;
    } else if (t == "match") {
        while (q["pairs"].size() < 10) {
            std::string term_, m;
            if (!ui.prompt("TERM " + std::to_string(q["pairs"].size() + 1) + " (BLANK WHEN DONE)", term_) || term_.empty()) break;
            if (!ui.prompt("WHAT MATCHES '" + term_ + "'", m)) break;
            Value p = Value::array();
            p.push(term_);
            p.push(m);
            q["pairs"].push(p);
        }
    } else if (t == "short") {
        std::string n;
        q["lines"] = ui.prompt("HOW MANY WRITING LINES", n, "3", true) && all_digits(n) && n.size() < 6 ? std::atoi(n.c_str()) : 3;
    } else if (t == "essay") {
        long k = ui.choose("WRITING SPACE", {space_label("lines"), space_label("blank"), space_label("box")}, 0);
        const char *spaces[3] = {"lines", "blank", "box"};
        q["space"] = spaces[k < 0 ? 0 : k];
        std::string n;
        q["lines"] = ui.prompt("HOW BIG (IN LINES, ~1/3 INCH EACH)", n, "12", true) && all_digits(n) && n.size() < 6 ? std::atoi(n.c_str()) : 12;
    }
    std::string dflt = t == "essay" ? "10" : t == "match" ? std::to_string(q.get("pairs").size()) : "1";
    std::string p;
    if (ui.prompt("POINTS", p, dflt, true)) q["points"] = store::parse_points(p, q["points"]);
    return true;
}

std::vector<App::QField> App::question_fields(const Value &q) {
    std::string t = q.s("type");
    if (t == "section") return {{"title", "TITLE", "line"}, {"prompt", "DIRECTIONS", "text"}};
    if (t == "passage") return {{"title", "TITLE", "line"}, {"text", "PASSAGE TEXT", "text"}, {"numbered", "LINE NUMBERS (PDF)", "bool"}};
    std::vector<QField> f = {{"prompt", "PROMPT", "text"}};
    if (t == "mc") {
        f.push_back({"choices", "CHOICES", "choices"});
        f.push_back({"answer", "CORRECT ANSWER", "mcans"});
    } else if (t == "tf") f.push_back({"answer", "ANSWER", "tf"});
    else if (t == "short") {
        f.push_back({"lines", "WRITING LINES", "int"});
        f.push_back({"answer", "ANSWER KEY / EXEMPLAR", "text"});
    } else if (t == "essay") {
        f.push_back({"space", "WRITING SPACE", "space"});
        f.push_back({"lines", "SIZE (LINES)", "int"});
        f.push_back({"answer", "RUBRIC / KEY NOTES", "text"});
    } else if (t == "fill") f.push_back({"answer", "ANSWER(S) FOR KEY", "line"});
    else if (t == "match") f.push_back({"pairs", "TERMS & MATCHES", "pairs"});
    else if (t == "chart") {
        std::string lay = q.s("layout");
        f = {{"prompt", "DIRECTIONS", "text"}, {"preset", "KIND (RESETS LABELS)", "preset"}};
        if (lay == "table") {
            f.push_back({"cols", "COLUMNS (1-8)", "int"});
            f.push_back({"rows", "ROWS (1-20)", "int"});
            f.push_back({"heads", "COLUMN HEADINGS", "list"});
            f.push_back({"sidecol", "LABEL EACH ROW (MATRIX)", "bool"});
            f.push_back({"side", "ROW LABELS", "list"});
        } else if (lay == "venn") {
            f.push_back({"circles", "CIRCLES (2 OR 3)", "int"});
            f.push_back({"heads", "CIRCLE LABELS", "list"});
        } else if (lay == "web") {
            f.push_back({"center", "CENTER TOPIC", "line"});
            f.push_back({"rows", "BUBBLES (3-8)", "int"});
            f.push_back({"heads", "BUBBLE LABELS", "list"});
        } else if (lay == "sequence") {
            f.push_back({"rows", "BOXES (2-8)", "int"});
            f.push_back({"heads", "BOX LABELS", "list"});
        } else if (lay == "frayer") {
            f.push_back({"center", "WORD IN THE MIDDLE", "line"});
            f.push_back({"heads", "CORNER LABELS", "list"});
        } else f.push_back({"heads", "STAGE LABELS", "list"});
        f.push_back({"lines", "SIZE", "size"});
        f.push_back({"answer", "KEY NOTES", "text"});
    }
    f.push_back({"points", std::string("POINTS") + (t == "match" ? " (0 = 1 PER PAIR)" : t == "chart" ? " (0 = NOT SCORED)" : ""), "num"});
    f.push_back({"standard", "STANDARD", "std1"});
    return f;
}

void App::question_editor(const std::string &aid, const QRef &qref) {
    std::vector<QField> fields;
    auto value = [&](const Value &q, const QField &f) -> std::string {
        const Value &v = q.get(f.key);
        if (f.kind == "choices") {
            std::vector<std::string> parts;
            long i = 0;
            for (const auto &c : v.arr()) parts.push_back(letter(i++) + ") " + text_of(c));
            return parts.empty() ? "-" : text::join(parts, "  ");
        }
        if (f.kind == "mcans") return v.is_number() && !v.is_null() && v.as_int() < (long long)q.get("choices").size() ? letter((long)v.as_int()) : "-  (NOT SET)";
        if (f.kind == "tf") return v.is_null() ? "-  (NOT SET)" : (v.truthy() ? "TRUE" : "FALSE");
        if (f.kind == "bool") return v.truthy() ? "YES" : "NO";
        if (f.kind == "space") return v.is_string() ? space_label(v.str()) : text_of(v);
        if (f.kind == "pairs") {
            if (!v.truthy()) return "-";
            std::vector<std::string> firsts;
            for (const auto &p : v.arr()) firsts.push_back(text_of(p.at(0)));
            return std::to_string(v.size()) + " PAIRS: " + text::join(firsts, ", ");
        }
        if (f.kind == "list") {
            std::vector<std::string> xs;
            for (const auto &x : v.arr()) xs.push_back(x.truthy() ? text_of(x) : "(blank)");
            return xs.empty() ? "-" : text::join(xs, " / ");
        }
        if (f.kind == "preset") return text::upper(preset_label(v.is_string() ? v.str() : "", "-"));
        if (f.kind == "size") return text::upper(store::chart_size_label(v.truthy() ? (int)v.as_int() : 0));
        if (f.kind == "int" || f.kind == "num") return text_of(v);
        std::string s = first_line(v.truthy() ? text_of(v) : "");
        return s.empty() ? "-" : s;
    };
    Value *q0 = qref();
    if (!q0) return;
    ui.list_screen(
        store::type_label(q0->s("type")),
        [&] {
            Value *q = qref();
            fields = q ? question_fields(*q) : std::vector<QField>();
            return (long)fields.size();
        },
        [&](long i, int) {
            UI::Row r;
            Value *q = qref();
            if (!q) return r;
            r.text = lpad(fields[(size_t)i].label, 26) + " " + value(*q, fields[(size_t)i]);
            r.has_style = true;
            r.st = ui.HI;
            return r;
        },
        [&](long i) -> Result {
            Value *q = qref();
            if (!q) return Result::back();
            const QField f = fields[(size_t)i];
            Value nv;
            bool got = false;
            std::string v;
            if (f.kind == "line") {
                got = ui.prompt(f.label, v, field(*q, f.key));
                nv = v;
            } else if (f.kind == "text") {
                v = field(*q, f.key);
                got = ui.edit_text(f.label, v);
                nv = v;
            } else if (f.kind == "int" || f.kind == "num") {
                std::string cur = q->has(f.key) ? text_of(q->get(f.key)) : "";
                if (ui.prompt(f.label, v, cur, true) && (q = qref())) {
                    if (f.kind == "num") nv = store::parse_points(v, q->get(f.key));
                    else nv = all_digits(v) && v.size() < 9 ? Value(std::atoi(v.c_str())) : q->get(f.key);
                    got = true;
                }
            } else if (f.kind == "bool") {
                nv = !q->get(f.key).truthy();
                got = true;
            } else if (f.kind == "tf") {
                const Value &cur = q->get(f.key);
                (*q)[f.key] = cur.is_null() ? Value(true) : cur.truthy() ? Value(false) : Value();
            } else if (f.kind == "space") {
                const char *order[3] = {"lines", "blank", "box"};
                std::string cur = q->has(f.key) ? q->s(f.key) : "lines";
                int k = 0;
                for (int n = 0; n < 3; n++) if (cur == order[n]) k = n;
                nv = order[(k + 1) % 3];
                got = true;
            } else if (f.kind == "choices") choices_editor(qref);
            else if (f.kind == "mcans") {
                if (q->get("choices").truthy()) {
                    std::vector<std::string> opts;
                    long n = 0;
                    for (const auto &c : q->get("choices").arr()) opts.push_back(letter(n++) + ") " + text_of(c));
                    long cur = q->get("answer").is_number() ? (long)q->get("answer").as_int() : -1;
                    long j = ui.choose("CORRECT ANSWER", opts, cur);
                    if (j >= 0 && (q = qref())) (*q)["answer"] = j;
                }
            } else if (f.kind == "pairs") pairs_editor(qref);
            else if (f.kind == "list") {
                std::vector<std::string> xs;
                for (const auto &x : q->get(f.key).arr()) xs.push_back(text_of(x));
                v = text::join(xs, "\n");
                if (ui.edit_text(f.label, v, "ONE PER LINE. ESC SAVES.")) {
                    nv = Value::array();
                    if (!text::strip(v).empty())
                        for (const auto &x : text::split(text::strip(v), "\n")) nv.push(text::strip(x));
                    got = true;
                }
            } else if (f.kind == "preset") {
                std::vector<std::string> labels;
                long cur = -1, n = 0;
                for (const auto &p : store::chart_presets()) {
                    labels.push_back(text::upper(p.label));
                    if (p.key == q->s(f.key)) cur = n;
                    n++;
                }
                long j = ui.choose("WHAT KIND OF CHART?", labels, cur);
                if (j >= 0 && (q = qref())) store::chart_preset(*q, store::chart_presets()[(size_t)j].key);
            } else if (f.kind == "size") {
                std::vector<std::string> labels;
                long cur = -1, n = 0;
                for (int s : store::CHART_SIZES) {
                    labels.push_back(text::upper(store::chart_size_label(s)));
                    if (q->get(f.key).is_number() && q->get(f.key).as_int() == s) cur = n;
                    n++;
                }
                long j = ui.choose("SIZE", labels, cur);
                if (j >= 0) {
                    nv = store::CHART_SIZES[(size_t)j];
                    got = true;
                }
            } else if (f.kind == "std1") {
                Value *a = assessment(aid);
                std::vector<std::string> pool;
                if (a && a->get("standards").truthy())
                    for (const auto &c : a->get("standards").arr()) pool.push_back(text_of(c));
                else {
                    std::string g = a && a->get("grades").truthy() ? field(*a, "grades") : field(st(), "grades");
                    for (size_t i2 : std_items(g, "", std_subject())) pool.push_back(text_of(store_.standards[i2].get("code")));
                }
                std::vector<std::string> opts = {"(none)"};
                for (const auto &c : pool)
                    opts.push_back(text::ljust(c, std::min<long>(18, std::max<long>(13, (long)text::length(c)))) + " " + truncate(store_.std_text(c), 70));
                long cur = 0;
                for (size_t n = 0; n < pool.size(); n++)
                    if (q->s("standard") == pool[n]) { cur = (long)n + 1; break; }
                long j = ui.choose("STANDARD FOR THIS QUESTION", opts, cur);
                if (j >= 0) {
                    nv = j == 0 ? std::string() : pool[(size_t)j - 1];
                    got = true;
                }
            }
            if (got && (q = qref())) (*q)[f.key] = nv;
            return Result::none();
        },
        nullptr, "1-0 / ARROWS+RETURN CHANGE   ESC BACK (AUTOSAVED)", [&] {
            Value *q = qref();
            if (!q) return std::string();
            std::string p = field(*q, "prompt");
            return truncate(first_line(p.empty() ? field(*q, "title") : p), 100);
        });
}

void App::choices_editor(const QRef &qref) {
    auto cs = [&]() -> Value * {
        Value *q = qref();
        if (!q) return nullptr;
        if (!q->get("choices").is_array()) (*q)["choices"] = Value::array();
        return &(*q)["choices"];
    };
    ui.list_screen(
        "ANSWER CHOICES", [&] { return cs() ? (long)cs()->size() : 0L; },
        [&](long i, int) {
            UI::Row r;
            Value *q = qref();
            if (!q) return r;
            bool correct = q->get("answer").is_number() && q->get("answer").as_int() == i;
            r.text = letter(i) + ")  " + text_of(cs()->at((size_t)i)) + (correct ? "   <== CORRECT" : "");
            if (correct) {
                r.has_style = true;
                r.st = ui.HI;
            }
            return r;
        },
        [&](long i) {
            std::string v;
            if (ui.prompt("CHOICE " + letter(i), v, text_of(cs()->at((size_t)i))) && !v.empty() && cs()) cs()->at((size_t)i) = v;
            return Result::none();
        },
        [&](int k, long i) -> Result {
            std::string key = text::lower(key_text(k));
            Value *list = cs();
            Value *q = qref();
            if (!list || !q) return Result::back();
            if (key == "a" && list->size() < 6) {
                std::string v;
                if (ui.prompt("CHOICE " + letter((long)list->size()), v) && !v.empty() && (list = cs())) {
                    list->push(v);
                    return Result::move((long)list->size() - 1);
                }
            }
            if (i < 0 || i >= (long)list->size()) return Result::none();
            if (key == "*" || key == "k" || key == " ") (*q)["answer"] = i;
            else if (key == "d") {
                list->arr_ref().erase(list->arr_ref().begin() + i);
                const Value &ans = q->get("answer");
                if (ans.is_number() && ans.as_int() == i) (*q)["answer"] = Value();
                else if (ans.is_number() && ans.as_int() > i) (*q)["answer"] = ans.as_int() - 1;
                return Result::move(std::max(0L, i - 1));
            } else if (key == "+" || key == "=" || key == "-" || key == "_") {
                long j = (key == "+" || key == "=") ? i + 1 : i - 1;
                if (j >= 0 && j < (long)list->size()) {
                    std::swap(list->at((size_t)i), list->at((size_t)j));
                    const Value &ans = q->get("answer");
                    if (ans.is_number() && ans.as_int() == i) (*q)["answer"] = j;
                    else if (ans.is_number() && ans.as_int() == j) (*q)["answer"] = i;
                    return Result::move(j);
                }
            }
            return Result::none();
        },
        "1-0 EDIT  A ADD  K MARK CORRECT  D DELETE  +/- MOVE  ESC DONE",
        [&] {
            Value *q = qref();
            return q ? truncate(first_line(field(*q, "prompt")), 100) : std::string();
        },
        "NO CHOICES. PRESS A TO ADD ONE.");
}

void App::pairs_editor(const QRef &qref) {
    auto ps = [&]() -> Value * {
        Value *q = qref();
        if (!q) return nullptr;
        if (!q->get("pairs").is_array()) (*q)["pairs"] = Value::array();
        return &(*q)["pairs"];
    };
    ui.list_screen(
        "MATCHING PAIRS", [&] { return ps() ? (long)ps()->size() : 0L; },
        [&](long i, int w) {
            UI::Row r;
            const Value &p = ps()->at((size_t)i);
            long half = (w - 6) / 2;
            r.text = text::ljust(truncate(text_of(p.at(0)), half), half) + "  ->  " + text_of(p.at(1));
            return r;
        },
        [&](long i) {
            const Value p = ps()->at((size_t)i);
            std::string t, m;
            if (!ui.prompt("TERM", t, text_of(p.at(0)))) return Result::none();
            if (ui.prompt("WHAT MATCHES '" + t + "'", m, text_of(p.at(1))) && ps()) {
                Value pair = Value::array();
                pair.push(t);
                pair.push(m);
                ps()->at((size_t)i) = pair;
            }
            return Result::none();
        },
        [&](int k, long i) -> Result {
            std::string key = text::lower(key_text(k));
            Value *list = ps();
            if (!list) return Result::back();
            if (key == "a" && list->size() < 10) {
                std::string t, m;
                if (ui.prompt("TERM " + std::to_string(list->size() + 1), t) && !t.empty() && ui.prompt("WHAT MATCHES '" + t + "'", m) && (list = ps())) {
                    Value pair = Value::array();
                    pair.push(t);
                    pair.push(m);
                    list->push(pair);
                    return Result::move((long)list->size() - 1);
                }
            }
            if (i < 0 || i >= (long)list->size()) return Result::none();
            if (key == "d") {
                list->arr_ref().erase(list->arr_ref().begin() + i);
                return Result::move(std::max(0L, i - 1));
            }
            if (key == "+" || key == "=" || key == "-" || key == "_") {
                long j = (key == "+" || key == "=") ? i + 1 : i - 1;
                if (j >= 0 && j < (long)list->size()) {
                    std::swap(list->at((size_t)i), list->at((size_t)j));
                    return Result::move(j);
                }
            }
            return Result::none();
        },
        "1-0 EDIT  A ADD  D DELETE  +/- MOVE  ESC DONE", [] { return std::string("MATCHES ARE SHUFFLED AUTOMATICALLY ON THE STUDENT COPY  (MAX 10)"); },
        "NO PAIRS. PRESS A TO ADD ONE.");
}

}  // namespace tui
