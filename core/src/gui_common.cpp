// Pieces the screens share (port of gui/common.py): filterable item lists, autosaving fields, and
// the standards field.
#include <algorithm>
#include <cmath>

#include "gui.h"
#include "markup.h"
#include "text.h"

namespace gui {

using namespace ui;

const std::vector<std::pair<std::string, std::string>> SECTIONS = {
    {"lessons", "Lesson Plans"}, {"assessments", "Assessments"}, {"standards", "Standards Library"}, {"settings", "Settings"}, {"calendar", "Calendar"}};

const std::vector<std::pair<std::string, std::string>> SORT_LABEL = {
    {"scheduled", "Date scheduled"}, {"updated", "Date modified"}, {"created", "Date created"}, {"title", "Title (A-Z)"}, {"unit", "Unit"}};

// the Sort by choices for a list: only lessons have days they're taught
static std::vector<std::pair<std::string, std::string>> sorts_for(const std::string &kind) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &s : SORT_LABEL)
        if (kind == "lesson" || s.first != "scheduled") out.push_back(s);
    return out;
}

Factory section_factory(const std::string &key) {
    if (key == "lessons") return lesson_list();
    if (key == "assessments") return assessment_list();
    if (key == "standards") return standards_library();
    if (key == "calendar") return calendar_page();
    if (key == "homedays") return home_days_page();
    if (key.empty()) return make_home();
    return settings_view();
}

std::string grade_label(const std::string &g) {
    if (g == store::ALL || g.empty()) return "All grades";
    if (g == "K") return "Kindergarten";
    for (const auto &p : text::split(g, "-")) {
        if (p.empty()) return g;
        for (char c : p)
            if (!(std::isdigit((unsigned char)c) || c == 'K')) return g;
    }
    return g.find('-') != std::string::npos ? "Grades " + g : "Grade " + g;
}

std::string first_line(const std::string &s) {
    for (const auto &line : text::split(s, "\n"))
        if (!text::strip(line).empty()) return markup::plain(text::strip(line));
    return "";
}

std::string commas(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return (n < 0 ? "-" : "") + out;
}

std::string plural(long long n, const std::string &word, const std::string &many) {
    return commas(n) + " " + (n == 1 ? word : (many.empty() ? word + "s" : many));
}

std::vector<MenuEntry> entries(const std::vector<std::string> &labels) {
    std::vector<MenuEntry> out;
    for (const auto &l : labels) {
        MenuEntry e;
        e.label = l;
        out.push_back(e);
    }
    return out;
}

void label_mid(Ui &ui, const std::string &text, bool bold, bool dim) {
    Skin &k = ui.sk();
    Font &f = bold ? *k.fb : *k.f;
    Rect r = ui.place(std::ceil(f.width(text)) + 1, ui.ctl_h());
    if (ui.drawing) ui.text_in(f, r, text, dim ? k["dim"] : k["text"]);
}

// ----------------------------------------------------------------------------- fields

bool line_field(App &app, Ui &ui, const std::string &key, Value &obj, const std::string &field, double width) {
    std::string v = obj.get(field).is_null() ? std::string() : store::text_of(obj.get(field));   // (missing: empty, not "None")
    if (!entry(ui, key, v, width)) return false;
    if (obj.s(field) != v || !obj.get(field).is_string()) {
        obj[field] = v;
        app.touch(obj);
    }
    return true;
}

bool auto_text(App &app, Ui &ui, const std::string &key, Value &obj, const std::string &field, int min_lines, int max_lines) {
    std::string v = obj.get(field).is_null() ? std::string() : store::text_of(obj.get(field));   // (missing: empty, not "None")
    if (!text_area(ui, key, v, min_lines, max_lines)) return false;
    if (obj.s(field) != v || !obj.get(field).is_string()) {
        obj[field] = v;
        app.touch(obj);
    }
    return true;
}

