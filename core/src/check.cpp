#include "check.h"

#include <cstdio>
#include <iostream>

#include "dates.h"
#include "json.h"
#include "files.h"
#include "markup.h"
#include "doc.h"
#include "export_txt.h"
#include "textwrap.h"
#include "pdf.h"
#include "export_docx.h"
#include "board.h"
#include "export_pptx.h"
#include "fontlib.h"
#include "exporting.h"
#include "zlib.h"
#include "store.h"
#include "text.h"

namespace check {

using json::Value;

static Value strings(const std::vector<std::string> &xs) {
    Value a = Value::array();
    for (const auto &x : xs) a.push(x);
    return a;
}

static Value date_list(const std::vector<Date> &ds) {
    Value a = Value::array();
    for (const auto &d : ds) a.push(d.iso());
    return a;
}

static Value ids(const std::vector<const Value *> &xs) {
    Value a = Value::array();
    for (const Value *x : xs) a.push(x->get("id"));
    return a;
}

static Value dump_store() {
    store::Store st;
    Value out = Value::object();
    out["warning"] = st.warning;
    out["data"] = st.data;
    out["subjects"] = strings(st.subjects);
    out["kas_count"] = st.kas_count;
    Value stds = Value::array();
    for (const auto &s : st.standards) stds.push(s);
    out["standards"] = stds;
    Value texts = Value::object();
    for (const auto &s : st.standards) texts[s.s("code")] = st.std_text(s.s("code"));
    out["std_text"] = texts;
    std::vector<const Value *> lessons, assessments;
    for (const auto &l : st.data.get("lessons").arr()) lessons.push_back(&l);
    for (const auto &a : st.data.get("assessments").arr()) assessments.push_back(&a);
    Value sorts = Value::object();
    for (const char *how : {"updated", "created", "title", "unit", "class"}) {
        sorts[std::string("lessons_") + how] = ids(store::sort_items(lessons, how));
        sorts[std::string("assess_") + how] = ids(store::sort_items(assessments, how));
    }
    out["sorts"] = sorts;
    Value cls = Value::array();
    for (const auto &c : store::classes(st.settings(), lessons)) {
        Value p = Value::array();
        p.push(c.first);
        p.push(c.second);
        cls.push(p);
    }
    out["classes"] = cls;
    out["units"] = strings(st.units(lessons));
    Value layouts = Value::object();
    for (const auto &k : store::page_layouts()) layouts[k.kind] = store::page_layout(st.settings(), k.kind);
    out["page_layouts"] = layouts;
    out["advanced_changes"] = strings(store::advanced_changes(st.settings()));
    Value per = Value::object();
    for (const Value *l : lessons) {
        Value x = Value::object();
        x["dates"] = date_list(store::lesson_dates(*l));
        Value vp = Value::array();
        for (const auto &p : store::vocab_pairs(store::field(*l, "vocab"))) {
            Value pr = Value::array();
            pr.push(p.first);
            pr.push(p.second);
            vp.push(pr);
        }
        x["vocab"] = vp;
        Value sf = Value::array();
        for (const auto &f : store::shown_fields(st.settings(), *l)) sf.push(f.key);
        x["shown"] = sf;
        x["attached"] = ids(st.attached(*l));
        Value periods = Value::array();
        for (const auto &p : store::periods_for(st.settings(), *l)) periods.push(p);
        x["periods"] = periods;
        per[l->s("id")] = x;
    }
    out["lessons"] = per;
    Value pts = Value::object();
    for (const Value *a : assessments) {
        Value qs = Value::array();
        for (const auto &q : a->get("questions").arr()) qs.push(store::fmt_points(store::points_of(q)));
        pts[a->s("id")] = qs;
    }
    out["points"] = pts;
    Value bs = Value::array();
    for (const auto &b : store::board_sections(st.settings())) {
        Value t = Value::array();
        t.push(b.key);
        t.push(b.label);
        t.push(b.col);
        bs.push(t);
    }
    out["board_sections"] = bs;
    Value misc = Value::object();
    Value sizes = Value::array();
    for (int n : store::CHART_SIZES) sizes.push(store::chart_size_label(n));
    misc["chart_sizes"] = sizes;
    Value nl = store::new_lesson(st.settings());
    Value keys = Value::array();
    for (const auto &m : nl.obj()) keys.push(m.first);
    misc["new_lesson_keys"] = keys;
    Value qk = Value::object();
    for (const auto &t : store::QUESTION_TYPES) qk[t.type] = store::new_question(t.type, "X.1");
    misc["new_questions"] = qk;
    Value na = Value::object();
    for (const char *k : {"Quiz", "Annotation Sheet", "Bell Ringer Sheet"}) {
        Value a = store::new_assessment(st.settings(), k);
        a.erase("id");
        a.erase("created");
        a.erase("updated");
        na[k] = a;
    }
    misc["new_assessments"] = na;
    Value presets = Value::object();
    for (const auto &p : store::chart_presets()) {
        Value q = store::new_question("chart");
        presets[p.key] = store::chart_preset(q, p.key);
    }
    misc["presets"] = presets;
    Value hexes = Value::array();
    for (const char *h : {"#7a0019", "7A0019", "#a01", "zz", "", " #abc ", "#12345"}) hexes.push(store::parse_hex(h));
    misc["hex"] = hexes;
    Value gm = Value::array();
    for (const char *g : {"K", "9-10", "K-12", "11-12", "Custom", "3"}) {
        Value row = Value::array();
        for (const char *w : {"9-10", "K", "ALL", "5", "11-12"}) row.push(store::grades_match(g, w));
        gm.push(row);
    }
    misc["grades_match"] = gm;
    Value pp = Value::array();
    for (const char *p : {"2", "2.5", " 3 ", "x", "1e2", "1_0", "-1", "inf"}) pp.push(store::parse_points(p, Value("old")));
    misc["parse_points"] = pp;
    out["misc"] = misc;
    return out;
}

// markup on every string in a JSON file: runs, spans, toggles, bits round trip, tidy, links
static Value dump_markup(const std::string &path) {
    std::string raw;
    files::read(path, raw);
    Value in = json::parse(raw), out = Value::array();
    for (const auto &item : in.arr()) {
        std::string t = item.str();
        Value r = Value::object();
        Value rs = Value::array();
        for (const auto &x : markup::runs(t)) {
            Value e = Value::array();
            e.push(x.text); e.push(x.bold); e.push(x.italic); e.push(x.under);
            rs.push(e);
        }
        r["runs"] = rs;
        Value sp = Value::array();
        for (const auto &x : markup::spans(t)) {
            Value e = Value::array();
            e.push(x.start); e.push(x.end);
            if (x.mark) e.push("mark"); else e.push(x.bits);
            sp.push(e);
        }
        r["spans"] = sp;
        Value tg = Value::array();
        long n = (long)text::length(t);
        for (long pos : {0L, n / 3, n / 2, n})
            for (char k : {'b', 'i', 'u'}) {
                auto got = markup::toggle(t, pos, k);
                Value e = Value::array();
                e.push(got.first); e.push(got.second);
                tg.push(e);
            }
        r["toggle"] = tg;
        auto fl = markup::flags_of(t);
        r["plain"] = fl.first;
        Value bits = Value::array();
        for (int b : fl.second) bits.push(b);
        r["bits"] = bits;
        r["marked"] = markup::marked(fl.first, fl.second);
        // tidy a scrambled set of bits
        std::vector<int> scr = fl.second;
        for (size_t k = 0; k < scr.size(); k++) scr[k] = (int)((k * 7 + scr.size()) % 8);
        auto td = markup::tidy(fl.first, scr);
        Value tb = Value::array();
        for (int b : td.second) tb.push(b);
        Value te = Value::array();
        te.push(td.first); te.push(tb);
        r["tidy"] = te;
        Value lk = Value::array();
        for (const auto &x : markup::links(t)) {
            Value e = Value::array();
            e.push(x.first);
            if (x.second.empty()) e.push(Value()); else e.push(x.second);
            lk.push(e);
        }
        r["links"] = lk;
        out.push(r);
    }
    return out;
}

// every document the store can make
static Value dump_docs() {
    store::Store st;
    Value out = Value::object();
    std::vector<const Value *> lessons, assessments;
    for (const auto &l : st.data.get("lessons").arr()) lessons.push_back(&l);
    for (const auto &a : st.data.get("assessments").arr()) assessments.push_back(&a);
    for (const Value *l : lessons) {
        std::string id = l->s("id");
        out["lesson:" + id] = doc::lesson_doc(*l, st);
        out["makeup:" + id] = doc::makeup_doc(*l, st);
        out["board:" + id] = doc::board_doc(*l, st);
        auto periods = store::periods_for(st.settings(), *l);
        for (size_t i = 0; i < periods.size(); i++) out["board:" + id + ":" + std::to_string(i)] = doc::board_doc(*l, st, &periods[i]);
    }
    for (const Value *a : assessments) {
        std::string id = a->s("id");
        for (int v = 0; v < 3; v++)
            for (int key = 0; key < 2; key++)
                out["assess:" + id + ":" + std::to_string(v) + ":" + std::to_string(key)] = doc::assessment_doc(*a, st, v, v ? 3 : 1, key != 0);
    }
    for (const auto &k : store::page_layouts()) out["sample:" + k.kind] = doc::layout_sample(st, k.kind, st.settings());
    out["preview"] = doc::preview_doc(st);
    for (const auto &c : store::classes(st.settings(), lessons)) {
        std::vector<const Value *> mine;
        for (const Value *l : lessons) if (store::in_class(*l, c.first)) mine.push_back(l);
        Value ys = Value::array();
        for (int y : doc::school_years(mine)) ys.push(y);
        out["years:" + c.first] = ys;
        out["map:" + c.first] = doc::curriculum_doc(st, c.first);
        out["map:" + c.first + ":rw"] = doc::curriculum_doc(st, c.first, store::DEFAULT_SUBJECT, "9-10");
        for (int y : doc::school_years(mine)) out["map:" + c.first + ":" + std::to_string(y)] = doc::curriculum_doc(st, c.first, "", "", y);
        Value rows = Value::array();
        for (const auto &r : doc::curriculum_rows(st, c.first)) {
            Value row = Value::array();
            for (const auto &x : r) row.push(x);
            rows.push(row);
        }
        out["rows:" + c.first] = rows;
    }
    return out;
}

// textwrap on [text, width, indent, subsequent] items
static Value dump_wrap(const std::string &path) {
    std::string raw;
    files::read(path, raw);
    Value in = json::parse(raw), out = Value::array();
    for (const auto &item : in.arr()) {
        Value lines = Value::array();
        for (const auto &l : textwrap::wrap(item.at(0).str(), (long)item.at(1).as_int(), std::string((size_t)item.at(2).as_int(), ' '),
                                             std::string((size_t)item.at(3).as_int(), ' ')))
            lines.push(l);
        out.push(lines);
    }
    return out;
}

static Value dump_txt() {
    Value docs = dump_docs(), out = Value::object();
    for (const auto &m : docs.obj()) {
        if (!m.second.is_object() || !m.second.has("blocks")) continue;
        Value lines = Value::array();
        for (const auto &l : export_txt::render_lines(m.second)) lines.push(l);
        out[m.first] = lines;
    }
    return out;
}

// every document as a PDF in a folder (name = the document's key with / and : made safe)
static Value dump_pdf(const std::string &folder) {
    Value docs = dump_docs(), out = Value::object();
    store::Store st;
    files::make_dirs(folder);
    for (const auto &m : docs.obj()) {
        if (!m.second.is_object() || !m.second.has("blocks")) continue;
        std::string name = text::replace(text::replace(m.first, "/", "_"), ":", "_");
        std::string err;
        int n = pdf::render_pdf(m.second, files::join(folder, name + ".pdf"), st.settings().s("font"), st.settings().s("page"), &err);
        out[m.first] = n;
    }
    return out;
}

static Value dump_docx(const std::string &folder) {
    Value docs = dump_docs(), out = Value::object();
    store::Store st;
    files::make_dirs(folder);
    for (const auto &m : docs.obj()) {
        if (!m.second.is_object() || !m.second.has("blocks")) continue;
        std::string name = text::replace(text::replace(m.first, "/", "_"), ":", "_");
        out[m.first] = export_docx::render_docx(m.second, files::join(folder, name + ".docx"), st.settings().s("font"), st.settings().s("page"));
    }
    return out;
}

// the board designs check_core.py tries (the same list is in the Python side)
static std::vector<Value> board_variants(const Value &base) {
    std::vector<Value> out;
    auto with = [&](std::initializer_list<std::pair<const char *, Value>> kv) {
        Value s = base;
        for (const auto &p : kv) s[p.first] = p.second;
        out.push_back(s);
    };
    Value order = Value::array();
    for (const char *k : {"homework", "materials", "targets", "standards"}) order.push(k);
    Value sides = Value::object();
    sides["materials"] = "left";
    sides["targets"] = "right";
    with({});
    with({{"board_layout", "one"}, {"board_panels", "outline"}, {"board_title_align", "center"}, {"board_codes_place", "left"}, {"logo_place", "right"}});
    with({{"board_layout", "sides"}, {"board_panels", "chalk"}, {"board_big_text", true}, {"board_style", "white"}});
    with({{"board_style", "school"}, {"primary_color", "#7a0019"}, {"secondary_color", "#ffcc00"}, {"text_color", ""}});
    with({{"board_style", "school"}, {"primary_color", ""}, {"secondary_color", "#ffcc00"}, {"text_color", "#ffffff"}, {"board_font", "Liberation Serif"},
          {"board_head_font", "Nimbus Sans"}});
    with({{"board_std_text", false}, {"board_order", order}, {"board_sides", sides}, {"board_font", "More Perfect DOS VGA"}, {"board_title_align", "center"}});
    return out;
}

static Value dump_board(const std::string &limit) {
    store::Store st;
    Value out = Value::object();
    int most = limit.empty() ? 1000000 : std::atoi(limit.c_str()), n = 0;
    std::vector<Value> variants = board_variants(st.settings());
    for (const auto &l : st.data.get("lessons").arr()) {
        if (n++ >= most) break;
        auto periods = store::periods_for(st.settings(), l);
        std::vector<const Value *> ps = {nullptr};
        for (auto &p : periods) ps.push_back(&p);
        for (size_t v = 0; v < variants.size(); v++)
            for (size_t pi = 0; pi < ps.size() && pi < 2; pi++) {
                Value d = doc::board_doc(l, st, ps[pi], &variants[v]);
                Value slides = Value::array();
                board::Slides pages = board::board_slides(d, st.logo());
                for (auto &c : pages.canvases) {
                    Value ops = Value::array();
                    for (const auto &o : c->ops) ops.push(o);
                    Value page = Value::object();
                    page["ops"] = ops;
                    Value ims = Value::array();
                    for (const auto &im : c->images) ims.push(im.first);
                    page["images"] = ims;
                    slides.push(page);
                }
                board::Slides deck = board::board_slides(d, st.logo(), [] { return new export_pptx::SlideCanvas(); });
                Value xml = Value::array();
                for (auto &c : deck.canvases) {
                    auto *sc = static_cast<export_pptx::SlideCanvas *>(c.get());
                    Value one = Value::object();
                    one["xml"] = sc->xml();
                    Value links = Value::array();
                    for (const auto &u : sc->slide_links) links.push(u);
                    one["links"] = links;
                    Value media = Value::array();
                    for (const auto &m : sc->media) media.push(m.second);
                    one["media"] = media;
                    xml.push(one);
                }
                Value entry = Value::object();
                entry["pdf"] = slides;
                entry["pptx"] = xml;
                Value names = Value::array();
                for (const auto &nm : pages.fonts->names) names.push(nm);
                entry["fonts"] = names;
                out[l.s("id") + ":" + std::to_string(v) + ":" + std::to_string(pi)] = entry;
            }
    }
    Value fams = Value::array();
    for (const auto &f : fontlib::families()) {
        Value p = Value::array();
        p.push(f.first);
        p.push(f.second);
        fams.push(p);
    }
    out["_families"] = fams;
    return out;
}

// the first lessons' boards as real files: PNG slides and the .pptx
static Value dump_png(const std::string &folder) {
    store::Store st;
    Value out = Value::object();
    files::make_dirs(folder);
    int n = 0;
    for (const auto &l : st.data.get("lessons").arr()) {
        if (n++ >= 3) break;
        auto variants = board_variants(st.settings());
        for (size_t v = 0; v < variants.size(); v++) {
            Value d = doc::board_doc(l, st, nullptr, &variants[v]);
            std::string name = files::join(folder, l.s("id") + "-" + std::to_string(v) + ".png");
            try {
                Value got = Value::array();
                for (const auto &p : board::render_png(d, st.logo(), name)) got.push(files::basename(p));
                out[files::basename(name)] = got;
            } catch (board::BoardError &e) {
                out[files::basename(name)] = e.message;
            }
        }
    }
    return out;
}

// real exports into a folder: every format for the first lessons (with their quizzes), every assessment
// as PDF, curriculum maps, and a day slideshow
static Value dump_export(const std::string &root) {
    store::Store st;
    Value out = Value::object();
    int n = 0;
    for (const auto &l : st.data.get("lessons").arr()) {
        if (n++ >= 4) break;
        Value got = Value::array();
        for (const auto &p : exporting::export_item(st, "lesson", l, "ALL", nullptr, root)) got.push(p);
        out["lesson:" + l.s("id")] = got;
    }
    for (const auto &a : st.data.get("assessments").arr()) {
        Value got = Value::array();
        for (const auto &p : exporting::export_item(st, "assessment", a, "PDF", nullptr, root)) got.push(p);
        out["assess:" + a.s("id")] = got;
    }
    std::vector<const Value *> lessons;
    for (const auto &l : st.data.get("lessons").arr()) lessons.push_back(&l);
    for (const auto &c : store::classes(st.settings(), lessons)) {
        Value got = Value::array();
        for (const auto &p : exporting::export_map(st, c.first, false, {"PDF", "DOCX", "CSV"}, "", "", root)) got.push(p);
        out["map:" + c.first] = got;
    }
    Value all = Value::array();
    for (const auto &p : exporting::export_map(st, "", true, {"PDF", "CSV"}, "", "", root)) all.push(p);
    out["map:all"] = all;
    std::vector<std::pair<Value, const Value *>> picks;
    for (const auto &p : st.settings().get("class_periods").arr()) picks.push_back({p, lessons.empty() ? nullptr : lessons[0]});
    if (!picks.empty()) {
        Value got = Value::array();
        for (const auto &p : exporting::export_day(st, picks, Date{2026, 10, 14}, root)) got.push(p);
        out["day"] = got;
    }
    return out;
}

int dump(const std::string &what, const std::string &arg) {
    Value out;
    if (what == "store") out = dump_store();
    else if (what == "markup") out = dump_markup(arg);
    else if (what == "docs") out = dump_docs();
    else if (what == "wrap") out = dump_wrap(arg);
    else if (what == "txt") out = dump_txt();
    else if (what == "pdf") out = dump_pdf(arg);
    else if (what == "docx") out = dump_docx(arg);
    else if (what == "board") out = dump_board(arg);
    else if (what == "png") out = dump_png(arg);
    else if (what == "export") out = dump_export(arg);
    else if (what == "dates") {
        Date near{2026, 10, 14};
        out = date_list(dates::parse_dates(arg, near));
    } else {
        std::fprintf(stderr, "unknown: %s\n", what.c_str());
        return 2;
    }
    std::cout << json::dump(out, 1) << "\n";
    return 0;
}

}  // namespace check
