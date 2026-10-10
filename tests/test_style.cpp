// src/style against the reference: the style records of
// tests/golden/math/style.golden, the Unicode predicate tables of
// tests/golden/math/unicode.golden, the char_measure triples of
// tests/golden/font, and text -> material against tests/golden/core/
// paragraphs.golden with the real font reader behind it.

#include "core_golden.h"

#include "fontlib.h"
#include "style.h"
#include "unicode_tables.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

// The reference's default registry: the files imgui-bundle ships, by role.
// CODE_BOLD is left to its stand-in, as the reference leaves it.
void register_defaults(FontLibrary& library)
{
    static const struct {
        Face face;
        const char* file;
    } defaults[] = {
        {Face::PROSE, "Roboto-Regular.ttf"},
        {Face::PROSE_BOLD, "Roboto-Bold.ttf"},
        {Face::PROSE_ITALIC, "Roboto-RegularItalic.ttf"},
        {Face::PROSE_BOLD_ITALIC, "Roboto-BoldItalic.ttf"},
        {Face::CODE, "Inconsolata-Medium.ttf"},
        {Face::MATH, "latinmodern-math.otf"},
    };
    for (const auto& entry : defaults) {
        std::string why;
        if (!library.register_font(
                entry.face, golden::data_path(std::string("fonts/") + entry.file).c_str(), &why))
            FAIL("%s: %s", entry.file, why.c_str());
    }
}

bool spacing_of(std::string_view token, Spacing* out)
{
    if (token == "nonfrench")
        *out = Spacing::NONFRENCH;
    else if (token == "french")
        *out = Spacing::FRENCH;
    else if (token == "extra_space_only")
        *out = Spacing::EXTRA_SPACE_ONLY;
    else
        return false;
    return true;
}

bool align_of(std::string_view token, Align* out)
{
    if (token == "justify")
        *out = Align::JUSTIFY;
    else if (token == "left")
        *out = Align::LEFT;
    else if (token == "center")
        *out = Align::CENTER;
    else if (token == "right")
        *out = Align::RIGHT;
    else
        return false;
    return true;
}

// A font key as the goldens spell it, which must be one the port can hold.
FontSpec spec_of(const std::string& key)
{
    FontSpec spec;
    if (!parse_font_key_text(key, &spec))
        FAIL("'%s' is not a font key", key.c_str());
    return spec;
}

std::string utf8(std::initializer_list<int> bytes)
{
    std::string out;
    for (const int b : bytes)
        out.push_back(static_cast<char>(b));
    return out;
}

std::vector<uint32_t> decoded(const std::string& text)
{
    std::vector<uint32_t> out;
    decode_utf8(text, out);
    return out;
}

using Codepoints = std::vector<uint32_t>;

// A resolver with round numbers: every face is a 10-unit monospace that has
// ASCII only, except that MATH also has U+2192 and nothing has U+FFFD unless
// `has_replacement`. Kerns "AV" by -1.5 and "To" by -2.5, both exact halves.
class FakeMetrics : public FontMetrics
{
public:
    FakeMetrics(Face face, bool has_replacement)
        : _face(face)
        , _has_replacement(has_replacement)
    {
    }
    Face face() const override { return _face; }
    double size() const override { return 10.0; }
    double ascent() const override { return 8.0; }
    double descent() const override { return 2.0; }
    double x_height() const override { return 5.0; }
    double em() const override { return 10.0; }
    double space_width() const override { return 4.5; }
    bool has(uint32_t codepoint) const override
    {
        if (codepoint == 0x2192)
            return _face == Face::MATH;
        if (codepoint == REPLACEMENT_CHARACTER)
            return _has_replacement;
        return codepoint < 0x80;
    }
    double advance(uint32_t) const override { return 10.0; }
    GlyphBBox bbox(uint32_t) const override { return GlyphBBox{0.0, -2.5, 10.0, 7.5}; }
    double kern(uint32_t codepoint, uint32_t following) const override
    {
        if (codepoint == 'A' && following == 'V')
            return -1.5;
        if (codepoint == 'T' && following == 'o')
            return -2.5;
        return 0.0;
    }

private:
    Face _face;
    bool _has_replacement;
};

struct FakeFonts {
    bool has_replacement = true;
    bool knows_math = true;
    int resolved = 0;
    FakeMetrics faces[FACE_COUNT] = {
        {Face::PROSE, true},        {Face::PROSE_BOLD, true},
        {Face::PROSE_ITALIC, true}, {Face::PROSE_BOLD_ITALIC, true},
        {Face::CODE, true},         {Face::CODE_BOLD, true},
        {Face::MATH, true},
    };
    FakeMetrics bare[FACE_COUNT] = {
        {Face::PROSE, false},        {Face::PROSE_BOLD, false},
        {Face::PROSE_ITALIC, false}, {Face::PROSE_BOLD_ITALIC, false},
        {Face::CODE, false},         {Face::CODE_BOLD, false},
        {Face::MATH, false},
    };

    static const FontMetrics* resolve(void* user, FontKey key)
    {
        FakeFonts& self = *static_cast<FakeFonts*>(user);
        ++self.resolved;
        FontSpec spec;
        if (!parse_font_key(key, &spec))
            return nullptr;
        if (spec.face == Face::MATH && !self.knows_math)
            return nullptr;
        const size_t index = static_cast<size_t>(spec.face);
        return self.has_replacement ? &self.faces[index] : &self.bare[index];
    }

