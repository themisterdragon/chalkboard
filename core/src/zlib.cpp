#include "zlib.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace zlib {

// ============================================================================= checksums

uint32_t crc32(const std::string &data, uint32_t crc) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(const std::string &data, uint32_t adler) {
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    size_t i = 0, n = data.size();
    while (i < n) {
        size_t chunk = std::min(n - i, (size_t)5552);
        for (size_t k = 0; k < chunk; k++) {
            a += (unsigned char)data[i + k];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        i += chunk;
    }
    return (b << 16) | a;
}

// ============================================================================= tables

static const int LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const int CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// ============================================================================= inflate

namespace {

struct Huff {
    std::vector<short> count, symbol;
};

struct Inflater {
    const unsigned char *in;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    std::string out;
    size_t limit;
    Inflater(const unsigned char *data, size_t size, size_t most) : in(data), n(size), limit(most) {}

    [[noreturn]] void fail(const char *m) { throw Error{m}; }

    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= n) fail("incomplete or truncated stream");
            val |= (uint32_t)in[pos++] << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return (int)(val & ((1U << need) - 1));
    }

    int decode(const Huff &h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= bits(1);
            int count = h.count[(size_t)len];
            if (code - count < first) return h.symbol[(size_t)(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        fail("invalid code");
    }

    // returns how many codes are left over (0 = complete, < 0 = oversubscribed)
    static int construct(Huff &h, const short *length, int n) {
        h.count.assign(16, 0);
        h.symbol.assign((size_t)n, 0);
        for (int s = 0; s < n; s++) h.count[(size_t)length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; len++) {
            left <<= 1;
            left -= h.count[(size_t)len];
            if (left < 0) return left;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h.count[(size_t)len]);
        for (int s = 0; s < n; s++)
            if (length[s] != 0) h.symbol[(size_t)offs[length[s]]++] = (short)s;
        return left;
    }

    void put(char c) {
        if (out.size() >= limit) fail("the data is too big once unpacked");
        out += c;
    }

    void codes(const Huff &lencode, const Huff &distcode) {
        for (;;) {
            int sym = decode(lencode);
            if (sym < 256) { put((char)sym); continue; }
            if (sym == 256) return;
            sym -= 257;
            if (sym >= 29) fail("invalid literal/length code");
            int len = LEN_BASE[sym] + bits(LEN_EXTRA[sym]);
            int dsym = decode(distcode);
            if (dsym >= 30) fail("invalid distance code");
            size_t dist = (size_t)(DIST_BASE[dsym] + bits(DIST_EXTRA[dsym]));
            if (dist > out.size()) fail("invalid distance too far back");
            if (out.size() + (size_t)len > limit) fail("the data is too big once unpacked");
            size_t from = out.size() - dist;
            for (int k = 0; k < len; k++) out += out[from + (size_t)k];
        }
    }

    void stored() {
        bitbuf = 0;
        bitcnt = 0;
        if (pos + 4 > n) fail("incomplete or truncated stream");
        unsigned len = in[pos] | (in[pos + 1] << 8);
        unsigned nlen = in[pos + 2] | (in[pos + 3] << 8);
        pos += 4;
        if (len != (~nlen & 0xFFFF)) fail("invalid stored block lengths");
        if (pos + len > n) fail("incomplete or truncated stream");
        if (out.size() + len > limit) fail("the data is too big once unpacked");
        out.append((const char *)in + pos, len);
        pos += len;
    }

    void fixed() {
        static Huff lencode, distcode;
        static bool ready = false;
        if (!ready) {
            short lengths[288];
            int s = 0;
            for (; s < 144; s++) lengths[s] = 8;
            for (; s < 256; s++) lengths[s] = 9;
            for (; s < 280; s++) lengths[s] = 7;
            for (; s < 288; s++) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; s++) lengths[s] = 5;
            construct(distcode, lengths, 30);
            ready = true;
        }
        codes(lencode, distcode);
    }

    void dynamic() {
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) fail("too many length or distance symbols");
        short lengths[320] = {0};
        for (int i = 0; i < ncode; i++) lengths[CL_ORDER[i]] = (short)bits(3);
        Huff lencode, distcode;
        if (construct(lencode, lengths, 19) != 0) fail("invalid code lengths set");
        int index = 0;
        std::memset(lengths, 0, sizeof lengths);
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 16) { lengths[index++] = (short)sym; continue; }
            short len = 0;
            int rep;
            if (sym == 16) {
                if (index == 0) fail("invalid bit length repeat");
                len = lengths[index - 1];
                rep = 3 + bits(2);
            } else if (sym == 17) rep = 3 + bits(3);
            else rep = 11 + bits(7);
            if (index + rep > nlen + ndist) fail("invalid bit length repeat");
            while (rep--) lengths[index++] = len;
        }
        if (lengths[256] == 0) fail("invalid code -- missing end-of-block");
        int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) fail("invalid literal/lengths set");
        err = construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) fail("invalid distances set");
        codes(lencode, distcode);
    }

    void run() {
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else fail("invalid block type");
        } while (!last);
    }
};

}  // namespace

