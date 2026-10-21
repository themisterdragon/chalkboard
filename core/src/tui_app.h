// The terminal app's screens (port of app.py): the menu, lessons, assessments, standards, exports,
// and settings. Lessons and assessments are found by id each time they're used, never held by
// reference, so a list growing or an import replacing everything can't leave one pointing at
// freed memory.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "json.h"
#include "schoolcal.h"
#include "store.h"
#include "term.h"
#include "tui.h"

namespace tui {

std::string grade_label(const std::string &g);
std::string first_line(const std::string &s);

class App {
public:
    App(term::Screen &screen, store::Store &st, bool no_boot = false);
    void run();
    std::function<void()> leave;   // set when running inside the window app: back to its windows

private:
    term::Screen &screen_;
    store::Store &store_;
    UI ui;
    bool no_boot_;
    std::string std_subject_;   // the subject last picked this session ("" = none yet)
    size_t codew_ = 13;

    json::Value &st() { return store_.settings(); }
    json::Value *lesson(const std::string &id);
    json::Value *assessment(const std::string &id);
    void save();

    // menu
    void boot();
    void main_menu();

    // lists of lessons and assessments
    struct Filter {
        std::string q, unit, kind, sort;
        bool has_kind = false;
        bool has_cls = false;   // sorted By Class and a class picked
        std::string cls;
    };
    std::vector<std::string> filtered(const std::string &which, const Filter &f, const std::vector<std::string> &fields);
    std::string filter_info(const Filter &f, size_t count, const std::string &noun);
    bool filter_key(const std::string &c, Filter &f, const std::string &which);
    void class_list(const std::string &title, Filter &f, const std::string &which, const std::function<std::vector<std::string>()> &items,
                    const std::function<UI::Row(const std::string &, int)> &row, const std::function<Result(const std::string &, long)> &on_open,
                    const std::function<Result(int, const std::string &, long)> &on_key, const std::string &noun, const std::function<std::string()> &info,
                    const std::string &hints, const std::string &empty);
    Result rename(const std::string &which, const std::string &id, const std::string &noun, const std::function<std::vector<std::string>()> &items);
    void preview(const json::Value &doc);

    // lessons
    void lessons_screen();
    void lesson_editor(const std::string &id);
    void lesson_sections();
    void lesson_attachments(const std::string &lesson_id);
    std::function<bool(const Date &)> lesson_marks();

    // standards
    std::string std_subject();
    std::vector<size_t> std_items(const std::string &grades, const std::string &query, const std::string &subject);
    UI::Row std_row(size_t i, const std::string &mark = "");
    bool pick_grade(const std::string &current, std::string &out);
    bool std_filter_keys(const std::string &c, std::string &grade, std::string &subject);
    std::string std_filter_info(const std::string &grade, const std::string &subject, const std::string &q);
    void std_detail(const std::string &code);
    std::vector<std::string> standards_picker(const std::vector<std::string> &selected, const std::string &grades);
    void standards_library();
    void import_standards();
    void export_standards();
    void remove_standards();

    // assessments
    void assessments_screen();
    UI::Row assessment_row(const json::Value &a, int w);
    std::string new_assessment_flow(const std::string &lesson_id = "", const std::string &unit = "");
    std::string add_assessment(const std::string &kind, const std::string &title, const std::string &lesson_id = "");
    void open_assessment(const std::string &id);
    void sheet_editor(const std::string &id);
    void assessment_editor(const std::string &id);
    void assessment_settings(const std::string &id);
    std::string default_std(const json::Value &a);
    bool add_question(const std::string &aid, json::Value &q);
    struct QField { std::string key, label, kind; };
    std::vector<QField> question_fields(const json::Value &q);
    // a question is found through q() each time: it may be in an assessment's list, or new and not added yet
    using QRef = std::function<json::Value *()>;
    void question_editor(const std::string &aid, const QRef &q);
    void choices_editor(const QRef &q);
    void pairs_editor(const QRef &q);

    // exports
    void export_screen(const std::string &kind, const std::string &id);
    void day_slideshow();
    void curriculum_map(const std::string *course);
    void export_now(const std::string &lesson_id, const std::string &fmt = "PNG");
    std::vector<std::string> do_export(const std::string &kind, const std::string &id, const std::string &fmt);
    void export_done(const std::vector<std::string> &files);

    // settings
    void board_settings();
    void board_sections();
    void font_picker(const std::string &key);
    void board_preview();
    void logo_settings();
    void class_periods();
    void period_editor(size_t i);
    // the school calendar and moving lessons by school days (tui_schoolcal.cpp)
    void school_calendar();
    bool edit_day_off(schoolcal::DayOff &o);
    void shift_lessons(const Date *from = nullptr);
    void undo_shift();
    std::vector<schoolcal::Move> undo_moves_;
    void backup_now();
    void export_everything();
    void import_backup();
    void pick_mascot();
    void page_layouts();
    void page_layout(const std::string &kind);
    void settings_screen();
};

// The terminal app on a real terminal. Returns the exit code.
int run_terminal(bool no_boot);

}  // namespace tui
