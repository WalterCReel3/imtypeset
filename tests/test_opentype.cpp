// src/opentype against tests/golden/font/*.raw.golden and
// latinmodern-math.otf.math.golden (tools/oracle/FORMAT-font.md): every table
// the reader reads, for every glyph id of every bundled file, in font units.
// Then the same files damaged, which must never do worse than fail to load.

#include "golden.h"

#include "opentype.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

const std::string RAW_SUFFIX = ".raw.golden";

// The basenames of the fonts that have a raw golden.
std::vector<std::string> font_names()
{
    std::vector<std::string> names;
    for (const std::string& path : golden::list("font", RAW_SUFFIX)) {
        const size_t slash = path.find_last_of('/');
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        names.push_back(file.substr(0, file.size() - RAW_SUFFIX.size()));
    }
    return names;
}

struct Loaded {
    std::vector<uint8_t> bytes;
    OpenTypeFont font;
    bool ok = false;
};

void load(const std::string& name, Loaded& out)
{
    out.ok = false;
    if (!golden::read_file(golden::data_path("fonts/" + name), out.bytes)) {
        FAIL("cannot read tests/data/fonts/%s", name.c_str());
        return;
    }
    std::string why;
    out.ok = out.font.load(out.bytes.data(), out.bytes.size(), &why);
    if (!out.ok)
        FAIL("%s does not load: %s", name.c_str(), why.c_str());
}

double mapped_count(const OpenTypeFont& font)
{
    double total = 0;
    for (const CmapRange& range : font.cmap())
        total += static_cast<double>(range.last - range.first) + 1.0;
    return total;
}

} // namespace

