#include "store.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <map>
#include <set>

#include "files.h"
#include "pyrand.h"
#include "resources.h"
#include "text.h"
#include "version.h"

namespace store {

const char *DEFAULT_SUBJECT = "Reading & Writing";
const char *ALL = "ALL";
const std::vector<std::string> GRADE_CHOICES = {"K", "1", "2", "3", "4", "5", "6", "7", "8", "9-10", "11-12", "9-12", "ALL"};
const char *GOOD_THINGS_PREFIX = "Good Things: ";
const char *BACKUP_KIND = "chalkboard-backup";
const char *MY_STANDARDS = "My Own Standards";
const char *LOGO_FILE = "logo.json";
static const char *STANDARDS_DIR = "data/standards";
static const char *GOOD_THINGS_FILE = "data/good_things.json";
static const std::vector<std::string> LOCAL_SETTINGS = {"export_dir", "backup_dir", "last_backup"};

// ----------------------------------------------------------------------------- small helpers

std::string text_of(const Value &v) {
    switch (v.type()) {
        case json::NUL: return "None";
        case json::BOOL: return v.as_bool() ? "True" : "False";
        case json::INT: return std::to_string(v.as_int());
        case json::FLOAT: {
            double d = v.as_double();
            if (std::isnan(d)) return "nan";
            if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
            return json::py_float(d);
        }
        case json::STRING: return v.str();
        default: return json::dump(v, -1);
    }
}

std::string field(const Value &x, const std::string &key) {
    const Value &v = x.get(key);
    if (!v.truthy()) return "";
    return v.is_string() ? v.str() : text_of(v);
}

static Value list_of(const std::vector<std::string> &xs) {
    Value a = Value::array();
    for (const auto &x : xs) a.push(x);
    return a;
}

static bool contains(const std::vector<std::string> &xs, const std::string &x) {
    return std::find(xs.begin(), xs.end(), x) != xs.end();
}

static std::string today_iso() { return strftime_now("%Y-%m-%d"); }

// ----------------------------------------------------------------------------- where things live

bool is_beta() {
    for (const char *p = CHALKBOARD_VERSION; *p; p++)
        if (*p >= 'a' && *p <= 'z') return true;   // 2.1.0b1, 2.1.0rc1: a test build
    return false;
}

std::string data_dir(int beta) {
    std::string forced = files::env("CHALKBOARD_DATA");
    if (!forced.empty()) return files::expanduser(forced);
    bool b = beta < 0 ? is_beta() : beta != 0;
    // a 3.x test build: a folder of its own (the 2.1 betas' "chalkboard-beta" may hold old data)
    std::string name = b ? "chalkboard-3-beta" : "chalkboard";
    std::string base;
#if defined(_WIN32)
    base = files::env("APPDATA");
    if (base.empty()) base = files::home();
#elif defined(__APPLE__)
    base = files::join(files::home(), "Library/Application Support");
#else
    base = files::env("XDG_DATA_HOME");
    if (base.empty()) base = files::join(files::home(), ".local/share");
#endif
    return files::join(base, name);
}

static void copy_tree(const std::string &from, const std::string &to) {
    files::make_dirs(to);
    for (const auto &name : files::list(from)) {
        if (name == "backups" || name == "plugins" || text::ends_with(name, ".tmp")) continue;
        std::string a = files::join(from, name), b = files::join(to, name);
        if (files::is_dir(a)) copy_tree(a, b);
        else files::copy(a, b);
    }
}

void seed_beta() {
    if (!is_beta() || !files::env("CHALKBOARD_DATA").empty()) return;
    std::string beta = data_dir(), regular = data_dir(0);
    if (files::exists(beta) || !files::is_dir(regular)) return;
    copy_tree(regular, beta);
}

// a test build (not one pointed at a data folder) exports and backs up somewhere of its own, so
// trying it can never overwrite the real app's files
static bool beta_files() { return is_beta() && files::env("CHALKBOARD_DATA").empty(); }

std::string default_export_dir() {
    std::string docs = files::join(files::home(), "Documents");
    return files::join(files::is_dir(docs) ? docs : files::home(), beta_files() ? "Chalkboard Beta" : "Chalkboard");
}

std::string default_backup_dir() { return files::join(default_export_dir(), "Backups"); }

// ----------------------------------------------------------------------------- settings

static Value make_default_settings() {
    Value s = Value::object();
    s["teacher"] = "";
    s["school"] = "";
    s["course"] = "English 10";
    s["grades"] = "9-10";
    s["subject"] = DEFAULT_SUBJECT;
    s["font"] = "Times";
    s["page"] = "Letter";
    s["theme"] = "green";
    s["uppercase"] = true;
    s["boot"] = true;
    s["mascot"] = "";
    s["export_dir"] = "";
    s["export_format"] = "PDF";
    s["export_include"] = "BOTH";
    s["export_versions"] = 1;
    s["lesson_export_format"] = "PDF";
    s["board_style"] = "chalk";
    s["board_sections"] = list_of({"question", "standards", "targets", "success", "vocab", "bell_ringer", "materials", "homework", "class_codes"});
    s["board_v"] = 2;
    s["lesson_hide"] = list_of({"question", "vocab"});
    s["board_std_text"] = true;
    s["primary_color"] = "";
    s["secondary_color"] = "";
    s["text_color"] = "";
    s["default_materials"] = "";
    s["lesson_sort"] = "updated";
    s["assess_sort"] = "updated";
    s["logo_place"] = "left";
    s["board_font"] = "";
    s["board_head_font"] = "";
    s["board_title_align"] = "left";
    s["board_codes_place"] = "right";
    s["board_layout"] = "auto";
    s["board_panels"] = "cards";
    s["board_big_text"] = false;
    s["board_sides"] = Value::object();
    s["board_order"] = Value::array();
    s["class_periods"] = Value::array();
    s["advanced"] = false;
    s["page_layouts"] = Value::object();
    s["backup_dir"] = "";
    s["last_backup"] = "";
    return s;
}

const Value &default_settings() {
    static const Value d = make_default_settings();
    return d;
}

const std::vector<std::pair<std::string, std::string>> SORTS = {
    {"updated", "DATE MODIFIED"}, {"created", "DATE CREATED"}, {"scheduled", "DATE SCHEDULED"}, {"title", "TITLE (A-Z)"},
    {"unit", "UNIT"}, {"class", "BY CLASS"}};

const std::vector<BoardSection> BOARD_SECTIONS = {
    {"question", "Essential Question", "top"}, {"standards", "Standards", "left"}, {"targets", "I Can...", "left"},
    {"success", "Success Criteria", "left"},   {"bell_ringer", "Bell Ringer", "right"}, {"materials", "Materials", "right"},
    {"vocab", "Words to Know", "right"},        {"homework", "Homework", "right"},       {"closure", "Exit Ticket", "right"},
    {"class_codes", "Class Codes", "right"}};
const std::vector<std::pair<std::string, std::string>> BOARD_SIDES = {{"top", "across the top"}, {"left", "left side"}, {"right", "right side"}};
const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> BOARD_CHOICES = {
    {"board_layout", {{"auto", "Balanced (moves sections to fit)"}, {"sides", "Keep each section on its side"}, {"one", "One column"}}},
    {"board_panels", {{"cards", "Cards"}, {"outline", "Outlined boxes"}, {"chalk", "Plain chalk (no boxes)"}}},
    {"board_title_align", {{"left", "Left, date on the right"}, {"center", "Centered, date underneath"}}},
    {"board_codes_place", {{"right", "Bottom right"}, {"left", "Bottom left"}}}};
const std::vector<std::pair<std::string, std::string>> BOARD_STYLES = {
    {"chalk", "CHALKBOARD (DARK GREEN)"}, {"white", "WHITEBOARD (WHITE)"}, {"school", "SCHOOL COLORS"}};
const std::vector<std::pair<std::string, std::string>> LOGO_PLACES = {{"left", "LEFT OF THE TITLE"}, {"right", "TOP RIGHT CORNER"}};

std::vector<BoardSection> board_sections(const Value &settings) {
    const Value &sides = settings.get("board_sides");
    std::vector<std::string> order;
    for (const auto &k : settings.get("board_order").arr()) order.push_back(k.str());
    std::map<std::string, size_t> rank;
    for (size_t i = 0; i < order.size(); i++) rank[order[i]] = i;   // a repeated key: its last place
    std::vector<BoardSection> out;
    for (const auto &b : BOARD_SECTIONS) {
        std::string side = sides.is_object() ? sides.get(b.key).str() : "";
        out.push_back({b.key, b.label, b.col != "top" && (side == "left" || side == "right") ? side : b.col});
    }
    auto pos = [&](const BoardSection &s) {
        auto it = rank.find(s.key);
        if (it != rank.end()) return it->second;
        size_t d = 0;
        for (size_t i = 0; i < BOARD_SECTIONS.size(); i++) if (BOARD_SECTIONS[i].key == s.key) d = i;
        return rank.size() + d;
    };
    std::stable_sort(out.begin(), out.end(), [&](const BoardSection &a, const BoardSection &b) {
        bool at = a.col != "top", bt = b.col != "top";
        if (at != bt) return !at;
        return pos(a) < pos(b);
    });
    return out;
}

bool move_board_section(Value &settings, const std::string &key, int step) {
    std::vector<std::string> keys;
    for (const auto &s : board_sections(settings)) if (s.col != "top") keys.push_back(s.key);
    auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end()) return false;
    long i = it - keys.begin(), j = i + step;
    if (j < 0 || j >= (long)keys.size()) return false;
    std::swap(keys[(size_t)i], keys[(size_t)j]);
    settings["board_order"] = list_of(keys);
    return true;
}

