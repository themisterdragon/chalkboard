#include "markup.h"

#include <algorithm>

#include "text.h"

namespace markup {

using U32 = std::u32string;

namespace {

bool is_s(char32_t c) { return text::isspace(c); }
bool is_w(char32_t c) { return text::isword(c); }

struct Match {
    size_t start = 0, end = 0;       // the whole match, marks included
    size_t gstart = 0, gend = 0;     // the text inside
    char kind = 0;                   // 'b', 'u', 'i'
};

// One alternative of the pattern at position i. The inner text is as short as it can be (.+?),
// can't hold a line break, and must start and end the way the alternative says.
bool try_kind(const U32 &s, size_t i, char kind, Match &m, size_t must_end = U32::npos) {
    size_t n = s.size();
    size_t ml = kind == 'i' ? 1 : 2;
    char32_t mc = kind == 'u' ? '_' : '*';
    if (i + ml > n) return false;
    for (size_t k = 0; k < ml; k++) if (s[i + k] != mc) return false;
    // what may come before the opening mark
    if (kind == 'u' && i > 0 && (is_w(s[i - 1]) || s[i - 1] == '_')) return false;
    if (kind == 'i' && i > 0 && (is_w(s[i - 1]) || s[i - 1] == '*')) return false;
    size_t g = i + ml;
    if (g >= n) return false;
    // the first character inside
    char32_t f = s[g];
    if (is_s(f)) return false;
    if (kind == 'u' && f == '_') return false;
    if (kind == 'i' && f == '*') return false;
    for (size_t j = g + 1; j + ml <= n; j++) {
        if (s[j - 1] == '\n') return false;   // . never matches a line break
        if (must_end != U32::npos && j + ml != must_end) continue;
        bool close = true;
        for (size_t k = 0; k < ml; k++) if (s[j + k] != mc) { close = false; break; }
        if (!close) continue;
        char32_t last = s[j - 1];
        if (is_s(last)) continue;
        if (kind == 'u' && last == '_') continue;
        if (kind == 'i' && last == '*') continue;
        size_t after = j + ml;
        if (after < n) {
            char32_t a = s[after];
            if (kind == 'b' && a == '*') continue;
            if (kind == 'u' && (is_w(a) || a == '_')) continue;
            if (kind == 'i' && (is_w(a) || a == '*')) continue;
        }
        m.start = i;
        m.end = after;
        m.gstart = g;
        m.gend = j;
        m.kind = kind;
        return true;
    }
    return false;
}

bool match_at(const U32 &s, size_t i, Match &m) {
    return try_kind(s, i, 'b', m) || try_kind(s, i, 'u', m) || try_kind(s, i, 'i', m);
}

std::vector<Match> finditer(const U32 &s) {
    std::vector<Match> out;
    size_t i = 0;
    while (i < s.size()) {
        Match m;
        if (match_at(s, i, m)) {
            out.push_back(m);
            i = m.end;
        } else i++;
    }
    return out;
}

void runs32(const U32 &t, bool b, bool it, bool u, std::vector<std::pair<U32, int>> &out) {
    size_t at = 0;
    for (const Match &m : finditer(t)) {
        if (m.start > at) out.push_back({t.substr(at, m.start - at), (b ? B : 0) | (it ? I : 0) | (u ? U : 0)});
        runs32(t.substr(m.gstart, m.gend - m.gstart), b || m.kind == 'b', it || m.kind == 'i', u || m.kind == 'u', out);
        at = m.end;
    }
    if (at < t.size()) out.push_back({t.substr(at), (b ? B : 0) | (it ? I : 0) | (u ? U : 0)});
}

const char *mark_of(int bit) { return bit == B ? "**" : bit == I ? "*" : "__"; }

}  // namespace

std::vector<Run> runs(const std::string &t, bool bold, bool italic, bool under) {
    std::vector<std::pair<U32, int>> raw;
    runs32(text::to32(t), bold, italic, under, raw);
    std::vector<Run> out;
    for (auto &r : raw) out.push_back({text::to8(r.first), (r.second & B) != 0, (r.second & I) != 0, (r.second & U) != 0});
    return out;
}

std::string plain(const std::string &t) {
    std::string out;
    for (const auto &r : runs(t)) out += r.text;
    return out;
}

static void walk(const U32 &s, long offset, int flags, std::vector<Span> &out) {
    for (const Match &m : finditer(s)) {
        int bit = m.kind == 'b' ? B : m.kind == 'i' ? I : U;
        long a = offset + (long)m.gstart, b = offset + (long)m.gend;
        out.push_back({offset + (long)m.start, a, true, 0});
        out.push_back({b, offset + (long)m.end, true, 0});
        out.push_back({a, b, false, flags | bit});
        walk(s.substr(m.gstart, m.gend - m.gstart), a, flags | bit, out);
    }
}

std::vector<Span> spans(const std::string &t) {
    std::vector<Span> out;
    walk(text::to32(t), 0, 0, out);
    return out;
}

std::pair<std::string, long> toggle(const std::string &buf8, long pos, char kind) {
    U32 buf = text::to32(buf8);
    U32 mark = kind == 'b' ? U"**" : kind == 'i' ? U"*" : U"__";
    long n = (long)buf.size();
    pos = std::max(0L, std::min(pos, n));
    long a = pos, b = pos;
    while (a > 0 && !is_s(buf[(size_t)a - 1])) a--;
    while (b < n && !is_s(buf[(size_t)b])) b++;
    U32 word = buf.substr((size_t)a, (size_t)(b - a));
    if (word.empty()) {
        U32 out = buf.substr(0, (size_t)pos) + mark + mark + buf.substr((size_t)pos);
        return {text::to8(out), pos + (long)mark.size()};
    }
    static const U32 PUNCT = U".,;:!?\"')(";
    size_t x = 0, y = word.size();
    while (x < y && PUNCT.find(word[x]) != U32::npos) x++;
    while (y > x && PUNCT.find(word[y - 1]) != U32::npos) y--;
    U32 core = word.substr(x, y - x);
    // word.index(core): where the core first appears in the word
    long lead = core.empty() ? 0 : (long)word.find(core);
    long ca = a + lead, cb = a + lead + (long)core.size();
    Match m;
    char kinds[3] = {'b', 'u', 'i'};
    char found = 0;
    for (char k : kinds)
        if (try_kind(core, 0, k, m, core.size()) && m.end == core.size()) { found = k; break; }
    if (found && found == kind) {
        U32 inner = core.substr(m.gstart, m.gend - m.gstart);
        U32 out = buf.substr(0, (size_t)ca) + inner + buf.substr((size_t)cb);
        long cur = std::max(ca, std::min(pos - (long)mark.size(), ca + (long)inner.size()));
        return {text::to8(out), cur};
    }
    U32 out = buf.substr(0, (size_t)ca) + mark + core + mark + buf.substr((size_t)cb);
    return {text::to8(out), cb + 2 * (long)mark.size()};
}

// ----------------------------------------------------------------------------- bits <-> marks

static std::pair<U32, std::vector<int>> flags32(const U32 &t) {
    std::vector<std::pair<U32, int>> raw;
    runs32(t, false, false, false, raw);
    U32 out;
    std::vector<int> fl;
    for (auto &r : raw) {
        out += r.first;
        fl.insert(fl.end(), r.first.size(), r.second);
    }
    return {out, fl};
}

std::pair<std::string, std::vector<int>> flags_of(const std::string &t) {
    auto r = flags32(text::to32(t));
    return {text::to8(r.first), r.second};
}

static const int ORDERS[6][3] = {{U, B, I}, {B, U, I}, {U, I, B}, {I, U, B}, {B, I, U}, {I, B, U}};

static U32 nest(const U32 &line, const std::vector<int> &fl, const int *order) {
    U32 out;
    std::vector<int> stack;
    for (size_t k = 0; k < line.size(); k++) {
        while (!stack.empty() && std::any_of(stack.begin(), stack.end(), [&](int s) { return !(fl[k] & s); })) {
            out += text::to32(mark_of(stack.back()));
            stack.pop_back();
        }
        for (int t = 0; t < 3; t++) {
            int bit = order[t];
            if ((fl[k] & bit) && std::find(stack.begin(), stack.end(), bit) == stack.end()) {
                stack.push_back(bit);
                out += text::to32(mark_of(bit));
            }
        }
        out += line[k];
    }
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) out += text::to32(mark_of(*it));
    return out;
}

