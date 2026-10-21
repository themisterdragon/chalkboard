#include "doc.h"

#include "homedays.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

#include "organizers.h"
#include "pyrand.h"
#include "text.h"

namespace doc {

using store::field;
using store::text_of;

static const char *LETTERS = "ABCDEFGHIJ";

static Value obj(std::initializer_list<std::pair<const char *, Value>> kv) {
    Value o = Value::object();
    for (const auto &p : kv) o[p.first] = p.second;
    return o;
}

static void extend(Value &a, const Value &more) {
    for (const auto &x : more.arr()) a.push(x);
}

static Value strings(const std::vector<std::string> &xs) {
    Value a = Value::array();
    for (const auto &x : xs) a.push(x);
    return a;
}

// int(x or dflt), with Python's int() rules for numbers and number-like text
static long int_or(const Value &v, long dflt) {
    if (!v.truthy()) return dflt;
    if (v.is_int() || v.is_bool()) return (long)v.as_int();
    if (v.is_float()) return std::isfinite(v.as_double()) ? (long)v.as_double() : dflt;
    if (v.is_string()) {
        std::string t = text::strip(v.str());
        char *end = nullptr;
        long x = std::strtol(t.c_str(), &end, 10);
        if (!t.empty() && end && *end == 0) return x;
    }
    return dflt;
}

static std::string join_truthy(const std::vector<std::string> &xs, const std::string &sep) {
    std::vector<std::string> keep;
    for (const auto &x : xs) if (!x.empty()) keep.push_back(x);
    return text::join(keep, sep);
}

// ----------------------------------------------------------------------------- text blocks

Value text_blocks(const std::string &value, const std::string &style) {
    Value out = Value::array();
    for (const auto &line : text::split(value, "\n")) {
        std::string s = text::strip(line);
        if (s.empty()) continue;
        std::u32string u = text::to32(s);
        if ((u[0] == '-' || u[0] == '*' || u[0] == 0x2022) && u.size() > 1 && text::isspace(u[1])) {
            size_t k = 1;
            while (k < u.size() && text::isspace(u[k])) k++;
            int indent = text::starts_with(line, "  ") || text::starts_with(line, "\t") ? 1 : 0;
            out.push(obj({{"t", "bullet"}, {"text", text::to8(u.substr(k))}, {"indent", indent}}));
        } else out.push(obj({{"t", "p"}, {"text", s}, {"style", style}}));
    }
    return out;
}

Value vocab_blocks(const std::string &value) {
    Value out = Value::array();
    for (const auto &p : store::vocab_pairs(value)) {
        if (!p.second.empty()) out.push(obj({{"t", "kv"}, {"label", p.first + ":"}, {"text", p.second}}));
        else out.push(obj({{"t", "p"}, {"text", p.first}, {"style", "bold"}}));
    }
    return out;
}

std::string norm_blanks(const std::string &s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '_') {
            size_t j = i;
            while (j < s.size() && s[j] == '_') j++;
            out += j - i >= 3 ? std::string(14, '_') : s.substr(i, j - i);
            i = j;
        } else out += s[i++];
    }
    return out;
}

static bool has_blank(const std::string &s) { return s.find("___") != std::string::npos; }

Value fields_block(const std::vector<std::string> &items) {
    if (items.empty()) return Value();
    typedef std::vector<std::string> V;
    std::vector<double> shares;
    bool none = false, known = true;
    if (items == V{"Name", "Date Missed", "Due"}) shares = {0.46, 0.30, 0.24};
    else if (items == V{"Turned In", "Teacher Initials"}) shares = {0.5, 0.5};
    else if (items == V{"Name", "Date"}) shares = {0.62, 0.38};
    else if (items == V{"Name", "Period"}) shares = {0.7, 0.3};
    else if (items == V{"Name", "Date", "Period"}) none = true;
    else known = false;
    Value b = obj({{"t", "fields"}, {"items", strings(items)}});
    if (none) return b;
    if (!known) {
        std::vector<double> weights;
        double total = 0;
        for (const auto &x : items) {
            double w = text::contains(text::lower(x), "name") ? 2.4 : 1 + (double)text::length(x) / 12;
            weights.push_back(w);
            total += w;
        }
        for (double w : weights) shares.push_back(w / total);
    }
    Value s = Value::array();
    for (double x : shares) s.push(x);
    b["shares"] = s;
    return b;
}

// ----------------------------------------------------------------------------- page layouts

struct Page {
    Value lay;
    std::string align;
    Page(const Value &settings, const std::string &kind) : lay(store::page_layout(settings, kind)), align(lay.s("title_align")) {}

    Value name_line() const {
        Value b = fields_block(store::layout_fields(lay.s("name_fields")));
        Value out = Value::array();
        if (!b.is_null()) out.push(b);
        return out;
    }

    Value top(Value title_blocks, const Value &before = Value::array(), const Value &after = Value::array(), bool name = true) const {
        for (auto &b : title_blocks.arr_ref()) {
            std::string t = b.s("t");
            if (t == "title" || t == "subtitle") b["align"] = align;
        }
        Value nl = name ? name_line() : Value::array();
        Value out = Value::array();
        if (lay.s("name_place") == "top") {
            extend(out, nl);
            extend(out, before);
            extend(out, title_blocks);
            extend(out, after);
            return out;
        }
        extend(out, title_blocks);
        extend(out, nl);
        extend(out, after);
        return out;
    }

    Value h1(const std::string &t) const {
        std::string style = lay.s("heading_style");
        return obj({{"t", "h1"}, {"text", style == "caps" ? text::upper(t) : t}, {"rule", style != "plain"}});
    }

    std::string head(const std::string &key, const std::string &usual) const {
        std::string v = text::strip(lay.s("head_" + key));
        return v.empty() ? usual : v;
    }

    std::string footer(const std::string &usual) const {
        if (!lay.get("show_footer").truthy()) return "";
        std::string t = text::strip(lay.s("footer_text"));
        return t.empty() ? usual : t;
    }
};

static const Value &settings_of(store::Store &st, const Value *settings) { return settings ? *settings : st.settings(); }

static Value make_doc(const std::string &title, const std::string &footer, const Value &blocks) {
    return obj({{"title", title}, {"footer", footer}, {"blocks", blocks}});
}

// ----------------------------------------------------------------------------- lessons

