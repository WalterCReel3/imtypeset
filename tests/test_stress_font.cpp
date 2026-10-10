// Hostile fonts, taken all the way: registry -> FaceData (minting, stand-ins)
// -> metric, kern, ink box and math variant queries -> the strict pass ->
// build_atlas_font -> add_face_to_atlas into a real ImFontAtlas -> a baked
// glyph through ImGui's rasteriser. A font is either refused with a reason or
// everything it answers is in range and rasterising it is clean under the
// sanitisers. That last clause is checked by ASan and UBSan, not by a CHECK:
// the tests that say "sanitiser" in a comment can only fail on memory there,
// though each still asserts what can be asserted (a reason on refusal, values
// in range, counts).
//
// The first half is one structured case per defect (the names say which); the
// second is a mutation fuzz of every bundled font with a fixed-seed generator,
// so a run is the same run everywhere.
//
// The formats past the bundled fonts' (collections, CID-keyed CFF, the Type 2
// arithmetic operators, the 'kern' table, post names, cmap formats 0, 6, 10
// and 13 and the Symbol and Macintosh encodings) have structured cases of
// their own, seeds of their own in the dense fuzz, with mutations aimed at
// their structures (mutate_aimed), and the tests named n1 to n5, which
// measure what the largest thing each can be made to describe costs. A
// collection goes through the chain once per face, and once as the face past
// its last.
//
// IMTYPESET_STRESS_ITERS is a multiplier on the number of random mutants per
// font (default 1; 10 is the long run). The structured cases do not scale.
// IMTYPESET_STRESS_SEED_TIMES=1 prints what each seed of the dense fuzz took.
// IMTYPESET_STRESS_DUMP names a file that each font is written to just before
// it is handed to the atlas, so that after a crash or an assertion in a
// rasteriser the file is the font that did it.
//
// TWO THINGS THIS DOES NOT HOLD. Both end in the same ImGui assertion, "Out
// of texture memory", where ImGui's assertions are compiled in, and in a
// glyph that is not drawn where they are not; neither touches memory it
// should not.
//
// One is ImGui's, under either rasteriser, and is described where the tests'
// atlas is made: a glyph wider than the atlas texture can fail to pack. The
// tests' atlas starts wide enough for anything the strict pass lets through
// at the size they bake at.
//
// The other is under FreeType: a TrueType font's hinting program is code
// FreeType runs, and a mutant's can move a glyph's points anywhere. The strict
// pass bounds the outline, not what the program does to it, so such a glyph
// can come out thousands of pixels across, which ImGui cannot pack. The
// default run and the 10x run do not meet one; a run over every installed
// font with mutants does (a mutated fpgm). IMTYPESET_STRESS_NO_HINTING=1 adds
// ImGuiFreeTypeLoaderFlags_NoHinting to every font added, which is what a
// host that takes fonts from strangers should do, and with it that run is
// clean.

#include "font_builders.h"
#include "golden.h"

#include "font_loader.h"
#include "fontlib.h"
#include "imtypeset.h"
#include "opentype.h"
#include "units.h"

#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <sys/resource.h>

using namespace ImTypeset::detail;

using namespace font_builders;

namespace
{

// --- The whole chain ------------------------------------------------------------------

struct Context {
    Context()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        // Wider than any glyph the strict pass lets through at the 16 px
        // these tests bake at (32 heights across, 512 px). ImGui 1.92.9 can
        // fail to pack a glyph wider than its texture: when the atlas makes
        // room by repacking rather than growing, ImFontAtlasPackInit forgets
        // the size of the rectangle being added and the texture then grows
        // in height only, until the attempts run out and the loader asserts
        // "Out of texture memory". The atlas starts 512 wide.
        io.Fonts->TexMinWidth = 1024;
    }
    ~Context() { ImGui::DestroyContext(); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    ImFontAtlas* atlas() const { return ImGui::GetIO().Fonts; }
    bool freetype() const
    {
        const char* name = atlas()->FontLoaderName;
        return name != nullptr && std::strncmp(name, "FreeType", 8) == 0;
    }
};

// An atlas that is replaced every so often, so thousands of fonts added and
// removed do not all leave their mark on one.
struct Atlases {
    std::unique_ptr<Context> context;
    int used = 0;

    Context& get()
    {
        if (!context || used >= 200) {
            context.reset();
            context.reset(new Context());
            used = 0;
        }
        ++used;
        return *context;
    }
};

enum class Outcome { READER_REFUSED, STRICT_REFUSED, BUILD_REFUSED, IMGUI_REFUSED, ACCEPTED };

struct Census {
    size_t run = 0, reader_refused = 0, strict_refused = 0, build_refused = 0, imgui_refused = 0,
           accepted = 0, stopped_before_atlas = 0, glyphs_baked = 0, glyphs_visible = 0,
           with_math = 0, minted = 0, collection_faces = 0, cid_keyed = 0, legacy_kerned = 0;

    void add(const Census& other)
    {
        run += other.run;
        reader_refused += other.reader_refused;
        strict_refused += other.strict_refused;
        build_refused += other.build_refused;
        imgui_refused += other.imgui_refused;
        accepted += other.accepted;
        stopped_before_atlas += other.stopped_before_atlas;
        glyphs_baked += other.glyphs_baked;
        glyphs_visible += other.glyphs_visible;
        with_math += other.with_math;
        minted += other.minted;
        collection_faces += other.collection_faces;
        cid_keyed += other.cid_keyed;
        legacy_kerned += other.legacy_kerned;
    }

    void print(const char* what, const char* loader) const
    {
        std::printf("stress_font census [%s, %s, ImWchar %d]: %zu mutants: %zu refused by the "
                    "reader, %zu by the strict pass, %zu by build_atlas_font, %zu by ImGui, %zu "
                    "accepted (%zu of them reader-chain only); %zu glyphs baked, %zu visible; "
                    "%zu mutants kept a MATH table, %zu codepoints minted; of those the reader "
                    "took, %zu were faces of a collection, %zu CID-keyed, %zu kerned from a kern "
                    "table\n",
                    what, loader, ATLAS_WCHAR_BITS, run, reader_refused, strict_refused,
                    build_refused, imgui_refused, accepted, stopped_before_atlas, glyphs_baked,
                    glyphs_visible, with_math, minted, collection_faces, cid_keyed, legacy_kerned);
    }
};

bool finite(double v)
{
    return std::isfinite(v);
}

// The codepoints a mutant is asked about: ASCII, a little Latin-1 and math,
// the ends of some cmap ranges, the minted codepoints and a few random ones.
std::vector<uint32_t> sample_codepoints(const FaceData& face, Rng& rng, bool thorough)
{
    std::vector<uint32_t> out;
    for (uint32_t cp = 0x20; cp < 0x7F; cp += thorough ? 1 : 3)
        out.push_back(cp);
    for (const uint32_t cp : {0xA0u, 0xE9u, 0xFFu, 0x2211u, 0x221Au, 0xE000u, 0xF8FFu, 0xFFFDu})
        out.push_back(cp);
    // ImGui indexes a baked font's glyphs by codepoint, so one question about
    // the top of plane 16 costs megabytes per font; the fuzz asks it only of
    // faces that map something there, through the lists below.
    if (thorough)
        for (const uint32_t cp : {0x1D44Eu, 0xF0000u, 0xFFFFDu, 0x10FFFFu})
            out.push_back(cp);
    const std::vector<CmapRange>& cmap = face.font().cmap();
    const size_t step = std::max<size_t>(1, cmap.size() / (thorough ? 24 : 8));
    for (size_t i = 0; i < cmap.size(); i += step) {
        out.push_back(cmap[i].first);
        out.push_back(cmap[i].last);
    }
    if (!cmap.empty())
        out.push_back(cmap.back().last);
    const std::vector<SyntheticCodepoint>& minted = face.synthetic_codepoints();
    const size_t minted_step = std::max<size_t>(1, minted.size() / (thorough ? 64 : 6));
    for (size_t i = 0; i < minted.size(); i += minted_step)
        out.push_back(minted[i].codepoint);
    for (int i = 0; i < 3; ++i)
        out.push_back(static_cast<uint32_t>(rng.below(thorough ? 0x110000 : 0x3000)));
    return out;
}

// Everything layout would ask of the face, with what must hold of the answers
// whatever the file says.
void check_answers(FontLibrary& library, Face role, const FaceData& face,
                   const std::vector<uint32_t>& sample, Census& census)
{
    const OpenTypeFont& font = face.font();
    const double size = 16.0;
    const double unit = size / face.units_per_em();
    CHECK(face.units_per_em() > 0);
    CHECK(font.glyph_count() > 0);

    std::string why;
    const FontMetrics* m = library.metrics(role, size, &why);
    if (m == nullptr) {
        CHECK(!why.empty());
        return;
    }
    CHECK(finite(m->ascent()) && finite(m->descent()) && finite(m->x_height()));
    CHECK(finite(m->space_width()) && m->space_width() >= 0.0);
    uint32_t previous = 'A';
    for (const uint32_t cp : sample) {
        CHECK_CONTEXT("U+%04X", cp);
        const uint32_t glyph = face.glyph(cp);
        CHECK_EQ(m->has(cp), face.has(cp));
        const double advance = m->advance(cp);
        CHECK(finite(advance) && advance >= 0.0 && advance <= 65535.0 * unit);
        if (glyph >= font.glyph_count())
            CHECK_EQ(advance, 0.0);
        const GlyphBBox box = m->bbox(cp);
        CHECK(finite(box.x_min) && finite(box.x_max) && finite(box.y_min) && finite(box.y_max));
        CHECK(box.x_min <= box.x_max && box.y_min <= box.y_max);
        // A GPOS value is 16 bits. The 'kern' table's subtables add up, and
        // there can be 65,535 of them, each giving a pair one 16-bit value.
        const double kern = m->kern(previous, cp);
        const double kern_units = font.legacy_kern_used() ? 32768.0 * 65535.0 : 32768.0;
        CHECK(finite(kern) && std::fabs(kern) <= kern_units * unit);
        previous = cp;
    }

    // Minted codepoints: downward from the top of plane 15 without a gap, each
    // one the face's and none the file's.
    const std::vector<SyntheticCodepoint>& minted = face.synthetic_codepoints();
    census.minted += minted.size();
    for (size_t i = 0; i < minted.size(); ++i) {
        CHECK_EQ(minted[i].codepoint, SYNTHETIC_FIRST - static_cast<uint32_t>(i));
        if (i % 16 != 0)
            continue;
        uint32_t glyph = 0;
        CHECK(face.lookup(minted[i].codepoint, &glyph));
        CHECK_EQ(glyph, minted[i].glyph);
        CHECK(!font.lookup(minted[i].codepoint, nullptr));
        CHECK(glyph <= 0xFFFFu);
    }
    CHECK(minted.size() <= size_t(SYNTHETIC_FIRST - SYNTHETIC_LAST + 1));

    // Stand-ins: free private-use codepoints, ascending with what they stand
    // for, and they round-trip.
    const auto& stand_ins = face.atlas_stand_ins();
    CHECK(stand_ins.size() <= 6400);
    if (face.atlas_stand_ins_unserved() != 0)
        CHECK(!face.warnings().empty());
    for (size_t i = 0; i < stand_ins.size(); i += std::max<size_t>(1, stand_ins.size() / 64)) {
        const uint32_t slot = stand_ins[i].first, codepoint = stand_ins[i].second;
        CHECK(slot >= 0xE000u && slot <= 0xF8FFu);
        CHECK(codepoint > 0xFFFFu && codepoint <= 0x10FFFFu);
        CHECK(!font.lookup(slot, nullptr));
        CHECK_EQ(face.atlas_codepoint(codepoint, 16), slot);
        CHECK_EQ(face.atlas_codepoint(slot, 16), 0u);
        CHECK_EQ(face.atlas_codepoint(codepoint, 32), codepoint);
        uint32_t glyph = 0xFFFFFFFFu;
        CHECK(face.glyph_for_atlas_codepoint(slot, 16, &glyph));
        CHECK_EQ(glyph, face.glyph(codepoint));
        if (i > 0) {
            CHECK(stand_ins[i - 1].first < slot);
            CHECK(stand_ins[i - 1].second < codepoint);
        }
    }

    const MathFontMetrics* math = m->as_math();
    if (math == nullptr)
        return;
    ++census.with_math;
    const MathParams& p = math->math();
    for (const double v : {p.math_x_height,
                           p.math_quad,
                           p.num1,
                           p.num2,
                           p.num3,
                           p.denom1,
                           p.denom2,
                           p.sup1,
                           p.sup3,
                           p.sub1,
                           p.sup_drop,
                           p.sub_drop,
                           p.axis_height,
                           p.default_rule_thickness,
                           p.big_op_spacing1,
                           p.big_op_spacing5,
                           p.radical_rule_thickness,
                           p.script_scale,
                           p.script_script_scale,
                           p.display_operator_min_height})
        CHECK(finite(v) && std::fabs(v) <= 65536.0 * size);
    for (const uint32_t cp : sample) {
        CHECK_CONTEXT("variants of U+%04X", cp);
        CHECK(finite(math->italic_correction(cp)));
        for (const GlyphVariant& v : math->vertical_variants(cp)) {
            CHECK(v.glyph <= 0xFFFFu);
            CHECK(finite(v.extent) && v.extent >= 0.0 && v.extent <= 65535.0 * unit);
            // The codepoint a variant is drawn by is one this face resolves
            // to that variant's glyph. (It can be U+0000: a cmap may map that
            // to the glyph, and then it is the lowest codepoint that does.)
            CHECK_EQ(face.glyph(v.codepoint), v.glyph);
        }
    }
}

// The yMax of a glyph's glyf header, in a font that passed the strict pass
// (its loca is then in order and inside glyf). False for a glyph with no data.
bool header_y_max(const OpenTypeFont& font, uint32_t glyph, int* y_max)
{
    TableRecord head, loca, glyf;
    for (size_t i = 0; i < font.table_count(); ++i) {
        const TableRecord record = font.table_record(i);
        if (record.tag == tag("head"))
            head = record;
        else if (record.tag == tag("loca"))
            loca = record;
        else if (record.tag == tag("glyf"))
            glyf = record;
    }
    if (head.size < 52 || loca.data == nullptr || glyf.data == nullptr)
        return false;
    const auto u16 = [](const uint8_t* at) { return (uint32_t(at[0]) << 8) | at[1]; };
    const bool is_long = u16(head.data + 50) != 0;
    const size_t width = is_long ? 4 : 2;
    if ((size_t(glyph) + 2) * width > loca.size)
        return false;
    const uint8_t* entry = loca.data + size_t(glyph) * width;
    const size_t begin = is_long ? (size_t(u16(entry)) << 16) | u16(entry + 2) : u16(entry) * 2;
    const size_t end =
        is_long ? (size_t(u16(entry + 4)) << 16) | u16(entry + 6) : u16(entry + 2) * 2;
    if (end > glyf.size || end < begin + 10)
        return false;
    *y_max = static_cast<int16_t>(u16(glyf.data + begin + 8));
    return true;
}

// A TrueType glyph that stb_truetype's rasteriser asserts on. It sizes a
// glyph's bitmap by the glyf header's box and then rasterises the outline;
// with a box whose top is at the baseline the bitmap starts at row 0, and an
// edge that lies wholly above it reaches `STBTT_assert(z->ey >= scan_y_top)`
// in stbtt__rasterize_sorted_edges, which is ImGui's assertion and so an
// abort where those are compiled in. The font the atlas is given has such a
// header raised over the outline (OpenTypeFont::atlas_box_repairs); this is
// the shape, for the tests to recognise one by.
bool drawn_above_box(const OpenTypeFont& font, uint32_t glyph)
{
    int y_max = 0;
    return !font.is_cff() && glyph < font.glyph_count() && header_y_max(font, glyph, &y_max) &&
           y_max <= 0 && font.bounds(glyph).y_max > 0.0;
}

// From the registry to a baked glyph. `atlases` null stops after
// build_atlas_font: the reader-only chain. `face_index` is the face of a
// collection, and may be one the file does not have.
Outcome run_chain(const Bytes& file, Census& census, Atlases* atlases, Rng& rng, bool thorough,
                  int face_index = 0)
{
    ++census.run;
    FontLibrary library;
    std::string why;
    // A face with a MATH table is asked as the math face, so the variant
    // chains and the math parameters are read as well. The directory says
    // which role to try first; the reader says which it is.
    bool names_math = false;
    for (const TableSpan& span : table_spans(file))
        names_math = names_math || span.tag == tag("MATH");
    Face role = names_math ? Face::MATH : Face::PROSE;
    if (!library.register_font(role, file.data(), file.size(), "mutant", &why, face_index)) {
        CHECK(!why.empty());
        // Refused, and nothing registered.
        CHECK(library.face_data(role) == nullptr);
        ++census.reader_refused;
        return Outcome::READER_REFUSED;
    }
    if (library.face_data(role)->font().has_math() != (role == Face::MATH)) {
        role = role == Face::MATH ? Face::PROSE : Face::MATH;
        if (!CHECK(
                library.register_font(role, file.data(), file.size(), "mutant", &why, face_index)))
            return Outcome::READER_REFUSED;
    }
    const FaceData* face = library.face_data(role);
    if (!CHECK(face != nullptr))
        return Outcome::READER_REFUSED;
    const std::vector<uint32_t> sample = sample_codepoints(*face, rng, thorough);
    check_answers(library, role, *face, sample, census);

    const OpenTypeFont& font = face->font();
    census.collection_faces += font.face_count() > 1 ? 1 : 0;
    census.cid_keyed += font.is_cid_keyed() ? 1 : 0;
    census.legacy_kerned += font.legacy_kern_used() ? 1 : 0;
    // The face that was asked for, of a file that has it.
    CHECK_EQ(font.face_index(), static_cast<uint32_t>(face_index));
    CHECK(font.face_index() < font.face_count());
    CHECK_EQ(font.face_count(), OpenTypeFont::faces_in(file.data(), file.size()));
    // Glyph names, which decide the order codepoints are minted in: one per
    // glyph or none at all.
    {
        std::vector<std::string> names;
        if (font.glyph_order_names(names))
            CHECK_EQ(names.size(), size_t(font.glyph_count()));
        else
            CHECK(names.empty());
    }
    // The 'kern' table as it was read: ascending, a value a pair.
    {
        const std::vector<uint32_t>& keys = font.legacy_kern_keys();
        CHECK_EQ(font.legacy_kern_values().size(), keys.size());
        for (size_t i = 1; i < keys.size(); i += std::max<size_t>(1, keys.size() / 64))
            CHECK(keys[i - 1] < keys[i]);
        if (font.legacy_kern_used() && !keys.empty()) {
            const size_t at = keys.size() / 2;
            const uint32_t left = keys[at] >> 16, right = keys[at] & 0xFFFFu;
            CHECK_EQ(font.pair_kern(left, right), font.legacy_kern_values()[at]);
            CHECK_EQ(font.pair_kern_accumulated(left, right), font.legacy_kern_values()[at]);
        }
    }
    // The cmap as the header promises it: ascending, not overlapping, and no
    // range starting at the missing glyph.
    {
        const std::vector<CmapRange>& cmap = font.cmap();
        for (size_t i = 0; i < cmap.size(); i += std::max<size_t>(1, cmap.size() / 256)) {
            CHECK(cmap[i].first <= cmap[i].last);
            CHECK(cmap[i].glyph != 0);
            // No glyph id wraps: the last of a run is not below its first.
            CHECK(cmap[i].last_glyph() >= cmap[i].glyph);
            CHECK(cmap[i].one_glyph ||
                  uint64_t(cmap[i].glyph) + (cmap[i].last - cmap[i].first) <= 0xFFFFFFFFull);
            if (i > 0)
                CHECK(cmap[i - 1].last < cmap[i].first);
        }
    }
    why.clear();
    if (!font.strict_check(&why)) {
        CHECK(!why.empty());
        // And the atlas entry refuses it, with that reason, touching no atlas.
        if (atlases != nullptr) {
            Context& context = atlases->get();
            const int fonts_before = context.atlas()->Fonts.Size;
            std::string atlas_why;
            CHECK(add_face_to_atlas(context.atlas(), *face, 16.0f, nullptr, &atlas_why) == nullptr);
            CHECK(atlas_why.find(why) != std::string::npos);
            CHECK_EQ(context.atlas()->Fonts.Size, fonts_before);
        }
        ++census.strict_refused;
        return Outcome::STRICT_REFUSED;
    }

    // The file the atlas is handed: bounded by the file it was made from, and
    // a font the reader itself loads, with a cmap that stays inside the font.
    for (const int bits : {16, 32}) {
        // The width this build draws with every time, the other now and then.
        if (bits != ATLAS_WCHAR_BITS && !thorough && !rng.chance(4))
            continue;
        Bytes built;
        why.clear();
        if (!build_atlas_font(*face, bits, built, &why)) {
            CHECK(!why.empty());
            ++census.build_refused;
            return Outcome::BUILD_REFUSED;
        }
        CHECK(built.size() <=
              file.size() + 12 + 16 * 64 + 16 * size_t(font.cmap().size() + 1) +
                  12 * (face->synthetic_codepoints().size() + face->atlas_stand_ins().size()) +
                  4 * 256);
        // One face, whatever the file was.
        CHECK(rd32(built, 0) != tag("ttcf"));
        CHECK_EQ(rd32(built, 0), font.sfnt_version());
        if (thorough || rng.chance(8)) {
            OpenTypeFont again;
            CHECK(again.load(built.data(), built.size(), &why));
            CHECK_EQ(again.face_count(), 1u);
            CHECK_EQ(again.glyph_count(), font.glyph_count());
            // Groups of one glyph stay groups of one glyph.
            bool groups_of_one_glyph = false;
            for (const CmapRange& range : font.cmap())
                groups_of_one_glyph =
                    groups_of_one_glyph || (range.one_glyph && range.last > range.first &&
                                            range.glyph < font.glyph_count());
            CHECK_EQ(again.cmap_format(), groups_of_one_glyph ? 13 : 12);
            for (const CmapRange& range : again.cmap())
                CHECK(range.last_glyph() < font.glyph_count());
            // A header box at or below the baseline over an outline that
            // rises above it is raised over the outline.
            for (const GlyphBoxRepair& repair : font.atlas_box_repairs()) {
                int y_max = 0;
                CHECK(repair.y_max > 0);
                if (CHECK(header_y_max(again, repair.glyph, &y_max)))
                    CHECK_EQ(y_max, static_cast<int>(repair.y_max));
            }
        }
    }
    if (atlases == nullptr) {
        ++census.accepted;
        ++census.stopped_before_atlas;
        return Outcome::ACCEPTED;
    }

    static const char* const dump = std::getenv("IMTYPESET_STRESS_DUMP");
    if (dump != nullptr && dump[0] != '\0') {
        if (std::FILE* out = std::fopen(dump, "wb")) {
            std::fwrite(file.data(), 1, file.size(), out);
            std::fclose(out);
        }
    }
    Context& context = atlases->get();
    why.clear();
    ImFontConfig config;
#ifdef IMGUI_ENABLE_FREETYPE
    static const char* const no_hinting = std::getenv("IMTYPESET_STRESS_NO_HINTING");
    if (no_hinting != nullptr && no_hinting[0] == '1')
        config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_NoHinting;
#endif
    ImFont* added = add_face_to_atlas(context.atlas(), *face, 16.0f, &config, &why);
    if (added == nullptr) {
        CHECK(!why.empty());
        ++census.imgui_refused;
        return Outcome::IMGUI_REFUSED;
    }
    ImGui::NewFrame();
    ImFontBaked* baked = added->GetFontBaked(16.0f);
    // Layout's metrics at the em size that is 16 px tall as ImGui means it.
    const FontMetrics* at_16_px =
        library.metrics(role, library.em_size_for_pixel_height(role, 16.0));
    if (baked == nullptr || at_16_px == nullptr) {
        FAIL("no baked font, or no metrics");
    } else {
        // How far from the origin the strict pass lets a glyph reach, in
        // pixels at this size, with room for hinting and oversampling.
        const float reach = 16.0f * 16.0f + 8.0f;
        for (const uint32_t cp : sample) {
            CHECK_CONTEXT("baking U+%04X", cp);
            const uint32_t asked = atlas_codepoint(*face, cp);
            if (asked == 0 || asked > IM_UNICODE_CODEPOINT_MAX)
                continue;
            const ImWchar c = static_cast<ImWchar>(asked);
            // The atlas and layout agree on which codepoints the font has: a
            // codepoint is the atlas's when layout resolves it to a real glyph.
            const uint32_t glyph = face->glyph(cp);
            const bool drawable = face->has(cp) && glyph != 0 && glyph < font.glyph_count();
            CHECK_EQ(added->IsGlyphInFont(c), drawable);
            const ImFontGlyph* g = baked->FindGlyph(c);
            if (g == nullptr)
                continue;
            ++census.glyphs_baked;
            CHECK(std::isfinite(g->AdvanceX));
            // stb_truetype's advance is the hmtx value scaled, and so is
            // layout's: they are the same number but for float against
            // double. FreeType's is hinted and rounded. The exception is an
            // advance past 32,767 units, which stb_truetype reads as negative
            // and ImGui then clamps to zero, recentring the glyph's box.
            const bool signed_advance = font.advance(glyph) > 32767;
            if (!context.freetype() && drawable && !signed_advance) {
                const double measured = at_16_px->advance(cp);
                CHECK(std::fabs(static_cast<double>(g->AdvanceX) - measured) <=
                      1.0e-4 * (1.0 + measured));
            }
            if (!g->Visible)
                continue;
            ++census.glyphs_visible;
            CHECK(std::isfinite(g->X0) && std::isfinite(g->X1) && std::isfinite(g->Y0) &&
                  std::isfinite(g->Y1));
            CHECK(g->X0 <= g->X1 && g->Y0 <= g->Y1);
            // Under stb_truetype the box is the glyf header's or the
            // charstring's, which the strict pass bounded. FreeType's hinter
            // runs the font's own bytecode and may put points anywhere.
            if (!context.freetype() && drawable && !signed_advance)
                CHECK(g->X0 >= -reach && g->X1 <= reach && g->Y1 - g->Y0 <= 2 * reach);
        }
    }
    ImGui::Render();
    context.atlas()->RemoveFont(added);
    ++census.accepted;
    return Outcome::ACCEPTED;
}

const char* loader_name()
{
#ifdef IMGUI_ENABLE_FREETYPE
    return "FreeType";
#else
    return "stb_truetype";
#endif
}

// --- Structured cases --------------------------------------------------------------------

struct Case {
    const char* name;
    Bytes file;
    Outcome expected;
    // The face asked for; of a collection, or one no file has.
    int face = 0;
    // What the case is a seed of in the dense fuzz, for its census: "" for
    // the glyf, CFF and MATH cases, else the structure it is there for. Those
    // seeds also get the mutations aimed at that structure.
    const char* kind = "";
};

// The kind of a case that is small enough to be a seed and is not one: a
// mutant of it costs what a thousand of the others do.
const char* const NOT_A_SEED = "not a seed";

Bytes patched(const MiniFont& font, uint32_t table,
              const std::vector<std::array<uint32_t, 3>>& writes)
{
    Sfnt sfnt = font.sfnt();
    Bytes& data = sfnt.table(table);
    for (const auto& w : writes) {
        if (w[2] == 4)
            wr32(data, w[0], w[1]);
        else
            wr16(data, w[0], w[1]);
    }
    return sfnt.build();
}

// The S5 font: ten levels of subroutine, each calling the next `fan` times,
// the last drawing sixteen curves per operator.
MiniCff cff_work_bomb()
{
    MiniCff cff;
    const int fan = 5, levels = 9;
    const int bias = cs_bias(static_cast<size_t>(levels) + 1);
    for (int level = 0; level < levels; ++level) {
        Bytes subr;
        for (int k = 0; k < fan; ++k) {
            cs_int(subr, level + 1 - bias);
            put8(subr, 10);
        }
        put8(subr, 11);
        cff.local_subrs.push_back(subr);
    }
    Bytes leaf;
    for (int k = 0; k < 96; ++k)
        put8(leaf, 139u + static_cast<uint32_t>(k % 7));
    put8(leaf, 8);
    put8(leaf, 11);
    cff.local_subrs.push_back(leaf);
    Bytes top;
    cs_int(top, 0);
    cs_int(top, 0);
    put8(top, 21);
    cs_int(top, 0 - bias);
    put8(top, 10);
    put8(top, 14);
    cff.charstrings = {cs_square(0, 0, 100), top};
    return cff;
}

// Where the CharStrings INDEX starts in a MiniCff's table: after the header,
// the Name INDEX, the Top DICT INDEX, the String INDEX and the Global Subr
// INDEX, none of which a test here changes the size of.
size_t mini_cff_charstrings_at(const Bytes& cff)
{
    // The Top DICT's first operand is the CharStrings offset, in the five-byte form.
    const size_t names = 4 + 2 + 1 + 8 + 4;
    return rd32(cff, names + 2 + 1 + 8 + 1);
}

// --- The hostile shapes of the formats past the bundled fonts' -------------------------------

Bytes mini_with(const MiniFont& font, uint32_t table, const Bytes& data)
{
    Sfnt sfnt = font.sfnt();
    sfnt.table(table) = data;
    return sfnt.build();
}

// .notdef and 'A'..'E' as squares: what the kern and cmap cases start from.
MiniFont six_squares()
{
    MiniFont f;
    for (int g = 0; g < 6; ++g)
        f.glyphs.push_back(square_glyph(0, 0, 100 + 50 * g));
    f.cmap = {{'A', 'E', 1}};
    return f;
}

// The two-face collection: three_squares, and a CFF font of three squares
// with other metrics.
MiniFont collection_cff_face()
{
    MiniCff cff;
    cff.charstrings = {cs_square(0, 0, 100), cs_square(50, 0, 400), cs_square(0, 0, 300)};
    MiniFont f = cff_font(cff);
    f.units_per_em = 2048;
    f.ascender = 1900;
    f.descender = -500;
    return f;
}

Bytes two_face_collection()
{
    return collection({three_squares().build(), collection_cff_face().build()});
}

// A kern table of `count` subtables of one pair each: the same pair in all
// of them, kerned by `value` each time, or a different pair in each, by 3.
Bytes kern_of_one_pair_subtables(size_t count, bool same_pair, int value = 3)
{
    std::vector<Bytes> subtables;
    subtables.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t k = static_cast<uint32_t>(i);
        subtables.push_back(
            kern_subtable({same_pair ? KernPair{1, 2, value} : KernPair{1 + k / 251, k % 251, 3}}));
    }
    return kern_table(subtables);
}

