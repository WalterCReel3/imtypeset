// src/fontlib against tests/golden/font (tools/oracle/FORMAT-font.md): the
// registry, the scaled metrics every FontMetrics method returns at the dumped
// sizes, the rounded triples layout consumes, the minted codepoints, and the
// reference's failures. Every double is compared exactly.

#include "golden.h"

#include "fontlib.h"
#include "units.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

std::string font_path(const std::string& name)
{
    return golden::data_path("fonts/" + name);
}

Face face_of(const std::string& value)
{
    Face face = Face::PROSE;
    if (!face_from_value(value, &face))
        FAIL("'%s' is not a role", value.c_str());
    return face;
}

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
        if (!library.register_font(entry.face, font_path(entry.file).c_str(), &why))
            FAIL("%s: %s", entry.file, why.c_str());
    }
}

double math_param(const MathParams& p, const std::string& name)
{
    static const std::map<std::string, double MathParams::*> fields = {
        {"math_x_height", &MathParams::math_x_height},
        {"math_quad", &MathParams::math_quad},
        {"num1", &MathParams::num1},
        {"num2", &MathParams::num2},
        {"num3", &MathParams::num3},
        {"denom1", &MathParams::denom1},
        {"denom2", &MathParams::denom2},
        {"sup1", &MathParams::sup1},
        {"sup2", &MathParams::sup2},
        {"sup3", &MathParams::sup3},
        {"sub1", &MathParams::sub1},
        {"sub2", &MathParams::sub2},
        {"sup_drop", &MathParams::sup_drop},
        {"sub_drop", &MathParams::sub_drop},
        {"delim1", &MathParams::delim1},
        {"delim2", &MathParams::delim2},
        {"axis_height", &MathParams::axis_height},
        {"default_rule_thickness", &MathParams::default_rule_thickness},
        {"big_op_spacing1", &MathParams::big_op_spacing1},
        {"big_op_spacing2", &MathParams::big_op_spacing2},
        {"big_op_spacing3", &MathParams::big_op_spacing3},
        {"big_op_spacing4", &MathParams::big_op_spacing4},
        {"big_op_spacing5", &MathParams::big_op_spacing5},
        {"radical_rule_thickness", &MathParams::radical_rule_thickness},
        {"radical_vertical_gap", &MathParams::radical_vertical_gap},
        {"radical_display_style_vertical_gap", &MathParams::radical_display_style_vertical_gap},
        {"script_scale", &MathParams::script_scale},
        {"script_script_scale", &MathParams::script_script_scale},
        {"display_operator_min_height", &MathParams::display_operator_min_height},
    };
    const auto it = fields.find(name);
    if (it == fields.end()) {
        FAIL("no MathParams field named '%s'", name.c_str());
        return 0.0;
    }
    return p.*(it->second);
}

