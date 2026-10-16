#include "jpeg.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI   // Windows leaves it out
#define M_PI 3.14159265358979323846
#endif

namespace jpeg {

namespace {

const int ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                        41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                        30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct Huff {
    std::vector<int> mincode, maxcode, valptr;
    std::vector<uint8_t> vals;
    bool ok = false;
    void build(const uint8_t *counts, const uint8_t *symbols, size_t nsym) {
        vals.assign(symbols, symbols + nsym);
        mincode.assign(17, 0);
        maxcode.assign(18, -1);
        valptr.assign(17, 0);
        int code = 0, k = 0;
        for (int len = 1; len <= 16; len++) {
            valptr[(size_t)len] = k;
            mincode[(size_t)len] = code;
            code += counts[len - 1];
            k += counts[len - 1];
            maxcode[(size_t)len] = counts[len - 1] ? code - 1 : -1;
            code <<= 1;
        }
        ok = true;
    }
};

struct Comp {
    int id, h, v, tq;
    int td = 0, ta = 0;
    int bw = 0, bh = 0;            // blocks across and down (whole MCUs)
    std::vector<int16_t> coef;     // 64 per block
    int pred = 0;
    std::vector<uint8_t> plane;    // decoded samples, bw*8 x bh*8
};

struct Decoder {
    const uint8_t *d;
    size_t n, pos = 0;
    std::string err;
    uint16_t qt[4][64] = {};
    Huff dc[4], ac[4];
    std::vector<Comp> comps;
    int width = 0, height = 0, hmax = 1, vmax = 1, mcux = 0, mcuy = 0;
    bool progressive = false, adobe = false;
    int adobe_transform = -1;
    int restart = 0;
    // bit reader
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool hit_marker = false;
    int eobrun = 0;

    bool fail(const char *m) {
        if (err.empty()) err = m;
        return false;
    }
    int u8() { return pos < n ? d[pos++] : 0; }
    int u16() {
        int a = u8();
        return (a << 8) | u8();
    }

    void reset_bits() {
        bitbuf = 0;
        bitcnt = 0;
        hit_marker = false;
    }
    int bit() {
        if (bitcnt == 0) {
            int b = 0;
            if (pos >= n) hit_marker = true;   // the file ended: zeros from here
            if (!hit_marker && pos < n) {
                b = d[pos];
                if (b == 0xFF) {
                    int b2 = pos + 1 < n ? d[pos + 1] : 0;
                    if (b2 == 0) pos += 2;
                    else {
                        hit_marker = true;   // a marker: pretend the rest is zeros
                        b = 0;
                    }
                } else pos++;
            }
            bitbuf = (uint32_t)b;
            bitcnt = 8;
        }
        bitcnt--;
        return (bitbuf >> bitcnt) & 1;
    }
    // the scan's data has run out (the rest would all be zeros): a damaged file stops here
    bool out_of_data() const { return hit_marker && bitcnt == 0 && !restart; }
    int bits(int k) {
        k = std::min(k, 16);   // a damaged table can ask for more
        int v = 0;
        for (int i = 0; i < k; i++) v = (v << 1) | bit();
        return v;
    }
    int extend(int v, int k) {
        k = std::min(k, 16);
        return k == 0 ? 0 : (v < (1 << (k - 1)) ? v - (1 << k) + 1 : v);
    }
    int decode(const Huff &h) {
        if (!h.ok) return -1;
        int code = 0;
        for (int len = 1; len <= 16; len++) {
            code = (code << 1) | bit();
            if (h.maxcode[(size_t)len] >= code && code >= h.mincode[(size_t)len] && h.maxcode[(size_t)len] != -1) {
                size_t i = (size_t)(h.valptr[(size_t)len] + code - h.mincode[(size_t)len]);
                return i < h.vals.size() ? h.vals[i] : -1;
            }
        }
        return -1;
    }

    bool read_sof(int len) {
        if (u8() != 8) return fail("only 8-bit JPEGs");
        height = u16();
        width = u16();
        int nc = u8();
        if (!width || !height || (nc != 1 && nc != 3 && nc != 4)) return fail("damaged JPEG");
        if ((long long)width * height > 36000000LL) return fail("too big");
        comps.clear();
        for (int i = 0; i < nc; i++) {
            Comp c{};
            c.id = u8();
            int hv = u8();
            c.h = std::max(1, hv >> 4);
            c.v = std::max(1, hv & 15);
            c.tq = u8() & 3;
            if (c.h > 4 || c.v > 4) return fail("damaged JPEG");
            comps.push_back(c);
            hmax = std::max(hmax, c.h);
            vmax = std::max(vmax, c.v);
        }
        (void)len;
        mcux = (width + 8 * hmax - 1) / (8 * hmax);
        mcuy = (height + 8 * vmax - 1) / (8 * vmax);
        for (auto &c : comps) {
            c.bw = mcux * c.h;
            c.bh = mcuy * c.v;
            c.coef.assign((size_t)c.bw * (size_t)c.bh * 64, 0);
        }
        return true;
    }

