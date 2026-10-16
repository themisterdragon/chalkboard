#include "textwrap.h"

#include <algorithm>

#include "text.h"

namespace textwrap {

using U32 = std::u32string;

namespace {

// textwrap's own idea of whitespace: just these six characters
bool ws(char32_t c) { return c == '\t' || c == '\n' || c == 0x0B || c == 0x0C || c == '\r' || c == ' '; }
bool word_punct(char32_t c) { return text::isword(c) || c == '!' || c == '"' || c == '\'' || c == '&' || c == '.' || c == ',' || c == '?'; }
bool letter(char32_t c) { return text::isword(c) && !text::isdigit(c); }   // [^\d\W]: word characters but digits (_ counts)

U32 expandtabs(const U32 &s) {
    U32 out;
    size_t col = 0;
    for (char32_t c : s) {
        if (c == '\t') {
            size_t n = 8 - col % 8;
            out.append(n, ' ');
            col += n;
        } else {
            out += c;
            col = (c == '\n' || c == '\r') ? 0 : col + 1;
        }
    }
    return out;
}

// the chunk starting at i (wordsep_re with break_on_hyphens): its length
size_t chunk_at(const U32 &s, size_t i) {
    size_t n = s.size();
    if (ws(s[i])) {
        size_t j = i;
        while (j < n && ws(s[j])) j++;
        return j - i;
    }
    // em-dash between words: (?<=word_punct) -{2,} (?=\w)
    if (s[i] == '-' && i > 0 && word_punct(s[i - 1])) {
        size_t j = i;
        while (j < n && s[j] == '-') j++;
        if (j - i >= 2 && j < n && text::isword(s[j])) return j - i;
    }
    // a word: \S+? then a hyphen break, the end of the word, or an em-dash
    for (size_t k = i + 1; k <= n; k++) {
        if (ws(s[k - 1])) break;   // \S+? can't take whitespace
        size_t p = k;              // position after the \S+? part
        // hyphenated: '-' then (?<=lt lt -) or (?<=lt - lt -), then (?= lt -? lt)
        if (p < n && s[p] == '-') {
            size_t q = p + 1;   // after the hyphen
            bool behind = (q >= 3 && letter(s[q - 3]) && letter(s[q - 2])) ||
                          (q >= 4 && letter(s[q - 4]) && s[q - 3] == '-' && letter(s[q - 2]));
            bool ahead = false;
            if (q < n && letter(s[q])) {
                if (q + 1 < n && letter(s[q + 1])) ahead = true;
                else if (q + 2 < n && s[q + 1] == '-' && letter(s[q + 2])) ahead = true;
            }
            if (behind && ahead) return q - i;
        }
        // end of word
        if (p == n || ws(s[p])) return p - i;
        // em-dash: (?<=word_punct) (?=-{2,}\w)
        if (word_punct(s[p - 1]) && p + 1 < n && s[p] == '-' && s[p + 1] == '-') {
            size_t j = p;
            while (j < n && s[j] == '-') j++;
            if (j < n && text::isword(s[j])) return p - i;
        }
    }
    return 1;
}

bool blank(const U32 &c) {
    for (char32_t ch : c)
        if (!text::isspace(ch)) return false;
    return true;
}

}  // namespace

std::vector<std::string> wrap(const std::string &text8, long width, const std::string &initial_indent, const std::string &subsequent_indent) {
    U32 s = expandtabs(text::to32(text8));
    for (auto &c : s) if (ws(c)) c = ' ';
    std::vector<U32> chunks;
    for (size_t i = 0; i < s.size();) {
        size_t len = chunk_at(s, i);
        chunks.push_back(s.substr(i, len));
        i += len;
    }
    std::reverse(chunks.begin(), chunks.end());
    std::vector<std::string> lines;
    U32 ind0 = text::to32(initial_indent), ind1 = text::to32(subsequent_indent);
    while (!chunks.empty()) {
        std::vector<U32> cur;
        long cur_len = 0;
        const U32 &indent = lines.empty() ? ind0 : ind1;
        long w = width - (long)indent.size();
        if (blank(chunks.back()) && !lines.empty()) chunks.pop_back();
        while (!chunks.empty()) {
            long l = (long)chunks.back().size();
            if (cur_len + l <= w) {
                cur.push_back(chunks.back());
                chunks.pop_back();
                cur_len += l;
            } else break;
        }
        if (!chunks.empty() && (long)chunks.back().size() > w) {
            // a word too long for any line: break it, after a hyphen if there's one
            long space_left = w < 1 ? 1 : w - cur_len;
            U32 &chunk = chunks.back();
            if (space_left > 0) {
                long end = space_left;
                if ((long)chunk.size() > space_left) {
                    long hyphen = -1;
                    for (long k = std::min(space_left, (long)chunk.size()) - 1; k >= 0; k--)
                        if (chunk[(size_t)k] == '-') { hyphen = k; break; }
                    if (hyphen > 0) {
                        bool other = false;
                        for (long k = 0; k < hyphen; k++) if (chunk[(size_t)k] != '-') other = true;
                        if (other) end = hyphen + 1;
                    }
                }
                end = std::min(end, (long)chunk.size());
                cur.push_back(chunk.substr(0, (size_t)end));
                chunk = chunk.substr((size_t)end);
                if (chunk.empty()) chunks.pop_back();   // (Python loops forever here when the indent fills the line)
            } else if (cur.empty()) {
                cur.push_back(chunk);
                chunks.pop_back();
            }
            cur_len = 0;
            for (const auto &c : cur) cur_len += (long)c.size();
        }
        if (!cur.empty() && blank(cur.back())) {
            cur_len -= (long)cur.back().size();
            cur.pop_back();
        }
        if (!cur.empty()) {
            U32 line = indent;
            for (const auto &c : cur) line += c;
            lines.push_back(text::to8(line));
        }
    }
    return lines;
}

}  // namespace textwrap
