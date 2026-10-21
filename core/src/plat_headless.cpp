// A window with no screen: scripted events in, pictures out (for tests and screenshots).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "files.h"
#include "plat.h"

namespace plat {

namespace {

class Headless : public Window {
public:
    int w, h;
    double k;
    std::function<bool(Event &)> feed;
    std::function<void(const raster::Image &)> shown;
    std::string clip;
    bool ended = false;
    double scale() override { return k; }
    void size(int &ow, int &oh) override {
        ow = w;
        oh = h;
    }
    void present(const raster::Image &img) override {
        frames++;
        if (shown) shown(img);
    }
    // timing for speed tests: time spent working (not waiting for the script), and frames drawn
    using Clock = std::chrono::steady_clock;
    Clock::time_point back{};
    double busy_ms = 0;
    long frames = 0, events = 0;
    ~Headless() override {
        if (std::getenv("CHALKBOARD_TIMING"))
            std::fprintf(stderr, "timing: %ld events, %ld frames, %.0f ms working, %.2f ms per event\n", events, frames, busy_ms,
                         events ? busy_ms / events : 0.0);
    }
    bool fresh = false;   // an event went out since the last wait
    bool next(Event &e, int timeout) override {
        if (back != Clock::time_point{}) busy_ms += std::chrono::duration<double, std::milli>(Clock::now() - back).count();
        struct Back {
            Clock::time_point &t;
            ~Back() { t = Clock::now(); }
        } on_return{back};
        // one event per wait, so each one gets drawn (like a person, not a flood)
        if (timeout == 0 && fresh) return false;
        fresh = true;
        events++;
        if (ended) {
            e = Event();
            e.type = Event::CLOSE;
            return true;
        }
        if (!feed(e)) {
            ended = true;
            e = Event();
            e.type = Event::CLOSE;
        }
        if (e.type == Event::RESIZE) {
            w = (int)e.x;
            h = (int)e.y;
        }
        return true;
    }
    void wake() override {}
    void title(const std::string &) override {}
    void cursor(Cursor) override {}
    std::string clipboard() override { return clip; }
    void set_clipboard(const std::string &s) override { clip = s; }
    void minimize() override {}
    void maximize(bool) override {}
    bool maximized() override { return false; }
    void min_size(int, int) override {}
    void dark_title(bool) override {}
    std::vector<std::string> picks;   // what the file pickers answer, in turn
    std::string pick() {
        if (picks.empty()) return "";
        std::string p = picks.front();
        picks.erase(picks.begin());
        return p;
    }
    std::string open_file(const std::string &, const std::vector<Filter> &) override { return pick(); }
    std::string save_file(const std::string &, const std::string &) override { return pick(); }
    std::string choose_folder(const std::string &) override { return pick(); }
};

// a script's steps, one at a time
struct Script {
    std::vector<std::string> steps;
    size_t at = 0;
    raster::Image last;
    std::string dir;
    Headless *win = nullptr;
    bool next(Event &e) {
        while (at < steps.size()) {
            std::istringstream in(steps[at++]);
            std::string op;
            in >> op;
            e = Event();
            if (op == "shot") {
                std::string name;
                in >> name;
                files::write(dir + "/" + name + ".png", last.png());
                continue;
            }
            if (op == "pick") {   // the next file picker's answer
                std::string p;
                std::getline(in, p);
                if (!p.empty() && p[0] == ' ') p = p.substr(1);
                win->picks.push_back(p);
                continue;
            }
            if (op == "wait") {   // let time pass (timers)
                int ms = 0;
                in >> ms;
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                e.type = Event::WAKE;
                return true;
            }
            if (op == "size") {
                e.type = Event::RESIZE;
                in >> e.x >> e.y;
                return true;
            }
            if (op == "click" || op == "dbl" || op == "rclick" || op == "up" || op == "down" || op == "move") {
                in >> e.x >> e.y;
                if (op == "move") {
                    e.type = Event::MOVE;
                    return true;
                }
                // a click: press now, release next time
                e.type = op == "up" ? Event::UP : Event::DOWN;
                e.button = op == "rclick" ? 3 : 1;
                e.clicks = op == "dbl" ? 2 : 1;
                if (op == "click" || op == "rclick" || op == "dbl")
                    steps.insert(steps.begin() + (long)at, "up " + std::to_string(e.x) + " " + std::to_string(e.y) + (op == "rclick" ? " r" : ""));
                if (op == "up") {
                    std::string r;
                    in >> r;
                    if (r == "r") e.button = 3;
                }
                return true;
            }
            if (op == "wheel") {
                e.type = Event::WHEEL;
                in >> e.x >> e.y >> e.dy;
                return true;
            }
            if (op == "type") {
                e.type = Event::TEXT;
                std::getline(in, e.text);
                if (!e.text.empty() && e.text[0] == ' ') e.text = e.text.substr(1);
                return true;
            }
            if (op == "key") {
                std::string name, mod;
                in >> name >> mod;
                e.type = Event::KEY;
                static const std::vector<std::pair<std::string, int>> K = {
                    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"home", K_HOME}, {"end", K_END},
                    {"pgup", K_PGUP}, {"pgdn", K_PGDN}, {"return", K_RETURN}, {"escape", K_ESCAPE}, {"tab", K_TAB},
                    {"backspace", K_BACKSPACE}, {"delete", K_DELETE}, {"space", ' '}, {"f10", K_F10}};
                e.key = name.size() == 1 ? name[0] : 0;
                for (auto &k : K)
                    if (k.first == name) e.key = k.second;
                if (mod.find("shift") != std::string::npos) e.mods |= SHIFT;
                if (mod.find("ctrl") != std::string::npos) e.mods |= CTRL;
                if (mod.find("alt") != std::string::npos) e.mods |= ALT;
                if (mod.find("cmd") != std::string::npos) e.mods |= CMD;
                return true;
            }
        }
        return false;
    }
};

}  // namespace

std::unique_ptr<Window> scripted(int w, int h, double scale, const std::vector<std::string> &steps, const std::string &outdir) {
    auto sc = std::make_shared<Script>();
    sc->steps = steps;
    sc->dir = outdir;
    std::unique_ptr<Headless> win(new Headless());
    win->w = w;
    win->h = h;
    win->k = scale;
    sc->win = win.get();
    win->feed = [sc](Event &e) { return sc->next(e); };
    win->shown = [sc](const raster::Image &img) { sc->last = img; };
    return std::unique_ptr<Window>(win.release());
}

std::unique_ptr<Window> headless(int w, int h, double scale, std::function<bool(Event &)> feed, std::function<void(const raster::Image &)> shown) {
    std::unique_ptr<Headless> win(new Headless());
    win->w = w;
    win->h = h;
    win->k = scale;
    win->feed = std::move(feed);
    win->shown = std::move(shown);
    return std::unique_ptr<Window>(win.release());
}

}  // namespace plat