std::string inflate_raw(const std::string &data, size_t limit) {
    Inflater f((const unsigned char *)data.data(), data.size(), limit);
    f.run();
    return std::move(f.out);
}

std::string decompress(const std::string &data, size_t limit) {
    if (data.size() < 6) throw Error{"incomplete or truncated stream"};
    unsigned char cmf = (unsigned char)data[0], flg = (unsigned char)data[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0) throw Error{"incorrect header check"};
    if (flg & 0x20) throw Error{"need dictionary"};
    Inflater f((const unsigned char *)data.data(), data.size(), limit);
    f.pos = 2;
    f.run();
    if (f.pos + 4 > data.size()) throw Error{"incomplete or truncated stream"};
    uint32_t want = 0;
    for (int i = 0; i < 4; i++) want = (want << 8) | (unsigned char)data[f.pos + (size_t)i];
    if (want != adler32(f.out)) throw Error{"incorrect data check"};
    return std::move(f.out);
}

// ============================================================================= deflate

namespace {

struct BitWriter {
    std::string out;
    uint64_t buf = 0;
    int cnt = 0;
    void put(uint32_t value, int nbits) {
        buf |= (uint64_t)value << cnt;
        cnt += nbits;
        while (cnt >= 8) {
            out += (char)(buf & 0xFF);
            buf >>= 8;
            cnt -= 8;
        }
    }
    void align() {
        if (cnt > 0) put(0, 8 - cnt);
    }
};

uint32_t reverse_bits(uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; }
    return r;
}

// Length-limited Huffman code lengths (package-merge).
std::vector<int> code_lengths(const std::vector<uint32_t> &freq, int maxbits) {
    size_t n = freq.size();
    std::vector<int> len(n, 0);
    std::vector<int> used;
    for (size_t i = 0; i < n; i++) if (freq[i]) used.push_back((int)i);
    if (used.empty()) return len;
    if (used.size() == 1) { len[(size_t)used[0]] = 1; return len; }
    struct Item { uint64_t w; std::vector<int> syms; };
    std::vector<Item> leaves;
    for (int s : used) leaves.push_back({freq[(size_t)s], {s}});
    std::stable_sort(leaves.begin(), leaves.end(), [](const Item &a, const Item &b) { return a.w < b.w; });
    std::vector<Item> cur = leaves;
    for (int level = 1; level < maxbits; level++) {
        std::vector<Item> packages;
        for (size_t i = 0; i + 1 < cur.size(); i += 2) {
            Item p{cur[i].w + cur[i + 1].w, cur[i].syms};
            p.syms.insert(p.syms.end(), cur[i + 1].syms.begin(), cur[i + 1].syms.end());
            packages.push_back(std::move(p));
        }
        std::vector<Item> merged;
        merged.reserve(leaves.size() + packages.size());
        size_t a = 0, b = 0;
        while (a < leaves.size() || b < packages.size()) {
            if (b >= packages.size() || (a < leaves.size() && leaves[a].w <= packages[b].w)) merged.push_back(leaves[a++]);
            else merged.push_back(std::move(packages[b++]));
        }
        cur = std::move(merged);
    }
    size_t take = 2 * used.size() - 2;
    for (size_t i = 0; i < take && i < cur.size(); i++)
        for (int s : cur[i].syms) len[(size_t)s]++;
    return len;
}

