// Drawing the PDF engine's canvas calls straight into pixels (raster::Image): board-slide PNGs,
// the live slide preview, and anything else laid out with the PDF engine.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "pdf.h"
#include "raster.h"

namespace paint {

// The outlines for a font family's four styles on this computer: a standard PDF name
// ("Helvetica-Bold") maps to a metric-compatible system font (Helvetica, Arial, Liberation Sans...).
std::shared_ptr<raster::Outlines> outlines_for(const std::string &pdf_name);
// a system family by name and style (0-3), with fallbacks; nullptr when nothing usable exists
std::shared_ptr<raster::Outlines> family_outlines(const std::string &family, int style, bool *faked_bold = nullptr, bool *faked_italic = nullptr);

class PixelCanvas : public pdf::Canvas {
public:
    // page_h: the page height in points (y flips); scale: pixels per point
    PixelCanvas(raster::Image &img, double page_h, double scale = 1);
    void link(double, double, double, double, const std::string &) override {}
    void image(const images::Image &img, double x, double y, double w, double h) override;
    void text(double x, double y, const std::string &s, int style = pdf::R, double size = pdf::SIZE, const pdf::RGB *color = nullptr) override;
    void line(double x1, double y1, double x2, double y2, double w = 0.5, pdf::RGB color = pdf::BLACK) override;
    void rect(double x, double y, double w, double h, double lw = 0.6) override;
    void fill(double x, double y, double w, double h, pdf::RGB color) override;
    void clip(double x, double y, double w, double h) override;
    void unclip() override;
    void path(const std::vector<std::pair<double, double>> &pts, double w = 0.8, pdf::RGB color = pdf::BLACK) override;
    void polygon(const std::vector<std::pair<double, double>> &pts, pdf::RGB color = pdf::BLACK) override;
    void ellipse(double cx, double cy, double rx, double ry, pdf::RGB color = pdf::RED, double lw = 1.1, const pdf::RGB *fill = nullptr) override;

private:
    raster::Image &img_;
    double ph_, k_;
    struct Face {
        std::shared_ptr<raster::Outlines> o;
        bool bold = false, italic = false;   // drawn thicker / slanted because the font has no such style
        bool ready = false;
    };
    std::vector<Face> faces_;   // by style, filled on first use
    raster::Pt to(double x, double y) const { return {x * k_, (ph_ - y) * k_}; }
    const Face &face(int style);
};

// one decoded picture: RGB (or gray) pixels and a mask; false when it can't be decoded
bool decode(const images::Image &img, std::vector<uint8_t> &pix, std::vector<uint8_t> &alpha, int &channels);

}  // namespace paint
