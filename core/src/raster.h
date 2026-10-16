// Chalkboard's own 2D renderer: anti-aliased shapes, lines, text from TrueType/OpenType fonts,
// and pictures, into an RGB pixel grid. It makes board-slide PNGs with nothing installed, draws
// the window app, and shows the live slide preview.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace raster {

struct Color {
    double r = 0, g = 0, b = 0;   // 0-1
};

struct Pt {
    double x, y;
};

// A shape: contours of points (curves are flattened into short lines as they're added).
class Path {
public:
    void move(double x, double y);
    void line(double x, double y);
    void quad(double cx, double cy, double x, double y);
    void cubic(double c1x, double c1y, double c2x, double c2y, double x, double y);
    void close();
    void rect(double x, double y, double w, double h);
    void ellipse(double cx, double cy, double rx, double ry);
    bool empty() const { return contours_.empty(); }
    std::vector<std::vector<Pt>> contours_;

private:
    Pt at_{0, 0};
};

// An outline of a glyph, in font units (y up)
struct Glyph {
    Path path;
    double advance = 0;
};

// One font file's glyph outlines (TrueType 'glyf' or OpenType 'CFF ')
class Outlines {
public:
    static std::shared_ptr<Outlines> load(const std::string &path, int index = 0);   // nullptr when it can't be read
    static std::shared_ptr<Outlines> from_data(std::shared_ptr<const std::string> data, uint32_t offset);
    double units_per_em() const { return upm_; }
    uint32_t glyph_index(uint32_t codepoint) const;
    const Glyph &glyph(uint32_t index);   // cached
    double ascent() const { return ascent_; }
    double descent() const { return descent_; }

private:
    std::shared_ptr<const std::string> data_;
    uint32_t off_ = 0;
    double upm_ = 1000, ascent_ = 800, descent_ = -200;
    bool cff_ = false;
    size_t cmap_ = 0, glyf_ = 0, loca_ = 0, hmtx_ = 0, cff_at_ = 0, cff_len_ = 0;
    int loca_long_ = 0;
    size_t work_ = 0;   // components read for the glyph being built
    uint32_t num_glyphs_ = 0, num_hmetrics_ = 0;
    std::vector<uint32_t> bmp_;                      // a format-4 table: codepoint -> glyph
    std::vector<std::array<uint32_t, 3>> groups_;    // or format-12 ranges: (first, last, first glyph)
    std::vector<Glyph> cache_;
    std::vector<bool> cached_;
    // CFF
    std::vector<std::pair<size_t, size_t>> charstrings_, gsubrs_, lsubrs_;
    std::vector<std::vector<std::pair<size_t, size_t>>> fd_lsubrs_;
    std::vector<uint8_t> fd_select_;
    bool parse();
    bool parse_cff();
    void glyf_outline(uint32_t index, Path &p, int depth, double m[6]);
    void cff_outline(uint32_t index, Path &p);
    double advance_of(uint32_t index) const;
};

// How much of each pixel a shape covers (0-255), in the shape's own box
struct Mask {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint8_t> a;
};
Mask coverage(const Path &p, const double *m = nullptr);

class Image {
public:
    Image(int w = 0, int h = 0, Color fill = {1, 1, 1});
    int w, h;
    std::vector<uint8_t> px;   // RGB, row by row

    // fill a path; transform: x' = a*x + c*y + e, y' = b*x + d*y + f (pixels, y down)
    void fill(const Path &p, Color c, double alpha = 1, const double *m = nullptr);
    void stroke(const std::vector<Pt> &pts, double width, Color c, bool round = false, bool closed = false, const double *m = nullptr);
    void rect(double x, double y, double w, double h, Color c, double alpha = 1);
    // RGB pixels (and an optional 0-255 mask) scaled into the box
    void picture(const uint8_t *rgb, const uint8_t *alpha, int sw, int sh, int channels, double x, double y, double w, double h);
    // a coverage mask in one color, its box moved by (dx, dy)
    void mask(const Mask &m, int dx, int dy, Color c, double alpha = 1);
    // the same, shaded from top (at y = y0) to bottom (at y = y1), straight down
    void mask_gradient(const Mask &m, int dx, int dy, Color top, Color bottom, double y0, double y1, double alpha = 1);
    // another image (or part of one: sx, sy, sw, sh) copied in at (x, y), no scaling
    void blit(const Image &src, int x, int y, int sx = 0, int sy = 0, int sw = -1, int sh = -1);
    void clip(double x, double y, double w, double h);   // only inside this box until unclip()
    void unclip();
    bool clipped(int &x0, int &y0, int &x1, int &y1) const;   // the drawable box (the clip, or the whole image)
    std::string png() const;   // the image as a PNG file

private:
    std::vector<float> acc_;
    std::vector<std::vector<int>> clips_;   // x0, y0, x1, y1
    void blend(int x, int y, Color c, double a);
};

}  // namespace raster