std::vector<uint32_t> canonical(const std::vector<int> &len) {
    int maxlen = 0;
    for (int l : len) maxlen = std::max(maxlen, l);
    std::vector<int> bl_count((size_t)maxlen + 1, 0);
    for (int l : len) if (l) bl_count[(size_t)l]++;
    std::vector<uint32_t> next((size_t)maxlen + 2, 0);
    uint32_t code = 0;
    for (int bits = 1; bits <= maxlen; bits++) {
        code = (code + (uint32_t)bl_count[(size_t)bits - 1]) << 1;
        next[(size_t)bits] = code;
    }
    std::vector<uint32_t> codes(len.size(), 0);
    for (size_t s = 0; s < len.size(); s++)
        if (len[s]) codes[s] = reverse_bits(next[(size_t)len[s]]++, len[s]);
    return codes;
}

struct Sym {
    uint16_t lit;     // 0-255 literal, 256 end, or a match length (3-258) when dist > 0
    uint16_t dist;
};

int len_code(int len) {
    for (int i = 28; i >= 0; i--) if (len >= LEN_BASE[i]) return i;
    return 0;
}

int dist_code(int dist) {
    for (int i = 29; i >= 0; i--) if (dist >= DIST_BASE[i]) return i;
    return 0;
}

void write_symbols(BitWriter &bw, const std::vector<Sym> &syms, const std::vector<uint32_t> &lc, const std::vector<int> &ll,
                   const std::vector<uint32_t> &dc, const std::vector<int> &dl) {
    for (const Sym &s : syms) {
        if (s.dist == 0) {
            bw.put(lc[s.lit], ll[s.lit]);
            continue;
        }
        int c = len_code(s.lit);
        bw.put(lc[(size_t)(257 + c)], ll[(size_t)(257 + c)]);
        if (LEN_EXTRA[c]) bw.put((uint32_t)(s.lit - LEN_BASE[c]), LEN_EXTRA[c]);
        int d = dist_code(s.dist);
        bw.put(dc[(size_t)d], dl[(size_t)d]);
        if (DIST_EXTRA[d]) bw.put((uint32_t)(s.dist - DIST_BASE[d]), DIST_EXTRA[d]);
    }
}

// the code-length alphabet with run-length codes 16/17/18
void rle_lengths(const std::vector<int> &lens, std::vector<std::pair<int, int>> &out) {
    size_t i = 0, n = lens.size();
    while (i < n) {
        int l = lens[i];
        size_t run = 1;
        while (i + run < n && lens[i + run] == l) run++;
        if (l == 0 && run >= 3) {
            size_t r = run;
            while (r >= 11) { size_t k = std::min(r, (size_t)138); out.push_back({18, (int)k - 11}); r -= k; }
            if (r >= 3) { out.push_back({17, (int)r - 3}); r = 0; }
            while (r--) out.push_back({0, 0});
            i += run;
        } else if (l != 0 && run >= 4) {
            out.push_back({l, 0});
            size_t r = run - 1;
            while (r >= 3) { size_t k = std::min(r, (size_t)6); out.push_back({16, (int)k - 3}); r -= k; }
            while (r--) out.push_back({l, 0});
            i += run;
        } else {
            out.push_back({l, 0});
            i++;
        }
    }
}

