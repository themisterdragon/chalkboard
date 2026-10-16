// The terminal app's export and settings screens.
#include <algorithm>
#include <filesystem>

#include "art.h"
#include "board.h"
#include "doc.h"
#include "exporting.h"
#include "files.h"
#include "fontlib.h"
#include "proc.h"
#include "text.h"
#include "tui_app.h"

namespace tui {

using json::Value;
using store::field;
using store::text_of;

static std::string key_text(int k) {
    if (k < 0x20 || k >= term::K_ENTER || k == 0x7f) return "";
    return text::to8((char32_t)k);
}

static std::string lpad(const std::string &s, size_t n) { return text::ljust(s, (long)n); }

static std::string label_of(const std::vector<std::pair<std::string, std::string>> &m, const std::string &k, const std::string &dflt = "") {
    for (const auto &x : m) if (x.first == k) return x.second;
    return dflt;
}

static UI::Row hi_row(const UI &ui, const std::string &t, bool hi = true) {
    UI::Row r;
    r.text = t;
    if (hi) {
        r.has_style = true;
        r.st = ui.HI;
    }
    return r;
}

static std::string format_label(const std::string &f) {
    if (f == "PDF") return "PDF";
    if (f == "DOCX") return "WORD (.DOCX - ALSO GOOGLE DOCS)";
    if (f == "TXT") return "PLAIN TEXT";
    if (f == "PNG") return "BOARD SLIDE (1920x1080 PNG + EDITABLE .PPTX SLIDESHOW)";
    if (f == "MAKEUP") return "MAKE-UP SHEET + CLASSWORK FOR ABSENT STUDENTS (PDF + DOCX)";
    if (f == "ALL") return "ALL FORMATS";
    return text::upper(f);
}

static std::string relpath(const std::string &p, const std::string &folder) {
    std::error_code ec;
    auto r = std::filesystem::relative(std::filesystem::u8path(p), std::filesystem::u8path(folder), ec);
    return ec || r.empty() ? p : r.u8string();
}

// ----------------------------------------------------------------------------- exports

void App::export_screen(const std::string &kind, const std::string &id) {
    std::vector<std::string> done;
    std::string fkey = kind == "lesson" ? "lesson_export_format" : "export_format";
    const auto &formats = kind == "lesson" ? exporting::LESSON_FORMATS : exporting::ASSESSMENT_FORMATS;
    if (std::find(formats.begin(), formats.end(), field(st(), fkey)) == formats.end()) st()[fkey] = "PDF";
    auto obj = [&]() -> Value * { return kind == "lesson" ? lesson(id) : assessment(id); };
    struct F { std::string key, text; };
    std::vector<F> fields;
    auto refresh = [&] {
        fields.clear();
        Value *o = obj();
        if (!o) return 0L;
        std::string fmt = field(st(), fkey);
        fields.push_back({"format", "FORMAT ........ " + format_label(fmt)});
        if (kind == "lesson") {
            auto linked = store_.attached(*o);
            if (!linked.empty())
                fields.push_back({"linked", "WORKSHEETS .... " + std::to_string(linked.size()) + " LINKED" +
                                                (fmt == "ALL" ? "  (INCLUDED: PDF + DOCX IN THE LESSON FOLDER)" : "  (PICK ALL FORMATS TO INCLUDE THEM)")});
        }
        std::string akind = o->s("kind");
        if (kind == "assessment" && std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), akind) == store::SHEET_KINDS.end()) {
            std::string inc = field(st(), "export_include");
            std::string inc_label = inc == "STUDENT" ? "STUDENT COPY ONLY" : inc == "KEY" ? "ANSWER KEY ONLY" : "STUDENT COPY + ANSWER KEY";
            long v = (long)st().get("export_versions").as_int(1);
            fields.push_back({"include", "INCLUDE ....... " + inc_label});
            fields.push_back({"versions", "VERSIONS ...... " + std::to_string(v) + (v > 1 ? "  (A = ORIGINAL ORDER; OTHERS SHUFFLED)" : "")});
        }
        if ((fmt == "PNG" || fmt == "ALL") && kind == "lesson")
            fields.push_back({"board", "BOARD SLIDE ... " + label_of(store::BOARD_STYLES, field(st(), "board_style")) + "  (BOARD DESIGNER)"});
        if (fmt != "PNG") fields.push_back({"font", "FONT .......... " + text::upper(field(st(), "font")) + "  /  " + text::upper(field(st(), "page")) + " PAPER"});
        std::string folder = store_.export_dir();
        for (const auto &d : exporting::export_subdirs(kind, *o)) folder = files::join(folder, d);
        fields.push_back({"folder", "FOLDER ........ " + folder});
        fields.push_back({"go", ">>> EXPORT NOW <<<"});
        return (long)fields.size();
    };
    Value *o0 = obj();
    if (!o0) return;
    std::string title = field(*o0, "title");
    refresh();
    ui.list_screen(
        "EXPORT: " + (title.empty() ? "untitled" : title), refresh, [&](long i, int) { return hi_row(ui, fields[(size_t)i].text, fields[(size_t)i].key == "go"); },
        [&](long i) -> Result {
            std::string key = fields[(size_t)i].key;
            if (key == "format") {
                auto it = std::find(formats.begin(), formats.end(), field(st(), fkey));
                st()[fkey] = formats[(size_t)((it - formats.begin() + 1) % (long)formats.size())];
            } else if (key == "include") {
                std::vector<std::string> order = {"BOTH", "STUDENT", "KEY"};
                auto it = std::find(order.begin(), order.end(), field(st(), "export_include"));
                st()["export_include"] = order[(size_t)(((it == order.end() ? 0 : it - order.begin()) + 1) % 3)];
            } else if (key == "versions") st()["export_versions"] = st().get("export_versions").as_int(1) % 4 + 1;
            else if (key == "board") board_settings();
            else if (key == "linked") lesson_attachments(id);
            else if (key == "font") st()["font"] = field(st(), "font") == "Times" ? "Helvetica" : "Times";
            else if (key == "folder") {
                std::string v;
                if (ui.prompt("EXPORT FOLDER", v, store_.export_dir()) && !v.empty()) st()["export_dir"] = v;
            } else if (key == "go") {
                auto files = do_export(kind, id, field(st(), fkey));
                if (!files.empty()) {
                    done = files;
                    return Result::back();
                }
            }
            save();
            return Result::move(i);
        },
        nullptr, "1-0 CHANGE / EXPORT  ESC CANCEL", [] { return std::string("PRESS A NUMBER TO CHANGE AN OPTION"); }, "NOTHING HERE YET.",
        (long)fields.size() - 1);
    if (!done.empty()) export_done(done);
}

