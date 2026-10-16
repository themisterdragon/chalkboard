// A month calendar for picking days (port of gui/calendar.py): the Day Slideshow and a lesson's
// Date(s). Drawn by Chalkboard, so it matches every look and its dark mode.
#include <algorithm>
#include <cmath>
#include <set>

#include "gui.h"

namespace gui {

using namespace ui;

namespace {
const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const char *DAY_NAMES[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

Date first_of(const Date &d) { return Date{d.y, d.m, 1}; }

Date add_months(const Date &d, int n) {
    int m = d.m - 1 + n;
    int y = d.y + (m >= 0 ? m / 12 : -((11 - m) / 12));
    m = ((m % 12) + 12) % 12;
    return Date{y, m + 1, 1};
}
}  // namespace

void Calendar::init(const Date *first, const Date *last) {
    if (first) {
        start = *first;
        end = last ? *last : *first;
    } else
        start = end = Date{};
    cursor = first ? *first : today();
    month = first_of(cursor);
}

bool Calendar::frame(Ui &ui, const std::string &key) {
    Skin &k = ui.sk();
    int S = ui.S();
    Id id = ui.id("calendar:" + key);
    double cell = std::max(k.f->width("00") + 18 * S, k.line + 14 * S);
    double head = k.line + 16 * S;
    double w = cell * 7 + 4 * S, h = head + k.line + 6 * S + cell * 6 + 4 * S;
    Rect r = ui.place(w, h);
    bool clicked = ui.hit(id, r);
    bool focus = ui.focused(id);
    bool picked = false;
    Date now = today();
    auto go = [&](const Date &d) {
        cursor = d;
        month = first_of(d);
        ui.again();
    };
    auto pick = [&](const Date &d, bool extend) {
        if (extend && allow_range && start.valid()) end = d;
        else start = end = d;
        go(d);
        picked = true;
    };
    auto page = [&](int n) {
        Date f = add_months(cursor, n);
        go(Date{f.y, f.m, std::min(cursor.d, days_in_month(f.y, f.m))});
    };
    if (focus) {
        bool shift = ui.has(plat::Event::KEY) && (ui.ev.mods & plat::SHIFT);
        if (ui.key(plat::K_LEFT)) go(cursor.plus(-1));
        else if (ui.key(plat::K_RIGHT)) go(cursor.plus(1));
        else if (ui.key(plat::K_UP)) go(cursor.plus(-7));
        else if (ui.key(plat::K_DOWN)) go(cursor.plus(7));
        else if (ui.key(plat::K_PGUP)) page(-1);
        else if (ui.key(plat::K_PGDN)) page(1);
        else if (ui.key(plat::K_HOME)) go(now);
        else if (ui.key(' ') || ui.key(plat::K_RETURN) || ui.key(' ', plat::SHIFT) || ui.key(plat::K_RETURN, plat::SHIFT)) pick(cursor, shift);
        else if (ui.has(plat::Event::TEXT) && ui.ev.text == " ") {
            ui.consume();
            pick(cursor, false);
        }
    }
    double x0 = r.x + 2 * S;
    // the days of the month's weeks, Sunday first
    Date f = month;
    int back = (f.weekday() + 1) % 7;   // Monday = 0 -> days since Sunday
    Date first_cell = f.plus(-back);
    double gy = r.y + head + k.line + 6 * S;
    // a click (hit() has taken the press and its release)
    if (clicked) {
        bool extend = ui.ev.mods & plat::SHIFT;
        if (ui.my < r.y + head) {
            if (ui.mx < x0 + cell) page(-1);
            else if (ui.mx >= x0 + 6 * cell) page(1);
        } else if (ui.my >= gy) {
            int row = (int)((ui.my - gy) / cell), col = (int)((ui.mx - x0) / cell);
            if (row >= 0 && row < 6 && col >= 0 && col < 7) pick(first_cell.plus(row * 7 + col), extend);
        }
    }
    if (!ui.drawing) return picked;
    double tw = cell * 7;
    std::string title = std::string(MONTHS[month.m - 1]) + " " + std::to_string(month.y);
    ui.text_in(*k.fb, {x0, r.y, tw, head}, title, k["text"], CENTER);
    for (int n : {-1, 1}) {
        double x = n < 0 ? x0 + cell / 2 : x0 + tw - cell / 2, y = r.y + head / 2, a = std::max(4.0 * S, std::floor(cell / 7));
        if (n < 0) ui.polygon({{x + a / 2, y - a}, {x - a / 2, y}, {x + a / 2, y + a}}, k["text"]);
        else ui.polygon({{x - a / 2, y - a}, {x + a / 2, y}, {x - a / 2, y + a}}, k["text"]);
    }
    double y = r.y + head;
    for (int i = 0; i < 7; i++) ui.text_in(*k.f, {x0 + cell * i, y, cell, k.line}, DAY_NAMES[i], k["dim"], CENTER);
    bool has = start.valid();
    Date lo = has ? std::min(start, end) : Date{}, hi = has ? (start < end ? end : start) : Date{};
    Color soft_text = hex(k["selsoft"]) == hex(k["sel"]) ? k["seltext"] : k["text"];
    int weeks = (back + days_in_month(month.y, month.m) + 6) / 7;
    for (int row = 0; row < weeks; row++)
        for (int col = 0; col < 7; col++) {
            Date day = first_cell.plus(row * 7 + col);
            Rect c{x0 + cell * col, gy + cell * row, cell, cell};
            double m = 2 * S;
            Color fg = day.m == month.m ? k["text"] : k["off"];
            if (has && lo <= day && day <= hi) {
                bool end_ = day == lo || day == hi;
                Color fill = end_ ? k["sel"] : k["selsoft"];
                if (k.soft) ui.rrect(c.in(m), 7 * S, fill, {}, 0, true, false);
                else ui.fill(c.in(m), fill);
                fg = end_ ? k["seltext"] : soft_text;
            }
            if (day == now) ui.box({c.x + m, c.y + m, c.w - 2 * m - 1, c.h - 2 * m - 1}, k["edge"], S);
            Font &font = day == now ? *k.fb : *k.f;
            ui.text_in(font, {c.x, c.y + cell * 0.44 - font.line / 2, c.w, font.line}, std::to_string(day.d), fg, CENTER);
            if (marks && marks(day)) {
                double rr = std::max(2.0, 2.0 * S), cx = c.x + c.w / 2, cy = c.b() - m - 5 * S;
                ui.oval({cx - rr, cy - rr, 2 * rr, 2 * rr}, fg, fg, 0, true, false);
            }
            if (focus && day == cursor) ui.dotted({c.x + m + 2 * S, c.y + m + 2 * S, c.w - 2 * m - 4 * S, c.h - 2 * m - 4 * S}, k["text"]);
        }
    return picked;
}

std::function<bool(const Date &)> lesson_marks(App &app) {
    auto days = std::make_shared<std::set<long>>();
    for (const auto &l : app.store.data["lessons"].arr())
        for (const auto &d : store::lesson_dates(l)) days->insert(d.ordinal());
    return [days](const Date &d) { return days->count(d.ordinal()) > 0; };
}

bool pick_dates(App &app, const std::string &title, const Date *first, const Date *last, Date &a, Date &b) {
    Calendar cal;
    cal.allow_range = true;
    cal.marks = lesson_marks(app);
    cal.init(first, last);
    bool focus = true;
    int r = dialog(
        app.ui, {title, 420, 0},
        [&](Ui &u) {
            int S = u.S();
            para(u, "Click a day. Teaching it over several days? Click the first day, then Shift-click the last. Dots are days that already have a "
                    "lesson.");
            u.space(10 * S);
            if (focus) {
                u.focus(u.id("calendar:cal"));
                focus = false;
            }
            bool enter = u.focused(u.id("calendar:cal")) && u.has(plat::Event::KEY) && u.ev.key == plat::K_RETURN && !u.ev.mods;
            bool picked = cal.frame(u, "cal");
            u.space(6 * S);
            if (cal.start.valid()) {
                Date lo = std::min(cal.start, cal.end), hi = cal.start < cal.end ? cal.end : cal.start;
                label(u, dates::fmt_range(lo, hi), true);
            } else
                label(u, " ", true);
            if (picked && enter) return 1;   // Return picks the day and closes
            return 0;
        },
        {{"OK", 1}, {"Cancel", 2}}, 0, 2);
    if (r != 1 || !cal.start.valid()) return false;
    a = std::min(cal.start, cal.end);
    b = cal.start < cal.end ? cal.end : cal.start;
    return true;
}

}  // namespace gui
