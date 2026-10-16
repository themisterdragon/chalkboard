// Graphic organizers as plain shapes, so every exporter draws the same picture (port of organizers.py).
// shapes() lays an organizer out in a w x h box (points, origin top-left, y down) and returns:
//   {"k": "rect", "x", "y", "w", "h", "lw", "fill"}        fill: [r, g, b] or null
//   {"k": "oval", "cx", "cy", "rx", "ry", "lw", "fill"}
//   {"k": "line", "pts": [[x, y], ...], "lw", "arrow"}     arrow: a head on the last point
//   {"k": "text", "x", "y", "w", "text", "bold", "size", "align"}   y is the top of the text
#pragma once
#include <string>
#include <vector>

#include "json.h"

namespace organizers {

const double LINE_GAP = 25;   // one writing line, about 1/3 inch

int clamp(const json::Value &v, int lo, int hi);
std::vector<std::string> heads_of(const json::Value &b, int n, const std::vector<std::string> &defaults = {});
int table_cols(const json::Value &b);
int table_rows(const json::Value &b);
bool has_heads(const json::Value &b);
int auto_lines(const json::Value &b);
json::Value shapes(const json::Value &b, double w, double h);
std::vector<std::string> text_lines(const json::Value &b, int width);

}  // namespace organizers