std::vector<Value> periods_for(const Value &settings, const Value &lesson) {
    std::string course = text::lower(text::strip(field(lesson, "course")));
    std::vector<Value> out;
    for (const auto &p : settings.get("class_periods").arr()) {
        std::string pc = text::lower(text::strip(field(p, "course")));
        if (!text::strip(field(p, "codes")).empty() && (pc.empty() || pc == course)) out.push_back(p);
    }
    return out;
}

std::string home_day_name(const Value &settings) {
    std::string n = text::strip(field(settings, "home_day_name"));
    return n.empty() ? "At-Home Day" : n;
}

std::vector<Date> lesson_dates(const Value &x) {
    Date near;
    if (!dates::stamp_day(field(x, "created"), near) && !dates::stamp_day(field(x, "updated"), near)) near = today();
    return dates::parse_dates(field(x, "date"), near);
}

std::vector<std::pair<Value, const Value *>> day_plan(const Value &settings, const std::vector<const Value *> &lessons, const Date &day) {
    std::vector<std::pair<Value, const Value *>> out;
    for (const auto &p : settings.get("class_periods").arr()) {
        std::string course = text::lower(text::strip(field(p, "course")));
        std::vector<const Value *> fits;
        for (const Value *l : lessons) {
            auto ds = lesson_dates(*l);
            if (std::find(ds.begin(), ds.end(), day) == ds.end()) continue;
            if (!course.empty() && text::lower(text::strip(field(*l, "course"))) != course) continue;
            fits.push_back(l);
        }
        std::stable_sort(fits.begin(), fits.end(), [](const Value *a, const Value *b) { return a->s("updated") > b->s("updated"); });
        out.push_back({p, fits.empty() ? nullptr : fits[0]});
    }
    return out;
}

bool grade_span(const std::string &g0, int &lo, int &hi) {
    std::string g = text::strip(g0);
    auto num = [](const std::string &t, int &v) {
        if (t == "K") { v = 0; return true; }
        if (t.empty()) return false;
        for (char c : t) if (c < '0' || c > '9') return false;
        v = std::atoi(t.c_str());
        return true;
    };
    size_t dash = g.find('-');
    if (dash == std::string::npos) {
        if (!num(g, lo)) return false;
        hi = lo;
        return true;
    }
    return num(g.substr(0, dash), lo) && num(g.substr(dash + 1), hi);
}

bool grades_match(const std::string &std_grades, const std::string &wanted) {
    if (wanted == ALL) return true;
    int a0, a1, b0, b1;
    if (!grade_span(std_grades, a0, a1) || !grade_span(wanted, b0, b1)) return true;
    return a0 <= b1 && b0 <= a1;
}

std::string parse_hex(const std::string &s0) {
    std::string s = text::lstrip_chars(text::strip(s0), "#");
    if (s.size() == 3) {
        std::string t;
        for (char c : s) { t += c; t += c; }
        s = t;
    }
    if (s.size() != 6) return "";
    for (char c : s) if (!isxdigit((unsigned char)c)) return "";
    return "#" + text::upper(s);
}

std::string clean_path(const std::string &path0) {
    std::string path = text::strip(path0);
    std::u32string u = text::to32(path);
    if (u.size() > 1 && u.front() == u.back() && (u.front() == '\'' || u.front() == '"')) path = text::to8(u.substr(1, u.size() - 2));
    return files::expanduser(text::replace(path, "\\ ", " "));
}

// ----------------------------------------------------------------------------- data files

Value normalize_data(Value d) {
    if (!d.is_object()) d = Value::object();
    Value settings = default_settings();
    const Value &raw = d.get("settings");
    bool has_raw = raw.is_object() && raw.truthy();
    Value old = has_raw ? raw : Value();
    if (raw.is_object())
        for (const auto &m : raw.obj()) settings[m.first] = m.second;
    d["settings"] = settings;
    for (const char *k : {"lessons", "assessments", "custom_standards"})
        if (!d.get(k).is_array()) d[k] = Value::array();
    for (const char *k : {"lessons", "assessments"})
        for (auto &x : d[k].arr_ref())
            if (x.is_object()) x.setdefault("created", x.get("updated").is_null() && !x.has("updated") ? Value("") : x.get("updated"));
    for (auto &l : d["lessons"].arr_ref())
        if (l.is_object()) l.setdefault("assessments", Value::array());
    Value &st = d["settings"];
    if (has_raw && old.get("board_v").as_double(old.has("board_v") ? 2 : 1) < 2 && st["board_sections"].is_array()) {
        json::Array &on = st["board_sections"].arr_ref();
        auto has = [&](const std::string &k) {
            for (const auto &v : on) if (v.is_string() && v.str() == k) return true;
            return false;
        };
        auto index_of = [&](const std::string &k) {
            for (size_t i = 0; i < on.size(); i++) if (on[i].is_string() && on[i].str() == k) return i;
            return on.size();
        };
        if (!has("question")) on.insert(on.begin(), Value("question"));
        if (!has("vocab")) {
            size_t at = has("success") ? index_of("success") + 1 : on.size();
            on.insert(on.begin() + (long)at, Value("vocab"));
        }
    }
    for (const char *key : {"board_font", "board_head_font"}) {
        std::string f = st.get(key).str();
        if (f == "Chalkboard Pixel" || f == "Chalkboard Chalk" || f == "Chalkboard Marquee") st[key] = "";
    }
    return d;
}

// Strict UTF-8, like Python's decoder: no overlong forms, surrogates, or values past U+10FFFF.
static bool valid_utf8(const std::string &s, std::string &why) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool ok = len > 0 && i + len <= n;
        for (size_t k = 1; ok && k < len; k++) ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
        if (ok && len == 3) {
            unsigned char d = (unsigned char)s[i + 1];
            if ((c == 0xE0 && d < 0xA0) || (c == 0xED && d >= 0xA0)) ok = false;
        }
        if (ok && len == 4) {
            unsigned char d = (unsigned char)s[i + 1];
            if ((c == 0xF0 && d < 0x90) || (c == 0xF4 && d >= 0x90)) ok = false;
        }
        if (!ok) {
            char b[120];
            std::snprintf(b, sizeof b, "'utf-8' codec can't decode byte 0x%02x in position %zu: %s", c, i,
                          len == 0 ? "invalid start byte" : "invalid continuation byte");
            why = b;
            return false;
        }
        i += len;
    }
    return true;
}

// open(path, encoding=...).read(): text mode turns \r\n and \r into \n
static std::string universal_newlines(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') i++;
        } else out += s[i];
    }
    return out;
}

Backup read_backup(const std::string &path0) {
    std::string path = clean_path(path0), raw_text, err;
    if (!files::is_file(path)) throw UserError{"NO FILE AT " + path};
    std::string name = files::basename(path);
    Value raw;
    if (!files::read(path, raw_text, &err)) throw UserError{"COULDN'T READ " + name + ": " + err};
    std::string why;
    if (!valid_utf8(raw_text, why)) throw UserError{"COULDN'T READ " + name + ": " + why};
    try {
        raw = json::parse(universal_newlines(raw_text), true);
    } catch (json::ParseError &e) {
        throw UserError{"COULDN'T READ " + name + ": " + e.message};
    }
    Backup b;
    bool is_raw = false;
    if (raw.is_object() && raw.get("kind").is_string() && raw.get("kind").str() == BACKUP_KIND && raw.get("data").is_object()) {
        b.data = raw.get("data");
        const Value &s = raw.get("standards");
        if (s.truthy() && !s.is_array()) throw UserError{"THE STANDARDS IN " + name + " ARE DAMAGED"};
        b.standards = s.is_array() ? s : Value::array();
        b.made = field(raw, "made");
    } else if (raw.is_object() && (raw.has("lessons") || raw.has("assessments"))) {
        b.data = raw;
        b.standards = Value::array();
        is_raw = true;
    } else throw UserError{name + " ISN'T A CHALKBOARD BACKUP"};
    for (const auto &d : b.standards.arr())
        if (!(d.is_object() && d.get("subject").truthy() && d.get("standards").is_array())) throw UserError{"THE STANDARDS IN " + name + " ARE DAMAGED"};
    if (!is_raw && raw.get("logo").truthy()) {
        try {
            b.logo = images::from_json(raw.get("logo"));
            b.has_logo = true;
        } catch (images::ImageError &) {
            b.has_logo = false;   // a damaged logo shouldn't keep the lessons from coming back
        }
    }
    b.data = normalize_data(b.data);
    return b;
}

std::string subject_filename(const std::string &subject, const std::string &folder) {
    std::string low = text::lower(subject), base;
    bool run = false;
    for (unsigned char c : low) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) { base += (char)c; run = false; }
        else if (!run && (c < 0x80 || (c & 0xC0) == 0xC0)) { base += '_'; run = true; }
        else if (c >= 0x80 && (c & 0xC0) == 0x80) { /* rest of a multi-byte character: same run */ }
    }
    base = text::strip_chars(base, "_");
    if (base.empty()) base = "standards";
    std::string name = base + ".json";
    for (int n = 2; files::exists(files::join(folder, name)); n++) name = base + "_" + std::to_string(n) + ".json";
    return name;
}