void standards_field(App &app, Ui &ui, const std::string &key, const std::function<Value *()> &find, const std::string &field,
                     const std::function<std::string()> &grades) {
    Skin &k = ui.sk();
    int S = ui.S();
    Value *obj = find();
    if (!obj) return;
    ui.push_id(key);
    std::string &code = ui.state<std::string>(ui.id("code"));
    auto codes = [&]() -> std::vector<std::string> {
        std::vector<std::string> out;
        Value *o = find();
        if (!o) return out;
        for (const auto &c : (*o)[field].arr())
            if (c.is_string()) out.push_back(c.str());
        return out;
    };
    auto set_codes = [&](const std::vector<std::string> &cs) {
        Value *o = find();
        if (!o) return;
        Value a = Value::array();
        for (const auto &c : cs) a.push(c);
        (*o)[field] = a;
        app.touch(*o);
        ui.again();
    };
    auto add_code = [&] {
        std::string raw = text::strip(code);
        if (raw.empty()) return;
        std::string found;
        if (app.store.std_index.count(raw)) found = raw;
        else
            for (const auto &p : app.store.std_index)
                if (text::lower(p.first) == text::lower(raw) && (found.empty() || p.first < found)) found = p.first;
        if (found.empty()) {
            app.status("No standard \xE2\x80\x9C" + raw + "\xE2\x80\x9D in the library. Check the code (like RL.9-10.1).");
            return;
        }
        auto cs = codes();
        if (std::find(cs.begin(), cs.end(), found) == cs.end()) {
            cs.push_back(found);
            set_codes(cs);
        }
        code.clear();
    };
    ui.flow();
    if (button(ui, "Choose Standards\xE2\x80\xA6")) {
        std::string g;
        if (grades) g = grades();
        else {
            g = obj->s("grades");
            if (g.empty()) g = app.settings().s("grades");
        }
        std::vector<std::string> sel;
        auto cur = codes();
        if (pick_standards(app, cur, g, sel) && sel != cur) set_codes(sel);
    }
    ui.place(6 * S, 1);
    // "or add a code:", the box, and Add stay together when the row wraps
    double ew = 16 * k.f->width("0") + 20 * S, aw = ui.btn_w("Add", true);
    Rect grp = ui.place(k.f->width("or add a code:") + 6 * S + ew + 6 * S + aw, ui.ctl_h());
    if (ui.drawing) ui.text_in(*k.f, grp, "or add a code:", k["text"]);
    Id eid = ui.id("entry:code");
    bool enter = ui.focused(eid) && ui.has(plat::Event::KEY) && ui.ev.key == plat::K_RETURN && !ui.ev.mods;
    if (enter) {
        ui.consume();
        add_code();
    }
    double ex = grp.x + k.f->width("or add a code:") + 6 * S;
    entry_at(ui, eid, {ex, grp.y, ew, grp.h}, code);
    if (button_at(ui, ui.id("btn:Add"), {ex + ew + 6 * S, grp.y + 3 * S, aw, grp.h - 6 * S}, "Add", true, false, true)) add_code();
    ui.flow(false);
    ui.space(6 * S);
    auto cs = codes();
    if (cs.empty()) label(ui, "None yet.", false, true);
    double cw = 0;
    for (const auto &c : cs) cw = std::max(cw, k.fb->width(c));
    cw = std::min(cw, 160.0 * S);
    for (size_t i = 0; i < cs.size(); i++) {
        ui.push_id((Id)i + 1);
        double rm = ui.btn_w("Remove", true);
        std::string t = app.store.std_text(cs[i]);
        if (t.empty()) t = "(not in the standards library)";
        auto remove = [&](const Rect &c) {
            if (button_at(ui, ui.id("remove"), {c.r() - rm, c.y + S, rm, ui.ctl_h() - 6 * S}, "Remove", true, false, true)) {
                auto now = codes();
                now.erase(std::remove(now.begin(), now.end(), cs[i]), now.end());
                set_codes(now);
            }
        };
        if (ui.width() - cw - rm - 20 * S < 220 * S) {
            // a narrow column: the code and Remove on one line, the standard's words under them
            ui.row({-1, rm}, ui.ctl_h());
            Rect a = ui.place(0, ui.ctl_h());
            if (ui.drawing) ui.text_in(*k.fb, a, cs[i], k["text"]);
            remove(ui.place(0, ui.ctl_h()));
            ui.row({});
            para(ui, t);
            ui.space(8 * S);
            ui.pop_id();
            continue;
        }
        double tw = ui.width() - cw - 10 * S - rm - 10 * S;
        auto lines = ui.wrap(*k.f, t, tw);
        double h = std::max(ui.ctl_h(), lines.size() * k.f->line) + 2 * S;
        ui.row({cw + 10 * S, tw, -1}, h);
        Rect a = ui.place(0, h);
        if (ui.drawing) ui.text(*k.fb, a.x, a.y + S + k.fb->ascent, cs[i], k["text"]);
        Rect b = ui.place(0, h);
        if (ui.drawing)
            for (size_t j = 0; j < lines.size(); j++) ui.text(*k.f, b.x, b.y + S + j * k.f->line + k.f->ascent, lines[j], k["text"]);
        remove(ui.place(0, h));
        ui.pop_id();
    }
    ui.row({});
    ui.pop_id();
}

// ----------------------------------------------------------------------------- item lists

