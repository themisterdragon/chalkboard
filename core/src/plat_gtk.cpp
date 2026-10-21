// Linux (and other Unix desktops): a GTK 3 window, with GTK loaded when the program starts, so
// the program still runs (as the terminal app) on a computer without it. Works on X11 and Wayland.
#if !defined(_WIN32) && !defined(__APPLE__)
#include <string>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>

#include "plat.h"

namespace plat {

namespace {

typedef void *gp;
typedef int gboolean;

// The parts of GDK's event structs that are read here (GTK 3's layout, which never changes)
struct GdkEventAny_ {
    int type;
    gp window;
    int8_t send_event;
};
struct GdkEventButton_ {
    int type;
    gp window;
    int8_t send_event;
    uint32_t time;
    double x, y;
    double *axes;
    unsigned state, button;
    gp device;
    double x_root, y_root;
};
struct GdkEventMotion_ {
    int type;
    gp window;
    int8_t send_event;
    uint32_t time;
    double x, y;
    double *axes;
    unsigned state;
    int16_t is_hint;
    gp device;
    double x_root, y_root;
};
struct GdkEventKey_ {
    int type;
    gp window;
    int8_t send_event;
    uint32_t time;
    unsigned state, keyval;
    int length;
    char *string;
    uint16_t hardware_keycode;
    uint8_t group;
};
struct GdkEventScroll_ {
    int type;
    gp window;
    int8_t send_event;
    uint32_t time;
    double x, y;
    unsigned state;
    int direction;
    gp device;
    double x_root, y_root, delta_x, delta_y;
    unsigned is_stop : 1;
};
struct GdkEventWindowState_ {
    int type;
    gp window;
    int8_t send_event;
    int changed_mask, new_window_state;
};
struct Alloc {
    int x, y, width, height;
};

#define FN(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
#define GTK_FUNCS(X)                                                                         \
    X(gboolean, gtk_init_check, int *, char ***)                                             \
    X(gp, gtk_window_new, int)                                                               \
    X(gp, gtk_drawing_area_new, void)                                                        \
    X(void, gtk_container_add, gp, gp)                                                       \
    X(void, gtk_widget_show_all, gp)                                                         \
    X(void, gtk_widget_queue_draw, gp)                                                       \
    X(void, gtk_widget_set_can_focus, gp, gboolean)                                          \
    X(void, gtk_widget_grab_focus, gp)                                                       \
    X(void, gtk_widget_add_events, gp, int)                                                  \
    X(int, gtk_widget_get_scale_factor, gp)                                                  \
    X(int, gtk_widget_get_allocated_width, gp)                                               \
    X(int, gtk_widget_get_allocated_height, gp)                                              \
    X(gp, gtk_widget_get_window, gp)                                                         \
    X(void, gtk_widget_destroy, gp)                                                          \
    X(void, gtk_widget_set_size_request, gp, int, int)                                       \
    X(void, gtk_window_set_title, gp, const char *)                                          \
    X(void, gtk_window_set_default_size, gp, int, int)                                       \
    X(void, gtk_window_maximize, gp)                                                         \
    X(void, gtk_window_unmaximize, gp)                                                       \
    X(void, gtk_window_iconify, gp)                                                          \
    X(void, gtk_window_set_icon, gp, gp)                                                     \
    X(void, gtk_window_set_position, gp, int)                                                \
    X(gp, gtk_clipboard_get, gp)                                                             \
    X(void, gtk_clipboard_set_text, gp, const char *, int)                                   \
    X(char *, gtk_clipboard_wait_for_text, gp)                                               \
    X(gp, gtk_im_multicontext_new, void)                                                     \
    X(void, gtk_im_context_set_client_window, gp, gp)                                        \
    X(gboolean, gtk_im_context_filter_keypress, gp, gp)                                      \
    X(void, gtk_im_context_focus_in, gp)                                                     \
    X(void, gtk_im_context_focus_out, gp)                                                    \
    X(void, gtk_im_context_set_cursor_location, gp, const Alloc *)                           \
    X(gp, gtk_file_chooser_dialog_new, const char *, gp, int, const char *, ...)             \
    X(int, gtk_dialog_run, gp)                                                               \
    X(char *, gtk_file_chooser_get_filename, gp)                                             \
    X(void, gtk_file_chooser_set_do_overwrite_confirmation, gp, gboolean)                    \
    X(void, gtk_file_chooser_set_current_name, gp, const char *)                             \
    X(void, gtk_file_chooser_add_filter, gp, gp)                                             \
    X(gp, gtk_file_filter_new, void)                                                         \
    X(void, gtk_file_filter_set_name, gp, const char *)                                      \
    X(void, gtk_file_filter_add_pattern, gp, const char *)                                   \
    X(gboolean, gtk_events_pending, void)                                                    \
    X(gboolean, gtk_main_iteration_do, gboolean)                                             \
    X(gp, gdk_atom_intern, const char *, gboolean)                                           \
    X(gp, gdk_cursor_new_from_name, gp, const char *)                                        \
    X(gp, gdk_display_get_default, void)                                                     \
    X(const char *, gdk_display_get_name, gp)                                                \
    X(gboolean, gdk_event_get_pointer_emulated, gp)                                          \
    X(gp, gdk_event_get_source_device, gp)                                                   \
    X(int, gdk_device_get_source, gp)                                                        \
    X(void, gdk_window_set_cursor, gp, gp)                                                   \
    X(gp, gdk_pixbuf_new_from_data, const uint8_t *, int, gboolean, int, int, int, int, gp, gp) \
    X(unsigned long, g_signal_connect_data, gp, const char *, gp, gp, gp, int)               \
    X(unsigned, g_timeout_add, unsigned, gp, gp)                                             \
    X(unsigned, g_idle_add, gp, gp)                                                          \
    X(gboolean, g_source_remove, unsigned)                                                   \
    X(void, g_free, gp)                                                                      \
    X(void, g_object_unref, gp)                                                              \
    X(gp, g_settings_schema_source_get_default, void)                                        \
    X(gp, g_settings_schema_source_lookup, gp, const char *, gboolean)                       \
    X(gboolean, g_settings_schema_has_key, gp, const char *)                                 \
    X(void, g_settings_schema_unref, gp)                                                     \
    X(gp, g_settings_new, const char *)                                                      \
    X(char *, g_settings_get_string, gp, const char *)                                       \
    X(gp, cairo_image_surface_create_for_data, uint8_t *, int, int, int, int)                \
    X(void, cairo_surface_set_device_scale, gp, double, double)                              \
    X(void, cairo_set_source_surface, gp, gp, double, double)                                \
    X(void, cairo_paint, gp)                                                                 \
    X(void, cairo_surface_destroy, gp)
GTK_FUNCS(FN)
// optional: the portal file picker (the desktop's own, on KDE too), GTK 3.20+
gp (*gtk_file_chooser_native_new)(const char *, gp, int, const char *, const char *) = nullptr;
int (*gtk_native_dialog_run)(gp) = nullptr;

bool loaded = false, usable = false;

bool load() {
    if (loaded) return usable;
    loaded = true;
    void *h = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h) return false;
    void *gdk = dlopen("libgdk-3.so.0", RTLD_NOW | RTLD_GLOBAL), *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_GLOBAL),
         *gobj = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_GLOBAL), *gio = dlopen("libgio-2.0.so.0", RTLD_NOW | RTLD_GLOBAL),
         *cairo = dlopen("libcairo.so.2", RTLD_NOW | RTLD_GLOBAL), *pix = dlopen("libgdk_pixbuf-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *libs[] = {h, gdk, glib, gobj, gio, cairo, pix};
    auto find = [&](const char *name) -> void * {
        for (void *l : libs)
            if (l)
                if (void *p = dlsym(l, name)) return p;
        return nullptr;
    };
    bool ok = true;
#define LOAD(ret, name, ...)                    \
    *(void **)&name = find(#name);              \
    if (!name) ok = false;
    GTK_FUNCS(LOAD)
    *(void **)&gtk_file_chooser_native_new = find("gtk_file_chooser_native_new");
    *(void **)&gtk_native_dialog_run = find("gtk_native_dialog_run");
    if (!ok) return false;
    usable = gtk_init_check(nullptr, nullptr);
    return usable;
}

int key_of(unsigned kv) {
    switch (kv) {
    case 0xff51: case 0xff96: return K_LEFT;
    case 0xff52: case 0xff97: return K_UP;
    case 0xff53: case 0xff98: return K_RIGHT;
    case 0xff54: case 0xff99: return K_DOWN;
    case 0xff50: case 0xff95: return K_HOME;
    case 0xff57: case 0xff9c: return K_END;
    case 0xff55: case 0xff9a: return K_PGUP;
    case 0xff56: case 0xff9b: return K_PGDN;
    case 0xff0d: case 0xff8d: return K_RETURN;
    case 0xff1b: return K_ESCAPE;
    case 0xff09: case 0xfe20: return K_TAB;
    case 0xff08: return K_BACKSPACE;
    case 0xffff: case 0xff9f: return K_DELETE;
    case 0xff63: case 0xff9e: return K_INSERT;
    case 0xff67: return K_MENU;
    }
    if (kv >= 0xffbe && kv <= 0xffc9) return K_F1 + (int)(kv - 0xffbe);
    if (kv >= 0x20 && kv < 0x7f) return (int)(kv >= 'A' && kv <= 'Z' ? kv + 32 : kv);
    if (kv >= 0xa0 && kv <= 0xff) return (int)kv;
    if ((kv & 0xff000000) == 0x01000000) return (int)(kv & 0x00ffffff);   // Unicode keysyms
    return 0;
}

int mods_of(unsigned state) {
    int m = 0;
    if (state & 1) m |= SHIFT;
    if (state & 4) m |= CTRL;
    if (state & 8) m |= ALT;
    if (state & ((1u << 26) | (1u << 28))) m |= CMD;
    return m;
}

class Gtk : public Window {
public:
    gp win = nullptr, area = nullptr, im = nullptr;
    std::deque<Event> q;
    std::mutex mu;   // q, for wake() from another thread
    std::vector<uint8_t> buf;   // what's shown: cairo RGB24 (0x00RRGGBB per pixel)
    int bw = 0, bh = 0;
    int last_w = 0, last_h = 0;
    bool is_max = false, closed = false;
    std::string title_;

    void push(const Event &e) {
        std::lock_guard<std::mutex> lock(mu);
        q.push_back(e);
    }
    double sc() { return area ? gtk_widget_get_scale_factor(area) : 1; }

    static double ms_now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static bool timing() {
        static const bool on = std::getenv("CHALKBOARD_TIMING") != nullptr;
        return on;
    }
    static gboolean on_draw(gp, gp cr, gp self) {
        Gtk *g = (Gtk *)self;
        if (g->buf.empty()) return 1;
        double t0 = timing() ? ms_now() : 0;
        struct Done {
            Gtk *g;
            double t0;
            ~Done() {
                if (timing()) std::fprintf(stderr, "paint at %.0f: %.1f ms\n", t0, ms_now() - t0);
                g->painted = true;
                if (g->wants) {   // the program has a newer picture waiting to be drawn
                    g->wants = false;
                    Event e;
                    e.type = Event::WAKE;
                    g->push(e);
                }
            }
        } done{g, t0};
        gp s = cairo_image_surface_create_for_data(g->buf.data(), 1 /* RGB24 */, g->bw, g->bh, g->bw * 4);
        double k = g->sc();
        cairo_surface_set_device_scale(s, k, k);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_paint(cr);
        cairo_surface_destroy(s);
        return 1;
    }
    static void on_size(gp, gp, gp self) {
        Gtk *g = (Gtk *)self;
        int w, h;
        g->size(w, h);
        if (w != g->last_w || h != g->last_h) {
            g->last_w = w;
            g->last_h = h;
            Event e;
            e.type = Event::RESIZE;
            g->push(e);
        }
    }
    static gboolean on_delete(gp, gp, gp self) {
        Event e;
        e.type = Event::CLOSE;
        ((Gtk *)self)->push(e);
        return 1;   // the program decides (it saves first)
    }
    static gboolean on_button(gp, GdkEventButton_ *b, gp self) {
        Gtk *g = (Gtk *)self;
        if (b->type == 5 || b->type == 6) return 1;   // GTK's extra double/triple-click events: counted below
        Event e;
        e.type = b->type == 7 ? Event::UP : Event::DOWN;
        double k = g->sc();
        e.x = b->x * k;
        e.y = b->y * k;
        e.button = (int)b->button;
        e.mods = mods_of(b->state);
        if (e.type == Event::DOWN) {
            gtk_widget_grab_focus(g->area);
            // double-clicks: GTK's own timing and distance settings
            static uint32_t last_t = 0;
            static double lx = -1e9, ly = -1e9;
            static int count = 0;
            static unsigned last_b = 0;
            if (b->time - last_t < 400 && std::abs(b->x - lx) < 5 && std::abs(b->y - ly) < 5 && b->button == last_b) count++;
            else count = 1;
            last_t = b->time;
            lx = b->x;
            ly = b->y;
            last_b = b->button;
            e.clicks = count;
        }
        g->push(e);
        return 1;
    }
    static gboolean on_motion(gp, GdkEventMotion_ *m, gp self) {
        Gtk *g = (Gtk *)self;
        Event e;
        e.type = Event::MOVE;
        double k = g->sc();
        e.x = m->x * k;
        e.y = m->y * k;
        e.mods = mods_of(m->state);
        if (m->state & (1u << 8)) e.button = 1;
        g->push(e);
        return 1;
    }
    static gboolean on_leave(gp, gp, gp self) {
        Event e;
        e.type = Event::LEAVE;
        ((Gtk *)self)->push(e);
        return 0;
    }
    static gboolean on_scroll(gp, GdkEventScroll_ *s, gp self) {
        Gtk *g = (Gtk *)self;
        Event e;
        e.type = Event::WHEEL;
        double k = g->sc();
        e.x = s->x * k;
        e.y = s->y * k;
        e.mods = mods_of(s->state);
        // a wheel click comes twice (a click, and the same as a smooth step): only the smooth one
        if (gdk_event_get_pointer_emulated(s)) return 1;
        switch (s->direction) {
        case 0: e.dy = -1; break;
        case 1: e.dy = 1; break;
        case 2: e.dx = -1; break;
        case 3: e.dx = 1; break;
        default: {
            // smooth steps: a trackpad's are distances (Wayland: tenths of the fingers' pixels;
            // X11: steps of 15 pixels), a mouse wheel's are clicks (Wayland: 1.5 each)
            gp dev = gdk_event_get_source_device(s);
            int src = dev ? gdk_device_get_source(dev) : 0;
            bool pad = src == 6 || src == 5 || src == 7;   // touchpad, touchscreen, trackpoint
            const char *dn = gdk_display_get_name(gdk_display_get_default());
            bool x11 = dn && dn[0] == ':';
            if (pad) {
                double per = (x11 ? 15.0 : 10.0) * k;
                e.dx = s->delta_x * per;
                e.dy = s->delta_y * per;
                e.pixels = true;
                e.stop = s->is_stop;
            } else {
                // Wayland gives 1.5 a click (newer: 1.0); X11 1.0
                auto clicks = [&](double d) {
                    if (x11 || d == std::floor(d)) return d;
                    return d / 1.5;
                };
                e.dx = clicks(s->delta_x);
                e.dy = clicks(s->delta_y);
            }
        }
        }
        if (timing())
            std::fprintf(stderr, "scroll at %.0f: dir %d delta %.3f, %s%s -> %.2f%s\n", ms_now(), s->direction, s->delta_y,
                         gdk_event_get_source_device(s) ? std::to_string(gdk_device_get_source(gdk_event_get_source_device(s))).c_str() : "?",
                         s->is_stop ? " stop" : "", e.dy, e.pixels ? " px" : " lines");
        if (e.dx == 0 && e.dy == 0 && !e.stop) return 1;
        g->push(e);
        return 1;
    }
    static gboolean on_key(gp, GdkEventKey_ *k, gp self) {
        Gtk *g = (Gtk *)self;
        int mods = mods_of(k->state);
        int key = key_of(k->keyval);
        // typing goes through the input method (accents, other alphabets); shortcuts don't
        bool shortcut = mods & (CTRL | CMD | ALT);
        if (!shortcut && !(key >= K_LEFT) && g->im && gtk_im_context_filter_keypress(g->im, k)) return 1;
        if (!key) return 0;
        Event e;
        e.type = Event::KEY;
        e.key = key;
        e.mods = mods;
        if (k->keyval == 0xfe20) e.mods |= SHIFT;   // Shift+Tab
        g->push(e);
        if (!shortcut && key >= 0x20 && key < K_LEFT && !(g->im)) {   // no input method: type it ourselves
            Event t;
            t.type = Event::TEXT;
            char32_t c = (char32_t)key;
            if (mods & SHIFT && c < 0x80 && k->string && k->length > 0) t.text = std::string(k->string, (size_t)k->length);
            else {
                char u[5] = {};
                if (c < 0x80) u[0] = (char)c;
                t.text = u;
            }
            g->push(t);
        }
        return 1;
    }
    static gboolean on_key_up(gp, GdkEventKey_ *k, gp self) {
        Gtk *g = (Gtk *)self;
        if (g->im) gtk_im_context_filter_keypress(g->im, k);
        return 1;
    }
    static void on_commit(gp, const char *s, gp self) {
        Event e;
        e.type = Event::TEXT;
        e.text = s ? s : "";
        if (!e.text.empty()) ((Gtk *)self)->push(e);
    }
    static gboolean on_focus(gp, gp ev, gp self) {
        Gtk *g = (Gtk *)self;
        bool in = ((int16_t *)((char *)ev + 18))[0] != 0;   // GdkEventFocus.in
        if (g->im) (in ? gtk_im_context_focus_in : gtk_im_context_focus_out)(g->im);
        Event e;
        e.type = in ? Event::FOCUS_IN : Event::FOCUS_OUT;
        g->push(e);
        return 0;
    }
    static gboolean on_state(gp, GdkEventWindowState_ *s, gp self) {
        ((Gtk *)self)->is_max = (s->new_window_state & 4) != 0;   // GDK_WINDOW_STATE_MAXIMIZED
        return 0;
    }
    static void on_realize(gp, gp self) {
        Gtk *g = (Gtk *)self;
        if (g->im) gtk_im_context_set_client_window(g->im, gtk_widget_get_window(g->area));
    }
    static gboolean on_wake(gp self) {
        Event e;
        e.type = Event::WAKE;
        ((Gtk *)self)->push(e);
        return 0;
    }
    static gboolean on_timeout(gp flag) {
        *(bool *)flag = true;
        return 0;
    }

    void connect(gp obj, const char *sig, gp fn) { g_signal_connect_data(obj, sig, fn, this, nullptr, 0); }

    bool start(const std::string &t, int w, int h, const raster::Image *icon) {
        win = gtk_window_new(0);
        if (!win) return false;
        title_ = t;
        gtk_window_set_title(win, t.c_str());
        gtk_window_set_default_size(win, w, h);
        gtk_window_set_position(win, 1);   // centered
        area = gtk_drawing_area_new();
        gtk_container_add(win, area);
        gtk_widget_set_can_focus(area, 1);
        gtk_widget_add_events(area, (1 << 2) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13) | (1 << 14) | (1 << 21) | (1 << 23));
        im = gtk_im_multicontext_new();
        connect(area, "draw", (gp)on_draw);
        connect(area, "size-allocate", (gp)on_size);
        connect(area, "button-press-event", (gp)on_button);
        connect(area, "button-release-event", (gp)on_button);
        connect(area, "motion-notify-event", (gp)on_motion);
        connect(area, "leave-notify-event", (gp)on_leave);
        connect(area, "scroll-event", (gp)on_scroll);
        connect(area, "key-press-event", (gp)on_key);
        connect(area, "key-release-event", (gp)on_key_up);
        connect(area, "focus-in-event", (gp)on_focus);
        connect(area, "focus-out-event", (gp)on_focus);
        connect(area, "realize", (gp)on_realize);
        connect(win, "delete-event", (gp)on_delete);
        connect(win, "window-state-event", (gp)on_state);
        if (im) connect(im, "commit", (gp)on_commit);
        if (icon && icon->w > 0) set_icon(*icon);
        gtk_widget_show_all(win);
        gtk_widget_grab_focus(area);
        return true;
    }

    std::vector<uint8_t> icon_px;
    void set_icon(const raster::Image &img) {
        icon_px.assign(img.px.begin(), img.px.end());
        gp pb = gdk_pixbuf_new_from_data(icon_px.data(), 0 /* RGB */, 0, 8, img.w, img.h, img.w * 3, nullptr, nullptr);
        if (pb) {
            gtk_window_set_icon(win, pb);
            g_object_unref(pb);
        }
    }

    double scale() override { return sc(); }
    void size(int &w, int &h) override {
        double k = sc();
        w = (int)(gtk_widget_get_allocated_width(area) * k);
        h = (int)(gtk_widget_get_allocated_height(area) * k);
    }
    bool painted = true, wants = false;
    double presented_at = 0;
    bool ready() override { return painted || ms_now() - presented_at > 100; }   // (a hidden window never paints)
    void want_frame() override { wants = true; }
    void present(const raster::Image &img) override {
        painted = false;
        presented_at = ms_now();
        bw = img.w;
        bh = img.h;
        buf.resize((size_t)bw * (size_t)bh * 4);
        const uint8_t *s = img.px.data();
        uint32_t *d = (uint32_t *)buf.data();
        for (size_t i = 0, n = (size_t)bw * (size_t)bh; i < n; i++, s += 3) d[i] = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        gtk_widget_queue_draw(area);
    }
    bool next(Event &e, int timeout_ms) override {
        auto take = [&]() {
            std::lock_guard<std::mutex> lock(mu);
            if (q.empty()) return false;
            e = q.front();
            q.pop_front();
            return true;
        };
        if (take()) return true;
        bool fired = false;
        unsigned id = timeout_ms >= 0 ? g_timeout_add((unsigned)timeout_ms, (gp)on_timeout, &fired) : 0;
        for (;;) {
            gtk_main_iteration_do(1);
            if (take()) {
                if (id && !fired) g_source_remove(id);
                // let a burst of queued events (a drag) arrive before the next drawing
                return true;
            }
            if (fired) return false;
        }
    }
    void wake() override { g_idle_add((gp)on_wake, this); }
    void title(const std::string &t) override {
        if (t == title_) return;
        title_ = t;
        gtk_window_set_title(win, t.c_str());
    }
    void cursor(Cursor c) override {
        static const char *names[] = {"default", "text", "pointer", "wait", "ns-resize", "ew-resize"};
        static gp made[6] = {};
        static int shown = -1;
        if ((int)c == shown) return;
        shown = (int)c;
        if (!made[c]) made[c] = gdk_cursor_new_from_name(gdk_display_get_default(), names[c]);
        if (gp w = gtk_widget_get_window(area)) gdk_window_set_cursor(w, made[c]);
    }
    gp board() { return gtk_clipboard_get(gdk_atom_intern("CLIPBOARD", 0)); }
    std::string clipboard() override {
        char *t = gtk_clipboard_wait_for_text(board());
        std::string s = t ? t : "";
        if (t) g_free(t);
        return s;
    }
    void set_clipboard(const std::string &s) override { gtk_clipboard_set_text(board(), s.c_str(), (int)s.size()); }
    void minimize() override { gtk_window_iconify(win); }
    void maximize(bool on) override { (on ? gtk_window_maximize : gtk_window_unmaximize)(win); }
    bool maximized() override { return is_max; }
    void min_size(int w, int h) override { gtk_widget_set_size_request(area, w, h); }
    void dark_title(bool) override {}   // the desktop decides on Linux

    std::string pick(const std::string &t, int action, const std::vector<Filter> &filters, const std::string &name) {
        const char *ok = action == 1 ? "_Save" : action == 2 ? "_Choose" : "_Open";
        gp d = nullptr;
        bool native = gtk_file_chooser_native_new && gtk_native_dialog_run;
        if (native) d = gtk_file_chooser_native_new(t.c_str(), win, action, ok, "_Cancel");
        else d = gtk_file_chooser_dialog_new(t.c_str(), win, action, "_Cancel", -6, ok, -3, (char *)nullptr);
        if (!d) return "";
        if (action == 1) {
            gtk_file_chooser_set_do_overwrite_confirmation(d, 1);
            gtk_file_chooser_set_current_name(d, name.c_str());
        }
        for (const auto &f : filters) {
            gp ff = gtk_file_filter_new();
            gtk_file_filter_set_name(ff, f.name.c_str());
            for (const auto &p : f.patterns) gtk_file_filter_add_pattern(ff, p.c_str());
            gtk_file_chooser_add_filter(d, ff);
        }
        int r = native ? gtk_native_dialog_run(d) : gtk_dialog_run(d);
        std::string out;
        if (r == -3) {
            char *p = gtk_file_chooser_get_filename(d);
            if (p) {
                out = p;
                g_free(p);
            }
        }
        if (native) g_object_unref(d);
        else gtk_widget_destroy(d);
        // the key that closed the picker shouldn't reach the window
        while (gtk_events_pending()) gtk_main_iteration_do(0);
        return out;
    }
    std::string open_file(const std::string &t, const std::vector<Filter> &f) override { return pick(t, 0, f, ""); }
    std::string save_file(const std::string &t, const std::string &name) override { return pick(t, 1, {}, name); }
    std::string choose_folder(const std::string &t) override { return pick(t, 2, {}, ""); }
    void text_spot(double x, double y, double h) override {
        if (!im) return;
        double k = sc();
        Alloc a{(int)(x / k), (int)(y / k), 1, (int)(h / k)};
        gtk_im_context_set_cursor_location(im, &a);
    }
    ~Gtk() override {
        if (win) gtk_widget_destroy(win);
        while (gtk_events_pending()) gtk_main_iteration_do(0);
    }
};

