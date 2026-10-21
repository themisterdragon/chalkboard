// Local storage for lessons, assessments, settings, and the standards library (port of store.py).
// Lessons, assessments, and settings stay JSON values, so data.json reads and writes exactly
// like the Python Chalkboard's.
#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dates.h"
#include "images.h"
#include "json.h"

namespace store {

using json::Value;

// A problem to show the teacher (Python's ValueError/OSError with a message).
struct UserError {
    std::string message;
};

extern const char *DEFAULT_SUBJECT;
extern const char *ALL;
extern const std::vector<std::string> GRADE_CHOICES;
extern const char *GOOD_THINGS_PREFIX;
extern const char *BACKUP_KIND;
extern const char *MY_STANDARDS;
extern const char *LOGO_FILE;
const int SAFETY_BACKUPS = 10;

bool is_beta();
std::string data_dir(int beta = -1);   // -1: this build's
void seed_beta();
std::string default_export_dir();
std::string default_backup_dir();

const Value &default_settings();

// list sort orders: key -> menu label
extern const std::vector<std::pair<std::string, std::string>> SORTS;

struct BoardSection { std::string key, label, col; };
extern const std::vector<BoardSection> BOARD_SECTIONS;
extern const std::vector<std::pair<std::string, std::string>> BOARD_SIDES;
// Board Designer choices: setting -> [(value, label)], first is the default
extern const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> BOARD_CHOICES;
extern const std::vector<std::pair<std::string, std::string>> BOARD_STYLES;
extern const std::vector<std::pair<std::string, std::string>> LOGO_PLACES;
std::vector<BoardSection> board_sections(const Value &settings);
bool move_board_section(Value &settings, const std::string &key, int step);
std::vector<Value> periods_for(const Value &settings, const Value &lesson);

// dates on lessons
std::vector<Date> lesson_dates(const Value &x);
// what this district calls a day of school at home ("NTI Day"; usually "At-Home Day")
std::string home_day_name(const Value &settings);
std::vector<std::pair<Value, const Value *>> day_plan(const Value &settings, const std::vector<const Value *> &lessons, const Date &day);

bool grade_span(const std::string &g, int &lo, int &hi);
bool grades_match(const std::string &std_grades, const std::string &wanted);
std::string parse_hex(const std::string &s);   // "" when it isn't a hex color
std::string clean_path(const std::string &path);

Value normalize_data(Value d);

struct Backup {
    Value data, standards;
    std::string made;
    bool has_logo = false;
    images::Image logo;
};
Backup read_backup(const std::string &path);   // throws UserError
std::string subject_filename(const std::string &subject, const std::string &folder);
Value read_standards_file(const std::string &path);   // [{"subject","source","standards"}]; throws UserError
Value json_subject(const Value &d, const std::string &stem, const std::string &source);
Value csv_subjects(const std::string &raw, const std::string &stem, const std::string &source);
std::vector<std::vector<std::string>> read_csv(const std::string &raw, char delimiter, bool skip_initial_space);
char sniff_delimiter(const std::string &sample, bool &skip_initial_space);

struct LessonField { std::string key, label, kind; };
extern const std::vector<LessonField> LESSON_FIELDS;
extern const std::vector<std::string> FIXED_FIELDS;
extern const char *VOCAB_HINT;

extern const std::vector<std::string> ASSESSMENT_KINDS;
extern const std::vector<std::string> MAKEUP_HELD;
extern const char *ANNOTATION;
extern const char *BELL_SHEET;
extern const std::vector<std::string> SHEET_KINDS;
extern const std::vector<std::pair<std::string, double>> ANNOTATION_COLS;
extern const std::vector<std::string> WEEKDAYS;

// Advanced Mode > Page Layouts
struct LayoutOption { std::string key, label, type; Value dflt; };
struct PageKind { std::string kind, name; std::vector<LayoutOption> options; };
extern const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> LAYOUT_CHOICES;
const std::vector<PageKind> &page_layouts();
const PageKind *page_kind(const std::string &kind);
Value page_layout(const Value &settings, const std::string &kind);
void set_page_layout(Value &settings, const std::string &kind, const Value &values);
std::vector<std::string> layout_fields(const std::string &text, size_t most = 6);
extern const std::vector<std::string> BOARD_DESIGN_KEYS;
std::vector<std::string> advanced_changes(const Value &settings);
void reset_advanced(Value &settings, const std::string &what = "");   // "" = everything

// questions
struct QuestionType { std::string type, label, desc; };
extern const std::vector<QuestionType> QUESTION_TYPES;
std::string type_label(const std::string &t);
std::string type_tag(const std::string &t);
extern const std::vector<std::string> FRAYER_HEADS;
extern const std::vector<std::string> PLOT_HEADS;
struct ChartPreset { std::string key, label; Value settings; };
const std::vector<ChartPreset> &chart_presets();
extern const std::vector<std::pair<std::string, std::string>> CHART_DIRECTIONS;
std::string chart_direction(const std::string &layout);
extern const std::vector<int> CHART_SIZES;
Value &chart_preset(Value &q, const std::string &key);
std::string chart_size_label(int n);

std::string new_id();
std::string now();
bool has_content(const Value &lesson, const std::string &key);
std::vector<LessonField> shown_fields(const Value &settings, const Value &lesson);
std::vector<std::pair<std::string, std::string>> vocab_pairs(const std::string &text);
Value new_lesson(const Value &settings);
Value new_assessment(const Value &settings, const std::string &kind = "Quiz");
Value new_question(const std::string &qtype, const std::string &default_std = "");
double points_of(const Value &q);
Value parse_points(const std::string &s, const Value &old);
std::string fmt_points(double p);

// sorting and grouping lists
struct UnitKey {
    int group;            // 0: has a number, 1: doesn't
    std::string number;   // digits, no leading zeros
    std::string low;
    bool operator<(const UnitKey &o) const;
};
UnitKey unit_sort_key(const std::string &u);
std::string class_of(const Value &x);
std::vector<std::pair<std::string, int>> classes(const Value &settings, const std::vector<const Value *> &items);
bool in_class(const Value &x, const std::string &name);
std::vector<const Value *> sort_items(std::vector<const Value *> items, std::string how);

// a value as text the way Python's str() would show it inside Chalkboard ("" for None)
std::string text_of(const Value &v);
// x.get(key) or "" (Python's `x.get(key) or ""` when the value should be text)
std::string field(const Value &x, const std::string &key);

class Store {
public:
    Store();                     // uses data_dir()
    explicit Store(const std::string &dir);

