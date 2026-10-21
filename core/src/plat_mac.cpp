// macOS: a Cocoa window that shows the pixels Chalkboard draws, sharp on Retina screens, with the
// Mac's own menu bar, file pickers, clipboard, and light/dark look. AppKit is reached through the
// Objective-C runtime from plain C++ and loaded when the program starts, so this builds anywhere
// (no macOS SDK needed) and the same program still runs as the terminal app over SSH.
#ifdef __APPLE__
#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "plat.h"

namespace plat {

namespace {

typedef void *id;
typedef void *SEL;
typedef signed char BOOL;
struct Point { double x, y; };
struct Size { double w, h; };
struct Rect { double x, y, w, h; };   // NSRect / CGRect

// ---- the runtime, AppKit, CoreGraphics, CoreFoundation: looked up once

void *(*p_getClass)(const char *);
SEL (*p_sel)(const char *);
void *p_msgSend;
#if defined(__x86_64__)
void *p_msgSend_stret, *p_msgSend_fpret;   // (x86_64 only: big struct and floating-point returns)
#endif
void *(*p_allocateClassPair)(void *, const char *, size_t);
void (*p_registerClassPair)(void *);
BOOL (*p_addMethod)(void *, SEL, void *, const char *);
void *(*p_poolPush)();
void (*p_poolPop)(void *);
void (*p_rectFill)(Rect);
void *(*p_colorSpaceRGB)();
void (*p_colorSpaceRelease)(void *);
void *(*p_cfDataCreate)(void *, const uint8_t *, long);
void (*p_cfRelease)(const void *);
void *(*p_providerFromData)(void *);
void (*p_providerRelease)(void *);
void *(*p_imageCreate)(size_t, size_t, size_t, size_t, size_t, void *, uint32_t, void *, const double *, bool, int);
void (*p_imageRelease)(void *);
id *k_pasteboardString, *k_runLoopMode, *k_darkAqua, *k_aqua;

id cls(const char *name) { return p_getClass(name); }
SEL sel(const char *name) { return p_sel(name); }

// objc_msgSend called through a pointer of the exact function type (arm64 cares)
template <typename R = id, typename... A>
R msg(id obj, const char *s, A... args) {
    return ((R(*)(id, SEL, A...))p_msgSend)(obj, sel(s), args...);
}
// a method returning a CGFloat: x86_64 has its own entry for floating-point returns
template <typename... A>
double msg_f(id obj, const char *s, A... args) {
#if defined(__x86_64__)
    return ((double (*)(id, SEL, A...))p_msgSend_fpret)(obj, sel(s), args...);
#else
    return ((double (*)(id, SEL, A...))p_msgSend)(obj, sel(s), args...);
#endif
}
// a method returning an NSRect (32 bytes): x86_64 returns it through memory (objc_msgSend_stret)
Rect msg_rect(id obj, const char *s) {
#if defined(__x86_64__)
    Rect r;
    ((void (*)(Rect *, id, SEL))p_msgSend_stret)(&r, obj, sel(s));
    return r;
#else
    return ((Rect(*)(id, SEL))p_msgSend)(obj, sel(s));
#endif
}

id ns(const std::string &s) { return msg(cls("NSString"), "stringWithUTF8String:", s.c_str()); }
std::string str(id s) {
    if (!s) return "";
    const char *c = msg<const char *>(s, "UTF8String");
    return c ? c : "";
}

bool load() {
    static int state = -1;
    if (state >= 0) return state == 1;
    state = 0;
    void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_GLOBAL);
    void *appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW | RTLD_GLOBAL);
    void *cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW | RTLD_GLOBAL);
    void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_NOW | RTLD_GLOBAL);
    void *fd = dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW | RTLD_GLOBAL);
    if (!objc || !appkit || !cg || !cf || !fd) return false;
    auto get = [](void *lib, const char *name, auto &fn) { *(void **)&fn = dlsym(lib, name); return fn != nullptr; };
    bool ok = get(objc, "objc_getClass", p_getClass) && get(objc, "sel_registerName", p_sel) && get(objc, "objc_allocateClassPair", p_allocateClassPair) &&
              get(objc, "objc_registerClassPair", p_registerClassPair) && get(objc, "class_addMethod", p_addMethod) &&
              get(objc, "objc_autoreleasePoolPush", p_poolPush) && get(objc, "objc_autoreleasePoolPop", p_poolPop) &&
              get(appkit, "NSRectFill", p_rectFill) && get(cg, "CGColorSpaceCreateDeviceRGB", p_colorSpaceRGB) &&
              get(cg, "CGColorSpaceRelease", p_colorSpaceRelease) && get(cf, "CFDataCreate", p_cfDataCreate) && get(cf, "CFRelease", p_cfRelease) &&
              get(cg, "CGDataProviderCreateWithCFData", p_providerFromData) && get(cg, "CGDataProviderRelease", p_providerRelease) &&
              get(cg, "CGImageCreate", p_imageCreate) && get(cg, "CGImageRelease", p_imageRelease);
    p_msgSend = dlsym(objc, "objc_msgSend");
