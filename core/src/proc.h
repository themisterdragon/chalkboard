// Running the few local tools Chalkboard uses (turning a slide into a PNG, opening a folder).
// No shell is ever involved: arguments go to the program as they are.
#pragma once
#include <string>
#include <vector>

namespace proc {

std::string which(const std::string &name);   // full path of a program on PATH, or ""
// Runs argv (argv[0] a full path), waits up to timeout_s seconds, and returns true when it ran and
// exited with 0. Nothing is shown: no console window, and its output goes nowhere.
bool run(const std::vector<std::string> &argv, int timeout_s = 60);
// Starts argv and doesn't wait (opening a file or folder in its app).
bool start(const std::vector<std::string> &argv);
// Opens a file or folder the way double-clicking it would.
bool open_path(const std::string &path);
// Tests: open_path() only writes what it would open to standard error.
extern bool dry_run;

}  // namespace proc
