// A .pptx slideshow of the board slides, fully editable (port of export_pptx.py): the board layout
// draws onto a SlideCanvas, and every panel, the title, the date, and the class codes become real
// text boxes. Opens in PowerPoint, Keynote, LibreOffice Impress, and Google Slides.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "images.h"
#include "pdf.h"

namespace export_pptx {

class SlideCanvas : public pdf::Canvas {
public:
    std::vector<std::string> shapes;
    std::vector<std::pair<std::string, std::string>> media;   // (file bytes, "png" or "jpeg")
    std::vector<std::string> slide_links;
    bool has_bg = false;
    pdf::RGB bg;

    bool editable() const override { return true; }
    void fill(double x, double y, double w, double h, pdf::RGB color) override;
    void line(double x1, double y1, double x2, double y2, double w = 0.5, pdf::RGB color = pdf::BLACK) override;
    void image(const images::Image &img, double x, double y, double w, double h) override;
    void text(double x, double y, const std::string &s, int style = pdf::R, double size = 11, const pdf::RGB *color = nullptr) override;
    void clip(double, double, double, double) override {}
    void unclip() override {}
    void textbox(double x, double top, double w, double h, const std::vector<std::vector<TextPiece>> &paras, const std::string &align = "l",
                 const std::string &name = "Text") override;
    void blocks(double x, double top, double w, double h, const json::Value &blocks, double size, pdf::RGB color, const std::string &name = "Text") override;
    std::string xml() const;

private:
    int next_id() const { return (int)shapes.size() + 2; }
    std::string xfrm(double x, double top, double w, double h) const;
    double width(const std::string &text, int style, double size) const;
    std::string face(int style) const;
    std::string run(const std::string &text, int style, double size, const pdf::RGB *color, bool under = false);
    std::string rich(const std::string &text, int base, double size, const pdf::RGB *color);
    void box(double x, double top, double w, double h, const std::string &paras, bool wrap, const std::string &name, bool fit = false);
};

std::pair<std::string, std::string> image_file(const images::Image &img);   // (bytes, "png"/"jpeg")
bool render_pptx(const std::vector<SlideCanvas *> &slides, const std::string &path, const std::string &title, pdf::RGB link_color,
                 std::string *err = nullptr);
std::vector<std::pair<std::string, std::string>> parts(const std::vector<SlideCanvas *> &slides, const std::string &title, pdf::RGB link_color);

}  // namespace export_pptx
