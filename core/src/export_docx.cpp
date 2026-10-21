#include "export_docx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

#include "files.h"
#include "fontmetrics.h"
#include "markup.h"
#include "organizers.h"
#include "store.h"
#include "text.h"
#include "zipfile.h"

namespace export_docx {

using json::Value;
using store::text_of;

static const long MARGIN = 1080, QIND = 400, LINE_H = 480;
static const char *RED = "B01010", *GRAY = "666666";
static const char *W_NS =
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:wpg=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\" "
    "xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\"";
static const double EMU = 12700;

namespace {

std::string S(long v) { return std::to_string(v); }

std::string escape(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// XML-safe text, without the control characters XML can't hold
std::string x(const std::string &s) {
    std::string o;
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        if (u <= 8 || u == 11 || u == 12 || (u >= 14 && u <= 31)) continue;
        o += c;
    }
    return escape(o);
}

struct Ctx {
    std::vector<std::string> links;   // web addresses in the document; they become relationships
    long ids = 0;                     // drawing object ids, unique in the document
};

struct RunOpt {
    bool b = false, i = false, u = false;
    double size = 0;
    std::string color;
};

std::string run(const std::string &t, RunOpt o = {}) {
    std::string rpr;
    if (o.b) rpr += "<w:b/>";
    if (o.i) rpr += "<w:i/>";
    if (o.u) rpr += "<w:u w:val=\"single\"/>";
    if (!o.color.empty()) rpr += "<w:color w:val=\"" + o.color + "\"/>";
    if (o.size) {
        std::string sz = S((long)(o.size * 2));
        rpr += "<w:sz w:val=\"" + sz + "\"/><w:szCs w:val=\"" + sz + "\"/>";
    }
    if (!rpr.empty()) rpr = "<w:rPr>" + rpr + "</w:rPr>";
    std::vector<std::string> parts;
    for (const auto &p : text::split(t, "\t")) parts.push_back(p.empty() ? "" : "<w:t xml:space=\"preserve\">" + x(p) + "</w:t>");
    return "<w:r>" + rpr + text::join(parts, "<w:tab/>") + "</w:r>";
}

RunOpt opt(bool b = false, bool i = false, double size = 0, const std::string &color = "", bool u = false) {
    RunOpt o;
    o.b = b;
    o.i = i;
    o.size = size;
    o.color = color;
    o.u = u;
    return o;
}

std::string rich(Ctx &ctx, const std::string &t, bool b = false, bool i = false, double size = 0, const std::string &color = "") {
    std::string out;
    for (const auto &r : markup::runs(t))
        for (const auto &part : markup::links(r.text)) {
            if (!part.second.empty()) {
                ctx.links.push_back(part.second);
                out += "<w:hyperlink r:id=\"rIdLink" + S((long)ctx.links.size()) + "\" w:history=\"1\">" +
                       run(part.first, opt(b || r.bold, i || r.italic, size, "0563C1", true)) + "</w:hyperlink>";
            } else out += run(part.first, opt(b || r.bold, i || r.italic, size, color, r.under));
        }
    return out;
}

struct ParaOpt {
    std::string align;
    long left = 0, hanging = 0, first = 0, right = 0, before = 0, after = 100;
    bool keep = false, border = false, new_page = false;
    std::vector<std::pair<std::string, long>> tabs;
};

std::string para(const std::vector<std::string> &runs, const ParaOpt &o = {}) {
    std::string ppr = o.new_page ? "<w:pageBreakBefore/>" : "";
    if (o.keep) ppr += "<w:keepNext/>";
    if (o.border) ppr += "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"000000\"/></w:pBdr>";
    if (!o.tabs.empty()) {
        ppr += "<w:tabs>";
        for (const auto &t : o.tabs) ppr += "<w:tab w:val=\"" + t.first + "\" w:pos=\"" + S(t.second) + "\"/>";
        ppr += "</w:tabs>";
    }
    ppr += "<w:spacing w:before=\"" + S(o.before) + "\" w:after=\"" + S(o.after) + "\"/>";
    if (o.left || o.hanging || o.first || o.right) {
        std::string ind = "w:left=\"" + S(o.left) + "\" w:right=\"" + S(o.right) + "\"";
        ind += o.hanging ? " w:hanging=\"" + S(o.hanging) + "\"" : (o.first ? " w:firstLine=\"" + S(o.first) + "\"" : "");
        ppr += "<w:ind " + ind + "/>";
    }
    if (!o.align.empty()) ppr += "<w:jc w:val=\"" + o.align + "\"/>";
    std::string body;
    for (const auto &r : runs) body += r;
    return "<w:p><w:pPr>" + ppr + "</w:pPr>" + body + "</w:p>";
}

ParaOpt po(long after = 100) {
    ParaOpt o;
    o.after = after;
    return o;
}

std::string table(const std::vector<std::string> &rows, const std::vector<long> &widths, long indent = 0, bool borders = false) {
    std::string edge = borders ? "w:val=\"single\" w:sz=\"4\" w:color=\"000000\"" : "w:val=\"nil\"";
    std::string tb;
    for (const char *e : {"top", "left", "bottom", "right", "insideH", "insideV"}) tb += std::string("<w:") + e + " " + edge + "/>";
    std::string grid;
    long total = 0;
    for (long w : widths) {
        grid += "<w:gridCol w:w=\"" + S(w) + "\"/>";
        total += w;
    }
    std::string body;
    for (const auto &r : rows) body += r;
    return "<w:tbl><w:tblPr><w:tblW w:w=\"" + S(total) + "\" w:type=\"dxa\"/><w:tblInd w:w=\"" + S(indent) +
           "\" w:type=\"dxa\"/><w:tblBorders>" + tb +
           "</w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:left w:w=\"60\" w:type=\"dxa\"/><w:right w:w=\"60\" "
           "w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>" +
           grid + "</w:tblGrid>" + body + "</w:tbl>";
}

std::string cell(const std::string &content, long width, bool bottom = false) {
    std::string b = bottom ? "<w:tcBorders><w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"555555\"/></w:tcBorders>" : "";
    return "<w:tc><w:tcPr><w:tcW w:w=\"" + S(width) + "\" w:type=\"dxa\"/>" + b + "</w:tcPr>" + (content.empty() ? "<w:p/>" : content) + "</w:tc>";
}

std::string row(const std::vector<std::string> &cells, long height = 0) {
    std::string tr = height ? "<w:trPr><w:cantSplit/><w:trHeight w:val=\"" + S(height) + "\" w:hRule=\"exact\"/></w:trPr>"
                            : "<w:trPr><w:cantSplit/></w:trPr>";
    std::string body;
    for (const auto &c : cells) body += c;
    return "<w:tr>" + tr + body + "</w:tr>";
}

std::string shaded_cell(const std::string &content, long width) {
    return "<w:tc><w:tcPr><w:tcW w:w=\"" + S(width) +
           "\" w:type=\"dxa\"/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"DDDDDD\"/><w:vAlign w:val=\"center\"/></w:tcPr>" + content +
           "</w:tc>";
}

std::string organizer_table(const Value &b, long width, long height) {
    long cols = organizers::table_cols(b), rows = organizers::table_rows(b);
    std::vector<std::string> side;
    for (const auto &s : b.get("side").arr())
        if ((long)side.size() < rows) side.push_back(text_of(s));
    bool sidecol = b.get("sidecol").truthy();
    for (const auto &s : side) if (!text::strip(s).empty()) sidecol = true;
    long sw = sidecol ? width * (cols <= 3 ? 24 : 18) / 100 : 0;
    long cw = (width - sw) / cols;
    std::vector<long> widths;
    if (sidecol) widths.push_back(sw);
    for (long i = 0; i < cols; i++) widths.push_back(cw);
    std::vector<std::string> out;
    long head = organizers::has_heads(b) ? 440 : 0;
    if (head) {
        ParaOpt hp = po(0);
        hp.keep = true;
        std::vector<std::string> cells;
        if (sidecol) cells.push_back(cell(para({}, hp), sw));
        ParaOpt cp = hp;
        cp.align = "center";
        for (const auto &h : organizers::heads_of(b, (int)cols)) cells.push_back(shaded_cell(para({run(h, opt(true))}, cp), cw));
        out.push_back(row(cells, head));
    }
    long rh = std::max(400L, (height - head) / rows);
    for (long r = 0; r < rows; r++) {
        ParaOpt kp = po(0);
        kp.keep = r < rows - 1;   // keepNext in every row but the last holds the chart on one page
        std::string label = r < (long)side.size() ? side[(size_t)r] : "";
        std::string blank = para({}, kp);
        std::vector<std::string> cells;
        if (sidecol) cells.push_back(cell(!text::strip(label).empty() ? para({run(label, opt(true))}, kp) : blank, sw));
        for (long c = 0; c < cols; c++) cells.push_back(cell(blank, cw));
        std::string body;
        for (const auto &c : cells) body += c;
        out.push_back("<w:tr><w:trPr><w:cantSplit/><w:trHeight w:val=\"" + S(rh) + "\" w:hRule=\"atLeast\"/></w:trPr>" + body + "</w:tr>");
    }
    return table(out, widths, QIND, true);
}

long emu(double v) { return (long)std::nearbyint(v * EMU); }   // Python's round(): halves go to even

std::string organizer_drawing(Ctx &ctx, const Value &b, long width, long height) {
    double wpt = (double)width / 20, hpt = (double)height / 20;
    std::string kids;
    auto sp = [&](const char *geom, double x0, double y0, double w, double h, double line_w = 0, const Value &fill = Value(),
                  const std::string &txt = "", const std::string &flip = "", bool arrow = false) {
        ctx.ids++;
        std::string fill_xml = "<a:noFill/>";
        if (fill.truthy()) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%02X%02X%02X", (int)(fill.at(0).as_double() * 255), (int)(fill.at(1).as_double() * 255),
                          (int)(fill.at(2).as_double() * 255));
            fill_xml = std::string("<a:solidFill><a:srgbClr val=\"") + hex + "\"/></a:solidFill>";
        }
        std::string ln = line_w ? "<a:ln w=\"" + S(emu(line_w)) + "\"><a:solidFill><a:srgbClr val=\"000000\"/></a:solidFill>" +
                                      (arrow ? "<a:tailEnd type=\"triangle\" w=\"med\" len=\"med\"/>" : "") + "</a:ln>"
                                : "<a:ln><a:noFill/></a:ln>";
        std::string body = "<wps:bodyPr rot=\"0\" vert=\"horz\" wrap=\"square\" lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" anchor=\"t\"><a:noAutofit/></wps:bodyPr>";
        std::string box = txt.empty() ? "" : "<wps:txbx><w:txbxContent>" + txt + "</w:txbxContent></wps:txbx>";
        kids += "<wps:wsp><wps:cNvPr id=\"" + S(ctx.ids) + "\" name=\"Shape " + S(ctx.ids) + "\"/><wps:cNvSpPr/><wps:spPr><a:xfrm" + flip +
                "><a:off x=\"" + S(emu(x0)) + "\" y=\"" + S(emu(y0)) + "\"/><a:ext cx=\"" + S(std::max(emu(w), 1L)) + "\" cy=\"" +
                S(std::max(emu(h), 1L)) + "\"/></a:xfrm><a:prstGeom prst=\"" + geom + "\"><a:avLst/></a:prstGeom>" + fill_xml + ln +
                "</wps:spPr>" + box + body + "</wps:wsp>";
    };
    sp("rect", 0, 0, wpt, hpt);   // an invisible frame, so apps that size a group by its contents keep the shape
    Value drawn = organizers::shapes(b, wpt, hpt);   // keep it alive while the loop reads it
    for (const auto &s : drawn.arr()) {
        std::string k = s.s("k");
        if (k == "rect") sp("rect", s["x"].as_double(), s["y"].as_double(), s["w"].as_double(), s["h"].as_double(), s["lw"].as_double(), s.get("fill"));
        else if (k == "oval")
            sp("ellipse", s["cx"].as_double() - s["rx"].as_double(), s["cy"].as_double() - s["ry"].as_double(), 2 * s["rx"].as_double(),
               2 * s["ry"].as_double(), s["lw"].as_double(), s.get("fill"));
        else if (k == "line") {
            const Value &pts = s.get("pts");
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                double x1 = pts.at(i).at(0).as_double(), y1 = pts.at(i).at(1).as_double();
                double x2 = pts.at(i + 1).at(0).as_double(), y2 = pts.at(i + 1).at(1).as_double();
                std::string flip = std::string(x2 < x1 ? " flipH=\"1\"" : "") + (y2 < y1 ? " flipV=\"1\"" : "");
                sp("line", std::min(x1, x2), std::min(y1, y2), std::fabs(x2 - x1), std::fabs(y2 - y1), s["lw"].as_double(), Value(), "", flip,
                   s.get("arrow").truthy() && i == pts.size() - 2);
            }
        } else if (k == "text") {
            ParaOpt p = po(0);
            if (s.s("align") == "center") p.align = "center";
            std::string txt = para({run(markup::plain(text_of(s.get("text"))), opt(s.get("bold").truthy(), false, s["size"].as_double()))}, p);
            sp("rect", s["x"].as_double(), s["y"].as_double(), s["w"].as_double(), s["size"].as_double() * 3.6, 0, Value(), txt);
        }
    }
    ctx.ids++;
    std::string cx = S(emu(wpt)), cy = S(emu(hpt));
    std::string group = "<wpg:wgp><wpg:cNvGrpSpPr/><wpg:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + cx + "\" cy=\"" + cy +
                        "\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"" + cx + "\" cy=\"" + cy + "\"/></a:xfrm></wpg:grpSpPr>" + kids + "</wpg:wgp>";
    std::string drawing = "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" + cx + "\" cy=\"" + cy +
                          "\"/><wp:docPr id=\"" + S(ctx.ids) + "\" name=\"Organizer " + S(ctx.ids) +
                          "\"/><wp:cNvGraphicFramePr/><a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\">" +
                          group + "</a:graphicData></a:graphic></wp:inline></w:drawing>";
    return "<w:p><w:pPr><w:spacing w:before=\"60\" w:after=\"60\"/><w:ind w:left=\"" + S(QIND) + "\"/></w:pPr><w:r>" + drawing + "</w:r></w:p>";
}

