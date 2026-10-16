#include "images.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include "files.h"
#include "zlib.h"

namespace images {

static const int MAX_SIDE = 800;
static const long long MAX_PIXELS = 36000000;

static uint32_t be32(const std::string &s, size_t o) {
    return ((uint32_t)(unsigned char)s[o] << 24) | ((uint32_t)(unsigned char)s[o + 1] << 16) | ((uint32_t)(unsigned char)s[o + 2] << 8) |
           (unsigned char)s[o + 3];
}
static unsigned be16(const std::string &s, size_t o) { return ((unsigned char)s[o] << 8) | (unsigned char)s[o + 1]; }

Image read_image(const std::string &path) {
    std::string raw, err;
    if (!files::read(path, raw, &err)) throw ImageError{"COULDN'T OPEN IT (" + err + ")"};
    if (raw.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) return read_png(raw);
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xD8) return read_jpeg(raw);
    throw ImageError{"THAT ISN'T A PNG OR JPEG PICTURE"};
}

// ----------------------------------------------------------------------------- JPEG

Image read_jpeg(const std::string &raw) {
    size_t i = 2;
    bool adobe = false;
    while (i + 4 <= raw.size()) {
        if ((unsigned char)raw[i] != 0xFF) throw ImageError{"THE JPEG IS DAMAGED"};
        unsigned marker = (unsigned char)raw[i + 1];
        if (marker == 0xFF) { i++; continue; }
        if (marker == 0x01 || (marker >= 0xD0 && marker < 0xD8)) { i += 2; continue; }
        unsigned seglen = be16(raw, i + 2);
        if (marker == 0xEE && raw.compare(i + 4, 5, "Adobe") == 0) adobe = true;
        static const std::set<unsigned> SOF = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF};
        if (SOF.count(marker)) {
            if (i + 10 > raw.size()) throw ImageError{"THE JPEG IS DAMAGED"};
            int depth = (unsigned char)raw[i + 4];
            int h = (int)be16(raw, i + 5), w = (int)be16(raw, i + 7), comps = (unsigned char)raw[i + 9];
            if (marker == 0xC3 || marker == 0xC7 || marker == 0xCB || marker == 0xCF || depth != 8)
                throw ImageError{"THAT KIND OF JPEG ISN'T SUPPORTED; SAVE IT AS A PNG"};
            if ((comps != 1 && comps != 3 && comps != 4) || !w || !h) throw ImageError{"THE JPEG IS DAMAGED"};
            Image img;
            img.w = w;
            img.h = h;
            img.filter = "DCTDecode";
            img.colors = comps;
            img.data = raw;
            img.inverted = comps == 4 && adobe;
            return img;
        }
        i += 2 + seglen;
    }
    throw ImageError{"THE JPEG IS DAMAGED"};
}

// ----------------------------------------------------------------------------- PNG

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

// undo the row filters of one (sub)image; rows are appended to out
static size_t unfilter(const std::string &data, size_t pos, int h, int bpp, size_t stride, std::vector<std::string> &rows) {
    std::string prev(stride, '\0');
    for (int r = 0; r < h; r++) {
        if (pos + 1 + stride > data.size()) throw ImageError{"THE PNG IS CUT OFF"};
        int ft = (unsigned char)data[pos];
        std::string row = data.substr(pos + 1, stride);
        pos += 1 + stride;
        auto R = [&](size_t x) { return (int)(unsigned char)row[x]; };
        auto P = [&](size_t x) { return (int)(unsigned char)prev[x]; };
        size_t b = (size_t)bpp;
        if (ft == 1) {
            for (size_t x = b; x < stride; x++) row[x] = (char)((R(x) + R(x - b)) & 255);
        } else if (ft == 2) {
            for (size_t x = 0; x < stride; x++) row[x] = (char)((R(x) + P(x)) & 255);
        } else if (ft == 3) {
            for (size_t x = 0; x < stride; x++) row[x] = (char)((R(x) + ((x >= b ? R(x - b) : 0) + P(x)) / 2) & 255);
        } else if (ft == 4) {
            for (size_t x = 0; x < stride; x++)
                row[x] = (char)(x >= b ? (R(x) + paeth(R(x - b), P(x), P(x - b))) & 255 : (R(x) + P(x)) & 255);
        } else if (ft != 0) throw ImageError{"THE PNG IS DAMAGED"};
        rows.push_back(row);
        prev = row;
    }
    return pos;
}

