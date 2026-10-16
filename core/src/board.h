// 1920x1080 "board slides" for a classroom display (port of export_png.py). The slide is laid out
// with the PDF engine's canvas and drawn into pixels by Chalkboard's own renderer (paint.h), so
// nothing has to be installed. The same layout also draws editable slideshow slides (export_pptx).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fontlib.h"
#include "images.h"
#include "json.h"
#include "pdf.h"
#include "raster.h"

namespace board {

const int W = 1920, H = 1080;

struct BoardError {
    std::string message;
};

struct Palette {
    pdf::RGB bg, panel, ink, label, dim;
};
Palette school_palette(const std::string &primary = "", const std::string &secondary = "", const std::string &text = "");
std::vector<std::string> low_contrast(const json::Value &settings);   // "HEADINGS", "TEXT"
Palette palette(const json::Value &doc);

// One slide's canvas, or more when the lesson has too much to read on one. make() makes each
// canvas (a PDF page, or an editable slideshow slide). fonts: reuse another board's fonts.
struct Slides {
    std::shared_ptr<fontlib::BoardFonts> fonts;
    std::vector<std::unique_ptr<pdf::Canvas>> canvases;
};
Slides board_slides(const json::Value &doc, const images::Image *logo, const std::function<pdf::Canvas *()> &make = nullptr,
                    std::shared_ptr<fontlib::BoardFonts> fonts = nullptr);

// Writes path (PNG), more slides as "name 2.png"..., and the editable "name.pptx". Returns every
// file written; throws BoardError.
std::vector<std::string> render_png(const json::Value &doc, const images::Image *logo, const std::string &path);
// A whole day's boards as one slideshow (base.pptx) and PDF (base.pdf). Throws BoardError.
std::vector<std::string> render_day(const std::vector<json::Value> &docs, const images::Image *logo, const std::string &base,
                                    const std::string &title = "Day Slideshow");
// The first slide as a PNG (the board designer's preview); returns how many slides there are.
int render_preview(const json::Value &doc, const images::Image *logo, const std::string &path);
// every slide drawn into pixels (scale 1 = 1920x1080; the live preview uses less)
std::vector<raster::Image> pictures(const json::Value &doc, const images::Image *logo, double scale = 1);

}  // namespace board