// A cmap of one format 13 group: every codepoint there is, one glyph.
Bytes whole_code_space_format13()
{
    return mini_with(six_squares(), tag("cmap"),
                     cmap_table({{0, 6, cmap_sub12or13(13, {{0, 0x10FFFF, 1}})}}));
}

// A cmap of one format 12 subtable of `count` groups that all start, and
// end, at 'A': each replaces the one before.
Bytes same_code_format12(size_t count)
{
    std::vector<CmapGroup> groups;
    for (size_t i = 0; i < count; ++i)
        groups.push_back({'A', 'A', 1 + static_cast<uint32_t>(i % 5)});
    return mini_with(six_squares(), tag("cmap"), cmap_table({{3, 10, cmap_sub12or13(12, groups)}}));
}

// The mini math font with a format 2 post table naming its glyphs, so the
// five unencoded variants are minted in name order.
Bytes named_math_font()
{
    Sfnt sfnt = mini_math().sfnt();
    sfnt.table(tag("MATH")) = math_table(1, {2, 3, 4, 5, 6});
    sfnt.table(tag("post")) =
        post_table(0x00020000u, {0, 36, 258, 259, 260, 261, 258}, {"b", "B", "a.alt", "Z"});
    return sfnt.build();
}

// A math font of 2 + shared + numbered glyphs. Glyph 1 is 'A'; the next
// `shared` glyphs are all named "x"; the `numbered` after them are named
// "x.1", "x.2", ...: the names a duplicate "x" would be given first. 'A' has
// the first five of the shared glyphs for variants, so minting reads the names.
Bytes shared_name_font(uint32_t shared, uint32_t numbered)
{
    MiniFont f;
    f.glyphs.push_back(square_glyph(50, 0, 400));
    f.glyphs.push_back(square_glyph(50, 0, 400));
    f.glyphs.resize(2 + shared + numbered);
    f.cmap = {{'A', 'A', 1}};
    std::vector<uint32_t> indices = {0, 36};
    std::vector<std::string> strings = {"x"};
    for (uint32_t i = 0; i < shared; ++i)
        indices.push_back(258);
    for (uint32_t i = 0; i < numbered; ++i) {
        indices.push_back(259 + i);
        strings.push_back("x." + std::to_string(i + 1));
    }
    Sfnt sfnt = f.sfnt();
    sfnt.table(tag("MATH")) = math_table(1, {2, 3, 4, 5, 6});
    sfnt.table(tag("post")) = post_table(0x00020000u, indices, strings);
    return sfnt.build();
}

// `dicts` Font DICTs whose Private DICTs all name one Subrs INDEX of `subrs`
// entries, the first of them a square. Five glyphs, spread over the DICTs,
// each calling that square.
CidCff dicts_sharing_subrs(size_t dicts, size_t subrs)
{
    CidCff cff;
    cff.charstrings.push_back(cs_square(0, 0, 50));
    for (int g = 1; g < 5; ++g)
        cff.charstrings.push_back(cs_call(10, 20, 0, subrs));
    cff.fd_subrs.assign(dicts, {});
    cff.fd_subrs[0].assign(subrs, Bytes{11});
    cff.fd_subrs[0][0] = subr_square(100);
    cff.share_subrs = true;
    const uint32_t last = static_cast<uint32_t>(std::min<size_t>(dicts, 256) - 1);
    cff.fd_select = fdselect0({0, 0, 1 % (last + 1), last / 2, last});
    cff.cids = {1, 2, 3, 4};
    return cff;
}

// 256 Font DICTs whose Private DICTs name 256 different Subrs INDEXes that
// lie over one another: a run of 0x02 bytes reads, from any byte of it, as an
// INDEX of 514 objects with two-byte offsets that are all 514.
Bytes overlapping_subrs_font()
{
    CidCff cff;
    cff.charstrings = {cs_square(0, 0, 50), cs_square(0, 0, 100)};
    cff.fd_subrs.assign(256, {});
    cff.fd_select = fdselect0({0, 255});
    cff.cids = {1};
    MiniFont f = cid_font(cff);
    const size_t run_at = f.cff.size();
    f.cff.resize(run_at + 2048, 0x02);
    for (size_t fd = 0; fd < 256; ++fd) {
        // `29 <offset from this DICT> 19` in place of the DICT's one operator.
        const size_t private_at = cff.privates_at + 6 * fd;
        wr32(f.cff, private_at + 1, static_cast<uint32_t>(run_at + fd - private_at));
        f.cff[private_at + 5] = 19;
    }
    return f.build();
}

// Ten levels of subroutine, each calling the next five times, the last doing
// nothing but stack work: roll, index, dup and drop, the stack as it found it.
MiniCff cff_stack_bomb()
{
    MiniCff cff;
    const int fan = 5, levels = 9;
    const int bias = cs_bias(static_cast<size_t>(levels) + 1);
    for (int level = 0; level < levels; ++level) {
        Bytes subr;
        for (int k = 0; k < fan; ++k) {
            cs_int(subr, level + 1 - bias);
            put8(subr, 10);
        }
        put8(subr, 11);
        cff.local_subrs.push_back(subr);
    }
    Bytes leaf;
    for (int k = 0; k < 8; ++k)
        append(leaf, cs_asm("1 2 3 3 1 roll 1 index dup drop drop drop drop drop"));
    put8(leaf, 11);
    cff.local_subrs.push_back(leaf);
    Bytes top = cs_asm("0 0 rmoveto");
    cs_int(top, 0 - bias);
    put8(top, 10);
    put8(top, 14);
    cff.charstrings = {cs_square(0, 0, 100), top};
    return cff;
}

// A CFF font whose glyphs compute their coordinates with the arithmetic,
// storage and conditional operators, and draw with the flex operators.
MiniCff arithmetic_cff()
{
    MiniCff cff;
    cff.charstrings = {
        cs_square(0, 0, 100),
        cs_asm("100 2 div 30 12 sub rmoveto 6 7 mul hlineto 144 sqrt -25 abs add vlineto endchar"),
        cs_asm("7 dup mul 3 10 exch sub rmoveto 10 20 30 70 4 1 roll rlineto endchar"),
        cs_asm("42 0 put 7 31 put 0 get 31 get rmoveto 11 22 3 4 ifelse hlineto 5 3 and 60 mul "
               "0 7 or add 9 not add 4 4 eq add vlineto endchar"),
        cs_asm("10 20 rmoveto 11 22 33 1 index add add add 99 drop neg hlineto 10 0 10 30 10 0 "
               "10 0 10 -30 10 0 50 flex endchar"),
        cs_asm("10 20 rmoveto 10 10 30 10 10 10 10 hflex 10 10 10 20 10 10 10 -10 10 hflex1 "
               "10 10 10 20 10 0 10 0 10 -10 15 flex1 endchar"),
        cs_asm("0.5 100.25 rmoveto -107 callgsubr endchar"),
    };
    cff.global_subrs = {subr_square(200)};
    return cff;
}