    MetricsResolver resolver() { return MetricsResolver{&resolve, this}; }
};

std::vector<std::string> lines_of(const std::vector<Node>& nodes)
{
    core_golden::Context context;
    std::vector<std::string> out;
    for (const Node& node : nodes)
        core_golden::write_node(context, node, out);
    return out;
}

} // namespace

// --- Unicode tables -------------------------------------------------------------------

TEST(unicode_tables_are_the_reference_interpreters)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/unicode.golden")));
    struct Table {
        const char* name;
        bool (*lookup)(uint32_t);
        bool seen;
    };
    Table tables[] = {
        {"isspace", &py_isspace, false},
        {"isprintable", &py_isprintable, false},
        {"isalnum", &py_isalnum, false},
        {"isalpha", &py_isalpha, false},
        {"isupper", &py_isupper, false},
        {"category_cc", [](uint32_t c) { return unicode_in(UNICODE_CATEGORY_CC, c); }, false},
        {"category_cf", [](uint32_t c) { return unicode_in(UNICODE_CATEGORY_CF, c); }, false},
        {"category_mn", [](uint32_t c) { return unicode_in(UNICODE_CATEGORY_MN, c); }, false},
        {"category_me", [](uint32_t c) { return unicode_in(UNICODE_CATEGORY_ME, c); }, false},
    };
    const std::vector<golden::Record>& records = file.records();
    size_t i = 0;
    while (i < records.size()) {
        const golden::Record& r = records[i++];
        if (r.is("unicode_version"))
            CHECK_EQ(r.str(0), std::string("14.0.0"));
        if (!r.is("predicate"))
            continue;
        const std::string name = r.str(0);
        CHECK_CONTEXT("%s", name.c_str());
        const uint32_t range_count = r.u32(1);
        const uint32_t true_count = r.u32(2);
        std::vector<bool> expected(0x110000, false);
        for (uint32_t k = 0; k < range_count && i < records.size(); ++k, ++i) {
            REQUIRE(records[i].is("range"));
            const uint32_t first = records[i].u32(0);
            const uint32_t last = records[i].u32(1);
            REQUIRE(first <= last && last <= 0x10FFFF);
            for (uint32_t c = first; c <= last; ++c)
                expected[c] = true;
        }
        Table* table = nullptr;
        for (Table& candidate : tables) {
            if (name == candidate.name)
                table = &candidate;
        }
        REQUIRE(table != nullptr);
        table->seen = true;
        uint32_t trues = 0;
        uint32_t wrong = 0;
        uint32_t first_wrong = 0;
        for (uint32_t c = 0; c < 0x110000; ++c) {
            const bool answer = table->lookup(c);
            trues += answer ? 1 : 0;
            if (answer != expected[c] && wrong++ == 0)
                first_wrong = c;
        }
        if (wrong != 0)
            FAIL("%u codepoints differ, the first U+%04X", wrong, first_wrong);
        CHECK_EQ(trues, true_count);
    }
    for (const Table& table : tables) {
        CHECK_CONTEXT("%s", table.name);
        CHECK(table.seen);
    }
    // Past the last codepoint nothing is anything.
    CHECK(!py_isspace(0x110000));
    CHECK(!py_isprintable(0xFFFFFFFFu));
}

// --- Font keys ----------------------------------------------------------------------

TEST(font_keys_are_spelled_and_read_as_the_reference_does)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/style.golden")));
    core_golden::FaceTable faces;
    int written = 0;
    int read = 0;
    int refused = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        if (r.is("font_key")) {
            Face face = Face::PROSE;
            REQUIRE(face_from_value(r.str(0), &face));
            const Scaled size = r.i64(1);
            const std::string key = r.str(2);
            CHECK_EQ(font_key_text(face, size), key);
            if (size >= 0 && size <= FontKey::MAX_SIZE) {
                // The interned key is the one the golden reader makes of the
                // string, and reads back as the face and size it was made of.
                const FontKey interned = font_key(face, size);
                CHECK(interned == faces.key(key));
                CHECK(interned == (FontSpec{face, size}.key()));
                FontSpec back;
                REQUIRE(parse_font_key(interned, &back));
                CHECK(back.face == face);
                CHECK_EQ(back.size, size);
            }
            ++written;
        } else if (r.is("parse_font_key")) {
            const std::string key = r.str(0);
            CHECK_CONTEXT("'%s'", key.c_str());
            const bool ok = r.raw(1) == "ok";
            FontSpec spec;
            const bool parsed = parse_font_key_text(key, &spec);
            // The one record the port answers differently, on purpose: digits
            // outside ASCII (docs/divergences.md, "style").
            if (key == "prose@\xD9\xA3\xD9\xA4") {
                CHECK(ok);
                CHECK(!parsed);
                ++refused;
                continue;
            }
            CHECK_EQ(parsed, ok);
            if (ok && parsed) {
                CHECK_EQ(std::string(face_value(spec.face)), r.str(2));
                CHECK_EQ(spec.size, r.i64(3));
            }
            ++read;
        }
    }
    CHECK_EQ(written, 63);
    CHECK_EQ(read, 25);
    CHECK_EQ(refused, 1);
}