#if defined(__x86_64__)
    p_msgSend_stret = dlsym(objc, "objc_msgSend_stret");
    p_msgSend_fpret = dlsym(objc, "objc_msgSend_fpret");
    ok = ok && p_msgSend_stret && p_msgSend_fpret;
#endif
    k_pasteboardString = (id *)dlsym(appkit, "NSPasteboardTypeString");
    k_runLoopMode = (id *)dlsym(fd, "NSDefaultRunLoopMode");
    k_darkAqua = (id *)dlsym(appkit, "NSAppearanceNameDarkAqua");
    k_aqua = (id *)dlsym(appkit, "NSAppearanceNameAqua");
    ok = ok && p_msgSend && k_pasteboardString && k_runLoopMode && k_aqua && cls("NSApplication");
    state = ok ? 1 : 0;
    return ok;
}

struct Pool {   // an autorelease pool for as long as this lives
    void *p;
    Pool() : p(p_poolPush()) {}
    ~Pool() { p_poolPop(p); }
};

// NSEvent modifier flags
const unsigned long F_SHIFT = 1ul << 17, F_CTRL = 1ul << 18, F_ALT = 1ul << 19, F_CMD = 1ul << 20;
const long WAKE_SUBTYPE = 0x4342;   // "CB": our own "something's ready" event

int mods_of(id ev) {
    unsigned long f = msg<unsigned long>(ev, "modifierFlags");
    return (f & F_SHIFT ? SHIFT : 0) | (f & F_CTRL ? CTRL : 0) | (f & F_ALT ? ALT : 0) | (f & F_CMD ? CMD : 0);
}

// a key from its event: the special keys by their place on the keyboard, the rest by the character
int key_of(id ev) {
    switch (msg<unsigned short>(ev, "keyCode")) {
    case 123: return K_LEFT;
    case 124: return K_RIGHT;
    case 125: return K_DOWN;
    case 126: return K_UP;
    case 115: return K_HOME;
    case 119: return K_END;
    case 116: return K_PGUP;
    case 121: return K_PGDN;
    case 36: case 76: return K_RETURN;
    case 53: return K_ESCAPE;
    case 48: return K_TAB;
    case 51: return K_BACKSPACE;
    case 117: return K_DELETE;
    case 114: return K_INSERT;
    case 110: return K_MENU;
    case 49: return ' ';
    case 122: return K_F1;
    case 120: return K_F2;
    case 99: return K_F3;
    case 118: return K_F4;
    case 96: return K_F5;
    case 97: return K_F6;
    case 98: return K_F7;
    case 100: return K_F8;
    case 101: return K_F9;
    case 109: return K_F10;
    case 103: return K_F11;
    case 111: return K_F12;
    default: break;
    }
    std::string c = str(msg(ev, "charactersIgnoringModifiers"));
    if (c.size() == 1 && c[0] >= 0x20 && c[0] < 0x7f) return c[0] >= 'A' && c[0] <= 'Z' ? c[0] + 32 : c[0];
    return 0;
}

class Mac;
Mac *g_win = nullptr;   // Chalkboard has one window

class Mac : public Window {
public:
    id win = nullptr, view = nullptr;
    void *image = nullptr;   // CGImage of the last picture shown
    int iw = 0, ih = 0;
    uint8_t edge[3] = {255, 255, 255};
    std::deque<Event> q;
    std::mutex mu;
    Cursor cur = ARROW;
    std::string title_, menu_sig_;

    void push(const Event &e) {
        std::lock_guard<std::mutex> lock(mu);
        q.push_back(e);
    }
    bool take(Event &e) {
        std::lock_guard<std::mutex> lock(mu);
        if (q.empty()) return false;
        e = q.front();
        q.pop_front();
        return true;
    }
    void push_simple(Event::Type t) {
        Event e;
        e.type = t;
        push(e);
    }