std::vector<Case> structured_cases()
{
    std::vector<Case> cases;
    const auto add = [&](const char* name, Bytes file, Outcome expected, int face = 0,
                         const char* kind = "") {
        cases.push_back({name, std::move(file), expected, face, kind});
    };
    const Outcome OK = Outcome::ACCEPTED, STRICT = Outcome::STRICT_REFUSED,
                  READER = Outcome::READER_REFUSED;

    // --- glyf and loca
    const MiniFont base = three_squares();
    add("intact glyf font", base.build(), OK);
    // S2, in the shape that reaches a wild read under stb_truetype, and in
    // the reviewer's, which stb_truetype takes for an empty glyph because the
    // offset goes negative as an int.
    add("loca pair far past glyf",
        patched(base, tag("loca"), {{4, 0x00100000u, 4}, {8, 0x00100040u, 4}}), STRICT);
    add("loca pair at 0x7FFFFFF0",
        patched(base, tag("loca"), {{4, 0x7FFFFFF0u, 4}, {8, 0x7FFFFFF8u, 4}}), STRICT);
    add("loca not in order", patched(base, tag("loca"), {{8, 0, 4}}), STRICT);
    {
        Sfnt sfnt = base.sfnt();
        const uint32_t size = static_cast<uint32_t>(sfnt.table(tag("glyf")).size());
        wr32(sfnt.table(tag("loca")), 12, size + 4);
        add("loca ends past glyf", sfnt.build(), STRICT);
    }
    {
        MiniFont f = base;
        f.glyphs[1].resize(6);
        add("glyph shorter than its header", f.build(), STRICT);
        f = base;
        wr16(f.glyphs[1], 0, 0xFFFE);
        add("contour count -2", f.build(), STRICT);
        f = base;
        wr16(f.glyphs[1], 10, 199);
        add("more points than data", f.build(), STRICT);
        f = base;
        f.glyphs[1].resize(f.glyphs[1].size() - 6);
        add("coordinates cut short", f.build(), STRICT);
        f = base;
        wr16(f.glyphs[1], 2, 700);
        wr16(f.glyphs[1], 6, 100);
        add("bounding box inside out", f.build(), STRICT);
        f = base;
        wr16(f.glyphs[1], 6, 32767);
        wr16(f.glyphs[1], 8, 32767);
        add("bounding box 32 em wide", f.build(), STRICT);
        f = base;
        f.glyphs[1] = simple_glyph({{{0, 0}}, {{0, 500}}, {{32700, 500}}, {{32700, 0}}});
        wr16(f.glyphs[1], 6, 500);
        add("a point 32 em out under an honest-looking box", f.build(), STRICT);
        f = base;
        // Two contours whose end points go 3, 1.
        f.glyphs[1] = square_glyph(0, 0, 100);
        wr16(f.glyphs[1], 0, 2);
        f.glyphs[1].insert(f.glyphs[1].begin() + 12, {0, 1});
        add("contour end points decreasing", f.build(), STRICT);
        f = base;
        // Found by the fuzz: a last contour that is one point, off the
        // curve. stb_truetype looks at the point after a contour's first when
        // that first is off the curve, and here there is none (a one-byte
        // read past its vertex array, in stbtt__GetGlyphShapeTT).
        f.glyphs[1] = simple_glyph({{{100, 100}}});
        f.glyphs[1][14] = 0x00;
        add("glyph ending in a contour of one off-curve point", f.build(), STRICT);
        // The same contour with another after it has a point to be read, and
        // real fonts contain it (Mukti.ttf does).
        f = base;
        f.glyphs[1] = simple_glyph({{{100, 100}}, {{0, 0}}, {{0, 300}}, {{300, 0}}});
        wr16(f.glyphs[1], 0, 2);
        f.glyphs[1].insert(f.glyphs[1].begin() + 10, {0, 0});
        f.glyphs[1][16] = 0x00;
        add("contour of one off-curve point before another contour", f.build(), OK);
        f = base;
        f.glyphs[1] = simple_glyph({{{100, 100}}});
        add("contour of one on-curve point", f.build(), OK);
        f = base;
        // A flag that repeats past the last point.
        f.glyphs[1][14] = 0x09;
        f.glyphs[1][15] = 200;
        add("flags repeat past the last point", f.build(), STRICT);
    }
    // --- composites
    {
        MiniFont f = base;
        f.glyphs[1] = composite_glyph({{1, 0, 0}});
        add("composite of itself", f.build(), STRICT);
        f = base;
        f.glyphs[1] = composite_glyph({{2, 0, 0}});
        f.glyphs[2] = composite_glyph({{1, 10, 10}});
        add("composite cycle A-B-A", f.build(), STRICT);
        f = base;
        f.glyphs[1] = composite_glyph({{0, 0, 0}, {9, 0, 0}});
        add("component out of range", f.build(), STRICT);
        f = base;
        f.glyphs[1] = composite_glyph({{2, 0, 0}});
        wr16(f.glyphs[1], 10, 0x0001); // arguments are point numbers
        add("component placed by point matching", f.build(), STRICT);
        f = base;
        f.glyphs[1] = composite_glyph({{2, 100, 0}, {0, 0, 300}});
        add("honest composite", f.build(), OK);
        f = base;
        f.glyphs[1] = composite_glyph({{2, 0, 0}});
        f.glyphs[1].resize(f.glyphs[1].size() - 3);
        add("component record cut short", f.build(), STRICT);

        // A chain: glyph 1 -> 2 -> ... -> the last, which is a square.
        const auto chain = [&](int length) {
            MiniFont c = base;
            c.glyphs.resize(1);
            for (int i = 1; i <= length; ++i)
                c.glyphs.push_back(composite_glyph({{static_cast<uint32_t>(i + 1), 1, 1}}));
            c.glyphs.push_back(square_glyph(0, 0, 300));
            c.cmap = {{'A', 'A', 1}};
            return c.build();
        };
        add("composite chain 8 deep", chain(8), OK);
        add("composite chain 40 deep", chain(40), STRICT);
        add("composite chain 2000 deep", chain(2000), STRICT);

        // Seven levels, each eight copies of the next: 2 million squares.
        MiniFont bomb = base;
        bomb.glyphs.resize(1);
        for (uint32_t level = 1; level <= 7; ++level) {
            std::vector<Component> parts;
            for (int k = 0; k < 8; ++k)
                parts.push_back({level + 1, k, k});
            bomb.glyphs.push_back(composite_glyph(parts));
        }
        bomb.glyphs.push_back(square_glyph(0, 0, 300));
        bomb.cmap = {{'A', 'A', 1}};
        add("composites that expand to two million squares", bomb.build(), STRICT);
    }
    // --- head, hhea, hmtx, the container
    {
        add(
            "hmtx without the trailing side bearings",
            [&] {
                Sfnt sfnt = base.sfnt();
                wr16(sfnt.table(tag("hhea")), 34, 1);
                sfnt.table(tag("hmtx")).resize(4);
                return sfnt.build();
            }(),
            STRICT);
        add(
            "hmtx shorter than numberOfHMetrics",
            [&] {
                Sfnt sfnt = base.sfnt();
                sfnt.table(tag("hmtx")).resize(8);
                return sfnt.build();
            }(),
            READER);
        // Found by the fuzz, and not refused: stb_truetype reads an advance
        // as a signed 16-bit number, so this one is negative there; ImGui
        // clamps it to zero and moves the glyph's box half the difference,
        // while layout, like FreeType and the format, advances by 33,368
        // units. Real fonts do this to mean a small negative advance
        // (NachlieliCLM-Bold.otf has two glyphs at 65,530), and nothing is
        // read out of bounds, so the font draws.
        add("advance width of 33,368 units", patched(base, tag("hmtx"), {{4, 0x8258, 2}}), OK);
        add("unitsPerEm 8", patched(base, tag("head"), {{18, 8, 2}}), STRICT);
        add("ascender equal to descender", patched(base, tag("hhea"), {{4, 100, 2}, {6, 100, 2}}),
            STRICT);
        add("indexToLocFormat 7", patched(base, tag("head"), {{50, 7, 2}}), STRICT);
        Sfnt sfnt = base.sfnt();
        sfnt.version = tag("OTTO");
        add("glyf outlines under an OTTO tag", sfnt.build(), STRICT);
        sfnt = base.sfnt();
        MiniCff square;
        square.charstrings = {cs_square(0, 0, 100), cs_square(0, 0, 200), cs_square(0, 0, 300)};
        sfnt.table(tag("CFF ")) = square.build();
        add("glyf and CFF outlines together", sfnt.build(), STRICT);
    }
    // --- cmap
    {
        MiniFont f = base;
        Sfnt sfnt = f.sfnt();
        sfnt.table(tag("cmap")) = cmap_format4({{'P', 'Z', 1}, {'A', 'R', 2}, {'A', 'C', 1}});
        add("cmap format 4 segments out of order and overlapping", sfnt.build(), OK);
        sfnt.table(tag("cmap")) =
            cmap_format12({{'M', 'Z', 1}, {'A', 'N', 1}, {'A', 'B', 2}, {0x10000, 0xFFFFFFFFu, 1}});
        add("cmap format 12 groups out of order and overlapping", sfnt.build(), OK);
        sfnt.table(tag("cmap")) = cmap_format12({{'A', 'Z', 2}, {0x1F600, 0x1F64F, 60000}});
        add("cmap maps past the last glyph", sfnt.build(), OK);
        sfnt.table(tag("cmap")) = cmap_format12({{'A', 'B', 1}});
        wr32(sfnt.table(tag("cmap")), 24, 0x7FFFFFFFu); // the group count
        add("cmap format 12 group count absurd", sfnt.build(), READER);
    }
    // --- CFF
    {
        MiniCff cff;
        cff.charstrings = {cs_square(0, 0, 100), cs_square(50, 0, 400)};
        add("intact CFF font", cff_font(cff).build(), OK);

        MiniCff self = cff;
        Bytes call;
        cs_int(call, 0 - cs_bias(1));
        put8(call, 10);
        self.local_subrs = {call};
        self.charstrings[1] = call;
        add("CFF subroutine that calls itself", cff_font(self).build(), OK);

        MiniCff mutual = cff;
        Bytes to_global, to_local;
        cs_int(to_global, 0 - cs_bias(1));
        put8(to_global, 29);
        cs_int(to_local, 0 - cs_bias(1));
        put8(to_local, 10);
        mutual.local_subrs = {to_global};
        mutual.global_subrs = {to_local};
        mutual.charstrings[1] = to_local;
        add("CFF local and global subroutines that call each other", cff_font(mutual).build(), OK);

        add("CFF subroutines that fan out to a million operators",
            cff_font(cff_work_bomb()).build(), STRICT);

        MiniFont f = cff_font(cff);
        const size_t chars = mini_cff_charstrings_at(f.cff);
        MiniFont g = f;
        g.cff[chars + 2] = 9; // offSize
        add("CFF CharStrings INDEX with offSize 9", g.build(), READER);
        g = f;
        wr32(g.cff, chars + 3 + 4, 0x00FFFFFFu);
        add("CFF CharStrings INDEX offset past the table", g.build(), READER);
        g = f;
        wr32(g.cff, chars + 3 + 4, 1);
        wr32(g.cff, chars + 3, 40);
        add("CFF CharStrings INDEX offsets decreasing", g.build(), READER);

        MiniCff with_subrs = cff;
        Bytes ret;
        put8(ret, 11);
        with_subrs.local_subrs = {ret, ret};
        g = cff_font(with_subrs);
        // The local Subr INDEX is the last thing in the table: 2 + 1 + 12 + 2.
        g.cff[g.cff.size() - 2 - 12 - 1] = 7;
        add("CFF local Subr INDEX with offSize 7", g.build(), STRICT);
        g = cff_font(with_subrs);
        // The Private DICT is `29 <offset> 19`, just before that INDEX.
        wr32(g.cff, g.cff.size() - 17 - 5, 0x00FFFFFFu);
        add("CFF Subrs offset past the table", g.build(), STRICT);

        MiniCff twice = cff;
        dict_int(twice.extra_top, 4);
        put8(twice.extra_top, 17);
        add("CFF Top DICT with two CharStrings offsets", cff_font(twice).build(), STRICT);
        MiniCff fd = cff;
        dict_int(fd.extra_top, 4);
        put8(fd.extra_top, 12);
        put8(fd.extra_top, 36);
        add("CFF Top DICT with an FDArray", cff_font(fd).build(), STRICT);

        MiniCff far = cff;
        far.charstrings[1] = cs_square(31000, 31000, 2000);
        add("CFF outline 33 em from the origin", cff_font(far).build(), STRICT);

        g = cff_font(cff);
        g.glyph_count = 5;
        g.cmap = {{'A', 'A', 1}};
        add("CFF with fewer charstrings than maxp has glyphs", g.build(), STRICT);

        MiniCff fraction = cff;
        fraction.local_subrs = {ret, ret, ret};
        Bytes half = {255, 0xFF, 0x95, 0x80, 0x00}; // -106.5 in 16.16
        put8(half, 10);
        put8(half, 14);
        fraction.charstrings[1] = half;
        add("CFF subroutine called by a fraction", cff_font(fraction).build(), STRICT);
    }
    // --- MATH
    {
        const MiniFont f = mini_math();
        const Bytes math = math_table(1, {2, 3, 4, 5, 6});
        add("intact MATH font", mini_math_file(f, math), OK);
        Bytes m = math;
        wr16(m, 4, 0xFFF0);
        add("MATH constants offset dangling", mini_math_file(f, m), OK);
        m = math;
        wr16(m, 6, 0xFFF0);
        add("MATH glyph info offset dangling", mini_math_file(f, m), OK);
        m = math;
        wr16(m, 8, 0xFFF0);
        add("MATH variants offset dangling", mini_math_file(f, m), OK);
        m = math;
        wr16(m, 224 + 10, 0xFFF0);
        add("MATH construction offset dangling", mini_math_file(f, m), OK);
        m = math;
        wr16(m, 224 + 18, 0xFFF0);
        add("MATH assembly offset dangling", mini_math_file(f, m), OK);
        m = math;
        wr16(m, 224 + 20, 0xFFFF);
        add("MATH variant count absurd", mini_math_file(f, m), OK);
        add("MATH variants past the last glyph", mini_math_file(f, math_table(1, {2, 65535, 900})),
            OK);
        add("MATH variant chain through the base itself",
            mini_math_file(f, math_table(1, {1, 1, 2, 2})), OK);
    }
    // --- GPOS, on a real font
    add("GPOS: 256 offsets to one class subtable",
        with_table("Roboto-Regular.ttf", tag("GPOS"), gpos_class_bomb(256, true, 300, 300)), OK);
    add("GPOS: 256 class subtables laid over one another",
        with_table("Roboto-Regular.ttf", tag("GPOS"), gpos_class_bomb(256, false, 300, 300)), OK);
    // --- collections
    {
        const Bytes ttc = two_face_collection();
        const uint32_t face0 = rd32(ttc, 12), face1 = rd32(ttc, 16);
        const uint32_t size = static_cast<uint32_t>(ttc.size());
        add("collection: its TrueType face", ttc, OK, 0, "collection");
        add("collection: its CFF face", ttc, OK, 1);
        add("collection asked for a face past its last", ttc, READER, 2);
        add("collection asked for face 2,147,483,647", ttc, READER, 0x7FFFFFFF);
        add("collection asked for face -1", ttc, READER, -1);
        add("single font asked for face 1", base.build(), READER, 1);
        Bytes f = ttc;
        wr32(f, 8, 0xFFFFFFFFu);
        add("ttcf numFonts 0xFFFFFFFF", f, READER);
        add("ttcf numFonts 0xFFFFFFFF, face 1", f, READER, 1);
        f = ttc;
        wr32(f, 8, 0);
        add("ttcf numFonts 0", f, READER);
        f = ttc;
        wr32(f, 8, 3);
        add("ttcf numFonts one more than there are offsets, face 0", f, OK, 0, "collection");
        // The "offset" of face 2 is the first four bytes after the offsets
        // that are there: face 0's version, 0x00010000, which is past the end.
        add("ttcf numFonts one more than there are offsets, that face", f, READER, 2);
        f = ttc;
        wr32(f, 12, 0);
        add("ttcf face offset at the ttcf header itself", f, READER);
        add("ttcf face offset at the ttcf header itself, the other face", f, OK, 1);
        f = ttc;
        wr32(f, 12, face1);
        add("ttcf two faces sharing one directory, face 0", f, OK, 0, "collection");
        add("ttcf two faces sharing one directory, face 1", f, OK, 1);
        f = ttc;
        wr32(f, 12, face1);
        wr32(f, 16, face0);
        add("ttcf faces in the other order", f, OK, 0, "collection");
        f = ttc;
        wr32(f, 16, size + 64);
        add("ttcf face directory past the end of the file", f, READER, 1);
        add("ttcf face directory past the end of the file, the other face", f, OK, 0, "collection");
        f = ttc;
        wr32(f, 16, size - 2);
        add("ttcf face directory in the last two bytes", f, READER, 1);
        f = ttc;
        wr32(f, 16, 0xFFFFFFFFu);
        add("ttcf face directory at 0xFFFFFFFF", f, READER, 1);
        f = ttc;
        wr16(f, face1 + 4, 0xFFFF);
        add("ttcf face with 65,535 tables stated", f, READER, 1);
        f = ttc;
        wr32(f, face1, tag("ttcf"));
        add("ttcf face whose version is ttcf", f, READER, 1);
        f = ttc;
        wr32(f, face1, tag("wOF2"));
        add("ttcf face whose version is wOF2", f, READER, 1);
        // Two directories and one set of tables, as real collections share them.
        f = collection({base.build(), base.build()});
        const uint32_t second = rd32(f, 16), apart = second - rd32(f, 12);
        for (size_t t = 0; t < rd16(f, second + 4); ++t)
            wr32(f, second + 12 + t * 16 + 8, rd32(f, second + 12 + t * 16 + 8) - apart);
        add("ttcf two directories naming the same tables, face 0", f, OK, 0, "collection");
        add("ttcf two directories naming the same tables, face 1", f, OK, 1);
    }
    // --- CID-keyed CFF
    {
        CidCff cid = two_dict_cid();
        cid.fd_select = fdselect0({0, 0, 1, 0, 1});
        add("CID-keyed CFF, FDSelect format 0", cid_font(cid).build(), OK, 0, "cid");
        const Bytes ranges = fdselect3({{0, 0}, {2, 1}, {3, 0}, {4, 1}}, 5);
        cid.fd_select = ranges;
        add("CID-keyed CFF, FDSelect format 3", cid_font(cid).build(), OK, 0, "cid");
        cid.share_subrs = true;
        add("CID-keyed CFF, Font DICTs sharing one Subrs INDEX", cid_font(cid).build(), OK, 0,
            "cid");
        cid.share_subrs = false;
        cid.cids.clear();
        add("CID-keyed CFF without a charset", cid_font(cid).build(), OK, 0, "cid");
        add("CID-keyed CFF in a collection",
            collection({cid_font(cid).build(), three_squares().build()}), OK, 0, "collection");

        cid = two_dict_cid();
        cid.fd_select = fdselect0({0, 0, 5, 0, 1});
        add("FDSelect format 0 naming a Font DICT that is not there", cid_font(cid).build(),
            STRICT);
        cid.fd_select = fdselect0({0, 0, 255, 0, 1});
        add("FDSelect format 0 naming Font DICT 255", cid_font(cid).build(), STRICT);
        cid.fd_select = fdselect3({{0, 0}, {4, 2}}, 5);
        add("FDSelect format 3 naming a Font DICT that is not there", cid_font(cid).build(),
            STRICT);
        cid.fd_select = fdselect3({{0, 0}, {2, 1}}, 3);
        add("FDSelect format 3 closing before the last glyph", cid_font(cid).build(), STRICT);
        cid.fd_select = fdselect3({{1, 0}, {2, 1}}, 5);
        add("FDSelect format 3 not starting at glyph 0", cid_font(cid).build(), READER);
        cid.fd_select = fdselect3({{0, 0}, {3, 1}, {2, 0}}, 5);
        add("FDSelect format 3 ranges going backwards", cid_font(cid).build(), READER);
        cid.fd_select = fdselect3({}, 5);
        add("FDSelect format 3 with no ranges", cid_font(cid).build(), READER);
        cid.fd_select = Bytes{7, 0, 0, 0, 0, 0, 0, 0};
        add("FDSelect format 7", cid_font(cid).build(), READER);
        {
            // 65,535 ranges of one glyph each, in order: nothing wrong with
            // it but its size.
            std::vector<std::array<uint32_t, 2>> many;
            for (uint32_t g = 0; g < 65535; ++g)
                many.push_back({g, g % 2});
            cid.fd_select = fdselect3(many, 65535);
            add("FDSelect format 3 with 65,535 ranges", cid_font(cid).build(), OK);
        }
        cid.fd_select = ranges;
        cid.with_fd_select = false;
        add("CID-keyed CFF without an FDSelect", cid_font(cid).build(), READER);
        cid.with_fd_select = true;
        cid.with_fd_array = false;
        add("CID-keyed CFF without an FDArray", cid_font(cid).build(), READER);
        cid.with_fd_array = true;
        cid.fd_array_override = cff_index({});
        add("CID-keyed CFF with an empty FDArray", cid_font(cid).build(), READER);
        {
            // The last Font DICT's end moved to the end of the table, so the
            // DICT runs on over the Private DICTs and both Subrs INDEXes.
            CidCff spans = two_dict_cid();
            spans.fd_select = ranges;
            MiniFont f = cid_font(spans);
            const size_t last = spans.fd_array_at + 3 + 2 * 4;
            wr32(f.cff, last, static_cast<uint32_t>(f.cff.size() - (spans.fd_array_at + 14)));
            add("Font DICT INDEX whose last DICT lies over what follows it", f.build(), STRICT);
            // And the first DICT's end moved back to its start: no DICT at all.
            f = cid_font(spans);
            // The second is then both DICTs end to end, and names a Private
            // DICT twice: which of the two a reader takes is its own choice.
            wr32(f.cff, spans.fd_array_at + 3 + 4, 1);
            add("Font DICT INDEX whose first DICT is empty", f.build(), STRICT);
        }
        add("256 Font DICTs naming one Subrs INDEX of 65,535",
            cid_font(dicts_sharing_subrs(256, 65535)).build(), OK);
        add("257 Font DICTs", cid_font(dicts_sharing_subrs(257, 4)).build(), READER);
        add("256 Font DICTs naming 256 Subrs INDEXes that overlap", overlapping_subrs_font(),
            STRICT);
    }
    // --- the Type 2 operators past the drawing ones
    {
        add("CFF glyphs computed with the arithmetic operators", cff_font(arithmetic_cff()).build(),
            OK, 0, "type2");
        MiniCff cff = arithmetic_cff();
        cff.charstrings[1] = cs_asm("0 0 rmoveto 50 hlineto 50 vlineto random endchar");
        cff.charstrings[2] = cs_asm("0 0 rmoveto 50 hlineto 5 0 div endchar");
        cff.charstrings[3] = cs_asm("0 0 rmoveto 50 hlineto 1 2 3 5 0 roll endchar");
        cff.charstrings[4] = cs_asm("0 0 rmoveto 50 hlineto 1 32 put -4 sqrt endchar");
        add("CFF glyphs that misuse the arithmetic operators", cff_font(cff).build(), OK, 0,
            "type2");
        cff = arithmetic_cff();
        Bytes dups = cs_asm("0 0 rmoveto 50 hlineto 1");
        for (int i = 0; i < 400; ++i)
            cs_esc(dups, 27);
        cs_op(dups, 14);
        cff.charstrings[1] = dups;
        add("CFF glyph that fills the stack with dup", cff_font(cff).build(), OK, 0, "type2");
        add("CFF subroutines that roll, index and dup a million times",
            cff_font(cff_stack_bomb()).build(), STRICT);
    }
    // --- the kern table
    {
        const MiniFont six = six_squares();
        add("kern table of three subtables",
            mini_with(six, tag("kern"),
                      kern_table({kern_subtable({{1, 2, -50}, {2, 3, 30}}),
                                  kern_subtable({{1, 2, -20}, {4, 5, 9}}, 0x09),
                                  kern_subtable({{1, 2, 7}}, 0x05),
                                  kern_subtable({{2, 3, -4}, {2, 3, 6}, {3, 4, -8}})})),
            OK, 0, "kern");
        {
            Sfnt sfnt = six.sfnt();
            sfnt.table(tag("kern")) = kern_table({kern_subtable({{1, 2, -50}, {2, 3, 30}})});
            sfnt.table(tag("GPOS")) = gpos_assemble({0}, {pairpos1(1, 2, -70)});
            add("kern table beside GPOS pair kerning", sfnt.build(), OK, 0, "kern");
        }
        add("kern table of 65,535 subtables of one pair each",
            mini_with(six, tag("kern"), kern_of_one_pair_subtables(65535, false)), OK);
        // 'A' 'B' kerned by 122,880,000 units: the reader keeps what the table
        // says, and the metrics hold the length it becomes to MAX_DIMEN.
        add("kern table of 4,096 subtables kerning one pair by 30,000 each",
            mini_with(six, tag("kern"), kern_of_one_pair_subtables(4096, true, 30000)), OK);
        Bytes claims = kern_table({kern_subtable({{1, 2, -50}})});
        wr16(claims, 4 + 6, 0xFFFF);
        add("kern subtable claiming 65,535 pairs in six bytes", mini_with(six, tag("kern"), claims),
            OK, 0, "kern");
        Bytes apple;
        put32(apple, 0x00010000u);
        put32(apple, 1);
        apple.resize(40, 0);
        add("kern table in Apple's form", mini_with(six, tag("kern"), apple), OK, 0, "kern");
    }
    // --- post names and minting
    {
        add("post format 2 names over a MATH table", named_math_font(), OK, 0, "post");
        {
            Sfnt sfnt = mini_math().sfnt();
            sfnt.table(tag("MATH")) = math_table(1, {2, 3, 4, 5, 6});
            Bytes post(40, 0);
            wr32(post, 0, 0x00020000u);
            wr16(post, 32, 0xFFFF);
            sfnt.table(tag("post")) = post;
            add("post format 2 stating 65,535 glyphs in 40 bytes", sfnt.build(), OK, 0, "post");
            sfnt.table(tag("post")) = post_table(0x00010000u);
            add("post format 1 over a MATH table", sfnt.build(), OK, 0, "post");
        }
        add("post format 2 with 60,000 glyphs of one name and its numbered names taken",
            shared_name_font(60000, 5000), OK);
    }
    // --- cmap subtables past format 4 and 12, and the encodings past Unicode
    {
        const MiniFont six = six_squares();
        const auto cmapped = [&](const std::vector<CmapRecord>& records) {
            return mini_with(six, tag("cmap"), cmap_table(records));
        };
        add("cmap format 0",
            cmapped({{3, 1, cmap_sub0({{'A', 1}, {'B', 2}, {'C', 0}, {0xFF, 5}})}}), OK, 0, "cmap");
        add("cmap format 6", cmapped({{3, 1, cmap_sub6('A', {1, 2, 0, 4, 5})}}), OK, 0, "cmap");
        add("cmap format 10", cmapped({{3, 10, cmap_sub10('A', {1, 2, 0, 4, 5})}}), OK, 0, "cmap");
        add("cmap format 10 above the BMP", cmapped({{3, 10, cmap_sub10(0x1F600, {1, 2, 0, 4})}}),
            OK, 0, "cmap");
        add("cmap format 13",
            cmapped({{0, 6, cmap_sub12or13(13, {{'A', 'Z', 1}, {0x1F600, 0x1F60F, 2}})}}), OK, 0,
            "cmap");
        add("cmap format 12 and 13 with groups that overlap and replace",
            cmapped(
                {{3, 10,
                  cmap_sub12or13(12, {{'A', 'E', 1}, {'E', 'G', 2}, {'C', 'Z', 3}, {'P', 'P', 0}})},
                 {0, 6, cmap_sub12or13(13, {{'A', 'E', 1}, {'E', 'G', 2}})}}),
            OK, 0, "cmap");
        add("cmap of every Unicode pair, each in another format",
            cmapped({{0, 0, cmap_sub0({{'A', 1}})},
                     {0, 3, cmap_sub4({{'A', 'B', 1}})},
                     {0, 4, cmap_sub12or13(12, {{'A', 'C', 1}})},
                     {0, 6, cmap_sub12or13(13, {{'A', 'D', 1}})},
                     {1, 0, cmap_sub6('A', {1, 2})},
                     {3, 0, cmap_sub4({{0xF041, 0xF045, 1}})},
                     {3, 1, cmap_sub6('A', {1, 2, 3})},
                     {3, 10, cmap_sub10('A', {1, 2, 3, 4, 5})}}),
            OK, 0, "cmap");
        add("cmap whose preferred subtables are in formats 2, 8 and 14",
            cmapped({{0, 4, cmap_sub_unread(2)},
                     {0, 6, cmap_sub_unread(14)},
                     {3, 1, cmap_sub4({{'A', 'E', 1}})},
                     {3, 10, cmap_sub_unread(8)}}),
            OK, 0, "cmap");
        add("cmap with a Symbol subtable only", cmapped({{3, 0, cmap_sub4({{0xF041, 0xF045, 1}})}}),
            OK, 0, "cmap");
        add("cmap with a Macintosh Roman format 0 subtable only",
            cmapped({{1, 0, cmap_sub0({{'A', 1}, {0x8A, 2}, {0xA5, 3}, {0xDB, 4}})}}), OK, 0,
            "cmap");
        add("cmap with a Macintosh Roman format 6 subtable only",
            cmapped({{1, 0, cmap_sub6(0x80, {1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5})}}), OK, 0, "cmap");
        add("cmap with subtables in formats 2, 8 and 14 only",
            cmapped({{0, 4, cmap_sub_unread(2)},
                     {3, 1, cmap_sub_unread(8)},
                     {3, 10, cmap_sub_unread(14)}}),
            READER);
        add("cmap with a Macintosh Roman format 12 subtable only",
            cmapped({{1, 0, cmap_sub12or13(12, {{'A', 'E', 1}})}}), READER);
        add("cmap format 13 of one group over every codepoint", whole_code_space_format13(), OK, 0,
            NOT_A_SEED);
        add("cmap format 12 of 100,000 groups at one code", same_code_format12(100000), OK);
        Bytes lying = cmap_table({{3, 10, cmap_sub10('A', {1, 2})}});
        wr32(lying, 12 + 16, 0x7FFFFFFFu);
        add("cmap format 10 entry count absurd", mini_with(six, tag("cmap"), lying), READER);
        lying = cmap_table({{3, 1, cmap_sub6('A', {1, 2})}});
        wr16(lying, 12 + 8, 0xFFFF);
        add("cmap format 6 entry count absurd", mini_with(six, tag("cmap"), lying), READER);
        lying = cmap_table({{0, 6, cmap_sub12or13(13, {{'A', 'E', 1}})}});
        wr32(lying, 12 + 12, 0x7FFFFFFFu);
        add("cmap format 13 group count absurd", mini_with(six, tag("cmap"), lying), READER);
        lying = cmap_table({{3, 1, cmap_sub0({{'A', 1}})}});
        lying.resize(lying.size() - 1);
        add("cmap format 0 a byte short", mini_with(six, tag("cmap"), lying), READER);
    }
    // --- hmtx and loca as real fonts have them wrong
    {
        add("hhea numberOfHMetrics 0", patched(base, tag("hhea"), {{34, 0, 2}}), STRICT);
        Sfnt sfnt = base.sfnt();
        sfnt.table(tag("loca")).resize(12);
        add("loca without the entry after the last glyph's", sfnt.build(), STRICT);
    }
    return cases;
}

} // namespace

