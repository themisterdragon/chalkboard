#include "raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "files.h"
#include "ttf.h"
#include "zlib.h"

#ifndef M_PI   // Windows leaves it out
#define M_PI 3.14159265358979323846
#endif

namespace raster {

// ============================================================================= paths

void Path::move(double x, double y) {
    contours_.push_back({{x, y}});
    at_ = {x, y};
}

void Path::line(double x, double y) {
    if (contours_.empty()) contours_.push_back({at_});
    contours_.back().push_back({x, y});
    at_ = {x, y};
}

// Curves become short lines. Enough of them that a big curve on a 1920-wide slide stays smooth;
// the fill flattens again in pixel space for anything drawn much larger.
void Path::quad(double cx, double cy, double x, double y) {
    Pt a = at_;
    double len = std::hypot(cx - a.x, cy - a.y) + std::hypot(x - cx, y - cy);
    int n = std::max(4, std::min(64, (int)(std::sqrt(len) * 1.2)));
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        line(u * u * a.x + 2 * u * t * cx + t * t * x, u * u * a.y + 2 * u * t * cy + t * t * y);
    }
}

void Path::cubic(double c1x, double c1y, double c2x, double c2y, double x, double y) {
    Pt a = at_;
    double len = std::hypot(c1x - a.x, c1y - a.y) + std::hypot(c2x - c1x, c2y - c1y) + std::hypot(x - c2x, y - c2y);
    int n = std::max(6, std::min(96, (int)(std::sqrt(len) * 1.5)));
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        line(u * u * u * a.x + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x,
             u * u * u * a.y + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y);
    }
}

void Path::close() {
    if (!contours_.empty() && !contours_.back().empty()) {
        Pt s = contours_.back().front();
        at_ = s;
    }
}

void Path::rect(double x, double y, double w, double h) {
    move(x, y);
    line(x + w, y);
    line(x + w, y + h);
    line(x, y + h);
    close();
}

void Path::ellipse(double cx, double cy, double rx, double ry) {
    const double k = 0.5523;
    move(cx + rx, cy);
    cubic(cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry);
    cubic(cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy);
    cubic(cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry);
    cubic(cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy);
    close();
}

// ============================================================================= the pixel grid

Image::Image(int w0, int h0, Color fill) : w(std::max(0, w0)), h(std::max(0, h0)) {
    px.resize((size_t)w * (size_t)h * 3);
    for (size_t i = 0; i < px.size(); i += 3) {
        px[i] = (uint8_t)std::lround(fill.r * 255);
        px[i + 1] = (uint8_t)std::lround(fill.g * 255);
        px[i + 2] = (uint8_t)std::lround(fill.b * 255);
    }
}

void Image::clip(double x, double y, double cw, double ch) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y), x1 = (int)std::ceil(x + cw), y1 = (int)std::ceil(y + ch);
    if (!clips_.empty()) {
        const auto &c = clips_.back();
        x0 = std::max(x0, c[0]);
        y0 = std::max(y0, c[1]);
        x1 = std::min(x1, c[2]);
        y1 = std::min(y1, c[3]);
    }
    clips_.push_back({x0, y0, x1, y1});
}

void Image::unclip() {
    if (!clips_.empty()) clips_.pop_back();
}

void Image::blend(int x, int y, Color c, double a) {
    if (a <= 0) return;
    uint8_t *p = &px[((size_t)y * (size_t)w + (size_t)x) * 3];
    double cs[3] = {c.r, c.g, c.b};
    if (a >= 1) {
        for (int k = 0; k < 3; k++) p[k] = (uint8_t)(std::max(0.0, std::min(1.0, cs[k])) * 255 + 0.5);
        return;
    }
    for (int k = 0; k < 3; k++) {
        double v = p[k] / 255.0 * (1 - a) + cs[k] * a;
        p[k] = (uint8_t)(std::max(0.0, std::min(1.0, v)) * 255 + 0.5);
    }
}

namespace {

// One color at one strength, worked out once for a whole shape: the same numbers blend() gives
// each pixel (a table of every possible result, built the first time it's needed)
struct Ink {
    double cs[3], a;
    uint8_t full[3], lut[3][256];
    bool built = false;
    Ink(Color c, double alpha) : cs{c.r, c.g, c.b}, a(alpha) {
        for (int k = 0; k < 3; k++) full[k] = (uint8_t)(std::max(0.0, std::min(1.0, cs[k])) * 255 + 0.5);
    }
    void put(uint8_t *p) {
        if (a <= 0) return;
        if (a >= 1) {
            p[0] = full[0];
            p[1] = full[1];
            p[2] = full[2];
            return;
        }
        if (!built) {
            for (int k = 0; k < 3; k++)
                for (int v = 0; v < 256; v++) {
                    double r = v / 255.0 * (1 - a) + cs[k] * a;
                    lut[k][v] = (uint8_t)(std::max(0.0, std::min(1.0, r)) * 255 + 0.5);
                }
            built = true;
        }
        p[0] = lut[0][p[0]];
        p[1] = lut[1][p[1]];
        p[2] = lut[2][p[2]];
    }
    void run(uint8_t *p, int n) {   // n pixels in a row
        if (a <= 0 || n <= 0) return;
        if (a >= 1) {
            // one pixel, then copy what's done onto the rest, doubling each time (memcpy is fast)
            p[0] = full[0];
            p[1] = full[1];
            p[2] = full[2];
            size_t done = 3, total = (size_t)n * 3;
            while (done < total) {
                size_t k = std::min(done, total - done);
                std::memcpy(p + done, p, k);
                done += k;
            }
            return;
        }
        put(p);   // builds the table
        for (int i = 1; i < n; i++) {
            p += 3;
            p[0] = lut[0][p[0]];
            p[1] = lut[1][p[1]];
            p[2] = lut[2][p[2]];
        }
    }
};

// a float that is exactly +0 (adding it changes nothing)
inline bool zero_bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u == 0;
}

struct Box {
    int x0, y0, x1, y1;
};

// Signed-area coverage accumulation (the method font-rs uses): each edge adds the area it sweeps
// to the cells it crosses; a running sum along each row gives how much of each pixel is inside.
struct Acc {
    int w, h, ox, oy;   // box size and where it sits in the image
    std::vector<float> a;
    Acc(int w0, int h0, int ox0, int oy0) : w(w0), h(h0), ox(ox0), oy(oy0), a((size_t)(w0 + 2) * (size_t)h0, 0.f) {}

    void line(double x0, double y0, double x1, double y1) {
        x0 -= ox;
        x1 -= ox;
        y0 -= oy;
        y1 -= oy;
        // keep x inside the box: the part to the left becomes an upright edge at x = 0 (it still
        // counts for winding), the part to the right one at x = w
        cut0(x0, y0, x1, y1);
    }