TEST(a_font_key_text_that_does_not_fit_is_refused)
{
    FontSpec spec;
    CHECK(parse_font_key_text("prose@9223372036854775807", &spec));
    CHECK_EQ(spec.size, INT64_MAX);
    CHECK(parse_font_key_text("prose@-9223372036854775807", &spec));
    CHECK_EQ(spec.size, -INT64_MAX);
    CHECK(!parse_font_key_text("prose@9223372036854775808", &spec));
    CHECK(!parse_font_key_text("prose@99999999999999999999999999", &spec));
    // Python's int() rules for underscores and signs.
    CHECK(!parse_font_key_text("prose@1__0", &spec));
    CHECK(!parse_font_key_text("prose@_1", &spec));
    CHECK(!parse_font_key_text("prose@1_", &spec));
    CHECK(!parse_font_key_text("prose@+ 1", &spec));
    CHECK(!parse_font_key_text("prose@--5", &spec));
    CHECK(!parse_font_key_text("prose@+", &spec));
    CHECK(parse_font_key_text("prose@\t00_12\v", &spec));
    CHECK_EQ(spec.size, 12);
    // A key some other registry minted.
    CHECK(!parse_font_key(FontKey::make(7, 10), &spec));
    CHECK(!parse_font_key(FontKey::make(FontKey::MAX_FACE, 10), nullptr));
    CHECK(parse_font_key(FontKey::make(6, 10), nullptr));
}

// --- Space factor, interword glue, strut ----------------------------------------------

TEST(space_factor_interword_glue_and_strut_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/style.golden")));
    int factors = 0;
    int glues = 0;
    int struts = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("space_factor_after")) {
            const uint32_t codepoint = c.u32();
            const int current = c.i32();
            CHECK_EQ(space_factor_after(codepoint, current), c.i32());
            ++factors;
        } else if (r.is("interword_glue")) {
            const Scaled space_width = c.i64();
            const int factor = c.i32();
            Spacing spacing = Spacing::NONFRENCH;
            REQUIRE(spacing_of(c.raw(), &spacing));
            const GlueSpec spec = interword_glue(space_width, factor, spacing);
            CHECK_EQ(spec.width, c.i64());
            CHECK_EQ(spec.stretch, c.i64());
            CHECK_EQ(spec.shrink, c.i64());
            CHECK(spec.stretch_order == GlueOrder::NORMAL);
            CHECK(spec.shrink_order == GlueOrder::NORMAL);
            ++glues;
        } else if (r.is("strut_filling")) {
            const Strut strut = Strut::filling(c.i64());
            CHECK_EQ(strut.height, c.i64());
            CHECK_EQ(strut.depth, c.i64());
            ++struts;
        } else {
            continue;
        }
        c.expect_done();
    }
    CHECK_EQ(factors, 180);
    CHECK_EQ(glues, 816);
    CHECK_EQ(struts, 15);
}

TEST(interword_glue_defaults_and_bad_space_factors)
{
    // The function's default is the transcription of section 1044.
    const GlueSpec plain = interword_glue(300);
    CHECK_EQ(plain.width, 300);
    CHECK_EQ(plain.stretch, 150);
    CHECK_EQ(plain.shrink, 100);
    const GlueSpec sentence = interword_glue(300, 3000);
    CHECK_EQ(sentence.width, 400);
    CHECK_EQ(sentence.stretch, 450);
    CHECK_EQ(sentence.shrink, 33);
    // The composition's default is the reference theme's.
    CHECK(Composition().spacing == Spacing::EXTRA_SPACE_ONLY);

    // A space factor nothing can produce is a contract violation, answered as
    // a neutral one and never divided by.
    static int violations = 0;
    violations = 0;
    const ContractHandler before = contract_handler();
    contract_handler() = [](const char*) { ++violations; };
    for (const int factor : {0, -1, 32768, INT32_MIN, INT32_MAX}) {
        const GlueSpec spec = interword_glue(300, factor);
        CHECK(spec == plain);
    }
    contract_handler() = before;
    CHECK_EQ(violations, 5);
}

TEST(a_strut_floors_a_box_and_leaves_the_rest)
{
    const Strut strut = Strut::filling(1258291);
    CHECK_EQ(strut.total(), 1258291);
    Box box;
    box.width = 77;
    box.height = 5;
    box.depth = 2000000;
    box.glue_set = 0.25;
    strut.apply_to(box);
    CHECK_EQ(box.height, strut.height);
    CHECK_EQ(box.depth, 2000000);
    CHECK_EQ(box.width, 77);
    CHECK_EQ(box.glue_set, 0.25);
}

// --- Metrics through the real reader ----------------------------------------------------