static std::string cp1252_to_utf8(const std::string &s) {
    static const char32_t HIGH[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                      0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::u32string u;
    for (unsigned char c : s) u += c >= 0x80 && c < 0xA0 ? HIGH[c - 0x80] : (char32_t)c;
    return text::to8(u);
}

Value read_standards_file(const std::string &path0) {
    std::string path = clean_path(path0);
    if (!files::is_file(path)) throw UserError{"NO FILE AT " + path};
    std::string stem = files::stem(path), base = files::basename(path);
    std::string source = "Imported from " + base + " on " + today_iso() + ".";
    std::string raw, why;
    if (!files::read(path, raw)) throw UserError{"COULDN'T OPEN " + base};
    if (raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw = raw.substr(3);
    else if (!valid_utf8(raw, why)) raw = cp1252_to_utf8(raw);   // Excel's "CSV" (not "CSV UTF-8")
    raw = universal_newlines(raw);
    if (text::ends_with(text::lower(path), ".json")) {
        Value data;
        try {
            data = json::parse(raw);
        } catch (json::ParseError &e) {
            throw UserError{"NOT VALID JSON (" + e.message + ")"};
        }
        Value out = Value::array();
        if (data.is_array())
            for (const auto &d : data.arr()) out.push(json_subject(d, stem, source));
        else out.push(json_subject(data, stem, source));
        return out;
    }
    return csv_subjects(raw, stem, source);
}

Value json_subject(const Value &d, const std::string &stem, const std::string &source) {
    if (!d.is_object() || !d.get("standards").is_array()) throw UserError{"JSON NEEDS A \"standards\" LIST (SEE THE README)"};
    Value out = Value::object();
    out["subject"] = text::strip(d.get("subject").truthy() ? text_of(d.get("subject")) : stem);
    out["source"] = d.get("source").truthy() ? text_of(d.get("source")) : source;
    out["standards"] = Value::array();
    std::set<std::string> seen;
    size_t i = 0;
    for (const auto &s : d.get("standards").arr()) {
        i++;
        auto str_or = [&](const char *k) { return s.has(k) ? text_of(s.get(k)) : std::string(); };
        if (!s.is_object() || text::strip(str_or("code")).empty() || text::strip(str_or("text")).empty())
            throw UserError{"STANDARD #" + std::to_string(i) + " NEEDS A \"code\" AND A \"text\""};
        std::string code = text::strip(text_of(s.get("code")));
        if (seen.count(code)) throw UserError{"CODE " + code + " IS IN THE FILE TWICE"};
        seen.insert(code);
        Value subs = Value::array();
        if (s.get("subs").truthy()) {
            if (!s.get("subs").is_array()) throw UserError{"STANDARD #" + std::to_string(i) + " HAS A BROKEN \"subs\" LIST"};
            for (const auto &kv : s.get("subs").arr()) {
                if (!kv.is_array() || kv.size() != 2) throw UserError{"STANDARD #" + std::to_string(i) + " HAS A BROKEN \"subs\" LIST"};
                Value pair = Value::array();
                pair.push(text::strip(text_of(kv.at(0))));
                pair.push(text::squash(text_of(kv.at(1))));
                subs.push(pair);
            }
        }
        auto opt = [&](const char *k) { return s.get(k).truthy() ? text::strip(text_of(s.get(k))) : std::string(); };
        Value st = Value::object();
        st["code"] = code;
        st["grades"] = opt("grades");
        st["strand"] = opt("strand");
        st["cluster"] = opt("cluster");
        st["text"] = text::squash(text_of(s.get("text")));
        st["subs"] = subs;
        out["standards"].push(st);
    }
    if (out["standards"].size() == 0) throw UserError{"THE FILE HAS NO STANDARDS IN IT"};
    return out;
}

// csv.Sniffer's delimiter guess, restricted to , ; and tab: the one whose count per line is the most consistent
char sniff_delimiter(const std::string &sample, bool &skip_initial_space) {
    std::vector<std::string> lines = text::split(sample, "\n");
    const char cands[3] = {',', '\t', ';'};   // csv.Sniffer's preference order
    char best = 0;
    double best_score = -1;
    for (char d : cands) {
        std::map<int, int> freq;
        int total = 0;
        for (const auto &line : lines) {
            int n = 0;
            bool q = false;
            for (char c : line) {
                if (c == '"') q = !q;
                else if (c == d && !q) n++;
            }
            freq[n]++;
            total++;
        }
        if (!total) continue;
        int mode = 0, count = 0;
        for (auto &kv : freq)
            if (kv.first > 0 && kv.second > count) { mode = kv.first; count = kv.second; }
        if (!mode) continue;
        double consistency = (double)count / total;
        if (consistency >= 0.9 && consistency > best_score + 1e-9) { best = d; best_score = consistency; }
    }
    if (!best) best = ',';
    const std::string &first = lines.empty() ? std::string() : lines[0];
    size_t plain = 0, spaced = 0;
    for (size_t i = 0; i < first.size(); i++)
        if (first[i] == best) { plain++; if (i + 1 < first.size() && first[i + 1] == ' ') spaced++; }
    skip_initial_space = plain > 0 && plain == spaced;
    return best;
}

std::vector<std::vector<std::string>> read_csv(const std::string &raw, char delim, bool skip_initial_space) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    size_t i = 0, n = raw.size();
    bool any = false;
    while (i < n) {
        char c = raw[i];
        if (c == '"' && cell.empty()) {
            // a quoted field ("" is a quote); text after the closing quote joins it, like Python
            i++;
            for (;;) {
                if (i >= n) break;
                if (raw[i] == '"') {
                    if (i + 1 < n && raw[i + 1] == '"') { cell += '"'; i += 2; continue; }
                    i++;
                    break;
                }
                cell += raw[i++];
            }
            any = true;
            continue;
        }
        if (c == delim) {
            row.push_back(cell);
            cell.clear();
            i++;
            if (skip_initial_space) while (i < n && raw[i] == ' ') i++;
            any = true;
            continue;
        }
        if (c == '\n' || c == '\r') {
            row.push_back(cell);
            cell.clear();
            rows.push_back(row);
            row.clear();
            any = false;
            i += (c == '\r' && i + 1 < n && raw[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        cell += c;
        any = true;
        i++;
    }
    if (any || !cell.empty() || !row.empty()) {
        row.push_back(cell);
        rows.push_back(row);
    }
    // an empty line is an empty row ([]), like csv.reader
    for (auto &r : rows)
        if (r.size() == 1 && r[0].empty()) r.clear();
    return rows;
}

Value csv_subjects(const std::string &raw, const std::string &stem, const std::string &source) {
    bool skip = false;
    char delim = sniff_delimiter(raw.substr(0, 4096), skip);
    auto rows = read_csv(raw, delim, skip);
    if (rows.empty()) throw UserError{"THE FILE IS EMPTY"};
    std::vector<std::string> head;
    for (const auto &h : rows[0]) head.push_back(text::replace(text::lower(text::strip(h)), " ", "_"));
    if (!contains(head, "code") || !contains(head, "text")) throw UserError{"THE FIRST ROW NEEDS COLUMN NAMES, INCLUDING code AND text"};
    std::vector<std::string> order;   // subjects in first-seen order
    std::map<std::string, Value> docs;
    std::map<std::string, std::pair<std::string, size_t>> tops;   // code -> (subject, index)
    struct Part { std::string code, parent, text; size_t row; };
    std::vector<Part> parts;
    std::set<std::string> seen;
    for (size_t n = 1; n < rows.size(); n++) {
        std::map<std::string, std::string> r;
        for (size_t i = 0; i < head.size(); i++) r[head[i]] = i < rows[n].size() ? text::strip(rows[n][i]) : "";   // a repeated column: the last wins
        bool anything = false;
        for (auto &kv : r) if (!kv.second.empty()) anything = true;
        if (!anything) continue;
        std::string code = r["code"], txt = text::squash(r["text"]);
        size_t rown = n + 1;
        if (code.empty() || txt.empty()) throw UserError{"ROW " + std::to_string(rown) + " NEEDS A code AND A text"};
        std::string subj = r.count("subject") && !r["subject"].empty() ? r["subject"] : stem;
        if (seen.count(code)) throw UserError{"ROW " + std::to_string(rown) + ": CODE " + code + " IS IN THE FILE TWICE"};
        seen.insert(code);
        if (!docs.count(subj)) {
            Value d = Value::object();
            d["subject"] = subj;
            d["source"] = source;
            d["standards"] = Value::array();
            docs[subj] = d;
            order.push_back(subj);
        }
        std::string parent = r.count("part_of") && !r["part_of"].empty() ? r["part_of"] : (r.count("parent") ? r["parent"] : "");
        if (!parent.empty()) {
            parts.push_back({code, parent, txt, rown});
            continue;
        }
        Value s = Value::object();
        s["code"] = code;
        s["grades"] = r.count("grades") ? r["grades"] : "";
        s["strand"] = r.count("strand") ? r["strand"] : "";
        s["cluster"] = r.count("cluster") ? r["cluster"] : "";
        s["text"] = txt;
        s["subs"] = Value::array();
        docs[subj]["standards"].push(s);
        tops[code] = {subj, docs[subj]["standards"].size() - 1};
    }
    for (const auto &p : parts) {
        auto it = tops.find(p.parent);
        if (it == tops.end()) throw UserError{"ROW " + std::to_string(p.row) + ": part_of " + p.parent + " ISN'T A CODE IN THE FILE"};
        std::string rest = text::starts_with(p.code, p.parent) ? p.code.substr(p.parent.size()) : "";
        std::string label = text::starts_with(rest, ".") ? rest.substr(1) : rest;
        std::u32string l32 = text::to32(label);
        bool lettered = l32.size() == 1 && text::lower(l32[0]) == l32[0] && text::upper(l32[0]) != l32[0];
        if (label.empty() || !(rest == "." + label || lettered))
            throw UserError{"ROW " + std::to_string(p.row) + ": A PART OF " + p.parent + " NEEDS A CODE LIKE " + p.parent + "a OR " + p.parent + ".1"};
        Value pair = Value::array();
        pair.push(label);
        pair.push(p.text);
        docs[it->second.first]["standards"].at(it->second.second)["subs"].push(pair);
    }
    Value out = Value::array();
    for (const auto &s : order)
        if (docs[s]["standards"].size()) out.push(docs[s]);
    if (out.size() == 0) throw UserError{"THE FILE HAS NO STANDARDS IN IT"};
    return out;
}

// ----------------------------------------------------------------------------- lessons and assessments

const std::vector<LessonField> LESSON_FIELDS = {
    {"title", "Title", "line"},
    {"unit", "Unit", "line"},
    {"course", "Course", "line"},
    {"date", "Date(s)", "line"},
    {"duration", "Duration", "line"},
    {"standards", "Standards", "standards"},
    {"question", "Essential Question", "text"},
    {"targets", "Learning Targets (I can...)", "text"},
    {"success", "Success Criteria", "text"},
    {"vocab", "Vocabulary", "vocab"},
    {"materials", "Materials & Texts", "text"},
    {"bell_ringer", "Bell Ringer / Warm-Up", "text"},
    {"instruction", "Direct Instruction (I Do)", "text"},
    {"guided", "Guided Practice (We Do)", "text"},
    {"independent", "Independent Practice (You Do)", "text"},
    {"closure", "Closure / Exit Ticket", "text"},
    {"differentiation", "Differentiation & Supports", "text"},
    {"checks", "Assessment / Checks for Understanding", "text"},
    {"assessments", "Assessments & Worksheets", "attached"},
    {"homework", "Homework", "text"},
    {"notes", "Notes & Reflection", "text"},
};
const std::vector<std::string> FIXED_FIELDS = {"title", "unit", "course", "date", "duration", "standards"};
const char *VOCAB_HINT = "One word per line: word: definition";

const std::vector<std::string> ASSESSMENT_KINDS = {"Quiz", "Test", "Assignment", "Worksheet", "Exit Ticket", "Homework", "Unit Exam"};
const std::vector<std::string> MAKEUP_HELD = {"Quiz", "Test", "Unit Exam"};
const char *ANNOTATION = "Annotation Sheet";
const char *BELL_SHEET = "Bell Ringer Sheet";
const std::vector<std::string> SHEET_KINDS = {"Annotation Sheet", "Bell Ringer Sheet"};
const std::vector<std::pair<std::string, double>> ANNOTATION_COLS = {{"Line", 0.36}, {"Symbol", 0.14}, {"Reason for Annotating", 0.50}};
const std::vector<std::string> WEEKDAYS = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};

const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> LAYOUT_CHOICES = {
    {"name_place", {{"below", "Under the title"}, {"top", "At the very top"}}},
    {"title_align", {{"center", "Centered"}, {"left", "Left"}}},
    {"heading_style", {{"line", "Bold, with a line under"}, {"plain", "Bold, no line"}, {"caps", "ALL CAPS, with a line under"}}},
    {"number_style", {{"1.", "1.  2.  3."}, {"1)", "1)  2)  3)"}, {"Q1.", "Q1.  Q2.  Q3."}, {"#1", "#1  #2  #3"}}},
    {"std_text", {{"full", "Code and full text"}, {"codes", "Codes only"}}},
};

static const std::vector<std::pair<std::string, std::string>> *choices_for(const std::string &type) {
    for (const auto &c : LAYOUT_CHOICES) if (c.first == type) return &c.second;
    return nullptr;
}

static std::vector<LayoutOption> page(const std::string &name_fields, const std::string &name_place, std::vector<LayoutOption> more) {
    std::vector<LayoutOption> out = {{"name_fields", "Name line", "fields", name_fields},
                                     {"name_place", "Name line goes", "name_place", name_place},
                                     {"title_align", "Title", "title_align", "center"},
                                     {"heading_style", "Headings", "heading_style", "line"}};
    out.insert(out.end(), more.begin(), more.end());
    out.push_back({"show_footer", "Footer at the bottom of each page", "bool", true});
    out.push_back({"footer_text", "Footer says (blank = your name, course, school)", "line", ""});
    return out;
}

const std::vector<PageKind> &page_layouts() {
    static const std::vector<PageKind> P = [] {
        std::vector<PageKind> p;
        std::vector<LayoutOption> lesson = {{"show_meta", "Course, unit, and date under the title", "bool", true},
                                            {"std_text", "Standards", "std_text", "full"},
                                            {"head_standards", "Heading: Standards", "line", "Standards"}};
        for (const auto &f : LESSON_FIELDS)
            if (f.kind == "text" || f.kind == "vocab" || f.kind == "attached") lesson.push_back({"head_" + f.key, "Heading: " + f.label, "line", f.label});
        p.push_back({"lesson", "Lesson Plans", page("", "top", lesson)});
        p.push_back({"makeup", "Make-Up Work",
                     page("Name, Date Missed, Due", "below",
                          {{"welcome", "Note to the student (blank = the usual \"We missed you!\" note)", "text", ""},
                           {"head_question", "Heading: Essential Question", "line", "Today's Big Question"},
                           {"head_targets", "Heading: Learning Targets", "line", "Today's Goals"},
                           {"head_success", "Heading: Success Criteria", "line", "How You'll Know You've Got It"},
                           {"head_vocab", "Heading: Vocabulary", "line", "Words to Know"},
                           {"head_materials", "Heading: Materials", "line", "What You'll Need"},
                           {"head_steps", "Heading over the steps", "line", "Your Make-Up Steps"},
                           {"head_bell_ringer", "Step: Bell Ringer", "line", "Bell Ringer"},
                           {"head_instruction", "Step: Direct Instruction", "line", "What We Learned"},
                           {"head_guided", "Step: Guided Practice", "line", "Class Practice"},
                           {"head_independent", "Step: Independent Practice", "line", "On Your Own"},
                           {"head_closure", "Step: Exit Ticket", "line", "Exit Ticket"},
                           {"head_homework", "Step: Homework", "line", "Homework"},
                           {"head_see_me", "Step: quizzes and tests to take with you", "line", "See Me"},
                           {"classwork", "Print the lesson's linked worksheets and assignments after the sheet", "bool", true},
                           {"write_lines", "Writing lines under the Bell Ringer and Exit Ticket", "num", 4},
                           {"signoff", "Sign-off line at the end (blank = none)", "fields", "Turned In, Teacher Initials"}})});
        p.push_back({"assessment", "Quizzes & Tests",
                     page("Name, Date, Period", "below",
                          {{"directions_label", "Label before the directions (blank = none)", "line", "Directions:"},
                           {"number_style", "Question numbers", "number_style", "1."}})});
        std::string cols;
        for (const auto &c : ANNOTATION_COLS) cols += (cols.empty() ? "" : ", ") + c.first;
        p.push_back({"annotation", "Annotation Sheets", page("Name, Date", "top", {{"columns", "Chart columns", "fields", cols}})});
        p.push_back({"bell", "Bell Ringer Sheets", page("Name, Period", "top", {})});
        return p;
    }();
    return P;
}

const PageKind *page_kind(const std::string &kind) {
    for (const auto &p : page_layouts()) if (p.kind == kind) return &p;
    return nullptr;
}

// int(v) the way Python does it for a page layout number
static bool py_int(const Value &v, long &out) {
    if (v.is_bool() || v.is_int()) { out = (long)v.as_int(); return true; }
    if (v.is_float()) {
        if (!std::isfinite(v.as_double())) return false;
        out = (long)v.as_double();
        return true;
    }
    if (v.is_string()) {
        std::string t = text::strip(v.str());
        if (t.empty()) return false;
        size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
        if (i >= t.size()) return false;
        for (size_t k = i; k < t.size(); k++) if (!isdigit((unsigned char)t[k]) && t[k] != '_') return false;
        out = std::atol(text::replace(t, "_", "").c_str());
        return true;
    }
    return false;
}

Value page_layout(const Value &settings, const std::string &kind) {
    const PageKind *pk = page_kind(kind);
    Value out = Value::object();
    if (!pk) return out;
    const Value &all = settings.get("page_layouts");
    const Value &saved = all.is_object() ? all.get(kind) : Value();
    for (const auto &opt : pk->options) {
        Value v = saved.is_object() && saved.has(opt.key) ? saved.get(opt.key) : opt.dflt;
        if (const auto *ch = choices_for(opt.type)) {
            bool ok = false;
            if (v.is_string()) for (const auto &c : *ch) if (c.first == v.str()) ok = true;
            if (!ok) v = opt.dflt;
        } else if (opt.type == "num") {
            long n;
            v = py_int(v, n) ? Value(std::max(0L, std::min(30L, n))) : opt.dflt;
        } else if (opt.type == "bool") {
            v = v.truthy();
        } else if (!v.is_string()) {
            v = opt.dflt;
        }
        out[opt.key] = v;
    }
    return out;
}

void set_page_layout(Value &settings, const std::string &kind, const Value &values) {
    const PageKind *pk = page_kind(kind);
    if (!pk) return;
    Value changed = Value::object();
    for (const auto &opt : pk->options)
        if (values.has(opt.key) && values.get(opt.key) != opt.dflt) changed[opt.key] = values.get(opt.key);
    Value layouts = settings.get("page_layouts").is_object() ? settings.get("page_layouts") : Value::object();
    if (changed.size()) layouts[kind] = changed;
    else layouts.erase(kind);
    settings["page_layouts"] = layouts;
}

std::vector<std::string> layout_fields(const std::string &t, size_t most) {
    std::vector<std::string> out;
    for (const auto &x : text::split(t, ",")) {
        std::string s = text::strip(text::rstrip_chars(text::strip(x), ":"));
        if (!s.empty()) out.push_back(s);
        if (out.size() == most) break;
    }
    return out;
}

const std::vector<std::string> BOARD_DESIGN_KEYS = {"board_font",   "board_head_font", "board_title_align", "board_codes_place", "board_layout",
                                                    "board_panels", "board_big_text",  "board_sides",       "board_order"};

std::vector<std::string> advanced_changes(const Value &settings) {
    std::vector<std::string> out;
    const Value &pl = settings.get("page_layouts");
    for (const auto &p : page_layouts())
        if (pl.is_object() && pl.get(p.kind).truthy()) out.push_back(p.name);
    for (const auto &k : BOARD_DESIGN_KEYS) {
        const Value &d = default_settings().get(k);
        if ((settings.has(k) ? settings.get(k) : d) != d) { out.push_back("Board Slide Design"); break; }
    }
    return out;
}

void reset_advanced(Value &settings, const std::string &what) {
    if (what.empty() || what == "board")
        for (const auto &k : BOARD_DESIGN_KEYS) settings[k] = default_settings().get(k);
    if (what.empty()) settings["page_layouts"] = Value::object();
    else if (what != "board") set_page_layout(settings, what, Value::object());
}

const std::vector<QuestionType> QUESTION_TYPES = {
    {"mc", "MULTIPLE CHOICE", "prompt + lettered choices"},
    {"tf", "TRUE / FALSE", "statement + True/False"},
    {"short", "SHORT ANSWER", "prompt + a few writing lines"},
    {"essay", "EXTENDED RESPONSE", "prompt + lined, blank, or boxed space"},
    {"fill", "FILL IN THE BLANK", "use ___ for each blank"},
    {"match", "MATCHING", "terms + shuffled definitions"},
    {"chart", "CHART / ORGANIZER", "chart, Venn diagram, idea web, sequence, Frayer, plot"},
    {"passage", "READING PASSAGE", "a text block with line numbers (not scored)"},
    {"section", "SECTION HEADER", "titled part with directions (not scored)"},
};

std::string type_label(const std::string &t) {
    for (const auto &q : QUESTION_TYPES) if (q.type == t) return q.label;
    return "";
}

std::string type_tag(const std::string &t) {
    static const std::map<std::string, std::string> TAGS = {{"mc", "MC"},    {"tf", "T/F"},   {"short", "SA"},    {"essay", "ER"},     {"fill", "FIB"},
                                                            {"match", "MAT"}, {"chart", "ORG"}, {"passage", "TEXT"}, {"section", "SECT"}};
    auto it = TAGS.find(t);
    return it == TAGS.end() ? "" : it->second;
}

const std::vector<std::string> FRAYER_HEADS = {"Definition", "Characteristics", "Examples", "Non-Examples"};
const std::vector<std::string> PLOT_HEADS = {"Exposition", "Rising Action", "Climax", "Falling Action", "Resolution"};

static Value preset(std::initializer_list<std::pair<const char *, Value>> kv) {
    Value o = Value::object();
    for (const auto &p : kv) o[p.first] = p.second;
    return o;
}

const std::vector<ChartPreset> &chart_presets() {
    static const std::vector<ChartPreset> P = {
        {"chart", "Chart (rows & columns)", preset({{"layout", "table"}, {"cols", 3}, {"rows", 4}, {"heads", Value::array()}, {"sidecol", false}})},
        {"matrix", "Matrix (labeled rows & columns)",
         preset({{"layout", "table"}, {"cols", 3}, {"rows", 3}, {"heads", Value::array()}, {"sidecol", true}})},
        {"tchart", "T-Chart", preset({{"layout", "table"}, {"cols", 2}, {"rows", 1}, {"heads", list_of({"", ""})}, {"sidecol", false}})},
        {"kwl", "K-W-L",
         preset({{"layout", "table"}, {"cols", 3}, {"rows", 1}, {"sidecol", false},
                 {"heads", list_of({"What I Know", "What I Want to Know", "What I Learned"})}})},
        {"cause", "Cause & Effect", preset({{"layout", "table"}, {"cols", 2}, {"rows", 4}, {"heads", list_of({"Cause", "Effect"})}, {"sidecol", false}})},
        {"swbst", "Somebody-Wanted-But-So-Then",
         preset({{"layout", "table"}, {"cols", 5}, {"rows", 1}, {"sidecol", false}, {"heads", list_of({"Somebody", "Wanted", "But", "So", "Then"})}})},
        {"venn2", "Venn diagram (2 circles)", preset({{"layout", "venn"}, {"circles", 2}, {"heads", Value::array()}})},
        {"venn3", "Venn diagram (3 circles)", preset({{"layout", "venn"}, {"circles", 3}, {"heads", Value::array()}})},
        {"web", "Idea web", preset({{"layout", "web"}, {"rows", 6}, {"heads", Value::array()}, {"center", ""}})},
        {"sequence", "Sequence / flow chart", preset({{"layout", "sequence"}, {"rows", 4}, {"heads", list_of({"First", "Next", "Then", "Last"})}})},
        {"frayer", "Frayer model", preset({{"layout", "frayer"}, {"heads", list_of(FRAYER_HEADS)}, {"center", ""}})},
        {"plot", "Plot diagram", preset({{"layout", "plot"}, {"heads", list_of(PLOT_HEADS)}})},
    };
    return P;
}

const std::vector<std::pair<std::string, std::string>> CHART_DIRECTIONS = {
    {"table", "Complete the chart."},          {"venn", "Compare and contrast using the Venn diagram."},
    {"web", "Complete the idea web."},         {"sequence", "Fill in each step in order."},
    {"frayer", "Complete the Frayer model."}, {"plot", "Complete the plot diagram."}};

std::string chart_direction(const std::string &layout) {
    for (const auto &d : CHART_DIRECTIONS) if (d.first == layout) return d.second;
    return "Complete the chart.";
}

const std::vector<int> CHART_SIZES = {0, -1, 6, 8, 10, 12, 15, 18, 20, 24, 30};

Value &chart_preset(Value &q, const std::string &key) {
    for (const auto &p : chart_presets())
        if (p.key == key) {
            for (const auto &m : p.settings.obj()) q[m.first] = m.second;
            q["preset"] = key;
        }
    return q;
}

static std::string fmt_g(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

std::string chart_size_label(int n) {
    if (n == 0) return "Automatic";
    if (n == -1) return "Fill the rest of the page";
    return std::to_string(n) + " lines (about " + fmt_g(std::nearbyint(n / 3.0 * 10) / 10) + " in)";
}

std::string new_id() {
    static const char *HEX = "0123456789abcdef";
    std::string r = os_random(5), out;
    for (unsigned char c : r) { out += HEX[c >> 4]; out += HEX[c & 15]; }
    return out;
}

std::string now() { return now_stamp(); }

bool has_content(const Value &lesson, const std::string &key) {
    const Value &v = lesson.get(key);
    if (v.is_array()) return v.size() > 0;
    if (v.is_string()) return !text::strip(v.str()).empty();
    return v.truthy();
}

std::vector<LessonField> shown_fields(const Value &settings, const Value &lesson) {
    std::vector<std::string> hide;
    for (const auto &h : settings.get("lesson_hide").arr()) hide.push_back(h.str());
    std::vector<LessonField> out;
    for (const auto &f : LESSON_FIELDS)
        if (!contains(hide, f.key) || has_content(lesson, f.key)) out.push_back(f);
    return out;
}

std::vector<std::pair<std::string, std::string>> vocab_pairs(const std::string &t) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &line : text::split(t, "\n")) {
        std::u32string s = text::to32(text::strip(line));
        // a leading "- ", "* ", or "• " bullet
        if (!s.empty() && (s[0] == '-' || s[0] == '*' || s[0] == 0x2022) && s.size() > 1 && text::isspace(s[1])) {
            size_t k = 1;
            while (k < s.size() && text::isspace(s[k])) k++;
            s = s.substr(k);
        }
        if (s.empty()) continue;
        auto dash = [](char32_t c) { return c == '-' || c == 0x2013 || c == 0x2014; };
        size_t n = s.size();
        bool found = false;
        std::u32string word, def;
        for (size_t e = 1; e < n && !found; e++) {
            size_t r = e;
            while (r < n && text::isspace(s[r])) r++;
            if (r < n && s[r] == ':') {
                word = s.substr(0, e);
                def = s.substr(r + 1);
                found = true;
            } else if (r > e && r + 1 < n && dash(s[r]) && text::isspace(s[r + 1])) {
                word = s.substr(0, e);
                def = s.substr(r + 2);
                found = true;
            }
        }
        if (!found) { word = s; def.clear(); }
        out.push_back({text::strip(text::to8(word)), text::strip(text::to8(def))});
    }
    return out;
}

Value new_lesson(const Value &settings) {
    Value d = Value::object();
    for (const auto &f : LESSON_FIELDS)
        if (f.kind == "text" || f.kind == "line" || f.kind == "vocab") d[f.key] = "";
    d["id"] = new_id();
    d["standards"] = Value::array();
    d["assessments"] = Value::array();
    d["course"] = settings.has("course") ? settings.get("course") : Value("");
    d["grades"] = settings.has("grades") ? settings.get("grades") : Value("9-10");
    d["materials"] = settings.has("default_materials") ? settings.get("default_materials") : Value("");
    d["created"] = now();
    d["updated"] = now();
    return d;
}

Value new_assessment(const Value &settings, const std::string &kind) {
    Value a = Value::object();
    a["id"] = new_id();
    a["title"] = "";
    a["kind"] = kind;
    a["unit"] = "";
    a["course"] = settings.has("course") ? settings.get("course") : Value("");
    a["grades"] = settings.has("grades") ? settings.get("grades") : Value("9-10");
    a["standards"] = Value::array();
    a["instructions"] = "";
    a["show_name"] = true;
    a["show_points"] = true;
    a["show_standards"] = false;
    a["questions"] = Value::array();
    a["created"] = now();
    a["updated"] = now();
    if (kind == ANNOTATION) a["rows"] = 10;
    else if (kind == BELL_SHEET) {
        a["weeks"] = list_of({"", ""});
        a["days"] = list_of(std::vector<std::string>(10, ""));
    }
    return a;
}

Value new_question(const std::string &qtype, const std::string &default_std) {
    Value q = Value::object();
    q["type"] = qtype;
    q["prompt"] = "";
    q["points"] = 1;
    q["standard"] = default_std;
    if (qtype == "mc") { q["choices"] = Value::array(); q["answer"] = Value(); }
    else if (qtype == "tf") q["answer"] = Value();
    else if (qtype == "short") { q["lines"] = 3; q["answer"] = ""; }
    else if (qtype == "essay") { q["lines"] = 12; q["space"] = "lines"; q["answer"] = ""; q["points"] = 10; }
    else if (qtype == "fill") q["answer"] = "";
    else if (qtype == "match") { q["pairs"] = Value::array(); q["points"] = 0; }
    else if (qtype == "chart") {
        q["layout"] = "table"; q["cols"] = 3; q["rows"] = 4; q["circles"] = 2; q["heads"] = Value::array(); q["side"] = Value::array();
        q["sidecol"] = false; q["center"] = ""; q["lines"] = 0; q["answer"] = ""; q["points"] = 0; q["preset"] = "chart";
    } else if (qtype == "passage") { q["title"] = ""; q["text"] = ""; q["numbered"] = true; q["points"] = 0; }
    else if (qtype == "section") { q["title"] = ""; q["points"] = 0; }
    return q;
}

// float(s) like Python: spaces around, underscores between digits, inf, nan
static bool py_float_of(const std::string &s0, double &out) {
    std::string s = text::strip(s0);
    if (s.empty()) return false;
    std::string low = text::lower(s);
    std::string body = low[0] == '+' || low[0] == '-' ? low.substr(1) : low;
    if (body == "inf" || body == "infinity" || body == "nan") {
        out = body == "nan" ? NAN : (low[0] == '-' ? -INFINITY : INFINITY);
        return true;
    }
    std::string t;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '_') {
            if (i == 0 || i + 1 >= s.size() || !isdigit((unsigned char)s[i - 1]) || !isdigit((unsigned char)s[i + 1])) return false;
            continue;
        }
        if (!(isdigit((unsigned char)c) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return false;
        t += c;
    }
    char *end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == 0 && end != t.c_str();
}

double points_of(const Value &q) {
    std::string t = q.s("type");
    if (t == "passage" || t == "section") return 0;
    if (t == "match" && !q.get("points").truthy()) return (double)q.get("pairs").size();
    const Value &p = q.get("points");
    if (!p.truthy()) return 0;
    if (p.is_number() || p.is_bool()) return p.as_double();
    double v;
    if (p.is_string() && py_float_of(p.str(), v)) return v;
    return 0;
}

Value parse_points(const std::string &s, const Value &old) {
    double v;
    if (!py_float_of(s, v)) return old;
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 9.2e18) return Value((long long)v);
    return Value(v);
}

std::string fmt_points(double p) {
    if (std::isfinite(p) && p == std::floor(p) && std::fabs(p) < 9.2e18) return std::to_string((long long)p);
    if (std::isnan(p)) return "nan";
    if (std::isinf(p)) return p > 0 ? "inf" : "-inf";
    return json::py_float(p);
}

bool UnitKey::operator<(const UnitKey &o) const {
    if (group != o.group) return group < o.group;
    if (number.size() != o.number.size()) return number.size() < o.number.size();
    if (number != o.number) return number < o.number;
    return low < o.low;
}

UnitKey unit_sort_key(const std::string &u) {
    UnitKey k;
    size_t i = 0;
    while (i < u.size() && !isdigit((unsigned char)u[i])) i++;
    k.low = text::lower(u);
    if (i == u.size()) { k.group = 1; k.number = ""; return k; }
    size_t j = i;
    while (j < u.size() && isdigit((unsigned char)u[j])) j++;
    std::string num = u.substr(i, j - i);
    size_t z = 0;
    while (z + 1 < num.size() && num[z] == '0') z++;
    k.group = 0;
    k.number = num.substr(z);
    return k;
}

std::string class_of(const Value &x) { return text::strip(field(x, "course")); }

std::vector<std::pair<std::string, int>> classes(const Value &settings, const std::vector<const Value *> &items) {
    std::map<std::string, int> count;
    for (const Value *x : items) count[text::lower(class_of(*x))]++;
    std::vector<std::string> names_order;
    std::map<std::string, std::string> names;
    std::vector<std::string> candidates;
    for (const auto &p : settings.get("class_periods").arr()) candidates.push_back(text::strip(field(p, "course")));
    std::vector<std::string> mine;
    for (const Value *x : items) mine.push_back(class_of(*x));
    std::stable_sort(mine.begin(), mine.end(), [](const std::string &a, const std::string &b) { return text::lower(a) < text::lower(b); });
    candidates.insert(candidates.end(), mine.begin(), mine.end());
    for (const auto &name : candidates) {
        std::string low = text::lower(name);
        if (!name.empty() && !names.count(low)) { names[low] = name; names_order.push_back(low); }
    }
    std::vector<std::pair<std::string, int>> out;
    for (const auto &low : names_order) out.push_back({names[low], count.count(low) ? count[low] : 0});
    if (count.count("") && count[""]) out.push_back({"", count[""]});
    return out;
}

bool in_class(const Value &x, const std::string &name) { return text::lower(class_of(x)) == text::lower(text::strip(name)); }

std::vector<const Value *> sort_items(std::vector<const Value *> items, std::string how) {
    auto low_title = [](const Value *x) { return text::lower(field(*x, "title")); };
    if (how == "class") how = "unit";
    if (how == "title") {
        std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) { return low_title(a) < low_title(b); });
        return items;
    }
    if (how == "scheduled") {   // the order they're taught: earliest first day, then title; no date last
        std::map<const Value *, long> first;
        for (const Value *x : items) {
            long best = LONG_MAX;
            for (const auto &d : lesson_dates(*x)) best = std::min(best, d.ordinal());
            first[x] = best;
        }
        std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) { return low_title(a) < low_title(b); });
        std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) { return first[a] < first[b]; });
        return items;
    }
    if (how == "unit") {
        std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) { return low_title(a) < low_title(b); });
        std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) {
            bool na = text::strip(field(*a, "unit")).empty(), nb = text::strip(field(*b, "unit")).empty();
            if (na != nb) return !na;
            return unit_sort_key(field(*a, "unit")) < unit_sort_key(field(*b, "unit"));
        });
        return items;
    }
    std::string key = how == "created" ? "created" : "updated";
    auto k = [&](const Value *x) {
        std::string v = field(*x, key);
        return v.empty() ? field(*x, "updated") : v;
    };
    // sorted(..., reverse=True) keeps equal items in their original order
    std::stable_sort(items.begin(), items.end(), [&](const Value *a, const Value *b) { return k(a) > k(b); });
    return items;
}

