// Exporting lessons and assessments to files (port of exporting.py). Shared by the terminal and
// window apps; no screen code here.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "dates.h"
#include "json.h"
#include "store.h"

namespace exporting {

struct ExportError {
    std::string message;
};

// progress(done, total, file name): before each file, and once more at the end with ""
using Progress = std::function<void(int, int, const std::string &)>;

extern const std::vector<std::string> LESSON_FORMATS;       // PDF, DOCX, TXT, PNG, MAKEUP, ALL
extern const std::vector<std::string> ASSESSMENT_FORMATS;   // PDF, DOCX, TXT, ALL
extern const std::vector<std::pair<std::string, std::string>> MAP_FORMATS;

std::string safe_name(const std::string &s);
std::vector<std::string> export_subdirs(const std::string &kind, const json::Value &obj);
std::string export_folder(store::Store &st, const std::string &kind, const json::Value &obj, const std::string &root = "");
std::string subfolder(const std::string &name, const std::string &fmt);
std::string files_folder(const std::vector<std::string> &files);
// [(file name, document)] for an assessment, per the student copy/key and versions settings
std::vector<std::pair<std::string, json::Value>> assessment_docs(store::Store &st, const json::Value &a);

// Writes the files for a lesson ("lesson") or assessment ("assessment") in one format (or ALL).
// Returns their paths; throws ExportError with a message for the teacher.
std::vector<std::string> export_item(store::Store &st, const std::string &kind, const json::Value &obj, const std::string &fmt,
                                     const Progress &progress = nullptr, const std::string &root = "");
// A day's board slides, class period by class period: [(period, lesson or null)]
std::vector<std::string> export_day(store::Store &st, const std::vector<std::pair<json::Value, const json::Value *>> &picks, const Date &day,
                                    const std::string &root = "");
// A class's curriculum map (all_classes: every class in one map); fmts from PDF, DOCX, CSV
std::vector<std::string> export_map(store::Store &st, const std::string &course, bool all_classes, const std::vector<std::string> &fmts,
                                    const std::string &subject = "", const std::string &grades = "", const std::string &root = "", int year = 0);
struct Everything {
    std::string folder;
    std::vector<std::string> files, problems;
};
Everything export_everything(store::Store &st, const std::string &parent, const Progress &progress = nullptr);

std::string csv_text(const std::vector<std::vector<std::string>> &rows);   // Excel-style CSV, \r\n lines

}  // namespace exporting
