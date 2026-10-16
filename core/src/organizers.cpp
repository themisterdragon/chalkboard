#include "organizers.h"

#include <algorithm>
#include <cmath>

#include "store.h"
#include "text.h"

#ifndef M_PI   // Windows leaves it out
#define M_PI 3.14159265358979323846
#endif

namespace organizers {

using json::Value;

static Value rgb(double r, double g, double b) {
    Value a = Value::array();
    a.push(r);
    a.push(g);
    a.push(b);
    return a;
}

static const double HEAD_FILL[3] = {0.86, 0.86, 0.86};

int clamp(const Value &v, int lo, int hi) {
    long x = lo;
    if (v.is_int() || v.is_bool()) x = (long)v.as_int();
    else if (v.is_float() && std::isfinite(v.as_double())) x = (long)v.as_double();
    else if (v.is_string()) {
        std::string t = text::strip(v.str());
        char *end = nullptr;
        long y = std::strtol(t.c_str(), &end, 10);
        bool ok = !t.empty() && end && *end == 0;
        x = ok ? y : lo;
    }
    return (int)std::max((long)lo, std::min((long)hi, x));
}

static std::string str_of(const Value &v) { return v.is_string() ? v.str() : store::text_of(v); }

std::vector<std::string> heads_of(const Value &b, int n, const std::vector<std::string> &defaults) {
    std::vector<std::string> heads;
    if (b.get("heads").truthy())
        for (const auto &h : b.get("heads").arr()) heads.push_back(str_of(h));
    std::vector<std::string> out;
    for (int i = 0; i < n; i++) {
        size_t k = (size_t)i;
        if (k < heads.size() && !text::strip(heads[k]).empty()) out.push_back(heads[k]);
        else out.push_back(k < defaults.size() ? defaults[k] : "");
    }
    return out;
}

int table_cols(const Value &b) {
    int n = (int)std::min<size_t>(b.get("heads").truthy() ? b.get("heads").size() : 0, 8);
    return std::max(clamp(b.get("cols"), 1, 8), n);
}

int table_rows(const Value &b) {
    int n = (int)std::min<size_t>(b.get("side").truthy() ? b.get("side").size() : 0, 20);
    return std::max(clamp(b.get("rows"), 1, 20), n);
}

bool has_heads(const Value &b) {
    if (b.get("heads").truthy())
        for (const auto &h : b.get("heads").arr())
            if (!text::strip(h.truthy() ? str_of(h) : "").empty()) return true;
    return b.get("preset").is_string() && b.get("preset").str() == "tchart";
}

static std::string layout_of(const Value &b) { return b.has("layout") ? str_of(b.get("layout")) : "table"; }

int auto_lines(const Value &b) {
    std::string lay = layout_of(b);
    if (lay == "table") return std::max(12, table_rows(b) * 3);
    if (lay == "sequence") {
        int n = clamp(b.get("rows"), 2, 8);
        return n <= 3 ? 9 : n * 4;
    }
    if (lay == "venn") return 15;
    if (lay == "web" || lay == "frayer" || lay == "plot") return 18;
    return 15;
}

static Value txt(double x, double y, double w, const std::string &s, double size = 10.5, const char *align = "left", bool bold = true) {
    Value t = Value::object();
    t["k"] = "text";
    t["x"] = x;
    t["y"] = y;
    t["w"] = w;
    t["text"] = s;
    t["bold"] = bold;
    t["size"] = size;
    t["align"] = align;
    return t;
}

static Value rect(double x, double y, double w, double h, double lw = 0.8, Value fill = Value()) {
    Value r = Value::object();
    r["k"] = "rect";
    r["x"] = x;
    r["y"] = y;
    r["w"] = w;
    r["h"] = h;
    r["lw"] = lw;
    r["fill"] = fill;
    return r;
}

static Value oval(double cx, double cy, double rx, double ry, double lw = 1.0, Value fill = Value()) {
    Value o = Value::object();
    o["k"] = "oval";
    o["cx"] = cx;
    o["cy"] = cy;
    o["rx"] = rx;
    o["ry"] = ry;
    o["lw"] = lw;
    o["fill"] = fill;
    return o;
}

static Value line(std::vector<std::pair<double, double>> pts, double lw = 0.8, bool arrow = false) {
    Value l = Value::object();
    l["k"] = "line";
    Value p = Value::array();
    for (auto &xy : pts) {
        Value q = Value::array();
        q.push(xy.first);
        q.push(xy.second);
        p.push(q);
    }
    l["pts"] = p;
    l["lw"] = lw;
    l["arrow"] = arrow;
    return l;
}

static Value table(const Value &b, double w, double h) {
    int cols = table_cols(b), rows = table_rows(b);
    auto heads = heads_of(b, cols);
    std::vector<std::string> side;
    if (b.get("side").truthy())
        for (const auto &s : b.get("side").arr()) if ((int)side.size() < rows) side.push_back(str_of(s));
    bool sidecol = b.get("sidecol").truthy();
    for (const auto &s : side) if (!text::strip(s).empty()) sidecol = true;
    double head = has_heads(b) ? 24 : 0;
    double sw = sidecol ? w * (cols <= 3 ? 0.24 : 0.18) : 0;
    double cw = (w - sw) / cols, rh = (h - head) / rows;
    Value out = Value::array();
    if (head) {
        out.push(rect(sw, 0, w - sw, head, 0, rgb(HEAD_FILL[0], HEAD_FILL[1], HEAD_FILL[2])));
        for (int i = 0; i < cols; i++) out.push(txt(sw + i * cw + 4, 6, cw - 8, heads[(size_t)i], cols <= 4 ? 10.5 : 9.5, "center"));
    }
    for (int i = 1; i < cols; i++) out.push(line({{sw + i * cw, 0}, {sw + i * cw, h}}));
    if (sidecol) {
        out.push(line({{sw, 0}, {sw, h}}, 1.0));
        for (int r = 0; r < rows; r++) {
            std::string label = (size_t)r < side.size() ? side[(size_t)r] : "";
            if (!text::strip(label).empty()) out.push(txt(5, head + r * rh + 5, sw - 10, label, 10));
        }
    }
    for (int r = 0; r <= rows; r++) {
        double y = head + r * rh;
        if (0 < y && y < h) out.push(line({{0, y}, {w, y}}, r == 0 ? 1.0 : 0.6));
    }
    out.push(rect(0, 0, w, h, 1.2));
    return out;
}

static Value venn(const Value &b, double w, double h) {
    int n = clamp(b.get("circles"), 2, 3) == 3 ? 3 : 2;
    auto heads = heads_of(b, n + 1);
    double top = 20;
    Value out = Value::array();
    if (n == 2) {
        double r = std::min((h - top - 4) / 2, w / 3.25), d = r * 1.15;
        double cx = w / 2, cy = top + r + 2;
        double c0 = cx - d / 2, c1 = cx + d / 2;
        out.push(oval(c0, cy, r, r, 1.2));
        out.push(oval(c1, cy, r, r, 1.2));
        out.push(txt(c0 - r, 2, r * 1.4, heads[0], 11, "center"));
        out.push(txt(c1 - r * 0.4, 2, r * 1.4, heads[1], 11, "center"));
        if (!text::strip(heads[2]).empty()) out.push(txt(cx - r * 0.42, cy - r * 0.62, r * 0.84, heads[2], 9.5, "center"));
        return out;
    }
    double r = std::min((h - 2 * top) / (2 + 0.866), w / 3.1), d = r * 1.05;
    double cx = w / 2, y1 = top + r;
    double xs[3] = {cx - d / 2, cx + d / 2, cx}, ys[3] = {y1, y1, y1 + d * 0.866};
    for (int i = 0; i < 3; i++) out.push(oval(xs[i], ys[i], r, r, 1.2));
    out.push(txt(xs[0] - r * 1.1, 2, r * 1.4, heads[0], 11, "center"));
    out.push(txt(xs[1] - r * 0.3, 2, r * 1.4, heads[1], 11, "center"));
    out.push(txt(cx - r, ys[2] + r + 4, r * 2, heads[2], 11, "center"));
    if (!text::strip(heads[3]).empty()) out.push(txt(cx - r * 0.3, y1 + d * 0.27 - 6, r * 0.6, heads[3], 8.5, "center"));
    return out;
}

static std::pair<double, double> edge(double cx, double cy, double rx, double ry, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    double t = 1 / std::sqrt((c / rx) * (c / rx) + (s / ry) * (s / ry));
    return {cx + c * t, cy + s * t};
}

static Value web(const Value &b, double w, double h) {
    int n = clamp(b.get("rows"), 3, 8);
    auto heads = heads_of(b, n);
    double cx = w / 2, cy = h / 2;
    double crx = std::min(w * 0.17, 95.0), cry = std::min(h * 0.13, 42.0);
    double scale = n <= 4 ? 1 : n <= 6 ? 0.86 : 0.74;
    double brx = std::min(w * 0.16, 92.0) * scale, bry = std::min(h * 0.14, 50.0) * scale;
    double ring_x = w / 2 - brx - 2, ring_y = h / 2 - bry - 2;
    Value out = Value::array();
    struct Bubble { double x, y; std::string label; };
    std::vector<Bubble> bubbles;
    for (int i = 0; i < n; i++) {
        double ang = -M_PI / 2 + 2 * M_PI * i / n;
        double bx = cx + ring_x * std::cos(ang), by = cy + ring_y * std::sin(ang);
        double to = std::atan2(by - cy, bx - cx);
        out.push(line({edge(cx, cy, crx, cry, to), edge(bx, by, brx, bry, to + M_PI)}, 0.9));
        bubbles.push_back({bx, by, heads[(size_t)i]});
    }
    for (const auto &bb : bubbles) {
        out.push(oval(bb.x, bb.y, brx, bry, 1.0, rgb(1, 1, 1)));
        if (!text::strip(bb.label).empty()) out.push(txt(bb.x - brx * 0.7, bb.y - bry + 7, brx * 1.4, bb.label, 9.5, "center"));
    }
    out.push(oval(cx, cy, crx, cry, 1.6, rgb(1, 1, 1)));
    std::string center = text::strip(store::field(b, "center"));
    if (!center.empty()) out.push(txt(cx - crx * 0.8, cy - 7, crx * 1.6, center, 12, "center"));
    return out;
}

static Value sequence(const Value &b, double w, double h) {
    int n = clamp(b.get("rows"), 2, 8);
    auto heads = heads_of(b, n);
    double gap = 26;
    Value out = Value::array();
    bool across = n <= 3;
    for (int i = 0; i < n; i++) {
        double x, y, bw, bh;
        if (across) {
            bw = (w - gap * (n - 1)) / n;
            x = i * (bw + gap);
            y = 0;
            bh = h;
        } else {
            bw = w;
            bh = (h - gap * (n - 1)) / n;
            x = 0;
            y = i * (bh + gap);
        }
        out.push(rect(x, y, bw, bh, 1.0));
        std::string label = text::strip(heads[(size_t)i]);
        if (label.empty()) label = std::to_string(i + 1) + ".";
        out.push(txt(x + 7, y + 6, bw - 14, label, 10.5));
        if (i < n - 1) {
            if (across) out.push(line({{x + bw + 4, h / 2}, {x + bw + gap - 4, h / 2}}, 1.4, true));
            else out.push(line({{w / 2, y + bh + 3}, {w / 2, y + bh + gap - 3}}, 1.4, true));
        }
    }
    return out;
}

static Value frayer(const Value &b, double w, double h) {
    auto heads = heads_of(b, 4, store::FRAYER_HEADS);
    Value out = Value::array();
    out.push(rect(0, 0, w, h, 1.2));
    out.push(line({{w / 2, 0}, {w / 2, h}}, 1.0));
    out.push(line({{0, h / 2}, {w, h / 2}}, 1.0));
    double rx = std::min(w * 0.17, 100.0), ry = std::min(h * 0.11, 44.0);
    for (int i = 0; i < 4; i++) {
        double x = (i % 2) * w / 2, y = (i / 2) * h / 2;
        if (i == 3) out.push(txt(w / 2 + rx + 8, y + 7, w / 2 - rx - 16, heads[3], 11));
        else out.push(txt(x + 8, y + 7, w / 2 - (i == 2 ? rx : 0) - 16, heads[(size_t)i], 11));
    }
    out.push(oval(w / 2, h / 2, rx, ry, 1.4, rgb(1, 1, 1)));
    std::string word = text::strip(store::field(b, "center"));
    if (!word.empty()) out.push(txt(w / 2 - rx * 0.85, h / 2 - 8, rx * 1.7, word, 13, "center"));
    return out;
}

static Value plot(const Value &b, double w, double h) {
    auto heads = heads_of(b, 5, store::PLOT_HEADS);
    double base = h * 0.6, peak = h * 0.27;
    Value out = Value::array();
    out.push(line({{0, base}, {w * 0.2, base}, {w * 0.5, peak}, {w * 0.8, base}, {w, base}}, 2.0));
    double boxes[5][4] = {{0, h * 0.66, w * 0.48, h * 0.34},
                          {0, h * 0.16, w * 0.25, h * 0.36},
                          {w * 0.31, 0, w * 0.38, h * 0.22},
                          {w * 0.75, h * 0.16, w * 0.25, h * 0.36},
                          {w * 0.52, h * 0.66, w * 0.48, h * 0.34}};
    for (int i = 0; i < 5; i++) {
        out.push(rect(boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3], 0.8));
        out.push(txt(boxes[i][0] + 6, boxes[i][1] + 5, boxes[i][2] - 12, heads[(size_t)i], 10));
    }
    return out;
}