// --- S1, S4, S6: GPOS --------------------------------------------------------------------

// S1. One lookup, 256 subtable offsets, one subtable. Read as 256 it is half
// a gigabyte and seven seconds under the sanitisers; it is one.
TEST(s1_gpos_subtables_that_share_one_offset_are_read_once)
{
    const Bytes gpos = gpos_class_bomb(256, true, 1000, 1000);
    const Bytes file = with_table("Roboto-Regular.ttf", tag("GPOS"), gpos);
    REQUIRE(!file.empty());
    const double rss_before = peak_rss_mb();
    const auto start = std::chrono::steady_clock::now();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.kern_subtable_count(), size_t(1));
    CHECK_EQ(font.kern_subtable_format(0), 2);
    CHECK(font.kern_storage_bytes() <= gpos.size() + size_t(font.glyph_count()) * 5);
    // The one subtable still answers: glyph 36 is covered, and the first cell
    // of the matrix is the bytes the coverage table also occupies.
    CHECK_EQ(font.pair_kern(36, 40), 1);
    std::printf("stress_font S1: a %zu byte GPOS with 256 offsets to one format 2 subtable: %zu "
                "subtable kept in %zu bytes, %.2f s, peak RSS +%.0f MB\n",
                gpos.size(), font.kern_subtable_count(), font.kern_storage_bytes(),
                seconds_since(start), peak_rss_mb() - rss_before);
}

// S1. The same with 1,024 subtables that are distinct and lie over one
// another: what is kept is bounded by the table's size, and the rest is
// dropped with a warning.
TEST(s1_gpos_subtables_that_overlap_are_bounded_by_the_table)
{
    const Bytes gpos = gpos_class_bomb(1024, false, 700, 700);
    const Bytes file = with_table("Roboto-Regular.ttf", tag("GPOS"), gpos);
    REQUIRE(!file.empty());
    const auto start = std::chrono::steady_clock::now();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK(font.kern_subtable_count() >= 1);
    CHECK(font.kern_subtable_count() < 16);
    CHECK(font.kern_storage_bytes() <=
          8 * gpos.size() + 8192 + font.kern_subtable_count() * size_t(font.glyph_count()) * 5);
    CHECK(warned(font.warnings(), "more than the table could hold"));
    std::printf("stress_font S1: a %zu byte GPOS with 1024 overlapping format 2 subtables: %zu "
                "kept in %zu bytes, %.2f s\n",
                gpos.size(), font.kern_subtable_count(), font.kern_storage_bytes(),
                seconds_since(start));
}

// S4. class1Count * class2Count * record size passes 32 bits. On a 64-bit
// size_t the product never wrapped, so this cannot fail here; it is computed
// in uint64_t whatever size_t is, and the subtable is refused for not fitting.
TEST(s4_gpos_class_counts_whose_product_passes_32_bits_are_refused)
{
    Bytes gpos = gpos_class_bomb(1, true, 4, 4);
    // The class counts are the last two fields of the 16-byte header.
    const size_t header = gpos.size() - 12 - 4 * 4 * 2 - 16;
    wr16(gpos, header + 4, 0xFFFF); // every value-format bit: a 16-byte record
    wr16(gpos, header + 12, 0xFFFF);
    wr16(gpos, header + 14, 0xFFFF);
    const Bytes file = with_table("Roboto-Regular.ttf", tag("GPOS"), gpos);
    REQUIRE(!file.empty());
    OpenTypeFont font;
    REQUIRE(font.load(file.data(), file.size()));
    CHECK_EQ(font.kern_subtable_count(), size_t(0));
    CHECK(warned(font.warnings(), "pair subtable is malformed"));
}

// S6. The cap on lookups counts lookups, not the entries of a feature's list.
TEST(s6_the_kern_lookup_cap_counts_distinct_lookups_and_warns_when_it_cuts)
{
    Bytes original;
    REQUIRE(read_font("Roboto-Regular.ttf", original));
    OpenTypeFont plain;
    REQUIRE(plain.load(original.data(), original.size()));
    uint32_t a = 0, v = 0, t = 0;
    REQUIRE(plain.lookup('A', &a) && plain.lookup('V', &v) && plain.lookup('T', &t));

    // Lookup 0 five thousand times, then lookup 1.
    std::vector<uint32_t> named(5000, 0);
    named.push_back(1);
    Bytes file = with_table("Roboto-Regular.ttf", tag("GPOS"),
                            gpos_assemble(named, {pairpos1(a, v, -70), pairpos1(a, t, -90)}));
    OpenTypeFont font;
    REQUIRE(font.load(file.data(), file.size()));
    CHECK_EQ(font.kern_subtable_count(), size_t(2));
    CHECK_EQ(font.pair_kern(a, v), -70);
    CHECK_EQ(font.pair_kern(a, t), -90);
    CHECK(!warned(font.warnings(), "more lookups than are read"));

    // Five thousand distinct lookups named: the first 4,096 are read, and the
    // font says the rest were not.
    named.clear();
    for (uint32_t i = 0; i < 5000; ++i)
        named.push_back(i);
    file = with_table("Roboto-Regular.ttf", tag("GPOS"),
                      gpos_assemble(named, {pairpos1(a, v, -70), pairpos1(a, t, -90)}));
    OpenTypeFont cut;
    REQUIRE(cut.load(file.data(), file.size()));
    CHECK_EQ(cut.pair_kern(a, v), -70);
    CHECK(warned(cut.warnings(), "more lookups than are read"));
}

