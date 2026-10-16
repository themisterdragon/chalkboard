#include "exporting.h"

#include <algorithm>
#include <regex>

#include "board.h"
#include "doc.h"
#include "export_docx.h"
#include "export_txt.h"
#include "files.h"
#include "pdf.h"
#include "text.h"

namespace exporting {

using json::Value;
using store::field;
using store::text_of;

const std::vector<std::string> LESSON_FORMATS = {"PDF", "DOCX", "TXT", "PNG", "MAKEUP", "ALL"};
const std::vector<std::string> ASSESSMENT_FORMATS = {"PDF", "DOCX", "TXT", "ALL"};
const std::vector<std::pair<std::string, std::string>> MAP_FORMATS = {{"PDF", "PDF"}, {"DOCX", "Word"}, {"CSV", "Spreadsheet (CSV)"}};
static const char *KEYS_FOLDER = "Answer Keys";

std::string safe_name(const std::string &s) {
    std::u32string out;
    for (char32_t c : text::to32(s)) {
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') continue;
        out += c;
    }
    std::string t = text::strip_chars(text::to8(out), " .");
    t = text::slice(t, 0, 80);
    return t.empty() ? "Untitled" : t;
}

std::vector<std::string> export_subdirs(const std::string &kind, const Value &obj) {
    std::string unit = text::strip(field(obj, "unit"));
    static const std::regex NUM("[0-9]+[A-Za-z]?");
    if (std::regex_match(unit, NUM)) unit = "Unit " + unit;
    std::vector<std::string> parts = {text::strip(field(obj, "course")), unit};
    if (kind == "lesson") {
        std::string t = field(obj, "title");
        parts.push_back(t.empty() ? "Untitled" : t);
    }
    std::vector<std::string> out;
    for (const auto &x : parts) if (!x.empty()) out.push_back(safe_name(x));
    return out;
}

std::string export_folder(store::Store &st, const std::string &kind, const Value &obj, const std::string &root) {
    std::string p = root.empty() ? st.export_dir() : root;
    for (const auto &d : export_subdirs(kind, obj)) p = files::join(p, d);
    return p;
}

std::string subfolder(const std::string &name, const std::string &fmt) {
    if (fmt == "PNG") return "Board Slides";
    if (fmt == "DOCX") return "Word";
    if (fmt == "TXT") return "Text";
    return fmt == "PDF" && text::ends_with(name, " - Answer Key") ? KEYS_FOLDER : "";
}

// Before subfolders, every file went straight in the item's folder: remove the old copy of each
// file just rewritten in a subfolder (and an older board's extra "<name> 2.png" slides).
static void tidy_old(const std::string &folder, const std::string &sub, const std::string &name, const std::vector<std::string> &wrote) {
    if (sub.empty()) return;
    std::vector<std::string> old;
    for (const auto &p : wrote) old.push_back(files::join(folder, files::basename(p)));
    if (!wrote.empty() && text::ends_with(wrote[0], ".png"))
        for (const auto &f : files::list(folder)) {
            if (!text::starts_with(f, name + " ") || !text::ends_with(f, ".png")) continue;
            std::string mid = f.substr(name.size() + 1, f.size() - name.size() - 5);
            if (!mid.empty() && std::all_of(mid.begin(), mid.end(), [](char c) { return c >= '0' && c <= '9'; })) old.push_back(files::join(folder, f));
        }
    for (const auto &p : old)
        if (files::is_file(p)) files::remove(p);
}

std::string files_folder(const std::vector<std::string> &fs) {
    if (fs.empty()) return "";
    std::vector<std::string> dirs;
    for (const auto &f : fs) dirs.push_back(files::dirname(f));
    // the longest folder every file is inside (whole folder names only)
    std::string common = dirs[0];
    for (const auto &d : dirs) {
        while (!common.empty() && !(d == common || text::starts_with(d, common + files::sep()) || text::starts_with(d, common + "/"))) {
            std::string up = files::dirname(common);
            if (up == common) { common.clear(); break; }
            common = up;
        }
    }
    return common.empty() ? dirs[0] : common;
}

std::vector<std::pair<std::string, Value>> assessment_docs(store::Store &st, const Value &a) {
    const Value &s = st.settings();
    std::string base = safe_name(field(a, "title"));
    std::string kind = a.s("kind");
    if (std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), kind) != store::SHEET_KINDS.end()) return {{base, doc::assessment_doc(a, st)}};
    std::vector<std::pair<std::string, Value>> docs;
    int versions = (int)s.get("export_versions").as_int(1);
    std::string include = s.s("export_include");
    std::vector<bool> keys = include == "STUDENT" ? std::vector<bool>{false} : include == "KEY" ? std::vector<bool>{true} : std::vector<bool>{false, true};
    for (int v = 0; v < versions; v++)
        for (bool key : keys) {
            std::string name = base + (versions > 1 ? std::string(" - Version ") + "ABCDEFGHIJ"[std::min(v, 9)] : "") + (key ? " - Answer Key" : "");
            docs.push_back({name, doc::assessment_doc(a, st, v, versions, key)});
        }
    return docs;
}

