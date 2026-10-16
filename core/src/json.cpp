#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

static const std::string EMPTY_STR;
static const Array EMPTY_ARR;
static const Object EMPTY_OBJ;
static const Value NULL_VALUE;

Value &Value::operator=(const Value &v) {
    if (this == &v) return *this;
    t_ = v.t_;
    b_ = v.b_;
    i_ = v.i_;
    d_ = v.d_;
    s_ = v.s_ ? std::make_shared<std::string>(*v.s_) : nullptr;
    a_ = v.a_ ? std::make_shared<Array>(*v.a_) : nullptr;
    o_ = v.o_ ? std::make_shared<Object>(*v.o_) : nullptr;
    return *this;
}

bool Value::truthy() const {
    switch (t_) {
        case NUL: return false;
        case BOOL: return b_;
        case INT: return i_ != 0;
        case FLOAT: return d_ != 0;
        case STRING: return !s_->empty();
        case ARRAY: return !a_->empty();
        case OBJECT: return !o_->empty();
    }
    return false;
}

int64_t Value::as_int(int64_t dflt) const {
    if (t_ == INT) return i_;
    if (t_ == BOOL) return b_ ? 1 : 0;
    if (t_ == FLOAT && std::isfinite(d_)) return (int64_t)d_;   // int(2.7) == 2, like Python
    return dflt;
}

double Value::as_double(double dflt) const {
    if (t_ == FLOAT) return d_;
    if (t_ == INT) return (double)i_;
    if (t_ == BOOL) return b_ ? 1 : 0;
    return dflt;
}

const std::string &Value::str() const { return t_ == STRING ? *s_ : EMPTY_STR; }

std::string &Value::str_ref() {
    if (t_ != STRING) *this = Value(std::string());
    return *s_;
}

const Array &Value::arr() const & { return t_ == ARRAY ? *a_ : EMPTY_ARR; }

Array &Value::arr_ref() {
    if (t_ != ARRAY) *this = Value(Array{});
    return *a_;
}

size_t Value::size() const {
    if (t_ == ARRAY) return a_->size();
    if (t_ == OBJECT) return o_->size();
    if (t_ == STRING) return s_->size();
    return 0;
}

const Value &Value::at(size_t i) const & {
    if (t_ != ARRAY || i >= a_->size()) return NULL_VALUE;
    return (*a_)[i];
}

Value &Value::at(size_t i) & { return arr_ref().at(i); }

Value Value::at_copy(size_t i) const { return at(i); }
Value Value::get_copy(const std::string &key) const { return get(key); }

const Object &Value::obj() const & { return t_ == OBJECT ? *o_ : EMPTY_OBJ; }

Object &Value::obj_ref() {
    if (t_ != OBJECT) *this = Value(Object{});
    return *o_;
}

const Value *Value::find(const std::string &key) const {
    if (t_ != OBJECT) return nullptr;
    for (const auto &m : *o_)
        if (m.first == key) return &m.second;
    return nullptr;
}

Value *Value::find(const std::string &key) {
    if (t_ != OBJECT) return nullptr;
    for (auto &m : *o_)
        if (m.first == key) return &m.second;
    return nullptr;
}

const Value &Value::get(const std::string &key) const & {
    const Value *v = find(key);
    return v ? *v : NULL_VALUE;
}

Value &Value::operator[](const std::string &key) {
    if (Value *v = find(key)) return *v;
    Object &o = obj_ref();
    o.emplace_back(key, Value());
    return o.back().second;
}

bool Value::erase(const std::string &key) {
    if (t_ != OBJECT) return false;
    for (auto it = o_->begin(); it != o_->end(); ++it)
        if (it->first == key) { o_->erase(it); return true; }
    return false;
}

Value &Value::setdefault(const std::string &key, Value v) {
    if (Value *x = find(key)) return *x;
    Object &o = obj_ref();
    o.emplace_back(key, std::move(v));
    return o.back().second;
}

std::string Value::s(const std::string &key) const {
    const Value *v = find(key);
    return v && v->is_string() ? *v->s_ : std::string();
}

bool Value::operator==(const Value &o) const {
    // Python: 1 == 1.0 == True; containers compare item by item (dicts ignore order)
    if (is_number() || is_bool()) {
        if (!(o.is_number() || o.is_bool())) return false;
        if (t_ == FLOAT || o.t_ == FLOAT) return as_double() == o.as_double();
        return as_int() == o.as_int();
    }
    if (t_ != o.t_) return false;
    switch (t_) {
        case NUL: return true;
        case STRING: return *s_ == *o.s_;
        case ARRAY: return *a_ == *o.a_;
        case OBJECT: {
            if (o_->size() != o.o_->size()) return false;
            for (const auto &m : *o_) {
                const Value *x = o.find(m.first);
                if (!x || !(m.second == *x)) return false;
            }
            return true;
        }
        default: return false;
    }
}