// One `<role>.answers.golden` replayed against a library already set up.
void replay_answers(FontLibrary& library, const std::string& golden_name)
{
    CHECK_CONTEXT("%s", golden_name.c_str());
    golden::File file;
    if (!file.load(golden::path("font/" + golden_name)))
        return;
    Face face = Face::PROSE;
    const FaceData* data = nullptr;
    const FontMetrics* m = nullptr;
    const MathFontMetrics* math = nullptr;
    bool is_math = false;
    int sizes = 0, characters = 0, kerns = 0, params = 0, chains = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("FACE")) {
            face = face_of(c.str());
            data = library.face_data(face);
            REQUIRE(data != nullptr);
            CHECK_EQ(c.str(), data->name());
            CHECK_EQ(c.str(), std::string(face_value(library.backing_face(face))));
            is_math = c.boolean();
            CHECK_EQ(is_math, data->font().has_math());
        } else if (r.is("GN")) {
            REQUIRE(data != nullptr);
            const uint32_t codepoint = c.u32();
            CHECK_EQ(c.boolean(), data->has(codepoint));
            CHECK_EQ(c.u32(), data->glyph(codepoint));
            const std::string name = c.str();
            if (data->font().is_cff())
                CHECK_EQ(name, std::string(data->glyph_name(codepoint)));
        } else if (r.is("SIZE")) {
            const double size = c.f64();
            std::string why;
            m = library.metrics(face, size, &why);
            if (m == nullptr) {
                FAIL("no metrics: %s", why.c_str());
                return;
            }
            // Identical arguments, identical object.
            CHECK(m == library.metrics(face, size));
            math = m->as_math();
            CHECK_EQ(math != nullptr, face == Face::MATH);
            CHECK(m->face() == face);
            ++sizes;
        } else if (r.is("FM")) {
            REQUIRE(m != nullptr);
            CHECK_EQ(c.f64(), m->ascent());
            CHECK_EQ(c.f64(), m->descent());
            CHECK_EQ(c.f64(), m->x_height());
            CHECK_EQ(c.f64(), m->em());
            CHECK_EQ(c.f64(), m->space_width());
            CHECK_EQ(c.f64(), m->size());
        } else if (r.is("CH")) {
            REQUIRE(m != nullptr);
            const uint32_t codepoint = c.u32();
            CHECK_CONTEXT("U+%04X", codepoint);
            CHECK_EQ(c.boolean(), m->has(codepoint));
            CHECK_EQ(c.f64(), m->advance(codepoint));
            const GlyphBBox box = m->bbox(codepoint);
            CHECK_EQ(c.f64(), box.x_min);
            CHECK_EQ(c.f64(), box.y_min);
            CHECK_EQ(c.f64(), box.x_max);
            CHECK_EQ(c.f64(), box.y_max);
            ++characters;
        } else if (r.is("K")) {
            REQUIRE(m != nullptr);
            const uint32_t left = c.u32(), right = c.u32();
            CHECK_CONTEXT("U+%04X U+%04X", left, right);
            CHECK_EQ(c.f64(), m->kern(left, right));
            ++kerns;
        } else if (r.is("MP")) {
            REQUIRE(math != nullptr);
            const std::string name = c.str();
            CHECK_CONTEXT("%s", name.c_str());
            CHECK_EQ(c.f64(), math_param(math->math(), name));
            ++params;
        } else if (r.is("IC") || r.is("IC0")) {
            REQUIRE(math != nullptr);
            const uint32_t codepoint = c.u32();
            CHECK_EQ(c.f64(), math->italic_correction(codepoint));
        } else if (r.is("VV")) {
            REQUIRE(math != nullptr);
            const uint32_t codepoint = c.u32();
            CHECK_CONTEXT("U+%04X", codepoint);
            const GlyphVariants chain = math->vertical_variants(codepoint);
            REQUIRE_EQ(static_cast<size_t>(c.i64()), chain.size);
            for (const GlyphVariant& variant : chain) {
                CHECK_EQ(c.u32(), variant.codepoint);
                CHECK_EQ(c.u32(), variant.glyph);
                CHECK_EQ(c.f64(), variant.extent);
            }
            ++chains;
        } else if (r.is("NOVV")) {
            REQUIRE(math != nullptr);
            CHECK(math->vertical_variants(c.u32()).empty());
        }
    }
    CHECK_EQ(sizes, 7);
    CHECK(characters > 2000);
    CHECK(kerns > 1000);
    if (is_math && face == Face::MATH) {
        CHECK_EQ(params, 7 * 29);
        CHECK(chains > 600);
    }
}

} // namespace

TEST(every_role_answers_as_the_reference_does_at_every_dumped_size)
{
    FontLibrary library;
    register_defaults(library);
    for (int i = 0; i < FACE_COUNT; ++i)
        replay_answers(library, std::string(face_value(static_cast<Face>(i))) + ".answers.golden");
}

TEST(a_registered_file_answers_as_the_reference_does)
{
    FontLibrary library;
    register_defaults(library);
    std::string why;
    REQUIRE(library.register_font(Face::CODE_BOLD, font_path("DroidSans.ttf").c_str(), &why));
    replay_answers(library, "registered.DroidSans.ttf.answers.golden");
}

TEST(char_measure_triples_round_as_the_reference_rounds_them)
{
    FontLibrary library;
    register_defaults(library);
    int files = 0;
    for (const std::string& path : golden::list("font", ".charmeasure.golden")) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!file.load(path))
            continue;
        ++files;
        Face face = Face::PROSE;
        const FontMetrics* m = nullptr;
        int triples = 0;
        for (const golden::Record& r : file) {
            CHECK_CONTEXT("line %d", r.line);
            golden::Cursor c(r);
            if (r.is("FACE")) {
                face = face_of(c.str());
            } else if (r.is("CMSIZE")) {
                // The em size is in scaled points: what layout builds a face at.
                m = library.metrics(face, static_cast<double>(c.i64()));
                REQUIRE(m != nullptr);
            } else if (r.is("CM")) {
                REQUIRE(m != nullptr);
                const uint32_t codepoint = c.u32();
                CHECK_CONTEXT("U+%04X", codepoint);
                // advtext.style.char_measure: round() of the advance and of the
                // ink box's height and depth, half to even.
                const GlyphBBox box = m->bbox(codepoint);
                CHECK_EQ(c.i64(), round_to_scaled(m->advance(codepoint)));
                CHECK_EQ(c.i64(), round_to_scaled(box.height()));
                CHECK_EQ(c.i64(), round_to_scaled(box.depth()));
                ++triples;
            }
        }
        CHECK(triples > 5000);
    }
    CHECK_EQ(files, 6);
}

