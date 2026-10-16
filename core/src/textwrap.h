// Python's textwrap.wrap, exactly (default options: tabs expanded, whitespace turned into spaces,
// long words broken, breaks allowed after hyphens), so text exports match the Python Chalkboard.
#pragma once
#include <string>
#include <vector>

namespace textwrap {

std::vector<std::string> wrap(const std::string &text, long width, const std::string &initial_indent = "",
                              const std::string &subsequent_indent = "");

}  // namespace textwrap