TEST(fontlib_metrics_and_char_measure_answer_as_the_reference_does)
{
    FontLibrary library;
    register_defaults(library);
    const MetricsResolver resolve = fontlib_metrics(library);
    CharMeasureCache measure(resolve);
    golden::File file;
    REQUIRE(file.load(golden::path("math/style.golden")));
    int faces = 0;
    int has = 0;
    int measures = 0;
    int kerns = 0;
    for (const golden::Record& r : file) {
        const bool mine = r.is("face_metrics") || r.is("has") || r.is("measure") || r.is("kern");
        if (!mine)
            continue;
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        const std::string key = c.str();
        CHECK_CONTEXT("%s", key.c_str());
        const FontSpec spec = spec_of(key);
        const FontMetrics* m = resolve(spec.key());
        REQUIRE(m != nullptr);
        if (r.is("face_metrics")) {
            CHECK_EQ(m->size(), c.f64());
            CHECK_EQ(m->ascent(), c.f64());
            CHECK_EQ(m->descent(), c.f64());
            CHECK_EQ(m->x_height(), c.f64());
            CHECK_EQ(m->em(), c.f64());
            CHECK_EQ(m->space_width(), c.f64());
            ++faces;
        } else if (r.is("has")) {
            const uint32_t codepoint = c.u32();
            CHECK_CONTEXT("U+%04X", codepoint);
            CHECK_EQ(m->has(codepoint), c.boolean());
            ++has;
        } else if (r.is("measure")) {
            const uint32_t codepoint = c.u32();
            CHECK_CONTEXT("U+%04X", codepoint);
            // Twice: the second answer is the memo's.
            for (int pass = 0; pass < 2; ++pass) {
                golden::Cursor again(r, 2);
                const CharDimens dimens = measure(spec.key(), codepoint);
                CHECK_EQ(dimens.width, again.i64());
                CHECK_EQ(dimens.height, again.i64());
                CHECK_EQ(dimens.depth, again.i64());
            }
            ++measures;
            continue;
        } else {
            const uint32_t codepoint = c.u32();
            const uint32_t following = c.u32();
            CHECK_CONTEXT("U+%04X U+%04X", codepoint, following);
            CHECK_EQ(m->kern(codepoint, following), c.f64());
            ++kerns;
        }
        c.expect_done();
    }
    CHECK_EQ(faces, 28);
    CHECK_EQ(has, 749);
    CHECK_EQ(measures, 749);
    CHECK_EQ(kerns, 56);
    CHECK_EQ(measure.size(), static_cast<size_t>(749));
}

TEST(char_measure_reproduces_the_bulk_triples)
{
    FontLibrary library;
    register_defaults(library);
    CharMeasureCache cache(fontlib_metrics(library));
    const CharMeasure measure = cache.measure();
    int files = 0;
    size_t triples = 0;
    for (const std::string& path : golden::list("font", ".charmeasure.golden")) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!file.load(path))
            continue;
        ++files;
        FontKey key;
        bool sized = false;
        for (const golden::Record& r : file) {
            CHECK_CONTEXT("line %d", r.line);
            golden::Cursor c(r);
            if (r.is("CMSIZE")) {
                c.i64();
                key = spec_of(c.str()).key();
                sized = true;
            } else if (r.is("CM")) {
                REQUIRE(sized);
                const uint32_t codepoint = c.u32();
                CHECK_CONTEXT("U+%04X", codepoint);
                const CharDimens dimens = measure(key, codepoint);
                CHECK_EQ(dimens.width, c.i64());
                CHECK_EQ(dimens.height, c.i64());
                CHECK_EQ(dimens.depth, c.i64());
                ++triples;
            }
        }
    }
    CHECK_EQ(files, 6);
    CHECK(triples > 30000);
    // Every triple was a different (key, codepoint), so the memo grew through
    // several rehashes and still answers the first one.
    CHECK_EQ(cache.size(), triples);
}

TEST(char_measure_of_an_unknown_face_is_nothing)
{
    FakeFonts fonts;
    fonts.knows_math = false;
    CharMeasureCache cache(fonts.resolver());
    const CharDimens known = cache(font_key(Face::PROSE, 10), 'x');
    CHECK_EQ(known.width, 10);
    // 7.5 and 2.5 are exact halves: round() takes each to the even neighbour.
    CHECK_EQ(known.height, 8);
    CHECK_EQ(known.depth, 2);
    const CharDimens unknown = cache(font_key(Face::MATH, 10), 'x');
    CHECK_EQ(unknown.width, 0);
    CHECK_EQ(unknown.height, 0);
    CHECK_EQ(unknown.depth, 0);
    const CharDimens foreign = cache(FontKey::make(99, 10), 'x');
    CHECK_EQ(foreign.width, 0);

    // The memo: a repeat does not go back to the resolver.
    const int resolved = fonts.resolved;
    cache(font_key(Face::PROSE, 10), 'x');
    cache(font_key(Face::MATH, 10), 'x');
    CHECK_EQ(fonts.resolved, resolved);
    cache.clear();
    CHECK_EQ(cache.size(), static_cast<size_t>(0));
    cache(font_key(Face::PROSE, 10), 'x');
    CHECK_EQ(fonts.resolved, resolved + 1);
    CHECK(!MetricsResolver());
    CHECK(MetricsResolver()(font_key(Face::PROSE, 10)) == nullptr);
}

// --- UTF-8 --------------------------------------------------------------------------------