Value lesson_doc(const Value &lesson, store::Store &st, const Value *settings) {
    const Value &s = settings_of(st, settings);
    Page pg(s, "lesson");
    std::string ltitle = field(lesson, "title");
    if (ltitle.empty()) ltitle = "Untitled Lesson";
    Value title = Value::array();
    title.push(obj({{"t", "title"}, {"text", ltitle}}));
    std::vector<std::string> meta;
    for (auto kv : std::vector<std::pair<const char *, const char *>>{{"Course", "course"}, {"Unit", "unit"}, {"Date", "date"}, {"Duration", "duration"}}) {
        std::string v = field(lesson, kv.second);
        if (!v.empty()) meta.push_back(std::string(kv.first) + ": " + v);
    }
    std::string sub = text::join(meta, "   |   ");
    if (!sub.empty() && pg.lay.get("show_meta").truthy()) title.push(obj({{"t", "subtitle"}, {"text", sub}}));
    Value blocks = pg.top(title);
    const Value &codes = lesson.get("standards");
    if (codes.truthy()) {
        blocks.push(pg.h1(pg.head("standards", "Standards")));
        if (pg.lay.s("std_text") == "codes") {
            std::vector<std::string> cs;
            for (const auto &c : codes.arr()) cs.push_back(text_of(c));
            blocks.push(obj({{"t", "p"}, {"text", text::join(cs, ", ")}}));
        } else
            for (const auto &c : codes.arr()) {
                std::string t = st.std_text(text_of(c));
                blocks.push(obj({{"t", "kv"}, {"label", text_of(c)}, {"text", t.empty() ? "(not in library)" : t}}));
            }
    }
    for (const auto &f : store::LESSON_FIELDS) {
        if (f.kind == "attached") {
            auto att = st.attached(lesson);
            if (!att.empty()) {
                blocks.push(pg.h1(pg.head(f.key, f.label)));
                for (const Value *a : att) blocks.push(obj({{"t", "bullet"}, {"text", handout_name(*a)}}));
            }
        }
        if ((f.kind != "text" && f.kind != "vocab") || text::strip(field(lesson, f.key)).empty()) continue;
        blocks.push(pg.h1(pg.head(f.key, f.label)));
        extend(blocks, f.kind == "vocab" ? vocab_blocks(field(lesson, f.key)) : text_blocks(field(lesson, f.key)));
    }
    std::string footer = join_truthy({field(s, "teacher"), field(lesson, "course"), field(s, "school")}, " - ");
    return make_doc(ltitle, pg.footer(footer), blocks);
}

static const std::vector<std::pair<std::string, bool>> MAKEUP_STEPS = {
    {"bell_ringer", true}, {"instruction", false}, {"guided", false}, {"independent", false}, {"closure", true}, {"homework", false}};

Value makeup_doc(const Value &lesson, store::Store &st, const Value *settings) {
    const Value &s = settings_of(st, settings);
    Page pg(s, "makeup");
    const Value &lay = pg.lay;
    std::string title = field(lesson, "title");
    if (title.empty()) title = "Untitled Lesson";
    Value head = Value::array();
    head.push(obj({{"t", "title"}, {"text", "Make-Up Work: " + title}}));
    std::string sub = join_truthy({field(lesson, "course"), field(lesson, "unit"), field(lesson, "date")}, "   |   ");
    if (!sub.empty()) head.push(obj({{"t", "subtitle"}, {"text", sub}}));
    std::string when = lesson.get("date").truthy() ? " on " + text_of(lesson.get("date")) : "";
    std::string ask = s.get("teacher").truthy() ? " Questions? Ask " + text_of(s.get("teacher")) + "." : " Questions? Ask your teacher.";
    std::string note = text::strip(lay.s("welcome"));
    if (note.empty())
        note = "We missed you! This is what we did in class" + when +
               ". Work through each step below, check it off when you finish, and turn everything in by the due date." + ask;
    Value blocks = pg.top(head, Value::array(), text_blocks(note, "italic"));
    auto linked = st.attached(lesson);
    std::vector<const Value *> held, classwork;
    for (const Value *a : linked) {
        std::string k = a->s("kind");
        if (std::find(store::MAKEUP_HELD.begin(), store::MAKEUP_HELD.end(), k) != store::MAKEUP_HELD.end()) held.push_back(a);
    }
    for (const Value *a : linked)
        if (std::find(held.begin(), held.end(), a) == held.end()) classwork.push_back(a);
    Value handouts = Value::array();
    for (const Value *a : classwork) handouts.push(obj({{"t", "bullet"}, {"text", handout_name(*a)}}));
    for (const char *key : {"question", "targets", "success", "vocab", "materials"}) {
        Value extra = std::string(key) == "materials" ? handouts : Value::array();
        if (!text::strip(field(lesson, key)).empty() || extra.size()) {
            blocks.push(pg.h1(pg.head(key, text::title(key))));
            extend(blocks, std::string(key) == "vocab" ? vocab_blocks(field(lesson, key)) : text_blocks(field(lesson, key)));
            extend(blocks, extra);
            if (std::string(key) == "targets" && lesson.get("standards").truthy()) {
                std::vector<std::string> cs;
                for (const auto &c : lesson.get("standards").arr()) cs.push_back(text_of(c));
                blocks.push(obj({{"t", "p"}, {"style", "small"}, {"text", "Standards: " + text::join(cs, ", ")}}));
            }
        }
    }
    std::vector<std::pair<std::string, bool>> steps;
    for (const auto &st_ : MAKEUP_STEPS)
        if (!text::strip(field(lesson, st_.first)).empty()) steps.push_back(st_);
    if (!steps.empty()) blocks.push(pg.h1(pg.head("steps", "Steps")));
    int i = 0;
    long write_lines = lay.get("write_lines").as_int();
    for (const auto &stp : steps) {
        i++;
        blocks.push(obj({{"t", "check"}, {"text", std::to_string(i) + ". " + pg.head(stp.first, text::title(stp.first))}}));
        Value steps_text = text_blocks(field(lesson, stp.first));
        for (auto b : steps_text.arr()) {
            b["pad"] = 18;
            blocks.push(b);
        }
        if (stp.second && write_lines) blocks.push(obj({{"t", "lines"}, {"n", write_lines}}));
        blocks.push(obj({{"t", "space"}, {"h", 6}}));
    }
    if (!held.empty()) {
        std::string who = s.get("teacher").truthy() ? text_of(s.get("teacher")) : "your teacher";
        blocks.push(obj({{"t", "check"}, {"text", std::to_string(steps.size() + 1) + ". " + pg.head("see_me", "See Me")}}));
        blocks.push(obj({{"t", "p"}, {"pad", 18}, {"text", "Set up a time with " + who + " to take:"}}));
        for (const Value *a : held) blocks.push(obj({{"t", "bullet"}, {"pad", 18}, {"text", handout_name(*a)}}));
        blocks.push(obj({{"t", "space"}, {"h", 6}}));
    }
    Value signoff = fields_block(store::layout_fields(lay.s("signoff")));
    if (!signoff.is_null()) {
        blocks.push(obj({{"t", "space"}, {"h", 14}}));
        blocks.push(obj({{"t", "rule"}}));
        blocks.push(signoff);
    }
    if (lay.get("classwork").truthy())
        for (const Value *a : classwork) {
            blocks.push(obj({{"t", "pagebreak"}}));
            extend(blocks, assessment_doc(*a, st, 0, 1, false, settings).get("blocks"));
        }
    std::string footer = join_truthy({field(s, "teacher"), field(lesson, "course"), field(s, "school")}, " - ");
    return make_doc("Make-Up Work - " + title, pg.footer(footer), blocks);
}