    void cut0(double xa, double ya, double xb, double yb) {
        if ((xa < 0) != (xb < 0) && xa != 0 && xb != 0) {
            double t = (0 - xa) / (xb - xa), ym = ya + (yb - ya) * t;
            cutw(std::max(xa, 0.0), ya, 0, ym);
            cutw(0, ym, std::max(xb, 0.0), yb);
            return;
        }
        cutw(std::max(xa, 0.0), ya, std::max(xb, 0.0), yb);
    }

    void cutw(double xa, double ya, double xb, double yb) {
        double W = w;
        if ((xa > W) != (xb > W) && xa != W && xb != W) {
            double t = (W - xa) / (xb - xa), ym = ya + (yb - ya) * t;
            seg(std::min(xa, W), ya, W, ym);
            seg(W, ym, std::min(xb, W), yb);
            return;
        }
        seg(std::min(xa, W), ya, std::min(xb, W), yb);
    }

    void seg(double x0, double y0, double x1, double y1) {
        if (std::fabs(y0 - y1) < 1e-9) return;
        double dir = 1;
        if (y0 > y1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
            dir = -1;
        }
        double dxdy = (x1 - x0) / (y1 - y0), x = x0;
        if (y0 < 0) {
            x -= y0 * dxdy;
            y0 = 0;
        }
        int ystart = (int)y0, yend = std::min(h, (int)std::ceil(y1));
        size_t stride = (size_t)w + 2;
        for (int y = ystart; y < yend; y++) {
            float *row = &a[(size_t)y * stride];
            double dy = std::min((double)(y + 1), y1) - std::max((double)y, y0);
            double xnext = x + dxdy * dy, d = dy * dir;
            double xa = std::min(x, xnext), xb = std::max(x, xnext);
            double x0f = std::floor(xa);
            int x0i = (int)x0f, x1i = (int)std::ceil(xb);
            if (x0i < 0) x0i = 0;
            if (x1i <= x0i + 1) {
                double xmf = 0.5 * (x + xnext) - x0f;
                if (x0i <= w) row[x0i] += (float)(d - d * xmf);
                if (x0i + 1 <= w + 1) row[x0i + 1] += (float)(d * xmf);
            } else {
                double s = 1.0 / (xb - xa), fx0 = xa - x0f;
                double a0 = 0.5 * s * (1 - fx0) * (1 - fx0);
                double fx1 = xb - std::ceil(xb) + 1;
                double am = 0.5 * s * fx1 * fx1;
                row[x0i] += (float)(d * a0);
                if (x1i == x0i + 2) row[x0i + 1] += (float)(d * (1 - a0 - am));
                else {
                    double a1 = s * (1.5 - fx0);
                    row[x0i + 1] += (float)(d * (a1 - a0));
                    for (int xi = x0i + 2; xi < x1i - 1; xi++) row[xi] += (float)(d * s);
                    double a2 = a1 + (x1i - x0i - 3) * s;
                    row[x1i - 1] += (float)(d * (1 - a2 - am));
                }
                if (x1i <= w + 1) row[x1i] += (float)(d * am);
            }
            x = xnext;
        }
    }
};

}  // namespace

void Image::fill(const Path &p, Color c, double alpha, const double *m) {
    if (p.contours_.empty() || w == 0 || h == 0) return;
    // the shape in pixels, and the box it covers (inside the image and the clip)
    std::vector<std::vector<Pt>> pts;
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto &ct : p.contours_) {
        std::vector<Pt> out;
        for (const auto &q : ct) {
            Pt t = m ? Pt{m[0] * q.x + m[2] * q.y + m[4], m[1] * q.x + m[3] * q.y + m[5]} : q;
            if (!std::isfinite(t.x) || !std::isfinite(t.y)) return;
            out.push_back(t);
            minx = std::min(minx, t.x);
            maxx = std::max(maxx, t.x);
            miny = std::min(miny, t.y);
            maxy = std::max(maxy, t.y);
        }
        if (out.size() >= 2) pts.push_back(out);
    }
    if (pts.empty()) return;
    Box b{(int)std::floor(minx), (int)std::floor(miny), (int)std::ceil(maxx) + 1, (int)std::ceil(maxy) + 1};
    int cx0 = 0, cy0 = 0, cx1 = w, cy1 = h;
    if (!clips_.empty()) {
        cx0 = std::max(0, clips_.back()[0]);
        cy0 = std::max(0, clips_.back()[1]);
        cx1 = std::min(w, clips_.back()[2]);
        cy1 = std::min(h, clips_.back()[3]);
    }
    b.x0 = std::max(b.x0, cx0);
    b.y0 = std::max(b.y0, cy0);
    b.x1 = std::min(b.x1, cx1);
    b.y1 = std::min(b.y1, cy1);
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return;
    // edges left of the box still count, so the box starts at the shape's left edge
    int ax0 = std::max((int)std::floor(minx), -1000000);
    ax0 = std::min(ax0, b.x0);
    ax0 = std::max(ax0, b.x0 - 4096);
    Acc acc(b.x1 - ax0, b.y1 - b.y0, ax0, b.y0);
    for (const auto &ct : pts) {
        for (size_t i = 0; i + 1 < ct.size(); i++) acc.line(ct[i].x, ct[i].y, ct[i + 1].x, ct[i + 1].y);
        acc.line(ct.back().x, ct.back().y, ct.front().x, ct.front().y);   // every contour closes itself
    }
    size_t stride = (size_t)acc.w + 2;
    Ink ink(c, alpha);
    for (int y = 0; y < acc.h; y++) {
        double sum = 0;
        const float *row = &acc.a[(size_t)y * stride];
        uint8_t *line = &px[(size_t)(y + b.y0) * (size_t)w * 3];
        int x = 0;
        while (x < acc.w) {
            if (!zero_bits(row[x])) {   // an edge passes here: this pixel on its own
                sum += row[x];
                int ix = x + ax0;
                x++;
                if (ix < b.x0) continue;
                double cov = std::min(1.0, std::fabs(sum));
                if (cov == 1.0) ink.put(line + (size_t)ix * 3);
                else if (cov > 0.002) blend(ix, y + b.y0, c, cov * alpha);
                continue;
            }
            // a run with no edges: the coverage stays the same all the way along
            int e = x + 1;
            while (e < acc.w && zero_bits(row[e])) e++;
            int from = std::max(x + ax0, b.x0), to = e + ax0;
            if (from < to) {
                double cov = std::min(1.0, std::fabs(sum));
                if (cov == 1.0) ink.run(line + (size_t)from * 3, to - from);
                else if (cov > 0.002 && to - from >= 48) {
                    Ink part(c, cov * alpha);   // a long stretch at one strength (often 0.99999994 inside a shape)
                    part.run(line + (size_t)from * 3, to - from);
                } else if (cov > 0.002)
                    for (int ix = from; ix < to; ix++) blend(ix, y + b.y0, c, cov * alpha);
            }
            x = e;
        }
    }
}