static U32 each(const U32 &line, const std::vector<int> &fl, const int *order) {
    U32 out;
    size_t k = 0;
    while (k < line.size()) {
        size_t j = k;
        while (j < line.size() && fl[j] == fl[k]) j++;
        std::vector<int> bits;
        for (int t = 0; t < 3; t++) if (fl[k] & order[t]) bits.push_back(order[t]);
        for (int b : bits) out += text::to32(mark_of(b));
        out += line.substr(k, j - k);
        for (auto it = bits.rbegin(); it != bits.rend(); ++it) out += text::to32(mark_of(*it));
        k = j;
    }
    return out;
}

// how far reading s back strays from (line, fl): -1 when it changes more than typed marks
static long aligned(const U32 &line, const std::vector<int> &fl, const U32 &s) {
    auto r = flags32(s);
    const U32 &pl = r.first;
    const std::vector<int> &got = r.second;
    long miss = 0;
    size_t k = 0;
    for (size_t i = 0; i < pl.size(); i++) {
        char32_t ch = pl[i];
        while (k < line.size() && line[k] != ch && (line[k] == '*' || line[k] == '_')) k++;
        if (k >= line.size() || line[k] != ch) return -1;
        miss += got[i] != fl[k];
        k++;
    }
    for (size_t j = k; j < line.size(); j++)
        if (line[j] != '*' && line[j] != '_') return -1;
    return miss + (long)(line.size() - pl.size());
}