TEST(every_bundled_font_matches_its_raw_golden)
{
    const std::vector<std::string> names = font_names();
    // The seven files under tests/data/fonts, each with a golden.
    REQUIRE_EQ(names.size(), size_t(7));
    for (const std::string& name : names) {
        CHECK_CONTEXT("%s", name.c_str());
        Loaded f;
        load(name, f);
        golden::File file;
        if (!f.ok || !file.load(golden::path("font/" + name + RAW_SUFFIX)))
            continue;
        const OpenTypeFont& font = f.font;
        CHECK(font.warnings().empty());

        std::vector<uint32_t> axis, axis_glyphs;
        int glyph_records = 0, cmap_records = 0, kern_answers = 0;
        for (const golden::Record& r : file) {
            CHECK_CONTEXT("line %d", r.line);
            golden::Cursor c(r);
            if (r.is("FONT")) {
                c.str();
                CHECK_EQ(c.i64(), static_cast<int64_t>(f.bytes.size()));
                c.raw();
                CHECK_EQ(c.str() == "OTTO", font.is_cff());
            } else if (r.is("HEAD")) {
                CHECK_EQ(c.i32(), font.units_per_em());
            } else if (r.is("HHEA")) {
                CHECK_EQ(c.i32(), font.hhea_ascender());
                CHECK_EQ(c.i32(), font.hhea_descender());
                CHECK_EQ(c.i32(), font.hhea_line_gap());
                CHECK_EQ(c.u32(), font.hmetric_count());
            } else if (r.is("MAXP")) {
                CHECK_EQ(c.u32(), font.glyph_count());
            } else if (r.is("OS2")) {
                const bool has_x_height = r.boolean(7);
                CHECK_EQ(has_x_height ? r.i32(8) : 0, font.os2_x_height());
            } else if (r.is("CMAPSUB")) {
                const int platform = c.i32(), encoding = c.i32(), format = c.i32();
                c.i64();
                if (c.boolean()) {
                    CHECK_EQ(platform, font.cmap_platform());
                    CHECK_EQ(encoding, font.cmap_encoding());
                    CHECK_EQ(format, font.cmap_format());
                }
            } else if (r.is("CMAPCOUNT")) {
                CHECK_EQ(static_cast<double>(c.i64()), mapped_count(font));
            } else if (r.is("CMAP")) {
                const uint32_t codepoint = c.u32();
                uint32_t glyph = 0xFFFFFFFFu;
                CHECK(font.lookup(codepoint, &glyph));
                CHECK_EQ(c.u32(), glyph);
                ++cmap_records;
            } else if (r.is("GLYPH")) {
                const uint32_t glyph = c.u32();
                const std::string glyph_name = c.str();
                CHECK_EQ(c.i32(), font.advance(glyph));
                CHECK_EQ(c.i32(), font.left_side_bearing(glyph));
                // Glyph names are the font's own only for CFF; fontTools
                // invents the TrueType ones.
                if (font.is_cff())
                    CHECK_EQ(glyph_name, std::string(font.glyph_name(glyph)));
                const GlyphBounds b = font.bounds(glyph);
                CHECK_EQ(r.f64(10), b.x_min);
                CHECK_EQ(r.f64(11), b.y_min);
                CHECK_EQ(r.f64(12), b.x_max);
                CHECK_EQ(r.f64(13), b.y_max);
                // Asked twice: the remembered answer is the computed one.
                CHECK(font.bounds(glyph) == b);
                ++glyph_records;
            } else if (r.is("KERNSUBTABLES")) {
                CHECK_EQ(static_cast<size_t>(c.i64()), font.kern_subtable_count());
            } else if (r.is("KSUB")) {
                const size_t index = static_cast<size_t>(c.i64());
                CHECK_EQ(c.i32(), font.kern_subtable_format(index));
            } else if (r.is("KPAIR")) {
                // Subtable 0 is searched first, so a pair it lists is the answer.
                if (c.i64() == 0) {
                    const uint32_t left = c.u32(), right = c.u32();
                    CHECK_EQ(c.i32(), font.pair_kern(left, right));
                    ++kern_answers;
                }
            } else if (r.is("KERNAXIS")) {
                for (int64_t n = c.i64(); n > 0; --n)
                    axis.push_back(c.u32());
            } else if (r.is("KERNAXISGID")) {
                for (int64_t n = c.i64(); n > 0; --n)
                    axis_glyphs.push_back(c.u32());
            } else if (r.is("KERNROW")) {
                REQUIRE_EQ(axis.size(), axis_glyphs.size());
                REQUIRE_EQ(r.size(), axis.size() + 1);
                const uint32_t left = c.u32();
                uint32_t left_glyph = 0;
                font.lookup(left, &left_glyph);
                for (size_t i = 0; i < axis.size(); ++i) {
                    CHECK_CONTEXT("pair U+%04X U+%04X", left, axis[i]);
                    CHECK_EQ(c.i32(), font.pair_kern(left_glyph, axis_glyphs[i]));
                    ++kern_answers;
                }
            } else if (r.is("KERNGID") || r.is("KERNFALL")) {
                if (r.is("KERNFALL")) {
                    c.i64();
                    c.i64();
                }
                const uint32_t left = c.u32(), right = c.u32();
                CHECK_EQ(c.i32(), font.pair_kern(left, right));
                ++kern_answers;
            }
        }
        CHECK_EQ(static_cast<uint32_t>(glyph_records), font.glyph_count());
        CHECK(cmap_records > 800);
        CHECK(kern_answers > 30000);
        // Nothing a bundled font holds is beyond the reader.
        CHECK(font.warnings().empty());

        // Out of range is zero, not undefined.
        const uint32_t past = font.glyph_count();
        CHECK_EQ(font.advance(past), 0);
        CHECK(font.bounds(past) == GlyphBounds{});
        CHECK(font.bounds(0xFFFFFFFFu) == GlyphBounds{});
        CHECK_EQ(font.pair_kern(past, 0xFFFFFFFFu), 0);
        CHECK(!font.lookup(0x7FFFFFFFu, nullptr));
    }
}

