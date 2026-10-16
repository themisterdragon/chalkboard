// A small JSON value that keeps keys in order, reads what Python's json module reads, and writes
// exactly what Python's json.dump(obj, f, indent=1, ensure_ascii=False) writes. That way a data.json
// saved by this core and one saved by the Python app are byte-for-byte the same.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace json {

class Value;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;

enum Type { NUL, BOOL, INT, FLOAT, STRING, ARRAY, OBJECT };

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : t_(BOOL), b_(b) {}
    Value(int i) : t_(INT), i_(i) {}
    Value(long i) : t_(INT), i_(i) {}
    Value(long long i) : t_(INT), i_(i) {}
    Value(size_t i) : t_(INT), i_((int64_t)i) {}
    Value(double d) : t_(FLOAT), d_(d) {}
    Value(const char *s) : t_(STRING), s_(std::make_shared<std::string>(s)) {}
    Value(std::string s) : t_(STRING), s_(std::make_shared<std::string>(std::move(s))) {}
    Value(Array a) : t_(ARRAY), a_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : t_(OBJECT), o_(std::make_shared<Object>(std::move(o))) {}
    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    // copies are deep (like copy.deepcopy), so changing one never changes another
    Value(const Value &v) { *this = v; }
    Value &operator=(const Value &v);
    Value(Value &&) = default;
    Value &operator=(Value &&) = default;

    Type type() const { return t_; }
    bool is_null() const { return t_ == NUL; }
    bool is_bool() const { return t_ == BOOL; }
    bool is_int() const { return t_ == INT; }
    bool is_float() const { return t_ == FLOAT; }
    bool is_number() const { return t_ == INT || t_ == FLOAT; }
    bool is_string() const { return t_ == STRING; }
    bool is_array() const { return t_ == ARRAY; }
    bool is_object() const { return t_ == OBJECT; }

    // Python truthiness: None, False, 0, "", [], {} are false
    bool truthy() const;

    bool as_bool() const { return t_ == BOOL ? b_ : truthy(); }
    int64_t as_int(int64_t dflt = 0) const;        // ints, whole floats, and bools; else dflt
    double as_double(double dflt = 0) const;
    const std::string &str() const;                // "" unless a string
    std::string &str_ref();                        // makes it a string if it isn't

    // arrays
    // Lists and members can't be read out of a temporary value: the loop would read freed memory.
    // Keep the value in a variable first. (The compiler enforces this.)
    const Array &arr() const &;                    // empty unless an array
    void arr() const && = delete;
    Array &arr_ref();                              // makes it an array if it isn't
    size_t size() const;
    const Value &at(size_t i) const &;            // null when out of range
    Value at(size_t i) const && { return at_copy(i); }
    Value at_copy(size_t i) const;
    Value &at(size_t i) &;
    void push(Value v) { arr_ref().push_back(std::move(v)); }

    // objects
    const Object &obj() const &;
    void obj() const && = delete;
    Object &obj_ref();
    bool has(const std::string &key) const { return find(key) != nullptr; }
    const Value *find(const std::string &key) const;
    Value *find(const std::string &key);
    const Value &get(const std::string &key) const &;   // null when missing (like dict.get)
    Value get(const std::string &key) const && { return get_copy(key); }
    Value get_copy(const std::string &key) const;
    Value &operator[](const std::string &key);        // inserts null at the end when missing
    Value &operator[](const char *key) { return (*this)[std::string(key)]; }
    const Value &operator[](const std::string &key) const { return get(key); }
    const Value &operator[](const char *key) const { return get(std::string(key)); }
    void set(const std::string &key, Value v) { (*this)[key] = std::move(v); }
    bool erase(const std::string &key);
    // dict.setdefault
    Value &setdefault(const std::string &key, Value v);
    // x.get(key) or "" as a string ("" when missing, null, or not a string)
    std::string s(const std::string &key) const;

    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const { return !(*this == o); }

private:
    Type t_ = NUL;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0;
    std::shared_ptr<std::string> s_;
    std::shared_ptr<Array> a_;
    std::shared_ptr<Object> o_;
};

struct ParseError {
    std::string message;   // like Python's: "Expecting value: line 1 column 1 (char 0)"
};

// Parses like json.loads (NaN/Infinity allowed, control characters in strings refused, a
// repeated key keeps its first place with the last value). A leading UTF-8 BOM is skipped when
// skip_bom is set (Python's "utf-8-sig"). Throws ParseError.
Value parse(const std::string &text, bool skip_bom = false);

// json.dumps(v, indent=indent, ensure_ascii=False). indent < 0 = one line with ", " and ": ".
std::string dump(const Value &v, int indent = 1);
// Compact: separators (",", ":"), for messages between programs.
std::string dump_compact(const Value &v);

// float -> Python repr(): 0.1, 1.0, 1e-05, 1e+16
std::string py_float(double d);

}  // namespace json