static std::string ext_of(const std::string &fmt) {
    if (fmt == "PDF") return ".pdf";
    if (fmt == "DOCX") return ".docx";
    if (fmt == "TXT") return ".txt";
    return ".png";
}

// writes one file; returns every file written (a board makes PNGs and a .pptx)
static std::vector<std::string> render(store::Store &st, const std::string &fmt, const Value &d, const std::string &path) {
    const Value &s = st.settings();
    std::string err;
    if (fmt == "PDF") {
        if (!pdf::render_pdf(d, path, s.s("font"), s.s("page"), &err)) throw ExportError{"EXPORT FAILED: " + err};
    } else if (fmt == "DOCX") {
        if (!export_docx::render_docx(d, path, s.s("font"), s.s("page"), &err)) throw ExportError{"EXPORT FAILED: " + err};
    } else if (fmt == "TXT") {
        if (!export_txt::render_txt(d, path, &err)) throw ExportError{"EXPORT FAILED: " + err};
    } else {
        try {
            return board::render_png(d, st.logo(), path);
        } catch (board::BoardError &e) {
            throw ExportError{e.message};
        }
    }
    return {path};
}

std::vector<std::string> export_item(store::Store &st, const std::string &kind, const Value &obj, const std::string &fmt, const Progress &progress,
                                     const std::string &root) {
    const Value &s = st.settings();
    std::string folder = export_folder(st, kind, obj, root), err;
    if (!files::make_dirs(folder, &err)) throw ExportError{"CANNOT CREATE FOLDER: " + err};
    std::string base = safe_name(field(obj, "title"));
    std::vector<std::string> fmts;
    if (fmt == "ALL") {
        for (const auto &f : kind == "lesson" ? LESSON_FORMATS : ASSESSMENT_FORMATS) if (f != "ALL") fmts.push_back(f);
    } else fmts.push_back(fmt);
    struct Job { std::string name; Value d; std::vector<std::string> only; bool has_only; };
    std::vector<Job> docs;
    auto take = [&](const std::string &f) {
        auto it = std::find(fmts.begin(), fmts.end(), f);
        if (it == fmts.end()) return false;
        fmts.erase(it);
        return true;
    };
    if (kind == "lesson") {
        if (take("MAKEUP")) docs.push_back({base + " - Make-Up Sheet", doc::makeup_doc(obj, st), {"PDF", "DOCX"}, true});
        if (!fmts.empty() && fmts != std::vector<std::string>{"PNG"}) docs.push_back({base + " - Lesson Plan", doc::lesson_doc(obj, st), {}, false});
        if (take("PNG")) {
            std::vector<Value> periods;
            bool codes = false;
            for (const auto &k : s.get("board_sections").arr()) if (k.is_string() && k.str() == "class_codes") codes = true;
            if (codes) periods = store::periods_for(s, obj);
            if (!periods.empty()) {   // one slide per class period, each with its own codes
                for (size_t i = 0; i < periods.size(); i++) {
                    std::string pname = field(periods[i], "name");
                    if (pname.empty()) pname = "Class " + std::to_string(i + 1);
                    docs.push_back({base + " - Board (" + safe_name(pname) + ")", doc::board_doc(obj, st, &periods[i]), {"PNG"}, true});
                }
            } else docs.push_back({base + " - Board", doc::board_doc(obj, st), {"PNG"}, true});
        }
        if (fmt == "ALL")
            for (const Value *a : st.attached(obj))
                for (auto &nd : assessment_docs(st, *a)) docs.push_back({nd.first, nd.second, {"PDF", "DOCX"}, true});
    } else
        for (auto &nd : assessment_docs(st, obj)) docs.push_back({nd.first, nd.second, {}, false});
    struct Out { std::string name; const Value *d; std::string f; };
    std::vector<Out> jobs;
    for (const auto &j : docs)
        for (const auto &f : j.has_only ? j.only : fmts) jobs.push_back({j.name, &j.d, f});
    std::vector<std::string> written;
    for (size_t i = 0; i < jobs.size(); i++) {
        const Out &j = jobs[i];
        if (progress) progress((int)i, (int)jobs.size(), j.name + ext_of(j.f));
        std::string sub = subfolder(j.name, j.f);
        std::string dir = sub.empty() ? folder : files::join(folder, sub);
        if (!files::make_dirs(dir, &err)) throw ExportError{"EXPORT FAILED: " + err};
        auto wrote = render(st, j.f, *j.d, files::join(dir, j.name + ext_of(j.f)));
        tidy_old(folder, sub, j.name, wrote);
        written.insert(written.end(), wrote.begin(), wrote.end());
    }
    if (progress) progress((int)jobs.size(), (int)jobs.size(), "");
    return written;
}