// A packet for a day of school at home (a snow day taught from home, a remote day): what we're learning, explained
// in writing, then a short list of what to do, the worksheets, and how to reach the teacher. Kept
// simple for students: no standards, no times, nothing technical.
Value home_day_doc(const Value &lesson, store::Store &st, const Date &day, const Value *settings) {
    const Value &s = settings_of(st, settings);
    Page pg(s, "makeup");
    std::string name = store::home_day_name(s);
    std::string title = field(lesson, "title");
    if (title.empty()) title = "Today's Lesson";
    Value head = Value::array();
    head.push(obj({{"t", "title"}, {"text", name + " Work"}}));
    std::string sub = join_truthy({field(lesson, "course"), day.valid() ? dates::fmt_date(day, true) : std::string()}, "   |   ");
    if (!sub.empty()) head.push(obj({{"t", "subtitle"}, {"text", sub}}));
    Value blocks = pg.top(head, Value::array(), Value::array(), false);
    Value who_line = Value::array(), share = Value::array();   // just their name
    who_line.push("Name");
    share.push(0.6);
    blocks.push(obj({{"t", "fields"}, {"items", who_line}, {"shares", share}}));
    // what we're learning: the teacher's written explanation, or the lesson's goals
    std::string explain = text::strip(field(lesson, "nti_explain"));
    blocks.push(pg.h1(title));
    if (!explain.empty()) extend(blocks, text_blocks(explain));
    else if (!text::strip(field(lesson, "targets")).empty()) {
        blocks.push(obj({{"t", "p"}, {"text", "Today we're learning to:"}}));
        extend(blocks, text_blocks(field(lesson, "targets")));
    }
    // what to do: each line of the shortened work is a step to check off (no shortened work yet:
    // the lesson's practice, as it is)
    std::vector<std::pair<std::string, std::string>> steps;   // (step, more under it)
    std::string work = field(lesson, "nti_work");
    if (text::strip(work).empty()) {
        if (!text::strip(field(lesson, "guided")).empty()) steps.push_back({"Practice", field(lesson, "guided")});
        if (!text::strip(field(lesson, "independent")).empty()) steps.push_back({"On your own", field(lesson, "independent")});
    }
    for (const auto &line : text::split(work, "\n")) {
        std::string t = text::strip(line);
        if (t.empty()) continue;
        if (t.rfind("- ", 0) == 0 || t.rfind("* ", 0) == 0) t = text::strip(t.substr(2));
        size_t k = 0;   // "1." / "2)" numbering: the steps are numbered here
        while (k < t.size() && std::isdigit((unsigned char)t[k])) k++;
        if (k > 0 && k < t.size() && (t[k] == '.' || t[k] == ')')) t = text::strip(t.substr(k + 1));
        steps.push_back({t, ""});
    }
    auto linked = st.attached(lesson);
    std::vector<const Value *> held, classwork;
    for (const Value *a : linked) {
        std::string k = a->s("kind");
        (std::find(store::MAKEUP_HELD.begin(), store::MAKEUP_HELD.end(), k) != store::MAKEUP_HELD.end() ? held : classwork).push_back(a);
    }
    if (!steps.empty() || !classwork.empty()) blocks.push(pg.h1("What to Do"));
    int i = 0;
    for (const auto &stp : steps) {
        blocks.push(obj({{"t", "check"}, {"text", std::to_string(++i) + ". " + stp.first}}));
        Value more = text_blocks(stp.second);
        for (auto b : more.arr()) {
            b["pad"] = 18;
            blocks.push(b);
        }
        blocks.push(obj({{"t", "space"}, {"h", 4}}));
    }
    for (const Value *a : classwork) blocks.push(obj({{"t", "check"}, {"text", std::to_string(++i) + ". " + handout_name(*a) + " (attached)"}}));
    if (!held.empty()) {
        blocks.push(obj({{"t", "space"}, {"h", 6}}));
        std::vector<std::string> names;
        for (const Value *a : held) names.push_back(handout_name(*a));
        blocks.push(obj({{"t", "p"}, {"text", "We'll do the " + text::join(names, " and the ") + " when we're back at school."}}));
    }
    // how to reach the teacher
    std::string contact = text::strip(field(s, "home_day_contact"));
    std::string who = field(s, "teacher");
    blocks.push(pg.h1("Questions?"));
    if (!contact.empty()) extend(blocks, text_blocks(contact));
    else blocks.push(obj({{"t", "p"}, {"text", who.empty() ? "Ask your teacher when we're back." : "Ask " + who + " when we're back."}}));
    Value signoff = fields_block(store::layout_fields(pg.lay.s("signoff")));
    if (!signoff.is_null()) {
        blocks.push(obj({{"t", "space"}, {"h", 14}}));
        blocks.push(obj({{"t", "rule"}}));
        blocks.push(signoff);
    }
    for (const Value *a : classwork) {   // the worksheets themselves, so nothing is missing at home
        blocks.push(obj({{"t", "pagebreak"}}));
        extend(blocks, assessment_doc(*a, st, 0, 1, false, settings).get("blocks"));
    }
    std::string footer = join_truthy({who, field(lesson, "course"), field(s, "school")}, " - ");
    return make_doc(name + " Work - " + title, pg.footer(footer), blocks);
}

// The teacher log for an at-home day: who, when, where; each class's participation (A in class,
// B taking part, C = B / A x 100, D by technology, E by book or paper packet); and who the
// teacher talked to (name, time, how). Blank rows to write in when there's room.
Value home_log_doc(store::Store &st, const Value &rec, const Value *settings) {
    const Value &s = settings_of(st, settings);
    std::string name = store::home_day_name(s);
    Date day = homedays::date_of(rec);
    std::string when = day.valid() ? dates::fmt_date(day, true) : "";
    Value blocks = Value::array();
    blocks.push(obj({{"t", "title"}, {"text", name + " Teacher Log"}}));
    blocks.push(obj({{"t", "subtitle"}, {"text", join_truthy({name + " " + std::to_string(rec.get("number").as_int()), when}, "   |   ")}}));
    for (const auto &kv : std::vector<std::pair<std::string, std::string>>{{"Teacher", field(s, "teacher")}, {"Date", when}, {"School", field(s, "school")}})
        blocks.push(obj({{"t", "kv"}, {"label", kv.first + ":"}, {"text", kv.second.empty() ? "________________________" : kv.second}}));
    blocks.push(obj({{"t", "space"}, {"h", 8}}));
    blocks.push(obj({{"t", "h1"}, {"text", "Participation"}, {"rule", true}}));
    Value rows = Value::array();
    long ta = 0, tb = 0, td = 0, te = 0, pa = 0;   // pa: in classes with B filled in (for the total %)
    for (const auto &c : rec.get("classes").arr()) {
        long a = homedays::count(c, "enrolled"), b = homedays::count(c, "participating"), d = homedays::count(c, "technology"),
             e = homedays::count(c, "paper");
        bool has_b = !text::strip(field(c, "participating")).empty();
        ta += a, tb += b, td += d, te += e, pa += has_b ? a : 0;
        std::string cls = field(c, "period");
        std::string course = field(c, "course");
        if (!course.empty() && text::lower(course) != text::lower(cls)) cls += " (" + course + ")";
        auto num = [&](const char *k, long v) { return text::strip(store::text_of(c.get(k))).empty() || c.get(k).is_null() ? std::string() : std::to_string(v); };
        Value r = Value::array();
        r.push(cls);
        r.push(num("enrolled", a));
        r.push(num("participating", b));
        r.push(has_b ? homedays::percent(b, a) : std::string());
        r.push(num("technology", d));
        r.push(num("paper", e));
        rows.push(r);
    }
    auto cols = [](std::initializer_list<std::pair<const char *, double>> cs) {
        Value out = Value::array();
        for (const auto &c : cs) {
            Value x = Value::array();
            x.push(c.first);
            x.push(c.second);
            out.push(x);
        }
        return out;
    };
    Value table = obj({{"t", "table"},
                       {"cols", cols({{"Class", 0.30}, {"A. In Class", 0.14}, {"B. Took Part", 0.14}, {"C. % (B / A)", 0.14}, {"D. Technology", 0.14},
                                      {"E. Paper", 0.14}})},
                       {"rows", rows}});
    if (rows.size() > 1) {
        Value t = Value::array();
        for (const auto &x : {std::string("All classes"), std::to_string(ta), std::to_string(tb), homedays::percent(tb, pa), std::to_string(td),
                              std::to_string(te)})
            t.push(x);
        table["total"] = t;
    }
    if (rows.size() == 0) table["blank"] = 6;
    blocks.push(table);
    blocks.push(obj({{"t", "p"}, {"style", "small"},
                     {"text", "C = B / A x 100.   D: students who used technology instruction.   E: students who used a textbook or paper packet."}}));
    blocks.push(obj({{"t", "space"}, {"h", 6}}));
    blocks.push(obj({{"t", "h1"}, {"text", "Communication Log"}, {"rule", true}}));
    Value crow = Value::array();
    for (const auto &c : rec.get("contacts").arr()) {
        Value r = Value::array();
        r.push(field(c, "name"));
        r.push(field(c, "time"));
        r.push(field(c, "mode"));
        crow.push(r);
    }
    blocks.push(obj({{"t", "table"},
                     {"cols", cols({{"Name", 0.45}, {"Time", 0.2}, {"Mode of Communication", 0.35}})},
                     {"rows", crow},
                     {"blank", crow.size() ? 3 : 10}}));
    blocks.push(obj({{"t", "space"}, {"h", 10}}));
    Value items = Value::array(), shares = Value::array();
    items.push("Teacher Signature");
    items.push("Date");
    shares.push(0.62);
    shares.push(0.30);
    blocks.push(obj({{"t", "fields"}, {"items", items}, {"shares", shares}}));
    return make_doc(name + " Teacher Log", join_truthy({field(s, "teacher"), field(s, "school")}, " - "), blocks);
}

