// Build helper: packs Chalkboard's data folder into a C++ file, so the program carries its own
// standards, prompts, and mascots and works as one file with nothing beside it.
//   embed <data folder> <out.cpp>
// Every file under the folder becomes resources::get("data/<path>").
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: embed <data folder> <out.cpp>\n";
        return 2;
    }
    fs::path root = fs::u8path(argv[1]);
    std::vector<fs::path> paths;
    for (auto &e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file() && e.path().filename().u8string()[0] != '.') paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());
    std::ostringstream o;
    o << "// Made by tools/embed.cpp from " << argv[1] << ". Don't edit; rebuild instead.\n";
    o << "#include \"resources.h\"\n#include <cstring>\n\nnamespace {\n";
    for (size_t i = 0; i < paths.size(); i++) {
        std::ifstream f(paths[i], std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // as numbers, not a string literal: some compilers cap string literals at 64 KB
        o << "const unsigned char R" << i << "[] = {";
        for (size_t k = 0; k < data.size(); k++) {
            if (k % 24 == 0) o << "\n";
            o << (int)(unsigned char)data[k] << ",";
        }
        o << "0};\n";
    }
    o << "struct Entry { const char *name; const unsigned char *data; size_t size; };\n";
    o << "const Entry ENTRIES[] = {\n";
    for (size_t i = 0; i < paths.size(); i++) {
        std::string rel = "data/" + fs::relative(paths[i], root).generic_u8string();
        o << "    {\"" << rel << "\", R" << i << ", sizeof R" << i << " - 1},\n";
    }
    o << "    {nullptr, nullptr, 0}};\n}  // namespace\n\n";
    o << "namespace resources {\n"
         "bool get(const std::string &name, std::string &out) {\n"
         "    for (const Entry *e = ENTRIES; e->name; e++)\n"
         "        if (name == e->name) { out.assign((const char *)e->data, e->size); return true; }\n"
         "    return false;\n"
         "}\n"
         "std::vector<std::string> names() {\n"
         "    std::vector<std::string> out;\n"
         "    for (const Entry *e = ENTRIES; e->name; e++) out.push_back(e->name);\n"
         "    return out;\n"
         "}\n"
         "}  // namespace resources\n";
    std::string text = o.str();
    std::ifstream old(fs::u8path(argv[2]), std::ios::binary);
    std::string before((std::istreambuf_iterator<char>(old)), std::istreambuf_iterator<char>());
    if (before == text) return 0;   // unchanged: keep the timestamp so nothing rebuilds
    std::ofstream out(fs::u8path(argv[2]), std::ios::binary);
    out << text;
    return out ? 0 : 1;
}