std::vector<std::string> export_day(store::Store &st, const std::vector<std::pair<Value, const Value *>> &picks, const Date &day, const std::string &root) {
    std::vector<Value> docs;
    for (const auto &p : picks)
        if (p.second) docs.push_back(doc::board_doc(*p.second, st, &p.first));
    if (docs.empty()) throw ExportError{"PICK A LESSON FOR AT LEAST ONE CLASS PERIOD"};
    std::string folder = files::join(root.empty() ? st.export_dir() : root, "Day Slideshows"), err;
    if (!files::make_dirs(folder, &err)) throw ExportError{"EXPORT FAILED: " + err};
    try {
        // 2026-10-04 sorts by date and never collides with next year's Oct 4
        return board::render_day(docs, st.logo(), files::join(folder, day.iso() + " - Day Slideshow"), dates::fmt_date(day, true) + " - Day Slideshow");
    } catch (board::BoardError &e) {
        throw ExportError{e.message};
    }
}

std::string csv_text(const std::vector<std::vector<std::string>> &rows) {
    std::string out;
    for (const auto &r : rows) {
        for (size_t i = 0; i < r.size(); i++) {
            const std::string &f = r[i];
            bool quote = f.find_first_of(",\"\r\n") != std::string::npos || (r.size() == 1 && f.empty());
            if (i) out += ',';
            out += quote ? "\"" + text::replace(f, "\"", "\"\"") + "\"" : f;
        }
        out += "\r\n";
    }
    return out;
}