Value code_blocks(const std::string &codes) {
    Value out = Value::array();
    for (const auto &line : text::split(codes, "\n")) {
        size_t k = line.find(':');
        std::string label = k == std::string::npos ? "" : line.substr(0, k), code = k == std::string::npos ? "" : line.substr(k + 1);
        if (k != std::string::npos && !text::strip(label).empty() && !text::strip(code).empty())
            out.push(obj({{"t", "kv"}, {"label", text::strip(label) + ":"}, {"text", text::strip(code)}}));
        else if (!text::strip(line).empty()) out.push(obj({{"t", "p"}, {"text", text::strip(line)}}));
    }
    return out;
}

Value board_doc(const Value &lesson, store::Store &st, const Value *period, const Value *settings) {
    const Value &s = settings_of(st, settings);
    std::vector<std::string> wanted;
    for (const auto &w : s.get("board_sections").arr()) wanted.push_back(text_of(w));
    Value cols = obj({{"top", Value::array()}, {"left", Value::array()}, {"right", Value::array()}});
    for (const auto &sec : store::board_sections(s)) {
        if (std::find(wanted.begin(), wanted.end(), sec.key) == wanted.end()) continue;
        if (sec.key == "class_codes") {
            if (period && code_blocks(field(*period, "codes")).size())
                cols[sec.col].push(obj({{"key", sec.key}, {"label", sec.label}, {"blocks", code_blocks(field(*period, "codes"))}, {"col", sec.col}}));
            continue;
        }
        if (sec.key == "standards") {
            const Value &codes = lesson.get("standards");
            if (!codes.truthy()) continue;
            std::vector<std::string> cs;
            for (const auto &c : codes.arr()) cs.push_back(text_of(c));
            Value shrt = Value::array();
            shrt.push(obj({{"t", "p"}, {"text", text::join(cs, ", ")}}));
            Value blocks;
            if (s.has("board_std_text") ? s.get("board_std_text").truthy() : true) {
                blocks = Value::array();
                for (const auto &c : cs) blocks.push(obj({{"t", "kv"}, {"label", c}, {"text", st.std_text(c)}}));
            } else blocks = shrt;
            cols[sec.col].push(obj({{"key", sec.key}, {"label", sec.label}, {"blocks", blocks}, {"short", shrt}, {"col", sec.col}}));
        } else if (!text::strip(field(lesson, sec.key)).empty()) {
            Value blocks = sec.key == "vocab" ? vocab_blocks(field(lesson, sec.key)) : text_blocks(field(lesson, sec.key));
            cols[sec.col].push(obj({{"key", sec.key}, {"label", sec.label}, {"blocks", blocks}, {"col", sec.col}}));
        }
    }
    std::string meta = join_truthy({field(lesson, "course"), field(lesson, "unit"), period ? field(*period, "name") : ""}, "  |  ");
    std::string footer = join_truthy({field(s, "teacher"), field(s, "school")}, " - ");
    std::string title = field(lesson, "title");
    auto get_or = [&](const char *k, Value d) { return s.has(k) ? s.get(k) : d; };
    Value colors = Value::array();
    colors.push(get_or("primary_color", ""));
    colors.push(get_or("secondary_color", ""));
    colors.push(get_or("text_color", ""));
    return obj({{"title", title.empty() ? "Untitled Lesson" : title},
                {"date", field(lesson, "date")},
                {"meta", meta},
                {"footer", footer},
                {"top", cols["top"]},
                {"left", cols["left"]},
                {"right", cols["right"]},
                {"style", get_or("board_style", "chalk")},
                {"colors", colors},
                {"logo", st.logo() != nullptr},   // the picture itself comes from the store
                {"logo_place", get_or("logo_place", "left")},
                {"font", get_or("board_font", "")},
                {"head_font", get_or("board_head_font", "")},
                {"title_align", get_or("board_title_align", "left")},
                {"codes_place", get_or("board_codes_place", "right")},
                {"layout", get_or("board_layout", "auto")},
                {"panels", get_or("board_panels", "cards")},
                {"big_text", get_or("board_big_text", false)},
                {"order", s.get("board_order").truthy() ? s.get("board_order") : Value::array()}});
}

static Value sample_lesson() {
    return obj({{"title", "Making a Strong Claim"},
                {"date", ""},
                {"course", ""},
                {"unit", "Argument Writing"},
                {"question", "What makes people change their minds?"},
                {"targets", "I can write a claim that takes a clear side.\nI can back it up with **two** reasons."},
                {"success", "My claim fits in one sentence.\nEach reason connects to the claim."},
                {"bell_ringer", "Finish the sentence: The best school lunch is ___ because..."},
                {"materials", "Notebook\nClaim sentence starters"},
                {"vocab", "claim: what you are arguing\nevidence: facts that back it up"},
                {"homework", "Write one claim about something you care about."}});
}

