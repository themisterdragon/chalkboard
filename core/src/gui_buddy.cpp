// The school mascot as a chibi in the window app (port of gui/buddy.py): it waits in a corner of
// the home screen and cheers when it's clicked or an export finishes.
#include "art.h"
#include "gui.h"

namespace gui {

using namespace ui;

static std::string cheer_text(const std::string &key) {
    std::string n = art::name(key);
    return n.empty() ? "Nice work!" : "GO " + n + "!";
}

Buddy::~Buddy() {
    if (app)
        for (int j : jobs) app->ui.cancel(j);
}

void Buddy::cheer(App &a) {
    app = &a;
    std::string key = a.settings().s("mascot");
    if (chibi::frame(key).empty()) return;
    for (int j : jobs) a.ui.cancel(j);
    jobs.clear();
    // hop twice with arms up, say the cheer, then stand again
    arms = true;
    hop = 1;
    talking = true;
    a.ui.again();
    struct Step {
        int ms, hop;
        bool arms, talk;
    };
    for (Step s : {Step{140, 0, true, true}, Step{260, 1, true, true}, Step{400, 0, false, true}, Step{2200, 0, false, false}})
        jobs.push_back(a.ui.after(s.ms, [this, s] {
            hop = s.hop;
            arms = s.arms;
            talking = s.talk;
        }));
}

Rect Buddy::draw(App &a, Ui &ui, double r, double b, int scale, bool bubble) {
    app = &a;
    Skin &k = ui.sk();
    int S = ui.S();
    const Value &st = a.settings();
    std::string key = st.s("mascot");
    auto rows = chibi::frame(key, arms);
    if (rows.empty()) return {r, b, 0, 0};
    auto pal = chibi::colors(key, st.s("primary_color"), st.s("secondary_color"));
    std::vector<std::pair<char, Color>> cols;
    for (const auto &p : pal) cols.push_back({p.first, hex(p.second)});
    double size = chibi::SIZE * scale;
    Rect box{r - size, b - size - 30 * S, size, size + 30 * S};
    if (ui.has(plat::Event::DOWN) && ui.ev.button == 1 && ui.over({box.x, box.y, box.w, box.h})) {
        ui.consume();
        cheer(a);
    }
    if (ui.over(box)) ui.cursor(plat::HAND);
    if (!ui.drawing) return box;
    double dy = hop ? 10 * S : 0;
    ui.pixel_art(rows, [&](char c, Color &out) {
        for (const auto &p : cols)
            if (p.first == c) { out = p.second; return true; }
        return false;
    }, r - size, b - size - dy, scale);
    if (talking && bubble) {
        std::string t = cheer_text(key);
        Font &f = *k.fb;
        double tw = f.width(t);
        Rect bb{r - size - tw - 24 * S, b - size - 4 * S, tw + 12 * S, f.line + 8 * S};
        ui.fill(bb, k["field"]);
        ui.box(bb, k["edge"], S);
        ui.text(f, bb.x + 6 * S, bb.y + 4 * S + f.ascent, t, k["text"]);
    }
    return box;
}

}  // namespace gui