void App::day_slideshow() {
    if (!st().get("class_periods").truthy()) {
        ui.msg = "?ADD YOUR CLASS PERIODS FIRST: SETTINGS > CLASS PERIODS & CODES.";
        return;
    }
    Date t = today();
    auto got = ui.calendar("DAY SLIDESHOW: PICK THE DAY", &t, nullptr, lesson_marks());
    if (got.kind != UI::Picked::DAYS) return;
    Date md = got.first;
    std::vector<const Value *> all;
    for (const auto &l : store_.data.get("lessons").arr()) all.push_back(&l);
    std::vector<const Value *> lessons = all;
    std::stable_sort(lessons.begin(), lessons.end(), [](const Value *a, const Value *b) { return a->s("updated") > b->s("updated"); });
    std::vector<std::pair<Value, std::string>> plan;   // (period, lesson id or "")
    for (const auto &p : store::day_plan(st(), lessons, md)) plan.push_back({p.first, p.second ? p.second->s("id") : ""});
    long n = 0;
    for (const auto &p : plan) if (!p.second.empty()) n++;
    ui.list_screen(
        "DAY SLIDESHOW: " + text::upper(dates::fmt_date(md, true)), [&] { return (long)plan.size(); },
        [&](long i, int) {
            const Value &p = plan[(size_t)i].first;
            std::string name = (p.get("name").truthy() ? field(p, "name") : "CLASS") +
                               (text::strip(field(p, "course")).empty() ? "" : " (" + field(p, "course") + ")");
            Value *l = plan[(size_t)i].second.empty() ? nullptr : lesson(plan[(size_t)i].second);
            std::string lt = l ? field(*l, "title") : "";
            return hi_row(ui, lpad(text::slice(text::upper(name), 0, 30), 31) + " " + (l ? (lt.empty() ? "UNTITLED" : lt) : "(SKIPPED)"));
        },
        [&](long i) {
            std::string course = text::lower(text::strip(field(plan[(size_t)i].first, "course")));
            std::vector<const Value *> mine;
            for (const Value *l : lessons)
                if (course.empty() || text::lower(text::strip(field(*l, "course"))) == course) mine.push_back(l);
            std::vector<std::string> labels = {"(SKIP THIS PERIOD)"};
            long cur = 0;
            for (size_t k = 0; k < mine.size(); k++) {
                std::string tt = field(*mine[k], "title");
                labels.push_back((tt.empty() ? "untitled" : tt) + (mine[k]->get("date").truthy() ? "  " + first_line(field(*mine[k], "date")) : ""));
                if (mine[k]->s("id") == plan[(size_t)i].second) cur = (long)k + 1;
            }
            std::string pname = field(plan[(size_t)i].first, "name");
            long j = ui.choose("LESSON FOR " + text::upper(pname.empty() ? "THIS CLASS" : pname), labels, cur);
            if (j >= 0) plan[(size_t)i].second = j ? mine[(size_t)j - 1]->s("id") : "";
            return Result::move(i);
        },
        [&](int k, long) -> Result {
            if (text::upper(key_text(k)) != "M") return Result::none();
            std::vector<std::pair<Value, const Value *>> picks;
            for (const auto &p : plan) picks.push_back({p.first, p.second.empty() ? nullptr : lesson(p.second)});
            try {
                export_done(exporting::export_day(store_, picks, md));
            } catch (exporting::ExportError &e) {
                ui.msg = "?" + e.message;
                return Result::none();
            }
            return Result::back();
        },
        "1-0 PICK A LESSON  M MAKE THE SLIDESHOW  ESC CANCEL", [&] {
            return "FOUND " + std::to_string(n) + " OF " + std::to_string(plan.size()) + " PERIODS WITH A LESSON THAT DAY. EACH PERIOD'S SLIDE, IN ORDER, IN ONE SLIDESHOW + PDF.";
        });
}

void App::curriculum_map(const std::string *course0) {
    std::vector<const Value *> lessons;
    for (const auto &l : store_.data.get("lessons").arr()) lessons.push_back(&l);
    std::vector<std::pair<std::string, int>> found;
    for (const auto &c : store::classes(st(), lessons)) if (c.second) found.push_back(c);
    if (found.empty()) {
        ui.msg = "?PLAN A FEW LESSONS FIRST. THE MAP IS BUILT FROM THEIR UNITS, DATES, AND STANDARDS.";
        return;
    }
    std::vector<std::string> names, labels;
    long cur = 0;
    for (size_t i = 0; i < found.size(); i++) {
        names.push_back(found[i].first);
        labels.push_back(text::upper(found[i].first.empty() ? "NO CLASS YET" : found[i].first) + "  (" + std::to_string(found[i].second) + " LESSON" +
                         (found[i].second != 1 ? "S" : "") + ")");
        if (course0 && *course0 == found[i].first) cur = (long)i;
    }
    labels.push_back("ALL MY CLASSES");
    long j = ui.choose("CURRICULUM MAP FOR WHICH CLASS?", labels, cur);
    if (j < 0) return;
    bool all = j >= (long)names.size();
    std::string course = all ? "" : names[(size_t)j];
    std::string subject = std::find(store_.subjects.begin(), store_.subjects.end(), field(st(), "subject")) != store_.subjects.end() ? field(st(), "subject") : "";
    std::string grades = field(st(), "grades");
    auto years = doc::school_years(lessons);
    int this_year = dates::school_year(today());
    int year = std::find(years.begin(), years.end(), this_year) != years.end() ? this_year : (years.empty() ? 0 : years[0]);
    for (;;) {
        std::vector<std::string> opts = {"PREVIEW", "SAVE: PDF + SPREADSHEET (CSV)", "SAVE: WORD + SPREADSHEET (CSV)", "SAVE: PDF + WORD + SPREADSHEET",
                                         "NOT-TAUGHT-YET LIST: " + (subject.empty() ? std::string("OFF") : text::upper(subject + " " + grade_label(grades))),
                                         "SCHOOL YEAR: " + (year ? dates::school_year_label(year) : std::string("EVERY YEAR"))};
        long k = ui.choose("CURRICULUM MAP: " + text::upper(all ? "ALL MY CLASSES" : (course.empty() ? "NO CLASS YET" : course)), opts, 0);
        if (k < 0) return;
        if (k == 0) preview(doc::curriculum_doc(store_, all ? names[0] : course, subject, grades, year));
        else if (k == 5) {
            std::vector<std::string> ys;
            for (int y : years) ys.push_back(dates::school_year_label(y));
            ys.push_back("EVERY YEAR");
            auto it = std::find(years.begin(), years.end(), year);
            long m = ui.choose("WHICH SCHOOL YEAR?", ys, it != years.end() ? (long)(it - years.begin()) : (long)years.size());
            if (m >= 0) year = m < (long)years.size() ? years[(size_t)m] : 0;
        } else if (k == 4) {
            const auto &subs = store_.subjects;
            if (subs.empty()) {
                ui.msg = "?IMPORT STANDARDS FIRST (MAIN MENU > STANDARDS LIBRARY > I).";
                continue;
            }
            std::vector<std::string> so = {"(LEAVE THE LIST OFF)"};
            for (const auto &x : subs) so.push_back(text::upper(x));
            auto it = std::find(subs.begin(), subs.end(), subject);
            long m = ui.choose("LIST WHAT ISN'T TAUGHT YET FROM", so, it != subs.end() ? (long)(it - subs.begin()) + 1 : 0);
            if (m >= 0) subject = m ? subs[(size_t)m - 1] : "";
            if (!subject.empty()) {
                std::string g;
                if (pick_grade(grades, g)) grades = g;
            }
        } else {
            std::vector<std::vector<std::string>> fm = {{"PDF", "CSV"}, {"DOCX", "CSV"}, {"PDF", "DOCX", "CSV"}};
            try {
                export_done(exporting::export_map(store_, course, all, fm[(size_t)k - 1], subject, grades, "", year));
            } catch (exporting::ExportError &e) {
                ui.msg = "?" + e.message;
            }
            return;
        }
    }
}

