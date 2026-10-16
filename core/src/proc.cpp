#include <cstdio>
#include "proc.h"

#include <chrono>
#include <thread>

#include "files.h"
#include "text.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace proc {

std::string which(const std::string &name) {
    if (name.empty()) return "";
#ifdef _WIN32
    const char sep = ';';
    std::vector<std::string> exts;
    std::string pathext = files::env("PATHEXT");
    if (pathext.empty()) pathext = ".COM;.EXE;.BAT;.CMD";
    bool has_ext = !files::ext(name).empty();
    if (has_ext) exts.push_back("");
    for (const auto &e : text::split(pathext, ";")) if (!e.empty()) exts.push_back(text::lower(e));
#else
    const char sep = ':';
    std::vector<std::string> exts = {""};
#endif
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return files::is_file(name) ? name : "";
    for (const auto &dir : text::split(files::env("PATH"), std::string(1, sep))) {
        if (dir.empty()) continue;
        for (const auto &e : exts) {
            std::string p = files::join(dir, name + e);
#ifdef _WIN32
            if (files::is_file(p)) return p;
#else
            struct stat st;
            if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(p.c_str(), X_OK) == 0) return p;
#endif
        }
    }
    return "";
}

#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// one argument, quoted the way programs built with the Microsoft C runtime read it back
static std::wstring quote(const std::wstring &a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0;; i++) {
        size_t slashes = 0;
        while (i < a.size() && a[i] == L'\\') { i++; slashes++; }
        if (i == a.size()) { out.append(slashes * 2, L'\\'); break; }
        if (a[i] == L'"') { out.append(slashes * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(slashes, L'\\'); out += a[i]; }
    }
    return out + L"\"";
}

static bool launch(const std::vector<std::string> &argv, bool wait, int timeout_s) {
    std::wstring cmd;
    for (size_t i = 0; i < argv.size(); i++) cmd += (i ? L" " : L"") + quote(wide(argv[i]));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring app = wide(argv[0]);
    if (!CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    bool ok = true;
    if (wait) {
        DWORD r = WaitForSingleObject(pi.hProcess, (DWORD)timeout_s * 1000);
        if (r != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            ok = false;
        } else {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            ok = code == 0;
        }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok;
}
#else
static bool launch(const std::vector<std::string> &argv, bool wait, int timeout_s) {
    std::vector<char *> args;
    for (const auto &a : argv) args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    if (!wait) {
#ifdef POSIX_SPAWN_SETSID
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);   // keeps running after Chalkboard closes
#endif
    }
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0].c_str(), &fa, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return false;
    if (!wait) {
        // reap it in the background so it never lingers as a zombie
        std::thread([pid] {
            int st;
            waitpid(pid, &st, 0);
        }).detach();
        return true;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    for (;;) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (r < 0) return false;
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
#endif

bool run(const std::vector<std::string> &argv, int timeout_s) { return !argv.empty() && launch(argv, true, timeout_s); }

bool start(const std::vector<std::string> &argv) { return !argv.empty() && launch(argv, false, 0); }

bool dry_run = false;

bool open_path(const std::string &path) {
    if (dry_run) {
        std::fprintf(stderr, "open: %s\n", path.c_str());
        return true;
    }
    // Chalkboard works offline: only local files and folders, never a web address
    std::string low = text::lower(text::strip(path));
    if (low.find("://") != std::string::npos || text::starts_with(low, "www.") || text::starts_with(low, "mailto:")) return false;
#ifdef _WIN32
    return (INT_PTR)ShellExecuteW(nullptr, L"open", wide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(__APPLE__)
    std::string open = which("open");
    return !open.empty() && start({open, path});
#else
    std::string open = which("xdg-open");
    return !open.empty() && start({open, path});
#endif
}

}  // namespace proc
