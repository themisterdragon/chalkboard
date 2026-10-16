// Exporting from the window app (port of gui/export.py): the options dialog, quick exports, the
// "files saved" dialog, board slide options, the school logo, class periods, Export Everything,
// the Day Slideshow, and the Curriculum Map. Exports run on their own thread with a progress box,
// so the window keeps drawing.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>

#include "art.h"
#include "board.h"
#include "doc.h"
#include "exporting.h"
#include "files.h"
#include "gui.h"
#include "images.h"
#include "proc.h"
#include "text.h"

namespace gui {

using namespace ui;

const std::vector<std::pair<std::string, std::string>> STYLE_TEXT = {
    {"chalk", "Chalkboard (dark green)"}, {"white", "Whiteboard (white)"}, {"school", "School colors"}};

namespace {

const std::vector<std::pair<std::string, std::string>> FORMAT_TEXT = {
    {"PDF", "PDF"},
    {"DOCX", "Word (.docx, also opens in Google Docs)"},
    {"TXT", "Plain text (for Google Classroom)"},
    {"PNG", "Board slide (1920\xC3\x97" "1080 PNG + editable slideshow for PowerPoint, Keynote, Google Slides)"},
    {"MAKEUP", "Make-up sheet + classwork for absent students (PDF + Word)"},
    {"ALL", "All of the above"}};
const std::vector<std::pair<std::string, std::string>> INCLUDE_TEXT = {
    {"BOTH", "Student copy and answer key"}, {"STUDENT", "Student copy only"}, {"KEY", "Answer key only"}};

std::string lookup(const std::vector<std::pair<std::string, std::string>> &m, const std::string &k) {
    for (const auto &p : m)
        if (p.first == k) return p.second;
    return k;
}

// a message from the core, as a sentence (Python's str(e)[:1].upper() + str(e)[1:].lower())
std::string sentence(const std::string &m) {
    if (m.empty()) return m;
    return text::upper(m.substr(0, 1)) + text::lower(m).substr(1);
}

std::string q(const std::string &s) { return "\xE2\x80\x9C" + s + "\xE2\x80\x9D"; }

std::string relpath(const std::string &p, const std::string &folder) {
    std::string f = folder;
    while (f.size() > 1 && (f.back() == '/' || f.back() == '\\')) f.pop_back();
    if (p.size() > f.size() && p.compare(0, f.size(), f) == 0 && (p[f.size()] == '/' || p[f.size()] == '\\')) return p.substr(f.size() + 1);
    return p;
}

bool is_sheet(const std::string &kind) {
    return std::find(store::SHEET_KINDS.begin(), store::SHEET_KINDS.end(), kind) != store::SHEET_KINDS.end();
}

}  // namespace

// ----------------------------------------------------------------------------- working on its own thread

void busy(App &app, const std::string &title, const std::function<void(const exporting::Progress &)> &job) {
    struct Shared {
        std::mutex mu;
        int done = 0, total = 0;
        std::string name;
        std::atomic<bool> finished{false};
        std::exception_ptr err;
    };
    auto sh = std::make_shared<Shared>();
    plat::Window *win = &app.win;
    std::thread worker([sh, job, win] {
        try {
            job([sh, win](int done, int total, const std::string &name) {
                {
                    std::lock_guard<std::mutex> lock(sh->mu);
                    sh->done = done;
                    sh->total = total;
                    sh->name = name;
                }
                win->wake();
            });
        } catch (...) {
            sh->err = std::current_exception();
        }
        sh->finished = true;
        win->wake();
    });
    // a box that shows how it's going (it can't be stopped partway, so it has no buttons)
    int poll = 0;
    std::function<void()> tick = [&] {
        app.ui.again();
        poll = app.ui.after(60, tick);
    };
    poll = app.ui.after(60, tick);
    while (!sh->finished && !app.ui.quitting())
        dialog(app.ui, {title, 460, 0},
               [&](Ui &u) {
                   int S = u.S();
                   if (sh->finished) return 1;
                   int done, total;
                   std::string name;
                   {
                       std::lock_guard<std::mutex> lock(sh->mu);
                       done = sh->done;
                       total = sh->total;
                       name = sh->name;
                   }
                   std::string msg = total == 0 ? "Getting started\xE2\x80\xA6" : done < total ? "Exporting " + std::to_string(done + 1) + " of " + std::to_string(total) + ": " + name : "Finishing\xE2\x80\xA6";
                   label(u, u.elide(*u.sk().f, msg, u.width()));
                   u.space(8 * S);
                   Rect r = u.place(0, 14 * S);
                   if (u.drawing) progress(u, r, total ? (double)done / std::max(1, total) : 0);
                   return 0;
               },
               {});
    app.ui.cancel(poll);
    worker.join();
    if (sh->err) std::rethrow_exception(sh->err);
}

// ----------------------------------------------------------------------------- the "files saved" dialog

void export_done(App &app, const std::vector<std::string> &paths) {
    std::string folder = exporting::files_folder(paths);
    size_t n = paths.size();
    std::vector<std::vector<std::string>> rows;
    for (const auto &p : paths) rows.push_back({relpath(p, folder)});
    int sel = 0;
    Buddy pal;
    bool cheered = false;
    app.status("Exported " + std::to_string(n) + " file" + (n != 1 ? "s" : "") + ".");
    bool first = true;
    dialog(app.ui, {"Export Complete", 600, 0},
           [&](Ui &u) {
               Skin &k = u.sk();
               int S = u.S();
               if (!cheered) {   // the school mascot cheers the export on
                   cheered = true;
                   int j = u.after(150, [&] { pal.cheer(app); });
                   pal.jobs.push_back(j);
               }
               double ph = chibi::SIZE * 2 * S + 30 * S;
               std::string msg = "Saved " + std::to_string(n) + " file" + (n != 1 ? "s" : "") + " to\n" + folder;
               auto lines = u.wrap(*k.f, msg, u.width() - 32 * S - 12 * S - (app.settings().s("mascot").empty() ? 0 : chibi::SIZE * 2 * S + 150 * S));
               Rect top = u.place(0, std::max(32.0 * S, std::max(app.settings().s("mascot").empty() ? 0 : ph, lines.size() * k.f->line)));
               if (u.drawing) {
                   u.pixel_art(*icon("folder"), icon_color, top.x, top.y, 2 * S);
                   for (size_t i = 0; i < lines.size(); i++) u.text(*k.f, top.x + 44 * S, top.y + i * k.f->line + k.f->ascent, lines[i], k["text"]);
               }
               pal.draw(app, u, top.r(), top.b(), 2 * S);
               u.space(10 * S);
               double rh = k.line + (k.soft ? 10 : 6) * S;
               Rect lr = u.place(0, std::min(8, std::max(3, (int)n)) * rh + 2 * S);
               if (first) {
                   u.focus(u.id("list:files"));
                   first = false;
               }
               ListResult res = list_view(u, "files", lr, {{"File", 520, true}}, rows, sel, "", false);
               if (res.open && sel >= 0) proc::open_path(paths[(size_t)sel]);
               u.space(4 * S);
               label(u, "Double-click a file to open it.", false, true, true);
               u.space(8 * S);
               u.flow();
               if (button(u, "Open File", sel >= 0)) proc::open_path(paths[(size_t)sel]);
               if (button(u, "Open Folder")) proc::open_path(folder);
               u.flow(false);
               return 0;
           },
           {{"Done", 1}});
}

// ----------------------------------------------------------------------------- exporting

void run_export(App &app, const std::string &kind, const std::string &id, const std::string &fmt) {
    app.save();
    Value obj;
    if (kind == "lesson") {
        if (!app.lesson(id)) return;
        obj = *app.lesson(id);
    } else {
        if (!app.assessment(id)) return;
        obj = *app.assessment(id);
    }
    std::vector<std::string> files;
    try {
        busy(app, "Exporting", [&](const exporting::Progress &p) { files = exporting::export_item(app.store, kind, obj, fmt, p); });
    } catch (exporting::ExportError &e) {
        alert(app.ui, "Export Didn't Work", sentence(e.message), "warn");
        app.status("");
        return;
    }
    export_done(app, files);
}

bool browse(App &app, std::string &folder) {
    std::string d = app.win.choose_folder("Export folder");
    if (d.empty()) return false;
    folder = d;
    return true;
}

void export_dialog(App &app, const std::string &kind, const std::string &id) {
    app.save();
    Value *objp = kind == "lesson" ? app.lesson(id) : app.assessment(id);
    if (!objp) return;
    Value obj = *objp;
    Value &st = app.settings();
    std::string fkey = kind == "lesson" ? "lesson_export_format" : "export_format";
    const auto &formats = kind == "lesson" ? exporting::LESSON_FORMATS : exporting::ASSESSMENT_FORMATS;
    std::string fmt = st.s(fkey);
    if (std::find(formats.begin(), formats.end(), fmt) == formats.end()) fmt = "PDF";
    size_t linked = kind == "lesson" ? app.store.attached(obj).size() : 0;
    bool copies = kind == "assessment" && !is_sheet(obj.s("kind"));
    std::string include = st.s("export_include").empty() ? "BOTH" : st.s("export_include");
    std::string versions = st.has("export_versions") ? store::text_of(st.get("export_versions")) : "1";
    std::string style = st.s("board_style").empty() ? "chalk" : st.s("board_style");
    std::string font = st.s("font").empty() ? "Times" : st.s("font");
    std::string page = st.s("page").empty() ? "Letter" : st.s("page");
    std::string folder = app.store.export_dir();
    std::string rel = relpath(exporting::export_folder(app.store, kind, obj), app.store.export_dir());
    int r = dialog(
        app.ui, {"Export: " + (obj.s("title").empty() ? std::string("untitled") : obj.s("title")), 780, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double lw = std::min(360.0 * S, u.width() * 0.48);
            Rect area{u.remaining().x, u.remaining().y, u.width(), 1e6};
            // left: the format
            u.begin({area.x, area.y, lw, 1e6}, -1);
            begin_group(u, "Format");
            for (const auto &f : formats)
                if (radio(u, lookup(FORMAT_TEXT, f), fmt == f, true, lw - 30 * S)) fmt = f;
            if (linked) {
                u.space(4 * S);
                std::string t = std::to_string(linked) + " linked worksheet" + (linked != 1 ? "s" : "") + ": " +
                                (fmt == "ALL" ? "included (PDF + Word)." : "pick All of the above to include them.");
                para(u, t, true, 0, k.fsmall.get());
            }
            end_group(u);
            Rect lu = u.end();
            // right: copies, the board slide, font and paper
            u.begin({area.x + lw + 14 * S, area.y, area.w - lw - 14 * S, 1e6}, -1);
            if (copies) {
                begin_group(u, "Copies");
                for (const auto &p : INCLUDE_TEXT)
                    if (radio(u, p.second, include == p.first)) include = p.first;
                u.space(6 * S);
                u.flow();
                label_mid(u, "Versions:");
                std::vector<std::pair<std::string, std::string>> vopts;
                for (int n = 1; n <= 4; n++)
                    vopts.push_back({std::to_string(n), n == 1 ? "1 (as written)" : std::to_string(n) + " (A\xE2\x80\x93" + std::string(1, "ABCD"[n - 1]) + ", shuffled)"});
                dropdown(u, "versions", vopts, versions, 18);
                u.flow(false);
                end_group(u);
                u.space(8 * S);
            }
            if (kind == "lesson" && (fmt == "PNG" || fmt == "ALL")) {
                begin_group(u, "Board Slide");
                u.row({std::min(u.width(), 24 * k.f->width("0") + 40 * S)});
                dropdown(u, "style", STYLE_TEXT, style, 24);
                u.row({});
                u.space(6 * S);
                u.flow();
                if (button(u, "Sections & Colors\xE2\x80\xA6", true, false, true)) {
                    board_options(app);
                    style = st.s("board_style").empty() ? "chalk" : st.s("board_style");
                }
                if (button(u, "Board Designer\xE2\x80\xA6", true, false, true)) board_designer(app);
                u.flow(false);
                end_group(u);
                u.space(8 * S);
            }
            if (fmt != "PNG") {
                begin_group(u, "Font & Paper");
                u.flow();
                for (auto p : std::vector<std::pair<std::string, std::string>>{{"Times", "Times (serif)"}, {"Helvetica", "Helvetica (sans)"}})
                    if (radio(u, p.second, font == p.first)) font = p.first;
                u.flow(false);
                u.flow();
                for (const char *p : {"Letter", "A4"})
                    if (radio(u, p, page == p)) page = p;
                u.flow(false);
                end_group(u);
            }
            Rect ru = u.end();
            u.place(0, std::max(lu.h, ru.h));
            u.space(10 * S);
            begin_group(u, "Save To");
            double bw = u.btn_w("Browse\xE2\x80\xA6", true);
            u.row({-bw - u.lay().gap, bw}, u.ctl_h());
            entry(u, "folder", folder);
            Rect b = u.place(bw, u.ctl_h());
            if (button_at(u, u.id("btn:browse"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Browse\xE2\x80\xA6", true, false, true)) browse(app, folder);
            u.row({});
            u.space(4 * S);
            para(u, "Files go in the subfolder " + rel + " (class, then unit" + (kind == "lesson" ? ", then lesson" : "") +
                        "). PDFs go right in it; answer keys, board slides, Word, and text files get folders of their own.",
                 true, 0, k.fsmall.get());
            end_group(u);
            return 0;
        },
        {{"Export", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return;
    st[fkey] = fmt;
    st["board_style"] = style;
    st["font"] = font;
    st["page"] = page;
    if (copies) {
        st["export_include"] = include;
        st["export_versions"] = std::stoi(versions);
    }
    std::string path = text::strip(folder);
    if (!path.empty() && files::expanduser(path) != app.store.export_dir()) st["export_dir"] = path;
    app.save();
    run_export(app, kind, id, fmt);
}

// ----------------------------------------------------------------------------- board slide options

void board_options(App &app) {
    Value &st = app.settings();
    std::vector<std::string> on;
    for (const auto &x : st.get("board_sections").arr()) on.push_back(store::text_of(x));
    auto secs_list = store::board_sections(st);
    std::vector<char> secs;
    for (const auto &s : secs_list) secs.push_back(std::find(on.begin(), on.end(), s.key) != on.end());
    bool stdtext = st.has("board_std_text") ? st.get("board_std_text").truthy() : true;
    std::vector<std::pair<std::string, std::string>> color_keys = {{"primary_color", "Background"}, {"secondary_color", "Headings"}, {"text_color", "Text"}};
    std::vector<std::string> colors;
    for (const auto &c : color_keys) colors.push_back(st.s(c.first));
    auto side_label = [](const std::string &col) {
        for (const auto &s : store::BOARD_SIDES)
            if (s.first == col) return s.second;
        return col;
    };
    int r = dialog(
        app.ui, {"Board Slide Options", 560, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "A 1920\xC3\x97" "1080 picture for the ViewBoard. Empty sections are left off, and text sizes itself to fit.");
            u.space(8 * S);
            begin_group(u, "Sections");
            double half = std::floor((u.width() - 18 * S) / 2);
            for (size_t i = 0; i < secs_list.size(); i += 2) {
                u.row({half, -1});
                for (size_t j = i; j < i + 2; j++) {
                    if (j >= secs_list.size()) {
                        u.place(1, 1);
                        continue;
                    }
                    bool v = secs[j];
                    u.push_id((Id)j + 1);
                    if (check(u, secs_list[j].label + "  (" + side_label(secs_list[j].col) + ")", v)) secs[j] = v;
                    u.pop_id();
                }
            }
            u.row({});
            u.space(6 * S);
            check(u, "Show each standard's full text (not just its code)", stdtext);
            end_group(u);
            u.space(8 * S);
            u.flow();
            if (button(u, "School Logo\xE2\x80\xA6", true, false, true)) logo_options(app);
            if (button(u, "Class Periods & Codes\xE2\x80\xA6", true, false, true)) {
                class_periods(app);
                for (size_t j = 0; j < secs_list.size(); j++)
                    if (secs_list[j].key == "class_codes") {
                        bool has = false;
                        for (const auto &x : st.get("board_sections").arr()) has |= store::text_of(x) == "class_codes";
                        secs[j] = has;
                    }
            }
            u.flow(false);
            u.space(10 * S);
            begin_group(u, "School Colors (HEX, like #7A0019)");
            double lw = k.f->width("Background:") + 8 * S;
            for (size_t i = 0; i < color_keys.size(); i++) {
                u.push_id((Id)i + 1);
                u.row({lw, 10 * k.f->width("0") + 20 * S, 28.0 * S}, u.ctl_h());
                label_mid(u, color_keys[i].second + ":");
                entry(u, "color", colors[i]);
                Rect sw = u.place(28 * S, u.ctl_h());
                if (u.drawing) {
                    Rect b{sw.x, sw.y + (sw.h - 18 * S) / 2, 28.0 * S, 18.0 * S};
                    std::string hx = store::parse_hex(colors[i]);
                    u.fill(b, k["dark"]);
                    u.fill(b.in(S), hx.empty() ? k["window"] : hex(hx));
                }
                u.pop_id();
            }
            u.row({});
            end_group(u);
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return;
    Value sections = Value::array();
    for (const auto &bs : store::BOARD_SECTIONS)
        for (size_t j = 0; j < secs_list.size(); j++)
            if (secs_list[j].key == bs.key && secs[j]) sections.push(bs.key);
    st["board_sections"] = sections;
    st["board_std_text"] = stdtext;
    std::vector<std::string> bad;
    bool any = false;
    for (size_t i = 0; i < color_keys.size(); i++) {
        std::string raw = text::strip(colors[i]);
        std::string code = raw.empty() ? "" : store::parse_hex(raw);
        if (!raw.empty() && code.empty()) bad.push_back(raw);
        else st[color_keys[i].first] = code;
    }
    for (const auto &c : color_keys) any |= !st.s(c.first).empty();
    if (any && bad.empty()) st["board_style"] = "school";
    app.save();
    if (!bad.empty()) {
        alert(app.ui, "School Colors", q(bad[0]) + " isn't a HEX color. Use something like #7A0019.", "warn");
        return;
    }
    if (st.s("board_style") == "school") {
        auto weak = board::low_contrast(st);
        if (!weak.empty()) {
            std::vector<std::string> low;
            for (const auto &w : weak) low.push_back(text::lower(w));
            alert(app.ui, "School Colors", "Heads up: the " + text::join(low, " and ") + " may be hard to read on that background.", "warn");
        }
    }
}

// ----------------------------------------------------------------------------- the school logo

std::string logo_text(App &app) {
    const images::Image *img = app.store.logo();
    if (!img) return "No logo. Board slides show just the title.";
    std::string where = app.settings().s("logo_place") != "right" ? "left of the title" : "top right corner";
    return (img->name.empty() ? std::string("Logo") : img->name) + " (" + std::to_string(img->w) + "\xC3\x97" + std::to_string(img->h) + "), " + where;
}

void logo_options(App &app) {
    Value &st = app.settings();
    std::string place = st.s("logo_place").empty() ? "left" : st.s("logo_place");
    std::string info = logo_text(app);
    auto changed = [&] {
        st["logo_place"] = place;
        app.save();
        info = logo_text(app);
    };
    dialog(
        app.ui, {"School Logo", 500, 0},
        [&](Ui &u) {
            int S = u.S();
            para(u, "A PNG or JPEG of your school's logo goes in the corner of every board slide. A PNG with a transparent background looks best.");
            u.space(10 * S);
            para(u, info, false, 0, u.sk().fb.get());
            u.space(6 * S);
            u.flow();
            label_mid(u, "Put it:");
            for (auto p : std::vector<std::pair<std::string, std::string>>{{"left", "Left of the title"}, {"right", "Top right corner"}})
                if (radio(u, p.second, place == p.first) && place != p.first) {
                    place = p.first;
                    changed();
                }
            u.flow(false);
            u.space(10 * S);
            u.flow();
            if (button(u, "Choose Picture\xE2\x80\xA6", true, false, true)) {
                std::string path = app.win.open_file("School logo", {{"Pictures", {"*.png", "*.jpg", "*.jpeg", "*.PNG", "*.JPG", "*.JPEG"}}, {"All files", {"*"}}});
                if (!path.empty()) {
                    images::Image img;
                    std::string err;
                    try {
                        busy(app, "Reading the Picture", [&](const exporting::Progress &) {
                            try {
                                img = images::read_image(path);
                                img.name = files::basename(path);
                                img.has_name = true;
                            } catch (images::ImageError &e) {
                                err = e.message;
                            }
                        });
                    } catch (...) {
                        err = "COULDN'T READ THAT PICTURE";
                    }
                    if (!err.empty())
                        alert(u, "School Logo", err.substr(0, 1) + text::lower(err.substr(1)), "warn");
                    else {
                        app.store.set_logo_image(img);
                        changed();
                    }
                }
            }
            if (button(u, "Remove", true, false, true) && app.store.logo() && confirm(u, "School Logo", "Take the logo off your board slides?", "Remove")) {
                app.store.remove_logo();
                changed();
            }
            u.flow(false);
            return 0;
        },
        {{"Done", 1}}, 0, 1);
}

// ----------------------------------------------------------------------------- class periods

std::string periods_text(App &app) {
    int n = 0;
    for (const auto &p : app.settings().get("class_periods").arr())
        if (!text::strip(p.s("codes")).empty()) n++;
    if (!n) return "No class codes. Each lesson gets one board slide.";
    return std::to_string(n) + " class period" + (n != 1 ? "s" : "") + " with codes: a board slide for each";
}

static bool edit_period(App &app, Value &p) {
    std::string name = p.s("name"), course = p.s("course"), codes = p.s("codes");
    bool first = true;
    int r = dialog(
        app.ui, {"Class Period", 500, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            double lw = k.f->width("Course:") + 8 * S;
            if (first) {
                focus_at_end(u, u.id("entry:name"));
                first = false;
            }
            u.row({lw, -1}, u.ctl_h());
            label_mid(u, "Name:");
            entry(u, "name", name);
            u.row({lw, -1}, u.ctl_h());
            label_mid(u, "Course:");
            entry(u, "course", course);
            u.row({});
            u.space(2 * S);
            para(u, "Leave the course blank to make this period's slide for every lesson, or type a course (like English 10) to make it only for that "
                    "course's lessons.",
                 true, 0, k.fsmall.get());
            u.space(8 * S);
            label(u, "Codes, one per line, like  Google Classroom: abc123");
            u.space(2 * S);
            text_area(u, "codes", codes, 5, 10, false);
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    p["name"] = text::strip(name).empty() ? "Class" : text::strip(name);
    p["course"] = text::strip(course);
    p["codes"] = text::strip(codes);
    return true;
}

void class_periods(App &app) {
    Value &st = app.settings();
    if (!st.get("class_periods").is_array()) st["class_periods"] = Value::array();
    int sel = -1;
    bool first = true;
    dialog(
        app.ui, {"Class Periods & Codes", 600, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            Value &periods = st["class_periods"];
            para(u, "Teach the same lesson to more than one class? Add each period with its class codes (type the app's name yourself, like Google "
                    "Classroom: abc123). Exporting a board slide then makes one slide per period, each with its own codes.");
            u.space(10 * S);
            std::vector<std::vector<std::string>> rows;
            for (const auto &p : periods.arr()) {
                std::vector<std::string> cs;
                for (const auto &x : text::split(p.s("codes"), "\n"))
                    if (!text::strip(x).empty()) cs.push_back(text::strip(x));
                rows.push_back({p.s("name"), p.s("course").empty() ? "Any" : p.s("course"), text::join(cs, " \xC2\xB7 ")});
            }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 6 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:periods"));
                first = false;
            }
            ListResult res = list_view(u, "periods", lr, {{"Period", 130, false}, {"Course", 130, false}, {"Codes", 260, true}}, rows, sel,
                                       "No periods yet. Click Add.");
            auto saved = [&] {
                app.save();
                u.again();
            };
            auto add = [&] {
                Value p = Value::object();
                p["name"] = "Period " + std::to_string(st["class_periods"].size() + 1);
                p["course"] = "";
                p["codes"] = "";
                if (!edit_period(app, p)) return;
                st["class_periods"].push(p);
                Value &sections = st["board_sections"];
                if (!sections.is_array()) sections = Value::array();
                bool has = false;
                for (const auto &x : sections.arr()) has |= store::text_of(x) == "class_codes";
                if (!has) sections.push("class_codes");   // adding codes means you want them on the slide
                sel = (int)st["class_periods"].size() - 1;
                saved();
            };
            auto edit = [&] {
                if (sel < 0 || sel >= (int)st["class_periods"].size()) return;
                Value p = st["class_periods"].at((size_t)sel);
                if (edit_period(app, p)) {
                    st["class_periods"].at((size_t)sel) = p;
                    saved();
                }
            };
            auto remove = [&] {
                if (sel < 0 || sel >= (int)st["class_periods"].size()) return;
                std::string nm = st["class_periods"].at((size_t)sel).s("name");
                if (!confirm(u, "Remove Period", "Remove " + (nm.empty() ? std::string("this period") : nm) + "?", "Remove")) return;
                auto &a = st["class_periods"].arr_ref();
                a.erase(a.begin() + sel);
                saved();
            };
            auto move = [&](int step) {
                auto &a = st["class_periods"].arr_ref();
                int j = sel + step;
                if (sel < 0 || j < 0 || j >= (int)a.size()) return;
                std::swap(a[(size_t)sel], a[(size_t)j]);
                sel = j;
                saved();
            };
            if (res.open) edit();
            if (res.del) remove();
            u.space(8 * S);
            u.flow();
            if (button(u, "Add\xE2\x80\xA6", true, false, true)) add();
            if (button(u, "Edit\xE2\x80\xA6", true, false, true)) edit();
            if (button(u, "Remove", true, false, true)) remove();
            if (button(u, "Move Up", true, false, true)) move(-1);
            if (button(u, "Move Down", true, false, true)) move(1);
            u.flow(false);
            return 0;
        },
        {{"Done", 1}}, 0, 1);
    app.save();
}

// ----------------------------------------------------------------------------- Export Everything

void export_everything_dialog(App &app) {
    std::string parent = app.win.choose_folder("Put the export folder where?");
    if (parent.empty() || !app.save()) return;
    exporting::Everything got;
    try {
        busy(app, "Exporting Everything", [&](const exporting::Progress &p) { got = exporting::export_everything(app.store, parent, p); });
    } catch (exporting::ExportError &e) {
        alert(app.ui, "Export Didn't Work", sentence(e.message), "warn");
        return;
    }
    app.status("Exported " + std::to_string(got.files.size()) + " files.");
    std::string msg = "Every lesson and assessment is in\n" + got.folder +
                      "\n\nas PDF and Word files, with a backup file. Keep the folder, or drag it into Google Drive or another cloud folder.";
    if (!got.problems.empty()) {
        std::vector<std::string> first(got.problems.begin(), got.problems.begin() + (long)std::min<size_t>(6, got.problems.size()));
        msg += "\n\nThese didn't export:\n" + text::join(first, "\n") + (got.problems.size() > 6 ? "\n\xE2\x80\xA6" : "");
    }
    if (confirm(app.ui, "Everything Exported", msg, "Open Folder", "Done")) proc::open_path(got.folder);
}

// ----------------------------------------------------------------------------- the Day Slideshow

void day_slideshow(App &app) {
    app.save();
    Value &st = app.settings();
    if (!st.get("class_periods").size()) {
        if (confirm(app.ui, "Day Slideshow",
                    "A day slideshow puts each class period's board slide in order, so the next class is one click away. First, add your class periods.",
                    "Add Class Periods\xE2\x80\xA6", "Cancel", "info"))
            class_periods(app);
        if (!st.get("class_periods").size()) return;
    }
    Value periods = st.get("class_periods");
    std::vector<const Value *> lessons;
    for (const auto &l : app.store.data["lessons"].arr())
        if (l.is_object()) lessons.push_back(&l);
    std::stable_sort(lessons.begin(), lessons.end(), [](const Value *a, const Value *b) { return a->s("updated") > b->s("updated"); });
    std::vector<std::string> picks(periods.size());
    Calendar cal;
    cal.marks = lesson_marks(app);
    Date day = today();
    cal.init(&day, &day);
    std::string found;
    auto match = [&](const Date &d) {
        day = d;
        auto plan = store::day_plan(st, lessons, d);
        int n = 0;
        for (size_t i = 0; i < picks.size() && i < plan.size(); i++) {
            picks[i] = plan[i].second ? plan[i].second->s("id") : "";
            if (plan[i].second) n++;
        }
        found = n ? "Found a lesson for " + std::to_string(n) + " of " + std::to_string(plan.size()) + " periods. Change any of them below."
                  : "No lessons are dated this day. Pick them below, or pick another day.";
    };
    match(day);
    bool first = true;
    int r = dialog(
        app.ui, {"Day Slideshow", 760, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Every class period's board slide, in order, in one slideshow (and a PDF). At passing time, just go to the next slide. Pick the day; "
                    "dots are days with lessons.");
            u.space(10 * S);
            Rect area{u.remaining().x, u.remaining().y, u.width(), 1e6};
            u.begin({area.x, area.y, 300.0 * S, 1e6}, 0);
            if (first) {
                u.focus(u.id("calendar:day"));
                first = false;
            }
            if (cal.frame(u, "day")) match(cal.start);
            Rect lu = u.end();
            u.begin({area.x + lu.w + 16 * S, area.y, area.w - lu.w - 16 * S, 1e6}, -1);
            label(u, dates::fmt_date(day, true), true);
            para(u, found, true);
            u.space(4 * S);
            begin_group(u, "Class Periods");
            for (size_t i = 0; i < periods.size(); i++) {
                const Value &p = periods.at(i);
                std::string course = text::strip(p.s("course"));
                u.push_id((Id)i + 1);
                label(u, (p.s("name").empty() ? "Class " + std::to_string(i + 1) : p.s("name")) + (course.empty() ? "" : " (" + course + ")") + ":");
                std::vector<std::pair<std::string, std::string>> opts = {{"", "(skip this period)"}};
                for (const Value *l : lessons)
                    if (course.empty() || text::lower(text::strip(l->s("course"))) == text::lower(course))
                        opts.push_back({l->s("id"), (l->s("title").empty() ? std::string("Untitled") : l->s("title")) +
                                                        (l->get("date").truthy() ? " \xE2\x80\x94 " + first_line(l->s("date")) : "")});
                u.row({std::min(u.width(), 30 * k.f->width("0") + 40 * S)});
                dropdown(u, "lesson", opts, picks[i], 30);
                u.row({});
                u.pop_id();
            }
            end_group(u);
            Rect ru = u.end();
            u.place(0, std::max(lu.h, ru.h));
            return 0;
        },
        {{"Make Slideshow", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return;
    std::vector<std::pair<Value, const Value *>> chosen;
    for (size_t i = 0; i < periods.size(); i++) {
        const Value *l = nullptr;
        for (const Value *x : lessons)
            if (!picks[i].empty() && x->s("id") == picks[i]) l = x;
        chosen.push_back({periods.at(i), l});
    }
    std::vector<std::string> files;
    try {
        busy(app, "Making the Day Slideshow", [&](const exporting::Progress &) { files = exporting::export_day(app.store, chosen, day); });
    } catch (exporting::ExportError &e) {
        alert(app.ui, "Day Slideshow", sentence(e.message), "warn");
        app.status("");
        return;
    }
    export_done(app, files);
}

// ----------------------------------------------------------------------------- the Curriculum Map

void curriculum_map(App &app, const std::string *course) {
    app.save();
    Value &st = app.settings();
    std::vector<const Value *> lessons;
    for (const auto &l : app.store.data["lessons"].arr())
        if (l.is_object()) lessons.push_back(&l);
    std::vector<std::pair<std::string, int>> found;
    for (const auto &c : store::classes(st, lessons))
        if (c.second) found.push_back(c);
    if (found.empty()) {
        alert(app.ui, "Curriculum Map", "Plan a few lessons first. The map is built from your lessons' units, dates, and standards.");
        return;
    }
    std::string cls = found[0].first;
    if (course)
        for (const auto &f : found)
            if (f.first == *course) cls = *course;
    // one school year at a time, so next year's copies of lessons don't double up this year's map
    auto years = doc::school_years(lessons);
    int now_year = dates::school_year(today());
    std::string year = years.empty() ? "" : std::to_string(std::find(years.begin(), years.end(), now_year) != years.end() ? now_year : years[0]);
    std::string subj = std::find(app.store.subjects.begin(), app.store.subjects.end(), st.s("subject")) != app.store.subjects.end() ? st.s("subject") : "";
    std::string grades = st.s("grades");
    std::vector<char> want;
    for (const auto &f : exporting::MAP_FORMATS) want.push_back(f.first == "PDF" || f.first == "CSV");
    auto chosen = [&]() -> std::pair<bool, std::string> { return cls == "*all*" ? std::make_pair(true, std::string()) : std::make_pair(false, cls); };
    auto the_year = [&] { return year.empty() ? 0 : std::stoi(year); };
    int r = dialog(
        app.ui, {"Curriculum Map", 560, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Every unit in the order you teach it, with its dates, essential questions, standards, lessons, and assessments, then where each "
                    "standard is taught. It's built from your lessons, so the more dates, units, and standards they have, the fuller the map.");
            u.space(10 * S);
            double lw = k.f->width("Not-taught-yet list:") + 8 * S;
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Class:");
            std::vector<std::pair<std::string, std::string>> copts;
            for (const auto &f : found) copts.push_back({f.first, (f.first.empty() ? std::string("No class yet") : f.first) + " (" + std::to_string(f.second) + " lesson" + (f.second != 1 ? "s" : "") + ")"});
            copts.push_back({"*all*", "All my classes"});
            dropdown(u, "class", copts, cls, 30);
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "School year:");
            std::vector<std::pair<std::string, std::string>> yopts;
            for (int y : years) yopts.push_back({std::to_string(y), dates::school_year_label(y)});
            yopts.push_back({"", "Every year"});
            dropdown(u, "year", yopts, year, 14);
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Not-taught-yet list:");
            std::vector<std::pair<std::string, std::string>> sopts = {{"", "Leave it off"}};
            for (const auto &x : app.store.subjects) sopts.push_back({x, x});
            dropdown(u, "subject", sopts, subj, 30);
            u.row({lw, 0}, u.ctl_h());
            label_mid(u, "Grades:");
            std::vector<std::pair<std::string, std::string>> gopts;
            for (const auto &x : store::GRADE_CHOICES) gopts.push_back({x, grade_label(x)});
            dropdown(u, "grades", gopts, grades, 14);
            u.row({});
            u.space(10 * S);
            begin_group(u, "Save as");
            for (size_t i = 0; i < exporting::MAP_FORMATS.size(); i++) {
                bool v = want[i];
                u.push_id((Id)i + 1);
                if (check(u, exporting::MAP_FORMATS[i].second, v)) want[i] = v;
                u.pop_id();
            }
            u.space(4 * S);
            para(u, "The spreadsheet has one row per lesson, for sorting or sharing with your school.", true);
            end_group(u);
            u.space(10 * S);
            if (button(u, "Preview\xE2\x80\xA6", true, false, true)) {
                std::string c = chosen().first ? found[0].first : cls;
                app.preview(doc::curriculum_doc(app.store, c, subj, grades, the_year()));
            }
            return 0;
        },
        {{"Save Map", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return;
    std::vector<std::string> fmts;
    for (size_t i = 0; i < exporting::MAP_FORMATS.size(); i++)
        if (want[i]) fmts.push_back(exporting::MAP_FORMATS[i].first);
    if (fmts.empty()) {
        alert(app.ui, "Curriculum Map", "Check at least one way to save it (PDF, Word, or spreadsheet).", "warn");
        return;
    }
    std::vector<std::string> files;
    try {
        auto c = chosen();
        busy(app, "Making the Curriculum Map", [&](const exporting::Progress &) { files = exporting::export_map(app.store, c.second, c.first, fmts, subj, grades, "", the_year()); });
    } catch (exporting::ExportError &e) {
        alert(app.ui, "Curriculum Map", sentence(e.message), "warn");
        return;
    }
    export_done(app, files);
}

}  // namespace gui