// --- The formats past the bundled fonts': what the hostile shapes cost -------------------------

// A format 13 group gives one glyph to every codepoint in it. It is kept as
// one range from the reader to the font the atlas is given, so a group over
// the whole code space, which is what the Unicode Last Resort font is made
// of, costs what the 28 bytes it is written in cost. What it costs is printed.
TEST(n1_a_format_13_cmap_over_the_whole_code_space_stays_one_range)
{
    const Bytes file = whole_code_space_format13();
    const double rss_before = peak_rss_mb();
    auto start = std::chrono::steady_clock::now();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    const double load_seconds = seconds_since(start);
    const double load_rss = peak_rss_mb() - rss_before;
    REQUIRE_EQ(font.cmap().size(), size_t(1));
    CHECK(font.cmap()[0].one_glyph);
    CHECK_EQ(font.cmap()[0].first, 0u);
    CHECK_EQ(font.cmap()[0].last, 0x10FFFFu);
    uint32_t glyph = 0;
    CHECK(font.lookup(0, &glyph) && glyph == 1);
    CHECK(font.lookup(0x8000, &glyph) && glyph == 1);
    CHECK(font.lookup(0x10FFFF, &glyph) && glyph == 1);
    CHECK(!font.lookup(0x110000, nullptr));

    start = std::chrono::steady_clock::now();
    FontLibrary library;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "lastresort", &why));
    const double register_seconds = seconds_since(start);
    const FaceData* face = library.face_data(Face::PROSE);
    start = std::chrono::steady_clock::now();
    Bytes built;
    REQUIRE(build_atlas_font(*face, 32, built, &why));
    const double build_seconds = seconds_since(start);
    const double all_rss = peak_rss_mb() - rss_before;
    std::printf("stress_font N1: a %zu byte font whose cmap is one format 13 group over "
                "U+0000..U+10FFFF: %zu ranges; load %.3f s, peak RSS +%.0f MB; register_font "
                "%.3f s; build_atlas_font %.3f s, a %zu byte font for the atlas; peak RSS +%.0f "
                "MB in all\n",
                file.size(), font.cmap().size(), load_seconds, load_rss, register_seconds,
                build_seconds, built.size(), all_rss);
    // Generous enough for the sanitiser build; what is measured is above.
    CHECK(load_seconds < 10.0 && register_seconds < 10.0 && build_seconds < 10.0);
    CHECK(all_rss < 768.0);
    // The font for the atlas is the file with a cmap of one group, and maps
    // what the file maps.
    CHECK(built.size() <= file.size() + 256);
    OpenTypeFont rebuilt;
    REQUIRE(rebuilt.load(built.data(), built.size(), &why));
    CHECK_EQ(rebuilt.cmap_format(), 13);
    REQUIRE_EQ(rebuilt.cmap().size(), size_t(1));
    CHECK(rebuilt.lookup(0, &glyph) && glyph == 1);
    CHECK(rebuilt.lookup(0x10FFFF, &glyph) && glyph == 1);

    // 100,000 format 12 groups that each replace the one before: one range.
    const Bytes same = same_code_format12(100000);
    start = std::chrono::steady_clock::now();
    REQUIRE(font.load(same.data(), same.size(), &why));
    REQUIRE_EQ(font.cmap().size(), size_t(1));
    CHECK_EQ(font.cmap()[0].first, uint32_t('A'));
    CHECK_EQ(font.cmap()[0].last, uint32_t('A'));
    CHECK_EQ(font.cmap()[0].glyph, 1u + (99999u % 5u));
    std::printf("stress_font N1: a format 12 cmap of 100,000 groups at one code: %.3f s\n",
                seconds_since(start));
    CHECK(seconds_since(start) < 5.0);
}

// 256 Font DICTs, the most FDSelect can name, whose Private DICTs all name
// one Subrs INDEX of 65,535 subroutines: read as 256 it would be 256 copies
// of 65,536 offsets, 64 MB; it is one.
TEST(n2_font_dicts_that_name_one_subrs_index_have_it_read_once)
{
    const Bytes file = cid_font(dicts_sharing_subrs(256, 65535)).build();
    const double rss_before = peak_rss_mb();
    const auto start = std::chrono::steady_clock::now();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    const double load_seconds = seconds_since(start);
    const double rss = peak_rss_mb() - rss_before;
    CHECK(font.is_cid_keyed());
    // Each glyph, whichever Font DICT it is in, draws subroutine 0's square.
    for (uint32_t g = 1; g < 5; ++g) {
        const GlyphBounds box = font.bounds(g);
        CHECK(box == (GlyphBounds{10, 20, 110, 120}));
    }
    CHECK(font.warnings().empty());
    if (!font.strict_check(&why))
        FAIL("strict: %s", why.c_str());
    std::printf("stress_font N2: a %zu byte CID-keyed font, 256 Font DICTs naming one Subrs "
                "INDEX of 65,535: load %.3f s, peak RSS +%.1f MB\n",
                file.size(), load_seconds, rss);
    CHECK(load_seconds < 5.0);
    CHECK(rss < 16.0);

    // 256 INDEXes that are different and lie over one another: kept while
    // they add up to no more than the table, the rest dropped, and the font
    // is not one for a rasteriser.
    const Bytes overlapping = overlapping_subrs_font();
    REQUIRE(font.load(overlapping.data(), overlapping.size(), &why));
    CHECK(warned(font.warnings(), "local subroutine indexes overlap"));
    why.clear();
    CHECK(!font.strict_check(&why));
    CHECK(why.find("overlap") != std::string::npos);

    why.clear();
    const Bytes too_many = cid_font(dicts_sharing_subrs(257, 4)).build();
    CHECK(!font.load(too_many.data(), too_many.size(), &why));
    CHECK(why.find("more Font DICTs") != std::string::npos);
}

// The kern table states its subtable count in 16 bits. 65,535 subtables of
// one pair each are sorted once, not once a subtable.
TEST(n3_a_kern_table_of_65535_subtables_is_read_in_one_pass)
{
    const MiniFont six = six_squares();
    {
        const Bytes file = mini_with(six, tag("kern"), kern_of_one_pair_subtables(65535, false));
        const auto start = std::chrono::steady_clock::now();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        const double seconds = seconds_since(start);
        // Subtable k kerns (1 + k / 251, k % 251) by 3.
        CHECK_EQ(font.legacy_kern_keys().size(), size_t(65535));
        CHECK_EQ(font.pair_kern(1, 0), 3);
        CHECK_EQ(font.pair_kern(1 + 65534 / 251, 65534 % 251), 3);
        CHECK_EQ(font.pair_kern(1 + 65535 / 251, 65535 % 251), 0);
        CHECK(font.warnings().empty());
        // A quarter as many, for how the cost grows: four times for work in
        // proportion, sixteen for work that goes over what was read so far
        // again with every subtable.
        const Bytes quarter = mini_with(six, tag("kern"), kern_of_one_pair_subtables(16384, false));
        const auto quarter_start = std::chrono::steady_clock::now();
        OpenTypeFont small;
        REQUIRE(small.load(quarter.data(), quarter.size(), &why));
        const double quarter_seconds = seconds_since(quarter_start);
        CHECK_EQ(small.legacy_kern_keys().size(), size_t(16384));
        std::printf("stress_font N3: a %zu byte kern table of 65,535 subtables of one pair each: "
                    "load %.3f s; of 16,384: %.3f s (%.1f times less)\n",
                    file.size() - six.build().size(), seconds, quarter_seconds,
                    seconds / std::max(quarter_seconds, 1e-9));
        CHECK(seconds < 5.0);
        // Below a few milliseconds the ratio is noise.
        if (seconds > 0.02)
            CHECK(seconds < 8.0 * quarter_seconds);
    }
    {
        // 16,384 of them kerning one pair by 2,000: they add up, far past
        // the 16 bits one value has.
        const Bytes file =
            mini_with(six, tag("kern"), kern_of_one_pair_subtables(16384, true, 2000));
        const auto start = std::chrono::steady_clock::now();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        const double seconds = seconds_since(start);
        REQUIRE_EQ(font.legacy_kern_keys().size(), size_t(1));
        CHECK_EQ(font.pair_kern(1, 2), 16384 * 2000);
        CHECK_EQ(font.pair_kern_accumulated(1, 2), 16384 * 2000);
        std::printf("stress_font N3: 16,384 kern subtables kerning one pair by 2,000: load %.3f "
                    "s; the pair's kern is %d units\n",
                    seconds, font.pair_kern(1, 2));
        CHECK(seconds < 5.0);
        // As a length it is held to the layout core's bound, in either
        // direction, at a size where the sum is far past it.
        for (const int each : {2000, -2000}) {
            CHECK_CONTEXT("each subtable kerns by %d", each);
            const Bytes summed =
                mini_with(six, tag("kern"), kern_of_one_pair_subtables(16384, true, each));
            FontLibrary library;
            REQUIRE(library.register_font(Face::PROSE, summed.data(), summed.size(), "sum", &why));
            const FaceData* face = library.face_data(Face::PROSE);
            REQUIRE(face != nullptr);
            uint32_t left = 0, right = 0;
            for (const CmapRange& range : face->font().cmap())
                for (uint32_t cp = range.first; cp <= range.last && cp < 0x10000u; ++cp) {
                    left = range.glyph_of(cp) == 1 ? cp : left;
                    right = range.glyph_of(cp) == 2 ? cp : right;
                }
            REQUIRE(left != 0 && right != 0);
            const double size = static_cast<double>(sp_from_pt(12.0));
            const FontMetrics* metrics = library.metrics(Face::PROSE, size);
            REQUIRE(metrics != nullptr);
            const double unbounded = 16384.0 * each * (size / face->font().units_per_em());
            CHECK(std::fabs(unbounded) > static_cast<double>(MAX_DIMEN));
            CHECK_EQ(metrics->kern(left, right),
                     each > 0 ? static_cast<double>(MAX_DIMEN) : -static_cast<double>(MAX_DIMEN));
            // A pair with no kern is still exactly none.
            CHECK_EQ(metrics->kern(right, left), 0.0);
        }
    }
    {
        // One subtable that states 65,535 pairs and has one: not read, and
        // the table says it was cut.
        Bytes kern = kern_table({kern_subtable({{1, 2, -50}})});
        REQUIRE_EQ(kern.size(), size_t(24));
        wr16(kern, 4 + 6, 0xFFFF);
        const Bytes file = mini_with(six, tag("kern"), kern);
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK(font.legacy_kern_keys().empty());
        CHECK_EQ(font.pair_kern(1, 2), 0);
        CHECK(warned(font.warnings(), "kern table is truncated"));
    }
}

// 60,000 glyphs named "x", and the names a duplicate "x" is given first,
// "x.1" to "x.5000", taken by other glyphs. Each duplicate has to count past
// the taken names; where it got to is remembered, so that is done once.
TEST(n4_post_names_met_again_are_numbered_without_starting_over)
{
    const uint32_t shared = 60000, numbered = 5000;
    const Bytes file = shared_name_font(shared, numbered);
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    auto start = std::chrono::steady_clock::now();
    std::vector<std::string> names;
    REQUIRE(font.glyph_order_names(names));
    const double seconds = seconds_since(start);
    REQUIRE_EQ(names.size(), size_t(2 + shared + numbered));
    CHECK_EQ(names[0], ".notdef");
    CHECK_EQ(names[1], "A");
    CHECK_EQ(names[2], "x");
    // The first duplicate is the first number not taken, and they run on.
    CHECK_EQ(names[3], "x.5001");
    CHECK_EQ(names[4], "x.5002");
    CHECK_EQ(names[2 + shared - 1], "x.64999");
    // The glyphs that own the numbered names keep them.
    CHECK_EQ(names[2 + shared], "x.1");
    CHECK_EQ(names[2 + shared + numbered - 1], "x.5000");

    start = std::chrono::steady_clock::now();
    FontLibrary library;
    REQUIRE(library.register_font(Face::MATH, file.data(), file.size(), "names", &why));
    const double register_seconds = seconds_since(start);
    // Glyphs 2..6 are the variants: "x", "x.5001", .., "x.5004", in that order.
    const std::vector<SyntheticCodepoint>& minted =
        library.face_data(Face::MATH)->synthetic_codepoints();
    REQUIRE_EQ(minted.size(), size_t(5));
    for (uint32_t i = 0; i < 5; ++i)
        CHECK_EQ(minted[i].glyph, 2 + i);
    std::printf("stress_font N4: post format 2, %u glyphs named \"x\" and \"x.1\"..\"x.%u\" "
                "taken: glyph_order_names %.3f s; register_font (which mints by name) %.3f s\n",
                shared, numbered, seconds, register_seconds);
    CHECK(seconds < 5.0);
    CHECK(register_seconds < 10.0);

    // A glyph count the table is too short for: no names, and glyph order.
    Sfnt sfnt = mini_math().sfnt();
    sfnt.table(tag("MATH")) = math_table(1, {2, 3, 4, 5, 6});
    Bytes post(40, 0);
    wr32(post, 0, 0x00020000u);
    wr16(post, 32, 0xFFFF);
    sfnt.table(tag("post")) = post;
    const Bytes lying = sfnt.build();
    REQUIRE(font.load(lying.data(), lying.size(), &why));
    CHECK(!font.glyph_order_names(names));
    CHECK(names.empty());
}

// The stack operators draw nothing, so only the count of operators stops a
// charstring that does nothing else: subroutines ten deep, five calls each,
// to a leaf that rolls, indexes, dups and drops.
TEST(n5_cff_stack_work_is_stopped_by_the_work_budget)
{
    const Bytes file = cff_font(cff_stack_bomb()).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    auto start = std::chrono::steady_clock::now();
    const GlyphBounds box = font.bounds(1);
    const double seconds = seconds_since(start);
    CHECK(box == GlyphBounds{});
    CHECK(warned(font.warnings(), "malformed or unsupported"));
    start = std::chrono::steady_clock::now();
    why.clear();
    CHECK(!font.strict_check(&why));
    CHECK(why.find("more work") != std::string::npos);
    std::printf("stress_font N5: a %zu byte CFF font whose glyph does stack work in a loop of "
                "subroutines: ink box %.3f s, strict pass %.3f s\n",
                file.size(), seconds, seconds_since(start));
    CHECK(seconds < 5.0);

    // dup until the stack is full: that glyph has no box, and the font is fine.
    MiniCff cff = arithmetic_cff();
    Bytes dups = cs_asm("0 0 rmoveto 50 hlineto 1");
    for (int i = 0; i < 400; ++i)
        cs_esc(dups, 27);
    cs_op(dups, 14);
    cff.charstrings[1] = dups;
    const Bytes full = cff_font(cff).build();
    REQUIRE(font.load(full.data(), full.size(), &why));
    CHECK(font.bounds(1) == GlyphBounds{});
    CHECK(font.bounds(2).x_max > 0.0);
    CHECK(font.strict_check(&why));
}

// --- S3: the directory ----------------------------------------------------------------------

namespace
{

// Roboto with `extra` more directory records, made-up tags or one tag
// repeated, each covering the whole file.
Bytes with_overlapping_records(size_t extra, bool same_tag)
{
    Bytes file;
    Sfnt sfnt;
    if (!read_font("Roboto-Regular.ttf", file) || !parse_sfnt(file, sfnt))
        return {};
    const Bytes real = sfnt.build();
    const size_t old_count = rd16(real, 4);
    Bytes out(real.begin(), real.begin() + 12);
    wr16(out, 4, static_cast<uint32_t>(old_count + extra));
    const size_t shift = extra * 16;
    for (size_t i = 0; i < old_count; ++i) {
        put32(out, rd32(real, 12 + i * 16));
        put32(out, 0);
        put32(out, static_cast<uint32_t>(rd32(real, 12 + i * 16 + 8) + shift));
        put32(out, rd32(real, 12 + i * 16 + 12));
    }
    const size_t total = real.size() + shift;
    for (size_t i = 0; i < extra; ++i) {
        put32(out, same_tag ? tag("zzzz") : 0x7A000000u + static_cast<uint32_t>(i));
        put32(out, 0);
        put32(out, 0);
        put32(out, static_cast<uint32_t>(total));
    }
    out.insert(out.end(), real.begin() + static_cast<std::ptrdiff_t>(12 + old_count * 16),
               real.end());
    return out;
}

} // namespace

// S3. A thousand records that each cover the whole file made the atlas copy a
// thousand times the file (184 MB from 184 KB); eleven thousand make 2 GiB.
TEST(s3_overlapping_directory_records_cannot_inflate_the_atlas_copy)
{
    for (const size_t extra : {size_t(1), size_t(1000), size_t(11500)}) {
        CHECK_CONTEXT("%zu extra records", extra);
        const Bytes file = with_overlapping_records(extra, false);
        REQUIRE(!file.empty());
        FontLibrary library;
        std::string why;
        // Good for metrics: the tables layout reads are all there.
        REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "s3", &why));
        const FaceData* face = library.face_data(Face::PROSE);
        Bytes built;
        why.clear();
        CHECK(!build_atlas_font(*face, 32, built, &why));
        CHECK(why.find("overlap") != std::string::npos);
        CHECK(built.empty());
        why.clear();
        CHECK(!face->font().strict_check(&why));
        CHECK(why.find("overlap") != std::string::npos);
    }
}

TEST(s3_a_table_named_twice_is_refused_by_the_reader)
{
    const Bytes file = with_overlapping_records(2, true);
    REQUIRE(!file.empty());
    OpenTypeFont font;
    std::string why;
    CHECK(!font.load(file.data(), file.size(), &why));
    CHECK(why.find("names a table twice") != std::string::npos);

    // A second head, differing from the first: whichever a reader took, another
    // could take the other.
    Bytes original;
    Sfnt sfnt;
    REQUIRE(read_font("Roboto-Regular.ttf", original) && parse_sfnt(original, sfnt));
    SfntTable second = *sfnt.find(tag("head"));
    wr16(second.data, 18, 4096);
    sfnt.tables.push_back(second);
    const Bytes twice = sfnt.build();
    FontLibrary library;
    why.clear();
    CHECK(!library.register_font(Face::PROSE, twice.data(), twice.size(), "twice", &why));
    CHECK(why.find("names a table twice") != std::string::npos);
}

// --- S5: CFF work ----------------------------------------------------------------------------