// ----------------------------------------------------------------------------- parsing

namespace {

struct Parser {
    const std::string &s;
    size_t p = 0;

    [[noreturn]] void fail(const std::string &what, size_t at) {
        // line and column the way Python counts them (column = characters, 1-based)
        size_t line = 1, col_start = 0;
        for (size_t i = 0; i < at && i < s.size(); i++)
            if (s[i] == '\n') { line++; col_start = i + 1; }
        size_t col = 1, chars = 0;
        for (size_t i = col_start; i < at && i < s.size(); i++)
            if (((unsigned char)s[i] & 0xC0) != 0x80) col++;
        for (size_t i = 0; i < at && i < s.size(); i++)
            if (((unsigned char)s[i] & 0xC0) != 0x80) chars++;
        throw ParseError{what + ": line " + std::to_string(line) + " column " + std::to_string(col) + " (char " + std::to_string(chars) + ")"};
    }

    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
    }

    bool lit(const char *w) {
        size_t n = std::strlen(w);
        if (s.compare(p, n, w) == 0) { p += n; return true; }
        return false;
    }

    static void put_utf8(std::string &out, uint32_t c) {
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
        }
    }

    int hex4(size_t at) {
        if (at + 4 > s.size()) return -1;
        int v = 0;
        for (size_t i = at; i < at + 4; i++) {
            char c = s[i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return -1;
            v = v * 16 + d;
        }
        return v;
    }

    std::string string() {
        size_t start = p;   // at the opening quote
        p++;
        std::string out;
        for (;;) {
            if (p >= s.size()) fail("Unterminated string starting at", start);
            unsigned char c = (unsigned char)s[p];
            if (c == '"') { p++; return out; }
            if (c < 0x20) fail("Invalid control character at", p);
            if (c != '\\') {
                size_t q = p;
                while (q < s.size() && s[q] != '"' && s[q] != '\\' && (unsigned char)s[q] >= 0x20) q++;
                out.append(s, p, q - p);
                p = q;
                continue;
            }
            if (p + 1 >= s.size()) fail("Unterminated string starting at", start);
            char e = s[p + 1];
            switch (e) {
                case '"': out += '"'; p += 2; break;
                case '\\': out += '\\'; p += 2; break;
                case '/': out += '/'; p += 2; break;
                case 'b': out += '\b'; p += 2; break;
                case 'f': out += '\f'; p += 2; break;
                case 'n': out += '\n'; p += 2; break;
                case 'r': out += '\r'; p += 2; break;
                case 't': out += '\t'; p += 2; break;
                case 'u': {
                    int u = hex4(p + 2);
                    if (u < 0) fail("Invalid \\uXXXX escape", p + 1);
                    p += 6;
                    uint32_t cp = (uint32_t)u;
                    if (u >= 0xD800 && u <= 0xDBFF && p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        int lo = hex4(p + 2);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + (((uint32_t)u - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
                            p += 6;
                        }
                    }
                    put_utf8(out, cp);   // a lone surrogate is kept as-is (like Python), written as WTF-8
                    break;
                }
                default: fail("Invalid \\escape", p);
            }
        }
    }

    Value number() {
        size_t start = p;
        if (s[p] == '-') p++;
        if (p < s.size() && s[p] == '0') p++;
        else if (p < s.size() && s[p] >= '1' && s[p] <= '9') while (p < s.size() && isdigit((unsigned char)s[p])) p++;
        else {
            p = start;
            if (lit("-Infinity")) return Value(-INFINITY);
            fail("Expecting value", start);
        }
        bool frac = false;
        if (p < s.size() && s[p] == '.' && p + 1 < s.size() && isdigit((unsigned char)s[p + 1])) {
            frac = true;
            p++;
            while (p < s.size() && isdigit((unsigned char)s[p])) p++;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            size_t q = p + 1;
            if (q < s.size() && (s[q] == '+' || s[q] == '-')) q++;
            if (q < s.size() && isdigit((unsigned char)s[q])) {
                frac = true;
                p = q;
                while (p < s.size() && isdigit((unsigned char)s[p])) p++;
            }
        }
        std::string t = s.substr(start, p - start);
        if (!frac) {
            int64_t v = 0;
            auto r = std::from_chars(t.data(), t.data() + t.size(), v);
            if (r.ec == std::errc()) return Value((long long)v);
            // bigger than 64 bits: keep it as a float (Python would keep every digit)
        }
        return Value(std::strtod(t.c_str(), nullptr));
    }

    Value value(int depth) {
        if (depth > 900) fail("Too deeply nested", p);   // Python stops near here too (RecursionError)
        ws();
        if (p >= s.size()) fail("Expecting value", p);
        char c = s[p];
        if (c == '"') return Value(string());
        if (c == '{') {
            size_t start = p++;
            Value v = Value::object();
            ws();
            if (p < s.size() && s[p] == '}') { p++; return v; }
            for (;;) {
                ws();
                if (p >= s.size() || s[p] != '"') fail("Expecting property name enclosed in double quotes", p);
                std::string k = string();
                ws();
                if (p >= s.size() || s[p] != ':') fail("Expecting ':' delimiter", p);
                p++;
                Value item = value(depth + 1);
                v[k] = std::move(item);   // a repeated key: first place, last value (like a dict)
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == '}') { p++; return v; }
                (void)start;
                fail("Expecting ',' delimiter", p);
            }
        }
        if (c == '[') {
            p++;
            Value v = Value::array();
            ws();
            if (p < s.size() && s[p] == ']') { p++; return v; }
            for (;;) {
                v.push(value(depth + 1));
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == ']') { p++; return v; }
                fail("Expecting ',' delimiter", p);
            }
        }
        if (lit("null")) return Value();
        if (lit("true")) return Value(true);
        if (lit("false")) return Value(false);
        if (lit("NaN")) return Value(NAN);
        if (lit("Infinity")) return Value(INFINITY);
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        fail("Expecting value", p);
    }
};

}  // namespace