    bool read_dht(int len) {
        int end = (int)pos + len - 2;
        while ((int)pos < end) {
            int tc = u8(), th = tc & 15;
            tc >>= 4;
            if (th > 3) return fail("damaged JPEG");
            uint8_t counts[16];
            size_t total = 0;
            for (int i = 0; i < 16; i++) {
                counts[i] = (uint8_t)u8();
                total += counts[i];
            }
            if (total > 256 || pos + total > n) return fail("damaged JPEG");
            (tc ? ac : dc)[th].build(counts, d + pos, total);
            pos += total;
        }
        return true;
    }

    bool read_dqt(int len) {
        int end = (int)pos + len - 2;
        while ((int)pos < end) {
            int pq = u8(), tq = pq & 3;
            pq >>= 4;
            for (int i = 0; i < 64; i++) qt[tq][ZIGZAG[i]] = (uint16_t)(pq ? u16() : u8());
        }
        return true;
    }

    // one block, baseline or the first/refining progressive scans
    void decode_block_baseline(Comp &c, int16_t *blk) {
        int t = decode(dc[c.td]);
        int diff = t > 0 ? extend(bits(t), t) : 0;
        c.pred += diff;
        blk[0] = (int16_t)c.pred;
        for (int k = 1; k < 64;) {
            int rs = decode(ac[c.ta]);
            if (rs < 0) return;
            int r = rs >> 4, s = rs & 15;
            if (!s) {
                if (r != 15) break;
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) break;
            blk[ZIGZAG[k]] = (int16_t)extend(bits(s), s);
            k++;
        }
    }

    void dc_first(Comp &c, int16_t *blk, int al) {
        int t = decode(dc[c.td]);
        int diff = t > 0 ? extend(bits(t), t) : 0;
        c.pred += diff;
        blk[0] = (int16_t)(c.pred * (1 << al));
    }
    void dc_refine(int16_t *blk, int al) {
        if (bit()) blk[0] = (int16_t)(blk[0] | (1 << al));
    }
    void ac_first(Comp &c, int16_t *blk, int ss, int se, int al) {
        if (eobrun > 0) {
            eobrun--;
            return;
        }
        for (int k = ss; k <= se;) {
            int rs = decode(ac[c.ta]);
            if (rs < 0) return;
            int r = rs >> 4, s = rs & 15;
            if (!s) {
                if (r < 15) {
                    eobrun = (1 << r) - 1;
                    if (r) eobrun += bits(r);
                    break;
                }
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) break;
            blk[ZIGZAG[k]] = (int16_t)(extend(bits(s), s) * (1 << al));
            k++;
        }
    }
    void ac_refine(Comp &c, int16_t *blk, int ss, int se, int al) {
        int p1 = 1 << al, m1 = -1 * (1 << al);
        int k = ss;
        if (eobrun <= 0) {
            for (; k <= se;) {
                int rs = decode(ac[c.ta]);
                if (rs < 0) return;
                int r = rs >> 4, s = rs & 15, val = 0;
                if (!s) {
                    if (r < 15) {
                        eobrun = (1 << r);
                        if (r) eobrun += bits(r);
                        break;
                    }
                } else val = bit() ? p1 : m1;
                while (k <= se) {
                    int16_t &z = blk[ZIGZAG[k]];
                    if (z) {
                        if (bit() && (z & p1) == 0) z = (int16_t)(z >= 0 ? z + p1 : z + m1);
                    } else {
                        if (r == 0) {
                            if (val) z = (int16_t)val;
                            k++;
                            break;
                        }
                        r--;
                    }
                    k++;
                }
            }
        }
        if (eobrun > 0) {
            for (; k <= se; k++) {
                int16_t &z = blk[ZIGZAG[k]];
                if (z && bit() && (z & p1) == 0) z = (int16_t)(z >= 0 ? z + p1 : z + m1);
            }
            eobrun--;
        }
    }

