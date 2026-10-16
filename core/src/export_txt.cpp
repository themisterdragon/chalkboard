#include "export_txt.h"

#include <algorithm>

#include "files.h"
#include "markup.h"
#include "organizers.h"
#include "store.h"
#include "text.h"
#include "textwrap.h"

namespace export_txt {

using json::Value;
using store::text_of;

Value unmark(const Value &b0) {
    Value b = b0;
    for (const char *k : {"text", "label", "title"})
        if (b.get(k).is_string()) b[k] = markup::plain(b.get(k).str());
    for (const char *k : {"left", "right"})
        if (b.get(k).is_array()) {
            Value a = Value::array();
            for (const auto &x : b.get(k).arr()) a.push(markup::plain(text_of(x)));
            b[k] = a;
        }
    if (b.s("t") == "days") {
        Value a = Value::array();
        for (const auto &d : b.get("days").arr()) {
            Value p = Value::array();
            p.push(d.at(0));
            p.push(markup::plain(text_of(d.at(1))));
            a.push(p);
        }
        b["days"] = a;
    }
    return b;
}

static void add(std::vector<std::string> &out, const std::vector<std::string> &more) { out.insert(out.end(), more.begin(), more.end()); }

std::vector<std::string> render_lines(const Value &doc, int width) {
    std::vector<std::string> out;
    auto w = [&](const std::string &t, int ind = 0, int sub = -1) {
        auto lines = textwrap::wrap(t, width, std::string((size_t)ind, ' '), std::string((size_t)(sub < 0 ? ind : sub), ' '));
        if (lines.empty()) lines.push_back("");
        return lines;
    };
    auto rep = [](const char *c, long n) { return n > 0 ? text::repeat(c, (size_t)n) : std::string(); };
    for (const auto &b0 : doc.get("blocks").arr()) {
        Value b = unmark(b0);
        std::string t = b.s("t"), txt = text_of(b.get("text"));
        if (t == "title") {
            long n = (long)text::length(txt);
            if (b.s("align") == "left") {
                out.push_back(txt);
                out.push_back(rep("=", std::min((long)width, n)));
            } else {
                out.push_back(text::rstrip(text::center(txt, width)));
                out.push_back(text::rstrip(text::center(rep("=", std::min((long)width, n + 4)), width)));
            }
        } else if (t == "subtitle") {
            out.push_back(b.s("align") == "left" ? txt : text::rstrip(text::center(txt, width)));
            out.push_back("");
        } else if (t == "fields") {
            std::vector<std::string> parts;
            for (const auto &k : b.get("items").arr()) parts.push_back(text_of(k) + ": " + rep("_", text_of(k) == "Name" ? 24 : 10));
            out.push_back(text::join(parts, "   "));
            out.push_back("");
        } else if (t == "h1") {
            out.push_back("");
            out.push_back(text::upper(txt));
            if (b.has("rule") ? b.get("rule").truthy() : true) out.push_back(rep("-", std::min((long)width, (long)text::length(txt))));
        } else if (t == "p") {
            add(out, w(txt));
            out.push_back("");
        } else if (t == "bullet") {
            int ind = 2 + 2 * (int)b.get("indent").as_int(0);
            add(out, w("- " + txt, ind, ind + 2));
        } else if (t == "kv") {
            add(out, w(text::rstrip(text_of(b.get("label")) + " " + txt), 0, 4));
            out.push_back("");
        } else if (t == "q") {
            std::string pts = b.get("points").truthy() ? "  " + text_of(b.get("points")) : "";
            out.push_back("");
            add(out, w(text::ljust(text_of(b.get("num")), 4) + txt + pts, 0, 4));
        } else if (t == "choice") {
            std::string mark = b.get("correct").truthy() ? "  <== " : "";
            add(out, w(text_of(b.get("label")) + " " + txt + mark, 6, 9));
        } else if (t == "choice_inline") {
            std::vector<std::string> items;
            long i = 0;
            for (const auto &it : b.get("items").arr()) {
                bool on = b.get("correct").is_number() && b.get("correct").as_int() == i;
                items.push_back(on ? "[" + text_of(it) + "]" : text_of(it));
                i++;
            }
            out.push_back("      " + text::join(items, "      "));
        } else if (t == "lines") {
            for (long i = 0; i < b.get("n").as_int(); i++) out.push_back("    " + rep("_", width - 4));
        } else if (t == "blank" || t == "box") {
            for (long i = 0; i < std::min(b.get("n").as_int(), (int64_t)6); i++) out.push_back("");
        } else if (t == "answer") {
            add(out, w("ANSWER: " + txt, 4, 12));
        } else if (t == "match") {
            const Value &L = b.get("left"), &R = b.get("right"), &K = b.get("key");
            size_t n = std::max(L.size(), R.size());
            for (size_t i = 0; i < n; i++) {
                std::string left = i < L.size() ? text_of(L.at(i)) : "", right = i < R.size() ? text_of(R.at(i)) : "";
                std::string blank = K.truthy() && !left.empty() ? "_" + text_of(K.at(i)) + "_" : (!left.empty() ? "___" : "   ");
                std::string rl = right.empty() ? "" : (i < 10 ? std::string(1, "ABCDEFGHIJ"[i]) : std::string("?")) + ". " + right;
                out.push_back(text::rstrip("    " + blank + " " + text::ljust(text::slice(left, 0, 30), 32) + rl));
            }
        } else if (t == "organizer") {
            out.push_back("");
            add(out, organizers::text_lines(b, width));
        } else if (t == "passage") {
            out.push_back("");
            if (b.get("title").truthy()) out.push_back(text::rstrip(text::center(text_of(b.get("title")), width)));
            for (const auto &p : text::split(b.get("text").truthy() ? txt : "", "\n"))
                if (!text::strip(p).empty()) {
                    add(out, w(text::strip(p), 8, 4));
                    out.push_back("");
                }
        } else if (t == "rule") {
            out.push_back(rep("-", width));
        } else if (t == "grid") {
            const Value &cols = b.get("cols");
            std::vector<long> widths;
            for (const auto &c : cols.arr())
                widths.push_back(std::max(6L, (long)((double)(width - (long)cols.size() - 1) * c.at(1).as_double())));
            std::string sep = "+", head = "|", empty = "|";
            for (size_t i = 0; i < widths.size(); i++) {
                sep += rep("-", widths[i]) + "+";
                head += text::center(text_of(cols.at(i).at(0)), widths[i]) + "|";
                empty += rep(" ", widths[i]) + "|";
            }
            out.push_back("");
            out.push_back(sep);
            out.push_back(head);
            out.push_back(sep);
            for (long r = 0; r < b.get("rows").as_int(); r++) {
                out.push_back(empty);
                out.push_back(empty);
                out.push_back(sep);
            }
        } else if (t == "days") {
            for (const auto &d : b.get("days").arr()) {
                std::string label = text_of(d.at(0)), prompt = text_of(d.at(1));
                out.push_back("");
                out.push_back(text::ljust(text::upper(label), width - 20) + "Date: ______________");
                if (!text::strip(prompt).empty()) add(out, w(text::strip(prompt), 2, 2));
                for (int i = 0; i < 3; i++) out.push_back("  " + rep("_", width - 2));
            }
        } else if (t == "pagebreak") {
            out.push_back("");
            out.push_back(rep("=", width));
            out.push_back(text::rstrip(text::center("(NEXT PAGE)", width)));
            out.push_back("");
        }
    }
    if (doc.get("footer").truthy()) {
        out.push_back("");
        out.push_back(rep("-", width));
        out.push_back(text_of(doc.get("footer")));
    }
    return out;
}

bool render_txt(const Value &doc, const std::string &path, std::string *err) {
    // Python writes "\n" as is everywhere except Windows, where text files get "\r\n"
    std::string body = text::join(render_lines(doc), "\n") + "\n";
#ifdef _WIN32
    body = text::replace(body, "\n", "\r\n");
#endif
    return files::write(path, body, err);
}

}  // namespace export_txt