void App::export_now(const std::string &lesson_id, const std::string &fmt) {
    auto files = do_export("lesson", lesson_id, fmt);
    if (!files.empty()) export_done(files);
}

std::vector<std::string> App::do_export(const std::string &kind, const std::string &id, const std::string &fmt) {
    Value *o = kind == "lesson" ? lesson(id) : assessment(id);
    if (!o) return {};
    Value copy = *o;   // the export works on a copy: nothing it does can move the original
    ExportBar bar(ui, field(st(), "mascot"));
    try {
        return exporting::export_item(store_, kind, copy, fmt, [&bar](int done, int total, const std::string &name) { bar(done, total, name); });
    } catch (exporting::ExportError &e) {
        ui.msg = "?" + e.message;
        return {};
    }
}

void App::export_done(const std::vector<std::string> &files) {
    std::string folder = exporting::files_folder(files);
    ui.list_screen(
        "EXPORT COMPLETE", [&] { return (long)files.size(); }, [&](long i, int) { return hi_row(ui, relpath(files[(size_t)i], folder)); },
        [&](long i) {
            ui.msg = proc::open_path(files[(size_t)i]) ? "OPENING..." : "?COULD NOT OPEN FILE";
            return Result::none();
        },
        [&](int k, long) {
            if (text::lower(key_text(k)) == "f") ui.msg = proc::open_path(folder) ? "OPENING FOLDER..." : "?COULD NOT OPEN FOLDER";
            return Result::none();
        },
        "1-0 OPEN FILE  F OPEN FOLDER  ESC DONE",
        [&] { return "SAVED " + std::to_string(files.size()) + " FILE" + (files.size() != 1 ? "S" : "") + " TO " + folder; });
}

// ----------------------------------------------------------------------------- board slides

void App::board_settings() {
    auto choice = [&](const std::string &key) {
        for (const auto &c : store::BOARD_CHOICES)
            if (c.first == key) return text::upper(label_of(c.second, field(st(), key), c.second[0].second));
        return std::string();
    };
    struct F { std::string key, text; };
    std::vector<F> fields;
    auto refresh = [&] {
        bool adv = st().get("advanced").truthy();
        std::string style = "COLORS .............. " + label_of(store::BOARD_STYLES, field(st(), "board_style"));
        if (!adv) fields = {{"style", style}, {"sections", "SECTIONS ..."}, {"preview", "PREVIEW A SLIDE"}};
        else {
            std::string head = field(st(), "board_head_font");
            fields = {{"style", style},
                      {"font", "TEXT FONT ........... " + text::upper(fontlib::label(field(st(), "board_font")))},
                      {"head", "HEADING FONT ........ " + text::upper(head.empty() ? "SAME AS TEXT" : fontlib::label(head))},
                      {"board_layout", "COLUMNS ............. " + choice("board_layout")},
                      {"board_panels", "SECTION BOXES ....... " + choice("board_panels")},
                      {"board_title_align", "TITLE ............... " + choice("board_title_align")},
                      {"board_codes_place", "CLASS CODES ......... " + choice("board_codes_place")},
                      {"big", std::string("TEXT SIZE ........... ") + (st().get("board_big_text").truthy() ? "EXTRA BIG (MAY USE MORE SLIDES)" : "BIGGEST THAT FITS")},
                      {"sections", "SECTIONS, SIDES & ORDER..."},
                      {"preview", "PREVIEW A SLIDE"},
                      {"reset", "RESET THE BOARD DESIGN TO DEFAULT"}};
        }
        return (long)fields.size();
    };
    ui.list_screen(
        st().get("advanced").truthy() ? "BOARD DESIGNER" : "BOARD SLIDES", refresh, [&](long i, int) { return hi_row(ui, fields[(size_t)i].text); },
        [&](long i) -> Result {
            std::string key = fields[(size_t)i].key;
            if (key == "style") {
                size_t k = 0;
                for (size_t n = 0; n < store::BOARD_STYLES.size(); n++) if (store::BOARD_STYLES[n].first == field(st(), "board_style")) k = n;
                st()["board_style"] = store::BOARD_STYLES[(k + 1) % store::BOARD_STYLES.size()].first;
            } else if (key == "font" || key == "head") font_picker(key == "font" ? "board_font" : "board_head_font");
            else if (key == "big") st()["board_big_text"] = !st().get("board_big_text").truthy();
            else if (key == "reset") {
                if (!ui.confirm("PUT FONTS, COLUMNS, BOXES, AND SECTION PLACES BACK TO THE USUAL")) return Result::move(i);
                store::reset_advanced(st(), "board");
                ui.msg = "THE BOARD DESIGN IS BACK TO THE USUAL ONE.";
            } else if (key == "sections") board_sections();
            else if (key == "preview") {
                board_preview();
                return Result::move(i);
            } else
                for (const auto &c : store::BOARD_CHOICES)
                    if (c.first == key) {
                        size_t k = 0;
                        for (size_t n = 0; n < c.second.size(); n++) if (c.second[n].first == field(st(), key)) k = n;
                        st()[key] = c.second[(k + 1) % c.second.size()].first;
                    }
            save();
            return Result::move(i);
        },
        [&](int k, long) {
            if (text::upper(key_text(k)) == "P") board_preview();
            return Result::none();
        },
        "1-0 CHANGE  P PREVIEW  ESC BACK", [&] {
            return std::string("1920x1080 PNG + SLIDESHOW. EMPTY SECTIONS ARE LEFT OFF; TEXT SIZES ITSELF.") +
                   (st().get("advanced").truthy() ? "" : " FONTS + LAYOUT: SETTINGS > ADVANCED MODE.");
        });
}