Value preview_doc(store::Store &st, const Value *settings) {
    std::vector<const Value *> lessons;
    for (const auto &l : st.data.get("lessons").arr()) lessons.push_back(&l);
    std::stable_sort(lessons.begin(), lessons.end(), [](const Value *a, const Value *b) { return a->s("updated") > b->s("updated"); });
    Value lesson;
    if (!lessons.empty()) lesson = *lessons[0];
    else {
        Date t = today();
        static const char *MON[12] = {"Oct", "Nov", "Dec", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep"};
        (void)MON;
        static const char *ABBR[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        lesson = sample_lesson();
        lesson["date"] = std::string(ABBR[t.m - 1]) + " " + std::to_string(t.d) + ", " + std::to_string(t.y);
        lesson["course"] = st.settings().has("course") ? st.settings().get("course") : Value("");
    }
    const Value &s = settings_of(st, settings);
    auto periods = store::periods_for(s, lesson);
    return board_doc(lesson, st, periods.empty() ? nullptr : &periods[0], &s);
}

static Value sample_quiz() {
    Value qs = Value::array();
    qs.push(obj({{"type", "section"}, {"title", "Part One"}, {"prompt", ""}}));
    Value ch = Value::array();
    ch.push("Lunch is at noon.");
    ch.push("Lunch should be longer.");
    ch.push("Lunch has pizza today.");
    qs.push(obj({{"type", "mc"}, {"prompt", "Which sentence is a claim?"}, {"points", 1}, {"answer", 1}, {"choices", ch}}));
    qs.push(obj({{"type", "short"}, {"prompt", "Give one reason that backs up your claim."}, {"points", 2}, {"lines", 3}}));
    return qs;
}

static const Value *newest(const std::vector<const Value *> &xs) {
    const Value *best = nullptr;
    for (const Value *x : xs)   // max(): the first of equals wins
        if (!best || x->s("updated") > best->s("updated")) best = x;
    return best;
}

Value layout_sample(store::Store &st, const std::string &kind, const Value &settings) {
    if (kind == "lesson" || kind == "makeup") {
        std::vector<const Value *> ls;
        for (const auto &l : st.data.get("lessons").arr()) ls.push_back(&l);
        const Value *n = newest(ls);
        Value lesson;
        if (n) lesson = *n;
        else {
            lesson = sample_lesson();
            lesson["course"] = settings.has("course") ? settings.get("course") : Value("");
            lesson["date"] = "Monday";
            lesson["instruction"] = "Notes on claims";
            lesson["closure"] = "Write one claim.";
        }
        return kind == "lesson" ? lesson_doc(lesson, st, &settings) : makeup_doc(lesson, st, &settings);
    }
    std::vector<std::string> want;
    if (kind == "annotation") want = {store::ANNOTATION};
    else if (kind == "bell") want = {store::BELL_SHEET};
    std::vector<const Value *> as;
    for (const auto &a : st.data.get("assessments").arr()) {
        std::string k = a.s("kind");
        bool sheet = std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), k) != store::SHEET_KINDS.end();
        if (want.empty() ? !sheet : std::find(want.begin(), want.end(), k) != want.end()) as.push_back(&a);
    }
    const Value *n = newest(as);
    Value a;
    if (n) a = *n;
    else {
        a = store::new_assessment(settings, want.empty() ? "Quiz" : want[0]);
        a["id"] = "sample";
        a["title"] = "Making a Strong Claim";
        a["instructions"] = "Answer every question.";
        if (want.empty()) a["questions"] = sample_quiz();
    }
    return assessment_doc(a, st, 0, 1, false, &settings);
}

// ----------------------------------------------------------------------------- assessments

std::string handout_name(const Value &a) {
    std::string title = field(a, "title");
    if (title.empty()) title = "Untitled";
    std::string kind = field(a, "kind");
    bool named = true;
    if (!kind.empty()) {
        auto words = text::split_ws(kind);
        named = !words.empty() && text::contains(text::lower(title), text::lower(words[0]));
    }
    return named ? title : title + " (" + kind + ")";
}

static Value grid_columns(const std::vector<std::string> &names) {
    Value out = Value::array();
    bool usual = names.empty() || names.size() == store::ANNOTATION_COLS.size();
    if (usual && !names.empty())
        for (size_t i = 0; i < names.size(); i++) if (names[i] != store::ANNOTATION_COLS[i].first) usual = false;
    if (usual) {
        for (const auto &c : store::ANNOTATION_COLS) {
            Value p = Value::array();
            p.push(c.first);
            p.push(c.second);
            out.push(p);
        }
        return out;
    }
    for (const auto &n : names) {
        Value p = Value::array();
        p.push(n);
        p.push(1.0 / (double)names.size());
        out.push(p);
    }
    return out;
}

Value annotation_doc(const Value &a, store::Store &st, const Value *settings) {
    const Value &s = settings_of(st, settings);
    Page pg(s, "annotation");
    std::string title = field(a, "title");
    if (title.empty()) title = "Annotation";
    Value tb = Value::array(), before = Value::array(), after = Value::array();
    tb.push(obj({{"t", "title"}, {"text", title}}));
    before.push(obj({{"t", "space"}, {"h", 4}}));
    after.push(obj({{"t", "space"}, {"h", 8}}));
    Value blocks = pg.top(tb, before, after);
    if (blocks.size() && blocks.at(0).s("t") == "space") blocks.arr_ref().erase(blocks.arr_ref().begin());
    extend(blocks, text_blocks(field(a, "instructions"), "italic"));
    blocks.push(obj({{"t", "grid"}, {"cols", grid_columns(store::layout_fields(pg.lay.s("columns")))}, {"rows", int_or(a.get("rows"), 10)}}));
    std::string footer = join_truthy({field(s, "teacher"), field(a, "course")}, " - ");
    return make_doc(title, pg.footer(footer), blocks);
}

Value bell_sheet_doc(const Value &a, store::Store &st, const Value *settings) {
    const Value &s = settings_of(st, settings);
    Page pg(s, "bell");
    std::string title = field(a, "title");
    if (title.empty()) title = "Bell Ringers";
    std::vector<std::string> weeks, days;
    for (const auto &w : a.get("weeks").arr()) weeks.push_back(text_of(w));
    weeks.push_back("");
    weeks.push_back("");
    for (const auto &d : a.get("days").arr()) days.push_back(text_of(d));
    for (int i = 0; i < 10; i++) days.push_back("");
    Value blocks = Value::array();
    for (int w = 0; w < 2; w++) {
        Value head = Value::array();
        head.push(obj({{"t", "title"}, {"text", title}}));
        if (!text::strip(weeks[(size_t)w]).empty()) head.push(obj({{"t", "subtitle"}, {"text", "Week of " + text::strip(weeks[(size_t)w])}}));
        else {
            Value shares = Value::array();
            shares.push(0.45);
            head.push(obj({{"t", "fields"}, {"items", strings({"Week of"})}, {"shares", shares}, {"center", pg.align == "center"}}));
        }
        if (w) {
            blocks.push(obj({{"t", "pagebreak"}}));
            for (auto &b : head.arr_ref()) {
                std::string t = b.s("t");
                if (t == "title" || t == "subtitle") b["align"] = pg.align;
            }
            extend(blocks, head);
        } else {
            Value top = pg.top(head);
            if (pg.lay.s("name_place") == "top" && top.at(0).s("t") == "fields")
                top.arr_ref().insert(top.arr_ref().begin() + 1, obj({{"t", "space"}, {"h", 4}}));
            extend(blocks, top);
            extend(blocks, text_blocks(field(a, "instructions"), "italic"));
        }
        Value dd = Value::array();
        for (int i = 0; i < 5; i++) {
            Value p = Value::array();
            p.push(store::WEEKDAYS[(size_t)i]);
            p.push(days[(size_t)(w * 5 + i)]);
            dd.push(p);
        }
        blocks.push(obj({{"t", "days"}, {"days", dd}}));
    }
    std::string footer = join_truthy({field(s, "teacher"), field(a, "course")}, " - ");
    return make_doc(title, pg.footer(footer), blocks);
}

static void shuffle_questions(std::vector<Value> &qs, PyRandom &rng) {
    std::vector<Value> out, run;
    for (auto &q : qs) {
        std::string t = q.s("type");
        if (t == "passage" || t == "section") {
            rng.shuffle(run);
            for (auto &x : run) out.push_back(x);
            out.push_back(q);
            run.clear();
        } else run.push_back(q);
    }
    rng.shuffle(run);
    for (auto &x : run) out.push_back(x);
    qs = out;
}