TEST(synthetic_codepoints_are_minted_in_glyph_name_order)
{
    FontLibrary library;
    register_defaults(library);
    const FaceData* data = library.face_data(Face::MATH);
    REQUIRE(data != nullptr);
    golden::File file;
    REQUIRE(file.load(golden::path("font/latinmodern-math.otf.math.golden")));
    const std::vector<SyntheticCodepoint>& minted = data->synthetic_codepoints();
    size_t next = 0;
    int variant_codepoints = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("SYNRANGE")) {
            CHECK_EQ(c.u32(), SYNTHETIC_FIRST);
            CHECK_EQ(c.u32(), SYNTHETIC_LAST);
        } else if (r.is("SYNCOUNT")) {
            CHECK_EQ(static_cast<size_t>(c.i64()), minted.size());
        } else if (r.is("SYN")) {
            REQUIRE(next < minted.size());
            const uint32_t codepoint = c.u32(), glyph = c.u32();
            CHECK_EQ(codepoint, minted[next].codepoint);
            CHECK_EQ(glyph, minted[next].glyph);
            CHECK_EQ(c.str(), std::string(data->glyph_name(codepoint)));
            // A minted codepoint resolves like any other, and the font's own
            // cmap knows nothing of it.
            CHECK(data->has(codepoint));
            CHECK_EQ(data->glyph(codepoint), glyph);
            CHECK(!data->font().lookup(codepoint, nullptr));
            ++next;
        } else if (r.is("VARIANTCP")) {
            const uint32_t glyph = c.u32();
            CHECK_EQ(c.u32(), data->variant_codepoint(glyph));
            ++variant_codepoints;
        }
    }
    CHECK_EQ(next, minted.size());
    CHECK_EQ(next, size_t(227));
    CHECK_EQ(variant_codepoints, 325);
    // Just outside what was minted, and a glyph no chain reaches.
    CHECK(!data->has(SYNTHETIC_FIRST + 1));
    CHECK(!data->has(SYNTHETIC_FIRST - 227));
    CHECK(!data->has(SYNTHETIC_LAST));
    CHECK_EQ(data->variant_codepoint(0), 0u);
    // No other bundled file mints anything.
    for (int i = 0; i < FACE_COUNT; ++i) {
        const Face face = static_cast<Face>(i);
        if (face != Face::MATH)
            CHECK(library.face_data(face)->synthetic_codepoints().empty());
    }
}

TEST(the_registry_matches_the_reference_through_registration_and_back)
{
    FontLibrary library;
    register_defaults(library);
    golden::File file;
    REQUIRE(file.load(golden::path("font/registry.golden")));
    int roles = 0, em_sizes = 0, states = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("STATE")) {
            const std::string label = c.str();
            const uint32_t before = library.generation();
            if (label == "registered code-bold DroidSans.ttf") {
                CHECK(library.register_font(Face::CODE_BOLD, font_path("DroidSans.ttf").c_str()));
                CHECK(library.generation() != before);
            } else if (label == "default again") {
                library.unregister_font(Face::CODE_BOLD);
                CHECK(library.generation() != before);
            }
            ++states;
        } else if (r.is("ROLE")) {
            const Face face = face_of(c.str());
            const FaceData* data = library.face_data(face);
            REQUIRE(data != nullptr);
            CHECK_EQ(c.str(), data->name());
            CHECK_EQ(c.str(), std::string(face_value(library.backing_face(face))));
            CHECK_EQ(static_cast<size_t>(c.i64()), data->byte_count());
            c.raw(); // sha256 of the file
            CHECK_EQ(static_cast<size_t>(c.i64()), data->synthetic_codepoints().size());
            ++roles;
        } else if (r.is("EMSIZE")) {
            const Face face = face_of(c.str());
            const double pixel_height = c.f64();
            CHECK_EQ(c.f64(), library.em_size_for_pixel_height(face, pixel_height));
            ++em_sizes;
        }
    }
    CHECK_EQ(states, 3);
    CHECK_EQ(roles, 3 * FACE_COUNT);
    CHECK_EQ(em_sizes, 3 * FACE_COUNT * 12);
}