static U32 one_line(const U32 &line, const std::vector<int> &fl) {
    if (std::none_of(fl.begin(), fl.end(), [](int f) { return f != 0; })) return line;
    U32 best = line;
    long least = aligned(line, fl, line);
    // last resort: words with a typed * or _ in them (and the spaces around them) stay plain
    std::vector<int> clean = fl;
    size_t i = 0;
    while (i < line.size()) {
        size_t a = i;
        while (a < line.size() && is_s(line[a])) a++;
        size_t b = a;
        bool has = false;
        while (b < line.size() && !is_s(line[b])) {
            if (line[b] == '*' || line[b] == '_') has = true;
            b++;
        }
        if (has) {
            size_t c = b;
            while (c < line.size() && is_s(line[c])) c++;
            for (size_t k = i; k < c; k++) clean[k] = 0;
            i = c;
        } else i++;
    }
    std::vector<int> bare(fl.size()), clean_bare(fl.size());
    for (size_t k = 0; k < line.size(); k++) {
        bare[k] = is_s(line[k]) ? 0 : fl[k];
        clean_bare[k] = is_s(line[k]) ? 0 : clean[k];
    }
    std::vector<std::vector<int>> tries;
    const std::vector<int> *cands[4] = {&fl, &bare, &clean, &clean_bare};
    for (const auto *bits : cands)
        if (std::find(tries.begin(), tries.end(), *bits) == tries.end()) tries.push_back(*bits);
    for (const auto &bits : tries)
        for (int make = 0; make < 2; make++)
            for (const auto &order : ORDERS) {
                U32 s = make == 0 ? nest(line, bits, order) : each(line, bits, order);
                long miss = aligned(line, fl, s);
                if (miss == 0) return s;
                if (miss >= 0 && (least < 0 || miss < least)) {
                    best = s;
                    least = miss;
                }
            }
    return best;
}

static U32 marked32(const U32 &t, const std::vector<int> &fl) {
    U32 out;
    size_t at = 0, start = 0;
    for (size_t k = 0; k <= t.size(); k++) {
        if (k == t.size() || t[k] == '\n') {
            U32 line = t.substr(start, k - start);
            std::vector<int> f;
            for (size_t j = at; j < at + line.size(); j++) f.push_back(j < fl.size() ? fl[j] : 0);
            if (start) out += U'\n';
            out += one_line(line, f);
            at += line.size() + 1;
            start = k + 1;
        }
    }
    return out;
}

std::string marked(const std::string &t, const std::vector<int> &fl) { return text::to8(marked32(text::to32(t), fl)); }