std::vector<std::string> export_map(store::Store &st, const std::string &course, bool all_classes, const std::vector<std::string> &fmts,
                                    const std::string &subject, const std::string &grades, const std::string &root, int year) {
    const Value &s = st.settings();
    Value d;
    std::vector<std::vector<std::string>> rows;
    std::string folder, base_name;
    if (all_classes) {   // every class, each starting on a new page
        std::vector<const Value *> lessons;
        for (const auto &l : st.data.get("lessons").arr()) lessons.push_back(&l);
        std::vector<std::string> names;
        for (const auto &c : store::classes(s, lessons))
            if (c.second && !doc::curriculum_units(st, c.first, year).empty()) names.push_back(c.first);
        if (names.empty()) throw ExportError{"NO LESSONS ARE PLANNED YET"};
        Value blocks = Value::array();
        for (size_t i = 0; i < names.size(); i++) {
            if (i) {
                Value pb = Value::object();
                pb["t"] = "pagebreak";
                blocks.push(pb);
            }
            Value one = doc::curriculum_doc(st, names[i], subject, grades, year);
            for (const auto &b : one.get("blocks").arr()) blocks.push(b);
        }
        d = Value::object();
        d["title"] = "Curriculum Map";
        std::vector<std::string> foot;
        for (const auto &x : {field(s, "teacher"), std::string("Curriculum Map")}) if (!x.empty()) foot.push_back(x);
        d["footer"] = text::join(foot, " - ");
        d["blocks"] = blocks;
        rows = doc::curriculum_rows(st, names[0], year);
        for (size_t i = 1; i < names.size(); i++) {
            auto more = doc::curriculum_rows(st, names[i], year);
            rows.insert(rows.end(), more.begin() + 1, more.end());
        }
        folder = root.empty() ? st.export_dir() : root;
        base_name = "Curriculum Map - All Classes";
    } else {
        d = doc::curriculum_doc(st, course, subject, grades, year);
        rows = doc::curriculum_rows(st, course, year);
        folder = root.empty() ? st.export_dir() : root;
        if (!course.empty()) folder = files::join(folder, safe_name(course));
        base_name = "Curriculum Map - " + (course.empty() ? std::string("No Class") : course);
    }
    if (year) base_name += " (" + dates::school_year_label(year) + ")";
    std::string base = files::join(folder, safe_name(base_name)), err;
    std::vector<std::string> out;
    if (!files::make_dirs(folder, &err)) throw ExportError{"EXPORT FAILED: " + err};
    for (const auto &f : fmts) {
        if (f == "CSV") {   // with a BOM, so Excel reads it as UTF-8
            if (!files::write(base + ".csv", "\xEF\xBB\xBF" + csv_text(rows), &err)) throw ExportError{"EXPORT FAILED: " + err};
            out.push_back(base + ".csv");
        } else {
            render(st, f, d, base + ext_of(f));
            out.push_back(base + ext_of(f));
        }
    }
    return out;
}

Everything export_everything(store::Store &st, const std::string &parent0, const Progress &progress) {
    std::string parent = files::expanduser(parent0), today = strftime_now("%Y-%m-%d");
    std::string name = "Chalkboard " + today;
    for (int n = 2; files::exists(files::join(parent, name)); n++) name = "Chalkboard " + today + " (" + std::to_string(n) + ")";
    Everything out;
    out.folder = files::join(parent, name);
    std::string err;
    if (!files::make_dirs(out.folder, &err)) throw ExportError{"CANNOT CREATE FOLDER: " + err};
    std::vector<std::pair<std::string, const Value *>> jobs;
    for (const auto &l : st.data.get("lessons").arr()) jobs.push_back({"lesson", &l});
    for (const auto &a : st.data.get("assessments").arr()) jobs.push_back({"assessment", &a});
    int total = (int)jobs.size() + 1;
    for (size_t i = 0; i < jobs.size(); i++) {
        std::string title = field(*jobs[i].second, "title");
        if (title.empty()) title = "Untitled";
        if (progress) progress((int)i, total, title);
        for (const char *fmt : {"PDF", "DOCX"}) {
            try {
                auto got = export_item(st, jobs[i].first, *jobs[i].second, fmt, nullptr, out.folder);
                out.files.insert(out.files.end(), got.begin(), got.end());
            } catch (ExportError &e) {
                out.problems.push_back(title + ": " + e.message);
                break;
            }
        }
    }
    if (progress) progress((int)jobs.size(), total, "backup file");
    try {
        out.files.push_back(st.backup(out.folder, false));
    } catch (store::UserError &e) {
        out.problems.push_back("BACKUP FILE: " + e.message);
    }
    if (progress) progress(total, total, "");
    return out;
}

}  // namespace exporting
