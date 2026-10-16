#include "ttf.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "files.h"
#include "text.h"

namespace ttf {

namespace {

const FontError SHORT{"font file is cut short"};

uint32_t u8(const std::string &b, size_t o) {
    if (o + 1 > b.size()) throw SHORT;
    return (unsigned char)b[o];
}
uint32_t u16(const std::string &b, size_t o) {
    if (o + 2 > b.size()) throw SHORT;
    return ((unsigned char)b[o] << 8) | (unsigned char)b[o + 1];
}
int i16(const std::string &b, size_t o) { return (int16_t)u16(b, o); }
uint32_t u32(const std::string &b, size_t o) {
    if (o + 4 > b.size()) throw SHORT;
    return ((uint32_t)(unsigned char)b[o] << 24) | ((uint32_t)(unsigned char)b[o + 1] << 16) | ((uint32_t)(unsigned char)b[o + 2] << 8) |
           (unsigned char)b[o + 3];
}

// cp1252 byte -> Unicode, 0 for the five bytes it leaves undefined
uint32_t cp1252_char(int b) {
    static const uint32_t HIGH[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                      0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    return b >= 0x80 && b < 0xA0 ? HIGH[b - 0x80] : (uint32_t)b;
}

std::string utf16be(const std::string &raw) {
    std::u32string out;
    size_t i = 0;
    for (; i + 1 < raw.size(); i += 2) {
        uint32_t c = ((unsigned char)raw[i] << 8) | (unsigned char)raw[i + 1];
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < raw.size()) {
            uint32_t lo = ((unsigned char)raw[i + 2] << 8) | (unsigned char)raw[i + 3];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                out += (char32_t)(0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00));
                i += 2;
                continue;
            }
        }
        out += (c >= 0xD800 && c <= 0xDFFF) ? (char32_t)0xFFFD : (char32_t)c;
    }
    if (i < raw.size()) out += (char32_t)0xFFFD;   // an odd byte left over
    return text::to8(out);
}

std::string mac_roman(const std::string &raw) {
    static const uint16_t HIGH[128] = {
        0xC4, 0xC5, 0xC7, 0xC9, 0xD1, 0xD6, 0xDC, 0xE1, 0xE0, 0xE2, 0xE4, 0xE3, 0xE5, 0xE7, 0xE9, 0xE8, 0xEA, 0xEB, 0xED, 0xEC, 0xEE, 0xEF,
        0xF1, 0xF3, 0xF2, 0xF4, 0xF6, 0xF5, 0xFA, 0xF9, 0xFB, 0xFC, 0x2020, 0xB0, 0xA2, 0xA3, 0xA7, 0x2022, 0xB6, 0xDF, 0xAE, 0xA9, 0x2122, 0xB4,
        0xA8, 0x2260, 0xC6, 0xD8, 0x221E, 0xB1, 0x2264, 0x2265, 0xA5, 0xB5, 0x2202, 0x2211, 0x220F, 0x3C0, 0x222B, 0xAA, 0xBA, 0x3A9, 0xE6, 0xF8,
        0xBF, 0xA1, 0xAC, 0x221A, 0x192, 0x2248, 0x2206, 0xAB, 0xBB, 0x2026, 0xA0, 0xC0, 0xC3, 0xD5, 0x152, 0x153, 0x2013, 0x2014, 0x201C, 0x201D,
        0x2018, 0x2019, 0xF7, 0x25CA, 0xFF, 0x178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0xB7, 0x201A, 0x201E, 0x2030, 0xC2,
        0xCA, 0xC1, 0xCB, 0xC8, 0xCD, 0xCE, 0xCF, 0xCC, 0xD3, 0xD4, 0xF8FF, 0xD2, 0xDA, 0xDB, 0xD9, 0x131, 0x2C6, 0x2DC, 0xAF, 0x2D8, 0x2D9,
        0x2DA, 0xB8, 0x2DD, 0x2DB, 0x2C7};
    std::u32string out;
    for (unsigned char c : raw) out += c < 0x80 ? (char32_t)c : (char32_t)HIGH[c - 0x80];
    return text::to8(out);
}

std::map<std::string, std::pair<uint32_t, uint32_t>> tables(const std::string &data, uint32_t off) {
    uint32_t n = u16(data, off + 4);
    std::map<std::string, std::pair<uint32_t, uint32_t>> out;
    for (uint32_t i = 0; i < n; i++) {
        size_t e = off + 12 + 16 * i;
        if (e + 16 > data.size()) throw SHORT;
        std::string tag = data.substr(e, 4);
        uint32_t at = u32(data, e + 8), length = u32(data, e + 12);
        if ((uint64_t)at + length > data.size()) throw FontError{"font file is cut short"};
        out[tag] = {at, length};
    }
    return out;
}

