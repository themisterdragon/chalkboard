// chalkboard-core: Chalkboard's engine. Run on its own, it's the terminal app.
#include <cstdio>
#include <cstring>
#include <string>

#include "art.h"
#include "check.h"
#include "files.h"
#include "fontlib.h"
#include "gui.h"
#include "term.h"
#include "tui_app.h"
#include "ui.h"
#include "version.h"

int main(int argc, char **argv) {
    bool no_boot = false;
    // chalkboard-gui (or --window) opens the window app; a program started without a terminal
    // (double-clicked) does too
    std::string me = argc > 0 ? files::basename(argv[0]) : "";
    int window = me.find("gui") != std::string::npos ? 1 : -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::printf("Chalkboard %s\n", CHALKBOARD_VERSION);
            return 0;
        }
        if (a == "--dump" && i + 1 < argc) return check::dump(argv[i + 1], i + 2 < argc ? argv[i + 2] : "");
        if (a == "--no-boot") no_boot = true;
        else if (a == "--window") window = 1;
        else if (a == "--fonts") {   // which fonts Chalkboard can see (for a font that doesn't show)
            for (const auto &d : fontlib::font_dirs()) std::printf("folder: %s\n", d.c_str());
            for (const auto &f : fontlib::system_font_order()) std::printf("%s\n", f.c_str());
            return 0;
        }
        else if (a == "--write-icon" && i + 1 < argc) {   // packaging: the app icon as .ico, .icns, or .png
            std::string out = argv[++i], ext = files::ext(out);
            std::string data = ext == ".ico" ? art::logo_ico() : ext == ".icns" ? art::logo_icns() : art::logo_png(1024, 96);
            return files::write(out, data) ? 0 : 1;
        }
        else if (a == "--script" && i + 2 < argc) {   // tests: the window app run by a script, no screen needed
            std::string script = argv[++i], out = argv[++i];
            return gui::run_script(script, out, no_boot);
        }
        else if (a == "--check-contrast") {   // tests: every look's colors meet WCAG AA (with sample accents)
            int bad = 0;
            for (const auto &sk : ui::skins())
                for (bool dark : {false, true})
                    for (const char *acc : {"", "#7A0019", "#FFCC00", "#4CBB17", "#333333", "#1F5A3A", "#FFFFFF", "#0057B8", "#FF69B4", "#007AFF",
                                            "#F2E6A0", "#000000"}) {
                        if (sk[0] != "modern" && *acc) continue;
                        for (const auto &p : ui::contrast_problems(sk[0], dark, acc)) {
                            std::printf("%s %s %s: %s\n", sk[0].c_str(), dark ? "dark" : "light", *acc ? acc : "(default)", p.c_str());
                            bad++;
                        }
                    }
            std::printf(bad ? "%d problems\n" : "every look passes\n", bad);
            return bad ? 1 : 0;
        }
        else if (a == "--terminal") window = 0;
        else if (a.rfind("-psn_", 0) == 0) {   // macOS: older Finders add this when they open the app
        }
        else if (a == "--no-plugins") {   // Chalkboard 2.x had plugins; 3 has none, so there is nothing to switch off
        } else if (a == "-h" || a == "--help") {
            std::printf("chalkboard: lesson planner & assessment builder\n\n"
                        "  --window      open in a window (chalkboard-gui does too)\n"
                        "  --terminal    run in this terminal\n"
                        "  --no-boot     skip the boot sequence\n"
                        "  --version     show the version\n");
            return 0;
        } else {
            std::fprintf(stderr, "chalkboard: unknown option %s (try --help)\n", argv[i]);
            return 2;
        }
    }
    if (window < 0) window = term::in_terminal() ? 0 : 1;
    if (window == 1) {
        int r = gui::run_window(no_boot, false);
        if (r >= 0) return r;
        if (!term::in_terminal()) return 1;
        std::fprintf(stderr, "chalkboard: no window can open here, so it's running in the terminal.\n");
    }
    return tui::run_terminal(no_boot);
}