// S5. A 768-byte font whose one glyph ran a million operators of sixteen
// curves each before the budget stopped it. The budget counts the segments.
TEST(s5_cff_work_is_counted_in_segments_not_operators)
{
    const Bytes file = cff_font(cff_work_bomb()).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    const auto start = std::chrono::steady_clock::now();
    const GlyphBounds box = font.bounds(1);
    const double first = seconds_since(start);
    // Stopped by the budget: no box, and a warning.
    CHECK_EQ(box.x_max, 0.0);
    CHECK(warned(font.warnings(), "malformed or unsupported"));
    // And asked again it is remembered, not run again.
    const auto again = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
        font.bounds(1);
    CHECK(seconds_since(again) < first + 0.05);
    why.clear();
    CHECK(!font.strict_check(&why));
    CHECK(why.find("more work") != std::string::npos);
    std::printf("stress_font S5: a %zu byte CFF font, one hostile glyph's ink box: %.3f s\n",
                file.size(), first);
}

// --- S7: stand-ins ---------------------------------------------------------------------------

namespace
{

// The mini math font (five minted codepoints) with ten supplementary
// codepoints of its own and all of U+E000..U+F8FF taken but `free_slots`.
Bytes pua_filling_font(uint32_t free_slots)
{
    MiniFont f = mini_math();
    f.cmap = {{'A', 'A', 1}};
    for (uint32_t cp = 0xE000; cp + free_slots <= 0xF8FF; ++cp)
        f.cmap.push_back({cp, cp, 1});
    for (uint32_t cp = 0x1D400; cp < 0x1D40A; ++cp)
        f.cmap.push_back({cp, cp, 1});
    return mini_math_file(f, math_table(1, {2, 3, 4, 5, 6}));
}

} // namespace

// S7. Fifteen supplementary codepoints and too few free private-use
// codepoints to stand in for them: the minted ones are served first, and the
// face says how many went without.
TEST(s7_stand_ins_serve_minted_codepoints_first_and_report_the_shortfall)
{
    Census census;
    Atlases atlases;
    Rng rng{7};
    {
        const Bytes file = pua_filling_font(3);
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::MATH, file.data(), file.size(), "pua", &why));
        const FaceData* face = library.face_data(Face::MATH);
        REQUIRE_EQ(face->synthetic_codepoints().size(), size_t(5));
        REQUIRE_EQ(face->atlas_stand_ins().size(), size_t(3));
        // The three free slots are the last three of the block, and they go
        // to the first three codepoints minted.
        CHECK_EQ(face->atlas_codepoint(0xFFFFB, 16), 0xF8FDu);
        CHECK_EQ(face->atlas_codepoint(0xFFFFC, 16), 0xF8FEu);
        CHECK_EQ(face->atlas_codepoint(0xFFFFD, 16), 0xF8FFu);
        CHECK_EQ(face->atlas_codepoint(0xFFFFA, 16), 0u);
        CHECK_EQ(face->atlas_codepoint(0x1D400, 16), 0u);
        CHECK_EQ(face->atlas_stand_ins_unserved(), size_t(12));
        REQUIRE_EQ(face->warnings().size(), size_t(1));
        CHECK(face->warnings()[0].find("12 supplementary codepoints") != std::string::npos);
        // A private-use codepoint the font maps itself is the font's.
        CHECK_EQ(face->atlas_codepoint(0xE000, 16), 0xE000u);
        uint32_t glyph = 0;
        CHECK(face->glyph_for_atlas_codepoint(0xF8FF, 16, &glyph));
        CHECK_EQ(glyph, 2u);
        CHECK(run_chain(file, census, &atlases, rng, true) == Outcome::ACCEPTED);
    }
    {
        // No free slot at all.
        const Bytes file = pua_filling_font(0);
        FontLibrary library;
        REQUIRE(library.register_font(Face::MATH, file.data(), file.size(), "pua", nullptr));
        const FaceData* face = library.face_data(Face::MATH);
        CHECK(face->atlas_stand_ins().empty());
        CHECK_EQ(face->atlas_stand_ins_unserved(), size_t(15));
        CHECK_EQ(face->atlas_codepoint(0xFFFFD, 16), 0u);
        CHECK_EQ(face->atlas_codepoint(0xFFFFD, 32), 0xFFFFDu);
        CHECK(run_chain(file, census, &atlases, rng, true) == Outcome::ACCEPTED);
    }
    {
        // Room for all fifteen: ascending in both, minted ones last.
        const Bytes file = pua_filling_font(40);
        FontLibrary library;
        REQUIRE(library.register_font(Face::MATH, file.data(), file.size(), "pua", nullptr));
        const FaceData* face = library.face_data(Face::MATH);
        REQUIRE_EQ(face->atlas_stand_ins().size(), size_t(15));
        CHECK_EQ(face->atlas_stand_ins_unserved(), size_t(0));
        CHECK(face->warnings().empty());
        CHECK_EQ(face->atlas_codepoint(0x1D400, 16), 0xF8D8u);
        CHECK_EQ(face->atlas_codepoint(0xFFFFD, 16), 0xF8D8u + 14);
        CHECK(run_chain(file, census, &atlases, rng, true) == Outcome::ACCEPTED);
    }
    {
        // More minted codepoints than the block holds: 7,000 unencoded
        // variants, an empty private-use block, 6,400 served.
        MiniFont f;
        f.glyphs.push_back(square_glyph(50, 0, 400));
        f.glyphs.push_back(square_glyph(50, 0, 400));
        std::vector<uint32_t> variants;
        for (uint32_t g = 2; g < 7002; ++g) {
            f.glyphs.push_back(Bytes());
            variants.push_back(g);
        }
        f.cmap = {{'A', 'A', 1}};
        const Bytes file = mini_math_file(f, math_table(1, variants));
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::MATH, file.data(), file.size(), "many", &why));
        const FaceData* face = library.face_data(Face::MATH);
        REQUIRE_EQ(face->synthetic_codepoints().size(), size_t(7000));
        CHECK_EQ(face->atlas_stand_ins().size(), size_t(6400));
        CHECK_EQ(face->atlas_stand_ins_unserved(), size_t(600));
        CHECK(face->atlas_codepoint(SYNTHETIC_FIRST, 16) != 0);
        CHECK(face->atlas_codepoint(SYNTHETIC_FIRST - 6399, 16) != 0);
        CHECK_EQ(face->atlas_codepoint(SYNTHETIC_FIRST - 6400, 16), 0u);
        CHECK(run_chain(file, census, &atlases, rng, false) == Outcome::ACCEPTED);
    }
}

// --- S2: the strict pass, and every structured case through the whole chain -------------------

// S2. Each case damages one thing. The ones marked for the strict pass load
// for metrics and are refused for the atlas; before the pass existed the
// first of them was a wild read in stb_truetype on the first glyph baked
// (SEGV in stbtt_GetGlyphBox under the sanitiser build of the stb
// configuration). That a refused font reaches no rasteriser is what the
// sanitisers hold here; the outcome of each case is asserted.
TEST(s2_structured_hostile_fonts_end_where_they_should_and_nowhere_worse)
{
    const std::vector<Case> cases = structured_cases();
    Census census;
    Atlases atlases;
    Rng rng{2};
    for (const Case& c : cases) {
        CHECK_CONTEXT("%s", c.name);
        REQUIRE(!c.file.empty());
        const Outcome got = run_chain(c.file, census, &atlases, rng, true, c.face);
        if (got != c.expected)
            FAIL("outcome %d, expected %d", static_cast<int>(got), static_cast<int>(c.expected));
    }
    census.print("structured", atlases.get().atlas()->FontLoaderName);
}

// The damaged glyph of the S2 font is not drawn, and the intact font's is.
TEST(s2_the_atlas_refuses_a_font_with_a_wild_loca_pair_and_takes_the_same_font_intact)
{
    const MiniFont base = three_squares();
    Context context;
    {
        const Bytes file = patched(base, tag("loca"), {{4, 0x00100000u, 4}, {8, 0x00100040u, 4}});
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "wild.ttf", &why));
        const FaceData* face = library.face_data(Face::PROSE);
        // For metrics the damage is one glyph's: an empty box, and a warning.
        CHECK_EQ(face->font().bounds(1).x_max, 0.0);
        CHECK(!face->font().warnings().empty());
        CHECK_EQ(face->font().advance(1), 600);
        const int before = context.atlas()->Fonts.Size;
        CHECK(add_face_to_atlas(context.atlas(), *face, 16.0f, nullptr, &why) == nullptr);
        CHECK(why.find("wild.ttf cannot be given to a rasteriser") != std::string::npos);
        CHECK(why.find("loca") != std::string::npos);
        CHECK_EQ(context.atlas()->Fonts.Size, before);
    }
    {
        const Bytes file = base.build();
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "fine.ttf", &why));
        ImFont* font = add_face_to_atlas(context.atlas(), *library.face_data(Face::PROSE), 16.0f,
                                         nullptr, &why);
        REQUIRE(font != nullptr);
        // What else the entry refuses without reaching an ImGui assertion.
        CHECK(add_face_to_atlas(nullptr, *library.face_data(Face::PROSE), 16.0f, nullptr, &why) ==
              nullptr);
        CHECK(!why.empty());
        ImFontConfig config;
        config.RasterizerDensity = 0.0f;
        CHECK(add_face_to_atlas(context.atlas(), *library.face_data(Face::PROSE), 16.0f, &config,
                                &why) == nullptr);
        CHECK(why.find("RasterizerDensity") != std::string::npos);
        ImGui::NewFrame();
        const ImFontGlyph* glyph = font->GetFontBaked(16.0f)->FindGlyph('A');
        REQUIRE(glyph != nullptr);
        CHECK(glyph->Visible);
        // In a frame the atlas is locked, and that is a refusal too.
        if (context.atlas()->Locked) {
            CHECK(add_face_to_atlas(context.atlas(), *library.face_data(Face::PROSE), 16.0f,
                                    nullptr, &why) == nullptr);
            CHECK(why.find("locked") != std::string::npos);
        }
        ImGui::Render();
    }
}

// Found by the fuzz under stb_truetype, as an abort: a glyph whose glyf header
// says its box is 100 units tall and ends at the baseline, and whose outline
// is a square above the baseline. stb_truetype makes the bitmap from the box
// and asserts in its rasteriser when an edge lies wholly above row 0
// (`z->ey >= scan_y_top`, imstb_truetype.h, stbtt__rasterize_sorted_edges).
// Nothing in the file is out of bounds, so the font measures, and installed
// fonts have such glyphs; the font the atlas is given has the header's yMax
// raised over the outline. Each case is baked by whichever rasteriser this
// is: under stb_truetype, in a build where ImGui's assertions are live, that
// is the proof.
namespace
{
// The font as build_atlas_font writes it, baked at two sizes: one where a
// box a little below the baseline starts at row 0 and one where it does not.
// Returns glyph 1's yMax in the font that was written.
int bake_repaired(const MiniFont& mini, size_t repairs)
{
    const Bytes file = mini.build();
    FontLibrary library;
    std::string why;
    if (!CHECK(library.register_font(Face::PROSE, file.data(), file.size(), "box.ttf", &why)))
        return 0;
    const FaceData* face = library.face_data(Face::PROSE);
    why.clear();
    CHECK(face->font().strict_check(&why));
    CHECK_CONTEXT("%s", why.c_str());
    CHECK_EQ(face->font().atlas_box_repairs().size(), repairs);
    Bytes built;
    if (!CHECK(build_atlas_font(*face, 32, built, &why)))
        return 0;
    OpenTypeFont rebuilt;
    int y_max = 0;
    CHECK(rebuilt.load(built.data(), built.size(), &why));
    CHECK(header_y_max(rebuilt, 1, &y_max));
    // The rebuilt font needs nothing more done to it.
    CHECK(rebuilt.strict_check(&why));
    CHECK(rebuilt.atlas_box_repairs().empty());
    for (const float pixels : {16.0f, 200.0f}) {
        Context context;
        ImFont* font = add_face_to_atlas(context.atlas(), *face, pixels, nullptr, &why);
        if (!CHECK(font != nullptr))
            continue;
        ImGui::NewFrame();
        const ImFontGlyph* glyph = font->GetFontBaked(pixels)->FindGlyph('A');
        CHECK(glyph != nullptr && glyph->Visible);
        ImGui::Render();
    }
    return y_max;
}
} // namespace

TEST(a_glyph_drawn_above_a_header_box_at_the_baseline_does_not_reach_a_rasteriser)
{
    MiniFont mini = three_squares();
    mini.glyphs[1] = simple_glyph({{{0, 100}}, {{0, 400}}, {{300, 400}}, {{300, 100}}});
    wr16(mini.glyphs[1], 4, static_cast<uint32_t>(-100)); // yMin
    wr16(mini.glyphs[1], 8, 0);                           // yMax
    {
        const Bytes file = mini.build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        // Layout measures the outline, whatever the header says.
        const GlyphBounds box = font.bounds(1);
        CHECK_EQ(box.y_min, 100.0);
        CHECK_EQ(box.y_max, 400.0);
        CHECK(drawn_above_box(font, 1));
        CHECK(!drawn_above_box(font, 2));
        // The file as it stands is the one a rasteriser asserts on; what is
        // noted is the one glyph and the top of its outline.
        REQUIRE_EQ(font.atlas_box_repairs().size(), size_t(1));
        CHECK_EQ(font.atlas_box_repairs()[0].glyph, 1u);
        CHECK_EQ(font.atlas_box_repairs()[0].y_max, 400);
    }
    CHECK_EQ(bake_repaired(mini, 1), 400);

    // The box a little below the baseline is the same case at a small size.
    {
        MiniFont below = mini;
        wr16(below.glyphs[1], 8, static_cast<uint32_t>(-1));
        CHECK_EQ(bake_repaired(below, 1), 400);
    }
    // One point above is enough: the rest of the outline hangs below.
    {
        MiniFont one = three_squares();
        one.glyphs[1] = simple_glyph({{{0, -300}}, {{150, 1}}, {{300, -300}}});
        wr16(one.glyphs[1], 8, 0);
        CHECK_EQ(bake_repaired(one, 1), 1);
    }
    // A composite is its components: a square that reaches 200 above the
    // baseline once placed, under a header whose box ends at the baseline.
    {
        MiniFont composite = three_squares();
        composite.glyphs[1] = composite_glyph({{2, 0, 0}}); // glyph 2 spans -100..200
        wr16(composite.glyphs[1], 4, static_cast<uint32_t>(-100));
        wr16(composite.glyphs[1], 8, 0);
        CHECK_EQ(bake_repaired(composite, 1), 200);
        // Moved down so that nothing of it is above, it is a glyph like any
        // other, and its header is left as it is.
        composite.glyphs[1] = composite_glyph({{2, 0, -200}});
        wr16(composite.glyphs[1], 4, static_cast<uint32_t>(-300));
        wr16(composite.glyphs[1], 8, 0);
        CHECK_EQ(bake_repaired(composite, 0), 0);
    }
    // A glyph that hangs from the baseline, with a second contour of one
    // point above it, as fonts that mark an attachment point that way have
    // (kalimati.ttf's glyph 174 is this shape): no edge runs to the point,
    // and the header is left as it is.
    {
        MiniFont hanging = three_squares();
        Bytes outline;
        put16(outline, 2);                           // contours
        put16(outline, 0);                           // xMin
        put16(outline, static_cast<uint32_t>(-300)); // yMin
        put16(outline, 300);                         // xMax
        put16(outline, 0);                           // yMax
        put16(outline, 3);                           // the square's last point
        put16(outline, 4);                           // the lone point
        put16(outline, 0);                           // no instructions
        for (int i = 0; i < 5; ++i)
            put8(outline, 0x01);
        int previous = 0;
        for (const int x : {0, 0, 300, 300, 150}) {
            put16(outline, static_cast<uint32_t>(x - previous));
            previous = x;
        }
        previous = 0;
        for (const int y : {-300, 0, 0, -300, 500}) {
            put16(outline, static_cast<uint32_t>(y - previous));
            previous = y;
        }
        hanging.glyphs[1] = outline;
        CHECK_EQ(bake_repaired(hanging, 0), 0);
    }
    // A composite placed so high that no 16-bit yMax holds it, in a font tall
    // enough for that to be within reach: refused.
    {
        MiniFont tall = three_squares();
        tall.ascender = 3000;
        tall.glyphs[2] = square_glyph(0, 0, 800);
        tall.glyphs[1] = composite_glyph({{2, 0, 32000}});
        wr16(tall.glyphs[1], 4, static_cast<uint32_t>(-100));
        wr16(tall.glyphs[1], 8, 0);
        const Bytes file = tall.build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        why.clear();
        CHECK(!font.strict_check(&why));
        CHECK(why.find("further up than a bounding box can say") != std::string::npos);
        CHECK_CONTEXT("%s", why.c_str());
        CHECK(font.atlas_box_repairs().empty());
    }
}

// Found by the fuzz, as an ImGui assertion ("Out of texture memory") under
// FreeType: a mutant whose OS/2 table had landed on other bytes. FreeType
// sizes a font by OS/2's typographic ascender and descender when fsSelection
// says USE_TYPO_METRICS, and layout sizes it by hhea's, so a font where the
// two differ was drawn at another size than it was measured at: by any
// factor in a hostile font, and by up to 1.65 in fonts people use (66 of the
// 2,917 installed where this was written set the bit with a different
// extent). The atlas's copy has the bit cleared. Under stb_truetype, which
// reads hhea only, this passes with or without that.
TEST(the_atlas_sizes_a_font_by_hhea_whatever_os2_says)
{
    Context context;
    // hhea says 1000 units from descender to ascender; OS/2 says 250, and 2.
    for (const int typo_extent : {250, 2}) {
        CHECK_CONTEXT("typographic extent %d", typo_extent);
        const MiniFont mini = three_squares();
        Sfnt sfnt = mini.sfnt();
        Bytes& os2 = sfnt.table(tag("OS/2"));
        os2.assign(96, 0);
        wr16(os2, 0, 3);
        wr16(os2, 62, 0x0080); // USE_TYPO_METRICS
        wr16(os2, 68, static_cast<uint32_t>(typo_extent));
        wr16(os2, 70, 0);
        wr16(os2, 74, 800); // usWinAscent
        wr16(os2, 76, 200); // usWinDescent
        const Bytes file = sfnt.build();
        FontLibrary library;
        std::string why;
        REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "typo.ttf", &why));
        const FaceData* face = library.face_data(Face::PROSE);
        ImFont* font = add_face_to_atlas(context.atlas(), *face, 20.0f, nullptr, &why);
        REQUIRE(font != nullptr);
        ImGui::NewFrame();
        ImFontBaked* baked = font->GetFontBaked(20.0f);
        const double em = library.em_size_for_pixel_height(Face::PROSE, 20.0);
        const FontMetrics* m = library.metrics(Face::PROSE, em);
        REQUIRE(m != nullptr);
        // 'A' is a 500-unit square with a 600-unit advance: 10 and 12 px.
        const ImFontGlyph* glyph = baked->FindGlyph('A');
        REQUIRE(glyph != nullptr);
        CHECK(glyph->Visible);
        CHECK(std::fabs(glyph->AdvanceX - m->advance('A')) <= 1.0);
        CHECK(std::fabs((glyph->X1 - glyph->X0) - m->bbox('A').width()) <= 2.0);
        CHECK(std::fabs(baked->Ascent - m->ascent()) <= 1.0);
        ImGui::Render();
        context.atlas()->RemoveFont(font);
    }
}