ItemList::ItemList(App &a, const std::string &kind, const std::string &pool_key, const std::string &sort_key, const std::string &noun)
    : Screen(a), kind_(kind), pool_key_(pool_key), sort_key_(sort_key), noun_(noun) {
    sort_ = a.settings().s(sort_key);
    if (sort_ == "class") sort_ = kind == "lesson" ? "scheduled" : "unit";   // (By Class was a sort; the class is a filter now)
    bool ok = false;
    for (const auto &s : sorts_for(kind)) ok |= s.first == sort_;
    if (!ok) sort_ = kind == "lesson" ? "scheduled" : "updated";
    // the class filter: as it was left (null: all classes; "": lessons with no class yet)
    const Value &c = a.settings().get(class_key());
    has_cls_ = c.is_string();
    cls_ = c.str();
}

Value &ItemList::pool() { return app.store.data[pool_key_]; }

Value *ItemList::selected() {
    if (sel_id_.empty()) return nullptr;
    if (std::find(ids_.begin(), ids_.end(), sel_id_) == ids_.end()) return nullptr;
    for (auto &x : pool().arr_ref())
        if (x.is_object() && x.s("id") == sel_id_) return &x;
    return nullptr;
}


void ItemList::open_class(bool has, const std::string &name) {
    has_cls_ = has;
    cls_ = name;
    app.settings()[class_key()] = has ? Value(name) : Value();
    app.save_soon();
    focus_list_ = true;
    app.ui.again();
}

std::string ItemList::new_class() { return has_cls_ ? cls_ : ""; }