static std::string samples(const std::string &row, int w, int ch, int depth) {
    size_t n = (size_t)w * (size_t)ch;
    if (depth == 8) return row.substr(0, n);
    std::string out(n, '\0');
    if (depth == 16) {
        for (size_t i = 0; i < n; i++) out[i] = row[2 * i];
        return out;
    }
    size_t per = (size_t)(8 / depth);
    int mask = (1 << depth) - 1;
    for (size_t i = 0; i < n; i++)
        out[i] = (char)(((unsigned char)row[i / per] >> (8 - depth * (int)(i % per + 1))) & mask);
    return out;
}

static void shrink(std::string &pix, std::string &alpha, int &w, int &h, int colors, int k) {
    int nw = w / k, nh = h / k;
    std::vector<int> offs = {k / 4, (3 * k) / 4};
    if (offs[0] == offs[1]) offs.pop_back();
    std::string out((size_t)nw * (size_t)nh * (size_t)colors, '\0'), oa((size_t)nw * (size_t)nh, '\0');
    size_t m = offs.size() * offs.size();
    for (int ny = 0; ny < nh; ny++)
        for (int nx = 0; nx < nw; nx++) {
            long tot = 0;
            long sums[4] = {0, 0, 0, 0};
            for (int sy : offs)
                for (int sx : offs) {
                    size_t at = (size_t)(ny * k + sy) * (size_t)w + (size_t)(nx * k + sx);
                    long a = (unsigned char)alpha[at];
                    tot += a;
                    for (int c = 0; c < colors; c++) sums[c] += a * (unsigned char)pix[at * (size_t)colors + (size_t)c];
                }
            size_t j = (size_t)ny * (size_t)nw + (size_t)nx;
            oa[j] = (char)(tot / (long)m);
            if (tot)
                for (int c = 0; c < colors; c++) out[j * (size_t)colors + (size_t)c] = (char)std::min(255L, sums[c] / tot);
        }
    pix.swap(out);
    alpha.swap(oa);
    w = nw;
    h = nh;
}