// Every bundled font passes the strict pass, and what the pass costs.
TEST(the_bundled_fonts_pass_the_strict_pass)
{
    for (const char* name : BUNDLED) {
        CHECK_CONTEXT("%s", name);
        Bytes file;
        REQUIRE(read_font(name, file));
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        const auto start = std::chrono::steady_clock::now();
        const bool ok = font.strict_check(&why);
        const double took = seconds_since(start);
        if (!ok)
            FAIL("refused: %s", why.c_str());
        std::printf("stress_font: strict pass over %s (%u glyphs): %.2f ms\n", name,
                    font.glyph_count(), took * 1000.0);
    }
}

// --- The mutation fuzz -------------------------------------------------------------------------

namespace
{

size_t stress_iterations()
{
    const char* text = std::getenv("IMTYPESET_STRESS_ITERS");
    if (text == nullptr)
        return 1;
    const long value = std::strtol(text, nullptr, 10);
    return value < 1 ? 1 : static_cast<size_t>(value);
}

uint32_t absurd(Rng& rng)
{
    static const uint32_t values[] = {0,           1,          2,        0x7F,        0x80,
                                      0xFF,        0x100,      0x7FFF,   0x8000,      0xFFFE,
                                      0xFFFF,      0x10000,    0xFFFFFF, 0x7FFFFFF0u, 0x7FFFFFFFu,
                                      0x80000000u, 0xFFFFFFFFu};
    return rng.chance(4) ? static_cast<uint32_t>(rng.next())
                         : values[rng.below(sizeof values / sizeof values[0])];
}

// Fields that hold a count, an offset or a format: (table, offset, width).
struct Field {
    uint32_t table;
    size_t offset;
    int width;
};

const Field FIELDS[] = {
    {tag("maxp"), 4, 2},  {tag("maxp"), 0, 4},  {tag("head"), 18, 2}, {tag("head"), 50, 2},
    {tag("head"), 36, 2}, {tag("head"), 40, 2}, {tag("hhea"), 4, 2},  {tag("hhea"), 6, 2},
    {tag("hhea"), 34, 2}, {tag("cmap"), 2, 2},  {tag("cmap"), 4, 2},  {tag("cmap"), 6, 2},
    {tag("cmap"), 8, 4},  {tag("GPOS"), 4, 2},  {tag("GPOS"), 6, 2},  {tag("GPOS"), 8, 2},
    {tag("MATH"), 4, 2},  {tag("MATH"), 6, 2},  {tag("MATH"), 8, 2},  {tag("OS/2"), 0, 2},
    {tag("OS/2"), 86, 2}, {tag("CFF "), 0, 1},  {tag("CFF "), 2, 1},  {tag("CFF "), 3, 1},
    {tag("CFF "), 4, 2},  {tag("CFF "), 6, 1},  {tag("hmtx"), 0, 2},  {tag("loca"), 0, 4},
};

void write_field(Bytes& file, size_t at, int width, uint32_t value)
{
    if (width == 4)
        wr32(file, at, value);
    else if (width == 2)
        wr16(file, at, value & 0xFFFFu);
    else if (at < file.size())
        file[at] = static_cast<uint8_t>(value);
}

// One mutant of `original`. The kinds: bit flips anywhere; bytes overwritten
// in one table; a cut at a table boundary or anywhere; a directory record's
// offset or length replaced; a table or a run of one zeroed; an absurd value
// in a count, offset or format field; a value in a structure reached through
// offsets (a cmap subtable, a GPOS list, a MATH subtable, a CFF INDEX, a loca
// entry, a glyph header); a run of bytes copied from elsewhere in the file.
Bytes mutate(const Bytes& original, const std::vector<TableSpan>& spans, Rng& rng)
{
    Bytes file = original;
    const TableSpan& span = spans[rng.below(spans.size())];
    const auto in_span = [&](const TableSpan& t) { return t.offset + rng.below(t.length); };
    const int rounds = 1 + static_cast<int>(rng.below(3));
    for (int round = 0; round < rounds; ++round) {
        switch (rng.below(12)) {
        case 0:
            for (size_t i = 0, n = 1 + rng.below(4); i < n; ++i)
                file[rng.below(file.size())] ^= static_cast<uint8_t>(1u << rng.below(8));
            break;
        case 1:
            if (span.length != 0)
                for (size_t i = 0, n = 1 + rng.below(8); i < n; ++i)
                    file[in_span(span)] = static_cast<uint8_t>(rng.next());
            break;
        case 2: {
            size_t cut = rng.below(file.size());
            if (rng.chance(2))
                cut = rng.chance(2) ? span.offset : span.offset + span.length - rng.below(4);
            file.resize(std::min(std::max<size_t>(cut, 1), file.size()));
            break;
        }
        case 3: {
            const size_t at = span.record + (rng.chance(2) ? 8 : 12);
            const uint32_t old = rd32(file, at);
            static const int nudges[] = {-4, -2, -1, 1, 2, 4, 16};
            const uint32_t value = rng.chance(2) ? absurd(rng)
                                   : rng.chance(2)
                                       ? old + static_cast<uint32_t>(nudges[rng.below(7)])
                                       : static_cast<uint32_t>(rng.below(file.size() + 8));
            wr32(file, at, value);
            if (rng.chance(4)) // and the tag, for a table that goes missing
                wr32(file, span.record, rd32(file, span.record) ^ 0x20u);
            break;
        }
        case 4: {
            if (span.length == 0)
                break;
            const size_t from = rng.chance(2) ? span.offset : in_span(span);
            const size_t end = rng.chance(2) ? span.offset + span.length
                                             : std::min(from + 1 + rng.below(64), file.size());
            for (size_t i = from; i < end && i < file.size(); ++i)
                file[i] = 0;
            break;
        }
        case 5:
        case 6: {
            const Field& field = FIELDS[rng.below(sizeof FIELDS / sizeof FIELDS[0])];
            if (const TableSpan* t = span_of(spans, field.table))
                write_field(file, t->offset + field.offset, field.width, absurd(rng));
            break;
        }
        case 7: {
            // Through one offset: the cmap subtable the first record names, a
            // GPOS list, a MATH subtable.
            static const uint32_t tables[] = {tag("cmap"), tag("GPOS"), tag("MATH"), tag("GPOS"),
                                              tag("MATH")};
            const TableSpan* t = span_of(spans, tables[rng.below(5)]);
            if (t == nullptr)
                break;
            size_t base = t->offset;
            if (t->tag == tag("cmap"))
                base += rd32(file, t->offset + 8);
            else
                base += rd16(file, t->offset + 4 + 2 * rng.below(3));
            // And perhaps through a second, as lookups and constructions are.
            if (rng.chance(2))
                base += rd16(file, base + 2 + 2 * rng.below(4));
            write_field(file, base + 2 * rng.below(12), rng.chance(4) ? 4 : 2, absurd(rng));
            break;
        }
        case 8: {
            // loca: an entry made absurd, or two swapped; and the header of
            // the glyph an entry names.
            const TableSpan* loca = span_of(spans, tag("loca"));
            const TableSpan* glyf = span_of(spans, tag("glyf"));
            const TableSpan* head = span_of(spans, tag("head"));
            if (loca == nullptr || glyf == nullptr || head == nullptr || loca->length < 8)
                break;
            const bool is_long = rd16(file, head->offset + 50) != 0;
            const size_t width = is_long ? 4 : 2;
            const size_t entries = loca->length / width;
            const size_t at = loca->offset + width * rng.below(entries);
            const size_t which = rng.below(4);
            if (which == 0) {
                write_field(file, at, static_cast<int>(width), absurd(rng));
            } else if (which == 1) {
                const size_t other = loca->offset + width * rng.below(entries);
                for (size_t k = 0; k < width; ++k)
                    std::swap(file[at + k], file[other + k]);
            } else {
                const size_t glyph_at =
                    glyf->offset + (is_long ? rd32(file, at) : rd16(file, at) * 2);
                // The contour count, the box, the first end points or the
                // first component.
                write_field(file, glyph_at + 2 * rng.below(9), 2,
                            which == 2 ? absurd(rng) : rd16(file, glyph_at + 2 * rng.below(9)));
            }
            break;
        }
        case 9: {
            // CFF: the head of the table, where the INDEXes and DICTs are in
            // a font this size, or any INDEX-like place in it.
            const TableSpan* cff = span_of(spans, tag("CFF "));
            if (cff == nullptr || cff->length == 0)
                break;
            const size_t at =
                cff->offset + (rng.chance(2) ? rng.below(std::min<size_t>(cff->length, 4096))
                                             : rng.below(cff->length));
            if (rng.chance(2))
                file[at] = static_cast<uint8_t>(absurd(rng));
            else
                file[at] ^= static_cast<uint8_t>(1u << rng.below(8));
            break;
        }
        case 10: {
            // A small structural table, anywhere in it.
            static const uint32_t tables[] = {tag("head"), tag("hhea"), tag("maxp"),
                                              tag("hmtx"), tag("cmap"), tag("loca"),
                                              tag("GPOS"), tag("MATH"), tag("OS/2")};
            const TableSpan* t = span_of(spans, tables[rng.below(9)]);
            if (t == nullptr || t->length == 0)
                break;
            const size_t at = in_span(*t);
            if (rng.chance(2))
                file[at] ^= static_cast<uint8_t>(1u << rng.below(8));
            else
                write_field(file, at & ~size_t(1), 2, absurd(rng));
            break;
        }
        default: {
            const size_t length = 1 + rng.below(32);
            const size_t from = rng.below(file.size()), to = rng.below(file.size());
            for (size_t i = 0; i < length && from + i < file.size() && to + i < file.size(); ++i)
                file[to + i] = file[from + i];
            break;
        }
        }
        if (file.size() < 12)
            break;
    }
    return file;
}

// --- Mutations aimed at one structure ----------------------------------------------------------

// A place in the file that holds a count, an offset, a format or a value:
// (offset in the file, width in bytes).
using Places = std::vector<std::array<size_t, 2>>;

uint32_t read_field(const Bytes& file, size_t at, size_t width)
{
    return width == 4         ? rd32(file, at)
           : width == 2       ? rd16(file, at)
           : at < file.size() ? file[at]
                              : 0u;
}

// A value for the place: absurd, next to what was there, or what another
// place of the same structure holds (an offset to a different table of it).
void disturb(Bytes& file, const Places& places, Rng& rng)
{
    if (places.empty())
        return;
    const auto& place = places[rng.below(places.size())];
    const uint32_t old = read_field(file, place[0], place[1]);
    static const int nudges[] = {-6, -3, -2, -1, 1, 2, 3, 6, 11};
    uint32_t value;
    switch (rng.below(4)) {
    case 0:
        value = absurd(rng);
        break;
    case 1:
        value = old + static_cast<uint32_t>(nudges[rng.below(9)]);
        break;
    case 2: {
        const auto& other = places[rng.below(places.size())];
        value = read_field(file, other[0], other[1]);
        break;
    }
    default:
        value = static_cast<uint32_t>(rng.below(48));
        break;
    }
    write_field(file, place[0], static_cast<int>(place[1]), value);
}

// One of the spans with this tag: in a collection there is one per face.
const TableSpan* any_span_of(const std::vector<TableSpan>& spans, uint32_t wanted, Rng& rng)
{
    std::vector<const TableSpan*> found;
    for (const TableSpan& s : spans)
        if (s.tag == wanted)
            found.push_back(&s);
    return found.empty() ? nullptr : found[rng.below(found.size())];
}

// The collection header: the tag, the version, the face count, each face's
// offset, and each face's own version and table count.
bool aim_collection(Bytes& file, Rng& rng)
{
    if (rd32(file, 0) != tag("ttcf"))
        return false;
    Places places = {{0, 4}, {4, 2}, {6, 2}, {8, 4}};
    const size_t faces = std::min<size_t>(rd32(file, 8), 8);
    for (size_t i = 0; i <= faces; ++i)
        places.push_back({12 + i * 4, 4});
    for (const size_t directory : face_directories(file)) {
        places.push_back({directory, 4});
        places.push_back({directory + 4, 2});
    }
    if (rng.chance(4)) {
        // A face's offset aimed at something that is there: the header, the
        // offsets, another face's directory, a table, the last bytes.
        const size_t which = 12 + 4 * rng.below(faces);
        const std::vector<TableSpan> spans = table_spans(file);
        const size_t targets[] = {0,
                                  4,
                                  12,
                                  rd32(file, 12 + 4 * rng.below(faces)) + 12,
                                  spans.empty() ? 0 : spans[rng.below(spans.size())].offset,
                                  file.size() - rng.below(16),
                                  file.size() + rng.below(16)};
        wr32(file, which, static_cast<uint32_t>(targets[rng.below(7)]));
        return true;
    }
    if (rng.chance(6)) {
        static const uint32_t versions[] = {0x00010000u, tag("OTTO"), tag("true"), tag("ttcf"),
                                            tag("typ1"), tag("wOFF"), tag("wOF2"), 0x00020000u};
        const std::vector<size_t> directories = face_directories(file);
        if (!directories.empty())
            wr32(file, directories[rng.below(directories.size())], versions[rng.below(8)]);
        return true;
    }
    disturb(file, places, rng);
    return true;
}

// The kern table: its header, and each subtable's version, length, format,
// coverage and pair count, and its first pairs.
bool aim_kern(Bytes& file, const TableSpan& kern, Rng& rng)
{
    Places places = {{kern.offset, 2}, {kern.offset + 2, 2}};
    size_t at = kern.offset + 4;
    const size_t end = kern.offset + kern.length;
    for (size_t t = 0; t < 64 && at + 14 <= end; ++t) {
        const size_t pairs = rd16(file, at + 6);
        places.insert(places.end(),
                      {{at, 2}, {at + 2, 2}, {at + 4, 1}, {at + 5, 1}, {at + 6, 2}, {at + 4, 2}});
        for (size_t k = 0; k < std::min<size_t>(pairs, 4); ++k)
            places.insert(places.end(),
                          {{at + 14 + k * 6, 2}, {at + 16 + k * 6, 2}, {at + 18 + k * 6, 2}});
        at += 14 + pairs * 6;
    }
    disturb(file, places, rng);
    return true;
}

// post: the version, the glyph count, the name indices and the strings.
bool aim_post(Bytes& file, const TableSpan& post, Rng& rng)
{
    if (rng.chance(5)) {
        static const uint32_t versions[] = {0x00010000u, 0x00020000u, 0x00025000u, 0x00030000u,
                                            0x00040000u};
        wr32(file, post.offset, versions[rng.below(5)]);
        return true;
    }
    Places places = {{post.offset + 32, 2}};
    const size_t count = std::min<size_t>(rd16(file, post.offset + 32), 32);
    for (size_t i = 0; i < count; ++i)
        places.push_back({post.offset + 34 + i * 2, 2});
    for (size_t at = post.offset + 34 + count * 2; at < post.offset + post.length; ++at)
        places.push_back({at, 1});
    disturb(file, places, rng);
    return true;
}

// cmap: the record count, each record, and the head of each subtable: its
// format, its lengths and counts, and its first entries or groups.
bool aim_cmap(Bytes& file, const TableSpan& cmap, Rng& rng)
{
    Places places = {{cmap.offset + 2, 2}};
    std::vector<size_t> subtables;
    const size_t records = std::min<size_t>(rd16(file, cmap.offset + 2), 12);
    for (size_t i = 0; i < records; ++i) {
        const size_t record = cmap.offset + 4 + i * 8;
        places.insert(places.end(), {{record, 2}, {record + 2, 2}, {record + 4, 4}});
        const size_t sub = cmap.offset + rd32(file, record + 4);
        subtables.push_back(sub);
        places.insert(places.end(), {{sub + 2, 2},
                                     {sub + 4, 4},
                                     {sub + 6, 2},
                                     {sub + 8, 2},
                                     {sub + 8, 4},
                                     {sub + 10, 2},
                                     {sub + 12, 4},
                                     {sub + 16, 4},
                                     {sub + 20, 4},
                                     {sub + 24, 4},
                                     {sub + 28, 4},
                                     {sub + 32, 4},
                                     {sub + 36, 4}});
    }
    if (!subtables.empty() && rng.chance(5)) {
        static const uint32_t formats[] = {0, 2, 4, 6, 8, 10, 12, 13, 14};
        wr16(file, subtables[rng.below(subtables.size())], formats[rng.below(9)]);
        return true;
    }
    if (records != 0 && rng.chance(6)) {
        // A record's platform and encoding, among the ones that mean something.
        static const uint32_t pairs[][2] = {{3, 10}, {0, 6}, {0, 4}, {3, 1}, {0, 3}, {0, 2},
                                            {0, 1},  {0, 0}, {3, 0}, {1, 0}, {0, 5}, {1, 1}};
        const size_t record = cmap.offset + 4 + rng.below(records) * 8;
        const size_t pair = rng.below(12);
        wr16(file, record, pairs[pair][0]);
        wr16(file, record + 2, pairs[pair][1]);
        return true;
    }
    disturb(file, places, rng);
    return true;
}

// The operands of a DICT whose operands are all in the five-byte form, as
// the builders write them: (operator, where the operand's value is).
struct DictOperand {
    uint32_t op = 0;
    size_t at = 0;
};

std::vector<DictOperand> dict_operands(const Bytes& file, size_t begin, size_t end)
{
    std::vector<DictOperand> out;
    std::vector<size_t> pending;
    size_t i = begin;
    while (i < end && end <= file.size() && out.size() < 64) {
        const unsigned b = file[i];
        if (b == 29) {
            pending.push_back(i + 1);
            i += 5;
            continue;
        }
        if (b > 21)
            break;
        uint32_t op = b;
        ++i;
        if (b == 12) {
            if (i >= end)
                break;
            op = 1200 + file[i++];
        }
        for (const size_t at : pending)
            out.push_back({op, at});
        pending.clear();
    }
    return out;
}

// The objects of an INDEX with four-byte offsets, as (begin, end) in the file.
std::vector<std::array<size_t, 2>> index_objects(const Bytes& file, size_t at)
{
    std::vector<std::array<size_t, 2>> out;
    const size_t count = rd16(file, at);
    if (count == 0 || count > 1024 || at + 3 > file.size() || file[at + 2] != 4)
        return out;
    const size_t base = at + 3 + (count + 1) * 4 - 1;
    for (size_t i = 0; i < count; ++i) {
        const size_t begin = base + rd32(file, at + 3 + i * 4);
        const size_t end = base + rd32(file, at + 3 + (i + 1) * 4);
        if (begin <= end && end <= file.size())
            out.push_back({begin, end});
    }
    return out;
}

// CFF, by following it: the Top DICT's operands; the head of the CharStrings
// INDEX; for a CID-keyed font the Font DICT INDEX, each Font DICT's Private
// operands and FDSelect; each Private DICT's Subrs operand and the head of
// each Subrs INDEX. Or an operator written into a charstring or a subroutine.
bool aim_cff(Bytes& file, const TableSpan& cff, Rng& rng)
{
    const size_t c = cff.offset;
    // The builders' tables start: header (4), Name INDEX (15), Top DICT INDEX.
    const std::vector<std::array<size_t, 2>> tops = index_objects(file, c + 19);
    if (tops.empty())
        return false;
    Places places;
    std::vector<std::array<size_t, 2>> code;
    const auto index_at = [&](size_t at, bool is_code) {
        places.push_back({at, 2});
        places.push_back({at + 2, 1});
        const size_t count = std::min<size_t>(rd16(file, at), 8);
        for (size_t i = 0; i <= count; ++i)
            places.push_back({at + 3 + i * 4, 4});
        const std::vector<std::array<size_t, 2>> objects = index_objects(file, at);
        if (is_code)
            code.insert(code.end(), objects.begin(), objects.end());
        return objects;
    };
    const auto private_at = [&](size_t size_at, size_t offset_at) {
        places.push_back({size_at, 4});
        places.push_back({offset_at, 4});
        const size_t dict = c + rd32(file, offset_at);
        for (const DictOperand& o : dict_operands(file, dict, dict + rd32(file, size_at))) {
            places.push_back({o.at, 4});
            if (o.op == 19)
                index_at(dict + rd32(file, o.at), true);
        }
    };
    size_t private_size_at = 0;
    for (const DictOperand& o : dict_operands(file, tops[0][0], tops[0][1])) {
        places.push_back({o.at, 4});
        const size_t target = c + rd32(file, o.at);
        if (o.op == 17) {
            index_at(target, true);
        } else if (o.op == 18) {
            if (private_size_at == 0)
                private_size_at = o.at;
            else
                private_at(private_size_at, o.at);
        } else if (o.op == 1236) {
            for (const auto& dict : index_at(target, false)) {
                const std::vector<DictOperand> operands = dict_operands(file, dict[0], dict[1]);
                if (operands.size() == 2 && operands[0].op == 18)
                    private_at(operands[0].at, operands[1].at);
            }
        } else if (o.op == 1237) {
            places.push_back({target + 1, 2});
            for (size_t k = 0; k < 20; ++k)
                places.push_back({target + k, 1});
        }
    }
    if (!code.empty() && rng.chance(3)) {
        const auto& object = code[rng.below(code.size())];
        if (object[1] > object[0]) {
            const size_t at = object[0] + rng.below(object[1] - object[0]);
            if (rng.chance(3)) {
                file[at] = static_cast<uint8_t>(rng.below(32));
            } else {
                // One of the escaped operators: 12, then which.
                file[at] = 12;
                if (at + 1 < file.size())
                    file[at + 1] = static_cast<uint8_t>(rng.below(40));
            }
        }
        return true;
    }
    disturb(file, places, rng);
    return true;
}

// One or two mutations of `original`, each aimed at a structure it has: the
// collection header, kern, post, a cmap subtable, the CFF table's DICTs and
// INDEXes. In a collection the table is any face's. A file with none of them
// gets a general mutation.
Bytes mutate_aimed(const Bytes& original, const std::vector<TableSpan>& spans, Rng& rng)
{
    Bytes file = original;
    bool hit = false;
    const int rounds = 1 + static_cast<int>(rng.below(2));
    for (int round = 0; round < rounds; ++round) {
        const TableSpan* table = nullptr;
        switch (rng.below(6)) {
        case 0:
            hit = aim_collection(file, rng) || hit;
            break;
        case 1:
            if ((table = any_span_of(spans, tag("kern"), rng)) != nullptr)
                hit = aim_kern(file, *table, rng) || hit;
            break;
        case 2:
            if ((table = any_span_of(spans, tag("post"), rng)) != nullptr)
                hit = aim_post(file, *table, rng) || hit;
            break;
        case 3:
            if ((table = any_span_of(spans, tag("cmap"), rng)) != nullptr)
                hit = aim_cmap(file, *table, rng) || hit;
            break;
        default:
            if ((table = any_span_of(spans, tag("CFF "), rng)) != nullptr)
                hit = aim_cff(file, *table, rng) || hit;
            break;
        }
    }
    return hit ? file : mutate(original, spans, rng);
}

// A mutant through the chain as every face its file says it has (the first
// four), and as the face after the last, which no file has.
void run_faces(const Bytes& file, Census& census, Atlases* atlases, Rng& rng, bool thorough)
{
    const uint32_t faces = OpenTypeFont::faces_in(file.data(), file.size());
    for (uint32_t face = 0; face < std::min<uint32_t>(std::max<uint32_t>(faces, 1), 4); ++face) {
        CHECK_CONTEXT("face %u of %u", face, faces);
        run_chain(file, census, atlases, rng, thorough, static_cast<int>(face));
    }
    if (faces <= 0x7FFFFFFFu) {
        CHECK_CONTEXT("face %u of %u", faces, faces);
        Census unused;
        const int past = static_cast<int>(std::max<uint32_t>(faces, 1));
        CHECK(run_chain(file, unused, atlases, rng, false, past) == Outcome::READER_REFUSED);
    }
}

// Random mutants per font per unit of IMTYPESET_STRESS_ITERS. One in five
// goes the whole way to a baked glyph; the rest stop after build_atlas_font,
// which is the chain up to but not including ImGui. A mutant costs a full
// parse of the font, and under the sanitisers at -O0 that is 15 to 60 ms, so
// the count is what the build can run in about twenty seconds: the sanitiser
// build runs a prefix of the sequence the others run.
#if defined(__SANITIZE_ADDRESS__)
constexpr size_t MUTANTS_PER_FONT = 120;
constexpr size_t SMALL_MUTANTS_PER_SEED = 80;
#elif defined(__OPTIMIZE__)
constexpr size_t MUTANTS_PER_FONT = 3000;
constexpr size_t SMALL_MUTANTS_PER_SEED = 3000;
#else
constexpr size_t MUTANTS_PER_FONT = 450;
constexpr size_t SMALL_MUTANTS_PER_SEED = 600;
#endif

} // namespace