std::vector<std::string> ItemList::items() {
    std::vector<const Value *> all;
    for (const auto &x : pool().arr())
        if (x.is_object()) all.push_back(&x);
    auto xs = store::sort_items(all, sort_);
    std::vector<std::string> out;
    std::string q = text::lower(text::strip(q_));
    for (const Value *x : xs) {
        if (has_cls_ && !store::in_class(*x, cls_)) continue;
        if (!unit_.empty() && text::strip(x->s("unit")) != unit_) continue;
        if (!q.empty()) {
            bool hit = false;
            for (const auto &f : search_fields_) {
                const Value &v = x->get(f);
                std::string s;
                if (v.is_array()) {   // str() of a list, as Python searched it
                    std::vector<std::string> parts;
                    for (const auto &e : v.arr()) parts.push_back("'" + store::text_of(e) + "'");
                    s = "[" + text::join(parts, ", ") + "]";
                } else
                    s = store::text_of(v);
                if (text::contains(text::lower(s), q)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) continue;
        }
        if (!match(*x)) continue;
        out.push_back(x->s("id"));
    }
    return out;
}

bool ItemList::tool_sel(Ui &ui, const std::string &text) { return tool(ui, text, selected() != nullptr); }

bool ItemList::escape() {
    if (!q_.empty()) {
        q_.clear();
        app.ui.again();
        return true;
    }
    return false;
}

bool ItemList::command(const std::string &name) {
    if (name == "new") cmd_new();
    else if (name == "open") cmd_open();
    else if (name == "preview") cmd_preview();
    else if (name == "export") cmd_export();
    else if (name == "duplicate") cmd_duplicate();
    else if (name == "find") {
        find_ = true;
        app.ui.again();
    }
    else return false;
    return true;
}


void ItemList::frame(Ui &ui, const Rect &body) {
    Skin &k = ui.sk();
    int S = ui.S();
    ui.begin(body, 0);
    begin_toolbar(ui);
    tools(ui);
    end_toolbar(ui);
    // the filters: each label with its box, wrapping to a second row in a narrow window
    std::vector<const Value *> all;
    for (const auto &x : pool().arr())
        if (x.is_object()) all.push_back(&x);
    Rect top = ui.remaining();
    ui.begin({top.x + 8 * S, top.y, top.w - 16 * S, 1e9}, 0);
    ui.flow();
    auto dw = [&](double chars) { return std::ceil(std::min(k.f->width("0") * chars, 520.0 * S) + k.line + 16 * S + (k.soft ? 6 * S : 0)); };
    auto pair = [&](const std::string &label, double w, const std::function<void()> &fn) {
        Rect r = ui.place(k.f->width(label) + 6 * S + w + 14 * S, ui.ctl_h() + (k.soft ? 2 * S : 0));
        ui.begin(r, 0);
        ui.flow();
        label_mid(ui, label);
        ui.place(6 * S, 1);
        fn();
        ui.end();
    };
    double ew = 18 * k.f->width("0") + 20 * S;
    pair("Search:", ew, [&] {
        Id sid = ui.id("entry:search");
        if (find_) {
            ui.focus(sid);
            find_ = false;
        }
        if (ui.focused(sid) && (ui.key(plat::K_DOWN) || ui.key(plat::K_RETURN))) focus_list_ = true;
        entry(ui, "search", q_, ew);
    });
    pair("Class:", dw(16), [&] {
        std::vector<std::pair<std::string, std::string>> copts = {{"\x01all", "All classes"}};
        bool loose = false;
        for (const auto &c : store::classes(app.settings(), all))
            if (c.first.empty()) loose |= c.second > 0;
            else copts.push_back({c.first, c.first});
        if (loose) copts.push_back({"\x01none", "No class yet"});
        std::string pick = !has_cls_ ? "\x01all" : cls_.empty() ? "\x01none" : cls_;
        if (dropdown(ui, "class", copts, pick, 16)) {
            if (pick == "\x01all") open_class(false, "");
            else open_class(true, pick == "\x01none" ? "" : pick);
        }
    });
    pair("Unit:", dw(14), [&] {
        auto units = app.store.units(all);
        std::vector<std::pair<std::string, std::string>> uopts = {{"", "All units"}};
        for (const auto &u : units) uopts.push_back({u, u});
        if (!unit_.empty() && std::find(units.begin(), units.end(), unit_) == units.end()) unit_.clear();
        dropdown(ui, "unit", uopts, unit_, 14);
    });
    if (double fw = filters_w(ui)) {
        Rect r = ui.place(fw, ui.ctl_h() + (k.soft ? 2 * S : 0));
        ui.begin(r, 0);
        ui.flow();
        filters(ui);
        ui.end();
    }
    pair("Sort by:", dw(14), [&] {
        if (dropdown(ui, "sort", sorts_for(kind_), sort_, 14)) {
            app.settings()[sort_key_] = sort_;
            app.save();
        }
    });
    Rect used = ui.end();
    ui.cut_top(used.h + 6 * S);
    Rect rest = ui.remaining();
    ui.end();
    rest = {rest.x + 8 * S, rest.y, rest.w - 16 * S, rest.h - 8 * S};
    ids_ = items();
    std::vector<std::vector<std::string>> rows;
    sel_ = -1;
    for (size_t i = 0; i < ids_.size(); i++) {
        for (const auto &x : pool().arr())
            if (x.is_object() && x.s("id") == ids_[i]) {
                rows.push_back(row(x));
                break;
            }
        if (ids_[i] == sel_id_) sel_ = (int)i;
    }
    std::string empty = pool().size() ? "Nothing matches. Clear the search or filters to see every " + noun_ + "."
                                      : "No " + noun_ + "s yet. Click New to make your first one.";
    int before = sel_;
    if (focus_list_) {
        ui.focus(ui.id("list:items"));
        focus_list_ = false;
    }
    ListResult lr = list_view(ui, "items", rest, columns_, rows, sel_, empty);
    if (sel_ != before || lr.changed) {
        sel_id_ = sel_ >= 0 && sel_ < (int)ids_.size() ? ids_[(size_t)sel_] : "";
        ui.again();
    }
    size_t n = pool().size();
    app.status(ids_.size() != n ? std::to_string(ids_.size()) + " of " + std::to_string(n) + " " + noun_ + "s"
                                : std::to_string(n) + " " + noun_ + (n != 1 ? "s" : ""));
    if (lr.open) cmd_open();
    if (lr.del) cmd_delete();
    if (lr.menu && selected()) {
        auto items = menu_items();
        std::vector<MenuEntry> es;
        for (const auto &it : items) {
            MenuEntry e;
            e.label = it.first;
            es.push_back(e);
        }
        int got = popup_menu(ui, lr.menu_x, lr.menu_y, es);
        if (got >= 0 && items[(size_t)got].second) items[(size_t)got].second();
    }
}

void ItemList::cmd_rename() {
    Value *x = selected();
    if (!x) return;
    std::string old = x->s("title"), t = old;
    if (ask(app.ui, "Rename", "New name for \xE2\x80\x9C" + (old.empty() ? std::string("untitled") : old) + "\xE2\x80\x9D:", t) && !text::strip(t).empty() &&
        text::strip(t) != old) {
        x = selected();
        if (!x) return;
        (*x)["title"] = text::strip(t);
        (*x)["updated"] = store::now();
        app.save();
    }
}

void ItemList::cmd_duplicate() {
    Value *x = selected();
    if (!x) return;
    Value dup = *x;
    std::string t = x->s("title");
    if (t.empty()) t = text::title(noun_);
    dup["id"] = store::new_id();
    dup["title"] = t + " (copy)";
    dup["created"] = store::now();
    dup["updated"] = store::now();
    std::string id = dup.s("id");
    pool().push(std::move(dup));
    app.save();
    sel_id_ = id;
    app.status("Made a copy. It's selected.");
}

}  // namespace gui