    // where an event happened, in device pixels from the top left (the view is flipped)
    void where(id ev, Event &e) {
        Point p = msg<Point>(ev, "locationInWindow");
        Point v = msg<Point>(view, "convertPoint:fromView:", p, (id) nullptr);
        double s = scale();
        e.x = v.x * s;
        e.y = v.y * s;
    }
    void mouse(id ev, Event::Type t, int button) {
        Event e;
        e.type = t;
        where(ev, e);
        e.button = button;
        e.mods = mods_of(ev);
        if (t == Event::DOWN) e.clicks = (int)std::max(1L, msg<long>(ev, "clickCount"));
        push(e);
    }
    void key(id ev, bool with_text) {
        Event e;
        e.type = Event::KEY;
        e.key = key_of(ev);
        e.mods = mods_of(ev);
        if (e.key) push(e);
        if (!with_text || (e.mods & (CMD | CTRL))) return;
        std::string t = str(msg(ev, "characters"));
        if (t.empty()) return;
        unsigned char c0 = (unsigned char)t[0];
        if (c0 < 0x20 || c0 == 0x7f) return;
        // the arrow and function keys come as characters in Apple's private range (U+F700-F8FF)
        if (c0 == 0xEF && t.size() >= 3 && (unsigned char)t[1] >= 0x9C && (unsigned char)t[1] <= 0xA3) return;
        Event te;
        te.type = Event::TEXT;
        te.text = t;
        push(te);
    }

    bool start(const std::string &t, int w, int h, const raster::Image *icon);
    std::vector<uint32_t> buf;   // the picture as shown (see present)

