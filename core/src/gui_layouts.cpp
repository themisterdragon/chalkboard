// Advanced Mode > Page Layouts (port of gui/layouts.py): name lines, titles, headings, and
// footers for each kind of printed page. Every change shows in a preview right away, and Reset to
// Default puts the page back the way Chalkboard comes. The terminal app changes the same settings.
#include <algorithm>
#include <map>

#include "doc.h"
#include "export_txt.h"
#include "gui.h"
#include "text.h"

namespace gui {

using namespace ui;

bool page_layout_dialog(App &app, const std::string &kind) {
    const store::PageKind *pk = store::page_kind(kind);
    if (!pk) return false;
    Value &st = app.settings();
    Value current = store::page_layout(st, kind);
    // what's being changed: text for everything (numbers and yes/no too, until it's saved)
    std::map<std::string, std::string> vals;
    std::map<std::string, bool> flags;
    for (const auto &o : pk->options) {
        if (o.type == "bool") flags[o.key] = current.get(o.key).truthy();
        else vals[o.key] = store::text_of(current.get(o.key));
    }
    auto values = [&] {
        Value out = Value::object();
        for (const auto &o : pk->options) {
            if (o.type == "bool") out[o.key] = flags[o.key];
            else if (o.type == "num") out[o.key] = vals[o.key].empty() ? 0 : std::stoi(vals[o.key]);
            else if (o.type == "text") out[o.key] = text::strip(vals[o.key]);
            else out[o.key] = vals[o.key];
        }
        return out;
    };
    std::string shown_for = "\x01";
    std::vector<std::string> lines;
    std::string note;
    int r = dialog(
        app.ui, DialogBox{"Page Layout: " + pk->name, 0, 0, 0.94, 0.92},
        [&](Ui &u) {
            int S = u.S();
            Rect area = u.clip_rect();
            area.h = area.b() - u.remaining().y;
            area.y = u.remaining().y;
            double lw = std::min(430.0 * S, area.w * 0.45);
            // left: the options
            u.begin_scroll(u.id("options"), {area.x, area.y, lw, area.h});
            u.begin({u.remaining().x, u.remaining().y, u.width() - 8 * S, 1e9}, 0);
            para(u, "Leave anything as it is to keep the usual layout. Separate name-line labels with commas, like: Name, Date, Period", true);
            u.space(6 * S);
            for (const auto &o : pk->options) {
                u.push_id(o.key);
                u.space(4 * S);
                if (o.type == "bool") {
                    check(u, o.label, flags[o.key], true, u.width());
                    u.pop_id();
                    continue;
                }
                para(u, o.label + ":");
                const std::vector<std::pair<std::string, std::string>> *ch = nullptr;
                for (const auto &c : store::LAYOUT_CHOICES)
                    if (c.first == o.type) ch = &c.second;
                if (ch) {
                    u.row({std::min(u.width(), 26 * u.sk().f->width("0") + 40 * S)});
                    dropdown(u, "choice", *ch, vals[o.key], 26);
                    u.row({});
                } else if (o.type == "num") {
                    std::vector<std::pair<std::string, std::string>> nopts;
                    for (int n = 0; n < 13; n++) nopts.push_back({std::to_string(n), n == 0 ? "None" : std::to_string(n)});
                    u.row({8 * u.sk().f->width("0") + 40 * S});
                    dropdown(u, "num", nopts, vals[o.key], 6);
                    u.row({});
                } else if (o.type == "text")
                    text_area(u, "text", vals[o.key], 3, 8, false);
                else
                    entry(u, "line", vals[o.key]);
                u.pop_id();
            }
            u.space(14 * S);
            if (button(u, "Reset to Default\xE2\x80\xA6", true, false, true) &&
                confirm(u, "Reset to Default?", "Put the " + pk->name + " layout back the way Chalkboard comes?", "Reset", "Cancel", "ask")) {
                for (const auto &o : pk->options) {
                    if (o.type == "bool") flags[o.key] = o.dflt.truthy();
                    else vals[o.key] = store::text_of(o.dflt);
                }
            }
            u.space(4 * S);
            u.end();
            u.end_scroll();
            // right: the preview, the plain-text export (it follows the same layout as the PDF and Word files)
            Value v = values();
            std::string key = json::dump(v, -1);
            if (key != shown_for) {
                shown_for = key;
                Value trying = st;
                store::set_page_layout(trying, kind, v);
                Value d = doc::layout_sample(app.store, kind, trying);
                note = "\xE2\x80\x9C" + d.s("title") + "\xE2\x80\x9D, the way the text export lays it out. PDF and Word files follow the same layout with real fonts and lines.";
                lines = export_txt::render_lines(d, 72);
            }
            Rect right{area.x + lw + 14 * S, area.y, area.w - lw - 14 * S, area.h};
            u.begin(right, 0);
            label(u, "Preview", true);
            para(u, note, true);
            u.space(6 * S);
            Rect box = u.remaining();
            u.end();
            text_view(u, "sample", box, lines);
            return 0;
        },
        {{"Save", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1) return false;
    store::set_page_layout(st, kind, values());
    app.save();
    app.status("Saved the " + pk->name + " layout. Your next export uses it.");
    return true;
}

}  // namespace gui