static std::pair<U32, std::vector<int>> tidy_once(U32 t, std::vector<int> fl) {
    fl.resize(t.size(), 0);
    for (size_t k = 0; k < t.size(); k++) if (t[k] == '\n') fl[k] = 0;
    size_t n = t.size();
    auto spans_of = [&](int bit) {
        std::vector<std::pair<size_t, size_t>> out;
        size_t k = 0;
        while (k < n) {
            if (fl[k] & bit) {
                size_t j = k;
                while (j < n && (fl[j] & bit)) j++;
                out.push_back({k, j});
                k = j;
            } else k++;
        }
        return out;
    };
    for (int bit : {B, I})   // a space between two bold (or italic) words is bold too
        for (auto ab : spans_of(bit)) {
            size_t b = ab.second, k = b;
            while (k < n && is_s(t[k]) && t[k] != '\n') k++;
            if (k > b && k < n && (fl[k] & bit))
                for (size_t x = b; x < k; x++) fl[x] |= bit;
        }
    for (int bit : {B, I, U})   // no mark starts or ends on a space
        for (auto ab : spans_of(bit)) {
            size_t a = ab.first, b = ab.second, k = a;
            while (k < b && is_s(t[k])) { fl[k] &= ~bit; k++; }
            long kk = (long)b - 1;
            while (kk >= (long)a && is_s(t[(size_t)kk])) { fl[(size_t)kk] &= ~bit; kk--; }
        }
    for (int bit : {I, U})   // italic and underline cover whole words
        for (auto ab : spans_of(bit)) {
            size_t a = ab.first, b = ab.second;
            while (a > 0 && is_w(t[a - 1])) a--;
            while (b < n && is_w(t[b])) b++;
            for (size_t k = a; k < b; k++) fl[k] |= bit;
        }
    for (int round = 0; round < 8; round++) {   // settle: reading back what was written changes nothing
        auto got = flags32(marked32(t, fl));
        if (got.first == t && got.second == fl) break;
        t = got.first;
        fl = got.second;
    }
    return {t, fl};
}

std::pair<std::string, std::vector<int>> tidy(const std::string &t8, const std::vector<int> &fl0) {
    U32 t = text::to32(t8);
    std::vector<int> fl = fl0;
    for (int round = 0; round < 4; round++) {
        auto got = tidy_once(t, fl);
        if (got.first == t && got.second == fl) break;
        t = got.first;
        fl = got.second;
    }
    return {text::to8(t), fl};
}

// ----------------------------------------------------------------------------- links

std::vector<std::pair<std::string, std::string>> links(const std::string &t8) {
    U32 t = text::to32(t8);
    std::vector<std::pair<std::string, std::string>> out;
    size_t at = 0, i = 0, n = t.size();
    auto starts_ci = [&](size_t k, const char *w) {
        for (size_t j = 0; w[j]; j++)
            if (k + j >= n || text::lower(t[k + j]) != (char32_t)(unsigned char)w[j]) return false;
        return true;
    };
    auto url_char = [](char32_t c) { return !text::isspace(c) && c != '<' && c != '>' && c != '"'; };
    while (i < n) {
        size_t len = 0;
        if (starts_ci(i, "https://")) len = 8;
        else if (starts_ci(i, "http://")) len = 7;
        else if (starts_ci(i, "www.")) len = 4;
        if (!len || i + len >= n || !url_char(t[i + len])) {
            i++;
            continue;
        }
        size_t e = i + len;
        while (e < n && url_char(t[e])) e++;
        U32 whole = t.substr(i, e - i);
        static const U32 TRAIL = U".,;:!?'\")]";
        size_t ue = whole.size();
        while (ue > 0 && TRAIL.find(whole[ue - 1]) != U32::npos) ue--;
        U32 url = whole.substr(0, ue);
        long opens = std::count(url.begin(), url.end(), U'('), closes = std::count(url.begin(), url.end(), U')');
        if (opens > closes && ue < whole.size() && whole[ue] == ')') url += U')';   // a Wikipedia-style link keeps its )
        if (i > at) out.push_back({text::to8(t.substr(at, i - at)), ""});
        std::string u8 = text::to8(url);
        out.push_back({u8, text::starts_with(text::lower(u8), "http") ? u8 : "https://" + u8});
        at = i + url.size();
        i = e;
    }
    if (at < n) out.push_back({text::to8(t.substr(at)), ""});
    return out;
}

}  // namespace markup
