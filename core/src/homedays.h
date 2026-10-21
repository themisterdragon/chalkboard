// Days of school at home (snow days taught from home; "At-Home Days" unless the district names them):
// the number a district allows, a lesson planned ahead for each one in each class, and a teacher
// log for each day used (how many took part, how, and who the teacher talked to).
//
// settings: home_day_name ("Remote Learning Day"), home_day_contact (office hours, for packets),
//           home_days_allotted (how many the district allows)
// lessons:  "home_ready": true keeps a lesson in its unit's at-home lessons (ready for a day off;
//           the older "home_slot": n counts too; no unit: it fits any unit), "home_used": "2026-12-08" once a day used it
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
bool is_home(const Value &lesson);   // one of its unit's at-home lessons
bool used(const Value &lesson);      // an at-home day has used it
// the unit a class is in on a day: its lesson that day's, or else the last one before it
std::string unit_on(const Value &lessons, const std::string &course, const Date &d);
// a class's at-home lessons (in one unit, or all of them when unit is null): unused first, then by unit and title
std::vector<const Value *> bank(const Value &lessons, const std::string &course, const std::string *unit = nullptr);
// the at-home lesson for a class on a day: an unused one in that day's unit, or else one with no
// unit (it fits any unit); null: none ready
const Value *pick(const Value &lessons, const std::string &course, const Date &d);
std::string unit_label(const Value &lesson);   // its unit, or "Any unit"
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
