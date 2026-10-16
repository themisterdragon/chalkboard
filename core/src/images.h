// Reading a PNG or JPEG (a school logo) into something the PDF engine can draw (see images.py).
#pragma once
#include <string>

#include "json.h"

namespace images {

struct ImageError {
    std::string message;   // for the teacher, in capitals like the rest of Chalkboard's errors
};

struct Image {
    int w = 0, h = 0;
    std::string filter;     // "DCTDecode" (JPEG as-is) or "FlateDecode" (zlib-compressed pixels)
    int colors = 3;         // 1 gray, 3 RGB, 4 CMYK
    std::string data;
    bool has_alpha = false;
    std::string alpha;      // zlib-compressed 8-bit mask
    bool inverted = false;  // Adobe CMYK JPEGs
    std::string name;       // the file it came from ("" when unknown)
    bool has_name = false;
};

Image read_image(const std::string &path);         // throws ImageError
Image read_png(const std::string &raw);
Image read_jpeg(const std::string &raw);
json::Value to_json(const Image &img);
Image from_json(const json::Value &d);              // throws ImageError("THE SAVED LOGO IS DAMAGED")

}  // namespace images