Value shapes(const Value &b, double w, double h) {
    std::string lay = b.get("layout").is_string() ? b.get("layout").str() : "";
    if (lay == "venn") return venn(b, w, h);
    if (lay == "web") return web(b, w, h);
    if (lay == "sequence") return sequence(b, w, h);
    if (lay == "frayer") return frayer(b, w, h);
    if (lay == "plot") return plot(b, w, h);
    return table(b, w, h);
}

std::vector<std::string> text_lines(const Value &b, int width) {
    std::string lay = layout_of(b);
    std::vector<std::string> out;
    const std::string ind = "    ";
    long w = width - (long)ind.size();
    auto rep = [](const char *c, long n) { return n > 0 ? text::repeat(c, (size_t)n) : std::string(); };
    if (lay == "table") {
        int cols = table_cols(b), rows = table_rows(b);
        auto heads = heads_of(b, cols);
        std::vector<std::string> side;
        if (b.get("side").truthy())
            for (const auto &s : b.get("side").arr()) side.push_back(str_of(s));
        bool sidecol = b.get("sidecol").truthy();
        for (const auto &s : side) if (!text::strip(s).empty()) sidecol = true;
        std::vector<std::string> labels;
        if (sidecol) labels.push_back("");
        labels.insert(labels.end(), heads.begin(), heads.end());
        long n = (long)labels.size();
        long cw = std::max(5L, (long)std::floor((double)(w - n - 1) / (double)n));
        std::string sep = "+";
        for (size_t i = 0; i < labels.size(); i++) sep += rep("-", cw) + "+";
        out.push_back(ind + sep);
        if (has_heads(b)) {
            std::string row = "|";
            for (const auto &lab : labels) row += text::center(text::slice(lab, 0, cw), cw) + "|";
            out.push_back(ind + row);
            out.push_back(ind + sep);
        }
        for (int r = 0; r < rows; r++) {
            std::vector<std::string> cells;
            if (sidecol) cells.push_back(text::ljust(text::slice((size_t)r < side.size() ? side[(size_t)r] : "", 0, cw - 1), cw));
            for (int c = 0; c < cols; c++) cells.push_back(rep(" ", cw));
            std::string a = "|", e = "|";
            for (const auto &c : cells) a += c + "|";
            for (size_t i = 0; i < labels.size(); i++) e += rep(" ", cw) + "|";
            out.push_back(ind + a);
            out.push_back(ind + e);
            out.push_back(ind + sep);
        }
        return out;
    }
    if (lay == "venn") {
        int n = clamp(b.get("circles"), 2, 3) == 3 ? 3 : 2;
        auto heads = heads_of(b, n);
        for (int i = 0; i < n; i++) if (heads[(size_t)i].empty()) heads[(size_t)i] = std::string(1, "ABC"[i]);
        std::vector<std::string> regions;
        if (n == 2) regions = {heads[0] + " only", "Both", heads[1] + " only"};
        else
            regions = {heads[0] + " only", heads[1] + " only", heads[2] + " only", heads[0] + " & " + heads[1],
                       heads[0] + " & " + heads[2], heads[1] + " & " + heads[2], "All three"};
        out.push_back(ind + "(VENN DIAGRAM)");
        for (const auto &reg : regions) {
            out.push_back("");
            out.push_back(ind + reg + ":");
            out.push_back(ind + rep("_", w));
            out.push_back(ind + rep("_", w));
        }
        return out;
    }
    if (lay == "web") {
        int n = clamp(b.get("rows"), 3, 8);
        std::string center = text::strip(store::field(b, "center"));
        out.push_back(ind + "(IDEA WEB)  Center: " + (center.empty() ? rep("_", 24) : center));
        int i = 0;
        for (const auto &label : heads_of(b, n)) {
            i++;
            out.push_back("");
            out.push_back(ind + text::rstrip("  " + std::to_string(i) + ". " + label));
            out.push_back(ind + "     " + rep("_", w - 5));
        }
        return out;
    }
    if (lay == "sequence") {
        int n = clamp(b.get("rows"), 2, 8), i = 0;
        for (const auto &label : heads_of(b, n)) {
            i++;
            std::string lab = label.empty() ? std::to_string(i) + "." : label;
            out.push_back(ind + "+" + rep("-", w - 2) + "+");
            out.push_back(ind + "| " + text::ljust(text::slice(lab, 0, w - 4), w - 3) + "|");
            out.push_back(ind + "|" + rep(" ", w - 2) + "|");
            out.push_back(ind + "+" + rep("-", w - 2) + "+");
            if (i < n) out.push_back(ind + text::rstrip(text::center("v", w)));
        }
        return out;
    }
    std::vector<std::string> heads = lay == "frayer" ? heads_of(b, 4, store::FRAYER_HEADS) : heads_of(b, 5, store::PLOT_HEADS);
    if (lay == "frayer") {
        std::string word = text::strip(store::field(b, "center"));
        out.push_back(ind + "(FRAYER MODEL)  Word: " + (word.empty() ? rep("_", 24) : word));
    } else out.push_back(ind + "(PLOT DIAGRAM)");
    for (const auto &label : heads) {
        out.push_back("");
        out.push_back(ind + label + ":");
        out.push_back(ind + rep("_", w));
        out.push_back(ind + rep("_", w));
    }
    return out;
}

}  // namespace organizers