TEST(the_math_table_matches_its_golden)
{
    Loaded f;
    load("latinmodern-math.otf", f);
    golden::File file;
    REQUIRE(f.ok);
    REQUIRE(file.load(golden::path("font/latinmodern-math.otf.math.golden")));
    const OpenTypeFont& font = f.font;
    REQUIRE(font.has_math());
    int italics = 0, accents = 0, constants = 0, vertical = 0, horizontal = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("MATHCONST")) {
            const int index = c.i32();
            CHECK_EQ(c.str(), std::string(math_constant_name(index)));
            CHECK_EQ(c.i32(), font.math_constant(index));
            ++constants;
        } else if (r.is("ITALIC")) {
            const uint32_t glyph = c.u32();
            CHECK_EQ(c.i32(), font.math_italic_correction(glyph));
            ++italics;
        } else if (r.is("TOPACCENT")) {
            const uint32_t glyph = c.u32();
            int value = 0;
            CHECK(font.math_top_accent(glyph, &value));
            CHECK_EQ(c.i32(), value);
            ++accents;
        } else if (r.is("MINCONNECTOROVERLAP")) {
            CHECK_EQ(c.i32(), font.math_min_connector_overlap());
        } else if (r.is("VVARIANTS") || r.is("HVARIANTS")) {
            const bool is_vertical = r.is("VVARIANTS");
            const uint32_t base = c.u32();
            const MathVariantList list = is_vertical ? font.math_vertical_variants(base)
                                                     : font.math_horizontal_variants(base);
            REQUIRE_EQ(static_cast<size_t>(c.i64()), list.size);
            for (const MathVariantRecord& record : list) {
                CHECK_EQ(c.u32(), static_cast<uint32_t>(record.glyph));
                CHECK_EQ(c.u32(), static_cast<uint32_t>(record.advance));
            }
            // A construction that is an assembly alone has no variant list,
            // and the glyph is then not among the bases.
            if (!list.empty())
                ++(is_vertical ? vertical : horizontal);
        }
    }
    CHECK_EQ(constants, MATH_CONSTANT_COUNT);
    CHECK_EQ(italics, 1002);
    CHECK_EQ(accents, 2475);
    CHECK_EQ(vertical, 94);
    CHECK_EQ(horizontal, 82);
    CHECK_EQ(font.math_vertical_bases().size(), size_t(94));
    CHECK_EQ(font.math_horizontal_bases().size(), size_t(82));
    // A glyph with no entry.
    CHECK_EQ(font.math_italic_correction(0), 0);
    CHECK(!font.math_top_accent(0, nullptr));
    CHECK(font.math_vertical_variants(0).empty());
    CHECK(font.warnings().empty());
}

TEST(a_font_without_the_table_has_no_math_and_no_names)
{
    Loaded f;
    load("Roboto-Regular.ttf", f);
    REQUIRE(f.ok);
    CHECK(!f.font.has_math());
    CHECK_EQ(f.font.math_constant(AxisHeight), 0);
    CHECK(f.font.math_vertical_bases().empty());
    CHECK(f.font.glyph_name(36).empty());
}

TEST(what_is_not_a_usable_font_is_refused_with_a_reason)
{
    OpenTypeFont font;
    std::string why;
    CHECK(!font.load(nullptr, 0, &why));
    CHECK(!why.empty());

    const uint8_t text[] = "This is not a font file at all, it is a sentence.";
    why.clear();
    CHECK(!font.load(text, sizeof text, &why));
    CHECK(!why.empty());
    CHECK(!font.loaded());

    const uint8_t collection[] = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 16};
    why.clear();
    CHECK(!font.load(collection, sizeof collection, &why));
    CHECK(why.find("collection") != std::string::npos);

    // An empty object answers zero to everything.
    CHECK_EQ(font.glyph_count(), 0u);
    CHECK_EQ(font.advance(0), 0);
    CHECK(font.bounds(0) == GlyphBounds{});
    CHECK_EQ(font.pair_kern(1, 2), 0);
    CHECK(!font.lookup(0x41, nullptr));
    CHECK(font.glyph_name(0).empty());
}

