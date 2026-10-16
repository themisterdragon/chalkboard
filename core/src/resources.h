// The data folder built into the program (see tools/embed.cpp): standards, prompts, mascots.
#pragma once
#include <string>
#include <vector>

namespace resources {
bool get(const std::string &name, std::string &out);   // name like "data/standards/index.json"
std::vector<std::string> names();
}  // namespace resources