void emit_block(BitWriter &bw, const std::vector<Sym> &syms, const std::string &data, size_t raw_start, size_t raw_end, bool last) {
    std::vector<uint32_t> lf(286, 0), df(30, 0);
    for (const Sym &s : syms) {
        if (s.dist == 0) lf[s.lit]++;
        else { lf[(size_t)(257 + len_code(s.lit))]++; df[(size_t)dist_code(s.dist)]++; }
    }
    lf[256]++;
    // dynamic
    std::vector<int> ll = code_lengths(lf, 15), dl = code_lengths(df, 15);
    int dist_used = 0;
    for (int l : dl) if (l) dist_used++;
    if (dist_used == 0) dl[0] = 1;                  // at least one distance code
    else if (dist_used == 1) { for (size_t k = 0; k < dl.size(); k++) if (!dl[k]) { dl[k] = 1; break; } }
    int nlit = 286, ndist = 30;
    while (nlit > 257 && ll[(size_t)nlit - 1] == 0) nlit--;
    while (ndist > 1 && dl[(size_t)ndist - 1] == 0) ndist--;
    std::vector<int> all(ll.begin(), ll.begin() + nlit);
    all.insert(all.end(), dl.begin(), dl.begin() + ndist);
    std::vector<std::pair<int, int>> rle;
    rle_lengths(all, rle);
    std::vector<uint32_t> cf(19, 0);
    for (auto &p : rle) cf[(size_t)p.first]++;
    std::vector<int> cl = code_lengths(cf, 7);
    int ncl = 19;
    while (ncl > 4 && cl[(size_t)CL_ORDER[ncl - 1]] == 0) ncl--;

    auto body_bits = [&](const std::vector<int> &l_len, const std::vector<int> &d_len) {
        uint64_t bits = 0;
        for (size_t s = 0; s < 286; s++) bits += (uint64_t)lf[s] * (uint64_t)l_len[s];
        for (int c = 0; c < 29; c++) bits += (uint64_t)lf[(size_t)(257 + c)] * (uint64_t)LEN_EXTRA[c];
        for (size_t d = 0; d < 30; d++) bits += (uint64_t)df[d] * (uint64_t)(d_len[d] + DIST_EXTRA[d]);
        return bits;
    };
    uint64_t dyn = 3 + 14 + (uint64_t)ncl * 3 + body_bits(ll, dl);
    for (auto &p : rle) dyn += (uint64_t)cl[(size_t)p.first] + (p.first == 16 ? 2 : p.first == 17 ? 3 : p.first == 18 ? 7 : 0);
    std::vector<int> fl(288), fd(30, 5);
    for (int s = 0; s < 288; s++) fl[(size_t)s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
    uint64_t fix = 3 + body_bits(fl, fd);
    size_t raw_len = raw_end - raw_start;
    uint64_t stored_bits = 3 + 7 + 32 + (uint64_t)raw_len * 8 + (raw_len / 65535) * 40;

    if (stored_bits <= fix && stored_bits <= dyn) {
        size_t at = raw_start;
        do {
            size_t chunk = std::min(raw_end - at, (size_t)65535);
            bool fin = last && at + chunk == raw_end;
            bw.put(fin ? 1 : 0, 1);
            bw.put(0, 2);
            bw.align();
            bw.put((uint32_t)chunk, 16);
            bw.put((uint32_t)(~chunk & 0xFFFF), 16);
            bw.out.append(data, at, chunk);
            at += chunk;
        } while (at < raw_end);
        return;
    }
    bw.put(last ? 1 : 0, 1);
    if (fix <= dyn) {
        bw.put(1, 2);
        write_symbols(bw, syms, canonical(fl), fl, canonical(fd), fd);
        std::vector<uint32_t> fc = canonical(fl);
        bw.put(fc[256], fl[256]);
        return;
    }
    bw.put(2, 2);
    bw.put((uint32_t)(nlit - 257), 5);
    bw.put((uint32_t)(ndist - 1), 5);
    bw.put((uint32_t)(ncl - 4), 4);
    for (int i = 0; i < ncl; i++) bw.put((uint32_t)cl[(size_t)CL_ORDER[i]], 3);
    std::vector<uint32_t> cc = canonical(cl);
    for (auto &p : rle) {
        bw.put(cc[(size_t)p.first], cl[(size_t)p.first]);
        if (p.first == 16) bw.put((uint32_t)p.second, 2);
        else if (p.first == 17) bw.put((uint32_t)p.second, 3);
        else if (p.first == 18) bw.put((uint32_t)p.second, 7);
    }
    std::vector<uint32_t> lc = canonical(ll), dc = canonical(dl);
    write_symbols(bw, syms, lc, ll, dc, dl);
    bw.put(lc[256], ll[256]);
}

}  // namespace