TEST(the_failures_the_reference_raises_are_reported_with_its_reasons)
{
    golden::File file;
    REQUIRE(file.load(golden::path("font/registry.golden")));
    std::map<std::string, std::pair<std::string, std::string>> errors;
    for (const golden::Record& r : file)
        if (r.is("ERR"))
            errors[std::string(r.raw(0))] = {std::string(r.raw(1)), r.str(2)};

    FontLibrary library;
    register_defaults(library);
    std::string why;

    // A file that is not there.
    REQUIRE_EQ(errors["register_a_file_that_does_not_exist"].first,
               std::string("MissingFontError"));
    uint32_t before = library.generation();
    CHECK(!library.register_font(Face::CODE_BOLD, font_path("no-such-font.ttf").c_str(), &why));
    CHECK(!why.empty());
    CHECK_EQ(library.generation(), before);
    CHECK(library.backing_face(Face::CODE_BOLD) == Face::PROSE_BOLD);

    // Withdrawing a role that has nothing registered touches nothing.
    REQUIRE_EQ(errors["unregister_a_role_with_nothing_registered"].first, std::string("ok"));
    library.unregister_font(Face::CODE_BOLD);
    CHECK_EQ(library.generation(), before);

    // The math role over a file with no MATH table: refused when asked for,
    // with the reference's message.
    REQUIRE_EQ(errors["math_role_registered_to_a_file_without_MATH"].first,
               std::string("MissingFontError"));
    REQUIRE(library.register_font(Face::MATH, font_path("Roboto-Regular.ttf").c_str(), &why));
    why.clear();
    CHECK(library.math_metrics(10.0, &why) == nullptr);
    CHECK_EQ(why, errors["math_role_registered_to_a_file_without_MATH"].second);
    CHECK(library.metrics(Face::MATH, 10.0) == nullptr);
    CHECK_EQ(errors["synthetic_codepoints_of_that_role"].second, std::string("0"));
    CHECK(library.face_data(Face::MATH)->synthetic_codepoints().empty());

    // A role nothing backs.
    FontLibrary empty;
    why.clear();
    CHECK(empty.metrics(Face::PROSE, 10.0, &why) == nullptr);
    CHECK(!why.empty());
    CHECK(empty.face_data(Face::CODE_BOLD) == nullptr);
    CHECK_EQ(empty.em_size_for_pixel_height(Face::PROSE, 16.0), 16.0);

    // Bytes that are not a font: refused at registration, registry unchanged.
    const char junk[] = "not a font";
    before = library.generation();
    why.clear();
    CHECK(!library.register_font(Face::CODE, junk, sizeof junk, "junk.ttf", &why));
    CHECK(!why.empty());
    CHECK_EQ(library.generation(), before);
    CHECK_EQ(library.face_data(Face::CODE)->name(), std::string("Inconsolata-Medium.ttf"));
}

TEST(a_missing_x_height_falls_back_to_a_fraction_of_the_em)
{
    golden::File file;
    REQUIRE(file.load(golden::path("font/registry.golden")));
    int checked = 0;
    for (const golden::Record& r : file) {
        if (!r.is("XHEIGHTFALLBACK"))
            continue;
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        const std::string name = c.str();
        const size_t offset = static_cast<size_t>(c.i64());
        const int in_file = c.i32();
        c.i64(); // unitsPerEm
        const int fallback = c.i32();
        const double at_16 = c.f64();

        std::vector<uint8_t> bytes;
        REQUIRE(golden::read_file(font_path(name), bytes));
        REQUIRE(offset + 2 <= bytes.size());
        // The record says where OS/2.sxHeight is; zero it, as the oracle did.
        CHECK_EQ(in_file,
                 static_cast<int>(static_cast<int16_t>((bytes[offset] << 8) | bytes[offset + 1])));
        bytes[offset] = bytes[offset + 1] = 0;
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::PROSE, bytes.data(), bytes.size(), name.c_str(), &why));
        CHECK_EQ(library.face_data(Face::PROSE)->x_height(), fallback);
        CHECK_EQ(library.metrics(Face::PROSE, 16.0)->x_height(), at_16);
        ++checked;
    }
    CHECK_EQ(checked, 3);
}