    double scale() override { return win ? msg_f(win, "backingScaleFactor") : 1.0; }
    void size(int &w, int &h) override {
        Rect b = msg_rect(view, "bounds");
        double s = scale();
        w = (int)std::lround(b.w * s);
        h = (int)std::lround(b.h * s);
    }
    void present(const raster::Image &img) override {
        Pool pool;
        if (image) p_imageRelease(image);
        image = nullptr;
        iw = img.w;
        ih = img.h;
        if (iw <= 0 || ih <= 0) return;
        const uint8_t *last = img.px.data() + img.px.size() - 3;
        std::memcpy(edge, last, 3);
        // 32 bits a pixel, the order Macs draw without converting (24-bit RGB gets converted on every
        // draw, which is slow on a Retina screen): B, G, R, unused in memory
        buf.resize((size_t)iw * (size_t)ih);
        const uint8_t *s = img.px.data();
        for (size_t i = 0, n = buf.size(); i < n; i++, s += 3) buf[i] = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        void *data = p_cfDataCreate(nullptr, (const uint8_t *)buf.data(), (long)(buf.size() * 4));
        void *prov = p_providerFromData(data);
        void *space = p_colorSpaceRGB();
        const uint32_t skip_first = 6, little32 = 2 << 12;   // kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little
        image = p_imageCreate((size_t)iw, (size_t)ih, 8, 32, (size_t)iw * 4, space, skip_first | little32, prov, nullptr, false, 0);
        p_colorSpaceRelease(space);
        p_providerRelease(prov);
        p_cfRelease(data);
        msg<void>(view, "setNeedsDisplay:", (BOOL)1);
        msg<void>(view, "displayIfNeeded");
    }
    void draw() {
        Rect b = msg_rect(view, "bounds");
        id color = msg(cls("NSColor"), "colorWithSRGBRed:green:blue:alpha:", edge[0] / 255.0, edge[1] / 255.0, edge[2] / 255.0, 1.0);
        msg<void>(color, "set");
        p_rectFill(b);
        if (!image) return;
        double s = scale();
        id ctx = msg(cls("NSGraphicsContext"), "currentContext");
        msg<void>(ctx, "setImageInterpolation:", (unsigned long)1);   // none: pixel for pixel
        id ns_img = msg(msg(cls("NSImage"), "alloc"), "initWithCGImage:size:", image, Size{iw / s, ih / s});
        msg<void>(ns_img, "drawInRect:fromRect:operation:fraction:respectFlipped:hints:", Rect{0, 0, iw / s, ih / s}, Rect{0, 0, 0, 0},
                  (unsigned long)1 /* copy */, 1.0, (BOOL)1, (id) nullptr);
        msg<void>(ns_img, "release");
    }
    bool next(Event &e, int timeout_ms) override {
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        id app = msg(cls("NSApplication"), "sharedApplication");
        for (;;) {
            if (take(e)) return true;
            Pool pool;
            id date;
            if (timeout_ms < 0) date = msg(cls("NSDate"), "distantFuture");
            else if (timeout_ms == 0) date = msg(cls("NSDate"), "distantPast");   // only what's already waiting
            else {
                double left = std::chrono::duration<double>(until - std::chrono::steady_clock::now()).count();
                if (left <= 0) return false;
                date = msg(cls("NSDate"), "dateWithTimeIntervalSinceNow:", left);
            }
            id ev = msg(app, "nextEventMatchingMask:untilDate:inMode:dequeue:", ~0ul, date, *k_runLoopMode, (BOOL)1);
            if (!ev) {
                if (timeout_ms == 0) return take(e);   // nothing more waiting (sendEvent: may have queued one)
                continue;
            }
            if (msg<unsigned long>(ev, "type") == 15 && msg<short>(ev, "subtype") == WAKE_SUBTYPE) {
                push_simple(Event::WAKE);
                continue;
            }
            msg<void>(app, "sendEvent:", ev);
            msg<void>(app, "updateWindows");
        }
    }
    void wake() override {
        if (!load()) return;
        Pool pool;
        id ev = msg(cls("NSEvent"), "otherEventWithType:location:modifierFlags:timestamp:windowNumber:context:subtype:data1:data2:",
                    (unsigned long)15, Point{0, 0}, (unsigned long)0, 0.0, (long)0, (id) nullptr, (short)WAKE_SUBTYPE, (long)0, (long)0);
        msg<void>(msg(cls("NSApplication"), "sharedApplication"), "postEvent:atStart:", ev, (BOOL)0);
    }
    void title(const std::string &t) override {
        if (t == title_) return;
        title_ = t;
        Pool pool;
        msg<void>(win, "setTitle:", ns(t));
    }
    void cursor(Cursor c) override {
        if (c == cur) return;
        cur = c;
        static const char *names[] = {"arrowCursor", "IBeamCursor", "pointingHandCursor", "arrowCursor", "resizeUpDownCursor", "resizeLeftRightCursor"};
        msg<void>(msg(cls("NSCursor"), names[c]), "set");
    }
    std::string clipboard() override {
        Pool pool;
        return str(msg(msg(cls("NSPasteboard"), "generalPasteboard"), "stringForType:", *k_pasteboardString));
    }
    void set_clipboard(const std::string &s) override {
        Pool pool;
        id pb = msg(cls("NSPasteboard"), "generalPasteboard");
        msg<long>(pb, "clearContents");
        msg<BOOL>(pb, "setString:forType:", ns(s), *k_pasteboardString);
    }
    void minimize() override { msg<void>(win, "miniaturize:", (id) nullptr); }
    void maximize(bool on) override {
        if (on != maximized()) msg<void>(win, "zoom:", (id) nullptr);
    }
    bool maximized() override { return msg<BOOL>(win, "isZoomed") != 0; }
    void min_size(int w, int h) override { msg<void>(win, "setContentMinSize:", Size{(double)w, (double)h}); }
    void dark_title(bool dark) override {
        id *name = dark && k_darkAqua ? k_darkAqua : k_aqua;
        msg<void>(win, "setAppearance:", msg(cls("NSAppearance"), "appearanceNamed:", *name));
    }

    std::string run_panel(id panel) {
        long r = msg<long>(panel, "runModal");
        msg<void>(win, "makeKeyAndOrderFront:", (id) nullptr);
        if (r != 1) return "";
        return str(msg(msg(panel, "URL"), "path"));
    }
    std::string open_file(const std::string &t, const std::vector<Filter> &filters) override {
        Pool pool;
        id panel = msg(cls("NSOpenPanel"), "openPanel");
        msg<void>(panel, "setTitle:", ns(t));
        msg<void>(panel, "setMessage:", ns(t));
        msg<void>(panel, "setCanChooseFiles:", (BOOL)1);
        msg<void>(panel, "setCanChooseDirectories:", (BOOL)0);
        id types = msg(cls("NSMutableArray"), "array");
        bool any = false;
        for (const auto &f : filters)
            for (const auto &p : f.patterns) {
                if (p == "*" || p == "*.*") any = true;
                else if (p.rfind("*.", 0) == 0) msg<void>(types, "addObject:", ns(p.substr(2)));
            }
        if (!any && msg<unsigned long>(types, "count")) msg<void>(panel, "setAllowedFileTypes:", types);
        return run_panel(panel);
    }
    std::string save_file(const std::string &t, const std::string &name) override {
        Pool pool;
        id panel = msg(cls("NSSavePanel"), "savePanel");
        msg<void>(panel, "setTitle:", ns(t));
        msg<void>(panel, "setMessage:", ns(t));
        msg<void>(panel, "setNameFieldStringValue:", ns(name));
        msg<void>(panel, "setCanCreateDirectories:", (BOOL)1);
        return run_panel(panel);
    }
    std::string choose_folder(const std::string &t) override {
        Pool pool;
        id panel = msg(cls("NSOpenPanel"), "openPanel");
        msg<void>(panel, "setTitle:", ns(t));
        msg<void>(panel, "setMessage:", ns(t));
        msg<void>(panel, "setCanChooseFiles:", (BOOL)0);
        msg<void>(panel, "setCanChooseDirectories:", (BOOL)1);
        msg<void>(panel, "setCanCreateDirectories:", (BOOL)1);
        return run_panel(panel);
    }
    bool menu_bar(const std::vector<MenuItem> &menus) override;
    ~Mac() override {
        if (image) p_imageRelease(image);
        if (win) {
            msg<void>(win, "orderOut:", (id) nullptr);
            msg<void>(win, "close");
        }
        g_win = nullptr;
    }
};