TEST(well_formed_utf8_decodes_to_what_it_encodes)
{
    CHECK(decoded("") == Codepoints{});
    CHECK(decoded("aZ") == (Codepoints{'a', 'Z'}));
    // The first and last codepoint of each length, and both sides of the
    // surrogate gap.
    CHECK(decoded(utf8({0x00})) == (Codepoints{0}));
    CHECK(decoded(utf8({0x7F, 0xC2, 0x80, 0xDF, 0xBF})) == (Codepoints{0x7F, 0x80, 0x7FF}));
    CHECK(decoded(utf8({0xE0, 0xA0, 0x80, 0xEF, 0xBF, 0xBF})) == (Codepoints{0x800, 0xFFFF}));
    CHECK(decoded(utf8({0xED, 0x9F, 0xBF, 0xEE, 0x80, 0x80})) == (Codepoints{0xD7FF, 0xE000}));
    CHECK(decoded(utf8({0xF0, 0x90, 0x80, 0x80, 0xF4, 0x8F, 0xBF, 0xBF})) ==
          (Codepoints{0x10000, 0x10FFFF}));
    // A NUL is a character and does not end the text.
    CHECK(decoded(std::string("a\0b", 3)) == (Codepoints{'a', 0, 'b'}));

    Codepoints out;
    std::vector<uint32_t> offsets;
    decode_utf8("a\xC3\xA9\xE6\xBC\xA2\xF0\x9F\x98\x80z", out, &offsets);
    CHECK(out == (Codepoints{'a', 0xE9, 0x6F22, 0x1F600, 'z'}));
    CHECK(offsets == (std::vector<uint32_t>{0, 1, 3, 6, 10}));
}

TEST(malformed_utf8_becomes_replacement_characters_and_reads_nothing_out_of_bounds)
{
    const uint32_t R = REPLACEMENT_CHARACTER;
    // Each expectation is what Python's bytes.decode("utf-8", "replace")
    // answers for the same bytes: one U+FFFD per maximal ill-formed subpart.
    CHECK(decoded(utf8({0x80})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xBF, 'a'})) == (Codepoints{R, 'a'}));
    CHECK(decoded(utf8({0xC0, 0x80})) == (Codepoints{R, R}));          // overlong NUL
    CHECK(decoded(utf8({0xC1, 0xBF})) == (Codepoints{R, R}));          // overlong U+007F
    CHECK(decoded(utf8({0xE0, 0x80, 0x80})) == (Codepoints{R, R, R})); // overlong, 3 bytes
    CHECK(decoded(utf8({0xE0, 0x9F, 0xBF})) == (Codepoints{R, R, R}));
    CHECK(decoded(utf8({0xF0, 0x80, 0x80, 0x80})) == (Codepoints{R, R, R, R}));
    CHECK(decoded(utf8({0xF0, 0x8F, 0xBF, 0xBF})) == (Codepoints{R, R, R, R}));
    CHECK(decoded(utf8({0xED, 0xA0, 0x80})) == (Codepoints{R, R, R}));          // U+D800
    CHECK(decoded(utf8({0xED, 0xBF, 0xBF})) == (Codepoints{R, R, R}));          // U+DFFF
    CHECK(decoded(utf8({0xF4, 0x90, 0x80, 0x80})) == (Codepoints{R, R, R, R})); // U+110000
    CHECK(decoded(utf8({0xF5, 0x80, 0x80, 0x80})) == (Codepoints{R, R, R, R}));
    CHECK(decoded(utf8({0xFE})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xFF, 0xFF})) == (Codepoints{R, R}));
    // Truncated: the bytes that were a valid start are one subpart.
    CHECK(decoded(utf8({0xC3})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xE6, 0xBC})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xF0, 0x9F, 0x98})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xF0, 0x9F})) == (Codepoints{R}));
    CHECK(decoded(utf8({0xF0})) == (Codepoints{R}));
    // Cut short by a byte that starts something else, which is kept.
    CHECK(decoded(utf8({0xE6, 0xBC, 'a'})) == (Codepoints{R, 'a'}));
    CHECK(decoded(utf8({0xF0, 0x9F, 0x98, 0xC3, 0xA9})) == (Codepoints{R, 0xE9}));
    CHECK(decoded(utf8({0xC3, 0xC3, 0xA9})) == (Codepoints{R, 0xE9}));
    CHECK(decoded(utf8({0xE6, 0xF0, 0x9F, 0x98, 0x80})) == (Codepoints{R, 0x1F600}));
    // A fifth and sixth byte never continue anything.
    CHECK(decoded(utf8({0xF8, 0x88, 0x80, 0x80, 0x80})) == (Codepoints{R, R, R, R, R}));

    // Every one- and two-byte string, and every lead byte followed by a valid
    // tail cut at each length: the decoder answers within the bytes given
    // (the sanitiser build is what would see otherwise) and never produces a
    // surrogate or a value past U+10FFFF.
    for (int a = 0; a < 256; ++a) {
        for (int b = 0; b < 256; ++b) {
            for (const std::string& text : {utf8({a}), utf8({a, b}), utf8({a, b, 0x80}),
                                            utf8({a, b, 0x80, 0x80}), utf8({a, 0x80, b})}) {
                Codepoints out;
                std::vector<uint32_t> offsets;
                decode_utf8(text, out, &offsets);
                bool sound = !out.empty() && out.size() <= text.size() &&
                             offsets.size() == out.size() && offsets[0] == 0;
                for (size_t i = 0; i < out.size(); ++i) {
                    sound = sound && out[i] <= 0x10FFFF && !(out[i] >= 0xD800 && out[i] <= 0xDFFF);
                    sound = sound && offsets[i] < text.size() &&
                            (i == 0 || offsets[i] > offsets[i - 1]);
                }
                if (!sound)
                    FAIL("bytes %02X %02X decoded unsoundly", a, b);
            }
        }
    }
}