void App::board_sections() {
    struct F { std::string key, text; };
    std::vector<F> fields;
    auto refresh = [&] {
        std::vector<std::string> on;
        for (const auto &k : st().get("board_sections").arr()) on.push_back(text_of(k));
        bool full = st().has("board_std_text") ? st().get("board_std_text").truthy() : true;
        fields = {{"std_text", std::string("STANDARDS SHOW ...... ") + (full ? "CODE + FULL TEXT" : "CODES ONLY")}};
        for (const auto &s : store::board_sections(st())) {
            bool is_on = std::find(on.begin(), on.end(), s.key) != on.end();
            fields.push_back({s.key, std::string("[") + (is_on ? "X" : " ") + "] " + lpad(text::upper(s.label), 19) + " (" +
                                         text::upper(label_of(store::BOARD_SIDES, s.col)) + ")"});
        }
        return (long)fields.size();
    };
    ui.list_screen(
        "BOARD SECTIONS", refresh, [&](long i, int) { return hi_row(ui, fields[(size_t)i].text); },
        [&](long i) {
            std::string key = fields[(size_t)i].key;
            if (key == "std_text") st()["board_std_text"] = !(st().has("board_std_text") ? st().get("board_std_text").truthy() : true);
            else {
                Value on = Value::array();
                size_t before = st().get("board_sections").size();
                for (const auto &k : st().get("board_sections").arr()) if (text_of(k) != key) on.push(k);
                if (on.size() == before) on.push(key);
                st()["board_sections"] = on;
            }
            save();
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::upper(key_text(k));
            if (i < 0 || fields[(size_t)i].key == "std_text") return Result::none();
            std::string key = fields[(size_t)i].key, side;
            for (const auto &s : store::board_sections(st())) if (s.key == key) side = s.col;
            if (c == "S" && (side == "left" || side == "right")) {
                if (!st().get("board_sides").is_object()) st()["board_sides"] = Value::object();
                st()["board_sides"][key] = side == "left" ? "right" : "left";
                save();
                return Result::move(i);
            }
            if ((c == "+" || c == "-" || c == "=" || c == "_") && side != "top") {
                if (store::move_board_section(st(), key, (c == "+" || c == "=") ? 1 : -1)) {
                    save();
                    refresh();
                    for (size_t n = 0; n < fields.size(); n++) if (fields[n].key == key) return Result::move((long)n);
                }
            }
            if (c == "R" && ui.confirm("PUT EVERY SECTION BACK IN ITS USUAL PLACE")) {
                st()["board_sides"] = Value::object();
                st()["board_order"] = Value::array();
                save();
            }
            return Result::none();
        },
        "RETURN ON/OFF  S SIDE  +/- MOVE  R RESET  ESC BACK", [&] {
            for (const auto &c : store::BOARD_CHOICES)
                if (c.first == "board_layout")
                    return "COLUMNS: " + text::upper(label_of(c.second, st().has("board_layout") ? field(st(), "board_layout") : "auto")) +
                           " (BOARD DESIGNER > COLUMNS)";
            return std::string();
        });
}

void App::font_picker(const std::string &key) {
    int rows, w, x0;
    ui.dims(rows, w, x0);
    ui.draw(rows - 1, x0, ui.tx("LOOKING FOR FONTS..."), ui.HI);
    ui.refresh();
    struct Fnt { std::string name, label, tag; };
    std::vector<Fnt> everything = {{"", key == "board_head_font" ? "SAME AS TEXT FONT" : text::upper(fontlib::label("")), ""}};
    for (const auto &f : fontlib::families())
        if (!(f.first == "Helvetica" && key == "board_font")) everything.push_back({f.first, f.first, ""});   // (the standard one, above)
    std::string query;
    std::vector<size_t> items;
    long start = 0;
    for (size_t i = 0; i < everything.size(); i++) if (everything[i].name == field(st(), key)) { start = (long)i; break; }
    ui.list_screen(
        "BOARD FONT",
        [&] {
            items.clear();
            std::string q = text::lower(query);
            for (size_t i = 0; i < everything.size(); i++)
                if (q.empty() || text::contains(text::lower(everything[i].label), q)) items.push_back(i);
            return (long)items.size();
        },
        [&](long i, int) {
            const Fnt &f = everything[items[(size_t)i]];
            UI::Row r;
            r.text = (f.name == field(st(), key) ? "* " : "  ") + f.label + f.tag;
            return r;
        },
        [&](long i) {
            const Fnt &f = everything[items[(size_t)i]];
            st()[key] = f.name;
            save();
            ui.msg = "FONT: " + text::upper(f.label.empty() ? "STANDARD" : f.label);
            return Result::back();
        },
        [&](int k, long) -> Result {
            if (k == term::K_BACKSPACE) {
                if (!query.empty()) query = text::slice(query, 0, (long)text::length(query) - 1);
                return Result::move(0);
            }
            std::string c = key_text(k);
            if (text::length(c) == 1 && !(c[0] >= '0' && c[0] <= '9')) {
                query += c;
                return Result::move(0);
            }
            return Result::none();
        },
        "1-0 PICK  TYPE TO FIND  ESC CANCEL",
        [&] { return query.empty() ? std::to_string(everything.size()) + " FONTS. TYPE TO FIND ONE. * = THE ONE IN USE" : "FIND: " + query + "_"; },
        "NO FONT MATCHES. BACKSPACE TO ERASE.", start);
}

