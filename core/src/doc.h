// Lessons and assessments as a format-neutral list of printable blocks (port of doc.py).
// Every exporter (PDF, DOCX, TXT) renders the same blocks:
//   title, subtitle, fields, h1, p, bullet, kv, q, choice, choice_inline, lines, blank, box,
//   answer, match, organizer, passage, space, rule, check, grid, days, pagebreak
// A document is {"title", "footer", "blocks"}. Blocks are JSON objects with the same keys the
// Python version uses, so the two can be compared directly.
#pragma once
#include <string>
#include <vector>

#include "json.h"
#include "store.h"

namespace doc {

using json::Value;

Value text_blocks(const std::string &value, const std::string &style = "normal");
Value vocab_blocks(const std::string &value);
std::string norm_blanks(const std::string &s);
Value fields_block(const std::vector<std::string> &items);   // null when there are no labels

// settings: the store's, or a draft (the layout and board designers preview before saving)
Value lesson_doc(const Value &lesson, store::Store &st, const Value *settings = nullptr);
Value makeup_doc(const Value &lesson, store::Store &st, const Value *settings = nullptr);
// a packet for a day of school at home: settings home_day_name, home_day_contact
Value home_day_doc(const Value &lesson, store::Store &st, const Date &day, const Value *settings = nullptr);
// the teacher log for an at-home day (a data.home_days record)
Value home_log_doc(store::Store &st, const Value &rec, const Value *settings = nullptr);
Value code_blocks(const std::string &codes);
Value board_doc(const Value &lesson, store::Store &st, const Value *period = nullptr, const Value *settings = nullptr);
Value preview_doc(store::Store &st, const Value *settings = nullptr);
Value layout_sample(store::Store &st, const std::string &kind, const Value &settings);

std::string handout_name(const Value &a);
Value annotation_doc(const Value &a, store::Store &st, const Value *settings = nullptr);
Value bell_sheet_doc(const Value &a, store::Store &st, const Value *settings = nullptr);
Value assessment_doc(const Value &a, store::Store &st, int version = 0, int versions = 1, bool key = false, const Value *settings = nullptr);

// curriculum map
std::string span(const std::vector<Date> &days);
bool item_school_year(const Value &x, int &year);
std::vector<int> school_years(const std::vector<const Value *> &items);
struct Unit {
    std::string unit;
    std::vector<Date> days;
    std::vector<const Value *> lessons, assessments;
    std::vector<std::string> standards, questions;
};
std::vector<Unit> curriculum_units(store::Store &st, const std::string &course, int year = 0);   // year 0 = every year
Value curriculum_doc(store::Store &st, const std::string &course, const std::string &subject = "", const std::string &grades = "",
                     int year = 0);
std::vector<std::vector<std::string>> curriculum_rows(store::Store &st, const std::string &course, int year = 0);

}  // namespace doc
