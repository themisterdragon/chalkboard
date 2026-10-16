#include "text.h"

namespace text {

std::u32string to32(const std::string &s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { out += 0xFFFD; i++; continue; }
        if (i + (size_t)len > n) { out += 0xFFFD; i++; continue; }
        bool ok = true;
        for (int k = 1; k < len; k++) {
            unsigned char d = (unsigned char)s[i + (size_t)k];
            if ((d & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok) { out += 0xFFFD; i++; continue; }
        out += cp;
        i += (size_t)len;
    }
    return out;
}

std::string to8(char32_t c) {
    std::string out;
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
        out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
    } else {
        out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F));
        out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
    }
    return out;
}

std::string to8(const std::u32string &s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) out += to8(c);
    return out;
}

size_t length(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

bool isspace(char32_t c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool isdigit(char32_t c) { return c >= '0' && c <= '9'; }

static bool is_letter(char32_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    if (c < 0x80) return false;
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;
    if (c < 0xC0) return false;
    if (c == 0xD7 || c == 0xF7) return false;
    if (c <= 0x2AF) return true;                                   // Latin
    if (c >= 0x370 && c <= 0x3FF) return c != 0x37E && c != 0x387; // Greek
    if (c >= 0x400 && c <= 0x52F) return true;                     // Cyrillic
    if (isspace(c)) return false;
    if (c >= 0x2000 && c <= 0x2BFF) return false;                  // punctuation, symbols, arrows, shapes
    if (c >= 0x3000 && c <= 0x303F) return false;                  // CJK punctuation
    if (c >= 0xFE30 && c <= 0xFE4F) return false;
    if (c >= 0xFF00 && c <= 0xFF0F) return false;
    if (c >= 0x1F000) return false;                                // emoji and pictographs
    return c >= 0x530;                                             // other scripts: letters
}

bool isalnum(char32_t c) { return isdigit(c) || is_letter(c); }
bool isword(char32_t c) { return c == '_' || isalnum(c); }

char32_t lower(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if ((c >= 0xC0 && c <= 0xDE) && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130) return 'i';
        if (c >= 0x139 && c <= 0x148) return (c % 2) ? c + 1 : c;
        if (c >= 0x179 && c <= 0x17E) return (c % 2) ? c + 1 : c;
        if (c == 0x178) return 0xFF;
        if (c == 0x138 || c == 0x149 || c == 0x17F) return c;
        return (c % 2) ? c : c + 1;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;     // Greek
    if (c >= 0x410 && c <= 0x42F) return c + 32;                   // Cyrillic
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

char32_t upper(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0xE0) return c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F) {
        if (c >= 0x139 && c <= 0x148) return (c % 2) ? c : c - 1;
        if (c >= 0x179 && c <= 0x17E) return (c % 2) ? c : c - 1;
        if (c == 0x131) return 'I';
        if (c == 0x138 || c == 0x149 || c == 0x17F || c == 0x130) return c;
        return (c % 2) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

std::string lower(const std::string &s) {
    bool ascii = true;
    for (unsigned char c : s) if (c >= 0x80) { ascii = false; break; }
    if (ascii) {
        std::string o = s;
        for (auto &c : o) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return o;
    }
    std::u32string u = to32(s);
    for (auto &c : u) c = lower(c);
    return to8(u);
}

std::string upper(const std::string &s) {
    std::u32string u = to32(s);
    std::string out;
    for (char32_t c : u) {
        if (c == 0xDF) { out += "SS"; continue; }   // ß -> SS, like Python
        out += to8(upper(c));
    }
    return out;
}

std::string title(const std::string &s) {
    std::u32string u = to32(s);
    bool prev = false;
    for (auto &c : u) {
        bool cased = is_letter(c);
        c = cased ? (prev ? lower(c) : upper(c)) : c;
        prev = cased;
    }
    return to8(u);
}

static bool in_set(char32_t c, const std::u32string &set) { return set.find(c) != std::u32string::npos; }

std::string strip_chars(const std::string &s, const std::string &chars) {
    std::u32string u = to32(s), set = to32(chars);
    size_t a = 0, b = u.size();
    while (a < b && in_set(u[a], set)) a++;
    while (b > a && in_set(u[b - 1], set)) b--;
    return to8(u.substr(a, b - a));
}

std::string rstrip_chars(const std::string &s, const std::string &chars) {
    std::u32string u = to32(s), set = to32(chars);
    size_t b = u.size();
    while (b > 0 && in_set(u[b - 1], set)) b--;
    return to8(u.substr(0, b));
}

std::string lstrip_chars(const std::string &s, const std::string &chars) {
    std::u32string u = to32(s), set = to32(chars);
    size_t a = 0;
    while (a < u.size() && in_set(u[a], set)) a++;
    return to8(u.substr(a));
}

// whitespace in UTF-8: one ASCII byte, or one of the multi-byte spaces
static size_t space_at(const std::string &s, size_t i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) return isspace(c) ? 1 : 0;
    if (c == 0xC2 && i + 1 < s.size() && ((unsigned char)s[i + 1] == 0x85 || (unsigned char)s[i + 1] == 0xA0)) return 2;
    if ((c == 0xE1 || c == 0xE2 || c == 0xE3) && i + 2 < s.size()) {
        std::u32string u = to32(s.substr(i, 3));
        return u.size() == 1 && isspace(u[0]) ? 3 : 0;
    }
    return 0;
}

// last character of s ending at byte b (exclusive): how many bytes if it's a space
static size_t space_before(const std::string &s, size_t b) {
    if (b == 0) return 0;
    unsigned char c = (unsigned char)s[b - 1];
    if (c < 0x80) return isspace(c) ? 1 : 0;
    size_t a = b - 1;
    while (a > 0 && ((unsigned char)s[a] & 0xC0) == 0x80) a--;
    return space_at(s, a) == b - a ? b - a : 0;
}

std::string strip(const std::string &s) {
    size_t a = 0, b = s.size(), k;
    while (a < b && (k = space_at(s, a))) a += k;
    while (b > a && (k = space_before(s, b))) b -= k;
    return s.substr(a, b - a);
}

std::string lstrip(const std::string &s) {
    size_t a = 0, k;
    while (a < s.size() && (k = space_at(s, a))) a += k;
    return s.substr(a);
}

std::string rstrip(const std::string &s) {
    size_t b = s.size(), k;
    while (b > 0 && (k = space_before(s, b))) b -= k;
    return s.substr(0, b);
}

std::vector<std::string> split(const std::string &s, const std::string &sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t k = s.find(sep, at);
        if (k == std::string::npos) { out.push_back(s.substr(at)); return out; }
        out.push_back(s.substr(at, k - at));
        at = k + sep.size();
    }
}

std::vector<std::string> split_ws(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size(), k;
    while (i < n) {
        while (i < n && (k = space_at(s, i))) i += k;
        if (i >= n) break;
        size_t a = i;
        while (i < n && !space_at(s, i)) i++;
        out.push_back(s.substr(a, i - a));
    }
    return out;
}

std::string join(const std::vector<std::string> &parts, const std::string &sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string squash(const std::string &s) { return join(split_ws(s), " "); }

bool starts_with(const std::string &s, const std::string &p) { return s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool contains(const std::string &s, const std::string &p) { return s.find(p) != std::string::npos; }

std::string replace(const std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    std::string out;
    size_t at = 0;
    for (;;) {
        size_t k = s.find(from, at);
        if (k == std::string::npos) { out.append(s, at, std::string::npos); return out; }
        out.append(s, at, k - at);
        out += to;
        at = k + from.size();
    }
}

std::string repeat(const std::string &s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; i++) out += s;
    return out;
}

std::string slice(const std::string &s, long a, long b) {
    std::u32string u = to32(s);
    long n = (long)u.size();
    if (a < 0) a += n;
    if (b < 0) b += n;
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a >= b) return "";
    return to8(u.substr((size_t)a, (size_t)(b - a)));
}

std::string center(const std::string &s, long width) {
    long n = (long)length(s);
    if (n >= width) return s;
    long marg = width - n, left = marg / 2 + (marg & width & 1);   // CPython's rounding
    return std::string((size_t)left, ' ') + s + std::string((size_t)(marg - left), ' ');
}

std::string ljust(const std::string &s, long width) {
    long n = (long)length(s);
    return n >= width ? s : s + std::string((size_t)(width - n), ' ');
}

std::string rjust(const std::string &s, long width) {
    long n = (long)length(s);
    return n >= width ? s : std::string((size_t)(width - n), ' ') + s;
}

}  // namespace text