// --- Composition, with round numbers --------------------------------------------------

TEST(append_text_sets_characters_kerns_glue_and_penalties)
{
    FakeFonts fonts;
    const MetricsResolver resolve = fonts.resolver();
    const FontSpec prose{Face::PROSE, 10};
    const FontKey key = prose.key();
    Composition nonfrench;
    nonfrench.spacing = Spacing::NONFRENCH;

    Run run;
    std::vector<uint32_t> offsets;
    run.source_offsets = &offsets;
    run.source_base = 100;
    // Leading white space is dropped; a run of it, of any kind, is one glue;
    // AV kerns by round(-1.5) = -2 and To by round(-2.5) = -2 (half to even);
    // the kern is not looked up across white space or past the end.
    REQUIRE(append_text(run, " \t AV\xC2\xA0\n To. A T", prose, true, nonfrench, resolve));
    // The space width is round(4.5) = 4: stretch round(2.0), shrink round(1.33).
    const GlueSpec space = interword_glue(4, 999, Spacing::NONFRENCH);
    const GlueSpec after_period = interword_glue(4, 3000, Spacing::NONFRENCH);
    const std::vector<Node> expected = {
        char_node(key, 'A'), kern(-2),
        char_node(key, 'V'), glue(space),
        char_node(key, 'T'), kern(-2),
        char_node(key, 'o'), char_node(key, '.'),
        glue(after_period),  char_node(key, 'A'),
        glue(space),         char_node(key, 'T'),
    };
    CHECK_LINES_EQ(lines_of(expected), lines_of(run.nodes), "material");
    CHECK(offsets ==
          (std::vector<uint32_t>{103, 103, 104, 105, 109, 109, 110, 111, 112, 113, 114, 115}));
    CHECK_EQ(run.space_factor, 999);

    // The space factor carries into the next append, and trailing white space
    // is a glue: stripping is the block's business.
    REQUIRE(append_text(run, " b ", prose, true, nonfrench, resolve));
    REQUIRE_EQ(run.nodes.size(), expected.size() + 3);
    CHECK(run.nodes[expected.size()] == glue(space));
    CHECK(run.nodes.back() == glue(interword_glue(4, 1000, Spacing::NONFRENCH)));

    // Unbreakable: every glue is preceded by a penalty that forbids the break.
    Run code;
    REQUIRE(append_text(code, "a  b", FontSpec{Face::CODE, 10}, false, Composition(), resolve));
    const FontKey code_key = font_key(Face::CODE, 10);
    const std::vector<Node> unbreakable = {
        char_node(code_key, 'a'),
        penalty(INF_PENALTY),
        glue(interword_glue(4, 1000, Spacing::EXTRA_SPACE_ONLY)),
        char_node(code_key, 'b'),
    };
    CHECK_LINES_EQ(lines_of(unbreakable), lines_of(code.nodes), "unbreakable material");

    // Empty and all-space text append nothing to an empty run.
    Run empty;
    REQUIRE(append_text(empty, "", prose, true, Composition(), resolve));
    REQUIRE(append_text(empty, " \n\t ", prose, true, Composition(), resolve));
    CHECK(empty.nodes.empty());
}

TEST(append_text_refuses_a_face_the_resolver_does_not_know)
{
    FakeFonts fonts;
    fonts.knows_math = false;
    Run run;
    run.nodes.push_back(penalty(0));
    std::string why;
    CHECK(!append_text(run, "x", FontSpec{Face::MATH, 10}, true, Composition(), fonts.resolver(),
                       &why));
    CHECK_EQ(why, std::string("no metrics for math@10"));
    CHECK(!append_text_block(run, "x\ny", FontSpec{Face::MATH, 10}, true, Align::LEFT,
                             Composition(), fonts.resolver(), &why));
    CHECK(
        !append_text(run, "x", FontSpec{Face::PROSE, 10}, true, Composition(), MetricsResolver()));
    CHECK_EQ(run.nodes.size(), static_cast<size_t>(1));
}