// ============================================================================= Store

Store::Store() : Store(std::string()) {}

Store::Store(const std::string &d) {
    if (d.empty()) seed_beta();
    dir = d.empty() ? data_dir() : d;
    path = files::join(dir, "data.json");
    data = Value::object();
    data["version"] = 1;
    data["settings"] = default_settings();
    data["lessons"] = Value::array();
    data["assessments"] = Value::array();
    data["custom_standards"] = Value::array();
    load();
    reload_standards();
}

void Store::load() {
    std::string raw, err;
    if (!files::exists(path)) return;
    std::string why;
    bool ok = files::read(path, raw, &err);
    Value d;
    if (ok) {
        if (!valid_utf8(raw, why)) { ok = false; err = why; }
        else {
            try {
                d = json::parse(raw);
                if (!d.is_object()) { ok = false; err = "the file doesn't hold Chalkboard's data"; }
            } catch (json::ParseError &e) {
                ok = false;
                err = e.message;
            }
        }
    }
    if (!ok) {
        std::string bad = path + ".unreadable-" + strftime_now("%Y%m%d-%H%M%S");
        files::copy(path, bad);
        warning = "?COULD NOT READ DATA FILE (" + err + "). A COPY WAS SAVED TO " + bad;
        return;
    }
    data = normalize_data(d);
}

