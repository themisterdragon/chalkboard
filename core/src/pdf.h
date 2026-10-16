// Dependency-free PDF output using the standard Times/Helvetica fonts (port of export_pdf.py).
// Text is drawn one byte per character in Windows-1252, like the standard PDF fonts expect.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "images.h"
#include "json.h"

namespace pdf {

enum Style { R = 0, B = 1, I = 2, BI = 3 };
struct RGB { double r = 0, g = 0, b = 0; };
class Fonts;
extern const RGB RED, GRAY, LINK, BLACK;
const int MARGIN = 54;
const double SIZE = 11;
const double LINE_GAP = 25;
const double QIND = 22;

std::string enc(const std::string &utf8);    // -> cp1252 bytes (unknown characters become "?")
std::string esc(const std::string &bytes);   // a PDF string's inside
int mixed(int base, bool bold, bool italic);

struct Piece {
    std::string text;
    int style;
    bool under;
};
using RichLine = std::vector<Piece>;

// The fonts of one document: styles R, B, I, BI (board slides add a heading family as 4-7).
class Fonts {
public:
    explicit Fonts(const std::string &family);
    virtual ~Fonts() = default;
    std::vector<std::string> names;              // PDF font names, one per style
    std::vector<std::vector<int>> tables;         // glyph widths for cp1252 32-255, one per style
    std::vector<std::pair<bool, bool>> fakes;     // (bold, italic) drawn by thickening/slanting; empty = none
    std::vector<std::string> office;              // the slideshow's font name per style (board fonts)
    double width(const std::string &s, int style, double size) const;
    // write the font objects; add(bytes) -> object number. Returns the numbers for F1, F2, ...
    virtual std::vector<int> pdf_fonts(const std::function<int(const std::string &)> &add) const;
    std::vector<std::string> wrap(const std::string &text, int style, double size, double width, double first_width = -1) const;
    std::vector<RichLine> rich_wrap(const std::string &text, int style, double size, double width, double first_width = -1) const;

protected:
    Fonts() = default;
};

// A page being drawn. The board layout also draws on a slideshow slide (export_pptx), which
// overrides these.
class Canvas {
public:
    virtual ~Canvas() = default;
    std::vector<std::string> ops;
    std::vector<std::pair<std::string, const images::Image *>> images;   // Im1, Im2, ...
    struct Link { double x1, y1, x2, y2; std::string url; };
    std::vector<Link> links;
    const std::vector<std::pair<bool, bool>> *fakes = nullptr;
    const class Fonts *fonts = nullptr;   // the board's fonts (the slideshow needs their names)

    // The slideshow makes editable text boxes instead of drawn lines of text.
    struct TextPiece {
        std::string text;
        int style;
        double size;
        RGB color;
    };
    virtual bool editable() const { return false; }
    // paras: pieces per paragraph; w < 0: one line, as wide as its text
    virtual void textbox(double, double, double, double, const std::vector<std::vector<TextPiece>> &, const std::string & = "l",
                         const std::string & = "Text") {}
    // a panel's board blocks as wrapping paragraphs
    virtual void blocks(double, double, double, double, const json::Value &, double, RGB, const std::string & = "Text") {}

    virtual void link(double x, double base, double w, double size, const std::string &url);
    virtual void image(const images::Image &img, double x, double y, double w, double h);
    virtual void text(double x, double y, const std::string &s, int style = R, double size = SIZE, const RGB *color = nullptr);
    virtual void line(double x1, double y1, double x2, double y2, double w = 0.5, RGB color = BLACK);
    virtual void rect(double x, double y, double w, double h, double lw = 0.6);
    virtual void fill(double x, double y, double w, double h, RGB color);
    virtual void clip(double x, double y, double w, double h);
    virtual void unclip();
    virtual void path(const std::vector<std::pair<double, double>> &pts, double w = 0.8, RGB color = BLACK);
    virtual void polygon(const std::vector<std::pair<double, double>> &pts, RGB color = BLACK);
    virtual void ellipse(double cx, double cy, double rx, double ry, RGB color = RED, double lw = 1.1, const RGB *fill = nullptr);
};

// organizers::shapes() output, top-left corner at (x0, top)
void draw_shapes(Canvas &c, const Fonts &f, const json::Value &items, double x0, double top);
// one line of formatted text; web addresses are drawn as links
void draw_rich(Canvas &c, const Fonts &f, double x, double base, const RichLine &line, double size, const RGB *color = nullptr);

struct Row {
    double h = 0;
    std::function<void(Canvas &, double top, double h)> draw;   // empty = nothing to draw
    bool glue = false, droptop = false, brk = false, fill = false;
};
std::vector<Row> build_rows(const json::Value &blocks, const Fonts &f, double x0, double width, double body_h);

// Lays the document out in pages and writes the file. Returns the number of pages, or 0 with err.
int render_pdf(const json::Value &doc, const std::string &path, const std::string &family = "Times", const std::string &page = "Letter",
               std::string *err = nullptr);
// The file itself, from drawn pages
std::string write(const std::vector<Canvas *> &pages, const Fonts &f, double W, double H, const std::string &title);

}  // namespace pdf
