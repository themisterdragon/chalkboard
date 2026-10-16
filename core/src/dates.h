// Calendar dates and reading the free-typed Date(s) field ("Oct 4-6, 2026", "10/4", "2026-10-04").
#pragma once
#include <string>
#include <vector>

struct Date {
    int y = 0, m = 0, d = 0;
    bool valid() const;
    long ordinal() const;                 // days since 0001-01-01 (like date.toordinal() - 1)
    static Date from_ordinal(long n);
    int weekday() const;                  // 0 = Monday, like date.weekday()
    Date plus(long days) const { return from_ordinal(ordinal() + days); }
    bool operator==(const Date &o) const { return y == o.y && m == o.m && d == o.d; }
    bool operator!=(const Date &o) const { return !(*this == o); }
    bool operator<(const Date &o) const { return ordinal() < o.ordinal(); }
    bool operator<=(const Date &o) const { return ordinal() <= o.ordinal(); }
    std::string iso() const;              // "2026-10-04"
};

bool make_date(int y, int m, int d, Date &out);   // false when it isn't a real day (like a ValueError)
int days_in_month(int y, int m);
Date today();
std::string now_stamp();                          // "2026-10-14 08:54", local time (store.now())
std::string strftime_now(const char *fmt);

namespace dates {
extern const char *MONTHS[12];                    // "jan" ... "dec"
const int SCHOOL_YEAR_START = 7;

int school_year(const Date &d);
std::string school_year_label(int start);         // 2026 -> "2026-27"
bool stamp_day(const std::string &stamp, Date &out);   // "2026-10-02 16:23" -> 2026-10-02
int guess_year(int month, int day, const Date &near);
std::vector<Date> parse_dates(const std::string &text, const Date &near);
std::string fmt_date(const Date &d, bool weekday = false, bool year = true);
std::string fmt_range(const Date &a, const Date &b);
}  // namespace dates
