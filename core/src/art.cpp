#include "art.h"

#include <map>

#include "json.h"
#include "resources.h"
#include "store.h"
#include "zlib.h"
#include "text.h"

namespace art {

const char *APP = "Chalkboard";

static const std::map<char, std::vector<std::string>> &font() {
    static const std::map<char, std::vector<std::string>> F = {
        {'A', {".#.", "#.#", "###", "#.#", "#.#"}}, {'B', {"##.", "#.#", "##.", "#.#", "##."}}, {'C', {"###", "#..", "#..", "#..", "###"}},
        {'D', {"##.", "#.#", "#.#", "#.#", "##."}}, {'H', {"#.#", "#.#", "###", "#.#", "#.#"}}, {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}},
        {'L', {"#..", "#..", "#..", "#..", "###"}}, {'O', {"###", "#.#", "#.#", "#.#", "###"}}, {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
        {' ', {"..", "..", "..", "..", ".."}},      {'E', {"###", "#..", "##.", "#..", "###"}}, {'F', {"###", "#..", "##.", "#..", "#.."}},
        {'G', {"###", "#..", "#.#", "#.#", "###"}}, {'I', {"###", ".#.", ".#.", ".#.", "###"}}, {'J', {"..#", "..#", "..#", "#.#", "###"}},
        {'M', {"#.#", "###", "###", "#.#", "#.#"}}, {'N', {"##.", "#.#", "#.#", "#.#", "#.#"}}, {'P', {"###", "#.#", "###", "#..", "#.."}},
        {'S', {"###", "#..", "###", "..#", "###"}}, {'T', {"###", ".#.", ".#.", ".#.", ".#."}}, {'U', {"#.#", "#.#", "#.#", "#.#", "###"}},
        {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}}, {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}}, {'0', {"###", "#.#", "#.#", "#.#", "###"}},
        {'1', {".#.", "##.", ".#.", ".#.", "###"}}, {'2', {"###", "..#", "###", "#..", "###"}}, {'3', {"###", "..#", ".##", "..#", "###"}},
        {'4', {"#.#", "#.#", "###", "..#", "..#"}}, {'5', {"###", "#..", "###", "..#", "###"}}, {'6', {"###", "#..", "###", "#.#", "###"}},
        {'7', {"###", "..#", ".#.", ".#.", ".#."}}, {'8', {"###", "#.#", "###", "#.#", "###"}}, {'9', {"###", "#.#", "###", "..#", "###"}},
    };
    return F;
}

std::vector<std::string> big(const std::string &t, const std::string &px) {
    std::vector<std::string> rows(5);
    std::string blank(text::length(px), ' ');
    for (char32_t c : text::to32(t)) {
        char32_t u = text::upper(c);
        auto it = font().find(u < 128 ? (char)u : ' ');
        const auto &glyph = it == font().end() ? font().at(' ') : it->second;
        for (int i = 0; i < 5; i++) {
            for (char p : glyph[(size_t)i]) rows[(size_t)i] += p == '#' ? px : blank;
            rows[(size_t)i] += blank;
        }
    }
    for (auto &r : rows) r = text::rstrip_chars(r, " ");
    return rows;
}

const std::vector<std::string> LOGO = {
    "................", "tttttttttttttttt", "tTTTTTTTTTTTTTTt", "tTnnnnnnnnnnnnTt", "tTnnwnnnnnnnnnTt", "tTnwnwnnwnnnnnTt",
    "tTnwwwnnwwwnnnTt", "tTnwnwnnwnnwnnTt", "tTnwnwnnwwwnnnTt", "tTnnnnnnnnnnnnTt", "tTnnnnnnnnnnnnTt", "tTTTTTTTTTwwTTTt",
    "tttttttttttttttt", ".tt..........tt.", ".tt..........tt.", "................",
};

static const std::map<char, uint32_t> LOGO_INK = {{'k', 0x000000}, {'w', 0xFFFFFF}, {'n', 0x1F5A3A}, {'t', 0x6B4423}, {'T', 0xA87040}};

void logo_pixels(int size, int margin, std::vector<uint8_t> &rgb, std::vector<uint8_t> &alpha) {
    int n = (int)LOGO.size(), cell = (size - 2 * margin) / n, off = (size - cell * n) / 2;
    rgb.assign((size_t)size * size * 3, 0);
    alpha.assign((size_t)size * size, 0);
    for (int y = 0; y < size; y++) {
        if (y < off || y >= off + cell * n) continue;
        int r = (y - off) / cell;
        for (int x = off; x < off + cell * n && x < size; x++) {
            char ch = LOGO[(size_t)r][(size_t)((x - off) / cell)];
            auto it = LOGO_INK.find(ch);
            if (it == LOGO_INK.end()) continue;
            size_t i = (size_t)y * size + x;
            rgb[i * 3] = (uint8_t)(it->second >> 16);
            rgb[i * 3 + 1] = (uint8_t)(it->second >> 8);
            rgb[i * 3 + 2] = (uint8_t)it->second;
            alpha[i] = 255;
        }
    }
}