void App::board_preview() {
    int rows, w, x0;
    ui.dims(rows, w, x0);
    ui.draw(rows - 1, x0, ui.tx("DRAWING A PREVIEW..."), ui.HI);
    ui.refresh();
    std::string path = files::join(store_.dir, "board-preview.png");
    int n;
    try {
        n = board::render_preview(doc::preview_doc(store_), store_.logo(), path);
    } catch (board::BoardError &e) {
        ui.msg = "?" + text::upper(e.message);
        return;
    }
    proc::open_path(path);
    ui.msg = "PREVIEW OPENED" + (n > 1 ? " (SLIDE 1 OF " + std::to_string(n) + ")" : std::string()) + ".";
}

// ----------------------------------------------------------------------------- settings

void App::logo_settings() {
    bool has = store_.logo() != nullptr;
    std::string place = st().has("logo_place") ? field(st(), "logo_place") : "left";
    std::vector<std::string> opts = {"CHOOSE A PNG OR JPEG PICTURE"};
    if (has) {
        opts.push_back("PUT IT " + label_of(store::LOGO_PLACES, place == "left" ? "right" : "left"));
        opts.push_back("REMOVE THE LOGO");
    }
    long j = ui.choose("SCHOOL LOGO (CORNER OF EVERY BOARD SLIDE)", opts);
    if (j == 0) {
        std::string path;
        if (!ui.prompt("PATH TO THE LOGO, A .PNG OR .JPG (DRAG IT HERE)", path, "", false, true) || path.empty()) return;
        int rows, w, x0;
        ui.dims(rows, w, x0);
        ui.draw(rows - 1, x0, ui.tx("READING THE PICTURE..."), ui.HI);
        ui.refresh();
        try {
            const images::Image &img = store_.set_logo(path);
            ui.msg = "LOGO SET (" + std::to_string(img.w) + "x" + std::to_string(img.h) + "). IT SHOWS " + label_of(store::LOGO_PLACES, place) + ".";
        } catch (images::ImageError &e) {
            ui.msg = "?" + text::upper(e.message);
        }
    } else if (j == 1) st()["logo_place"] = place == "left" ? "right" : "left";
    else if (j == 2 && ui.confirm("TAKE THE LOGO OFF YOUR BOARD SLIDES")) store_.remove_logo();
}

void App::class_periods() {
    auto periods = [&]() -> Value & {
        if (!st().get("class_periods").is_array()) st()["class_periods"] = Value::array();
        return st()["class_periods"];
    };
    ui.list_screen(
        "CLASS PERIODS & CODES", [&] { return (long)periods().size(); },
        [&](long i, int) {
            const Value &p = periods().at((size_t)i);
            std::vector<std::string> codes;
            for (const auto &x : text::split(field(p, "codes"), "\n")) if (!text::strip(x).empty()) codes.push_back(text::strip(x));
            std::string course = field(p, "course");
            return hi_row(ui, lpad(field(p, "name"), 14) + " " + lpad(course.empty() ? "ANY COURSE" : course, 14) + " " +
                                  (codes.empty() ? "(NO CODES)" : text::join(codes, " / ")));
        },
        [&](long i) {
            period_editor((size_t)i);
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::lower(key_text(k));
            if (c == "n") {
                Value p = Value::object();
                p["name"] = "Period " + std::to_string(periods().size() + 1);
                p["course"] = "";
                p["codes"] = "";
                periods().push(p);
                bool has = false;
                for (const auto &x : st().get("board_sections").arr()) if (text_of(x) == "class_codes") has = true;
                if (!has) st().setdefault("board_sections", Value::array()).push("class_codes");
                period_editor(periods().size() - 1);
                save();
                return Result::move((long)periods().size() - 1);
            }
            if (i < 0 || i >= (long)periods().size()) return Result::none();
            std::string name = field(periods().at((size_t)i), "name");
            if (c == "d" && ui.confirm("REMOVE " + (name.empty() ? std::string("THIS PERIOD") : name))) {
                periods().arr_ref().erase(periods().arr_ref().begin() + i);
                save();
                return Result::move(std::max(0L, i - 1));
            }
            if (c == "+" || c == "-") {
                long j = i + (c == "+" ? 1 : -1);
                if (j >= 0 && j < (long)periods().size()) {
                    std::swap(periods().at((size_t)i), periods().at((size_t)j));
                    save();
                    return Result::move(j);
                }
            }
            return Result::none();
        },
        "1-0 EDIT  N NEW  D REMOVE  +/- MOVE  ESC BACK", [] { return std::string("ONE BOARD SLIDE PER PERIOD, EACH WITH ITS OWN CODES"); },
        "NO PERIODS YET. N ADDS ONE (E.G. PERIOD 1, WITH ITS GOOGLE CLASSROOM CODE).");
}

void App::period_editor(size_t i) {
    struct F { std::string key, label, kind; };
    std::vector<F> fields = {{"name", "NAME", "line"}, {"course", "COURSE (BLANK = EVERY LESSON)", "line"}, {"codes", "CODES", "text"}};
    auto p = [&]() -> Value * {
        Value &list = st()["class_periods"];
        return list.is_array() && i < list.size() ? &list.at(i) : nullptr;
    };
    if (!p()) return;
    ui.list_screen(
        "CLASS PERIOD: " + field(*p(), "name"), [&] { return (long)fields.size(); },
        [&](long n, int) {
            const F &f = fields[(size_t)n];
            std::string v = p() ? field(*p(), f.key) : "";
            if (f.kind == "text") {
                std::vector<std::string> parts;
                for (const auto &x : text::split(v, "\n")) if (!text::strip(x).empty()) parts.push_back(text::strip(x));
                v = text::join(parts, " / ");
            }
            return hi_row(ui, lpad(f.label, 30) + " " + (v.empty() ? "-" : v));
        },
        [&](long n) {
            const F &f = fields[(size_t)n];
            if (!p()) return Result::back();
            std::string v;
            bool got;
            if (f.kind == "text") {
                std::string name = field(*p(), "name");
                v = field(*p(), f.key);
                got = ui.edit_text("CODES FOR " + (name.empty() ? "THIS PERIOD" : name), v, "ONE PER LINE, NAME: CODE, E.G. GOOGLE CLASSROOM: ABC123. ESC SAVES.");
            } else got = ui.prompt(f.label, v, field(*p(), f.key));
            if (got && p()) {
                (*p())[f.key] = text::strip(v);
                save();
            }
            return Result::move(n);
        },
        nullptr, "1-0 CHANGE  ESC BACK", [] { return std::string("A COURSE MAKES THIS SLIDE ONLY FOR THAT COURSE'S LESSONS"); });
}