    bool read_sos() {
        int ns = u8();
        std::vector<Comp *> sc;
        for (int i = 0; i < ns; i++) {
            int id = u8(), t = u8();
            Comp *c = nullptr;
            for (auto &x : comps) if (x.id == id) c = &x;
            if (!c) return fail("damaged JPEG");
            c->td = (t >> 4) & 3;
            c->ta = t & 3;
            sc.push_back(c);
        }
        int ss = u8(), se = u8(), a = u8(), ah = a >> 4, al = a & 15;
        if (comps.empty()) return fail("damaged JPEG");
        reset_bits();
        eobrun = 0;
        for (auto *c : sc) c->pred = 0;
        long count = 0, total = 0;
        auto restart_check = [&]() {
            if (!restart) return;
            if (++count % restart || count >= total) return;   // none after the scan's last MCU
            // the restart marker: realign and reset predictions (stop at any other marker)
            reset_bits();
            while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] != 0 && d[pos + 1] != 0xFF)) pos++;
            if (pos + 1 < n && d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7) pos += 2;
            for (auto *c : sc) c->pred = 0;
            eobrun = 0;
        };
        if (sc.size() == 1) {   // one component: its blocks in order, not in MCUs
            Comp &c = *sc[0];
            int bw = ((width * c.h + hmax - 1) / hmax + 7) / 8, bh = ((height * c.v + vmax - 1) / vmax + 7) / 8;
            bw = std::max(1, std::min(bw, c.bw));
            bh = std::max(1, std::min(bh, c.bh));
            total = (long)bw * bh;
            for (int by = 0; by < bh && !out_of_data(); by++)
                for (int bx = 0; bx < bw; bx++) {
                    int16_t *blk = &c.coef[((size_t)by * (size_t)c.bw + (size_t)bx) * 64];
                    run_block(c, blk, ss, se, ah, al);
                    restart_check();
                }
        } else {
            total = (long)mcux * mcuy;
            for (int my = 0; my < mcuy && !out_of_data(); my++)
                for (int mx = 0; mx < mcux; mx++) {
                    for (auto *cp : sc) {
                        Comp &c = *cp;
                        for (int v = 0; v < c.v; v++)
                            for (int h = 0; h < c.h; h++) {
                                int bx = mx * c.h + h, by = my * c.v + v;
                                run_block(c, &c.coef[((size_t)by * (size_t)c.bw + (size_t)bx) * 64], ss, se, ah, al);
                            }
                    }
                    restart_check();
                }
        }
        // skip to the next marker
        while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] != 0 && !(d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7))) pos++;
        return true;
    }

    void run_block(Comp &c, int16_t *blk, int ss, int se, int ah, int al) {
        if (!progressive) {
            decode_block_baseline(c, blk);
            return;
        }
        if (ss == 0) {
            if (ah == 0) dc_first(c, blk, al);
            else dc_refine(blk, al);
        } else if (ah == 0) ac_first(c, blk, ss, std::min(se, 63), al);
        else ac_refine(c, blk, ss, std::min(se, 63), al);
    }

    void idct_all() {
        static float cosv[8][8];
        static bool ready = false;
        if (!ready) {
            for (int x = 0; x < 8; x++)
                for (int u = 0; u < 8; u++) cosv[x][u] = (float)((u == 0 ? std::sqrt(0.5) : 1.0) * std::cos((2 * x + 1) * u * M_PI / 16));
            ready = true;
        }
        for (auto &c : comps) {
            c.plane.assign((size_t)c.bw * 8 * (size_t)c.bh * 8, 0);
            const uint16_t *q = qt[c.tq];
            for (int by = 0; by < c.bh; by++)
                for (int bx = 0; bx < c.bw; bx++) {
                    const int16_t *blk = &c.coef[((size_t)by * (size_t)c.bw + (size_t)bx) * 64];
                    bool flat = true;   // only the average: every pixel the same (most of a damaged or empty picture)
                    for (int i = 1; i < 64 && flat; i++) flat = blk[i] == 0;
                    if (flat) {
                        int v = (int)std::lround((float)blk[0] * q[0] / 8 + 128);
                        uint8_t b = (uint8_t)std::max(0, std::min(255, v));
                        for (int y = 0; y < 8; y++)
                            std::memset(&c.plane[((size_t)by * 8 + (size_t)y) * (size_t)c.bw * 8 + (size_t)bx * 8], b, 8);
                        continue;
                    }
                    float f[64], t[64];
                    for (int i = 0; i < 64; i++) f[i] = (float)blk[i] * q[i];
                    for (int y = 0; y < 8; y++)   // rows
                        for (int x = 0; x < 8; x++) {
                            float s = 0;
                            for (int u = 0; u < 8; u++) s += cosv[x][u] * f[y * 8 + u];
                            t[y * 8 + x] = s / 2;
                        }
                    for (int x = 0; x < 8; x++)
                        for (int y = 0; y < 8; y++) {
                            float s = 0;
                            for (int v = 0; v < 8; v++) s += cosv[y][v] * t[v * 8 + x];
                            int val = (int)std::lround(s / 2 + 128);
                            c.plane[((size_t)by * 8 + (size_t)y) * (size_t)c.bw * 8 + (size_t)bx * 8 + (size_t)x] = (uint8_t)std::max(0, std::min(255, val));
                        }
                }
        }
    }

    bool run() {
        if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return fail("not a JPEG");
        pos = 2;
        bool frame = false;
        while (pos + 4 <= n) {
            if (d[pos] != 0xFF) {
                pos++;
                continue;
            }
            int m = d[pos + 1];
            pos += 2;
            if (m == 0xFF || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
                if (m == 0xFF) pos--;
                continue;
            }
            if (m == 0xD9) break;
            int len = u16();
            size_t next = pos + (size_t)len - 2;
            if (len < 2 || next > n) return fail("damaged JPEG");
            if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
                progressive = m == 0xC2;
                if (!read_sof(len)) return false;
                frame = true;
            } else if (m == 0xC4) {
                if (!read_dht(len)) return false;
            } else if (m == 0xDB) read_dqt(len);
            else if (m == 0xDD) restart = u16();
            else if (m == 0xEE && len >= 12 && std::memcmp(d + pos, "Adobe", 5) == 0) {
                adobe = true;
                adobe_transform = d[pos + 11];
            } else if (m == 0xDA) {
                if (!frame) return fail("damaged JPEG");
                if (!read_sos()) return false;
                continue;
            } else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) return fail("that kind of JPEG isn't supported");
            pos = next;
        }
        if (!frame) return fail("damaged JPEG");
        idct_all();
        return true;
    }
};

}  // namespace