std::map<int, std::string> names_of(const std::string &table) {
    uint32_t count = u16(table, 2), strings = u16(table, 4);
    std::map<int, std::pair<int, std::string>> found;
    for (uint32_t i = 0; i < count; i++) {
        size_t r = 6 + 12 * (size_t)i;
        uint32_t plat = u16(table, r), encid = u16(table, r + 2), lang = u16(table, r + 4), nid = u16(table, r + 6);
        uint32_t length = u16(table, r + 8), at = u16(table, r + 10);
        size_t a = std::min(table.size(), (size_t)strings + at), b = std::min(table.size(), (size_t)strings + at + length);
        std::string raw = table.substr(a, b - a);
        int rank;
        std::string t;
        if (plat == 3 && (encid == 0 || encid == 1 || encid == 10)) {
            rank = lang == 0x409 ? 0 : 1;
            t = utf16be(raw);
        } else if (plat == 1 && encid == 0) {
            rank = 2;
            t = mac_roman(raw);
        } else if (plat == 0) {
            rank = 3;
            t = utf16be(raw);
        } else continue;
        auto it = found.find((int)nid);
        if (it == found.end() || rank < it->second.first) found[(int)nid] = {rank, text::strip_chars(t, std::string("\0 ", 2))};
    }
    std::map<int, std::string> out;
    for (auto &kv : found) out[kv.first] = kv.second.second;
    return out;
}

std::map<uint32_t, uint32_t> sub(const std::string &d, size_t at, const std::vector<uint32_t> &want, bool symbol) {
    uint32_t fmt = u16(d, at);
    std::map<uint32_t, uint32_t> out;
    if (fmt == 4) {
        uint32_t seg = u16(d, at + 6) / 2;
        size_t ends = at + 14, starts = at + 16 + 2 * (size_t)seg;
        size_t deltas = starts + 2 * (size_t)seg, ranges = starts + 4 * (size_t)seg;
        for (uint32_t i = 0; i < seg; i++) {
            uint32_t end = u16(d, ends + 2 * i), start = u16(d, starts + 2 * i);
            uint32_t delta = u16(d, deltas + 2 * i), ro = u16(d, ranges + 2 * i);
            for (uint32_t ch : want) {
                uint32_t c = symbol ? ch + 0xF000 : ch;
                if (start <= c && c <= end) {
                    uint32_t g;
                    if (ro == 0) g = (c + delta) & 0xFFFF;
                    else {
                        size_t p = ranges + 2 * (size_t)i + ro + 2 * (size_t)(c - start);
                        g = p + 2 <= d.size() ? u16(d, p) : 0;
                        g = g ? (g + delta) & 0xFFFF : 0;
                    }
                    if (g) out[ch] = g;
                }
            }
        }
    } else if (fmt == 12) {
        uint32_t n = u32(d, at + 12);
        for (uint32_t i = 0; i < n; i++) {
            size_t e = at + 16 + 12 * (size_t)i;
            uint32_t start = u32(d, e), end = u32(d, e + 4), g0 = u32(d, e + 8);
            for (uint32_t ch : want)
                if (start <= ch && ch <= end) out[ch] = g0 + ch - start;
        }
    } else if (fmt == 0) {
        for (uint32_t ch : want)
            if (ch < 256 && u8(d, at + 6 + ch)) out[ch] = u8(d, at + 6 + ch);
    }
    return out;
}

const std::vector<uint32_t> &latin() {
    static const std::vector<uint32_t> L = {'0', '9', 'A', 'E', 'a', 'e', 'z'};
    return L;
}

bool describe(const std::map<std::string, std::string> &t, std::string &family, bool &bold, bool &italic) {
    auto n = names_of(t.at("name"));
    family = n.count(1) ? n[1] : "";
    if (family.empty() || family[0] == '.') return false;   // Apple's hidden system fonts start with a dot
    auto os2 = t.find("OS/2");
    if (os2 != t.end() && os2->second.size() >= 64) {
        if (u16(os2->second, 8) == 0x0002) return false;   // its maker doesn't allow embedding it in documents
        uint32_t sel = u16(os2->second, 62);
        bold = sel & 0x20;
        italic = sel & 0x01;
    } else {
        uint32_t style = u16(t.at("head"), 44);
        bold = style & 1;
        italic = style & 2;
    }
    std::string s = text::lower(n.count(2) ? n[2] : "");
    bold = bold || text::contains(s, "bold");
    italic = italic || text::contains(s, "italic") || text::contains(s, "oblique");
    return true;
}

}  // namespace