bool Store::save(std::string *err) {
    if (!files::make_dirs(dir, err)) return false;
    if (!backed_up_ && files::exists(path)) {
        files::copy(path, path + ".bak");
        backed_up_ = true;
    }
    if (!files::write_private(path, json::dump(data, 1), err)) return false;
    if (backed_up_ && files::exists(path + ".bak")) files::make_private(path + ".bak");
    return true;
}

std::string Store::export_dir() const {
    std::string d = beta_files() ? "" : field(settings(), "export_dir");
    return files::expanduser(d.empty() ? default_export_dir() : d);
}

const images::Image *Store::logo() {
    if (!logo_loaded_) {
        logo_loaded_ = true;
        has_logo_ = false;
        std::string raw, p = files::join(dir, LOGO_FILE);
        if (files::exists(p)) {
            std::string err;
            try {
                if (!files::read(p, raw, &err)) throw images::ImageError{err};
                logo_ = images::from_json(json::parse(raw));
                has_logo_ = true;
            } catch (images::ImageError &e) {
                warning = "?COULD NOT READ THE SCHOOL LOGO (" + e.message + ")";
            } catch (json::ParseError &e) {
                warning = "?COULD NOT READ THE SCHOOL LOGO (" + e.message + ")";
            }
        }
    }
    return has_logo_ ? &logo_ : nullptr;
}

