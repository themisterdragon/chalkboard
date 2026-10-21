// Fuzz targets for everything that reads data from outside: built with clang's libFuzzer and the
// address/undefined-behavior sanitizers by core/fuzz/run.sh. TARGET picks which one.
#include <cstddef>
#include <cstdint>
#include <string>

#include "schoolcal.h"
#include "dates.h"
#include "doc.h"
#include "export_txt.h"
#include "images.h"
#include "jpeg.h"
#include "json.h"
#include "markup.h"
#include "organizers.h"
#include "pdf.h"
#include "raster.h"
#include "export_docx.h"
#include "store.h"
#include "textwrap.h"
#include "ttf.h"
#include "zipfile.h"
#include "zlib.h"

static std::string str(const uint8_t *d, size_t n) { return std::string((const char *)d, n); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string s = str(data, size);
#if TARGET == 1
    try {
        json::Value v = json::parse(s, true);
        std::string out = json::dump(v, 1);
        json::Value back = json::parse(out);   // what we write, we can read
        if (!(back == v) && out.find("NaN") == std::string::npos) __builtin_trap();
    } catch (json::ParseError &) {
    }
#elif TARGET == 2
    try {
        std::string out = zlib::decompress(s, 1 << 24);
        if (zlib::decompress(zlib::compress(out)) != out) __builtin_trap();
    } catch (zlib::Error &) {
    }
    try {
        zlib::inflate_raw(s, 1 << 24);
    } catch (zlib::Error &) {
    }
    if (zlib::inflate_raw(zlib::deflate_raw(s)) != s) __builtin_trap();
    try {
        zlib::b64decode(s);
    } catch (zlib::Error &) {
    }
#elif TARGET == 3
    try {
        images::Image img = images::read_png(std::string("\x89PNG\r\n\x1a\n", 8) + s);
        json::Value j = images::to_json(img);
        images::from_json(j);
    } catch (images::ImageError &) {
    }
    try {
        images::read_jpeg("\xff\xd8" + s);
    } catch (images::ImageError &) {
    }
#elif TARGET == 4
    try {
        auto offs = ttf::faces_in(s);
        auto data = std::make_shared<const std::string>(s);
        for (auto off : offs) {
            ttf::Face f(data, off);
            f.file();
        }
        ttf::names(s, 0);
    } catch (ttf::FontError &) {
    }
#elif TARGET == 5
    // standards files a teacher imports
    try {
        store::csv_subjects(s, "stem", "source");
    } catch (store::UserError &) {
    }
    try {
        json::Value v = json::parse(s);
        try {
            store::json_subject(v, "stem", "source");
        } catch (store::UserError &) {
        }
    } catch (json::ParseError &) {
    }
#elif TARGET == 6
    // text: formatting marks, wrapping, dates, vocabulary
    markup::runs(s);
    markup::spans(s);
    auto fl = markup::flags_of(s);
    markup::marked(fl.first, fl.second);
    markup::tidy(fl.first, fl.second);
    markup::toggle(s, (long)(size % 7), "biu"[size % 3]);
    markup::links(s);
    textwrap::wrap(s, 3 + (long)(size % 70), "  ", "    ");
    dates::parse_dates(s, Date{2026, 10, 14});
    store::vocab_pairs(s);
    pdf::enc(s);
#elif TARGET == 7
    // a whole data file: whatever is in it, every document and export must come out without a crash
    try {
        json::Value v = json::parse(s);
        static store::Store *st = nullptr;
        if (!st) {
            setenv("CHALKBOARD_DATA", "/nonexistent-chalkboard-fuzz", 1);
            st = new store::Store();
        }
        st->data = store::normalize_data(v);
        st->reload_standards();
        for (const auto &l : st->data.get("lessons").arr()) {
            if (!l.is_object()) continue;
            json::Value d = doc::lesson_doc(l, *st);
            export_txt::render_lines(d);
            export_docx::parts(d, "Times", "Letter");
            pdf::Fonts f("Times");
            pdf::build_rows(d.get("blocks"), f, 54, 504, 666);
            doc::makeup_doc(l, *st);
            doc::board_doc(l, *st);
        }
        for (const auto &a : st->data.get("assessments").arr()) {
            if (!a.is_object()) continue;
            for (int v2 = 0; v2 < 2; v2++) {
                json::Value d = doc::assessment_doc(a, *st, v2, 2, v2 == 1);
                export_txt::render_lines(d);
                export_docx::parts(d, "Times", "Letter");
                pdf::Fonts f("Times");
                auto rows = pdf::build_rows(d.get("blocks"), f, 54, 504, 666);
                pdf::Canvas c;
                for (auto &r : rows)
                    if (r.draw) r.draw(c, 700, r.h);
            }
        }
        std::vector<const json::Value *> ls;
        for (const auto &l : st->data.get("lessons").arr()) ls.push_back(&l);
        for (const auto &c : store::classes(st->settings(), ls)) doc::curriculum_doc(*st, c.first, store::DEFAULT_SUBJECT, "9-10");
        for (const auto &k : store::page_layouts()) store::page_layout(st->settings(), k.kind);
        store::board_sections(st->settings());
    } catch (json::ParseError &) {
    }
#elif TARGET == 8
    std::vector<std::pair<std::string, std::string>> out;
    try {
        zipfile::read(s, out);
    } catch (zlib::Error &) {
    }
#elif TARGET == 9
    // a school logo JPEG, decoded into pixels
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    if (jpeg::decode(s, w, h, px)) {
        volatile uint8_t sink = px.empty() ? 0 : px.back();
        (void)sink;
    }
#elif TARGET == 10
    // a font's glyph outlines, drawn
    auto o = raster::Outlines::from_data(std::make_shared<const std::string>(s), 0);
    if (o) {
        raster::Image img(64, 64);
        double sc = 48 / o->units_per_em();
        double m[6] = {sc, 0, 0.2 * sc, -sc, 4, 52};
        for (uint32_t c : {0x41u, 0x67u, 0x20ACu, 0x1F600u, 0u})
            img.fill(o->glyph(o->glyph_index(c)).path, {0, 0, 0}, 1, m);
        for (uint32_t g = 0; g < 40; g++) img.fill(o->glyph(g).path, {0, 0, 0}, 1, m);
    }
#elif TARGET == 11
    // a district calendar file (.ics)
    auto events = schoolcal::read_ics(s);
    Date f, l;
    schoolcal::ics_first_last(events, f, l);
    schoolcal::Calendar cal;
    for (const auto &e : events) cal.off.push_back(e.day);
    auto next = schoolcal::next_year(cal);
    volatile size_t sink = next.off.size();
    (void)sink;
#elif TARGET == 12
    // a Date(s) field as typed, moved by school days (past a break)
    schoolcal::Calendar cal;
    cal.off.push_back({Date{2026, 10, 5}, Date{2026, 10, 9}, "Fall Break", "break"});
    for (int n : {3, -2}) {
        std::string out = schoolcal::move_dates(s, Date{2026, 10, 14}, [&](const Date &d) { return cal.add_school_days(d, n); }, {});
        volatile size_t sink = out.size();
        (void)sink;
    }
#endif
    return 0;
}