std::vector<uint32_t> faces_in(const std::string &data) {
    if (data.compare(0, 4, "ttcf") == 0) {
        uint32_t n = u32(data, 8);
        std::vector<uint32_t> out;
        for (uint32_t i = 0; i < std::min(n, 64u); i++) out.push_back(u32(data, 12 + 4 * i));
        return out;
    }
    if (data.compare(0, 4, std::string("\0\1\0\0", 4)) == 0 || data.compare(0, 4, "OTTO") == 0 || data.compare(0, 4, "true") == 0) return {0};
    return {};
}

std::map<int, std::string> names(const std::string &data, uint32_t off) {
    auto t = tables(data, off);
    if (!t.count("name")) return {};
    return names_of(data.substr(t["name"].first, t["name"].second));
}

std::map<uint32_t, uint32_t> cmap_lookup(const std::string &d, size_t base, const std::vector<uint32_t> &want) {
    std::map<std::pair<uint32_t, uint32_t>, size_t> subs;
    uint32_t count = u16(d, base + 2);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = base + 4 + 8 * (size_t)i;
        subs[{u16(d, e), u16(d, e + 2)}] = base + u32(d, e + 4);
    }
    static const std::pair<uint32_t, uint32_t> ORDER[] = {{3, 10}, {0, 4}, {0, 6}, {3, 1}, {0, 3}, {0, 2}, {0, 1}, {0, 0}, {3, 0}, {1, 0}};
    for (auto key : ORDER) {
        auto it = subs.find(key);
        if (it != subs.end() && it->second + 8 <= d.size()) {
            auto out = sub(d, it->second, want, key == std::make_pair(3u, 0u));
            if (!out.empty()) return out;
        }
    }
    return {};
}

std::vector<Found> scan(const std::string &path) {
    // only the few tables it needs: a big .ttc of Chinese, Japanese, and Korean fonts is mostly glyphs we skip
    std::vector<Found> out;
    std::string head;
    if (!files::read_range(path, 0, 12 + 4 * 64, head)) return out;
    std::vector<uint32_t> offs;
    try {
        offs = faces_in(head);
    } catch (FontError &) {
        return out;
    }
    // (a damaged face throws, and the whole file is skipped, like the Python version)
    for (size_t i = 0; i < offs.size(); i++) {
        {
            std::string top, dirs;
            files::read_range(path, offs[i], 12, top);
            if (top.size() < 12) break;
            uint32_t n = u16(top, 4);
            files::read_range(path, offs[i] + 12, 16 * (size_t)n, dirs);
            std::map<std::string, std::pair<uint32_t, uint32_t>> t;
            for (size_t k = 0; k + 16 <= dirs.size(); k += 16) t[dirs.substr(k, 4)] = {u32(dirs, k + 8), u32(dirs, k + 12)};
            bool ok = true;
            for (const char *k : {"cmap", "hmtx", "hhea", "head", "name"}) if (!t.count(k)) ok = false;
            if (!ok) continue;
            std::map<std::string, std::string> got;
            for (const char *tag : {"name", "OS/2", "head", "cmap"})
                if (t.count(tag)) files::read_range(path, t[tag].first, std::min<size_t>(t[tag].second, 1 << 20), got[tag]);
            if (cmap_lookup(got["cmap"], 0, latin()).size() < latin().size()) continue;   // no English letters
            Found f;
            f.index = (int)i;
            if (describe(got, f.family, f.bold, f.italic)) out.push_back(f);
        }
    }
    return out;
}

