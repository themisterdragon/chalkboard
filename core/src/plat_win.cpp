// Windows: a plain Win32 window that shows the pixels Chalkboard draws. Sharp on high-DPI screens
// (per-monitor DPI), dark title bar in dark mode, the system's file pickers, and the clipboard.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>

#include "plat.h"

namespace plat {

namespace {

std::wstring wide(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const wchar_t *w, int len = -1) {
    if (!w) return "";
    if (len < 0) len = (int)wcslen(w);
    if (len == 0) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
    return s;
}

const UINT WM_WAKE = WM_APP + 1;

int mods_now() {
    int m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ALT;
    return m;
}

int key_of(WPARAM vk) {
    switch (vk) {
    case VK_LEFT: return K_LEFT;
    case VK_RIGHT: return K_RIGHT;
    case VK_UP: return K_UP;
    case VK_DOWN: return K_DOWN;
    case VK_HOME: return K_HOME;
    case VK_END: return K_END;
    case VK_PRIOR: return K_PGUP;
    case VK_NEXT: return K_PGDN;
    case VK_RETURN: return K_RETURN;
    case VK_ESCAPE: return K_ESCAPE;
    case VK_TAB: return K_TAB;
    case VK_BACK: return K_BACKSPACE;
    case VK_DELETE: return K_DELETE;
    case VK_INSERT: return K_INSERT;
    case VK_APPS: return K_MENU;
    case VK_SPACE: return ' ';
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return K_F1 + (int)(vk - VK_F1);
    if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return (int)vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (int)('0' + vk - VK_NUMPAD0);
    if (vk == VK_ADD) return '+';
    if (vk == VK_SUBTRACT) return '-';
    // =, -, and the rest: the character the key makes on this keyboard layout
    UINT c = MapVirtualKeyW((UINT)vk, 2 /* MAPVK_VK_TO_CHAR */) & 0x7fffffff;
    if (c >= 0x20 && c < 0x7f) return (int)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return 0;
}

class Win : public Window {
public:
    HWND hwnd = nullptr;
    std::deque<Event> q;
    std::mutex mu;
    std::vector<uint32_t> buf;   // BGRX, top-down
    int bw = 0, bh = 0;
    int min_w = 0, min_h = 0;    // points
    double dpi = 96;
    bool tracking = false, is_max = false;
    HCURSOR cur = nullptr;
    wchar_t high = 0;            // the first half of a character outside the BMP
    // double-clicks: the system's timing and distance
    DWORD last_t = 0;
    int last_x = -100000, last_y = -100000, last_b = 0, clicks = 0;
    int captured = 0;

    void push(const Event &e) {
        std::lock_guard<std::mutex> lock(mu);
        q.push_back(e);
    }

    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Win *w = (Win *)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (msg == WM_NCCREATE) {
            auto *cs = (CREATESTRUCTW *)lp;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(h, msg, wp, lp);
        }
        if (!w) return DefWindowProcW(h, msg, wp, lp);
        return w->handle(msg, wp, lp);
    }

    void mouse(Event::Type t, int button, LPARAM lp) {
        Event e;
        e.type = t;
        e.x = GET_X_LPARAM(lp);
        e.y = GET_Y_LPARAM(lp);
        e.button = button;
        e.mods = mods_now();
        if (t == Event::DOWN) {
            DWORD now = GetMessageTime();
            int dx = GetSystemMetrics(SM_CXDOUBLECLK) / 2, dy = GetSystemMetrics(SM_CYDOUBLECLK) / 2;
            if (button == last_b && now - last_t <= GetDoubleClickTime() && std::abs((int)e.x - last_x) <= dx && std::abs((int)e.y - last_y) <= dy) clicks++;
            else clicks = 1;
            last_t = now;
            last_x = (int)e.x;
            last_y = (int)e.y;
            last_b = button;
            e.clicks = clicks;
            if (!captured++) SetCapture(hwnd);
            SetFocus(hwnd);
        } else if (t == Event::UP) {
            if (captured && !--captured) ReleaseCapture();
        }
        push(e);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CLOSE: {
            Event e;
            e.type = Event::CLOSE;
            push(e);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (!buf.empty()) {
                BITMAPINFO bi = {};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = bw;
                bi.bmiHeader.biHeight = -bh;   // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                SetDIBitsToDevice(dc, 0, 0, (DWORD)bw, (DWORD)bh, 0, 0, 0, (UINT)bh, buf.data(), &bi, DIB_RGB_COLORS);
                // past the picture (a resize it hasn't caught up with): the edge color, not garbage
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (rc.right > bw || rc.bottom > bh) {
                    uint32_t px = buf.back();
                    HBRUSH br = CreateSolidBrush(RGB((px >> 16) & 255, (px >> 8) & 255, px & 255));
                    RECT a = {bw, 0, rc.right, rc.bottom}, b = {0, bh, bw, rc.bottom};
                    FillRect(dc, &a, br);
                    FillRect(dc, &b, br);
                    DeleteObject(br);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE: {
            is_max = wp == SIZE_MAXIMIZED;
            if (wp == SIZE_MINIMIZED) return 0;
            Event e;
            e.type = Event::RESIZE;
            push(e);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto *mm = (MINMAXINFO *)lp;
            RECT r = {0, 0, (LONG)(min_w * dpi / 96), (LONG)(min_h * dpi / 96)};
            AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
            mm->ptMinTrackSize.x = r.right - r.left;
            mm->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }
        case 0x02E0: {   // WM_DPICHANGED: a screen with another scale
            dpi = HIWORD(wp);
            RECT *r = (RECT *)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Event e;
            e.type = Event::RESIZE;
            push(e);
            return 0;
        }
        case WM_SETTINGCHANGE: {   // light/dark or the accent color changed
            Event e;
            e.type = Event::LOOK;
            push(e);
            break;
        }
        case WM_MOUSEMOVE: {
            if (!tracking) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                tracking = true;
            }
            Event e;
            e.type = Event::MOVE;
            e.x = GET_X_LPARAM(lp);
            e.y = GET_Y_LPARAM(lp);
            e.mods = mods_now();
            if (wp & MK_LBUTTON) e.button = 1;
            push(e);
            return 0;
        }
        case WM_MOUSELEAVE: {
            tracking = false;
            Event e;
            e.type = Event::LEAVE;
            push(e);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: mouse(Event::DOWN, 1, lp); return 0;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: mouse(Event::DOWN, 2, lp); return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: mouse(Event::DOWN, 3, lp); return 0;
        case WM_LBUTTONUP: mouse(Event::UP, 1, lp); return 0;
        case WM_MBUTTONUP: mouse(Event::UP, 2, lp); return 0;
        case WM_RBUTTONUP: mouse(Event::UP, 3, lp); return 0;
        case WM_CAPTURECHANGED:
            captured = 0;
            return 0;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            Event e;
            e.type = Event::WHEEL;
            e.x = p.x;
            e.y = p.y;
            e.mods = mods_now();
            double d = GET_WHEEL_DELTA_WPARAM(wp) / 120.0;
            if (msg == WM_MOUSEWHEEL) e.dy = -d;
            else e.dx = d;
            push(e);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && cur) {
                SetCursor(cur);
                return TRUE;
            }
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            Event e;
            e.type = msg == WM_SETFOCUS ? Event::FOCUS_IN : Event::FOCUS_OUT;
            push(e);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int m = mods_now();
            if (msg == WM_SYSKEYDOWN && wp == VK_F4 && (m & ALT)) break;   // Alt+F4 closes the window
            int k = key_of(wp);
            if (!k) break;
            Event e;
            e.type = Event::KEY;
            e.key = k;
            e.mods = m;
            push(e);
            return 0;
        }
        case WM_SYSKEYUP:
            if (wp == VK_MENU || wp == VK_F10) return 0;   // don't open the (hidden) system menu
            break;
        case WM_CHAR:
        case WM_SYSCHAR: {
            wchar_t c = (wchar_t)wp;
            if (msg == WM_SYSCHAR) return 0;   // Alt+letter: the KEY event has it
            int m = mods_now();
            // Ctrl+letter makes control characters (the KEY event has it); Ctrl+Alt is AltGr, which types
            if ((m & CTRL) && !(m & ALT)) return 0;
            if (c < 0x20 || c == 0x7f) return 0;
            if (c >= 0xD800 && c < 0xDC00) {
                high = c;
                return 0;
            }
            std::wstring w;
            if (c >= 0xDC00 && c < 0xE000 && high) {
                w = std::wstring{high, c};
                high = 0;
            } else
                w = std::wstring(1, c);
            Event e;
            e.type = Event::TEXT;
            e.text = narrow(w.c_str(), (int)w.size());
            push(e);
            return 0;
        }
        case WM_WAKE: {
            Event e;
            e.type = Event::WAKE;
            push(e);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool start(const std::string &t, int w, int h) {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof wc;
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = proc;
            wc.hInstance = inst;
            wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));   // the program's own icon
            if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
            wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
            wc.lpszClassName = L"Chalkboard";
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }
        // the screen this opens on, its scale, and the room left by the taskbar
        POINT pt = {0, 0};
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(mon, &mi);
        typedef UINT(WINAPI * DpiFn)(HWND);
        HDC sdc = GetDC(nullptr);
        dpi = GetDeviceCaps(sdc, LOGPIXELSX);
        ReleaseDC(nullptr, sdc);
        RECT work = mi.rcWork;
        int pw = (int)std::lround(w * dpi / 96), ph = (int)std::lround(h * dpi / 96);
        RECT r = {0, 0, pw, ph};
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
        int ww = std::min((int)(r.right - r.left), (int)(work.right - work.left - 40)), wh = std::min((int)(r.bottom - r.top), (int)(work.bottom - work.top - 40));
        int x = work.left + (work.right - work.left - ww) / 2, y = work.top + std::max(0, (int)(work.bottom - work.top - wh) / 2);
        hwnd = CreateWindowExW(0, L"Chalkboard", wide(t).c_str(), WS_OVERLAPPEDWINDOW, x, y, ww, wh, nullptr, nullptr, inst, this);
        if (!hwnd) return false;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (auto fn = (DpiFn)(void *)GetProcAddress(u32, "GetDpiForWindow")) dpi = fn(hwnd);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        cur = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        return true;
    }

    double scale() override { return dpi / 96.0; }
    void size(int &w, int &h) override {
        RECT r;
        GetClientRect(hwnd, &r);
        w = r.right - r.left;
        h = r.bottom - r.top;
    }
    void present(const raster::Image &img) override {
        bw = img.w;
        bh = img.h;
        buf.resize((size_t)bw * (size_t)bh);
        const uint8_t *s = img.px.data();
        for (size_t i = 0, n = buf.size(); i < n; i++, s += 3) buf[i] = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
    bool take(Event &e) {
        std::lock_guard<std::mutex> lock(mu);
        if (q.empty()) return false;
        e = q.front();
        q.pop_front();
        return true;
    }
    bool next(Event &e, int timeout_ms) override {
        DWORD start = GetTickCount();
        for (;;) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {
                    Event c;
                    c.type = Event::CLOSE;
                    push(c);
                    break;
                }
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (take(e)) return true;
            DWORD wait = INFINITE;
            if (timeout_ms >= 0) {
                DWORD gone = GetTickCount() - start;
                if (gone >= (DWORD)timeout_ms) return false;
                wait = (DWORD)timeout_ms - gone;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    void wake() override { PostMessageW(hwnd, WM_WAKE, 0, 0); }
    std::string title_;
    void title(const std::string &t) override {
        if (t == title_) return;
        title_ = t;
        SetWindowTextW(hwnd, wide(t).c_str());
    }
    void cursor(Cursor c) override {
        static const LPCWSTR names[] = {(LPCWSTR)IDC_ARROW, (LPCWSTR)IDC_IBEAM, (LPCWSTR)IDC_HAND, (LPCWSTR)IDC_WAIT, (LPCWSTR)IDC_SIZENS, (LPCWSTR)IDC_SIZEWE};
        HCURSOR n = LoadCursorW(nullptr, names[c]);
        if (n == cur) return;
        cur = n;
        POINT p;
        GetCursorPos(&p);
        if (WindowFromPoint(p) == hwnd) SetCursor(cur);
    }
    std::string clipboard() override {
        std::string s;
        if (!OpenClipboard(hwnd)) return s;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto *w = (const wchar_t *)GlobalLock(h)) {
                s = narrow(w);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        // Windows line endings to plain ones
        std::string out;
        for (size_t i = 0; i < s.size(); i++)
            if (!(s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')) out += s[i];
        return out;
    }
    void set_clipboard(const std::string &s0) override {
        std::string s;
        for (char c : s0) {
            if (c == '\n') s += '\r';
            s += c;
        }
        std::wstring w = wide(s);
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (g) {
            if (void *p = GlobalLock(g)) {
                memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(g);
                if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
            } else
                GlobalFree(g);
        }
        CloseClipboard();
    }
    void minimize() override { ShowWindow(hwnd, SW_MINIMIZE); }
    void maximize(bool on) override { ShowWindow(hwnd, on ? SW_MAXIMIZE : SW_RESTORE); }
    bool maximized() override { return IsZoomed(hwnd) != 0; }
    void min_size(int w, int h) override {
        min_w = w;
        min_h = h;
    }
    void dark_title(bool dark) override {
        typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
        static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (!dwm) return;
        auto fn = (SetAttr)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (!fn) return;
        BOOL on = dark;
        if (FAILED(fn(hwnd, 20, &on, sizeof on))) fn(hwnd, 19, &on, sizeof on);   // DWMWA_USE_IMMERSIVE_DARK_MODE (19 before Windows 10 20H1)
        // repaint the frame in its new color
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    std::string pick(const std::string &t, int action, const std::vector<Filter> &filters, const std::string &name) {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::string out;
        IFileDialog *d = nullptr;
        HRESULT hr = action == 1 ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, (void **)&d)
                                 : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void **)&d);
        if (SUCCEEDED(hr)) {
            d->SetTitle(wide(t).c_str());
            DWORD opts = 0;
            d->GetOptions(&opts);
            opts |= FOS_FORCEFILESYSTEM;
            if (action == 2) opts |= FOS_PICKFOLDERS;
            if (action == 1) opts |= FOS_OVERWRITEPROMPT;
            d->SetOptions(opts);
            if (action == 1) d->SetFileName(wide(name).c_str());
            std::vector<std::wstring> names, specs;
            for (const auto &f : filters) {
                std::string spec;
                for (const auto &p : f.patterns) spec += (spec.empty() ? "" : ";") + p;
                names.push_back(wide(f.name));
                specs.push_back(wide(spec));
            }
            std::vector<COMDLG_FILTERSPEC> fs;
            for (size_t i = 0; i < names.size(); i++) fs.push_back({names[i].c_str(), specs[i].c_str()});
            if (!fs.empty()) d->SetFileTypes((UINT)fs.size(), fs.data());
            if (SUCCEEDED(d->Show(hwnd))) {
                IShellItem *item = nullptr;
                if (SUCCEEDED(d->GetResult(&item))) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                        out = narrow(p);
                        CoTaskMemFree(p);
                    }
                    item->Release();
                }
            }
            d->Release();
        }
        if (SUCCEEDED(init)) CoUninitialize();
        // the click or key that closed the picker shouldn't reach the window
        MSG m;
        while (PeekMessageW(&m, hwnd, WM_KEYFIRST, WM_MOUSELAST, PM_REMOVE)) {
        }
        captured = 0;
        return out;
    }
    std::string open_file(const std::string &t, const std::vector<Filter> &f) override { return pick(t, 0, f, ""); }
    std::string save_file(const std::string &t, const std::string &name) override { return pick(t, 1, {}, name); }
    std::string choose_folder(const std::string &t) override { return pick(t, 2, {}, ""); }
    ~Win() override {
        if (hwnd) DestroyWindow(hwnd);
    }
};

DWORD reg_dword(HKEY root, const wchar_t *path, const wchar_t *name, DWORD dflt, bool *found = nullptr) {
    DWORD v = 0, size = sizeof v;
    bool ok = RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS;
    if (found) *found = ok;
    return ok ? v : dflt;
}

}  // namespace

std::unique_ptr<Window> Window::open(const std::string &title, int w, int h, const raster::Image *icon) {
    (void)icon;   // Windows shows the program's own icon
    // crisp on high-DPI screens: per-monitor DPI where Windows has it, the whole-system kind before that
    static bool aware = false;
    if (!aware) {
        aware = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI * Ctx)(HANDLE);
        if (auto fn = (Ctx)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext")) {
            if (!fn((HANDLE)-4)) fn((HANDLE)-3);   // PER_MONITOR_AWARE_V2, else PER_MONITOR_AWARE
        } else
            SetProcessDPIAware();
    }
    std::unique_ptr<Win> win(new Win());
    if (!win->start(title, w, h)) return nullptr;
    return std::unique_ptr<Window>(win.release());
}

bool system_dark() {
    bool found = false;
    DWORD light = reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", 1, &found);
    return found && light == 0;
}

std::string system_accent() {
    bool found = false;
    DWORD c = reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0, &found);   // 0xAABBGGRR
    if (!found) return "";
    char hex[8];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", (unsigned)(c & 255), (unsigned)((c >> 8) & 255), (unsigned)((c >> 16) & 255));
    return hex;
}

std::pair<std::string, double> system_font() {
    NONCLIENTMETRICSW m = {};
    m.cbSize = sizeof m;
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) return {"", 0};
    HDC dc = GetDC(nullptr);
    double dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return {narrow(m.lfMessageFont.lfFaceName), std::fabs((double)m.lfMessageFont.lfHeight) * 72.0 / dpi};
}

}  // namespace plat

#endif
