// Self-checks for development: print what the core works out as JSON, so scripts/check_core.py can
// compare it with the Python Chalkboard line by line. Not used by teachers.
#pragma once
#include <string>

namespace check {
int dump(const std::string &what, const std::string &arg);
}
