// The school year's calendar (the first and last day, and the days off) and moving lessons by
// school days: a snow day moves everything after it back a day, past weekends and breaks.
// Saved in the settings ("school_calendar"), so it's in backups and moves with the data file.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "dates.h"
#include "json.h"

namespace schoolcal {

using json::Value;

struct DayOff {
    Date from, to;      // inclusive (one day: from == to)
    std::string name;   // "Fall Break"
    std::string kind;   // a KINDS key
};
// (key, label): every kind but "early" is a day with no class
extern const std::vector<std::pair<std::string, std::string>> KINDS;
std::string kind_label(const std::string &kind);
std::string kind_short(const std::string &kind);   // "Teacher day" (for lists)

struct Calendar {
    Date first, last;   // the first and last day of school (not valid: not set)
    std::vector<DayOff> off;
    bool empty() const { return !first.valid() && !last.valid() && off.empty(); }
    // the day off (or early release) on d, if there is one
    const DayOff *day_off(const Date &d) const;
    bool is_school_day(const Date &d) const;
    // n school days after d (n < 0: before it); d itself needn't be a school day
    Date add_school_days(const Date &d, int n) const;
    // why there's no class on d ("Fall Break", "Saturday", "Summer"), or "" when there is
    std::string no_school(const Date &d) const;
};

Calendar read(const Value &settings);
void write(Value &settings, const Calendar &c);
void sort(std::vector<DayOff> &off);   // by first day

// ---- rewriting the Date(s) field. Free text, so only the dates in it change: the words around
// them, and how each is written ("Tues., Nov. 4, 2026", "11/4", "2026-11-04"), stay as typed.

// how this teacher writes each weekday's short name ("Tues", "Thu"), learned from their lessons
struct WeekdayWords {
    std::string word[7];   // Monday first; "" = not seen (a usual short name is used)
};
WeekdayWords weekday_words(const Value &lessons);
// the text with each date d in it changed to move(d)
std::string move_dates(const std::string &text, const Date &near, const std::function<Date(const Date &)> &move, const WeekdayWords &words);

// ---- shifting lessons
struct Move {
    std::string id, title, course;
    std::string before, after;   // the Date(s) field
    Date first_before, first_after;
    bool past_last = false;      // lands after the last day of school
};
// the lessons (in course, or all when course is "") whose first day is on or after from, moved n
// school days (n < 0: earlier), in the order they're taught
std::vector<Move> plan_shift(const Value &lessons, const Calendar &cal, const Date &from, int n, const std::string &course);

// ---- a district calendar file (.ics): its all-day events, each a possible day off
struct Event {
    DayOff day;
    bool looks_off = false;   // its name says no school ("Break", "Holiday", "No School"...)
};
std::vector<Event> read_ics(const std::string &text);
// a first or last day of school named in the file ("First Day of School"), when there is one
bool ics_first_last(const std::vector<Event> &events, Date &first, Date &last);
bool is_first_or_last(const Event &e);   // "First Day of School", "Last Day for Students"

// the same calendar a year later (each day on the same date), to fix up for next year
Calendar next_year(const Calendar &c);

}  // namespace schoolcal