// ---- the methods of our view class (it's also the window's delegate and the menus' target)

BOOL yes(id, SEL) { return 1; }
BOOL yes1(id, SEL, id) { return 1; }
void v_draw(id, SEL, Rect) {
    if (g_win) g_win->draw();
}
void v_down(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::DOWN, 1);
}
void v_up(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::UP, 1);
}
void v_rdown(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::DOWN, 3);
}
void v_rup(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::UP, 3);
}
void v_odown(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::DOWN, 2);
}
void v_oup(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::UP, 2);
}
void v_move(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::MOVE, 0);
}
void v_drag(id, SEL, id ev) {
    if (g_win) g_win->mouse(ev, Event::MOVE, 1);
}
void v_exit(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::LEAVE);
}
void v_wheel(id, SEL, id ev) {
    if (!g_win) return;
    Event e;
    e.type = Event::WHEEL;
    g_win->where(ev, e);
    e.mods = mods_of(ev);
    double dx = msg_f(ev, "scrollingDeltaX"), dy = msg_f(ev, "scrollingDeltaY");
    // a trackpad gives points (sent on as device pixels, so the page moves with the fingers), a
    // mouse wheel lines (down/right positive)
    bool precise = msg<BOOL>(ev, "hasPreciseScrollingDeltas");
    double per = precise ? g_win->scale() : 1.0;
    e.pixels = precise;
    e.dx = -dx * per;
    e.dy = -dy * per;
    if (e.dx != 0 || e.dy != 0) g_win->push(e);
}
void v_key(id, SEL, id ev) {
    if (g_win) g_win->key(ev, true);
}
void v_resized(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::RESIZE);
}
void v_backing(id, SEL) {
    if (g_win) g_win->push_simple(Event::RESIZE);
}
BOOL v_should_close(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::CLOSE);
    return 0;   // Chalkboard decides (it saves first)
}
void v_focus_in(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::FOCUS_IN);
}
void v_focus_out(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::FOCUS_OUT);
}
void v_look(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::LOOK);
}
// a menu item: picked with the mouse, a MENU event; picked by its shortcut, the key itself (so the
// shortcut works the same as on Windows and Linux, wherever the focus is)
void v_menu(id, SEL, id item) {
    if (!g_win) return;
    id cur = msg(msg(cls("NSApplication"), "sharedApplication"), "currentEvent");
    if (cur && msg<unsigned long>(cur, "type") == 10 /* key down */) {
        g_win->key(cur, false);
        return;
    }
    Event e;
    e.type = Event::MENU;
    e.menu = (int)msg<long>(item, "tag");
    g_win->push(e);
}
void v_quit(id, SEL, id) {
    if (g_win) g_win->push_simple(Event::CLOSE);
}