Value parse(const std::string &text, bool skip_bom) {
    Parser ps{text};
    if (skip_bom && text.compare(0, 3, "\xEF\xBB\xBF") == 0) ps.p = 3;
    Value v = ps.value(0);
    ps.ws();
    if (ps.p != text.size()) ps.fail("Extra data", ps.p);
    return v;
}

// ----------------------------------------------------------------------------- writing

std::string py_float(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    // shortest round-trip digits, then Python's choice of fixed or exponent notation
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
    std::string sci(buf, r.ptr);
    bool neg = sci[0] == '-';
    if (neg) sci.erase(0, 1);
    size_t e = sci.find('e');
    std::string mant = sci.substr(0, e);
    int exp = std::atoi(sci.c_str() + e + 1);
    std::string digits;
    for (char c : mant) if (c != '.') digits += c;
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    int decpt = exp + 1;   // digits "123", decpt 1 -> 1.23
    std::string out;
    if (decpt <= -4 || decpt > 16) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        int x = decpt - 1;
        char eb[16];
        std::snprintf(eb, sizeof eb, "e%c%02d", x < 0 ? '-' : '+', x < 0 ? -x : x);
        out += eb;
    } else if (decpt <= 0) {
        out = "0." + std::string((size_t)-decpt, '0') + digits;
    } else if ((size_t)decpt >= digits.size()) {
        out = digits + std::string((size_t)decpt - digits.size(), '0') + ".0";
    } else {
        out = digits.substr(0, (size_t)decpt) + "." + digits.substr((size_t)decpt);
    }
    return neg ? "-" + out : out;
}

static void dump_string(std::string &o, const std::string &s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else o += (char)c;
        }
    }
    o += '"';
}

static void dump_to(std::string &o, const Value &v, int indent, int level, const char *item_sep, const char *key_sep) {
    switch (v.type()) {
        case NUL: o += "null"; return;
        case BOOL: o += v.as_bool() ? "true" : "false"; return;
        case INT: o += std::to_string(v.as_int()); return;
        case FLOAT: o += py_float(v.as_double()); return;
        case STRING: dump_string(o, v.str()); return;
        case ARRAY: {
            const Array &a = v.arr();
            if (a.empty()) { o += "[]"; return; }
            o += '[';
            for (size_t i = 0; i < a.size(); i++) {
                if (i) o += item_sep;
                if (indent >= 0) { o += '\n'; o.append((size_t)(indent * (level + 1)), ' '); }
                dump_to(o, a[i], indent, level + 1, item_sep, key_sep);
            }
            if (indent >= 0) { o += '\n'; o.append((size_t)(indent * level), ' '); }
            o += ']';
            return;
        }
        case OBJECT: {
            const Object &ob = v.obj();
            if (ob.empty()) { o += "{}"; return; }
            o += '{';
            for (size_t i = 0; i < ob.size(); i++) {
                if (i) o += item_sep;
                if (indent >= 0) { o += '\n'; o.append((size_t)(indent * (level + 1)), ' '); }
                dump_string(o, ob[i].first);
                o += key_sep;
                dump_to(o, ob[i].second, indent, level + 1, item_sep, key_sep);
            }
            if (indent >= 0) { o += '\n'; o.append((size_t)(indent * level), ' '); }
            o += '}';
            return;
        }
    }
}

std::string dump(const Value &v, int indent) {
    std::string o;
    // with an indent Python's item separator is "," (no trailing space); without one it's ", "
    dump_to(o, v, indent, 0, indent >= 0 ? "," : ", ", ": ");
    return o;
}

std::string dump_compact(const Value &v) {
    std::string o;
    dump_to(o, v, -1, 0, ",", ":");
    return o;
}

}  // namespace json