void App::backup_now() {
    std::string folder;
    if (!ui.prompt("SAVE A BACKUP IN WHICH FOLDER (DRAG ONE HERE)", folder, store_.backup_dir(), false, true) || folder.empty()) return;
    save();
    try {
        ui.msg = "BACKED UP TO " + store_.backup(folder);
    } catch (store::UserError &e) {
        ui.msg = "?COULD NOT BACK UP: " + e.message;
    }
}

void App::export_everything() {
    std::string parent;
    if (!ui.prompt("PUT THE NEW FOLDER WHERE (DRAG A FOLDER HERE)", parent, store_.export_dir(), false, true) || parent.empty()) return;
    save();
    ExportBar bar(ui, field(st(), "mascot"));
    exporting::Everything got;
    try {
        got = exporting::export_everything(store_, parent, [&bar](int d, int t, const std::string &n) { bar(d, t, n); });
    } catch (exporting::ExportError &e) {
        ui.msg = "?" + e.message;
        return;
    }
    ui.msg = "EXPORTED " + std::to_string(got.files.size()) + " FILES TO " + got.folder +
             (got.problems.empty() ? "" : "  (" + std::to_string(got.problems.size()) + " DIDN'T: " + got.problems[0] + ")");
    if (ui.confirm("OPEN THE FOLDER NOW")) proc::open_path(got.folder);
}

void App::import_backup() {
    std::string path;
    if (!ui.prompt("PATH TO A CHALKBOARD BACKUP .JSON FILE (DRAG IT HERE)", path, "", false, true) || path.empty()) return;
    long j = ui.choose("IMPORT HOW", {"ADD WHAT I DON'T HAVE (KEEPS MY WORK)", "REPLACE EVERYTHING WITH THE BACKUP"});
    if (j < 0 || (j == 1 && !ui.confirm("REPLACE ALL YOUR LESSONS, ASSESSMENTS, AND SETTINGS"))) return;
    save();
    Value got;
    try {
        got = store_.import_backup(path, j == 1);
    } catch (store::UserError &e) {
        ui.msg = "?" + text::upper(e.message);
        return;
    }
    std_subject_.clear();
    ui.apply_theme();
    auto n = [&](const char *k) { return std::to_string(got.get(k).as_int()); };
    if (j == 1) ui.msg = "REPLACED WITH THE BACKUP: " + n("lessons") + " LESSONS, " + n("assessments") + " ASSESSMENTS.";
    else
        ui.msg = "ADDED " + n("lessons") + " LESSONS, " + n("assessments") + " ASSESSMENTS, " + n("subjects") + " SUBJECTS, " + n("standards") +
                 " CUSTOM STANDARDS; " + n("updated") + " UPDATED.";
    ui.msg += " YOUR OLD WORK IS SAVED IN " + got.s("safety");
}

void App::pick_mascot() {
    std::vector<std::string> keys = {""};
    for (const auto &m : art::mascots()) keys.push_back(m.first);
    size_t i = 0;
    for (size_t n = 0; n < keys.size(); n++) if (!keys[n].empty() && keys[n] == field(st(), "mascot")) i = n;
    for (;;) {
        int rows, w, x0;
        ui.dims(rows, w, x0);
        std::string key = keys[i];
        ui.begin();
        ui.header("SCHOOL MASCOT");
        ui.line(2, "SHOWS UP IN THE BOOT SEQUENCE AND RUNS ALONG THE EXPORT PROGRESS BAR", &ui.DIM);
        int y = 4, mx = x0 + 5;
        ui.put_sprite(y, mx, art::sprite(key));
        ui.put(y + 3, mx - 4, "<", &ui.HI);
        ui.put(y + 3, mx + 19, ">", &ui.HI);
        ui.line(y + 10, key.empty() ? "NONE (THE CHALKBOARD LOGO RUNS THE EXPORT BAR)" : "GO " + art::name(key) + "!", &ui.HI);
        ui.line(y + 12, std::to_string(i + 1) + " OF " + std::to_string(keys.size()), &ui.DIM);
        ui.footer("LEFT/RIGHT BROWSE   ENTER PICK   ESC CANCEL");
        ui.show();
        int k = ui.key();
        if (k == term::K_CLOSED) return;
        if (k == term::K_LEFT) i = (i + keys.size() - 1) % keys.size();
        else if (k == term::K_RIGHT || k == ' ') i = (i + 1) % keys.size();
        else if (k == term::K_ENTER) {
            st()["mascot"] = key;
            ui.msg = key.empty() ? "NO MASCOT" : "MASCOT SET: " + art::name(key);
            return;
        } else if (k == term::K_ESC || k == term::K_BACKSPACE) return;
    }
}

void App::page_layouts() {
    const auto &kinds = store::page_layouts();
    ui.list_screen(
        "PAGE LAYOUTS", [&] { return (long)kinds.size(); },
        [&](long i, int) {
            const Value &pl = st().get("page_layouts");
            bool changed = pl.is_object() && pl.get(kinds[(size_t)i].kind).truthy();
            return hi_row(ui, text::upper(kinds[(size_t)i].name) + (changed ? " (CHANGED)" : "") + " ...");
        },
        [&](long i) {
            page_layout(kinds[(size_t)i].kind);
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            if (text::upper(key_text(k)) == "R" && !store::advanced_changes(st()).empty() &&
                ui.confirm("PUT EVERY PAGE LAYOUT AND THE BOARD DESIGN BACK TO THE USUAL")) {
                store::reset_advanced(st());
                save();
                ui.msg = "EVERYTHING IS BACK TO THE USUAL LAYOUT.";
                return Result::move(i);
            }
            return Result::none();
        },
        "1-0 CHANGE  R RESET EVERYTHING  ESC BACK", [] { return std::string("NAME LINES, TITLES, HEADINGS, AND FOOTERS FOR EACH KIND OF PAGE."); });
}