id make_view_class() {
    static id c = nullptr;
    if (c) return c;
    c = p_allocateClassPair(cls("NSView"), "ChalkboardView", 0);
    auto add = [&](const char *name, void *fn, const char *types) { p_addMethod(c, sel(name), fn, types); };
    add("isFlipped", (void *)yes, "c@:");
    add("acceptsFirstResponder", (void *)yes, "c@:");
    add("acceptsFirstMouse:", (void *)yes1, "c@:@");
    add("drawRect:", (void *)v_draw, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    add("mouseDown:", (void *)v_down, "v@:@");
    add("mouseUp:", (void *)v_up, "v@:@");
    add("rightMouseDown:", (void *)v_rdown, "v@:@");
    add("rightMouseUp:", (void *)v_rup, "v@:@");
    add("otherMouseDown:", (void *)v_odown, "v@:@");
    add("otherMouseUp:", (void *)v_oup, "v@:@");
    add("mouseMoved:", (void *)v_move, "v@:@");
    add("mouseDragged:", (void *)v_drag, "v@:@");
    add("rightMouseDragged:", (void *)v_move, "v@:@");
    add("otherMouseDragged:", (void *)v_move, "v@:@");
    add("mouseExited:", (void *)v_exit, "v@:@");
    add("scrollWheel:", (void *)v_wheel, "v@:@");
    add("keyDown:", (void *)v_key, "v@:@");
    add("viewDidChangeBackingProperties", (void *)v_backing, "v@:");
    add("windowDidResize:", (void *)v_resized, "v@:@");
    add("windowShouldClose:", (void *)v_should_close, "c@:@");
    add("windowDidBecomeKey:", (void *)v_focus_in, "v@:@");
    add("windowDidResignKey:", (void *)v_focus_out, "v@:@");
    add("lookChanged:", (void *)v_look, "v@:@");
    add("chalkboardMenu:", (void *)v_menu, "v@:@");
    add("chalkboardQuit:", (void *)v_quit, "v@:@");
    p_registerClassPair(c);
    return c;
}

bool Mac::start(const std::string &t, int w, int h, const raster::Image *icon) {
    Pool pool;
    id app = msg(cls("NSApplication"), "sharedApplication");
    msg<BOOL>(app, "setActivationPolicy:", (long)0);   // a regular app: Dock icon and menu bar
    id vc = make_view_class();
    Rect r{0, 0, (double)w, (double)h};
    // fit the screen (less the menu bar and Dock)
    id screen = msg(cls("NSScreen"), "mainScreen");
    if (screen) {
        Rect vis = msg_rect(screen, "visibleFrame");
        r.w = std::min(r.w, vis.w - 40);
        r.h = std::min(r.h, vis.h - 40);
    }
    win = msg(msg(cls("NSWindow"), "alloc"), "initWithContentRect:styleMask:backing:defer:", r, (unsigned long)(1 | 2 | 4 | 8), (unsigned long)2, (BOOL)0);
    if (!win) return false;
    msg<void>(win, "setReleasedWhenClosed:", (BOOL)0);
    view = msg(msg(vc, "alloc"), "initWithFrame:", r);
    msg<void>(win, "setContentView:", view);
    msg<void>(win, "setDelegate:", view);
    msg<void>(win, "setAcceptsMouseMovedEvents:", (BOOL)1);
    msg<BOOL>(win, "makeFirstResponder:", view);
    // mouse moves and leaving, wherever the view is (it follows the view's size by itself)
    id area = msg(msg(cls("NSTrackingArea"), "alloc"), "initWithRect:options:owner:userInfo:", Rect{0, 0, 0, 0},
                  (unsigned long)(0x01 | 0x02 | 0x80 | 0x200), view, (id) nullptr);
    msg<void>(view, "addTrackingArea:", area);
    msg<void>(area, "release");
    title(t);
    msg<void>(win, "center");
    // the computer switching light/dark or its accent color
    id dist = msg(cls("NSDistributedNotificationCenter"), "defaultCenter");
    msg<void>(dist, "addObserver:selector:name:object:", view, sel("lookChanged:"), ns("AppleInterfaceThemeChangedNotification"), (id) nullptr);
    msg<void>(dist, "addObserver:selector:name:object:", view, sel("lookChanged:"), ns("AppleColorPreferencesChangedNotification"), (id) nullptr);
    if (icon && icon->w > 0) {
        void *data = p_cfDataCreate(nullptr, icon->px.data(), (long)icon->px.size());
        void *prov = p_providerFromData(data);
        void *space = p_colorSpaceRGB();
        void *cgi = p_imageCreate((size_t)icon->w, (size_t)icon->h, 8, 24, (size_t)icon->w * 3, space, 0, prov, nullptr, false, 0);
        if (cgi) {
            id img = msg(msg(cls("NSImage"), "alloc"), "initWithCGImage:size:", cgi, Size{(double)icon->w, (double)icon->h});
            msg<void>(app, "setApplicationIconImage:", img);
            msg<void>(img, "release");
            p_imageRelease(cgi);
        }
        p_colorSpaceRelease(space);
        p_providerRelease(prov);
        p_cfRelease(data);
    }
    static bool launched = false;
    if (!launched) {
        launched = true;
        msg<void>(app, "finishLaunching");
    }
    msg<void>(win, "makeKeyAndOrderFront:", (id) nullptr);
    msg<void>(app, "activateIgnoringOtherApps:", (BOOL)1);
    return true;
}

// ---- the menu bar

std::string key_equivalent(int key) {
    int c = 0;
    switch (key) {
    case K_LEFT: c = 0xF702; break;
    case K_RIGHT: c = 0xF703; break;
    case K_UP: c = 0xF700; break;
    case K_DOWN: c = 0xF701; break;
    case K_HOME: c = 0xF729; break;
    case K_END: c = 0xF72B; break;
    case K_PGUP: c = 0xF72C; break;
    case K_PGDN: c = 0xF72D; break;
    case K_DELETE: c = 0xF728; break;
    case K_BACKSPACE: c = 0x08; break;
    case K_RETURN: c = '\r'; break;
    case K_ESCAPE: c = 0x1B; break;
    case K_TAB: c = '\t'; break;
    default:
        if (key >= K_F1 && key <= K_F12) c = 0xF704 + (key - K_F1);
        else if (key > 0 && key < 0x110000) c = key;
    }
    std::string s;
    if (c <= 0) return s;
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) s += (char)(0xC0 | (c >> 6)), s += (char)(0x80 | (c & 0x3F));
    else s += (char)(0xE0 | (c >> 12)), s += (char)(0x80 | ((c >> 6) & 0x3F)), s += (char)(0x80 | (c & 0x3F));
    return s;
}