Mask coverage(const Path &p, const double *m) {
    Mask out;
    std::vector<std::vector<Pt>> pts;
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto &ct : p.contours_) {
        std::vector<Pt> o;
        for (const auto &q : ct) {
            Pt t = m ? Pt{m[0] * q.x + m[2] * q.y + m[4], m[1] * q.x + m[3] * q.y + m[5]} : q;
            if (!std::isfinite(t.x) || !std::isfinite(t.y)) return out;
            o.push_back(t);
            minx = std::min(minx, t.x);
            maxx = std::max(maxx, t.x);
            miny = std::min(miny, t.y);
            maxy = std::max(maxy, t.y);
        }
        if (o.size() >= 2) pts.push_back(o);
    }
    if (pts.empty() || maxx - minx > 8192 || maxy - miny > 8192) return out;
    out.x0 = (int)std::floor(minx);
    out.y0 = (int)std::floor(miny);
    out.w = (int)std::ceil(maxx) + 1 - out.x0;
    out.h = (int)std::ceil(maxy) + 1 - out.y0;
    Acc acc(out.w, out.h, out.x0, out.y0);
    for (const auto &ct : pts) {
        for (size_t i = 0; i + 1 < ct.size(); i++) acc.line(ct[i].x, ct[i].y, ct[i + 1].x, ct[i + 1].y);
        acc.line(ct.back().x, ct.back().y, ct.front().x, ct.front().y);
    }
    out.a.assign((size_t)out.w * (size_t)out.h, 0);
    size_t stride = (size_t)acc.w + 2;
    for (int y = 0; y < out.h; y++) {
        double sum = 0;
        const float *row = &acc.a[(size_t)y * stride];
        for (int x = 0; x < out.w; x++) {
            sum += row[x];
            out.a[(size_t)y * (size_t)out.w + (size_t)x] = (uint8_t)std::lround(std::min(1.0, std::fabs(sum)) * 255);
        }
    }
    return out;
}

bool Image::clipped(int &x0, int &y0, int &x1, int &y1) const {
    x0 = 0;
    y0 = 0;
    x1 = w;
    y1 = h;
    if (!clips_.empty()) {
        x0 = std::max(0, clips_.back()[0]);
        y0 = std::max(0, clips_.back()[1]);
        x1 = std::min(w, clips_.back()[2]);
        y1 = std::min(h, clips_.back()[3]);
    }
    return x0 < x1 && y0 < y1;
}

void Image::mask(const Mask &m, int dx, int dy, Color c, double alpha) {
    int cx0, cy0, cx1, cy1;
    if (!clipped(cx0, cy0, cx1, cy1)) return;
    int ox = m.x0 + dx, oy = m.y0 + dy;
    int x0 = std::max(cx0, ox), y0 = std::max(cy0, oy), x1 = std::min(cx1, ox + m.w), y1 = std::min(cy1, oy + m.h);
    uint8_t cs[3] = {(uint8_t)(std::max(0.0, std::min(1.0, c.r)) * 255 + 0.5), (uint8_t)(std::max(0.0, std::min(1.0, c.g)) * 255 + 0.5),
                     (uint8_t)(std::max(0.0, std::min(1.0, c.b)) * 255 + 0.5)};
    int al = (int)(std::max(0.0, std::min(1.0, alpha)) * 256);
    for (int y = y0; y < y1; y++) {
        const uint8_t *a = &m.a[(size_t)(y - oy) * (size_t)m.w + (size_t)(x0 - ox)];
        uint8_t *p = &px[((size_t)y * (size_t)w + (size_t)x0) * 3];
        for (int x = x0; x < x1; x++, a++, p += 3) {
            int k = (*a * al) >> 8;
            if (!k) continue;
            if (k >= 255) {
                p[0] = cs[0];
                p[1] = cs[1];
                p[2] = cs[2];
                continue;
            }
            for (int i = 0; i < 3; i++) p[i] = (uint8_t)((p[i] * (255 - k) + cs[i] * k + 127) / 255);
        }
    }
}

void Image::mask_gradient(const Mask &m, int dx, int dy, Color top, Color bottom, double gy0, double gy1, double alpha) {
    int cx0, cy0, cx1, cy1;
    if (!clipped(cx0, cy0, cx1, cy1)) return;
    int ox = m.x0 + dx, oy = m.y0 + dy;
    int x0 = std::max(cx0, ox), y0 = std::max(cy0, oy), x1 = std::min(cx1, ox + m.w), y1 = std::min(cy1, oy + m.h);
    double span = std::max(1e-6, gy1 - gy0);
    for (int y = y0; y < y1; y++) {
        double t = std::max(0.0, std::min(1.0, (y + 0.5 - gy0) / span));
        Color c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t};
        const uint8_t *a = &m.a[(size_t)(y - oy) * (size_t)m.w + (size_t)(x0 - ox)];
        uint8_t *line = &px[(size_t)y * (size_t)w * 3];
        for (int x = x0; x < x1;) {
            int e = x + 1;   // a stretch of the same strength (the inside of the shape)
            while (e < x1 && a[e - x0] == a[x - x0]) e++;
            uint8_t v = a[x - x0];
            if (v && e - x >= 16) {
                Ink ink(c, v / 255.0 * alpha);
                ink.run(line + (size_t)x * 3, e - x);
            } else if (v) {
                for (int i = x; i < e; i++) blend(i, y, c, v / 255.0 * alpha);
            }
            x = e;
        }
    }
}

void Image::blit(const Image &src, int x, int y, int sx, int sy, int sw, int sh) {
    if (sw < 0) sw = src.w - sx;
    if (sh < 0) sh = src.h - sy;
    int cx0, cy0, cx1, cy1;
    if (!clipped(cx0, cy0, cx1, cy1)) return;
    int x0 = std::max(cx0, x), y0 = std::max(cy0, y), x1 = std::min({cx1, x + sw}), y1 = std::min({cy1, y + sh});
    if (sx < 0 || sy < 0 || sx + sw > src.w || sy + sh > src.h) return;
    for (int yy = y0; yy < y1; yy++)
        if (x1 > x0)
            std::memcpy(&px[((size_t)yy * (size_t)w + (size_t)x0) * 3], &src.px[((size_t)(yy - y + sy) * (size_t)src.w + (size_t)(x0 - x + sx)) * 3],
                        (size_t)(x1 - x0) * 3);
}

