#include "tui_app.h"

#include <algorithm>
#include <set>

#include "art.h"
#include "doc.h"
#include "export_txt.h"
#include "markup.h"
#include "text.h"
#include "version.h"

namespace tui {

using json::Value;
using store::field;
using store::text_of;

// Python's // (rounds down, also for negative numbers)
static int fdiv(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

static std::string key_text(int k) {
    if (k < 0x20 || k >= term::K_ENTER || k == 0x7f) return "";
    return text::to8((char32_t)k);
}

std::string grade_label(const std::string &g) {
    if (g == store::ALL) return "ALL GRADES";
    // [K\d]+(-\d+)?
    bool plain = !g.empty();
    size_t i = 0;
    while (i < g.size() && (g[i] == 'K' || isdigit((unsigned char)g[i]))) i++;
    if (i == 0) plain = false;
    if (plain && i < g.size()) {
        if (g[i] != '-' || i + 1 >= g.size()) plain = false;
        else
            for (size_t j = i + 1; j < g.size(); j++) if (!isdigit((unsigned char)g[j])) plain = false;
    }
    if (!plain) return text::upper(g);
    if (g == "K") return "KINDERGARTEN";
    return g.find('-') != std::string::npos ? "GRADES " + g : "GRADE " + g;
}

std::string first_line(const std::string &s) {
    for (const auto &line : text::split(s, "\n"))
        if (!text::strip(line).empty()) return markup::plain(text::strip(line));
    return "";
}

static std::string lpad(const std::string &s, size_t n, char c = ' ') {
    size_t len = text::length(s);
    return len >= n ? s : s + std::string(n - len, c);
}

// ----------------------------------------------------------------------------- the app

App::App(term::Screen &screen, store::Store &st, bool no_boot)
    : screen_(screen), store_(st), ui(screen, [this]() -> Value & { return store_.settings(); }), no_boot_(no_boot) {
    if (!store_.warning.empty()) {
        ui.msg = store_.warning;
        store_.warning.clear();
    }
}

Value *App::lesson(const std::string &id) {
    for (auto &l : store_.data["lessons"].arr_ref())
        if (l.is_object() && l.get("id").is_string() && l.get("id").str() == id) return &l;
    return nullptr;
}

Value *App::assessment(const std::string &id) { return store_.assessment(id); }

void App::save() {
    std::string err;
    if (!store_.save(&err)) ui.msg = "?COULD NOT SAVE: " + err;
}

// every lesson and assessment needs an id the screens can find it by
static void ensure_ids(store::Store &s) {
    std::set<std::string> seen;
    for (const char *k : {"lessons", "assessments"})
        for (auto &x : s.data[k].arr_ref()) {
            if (!x.is_object()) continue;
            std::string id = x.get("id").is_string() ? x.get("id").str() : "";
            if (id.empty() || seen.count(id)) {
                if (!id.empty() && std::string(k) == "assessments") continue;   // a duplicate assessment id: lessons link by it, so leave it
                id = store::new_id();
                x["id"] = id;
            }
            seen.insert(id);
        }
}

void App::run() {
    ensure_ids(store_);
    if ((st().has("boot") ? st().get("boot").truthy() : true) && !no_boot_) boot();
    main_menu();
    save();
}

void App::boot() {
    int rows, w, x0;
    ui.dims(rows, w, x0);
    screen_.clear(ui.N);
    bool skip = false;
    auto pause = [&](int ms) {
        if (skip) return;
        screen_.present();
        screen_.sleep_ms(ms);
        if (screen_.read(0) != term::K_NONE) skip = true;
    };
    auto banner = art::big(art::APP, w >= 106 ? "\xE2\x96\x88\xE2\x96\x88" : "\xE2\x96\x88");
    for (size_t i = 0; i < banner.size(); i++) {
        ui.draw(2 + (int)i, x0 + 1, banner[i], ui.HI);
        pause(50);
    }
    std::string key = st().s("mascot");
    bool has_art = art::has_mascot(key) && !(w < 77 || rows < 19);   // no room beside the stats: skip it
    std::vector<std::string> pic = has_art ? art::sprite(key) : std::vector<std::string>();
    int left = x0 + 1, mx = left + 58;
    if (has_art) {   // scan the mascot in, two pixel rows at a time
        for (int dy = 0; dy < 8; dy++) {
            std::vector<std::string> two = {pic[(size_t)dy * 2], pic[(size_t)dy * 2 + 1]};
            for (int i = 0; i < 16; i++) {
                char a = two[0][(size_t)i], b = two[1][(size_t)i];
                if (a == '.' && b == '.') continue;
                ui.draw(9 + dy, mx + i, a == b ? "\xE2\x96\x88" : "\xE2\x96\x80", ui.pix(a, a == b ? '.' : b));
            }
            pause(35);
        }
    }
    std::string kas = "STANDARDS, " + std::to_string(store_.subjects.size()) + " SUBJECTS ";
    std::vector<std::string> lines = {std::string(art::APP) + " LESSON PLANNING SYSTEM  V" + CHALKBOARD_VERSION,
                                      "",
                                      lpad(kas, 31, '.') + " " + std::to_string(store_.kas_count) + " STANDARDS",
                                      "LESSON PLANS .................. " + std::to_string(store_.data.get("lessons").size()),
                                      "ASSESSMENTS ................... " + std::to_string(store_.data.get("assessments").size()),
                                      "NETWORK ....................... NOT REQUIRED"};
    for (size_t i = 0; i < lines.size(); i++) {
        ui.draw(9 + (int)i, left, ui.tx(lines[i]), ui.N);
        pause(120);
    }
    if (has_art) {
        std::string cheer = "GO " + art::name(key) + "!";
        ui.draw(18, mx + fdiv(16 - (int)text::length(cheer), 2), cheer, ui.HI);
        for (int f = 0; f < 4; f++) {
            ui.draw_sprite(9, mx, f % 2 == 0 ? art::hop(pic) : pic);
            pause(110);
        }
    }
    std::u32string run = text::to32(ui.tx("]RUN CHALKBOARD"));
    for (size_t j = 0; j < run.size(); j++) {
        ui.draw(9 + (int)lines.size() + 1, left + (int)j, text::to8(run[j]), ui.HI);
        pause(55);
    }
    pause(400);
}

void App::main_menu() {
    struct Item { std::string key, label; std::function<void()> fn; };
    std::vector<Item> items = {{"1", "LESSON PLANS", [this] { lessons_screen(); }},
                               {"2", "ASSESSMENTS & ASSIGNMENTS", [this] { assessments_screen(); }},
                               {"3", "STANDARDS LIBRARY", [this] { standards_library(); }},
                               {"4", "SETTINGS", [this] { settings_screen(); }},
                               {"Q", "QUIT", nullptr}};
    if (leave) items.insert(items.begin() + 4, {"W", "WINDOW VIEW", [this] {
                                                    save();
                                                    leave();
                                                }});
    size_t sel = 0;
    bool first = true;
    for (;;) {
        int rows, w, x0;
        ui.dims(rows, w, x0);
        ui.begin();
        ui.header("MAIN MENU");
        int y = 2;
        if (rows >= 24) {
            auto banner = art::big(art::APP, w >= 106 ? "\xE2\x96\x88\xE2\x96\x88" : "\xE2\x96\x88");
            for (size_t i = 0; i < banner.size(); i++) ui.put(y + (int)i, x0 + 1, banner[i], &ui.HI);
            y += 6;
        }
        ui.line(y, "* LESSON PLANS, ASSESSMENTS & ASSIGNMENTS *", &ui.DIM);
        y += 2;
        int mx = x0 + 1;
        for (size_t i = 0; i < items.size(); i++) {
            ui.badge(y, mx, items[i].key);
            ui.put(y, mx + 5, items[i].label, i == sel ? &ui.INV : &ui.N);
            y += 2;
        }
        if (y < rows - 3) {
            std::string who = st().get("teacher").truthy() ? "   //   " + text::upper(field(st(), "teacher")) : "";
            ui.line(y + 1, std::to_string(store_.data.get("lessons").size()) + " LESSONS   //   " + std::to_string(store_.data.get("assessments").size()) +
                               " ASSESSMENTS" + who,
                    &ui.DIM);
        }
        ui.footer("PRESS A NUMBER, OR USE ARROWS + RETURN");
        ui.show(first);
        first = false;
        int k = ui.key();
        if (k == term::K_CLOSED) return;
        std::string c = text::upper(key_text(k));
        const Item *choice = nullptr;
        if (k == term::K_ENTER) choice = &items[sel];
        else if (k == term::K_UP) sel = (sel + items.size() - 1) % items.size();
        else if (k == term::K_DOWN) sel = (sel + 1) % items.size();
        else
            for (const auto &it : items) if (it.key == c) { choice = &it; break; }
        if (choice) {
            ui.echo(choice->key);
            if (!choice->fn) return;
            sel = (size_t)(choice - items.data());
            choice->fn();
            first = true;
        }
    }
}

// ----------------------------------------------------------------------------- lists

std::vector<std::string> App::filtered(const std::string &which, const Filter &f, const std::vector<std::string> &fields) {
    std::vector<const Value *> pool;
    for (const auto &x : store_.data.get(which).arr()) if (x.is_object()) pool.push_back(&x);
    std::string how = st().has(f.sort) ? field(st(), f.sort) : "updated";
    auto xs = store::sort_items(pool, how);
    std::vector<std::string> out;
    std::string q = text::lower(f.q);
    for (const Value *x : xs) {
        if (how == "class" && f.has_cls && !store::in_class(*x, f.cls)) continue;
        if (!f.unit.empty() && text::strip(field(*x, "unit")) != f.unit) continue;
        if (!f.kind.empty() && x->s("kind") != f.kind) continue;
        if (!q.empty()) {
            bool hit = false;
            for (const auto &k : fields)
                if (text::contains(text::lower(x->has(k) ? text_of(x->get(k)) : ""), q)) { hit = true; break; }
            if (!hit) continue;
        }
        out.push_back(x->s("id"));
    }
    return out;
}

static std::string sort_label(const std::string &k) {
    for (const auto &s : store::SORTS) if (s.first == k) return s.second;
    return store::SORTS[0].second;
}

std::string App::filter_info(const Filter &f, size_t count, const std::string &noun) {
    std::vector<std::string> bits = {std::to_string(count) + " " + noun};
    std::string how = st().has(f.sort) ? field(st(), f.sort) : "updated";
    if (how == "class" && f.has_cls) bits.insert(bits.begin(), "CLASS: " + text::upper(f.cls.empty() ? "NO CLASS YET" : f.cls));
    if (!f.unit.empty()) bits.push_back("UNIT: " + f.unit);
    if (!f.kind.empty()) bits.push_back("TYPE: " + f.kind);
    if (!f.q.empty()) bits.push_back("SEARCH: '" + f.q + "'");
    bits.push_back("SORT: " + sort_label(how));
    return text::join(bits, "   ");
}

bool App::filter_key(const std::string &c, Filter &f, const std::string &which) {
    if (c == "/") {
        std::string q;
        if (ui.prompt("SEARCH (BLANK CLEARS)", q, f.q)) f.q = q;
    } else if (c == "u") {
        std::vector<const Value *> pool;
        for (const auto &x : store_.data.get(which).arr()) pool.push_back(&x);
        auto units = store_.units(pool);
        if (units.empty()) {
            ui.msg = "?NOTHING HAS A UNIT YET. FILL IN THE UNIT FIELD TO GROUP THINGS.";
            return true;
        }
        std::vector<std::string> opts = {"(ALL UNITS)"};
        opts.insert(opts.end(), units.begin(), units.end());
        auto at = std::find(units.begin(), units.end(), f.unit);
        long j = ui.choose("SHOW WHICH UNIT?", opts, at != units.end() ? (long)(at - units.begin()) + 1 : 0);
        if (j >= 0) f.unit = j ? units[(size_t)j - 1] : "";
    } else if (c == "t" && f.has_kind) {
        std::vector<std::string> kinds, all = store::ASSESSMENT_KINDS;
        all.insert(all.end(), store::SHEET_KINDS.begin(), store::SHEET_KINDS.end());
        for (const auto &k : all)
            for (const auto &x : store_.data.get(which).arr())
                if (x.s("kind") == k) { kinds.push_back(k); break; }
        std::vector<std::string> opts = {ui.tx("(all types)")};
        for (const auto &k : kinds) opts.push_back(ui.tx(k));
        auto at = std::find(kinds.begin(), kinds.end(), f.kind);
        long j = ui.choose("SHOW WHICH TYPE?", opts, at != kinds.end() ? (long)(at - kinds.begin()) + 1 : 0);
        if (j >= 0) f.kind = j ? kinds[(size_t)j - 1] : "";
    } else if (c == "o") {
        std::string cur = st().has(f.sort) ? field(st(), f.sort) : "updated";
        size_t i = 0;
        bool found = false;
        for (size_t k = 0; k < store::SORTS.size(); k++) if (store::SORTS[k].first == cur) { i = k; found = true; }
        st()[f.sort] = store::SORTS[found ? (i + 1) % store::SORTS.size() : 0].first;
        save();
        ui.msg = "SORTED BY " + sort_label(field(st(), f.sort));
    } else return false;
    return true;
}

void App::class_list(const std::string &title, Filter &f, const std::string &which, const std::function<std::vector<std::string>()> &items,
                     const std::function<UI::Row(const std::string &, int)> &row, const std::function<Result(const std::string &, long)> &on_open,
                     const std::function<Result(int, const std::string &, long)> &on_key, const std::string &noun, const std::function<std::string()> &info,
                     const std::string &hints, const std::string &empty) {
    auto by_class = [&] { return field(st(), f.sort) == "class"; };
    auto picking = [&] { return by_class() && !f.has_cls && f.q.empty(); };
    struct Entry { bool cls; std::string name, id; long count; };
    std::vector<Entry> shown;
    auto refresh = [&] {
        shown.clear();
        if (picking()) {
            std::vector<const Value *> pool;
            for (const auto &x : store_.data.get(which).arr()) pool.push_back(&x);
            for (const auto &c : store::classes(st(), pool)) shown.push_back({true, c.first, "", c.second});
        } else
            for (const auto &id : items()) shown.push_back({false, "", id, 0});
        return (long)shown.size();
    };
    for (;;) {
        std::string h = by_class() && f.has_cls ? text::replace(hints, "ESC BACK", "ESC ALL CLASSES") : hints;
        ui.list_screen(
            title, refresh,
            [&](long i, int w) {
                const Entry &e = shown[(size_t)i];
                if (e.cls) {
                    UI::Row r;
                    r.text = text::upper(e.name.empty() ? "NO CLASS YET" : e.name) + "  (" + std::to_string(e.count) + " " + noun + (e.count == 1 ? "" : "S") + ")";
                    r.has_style = true;
                    r.st = ui.HI;
                    return r;
                }
                return row(e.id, w);
            },
            [&](long i) {
                const Entry &e = shown[(size_t)i];
                if (e.cls) {
                    f.has_cls = true;
                    f.cls = e.name;
                    return Result::move(0);
                }
                return on_open(e.id, i);
            },
            [&](int k, long i) {
                const Entry *e = i >= 0 && i < (long)shown.size() ? &shown[(size_t)i] : nullptr;
                return on_key(k, e && !e->cls ? e->id : std::string(), i);
            },
            h, [&] { return picking() ? std::string("PICK A CLASS. (O CHANGES THE SORT.)") : info(); }, empty);
        if (by_class() && f.has_cls && f.q.empty()) {
            f.has_cls = false;
            f.cls.clear();
            continue;
        }
        return;
    }
}

Result App::rename(const std::string &which, const std::string &id, const std::string &noun, const std::function<std::vector<std::string>()> &items) {
    Value *x = which == "lessons" ? lesson(id) : assessment(id);
    if (!x) return Result::none();
    std::string t;
    if (!ui.prompt("RENAME " + noun, t, field(*x, "title")) || text::strip(t).empty() || text::strip(t) == field(*x, "title")) return Result::none();
    (*x)["title"] = text::strip(t);
    (*x)["updated"] = store::now();
    save();
    ui.msg = "RENAMED.";
    auto xs = items();
    for (size_t j = 0; j < xs.size(); j++)
        if (xs[j] == id) return Result::move((long)j);
    return Result::none();
}

void App::preview(const Value &d) {
    int rows, w, x0;
    ui.dims(rows, w, x0);
    std::vector<std::pair<std::string, Style>> lines;
    for (const auto &l : export_txt::render_lines(d, std::min(78, w - 4))) lines.push_back({l, ui.N});
    ui.view_lines("PREVIEW: " + text_of(d.get("title")), lines);
}

// ----------------------------------------------------------------------------- lessons

std::function<bool(const Date &)> App::lesson_marks() {
    auto days = std::make_shared<std::set<long>>();
    for (const auto &l : store_.data.get("lessons").arr())
        for (const auto &d : store::lesson_dates(l)) days->insert(d.ordinal());
    return [days](const Date &d) { return days->count(d.ordinal()) > 0; };
}

void App::lessons_screen() {
    Filter f;
    f.sort = "lesson_sort";
    auto items = [&] { return filtered("lessons", f, {"title", "unit", "course", "date", "standards"}); };
    auto row = [&](const std::string &id, int w) {
        UI::Row r;
        const Value *l = lesson(id);
        if (!l) return r;
        std::vector<std::string> bits;
        for (const char *k : {"unit", "date"}) if (l->get(k).truthy()) bits.push_back(text_of(l->get(k)));
        std::string right = truncate(text::join(bits, "  "), w / 2);
        std::string t = field(*l, "title");
        std::string left = truncate(t.empty() ? "(untitled lesson)" : t, w - (long)text::length(right) - 2);
        r.text = text::ljust(left, w - (long)text::length(right)) + right;
        r.has_style = true;
        r.st = ui.HI;
        return r;
    };
    auto on_key = [&](int k, const std::string &id, long) -> Result {
        std::string c = text::lower(key_text(k));
        if (c == "w") {
            day_slideshow();
            return Result::none();
        }
        if (c == "y") {
            if (field(st(), "lesson_sort") == "class" && f.has_cls) curriculum_map(&f.cls);
            else curriculum_map(nullptr);
            return Result::none();
        }
        if (c == "n") {
            std::string t;
            if (!ui.prompt("LESSON TITLE", t)) return Result::none();
            Value l = store::new_lesson(st());
            l["title"] = t;
            if (!f.unit.empty()) l["unit"] = f.unit;
            if (field(st(), "lesson_sort") == "class" && f.has_cls && !f.cls.empty()) l["course"] = f.cls;
            std::string nid = l.s("id");
            store_.data["lessons"].push(l);
            save();
            lesson_editor(nid);
            return Result::move(0);
        }
        if (filter_key(c, f, "lessons")) return Result::move(0);
        Value *l = id.empty() ? nullptr : lesson(id);
        if (!l) return Result::none();
        std::string title = field(*l, "title");
        if (c == "d" && ui.confirm("DELETE LESSON '" + (title.empty() ? "untitled" : title) + "'")) {
            auto &arr = store_.data["lessons"].arr_ref();
            for (size_t i = 0; i < arr.size(); i++)
                if (arr[i].s("id") == id) { arr.erase(arr.begin() + (long)i); break; }
            save();
            ui.msg = "DELETED.";
        } else if (c == "r") return rename("lessons", id, "LESSON", items);
        else if (c == "c") {
            Value dup = *l;
            dup["id"] = store::new_id();
            dup["title"] = (title.empty() ? "Lesson" : title) + " (copy)";
            dup["created"] = store::now();
            dup["updated"] = store::now();
            store_.data["lessons"].push(dup);
            save();
            ui.msg = "DUPLICATED. THE COPY IS AT THE TOP.";
            return Result::move(0);
        } else if (c == "x") export_screen("lesson", id);
        else if (c == "b") export_now(id);
        else if (c == "m") export_now(id, "MAKEUP");
        else if (c == "p") preview(doc::lesson_doc(*l, store_));
        return Result::none();
    };
    bool any = store_.data.get("lessons").size() > 0;
    class_list(
        "LESSON PLANS", f, "lessons", items, row,
        [&](const std::string &id, long) {
            lesson_editor(id);
            return Result::none();
        },
        on_key, "LESSON", [&] { return filter_info(f, items().size(), "LESSONS"); },
        "N NEW  R RENAME  C COPY  D DEL  X EXPORT  B BOARD  M MAKE-UP  W WHOLE DAY  Y YEAR MAP  P PREVIEW  / SEARCH  U UNIT  O SORT  ESC BACK",
        any ? "NO LESSONS MATCH. PRESS N TO PLAN ONE." : "NO LESSONS YET. PRESS N TO PLAN ONE.");
}

void App::lesson_editor(const std::string &id) {
    std::vector<store::LessonField> fields;
    auto fields_now = [&] {
        Value *l = lesson(id);
        fields = l ? store::shown_fields(st(), *l) : std::vector<store::LessonField>();
        return (long)fields.size();
    };
    auto at = [&](const std::string &key) -> Result {
        fields_now();
        for (size_t i = 0; i < fields.size(); i++)
            if (fields[i].key == key) return Result::move((long)i);
        return Result::none();
    };
    auto row = [&](long i, int) {
        UI::Row r;
        Value *l = lesson(id);
        if (!l) return r;
        const auto &fd = fields[(size_t)i];
        std::string val;
        if (fd.kind == "standards") {
            std::vector<std::string> cs;
            for (const auto &c : l->get("standards").arr()) cs.push_back(text_of(c));
            val = text::join(cs, ", ");
        } else if (fd.kind == "attached") {
            std::vector<std::string> ts;
            for (const Value *a : store_.attached(*l)) {
                std::string t = field(*a, "title");
                ts.push_back(t.empty() ? "untitled" : t);
            }
            val = text::join(ts, "; ");
        } else {
            val = first_line(field(*l, fd.key));
            long extra = -1;
            for (const auto &ln : text::split(field(*l, fd.key), "\n")) if (!text::strip(ln).empty()) extra++;
            if (!val.empty() && extra > 0) val += "  (+" + std::to_string(extra) + " more)";
        }
        if (val.empty()) val = "-";
        r.text = lpad(ui.tx(fd.label), 38) + " " + val;
        r.has_style = true;
        r.st = val != "-" ? ui.HI : ui.DIM;
        return r;
    };
    auto on_open = [&](long i) -> Result {
        Value *l = lesson(id);
        if (!l) return Result::back();
        store::LessonField fd = fields[(size_t)i];
        std::string v;
        bool got = false;
        if (fd.kind == "line" && fd.key == "date") {   // a calendar (it writes the year), or / to type it
            auto days = store::lesson_dates(*l);
            std::string t = field(*l, "title");
            auto picked = ui.calendar("DATE(S): " + text::upper(t.empty() ? "UNTITLED" : t), days.empty() ? nullptr : &days.front(),
                                      days.empty() ? nullptr : &days.back(), lesson_marks(), true, true);
            if (picked.kind == UI::Picked::TYPE) got = ui.prompt(fd.label, v, field(*lesson(id), fd.key));
            else if (picked.kind == UI::Picked::DAYS) {
                v = dates::fmt_range(picked.first, picked.last);
                got = true;
            }
        } else if (fd.kind == "line") got = ui.prompt(fd.label, v, field(*l, fd.key));
        else if (fd.kind == "text" && fd.key == "bell_ringer") {
            v = field(*l, fd.key);
            got = ui.edit_text(fd.label, v, "", [this](const std::string &cur) { return store_.good_thing(cur); }, "CTRL-G RANDOM GOOD THING",
                               store::GOOD_THINGS_PREFIX);
        } else if (fd.kind == "text") {
            v = field(*l, fd.key);
            got = ui.edit_text(fd.label, v);
        } else if (fd.kind == "vocab") {
            v = field(*l, fd.key);
            got = ui.edit_text(fd.label, v, ui.tx(store::VOCAB_HINT) + ". ESC SAVES. THEN V ON THE LESSON MAKES A QUIZ.");
        } else if (fd.kind == "attached") {
            lesson_attachments(id);
            return Result::none();
        } else {
            std::vector<std::string> cur;
            for (const auto &c : l->get("standards").arr()) cur.push_back(text_of(c));
            std::string g = l->get("grades").truthy() ? field(*l, "grades") : field(st(), "grades");
            auto picked = standards_picker(cur, g);
            if ((l = lesson(id))) {
                Value a = Value::array();
                for (const auto &c : picked) a.push(c);
                (*l)["standards"] = a;
                (*l)["updated"] = store::now();
                save();
            }
            return Result::none();
        }
        if (!got) return Result::none();
        if ((l = lesson(id))) {
            (*l)[fd.key] = v;
            (*l)["updated"] = store::now();
            save();
        }
        fields_now();
        return Result::move(std::min(i + 1, (long)fields.size() - 1));
    };
    auto on_key = [&](int k, long) -> Result {
        Value *l = lesson(id);
        if (!l) return Result::back();
        std::string c = text::lower(key_text(k));
        if (c == "x") export_screen("lesson", id);
        else if (c == "b") export_now(id);
        else if (c == "m") export_now(id, "MAKEUP");
        else if (c == "p") preview(doc::lesson_doc(*l, store_));
        else if (c == "g") {
            std::string cur = text::strip(field(*l, "bell_ringer"));
            if (!cur.empty() && !text::starts_with(cur, store::GOOD_THINGS_PREFIX) && !ui.confirm("REPLACE THE BELL RINGER WITH A GOOD THING"))
                return Result::none();
            l = lesson(id);
            (*l)["bell_ringer"] = store_.good_thing(cur);
            (*l)["updated"] = store::now();
            save();
            ui.msg = "GOOD THINGS BELL RINGER ADDED. PRESS G AGAIN FOR ANOTHER.";
            return at("bell_ringer");
        } else if (c == "v") {
            Value *a = store_.vocab_quiz(*l);
            if (!a) {
                ui.msg = "?TYPE AT LEAST TWO WORDS WITH DEFINITIONS IN VOCABULARY FIRST (WORD: DEFINITION)";
                return at("vocab");
            }
            save();
            ui.msg = "MADE '" + field(*a, "title") + "' (" + std::to_string(a->get("questions").at(0).get("pairs").size()) +
                     " WORDS) AND LINKED IT TO THIS LESSON";
            return at("assessments");
        } else if (c == "s") {
            lesson_sections();
            return Result::move(0);
        } else if (c == "h") {
            std::string t = field(*l, "title"), name;
            if (!ui.prompt("ANNOTATION SHEET HEADING (E.G. HAMLET 4.1 ANNOTATION)", name, t.empty() ? "" : t + " Annotation") || name.empty())
                return Result::none();
            std::string aid = add_assessment(store::ANNOTATION, name, id);
            l = lesson(id);
            std::string hw = text::rstrip(field(*l, "homework"));
            (*l)["homework"] = (hw.empty() ? "" : hw + "\n") + "- Complete the annotation sheet: " + name;
            (*l)["updated"] = store::now();
            save();
            Value *a = assessment(aid);
            ui.msg = "ADDED '" + (a ? field(*a, "title") : name) + "' TO HOMEWORK. IT EXPORTS WITH THE LESSON (X, ALL FORMATS).";
            return at("homework");
        }
        return Result::none();
    };
    Value *l0 = lesson(id);
    std::string t0 = l0 ? field(*l0, "title") : "";
    ui.list_screen(
        "LESSON: " + (t0.empty() ? "untitled" : t0), fields_now, row, on_open, on_key,
        "1-0/RETURN EDIT  G GOOD THING  H ANNOTATION HW  V VOCAB QUIZ  S SECTIONS  P PREVIEW  X EXPORT  B BOARD  M MAKE-UP  ESC BACK", [&] {
            Value *l = lesson(id);
            std::string t = l ? field(*l, "title") : "";
            return "EDITING '" + (t.empty() ? "untitled" : t) + "'   LAST SAVED " + (l ? field(*l, "updated") : "");
        });
}

void App::lesson_sections() {
    std::vector<store::LessonField> keys;
    for (const auto &fd : store::LESSON_FIELDS)
        if (std::find(store::FIXED_FIELDS.begin(), store::FIXED_FIELDS.end(), fd.key) == store::FIXED_FIELDS.end()) keys.push_back(fd);
    auto hidden = [&](const std::string &k) {
        for (const auto &h : st().get("lesson_hide").arr()) if (h.is_string() && h.str() == k) return true;
        return false;
    };
    ui.list_screen(
        "LESSON SECTIONS", [&] { return (long)keys.size(); },
        [&](long i, int) {
            bool on = !hidden(keys[(size_t)i].key);
            UI::Row r;
            r.text = std::string("[") + (on ? "X" : " ") + "] " + ui.tx(keys[(size_t)i].label);
            r.has_style = true;
            r.st = on ? ui.HI : ui.DIM;
            return r;
        },
        [&](long i) {
            std::string k = keys[(size_t)i].key;
            Value hide = Value::array();
            bool was = hidden(k);
            for (const auto &h : st().get("lesson_hide").arr())
                if (!(h.is_string() && h.str() == k)) hide.push(h);
            if (!was) hide.push(k);
            st()["lesson_hide"] = hide;
            save();
            return Result::move(i);
        },
        nullptr, "1-0/RETURN SHOW OR HIDE  ESC BACK", [] { return std::string("UNCHECKED SECTIONS ARE HIDDEN WHILE EMPTY. NOTHING IS DELETED."); });
}

void App::lesson_attachments(const std::string &lesson_id) {
    std::vector<std::string> ids;
    auto items = [&] {
        ids.clear();
        if (Value *l = lesson(lesson_id))
            for (const Value *a : store_.attached(*l)) ids.push_back(a->s("id"));
        return (long)ids.size();
    };
    auto touch = [&] {
        if (Value *l = lesson(lesson_id)) {
            (*l)["updated"] = store::now();
            save();
        }
    };
    auto set_ids = [&](const std::vector<std::string> &xs) {
        if (Value *l = lesson(lesson_id)) {
            Value a = Value::array();
            for (const auto &x : xs) a.push(x);
            (*l)["assessments"] = a;
        }
    };
    auto on_key = [&](int k, long i) -> Result {
        std::string c = text::lower(key_text(k));
        items();
        set_ids(ids);   // drop links to deleted ones
        if (c == "n") {
            std::string aid = new_assessment_flow(lesson_id);
            if (!aid.empty()) {
                touch();
                open_assessment(aid);
                items();
                return Result::move((long)ids.size() - 1);
            }
            return Result::none();
        }
        if (c == "l") {
            Value *l = lesson(lesson_id);
            std::string unit = l ? text::strip(field(*l, "unit")) : "";
            std::vector<const Value *> all;
            for (const auto &x : store_.data.get("assessments").arr()) all.push_back(&x);
            std::vector<const Value *> pool;
            for (const Value *x : store::sort_items(all, "updated"))
                if (std::find(ids.begin(), ids.end(), x->s("id")) == ids.end()) pool.push_back(x);
            std::stable_sort(pool.begin(), pool.end(), [&](const Value *a, const Value *b) {
                bool ka = unit.empty() || text::strip(field(*a, "unit")) != unit, kb = unit.empty() || text::strip(field(*b, "unit")) != unit;
                return !ka && kb;   // this unit first
            });
            if (pool.empty()) {
                ui.msg = "?NOTHING ELSE TO LINK. PRESS N TO BUILD ONE.";
                return Result::none();
            }
            std::vector<std::string> opts;
            for (const Value *x : pool) {
                std::string t = field(*x, "title"), u = text::strip(field(*x, "unit"));
                opts.push_back(lpad(field(*x, "kind"), 18) + (t.empty() ? "(untitled)" : t) + (u.empty() ? "" : "   [" + field(*x, "unit") + "]"));
            }
            long j = ui.choose("LINK WHICH ONE TO THIS LESSON?", opts);
            if (j >= 0) {
                ids.push_back(pool[(size_t)j]->s("id"));
                set_ids(ids);
                touch();
                ui.msg = "LINKED.";
                return Result::move((long)ids.size() - 1);
            }
            return Result::none();
        }
        if (i < 0 || i >= (long)ids.size()) return Result::none();
        Value *a = assessment(ids[(size_t)i]);
        if (!a) return Result::none();
        std::string t = field(*a, "title");
        if (c == "r" && ui.confirm("REMOVE '" + (t.empty() ? "untitled" : t) + "' FROM THIS LESSON (IT IS NOT DELETED)")) {
            ids.erase(ids.begin() + i);
            set_ids(ids);
            touch();
            return Result::move(std::max(0L, i - 1));
        }
        if (c == "+" || c == "=" || c == "-" || c == "_") {
            long j = (c == "+" || c == "=") ? i + 1 : i - 1;
            if (j >= 0 && j < (long)ids.size()) {
                std::swap(ids[(size_t)i], ids[(size_t)j]);
                set_ids(ids);
                touch();
                return Result::move(j);
            }
        } else if (c == "e") {
            std::string kind = field(*a, "kind");
            return rename("assessments", ids[(size_t)i], text::upper(kind.empty() ? "item" : kind), [&] {
                items();
                return ids;
            });
        } else if (c == "x") export_screen("assessment", ids[(size_t)i]);
        else if (c == "p") preview(doc::assessment_doc(*a, store_));
        return Result::none();
    };
    Value *l0 = lesson(lesson_id);
    std::string t0 = l0 ? field(*l0, "title") : "";
    ui.list_screen(
        "WORKSHEETS: " + (t0.empty() ? "untitled" : t0), items,
        [&](long i, int w) {
            Value *a = assessment(ids[(size_t)i]);
            return a ? assessment_row(*a, w) : UI::Row();
        },
        [&](long i) {
            open_assessment(ids[(size_t)i]);
            return Result::none();
        },
        on_key, "1-0 OPEN  N NEW  L LINK EXISTING  E RENAME  R REMOVE  +/- MOVE  P PREVIEW  X EXPORT  ESC BACK",
        [&] { return std::to_string(ids.size()) + " LINKED. EXPORT ALL ON THE LESSON PUTS THEM IN ITS FOLDER."; },
        "NOTHING YET. N BUILDS A QUIZ, WORKSHEET, ANNOTATION OR BELL RINGER SHEET; L LINKS ONE YOU MADE.");
}

// ----------------------------------------------------------------------------- the terminal

int run_terminal(bool no_boot) {
    std::unique_ptr<term::Screen> screen;
    try {
        screen = term::open_terminal();
    } catch (std::exception &) {
        std::fprintf(stderr, "Chalkboard's terminal app needs a terminal. Start it from one, or open the Chalkboard window app.\n");
        return 1;
    }
    store::Store st;
    {
        App app(*screen, st, no_boot);
        app.run();
    }
    screen.reset();
    std::printf("]BYE.\n");
    return 0;
}

}  // namespace tui