bool decode(const std::string &data, int &w, int &h, std::vector<uint8_t> &rgb, std::string *err) {
    Decoder dec;
    dec.d = (const uint8_t *)data.data();
    dec.n = data.size();
    if (!dec.run()) {
        if (err) *err = dec.err;
        return false;
    }
    w = dec.width;
    h = dec.height;
    rgb.assign((size_t)w * (size_t)h * 3, 0);
    size_t nc = dec.comps.size();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double v[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < nc; k++) {
                const Comp &c = dec.comps[k];
                size_t stride = (size_t)c.bw * 8;
                if (c.h == dec.hmax && c.v == dec.vmax) {
                    v[k] = c.plane[(size_t)y * stride + (size_t)x];
                    continue;
                }
                // a smaller color plane: blend the four nearest samples
                int pw = (dec.width * c.h + dec.hmax - 1) / dec.hmax, ph = (dec.height * c.v + dec.vmax - 1) / dec.vmax;
                double fx = (x + 0.5) * c.h / dec.hmax - 0.5, fy = (y + 0.5) * c.v / dec.vmax - 0.5;
                int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                double tx = fx - x0, ty = fy - y0;
                auto at = [&](int sx, int sy) {
                    sx = std::max(0, std::min(pw - 1, sx));
                    sy = std::max(0, std::min(ph - 1, sy));
                    return (double)c.plane[(size_t)sy * stride + (size_t)sx];
                };
                v[k] = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
            }
            double r, g, b;
            bool ycc = nc == 3 ? (!dec.adobe || dec.adobe_transform != 0) : (nc == 4 && dec.adobe_transform == 2);
            if (nc == 1) r = g = b = v[0];
            else if (ycc) {
                double Y = v[0], cb = v[1] - 128, cr = v[2] - 128;
                r = Y + 1.402 * cr;
                g = Y - 0.344136 * cb - 0.714136 * cr;
                b = Y + 1.772 * cb;
            } else {
                r = v[0];
                g = v[1];
                b = v[2];
            }
            if (nc == 4) {   // CMYK (Adobe's are stored inverted; YCCK's color turns into true CMY)
                double k = v[3];
                if (ycc) {
                    r = 255 - r;
                    g = 255 - g;
                    b = 255 - b;
                }
                if (dec.adobe) {
                    r = r * k / 255;
                    g = g * k / 255;
                    b = b * k / 255;
                } else {
                    r = (255 - r) * (255 - k) / 255;
                    g = (255 - g) * (255 - k) / 255;
                    b = (255 - b) * (255 - k) / 255;
                }
            }
            uint8_t *p = &rgb[((size_t)y * (size_t)w + (size_t)x) * 3];
            p[0] = (uint8_t)std::max(0.0, std::min(255.0, std::round(r)));
            p[1] = (uint8_t)std::max(0.0, std::min(255.0, std::round(g)));
            p[2] = (uint8_t)std::max(0.0, std::min(255.0, std::round(b)));
        }
    return true;
}

}  // namespace jpeg
