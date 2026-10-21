#include "schoolcal.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>

#include "store.h"
#include "text.h"

namespace schoolcal {

using dates::Span;

const std::vector<std::pair<std::string, std::string>> KINDS = {
    {"holiday", "Holiday"}, {"break", "Break"}, {"teacher", "Teacher day (no students)"}, {"early", "Early release (still a school day)"}};

std::string kind_label(const std::string &kind) {
    for (const auto &k : KINDS)
        if (k.first == kind) return k.second;
    return KINDS[0].second;
}

std::string kind_short(const std::string &kind) {
    std::string l = kind_label(kind);
    size_t p = l.find(" (");
    return p == std::string::npos ? l : l.substr(0, p);
}

namespace {

const char *FULL_DAY[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char *SHORT_DAY[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char *FULL_MONTH[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

bool counts(const Calendar &c, const Date &d) {   // a weekday that isn't a day off
    if (d.weekday() >= 5) return false;
    const DayOff *o = c.day_off(d);
    return !o || o->kind == "early";
}

// the word's capitals copied onto w: "TUES" -> "WED", "tues" -> "wed", otherwise "Wed"
std::string same_case(const std::string &like, const std::string &w) {
    bool all_up = true, all_low = true;
    for (char ch : like)
        if (std::isalpha((unsigned char)ch)) {
            all_up &= std::isupper((unsigned char)ch) != 0;
            all_low &= std::islower((unsigned char)ch) != 0;
        }
    if (all_up && like.size() > 1) return text::upper(w);
    if (all_low) return text::lower(w);
    return text::title(text::lower(w));
}

// which weekday a word names ("Tues" -> 1), or -1
int weekday_of(const std::string &word) {
    std::string w = text::lower(word);
    if (w.size() < 3) return -1;
    if (w == "weds") return 2;
    for (int i = 0; i < 7; i++) {
        std::string full = text::lower(FULL_DAY[i]);
        if (full.compare(0, w.size(), w) == 0) return i;
    }
    return -1;
}

// a weekday word at the very end of before ("Tues., "): where the word starts and how long it is
bool weekday_before(const std::string &before, size_t &at, size_t &len) {
    size_t e = before.size();
    while (e > 0 && (before[e - 1] == ' ' || before[e - 1] == '\t')) e--;
    if (e > 0 && before[e - 1] == ',') e--;
    if (e > 0 && before[e - 1] == '.') e--;
    size_t b = e;
    while (b > 0 && std::isalpha((unsigned char)before[b - 1])) b--;
    if (b == e || (b > 0 && std::isalnum((unsigned char)before[b - 1]))) return false;
    if (weekday_of(before.substr(b, e - b)) < 0) return false;
    at = b;
    len = e - b;
    return true;
}

std::string suffix_for(int d) {
    if (d % 100 >= 11 && d % 100 <= 13) return "th";
    return d % 10 == 1 ? "st" : d % 10 == 2 ? "nd" : d % 10 == 3 ? "rd" : "th";
}

// a number written as wide as the old one was ("04" stays two digits)
std::string number_like(const std::string &old, int v) {
    std::string s = std::to_string(v);
    while (s.size() < old.size() && !old.empty() && old[0] == '0') s = "0" + s;
    return s;
}

// a month written the way the old word was: "Oct." -> "Nov.", "October" -> "November", "oct" -> "nov"
std::string month_like(const std::string &old, int m) {
    std::string letters;
    for (char ch : old)
        if (std::isalpha((unsigned char)ch)) letters += ch;
    bool dot = !old.empty() && old.back() == '.';
    std::string full = FULL_MONTH[m - 1];
    std::string word;
    int was = 0;
    for (int i = 0; i < 12; i++)
        if (text::lower(letters).compare(0, 3, text::lower(std::string(FULL_MONTH[i]).substr(0, 3))) == 0) was = i + 1;
    bool was_full = was && text::lower(letters) == text::lower(FULL_MONTH[was - 1]);
    if (was_full && !dot) word = full;
    else if (letters.size() == 4 && m == 9) word = "Sept";   // "Sept." stays four letters
    else word = full.substr(0, 3);
    return same_case(letters, word) + (dot ? "." : "");
}

struct Edit {
    size_t at, len;
    std::string with;
};

}  // namespace

const DayOff *Calendar::day_off(const Date &d) const {
    for (const auto &o : off)
        if (o.from <= d && d <= o.to) return &o;
    return nullptr;
}

bool Calendar::is_school_day(const Date &d) const {
    if (first.valid() && d < first) return false;
    if (last.valid() && last < d) return false;
    return counts(*this, d);
}

Date Calendar::add_school_days(const Date &d0, int n) const {
    // weekends and days off are skipped; before the first day or after the last, weekdays still
    // count (a lesson pushed into summer is flagged, not lost)
    Date d = d0;
    int step = n < 0 ? -1 : 1;
    for (int left = std::abs(n), guard = 0; left > 0 && guard < 4000; guard++) {
        d = d.plus(step);
        if (counts(*this, d)) left--;
    }
    return d;
}

std::string Calendar::no_school(const Date &d) const {
    if (d.weekday() >= 5) return FULL_DAY[d.weekday()];
    if (const DayOff *o = day_off(d))
        if (o->kind != "early") return o->name.empty() ? kind_label(o->kind) : o->name;
    if (first.valid() && d < first) return "Before the first day of school";
    if (last.valid() && last < d) return "After the last day of school";
    return "";
}

// ----------------------------------------------------------------------------- saving

static Date day_of(const std::string &s) {
    Date d;
    if (!dates::stamp_day(s, d)) return Date{};
    return d;
}

Calendar read(const Value &settings) {
    Calendar c;
    const Value &v = settings.get("school_calendar");
    if (!v.is_object()) return c;
    c.first = day_of(v.s("first_day"));
    c.last = day_of(v.s("last_day"));
    for (const auto &o : v.get("days_off").arr()) {
        if (!o.is_object()) continue;
        DayOff d;
        d.from = day_of(o.s("from"));
        d.to = day_of(o.s("to"));
        if (!d.from.valid()) continue;
        if (!d.to.valid() || d.to < d.from) d.to = d.from;
        d.name = o.s("name");
        d.kind = o.s("kind");
        bool known = false;
        for (const auto &k : KINDS) known |= k.first == d.kind;
        if (!known) d.kind = "holiday";
        c.off.push_back(d);
    }
    sort(c.off);
    return c;
}

void write(Value &settings, const Calendar &c) {
    Value v = Value::object();
    v["first_day"] = c.first.valid() ? c.first.iso() : "";
    v["last_day"] = c.last.valid() ? c.last.iso() : "";
    Value off = Value::array();
    std::vector<DayOff> sorted = c.off;
    sort(sorted);
    for (const auto &o : sorted) {
        Value x = Value::object();
        x["from"] = o.from.iso();
        x["to"] = o.to.iso();
        x["name"] = o.name;
        x["kind"] = o.kind;
        off.push(x);
    }
    v["days_off"] = off;
    settings["school_calendar"] = v;
}

void sort(std::vector<DayOff> &off) {
    std::stable_sort(off.begin(), off.end(), [](const DayOff &a, const DayOff &b) { return a.from < b.from; });
}

// ----------------------------------------------------------------------------- rewriting dates

static Date lesson_near(const Value &x) {
    Date near;
    if (!dates::stamp_day(store::field(x, "created"), near) && !dates::stamp_day(store::field(x, "updated"), near)) near = today();
    return near;
}

WeekdayWords weekday_words(const Value &lessons) {
    std::map<std::string, int> seen[7];
    for (const auto &x : lessons.arr()) {
        if (!x.is_object()) continue;
        std::string t = store::field(x, "date");
        for (const auto &m : dates::find_dates(t, lesson_near(x))) {
            size_t at, len;
            if (!weekday_before(t.substr(0, m.pos), at, len)) continue;
            std::string w = t.substr(at, len);
            int wd = weekday_of(w);
            if (wd != m.start.weekday() || text::lower(w) == text::lower(FULL_DAY[wd])) continue;   // a short name, and right
            seen[wd][text::title(text::lower(w))]++;
        }
    }
    WeekdayWords out;
    for (int i = 0; i < 7; i++) {
        int best = 0;
        for (const auto &p : seen[i])
            if (p.second > best) {
                best = p.second;
                out.word[i] = p.first;
            }
    }
    return out;
}

std::string move_dates(const std::string &t, const Date &near, const std::function<Date(const Date &)> &move, const WeekdayWords &words) {
    std::string out;
    size_t done = 0;
    for (const auto &m : dates::find_dates(t, near)) {
        Date a = move(m.start), b = m.end == m.start ? a : move(m.end);
        std::vector<Edit> edits;
        // a weekday just before it ("Tues., Nov. 4")
        size_t wat, wlen;
        if (m.pos >= done && weekday_before(t.substr(0, m.pos), wat, wlen) && wat >= done) {
            std::string old = t.substr(wat, wlen);
            int wd = a.weekday();
            bool full = text::lower(old) == text::lower(FULL_DAY[weekday_of(old)]);
            std::string w = full ? FULL_DAY[wd] : !words.word[wd].empty() ? words.word[wd] : SHORT_DAY[wd];
            edits.push_back({wat, wlen, same_case(old, w)});
        }
        auto part = [&](const Span &s) { return t.substr(s.at, s.len); };
        auto month_word = [&](const Span &s) {   // the whole word, with its dot ("Sept.")
            Span w = s;
            while (w.at + w.len < t.size() && std::isalpha((unsigned char)t[w.at + w.len])) w.len++;
            if (w.at + w.len < t.size() && t[w.at + w.len] == '.') w.len++;
            return w;
        };
        auto year_like = [&](const Span &s, int y) { return s.len == 2 ? number_like("00", y % 100) : std::to_string(y); };
        auto day = [&](const Span &s, int d) {
            edits.push_back({s.at, s.len, number_like(part(s), d)});
            size_t e = s.at + s.len;   // "4th" -> "5th"
            if (m.kind == 0 && e + 2 <= t.size()) {
                std::string suf = text::lower(t.substr(e, 2));
                if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") edits.push_back({e, 2, same_case(t.substr(e, 2), suffix_for(d))});
            }
        };
        if (m.kind == 2) {
            edits.push_back({m.year1.at, m.year1.len, std::to_string(a.y)});
            edits.push_back({m.month1.at, m.month1.len, number_like("00", a.m)});   // always two digits
            edits.push_back({m.day1.at, m.day1.len, number_like("00", a.d)});
        } else {
            Span m1 = m.kind == 0 ? month_word(m.month1) : m.month1;
            std::string m1old = part(m1);
            auto month = [&](const std::string &old, int mm) { return m.kind == 0 ? month_like(old, mm) : number_like(old, mm); };
            edits.push_back({m1.at, m1.len, month(m1old, a.m)});
            day(m.day1, a.d);
            if (m.year1.len) edits.push_back({m.year1.at, m.year1.len, year_like(m.year1, a.y)});
            if (m.day2.len) {
                if (m.month2.len) {
                    Span m2 = m.kind == 0 ? month_word(m.month2) : m.month2;
                    edits.push_back({m2.at, m2.len, month(part(m2), b.m)});
                } else if (b.m != a.m)   // "Oct 30-31" now crosses into November: say so
                    edits.push_back({m.day2.at, 0, m.kind == 0 ? month_like(m1old, b.m) + " " : std::to_string(b.m) + "/"});
                day(m.day2, b.d);
                if (m.year2.len) edits.push_back({m.year2.at, m.year2.len, year_like(m.year2, b.y)});
            }
        }
        std::sort(edits.begin(), edits.end(), [](const Edit &x, const Edit &y) { return x.at < y.at || (x.at == y.at && x.len < y.len); });
        for (const auto &e : edits) {
            if (e.at < done) continue;
            out += t.substr(done, e.at - done) + e.with;
            done = e.at + e.len;
        }
    }
    return out + t.substr(std::min(done, t.size()));
}

// ----------------------------------------------------------------------------- shifting lessons

std::vector<Move> plan_shift(const Value &lessons, const Calendar &cal, const Date &from, int n, const std::string &course) {
    std::vector<Move> out;
    if (n == 0) return out;
    WeekdayWords words = weekday_words(lessons);
    for (const auto &x : lessons.arr()) {
        if (!x.is_object()) continue;
        if (!text::strip(course).empty() && !store::in_class(x, course)) continue;
        auto ds = store::lesson_dates(x);
        if (ds.empty()) continue;
        Date first = *std::min_element(ds.begin(), ds.end());
        if (first < from) continue;
        Move mv;
        mv.id = x.s("id");
        mv.title = store::field(x, "title");
        mv.course = store::class_of(x);
        mv.before = store::field(x, "date");
        mv.after = move_dates(mv.before, lesson_near(x), [&](const Date &d) { return cal.add_school_days(d, n); }, words);
        if (mv.after == mv.before) continue;
        auto now = dates::parse_dates(mv.after, lesson_near(x));
        mv.first_before = first;
        mv.first_after = now.empty() ? first : *std::min_element(now.begin(), now.end());
        for (const auto &d : now) mv.past_last |= cal.last.valid() && cal.last < d;
        out.push_back(mv);
    }
    std::stable_sort(out.begin(), out.end(), [](const Move &a, const Move &b) { return text::lower(a.title) < text::lower(b.title); });
    std::stable_sort(out.begin(), out.end(), [](const Move &a, const Move &b) { return a.first_before < b.first_before; });
    return out;
}

// ----------------------------------------------------------------------------- .ics files

std::vector<Event> read_ics(const std::string &raw) {
    // unfold: a line starting with a space or tab continues the one before
    std::vector<std::string> lines;
    for (auto line : text::split(text::replace(raw, "\r\n", "\n"), "\n")) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !lines.empty()) lines.back() += line.substr(1);
        else lines.push_back(line);
    }
    auto ymd = [](const std::string &v, Date &d) {   // 20261005 (a day, no time)
        if (v.size() != 8) return false;
        for (char ch : v)
            if (!std::isdigit((unsigned char)ch)) return false;
        return make_date(std::stoi(v.substr(0, 4)), std::stoi(v.substr(4, 2)), std::stoi(v.substr(6, 2)), d);
    };
    auto unescape = [](const std::string &s) {
        std::string o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[++i];
                o += n == 'n' || n == 'N' ? ' ' : n;
            } else
                o += s[i];
        }
        return text::strip(o);
    };
    std::vector<Event> out;
    bool in = false;
    Date start, end;
    bool has_end = false, all_day = false;
    long days = 0;
    std::string name;
    for (const auto &line : lines) {
        size_t colon = line.find(':');
        std::string head = text::upper(colon == std::string::npos ? line : line.substr(0, colon));
        std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
        std::string prop = head.substr(0, head.find(';'));
        if (prop == "BEGIN" && text::upper(value) == "VEVENT") {
            in = true;
            start = end = Date{};
            has_end = all_day = false;
            days = 0;
            name.clear();
        } else if (!in)
            continue;
        else if (prop == "DTSTART")
            all_day = ymd(text::strip(value), start);   // a time ("20261005T080000Z") isn't a day off
        else if (prop == "DTEND")
            has_end = ymd(text::strip(value), end);
        else if (prop == "DURATION") {
            std::smatch dm;   // P3D, P1W
            std::string v = text::upper(text::strip(value));
            if (std::regex_match(v, dm, std::regex("P(\\d+)([DW])"))) days = std::stol(dm[1].str()) * (dm[2].str() == "W" ? 7 : 1);
        } else if (prop == "SUMMARY")
            name = unescape(value);
        else if (prop == "END" && text::upper(value) == "VEVENT") {
            in = false;
            if (!all_day || !start.valid()) continue;
            Event e;
            e.day.from = start;
            // an all-day event's end is the day after it
            e.day.to = has_end && start < end ? end.plus(-1) : days > 0 ? start.plus(days - 1) : start;
            if (e.day.to.ordinal() - e.day.from.ordinal() > 120) continue;   // (a whole-year event)
            e.day.name = name.empty() ? "Day off" : name;
            std::string low = text::lower(e.day.name);
            auto has = [&](std::initializer_list<const char *> words) {
                for (const char *w : words)
                    if (text::contains(low, w)) return true;
                return false;
            };
            if (has({"early release", "early dismissal", "early out", "half day", "half-day"})) e.day.kind = "early";
            else if (has({"professional", "pd day", "in-service", "inservice", "teacher", "staff", "records", "non-student", "no students",
                          "workday", "work day"}))
                e.day.kind = "teacher";
            else if (has({"break", "vacation", "recess"})) e.day.kind = "break";
            else e.day.kind = "holiday";
            e.looks_off = e.day.kind != "holiday" ||
                          has({"no school", "holiday", "closed", "day off", "no classes", "observance", "snow", "labor day", "memorial day",
                               "thanksgiving", "christmas", "new year", "martin luther king", "mlk", "presidents", "veterans", "independence",
                               "juneteenth", "good friday", "election day", "columbus", "indigenous"});
            out.push_back(e);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Event &a, const Event &b) { return a.day.from < b.day.from; });
    return out;
}

bool is_first_or_last(const Event &e) {
    std::string low = text::lower(e.day.name);
    bool school = text::contains(low, "school") || text::contains(low, "student") || text::contains(low, "class");
    return school && (text::contains(low, "first day") || text::contains(low, "last day"));
}

bool ics_first_last(const std::vector<Event> &events, Date &first, Date &last) {
    bool any = false;
    for (const auto &e : events) {
        if (!is_first_or_last(e)) continue;
        std::string low = text::lower(e.day.name);
        if (text::contains(low, "first day")) {
            first = e.day.from;
            any = true;
        } else {
            last = e.day.to;
            any = true;
        }
    }
    return any;
}

Calendar next_year(const Calendar &c) {
    auto later = [](const Date &d) {
        if (!d.valid()) return d;
        Date o;
        if (!make_date(d.y + 1, d.m, d.d, o)) make_date(d.y + 1, d.m, d.d - 1, o);   // February 29
        return o;
    };
    Calendar n;
    n.first = later(c.first);
    n.last = later(c.last);
    for (auto o : c.off) {
        o.from = later(o.from);
        o.to = later(o.to);
        n.off.push_back(o);
    }
    return n;
}

}  // namespace schoolcal
