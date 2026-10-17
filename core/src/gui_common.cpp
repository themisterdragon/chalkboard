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
    {"updated", "Date modified"}, {"created", "Date created"}, {"title", "Title (A-Z)"}, {"unit", "Unit"}, {"class", "By class"}};

Factory section_factory(const std::string &key) {
    if (key == "lessons") return lesson_list();
    if (key == "assessments") return assessment_list();
    if (key == "standards") return standards_library();
    if (key == "calendar") return calendar_page();
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
    std::string v = store::text_of(obj.get(field));
    if (!entry(ui, key, v, width)) return false;
    if (obj.s(field) != v || !obj.get(field).is_string()) {
        obj[field] = v;
        app.touch(obj);
    }
    return true;
}

bool auto_text(App &app, Ui &ui, const std::string &key, Value &obj, const std::string &field, int min_lines, int max_lines) {
    std::string v = store::text_of(obj.get(field));
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
    bool ok = false;
    for (const auto &s : SORT_LABEL) ok |= s.first == sort_;
    if (!ok) sort_ = "updated";
    // By Class: come back to the same class
    if (kind == "lesson") {
        has_cls_ = a.has_class_lesson;
        cls_ = a.open_class_lesson;
    } else {
        has_cls_ = a.has_class_assessment;
        cls_ = a.open_class_assessment;
    }
}

Value &ItemList::pool() { return app.store.data[pool_key_]; }

Value *ItemList::selected() {
    if (sel_id_.empty()) return nullptr;
    if (std::find(ids_.begin(), ids_.end(), sel_id_) == ids_.end()) return nullptr;
    for (auto &x : pool().arr_ref())
        if (x.is_object() && x.s("id") == sel_id_) return &x;
    return nullptr;
}

bool ItemList::picking() const { return by_class() && !has_cls_ && text::strip(q_).empty(); }

void ItemList::open_class(bool has, const std::string &name) {
    has_cls_ = has;
    cls_ = name;
    if (kind_ == "lesson") {
        app.has_class_lesson = has;
        app.open_class_lesson = name;
    } else {
        app.has_class_assessment = has;
        app.open_class_assessment = name;
    }
    focus_list_ = true;
    app.ui.again();
}

std::string ItemList::new_class() { return by_class() && has_cls_ ? cls_ : ""; }