void Image::stroke(const std::vector<Pt> &pts0, double width, Color c, bool round, bool closed, const double *m) {
    std::vector<Pt> pts;
    for (const auto &q : pts0) pts.push_back(m ? Pt{m[0] * q.x + m[2] * q.y + m[4], m[1] * q.x + m[3] * q.y + m[5]} : q);
    if (closed && !pts.empty()) pts.push_back(pts.front());
    if (pts.size() < 2) return;
    double hw = std::max(width, 0.35) / 2;   // a hairline still shows
    Path p;
    auto add = [&](std::vector<Pt> poly) {
        double area = 0;
        for (size_t i = 0; i < poly.size(); i++) {
            const Pt &a = poly[i], &b = poly[(i + 1) % poly.size()];
            area += a.x * b.y - b.x * a.y;
        }
        if (area < 0) std::reverse(poly.begin(), poly.end());   // all one way round, so overlaps add up
        p.move(poly[0].x, poly[0].y);
        for (size_t i = 1; i < poly.size(); i++) p.line(poly[i].x, poly[i].y);
        p.close();
    };
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        double dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y, len = std::hypot(dx, dy);
        if (len < 1e-9) continue;
        double nx = -dy / len * hw, ny = dx / len * hw;
        add({{pts[i].x + nx, pts[i].y + ny}, {pts[i + 1].x + nx, pts[i + 1].y + ny}, {pts[i + 1].x - nx, pts[i + 1].y - ny}, {pts[i].x - nx, pts[i].y - ny}});
    }
    // joins: round ones, or (miter-ish) a small round patch so corners don't show a notch
    for (size_t i = 1; i + 1 < pts.size() + (closed ? 1 : 0) && i < pts.size(); i++) {
        if (!round && hw < 1.2) continue;
        std::vector<Pt> circle;
        for (int k = 0; k < 16; k++) circle.push_back({pts[i].x + hw * std::cos(k * M_PI / 8), pts[i].y + hw * std::sin(k * M_PI / 8)});
        add(circle);
    }
    Image::fill(p, c, 1, nullptr);
}

void Image::rect(double x, double y, double rw, double rh, Color c, double alpha) {
    // upright boxes are most of a slide: each pixel's share is (share across) x (share down)
    if (!(rw > 0) || !(rh > 0) || !std::isfinite(x + y + rw + rh)) return;
    int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
    int x1 = std::min(w, (int)std::ceil(x + rw)), y1 = std::min(h, (int)std::ceil(y + rh));
    if (!clips_.empty()) {
        x0 = std::max(x0, clips_.back()[0]);
        y0 = std::max(y0, clips_.back()[1]);
        x1 = std::min(x1, clips_.back()[2]);
        y1 = std::min(y1, clips_.back()[3]);
    }
    if (x0 >= x1 || y0 >= y1) return;
    std::vector<double> cx((size_t)(x1 - x0));
    for (int px_ = x0; px_ < x1; px_++) cx[(size_t)(px_ - x0)] = std::min((double)px_ + 1, x + rw) - std::max((double)px_, x);
    // the columns fully inside (share across = 1), as one run
    int in0 = x0, in1 = x1;
    while (in0 < x1 && cx[(size_t)(in0 - x0)] != 1.0) in0++;
    while (in1 > in0 && cx[(size_t)(in1 - 1 - x0)] != 1.0) in1--;
    for (int i = in0; i < in1; i++)
        if (cx[(size_t)(i - x0)] != 1.0) in0 = in1;   // not one solid stretch: go pixel by pixel
    Ink ink(c, alpha);
    for (int py = y0; py < y1; py++) {
        double cy = std::min((double)py + 1, y + rh) - std::max((double)py, y);
        uint8_t *row = &px[(size_t)py * (size_t)w * 3];
        for (int px_ = x0; px_ < x1; px_++) {
            if (cy == 1.0 && px_ == in0 && in0 < in1) {
                ink.run(row + (size_t)px_ * 3, in1 - in0);
                px_ = in1 - 1;
                continue;
            }
            double cov = cx[(size_t)(px_ - x0)] * cy;
            if (cov == 1.0) ink.put(row + (size_t)px_ * 3);
            else if (cov > 0.002) blend(px_, py, c, cov * alpha);
        }
    }
}

void Image::picture(const uint8_t *rgb, const uint8_t *alpha, int sw, int sh, int channels, double x, double y, double dw, double dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
    int x1 = std::min(w, (int)std::ceil(x + dw)), y1 = std::min(h, (int)std::ceil(y + dh));
    if (!clips_.empty()) {
        x0 = std::max(x0, clips_.back()[0]);
        y0 = std::max(y0, clips_.back()[1]);
        x1 = std::min(x1, clips_.back()[2]);
        y1 = std::min(y1, clips_.back()[3]);
    }
    for (int py = y0; py < y1; py++)
        for (int pxx = x0; pxx < x1; pxx++) {
            // average the source pixels under this one (good for shrinking a big logo)
            double sx0 = (pxx - x) / dw * sw, sx1 = (pxx + 1 - x) / dw * sw, sy0 = (py - y) / dh * sh, sy1 = (py + 1 - y) / dh * sh;
            int ix0 = std::max(0, (int)std::floor(sx0)), ix1 = std::min(sw, std::max(ix0 + 1, (int)std::ceil(sx1)));
            int iy0 = std::max(0, (int)std::floor(sy0)), iy1 = std::min(sh, std::max(iy0 + 1, (int)std::ceil(sy1)));
            double r = 0, g = 0, bl = 0, a = 0;
            int n = 0;
            for (int sy = iy0; sy < iy1; sy++)
                for (int sx = ix0; sx < ix1; sx++) {
                    size_t i = (size_t)sy * (size_t)sw + (size_t)sx;
                    double aa = alpha ? alpha[i] / 255.0 : 1;
                    const uint8_t *s = rgb + i * (size_t)channels;
                    double cr = s[0] / 255.0, cg = channels >= 3 ? s[1] / 255.0 : cr, cb = channels >= 3 ? s[2] / 255.0 : cr;
                    r += cr * aa;
                    g += cg * aa;
                    bl += cb * aa;
                    a += aa;
                    n++;
                }
            if (!n || a <= 0) continue;
            blend(pxx, py, {r / a, g / a, bl / a}, a / n);
        }
}

std::string Image::png() const {
    auto be32 = [](uint32_t v) {
        std::string s;
        for (int i = 3; i >= 0; i--) s += (char)((v >> (i * 8)) & 0xFF);
        return s;
    };
    auto chunk = [&](const std::string &t, const std::string &d) { return be32((uint32_t)d.size()) + t + d + be32(zlib::crc32(t + d)); };
    std::string rows;
    rows.reserve(((size_t)w * 3 + 1) * (size_t)h);
    for (int y = 0; y < h; y++) {
        rows += '\0';
        rows.append((const char *)&px[(size_t)y * (size_t)w * 3], (size_t)w * 3);
    }
    std::string ihdr = be32((uint32_t)w) + be32((uint32_t)h) + std::string(1, 8) + std::string(1, 2) + std::string(3, '\0');
    return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", zlib::compress(rows)) + chunk("IEND", "");
}