std::string read_kde(const std::string &key) {
    const char *home = std::getenv("HOME");
    const char *cfg = std::getenv("XDG_CONFIG_HOME");
    std::string path = cfg && *cfg ? std::string(cfg) + "/kdeglobals" : std::string(home ? home : "") + "/.config/kdeglobals";
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
        if (line.compare(0, key.size() + 1, key + "=") == 0) return line.substr(key.size() + 1);
    return "";
}

std::string gnome(const char *key) {
    if (!load()) return "";
    gp src = g_settings_schema_source_get_default();
    if (!src) return "";
    gp schema = g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", 1);
    if (!schema) return "";
    bool has = g_settings_schema_has_key(schema, key);
    g_settings_schema_unref(schema);
    if (!has) return "";
    static gp s = nullptr;
    if (!s) s = g_settings_new("org.gnome.desktop.interface");
    char *v = g_settings_get_string(s, key);
    std::string out = v ? v : "";
    if (v) g_free(v);
    return out;
}

std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

std::unique_ptr<Window> Window::open(const std::string &title, int w, int h, const raster::Image *icon) {
    if (!load()) return nullptr;
    std::unique_ptr<Gtk> g(new Gtk());
    if (!g->start(title, w, h, icon)) return nullptr;
    return std::unique_ptr<Window>(g.release());
}