TEST(a_character_the_face_lacks_is_borrowed_replaced_or_left)
{
    FakeFonts fonts;
    const MetricsResolver resolve = fonts.resolver();
    const Composition composition;
    const FontKey prose = font_key(Face::PROSE, 10);
    const FontKey bold = font_key(Face::PROSE_BOLD, 10);
    const FontKey math = font_key(Face::MATH, 10);

    // Borrowed from the math face, at the same size.
    StandIn shown = stand_in(composition, resolve, bold, 0x2192);
    CHECK(shown.font == math);
    CHECK_EQ(shown.codepoint, 0x2192u);
    // No face has it: U+FFFD in the regular face.
    shown = stand_in(composition, resolve, bold, 0x6F22);
    CHECK(shown.font == prose);
    CHECK_EQ(shown.codepoint, REPLACEMENT_CHARACTER);
    // White space and Cc, Cf, Mn, Me are left as written.
    for (const uint32_t codepoint : {0x3000u, 0x85u, 0x200Du, 0xADu, 0x301u, 0x20DDu}) {
        shown = stand_in(composition, resolve, bold, codepoint);
        CHECK(shown.font == bold);
        CHECK_EQ(shown.codepoint, codepoint);
    }
    // A key that is not a face's is left alone.
    shown = stand_in(composition, resolve, FontKey::make(99, 10), 0x6F22);
    CHECK(shown.font == FontKey::make(99, 10));
    CHECK_EQ(shown.codepoint, 0x6F22u);

    // No math face: it is passed over, and the arrow is replaced.
    fonts.knows_math = false;
    shown = stand_in(composition, resolve, bold, 0x2192);
    CHECK(shown.font == prose);
    CHECK_EQ(shown.codepoint, REPLACEMENT_CHARACTER);
    // No U+FFFD in the regular face either: a question mark.
    fonts.has_replacement = false;
    shown = stand_in(composition, resolve, bold, 0x2192);
    CHECK(shown.font == prose);
    CHECK_EQ(shown.codepoint, uint32_t('?'));
    // The regular face is the one asked about itself: math is not searched for
    // what math is bound to, and the replacement is still the regular face's.
    fonts.knows_math = true;
    shown = stand_in(composition, resolve, math, 0x6F22);
    CHECK(shown.font == prose);
    CHECK_EQ(shown.codepoint, uint32_t('?'));

    // In material: the borrowed character is an ordinary node in the other
    // face, the replaced one remembers what was written, and neither is
    // followed by a kern. Malformed bytes are U+FFFD as text, which the fake
    // faces have, so it is an ordinary character.
    fonts.has_replacement = true;
    Run run;
    REQUIRE(append_text(run, "A\xE2\x86\x92V\xE6\xBC\xA2\xFFV", FontSpec{Face::PROSE, 10}, true,
                        composition, resolve));
    const std::vector<Node> expected = {
        char_node(prose, 'A'),
        char_node(math, 0x2192),
        char_node(prose, 'V'),
        replaced_char(prose, REPLACEMENT_CHARACTER, 0x6F22),
        char_node(prose, REPLACEMENT_CHARACTER),
        char_node(prose, 'V'),
    };
    CHECK_LINES_EQ(lines_of(expected), lines_of(run.nodes), "material");
    CHECK(!run.nodes[4].is_replaced());
}

TEST(a_text_block_strips_normalises_newlines_and_breaks_hard)
{
    FakeFonts fonts;
    const MetricsResolver resolve = fonts.resolver();
    const FontSpec prose{Face::PROSE, 10};
    const FontKey key = prose.key();
    const Composition composition;
    const Node space = glue(interword_glue(4, 1000, Spacing::EXTRA_SPACE_ONLY));

    // Soft: every newline spelling is white space.
    Run soft;
    REQUIRE(append_text_block(soft, "\r\n a\r\nb\rc\n\n", prose, false, Align::JUSTIFY, composition,
                              resolve));
    const std::vector<Node> soft_expected = {
        char_node(key, 'a'), space, char_node(key, 'b'), space, char_node(key, 'c'),
    };
    CHECK_LINES_EQ(lines_of(soft_expected), lines_of(soft.nodes), "soft material");

    // Hard: \r\n is one newline and not two; white space around a newline
    // goes; a blank line in the middle is two breaks in a row; newlines at the
    // ends are dropped.
    Run hard;
    std::vector<uint32_t> offsets;
    hard.source_offsets = &offsets;
    REQUIRE(append_text_block(hard, "\n a \r\n b\r\rc \n", prose, true, Align::JUSTIFY, composition,
                              resolve));
    std::vector<Node> hard_expected = {char_node(key, 'a')};
    fill_and_break(Align::JUSTIFY, hard_expected);
    hard_expected.push_back(char_node(key, 'b'));
    fill_and_break(Align::JUSTIFY, hard_expected);
    fill_and_break(Align::JUSTIFY, hard_expected);
    hard_expected.push_back(char_node(key, 'c'));
    CHECK_LINES_EQ(lines_of(hard_expected), lines_of(hard.nodes), "hard material");
    CHECK(offsets == (std::vector<uint32_t>{2, 4, 4, 4, 7, 8, 8, 8, 9, 9, 9, 10}));

    // Ragged left and centred leave the fill out.
    for (const Align align : {Align::RIGHT, Align::CENTER}) {
        Run ragged;
        REQUIRE(append_text_block(ragged, "a\nb", prose, true, align, composition, resolve));
        const std::vector<Node> expected = {
            char_node(key, 'a'),
            penalty(EJECT_PENALTY),
            char_node(key, 'b'),
        };
        CHECK_LINES_EQ(lines_of(expected), lines_of(ragged.nodes), "ragged material");
    }
    std::vector<Node> terminator;
    fill_and_break(Align::LEFT, terminator);
    REQUIRE_EQ(terminator.size(), static_cast<size_t>(3));
    CHECK(terminator[0] == penalty(INF_PENALTY));
    CHECK(terminator[1] == glue(PARFILLSKIP));
    CHECK(terminator[1].stretch_order == GlueOrder::FIL);
    CHECK(terminator[2] == penalty(EJECT_PENALTY));

    // Nothing but white space: nothing.
    Run blank;
    REQUIRE(append_text_block(blank, " \r\n\n ", prose, true, Align::LEFT, composition, resolve));
    CHECK(blank.nodes.empty());
}

// --- Text -> material against the reference's paragraphs -------------------------------