const images::Image &Store::set_logo(const std::string &path0) {
    std::string p = clean_path(path0);
    if (!files::is_file(p)) throw images::ImageError{"NO FILE AT " + p};
    images::Image img = images::read_image(p);
    img.name = files::basename(p);
    img.has_name = true;
    return set_logo_image(img);
}

const images::Image &Store::set_logo_image(const images::Image &img) {
    files::make_dirs(dir);
    std::string err;
    if (!files::write_private(files::join(dir, LOGO_FILE), json::dump(images::to_json(img), 1), &err)) throw images::ImageError{err};
    logo_ = img;
    has_logo_ = true;
    logo_loaded_ = true;
    return logo_;
}

void Store::remove_logo() {
    files::remove(files::join(dir, LOGO_FILE));
    has_logo_ = false;
    logo_loaded_ = true;
}

std::string Store::backup_dir() const {
    std::string d = beta_files() ? "" : field(settings(), "backup_dir");
    return files::expanduser(d.empty() ? default_backup_dir() : d);
}

std::string Store::backup(const std::string &folder0, bool remember) {
    std::string folder = folder0.empty() ? backup_dir() : clean_path(folder0), err;
    if (files::exists(folder) && !files::is_dir(folder)) throw UserError{folder + " IS A FILE, NOT A FOLDER"};
    if (!files::make_dirs(folder, &err)) throw UserError{err};
    std::string stamp = strftime_now("%Y-%m-%d-%H%M");
    std::string name = "chalkboard-backup-" + stamp + ".json";
    for (int n = 2; files::exists(files::join(folder, name)); n++) name = "chalkboard-backup-" + stamp + "-" + std::to_string(n) + ".json";
    std::string p = files::join(folder, name);
    if (remember) {
        settings()["backup_dir"] = folder;
        settings()["last_backup"] = now();
    }
    Value stds = Value::array();
    for (const auto &doc : load_kas()) {
        if (!doc.get("file").truthy()) continue;
        Value d = doc;
        d.erase("file");
        stds.push(d);
    }
    const images::Image *lg = logo();
    Value b = Value::object();
    b["kind"] = BACKUP_KIND;
    b["format"] = 1;
    b["app"] = CHALKBOARD_VERSION;
    b["made"] = now();
    b["data"] = data;
    b["standards"] = stds;
    b["logo"] = lg ? images::to_json(*lg) : Value();
    if (!files::write_private(p, json::dump(b, 1), &err)) throw UserError{err};
    if (remember && !save(&err)) throw UserError{err};
    return p;
}