// ============================================================================= font outlines

namespace {
uint32_t U8(const std::string &d, size_t o) { return o < d.size() ? (unsigned char)d[o] : 0; }
uint32_t U16(const std::string &d, size_t o) { return (U8(d, o) << 8) | U8(d, o + 1); }
int I16(const std::string &d, size_t o) { return (int16_t)U16(d, o); }
uint32_t U32(const std::string &d, size_t o) { return (U16(d, o) << 16) | U16(d, o + 2); }
}  // namespace

std::shared_ptr<Outlines> Outlines::load(const std::string &path, int index) {
    auto data = std::make_shared<std::string>();
    if (!files::read(path, *data)) return nullptr;
    std::vector<uint32_t> offs;
    try {
        offs = ttf::faces_in(*data);
    } catch (...) {
        return nullptr;
    }
    if (offs.empty()) return nullptr;
    return from_data(data, offs[std::min((size_t)std::max(0, index), offs.size() - 1)]);
}

std::shared_ptr<Outlines> Outlines::from_data(std::shared_ptr<const std::string> data, uint32_t offset) {
    auto o = std::shared_ptr<Outlines>(new Outlines());
    o->data_ = data;
    o->off_ = offset;
    try {
        if (!o->parse()) return nullptr;
    } catch (...) {   // a damaged font file is simply not used
        return nullptr;
    }
    return o;
}

bool Outlines::parse() {
    const std::string &d = *data_;
    uint32_t n = U16(d, off_ + 4);
    size_t head = 0, hhea = 0, maxp = 0;
    for (uint32_t i = 0; i < n; i++) {
        size_t e = off_ + 12 + 16 * (size_t)i;
        if (e + 16 > d.size()) return false;
        std::string tag = d.substr(e, 4);
        size_t at = U32(d, e + 8), len = U32(d, e + 12);
        if (at + len > d.size()) return false;
        if (tag == "cmap") cmap_ = at;
        else if (tag == "glyf") glyf_ = at;
        else if (tag == "loca") loca_ = at;
        else if (tag == "hmtx") hmtx_ = at;
        else if (tag == "head") head = at;
        else if (tag == "hhea") hhea = at;
        else if (tag == "maxp") maxp = at;
        else if (tag == "CFF ") {
            cff_at_ = at;
            cff_len_ = len;
        }
    }
    if (!head || !hhea || !maxp || !cmap_ || !hmtx_) return false;
    upm_ = U16(d, head + 18) ? U16(d, head + 18) : 1000;
    loca_long_ = I16(d, head + 50);
    ascent_ = I16(d, hhea + 4);
    descent_ = I16(d, hhea + 6);
    num_hmetrics_ = U16(d, hhea + 34);
    num_glyphs_ = U16(d, maxp + 4);
    cff_ = !glyf_ && cff_at_;
    if (!cff_ && (!glyf_ || !loca_)) return false;
    if (cff_ && !parse_cff()) return false;
    cache_.resize(num_glyphs_);
    cached_.assign(num_glyphs_, false);
    // the codepoint -> glyph table (format 12 or 4), searched with a binary search later
    size_t best = 0;
    int rank = 99;
    uint32_t count = U16(d, cmap_ + 2);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = cmap_ + 4 + 8 * (size_t)i;
        uint32_t plat = U16(d, e), enc = U16(d, e + 2);
        size_t at = cmap_ + U32(d, e + 4);
        uint32_t fmt = U16(d, at);
        int r = (plat == 3 && enc == 10 && fmt == 12) ? 0 : (plat == 0 && fmt == 12) ? 1 : (plat == 3 && enc == 1 && fmt == 4) ? 2 : (plat == 0 && fmt == 4) ? 3
              : (plat == 3 && enc == 0 && fmt == 4) ? 4 : 99;
        if (r < rank) {
            rank = r;
            best = at;
        }
    }
    if (!best) return true;
    uint32_t fmt = U16(d, best);
    if (fmt == 12) {   // ranges of characters, kept as ranges (a damaged one can't blow up)
        uint32_t groups = U32(d, best + 12);
        for (uint32_t i = 0; i < groups && i < 200000 && best + 16 + 12 * (size_t)i + 12 <= d.size(); i++) {
            size_t e = best + 16 + 12 * (size_t)i;
            uint32_t s0 = U32(d, e), en = U32(d, e + 4), g = U32(d, e + 8);
            if (en < s0 || s0 > 0x10FFFF) continue;
            groups_.push_back({s0, std::min(en, 0x10FFFFu), g});
        }
        std::stable_sort(groups_.begin(), groups_.end(), [](const std::array<uint32_t, 3> &x, const std::array<uint32_t, 3> &y) { return x[0] < y[0]; });
    } else if (fmt == 4) {   // the 16-bit characters: a table of all of them
        bmp_.assign(0x10000, 0);
        uint32_t seg = U16(d, best + 6) / 2;
        size_t ends = best + 14, starts = best + 16 + 2 * (size_t)seg, deltas = starts + 2 * (size_t)seg, ranges = starts + 4 * (size_t)seg;
        size_t work = 0;
        for (uint32_t i = 0; i < seg && work < (1u << 20); i++) {
            uint32_t en = U16(d, ends + 2 * i), st = U16(d, starts + 2 * i), delta = U16(d, deltas + 2 * i), ro = U16(d, ranges + 2 * i);
            if (st == 0xFFFF) break;
            for (uint32_t c = st; c <= en && work < (1u << 20); c++, work++) {
                uint32_t g;
                if (ro == 0) g = (c + delta) & 0xFFFF;
                else {
                    size_t p = ranges + 2 * (size_t)i + ro + 2 * (size_t)(c - st);
                    g = U16(d, p);
                    if (g) g = (g + delta) & 0xFFFF;
                }
                uint32_t cp = rank == 4 && c >= 0xF000 && c <= 0xF0FF ? c - 0xF000 : c;   // symbol fonts
                if (g && !bmp_[cp]) bmp_[cp] = g;
            }
        }
    }
    return true;
}

uint32_t Outlines::glyph_index(uint32_t cp) const {
    if (!bmp_.empty()) return cp < 0x10000 ? bmp_[cp] : 0;
    // the last range starting at or before cp
    auto it = std::upper_bound(groups_.begin(), groups_.end(), cp, [](uint32_t c, const std::array<uint32_t, 3> &g) { return c < g[0]; });
    if (it == groups_.begin()) return 0;
    --it;
    return cp <= (*it)[1] ? (*it)[2] + (cp - (*it)[0]) : 0;
}

double Outlines::advance_of(uint32_t g) const {
    if (!num_hmetrics_) return upm_ / 2;
    uint32_t i = std::min(g, num_hmetrics_ - 1);
    return U16(*data_, hmtx_ + 4 * (size_t)i);
}