static bool anchored_choice(const std::string &c) {
    static const std::regex R("\\b(all|none|both|neither)\\b.*\\babove\\b|^(both|neither)\\b|^[A-D] and [A-D]\\b",
                              std::regex::ECMAScript | std::regex::icase);
    return std::regex_search(c, R);
}

static void shuffle_choices(Value &q, PyRandom &rng) {
    const Value &choices = q.get("choices");
    size_t n = choices.size();
    if (!choices.is_array() || n < 3) return;
    for (const auto &c : choices.arr())
        if (anchored_choice(text_of(c))) return;
    std::vector<size_t> order;
    for (size_t i = 0; i < n; i++) order.push_back(i);
    rng.shuffle(order);
    Value shuffled = Value::array();
    for (size_t i : order) shuffled.push(choices.at(i));
    q["choices"] = shuffled;
    const Value &ans = q.get("answer");
    if (!ans.is_null() && ans.is_number() && ans.as_int() < (long long)n)
        for (size_t k = 0; k < n; k++)
            if ((long long)order[k] == ans.as_int()) { q["answer"] = (long long)k; break; }
}

static std::string letter(long i) { return i >= 0 && i < 10 ? std::string(1, LETTERS[i]) : "?"; }

Value assessment_doc(const Value &a, store::Store &st, int version, int versions, bool key, const Value *settings) {
    std::string kind = a.s("kind");
    if (kind == store::ANNOTATION) return annotation_doc(a, st, settings);
    if (kind == store::BELL_SHEET) return bell_sheet_doc(a, st, settings);
    const Value &s = settings_of(st, settings);
    std::vector<Value> qs;
    for (const auto &q : a.get("questions").arr()) qs.push_back(q);
    std::string id = text_of(a.get("id"));
    PyRandom rng(id + ":" + std::to_string(version));
    if (version > 0) {
        shuffle_questions(qs, rng);
        for (auto &q : qs)
            if (q.s("type") == "mc") shuffle_choices(q, rng);
    }
    Page pg(s, "assessment");
    std::string title = field(a, "title");
    if (title.empty()) title = "Untitled " + (a.has("kind") ? text_of(a.get("kind")) : std::string("Assessment"));
    double total = 0;
    for (const auto &q : qs) total += store::points_of(q);
    Value head = Value::array();
    head.push(obj({{"t", "title"}, {"text", title + (key ? " - Answer Key" : "")}}));
    std::vector<std::string> sub = {field(a, "kind"), field(a, "course")};
    if (versions > 1) sub.push_back(std::string("Version ") + letter(version));
    bool show_points = a.has("show_points") ? a.get("show_points").truthy() : true;
    if (show_points && total) sub.push_back(store::fmt_points(total) + " point" + (total == 1 ? "" : "s"));
    head.push(obj({{"t", "subtitle"}, {"text", join_truthy(sub, "   |   ")}}));
    bool show_name = a.has("show_name") ? a.get("show_name").truthy() : true;
    Value blocks = pg.top(head, Value::array(), Value::array(), show_name && !key);
    if (a.get("standards").truthy() && (a.get("show_standards").truthy() || key)) {
        std::vector<std::string> cs;
        for (const auto &c : a.get("standards").arr()) cs.push_back(text_of(c));
        blocks.push(obj({{"t", "p"}, {"text", "Standards: " + text::join(cs, ", ")}, {"style", "small"}}));
    }
    Value instr = text_blocks(field(a, "instructions"));
    std::string label = text::strip(pg.lay.s("directions_label"));
    if (instr.size() && !label.empty()) {
        Value first = instr.at(0);
        if (first.s("t") == "p") instr.at(0) = obj({{"t", "kv"}, {"label", label}, {"text", first.get("text")}});
        else instr.arr_ref().insert(instr.arr_ref().begin(), obj({{"t", "kv"}, {"label", label}, {"text", ""}}));
    }
    extend(blocks, instr);
    std::string number = pg.lay.s("number_style");
    int num = 0;
    std::vector<std::string> quick;
    Value body = Value::array();
    for (const auto &q : qs) {
        std::string t = q.s("type");
        if (t == "section") {
            std::string st_ = field(q, "title");
            body.push(pg.h1(st_.empty() ? "Section" : st_));
            extend(body, text_blocks(field(q, "prompt"), "italic"));
            continue;
        }
        if (t == "passage") {
            body.push(obj({{"t", "passage"}, {"title", q.has("title") ? q.get("title") : Value("")},
                           {"text", q.has("text") ? q.get("text") : Value("")}, {"numbered", q.has("numbered") ? q.get("numbered") : Value(true)}}));
            continue;
        }
        num++;
        double pts = store::points_of(q);
        std::string prompt = field(q, "prompt");
        if (t == "chart" && text::strip(prompt).empty()) prompt = store::chart_direction(q.s("layout"));
        if (t == "fill") prompt = has_blank(prompt) ? norm_blanks(prompt) : prompt + " " + std::string(14, '_');
        std::string lab;
        if (show_points && pts) lab = "(" + store::fmt_points(pts) + " pt" + (pts == 1 ? "" : "s") + ")";
        if (key && q.get("standard").truthy()) lab = text::strip(lab + "  [" + text_of(q.get("standard")) + "]");
        body.push(obj({{"t", "space"}, {"h", 8}}));
        body.push(obj({{"t", "q"}, {"num", text::replace(number, "1", std::to_string(num))}, {"text", prompt}, {"points", lab}}));
        const Value &ans = q.get("answer");
        if (t == "mc") {
            const Value &choices = q.get("choices");
            size_t n = choices.truthy() ? choices.size() : 0;
            for (size_t i = 0; i < n; i++) {
                bool correct = key && ans.is_number() && !ans.is_bool() && ans.as_double() == (double)i;
                body.push(obj({{"t", "choice"}, {"label", letter((long)i) + "."}, {"text", choices.at(i)}, {"correct", correct}, {"last", i == n - 1}}));
            }
            if (!ans.is_null() && ans.as_int() < (long long)n) quick.push_back(std::to_string(num) + "-" + letter((long)ans.as_int()));
        } else if (t == "tf") {
            Value correct = key && !ans.is_null() ? Value(ans.truthy() ? 0 : 1) : Value();
            body.push(obj({{"t", "choice_inline"}, {"items", strings({"True", "False"})}, {"correct", correct}}));
            if (!ans.is_null()) quick.push_back(std::to_string(num) + "-" + (ans.truthy() ? "T" : "F"));
        } else if (t == "short") {
            if (key) body.push(obj({{"t", "answer"}, {"text", ans.truthy() ? ans : Value("Answers will vary.")}}));
            else body.push(obj({{"t", "lines"}, {"n", int_or(q.get("lines"), 3)}}));
        } else if (t == "essay") {
            if (key) body.push(obj({{"t", "answer"}, {"text", ans.truthy() ? ans : Value("Answers will vary; see rubric.")}}));
            else {
                long n = int_or(q.get("lines"), 12);
                std::string space = q.has("space") ? text_of(q.get("space")) : "lines";
                std::string kind_ = space == "blank" || space == "box" ? space : "lines";
                body.push(obj({{"t", kind_}, {"n", n}}));
            }
        } else if (t == "chart") {
            if (key) body.push(obj({{"t", "answer"}, {"text", ans.truthy() ? ans : Value("Answers will vary.")}}));
            else {
                Value b = Value::object();
                for (const char *k : {"layout", "cols", "rows", "circles", "heads", "side", "sidecol", "center", "preset"}) b[k] = q.get(k);
                long n = int_or(q.get("lines"), 0);
                b["t"] = "organizer";
                b["n"] = n <= 0 ? (long)organizers::auto_lines(q) : std::max(n, 4L);
                b["fill"] = n < 0;
                body.push(b);
            }
        } else if (t == "fill") {
            if (key) body.push(obj({{"t", "answer"}, {"text", ans.truthy() ? ans : Value("-")}}));
            if (ans.truthy()) quick.push_back(std::to_string(num) + "-" + text_of(ans));
        } else if (t == "match") {
            std::vector<std::pair<Value, Value>> pairs;
            for (const auto &p : q.get("pairs").arr())
                if (p.at(0).truthy() || p.at(1).truthy()) pairs.push_back({p.at(0), p.at(1)});
            std::vector<size_t> right;
            for (size_t i = 0; i < pairs.size(); i++) right.push_back(i);
            PyRandom r2(id + ":" + std::to_string(version) + ":" + std::to_string(num));
            r2.shuffle(right);
            std::vector<std::string> letters;
            for (size_t i = 0; i < pairs.size(); i++) {
                long at = (long)(std::find(right.begin(), right.end(), i) - right.begin());
                letters.push_back(i < 10 ? letter(at) : "?");
            }
            Value left = Value::array(), rgt = Value::array();
            for (auto &p : pairs) left.push(p.first);
            for (size_t i : right) rgt.push(pairs[i].second);
            body.push(obj({{"t", "match"}, {"left", left}, {"right", rgt}, {"key", key ? strings(letters) : Value()}}));
            if (!pairs.empty()) quick.push_back(std::to_string(num) + "-" + text::join(letters, ""));
        }
    }
    if (key && !quick.empty()) blocks.push(obj({{"t", "kv"}, {"label", "Quick key:"}, {"text", text::join(quick, ", ")}}));
    extend(blocks, body);
    std::string footer = join_truthy({field(s, "teacher"), field(a, "course"), title}, " - ");
    return make_doc(title, pg.footer(footer), blocks);
}

