// Days of school at home (snow days taught from home; "At-Home Days" unless the district names them):
// the number a district allows, a lesson planned ahead for each one in each class, and a teacher
// log for each day used (how many took part, how, and who the teacher talked to).
//
// settings: home_day_name ("Remote Learning Day"), home_day_contact (office hours, for packets),
//           home_days_allotted (how many the district allows)
// lessons:  "home_slot": n (1..allotted) marks a lesson planned for at-home day n in its class
// data:     "home_days": [{"number": 3, "date": "2026-10-22",
//                          "classes": [{"period", "course", "enrolled", "participating", "technology", "paper"}],
//                          "contacts": [{"name", "time", "mode"}]}]
#pragma once
#include <string>
#include <vector>

#include "dates.h"
#include "json.h"

namespace homedays {

using json::Value;

extern const std::vector<std::string> MODES;   // ways of reaching someone: "Phone call", "Email", ...

int allotted(const Value &settings);
// the lesson planned for at-home day n in a class, or null
const Value *planned(const Value &lessons, const std::string &course, int n);
Value *planned(Value &lessons, const std::string &course, int n);
// the day's record (null when day n hasn't been used); make() adds it when it's missing
const Value *find(const Value &data, int n);
Value &make(Value &data, int n);
const Value *on_date(const Value &data, const Date &d);   // the record used on that day
int next_unused(const Value &data, const Value &settings);   // the first day not used yet (0: all used)
Date date_of(const Value &rec);
// the classes a log lists: each class period (with its course), or each class when there are none
std::vector<std::pair<std::string, std::string>> log_classes(const Value &settings, const Value &lessons);   // (period, course)
// "87%" (participating / enrolled x 100, rounded), or "" when enrolled is 0
std::string percent(long participating, long enrolled);
long count(const Value &row, const char *key);   // a number typed in a log ("" = 0)

}  // namespace homedays