const Glyph &Outlines::glyph(uint32_t index) {
    static const Glyph EMPTY;
    if (index >= num_glyphs_) return EMPTY;
    if (!cached_[index]) {
        Glyph g;
        g.advance = advance_of(index);
        if (cff_) cff_outline(index, g.path);
        else {
            double m[6] = {1, 0, 0, 1, 0, 0};
            work_ = 0;
            glyf_outline(index, g.path, 0, m);
            if (work_ > 100000) g.path = Path();   // a damaged font: no glyph rather than a hang
        }
        cache_[index] = std::move(g);
        cached_[index] = true;
    }
    return cache_[index];
}

void Outlines::glyf_outline(uint32_t index, Path &p, int depth, double m[6]) {
    const std::string &d = *data_;
    if (depth > 8 || index >= num_glyphs_ || ++work_ > 100000) return;
    size_t a, b;
    if (loca_long_) {
        a = U32(d, loca_ + 4 * (size_t)index);
        b = U32(d, loca_ + 4 * (size_t)index + 4);
    } else {
        a = (size_t)U16(d, loca_ + 2 * (size_t)index) * 2;
        b = (size_t)U16(d, loca_ + 2 * (size_t)index + 2) * 2;
    }
    if (b <= a) return;
    size_t g = glyf_ + a;
    if (g + 10 > d.size()) return;
    int nc = I16(d, g);
    auto tx = [&](double x, double y) { return Pt{m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]}; };
    if (nc >= 0) {
        std::vector<uint32_t> ends;
        for (int i = 0; i < nc; i++) ends.push_back(U16(d, g + 10 + 2 * (size_t)i));
        if (ends.empty()) return;
        uint32_t npts = ends.back() + 1;
        if (npts > 65535) return;
        size_t pp = g + 10 + 2 * (size_t)nc;
        pp += 2 + U16(d, pp);   // instructions
        std::vector<uint8_t> flags;
        while (flags.size() < npts && pp < d.size()) {
            uint8_t f = (uint8_t)U8(d, pp++);
            flags.push_back(f);
            if (f & 8) {
                uint32_t rep = U8(d, pp++);
                for (uint32_t r = 0; r < rep && flags.size() < npts; r++) flags.push_back(f);
            }
        }
        if (flags.size() < npts) return;
        work_ += npts;
        if (work_ > 100000) return;
        std::vector<int> xs(npts), ys(npts);
        int v = 0;
        for (uint32_t i = 0; i < npts; i++) {
            uint8_t f = flags[i];
            if (f & 2) {
                int dx = (int)U8(d, pp++);
                v += (f & 16) ? dx : -dx;
            } else if (!(f & 16)) {
                v += I16(d, pp);
                pp += 2;
            }
            xs[i] = v;
        }
        v = 0;
        for (uint32_t i = 0; i < npts; i++) {
            uint8_t f = flags[i];
            if (f & 4) {
                int dy = (int)U8(d, pp++);
                v += (f & 32) ? dy : -dy;
            } else if (!(f & 32)) {
                v += I16(d, pp);
                pp += 2;
            }
            ys[i] = v;
        }
        uint32_t start = 0;
        for (uint32_t e : ends) {
            if (e < start || e >= npts) return;
            uint32_t n = e - start + 1;
            auto on = [&](uint32_t k) { return (flags[start + k % n] & 1) != 0; };
            auto P = [&](uint32_t k) { return Pt{(double)xs[start + k % n], (double)ys[start + k % n]}; };
            // start on an on-curve point (or the middle of two off-curve ones)
            uint32_t s0 = 0;
            while (s0 < n && !on(s0)) s0++;
            Pt first;
            if (s0 == n) {
                Pt a0 = P(0), a1 = P(1);
                first = {(a0.x + a1.x) / 2, (a0.y + a1.y) / 2};
                s0 = 0;
            } else first = P(s0);
            Pt t = tx(first.x, first.y);
            p.move(t.x, t.y);
            bool pending = false;
            Pt ctrl{0, 0};
            for (uint32_t k = 1; k <= n; k++) {
                uint32_t idx = s0 + k;
                Pt q = P(idx);
                if (on(idx)) {
                    Pt tq = tx(q.x, q.y);
                    if (pending) {
                        Pt tc = tx(ctrl.x, ctrl.y);
                        p.quad(tc.x, tc.y, tq.x, tq.y);
                    } else p.line(tq.x, tq.y);
                    pending = false;
                } else {
                    if (pending) {
                        Pt mid{(ctrl.x + q.x) / 2, (ctrl.y + q.y) / 2};
                        Pt tc = tx(ctrl.x, ctrl.y), tm = tx(mid.x, mid.y);
                        p.quad(tc.x, tc.y, tm.x, tm.y);
                    }
                    ctrl = q;
                    pending = true;
                }
            }
            if (pending) {
                Pt tc = tx(ctrl.x, ctrl.y), tf = tx(first.x, first.y);
                p.quad(tc.x, tc.y, tf.x, tf.y);
            }
            p.close();
            start = e + 1;
        }
        return;
    }
    // a composite glyph: other glyphs, moved and scaled
    size_t pp = g + 10;
    for (int guard = 0; guard < 64; guard++) {
        uint32_t flags = U16(d, pp), gi = U16(d, pp + 2);
        pp += 4;
        double dx, dy;
        if (flags & 1) {
            dx = (flags & 2) ? I16(d, pp) : 0;
            dy = (flags & 2) ? I16(d, pp + 2) : 0;
            pp += 4;
        } else {
            dx = (flags & 2) ? (int8_t)U8(d, pp) : 0;
            dy = (flags & 2) ? (int8_t)U8(d, pp + 1) : 0;
            pp += 2;
        }
        double a = 1, b = 0, c = 0, e = 1;
        if (flags & 8) {
            a = e = I16(d, pp) / 16384.0;
            pp += 2;
        } else if (flags & 0x40) {
            a = I16(d, pp) / 16384.0;
            e = I16(d, pp + 2) / 16384.0;
            pp += 4;
        } else if (flags & 0x80) {
            a = I16(d, pp) / 16384.0;
            b = I16(d, pp + 2) / 16384.0;
            c = I16(d, pp + 4) / 16384.0;
            e = I16(d, pp + 6) / 16384.0;
            pp += 8;
        }
        double cm[6] = {m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * e, m[1] * c + m[3] * e, m[0] * dx + m[2] * dy + m[4],
                        m[1] * dx + m[3] * dy + m[5]};
        glyf_outline(gi, p, depth + 1, cm);
        if (!(flags & 0x20)) break;
    }
}

// ----------------------------------------------------------------------------- CFF (OpenType)