    std::string dir, path;
    std::string warning;         // a problem to show once ("" = none)
    Value data;
    Value &settings() { return data["settings"]; }
    const Value &settings() const { return data.get("settings"); }

    void load();
    bool save(std::string *err = nullptr);
    std::string export_dir() const;

    // school logo (board slides)
    const images::Image *logo();
    const images::Image &set_logo(const std::string &path);   // throws images::ImageError
    const images::Image &set_logo_image(const images::Image &img);
    void remove_logo();

    // backups
    std::string backup_dir() const;
    std::string backup(const std::string &folder = "", bool remember = true);   // throws UserError
    Value import_backup(const std::string &path, bool replace = false);         // throws UserError

    // lessons <-> assessments
    Value *assessment(const std::string &aid);
    const Value *assessment(const std::string &aid) const;
    std::vector<const Value *> attached(const Value &lesson) const;
    Value *vocab_quiz(Value &lesson);
    std::vector<std::string> units(const std::vector<const Value *> &items) const;

    // standards
    std::string standards_dir() const;
    const std::vector<Value> &load_kas();
    std::vector<std::string> imported_subjects();
    Value import_standards(const std::string &path);   // [[subject, count, skipped]]; throws UserError
    std::vector<std::string> shareable_subjects();
    std::string export_standards(const std::string &folder, const std::vector<std::string> &subjects);
    void remove_subject(const std::string &subject);
    void reload_standards(bool rescan = false);
    std::string std_text(const std::string &code) const;
    const Value *standard(const std::string &code) const;

    std::vector<std::string> subjects;
    std::vector<Value> standards;
    std::unordered_map<std::string, size_t> std_index;
    int kas_count = 0;

    std::string good_thing(const std::string &current = "");

private:
    bool backed_up_ = false;
    bool kas_loaded_ = false;
    std::vector<Value> kas_;
    bool logo_loaded_ = false;
    bool has_logo_ = false;
    images::Image logo_;
    std::vector<std::string> good_things_;
};

}  // namespace store