std::string deflate_raw(const std::string &data) {
    BitWriter bw;
    size_t n = data.size();
    if (n == 0) {   // one empty fixed block
        bw.put(1, 1);
        bw.put(1, 2);
        bw.put(0, 7);
        bw.align();
        return bw.out;
    }
    const unsigned char *s = (const unsigned char *)data.data();
    const int WINDOW = 32768, HASH = 1 << 15, MAX_CHAIN = 128, NICE = 258;
    std::vector<int32_t> head(HASH, -1), prev(n, -1);
    auto hash3 = [&](size_t i) { return (uint32_t)((s[i] << 10) ^ (s[i + 1] << 5) ^ s[i + 2]) & (HASH - 1); };
    auto insert = [&](size_t i) {
        if (i + 2 >= n) return;
        uint32_t h = hash3(i);
        prev[i] = head[h];
        head[h] = (int32_t)i;
    };
    auto longest = [&](size_t i, int &best_dist) {
        int best = 0;
        if (i + 2 >= n) return 0;
        int32_t cand = head[hash3(i)];
        int chain = MAX_CHAIN;
        size_t maxlen = std::min((size_t)258, n - i);
        while (cand >= 0 && chain-- > 0 && i - (size_t)cand <= (size_t)WINDOW) {
            const unsigned char *a = s + i, *b = s + cand;
            if (b[best] == a[best] && b[0] == a[0]) {
                size_t l = 0;
                while (l < maxlen && a[l] == b[l]) l++;
                if ((int)l > best) {
                    best = (int)l;
                    best_dist = (int)(i - (size_t)cand);
                    if (l >= (size_t)NICE || l == maxlen) break;
                }
            }
            cand = prev[(size_t)cand];
        }
        return best >= 3 ? best : 0;
    };

    std::vector<Sym> syms;
    size_t block_start = 0, i = 0;
    const size_t BLOCK_SYMS = 1 << 15;
    while (i < n) {
        int dist = 0;
        int len = longest(i, dist);
        insert(i);
        if (len && i + 1 < n) {   // lazy matching: a longer match starting one byte later wins
            int d2 = 0;
            if (longest(i + 1, d2) > len) {
                syms.push_back({s[i], 0});
                i++;
                continue;   // the next round finds that longer match (and adds i + 1 to the chains)
            }
        }
        if (len) {
            syms.push_back({(uint16_t)len, (uint16_t)dist});
            for (size_t k = i + 1; k < i + (size_t)len; k++) insert(k);
            i += (size_t)len;
        } else {
            syms.push_back({s[i], 0});
            i++;
        }
        if (syms.size() >= BLOCK_SYMS) {
            emit_block(bw, syms, data, block_start, i, i >= n);
            syms.clear();
            block_start = i;
        }
    }
    if (!syms.empty() || block_start < n) emit_block(bw, syms, data, block_start, n, true);
    bw.align();
    return bw.out;
}

std::string compress(const std::string &data) {
    std::string out;
    out += (char)0x78;
    out += (char)0xDA;   // best compression, like zlib.compress(..., 9)
    out += deflate_raw(data);
    uint32_t a = adler32(data);
    for (int i = 3; i >= 0; i--) out += (char)((a >> (i * 8)) & 0xFF);
    return out;
}

// ============================================================================= base64

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const std::string &data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t v = ((unsigned char)data[i] << 16) | ((unsigned char)data[i + 1] << 8) | (unsigned char)data[i + 2];
        out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += B64[(v >> 6) & 63]; out += B64[v & 63];
    }
    if (i + 1 == data.size()) {
        uint32_t v = (unsigned char)data[i] << 16;
        out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == data.size()) {
        uint32_t v = ((unsigned char)data[i] << 16) | ((unsigned char)data[i + 1] << 8);
        out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += B64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string b64decode(const std::string &text) {
    std::string out;
    uint32_t v = 0;
    int bits = 0, quad = 0, pads = 0;
    for (char c : text) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else if (c == '=') {
            pads++;
            if (quad >= 2 && quad + pads >= 4) { quad = 0; pads = 0; v = 0; bits = 0; }
            continue;
        } else continue;   // not in the alphabet: skipped
        if (pads) { pads = 0; }
        v = (v << 6) | (uint32_t)d;
        bits += 6;
        quad = (quad + 1) % 4;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((v >> bits) & 0xFF);
        }
    }
    if (quad == 1) throw Error{"Invalid base64-encoded string"};
    if (quad != 0 && pads == 0) throw Error{"Incorrect padding"};
    return out;
}

}  // namespace zlib