namespace {

// a CFF INDEX at o: its items as (offset, length); returns the offset just past it
size_t cff_index(const std::string &d, size_t o, std::vector<std::pair<size_t, size_t>> &items) {
    items.clear();
    uint32_t count = U16(d, o);
    if (!count) return o + 2;
    uint32_t osize = U8(d, o + 2);
    if (osize < 1 || osize > 4) return d.size();
    size_t base = o + 3 + (size_t)(count + 1) * osize - 1;
    auto off = [&](uint32_t i) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < osize; k++) v = (v << 8) | U8(d, o + 3 + (size_t)i * osize + k);
        return (size_t)v;
    };
    for (uint32_t i = 0; i < count; i++) {
        size_t a = off(i), b = off(i + 1);
        if (b < a || base + b > d.size()) return d.size();
        items.push_back({base + a, b - a});
    }
    return base + off(count);
}

// a DICT: operator -> operands
std::vector<std::pair<int, std::vector<double>>> cff_dict(const std::string &d, size_t o, size_t len) {
    std::vector<std::pair<int, std::vector<double>>> out;
    std::vector<double> st;
    size_t end = std::min(d.size(), o + len);
    while (o < end) {
        uint32_t b = U8(d, o);
        if (b <= 21) {
            int op = (int)b;
            o++;
            if (b == 12) op = 1200 + (int)U8(d, o++);
            out.push_back({op, st});
            st.clear();
        } else if (b == 28) {
            st.push_back(I16(d, o + 1));
            o += 3;
        } else if (b == 29) {
            st.push_back((int32_t)U32(d, o + 1));
            o += 5;
        } else if (b == 30) {   // a real number, nibble by nibble
            std::string s;
            o++;
            bool done = false;
            while (o < end && !done) {
                uint32_t byte = U8(d, o++);
                for (int k = 0; k < 2 && !done; k++) {
                    uint32_t nib = k ? byte & 15 : byte >> 4;
                    if (nib <= 9) s += (char)('0' + nib);
                    else if (nib == 10) s += '.';
                    else if (nib == 11) s += 'E';
                    else if (nib == 12) s += "E-";
                    else if (nib == 14) s += '-';
                    else if (nib == 15) done = true;
                }
            }
            st.push_back(std::atof(s.c_str()));
        } else if (b >= 32 && b <= 246) {
            st.push_back((int)b - 139);
            o++;
        } else if (b >= 247 && b <= 250) {
            st.push_back(((int)b - 247) * 256 + (int)U8(d, o + 1) + 108);
            o += 2;
        } else if (b >= 251 && b <= 254) {
            st.push_back(-((int)b - 251) * 256 - (int)U8(d, o + 1) - 108);
            o += 2;
        } else o++;
    }
    return out;
}

const std::vector<double> *dict_get(const std::vector<std::pair<int, std::vector<double>>> &dict, int op) {
    for (const auto &e : dict)
        if (e.first == op) return e.second.empty() ? nullptr : &e.second;   // an operator with no numbers: as if missing
    return nullptr;
}

}  // namespace

// a number from a CFF dictionary used as a size or offset: nonsense becomes "nothing there"
static size_t off(double v) { return std::isfinite(v) && v >= 0 && v < 4e9 ? (size_t)v : (size_t)1 << 40; }

bool Outlines::parse_cff() {
    const std::string &d = *data_;
    size_t base = cff_at_;
    size_t hdr = U8(d, base + 2);
    std::vector<std::pair<size_t, size_t>> names, tops, strings;
    size_t p = cff_index(d, base + hdr, names);
    p = cff_index(d, p, tops);
    p = cff_index(d, p, strings);
    p = cff_index(d, p, gsubrs_);
    if (tops.empty()) return false;
    auto top = cff_dict(d, tops[0].first, tops[0].second);
    const auto *cs = dict_get(top, 17);
    if (!cs || cs->empty()) return false;
    cff_index(d, base + off(cs->at(0)), charstrings_);
    auto load_private = [&](const std::vector<double> *priv, std::vector<std::pair<size_t, size_t>> &subrs) {
        subrs.clear();
        if (!priv || priv->size() < 2) return;
        size_t size = off(priv->at(0)), at = base + off(priv->at(1));
        auto pd = cff_dict(d, at, size);
        if (const auto *s = dict_get(pd, 19)) cff_index(d, at + off(s->at(0)), subrs);
    };
    load_private(dict_get(top, 18), lsubrs_);
    if (const auto *fda = dict_get(top, 1236)) {   // a CID font: each glyph picks its own private dict
        std::vector<std::pair<size_t, size_t>> fds;
        cff_index(d, base + off(fda->at(0)), fds);
        for (const auto &fd : fds) {
            auto fdict = cff_dict(d, fd.first, fd.second);
            fd_lsubrs_.emplace_back();
            load_private(dict_get(fdict, 18), fd_lsubrs_.back());
        }
        if (const auto *fs = dict_get(top, 1237)) {
            size_t at = base + off(fs->at(0));
            uint32_t fmt = U8(d, at);
            fd_select_.assign(charstrings_.size(), 0);
            if (fmt == 0)
                for (size_t g = 0; g < charstrings_.size(); g++) fd_select_[g] = (uint8_t)U8(d, at + 1 + g);
            else if (fmt == 3) {
                uint32_t nr = U16(d, at + 1);
                for (uint32_t r = 0; r < nr; r++) {
                    uint32_t first = U16(d, at + 3 + 3 * (size_t)r), fd = U8(d, at + 5 + 3 * (size_t)r), next = U16(d, at + 6 + 3 * (size_t)r);
                    for (uint32_t g = first; g < next && g < fd_select_.size(); g++) fd_select_[g] = (uint8_t)fd;
                }
            }
        }
    }
    return true;
}