TEST(what_was_handed_out_survives_a_registry_change)
{
    FontLibrary library;
    register_defaults(library);
    const FontMetrics* before = library.metrics(Face::CODE_BOLD, 16.0);
    const FaceData* data_before = library.face_data(Face::PROSE_BOLD);
    REQUIRE(before != nullptr);
    const double advance = before->advance('A');

    // CODE_BOLD was Roboto Bold through its stand-in; now both roles change.
    REQUIRE(library.register_font(Face::CODE_BOLD, font_path("DroidSans.ttf").c_str()));
    REQUIRE(library.register_font(Face::PROSE_BOLD, font_path("Roboto-Regular.ttf").c_str()));
    const FontMetrics* after = library.metrics(Face::CODE_BOLD, 16.0);
    REQUIRE(after != nullptr);
    CHECK(after != before);
    // The old objects still answer, from the old file.
    CHECK_EQ(before->advance('A'), advance);
    CHECK_EQ(data_before->name(), std::string("Roboto-Bold.ttf"));
    CHECK_EQ(library.face_data(Face::PROSE_BOLD)->name(), std::string("Roboto-Regular.ttf"));
    CHECK_EQ(library.face_data(Face::CODE_BOLD)->name(), std::string("DroidSans.ttf"));
}

TEST(a_sixteen_bit_atlas_gets_a_stand_in_for_every_supplementary_codepoint)
{
    FontLibrary library;
    register_defaults(library);
    const FaceData* data = library.face_data(Face::MATH);
    REQUIRE(data != nullptr);

    // Every supplementary codepoint the face can be asked for.
    std::vector<uint32_t> supplementary;
    for (const CmapRange& range : data->font().cmap())
        for (uint32_t cp = range.first; cp <= range.last; ++cp)
            if (cp > 0xFFFF)
                supplementary.push_back(cp);
    for (const SyntheticCodepoint& minted : data->synthetic_codepoints())
        supplementary.push_back(minted.codepoint);
    CHECK(supplementary.size() > 227);
    CHECK_EQ(data->atlas_stand_ins().size(), supplementary.size());

    std::set<uint32_t> used;
    for (const uint32_t codepoint : supplementary) {
        CHECK_CONTEXT("U+%05X", codepoint);
        // A 32-bit ImWchar names it directly.
        CHECK_EQ(data->atlas_codepoint(codepoint, 32), codepoint);
        uint32_t glyph = 0;
        CHECK(data->glyph_for_atlas_codepoint(codepoint, 32, &glyph));
        CHECK_EQ(glyph, data->glyph(codepoint));
        // A 16-bit one gets a private-use stand-in the font itself leaves
        // free, and it leads back to the same glyph.
        const uint32_t stand_in = data->atlas_codepoint(codepoint, 16);
        CHECK(stand_in >= 0xE000 && stand_in <= 0xF8FF);
        CHECK(!data->font().lookup(stand_in, nullptr));
        CHECK(used.insert(stand_in).second);
        glyph = 0;
        CHECK(data->glyph_for_atlas_codepoint(stand_in, 16, &glyph));
        CHECK_EQ(glyph, data->glyph(codepoint));
    }
    // The BMP is itself at either width.
    CHECK_EQ(data->atlas_codepoint(0x2211, 16), 0x2211u);
    uint32_t glyph = 0;
    CHECK(data->glyph_for_atlas_codepoint(0x2211, 16, &glyph));
    CHECK_EQ(glyph, data->glyph(0x2211));
    // A 16-bit atlas cannot be asked for a supplementary codepoint directly,
    // and a supplementary codepoint the face lacks has no stand-in.
    CHECK(!data->glyph_for_atlas_codepoint(0x1D44E, 16, nullptr));
    CHECK_EQ(data->atlas_codepoint(0x1F600, 16), 0u);
    // A stand-in is a codepoint the face does not map. As a layout codepoint
    // it is missing, though a 16-bit atlas now holds a glyph there.
    const uint32_t first_stand_in = data->atlas_stand_ins().front().first;
    CHECK(!data->has(first_stand_in));
    CHECK_EQ(data->atlas_codepoint(first_stand_in, 16), 0u);
    CHECK_EQ(data->atlas_codepoint(first_stand_in, 32), first_stand_in);
    // A face with nothing above the BMP needs none.
    CHECK(library.face_data(Face::PROSE)->atlas_stand_ins().empty());
}