namespace
{

struct Piece {
    const char* text;
    Face face;
    bool breakable;
};

// The calls the reference's markdown converter makes of `_append_text` for the
// two markdown-built cases that need no markdown to replay: one per inline
// token, in the face its emphasis selects, code spans unbreakable.
const Piece MIXED_FACES[] = {
    {"Plain words, then ", Face::PROSE, true},
    {"bold words", Face::PROSE_BOLD, true},
    {" and ", Face::PROSE, true},
    {"italic words", Face::PROSE_ITALIC, true},
    {" and ", Face::PROSE, true},
    {"both at once", Face::PROSE_BOLD_ITALIC, true},
    {", with ", Face::PROSE, true},
    {"inline code --flag=value", Face::CODE, false},
    {" that must not be cut, and a ", Face::PROSE, true},
    {"link", Face::PROSE, true},
    {" to finish the paragraph off properly across a few lines.", Face::PROSE, true},
};

const Piece STAND_INS_IN_CODE[] = {
    {"Inline ", Face::PROSE, true},
    {"\xCF\x80 r\xC2\xB2 \xE2\x86\x92 \xE6\xBC\xA2", Face::CODE, false},
    {" in the code face.", Face::PROSE, true},
};

} // namespace

TEST(text_becomes_the_material_the_reference_builds)
{
    FontLibrary library;
    register_defaults(library);
    const MetricsResolver resolve = fontlib_metrics(library);
    CharMeasureCache measure(resolve);

    golden::File file;
    REQUIRE(file.load(golden::path("core/paragraphs.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/paragraphs");
    REQUIRE(!cases.empty());
    int from_text = 0;
    int from_pieces = 0;
    int not_replayed = 0;
    int measured = 0;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
        core_golden::Reader reader(file, c);
        core_golden::Context context;
        const golden::Record& source = reader.take("source");
        const std::string source_kind(source.raw(0));
        const std::string text = source.str(1);
        golden::Cursor theme(reader.take("theme"));
        theme.f64(); // px per inch: already in the key's size
        Composition composition;
        REQUIRE(spacing_of(theme.raw(), &composition.spacing));
        theme.raw(); // block kind: the key says which face and size it chose
        Align align = Align::JUSTIFY;
        REQUIRE(align_of(theme.raw(), &align));
        const FontSpec font = spec_of(theme.str());
        theme.i64(); // left indent: paragraph assembly's
        // The first-line indent is a box paragraph assembly puts before the
        // run; no case has one, and if one appears this test must say so.
        REQUIRE_EQ(theme.i64(), 0);
        theme.expect_done();
        // The one theme not built on the default faces: Faces.mono().
        if (c.name == "monospace-body") {
            composition.regular = Face::CODE;
            composition.code = Face::CODE;
        }

        // What char_measure answered for every character of the case, the
        // math face's synthetic codepoints included.
        const core_golden::MeasureTable table = core_golden::read_measure(reader, context);
        CHECK(table.present);
        for (const core_golden::MeasureTable::Entry& entry : table.entries) {
            CHECK_CONTEXT("%s U+%04X", context.faces.text(entry.font).c_str(), entry.codepoint);
            const CharDimens dimens = measure(entry.font, entry.codepoint);
            CHECK_EQ(dimens.width, entry.width);
            CHECK_EQ(dimens.height, entry.height);
            CHECK_EQ(dimens.depth, entry.depth);
            ++measured;
        }

        std::string list_name;
        const size_t first = reader.position();
        core_golden::read_nodes(reader, context, &list_name);
        REQUIRE_EQ(list_name, std::string("material"));
        const std::vector<std::string> expected = reader.lines(first, reader.position());

        Run run;
        std::string why;
        if (source_kind == "text" || source_kind == "text-hard-breaks") {
            if (!append_text_block(run, text, font, source_kind == "text-hard-breaks", align,
                                   composition, resolve, &why)) {
                FAIL("refused: %s", why.c_str());
                continue;
            }
            ++from_text;
        } else if (c.name == "mixed-faces" || c.name == "stand-in-characters-in-code") {
            const bool mixed = c.name == "mixed-faces";
            const Piece* pieces = mixed ? MIXED_FACES : STAND_INS_IN_CODE;
            const size_t count =
                mixed ? IM_ARRAYSIZE(MIXED_FACES) : IM_ARRAYSIZE(STAND_INS_IN_CODE);
            bool ok = true;
            for (size_t i = 0; i < count && ok; ++i) {
                ok = append_text(run, pieces[i].text, FontSpec{pieces[i].face, font.size},
                                 pieces[i].breakable, composition, resolve, &why);
            }
            if (!ok) {
                FAIL("refused: %s", why.c_str());
                continue;
            }
            ++from_pieces;
        } else {
            // Built by markdown, with inline math or from the demo document:
            // not replayable without the layers that stay in Python (D1).
            ++not_replayed;
            continue;
        }
        std::vector<std::string> actual;
        core_golden::write_nodes(context, "material", run.nodes, actual);
        CHECK_LINES_EQ(expected, actual, "material");
    }
    CHECK_EQ(from_text, 19);
    CHECK_EQ(from_pieces, 2);
    CHECK_EQ(not_replayed, 5);
    CHECK_EQ(measured, 814);
}
