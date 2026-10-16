// Inline formatting in any text field: **bold**, *italic*, __underline__ (port of markup.py).
// A mark only counts when the text inside it doesn't start or end with a space, so "5 * 3 * 2",
// bullets ("* item"), and fill-in-the-blank lines ("_____") print as typed. Marks never cross a
// line break. Positions are in characters (code points), like Python's.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace markup {

enum Bits { B = 1, I = 2, U = 4 };

struct Run {
    std::string text;
    bool bold = false, italic = false, under = false;
};

std::vector<Run> runs(const std::string &text, bool bold = false, bool italic = false, bool under = false);
std::string plain(const std::string &text);

// for an editor: (start, end, kind) where kind is "mark" or the formatting bits (B|I|U) as a number
struct Span {
    long start, end;
    bool mark;
    int bits;
};
std::vector<Span> spans(const std::string &text);

// Ctrl-B/T/U at the cursor: returns (text, cursor)
std::pair<std::string, long> toggle(const std::string &buf, long pos, char kind);

// marked text <-> plain text + B/I/U bits per character
std::pair<std::string, std::vector<int>> flags_of(const std::string &text);
std::string marked(const std::string &text, const std::vector<int> &fl);
std::pair<std::string, std::vector<int>> tidy(const std::string &text, const std::vector<int> &fl);

// web addresses written out: [(piece, url or "")]
std::vector<std::pair<std::string, std::string>> links(const std::string &text);

}  // namespace markup