// ----------------------------------------------------------------------------- curriculum map

std::string span(const std::vector<Date> &days) {
    if (days.empty()) return "";
    Date a = *std::min_element(days.begin(), days.end()), b = *std::max_element(days.begin(), days.end());
    if (a == b) return dates::fmt_date(a);
    return dates::fmt_date(a, false, a.y != b.y) + " - " + dates::fmt_date(b);
}

bool item_school_year(const Value &x, int &year) {
    auto days = store::lesson_dates(x);
    Date made;
    if (!days.empty()) made = days[0];
    else if (!dates::stamp_day(field(x, "created"), made) && !dates::stamp_day(field(x, "updated"), made)) return false;
    year = dates::school_year(made);
    return true;
}

std::vector<int> school_years(const std::vector<const Value *> &items) {
    std::set<int> ys;
    for (const Value *x : items) {
        int y;
        if (item_school_year(*x, y)) ys.insert(y);
    }
    return std::vector<int>(ys.rbegin(), ys.rend());
}

std::vector<Unit> curriculum_units(store::Store &st, const std::string &course, int year) {
    auto mine = [&](const Value &x) {
        if (!store::in_class(x, course)) return false;
        if (!year) return true;
        int y;
        return item_school_year(x, y) && y == year;
    };
    std::vector<const Value *> lessons, assessments;
    for (const auto &l : st.data.get("lessons").arr()) if (mine(l)) lessons.push_back(&l);
    for (const auto &a : st.data.get("assessments").arr()) if (mine(a)) assessments.push_back(&a);
    std::vector<std::string> order;
    std::map<std::string, std::vector<const Value *>> groups;
    for (const Value *l : lessons) {
        std::string u = text::strip(field(*l, "unit"));
        if (!groups.count(u)) order.push_back(u);
        groups[u].push_back(l);
    }
    std::vector<Unit> out;
    const Date MAXD{9999, 12, 31}, MIND{1, 1, 1};
    for (const auto &unit : order) {
        auto ls = groups[unit];
        auto first = [&](const Value *l) {
            auto ds = store::lesson_dates(*l);
            return ds.empty() ? MAXD : *std::min_element(ds.begin(), ds.end());
        };
        std::stable_sort(ls.begin(), ls.end(), [&](const Value *a, const Value *b) {
            Date fa = first(a), fb = first(b);
            if (fa != fb) return fa < fb;
            return text::lower(field(*a, "title")) < text::lower(field(*b, "title"));
        });
        Unit u;
        u.unit = unit;
        u.lessons = ls;
        std::set<std::string> linked;
        for (const Value *l : ls) {
            for (const auto &d : store::lesson_dates(*l)) u.days.push_back(d);
            for (const auto &aid : l->get("assessments").arr()) linked.insert(text_of(aid));
        }
        for (const auto &a : st.data.get("assessments").arr())
            if (linked.count(text_of(a.get("id")))) u.assessments.push_back(&a);
        for (const Value *a : assessments)
            if (!unit.empty() && text::strip(field(*a, "unit")) == unit && !linked.count(text_of(a->get("id")))) u.assessments.push_back(a);
        auto add_unique = [](std::vector<std::string> &v, const std::string &x) {
            if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
        };
        for (const Value *x : ls)
            for (const auto &c : x->get("standards").arr()) add_unique(u.standards, text_of(c));
        for (const Value *x : u.assessments)
            for (const auto &c : x->get("standards").arr()) add_unique(u.standards, text_of(c));
        for (const Value *l : ls) {
            std::string q = text::strip(field(*l, "question"));
            if (!q.empty()) add_unique(u.questions, q);
        }
        out.push_back(u);
    }
    std::stable_sort(out.begin(), out.end(), [&](const Unit &a, const Unit &b) {
        bool na = a.unit.empty(), nb = b.unit.empty();
        if (na != nb) return !na;
        bool da = a.days.empty(), db = b.days.empty();
        if (da != db) return !da;
        Date ma = da ? MIND : *std::min_element(a.days.begin(), a.days.end());
        Date mb = db ? MIND : *std::min_element(b.days.begin(), b.days.end());
        if (ma != mb) return ma < mb;
        return store::unit_sort_key(a.unit) < store::unit_sort_key(b.unit);
    });
    return out;
}

// "RL.9-10.2" before "RL.9-10.10"
static bool code_less(const std::string &a, const std::string &b) {
    auto parts = [](const std::string &s) {
        std::vector<std::pair<int, std::string>> out;   // (0, digits) or (1, lower text)
        size_t i = 0;
        while (i < s.size()) {
            size_t j = i;
            bool dig = isdigit((unsigned char)s[i]);
            while (j < s.size() && (bool)isdigit((unsigned char)s[j]) == dig) j++;
            std::string p = s.substr(i, j - i);
            if (dig) {
                size_t z = 0;
                while (z + 1 < p.size() && p[z] == '0') z++;
                out.push_back({0, p.substr(z)});
            } else out.push_back({1, text::lower(p)});
            i = j;
        }
        return out;
    };
    auto pa = parts(a), pb = parts(b);
    for (size_t i = 0; i < pa.size() && i < pb.size(); i++) {
        if (pa[i].first != pb[i].first) return pa[i].first < pb[i].first;
        if (pa[i].first == 0) {
            if (pa[i].second.size() != pb[i].second.size()) return pa[i].second.size() < pb[i].second.size();
            if (pa[i].second != pb[i].second) return pa[i].second < pb[i].second;
        } else if (pa[i].second != pb[i].second) return pa[i].second < pb[i].second;
    }
    return pa.size() < pb.size();
}

