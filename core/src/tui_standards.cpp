// The terminal app's standards screens: picking standards for a lesson, and the library.
#include <algorithm>

#include "text.h"
#include "textwrap.h"
#include "tui_app.h"

namespace tui {

using json::Value;
using store::field;
using store::text_of;

static std::string key_text(int k) {
    if (k < 0x20 || k >= term::K_ENTER || k == 0x7f) return "";
    return text::to8((char32_t)k);
}

static std::string sub_label(const Value &parent, const std::string &label) {
    return parent.get("sub_grades").truthy() ? "GRADE " + label + ": " : label + ". ";
}

std::string App::std_subject() {
    std::string s = !std_subject_.empty() ? std_subject_ : field(st(), "subject");
    if (s.empty()) s = store::DEFAULT_SUBJECT;
    return std::find(store_.subjects.begin(), store_.subjects.end(), s) != store_.subjects.end() ? s : store::ALL;
}

std::vector<size_t> App::std_items(const std::string &grades, const std::string &query, const std::string &subject) {
    std::vector<size_t> out;
    std::string q = text::lower(query);
    for (size_t i = 0; i < store_.standards.size(); i++) {
        const Value &s = store_.standards[i];
        if (subject != store::ALL && s.s("subject") != subject && !s.get("custom").truthy()) continue;
        if (grades != store::ALL && !store::grades_match(text_of(s.get("grades")), grades)) continue;
        if (!q.empty() && !text::contains(text::lower(text_of(s.get("code"))), q) && !text::contains(text::lower(text_of(s.get("text"))), q) &&
            !text::contains(text::lower(text_of(s.get("cluster"))), q) && !text::contains(text::lower(text_of(s.get("strand"))), q))
            continue;
        out.push_back(i);
    }
    size_t w = 9;
    for (size_t k = 0; k < out.size() && k < 400; k++) w = std::max(w, text::length(text_of(store_.standards[out[k]].get("code"))));
    codew_ = std::min<size_t>(18, w);
    return out;
}

UI::Row App::std_row(size_t i, const std::string &mark) {
    const Value &s = store_.standards[i];
    bool part = s.get("parent").truthy();
    UI::Row r;
    r.text = mark + (part ? "    " : "") + text::ljust(text_of(s.get("code")), (long)codew_) + " " + text_of(s.get("text"));
    r.has_style = true;
    r.st = part ? ui.DIM : ui.N;
    return r;
}

bool App::pick_grade(const std::string &current, std::string &out) {
    std::vector<std::string> labels;
    for (const auto &g : store::GRADE_CHOICES) labels.push_back(grade_label(g));
    auto it = std::find(store::GRADE_CHOICES.begin(), store::GRADE_CHOICES.end(), current);
    long j = ui.choose("GRADE LEVEL", labels, it == store::GRADE_CHOICES.end() ? -1 : (long)(it - store::GRADE_CHOICES.begin()));
    if (j < 0) return false;
    out = store::GRADE_CHOICES[(size_t)j];
    return true;
}

bool App::std_filter_keys(const std::string &c, std::string &grade, std::string &subject) {
    if (c == "f") {
        std::vector<std::string> opts = {store::ALL};
        opts.insert(opts.end(), store_.subjects.begin(), store_.subjects.end());
        std::vector<std::string> labels;
        for (const auto &o : opts) labels.push_back(ui.tx(o == store::ALL ? "All subjects" : o));
        auto it = std::find(opts.begin(), opts.end(), subject);
        long j = ui.choose("SUBJECT", labels, it == opts.end() ? -1 : (long)(it - opts.begin()));
        if (j >= 0) subject = std_subject_ = opts[(size_t)j];
        return true;
    }
    if (c == "g") {
        std::string g;
        if (pick_grade(grade, g)) grade = g;
        return true;
    }
    return false;
}

std::string App::std_filter_info(const std::string &grade, const std::string &subject, const std::string &q) {
    std::string subj = subject == store::ALL ? "ALL SUBJECTS" : text::upper(subject);
    return subj + "   " + grade_label(grade) + (q.empty() ? "" : "   SEARCH '" + q + "'");
}

void App::std_detail(const std::string &code) {
    const Value *s = store_.standard(code);
    if (!s) return;
    int rows, w, x0;
    ui.dims(rows, w, x0);
    long width = std::min(80, w - 6);
    std::vector<std::pair<std::string, Style>> lines;
    auto add = [&](const std::vector<std::string> &ls, Style st) {
        for (const auto &l : ls) lines.push_back({l, st});
    };
    lines.push_back({text_of(s->get("code")), ui.HI});
    add(textwrap::wrap(text_of(s->get("subject")) + "  /  " + text_of(s->get("strand")) + "  /  " + text_of(s->get("cluster")) + "  /  " +
                           grade_label(text_of(s->get("grades"))),
                       width),
        ui.DIM);
    lines.push_back({"", ui.N});
    if (s->get("parent").truthy()) {
        const Value *p = store_.standard(text_of(s->get("parent")));
        if (p) {
            lines.push_back({"PART OF " + text_of(p->get("code")) + ":", ui.DIM});
            add(textwrap::wrap(text_of(p->get("text")), width), ui.DIM);
            lines.push_back({"", ui.N});
            add(textwrap::wrap(sub_label(*p, text_of(s->get("label"))) + text_of(s->get("text")), width, "", "   "), ui.N);
        }
    } else {
        add(textwrap::wrap(text_of(s->get("text")), width), ui.N);
        for (const auto &kv : s->get("subs").arr())
            add(textwrap::wrap(sub_label(*s, text_of(kv.at(0))) + text_of(kv.at(1)), width, "  ", "     "), ui.N);
    }
    lines.push_back({"", ui.N});
    add(textwrap::wrap("SOURCE: " + text_of(s->get("source")), width), ui.DIM);
    ui.view_lines(text_of(s->get("code")), lines, "ARROWS/SPACE SCROLL   ESC BACK", true);
}

std::vector<std::string> App::standards_picker(const std::vector<std::string> &selected, const std::string &grades) {
    std::vector<std::string> sel = selected;
    std::string g = std::find(store::GRADE_CHOICES.begin(), store::GRADE_CHOICES.end(), grades) != store::GRADE_CHOICES.end() ? grades : store::ALL;
    std::string s = std_subject(), q;
    bool show = false;
    std::vector<size_t> items;
    auto has = [&](const std::string &c) { return std::find(sel.begin(), sel.end(), c) != sel.end(); };
    auto toggle = [&](long i) {
        std::string c = text_of(store_.standards[items[(size_t)i]].get("code"));
        auto it = std::find(sel.begin(), sel.end(), c);
        if (it != sel.end()) sel.erase(it);
        else sel.push_back(c);
    };
    ui.list_screen(
        "PICK STANDARDS",
        [&] {
            if (show) {
                items.clear();
                for (const auto &c : sel)
                    if (store_.std_index.count(c)) items.push_back(store_.std_index[c]);
            } else items = std_items(g, q, s);
            return (long)items.size();
        },
        [&](long i, int) { return std_row(items[(size_t)i], has(text_of(store_.standards[items[(size_t)i]].get("code"))) ? "[X] " : "[ ] "); },
        [&](long i) {
            toggle(i);
            return Result::none();
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(key_text(k));
            if (std_filter_keys(c, g, s)) return Result::move(0);
            if (c == "/") {
                std::string nq;
                if (ui.prompt("SEARCH CODE OR TEXT (BLANK CLEARS)", nq, q)) q = nq;
                return Result::move(0);
            }
            if (c == " " && i >= 0) toggle(i);
            else if (c == "v" && i >= 0) std_detail(text_of(store_.standards[items[(size_t)i]].get("code")));
            else if (c == "c") sel.clear();
            else if (c == "s") {
                q.clear();
                show = !show;
                return Result::move(0);
            }
            return Result::none();
        },
        "1-0/SPACE TOGGLE  V VIEW  F SUBJECT  G GRADE  / SEARCH  S SHOW SELECTED  C CLEAR  ESC DONE",
        [&] {
            return (sel.empty() ? std::string("NONE SELECTED") : std::to_string(sel.size()) + " SELECTED: " + text::join(sel, ", ")) + "   " +
                   std_filter_info(g, s, q) + (show ? "   (SHOWING SELECTED)" : "");
        });
    return sel;
}

void App::standards_library() {
    std::string g0 = st().has("grades") ? field(st(), "grades") : "9-10";
    std::string g = std::find(store::GRADE_CHOICES.begin(), store::GRADE_CHOICES.end(), g0) != store::GRADE_CHOICES.end() ? g0 : store::ALL;
    std::string s = std_subject(), q;
    std::vector<size_t> items;
    ui.list_screen(
        "STANDARDS LIBRARY",
        [&] {
            items = std_items(g, q, s);
            return (long)items.size();
        },
        [&](long i, int) { return std_row(items[(size_t)i]); },
        [&](long i) {
            std_detail(text_of(store_.standards[items[(size_t)i]].get("code")));
            return Result::none();
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(key_text(k));
            if (std_filter_keys(c, g, s)) return Result::move(0);
            if (c == "/") {
                std::string nq;
                if (ui.prompt("SEARCH CODE OR TEXT (BLANK CLEARS)", nq, q)) q = nq;
                return Result::move(0);
            }
            if (c == "a") {
                std::string code;
                if (!ui.prompt("CODE FOR YOUR STANDARD (E.G. SL.9-10.1 OR DISTRICT-1)", code) || code.empty()) return Result::none();
                if (store_.std_index.count(code)) {
                    ui.msg = "?THAT CODE ALREADY EXISTS.";
                    return Result::none();
                }
                std::string body;
                if (ui.edit_text("STANDARD " + code, body, "ENTER THE FULL TEXT OF THE STANDARD. ESC SAVES.") && !text::strip(body).empty()) {
                    Value c2 = Value::object();
                    c2["code"] = code;
                    c2["text"] = text::squash(body);
                    c2["grades"] = "Custom";
                    store_.data["custom_standards"].push(c2);
                    store_.reload_standards();
                    save();
                    ui.msg = "ADDED " + code + ".";
                }
            } else if (c == "i") {
                import_standards();
                return Result::move(0);
            } else if (c == "x") {
                remove_standards();
                return Result::move(0);
            } else if (c == "e") {
                export_standards();
                return Result::move(0);
            } else if (c == "d" && i >= 0) {
                const Value &sd = store_.standards[items[(size_t)i]];
                std::string code = text_of(sd.get("code"));
                if (!sd.get("custom").truthy()) ui.msg = "?ONLY YOUR OWN CUSTOM STANDARDS CAN BE DELETED.";
                else if (ui.confirm("DELETE CUSTOM STANDARD " + code)) {
                    Value keep = Value::array();
                    for (const auto &x : store_.data.get("custom_standards").arr())
                        if (text_of(x.get("code")) != code) keep.push(x);
                    store_.data["custom_standards"] = keep;
                    store_.reload_standards();
                    save();
                }
            }
            return Result::none();
        },
        "1-0 VIEW  F SUBJECT  G GRADE  / SEARCH  I IMPORT  E EXPORT  X REMOVE  A ADD CUSTOM  D DELETE  ESC BACK",
        [&] { return std_filter_info(g, s, q); }, "NO STANDARDS YET. I IMPORTS A CSV OR JSON FILE OF STANDARDS; A ADDS ONE BY HAND.");
}

void App::import_standards() {
    std::string path;
    if (!ui.prompt("PATH TO A .CSV OR .JSON STANDARDS FILE (DRAG IT HERE)", path, "", false, true) || path.empty()) return;
    Value done;
    try {
        done = store_.import_standards(path);
    } catch (store::UserError &e) {
        ui.msg = "?" + text::upper(e.message);
        return;
    }
    if (!store_.warning.empty()) {
        ui.msg = store_.warning;
        store_.warning.clear();
        return;
    }
    std::vector<std::string> parts;
    for (const auto &d : done.arr()) {
        long skip = (long)d.at(2).as_int();
        parts.push_back(text::upper(text_of(d.at(0))) + ": " + std::to_string(d.at(1).as_int()) +
                        (skip ? " (" + std::to_string(skip) + " SKIPPED: CODE ALREADY USED)" : ""));
    }
    ui.msg = "IMPORTED " + text::join(parts, "; ");
    if (done.size()) std_subject_ = text_of(done.at(0).at(0));
}

void App::export_standards() {
    auto opts = store_.shareable_subjects();
    if (opts.empty()) {
        ui.msg = "?NOTHING TO SHARE YET: ONLY STANDARDS YOU IMPORTED OR ADDED CAN BE EXPORTED.";
        return;
    }
    std::vector<std::string> labels;
    for (const auto &o : opts) labels.push_back(ui.tx(o));
    if (opts.size() > 1) labels.insert(labels.begin(), "ALL OF THEM");
    long j = ui.choose("EXPORT WHICH STANDARDS TO SHARE", labels);
    if (j < 0) return;
    std::vector<std::string> pick = opts.size() > 1 && j == 0 ? opts : std::vector<std::string>{opts[(size_t)(j - (opts.size() > 1 ? 1 : 0))]};
    std::string folder;
    if (!ui.prompt("SAVE IN WHICH FOLDER (DRAG ONE HERE)", folder, store_.export_dir(), false, true) || folder.empty()) return;
    try {
        std::string path = store_.export_standards(folder, pick);
        ui.msg = "SAVED " + path + "  (ANOTHER TEACHER IMPORTS IT WITH I)";
    } catch (store::UserError &e) {
        ui.msg = "?" + text::upper(e.message);
    }
}

void App::remove_standards() {
    auto opts = store_.imported_subjects();
    if (opts.empty()) {
        ui.msg = "?NO IMPORTED SUBJECTS TO REMOVE.";
        return;
    }
    std::vector<std::string> labels;
    for (const auto &o : opts) labels.push_back(ui.tx(o));
    long j = ui.choose("REMOVE WHICH SUBJECT", labels);
    if (j >= 0 && ui.confirm("REMOVE " + text::upper(opts[(size_t)j]) + " (LESSONS KEEP THEIR CODES)")) {
        store_.remove_subject(opts[(size_t)j]);
        ui.msg = "REMOVED " + text::upper(opts[(size_t)j]) + ".";
    }
}

}  // namespace tui