Value Store::import_backup(const std::string &bpath, bool replace) {
    Backup b = read_backup(bpath);
    std::string safety;
    try {
        safety = backup(files::join(dir, "backups"), false);
        std::string folder = files::dirname(safety);
        std::vector<std::string> old;
        for (const auto &n : files::list(folder))
            if (text::starts_with(n, "chalkboard-backup-")) old.push_back(files::join(folder, n));
        std::stable_sort(old.begin(), old.end(), [](const std::string &a, const std::string &c) { return files::mtime(a) < files::mtime(c); });
        if (old.size() > (size_t)SAFETY_BACKUPS)
            for (size_t i = 0; i + SAFETY_BACKUPS < old.size(); i++) files::remove(old[i]);
    } catch (UserError &e) {
        throw UserError{"COULDN'T BACK UP YOUR WORK FIRST, SO NOTHING WAS IMPORTED (" + e.message + ")"};
    }
    Value &nw = b.data;
    Value got = Value::object();
    got["lessons"] = 0;
    got["assessments"] = 0;
    got["updated"] = 0;
    got["standards"] = 0;
    got["subjects"] = 0;
    got["safety"] = safety;
    if (replace) {
        Value keep = Value::object();
        for (const auto &k : LOCAL_SETTINGS)
            if (settings().has(k)) keep[k] = settings().get(k);
        for (const auto &m : settings().obj())
            if (text::starts_with(m.first, "gui_")) keep[m.first] = m.second;
        Value st = nw.get("settings");
        for (const auto &m : keep.obj()) st[m.first] = m.second;
        data["settings"] = st;
        for (const char *k : {"lessons", "assessments", "custom_standards"}) data[k] = nw.get(k);
        for (const auto &m : nw.obj())
            if (m.first != "settings" && m.first != "lessons" && m.first != "assessments" && m.first != "custom_standards") data[m.first] = m.second;
        got["lessons"] = nw.get("lessons").size();
        got["assessments"] = nw.get("assessments").size();
        got["standards"] = nw.get("custom_standards").size();
        for (const auto &doc : load_kas())
            if (doc.get("file").truthy()) files::remove(doc.get("file").str());
        reload_standards(true);
    } else {
        for (const char *k : {"lessons", "assessments"}) {
            json::Array &mine = data[k].arr_ref();
            std::map<std::string, size_t> have;
            for (size_t i = 0; i < mine.size(); i++)
                if (mine[i].get("id").truthy()) have[text_of(mine[i].get("id"))] = i;   // a repeated id: its last place
            for (const auto &x : nw.get(k).arr()) {
                auto it = x.get("id").is_null() ? have.end() : have.find(text_of(x.get("id")));
                if (it == have.end()) {
                    mine.push_back(x);
                    got[k] = got[k].as_int() + 1;
                } else if (field(x, "updated") > field(mine[it->second], "updated")) {
                    mine[it->second] = x;
                    got["updated"] = got["updated"].as_int() + 1;
                }
            }
        }
        std::set<std::string> codes;
        for (const auto &c : data["custom_standards"].arr()) codes.insert(text_of(c.get("code")));
        for (const auto &c : nw.get("custom_standards").arr()) {
            if (c.get("code").truthy() && !codes.count(text_of(c.get("code")))) {
                data["custom_standards"].push(c);
                codes.insert(text_of(c.get("code")));
                got["standards"] = got["standards"].as_int() + 1;
            }
        }
    }
    std::vector<std::string> have = imported_subjects();
    files::make_dirs(standards_dir());
    for (const auto &doc0 : b.standards.arr()) {
        std::string subj = text_of(doc0.get("subject"));
        if (contains(have, subj)) continue;
        Value doc = doc0;
        doc.erase("file");
        files::write_private(files::join(standards_dir(), subject_filename(subj, standards_dir())), json::dump(doc, 1));
        got["subjects"] = got["subjects"].as_int() + 1;
    }
    if (b.has_logo && (replace || !logo())) set_logo_image(b.logo);
    std::string err;
    if (!save(&err)) throw UserError{err};
    reload_standards(true);
    return got;
}

Value *Store::assessment(const std::string &aid) {
    for (auto &a : data["assessments"].arr_ref())
        if (a.is_object() && a.get("id").is_string() && a.get("id").str() == aid) return &a;
    return nullptr;
}

const Value *Store::assessment(const std::string &aid) const {
    for (const auto &a : data.get("assessments").arr())
        if (a.is_object() && a.get("id").is_string() && a.get("id").str() == aid) return &a;
    return nullptr;
}

std::vector<const Value *> Store::attached(const Value &lesson) const {
    std::vector<const Value *> out;
    for (const auto &id : lesson.get("assessments").arr())
        if (const Value *a = assessment(id.str())) out.push_back(a);
    return out;
}

Value *Store::vocab_quiz(Value &lesson) {
    Value pairs = Value::array();
    for (const auto &p : vocab_pairs(field(lesson, "vocab")))
        if (!p.first.empty() && !p.second.empty()) {
            Value pr = Value::array();
            pr.push(p.first);
            pr.push(p.second);
            pairs.push(pr);
        }
    if (pairs.size() < 2) return nullptr;
    std::string title = text::strip(field(lesson, "title"));
    Value q = new_question("match");
    q["prompt"] = "Match each word with its definition.";
    q["pairs"] = pairs;
    Value a = new_assessment(settings(), "Quiz");
    a["title"] = title.empty() ? "Vocabulary Quiz" : title + " Vocabulary Quiz";
    a["unit"] = lesson.has("unit") ? lesson.get("unit") : Value("");
    a["course"] = lesson.has("course") ? lesson.get("course") : Value("");
    if (lesson.get("grades").truthy()) a["grades"] = lesson.get("grades");
    Value qs = Value::array();
    qs.push(q);
    a["questions"] = qs;
    std::string id = a.s("id");
    data["assessments"].push(a);
    lesson.setdefault("assessments", Value::array()).push(id);
    lesson["updated"] = now();
    return assessment(id);
}

std::vector<std::string> Store::units(const std::vector<const Value *> &items) const {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const Value *x : items) {
        std::string u = text::strip(field(*x, "unit"));
        if (!u.empty() && seen.insert(u).second) out.push_back(u);
    }
    std::sort(out.begin(), out.end());   // sorted(set) first: equal keys come out in a fixed order
    std::stable_sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) { return unit_sort_key(a) < unit_sort_key(b); });
    return out;
}

std::string Store::standards_dir() const { return files::join(dir, "standards"); }