void App::page_layout(const std::string &kind) {
    const store::PageKind *pk = store::page_kind(kind);
    if (!pk) return;
    auto choice_label = [](const std::string &type, const std::string &v) {
        for (const auto &c : store::LAYOUT_CHOICES)
            if (c.first == type) return text::upper(label_of(c.second, v, v));
        return v;
    };
    auto is_choice = [](const std::string &type) {
        for (const auto &c : store::LAYOUT_CHOICES) if (c.first == type) return true;
        return false;
    };
    auto change = [&](const std::string &key, const Value &v) {
        Value values = store::page_layout(st(), kind);
        values[key] = v;
        store::set_page_layout(st(), kind, values);
        save();
    };
    ui.list_screen(
        "PAGE LAYOUT: " + text::upper(pk->name), [&] { return (long)pk->options.size(); },
        [&](long i, int) {
            const auto &o = pk->options[(size_t)i];
            Value lay = store::page_layout(st(), kind);
            const Value &v = lay.get(o.key);
            std::string s;
            if (o.type == "bool") s = v.truthy() ? "YES" : "NO";
            else if (is_choice(o.type)) s = choice_label(o.type, v.str());
            else if (o.type == "num") s = v.truthy() ? text_of(v) : "NONE";
            else if (o.type == "text") {
                s = first_line(v.str());
                if (s.empty()) s = "(THE USUAL)";
            } else {
                s = v.str();
                if (s.empty()) s = o.type == "fields" ? "(NONE)" : o.key == "footer_text" ? "(THE USUAL)" : "(NONE)";
            }
            return hi_row(ui, lpad(text::slice(text::upper(o.label), 0, 44), 45) + " " + s);
        },
        [&](long i) {
            const auto &o = pk->options[(size_t)i];
            Value cur = store::page_layout(st(), kind).get(o.key);
            std::string v;
            if (o.type == "bool") change(o.key, !cur.truthy());
            else if (is_choice(o.type)) {
                for (const auto &c : store::LAYOUT_CHOICES)
                    if (c.first == o.type) {
                        size_t k = 0;
                        for (size_t n = 0; n < c.second.size(); n++) if (c.second[n].first == cur.str()) k = n;
                        change(o.key, c.second[(k + 1) % c.second.size()].first);
                    }
            } else if (o.type == "num") {
                if (ui.prompt(text::upper(o.label) + " (0-12)", v, text_of(cur)) && !text::strip(v).empty()) {
                    std::string t = text::strip(v);
                    bool digits = std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
                    if (digits && t.size() < 9) change(o.key, std::min(12, std::atoi(t.c_str())));
                }
            } else if (o.type == "text") {
                v = cur.str();
                if (ui.edit_text(text::upper(o.label), v, "BLANK KEEPS THE USUAL NOTE. ESC SAVES.")) change(o.key, text::strip(v));
            } else {
                std::string hint = o.type == "fields" ? " (COMMAS BETWEEN, BLANK = NONE)" : "";
                if (ui.prompt(text::upper(o.label) + hint, v, cur.str())) change(o.key, text::strip(v));
            }
            return Result::move(i);
        },
        [&](int k, long i) -> Result {
            std::string c = text::upper(key_text(k));
            if (c == "P") preview(doc::layout_sample(store_, kind, st()));
            else if (c == "R" && ui.confirm("PUT THE " + text::upper(pk->name) + " LAYOUT BACK TO THE USUAL")) {
                store::reset_advanced(st(), kind);
                save();
                ui.msg = text::upper(pk->name) + ": BACK TO THE USUAL LAYOUT.";
                return Result::move(i);
            }
            return Result::none();
        },
        "1-0 CHANGE  P PREVIEW  R RESET TO DEFAULT  ESC BACK", [] { return std::string("EVERY EXPORT (PDF, WORD, TEXT) FOLLOWS THIS LAYOUT."); });
}

