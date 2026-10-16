#include "fontlib.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#include "files.h"
#include "fontmetrics.h"
#include "text.h"
#include "zlib.h"

namespace fs = std::filesystem;

namespace fontlib {

static const std::vector<std::pair<std::string, std::string>> OFFICE = {{"Helvetica", "Arial"}, {"Times", "Times New Roman"}};

static bool is_standard(const std::string &n) { return n == "Helvetica" || n == "Times"; }

std::vector<std::string> font_dirs() {
    std::string home = files::home();
#if defined(__APPLE__)
    return {"/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts", files::join(home, "Library/Fonts")};
#elif defined(_WIN32)
    std::string win = files::env("WINDIR");
    if (win.empty()) win = "C:\\Windows";
    std::string local = files::env("LOCALAPPDATA");
    if (local.empty()) local = files::join(home, "AppData\\Local");
    return {files::join(win, "Fonts"), files::join(local, "Microsoft\\Windows\\Fonts")};
#else
    std::string xdg = files::env("XDG_DATA_HOME");
    if (xdg.empty()) xdg = files::join(home, ".local/share");
    std::vector<std::string> dirs = {files::join(xdg, "fonts"), files::join(home, ".fonts")};
    std::string sys = files::env("XDG_DATA_DIRS");
    if (sys.empty()) sys = "/usr/local/share:/usr/share";
    for (const auto &d : text::split(sys, ":"))
        if (!d.empty()) dirs.push_back(files::join(d, "fonts"));
    return dirs;
#endif
}

static std::map<std::string, std::map<int, std::pair<std::string, int>>> g_found;
static std::vector<std::string> g_order;
static bool g_scanned = false;

// os.walk: a folder's files (sorted), then its subfolders (in the order the system lists them)
static void walk(const fs::path &dir, std::set<std::string> &seen, std::map<std::string, std::map<int, std::pair<std::string, int>>> &found,
                 int depth) {
    if (depth > 32) return;
    std::error_code ec;
    std::vector<std::string> names;
    std::vector<fs::path> subdirs;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) {
            if (!it->is_symlink(e2)) subdirs.push_back(it->path());
        } else names.push_back(it->path().filename().u8string());
    }
    std::sort(names.begin(), names.end());
    for (const auto &name : names) {
        std::string low = text::lower(name);
        if (!(text::ends_with(low, ".ttf") || text::ends_with(low, ".otf") || text::ends_with(low, ".ttc") || text::ends_with(low, ".otc"))) continue;
        fs::path p = dir / fs::u8path(name);
        std::error_code e3;
        std::string real = fs::weakly_canonical(p, e3).u8string();
        if (e3) real = p.u8string();
        if (!seen.insert(real).second) continue;
        std::vector<ttf::Found> got;
        try {
            got = ttf::scan(p.u8string());
        } catch (...) {
            continue;
        }
        for (const auto &f : got) {
            auto &styles = found[f.family];
            int style = (f.bold ? 1 : 0) + 2 * (f.italic ? 1 : 0);
            if (!styles.count(style)) styles[style] = {p.u8string(), f.index};
        }
    }
    for (const auto &d : subdirs) walk(d, seen, found, depth + 1);
}

const std::map<std::string, std::map<int, std::pair<std::string, int>>> &system_fonts(bool refresh) {
    if (g_scanned && !refresh) return g_found;
    std::map<std::string, std::map<int, std::pair<std::string, int>>> found;
    std::set<std::string> seen;
    for (const auto &top : font_dirs()) walk(fs::u8path(top), seen, found, 0);
    g_found.clear();
    g_order.clear();
    for (auto &kv : found)
        if (kv.second.count(0) || kv.second.size() == 1) {
            g_found[kv.first] = kv.second;
            g_order.push_back(kv.first);
        }
    std::stable_sort(g_order.begin(), g_order.end(), [](const std::string &a, const std::string &b) { return text::lower(a) < text::lower(b); });
    g_scanned = true;
    return g_found;
}

const std::vector<std::string> &system_font_order() {
    system_fonts();
    return g_order;
}

std::vector<std::pair<std::string, std::string>> families() {
    std::vector<std::pair<std::string, std::string>> out = {{"Helvetica", "standard"}, {"Times", "standard"}};
    for (const auto &n : system_font_order())
        if (n != "Arial" && n != "Times New Roman") out.push_back({n, "system"});
    return out;
}

std::string label(const std::string &name) { return name.empty() ? "Standard (Helvetica / Arial)" : name; }

bool available(const std::string &name) { return name.empty() || is_standard(name) || system_fonts().count(name); }

std::string office_name(const std::string &name) {
    if (name.empty() || !available(name)) return "Arial";
    for (const auto &o : OFFICE) if (o.first == name) return o.second;
    return name;
}

