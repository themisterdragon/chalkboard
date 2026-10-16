// The Board Designer (port of gui/board.py): fonts and where things go on a board slide, with a
// live preview. It changes the same settings as the terminal app's Board Designer, so a slide
// designed in one looks the same from the other.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

#include "board.h"
#include "doc.h"
#include "fontlib.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

namespace {

const char *KEYS[] = {"board_font", "board_head_font", "board_layout", "board_panels", "board_title_align", "board_codes_place",
                      "board_big_text", "board_sides", "board_order", "board_sections", "board_std_text"};

std::string font_text(const std::string &name, bool heading) {
    if (heading && name.empty()) return "Same as the text font";
    return fontlib::label(name);
}

const std::vector<std::pair<std::string, std::string>> &choices(const std::string &key) {
    static const std::vector<std::pair<std::string, std::string>> none;
    for (const auto &c : store::BOARD_CHOICES)
        if (c.first == key) return c.second;
    return none;
}

std::string side_text(const std::string &col) {
    for (const auto &s : store::BOARD_SIDES)
        if (s.first == col) return text::upper(s.second.substr(0, 1)) + s.second.substr(1);
    return col;
}

// the preview, drawn on its own thread
struct Preview {
    std::mutex mu;
    raster::Image img;
    bool has = false;
    int slides = 0;
    std::string err, title;
    std::atomic<bool> busy{false};
    std::thread worker;
    ~Preview() {
        if (worker.joinable()) worker.join();
    }
};

// Which sections show, which side each goes on, and their order. Changes draft; true if changed.
bool section_order(App &app, Value &draft) {
    Value work = Value::object();
    work["board_sides"] = draft.get("board_sides").is_object() ? draft.get("board_sides") : Value::object();
    work["board_order"] = draft.get("board_order").is_array() ? draft.get("board_order") : Value::array();
    work["board_sections"] = draft.get("board_sections").is_array() ? draft.get("board_sections") : Value::array();
    bool std_text = !(draft.get("board_std_text").is_bool() && !draft.get("board_std_text").as_bool());
    std::string sel_key;
    bool first = true;
    int r = dialog(
        app.ui, {"Sections, Sides & Order", 580, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            para(u, "Sections show in this order, top to bottom. Empty ones are left off each slide. \xE2\x80\x9CKeep each section on its side\xE2\x80\x9D "
                    "under Columns uses the sides here exactly; Balanced moves a section over when that makes the text bigger.");
            u.space(8 * S);
            auto secs = store::board_sections(work);
            std::vector<std::vector<std::string>> rows;
            int sel = -1;
            for (size_t i = 0; i < secs.size(); i++) {
                bool on = false;
                for (const auto &x : work.get("board_sections").arr()) on |= store::text_of(x) == secs[i].key;
                rows.push_back({secs[i].label, on ? "Yes" : "No", side_text(secs[i].col)});
                if (secs[i].key == sel_key) sel = (int)i;
            }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 10 * rh + hh + 2 * S);
            if (first) {
                u.focus(u.id("list:sections"));
                first = false;
            }
            ListResult res = list_view(u, "sections", lr, {{"Section", 200, true}, {"Shown", 70, false}, {"Side", 130, false}}, rows, sel);
            if (sel >= 0 && sel < (int)secs.size() && secs[(size_t)sel].key != sel_key) {
                sel_key = secs[(size_t)sel].key;
                u.again();
            }
            auto act = [&](const std::string &what) {
                if (sel < 0 || sel >= (int)secs.size()) return;
                std::string key = secs[(size_t)sel].key, side = secs[(size_t)sel].col;
                if (what == "show") {
                    auto &on = work["board_sections"].arr_ref();
                    auto it = std::find_if(on.begin(), on.end(), [&](const Value &x) { return store::text_of(x) == key; });
                    if (it != on.end()) on.erase(it);
                    else on.push_back(key);
                } else if (what == "side" && (side == "left" || side == "right"))
                    work["board_sides"][key] = side == "left" ? "right" : "left";
                else if ((what == "up" || what == "down") && side != "top")
                    store::move_board_section(work, key, what == "up" ? -1 : 1);
                u.again();
            };
            if (res.open) act("show");
            u.space(8 * S);
            u.flow();
            if (button(u, "Show / Hide", true, false, true)) act("show");
            if (button(u, "Switch Side", true, false, true)) act("side");
            if (button(u, "Move Up", true, false, true)) act("up");
            if (button(u, "Move Down", true, false, true)) act("down");
            u.place(6 * S, 1);
            if (button(u, "Usual Places", true, false, true)) {
                work["board_sides"] = Value::object();
                work["board_order"] = Value::array();
            }
            u.flow(false);
            u.space(8 * S);
            check(u, "Show each standard's full text (not just its code)", std_text);
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    work["board_std_text"] = std_text;
    for (const auto &m : work.obj()) draft[m.first] = m.second;
    return true;
}

// A font family name ("" for the standard one / same as text); false when cancelled.
bool font_picker(App &app, const std::string &current, bool heading, std::string &out) {
    std::vector<std::vector<std::string>> all;   // (value, name, kind)
    all.push_back({"", heading ? "Same as the text font" : fontlib::label(""), ""});
    for (const auto &f : fontlib::families())
        if (!(f.first == "Helvetica" && !heading)) all.push_back({f.first, f.first, f.second == "standard" ? "Standard" : "On this computer"});
    std::string q, sel_v = current;
    bool first = true, done = false;
    std::unique_ptr<Font> sample_font;
    std::string sample_for = "\x01";
    int r = dialog(
        app.ui, {heading ? "Heading Font" : "Text Font", 520, 0},
        [&](Ui &u) {
            Skin &k = u.sk();
            int S = u.S();
            u.row({k.f->width("Find:") + 6 * S, -1}, u.ctl_h());
            label_mid(u, "Find:");
            if (first) {
                u.focus(u.id("entry:find"));
                first = false;
            }
            Id fid = u.id("entry:find");
            if (u.focused(fid) && u.key(plat::K_DOWN)) u.focus(u.id("list:fonts"));
            entry(u, "find", q);
            u.row({});
            u.space(8 * S);
            std::string ql = text::lower(text::strip(q));
            std::vector<std::vector<std::string>> rows;
            std::vector<std::string> vals;
            int sel = -1;
            for (const auto &x : all)
                if (ql.empty() || text::contains(text::lower(x[1]), ql)) {
                    rows.push_back({x[1], x[2]});
                    vals.push_back(x[0]);
                    if (x[0] == sel_v) sel = (int)vals.size() - 1;
                }
            double rh = k.line + (k.soft ? 10 : 6) * S, hh = k.fb->line + 2 * (k.soft ? 5 : 2) * S + (k.bevel ? 2 * S : S);
            Rect lr = u.place(0, 12 * rh + hh + 2 * S);
            ListResult res = list_view(u, "fonts", lr, {{"Font", 260, true}, {"Kind", 140, false}}, rows, sel, "No font has that in its name.");
            if (sel >= 0 && sel < (int)vals.size()) sel_v = vals[(size_t)sel];
            if (res.open) done = true;
            u.space(8 * S);
            // a sample in the font
            Rect sb = u.place(0, 74 * S);
            if (u.drawing) {
                u.fill(sb, k["edge"]);
                u.fill(sb.in(S), k["field"]);
                if (sel >= 0) {
                    std::string name = sel_v;
                    std::string fam = name.empty() || name == "Helvetica" ? "Helvetica" : name;
                    if (fam != sample_for) {
                        std::vector<std::string> fams = {fam};
                        if (fam == "Helvetica") fams = {"Helvetica", "Arial", "Liberation Sans", "Nimbus Sans"};
                        if (fam == "Times") fams = {"Times", "Times New Roman", "Liberation Serif", "Nimbus Roman"};
                        sample_font.reset(new Font(fams, 30.0 * S));
                        sample_for = fam;
                    }
                    u.clip(sb.in(S));
                    u.text_in(*sample_font, {sb.x + 10 * S, sb.y, sb.w - 20 * S, sb.h}, "The quick brown fox \xC2\xB7 123", k["text"]);
                    u.unclip();
                }
            }
            u.space(4 * S);
            std::string hint = sel < 0 ? "" : sel_v.empty() || sel_v == "Helvetica" ? "Works everywhere: Arial in the slideshow."
                                          : sel_v == "Times"                        ? "Works everywhere: Times New Roman in the slideshow."
                                                                                    : "The slideshow shows this font on computers that have it.";
            para(u, hint, true);
            label(u, std::to_string(all.size() - 1) + " fonts.", false, true);
            return done ? 1 : 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    out = sel_v;
    return true;
}

}  // namespace

void board_designer(App &app) {
    while (true) {
        Value &st = app.settings();
        Value draft = Value::object();
        for (const char *k : KEYS) {
            const Value &v = st.get(k);
            if (std::string(k) == "board_sides") draft[k] = v.is_object() ? v : Value::object();
            else if (std::string(k) == "board_order" || std::string(k) == "board_sections") draft[k] = v.is_array() ? v : Value::array();
            else draft[k] = v;
        }
        for (auto p : std::vector<std::pair<std::string, Value>>{{"board_font", ""}, {"board_head_font", ""}, {"board_big_text", false}, {"board_std_text", true}})
            if (draft.get(p.first).is_null()) draft[p.first] = p.second;
        auto pv = std::make_shared<Preview>();
        int job = 0;
        bool again = false;
        double want_w = 0;
        std::function<void()> start;
        auto merged = [&] {
            Value out = st;
            for (const auto &m : draft.obj()) out[m.first] = m.second;
            return out;
        };
        auto refresh = [&] {
            if (job) app.ui.cancel(job);
            job = app.ui.after(350, [&] {
                job = 0;
                start();
            });
        };
        int poll = 0;
        start = [&] {
            if (pv->busy) {
                again = true;
                return;
            }
            if (pv->worker.joinable()) pv->worker.join();
            pv->busy = true;
            Value m = merged();
            Value doc = doc::preview_doc(app.store, &m);
            std::shared_ptr<images::Image> logo;
            if (const images::Image *l = app.store.logo()) logo = std::make_shared<images::Image>(*l);
            double scale = std::max(0.2, std::min(1.0, want_w > 0 ? want_w / board::W : 0.4));
            plat::Window *win = &app.win;
            pv->worker = std::thread([pv, doc, logo, scale, win] {
                try {
                    auto pics = board::pictures(doc, logo.get(), scale);
                    std::lock_guard<std::mutex> lock(pv->mu);
                    pv->slides = (int)pics.size();
                    if (!pics.empty()) pv->img = pics[0];
                    pv->has = !pics.empty();
                    pv->err.clear();
                    pv->title = doc.s("title");
                } catch (board::BoardError &e) {
                    std::lock_guard<std::mutex> lock(pv->mu);
                    pv->err = e.message;
                } catch (std::exception &e) {
                    std::lock_guard<std::mutex> lock(pv->mu);
                    pv->err = e.what();
                }
                pv->busy = false;
                win->wake();
            });
        };
        std::function<void()> tick = [&] {
            if (!pv->busy && again) {
                again = false;
                start();
            }
            app.ui.again();
            poll = app.ui.after(150, tick);
        };
        poll = app.ui.after(150, tick);
        bool started = false;
        int r = dialog(
            app.ui, DialogBox{"Board Designer", 0, 0, 0.94, 0.92},
            [&](Ui &u) {
                Skin &k = u.sk();
                int S = u.S();
                Rect area = u.clip_rect();
                area.h = area.b() - u.remaining().y;
                area.y = u.remaining().y;
                double lw = std::min(360.0 * S, area.w * 0.4);
                // left: the settings
                u.begin_scroll(u.id("designer"), {area.x, area.y, lw, area.h});
                u.begin({u.remaining().x, u.remaining().y, u.width() - 6 * S, 1e9}, 0);
                begin_group(u, "Fonts");
                for (auto f : std::vector<std::tuple<std::string, std::string, bool>>{{"board_font", "Text:", false}, {"board_head_font", "Headings and title:", true}}) {
                    std::string key = std::get<0>(f);
                    bool heading = std::get<2>(f);
                    label(u, std::get<1>(f));
                    double bw = u.btn_w("Change\xE2\x80\xA6", true);
                    u.row({-bw - u.lay().gap, bw}, u.ctl_h());
                    label_mid(u, font_text(draft.s(key), heading), true);
                    Rect b = u.place(bw, u.ctl_h());
                    u.push_id(key);
                    if (button_at(u, u.id("btn:change"), {b.x, b.y + 3 * S, b.w, b.h - 6 * S}, "Change\xE2\x80\xA6", true, false, true)) {
                        std::string got;
                        if (font_picker(app, draft.s(key), heading, got)) {
                            draft[key] = got;
                            refresh();
                        }
                    }
                    u.pop_id();
                    u.row({});
                    u.space(4 * S);
                }
                para(u, "Board pictures always show the font you pick. The slideshow asks for it by name, so it shows there on computers that have it.", true);
                end_group(u);
                u.space(10 * S);
                begin_group(u, "Layout");
                for (auto p : std::vector<std::pair<std::string, std::string>>{
                         {"board_layout", "Columns:"}, {"board_panels", "Sections look like:"}, {"board_title_align", "Title:"}, {"board_codes_place", "Class codes:"}}) {
                    const auto &opts = choices(p.first);
                    std::string v = draft.s(p.first);
                    bool ok = false;
                    for (const auto &o : opts) ok |= o.first == v;
                    if (!ok && !opts.empty()) v = opts[0].first;
                    label(u, p.second);
                    u.row({std::min(u.width(), 30 * k.f->width("0") + 40 * S)});
                    if (dropdown(u, p.first, opts, v, 30)) {
                        draft[p.first] = v;
                        refresh();
                    }
                    u.row({});
                    u.space(2 * S);
                }
                u.space(6 * S);
                bool big = draft.get("board_big_text").truthy();
                if (check(u, "Extra-big text (a full lesson may use more slides)", big, true, u.width())) {
                    draft["board_big_text"] = big;
                    refresh();
                }
                u.space(10 * S);
                if (button(u, "Sections, Sides & Order\xE2\x80\xA6", true, false, true) && section_order(app, draft)) refresh();
                end_group(u);
                u.space(10 * S);
                begin_group(u, "Start Over");
                bool reset = button(u, "Reset to Default\xE2\x80\xA6", true, false, true) &&
                             confirm(u, "Reset the Board Design?",
                                     "Fonts, columns, section boxes, the title, class codes, text size, and each section's side and order go back to how "
                                     "Chalkboard comes. Your colors, logo, and which sections show stay the same.",
                                     "Reset", "Cancel", "warn");
                end_group(u);
                u.end();
                u.end_scroll();
                if (reset) return 3;
                // right: the preview
                Rect right{area.x + lw + 14 * S, area.y, area.w - lw - 14 * S, area.h};
                u.begin(right, 0);
                label(u, "Preview", true);
                std::string note;
                raster::Image img;
                bool has;
                {
                    std::lock_guard<std::mutex> lock(pv->mu);
                    has = pv->has;
                    if (has) img = pv->img;
                    if (!pv->err.empty()) note = "Couldn't draw the preview: " + pv->err.substr(0, 1) + text::lower(pv->err.substr(1));
                    else if (pv->busy || !has) note = "Drawing the preview\xE2\x80\xA6";
                    else {
                        int n = pv->slides;
                        note = "The first slide of \xE2\x80\x9C" + pv->title + "\xE2\x80\x9D." +
                               (n > 1 ? " It continues onto " + std::to_string(n - 1) + " more slide" + (n > 2 ? "s" : "") + "." : "");
                    }
                }
                para(u, note, true);
                u.space(6 * S);
                Rect box = u.remaining();
                u.end();
                want_w = std::min(box.w - 8 * S, (box.h - 8 * S) * board::W / board::H);
                if (!started) {
                    started = true;
                    u.after(1, [&] { start(); });
                }
                if (u.drawing) {
                    u.fill(box, k["edge"]);
                    u.fill(box.in(S), k["field"]);
                    if (has) {
                        // the slide, shrunk to fit if it was drawn for a bigger box
                        double fit = std::min({(box.w - 8 * S) / img.w, (box.h - 8 * S) / img.h, 1.0});
                        if (fit >= 0.999)
                            u.picture(img, std::round(box.x + (box.w - img.w) / 2), std::round(box.y + (box.h - img.h) / 2));
                        else {
                            double w = img.w * fit, h = img.h * fit;
                            u.img.picture(img.px.data(), nullptr, img.w, img.h, 3, std::round(box.x + (box.w - w) / 2), std::round(box.y + (box.h - h) / 2), w, h);
                        }
                    }
                }
                return 0;
            },
            {{"Save", 1}, {"Cancel", 2}}, 0, 2);
        app.ui.cancel(poll);
        if (job) app.ui.cancel(job);
        if (pv->worker.joinable()) pv->worker.join();
        if (r == 3) {
            store::reset_advanced(app.settings(), "board");
            app.save();
            app.status("The board design is back to the usual one.");
            continue;   // the designer again, from the usual design
        }
        if (r != 1) return;
        for (const auto &m : draft.obj()) app.settings()[m.first] = m.second;
        app.save();
        app.status("Saved the board design. Your next board export uses it.");
        return;
    }
}

}  // namespace gui
