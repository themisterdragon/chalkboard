// Putting Chalkboard on this computer: the setup wizard's last step (port of gui/install.py).
//   Windows: copy Chalkboard.exe into %LOCALAPPDATA%\Programs\Chalkboard, with Start-menu (and
//            desktop) shortcuts.
//   macOS:   running from the downloaded disk image, copy the app to Applications.
//   Linux:   copy the program to ~/.local/bin/chalkboard, with an app-menu entry and icon.
// Anything already installed (or run from a build folder) has nothing to do.
#pragma once
#include <string>

namespace install {

struct Plan {
    std::string kind;     // "windows", "mac", "linux" ("" = nothing to do)
    std::string source, target, text;
};
std::string self_path();   // this program's file (the .app folder on a Mac)
Plan plan();
// Does it; returns a short note for the teacher. Throws install::Error.
struct Error {
    std::string message;
};
std::string install(const Plan &p, bool desktop_shortcut = true);
bool relaunch(const Plan &p);   // macOS: open the copy in Applications

}  // namespace install
