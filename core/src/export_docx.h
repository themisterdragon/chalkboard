// Dependency-free .docx output (opens in Word, Google Docs, LibreOffice, Pages). Port of export_docx.py.
#pragma once
#include <string>

#include "json.h"

namespace export_docx {

bool render_docx(const json::Value &doc, const std::string &path, const std::string &family = "Times", const std::string &page = "Letter",
                 std::string *err = nullptr);
// the parts of the file, for checking: [(name, xml)]
std::vector<std::pair<std::string, std::string>> parts(const json::Value &doc, const std::string &family, const std::string &page);

}  // namespace export_docx
