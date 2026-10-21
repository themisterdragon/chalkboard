#include "dates.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <regex>

#include "text.h"

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m) {
    static const int D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leap(y) ? 29 : D[(m - 1) % 12];
}

bool Date::valid() const { return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m); }

bool make_date(int y, int m, int d, Date &out) {
    Date t{y, m, d};
    if (!t.valid()) return false;
    out = t;
    return true;
}

long Date::ordinal() const {
    long yy = y - 1;
    long n = yy * 365 + yy / 4 - yy / 100 + yy / 400;
    for (int k = 1; k < m; k++) n += days_in_month(y, k);
    return n + d - 1;
}

Date Date::from_ordinal(long n) {
    // n days after 0001-01-01
    long y400 = n / 146097;
    n %= 146097;
    long y100 = n / 36524;
    if (y100 == 4) y100 = 3;
    n -= y100 * 36524;
    long y4 = n / 1461;
    n %= 1461;
    long y1 = n / 365;
    if (y1 == 4) y1 = 3;
    n -= y1 * 365;
    Date out;
    out.y = (int)(y400 * 400 + y100 * 100 + y4 * 4 + y1 + 1);
    out.m = 1;
    while (n >= days_in_month(out.y, out.m)) { n -= days_in_month(out.y, out.m); out.m++; }
    out.d = (int)n + 1;
    return out;
}

int Date::weekday() const { return (int)(ordinal() % 7); }   // 0001-01-01 was a Monday

std::string Date::iso() const {
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
}

static std::tm local_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

