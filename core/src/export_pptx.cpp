#include "export_pptx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "files.h"
#include "markup.h"
#include "store.h"
#include "text.h"
#include "zipfile.h"
#include "zlib.h"

namespace export_pptx {

using json::Value;
using store::text_of;

static const long CX = 12192000, CY = 6858000;   // 13.333 x 7.5 in, PowerPoint's widescreen size
static const long PX = CX / 1920;                 // EMU per board pixel (the layout is 1920 x 1080, y up from the bottom)
static const double ASCENT = 0.905;               // Arial's ascent: from a text box's top to the first baseline, per font size
static const std::string P_NS =
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
static const std::string REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
static const std::string CORE_REL = "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties";
static const std::string HEAD = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
static const std::string EMPTY_TREE =
    "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
    "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/>"
    "<a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>";

static std::string escape(const std::string &s) {
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

static long rnd(double v) { return (long)std::nearbyint(v); }   // Python's round(): halves to even
static std::string S(long v) { return std::to_string(v); }

static std::string hex(const pdf::RGB &c) {
    char b[8];
    auto ch = [](double v) { return (int)std::nearbyint(std::max(0.0, std::min(1.0, v)) * 255); };
    std::snprintf(b, sizeof b, "%02X%02X%02X", ch(c.r), ch(c.g), ch(c.b));
    return b;
}

static long sz(double px) { return std::max(100L, rnd(px * 50)); }   // board pixels -> hundredths of a point

static std::string rels(const std::vector<std::vector<std::string>> &items) {
    std::string body;
    for (const auto &it : items)
        body += "<Relationship Id=\"" + it[0] + "\" Type=\"" + (text::starts_with(it[1], "http") ? it[1] : REL + "/" + it[1]) + "\" Target=\"" + it[2] + "\"/>";
    return HEAD + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" + body + "</Relationships>";
}

std::string SlideCanvas::xfrm(double x, double top, double w, double h) const {
    return "<a:xfrm><a:off x=\"" + S(rnd(x * PX)) + "\" y=\"" + S(rnd((1080 - top) * PX)) + "\"/><a:ext cx=\"" + S(std::max(1L, rnd(w * PX))) + "\" cy=\"" +
           S(std::max(1L, rnd(h * PX))) + "\"/></a:xfrm>";
}

double SlideCanvas::width(const std::string &t, int style, double size) const {
    static const pdf::Fonts helvetica("Helvetica");   // Arial has Helvetica's widths
    return (fonts ? *fonts : helvetica).width(markup::plain(t), style, size);
}

std::string SlideCanvas::face(int style) const { return fonts && (size_t)style < fonts->office.size() ? fonts->office[(size_t)style] : "Arial"; }

void SlideCanvas::fill(double x, double y, double w, double h, pdf::RGB color) {
    if (x <= 0 && y <= 0 && w >= 1920 && h >= 1080) {
        bg = color;
        has_bg = true;
        return;
    }
    std::string n = S(next_id());
    shapes.push_back("<p:sp><p:nvSpPr><p:cNvPr id=\"" + n + "\" name=\"Panel " + n + "\"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr><p:spPr>" + xfrm(x, y + h, w, h) +
                     "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val=\"" + hex(color) +
                     "\"/></a:solidFill><a:ln><a:noFill/></a:ln></p:spPr></p:sp>");
}

void SlideCanvas::line(double x1, double y1, double x2, double y2, double w, pdf::RGB color) {
    std::string n = S(next_id());
    double x = std::min(x1, x2), top = std::max(y1, y2);
    shapes.push_back("<p:cxnSp><p:nvCxnSpPr><p:cNvPr id=\"" + n + "\" name=\"Line " + n + "\"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr><p:spPr>" +
                     xfrm(x, top, std::fabs(x2 - x1), std::fabs(y2 - y1)) + "<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom><a:ln w=\"" + S(rnd(w * PX)) +
                     "\"><a:solidFill><a:srgbClr val=\"" + hex(color) + "\"/></a:solidFill></a:ln></p:spPr></p:cxnSp>");
}

void SlideCanvas::image(const images::Image &img, double x, double y, double w, double h) {
    media.push_back(image_file(img));
    std::string rid = "rIdImg" + S((long)media.size()), n = S(next_id());
    shapes.push_back("<p:pic><p:nvPicPr><p:cNvPr id=\"" + n +
                     "\" name=\"School logo\" descr=\"School logo\"/><p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
                     "<p:blipFill><a:blip r:embed=\"" + rid + "\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>" + xfrm(x, y + h, w, h) +
                     "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>");
}

void SlideCanvas::text(double x, double y, const std::string &s, int style, double size, const pdf::RGB *color) {
    if (s.empty()) return;
    textbox(x, y + size * ASCENT, -1, size * 1.2, {{{s, style, size, color ? *color : pdf::BLACK}}});
}

std::string SlideCanvas::run(const std::string &t, int style, double size, const pdf::RGB *color, bool under) {
    std::string out;
    for (const auto &part : markup::links(t)) {
        std::string link;
        if (!part.second.empty()) {
            slide_links.push_back(part.second);
            link = "<a:hlinkClick r:id=\"rIdLink" + S((long)slide_links.size()) + "\"/>";
        }
        std::string u = under || !part.second.empty() ? " u=\"sng\"" : "";
        int s4 = style % 4;
        out += "<a:r><a:rPr lang=\"en-US\" sz=\"" + S(sz(size)) + "\" b=\"" + (s4 == pdf::B || s4 == pdf::BI ? "1" : "0") + "\" i=\"" +
               (s4 == pdf::I || s4 == pdf::BI ? "1" : "0") + "\"" + u + " dirty=\"0\"><a:solidFill><a:srgbClr val=\"" + hex(color ? *color : pdf::BLACK) +
               "\"/></a:solidFill><a:latin typeface=\"" + escape(face(style)) + "\"/>" + link + "</a:rPr><a:t>" + escape(part.first) + "</a:t></a:r>";
    }
    return out;
}

std::string SlideCanvas::rich(const std::string &t, int base, double size, const pdf::RGB *color) {
    std::string out;
    for (const auto &r : markup::runs(t)) out += run(r.text, pdf::mixed(base, r.bold, r.italic), size, color, r.under);
    return out;
}

void SlideCanvas::textbox(double x, double top, double w, double h, const std::vector<std::vector<TextPiece>> &paras, const std::string &align,
                          const std::string &name) {
    std::string body;
    for (const auto &para : paras) {
        body += "<a:p><a:pPr algn=\"" + align + "\"/>";
        for (const auto &p : para) body += rich(p.text, p.style, p.size, &p.color);
        body += "</a:p>";
    }
    bool wrap = w >= 0;
    if (!wrap) {
        double most = 0;
        bool first = true;
        for (const auto &para : paras) {
            double total = 0;
            for (const auto &p : para) total += width(p.text, p.style, p.size);
            if (first || total > most) most = total;
            first = false;
        }
        w = most * 1.04 + 4;
    }
    box(x, top, w, h, body, wrap, name);
}

void SlideCanvas::blocks(double x, double top, double w, double h, const Value &bl, double size, pdf::RGB color, const std::string &name) {
    std::string lead = "<a:lnSpc><a:spcPts val=\"" + S(sz(size * 1.24)) + "\"/></a:lnSpc>";
    std::string out, prev;
    bool first = true;
    for (const auto &b : bl.arr()) {
        std::string t = b.s("t");
        double gap = first ? 0 : size * (t == prev && t == "bullet" ? 0.22 : 0.55);
        first = false;
        prev = t;
        std::string before = "<a:spcBef><a:spcPts val=\"" + S(sz(gap)) + "\"/></a:spcBef>", ppr, body;
        if (t == "bullet") {
            double ind = size * (1.05 + 1.05 * b.get("indent").as_double(0));
            ppr = "<a:pPr marL=\"" + S(rnd(ind * PX)) + "\" indent=\"" + S(-rnd(size * 0.8 * PX)) + "\">" + lead + before + "<a:buFont typeface=\"" + escape(face(pdf::R)) +
                  "\"/><a:buChar char=\"&#8226;\"/></a:pPr>";
            body = rich(text_of(b.get("text")), pdf::R, size, &color);
        } else if (t == "kv") {
            ppr = "<a:pPr>" + lead + before + "<a:buNone/></a:pPr>";
            body = run(text_of(b.get("label")) + "  ", pdf::B, size, &color) + rich(text_of(b.get("text")), pdf::R, size, &color);
        } else {
            ppr = "<a:pPr>" + lead + before + "<a:buNone/></a:pPr>";
            body = rich(text_of(b.get("text")), b.get("style").is_string() && b.get("style").str() == "bold" ? pdf::B : pdf::R, size, &color);
        }
        out += "<a:p>" + ppr + body + "</a:p>";
    }
    box(x, top, w, h, out, true, name, true);
}

void SlideCanvas::box(double x, double top, double w, double h, const std::string &paras, bool wrap, const std::string &name, bool fit) {
    std::string n = S(next_id());
    std::string body = std::string("<a:bodyPr wrap=\"") + (wrap ? "square" : "none") + "\" lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" anchor=\"t\">" +
                       (fit ? "<a:normAutofit/>" : !wrap ? "<a:spAutoFit/>" : "") + "</a:bodyPr>";
    shapes.push_back("<p:sp><p:nvSpPr><p:cNvPr id=\"" + n + "\" name=\"" + escape(name) + "\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr>" +
                     xfrm(x, top, w, h) + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody>" + body + "<a:lstStyle/>" + paras +
                     "</p:txBody></p:sp>");
}

std::string SlideCanvas::xml() const {
    std::string bgx = has_bg ? "<p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"" + hex(bg) + "\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>" : "";
    std::string all;
    for (const auto &s : shapes) all += s;
    return HEAD + "<p:sld " + P_NS + "><p:cSld>" + bgx +
           "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" "
           "cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>" +
           all + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>";
}

std::pair<std::string, std::string> image_file(const images::Image &img) {
    if (img.filter == "DCTDecode") return {img.data, "jpeg"};
    int w = img.w, h = img.h, c = img.colors;
    std::string pix = zlib::decompress(img.data), alpha = img.has_alpha ? zlib::decompress(img.alpha) : "";
    int kind = (c == 1 ? 0 : 2) + (img.has_alpha ? 4 : 0);   // gray / RGB, with or without alpha
    std::string rows;
    for (int y = 0; y < h; y++) {
        rows += '\0';
        size_t line = (size_t)y * (size_t)w * (size_t)c;
        if (img.has_alpha) {
            for (int x = 0; x < w; x++) {
                rows.append(pix, line + (size_t)x * (size_t)c, (size_t)c);
                rows += alpha[(size_t)y * (size_t)w + (size_t)x];
            }
        } else rows.append(pix, line, (size_t)w * (size_t)c);
    }
    auto be32 = [](uint32_t v) {
        std::string s;
        for (int i = 3; i >= 0; i--) s += (char)((v >> (i * 8)) & 0xFF);
        return s;
    };
    auto chunk = [&](const std::string &t, const std::string &d) { return be32((uint32_t)d.size()) + t + d + be32(zlib::crc32(t + d)); };
    std::string ihdr = be32((uint32_t)w) + be32((uint32_t)h) + std::string(1, 8) + std::string(1, (char)kind) + std::string(3, '\0');
    return {std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", zlib::compress(rows)) + chunk("IEND", ""), "png"};
}

static std::string theme(const pdf::RGB &link) {
    std::string L = hex(link);
    std::string fills, lines, effects;
    for (int i = 0; i < 3; i++) {
        fills += "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
        lines += "<a:ln w=\"9525\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>";
        effects += "<a:effectStyle><a:effectLst/></a:effectStyle>";
    }
    return HEAD + "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Chalkboard\"><a:themeElements><a:clrScheme name=\"Chalkboard\">"
                  "<a:dk1><a:srgbClr val=\"000000\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1>"
                  "<a:dk2><a:srgbClr val=\"1F3B30\"/></a:dk2><a:lt2><a:srgbClr val=\"F6F3E9\"/></a:lt2>"
                  "<a:accent1><a:srgbClr val=\"136B4C\"/></a:accent1><a:accent2><a:srgbClr val=\"F9D26B\"/></a:accent2>"
                  "<a:accent3><a:srgbClr val=\"4472C4\"/></a:accent3><a:accent4><a:srgbClr val=\"ED7D31\"/></a:accent4>"
                  "<a:accent5><a:srgbClr val=\"A5A5A5\"/></a:accent5><a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>"
                  "<a:hlink><a:srgbClr val=\"" + L + "\"/></a:hlink><a:folHlink><a:srgbClr val=\"" + L + "\"/></a:folHlink>"
                  "</a:clrScheme><a:fontScheme name=\"Chalkboard\">"
                  "<a:majorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
                  "<a:minorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
                  "</a:fontScheme><a:fmtScheme name=\"Chalkboard\"><a:fillStyleLst>" + fills + "</a:fillStyleLst><a:lnStyleLst>" + lines +
           "</a:lnStyleLst><a:effectStyleLst>" + effects + "</a:effectStyleLst><a:bgFillStyleLst>" + fills +
           "</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>";
}

std::vector<std::pair<std::string, std::string>> parts(const std::vector<SlideCanvas *> &slides, const std::string &title, pdf::RGB link_color) {
    size_t n = slides.size();
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::string slide_ct;
    for (size_t i = 1; i <= n; i++)
        slide_ct += "<Override PartName=\"/ppt/slides/slide" + S((long)i) + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
    std::string ctypes =
        HEAD + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
               "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
               "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
               "<Default Extension=\"png\" ContentType=\"image/png\"/>"
               "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
               "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
               "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
               "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
               "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
               "<Override PartName=\"/ppt/presProps.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presProps+xml\"/>"
               "<Override PartName=\"/ppt/viewProps.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml\"/>"
               "<Override PartName=\"/ppt/tableStyles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml\"/>" +
        slide_ct +
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/></Types>";
    std::string ids;
    for (size_t i = 1; i <= n; i++) ids += "<p:sldId id=\"" + S(255 + (long)i) + "\" r:id=\"rId" + S((long)i + 1) + "\"/>";
    std::string pres = HEAD + "<p:presentation " + P_NS +
                       " saveSubsetFonts=\"1\"><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst><p:sldIdLst>" + ids +
                       "</p:sldIdLst><p:sldSz cx=\"" + S(CX) + "\" cy=\"" + S(CY) + "\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>";
    std::vector<std::vector<std::string>> pr = {{"rId1", "slideMaster", "slideMasters/slideMaster1.xml"}};
    for (size_t i = 1; i <= n; i++) pr.push_back({"rId" + S((long)i + 1), "slide", "slides/slide" + S((long)i) + ".xml"});
    long k = (long)n;
    pr.push_back({"rId" + S(k + 2), "theme", "theme/theme1.xml"});
    pr.push_back({"rId" + S(k + 3), "presProps", "presProps.xml"});
    pr.push_back({"rId" + S(k + 4), "viewProps", "viewProps.xml"});
    pr.push_back({"rId" + S(k + 5), "tableStyles", "tableStyles.xml"});
    std::string core = HEAD +
                       "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                       "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>" + escape(title) +
                       "</dc:title><dc:creator>Chalkboard</dc:creator><dcterms:created xsi:type=\"dcterms:W3CDTF\">" + stamp +
                       "</dcterms:created><dcterms:modified xsi:type=\"dcterms:W3CDTF\">" + stamp + "</dcterms:modified></cp:coreProperties>";
    std::string app = HEAD + "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Chalkboard</Application><Slides>" +
                      S(k) + "</Slides></Properties>";
    std::string tree_inner = EMPTY_TREE.substr(8, EMPTY_TREE.size() - 8 - 9);
    std::string master = HEAD + "<p:sldMaster " + P_NS + "><p:cSld><p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>" + tree_inner +
                         "</p:cSld><p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
                         "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" "
                         "folHlink=\"folHlink\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
                         "<p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>";
    std::string layout = HEAD + "<p:sldLayout " + P_NS + " type=\"blank\" preserve=\"1\">" + text::replace(EMPTY_TREE, "<p:cSld>", "<p:cSld name=\"Blank\">") +
                         "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
    std::vector<std::pair<std::string, std::string>> out = {
        {"[Content_Types].xml", ctypes},
        {"_rels/.rels", rels({{"rId1", "officeDocument", "ppt/presentation.xml"}, {"rId2", CORE_REL, "docProps/core.xml"}, {"rId3", "extended-properties", "docProps/app.xml"}})},
        {"docProps/core.xml", core},
        {"docProps/app.xml", app},
        {"ppt/presentation.xml", pres},
        {"ppt/_rels/presentation.xml.rels", rels(pr)},
        {"ppt/presProps.xml", HEAD + "<p:presentationPr " + P_NS + "/>"},
        {"ppt/viewProps.xml", HEAD + "<p:viewPr " + P_NS + "/>"},
        {"ppt/tableStyles.xml", HEAD + "<a:tblStyleLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" def=\"{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}\"/>"},
        {"ppt/theme/theme1.xml", theme(link_color)},
        {"ppt/slideMasters/slideMaster1.xml", master},
        {"ppt/slideMasters/_rels/slideMaster1.xml.rels", rels({{"rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"}, {"rId2", "theme", "../theme/theme1.xml"}})},
        {"ppt/slideLayouts/slideLayout1.xml", layout},
        {"ppt/slideLayouts/_rels/slideLayout1.xml.rels", rels({{"rId1", "slideMaster", "../slideMasters/slideMaster1.xml"}})},
    };
    for (size_t i = 1; i <= n; i++) {
        const SlideCanvas &sl = *slides[i - 1];
        std::vector<std::vector<std::string>> items = {{"rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"}};
        for (size_t j = 1; j <= sl.media.size(); j++) {
            std::string name = "s" + S((long)i) + "img" + S((long)j) + "." + sl.media[j - 1].second;
            out.push_back({"ppt/media/" + name, sl.media[j - 1].first});
            items.push_back({"rIdImg" + S((long)j), "image", "../media/" + name});
        }
        out.push_back({"ppt/slides/slide" + S((long)i) + ".xml", sl.xml()});
        std::string body = rels(items);
        if (!sl.slide_links.empty()) {   // web links point outside the file
            std::string extra;
            for (size_t j = 1; j <= sl.slide_links.size(); j++)
                extra += "<Relationship Id=\"rIdLink" + S((long)j) + "\" Type=\"" + REL + "/hyperlink\" Target=\"" + escape(sl.slide_links[j - 1]) +
                         "\" TargetMode=\"External\"/>";
            body = text::replace(body, "</Relationships>", extra + "</Relationships>");
        }
        out.push_back({"ppt/slides/_rels/slide" + S((long)i) + ".xml.rels", body});
    }
    return out;
}

bool render_pptx(const std::vector<SlideCanvas *> &slides, const std::string &path, const std::string &title, pdf::RGB link_color, std::string *err) {
    zipfile::Writer z;
    for (const auto &p : parts(slides, title, link_color)) z.add(p.first, p.second, !text::starts_with(p.first, "ppt/media/"));
    return files::write(path, z.finish(), err);
}

}  // namespace export_pptx