id menu_item(const MenuItem &m, id target) {
    if (m.label.empty()) return msg(cls("NSMenuItem"), "separatorItem");
    id item = msg(msg(cls("NSMenuItem"), "alloc"), "initWithTitle:action:keyEquivalent:", ns(m.label), m.sub.empty() ? sel("chalkboardMenu:") : (SEL) nullptr,
                  ns(m.sub.empty() ? key_equivalent(m.key) : ""));
    if (m.sub.empty()) {
        msg<void>(item, "setTarget:", target);
        msg<void>(item, "setTag:", (long)m.id);
        unsigned long mask = (m.mods & CMD ? F_CMD : 0) | (m.mods & SHIFT ? F_SHIFT : 0) | (m.mods & ALT ? F_ALT : 0) | (m.mods & CTRL ? F_CTRL : 0);
        if (m.key) msg<void>(item, "setKeyEquivalentModifierMask:", mask);
        msg<void>(item, "setState:", (long)(m.checked ? 1 : 0));
    }
    msg<void>(item, "setEnabled:", (BOOL)(m.enabled ? 1 : 0));
    return item;
}

id build_menu(const std::string &title, const std::vector<MenuItem> &items, id target) {
    id menu = msg(msg(cls("NSMenu"), "alloc"), "initWithTitle:", ns(title));
    msg<void>(menu, "setAutoenablesItems:", (BOOL)0);
    bool last_sep = true;   // no separator first, two together, or last
    for (size_t i = 0; i < items.size(); i++) {
        const MenuItem &m = items[i];
        if (m.label.empty()) {
            bool more = false;
            for (size_t k = i + 1; k < items.size(); k++) more |= !items[k].label.empty();
            if (last_sep || !more) continue;
        }
        id item = menu_item(m, target);
        if (!m.sub.empty()) {
            id sub = build_menu(m.label, m.sub, target);
            msg<void>(item, "setSubmenu:", sub);
            msg<void>(sub, "release");
        }
        msg<void>(menu, "addItem:", item);
        if (!m.label.empty()) msg<void>(item, "release");
        last_sep = m.label.empty();
    }
    return menu;
}

std::string signature(const std::vector<MenuItem> &ms) {
    std::string s;
    for (const auto &m : ms)
        s += m.label + "|" + std::to_string(m.id) + "|" + std::to_string(m.key) + "|" + std::to_string(m.mods) + "|" + (m.checked ? "1" : "0") +
             (m.enabled ? "1" : "0") + "{" + signature(m.sub) + "}";
    return s;
}