std::string logo_png(int size, int margin) {
    std::vector<uint8_t> rgb, alpha;
    logo_pixels(size, margin, rgb, alpha);
    std::string raw;
    raw.reserve((size_t)size * (size * 4 + 1));
    for (int y = 0; y < size; y++) {
        raw += '\0';   // filter: none
        for (int x = 0; x < size; x++) {
            size_t i = (size_t)y * size + x;
            raw += (char)rgb[i * 3];
            raw += (char)rgb[i * 3 + 1];
            raw += (char)rgb[i * 3 + 2];
            raw += (char)alpha[i];
        }
    }
    auto be32 = [](uint32_t v) {
        std::string s(4, '\0');
        for (int i = 0; i < 4; i++) s[(size_t)i] = (char)(v >> (24 - 8 * i));
        return s;
    };
    auto chunk = [&](const std::string &type, const std::string &data) {
        return be32((uint32_t)data.size()) + type + data + be32(zlib::crc32(type + data));
    };
    std::string ihdr = be32((uint32_t)size) + be32((uint32_t)size) + std::string("\x08\x06\x00\x00\x00", 5);
    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", zlib::compress(raw)) + chunk("IEND", "");
}

std::string logo_ico() {
    // ICONDIR, then an entry per size, then the PNGs (Windows Vista and later read PNG icons)
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    auto le16 = [](unsigned v) { return std::string{(char)(v & 255), (char)(v >> 8 & 255)}; };
    auto le32 = [](uint32_t v) { return std::string{(char)(v & 255), (char)(v >> 8 & 255), (char)(v >> 16 & 255), (char)(v >> 24 & 255)}; };
    std::vector<std::string> pngs;
    for (int n : sizes) pngs.push_back(logo_png(n, n / 16));
    std::string head = le16(0) + le16(1) + le16((unsigned)pngs.size()), dir, body;
    uint32_t at = (uint32_t)(6 + 16 * pngs.size());
    for (size_t i = 0; i < pngs.size(); i++) {
        int n = sizes[i];
        dir += std::string(1, (char)(n >= 256 ? 0 : n)) + std::string(1, (char)(n >= 256 ? 0 : n)) + std::string(2, '\0') + le16(1) + le16(32) +
               le32((uint32_t)pngs[i].size()) + le32(at);
        at += (uint32_t)pngs[i].size();
        body += pngs[i];
    }
    return head + dir + body;
}

std::string logo_icns() {
    auto be32 = [](uint32_t v) { return std::string{(char)(v >> 24), (char)(v >> 16 & 255), (char)(v >> 8 & 255), (char)(v & 255)}; };
    const std::pair<const char *, int> types[] = {{"icp4", 16}, {"icp5", 32}, {"icp6", 64}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512}, {"ic10", 1024}};
    std::string body;
    for (const auto &t : types) {
        std::string png = logo_png(t.second, t.second * 6 / 64);
        body += std::string(t.first, 4) + be32((uint32_t)png.size() + 8) + png;
    }
    return std::string("icns", 4) + be32((uint32_t)body.size() + 8) + body;
}

const std::vector<std::string> &logo_sprite() {
    static const std::vector<std::string> S = [] {
        std::map<char, char> tone = {{'.', '.'}, {'k', '.'}, {'n', '.'}, {'w', 'h'}, {'t', 'd'}, {'T', 'n'}};
        std::vector<std::string> out;
        for (const auto &row : LOGO) {
            std::string r;
            for (char c : row) r += tone[c];
            out.push_back(r);
        }
        return out;
    }();
    return S;
}

static std::vector<std::string> full(const std::vector<std::string> &rows) {
    std::vector<std::string> out;
    for (const auto &r : rows) out.push_back(r.size() == 16 ? r : r + std::string(r.rbegin(), r.rend()));
    return out;
}

bool valid(const std::vector<std::string> &rows) {
    if (rows.size() != 16) return false;
    for (const auto &r : rows)
        if (r.size() != 8 && r.size() != 16) return false;
    auto f = full(rows);
    for (const auto &r : f)
        for (char c : r)
            if (c != '.' && c != 'd' && c != 'n' && c != 'h') return false;
    return f[0] == std::string(16, '.');
}

struct Mascot {
    std::string key, name;
    std::vector<std::string> rows;
};

static const std::vector<Mascot> &all() {
    static const std::vector<Mascot> M = [] {
        std::vector<Mascot> out;
        std::string raw;
        if (!resources::get("data/mascots.json", raw)) return out;
        try {
            json::Value d = json::parse(raw);
            for (const auto &m : d.obj()) {
                Mascot x;
                x.key = m.first;
                x.name = m.second.s("name");
                for (const auto &r : m.second.get("rows").arr()) {
                    if (!r.is_string()) { x.rows.clear(); break; }
                    x.rows.push_back(r.str());
                }
                if (valid(x.rows)) out.push_back(x);   // a bad edit to the file drops one, not the app
            }
        } catch (json::ParseError &) {
        }
        return out;
    }();
    return M;
}