bool system_dark() {
    const char *t = std::getenv("GTK_THEME");
    if (t && lower(t).find(":dark") != std::string::npos) return true;
    std::string scheme = gnome("color-scheme");
    if (scheme == "prefer-dark" || scheme == "prefer-light") return scheme == "prefer-dark";
    if (lower(read_kde("ColorScheme")).find("dark") != std::string::npos) return true;
    return lower(gnome("gtk-theme")).find("dark") != std::string::npos;
}

std::string system_accent() {
    static const std::pair<const char *, const char *> GNOME[] = {{"blue", "#3584E4"},   {"teal", "#2190A4"}, {"green", "#3A944A"},
                                                                  {"yellow", "#C88800"}, {"orange", "#ED5B00"}, {"red", "#E62D42"},
                                                                  {"pink", "#D56199"},   {"purple", "#9141AC"}, {"slate", "#6F8396"}};
    std::string name = gnome("accent-color");
    for (const auto &p : GNOME)
        if (name == p.first) return p.second;
    std::string kde = read_kde("AccentColor");
    int r, g, b;
    if (std::sscanf(kde.c_str(), "%d,%d,%d", &r, &g, &b) == 3 && r >= 0 && r < 256 && g >= 0 && g < 256 && b >= 0 && b < 256) {
        char hex[8];
        std::snprintf(hex, sizeof hex, "#%02X%02X%02X", r, g, b);
        return hex;
    }
    return "";
}

std::pair<std::string, double> system_font() {
    std::string f = gnome("font-name");   // "Cantarell 11"
    if (f.empty()) {
        std::string kde = read_kde("font");   // "Noto Sans,10,-1,5,..."
        if (!kde.empty()) {
            size_t c = kde.find(',');
            double size = c == std::string::npos ? 0 : std::atof(kde.c_str() + c + 1);
            return {kde.substr(0, c), size};
        }
        return {"", 0};
    }
    size_t sp = f.find_last_of(' ');
    if (sp == std::string::npos) return {f, 0};
    return {f.substr(0, sp), std::atof(f.c_str() + sp + 1)};
}

}  // namespace plat

#endif
