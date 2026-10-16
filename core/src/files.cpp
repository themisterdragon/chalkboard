#include "files.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace files {

static fs::path P(const std::string &s) { return fs::u8path(s); }
static std::string U(const fs::path &p) { return p.u8string(); }

static void set_err(std::string *err, const std::string &m) {
    if (err) *err = m;
}

bool read(const std::string &path, std::string &out, std::string *err) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) {
        std::error_code ec;
        set_err(err, !fs::exists(P(path), ec) ? "No such file or directory" : fs::is_directory(P(path), ec) ? "Is a directory" : "Permission denied");
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool read_range(const std::string &path, uint64_t offset, size_t len, std::string &out) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return false;
    out.assign(len, '\0');
    f.seekg((std::streamoff)offset);
    if (!f) {
        out.clear();
        return true;
    }
    f.read(&out[0], (std::streamsize)len);
    out.resize((size_t)f.gcount());
    return true;
}

bool write(const std::string &path, const std::string &data, std::string *err) {
    std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
    if (!f) { set_err(err, "Couldn't write " + path); return false; }
    f.write(data.data(), (std::streamsize)data.size());
    f.close();
    if (!f) { set_err(err, "Couldn't finish writing " + path + " (is the disk full?)"); return false; }
    return true;
}

bool write_private(const std::string &path, const std::string &data, std::string *err) {
    std::string tmp = path + ".tmp";
#ifdef _WIN32
    if (!write(tmp, data, err)) return false;
#else
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { set_err(err, std::string(std::strerror(errno)) + ": '" + tmp + "'"); return false; }
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            set_err(err, std::string(std::strerror(errno)) + ": '" + tmp + "'");
            ::close(fd);
            return false;
        }
        off += (size_t)n;
    }
    if (::close(fd) != 0) { set_err(err, std::string(std::strerror(errno)) + ": '" + tmp + "'"); return false; }
    ::chmod(tmp.c_str(), 0600);   // in case the .tmp was left over from before
#endif
    std::error_code ec;
    fs::rename(P(tmp), P(path), ec);
    if (ec) {
#ifdef _WIN32
        // replacing a file that's open elsewhere can fail on Windows; try once more the long way
        if (MoveFileExW(P(tmp).wstring().c_str(), P(path).wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
#endif
        set_err(err, ec.message() + ": '" + path + "'");
        return false;
    }
    return true;
}

bool exists(const std::string &path) {
    std::error_code ec;
    return fs::exists(P(path), ec);
}

bool is_file(const std::string &path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}

bool is_dir(const std::string &path) {
    std::error_code ec;
    return fs::is_directory(P(path), ec);
}

bool make_dirs(const std::string &path, std::string *err) {
    std::error_code ec;
    if (fs::is_directory(P(path), ec)) return true;
    fs::create_directories(P(path), ec);
    if (ec) { set_err(err, ec.message() + ": '" + path + "'"); return false; }
    return true;
}

std::vector<std::string> list(const std::string &dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) out.push_back(U(it->path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

bool remove(const std::string &path, std::string *err) {
    std::error_code ec;
    if (!fs::remove(P(path), ec) || ec) {
        set_err(err, ec ? ec.message() + ": '" + path + "'" : "No such file or directory: '" + path + "'");
        return false;
    }
    return true;
}

bool copy(const std::string &from, const std::string &to, std::string *err) {
    std::error_code ec;
    fs::copy_file(P(from), P(to), fs::copy_options::overwrite_existing, ec);
    if (ec) { set_err(err, ec.message()); return false; }
    auto t = fs::last_write_time(P(from), ec);
    if (!ec) fs::last_write_time(P(to), t, ec);
    return true;
}

bool rename(const std::string &from, const std::string &to, std::string *err) {
    std::error_code ec;
    fs::rename(P(from), P(to), ec);
    if (ec) { set_err(err, ec.message()); return false; }
    return true;
}

bool make_private(const std::string &path) {
#ifdef _WIN32
    (void)path;
    return true;
#else
    return ::chmod(path.c_str(), 0600) == 0;
#endif
}

double mtime(const std::string &path) {
#ifdef _WIN32
    std::error_code ec;
    auto t = fs::last_write_time(P(path), ec);
    if (ec) return 0;
    return (double)std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count() / 1000.0;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return 0;
#ifdef __APPLE__
    return (double)st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec / 1e9;
#else
    return (double)st.st_mtim.tv_sec + st.st_mtim.tv_nsec / 1e9;
#endif
#endif
}

std::string sep() {
#ifdef _WIN32
    return "\\";
#else
    return "/";
#endif
}

std::string join(const std::string &a, const std::string &b) {
    // os.path.join: an absolute b replaces a
    if (a.empty()) return b;
    if (!b.empty() && (b[0] == '/' || (b.size() > 1 && b[1] == ':') || b[0] == '\\')) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + sep() + b;
}

std::string basename(const std::string &path) {
    size_t k = path.find_last_of("/\\");
    return k == std::string::npos ? path : path.substr(k + 1);
}

std::string dirname(const std::string &path) {
    size_t k = path.find_last_of("/\\");
    if (k == std::string::npos) return "";
    if (k == 0) return path.substr(0, 1);
    return path.substr(0, k);
}

std::string ext(const std::string &path) {
    std::string b = basename(path);
    size_t k = b.rfind('.');
    if (k == std::string::npos || k == 0) return "";
    // os.path.splitext: leading dots don't count
    size_t lead = 0;
    while (lead < b.size() && b[lead] == '.') lead++;
    if (k < lead) return "";
    return b.substr(k);
}

std::string stem(const std::string &path) {
    std::string b = basename(path), e = ext(path);
    return b.substr(0, b.size() - e.size());
}

std::string env(const char *name) {
#ifdef _WIN32
    // _wgetenv so names with accents come through
    std::wstring w(name, name + std::strlen(name));
    const wchar_t *v = _wgetenv(w.c_str());
    return v ? U(fs::path(v)) : "";
#else
    const char *v = std::getenv(name);
    return v ? v : "";
#endif
}

std::string home() {
#ifdef _WIN32
    std::string h = env("USERPROFILE");
    if (h.empty()) h = env("HOMEDRIVE") + env("HOMEPATH");
    return h;
#else
    return env("HOME");
#endif
}

std::string expanduser(const std::string &path) {
    if (path == "~") return home();
    if (path.size() >= 2 && path[0] == '~' && (path[1] == '/' || path[1] == '\\')) return home() + path.substr(1);
    return path;
}

}  // namespace files