const std::vector<std::pair<std::string, std::string>> &mascots() {
    static const std::vector<std::pair<std::string, std::string>> L = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto &m : all()) out.push_back({m.key, m.name});
        return out;
    }();
    return L;
}

bool has_mascot(const std::string &key) {
    for (const auto &m : all()) if (m.key == key) return true;
    return false;
}

std::vector<std::string> sprite(const std::string &key) {
    for (const auto &m : all()) if (m.key == key) return full(m.rows);
    return logo_sprite();
}

std::string name(const std::string &key) {
    for (const auto &m : all()) if (m.key == key) return m.name;
    return "";
}

std::vector<std::string> hop(const std::vector<std::string> &rows) {
    if (rows.empty()) return rows;
    std::vector<std::string> out(rows.begin() + 1, rows.end());
    out.push_back(std::string(rows[0].size(), '.'));
    return out;
}

}  // namespace art

// ----------------------------------------------------------------------------- chibis

namespace chibi {

static bool chibi_valid(const json::Value &m) {
    if (!m.is_object() || !m.get("head").is_array() || m.get("head").size() != 15) return false;
    std::vector<const json::Value *> rows;
    for (const auto &r : m.get("head").arr()) rows.push_back(&r);
    const json::Value &body = m.get("body");
    if (body.truthy()) {
        if (!body.is_array() || body.size() != 9) return false;
        for (const auto &r : body.arr()) rows.push_back(&r);
    }
    for (const auto *r : rows) {
        if (!r->is_string()) return false;
        size_t n = text::length(r->str());
        if (n != 12 && n != (size_t)SIZE) return false;
        for (char c : r->str())
            if (std::string(".kwrpsabcd").find(c) == std::string::npos) return false;
    }
    return true;
}

static const json::Value &data() {
    static const json::Value D = [] {
        json::Value d = json::Value::object();
        std::string raw;
        if (!resources::get("data/chibis.json", raw)) return d;
        try {
            d = json::parse(raw);
        } catch (json::ParseError &) {
            return json::Value::object();
        }
        json::Value keep = json::Value::object();
        const json::Value &ms = d.get("mascots");
        for (const auto &m : ms.obj())
            if (chibi_valid(m.second)) keep[m.first] = m.second;   // a bad edit to the file drops one mascot, not the app
        d["mascots"] = keep;
        return d;
    }();
    return D;
}

std::vector<std::string> keys() {
    std::vector<std::string> out;
    const json::Value &ms = data().get("mascots");
    for (const auto &m : ms.obj()) out.push_back(m.first);
    return out;
}

static std::vector<std::string> full(const json::Value &rows) {
    std::vector<std::string> out;
    for (const auto &r : rows.arr()) {
        std::string s = r.str();
        if ((int)s.size() != SIZE) s += std::string(s.rbegin(), s.rend());
        out.push_back(s);
    }
    return out;
}

std::vector<std::string> frame(const std::string &key, bool cheer) {
    const json::Value &d = data();
    const json::Value *m = d.get("mascots").find(key);
    if (!m || !m->truthy()) return {};
    std::vector<std::string> head = full(m->get("head")), body;
    if (cheer && !m->has("body")) {
        const json::Value &ch = d.get("cheer");
        body = full(ch.get("body"));
        auto raise = full(ch.get("raise"));
        for (size_t i = 0; i < raise.size() && 12 + i < head.size(); i++)
            for (size_t x = 0; x < raise[i].size() && x < head[12 + i].size(); x++)
                if (raise[i][x] != '.' && head[12 + i][x] == '.') head[12 + i][x] = raise[i][x];
    } else {
        body = full(m->get("body").truthy() ? m->get("body") : d.get("body"));
    }
    head.insert(head.end(), body.begin(), body.end());
    return head;
}

static double light(const std::string &hex) {
    auto v = [&](size_t i) { return std::stoi(hex.substr(i, 2), nullptr, 16) / 255.0; };
    return 0.299 * v(1) + 0.587 * v(3) + 0.114 * v(5);
}

std::vector<std::pair<char, std::string>> colors(const std::string &key, const std::string &primary, const std::string &secondary) {
    const json::Value &d = data();
    std::vector<std::pair<char, std::string>> out;
    auto put = [&](char c, const std::string &v) {
        for (auto &p : out)
            if (p.first == c) { p.second = v; return; }
        out.push_back({c, v});
    };
    auto add = [&](const json::Value &o) {
        for (const auto &m : o.obj())
            if (!m.first.empty() && m.second.is_string()) put(m.first[0], m.second.str());
    };
    add(d.get("fixed"));
    add(d.get("jersey"));
    if (const json::Value *m = d.get("mascots").find(key)) add(m->get("colors"));
    std::string p = store::parse_hex(primary), s = store::parse_hex(secondary);
    if (!p.empty()) put('p', p);
    if (!s.empty()) put('s', s);
    if (!p.empty() && s.empty()) put('s', light(p) < 0.5 ? "#ffffff" : "#1b1b1f");
    return out;
}

}  // namespace chibi