namespace
{

// Every query a caller can make, over a font that may be damaged. The answers
// are not checked; reaching the end without a sanitiser report is the test.
double touch_everything(const OpenTypeFont& font)
{
    double sum = 0;
    const uint32_t glyphs = font.glyph_count();
    for (uint32_t g = 0; g < glyphs; ++g) {
        const GlyphBounds b = font.bounds(g);
        sum += b.x_min + b.y_max + font.advance(g) + font.left_side_bearing(g);
        sum += font.math_italic_correction(g);
        sum += static_cast<double>(font.glyph_name(g).size());
        for (const MathVariantRecord& record : font.math_vertical_variants(g))
            sum += record.glyph + record.advance;
        for (const MathVariantRecord& record : font.math_horizontal_variants(g))
            sum += record.glyph + record.advance;
        sum += font.pair_kern(g, (g * 7 + 3) % (glyphs + 1));
    }
    for (uint32_t cp = 0; cp < 0x3000; ++cp) {
        uint32_t glyph = 0;
        if (font.lookup(cp, &glyph))
            sum += glyph;
    }
    for (int i = 0; i < MATH_CONSTANT_COUNT; ++i)
        sum += font.math_constant(i);
    return sum;
}

struct Lcg {
    uint32_t state;
    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
};

} // namespace

TEST(a_truncated_font_fails_to_load_or_answers_without_reading_past_its_end)
{
    for (const std::string& name : font_names()) {
        CHECK_CONTEXT("%s", name.c_str());
        std::vector<uint8_t> bytes;
        REQUIRE(golden::read_file(golden::data_path("fonts/" + name), bytes));
        int loaded = 0, refused = 0;
        // Cut at a spread of lengths, the first few hundred bytes densely: that
        // is where the directory and the small tables are.
        std::vector<size_t> lengths;
        for (size_t n = 0; n < 600 && n < bytes.size(); n += 7)
            lengths.push_back(n);
        for (size_t n = 600; n < bytes.size(); n += bytes.size() / 61 + 1)
            lengths.push_back(n);
        for (const size_t length : lengths) {
            // An exact-size copy, so that a read past the cut is a read past
            // an allocation and the sanitiser sees it.
            const std::vector<uint8_t> cut(bytes.begin(),
                                           bytes.begin() + static_cast<long>(length));
            OpenTypeFont font;
            std::string why;
            if (font.load(cut.data(), cut.size(), &why)) {
                ++loaded;
                touch_everything(font);
            } else {
                ++refused;
                CHECK(!why.empty());
            }
        }
        CHECK(refused > 0);
        CHECK_EQ(static_cast<size_t>(loaded + refused), lengths.size());
    }
}

TEST(a_font_with_flipped_bytes_fails_to_load_or_answers_without_reading_past_its_end)
{
    for (const std::string& name : font_names()) {
        CHECK_CONTEXT("%s", name.c_str());
        std::vector<uint8_t> original;
        REQUIRE(golden::read_file(golden::data_path("fonts/" + name), original));
        Lcg rng{static_cast<uint32_t>(original.size())};
        int loaded = 0;
        for (int mutant = 0; mutant < 60; ++mutant) {
            std::vector<uint8_t> bytes = original;
            // Half the mutants are damaged in the first 4 KiB, where the
            // directory and the tables every font needs tend to be.
            const size_t span =
                mutant % 2 == 0 ? std::min<size_t>(bytes.size(), 4096) : bytes.size();
            const int flips = 1 + static_cast<int>(rng.next() % 24);
            for (int i = 0; i < flips; ++i)
                bytes[rng.next() % span] = static_cast<uint8_t>(rng.next());
            OpenTypeFont font;
            if (font.load(bytes.data(), bytes.size())) {
                ++loaded;
                touch_everything(font);
            }
        }
        // Most damage lands in outlines and is survived.
        CHECK(loaded > 0);
    }
}