long fill_height(long used, long body_h) { return std::max(1440L, body_h - used - 700); }

std::string body_xml(Ctx &ctx, const Value &blocks, long width, long body_h) {
    std::string out;
    long used = 0;
    bool new_page = false;
    const Value &bl = blocks;
    for (size_t bi = 0; bi < bl.size(); bi++) {
        const Value &b = bl.at(bi);
        std::string t = b.s("t");
        auto str = [&](const char *k) { return text_of(b.get(k)); };
        if (t == "title") used += 520;
        else if (t == "subtitle") used += 460;
        else if (t == "fields") used += 800;
        else if (t == "h1") used += 600;
        if (t == "p") used += 360 * ((long)text::length(str("text")) / 95 + 1);
        if (t == "title") {
            ParaOpt p = po(40);
            p.align = b.has("align") ? str("align") : "center";
            p.keep = true;
            p.new_page = new_page;
            out += para({run(markup::plain(str("text")), opt(true, false, 17))}, p);
            new_page = false;
        } else if (t == "subtitle") {
            ParaOpt p = po(200);
            p.align = b.has("align") ? str("align") : "center";
            out += para({run(markup::plain(str("text")), opt(false, true, 10.5, GRAY))}, p);
        } else if (t == "fields" && b.get("shares").truthy()) {
            std::vector<std::string> cells;
            std::vector<long> widths;
            const short *hb = fontmetrics::widths("Helvetica-Bold");
            const Value &items = b.get("items"), &shares = b.get("shares");
            for (size_t i = 0; i < items.size() && i < shares.size(); i++) {
                std::string k = text_of(items.at(i));
                long seg = (long)((double)width * shares.at(i).as_double());
                long tw = 0;
                for (char32_t c : text::to32(k + ":")) if (c >= 32 && c < 127) tw += hb[c - 32];
                double text_w = (double)tw * 11 / 1000;
                long lw = std::min(seg - 600, (long)(text_w * 20) + 260);
                cells.push_back(cell(para({run(k + ":", opt(true))}, po(0)), lw));
                cells.push_back(cell("", seg - lw - 240, true));
                cells.push_back(cell("", 240));
                widths.push_back(lw);
                widths.push_back(seg - lw - 240);
                widths.push_back(240);
            }
            long total = 0;
            for (long w : widths) total += w;
            long indent = b.get("center").truthy() ? (long)std::floor((double)(width - total) / 2) : 0;
            out += table({row(cells, 360)}, widths, indent);
            out += para({}, po(120));
        } else if (t == "fields") {
            std::vector<std::string> runs;
            for (const auto &it : b.get("items").arr()) {
                std::string k = text_of(it);
                int n = k == "Name" ? 34 : k == "Date" ? 14 : k == "Period" ? 7 : 12;
                runs.push_back(run(k + ": ", opt(true)));
                runs.push_back(run(std::string((size_t)n, '_') + "     "));
            }
            out += para(runs, po(200));
        } else if (t == "h1") {
            ParaOpt p = po(100);
            p.before = 240;
            p.keep = true;
            p.border = b.has("rule") ? b.get("rule").truthy() : true;
            out += para({run(markup::plain(str("text")), opt(true, false, 12.5))}, p);
        } else if (t == "p") {
            std::string st = b.has("style") ? str("style") : "normal";
            ParaOpt p = po(100);
            p.left = (long)(b.get("pad").as_double(0) * 20);
            out += para({rich(ctx, str("text"), st == "bold", st == "italic" || st == "small", st == "small" ? 9.5 : 0, st == "small" ? GRAY : "")}, p);
        } else if (t == "check") {
            ParaOpt p = po(60);
            p.left = 360;
            p.hanging = 360;
            p.before = 160;
            p.keep = true;
            p.tabs = {{"left", 360}};
            out += para({run("\xE2\x98\x90", opt(false, false, 15)), run("\t" + markup::plain(str("text")), opt(true, false, 11.5))}, p);
        } else if (t == "bullet") {
            long left = 360 + 360 * (long)b.get("indent").as_int(0) + (long)(b.get("pad").as_double(0) * 20);
            ParaOpt p = po(60);
            p.left = left;
            p.hanging = 240;
            p.tabs = {{"left", left}};
            out += para({run("\xE2\x80\xA2\t"), rich(ctx, str("text"))}, p);
        } else if (t == "kv") {
            out += para({run(markup::plain(str("label")) + " ", opt(true)), rich(ctx, str("text"))});
        } else if (t == "q") {
            std::vector<std::string> runs = {run(str("num"), opt(true)), run("\t"), rich(ctx, str("text"))};
            if (b.get("points").truthy()) runs.push_back(run("  " + str("points"), opt(false, true, 0, GRAY)));
            ParaOpt p = po(60);
            p.left = QIND;
            p.hanging = QIND;
            p.before = 160;
            p.keep = true;
            p.tabs = {{"left", QIND}};
            out += para(runs, p);
        } else if (t == "choice") {
            bool correct = b.get("correct").truthy();
            std::string c = correct ? RED : "";
            std::string label = (correct ? "\xE2\x9C\x94 " : "") + str("label");
            ParaOpt p = po(40);
            p.left = QIND + 640;
            p.hanging = 400;
            p.keep = !b.get("last").truthy();
            out += para({run(label, opt(correct, false, 0, c)), run("  "), rich(ctx, str("text"), correct, false, 0, c)}, p);
        } else if (t == "choice_inline") {
            std::vector<std::string> runs;
            long i = 0;
            for (const auto &it : b.get("items").arr()) {
                bool hit = b.get("correct").is_number() && b.get("correct").as_int() == i;
                runs.push_back(run((hit ? "\xE2\x9C\x94 " : "") + text_of(it), opt(hit, false, 0, hit ? RED : "")));
                runs.push_back(run("          "));
                i++;
            }
            ParaOpt p = po(80);
            p.left = QIND + 240;
            out += para(runs, p);
        } else if (t == "lines" || t == "blank" || t == "box") {
            long w = width - QIND, n = (long)b.get("n").as_int();
            if (t == "lines") {
                std::vector<std::string> rows;
                for (long i = 0; i < n; i++) rows.push_back(row({cell("", w, true)}, LINE_H));
                out += table(rows, {w}, QIND);
            } else out += table({row({cell("", w)}, LINE_H * n)}, {w}, QIND, t == "box");
            out += para({}, po(0));
        } else if (t == "answer") {
            ParaOpt p = po(100);
            p.left = QIND;
            out += para({run("Answer: ", opt(true, true, 0, RED)), rich(ctx, str("text"), false, true, 0, RED)}, p);
        } else if (t == "match") {
            long lw = (width - QIND) * 42 / 100, rw = width - QIND - lw;
            std::vector<std::string> L, Rt, K;
            for (const auto &v : b.get("left").arr()) L.push_back(text_of(v));
            for (const auto &v : b.get("right").arr()) Rt.push_back(text_of(v));
            for (const auto &v : b.get("key").arr()) K.push_back(text_of(v));
            bool has_key = b.get("key").truthy();
            std::vector<std::string> rows;
            size_t n = std::max(L.size(), Rt.size());
            for (size_t i = 0; i < n; i++) {
                std::string left = i < L.size() ? L[i] : "", right = i < Rt.size() ? Rt[i] : "";
                std::string lp, rp;
                if (!left.empty()) {
                    std::vector<std::string> runs;
                    if (has_key) runs.push_back(run("  " + (i < K.size() ? K[i] : "") + "  ", opt(true, false, 0, RED)));
                    else runs.push_back(run("______"));
                    runs.push_back(run("   "));
                    runs.push_back(rich(ctx, left));
                    lp = para(runs, po(60));
                }
                if (!right.empty())
                    rp = para({run((i < 10 ? std::string(1, "ABCDEFGHIJ"[i]) : std::string("?")) + ". ", opt(true)), rich(ctx, right)}, po(60));
                rows.push_back(row({cell(lp, lw), cell(rp, rw)}));
            }
            out += table(rows, {lw, rw}, QIND);
            out += para({}, po(0));
        } else if (t == "organizer") {
            long n = (long)b.get("n").as_int();
            long h = std::min(LINE_H * (b.get("fill").truthy() ? std::max(n, 22L) : n), body_h - 1600);
            if (b.get("layout").is_string() && b.get("layout").str() == "table") {
                out += organizer_table(b, width - QIND, h);
                out += para({}, po(0));
            } else out += organizer_drawing(ctx, b, width - QIND, h);
        } else if (t == "passage") {
            if (b.get("title").truthy()) {
                ParaOpt p = po(100);
                p.align = "center";
                p.before = 200;
                p.keep = true;
                out += para({run(markup::plain(str("title")), opt(true, false, 12))}, p);
            }
            for (const auto &p : text::split(b.get("text").truthy() ? str("text") : "", "\n"))
                if (!text::strip(p).empty()) {
                    ParaOpt q = po(80);
                    q.left = 360;
                    q.right = 360;
                    q.first = 360;
                    out += para({rich(ctx, text::strip(p))}, q);
                }
        } else if (t == "table") {   // a filled-in table (see pdf.cpp)
            std::vector<long> widths;
            std::vector<std::string> heads;
            for (const auto &c : b.get("cols").arr()) {
                widths.push_back((long)((double)width * c.at(1).as_double()));
                heads.push_back(text_of(c.at(0)));
            }
            auto line = [&](const std::vector<std::string> &cells, bool head, bool bold, long h) {
                std::vector<std::string> cs;
                for (size_t i = 0; i < widths.size(); i++) {
                    ParaOpt cp = po(0);
                    if (i) cp.align = "center";
                    std::string v = i < cells.size() ? cells[i] : "";
                    std::string content = para({run(markup::plain(v), opt(head || bold, false, 10))}, cp);
                    cs.push_back(head ? shaded_cell(content, widths[i]) : cell(content, widths[i]));
                }
                return row(cs, h);
            };
            std::vector<std::string> rows = {line(heads, true, false, 400)};
            for (const auto &r : b.get("rows").arr()) {
                std::vector<std::string> cells;
                for (const auto &c : r.arr()) cells.push_back(text_of(c));
                rows.push_back(line(cells, false, false, 380));
            }
            for (long i = 0; i < b.get("blank").as_int(); i++) rows.push_back(line({}, false, false, 440));
            if (b.get("total").is_array()) {
                std::vector<std::string> cells;
                for (const auto &c : b.get("total").arr()) cells.push_back(text_of(c));
                rows.push_back(line(cells, false, true, 380));
            }
            out += table(rows, widths, 0, true);
            out += para({run("")}, po(120));
        } else if (t == "grid") {
            std::vector<long> widths;
            std::vector<std::string> heads;
            for (const auto &c : b.get("cols").arr()) {
                widths.push_back((long)((double)width * c.at(1).as_double()));
                heads.push_back(text_of(c.at(0)));
            }
            std::vector<std::string> hcells;
            ParaOpt cp = po(0);
            cp.align = "center";
            for (size_t i = 0; i < widths.size(); i++) hcells.push_back(shaded_cell(para({run(heads[i], opt(true))}, cp), widths[i]));
            std::vector<std::string> rows = {row(hcells, 440)};
            long nrows = (long)b.get("rows").as_int();
            long rh = (long)std::floor((double)(fill_height(used, body_h) - 440) / (double)nrows);
            for (long r = 0; r < nrows; r++) {
                std::string body;
                for (long w : widths) body += cell("", w);
                rows.push_back("<w:tr><w:trPr><w:trHeight w:val=\"" + S(rh) + "\" w:hRule=\"atLeast\"/></w:trPr>" + body + "</w:tr>");
            }
            out += table(rows, widths, 0, true);
        } else if (t == "days") {
            const Value &days = b.get("days");
            long bh = fill_height(used, body_h) / (long)days.size();
            std::vector<std::string> rows;
            for (const auto &d : days.arr()) {
                std::string label = text_of(d.at(0)), prompt = text_of(d.at(1));
                ParaOpt p = po(60);
                p.tabs = {{"right", width - 160}};
                std::string content = para({run(label, opt(true, false, 12)), run("\t"), run("Date: ", opt(true, false, 10)), run("____________", opt(false, false, 10))}, p);
                if (!text::strip(prompt).empty()) content += para({rich(ctx, text::strip(prompt), false, true)}, po(60));
                rows.push_back(row({cell(content, width)}, bh));
            }
            out += table(rows, {width}, 0, true);
        } else if (t == "space") {
        } else if (t == "rule") {
            ParaOpt p = po(100);
            p.border = true;
            out += para({}, p);
        } else if (t == "pagebreak") {
            // a following title starts the page itself, so no extra paragraph spills onto the page before
            if (bi + 1 < bl.size() && bl.at(bi + 1).s("t") == "title") new_page = true;
            else out += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>";
            used = 0;
        }
    }
    out += para({});
    return out;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> parts(const Value &doc, const std::string &family, const std::string &page) {
    std::string font = family == "Times" ? "Times New Roman" : "Arial";
    long pw = page == "A4" ? 11906 : 12240, ph = page == "A4" ? 16838 : 15840;
    long width = pw - 2 * MARGIN;
    Ctx ctx;
    std::string body = body_xml(ctx, doc.get("blocks"), width, ph - 2 * MARGIN);
    std::string M = S(MARGIN);
    std::string sect = "<w:sectPr><w:footerReference w:type=\"default\" r:id=\"rId2\"/><w:pgSz w:w=\"" + S(pw) + "\" w:h=\"" + S(ph) +
                       "\"/><w:pgMar w:top=\"" + M + "\" w:right=\"" + M + "\" w:bottom=\"" + M + "\" w:left=\"" + M +
                       "\" w:header=\"720\" w:footer=\"500\" w:gutter=\"0\"/></w:sectPr>";
    const std::string XML = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    std::string document = XML + "<w:document " + W_NS + "><w:body>" + body + sect + "</w:body></w:document>";
    std::string small = std::string("<w:rPr><w:color w:val=\"") + GRAY + "\"/><w:sz w:val=\"17\"/><w:szCs w:val=\"17\"/></w:rPr>";
    auto fld = [&](const char *instr) {
        return "<w:r>" + small + "<w:fldChar w:fldCharType=\"begin\"/></w:r><w:r>" + small + "<w:instrText xml:space=\"preserve\"> " + instr +
               " </w:instrText></w:r><w:r>" + small + "<w:fldChar w:fldCharType=\"separate\"/></w:r>" + run("1", opt(false, false, 8.5, GRAY)) +
               "<w:r>" + small + "<w:fldChar w:fldCharType=\"end\"/></w:r>";
    };
    std::string footer = XML + "<w:ftr " + W_NS + "><w:p><w:pPr><w:tabs><w:tab w:val=\"right\" w:pos=\"" + S(width) + "\"/></w:tabs></w:pPr>" +
                         run(doc.has("footer") ? text_of(doc.get("footer")) : "", opt(false, true, 8.5, GRAY)) + "<w:r><w:tab/></w:r>" +
                         run("Page ", opt(false, false, 8.5, GRAY)) + fld("PAGE") + run(" of ", opt(false, false, 8.5, GRAY)) + fld("NUMPAGES") +
                         "</w:p></w:ftr>";
    std::string styles = XML + "<w:styles " + W_NS + "><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"" + font + "\" w:hAnsi=\"" + font +
                         "\" w:cs=\"" + font + "\" w:eastAsia=\"" + font +
                         "\"/><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"en-US\"/></w:rPr></w:rPrDefault>"
                         "<w:pPrDefault><w:pPr><w:spacing w:after=\"100\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
                         "</w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
                         "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/>"
                         "<w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/><w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/>"
                         "<w:left w:w=\"108\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/>"
                         "</w:tblCellMar></w:tblPr></w:style></w:styles>";
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::string core = XML +
                       "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                       "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>" +
                       x(doc.has("title") ? text_of(doc.get("title")) : "") +
                       "</dc:title><dc:creator>Chalkboard</dc:creator><dcterms:created xsi:type=\"dcterms:W3CDTF\">" + stamp +
                       "</dcterms:created><dcterms:modified xsi:type=\"dcterms:W3CDTF\">" + stamp + "</dcterms:modified></cp:coreProperties>";
    std::string app = XML + "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Chalkboard</Application></Properties>";
    std::string ctypes =
        XML +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
        "</Types>";
    std::string rels =
        XML +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
        "</Relationships>";
    std::string doc_rels = XML +
                           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
                           "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>";
    for (size_t n = 0; n < ctx.links.size(); n++)
        doc_rels += "<Relationship Id=\"rIdLink" + S((long)n + 1) +
                    "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"" + escape(ctx.links[n]) +
                    "\" TargetMode=\"External\"/>";
    doc_rels += "</Relationships>";
    return {{"[Content_Types].xml", ctypes}, {"_rels/.rels", rels},     {"word/document.xml", document}, {"word/_rels/document.xml.rels", doc_rels},
            {"word/styles.xml", styles},     {"word/footer1.xml", footer}, {"docProps/core.xml", core},   {"docProps/app.xml", app}};
}

bool render_docx(const Value &doc, const std::string &path, const std::string &family, const std::string &page, std::string *err) {
    zipfile::Writer z;
    for (const auto &p : parts(doc, family, page)) z.add(p.first, p.second);
    return files::write(path, z.finish(), err);
}

}  // namespace export_docx