Face::Face(std::shared_ptr<const std::string> data, uint32_t off, const std::string &name) : data_(data), off_(off) {
    const std::string &d = *data_;
    t_ = tables(d, off);
    for (const char *k : {"head", "hhea", "hmtx", "cmap"})
        if (!t_.count(k)) throw FontError{std::string("no ") + k + " table"};
    size_t head = t_["head"].first, hhea = t_["hhea"].first;
    upm = (int)u16(d, head + 18);
    if (!upm) upm = 1000;
    double k = 1000.0 / upm;
    auto rnd = [](double v) { return (int)std::nearbyint(v); };   // Python's round(): halves to even
    for (int o : {36, 38, 40, 42}) bbox.push_back(rnd(i16(d, head + (size_t)o) * k));
    ascent = rnd(i16(d, hhea + 4) * k);
    descent = rnd(i16(d, hhea + 6) * k);
    cap = ascent;
    if (t_.count("OS/2")) {
        size_t os2 = t_["OS/2"].first;
        if (u16(d, os2) >= 2) {
            cap = rnd(i16(d, os2 + 88) * k);
            if (!cap) cap = ascent;
        }
    }
    italic_angle = t_.count("post") ? i16(d, t_["post"].first + 4) : 0;
    cff = t_.count("CFF ") || t_.count("CFF2");
    auto n = names(d, off);
    std::string p = n.count(6) && !n[6].empty() ? n[6] : !name.empty() ? name : n.count(4) && !n[4].empty() ? n[4] : "Font";
    std::u32string clean;
    for (char32_t c : text::to32(p))
        if (text::isalnum(c) || c == '-') clean += c;
    ps = text::to8(clean.substr(0, 60));
    if (ps.empty()) ps = "Font";
    family = n.count(16) && !n[16].empty() ? n[16] : n.count(1) && !n[1].empty() ? n[1] : p;
    std::vector<uint32_t> want;
    for (int b = 32; b < 256; b++)
        if (uint32_t c = cp1252_char(b)) want.push_back(c);
    std::sort(want.begin(), want.end());
    want.erase(std::unique(want.begin(), want.end()), want.end());
    auto cmap = cmap_lookup(d, t_["cmap"].first, want);
    uint32_t count = u16(d, hhea + 34);
    std::vector<uint32_t> adv;
    for (uint32_t i = 0; i < count; i++) adv.push_back(u16(d, t_["hmtx"].first + 4 * (size_t)i));
    for (int b = 32; b < 256; b++) {
        uint32_t ch = cp1252_char(b);
        if (!ch) {
            widths.push_back(0);
            continue;
        }
        auto it = cmap.find(ch);
        uint32_t g = it == cmap.end() ? 0 : it->second;
        widths.push_back(adv.empty() ? 500 : rnd(adv[std::min<size_t>(g, adv.size() - 1)] * k));
    }
}

static uint32_t checksum(std::string b) {
    while (b.size() % 4) b += '\0';
    uint32_t sum = 0;
    for (size_t i = 0; i < b.size(); i += 4) sum += u32(b, i);
    return sum;
}

std::string Face::file() const {
    const std::string &d = *data_;
    if (off_ == 0 && d.compare(0, 4, "ttcf") != 0) return d;
    std::vector<std::string> tags;
    for (auto &kv : t_) tags.push_back(kv.first);   // std::map keeps them sorted, like sorted()
    uint32_t n = (uint32_t)tags.size();
    int es = 0;
    for (int k = 0; k < 16; k++) if ((1u << k) <= n) es = k;
    std::string out;
    auto p16 = [&](std::string &s, uint32_t v) { s += (char)(v >> 8); s += (char)(v & 0xFF); };
    auto p32 = [&](std::string &s, uint32_t v) { p16(s, v >> 16); p16(s, v & 0xFFFF); };
    out += cff ? std::string("OTTO") : std::string("\0\1\0\0", 4);
    p16(out, n);
    p16(out, (1u << es) * 16);
    p16(out, (uint32_t)es);
    p16(out, n * 16 - (1u << es) * 16);
    std::string dirs, body;
    uint32_t pos = 12 + 16 * n;
    for (const auto &tag : tags) {
        auto loc = t_.at(tag);
        std::string chunk = d.substr(loc.first, loc.second);
        dirs += tag;
        p32(dirs, checksum(chunk));
        p32(dirs, pos + (uint32_t)body.size());
        p32(dirs, loc.second);
        body += chunk + std::string((4 - loc.second % 4) % 4, '\0');
    }
    return out + dirs + body;
}

std::shared_ptr<Face> load(const std::string &path, int index) {
    auto data = std::make_shared<std::string>();
    std::string err;
    if (!files::read(path, *data, &err)) throw FontError{err};
    auto offs = faces_in(*data);
    if (offs.empty()) throw FontError{"not a TrueType or OpenType font"};
    return std::make_shared<Face>(data, offs[std::min((size_t)index, offs.size() - 1)]);
}

}  // namespace ttf
