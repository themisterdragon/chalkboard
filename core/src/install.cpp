#include "install.h"

#include "art.h"
#include "files.h"
#include "proc.h"
#include "text.h"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <climits>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace install {

#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string narrow(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#endif

std::string self_path() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return n ? narrow(std::wstring(buf, n)) : "";
#elif defined(__APPLE__)
    char buf[PATH_MAX * 2];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    char real[PATH_MAX];
    std::string exe = realpath(buf, real) ? real : buf;
    // the .app bundle around it, if there is one
    size_t at = exe.rfind(".app/");
    return at == std::string::npos ? exe : exe.substr(0, at + 4);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, (size_t)n) : "";
#endif
}

Plan plan() {
    Plan p;
    std::string me = self_path();
    if (me.empty()) return p;
#ifdef _WIN32
    std::string base = files::env("LOCALAPPDATA");
    if (base.empty()) base = files::join(files::home(), "AppData\\Local");
    std::string target = files::join(files::join(files::join(base, "Programs"), "Chalkboard"), "Chalkboard.exe");
    if (text::lower(me) == text::lower(target)) return p;
    p = {"windows", me, target, "Chalkboard will be copied to your Programs folder and added to the Start menu."};
#elif defined(__APPLE__)
    if (!text::ends_with(me, ".app") || !text::starts_with(me, "/Volumes/")) return p;   // already copied somewhere
    std::string apps = access("/Applications", W_OK) == 0 ? "/Applications" : files::join(files::home(), "Applications");
    p = {"mac", me, files::join(apps, files::basename(me)),
         "Chalkboard will be copied to your " + files::basename(apps) + " folder and reopened from there. Then you can eject the disk image."};
#else
    std::string target = files::join(files::join(files::home(), ".local/bin"), "chalkboard");
    // installed by a package (or already copied): nothing to do
    if (me == target || text::starts_with(me, "/usr/") || text::starts_with(me, "/opt/") || text::starts_with(me, "/nix/")) return p;
    p = {"linux", me, target, "Chalkboard will be installed as chalkboard, with an entry in your app menu."};
#endif
    return p;
}

#ifdef _WIN32
static void shortcut(const std::string &link, const std::string &target) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW *sl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&sl);
    if (FAILED(hr)) {
        if (SUCCEEDED(init)) CoUninitialize();
        throw Error{"COULDN'T MAKE A SHORTCUT"};
    }
    sl->SetPath(wide(target).c_str());
    sl->SetWorkingDirectory(wide(files::dirname(target)).c_str());
    sl->SetIconLocation(wide(target).c_str(), 0);
    sl->SetDescription(L"Chalkboard lesson planner");
    IPersistFile *pf = nullptr;
    hr = sl->QueryInterface(IID_IPersistFile, (void **)&pf);
    if (SUCCEEDED(hr)) {
        hr = pf->Save(wide(link).c_str(), TRUE);
        pf->Release();
    }
    sl->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) throw Error{"COULDN'T SAVE THE SHORTCUT " + link};
}
#endif

std::string install(const Plan &p, bool desktop_shortcut) {
    std::string err;
    if (p.kind.empty()) return "";
    if (!files::make_dirs(files::dirname(p.target), &err)) throw Error{err};
#ifdef _WIN32
    (void)desktop_shortcut;
    if (!CopyFileW(wide(p.source).c_str(), wide(p.target).c_str(), FALSE)) throw Error{"COULDN'T COPY CHALKBOARD TO " + p.target};
    std::string roaming = files::env("APPDATA");
    if (roaming.empty()) roaming = files::join(files::home(), "AppData\\Roaming");
    std::vector<std::string> links = {files::join(roaming, "Microsoft\\Windows\\Start Menu\\Programs\\Chalkboard.lnk")};
    if (desktop_shortcut) links.push_back(files::join(files::join(files::home(), "Desktop"), "Chalkboard.lnk"));
    for (const auto &l : links) {
        files::make_dirs(files::dirname(l));
        shortcut(l, p.target);
    }
    return std::string("Chalkboard is in your Start menu") + (desktop_shortcut ? " and on your desktop." : ".");
#elif defined(__APPLE__)
    (void)desktop_shortcut;
    if (files::exists(p.target) && !proc::run({"/bin/rm", "-rf", p.target})) throw Error{"COULDN'T REPLACE " + p.target};
    if (!proc::run({"/usr/bin/ditto", p.source, p.target}, 300)) throw Error{"COULDN'T COPY CHALKBOARD TO " + p.target};
    proc::run({"/usr/bin/xattr", "-dr", "com.apple.quarantine", p.target});
    return "Chalkboard is in your Applications folder.";
#else
    (void)desktop_shortcut;
    // copy to a temporary name, then move it into place (a running copy there keeps working)
    std::string tmp = p.target + ".new";
    if (!files::copy(p.source, tmp, &err)) throw Error{err};
    chmod(tmp.c_str(), 0755);
    if (!files::rename(tmp, p.target, &err)) throw Error{err};
    std::string share = files::env("XDG_DATA_HOME");
    if (share.empty()) share = files::join(files::home(), ".local/share");
    std::string icon = files::join(share, "icons/chalkboard.png");
    files::make_dirs(files::dirname(icon));
    if (!files::write(icon, art::logo_png(256, 16), &err)) throw Error{err};
    std::string entry = files::join(share, "applications/chalkboard.desktop");
    files::make_dirs(files::dirname(entry));
    std::string desk = "[Desktop Entry]\nVersion=1.0\nName=Chalkboard\nComment=Lesson planner\nExec=\"" + p.target +
                       "\" --window\nTerminal=false\nType=Application\nIcon=" + icon + "\nStartupWMClass=Chalkboard\nCategories=Education;Office;\n";
    if (!files::write(entry, desk, &err)) throw Error{err};
    // Chalkboard 2.x put its window app at chalkboard-gui with a menu entry of its own: retire the
    // entry, and make chalkboard-gui open this one (only the files 2.x's install.sh made)
    std::string old_entry = files::join(share, "applications/chalkboard-gui.desktop"), old_desk;
    std::string old_bin = files::join(files::dirname(p.target), "chalkboard-gui");
    if (files::read(old_entry, old_desk) && old_desk.find("Exec=" + old_bin) != std::string::npos) {
        files::remove(old_entry);
        files::remove(files::join(share, "icons/chalkboard-gui.png"));
    }
    std::string head;
    if (files::read_range(old_bin, 0, 64, head) && text::starts_with(head, "#!") && head.find("python") != std::string::npos) {
        files::remove(old_bin);
        if (symlink("chalkboard", old_bin.c_str()) != 0) files::remove(old_bin);
    }
    return "Chalkboard is in your app menu.";
#endif
}

bool relaunch(const Plan &p) {
    if (p.kind != "mac") return false;
    return proc::start({"/usr/bin/open", "-n", p.target});
}

}  // namespace install
