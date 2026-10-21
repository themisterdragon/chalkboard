#include "homedays.h"

#include <algorithm>
#include <cmath>

#include "store.h"
#include "text.h"

namespace homedays {

const std::vector<std::string> MODES = {"Phone call", "Email", "Text or Remind", "Google Classroom", "Video call", "In person", "Other"};

int allotted(const Value &settings) { return (int)std::max<int64_t>(0, std::min<int64_t>(60, settings.get("home_days_allotted").as_int())); }

static bool same_class(const Value &l, const std::string &course) { return text::lower(store::class_of(l)) == text::lower(text::strip(course)); }

bool is_home(const Value &l) { return l.get("home_ready").truthy() || l.get("home_slot").as_int() > 0; }
bool used(const Value &l) { return !text::strip(store::field(l, "home_used")).empty(); }

std::string unit_on(const Value &lessons, const std::string &course, const Date &d) {
    std::string unit;
    long best = -1;   // the latest day on or before d
    for (const auto &l : lessons.arr()) {
        if (!l.is_object() || is_home(l) || !same_class(l, course)) continue;
        for (const auto &x : store::lesson_dates(l))
            if (x.ordinal() <= d.ordinal() && x.ordinal() > best) {
                best = x.ordinal();
                unit = text::strip(store::field(l, "unit"));
            }
    }
    return unit;
}

std::vector<const Value *> bank(const Value &lessons, const std::string &course, const std::string *unit) {
    std::vector<const Value *> out;
    for (const auto &l : lessons.arr())
        if (l.is_object() && is_home(l) && same_class(l, course) && (!unit || text::strip(store::field(l, "unit")) == *unit)) out.push_back(&l);
    std::stable_sort(out.begin(), out.end(), [](const Value *a, const Value *b) {
        if (used(*a) != used(*b)) return !used(*a);
        auto ua = store::unit_sort_key(store::field(*a, "unit")), ub = store::unit_sort_key(store::field(*b, "unit"));
        if (ua < ub || ub < ua) return ua < ub;
        return text::lower(store::field(*a, "title")) < text::lower(store::field(*b, "title"));
    });
    return out;
}

const Value *pick(const Value &lessons, const std::string &course, const Date &d) {
    std::string unit = unit_on(lessons, course, d);
    for (const Value *l : bank(lessons, course, &unit))
        if (!used(*l)) return l;
    return nullptr;
}

const Value *find(const Value &data, int n) {
    for (const auto &r : data.get("home_days").arr())
        if (r.is_object() && r.get("number").as_int() == n) return &r;
    return nullptr;
}

Value &make(Value &data, int n) {
    Value &all = data["home_days"];
    if (!all.is_array()) all = Value::array();
    for (auto &r : all.arr_ref())
        if (r.is_object() && r.get("number").as_int() == n) return r;
    Value r = Value::object();
    r["number"] = n;
    r["date"] = "";
    r["classes"] = Value::array();
    r["contacts"] = Value::array();
    all.push(r);
    return all.arr_ref().back();
}

Date date_of(const Value &rec) {
    Date d;
    if (!dates::stamp_day(rec.s("date"), d)) return Date{};
    return d;
}

const Value *on_date(const Value &data, const Date &d) {
    for (const auto &r : data.get("home_days").arr())
        if (r.is_object() && date_of(r) == d) return &r;
    return nullptr;
}

int next_unused(const Value &data, const Value &settings) {
    for (int n = 1; n <= allotted(settings); n++) {
        const Value *r = find(data, n);
        if (!r || !date_of(*r).valid()) return n;
    }
    return 0;
}

std::vector<std::pair<std::string, std::string>> log_classes(const Value &settings, const Value &lessons) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &p : settings.get("class_periods").arr())
        if (p.is_object()) out.push_back({text::strip(store::field(p, "name")), text::strip(store::field(p, "course"))});
    if (!out.empty()) return out;
    std::vector<const Value *> all;
    for (const auto &l : lessons.arr())
        if (l.is_object()) all.push_back(&l);
    for (const auto &c : store::classes(settings, all))
        if (!c.first.empty()) out.push_back({c.first, c.first});
    return out;
}

std::string percent(long participating, long enrolled) {
    if (enrolled <= 0) return "";
    return std::to_string((long)std::lround((double)participating * 100.0 / (double)enrolled)) + "%";
}

long count(const Value &row, const char *key) {
    const Value &v = row.get(key);
    if (v.is_number()) return std::max<long>(0, (long)v.as_int());
    std::string s = text::strip(v.str());
    long n = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return 0;
        n = std::min(100000L, n * 10 + (ch - '0'));
    }
    return n;
}

}  // namespace homedays