const std::vector<Value> &Store::load_kas() {
    if (kas_loaded_) return kas_;
    std::vector<std::string> order;
    std::map<std::string, Value> docs;
    auto put = [&](const Value &doc) {
        std::string subj = text_of(doc.get("subject"));
        if (!docs.count(subj)) order.push_back(subj);
        docs[subj] = doc;
    };
    std::string raw;
    if (resources::get(std::string(STANDARDS_DIR) + "/index.json", raw)) {
        try {
            Value index = json::parse(raw);
            for (const auto &s : index.get("subjects").arr()) {
                std::string body;
                if (!resources::get(std::string(STANDARDS_DIR) + "/" + s.get("file").str(), body)) break;
                Value doc = json::parse(body);
                if (!doc.has("subject")) break;
                put(doc);
            }
        } catch (json::ParseError &) {
        }
    }
    std::string d = standards_dir();
    if (files::is_dir(d))
        for (const auto &name : files::list(d)) {
            if (!text::ends_with(name, ".json")) continue;
            std::string p = files::join(d, name), body;
            try {
                if (!files::read(p, body)) throw json::ParseError{"unreadable"};
                Value doc = json::parse(body);
                if (!doc.is_object() || !doc.has("subject")) throw json::ParseError{"no subject"};
                doc["file"] = p;
                put(doc);
            } catch (json::ParseError &) {
                warning = "?COULD NOT READ STANDARDS FILE " + p;
            }
        }
    kas_.clear();
    for (const auto &s : order) kas_.push_back(docs[s]);
    kas_loaded_ = true;
    return kas_;
}

std::vector<std::string> Store::imported_subjects() {
    std::vector<std::string> out;
    for (const auto &doc : load_kas())
        if (doc.get("file").truthy()) out.push_back(text_of(doc.get("subject")));
    return out;
}

Value Store::import_standards(const std::string &p) {
    Value docs = read_standards_file(p);
    std::map<std::string, std::string> taken;
    for (const auto &s : standards) taken[s.s("code")] = s.s("subject");
    files::make_dirs(standards_dir());
    Value done = Value::array();
    for (auto &doc : docs.arr_ref()) {
        std::string subj = doc.s("subject");
        Value keep = Value::array();
        int skipped = 0;
        for (const auto &s : doc.get("standards").arr()) {
            auto it = taken.find(s.s("code"));
            if (it != taken.end() && it->second != subj) skipped++;
            else keep.push(s);
        }
        if (!keep.size()) throw UserError{"EVERY CODE IN " + subj + " IS ALREADY USED BY ANOTHER SUBJECT"};
        doc["standards"] = keep;
        std::string old;
        for (const auto &k : load_kas())
            if (text_of(k.get("subject")) == subj && k.get("file").truthy()) { old = k.get("file").str(); break; }
        std::string out = old.empty() ? files::join(standards_dir(), subject_filename(subj, standards_dir())) : old, err;
        if (!files::write(out + ".tmp", json::dump(doc, 1), &err) || !files::rename(out + ".tmp", out, &err)) throw UserError{err};
        Value row = Value::array();
        row.push(subj);
        row.push(keep.size());
        row.push(skipped);
        done.push(row);
    }
    reload_standards(true);
    return done;
}

std::vector<std::string> Store::shareable_subjects() {
    std::vector<std::string> out = imported_subjects();
    if (data.get("custom_standards").size()) out.push_back(MY_STANDARDS);
    return out;
}

std::string Store::export_standards(const std::string &folder0, const std::vector<std::string> &subjects_) {
    Value docs = Value::array();
    for (const auto &subj : subjects_) {
        if (subj == MY_STANDARDS) {
            Value d = Value::object();
            d["subject"] = MY_STANDARDS;
            d["source"] = "Written by a teacher, shared " + today_iso() + ".";
            Value list = Value::array();
            for (const auto &c : data.get("custom_standards").arr()) {
                Value s = Value::object();
                s["code"] = c.get("code");
                const Value &g = c.get("grades");
                s["grades"] = g.is_string() && g.str() == "Custom" ? Value("") : (c.has("grades") ? g : Value(""));
                s["strand"] = "";
                s["cluster"] = "";
                s["text"] = c.get("text");
                s["subs"] = Value::array();
                list.push(s);
            }
            d["standards"] = list;
            docs.push(d);
            continue;
        }
        const Value *found = nullptr;
        for (const auto &d : load_kas())
            if (text_of(d.get("subject")) == subj && d.get("file").truthy()) { found = &d; break; }
        if (!found) throw UserError{subj + " ISN'T A SUBJECT YOU IMPORTED"};
        Value d = *found;
        d.erase("file");
        docs.push(d);
    }
    if (!docs.size()) throw UserError{"NOTHING TO EXPORT"};
    std::string folder = clean_path(folder0), err;
    if (files::exists(folder) && !files::is_dir(folder)) throw UserError{folder + " IS A FILE, NOT A FOLDER"};
    if (!files::make_dirs(folder, &err)) throw UserError{err};
    std::string name = docs.size() == 1 ? subject_filename(docs.at(0).s("subject") + " standards", folder)
                                         : subject_filename("standards " + today_iso(), folder);
    std::string p = files::join(folder, name);
    if (!files::write(p + ".tmp", json::dump(docs.size() == 1 ? docs.at(0) : docs, 1), &err) || !files::rename(p + ".tmp", p, &err))
        throw UserError{err};
    return p;
}

void Store::remove_subject(const std::string &subject) {
    for (const auto &doc : load_kas())
        if (text_of(doc.get("subject")) == subject && doc.get("file").truthy()) files::remove(doc.get("file").str());
    reload_standards(true);
}

void Store::reload_standards(bool rescan) {
    if (rescan) kas_loaded_ = false;
    standards.clear();
    subjects.clear();
    std::vector<Value> &kas = const_cast<std::vector<Value> &>(load_kas());
    for (auto &doc : kas) {
        std::string subj = text_of(doc.get("subject"));
        std::string src = doc.get("source").truthy() ? text_of(doc.get("source")) : "Imported.";
        subjects.push_back(subj);
        for (auto &s : doc["standards"].arr_ref()) {
            s.setdefault("subs", Value::array());
            Value e = Value::object();
            e["code"] = s.get("code");
            e["grades"] = s.has("grades") ? s.get("grades") : Value("");
            e["strand"] = s.has("strand") ? s.get("strand") : Value("");
            e["cluster"] = s.has("cluster") ? s.get("cluster") : Value("");
            e["text"] = s.get("text");
            e["parent"] = Value();
            e["subs"] = s.get("subs");
            e["sub_grades"] = s.has("sub_grades") ? s.get("sub_grades") : Value(false);
            e["subject"] = subj;
            e["source"] = src;
            standards.push_back(e);
            std::string code = text_of(s.get("code"));
            for (const auto &sub : s.get("subs").arr()) {
                std::string label = text_of(sub.at(0)), txt = text_of(sub.at(1));
                std::u32string l32 = text::to32(label);
                bool lettered = l32.size() == 1 && text::lower(l32[0]) == l32[0] && text::upper(l32[0]) != l32[0];
                Value c = Value::object();
                c["code"] = lettered ? code + label : code + "." + label;
                c["label"] = label;
                c["grades"] = s.get("sub_grades").truthy() ? Value(label) : (s.has("grades") ? s.get("grades") : Value(""));
                c["strand"] = s.has("strand") ? s.get("strand") : Value("");
                c["cluster"] = s.has("cluster") ? s.get("cluster") : Value("");
                c["text"] = sub.at(1);
                c["parent"] = code;
                c["subs"] = Value::array();
                c["subject"] = subj;
                c["source"] = src;
                (void)txt;
                standards.push_back(c);
            }
        }
    }
    for (const auto &c : data.get("custom_standards").arr()) {
        Value e = Value::object();
        e["code"] = c.get("code");
        e["grades"] = c.get("grades").truthy() ? c.get("grades") : Value("Custom");
        e["strand"] = "Custom";
        e["cluster"] = "Custom";
        e["text"] = c.get("text");
        e["parent"] = Value();
        e["subs"] = Value::array();
        e["custom"] = true;
        e["subject"] = "Custom";
        e["source"] = "Added by you.";
        standards.push_back(e);
    }
    std_index.clear();
    kas_count = 0;
    for (size_t i = 0; i < standards.size(); i++) {
        std_index[text_of(standards[i].get("code"))] = i;
        if (!standards[i].get("parent").truthy() && !standards[i].get("custom").truthy()) kas_count++;
    }
}

const Value *Store::standard(const std::string &code) const {
    auto it = std_index.find(code);
    return it == std_index.end() ? nullptr : &standards[it->second];
}

std::string Store::std_text(const std::string &code) const {
    const Value *s = standard(code);
    if (!s) return "";
    std::string t = text_of(s->get("text"));
    if (s->get("parent").truthy()) return t;
    if (s->get("subs").size() && text::ends_with(text::rstrip(t), ":")) {
        std::vector<std::string> parts;
        for (const auto &kv : s->get("subs").arr()) parts.push_back("(" + text_of(kv.at(0)) + ") " + text_of(kv.at(1)));
        return t + " " + text::join(parts, " ");
    }
    return t;
}

std::string Store::good_thing(const std::string &current) {
    if (good_things_.empty()) {
        std::string raw;
        if (resources::get(GOOD_THINGS_FILE, raw)) {
            try {
                Value parsed = json::parse(raw);   // keep it alive while the loop reads it
                for (const auto &p : parsed.get("prompts").arr()) good_things_.push_back(GOOD_THINGS_PREFIX + p.str());
            } catch (json::ParseError &) {
            }
        }
        if (good_things_.empty()) return "";
    }
    std::set<std::string> used;
    for (const auto &l : data.get("lessons").arr()) used.insert(text::strip(field(l, "bell_ringer")));
    used.insert(text::strip(current));
    std::vector<std::string> fresh;
    for (const auto &g : good_things_) if (!used.count(g)) fresh.push_back(g);
    if (fresh.empty())
        for (const auto &g : good_things_) if (g != text::strip(current)) fresh.push_back(g);
    if (fresh.empty()) return good_things_[0];
    std::string r = os_random(4);
    uint32_t v = ((uint32_t)(unsigned char)r[0] << 24) | ((uint32_t)(unsigned char)r[1] << 16) | ((uint32_t)(unsigned char)r[2] << 8) | (unsigned char)r[3];
    return fresh[v % fresh.size()];
}

}  // namespace store