bool Mac::menu_bar(const std::vector<MenuItem> &menus_in) {
    std::string sig = signature(menus_in);
    if (sig == menu_sig_) return true;
    menu_sig_ = sig;
    Pool pool;
    // a Mac keeps About and Quit in the app's own menu (the first one, named after the app)
    std::vector<MenuItem> menus = menus_in;
    MenuItem about, quit;
    for (auto &top : menus)
        for (auto it = top.sub.begin(); it != top.sub.end();) {
            if (it->label.rfind("About ", 0) == 0) about = *it, it = top.sub.erase(it);
            else if (it->label == "Quit") quit = *it, it = top.sub.erase(it);
            else ++it;
        }
    std::vector<MenuItem> app_items;
    if (!about.label.empty()) app_items.push_back(about);
    MenuItem sep;
    app_items.push_back(sep);
    id main = msg(msg(cls("NSMenu"), "alloc"), "initWithTitle:", ns(""));
    id app_menu = build_menu("Chalkboard", app_items, view);
    // Hide, Hide Others, Show All: the Mac's own
    id app = msg(cls("NSApplication"), "sharedApplication");
    auto standard = [&](const char *label, const char *action, const char *key, unsigned long mask) {
        id it = msg(msg(cls("NSMenuItem"), "alloc"), "initWithTitle:action:keyEquivalent:", ns(label), sel(action), ns(key));
        msg<void>(it, "setTarget:", app);
        if (mask) msg<void>(it, "setKeyEquivalentModifierMask:", mask);
        msg<void>(app_menu, "addItem:", it);
        msg<void>(it, "release");
    };
    standard("Hide Chalkboard", "hide:", "h", F_CMD);
    standard("Hide Others", "hideOtherApplications:", "h", F_CMD | F_ALT);
    standard("Show All", "unhideAllApplications:", "", 0);
    msg<void>(app_menu, "addItem:", msg(cls("NSMenuItem"), "separatorItem"));
    if (quit.label.empty()) {
        quit.label = "Quit";
        quit.key = 'q';
        quit.mods = CMD;
    }
    quit.label = "Quit Chalkboard";
    id qi = msg(msg(cls("NSMenuItem"), "alloc"), "initWithTitle:action:keyEquivalent:", ns(quit.label), quit.id ? sel("chalkboardMenu:") : sel("chalkboardQuit:"), ns("q"));
    msg<void>(qi, "setTarget:", view);
    msg<void>(qi, "setTag:", (long)quit.id);
    msg<void>(app_menu, "addItem:", qi);
    msg<void>(qi, "release");
    id app_item = msg(msg(cls("NSMenuItem"), "alloc"), "init");
    msg<void>(app_item, "setSubmenu:", app_menu);
    msg<void>(main, "addItem:", app_item);
    msg<void>(app_item, "release");
    msg<void>(app_menu, "release");
    for (const auto &top : menus) {
        id item = menu_item(top, view);
        id sub = build_menu(top.label, top.sub, view);
        msg<void>(item, "setSubmenu:", sub);
        msg<void>(main, "addItem:", item);
        msg<void>(sub, "release");
        msg<void>(item, "release");
    }
    msg<void>(app, "setMainMenu:", main);
    msg<void>(main, "release");
    return true;
}

}  // namespace

std::unique_ptr<Window> Window::open(const std::string &title, int w, int h, const raster::Image *icon) {
    if (g_win || !load()) return nullptr;
    std::unique_ptr<Mac> win(new Mac());
    g_win = win.get();
    if (!win->start(title, w, h, icon)) {
        g_win = nullptr;
        return nullptr;
    }
    return std::unique_ptr<Window>(win.release());
}

bool system_dark() {
    if (!load()) return false;
    Pool pool;
    std::string style = str(msg(msg(cls("NSUserDefaults"), "standardUserDefaults"), "stringForKey:", ns("AppleInterfaceStyle")));
    return style == "Dark";
}

std::string system_accent() {
    if (!load()) return "";
    Pool pool;
    id ncolor = cls("NSColor");
    if (!msg<BOOL>(ncolor, "respondsToSelector:", sel("controlAccentColor"))) return "";   // macOS 10.14 and later
    id c = msg(msg(ncolor, "controlAccentColor"), "colorUsingColorSpace:", msg(cls("NSColorSpace"), "sRGBColorSpace"));
    if (!c) return "";
    auto ch = [&](const char *s) { return (int)std::lround(std::max(0.0, std::min(1.0, msg_f(c, s))) * 255); };
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", ch("redComponent"), ch("greenComponent"), ch("blueComponent"));
    return hex;
}

std::pair<std::string, double> system_font() {
    if (!load()) return {"", 0};
    Pool pool;
    return {"", msg_f(cls("NSFont"), "systemFontSize")};
}

}  // namespace plat

#endif  // __APPLE__