static std::string plural(size_t n, const std::string &word) { return std::to_string(n) + " " + word + (n != 1 ? "s" : ""); }

Value curriculum_doc(store::Store &st, const std::string &course, const std::string &subject, const std::string &grades, int year) {
    const Value &s = st.settings();
    auto units = curriculum_units(st, course, year);
    std::string name = (course.empty() ? "Lessons With No Class" : course) + (year ? " (" + dates::school_year_label(year) + ")" : "");
    size_t nl = 0;
    std::vector<Date> all;
    for (const auto &u : units) {
        nl += u.lessons.size();
        all.insert(all.end(), u.days.begin(), u.days.end());
    }
    std::string sp = span(all);
    std::string count = plural(units.size(), "unit") + ", " + plural(nl, "lesson");
    std::vector<std::string> sub = {field(s, "teacher"), field(s, "school"), sp, count};
    Value blocks = Value::array();
    blocks.push(obj({{"t", "title"}, {"text", "Curriculum Map: " + name}}));
    blocks.push(obj({{"t", "subtitle"}, {"text", join_truthy(sub, "   |   ")}}));
    auto utitle = [](const Unit &u) { return u.unit.empty() ? std::string("Lessons Without a Unit") : u.unit; };
    if (units.empty()) blocks.push(obj({{"t", "p"}, {"text", "No lessons are planned for this class yet."}}));
    else {
        blocks.push(obj({{"t", "h1"}, {"text", "At a Glance"}}));
        for (const auto &u : units) {
            std::vector<std::string> bits = {span(u.days), plural(u.lessons.size(), "lesson")};
            if (!u.assessments.empty()) bits.push_back(plural(u.assessments.size(), "assessment"));
            if (!u.standards.empty()) bits.push_back("Standards: " + text::join(u.standards, ", "));
            blocks.push(obj({{"t", "kv"}, {"label", utitle(u) + ":"}, {"text", join_truthy(bits, "   |   ")}}));
        }
    }
    for (const auto &u : units) {
        blocks.push(obj({{"t", "h1"}, {"text", utitle(u) + (u.days.empty() ? "" : "  (" + span(u.days) + ")")}}));
        if (!u.questions.empty()) {
            blocks.push(obj({{"t", "kv"}, {"label", "Essential Questions:"}, {"text", ""}}));
            for (const auto &q : u.questions) blocks.push(obj({{"t", "bullet"}, {"text", q}}));
        }
        if (!u.standards.empty()) blocks.push(obj({{"t", "kv"}, {"label", "Standards:"}, {"text", text::join(u.standards, ", ")}}));
        blocks.push(obj({{"t", "kv"}, {"label", "Lessons:"}, {"text", ""}}));
        for (const Value *l : u.lessons) {
            std::string when = span(store::lesson_dates(*l));
            if (when.empty()) when = text::strip(field(*l, "date"));
            std::string t = field(*l, "title");
            blocks.push(obj({{"t", "bullet"}, {"text", (when.empty() ? "" : when + ": ") + (t.empty() ? "Untitled" : t)}}));
        }
        if (!u.assessments.empty()) {
            blocks.push(obj({{"t", "kv"}, {"label", "Assessments:"}, {"text", ""}}));
            for (const Value *a : u.assessments) blocks.push(obj({{"t", "bullet"}, {"text", handout_name(*a)}}));
        }
    }
    // where each standard is taught
    std::vector<std::string> codes;
    std::map<std::string, std::vector<std::pair<std::string, int>>> where;
    auto bump = [&](const std::string &c, const std::string &t, int add) {
        if (!where.count(c)) codes.push_back(c);
        auto &places = where[c];
        for (auto &p : places)
            if (p.first == t) { p.second += add; return; }
        places.push_back({t, add});
    };
    for (const auto &u : units) {
        for (const Value *l : u.lessons)
            for (const auto &c : l->get("standards").arr()) bump(text_of(c), utitle(u), 1);
        for (const Value *a : u.assessments)
            for (const auto &c : a->get("standards").arr()) bump(text_of(c), utitle(u), 0);
    }
    if (!codes.empty()) {
        blocks.push(obj({{"t", "h1"}, {"text", "Standards Coverage"}}));
        std::vector<std::string> sorted = codes;
        std::stable_sort(sorted.begin(), sorted.end(), code_less);
        for (const auto &c : sorted) {
            std::vector<std::string> places;
            for (const auto &p : where[c])
                places.push_back(p.second ? p.first + " (" + plural((size_t)p.second, "lesson") + ")" : p.first + " (assessed)");
            blocks.push(obj({{"t", "kv"}, {"label", c + ":"}, {"text", text::join(places, ", ")}}));
        }
    }
    if (!subject.empty() && std::find(st.subjects.begin(), st.subjects.end(), subject) != st.subjects.end()) {
        std::vector<const Value *> missing;
        for (const auto &sd : st.standards) {
            if (sd.s("subject") != subject || sd.get("parent").truthy() || sd.get("custom").truthy()) continue;
            if (!store::grades_match(sd.s("grades"), grades)) continue;
            std::string code = sd.s("code");
            if (where.count(code)) continue;
            bool part = false;
            for (const auto &c : codes) {
                const Value *x = st.standard(c);
                if (text::starts_with(c, code) && x && x->get("parent").is_string() && x->get("parent").str() == code) { part = true; break; }
            }
            if (!part) missing.push_back(&sd);
        }
        blocks.push(obj({{"t", "h1"}, {"text", "Not Taught Yet (" + subject + (grades.empty() ? "" : ", grades " + grades) + ")"}}));
        if (!missing.empty())
            for (const Value *m : missing) blocks.push(obj({{"t", "kv"}, {"label", m->s("code") + ":"}, {"text", st.std_text(m->s("code"))}}));
        else blocks.push(obj({{"t", "p"}, {"text", "Every standard is taught somewhere in this plan."}}));
    }
    std::string footer = join_truthy({field(s, "teacher"), "Curriculum Map", name}, " - ");
    return make_doc("Curriculum Map - " + name, footer, blocks);
}

std::vector<std::vector<std::string>> curriculum_rows(store::Store &st, const std::string &course, int year) {
    std::vector<std::vector<std::string>> rows = {
        {"School Year", "Class", "Unit", "Unit Dates", "Lesson", "Lesson Dates", "Essential Question", "Standards", "Assessments"}};
    for (const auto &u : curriculum_units(st, course, year))
        for (const Value *l : u.lessons) {
            int y = 0;
            bool has = item_school_year(*l, y);
            std::string when = span(store::lesson_dates(*l));
            if (when.empty()) when = text::strip(field(*l, "date"));
            std::vector<std::string> cs, hs;
            for (const auto &c : l->get("standards").arr()) cs.push_back(text_of(c));
            for (const Value *a : st.attached(*l)) hs.push_back(handout_name(*a));
            std::string t = field(*l, "title");
            rows.push_back({has ? dates::school_year_label(y) : "", course, u.unit, span(u.days), t.empty() ? "Untitled" : t, when,
                            text::strip(field(*l, "question")), text::join(cs, ", "), text::join(hs, "; ")});
        }
    return rows;
}

}  // namespace doc
