// Plain-text output: easy to paste into Google Classroom, an LMS, or an email (port of export_txt.py).
#pragma once
#include <string>
#include <vector>

#include "json.h"

namespace export_txt {

const int WIDTH = 78;
json::Value unmark(const json::Value &b);
std::vector<std::string> render_lines(const json::Value &doc, int width = WIDTH);
bool render_txt(const json::Value &doc, const std::string &path, std::string *err = nullptr);

}  // namespace export_txt