void App::settings_screen() {
    struct F { std::string key, label, kind; };
    std::vector<F> everything = {
        {"teacher", "TEACHER NAME (FOR FOOTERS)", "line"},
        {"school", "SCHOOL", "line"},
        {"course", "DEFAULT COURSE", "line"},
        {"subject", "DEFAULT SUBJECT (STANDARDS)", "subject"},
        {"grades", "DEFAULT GRADE BAND", "grades"},
        {"default_materials", "DEFAULT MATERIALS NEEDED", "text"},
        {"lesson_hide", "LESSON SECTIONS", "sections"},
        {"font", "DOCUMENT FONT", "font"},
        {"page", "PAPER SIZE", "page"},
        {"export_dir", "EXPORT FOLDER", "folder"},
        {"board_style", "BOARD SLIDE DESIGNER (VIEWBOARD)", "board"},
        {"logo", "SCHOOL LOGO (BOARD SLIDES)", "logo"},
        {"class_periods", "CLASS PERIODS & CODES", "periods"},
        {"primary_color", "SCHOOL COLOR 1 (BACKGROUND)", "color"},
        {"secondary_color", "SCHOOL COLOR 2 (HEADINGS)", "color"},
        {"text_color", "SCHOOL COLOR 3 (TEXT)", "color"},
        {"theme", "SCREEN COLOR", "theme"},
        {"uppercase", "ALL-CAPS MENUS", "bool"},
        {"boot", "BOOT SEQUENCE", "bool"},
        {"mascot", "SCHOOL MASCOT (BOOT & EXPORT)", "mascot"},
        {"backup_dir", "BACK UP EVERYTHING NOW", "backup"},
        {"", "EXPORT EVERYTHING (PDF + WORD)", "everything"},
        {"", "IMPORT A BACKUP", "restore"},
        {"advanced", "ADVANCED MODE", "advanced"},
        {"page_layouts", "  PAGE LAYOUTS", "layouts"},
    };
    std::vector<F> fields;
    auto refresh = [&] {
        fields.clear();
        for (const auto &f : everything)
            if (f.kind != "layouts" || st().get("advanced").truthy()) fields.push_back(f);
        return (long)fields.size();
    };
    ui.list_screen(
        "SETTINGS", refresh,
        [&](long i, int) {
            const F &f = fields[(size_t)i];
            const Value &raw = st().get(f.key);
            std::string v = raw.truthy() ? text_of(raw) : "";
            if (f.kind == "bool") v = raw.truthy() ? "ON" : "OFF";
            else if (f.kind == "folder") v = store_.export_dir();
            else if (f.kind == "logo") {
                const images::Image *img = store_.logo();
                v = img ? (img->name.empty() ? "LOGO" : img->name) + " (" + std::to_string(img->w) + "x" + std::to_string(img->h) + "), " +
                              label_of(store::LOGO_PLACES, st().has("logo_place") ? field(st(), "logo_place") : "left")
                        : "NONE";
            } else if (f.kind == "mascot") v = art::has_mascot(raw.str()) ? art::name(raw.str()) + " ..." : "NONE ...";
            else if (f.kind == "periods") {
                long n = 0;
                for (const auto &p : st().get("class_periods").arr()) if (!text::strip(field(p, "codes")).empty()) n++;
                v = n ? std::to_string(n) + " WITH CODES: ONE BOARD SLIDE EACH ..." : "NONE ...";
            } else if (f.kind == "backup")
                v = store_.backup_dir() + (st().get("last_backup").truthy() ? "  (LAST: " + field(st(), "last_backup") + ")" : "");
            else if (f.kind == "restore") v = "FROM A BACKUP FILE OR ANOTHER COMPUTER'S DATA.JSON";
            else if (f.kind == "everything") v = "A FOLDER TO KEEP OR DRAG INTO GOOGLE DRIVE ...";
            else if (f.kind == "board") v = label_of(store::BOARD_STYLES, raw.str()) + " ...";
            else if (f.kind == "advanced") {
                auto changed = store::advanced_changes(st());
                v = std::string(raw.truthy() ? "ON" : "OFF: NAME LINES, HEADINGS, FONTS ...") +
                    (!changed.empty() && !raw.truthy() ? "  (" + std::to_string(changed.size()) + " CHANGED, STILL USED)" : "");
            } else if (f.kind == "layouts") {
                size_t n = st().get("page_layouts").size();
                v = n ? std::to_string(n) + " CHANGED ..." : "ALL THE USUAL ...";
            } else if (f.kind == "sections") {
                long n = 0;
                for (const auto &k : raw.arr())
                    if (std::find(store::FIXED_FIELDS.begin(), store::FIXED_FIELDS.end(), text_of(k)) == store::FIXED_FIELDS.end()) n++;
                v = n ? std::to_string(n) + " HIDDEN ..." : "ALL SHOWN ...";
            } else if (f.kind == "grades") v = grade_label(raw.truthy() ? raw.str() : store::ALL);
            else if (f.kind == "text") {
                long extra = -1;
                for (const auto &l : text::split(raw.str(), "\n")) if (!text::strip(l).empty()) extra++;
                v = first_line(raw.str()) + (extra > 0 ? "  (+" + std::to_string(extra) + " more)" : "");
            }
            return hi_row(ui, lpad(f.label, 30) + " " + (v.empty() ? "-" : v));
        },
        [&](long i) -> Result {
            const F f = fields[(size_t)i];
            std::string v;
            if (f.kind == "color") {
                if (ui.prompt(f.label + " HEX, E.G. #7A0019 (BLANK CLEARS)", v, field(st(), f.key))) {
                    std::string code = v.empty() ? "" : store::parse_hex(v);
                    if (!v.empty() && code.empty()) ui.msg = "?'" + v + "' IS NOT A HEX COLOR (E.G. #7A0019)";
                    else {
                        st()[f.key] = code;
                        if (!code.empty()) {
                            st()["board_style"] = "school";
                            ui.msg = "BOARD SLIDES NOW USE SCHOOL COLORS";
                            auto weak = board::low_contrast(st());
                            if (!weak.empty()) ui.msg = "NOTE: " + text::join(weak, " + ") + " MAY BE HARD TO READ ON THE BACKGROUND";
                        } else if (!st().get("primary_color").truthy() && !st().get("secondary_color").truthy() && !st().get("text_color").truthy() &&
                                   field(st(), "board_style") == "school")
                            st()["board_style"] = "chalk";
                    }
                }
            } else if (f.kind == "line" || f.kind == "folder") {
                if (ui.prompt(f.label, v, f.kind == "folder" ? store_.export_dir() : field(st(), f.key))) st()[f.key] = v;
            } else if (f.kind == "text") {
                v = field(st(), f.key);
                if (ui.edit_text(f.label, v, "PRE-FILLS MATERIALS & TEXTS ON EVERY NEW LESSON. '- ' STARTS A BULLET. ESC SAVES.")) st()[f.key] = v;
            } else if (f.kind == "grades") {
                if (pick_grade(field(st(), f.key), v)) st()[f.key] = v;
            } else if (f.kind == "subject") {
                const auto &opts = store_.subjects;
                if (opts.empty()) {
                    ui.msg = "?IMPORT STANDARDS FIRST (MAIN MENU > STANDARDS LIBRARY > I).";
                    return Result::move(i);
                }
                std::vector<std::string> labels;
                for (const auto &o : opts) labels.push_back(ui.tx(o));
                auto it = std::find(opts.begin(), opts.end(), field(st(), f.key));
                long j = ui.choose(f.label, labels, it == opts.end() ? -1 : (long)(it - opts.begin()));
                if (j >= 0) {
                    st()[f.key] = opts[(size_t)j];
                    std_subject_.clear();
                }
            } else if (f.kind == "font") st()[f.key] = field(st(), f.key) == "Times" ? "Helvetica" : "Times";
            else if (f.kind == "page") st()[f.key] = field(st(), f.key) == "Letter" ? "A4" : "Letter";
            else if (f.kind == "theme") {
                std::vector<std::string> order = {"green", "amber", "white"};
                auto it = std::find(order.begin(), order.end(), st().has(f.key) ? field(st(), f.key) : "green");
                st()[f.key] = order[(size_t)(((it == order.end() ? 0 : it - order.begin()) + 1) % 3)];
                ui.apply_theme();
            } else if (f.kind == "bool") st()[f.key] = !st().get(f.key).truthy();
            else if (f.kind == "advanced") {
                bool on = !st().get(f.key).truthy();
                st()[f.key] = on;
                ui.msg = on ? "ADVANCED MODE ON: PAGE LAYOUTS + MORE IN THE BOARD SLIDE DESIGNER" : "ADVANCED MODE OFF. ANYTHING YOU CHANGED STILL APPLIES.";
            } else if (f.kind == "layouts") page_layouts();
            else if (f.kind == "board") board_settings();
            else if (f.kind == "sections") lesson_sections();
            else if (f.kind == "logo") logo_settings();
            else if (f.kind == "periods") class_periods();
            else if (f.kind == "mascot") pick_mascot();
            else if (f.kind == "backup") {
                backup_now();
                return Result::move(i);
            } else if (f.kind == "restore") {
                import_backup();
                return Result::move(i);
            } else if (f.kind == "everything") {
                export_everything();
                return Result::move(i);
            }
            save();
            return Result::move(i);
        },
        nullptr, "1-0 CHANGE  ESC BACK", [&] { return "DATA FILE: " + store_.path; });
}

}  // namespace tui