Image read_png(const std::string &raw) {
    size_t pos = 8;
    std::string idat, plte, trns;
    bool have_ihdr = false, have_plte = false, have_trns = false, have_idat = false;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0;
    while (pos + 8 <= raw.size()) {
        uint32_t length = be32(raw, pos);
        std::string kind = raw.substr(pos + 4, 4);
        std::string body = raw.substr(std::min(raw.size(), pos + 8), length);
        pos += 12 + (size_t)length;
        if (kind == "IHDR") {
            if (body.size() < 13) throw ImageError{"THE PNG IS DAMAGED"};
            w = be32(body, 0);
            h = be32(body, 4);
            depth = (unsigned char)body[8];
            ctype = (unsigned char)body[9];
            interlace = (unsigned char)body[12];
            have_ihdr = true;
        } else if (kind == "PLTE") { plte = body; have_plte = true; }
        else if (kind == "tRNS") { trns = body; have_trns = true; }
        else if (kind == "IDAT") { idat += body; have_idat = true; }
        else if (kind == "IEND") break;
    }
    if (!have_ihdr || !have_idat) throw ImageError{"THE PNG IS DAMAGED"};
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) || !w || !h || (ctype == 3 && plte.empty()))
        throw ImageError{"THE PNG IS DAMAGED"};
    // (unsigned 64-bit: two 32-bit sides can't overflow it, however large a damaged file claims they are)
    if ((uint64_t)w * (uint64_t)h > (uint64_t)MAX_PIXELS)
        throw ImageError{"THE PICTURE IS HUGE (" + std::to_string(w) + "x" + std::to_string(h) + "); SAVE A SMALLER COPY"};
    std::string data;
    try {
        // a picture never needs more than its pixels plus one filter byte per row (times 8 for safety)
        data = zlib::decompress(idat, (size_t)((uint64_t)w * h * 8 + (uint64_t)h * 8 + 1024));
    } catch (zlib::Error &) {
        throw ImageError{"THE PNG IS DAMAGED"};
    }
    int W = (int)w, H = (int)h;
    int colors = (ctype == 0 || ctype == 4) ? 1 : 3;
    std::string pix((size_t)W * (size_t)H * (size_t)colors, '\0'), alpha((size_t)W * (size_t)H, '\xff');
    int scale = depth < 8 && ctype != 3 ? 255 / ((1 << depth) - 1) : 1;
    std::vector<int> key;
    if (have_trns && ctype == 0 && trns.size() >= 2) key.push_back((int)(be16(trns, 0) >> (depth == 16 ? 8 : 0)));
    else if (have_trns && ctype == 2 && trns.size() >= 6)
        for (int i = 0; i < 3; i++) key.push_back((int)(be16(trns, (size_t)i * 2) >> (depth == 16 ? 8 : 0)));
    std::vector<std::array<unsigned char, 3>> pal;
    for (size_t i = 0; i + 2 < plte.size() + 0 && i + 3 <= plte.size(); i += 3)
        pal.push_back({(unsigned char)plte[i], (unsigned char)plte[i + 1], (unsigned char)plte[i + 2]});
    std::vector<int> pal_a;
    for (unsigned char c : trns) pal_a.push_back(c);
    while (pal_a.size() < pal.size()) pal_a.push_back(255);
    (void)have_plte;

    int bpp = std::max(1, ch * depth / 8);
    size_t p = 0;
    struct Pass { int x0, y0, dx, dy; };
    static const Pass ADAM7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass PLAIN[1] = {{0, 0, 1, 1}};
    const Pass *passes = interlace ? ADAM7 : PLAIN;
    int npass = interlace ? 7 : 1;
    for (int pi = 0; pi < npass; pi++) {
        const Pass &ps = passes[pi];
        int pw = (W - ps.x0 + ps.dx - 1) / ps.dx, ph = (H - ps.y0 + ps.dy - 1) / ps.dy;
        if (pw <= 0 || ph <= 0) continue;
        std::vector<std::string> rows;
        p = unfilter(data, p, ph, bpp, (size_t)(((long long)pw * ch * depth + 7) / 8), rows);
        std::string s;
        for (const auto &row : rows) s += samples(row, pw, ch, depth);
        size_t n = (size_t)pw * (size_t)ph;
        std::string packed(n * (size_t)colors, '\0'), a(n, '\xff');
        if (ctype == 3) {
            int mx = 0;
            for (unsigned char c : s) mx = std::max(mx, (int)c);
            if (!pal.empty() && mx >= (int)pal.size()) throw ImageError{"THE PNG IS DAMAGED"};
            for (size_t i = 0; i < n; i++) {
                unsigned char idx = (unsigned char)s[i];
                for (int c = 0; c < 3; c++) packed[i * 3 + (size_t)c] = idx < pal.size() ? (char)pal[idx][(size_t)c] : 0;
                a[i] = idx < pal_a.size() ? (char)pal_a[idx] : 0;
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                bool match = !key.empty();
                for (int c = 0; c < colors; c++) {
                    int v = (unsigned char)s[i * (size_t)ch + (size_t)c];
                    if (scale != 1) v = std::min(255, v * scale);
                    packed[i * (size_t)colors + (size_t)c] = (char)v;
                    if (match && v != key[(size_t)c] * scale) match = false;
                }
                if (ctype == 4 || ctype == 6) a[i] = s[i * (size_t)ch + (size_t)ch - 1];
                else if (!key.empty()) a[i] = match ? 0 : (char)255;
            }
        }
        if (!interlace) {
            pix.swap(packed);
            alpha.swap(a);
            break;
        }
        for (int r = 0; r < ph; r++) {
            int y = ps.y0 + r * ps.dy;
            for (int i = 0; i < pw; i++) {
                size_t at = (size_t)y * (size_t)W + (size_t)(ps.x0 + i * ps.dx), j = (size_t)r * (size_t)pw + (size_t)i;
                for (int c = 0; c < colors; c++) pix[at * (size_t)colors + (size_t)c] = packed[j * (size_t)colors + (size_t)c];
                alpha[at] = a[j];
            }
        }
    }
    if (std::max(W, H) > MAX_SIDE) shrink(pix, alpha, W, H, colors, (std::max(W, H) + MAX_SIDE - 1) / MAX_SIDE);
    bool opaque = std::all_of(alpha.begin(), alpha.end(), [](char c) { return (unsigned char)c == 255; });
    Image img;
    img.w = W;
    img.h = H;
    img.filter = "FlateDecode";
    img.colors = colors;
    img.data = zlib::compress(pix);
    img.has_alpha = !opaque;
    if (!opaque) img.alpha = zlib::compress(alpha);
    return img;
}