static std::map<std::string, std::pair<std::vector<Entry>, std::vector<std::pair<bool, bool>>>> g_faces;

std::pair<std::vector<Entry>, std::vector<std::pair<bool, bool>>> faces(const std::string &name) {
    auto hit = g_faces.find(name);
    if (hit != g_faces.end()) return hit->second;
    std::pair<std::vector<Entry>, std::vector<std::pair<bool, bool>>> out;
    if (name.empty() || is_standard(name) || !available(name)) {
        std::vector<std::string> std_ = name == "Times" ? std::vector<std::string>{"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"}
                                                         : std::vector<std::string>{"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"};
        for (const auto &s : std_) out.first.push_back({s, nullptr});
        out.second.assign(4, {false, false});
    } else {
        std::map<int, std::shared_ptr<ttf::Face>> loaded;
        for (const auto &kv : system_fonts().at(name)) {
            try {
                loaded[kv.first] = ttf::load(kv.second.first, kv.second.second);
            } catch (...) {
            }
        }
        if (loaded.empty()) out = faces("");
        else
            for (int s = 0; s < 4; s++) {
                if (loaded.count(s)) {
                    out.first.push_back({"", loaded[s]});
                    out.second.push_back({false, false});
                    continue;
                }
                // no real bold or italic in this family: draw it from the closest style
                int near = 0;
                for (int k : {s & 1, s & 2, 0, 1, 2, 3})
                    if (loaded.count(k)) { near = k; break; }
                out.first.push_back({"", loaded[near]});
                out.second.push_back({(s & 1) && !(near & 1), (s & 2) && !(near & 2)});
            }
    }
    g_faces[name] = out;
    return out;
}

BoardFonts::BoardFonts(const std::string &text_font, const std::string &head) {
    auto t = faces(text_font);
    auto h = head.empty() ? t : faces(head);
    entries = t.first;
    entries.insert(entries.end(), h.first.begin(), h.first.end());
    fakes = t.second;
    fakes.insert(fakes.end(), h.second.begin(), h.second.end());
    for (int i = 0; i < 4; i++) office.push_back(office_name(text_font));
    for (int i = 0; i < 4; i++) office.push_back(office_name(head.empty() ? text_font : head));
    for (const auto &e : entries) {
        if (e.face) {
            tables.push_back(e.face->widths);
            names.push_back(e.face->ps);
        } else {
            const short *w = fontmetrics::widths(e.std_name.c_str());
            tables.emplace_back(w, w + 224);
            names.push_back(e.std_name);
        }
    }
}

static std::string join_ints(const std::vector<int> &xs) {
    std::string o;
    for (size_t i = 0; i < xs.size(); i++) o += (i ? " " : "") + std::to_string(xs[i]);
    return o;
}

std::vector<int> BoardFonts::pdf_fonts(const std::function<int(const std::string &)> &add) const {
    std::map<const void *, int> made_face;
    std::map<std::string, int> made_std;
    std::vector<int> ids;
    for (const auto &e : entries) {
        if (!e.face) {
            if (!made_std.count(e.std_name))
                made_std[e.std_name] = add("<< /Type /Font /Subtype /Type1 /BaseFont /" + e.std_name + " /Encoding /WinAnsiEncoding >>");
            ids.push_back(made_std[e.std_name]);
            continue;
        }
        const ttf::Face &f = *e.face;
        if (!made_face.count(&f)) {
            std::string raw = f.file(), data = zlib::compress(raw);
            std::string kind = f.cff ? "/Subtype /OpenType " : "/Length1 " + std::to_string(raw.size()) + " ";
            int fid = add("<< " + kind + "/Filter /FlateDecode /Length " + std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream");
            int flags = 32 + (f.italic_angle ? 64 : 0);
            int desc = add("<< /Type /FontDescriptor /FontName /" + f.ps + " /Flags " + std::to_string(flags) + " /FontBBox [" + join_ints(f.bbox) +
                           "] /ItalicAngle " + std::to_string(f.italic_angle) + " /Ascent " + std::to_string(f.ascent) + " /Descent " +
                           std::to_string(f.descent) + " /CapHeight " + std::to_string(f.cap) + " /StemV 80 /" + (f.cff ? "FontFile3" : "FontFile2") +
                           " " + std::to_string(fid) + " 0 R >>");
            made_face[&f] = add("<< /Type /Font /Subtype /" + std::string(f.cff ? "Type1" : "TrueType") + " /BaseFont /" + f.ps +
                                " /FirstChar 32 /LastChar 255 /Widths [" + join_ints(f.widths) + "] /Encoding /WinAnsiEncoding /FontDescriptor " +
                                std::to_string(desc) + " 0 R >>");
        }
        ids.push_back(made_face[&f]);
    }
    return ids;
}

}  // namespace fontlib