Date today() {
    std::tm tm = local_now();
    return Date{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

std::string strftime_now(const char *fmt) {
    std::tm tm = local_now();
    char b[128];
    size_t n = std::strftime(b, sizeof b, fmt, &tm);
    return std::string(b, n);
}

std::string now_stamp() { return strftime_now("%Y-%m-%d %H:%M"); }

namespace dates {

const char *MONTHS[12] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
static const char *WEEKDAY_ABBR[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

int school_year(const Date &d) { return d.m >= SCHOOL_YEAR_START ? d.y : d.y - 1; }

std::string school_year_label(int start) {
    char b[24];
    std::snprintf(b, sizeof b, "%d-%02d", start, (start + 1) % 100);
    return b;
}

bool stamp_day(const std::string &stamp, Date &out) {
    // int() of each part of stamp[:10].split("-"): exactly three whole numbers
    std::string s = stamp.substr(0, 10);
    auto parts = text::split(s, "-");
    if (parts.size() != 3) return false;
    int v[3];
    for (int i = 0; i < 3; i++) {
        std::string p = text::strip(parts[(size_t)i]);
        if (p.empty()) return false;
        size_t k = 0;
        if (p[0] == '+' || p[0] == '-') k = 1;
        if (k >= p.size()) return false;
        for (size_t j = k; j < p.size(); j++) if (!(isdigit((unsigned char)p[j]) || p[j] == '_')) return false;
        v[i] = std::atoi(p.c_str());
    }
    return make_date(v[0], v[1], v[2], out);
}

int guess_year(int month, int day, const Date &near) {
    for (int y = near.y - 1; y <= near.y + 1; y++) {
        Date t;
        if (!make_date(y, month, day, t)) continue;
        long gap = t.ordinal() - near.ordinal();
        if (gap >= -62 && gap < 304) return y;
    }
    return near.y;
}

static int month_of(const std::string &word) {
    std::string w = text::lower(word.substr(0, 3));
    for (int i = 0; i < 12; i++) if (w == MONTHS[i]) return i + 1;
    return 0;
}

static const std::string MONTH_RE = "(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\\.?";
static const std::string TO_RE = "\\s*(?:-|\xE2\x80\x93|to)\\s*";

static const std::regex &pattern(int n) {
    static const std::regex P0(MONTH_RE + "\\s+(\\d{1,2})(?:st|nd|rd|th)?(?:,?\\s*(\\d{4}))?(?:" + TO_RE + "(?:" + MONTH_RE +
                                   "\\s+)?(\\d{1,2})(?:st|nd|rd|th)?(?:,?\\s*(\\d{4}))?)?",
                               std::regex::ECMAScript | std::regex::icase);
    static const std::regex P1("\\b(\\d{1,2})/(\\d{1,2})(?:/(\\d{2,4}))?(?:" + TO_RE + "(?:(\\d{1,2})/)?(\\d{1,2})(?:/(\\d{2,4}))?)?");
    static const std::regex P2("\\b(\\d{4})-(\\d{1,2})-(\\d{1,2})\\b");
    return n == 0 ? P0 : n == 1 ? P1 : P2;
}

// "26" -> 2026, "2026" -> 2026, "" -> none
static bool full_year(const std::string &s, int &out) {
    if (s.empty()) return false;
    out = s.size() == 2 ? 2000 + std::atoi(s.c_str()) : std::atoi(s.c_str());
    return true;
}

std::vector<DateMatch> find_dates(const std::string &raw, const Date &near) {
    std::vector<DateMatch> found;
    for (int n = 0; n < 3; n++) {
        const std::regex &pat = pattern(n);
        for (auto it = std::sregex_iterator(raw.begin(), raw.end(), pat); it != std::sregex_iterator(); ++it) {
            const std::smatch &m = *it;
            auto g = [&](int i) { return m[i].matched ? m[i].str() : std::string(); };
            auto at = [&](int i) { return m[i].matched ? Span{(size_t)m.position(i), (size_t)m.length(i)} : Span{}; };
            DateMatch dm;
            dm.pos = (size_t)m.position(0);
            dm.len = (size_t)m.length(0);
            dm.kind = n;
            int y1 = 0, m1, d1, m2 = 0, y2 = 0;
            bool has_y1 = false, has_y2 = false;
            std::string d2;
            if (n == 2) {
                y1 = std::atoi(g(1).c_str()); has_y1 = true;
                m1 = std::atoi(g(2).c_str());
                d1 = std::atoi(g(3).c_str());
                dm.year1 = at(1);
                dm.month1 = at(2);
                dm.day1 = at(3);
            } else if (n == 0) {
                m1 = month_of(g(1));
                // groups: 1 month, 2 day, 3 year, then 4 month, 5 day, 6 year after "to"
                d1 = std::atoi(g(2).c_str());
                has_y1 = full_year(g(3), y1);
                m2 = g(4).empty() ? m1 : month_of(g(4));
                d2 = g(5);
                has_y2 = full_year(g(6), y2);
            } else {
                m1 = std::atoi(g(1).c_str());
                d1 = std::atoi(g(2).c_str());
                has_y1 = full_year(g(3), y1);
                m2 = g(4).empty() ? m1 : std::atoi(g(4).c_str());
                d2 = g(5);
                has_y2 = full_year(g(6), y2);
            }
            if (n != 2) {
                // (the month word's group is the whole word with its dot: find it from the match)
                dm.month1 = at(1);
                dm.day1 = at(2);
                dm.year1 = at(3);
                dm.month2 = at(4);
                dm.day2 = at(5);
                dm.year2 = at(6);
            }
            if (!has_y1 && has_y2) { y1 = y2 - (m2 < m1 ? 1 : 0); has_y1 = true; }   // "Oct 30 - Nov 2, 2026"
            Date start, end;
            if (!make_date(has_y1 && y1 ? y1 : guess_year(m1, d1, near), m1, d1, start)) continue;
            if (!d2.empty()) {
                int ey = has_y2 && y2 ? y2 : start.y + (m2 < m1 ? 1 : 0);
                if (!make_date(ey, m2, std::atoi(d2.c_str()), end)) continue;
            } else end = start;
            dm.start = start;
            dm.end = end;
            found.push_back(dm);
        }
        if (!found.empty()) break;
    }
    return found;
}

std::vector<Date> parse_dates(const std::string &raw, const Date &near) {
    std::vector<Date> found;
    for (const DateMatch &m : find_dates(raw, near)) {
        long span = std::min(std::max(m.end.ordinal() - m.start.ordinal(), 0L), 30L);
        for (long i = 0; i <= span; i++) found.push_back(m.start.plus(i));
    }
    std::vector<Date> out;   // dict.fromkeys: first time each day appears
    for (const Date &d : found)
        if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    return out;
}

std::string fmt_date(const Date &d, bool weekday, bool year) {
    std::string out = text::title(MONTHS[d.m - 1]) + " " + std::to_string(d.d) + (year ? ", " + std::to_string(d.y) : "");
    return weekday ? std::string(WEEKDAY_ABBR[d.weekday()]) + ", " + out : out;
}

std::string fmt_range(const Date &a0, const Date &b0) {
    if (a0 == b0) return fmt_date(a0);
    Date a = std::min(a0, b0), b = a0 < b0 ? b0 : a0;
    if (a.y != b.y) return fmt_date(a) + " - " + fmt_date(b);
    if (a.m != b.m) return fmt_date(a, false, false) + " - " + fmt_date(b);
    return text::title(MONTHS[a.m - 1]) + " " + std::to_string(a.d) + "-" + std::to_string(b.d) + ", " + std::to_string(a.y);
}

}  // namespace dates