// ----------------------------------------------------------------------------- storage

json::Value to_json(const Image &img) {
    json::Value d = json::Value::object();
    d["w"] = img.w;
    d["h"] = img.h;
    d["filter"] = img.filter;
    d["colors"] = img.colors;
    d["data"] = zlib::b64encode(img.data);
    d["alpha"] = img.has_alpha ? json::Value(zlib::b64encode(img.alpha)) : json::Value();
    d["inverted"] = img.inverted;
    if (img.has_name) d["name"] = img.name;
    return d;
}

Image from_json(const json::Value &d) {
    const ImageError bad{"THE SAVED LOGO IS DAMAGED"};
    if (!d.is_object() || !d.has("data") || !d.has("w") || !d.has("h") || !d.has("filter") || !d.has("colors")) throw bad;
    Image img;
    try {
        if (!d["data"].is_string()) throw bad;
        img.data = zlib::b64decode(d["data"].str());
        if (d["alpha"].truthy()) {
            if (!d["alpha"].is_string()) throw bad;
            img.alpha = zlib::b64decode(d["alpha"].str());
            img.has_alpha = true;
        }
    } catch (zlib::Error &) {
        throw bad;
    }
    // int(w), int(h): numbers or number-like text
    auto whole = [&](const json::Value &v, int &out) {
        if (v.is_int() || v.is_bool()) { out = (int)v.as_int(); return true; }
        if (v.is_float()) { out = (int)v.as_double(); return std::isfinite(v.as_double()); }
        if (v.is_string()) {
            std::string t = v.str();
            char *end = nullptr;
            long x = std::strtol(t.c_str(), &end, 10);
            if (t.empty() || *end) return false;
            out = (int)x;
            return true;
        }
        return false;
    };
    if (!whole(d["w"], img.w) || !whole(d["h"], img.h)) throw bad;
    img.filter = d["filter"].str();
    if (img.filter != "DCTDecode" && img.filter != "FlateDecode") throw bad;
    if (!d["colors"].is_number() && !d["colors"].is_bool()) throw bad;
    int colors = (int)d["colors"].as_int(-1);
    if (d["colors"].is_float() && d["colors"].as_double() != (double)colors) throw bad;
    if (colors != 1 && colors != 3 && colors != 4) throw bad;
    img.colors = colors;
    img.inverted = d["inverted"].truthy();
    if (d.has("name")) { img.name = d["name"].str(); img.has_name = true; }
    return img;
}

}  // namespace images