// Every bundled font, mutated at random with a fixed seed. Sanitiser: a
// mutant that corrupts memory fails only under ASan and UBSan. What is
// asserted everywhere is in run_chain: a reason with every refusal, every
// answer in range, the atlas in agreement with layout on what the font has.
TEST(fuzz_every_bundled_font_all_the_way_to_a_baked_glyph)
{
    const size_t iterations = stress_iterations();
    const auto start = std::chrono::steady_clock::now();
    Atlases atlases;
    Census total;
    uint64_t seed = 0x1234;
    for (const char* name : BUNDLED) {
        CHECK_CONTEXT("%s", name);
        Bytes original;
        REQUIRE(read_font(name, original));
        const std::vector<TableSpan> spans = table_spans(original);
        REQUIRE(spans.size() >= 9);
        Census census;
        Rng rng{++seed * 0x9E3779B97F4A7C15ull};

        // The file itself, and a cut at each end of every table.
        CHECK(run_chain(original, census, &atlases, rng, true) == Outcome::ACCEPTED);
        for (const TableSpan& span : spans) {
            for (const size_t cut : {span.offset + 1, span.offset + span.length - 1}) {
                CHECK_CONTEXT("cut at %zu", cut);
                if (cut == 0 || cut >= original.size())
                    continue;
                const Bytes file(original.begin(),
                                 original.begin() + static_cast<std::ptrdiff_t>(cut));
                run_chain(file, census, &atlases, rng, false);
            }
        }
        // The math face is four times the size of the others and costs as
        // much more to parse.
        const bool large = original.size() > 400000;
        const size_t mutants = MUTANTS_PER_FONT * iterations / (large ? 2 : 1);
        for (size_t i = 0; i < mutants; ++i) {
            CHECK_CONTEXT("mutant %zu", i);
            const Bytes file = mutate(original, spans, rng);
            // One in five goes to the atlas; the rest stop at the rebuilt file.
            const bool full = i % 5 == 0;
            run_chain(file, census, full ? &atlases : nullptr, rng, false);
        }
        census.print(name, loader_name());
        total.add(census);
    }
    total.print("all fonts", loader_name());
    std::printf("stress_font: the fuzz took %.1f s at IMTYPESET_STRESS_ITERS=%zu\n",
                seconds_since(start), iterations);
    // The fuzz is doing its job only if it reaches every stage.
    CHECK(total.reader_refused > 0);
    CHECK(total.strict_refused > 0);
    CHECK(total.accepted > total.stopped_before_atlas);
    CHECK(total.glyphs_visible > 0);
}

// The synthetic fonts are a few hundred bytes, every one of them structure: a
// random mutant of one lands on a loca entry, a contour count, a component,
// an INDEX offset or a DICT operand nearly every time, where a mutant of a
// 170 KB font mostly lands in the middle of an outline. Each goes the whole
// way. Sanitiser: as above.
TEST(fuzz_small_synthetic_fonts_densely_all_the_way)
{
    const size_t per_seed = SMALL_MUTANTS_PER_SEED * stress_iterations();
    const auto start = std::chrono::steady_clock::now();
    Atlases atlases;
    Census census;
    // By what the seed is there for; "" is the glyf, CFF and MATH seeds.
    struct Kind {
        const char* name;
        size_t seeds = 0;
        Census census;
        double seconds = 0.0;
    };
    std::vector<Kind> kinds;
    size_t seeds = 0;
    uint64_t seed = 0xABCD;
    for (const Case& c : structured_cases()) {
        // A collection is one seed, whichever of its faces the case asks for.
        if (c.expected != Outcome::ACCEPTED || c.file.size() > 16384 || c.face != 0 ||
            c.kind == NOT_A_SEED)
            continue;
        CHECK_CONTEXT("%s", c.name);
        ++seeds;
        const std::vector<TableSpan> spans = table_spans(c.file);
        REQUIRE(!spans.empty());
        Rng rng{++seed * 0x9E3779B97F4A7C15ull};
        const bool aimed = c.kind[0] != '\0';
        const bool every_face = rd32(c.file, 0) == tag("ttcf");
        Census mine;
        const auto seed_start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < per_seed; ++i) {
            CHECK_CONTEXT("mutant %zu", i);
            // Half the mutants of a seed that is there for one structure are
            // aimed at that structure; the rest fall anywhere.
            const Bytes file =
                aimed && i % 2 == 1 ? mutate_aimed(c.file, spans, rng) : mutate(c.file, spans, rng);
            if (every_face)
                run_faces(file, mine, &atlases, rng, false);
            else
                run_chain(file, mine, &atlases, rng, false);
        }
        census.add(mine);
        Kind* kind = nullptr;
        for (Kind& k : kinds)
            if (std::strcmp(k.name, c.kind) == 0)
                kind = &k;
        if (kind == nullptr) {
            kinds.push_back({c.kind, 0, Census(), 0.0});
            kind = &kinds.back();
        }
        kind->seconds += seconds_since(seed_start);
        if (std::getenv("IMTYPESET_STRESS_SEED_TIMES") != nullptr)
            std::printf("stress_font: seed \"%s\": %.1f s\n", c.name, seconds_since(seed_start));
        ++kind->seeds;
        kind->census.add(mine);
    }
    for (const Kind& kind : kinds) {
        char what[96];
        std::snprintf(what, sizeof what, "%zu %s seeds, %.1f s", kind.seeds,
                      kind.name[0] != '\0' ? kind.name : "glyf, CFF and MATH", kind.seconds);
        kind.census.print(what, loader_name());
    }
    census.print("synthetic seeds", loader_name());
    std::printf("stress_font: %zu synthetic seeds, %zu mutants each (a mutant of a collection "
                "goes through once per face), %.1f s\n",
                seeds, per_seed, seconds_since(start));
    CHECK(seeds >= 40);
    CHECK(kinds.size() >= 7);
    CHECK(census.reader_refused > 0);
    CHECK(census.strict_refused > 0);
    CHECK(census.glyphs_visible > 0);
    // The fuzz reaches the new paths only if mutants that still have them
    // get past the reader.
    CHECK(census.collection_faces > 0);
    CHECK(census.cid_keyed > 0);
    CHECK(census.legacy_kerned > 0);
}

// Fonts a host would bring, if there are any to hand. IMTYPESET_STRESS_FONT_LIST
// names a text file of font paths, one a line; each is read in place and goes
// the whole way, as shipped and as a few mutants. Unset, the test says so and
// does nothing: no font outside the tree is assumed to exist.
TEST(fuzz_fonts_named_by_the_environment)
{
    const char* list = std::getenv("IMTYPESET_STRESS_FONT_LIST");
    if (list == nullptr || list[0] == '\0') {
        std::printf("stress_font: IMTYPESET_STRESS_FONT_LIST is not set; no installed font was "
                    "read\n");
        return;
    }
    std::string text;
    REQUIRE(golden::read_file(list, text));
    Atlases atlases;
    Census as_shipped, mutants;
    size_t files = 0, strict_refusals = 0, faces_read = 0, collections = 0;
    Rng rng{0x5EED};
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string path = text.substr(at, end - at);
        at = end + 1;
        Bytes file;
        if (path.empty() || !golden::read_file(path, file) || file.size() < 12)
            continue;
        CHECK_CONTEXT("%s", path.c_str());
        ++files;
        // Every face of a collection (the first 64), and the face past the last.
        const uint32_t stated = OpenTypeFont::faces_in(file.data(), file.size());
        const uint32_t faces = std::min<uint32_t>(std::max<uint32_t>(stated, 1), 64);
        collections += stated > 1 ? 1 : 0;
        for (uint32_t face = 0; face < faces; ++face) {
            CHECK_CONTEXT("face %u of %u", face, stated);
            ++faces_read;
            const Outcome outcome =
                run_chain(file, as_shipped, &atlases, rng, false, static_cast<int>(face));
            if (outcome == Outcome::STRICT_REFUSED) {
                ++strict_refusals;
                std::printf("stress_font: the strict pass refuses %s, face %u\n", path.c_str(),
                            face);
            } else if (outcome != Outcome::ACCEPTED) {
                std::printf("stress_font: %s, face %u: outcome %d\n", path.c_str(), face,
                            static_cast<int>(outcome));
            }
        }
        if (stated > 1 && stated <= 0x7FFFFFFFu) {
            Census unused;
            CHECK(run_chain(file, unused, nullptr, rng, false, static_cast<int>(stated)) ==
                  Outcome::READER_REFUSED);
        }
        const std::vector<TableSpan> spans = table_spans(file);
        if (spans.empty())
            continue;
        for (size_t i = 0; i < 4 * stress_iterations(); ++i) {
            CHECK_CONTEXT("mutant %zu", i);
            // A collection's mutants are aimed at its header half the time,
            // and go through as any face, the one past the last included.
            if (stated > 1) {
                const Bytes mutant =
                    i % 2 == 1 ? mutate_aimed(file, spans, rng) : mutate(file, spans, rng);
                run_chain(mutant, mutants, &atlases, rng, false,
                          static_cast<int>(rng.below(faces + 1)));
            } else {
                run_chain(mutate(file, spans, rng), mutants, &atlases, rng, false);
            }
        }
    }
    std::printf("stress_font: %zu fonts from %s: %zu faces, %zu of the files collections\n", files,
                list, faces_read, collections);
    as_shipped.print("listed fonts as shipped", loader_name());
    mutants.print("listed fonts mutated", loader_name());
    CHECK(files > 0);
}

// The same mutants through the public entry, which is what a host calls.
TEST(fuzz_mutants_through_the_public_entry)
{
    const size_t per_font = 6 * stress_iterations();
    Context context;
    struct Typeset {
        ImTypesetContext* ctx = ImTypeset::CreateContext();
        ~Typeset() { ImTypeset::DestroyContext(ctx); }
    } typeset;
    if (typeset.ctx == nullptr) {
        FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
        return;
    }
    size_t added = 0, refused = 0;
    const auto offer = [&](const Bytes& file, ImTypesetFace face) {
        ImFont* font =
            ImTypeset::AddFontFromMemoryTTF(face, file.data(), static_cast<int>(file.size()));
        if (font == nullptr) {
            const ImTypesetErrorInfo* error = ImTypeset::GetLastError();
            CHECK_EQ(error->Code, ImTypesetError_FontRefused);
            CHECK(error->Message != nullptr && error->Message[0] != '\0');
            ++refused;
            return;
        }
        ++added;
        CHECK(ImTypeset::GetFont(face) == font);
        ImGui::NewFrame();
        ImFontBaked* baked = font->GetFontBaked(16.0f);
        for (const ImWchar c : {ImWchar('A'), ImWchar('g'), ImWchar(0x2211)})
            if (const ImFontGlyph* glyph = baked->FindGlyph(c))
                CHECK(std::isfinite(glyph->AdvanceX));
        ImGui::Render();
    };
    for (const Case& c : structured_cases()) {
        CHECK_CONTEXT("%s", c.name);
        const size_t before = added;
        offer(c.file, ImTypesetFace_Regular);
        // What the chain refuses at any stage, the entry refuses. The entry
        // is offered face 0; the cases that are about another face, or about
        // a collection (which the entry may not take at all), are offered for
        // the sanitisers only.
        if (c.face == 0 && rd32(c.file, 0) != tag("ttcf"))
            CHECK_EQ(added != before, c.expected == Outcome::ACCEPTED);
    }
    uint64_t seed = 0x77;
    for (const char* name : BUNDLED) {
        CHECK_CONTEXT("%s", name);
        Bytes original;
        REQUIRE(read_font(name, original));
        const std::vector<TableSpan> spans = table_spans(original);
        Rng rng{++seed * 0x9E3779B97F4A7C15ull};
        const bool math = std::strstr(name, "math") != nullptr;
        for (size_t i = 0; i < per_font; ++i) {
            CHECK_CONTEXT("mutant %zu", i);
            offer(mutate(original, spans, rng), math ? ImTypesetFace_Math : ImTypesetFace_Regular);
        }
    }
    std::printf("stress_font census [public entry, %s, ImWchar %d]: %zu mutants: %zu refused, "
                "%zu added\n",
                loader_name(), ATLAS_WCHAR_BITS, added + refused, refused, added);
    CHECK(added > 0);
    CHECK(refused > 0);
}