void Outlines::cff_outline(uint32_t index, Path &p) {
    if (index >= charstrings_.size()) return;
    const std::string &d = *data_;
    const auto *lsubrs = &lsubrs_;
    if (!fd_lsubrs_.empty() && index < fd_select_.size() && fd_select_[index] < fd_lsubrs_.size()) lsubrs = &fd_lsubrs_[fd_select_[index]];
    auto bias = [](size_t n) { return n < 1240 ? 107 : n < 33900 ? 1131 : 32768; };
    std::vector<double> st;
    double x = 0, y = 0;
    int nstems = 0;
    bool open = false, have_width = false;
    auto moveto = [&](double nx, double ny) {
        if (open) p.close();
        x = nx;
        y = ny;
        p.move(x, y);
        open = true;
    };
    auto lineto = [&](double nx, double ny) {
        x = nx;
        y = ny;
        p.line(x, y);
    };
    auto curveto = [&](double a, double b, double c, double e, double f, double g) {
        double x1 = x + a, y1 = y + b, x2 = x1 + c, y2 = y1 + e;
        x = x2 + f;
        y = y2 + g;
        p.cubic(x1, y1, x2, y2, x, y);
    };
    int calls = 0;
    std::function<bool(size_t, size_t, int)> run = [&](size_t at, size_t len, int depth) -> bool {
        if (depth > 10 || ++calls > 2000) return false;
        size_t o = at, end = std::min(d.size(), at + len);
        while (o < end) {
            uint32_t b = U8(d, o);
            if (b >= 32 || b == 28) {
                if (b == 28) {
                    st.push_back(I16(d, o + 1));
                    o += 3;
                } else if (b <= 246) {
                    st.push_back((int)b - 139);
                    o++;
                } else if (b <= 250) {
                    st.push_back(((int)b - 247) * 256 + (int)U8(d, o + 1) + 108);
                    o += 2;
                } else if (b <= 254) {
                    st.push_back(-((int)b - 251) * 256 - (int)U8(d, o + 1) - 108);
                    o += 2;
                } else {
                    st.push_back((int32_t)U32(d, o + 1) / 65536.0);
                    o += 5;
                }
                if (st.size() > 96) return false;
                continue;
            }
            o++;
            auto width = [&](size_t want_even_or_n, bool parity) {
                // the first stack-clearing operator may carry the advance width first
                if (!have_width) {
                    have_width = true;
                    if (parity ? st.size() % 2 == 1 : st.size() > want_even_or_n) st.erase(st.begin());
                }
            };
            switch (b) {
                case 1: case 3: case 18: case 23:   // hstem vstem hstemhm vstemhm
                    width(0, true);
                    nstems += (int)st.size() / 2;
                    st.clear();
                    break;
                case 19: case 20:   // hintmask cntrmask
                    width(0, true);
                    nstems += (int)st.size() / 2;
                    st.clear();
                    o += (size_t)(nstems + 7) / 8;
                    break;
                case 21:
                    width(2, false);
                    if (st.size() >= 2) moveto(x + st[0], y + st[1]);
                    st.clear();
                    break;
                case 22:
                    width(1, false);
                    if (!st.empty()) moveto(x + st[0], y);
                    st.clear();
                    break;
                case 4:
                    width(1, false);
                    if (!st.empty()) moveto(x, y + st[0]);
                    st.clear();
                    break;
                case 5:
                    for (size_t i = 0; i + 1 < st.size(); i += 2) lineto(x + st[i], y + st[i + 1]);
                    st.clear();
                    break;
                case 6: case 7: {
                    bool horiz = b == 6;
                    for (size_t i = 0; i < st.size(); i++, horiz = !horiz) {
                        if (horiz) lineto(x + st[i], y);
                        else lineto(x, y + st[i]);
                    }
                    st.clear();
                    break;
                }
                case 8:
                    for (size_t i = 0; i + 5 < st.size(); i += 6) curveto(st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]);
                    st.clear();
                    break;
                case 24: {   // rcurveline
                    size_t i = 0;
                    for (; i + 7 < st.size(); i += 6) curveto(st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]);
                    if (i + 1 < st.size()) lineto(x + st[i], y + st[i + 1]);
                    st.clear();
                    break;
                }
                case 25: {   // rlinecurve
                    size_t i = 0;
                    for (; i + 7 < st.size(); i += 2) lineto(x + st[i], y + st[i + 1]);
                    if (i + 5 < st.size()) curveto(st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]);
                    st.clear();
                    break;
                }
                case 26: {   // vvcurveto
                    size_t i = 0;
                    double dx1 = 0;
                    if (st.size() % 4 == 1) dx1 = st[i++];
                    for (; i + 3 < st.size(); i += 4) {
                        curveto(dx1, st[i], st[i + 1], st[i + 2], 0, st[i + 3]);
                        dx1 = 0;
                    }
                    st.clear();
                    break;
                }
                case 27: {   // hhcurveto
                    size_t i = 0;
                    double dy1 = 0;
                    if (st.size() % 4 == 1) dy1 = st[i++];
                    for (; i + 3 < st.size(); i += 4) {
                        curveto(st[i], dy1, st[i + 1], st[i + 2], st[i + 3], 0);
                        dy1 = 0;
                    }
                    st.clear();
                    break;
                }
                case 30: case 31: {   // vhcurveto hvcurveto
                    bool h = b == 31;
                    size_t i = 0;
                    while (i + 3 < st.size()) {
                        double extra = st.size() - i == 5 ? st[i + 4] : 0;   // the last curve may end off the axis
                        if (h) curveto(st[i], 0, st[i + 1], st[i + 2], extra, st[i + 3]);
                        else curveto(0, st[i], st[i + 1], st[i + 2], st[i + 3], extra);
                        h = !h;
                        i += 4;
                    }
                    st.clear();
                    break;
                }
                case 10: case 29: {   // callsubr callgsubr
                    if (st.empty()) return false;
                    const auto &subrs = b == 10 ? *lsubrs : gsubrs_;
                    long n = (long)st.back() + bias(subrs.size());
                    st.pop_back();
                    if (n < 0 || n >= (long)subrs.size()) return false;
                    if (!run(subrs[(size_t)n].first, subrs[(size_t)n].second, depth + 1)) return false;
                    break;
                }
                case 11: return true;   // return
                case 14:                 // endchar
                    width(0, true);
                    if (open) p.close();
                    open = false;
                    st.clear();
                    return false;
                case 12: {
                    uint32_t e = U8(d, o++);
                    if (e == 35 && st.size() >= 13) {   // flex
                        curveto(st[0], st[1], st[2], st[3], st[4], st[5]);
                        curveto(st[6], st[7], st[8], st[9], st[10], st[11]);
                    } else if (e == 34 && st.size() >= 7) {   // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6
                        curveto(st[0], 0, st[1], st[2], st[3], 0);
                        curveto(st[4], 0, st[5], -st[2], st[6], 0);
                    } else if (e == 36 && st.size() >= 9) {   // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6
                        curveto(st[0], st[1], st[2], st[3], st[4], 0);
                        curveto(st[5], 0, st[6], st[7], st[8], -(st[1] + st[3] + st[7]));
                    } else if (e == 37 && st.size() >= 11) {   // flex1: five points, then d6 along the longer way
                        double dx = st[0] + st[2] + st[4] + st[6] + st[8], dy = st[1] + st[3] + st[5] + st[7] + st[9];
                        curveto(st[0], st[1], st[2], st[3], st[4], st[5]);
                        if (std::fabs(dx) > std::fabs(dy)) curveto(st[6], st[7], st[8], st[9], st[10], -dy);
                        else curveto(st[6], st[7], st[8], st[9], -dx, st[10]);
                    }
                    st.clear();
                    break;
                }
                default: st.clear(); break;
            }
        }
        return true;
    };
    run(charstrings_[index].first, charstrings_[index].second, 0);
    if (open) p.close();
}

}  // namespace raster