// A host that animates its zoom asks for a new size every frame. The cache
// keeps the sizes most recently asked for live; the rest expire, and are
// freed when the library's owner says it holds no pointer to them. Sanitiser:
// a pointer used after it was freed fails only there.
TEST(the_metrics_cache_is_bounded_and_no_pointer_dies_before_it_is_released)
{
    FontLibrary library;
    register_defaults(library);
    for (const Face face : {Face::PROSE, Face::MATH}) {
        CHECK_CONTEXT("%s", face_value(face));
        const double base = 9.0;
        const FontMetrics* first = library.metrics(face, base);
        REQUIRE(first != nullptr);
        const double first_advance = first->advance('x');

        // While a size is live, the same arguments give the same object, and
        // asking renews it.
        for (size_t i = 1; i <= METRICS_SIZES_KEPT; ++i)
            REQUIRE(library.metrics(face, base + static_cast<double>(i) / 1024.0) != nullptr);
        CHECK(library.metrics(face, base) == first);
        CHECK_EQ(library.metrics_expired(), size_t(0));

        // A holder that never asks again and an owner that never releases:
        // the size expires, and the pointer is as good as it was. This is how
        // every caller behaved before the cache was bounded.
        for (size_t i = 1; i <= 3 * METRICS_SIZES_KEPT; ++i)
            REQUIRE(library.metrics(face, base + 1.0 + static_cast<double>(i) / 1024.0) != nullptr);
        CHECK(library.metrics_expired() > 0);
        CHECK_EQ(first->advance('x'), first_advance);
        CHECK_EQ(first->size(), base);
        // Asked for again it is a new object that answers the same.
        const FontMetrics* second = library.metrics(face, base);
        REQUIRE(second != nullptr);
        CHECK(second != first);
        CHECK_EQ(second->advance('x'), first_advance);

        // An owner that releases between frames, which is what a context
        // does: 100,000 sizes, a pointer held within each frame only, and
        // never more than twice the bound live or one generation expired.
        // (The math face builds its variant chains per size, which under the
        // sanitisers costs too much to do 100,000 times; it gets 5,000.)
        library.release_expired_metrics();
        CHECK_EQ(library.metrics_expired(), size_t(0));
        const int sizes = face == Face::MATH ? 5000 : 100000;
        size_t most_live = 0, most_expired = 0, releases = 0;
        for (int i = 0; i < sizes; ++i) {
            if (library.metrics_expired() != 0) {
                most_expired = std::max(most_expired, library.metrics_expired());
                library.release_expired_metrics();
                ++releases;
            }
            const double size = 4.0 + i * (1.0 / 4096.0);
            const FontMetrics* m = library.metrics(face, size);
            REQUIRE(m != nullptr);
            // The frame's own use of what it asked for, after other sizes
            // were asked for in the same frame.
            const FontMetrics* other = library.metrics(face, size + 0.5);
            REQUIRE(other != nullptr);
            if (i % 512 == 0) {
                CHECK_EQ(m->size(), size);
                CHECK_EQ(other->size(), size + 0.5);
            }
            most_live = std::max(most_live, library.metrics_cached(face));
        }
        CHECK(most_live <= 2 * METRICS_SIZES_KEPT);
        CHECK(most_expired <= METRICS_SIZES_KEPT);
        CHECK(releases > 4);
        CHECK(library.metrics_cached(face) >= METRICS_SIZES_KEPT);
        if (face == Face::MATH) {
            const FontMetrics* again = library.metrics(face, base);
            REQUIRE(again != nullptr && again->as_math() != nullptr);
            CHECK(!again->as_math()->vertical_variants('(').empty());
        }
    }
    // The other roles were not touched by all that.
    CHECK_EQ(library.metrics_cached(Face::CODE), size_t(0));

    // A registry change frees nothing: a pointer held across it still answers.
    const FontMetrics* before = library.metrics(Face::PROSE, 16.0);
    const double advance = before->advance('x');
    REQUIRE(library.register_font(Face::PROSE, font_path("DroidSans.ttf").c_str()));
    CHECK_EQ(library.metrics_cached(Face::PROSE), size_t(0));
    CHECK_EQ(before->advance('x'), advance);
    CHECK(library.metrics(Face::PROSE, 16.0) != before);
}