std::vector<std::string> ItemList::items() {
    std::vector<const Value *> all;
    for (const auto &x : pool().arr())
        if (x.is_object()) all.push_back(&x);
    auto xs = store::sort_items(all, sort_);
    std::vector<std::string> out;
    std::string q = text::lower(text::strip(q_));
    for (const Value *x : xs) {
        if (by_class() && has_cls_ && !picking() && !store::in_class(*x, cls_)) continue;
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
    if (by_class() && has_cls_) {
        open_class(false, "");
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

void ItemList::picker(Ui &ui, const Rect &r) {
    Skin &k = ui.sk();
    int S = ui.S();
    std::vector<const Value *> all;
    for (const auto &x : pool().arr())
        if (x.is_object()) all.push_back(&x);
    auto found = store::classes(app.settings(), all);
    ui.begin_scroll(ui.id("picker"), r);
    ui.space(4 * S);
    para(ui, found.empty() ? "No " + noun_ + "s yet. Click New to make your first one." : "Pick a class to see its " + noun_ + "s and add new ones.", false,
         600 * S);
    ui.space(10 * S);
    // a card for each class: its color, its name, how many it has
    double gap = 14 * S, ch = k.fhead->line + k.f->line + 34 * S;
    int cols = std::max(1, (int)((ui.width() + gap) / (240 * S + gap)));
    double cw = std::min(340.0 * S, (ui.width() - (cols - 1) * gap) / cols);
    Rect row;
    for (size_t i = 0; i < found.size(); i++) {
        const auto &f = found[i];
        if (i % (size_t)cols == 0) {
            if (i) ui.space(gap);
            row = ui.place(0, ch);
        }
        Rect c{row.x + (double)(i % (size_t)cols) * (cw + gap), row.y, cw, ch};
        std::string name = f.first.empty() ? "No class yet" : f.first;
        Id id = ui.id("class:" + name);
        if (focus_list_ && i == 0) {   // the keyboard starts on the first class
            ui.focus(id);
            focus_list_ = false;
        }
        bool go = ui.hit(id, c, true, true);
        bool focus = ui.focused(id);
        if (focus && (ui.key(plat::K_RETURN) || ui.key(' '))) go = true;
        bool hot = ui.over(c);
        if (hot) ui.cursor(plat::HAND);
        if (ui.drawing) {
            Color stripe = class_color(k, f.first);
            if (k.soft) {
                ui.shadow(c, 12 * S, hot ? 10 * S : 6 * S, k.dark ? 0.35 : (hot ? 0.12 : 0.07));
                ui.rrect(c, 12 * S, hot ? mix(k["card"], k["text"], 0.03) : k["card"], k["line"], S);
                ui.rrect({c.x + 14 * S, c.y + 16 * S, 5.0 * S, c.h - 32 * S}, 2.5 * S, stripe, {}, 0, true, false);
            } else {
                ui.fill(c, hot ? k["selsoft"] : k["card"]);
                ui.box(c, k["dark"], S);
                ui.fill({c.x + 8 * S, c.y + 12 * S, 5.0 * S, c.h - 24 * S}, stripe);
            }
            Color fg = !k.soft && hot && hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["text"];
            Color dim = !k.soft && hot && hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["dim"];
            double tx = c.x + 32 * S, tw = c.w - 44 * S, ty = c.y + (c.h - k.fhead->line - k.f->line - 2 * S) / 2;
            ui.text(*k.fhead, tx, std::round(ty + k.fhead->ascent), ui.elide(*k.fhead, name, tw), fg);
            ui.text(*k.f, tx, std::round(ty + k.fhead->line + 2 * S + k.f->ascent), plural(f.second, noun_), dim);
            if (focus) {
                if (k.soft) ui.rrect(c.in(-3 * S), 14 * S, {}, k["focus"], 2 * S, false, true);
                else ui.dotted(c.in(3 * S), k["focus"]);
            }
        }
        if (go) open_class(true, f.first);
    }
    ui.end_scroll();
    (void)k;
    app.status(std::to_string(found.size()) + " class" + (found.size() != 1 ? "es" : ""));
}

void ItemList::frame(Ui &ui, const Rect &body) {
    Skin &k = ui.sk();
    int S = ui.S();
    ui.begin(body, 0);
    begin_toolbar(ui);
    tools(ui);
    end_toolbar(ui);
    // the filters
    Rect filt = ui.cut_top(ui.ctl_h() + 6 * S);
    ui.begin({filt.x + 8 * S, filt.y, filt.w - 16 * S, ui.ctl_h()}, 0);
    ui.flow();
    label_mid(ui, "Search:");
    ui.place(6 * S, 1);
    Id sid = ui.id("entry:search");
    if (find_) {
        ui.focus(sid);
        find_ = false;
    }
    if (ui.focused(sid) && (ui.key(plat::K_DOWN) || ui.key(plat::K_RETURN))) focus_list_ = true;
    entry(ui, "search", q_, 22 * k.f->width("0") + 20 * S);
    ui.place(14 * S, 1);
    label_mid(ui, "Unit:");
    ui.place(6 * S, 1);
    std::vector<const Value *> all;
    for (const auto &x : pool().arr())
        if (x.is_object()) all.push_back(&x);
    auto units = app.store.units(all);
    std::vector<std::pair<std::string, std::string>> uopts = {{"", "All units"}};
    for (const auto &u : units) uopts.push_back({u, u});
    if (!unit_.empty() && std::find(units.begin(), units.end(), unit_) == units.end()) unit_.clear();
    dropdown(ui, "unit", uopts, unit_, 14);
    ui.place(14 * S, 1);
    filters(ui);
    label_mid(ui, "Sort by:");
    ui.place(6 * S, 1);
    if (dropdown(ui, "sort", SORT_LABEL, sort_, 14)) {
        app.settings()[sort_key_] = sort_;
        app.save();
    }
    ui.end();
    Rect rest = ui.remaining();
    ui.end();
    rest = {rest.x + 8 * S, rest.y, rest.w - 16 * S, rest.h - 8 * S};
    if (picking()) {
        picker(ui, rest);
        return;
    }
    if (by_class() && has_cls_ && text::strip(q_).empty()) {
        // "◄ All Classes" over the list inside a class
        ui.begin({rest.x, rest.y, rest.w, ui.ctl_h()}, 0);
        ui.flow();
        if (button(ui, "\xE2\x97\x84 All Classes", true, false, true)) open_class(false, "");
        ui.place(10 * S, 1);
        label_mid(ui, cls_.empty() ? "No class yet" : cls_, true);
        ui.end();
        rest.y += ui.ctl_h() + 6 * S;
        rest.h -= ui.ctl_h() + 6 * S;
    }
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
