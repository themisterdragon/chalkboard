// A JPEG decoder (baseline and progressive; gray, color, and CMYK), so a JPEG school logo can be
// drawn into board-slide pictures and the window app. No outside library.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace jpeg {

// RGB pixels, 3 bytes each; false when the file isn't a JPEG this can read
bool decode(const std::string &data, int &w, int &h, std::vector<uint8_t> &rgb, std::string *err = nullptr);

}  // namespace jpeg
