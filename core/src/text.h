// Text helpers that behave like Python's str methods on UTF-8 text.
#pragma once
#include <string>
#include <vector>

namespace text {

std::u32string to32(const std::string &s);     // UTF-8 -> code points (bad bytes become U+FFFD)
std::string to8(const std::u32string &s);
std::string to8(char32_t c);
size_t length(const std::string &s);            // len() in code points

bool isspace(char32_t c);                       // str.isspace()
bool isword(char32_t c);                        // re's \w (letters, digits, _)
bool isdigit(char32_t c);                       // ASCII 0-9 (what Chalkboard's patterns mean)
bool isalnum(char32_t c);
char32_t lower(char32_t c);
char32_t upper(char32_t c);

std::string lower(const std::string &s);        // str.lower()
std::string upper(const std::string &s);        // str.upper()
std::string title(const std::string &s);        // str.title()
std::string strip(const std::string &s);        // str.strip()
std::string lstrip(const std::string &s);
std::string rstrip(const std::string &s);
std::string strip_chars(const std::string &s, const std::string &chars);   // str.strip(chars)
std::string rstrip_chars(const std::string &s, const std::string &chars);
std::string lstrip_chars(const std::string &s, const std::string &chars);
std::vector<std::string> split(const std::string &s, const std::string &sep);   // str.split(sep)
std::vector<std::string> split_ws(const std::string &s);                        // str.split()
std::string join(const std::vector<std::string> &parts, const std::string &sep);
std::string squash(const std::string &s);       // " ".join(s.split())
bool starts_with(const std::string &s, const std::string &p);
bool ends_with(const std::string &s, const std::string &p);
bool contains(const std::string &s, const std::string &p);
std::string replace(const std::string &s, const std::string &from, const std::string &to);
std::string repeat(const std::string &s, size_t n);
std::string slice(const std::string &s, long a, long b = 0x3fffffffL);   // s[a:b] in code points
std::string center(const std::string &s, long width);   // str.center(width)
std::string ljust(const std::string &s, long width);    // str.ljust(width)
std::string rjust(const std::string &s, long width);    // str.rjust(width)

}  // namespace text
