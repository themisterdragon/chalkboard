// Files and folders, with UTF-8 paths on every system (Windows included).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace files {

bool read(const std::string &path, std::string &out, std::string *err = nullptr);
// up to len bytes from offset (fewer at the end of the file); false only if it can't be opened
bool read_range(const std::string &path, uint64_t offset, size_t len, std::string &out);
bool write(const std::string &path, const std::string &data, std::string *err = nullptr);
// Write atomically through path + ".tmp", readable only by this user (lessons, names, school).
bool write_private(const std::string &path, const std::string &data, std::string *err = nullptr);
bool exists(const std::string &path);
bool is_file(const std::string &path);
bool is_dir(const std::string &path);
bool make_dirs(const std::string &path, std::string *err = nullptr);
std::vector<std::string> list(const std::string &dir);       // names, sorted (like sorted(os.listdir))
bool remove(const std::string &path, std::string *err = nullptr);
bool copy(const std::string &from, const std::string &to, std::string *err = nullptr);   // keeps the time (copy2)
bool rename(const std::string &from, const std::string &to, std::string *err = nullptr);
bool make_private(const std::string &path);                  // chmod 600 (no-op on Windows)
double mtime(const std::string &path);                       // seconds; 0 when missing

std::string join(const std::string &a, const std::string &b);
std::string basename(const std::string &path);
std::string dirname(const std::string &path);
std::string stem(const std::string &path);                    // "a/b.c.json" -> "b.c"
std::string ext(const std::string &path);                     // ".json"
std::string home();
std::string expanduser(const std::string &path);
std::string env(const char *name);
std::string sep();                                           // "/" or "\\"

}  // namespace files
