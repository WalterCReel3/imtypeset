// The parts of the OpenType reader that no bundled font reaches, on fonts a
// few hundred bytes long written here: collections, the refusals that name a
// format, cmap formats 0, 6, 10, 12 and 13 and which subtable is picked, an
// hmtx with no full record, a loca one entry short, post glyph names,
// CID-keyed CFF, the Type 2 arithmetic and flex operators, the version 0
// 'kern' table, and GPOS null offsets.
//
// Every expectation is worked out by hand from the OpenType and CFF
// specifications and from what src/opentype.h says the reader does; none is
// the reader's own output written down. The builders are tests/font_builders.h.

#include "font_builders.h"
#include "golden.h"

#include "font_loader.h"
#include "fontlib.h"
#include "opentype.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace ImTypeset::detail;
using namespace font_builders;

namespace
{

// A glyf font of `count` glyphs; 'A' on are glyphs 1 on. Glyph g is a square
// of side 100 + 10g whose outline starts at x = 10g. A simple glyph drawn at
// top level is shifted in x by its hmtx side bearing minus the glyf header's
// xMin (tools/oracle/FORMAT-font.md), and the builder's side bearings are 0,
// so its ink box starts at 0.
MiniFont squares(uint32_t count)
{
    MiniFont f;
    for (uint32_t g = 0; g < count; ++g)
        f.glyphs.push_back(
            square_glyph(static_cast<int>(10 * g), 0, static_cast<int>(100 + 10 * g)));
    if (count > 1)
        f.cmap = {{'A', 'A' + count - 2, 1}};
    return f;
}

GlyphBounds square_box(uint32_t g)
{
    return GlyphBounds{0.0, 0.0, 10.0 * g + 100.0, 10.0 * g + 100.0};
}

Bytes with(const MiniFont& font, uint32_t table, const Bytes& data)
{
    Sfnt sfnt = font.sfnt();
    sfnt.table(table) = data;
    return sfnt.build();
}

Bytes with_cmap(const Bytes& cmap)
{
    return with(squares(12), tag("cmap"), cmap);
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

void check_box(const OpenTypeFont& font, uint32_t glyph, const GlyphBounds& want)
{
    const GlyphBounds got = font.bounds(glyph);
    CHECK_CONTEXT("glyph %u", glyph);
    CHECK_EQ(got.x_min, want.x_min);
    CHECK_EQ(got.y_min, want.y_min);
    CHECK_EQ(got.x_max, want.x_max);
    CHECK_EQ(got.y_max, want.y_max);
}

// The whole of what the font maps, range by range.
// `one_glyph`: whether each range gives its one glyph to every codepoint in
// it, as a format 13 group does, rather than consecutive glyphs.
void check_cmap(const OpenTypeFont& font, const std::vector<CmapGroup>& want,
                bool one_glyph = false)
{
    const std::vector<CmapRange>& got = font.cmap();
    REQUIRE_EQ(got.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK_CONTEXT("range %zu", i);
        CHECK_EQ(got[i].first, want[i].first);
        CHECK_EQ(got[i].last, want[i].last);
        CHECK_EQ(got[i].glyph, want[i].glyph);
        CHECK_EQ(got[i].one_glyph, one_glyph);
    }
}

void check_maps(const OpenTypeFont& font, uint32_t codepoint, uint32_t want)
{
    CHECK_CONTEXT("U+%04X", codepoint);
    uint32_t glyph = 0;
    if (CHECK(font.lookup(codepoint, &glyph)))
        CHECK_EQ(glyph, want);
}

void check_unmapped(const OpenTypeFont& font, uint32_t codepoint)
{
    CHECK_CONTEXT("U+%04X", codepoint);
    CHECK(!font.lookup(codepoint, nullptr));
}

// A headless ImGui context for the length of a scope.
struct Context {
    Context()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~Context() { ImGui::DestroyContext(); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

// Face 0: a glyf font, 1000 units to the em, three glyphs, 'A' and 'B'.
// Face 1: a CFF font, 2048 units to the em, four glyphs, 'A'..'C', advance 777.
MiniFont collection_face0()
{
    return three_squares();
}

MiniFont collection_face1()
{
    MiniCff cff;
    cff.charstrings = {cs_square(0, 0, 100), cs_square(40, 60, 1000), cs_square(0, 0, 300),
                       cs_square(0, 0, 400)};
    MiniFont f = cff_font(cff);
    f.units_per_em = 2048;
    f.ascender = 1900;
    f.descender = -500;
    f.advance = 777;
    return f;
}

Bytes two_face_collection()
{
    return collection({collection_face0().build(), collection_face1().build()});
}

} // namespace

// --- 1. Collections ------------------------------------------------------------------------

TEST(a_collection_holds_its_faces_by_index_each_with_its_own_tables)
{
    const Bytes file = two_face_collection();
    CHECK_EQ(OpenTypeFont::faces_in(file.data(), file.size()), 2u);

    FontLoadOptions options;
    std::string why;
    OpenTypeFont first;
    REQUIRE(first.load(file.data(), file.size(), &why, options));
    CHECK_EQ(first.face_index(), 0u);
    CHECK_EQ(first.face_count(), 2u);
    CHECK_EQ(first.sfnt_version(), 0x00010000u);
    CHECK(!first.is_cff());
    CHECK_EQ(first.units_per_em(), 1000);
    CHECK_EQ(first.glyph_count(), 3u);
    CHECK_EQ(first.hhea_ascender(), 800);
    CHECK_EQ(first.hhea_descender(), -200);
    CHECK_EQ(first.advance(1), 600);
    check_cmap(first, {{'A', 'B', 1}});
    // A square of 500 from x = 100, moved to its side bearing of 0.
    check_box(first, 1, GlyphBounds{0, 0, 500, 500});

    options.face_index = 1;
    OpenTypeFont second;
    REQUIRE(second.load(file.data(), file.size(), &why, options));
    CHECK_EQ(second.face_index(), 1u);
    CHECK_EQ(second.face_count(), 2u);
    CHECK_EQ(second.sfnt_version(), tag("OTTO"));
    CHECK(second.is_cff());
    CHECK_EQ(second.units_per_em(), 2048);
    CHECK_EQ(second.glyph_count(), 4u);
    CHECK_EQ(second.hhea_ascender(), 1900);
    CHECK_EQ(second.hhea_descender(), -500);
    CHECK_EQ(second.advance(1), 777);
    check_cmap(second, {{'A', 'C', 1}});
    check_box(second, 1, GlyphBounds{40, 60, 1040, 1060});
    CHECK(second.strict_check(&why));

    // The tables of a face are its own directory's, wherever they are.
    REQUIRE(second.table_count() > 0);
    for (size_t i = 0; i < second.table_count(); ++i) {
        const TableRecord record = second.table_record(i);
        CHECK(record.data >= file.data() && record.data + record.size <= file.data() + file.size());
    }
}

TEST(a_face_index_the_file_does_not_have_fails_and_says_how_many_it_has)
{
    const Bytes file = two_face_collection();
    FontLoadOptions options;
    options.face_index = 2;
    std::string why;
    OpenTypeFont font;
    CHECK(!font.load(file.data(), file.size(), &why, options));
    CHECK(!font.loaded());
    CHECK(contains(why, "face index 2"));
    CHECK(contains(why, "2 faces"));
    options.face_index = 0xFFFFFFFFu;
    why.clear();
    CHECK(!font.load(file.data(), file.size(), &why, options));
    CHECK(contains(why, "face index 4294967295"));

    // A file that is not a collection has face 0 and no other.
    const Bytes single = collection_face0().build();
    CHECK_EQ(OpenTypeFont::faces_in(single.data(), single.size()), 1u);
    options.face_index = 1;
    why.clear();
    CHECK(!font.load(single.data(), single.size(), &why, options));
    CHECK(contains(why, "face index 1"));
    CHECK(contains(why, "not a font collection"));
    options.face_index = 0;
    CHECK(font.load(single.data(), single.size(), &why, options));
    CHECK_EQ(font.face_count(), 1u);

    // Too short to have a version tag.
    CHECK_EQ(OpenTypeFont::faces_in(single.data(), 3), 0u);
    CHECK_EQ(OpenTypeFont::faces_in(nullptr, 0), 0u);
    // A collection of one face says "1 face".
    const Bytes one = collection({single});
    options.face_index = 1;
    why.clear();
    CHECK(!font.load(one.data(), one.size(), &why, options));
    CHECK(contains(why, "holds 1 face"));
    CHECK(!contains(why, "1 faces"));
}

TEST(a_collection_header_that_lies_fails_with_a_reason)
{
    const Bytes good = two_face_collection();
    OpenTypeFont font;
    std::string why;
    {
        Bytes file = good;
        wr32(file, 8, 0); // no faces
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(contains(why, "no faces"));
    }
    {
        Bytes file = good;
        wr32(file, 8, 0xFFFFFFFFu); // more offsets than the file is long
        why.clear();
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(contains(why, "truncated"));
        // faces_in reports what the header states, and promises nothing more.
        CHECK_EQ(OpenTypeFont::faces_in(file.data(), file.size()), 0xFFFFFFFFu);
    }
    {
        Bytes file = good;
        wr32(file, 16, static_cast<uint32_t>(file.size() + 100)); // face 1 past the end
        FontLoadOptions options;
        options.face_index = 1;
        why.clear();
        CHECK(!font.load(file.data(), file.size(), &why, options));
        CHECK(contains(why, "outside the file"));
        CHECK(font.load(file.data(), file.size(), &why)); // face 0 is untouched
    }
    {
        Bytes file = good;
        wr32(file, 12, 0); // face 0 is the collection header itself
        why.clear();
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(contains(why, "not an OpenType or TrueType font"));
    }
    {
        // Two faces that share one directory are the same face twice.
        Bytes file = good;
        wr32(file, 12, rd32(file, 16));
        FontLoadOptions options;
        OpenTypeFont a, b;
        REQUIRE(a.load(file.data(), file.size(), &why, options));
        options.face_index = 1;
        REQUIRE(b.load(file.data(), file.size(), &why, options));
        CHECK_EQ(a.units_per_em(), 2048);
        CHECK_EQ(b.units_per_em(), 2048);
        CHECK_EQ(a.face_index(), 0u);
        CHECK_EQ(b.face_index(), 1u);
    }
}

TEST(the_registry_takes_a_face_index_and_refuses_one_out_of_range_or_negative)
{
    const Bytes file = two_face_collection();
    FontLibrary library;
    std::string why;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "two.ttc", &why, 0));
    CHECK_EQ(library.face_data(Face::PROSE)->units_per_em(), 1000);
    REQUIRE(library.register_font(Face::PROSE_BOLD, file.data(), file.size(), "two.ttc", &why, 1));
    const FaceData* bold = library.face_data(Face::PROSE_BOLD);
    CHECK_EQ(bold->units_per_em(), 2048);
    CHECK_EQ(bold->font().face_index(), 1u);
    CHECK_EQ(bold->glyph('C'), 3u);
    // 777 units at 2048 to the em, set at 2048: the advance in units.
    const FontMetrics* m = library.metrics(Face::PROSE_BOLD, 2048.0, &why);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->advance('A'), 777.0);

    const uint32_t before = library.generation();
    why.clear();
    CHECK(!library.register_font(Face::PROSE, file.data(), file.size(), "two.ttc", &why, 2));
    CHECK(contains(why, "face index 2"));
    CHECK(contains(why, "2 faces"));
    why.clear();
    CHECK(!library.register_font(Face::PROSE, file.data(), file.size(), "two.ttc", &why, -1));
    CHECK(contains(why, "negative"));
    const Bytes single = collection_face0().build();
    why.clear();
    CHECK(!library.register_font(Face::PROSE, single.data(), single.size(), "one.ttf", &why, 1));
    CHECK(contains(why, "face index 1"));
    // A refusal changes nothing.
    CHECK_EQ(library.generation(), before);
    CHECK_EQ(library.face_data(Face::PROSE)->units_per_em(), 1000);
}

TEST(the_atlas_is_handed_one_face_of_a_collection_as_a_font_of_its_own)
{
    const Bytes file = two_face_collection();
    FontLibrary library;
    std::string why;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "two.ttc", &why, 1));
    const FaceData* face = library.face_data(Face::PROSE);
    Bytes built;
    REQUIRE(build_atlas_font(*face, 32, built, &why));
    REQUIRE(built.size() >= 12);
    // The face's version, not the collection's.
    CHECK_EQ(rd32(built, 0), tag("OTTO"));
    CHECK_EQ(OpenTypeFont::faces_in(built.data(), built.size()), 1u);
    CHECK(built.size() < file.size()); // face 0's tables are not in it
    OpenTypeFont again;
    REQUIRE(again.load(built.data(), built.size(), &why));
    CHECK_EQ(again.face_count(), 1u);
    CHECK_EQ(again.glyph_count(), face->font().glyph_count());
    CHECK_EQ(again.units_per_em(), 2048);
    for (uint32_t g = 0; g < again.glyph_count(); ++g) {
        CHECK_EQ(again.advance(g), face->font().advance(g));
        CHECK(again.bounds(g) == face->font().bounds(g));
    }
    check_cmap(again, {{'A', 'C', 1}});
    CHECK(again.strict_check(&why));

    // And face 0 of the same file.
    REQUIRE(library.register_font(Face::CODE, file.data(), file.size(), "two.ttc", &why, 0));
    REQUIRE(build_atlas_font(*library.face_data(Face::CODE), 32, built, &why));
    CHECK_EQ(rd32(built, 0), 0x00010000u);
    REQUIRE(again.load(built.data(), built.size(), &why));
    CHECK_EQ(again.glyph_count(), 3u);
    CHECK_EQ(again.units_per_em(), 1000);
}

TEST(a_face_of_a_collection_bakes_a_visible_glyph)
{
    const Bytes file = two_face_collection();
    Context context;
    for (const int index : {0, 1}) {
        CHECK_CONTEXT("face %d", index);
        FontLibrary library;
        std::string why;
        REQUIRE(
            library.register_font(Face::PROSE, file.data(), file.size(), "two.ttc", &why, index));
        const FaceData* face = library.face_data(Face::PROSE);
        ImFont* font = add_face_to_atlas(ImGui::GetIO().Fonts, *face, 32.0f, nullptr, &why);
        if (font == nullptr) {
            FAIL("add_face_to_atlas: %s", why.c_str());
            continue;
        }
        ImGui::NewFrame();
        CHECK(font->IsGlyphInFont('A'));
        CHECK(font->IsGlyphInFont('B'));
        CHECK_EQ(font->IsGlyphInFont('C'), index == 1);
        const ImFontGlyph* glyph = font->GetFontBaked(32.0f)->FindGlyph('A');
        if (CHECK(glyph != nullptr)) {
            CHECK(glyph->Visible);
            CHECK(glyph->X1 > glyph->X0 && glyph->Y1 > glyph->Y0);
        }
        ImGui::Render();
        ImGui::GetIO().Fonts->RemoveFont(font);
    }
}

// --- 2. Refusals that name the format ------------------------------------------------------

TEST(what_is_recognised_and_not_read_is_refused_by_name)
{
    const auto padded = [](std::initializer_list<uint8_t> start) {
        Bytes file(start);
        file.resize(64, 0);
        return file;
    };
    struct Refusal {
        const char* what;
        Bytes file;
        const char* names;
    };
    const char* const ps = "%!PS-AdobeFont-1.0: Sample 001.000\n";
    Bytes type1(ps, ps + std::strlen(ps));
    type1.resize(64, ' ');
    Sfnt cff2 = squares(3).sfnt();
    cff2.remove(tag("glyf"));
    cff2.remove(tag("loca"));
    cff2.version = tag("OTTO");
    cff2.table(tag("CFF2")) = Bytes{2, 0, 5, 0, 0};
    Sfnt bitmaps = squares(3).sfnt();
    bitmaps.remove(tag("glyf"));
    bitmaps.remove(tag("loca"));
    bitmaps.table(tag("CBDT")) = Bytes{0, 3, 0, 0};
    bitmaps.table(tag("CBLC")) = Bytes{0, 3, 0, 0, 0, 0, 0, 0};
    const std::vector<Refusal> refusals = {
        {"wOFF", padded({'w', 'O', 'F', 'F'}), "WOFF"},
        {"wOF2", padded({'w', 'O', 'F', '2'}), "WOFF2"},
        {"%!PS-AdobeFont", type1, "Type 1"},
        {"a PFB segment header", padded({0x80, 0x01, 0x10, 0x00, 0x00, 0x00, '%', '!'}), "Type 1"},
        {"sfnt version typ1", padded({'t', 'y', 'p', '1'}), "Type 1"},
        {"CFF2 outlines only", cff2.build(), "CFF2"},
        {"CBDT bitmaps only", bitmaps.build(), "bitmap"},
    };
    for (const Refusal& r : refusals) {
        CHECK_CONTEXT("%s", r.what);
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(r.file.data(), r.file.size(), &why));
        CHECK(!font.loaded());
        if (!contains(why, r.names))
            FAIL("the reason does not name %s: \"%s\"", r.names, why.c_str());
        FontLibrary library;
        why.clear();
        CHECK(!library.register_font(Face::PROSE, r.file.data(), r.file.size(), r.what, &why));
        CHECK(contains(why, r.names));
    }
    // WOFF is not taken for WOFF2.
    {
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(refusals[0].file.data(), refusals[0].file.size(), &why));
        CHECK(!contains(why, "WOFF2"));
    }
    // A face of a collection in one of those formats is refused the same way.
    {
        Bytes file = two_face_collection();
        const size_t face1 = rd32(file, 16);
        wr32(file, face1, tag("wOF2"));
        FontLoadOptions options;
        options.face_index = 1;
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(file.data(), file.size(), &why, options));
        CHECK(contains(why, "WOFF2"));
    }
    // Two bytes are not enough to say anything but that.
    {
        const Bytes file = {0x80, 0x01};
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(!why.empty());
    }
}

// --- 3. cmap --------------------------------------------------------------------------------

TEST(cmap_format_0_maps_bytes_and_leaves_out_glyph_zero)
{
    const Bytes sub = cmap_sub0({{0x41, 1}, {0x42, 2}, {0x43, 0}, {0x44, 3}, {0xFF, 5}});
    CHECK_EQ(sub.size(), size_t(262));
    const Bytes file = with_cmap(cmap_table({{3, 1, sub}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_platform(), 3);
    CHECK_EQ(font.cmap_encoding(), 1);
    CHECK_EQ(font.cmap_format(), 0);
    check_cmap(font, {{0x41, 0x42, 1}, {0x44, 0x44, 3}, {0xFF, 0xFF, 5}});
    check_maps(font, 0x41, 1);
    check_maps(font, 0x42, 2);
    check_unmapped(font, 0x43);
    check_maps(font, 0x44, 3);
    check_maps(font, 0xFF, 5);
    check_unmapped(font, 0x00);
    check_unmapped(font, 0x40);
    check_unmapped(font, 0x100);

    // 261 bytes of it is a truncated table, and the load says so.
    Bytes cut = cmap_table({{3, 1, sub}});
    cut.pop_back();
    const Bytes short_file = with_cmap(cut);
    why.clear();
    CHECK(!font.load(short_file.data(), short_file.size(), &why));
    CHECK(contains(why, "format 0"));
}

TEST(cmap_format_6_maps_a_trimmed_array_and_leaves_out_glyph_zero)
{
    const Bytes file = with_cmap(cmap_table({{3, 1, cmap_sub6(0x30, {1, 2, 0, 4})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_format(), 6);
    check_cmap(font, {{0x30, 0x31, 1}, {0x33, 0x33, 4}});
    check_unmapped(font, 0x2F);
    check_maps(font, 0x30, 1);
    check_maps(font, 0x31, 2);
    check_unmapped(font, 0x32);
    check_maps(font, 0x33, 4);
    check_unmapped(font, 0x34);

    // Entries that would pass U+FFFF are not codes of a 16-bit format.
    const Bytes top = with_cmap(cmap_table({{3, 1, cmap_sub6(0xFFFE, {1, 2, 3, 4})}}));
    REQUIRE(font.load(top.data(), top.size(), &why));
    check_cmap(font, {{0xFFFE, 0xFFFF, 1}});

    // An entry count the subtable does not hold.
    Bytes lying = cmap_sub6(0x30, {1, 2, 0, 4});
    wr16(lying, 8, 500);
    const Bytes bad = with_cmap(cmap_table({{3, 1, lying}}));
    why.clear();
    CHECK(!font.load(bad.data(), bad.size(), &why));
    CHECK(contains(why, "format 6"));
}

TEST(cmap_format_10_maps_a_trimmed_array_of_32_bit_codes)
{
    const Bytes file = with_cmap(cmap_table({{3, 10, cmap_sub10(0x1F600, {3, 4, 0, 5, 5})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_platform(), 3);
    CHECK_EQ(font.cmap_encoding(), 10);
    CHECK_EQ(font.cmap_format(), 10);
    // A range is a run of consecutive glyphs: 5, 5 is two ranges.
    check_cmap(font, {{0x1F600, 0x1F601, 3}, {0x1F603, 0x1F603, 5}, {0x1F604, 0x1F604, 5}});
    check_unmapped(font, 0x1F5FF);
    check_maps(font, 0x1F600, 3);
    check_maps(font, 0x1F601, 4);
    check_unmapped(font, 0x1F602);
    check_maps(font, 0x1F603, 5);
    check_maps(font, 0x1F604, 5);
    check_unmapped(font, 0x1F605);

    // Cut off at U+10FFFF.
    const Bytes top = with_cmap(cmap_table({{3, 10, cmap_sub10(0x10FFFE, {1, 2, 3, 4})}}));
    REQUIRE(font.load(top.data(), top.size(), &why));
    check_cmap(font, {{0x10FFFE, 0x10FFFF, 1}});
    const Bytes beyond = with_cmap(cmap_table({{3, 10, cmap_sub10(0xFFFFFFFEu, {1, 2, 3, 4})}}));
    REQUIRE(font.load(beyond.data(), beyond.size(), &why));
    check_cmap(font, {});

    Bytes lying = cmap_sub10(0x1F600, {3, 4});
    wr32(lying, 16, 0x7FFFFFFFu);
    const Bytes bad = with_cmap(cmap_table({{3, 10, lying}}));
    why.clear();
    CHECK(!font.load(bad.data(), bad.size(), &why));
    CHECK(contains(why, "format 10"));
}

TEST(cmap_format_12_runs_glyph_ids_through_a_group_and_drops_only_a_code_of_glyph_zero)
{
    const Bytes file = with_cmap(cmap_table(
        {{3, 10,
          cmap_sub12or13(
              12, {{0x41, 0x43, 1}, {0x50, 0x53, 0}, {0x60, 0x60, 0}, {0x1F600, 0x1F602, 7}})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_format(), 12);
    // U+0050 is glyph 0 and is not a mapping; the ids run on from 1 after it.
    check_cmap(font, {{0x41, 0x43, 1}, {0x51, 0x53, 1}, {0x1F600, 0x1F602, 7}});
    check_maps(font, 0x41, 1);
    check_maps(font, 0x43, 3);
    check_unmapped(font, 0x50);
    check_maps(font, 0x51, 1);
    check_maps(font, 0x52, 2);
    check_maps(font, 0x53, 3);
    check_unmapped(font, 0x60);
    check_maps(font, 0x1F601, 8);
    CHECK(font.warnings().empty());
}

TEST(cmap_format_13_gives_a_group_one_glyph_and_drops_a_group_of_glyph_zero)
{
    const Bytes file = with_cmap(cmap_table(
        {{0, 6, cmap_sub12or13(13, {{0x100, 0x102, 7}, {0x200, 0x2FF, 0}, {0x300, 0x300, 9}})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_platform(), 0);
    CHECK_EQ(font.cmap_encoding(), 6);
    CHECK_EQ(font.cmap_format(), 13);
    check_cmap(font, {{0x100, 0x102, 7}, {0x300, 0x300, 9}}, true);
    check_maps(font, 0x100, 7);
    check_maps(font, 0x101, 7);
    check_maps(font, 0x102, 7);
    check_unmapped(font, 0x103);
    check_unmapped(font, 0x200);
    check_unmapped(font, 0x280);
    check_unmapped(font, 0x2FF);
    check_maps(font, 0x300, 9);
}

TEST(cmap_format_12_and_13_groups_are_cut_at_the_last_codepoint)
{
    OpenTypeFont font;
    std::string why;
    const Bytes twelve =
        with_cmap(cmap_table({{3, 10, cmap_sub12or13(12, {{0x10FFFE, 0xFFFFFFFFu, 2}})}}));
    REQUIRE(font.load(twelve.data(), twelve.size(), &why));
    check_cmap(font, {{0x10FFFE, 0x10FFFF, 2}});
    check_maps(font, 0x10FFFF, 3);
    check_unmapped(font, 0x110000);
    const Bytes thirteen =
        with_cmap(cmap_table({{0, 6, cmap_sub12or13(13, {{0x10FFFE, 0x110005, 3}})}}));
    REQUIRE(font.load(thirteen.data(), thirteen.size(), &why));
    check_cmap(font, {{0x10FFFE, 0x10FFFF, 3}}, true);
    check_maps(font, 0x10FFFE, 3);
    check_maps(font, 0x10FFFF, 3);
    // A group that starts past the last codepoint is inverted once it is cut.
    const Bytes past =
        with_cmap(cmap_table({{3, 10, cmap_sub12or13(12, {{0x110000, 0x110010, 2}})}}));
    REQUIRE(font.load(past.data(), past.size(), &why));
    check_cmap(font, {});
    CHECK(warned(font.warnings(), "out of order or overlaps"));
}

TEST(cmap_format_12_and_13_groups_out_of_order_are_skipped_and_one_at_the_end_replaces)
{
    OpenTypeFont font;
    std::string why;
    {
        // Inverted; then one that starts inside the last one taken; then one
        // after that one's end, which is taken: a skipped group moves nothing.
        const Bytes file = with_cmap(cmap_table(
            {{3, 10,
              cmap_sub12or13(
                  12, {{0x50, 0x40, 9}, {0x41, 0x45, 1}, {0x43, 0x60, 2}, {0x50, 0x52, 3}})}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_cmap(font, {{0x41, 0x45, 1}, {0x50, 0x52, 3}});
        check_maps(font, 0x43, 3);
        check_unmapped(font, 0x46);
        CHECK(warned(font.warnings(), "out of order or overlaps"));
    }
    {
        // Starting exactly at the last one's end: that one codepoint is replaced.
        const Bytes file = with_cmap(
            cmap_table({{3, 10, cmap_sub12or13(12, {{0x41, 0x45, 1}, {0x45, 0x47, 10}})}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_cmap(font, {{0x41, 0x44, 1}, {0x45, 0x47, 10}});
        check_maps(font, 0x44, 4);
        check_maps(font, 0x45, 10);
        check_maps(font, 0x47, 12);
        CHECK(font.warnings().empty());
    }
    {
        // A group of one codepoint replaced whole, twice over.
        const Bytes file = with_cmap(cmap_table(
            {{3, 10, cmap_sub12or13(12, {{0x41, 0x41, 1}, {0x41, 0x41, 2}, {0x41, 0x41, 3}})}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_cmap(font, {{0x41, 0x41, 3}});
    }
    {
        // An earlier group is not reached by one that starts before the last
        // one's end, even where it would not touch a codepoint already taken.
        const Bytes file = with_cmap(
            cmap_table({{3, 10, cmap_sub12or13(12, {{0x60, 0x62, 1}, {0x41, 0x42, 5}})}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_cmap(font, {{0x60, 0x62, 1}});
        CHECK(warned(font.warnings(), "out of order or overlaps"));
    }
    {
        const Bytes file = with_cmap(cmap_table(
            {{0, 6, cmap_sub12or13(13, {{0x41, 0x43, 5}, {0x43, 0x44, 6}, {0x42, 0x49, 7}})}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_cmap(font, {{0x41, 0x42, 5}, {0x43, 0x44, 6}}, true);
        check_maps(font, 0x41, 5);
        check_maps(font, 0x42, 5);
        check_maps(font, 0x43, 6);
        check_maps(font, 0x44, 6);
        check_unmapped(font, 0x45);
        CHECK(warned(font.warnings(), "out of order or overlaps"));
    }
}

// The header says of lookup(): "The glyph may be past the last glyph, if that
// is what the file says; it is never 0", and of cmap(): "No range maps a
// codepoint to glyph 0".
TEST(cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero)
{
    const Bytes file =
        with_cmap(cmap_table({{3, 10, cmap_sub12or13(12, {{0x41, 0x44, 0xFFFFFFFEu}})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    for (uint32_t cp = 0x41; cp <= 0x44; ++cp) {
        CHECK_CONTEXT("U+%04X", cp);
        uint32_t glyph = 0xDEAD;
        if (font.lookup(cp, &glyph))
            CHECK_NE(glyph, 0u);
    }
    // The two codes whose ids fit are mapped to them and the rest to nothing.
    check_cmap(font, {{0x41, 0x42, 0xFFFFFFFEu}});
    check_maps(font, 0x42, 0xFFFFFFFFu);
    check_unmapped(font, 0x43);
    check_unmapped(font, 0x44);
    CHECK(warned(font.warnings(), "run past 32 bits"));
    // And one layer up: a codepoint the face has is not the missing glyph,
    // and is not a glyph of the font it was never mapped to.
    FontLibrary library;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "wrap", &why));
    const FaceData* face = library.face_data(Face::PROSE);
    for (uint32_t cp = 0x41; cp <= 0x44; ++cp) {
        CHECK_CONTEXT("U+%04X through the face", cp);
        if (face->has(cp))
            CHECK(face->glyph(cp) >= face->font().glyph_count());
    }
}

TEST(the_cmap_subtable_is_picked_in_the_reference_order_whatever_the_table_order)
{
    // (platform, encoding) in preference order; record k maps 'A' to glyph k + 1.
    const std::array<std::array<uint32_t, 2>, 8> order = {
        {{3, 10}, {0, 6}, {0, 4}, {3, 1}, {0, 3}, {0, 2}, {0, 1}, {0, 0}}};
    for (size_t best = 0; best < order.size(); ++best) {
        CHECK_CONTEXT("the best present is (%u, %u)", order[best][0], order[best][1]);
        // The records from `best` on, in the reverse of preference order, the
        // BMP ones in format 4 and the others in format 12.
        std::vector<CmapRecord> records;
        for (size_t k = order.size(); k-- > best;) {
            const uint32_t glyph = static_cast<uint32_t>(k + 1);
            const bool full = order[k][1] == 10 || order[k][1] == 6 || order[k][1] == 4;
            records.push_back(
                {order[k][0], order[k][1],
                 full ? cmap_sub12or13(12, {{'A', 'A', glyph}}) : cmap_sub4({{'A', 'A', glyph}})});
        }
        // And two that are never preferred to any of them.
        records.push_back({3, 0, cmap_sub4({{'A', 'A', 10}})});
        records.push_back({1, 0, cmap_sub0({{'A', 11}})});
        const Bytes file = with_cmap(cmap_table(records));
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.cmap_platform(), static_cast<int>(order[best][0]));
        CHECK_EQ(font.cmap_encoding(), static_cast<int>(order[best][1]));
        check_maps(font, 'A', static_cast<uint32_t>(best + 1));
        CHECK(font.warnings().empty());
    }
}

TEST(a_preferred_cmap_subtable_in_an_unread_format_is_passed_over_with_a_warning)
{
    for (const uint32_t format : {2u, 8u, 14u}) {
        CHECK_CONTEXT("format %u", format);
        const Bytes file = with_cmap(cmap_table({{0, 4, cmap_sub_unread(format)},
                                                 {3, 1, cmap_sub4({{'A', 'B', 1}})},
                                                 {3, 10, cmap_sub_unread(format)}}));
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.cmap_platform(), 3);
        CHECK_EQ(font.cmap_encoding(), 1);
        CHECK_EQ(font.cmap_format(), 4);
        check_cmap(font, {{'A', 'B', 1}});
        CHECK(warned(font.warnings(), "preferred cmap subtable is in a format that is not read"));

        // With nothing else, the font has no cmap this reader reads.
        const Bytes only = with_cmap(
            cmap_table({{0, 4, cmap_sub_unread(format)}, {3, 10, cmap_sub_unread(format)}}));
        why.clear();
        CHECK(!font.load(only.data(), only.size(), &why));
        CHECK(contains(why, "formats that are not read"));
    }
    // The usual home of format 14, (0, 5), is not one of the pairs looked at.
    const Bytes variation =
        with_cmap(cmap_table({{0, 5, cmap_sub_unread(14)}, {3, 1, cmap_sub4({{'A', 'B', 1}})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(variation.data(), variation.size(), &why));
    CHECK(font.warnings().empty());
    const Bytes none = with_cmap(cmap_table({{0, 5, cmap_sub_unread(14)}}));
    why.clear();
    CHECK(!font.load(none.data(), none.size(), &why));
    CHECK(contains(why, "no Unicode, Symbol or Macintosh Roman"));
}

TEST(a_symbol_only_font_loads_with_its_codes_as_they_stand)
{
    const Bytes file = with_cmap(cmap_table({{3, 0, cmap_sub4({{0xF041, 0xF043, 1}})}}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.cmap_platform(), 3);
    CHECK_EQ(font.cmap_encoding(), 0);
    CHECK_EQ(font.cmap_format(), 4);
    check_cmap(font, {{0xF041, 0xF043, 1}});
    check_maps(font, 0xF041, 1);
    check_unmapped(font, 0x41);
    CHECK(warned(font.warnings(), "no Unicode cmap subtable"));

    // A Unicode subtable beside it is preferred, and nothing is said.
    const Bytes both = with_cmap(
        cmap_table({{3, 0, cmap_sub4({{0xF041, 0xF043, 1}})}, {0, 3, cmap_sub4({{'A', 'A', 5}})}}));
    REQUIRE(font.load(both.data(), both.size(), &why));
    CHECK_EQ(font.cmap_platform(), 0);
    check_cmap(font, {{'A', 'A', 5}});
    CHECK(font.warnings().empty());
}

TEST(a_macintosh_roman_only_font_loads_with_its_codes_taken_to_unicode)
{
    // Mac OS Roman: 0x8A is a-dieresis, 0xA5 the bullet, 0xDB the euro sign.
    const std::vector<CmapGroup> expected = {
        {0x41, 0x41, 1}, {0xE4, 0xE4, 2}, {0x2022, 0x2022, 3}, {0x20AC, 0x20AC, 4}};
    std::vector<uint32_t> trimmed(0xDB - 0x41 + 1, 0);
    trimmed[0] = 1;
    trimmed[0x8A - 0x41] = 2;
    trimmed[0xA5 - 0x41] = 3;
    trimmed[0xDB - 0x41] = 4;
    const Bytes subtables[] = {cmap_sub0({{0x41, 1}, {0x8A, 2}, {0xA5, 3}, {0xDB, 4}}),
                               cmap_sub6(0x41, trimmed)};
    for (const Bytes& sub : subtables) {
        CHECK_CONTEXT("format %u", rd16(sub, 0));
        const Bytes file = with_cmap(cmap_table({{1, 0, sub}}));
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.cmap_platform(), 1);
        CHECK_EQ(font.cmap_encoding(), 0);
        CHECK_EQ(font.cmap_format(), static_cast<int>(rd16(sub, 0)));
        check_cmap(font, expected);
        check_maps(font, 0x41, 1);
        check_maps(font, 0xE4, 2);
        check_maps(font, 0x2022, 3);
        check_maps(font, 0x20AC, 4);
        // The Mac codes themselves are not codepoints the font has.
        check_unmapped(font, 0x8A);
        check_unmapped(font, 0xA5);
        check_unmapped(font, 0xDB);
        CHECK(warned(font.warnings(), "no Unicode cmap subtable"));
    }
    // A one-byte encoding has no use for a 32-bit format.
    const Bytes wide = with_cmap(cmap_table({{1, 0, cmap_sub12or13(12, {{'A', 'A', 1}})}}));
    OpenTypeFont font;
    std::string why;
    CHECK(!font.load(wide.data(), wide.size(), &why));
    CHECK(contains(why, "formats that are not read"));
    // Symbol is preferred to Macintosh Roman.
    const Bytes both = with_cmap(
        cmap_table({{1, 0, cmap_sub0({{0x41, 1}})}, {3, 0, cmap_sub4({{0xF041, 0xF041, 2}})}}));
    REQUIRE(font.load(both.data(), both.size(), &why));
    CHECK_EQ(font.cmap_platform(), 3);
    CHECK_EQ(font.cmap_encoding(), 0);
}

// --- 4. hmtx and loca -----------------------------------------------------------------------

TEST(an_hmtx_with_no_full_record_loads_with_no_advances_and_fails_the_strict_pass)
{
    Sfnt sfnt = squares(4).sfnt();
    wr16(sfnt.table(tag("hhea")), 34, 0);
    const Bytes file = sfnt.build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.hmetric_count(), 0u);
    for (uint32_t g = 0; g < 4; ++g)
        CHECK_EQ(font.advance(g), 0);
    // With no full record the table is side bearings from its first byte:
    // the builder's (600, 0) pairs read as bearings 600, 0, 600, 0.
    CHECK_EQ(font.left_side_bearing(0), 600);
    CHECK_EQ(font.left_side_bearing(1), 0);
    CHECK_EQ(font.left_side_bearing(2), 600);
    CHECK_EQ(font.left_side_bearing(3), 0);
    CHECK(warned(font.warnings(), "numberOfHMetrics is zero"));
    // And a simple glyph's box starts at its side bearing.
    check_box(font, 3, square_box(3));
    check_box(font, 2, GlyphBounds{600, 0, 720, 120});
    why.clear();
    CHECK(!font.strict_check(&why));
    CHECK(contains(why, "numberOfHMetrics"));

    FontLibrary library;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "zero", &why));
    const FontMetrics* m = library.metrics(Face::PROSE, 16.0, &why);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->advance('A'), 0.0);
}

TEST(a_loca_one_entry_short_ends_the_last_glyph_at_the_end_of_glyf)
{
    for (const bool long_loca : {true, false}) {
        CHECK_CONTEXT("%s loca", long_loca ? "long" : "short");
        MiniFont mini = squares(4);
        mini.long_loca = long_loca;
        Sfnt sfnt = mini.sfnt();
        Bytes& loca = sfnt.table(tag("loca"));
        const size_t width = long_loca ? 4 : 2;
        REQUIRE_EQ(loca.size(), 5 * width);
        loca.resize(4 * width);
        const Bytes file = sfnt.build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK(warned(font.warnings(), "loca table has no entry after the last glyph"));
        for (uint32_t g = 0; g < 4; ++g)
            check_box(font, g, square_box(g));
        why.clear();
        CHECK(!font.strict_check(&why));
        CHECK(contains(why, "loca"));

        // Two entries short is a truncated table.
        loca.resize(3 * width);
        const Bytes shorter = sfnt.build();
        why.clear();
        CHECK(!font.load(shorter.data(), shorter.size(), &why));
        CHECK(contains(why, "loca"));
    }
}

// --- 5. post names ---------------------------------------------------------------------------

namespace
{

bool names_of(const Bytes& file, std::vector<std::string>& names)
{
    OpenTypeFont font;
    std::string why;
    if (!font.load(file.data(), file.size(), &why)) {
        FAIL("load: %s", why.c_str());
        return false;
    }
    return font.glyph_order_names(names);
}

void check_names(const std::vector<std::string>& got, const std::vector<const char*>& want)
{
    REQUIRE_EQ(got.size(), want.size());
    for (size_t g = 0; g < want.size(); ++g) {
        CHECK_CONTEXT("glyph %zu", g);
        CHECK_EQ(got[g], want[g]);
    }
}

} // namespace

TEST(post_format_1_names_glyphs_in_the_standard_macintosh_order)
{
    std::vector<std::string> names;
    REQUIRE(names_of(with(squares(6), tag("post"), post_table(0x00010000u)), names));
    check_names(names, {".notdef", ".null", "nonmarkingreturn", "space", "exclam", "quotedbl"});

    // All 258 of them; the ends, and the letters, which sit 29 below ASCII.
    REQUIRE(names_of(with(squares(258), tag("post"), post_table(0x00010000u)), names));
    REQUIRE_EQ(names.size(), size_t(258));
    CHECK_EQ(names[36], "A");
    CHECK_EQ(names[61], "Z");
    CHECK_EQ(names[68], "a");
    CHECK_EQ(names[93], "z");
    CHECK_EQ(names[19], "zero");
    CHECK_EQ(names[257], "dcroat");

    // One glyph more than the order names: the font has no such names.
    names.assign(1, "stale");
    CHECK(!names_of(with(squares(259), tag("post"), post_table(0x00010000u)), names));
    CHECK(names.empty());
}

TEST(post_format_2_names_glyphs_by_standard_index_and_by_its_own_strings)
{
    std::vector<std::string> names;
    // 258 on are the Pascal strings in order; an index may be used in any order.
    REQUIRE(names_of(with(squares(6), tag("post"),
                          post_table(0x00020000u, {0, 36, 259, 258, 68, 3}, {"alpha", "beta.alt"})),
                     names));
    check_names(names, {".notdef", "A", "beta.alt", "alpha", "a", "space"});
}

TEST(post_format_2_a_string_that_is_not_there_names_the_glyph_by_its_number)
{
    std::vector<std::string> names;
    // Index 260 has no string at all; glyph 2 is "glyph00002".
    REQUIRE(names_of(
        with(squares(4), tag("post"), post_table(0x00020000u, {0, 258, 260, 259}, {"one", "two"})),
        names));
    check_names(names, {".notdef", "one", "glyph00002", "two"});

    // A string whose length byte runs past the table is empty as well.
    Bytes post = post_table(0x00020000u, {0, 258, 259, 36}, {"one", "two"});
    post[post.size() - 4] = 200; // the length byte of "two"
    REQUIRE(names_of(with(squares(4), tag("post"), post), names));
    check_names(names, {".notdef", "one", "glyph00002", "A"});

    // Fewer glyphs named than the font has: the rest go by number.
    REQUIRE(names_of(with(squares(5), tag("post"), post_table(0x00020000u, {0, 36}, {})), names));
    check_names(names, {".notdef", "A", "glyph00002", "glyph00003", "glyph00004"});

    // More glyphs named than the font has: the font's count stands.
    REQUIRE(names_of(
        with(squares(2), tag("post"), post_table(0x00020000u, {0, 36, 37, 38, 258}, {"extra"})),
        names));
    check_names(names, {".notdef", "A"});
}

TEST(post_format_2_a_name_met_again_is_numbered_past_names_the_table_uses)
{
    std::vector<std::string> names;
    // "a" is standard name 68.
    REQUIRE(names_of(with(squares(4), tag("post"), post_table(0x00020000u, {0, 68, 68, 68}, {})),
                     names));
    check_names(names, {".notdef", "a", "a.1", "a.2"});

    // The table itself names a glyph "a.1": the duplicate goes to "a.2", the
    // glyph that owns "a.1" keeps it, and the next duplicate is "a.3".
    REQUIRE(names_of(
        with(squares(5), tag("post"), post_table(0x00020000u, {0, 68, 68, 258, 68}, {"a.1"})),
        names));
    check_names(names, {".notdef", "a", "a.2", "a.1", "a.3"});

    // The same when the owner comes first.
    REQUIRE(names_of(
        with(squares(4), tag("post"), post_table(0x00020000u, {0, 258, 68, 68}, {"a.1"})), names));
    check_names(names, {".notdef", "a.1", "a", "a.2"});

    // Unnamed glyphs are distinct by number already; a glyph the table names
    // with another's number is the duplicate.
    REQUIRE(names_of(
        with(squares(4), tag("post"), post_table(0x00020000u, {0, 300, 300, 258}, {"glyph00001"})),
        names));
    check_names(names, {".notdef", "glyph00001", "glyph00002", "glyph00001.1"});
}

TEST(a_font_without_post_names_has_no_glyph_order_names)
{
    std::vector<std::string> names = {"stale"};
    CHECK(!names_of(with(squares(4), tag("post"), post_table(0x00030000u)), names));
    CHECK(names.empty());
    CHECK(!names_of(squares(4).build(), names)); // the builder's own post is version 3
    Sfnt sfnt = squares(4).sfnt();
    sfnt.remove(tag("post"));
    CHECK(!names_of(sfnt.build(), names));
    CHECK(!names_of(with(squares(4), tag("post"), post_table(0x00025000u)), names));
    // A format 2 table cut before its indices.
    Bytes cut = post_table(0x00020000u, {0, 36, 37, 38}, {});
    cut.resize(36);
    CHECK(!names_of(with(squares(4), tag("post"), cut), names));
    CHECK(names.empty());
    // Not loaded at all.
    OpenTypeFont empty;
    names.assign(1, "stale");
    CHECK(!empty.glyph_order_names(names));
    CHECK(names.empty());
}

TEST(unencoded_math_variants_are_minted_in_name_order_from_post_and_in_glyph_order_without)
{
    // mini_math: 'A' is glyph 1; glyphs 2..6 are its variants, unencoded.
    const Bytes math = math_table(1, {2, 3, 4, 5, 6});
    const auto minted_glyphs = [&](const Bytes& post) {
        Sfnt sfnt = mini_math().sfnt();
        sfnt.table(tag("MATH")) = math;
        sfnt.table(tag("post")) = post;
        const Bytes file = sfnt.build();
        std::vector<uint32_t> glyphs;
        FontLibrary library;
        std::string why;
        if (!library.register_font(Face::MATH, file.data(), file.size(), "math", &why)) {
            FAIL("register_font: %s", why.c_str());
            return glyphs;
        }
        const std::vector<SyntheticCodepoint>& minted =
            library.face_data(Face::MATH)->synthetic_codepoints();
        for (size_t i = 0; i < minted.size(); ++i) {
            CHECK_EQ(minted[i].codepoint, SYNTHETIC_FIRST - static_cast<uint32_t>(i));
            glyphs.push_back(minted[i].glyph);
        }
        return glyphs;
    };
    const auto check_order = [](const std::vector<uint32_t>& got,
                                const std::vector<uint32_t>& want) {
        REQUIRE_EQ(got.size(), want.size());
        for (size_t i = 0; i < want.size(); ++i)
            CHECK_EQ(got[i], want[i]);
    };
    // Bytewise: "B" < "Z" < "a" < "a.alt" < "b".
    check_order(minted_glyphs(post_table(0x00020000u, {0, 36, 258, 259, 260, 261, 262},
                                         {"b", "B", "a.alt", "Z", "a"})),
                {3, 5, 6, 4, 2});
    // Bytes above 0x7F sort above ASCII, as unsigned.
    check_order(minted_glyphs(post_table(0x00020000u, {0, 36, 258, 259, 260, 261, 262},
                                         {"\xE9", "z", "\x80", "A.x", "~"})),
                {5, 3, 6, 4, 2});
    // Names made up for duplicates count: "x", "x.1", "x.2", "w", "x.3".
    check_order(
        minted_glyphs(post_table(0x00020000u, {0, 36, 258, 258, 258, 259, 258}, {"x", "w"})),
        {5, 2, 3, 4, 6});
    // No names: glyph order.
    check_order(minted_glyphs(post_table(0x00030000u)), {2, 3, 4, 5, 6});
}

// --- 6. CID-keyed CFF ------------------------------------------------------------------------

namespace
{

// The boxes of two_dict_cid's glyphs: a square of 100 or of 300 from (10, 20).
const GlyphBounds FD0_BOX = {10, 20, 110, 120};
const GlyphBounds FD1_BOX = {10, 20, 310, 320};
const GlyphBounds NO_BOX = {};

} // namespace

TEST(a_cid_keyed_font_draws_each_glyph_with_the_subroutines_of_its_own_font_dict)
{
    struct Select {
        const char* what;
        Bytes bytes;
    };
    const Select selects[] = {
        {"FDSelect format 0", fdselect0({0, 0, 1, 0, 1})},
        {"FDSelect format 3", fdselect3({{0, 0}, {2, 1}, {3, 0}, {4, 1}}, 5)},
    };
    for (const Select& select : selects) {
        CHECK_CONTEXT("%s", select.what);
        CidCff cff = two_dict_cid();
        cff.fd_select = select.bytes;
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK(font.is_cff());
        CHECK(font.is_cid_keyed());
        CHECK_EQ(font.glyph_count(), 5u);
        check_box(font, 0, GlyphBounds{0, 0, 50, 50});
        check_box(font, 1, FD0_BOX);
        check_box(font, 2, FD1_BOX);
        check_box(font, 3, FD0_BOX);
        check_box(font, 4, FD1_BOX);
        CHECK(font.warnings().empty());
        if (!font.strict_check(&why))
            FAIL("strict: %s", why.c_str());

        // Numbers, not names.
        for (uint32_t g = 0; g < 5; ++g)
            CHECK(font.glyph_name(g).empty());
        std::vector<std::string> names;
        REQUIRE(font.glyph_order_names(names));
        check_names(names, {".notdef", "cid00100", "cid00007", "cid65535", "cid00001"});
    }
}

TEST(a_cid_keyed_font_without_a_charset_numbers_its_glyphs_as_they_come)
{
    CidCff cff = two_dict_cid();
    cff.fd_select = fdselect0({0, 0, 1, 0, 1});
    cff.cids.clear();
    const Bytes file = cid_font(cff).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    std::vector<std::string> names;
    REQUIRE(font.glyph_order_names(names));
    check_names(names, {".notdef", "cid00001", "cid00002", "cid00003", "cid00004"});
    check_box(font, 2, FD1_BOX);
}

TEST(a_malformed_fdselect_fails_the_load_and_says_fdselect)
{
    struct Bad {
        const char* what;
        Bytes select;
    };
    const Bad bad[] = {
        {"format 3 not starting at glyph 0", fdselect3({{1, 0}, {2, 1}}, 5)},
        {"format 3 ranges not increasing", fdselect3({{0, 0}, {3, 1}, {2, 0}}, 5)},
        {"format 3 ranges repeating", fdselect3({{0, 0}, {2, 1}, {2, 0}}, 5)},
        {"format 3 sentinel not past the last range", fdselect3({{0, 0}, {2, 1}}, 2)},
        {"format 3 with no ranges", fdselect3({}, 5)},
        {"format 7", Bytes{7, 0, 0, 0, 0, 0, 0, 0}},
    };
    for (const Bad& b : bad) {
        CHECK_CONTEXT("%s", b.what);
        CidCff cff = two_dict_cid();
        cff.fd_select = b.select;
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(file.data(), file.size(), &why));
        if (!contains(why, "FDSelect"))
            FAIL("the reason does not name FDSelect: \"%s\"", why.c_str());
    }
    {
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect0({0, 0, 1, 0, 1});
        cff.with_fd_select = false;
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(contains(why, "FDSelect"));
    }
    {
        // Formats 0 and 3 that the table ends inside of: a second FDSelect
        // is put at the very end of the table, short, and the Top DICT is
        // pointed at it. The operand is the last in the Top DICT, `29
        // <offset> 12 37`, after ROS (17 bytes), charset (6), CharStrings (6)
        // and FDArray (7).
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect0({0, 0, 1, 0, 1});
        MiniFont mini = cid_font(cff);
        const size_t top_data = 4 + 15 + 2 + 1 + 8;
        const size_t operand = top_data + 17 + 6 + 6 + 7 + 1;
        REQUIRE_EQ(rd32(mini.cff, operand), static_cast<uint32_t>(cff.fd_select_at));
        wr32(mini.cff, operand, static_cast<uint32_t>(mini.cff.size()));
        MiniFont three = mini;
        append(mini.cff, Bytes{0, 0, 0});                 // format 0, two of five glyphs
        append(three.cff, Bytes{3, 0, 2, 0, 0, 0, 0, 2}); // format 3, the second range cut
        const Bytes cut3 = three.build();
        OpenTypeFont font3;
        std::string why3;
        CHECK(!font3.load(cut3.data(), cut3.size(), &why3));
        CHECK(contains(why3, "FDSelect"));
        const Bytes file = mini.build();
        OpenTypeFont font;
        std::string why;
        CHECK(!font.load(file.data(), file.size(), &why));
        CHECK(contains(why, "FDSelect"));
    }
}

TEST(a_glyph_whose_font_dict_is_not_there_has_no_box_and_fails_the_strict_pass)
{
    {
        // Font DICT 5 of 2.
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect0({0, 0, 5, 0, 1});
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_box(font, 1, FD0_BOX);
        check_box(font, 2, NO_BOX);
        check_box(font, 3, FD0_BOX);
        check_box(font, 4, FD1_BOX);
        CHECK(!font.strict_check(&why));
        CHECK(contains(why, "Font DICT"));
    }
    {
        // Exactly one past the last Font DICT.
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect3({{0, 0}, {4, 2}}, 5);
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_box(font, 3, FD0_BOX);
        check_box(font, 4, NO_BOX);
        CHECK(!font.strict_check(&why));
        CHECK(contains(why, "Font DICT"));
    }
    {
        // A format 3 that closes at glyph 3 of 5: glyphs 3 and 4 have none.
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect3({{0, 0}, {2, 1}}, 3);
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_box(font, 1, FD0_BOX);
        check_box(font, 2, FD1_BOX);
        check_box(font, 3, NO_BOX);
        check_box(font, 4, NO_BOX);
        CHECK(!font.strict_check(&why));
        CHECK(contains(why, "Font DICT"));
        // Layout still measures the font; the atlas does not get it.
        FontLibrary library;
        REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "cid", &why));
        const FontMetrics* m = library.metrics(Face::PROSE, 1000.0, &why);
        REQUIRE(m != nullptr);
        CHECK_EQ(m->advance('C'), 600.0);
        CHECK_EQ(m->bbox('C').width(), 0.0);
        CHECK_EQ(m->bbox('B').width(), 300.0);
    }
    {
        // A sentinel past the last glyph is the usual way to close; and one
        // far past it is no different.
        CidCff cff = two_dict_cid();
        cff.fd_select = fdselect3({{0, 0}, {2, 1}}, 0xFFFF);
        const Bytes file = cid_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_box(font, 4, FD1_BOX);
        CHECK(font.strict_check(&why));
    }
}

TEST(a_cid_keyed_font_without_its_font_dicts_fails_the_load)
{
    CidCff cff = two_dict_cid();
    cff.fd_select = fdselect0({0, 0, 1, 0, 1});
    cff.with_fd_array = false;
    Bytes file = cid_font(cff).build();
    OpenTypeFont font;
    std::string why;
    CHECK(!font.load(file.data(), file.size(), &why));
    CHECK(contains(why, "Font DICT"));

    // An FDArray that is an empty INDEX.
    cff.with_fd_array = true;
    cff.fd_array_override = cff_index({});
    file = cid_font(cff).build();
    why.clear();
    CHECK(!font.load(file.data(), file.size(), &why));
    CHECK(contains(why, "Font DICT"));
}

TEST(font_dicts_that_name_the_same_subrs_index_share_it)
{
    CidCff cff = two_dict_cid();
    cff.fd_select = fdselect0({0, 0, 1, 0, 1});
    cff.share_subrs = true;
    const Bytes file = cid_font(cff).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    // Both Font DICTs draw the first one's square.
    check_box(font, 1, FD0_BOX);
    check_box(font, 2, FD0_BOX);
    check_box(font, 4, FD0_BOX);
    CHECK(font.warnings().empty());
    if (!font.strict_check(&why))
        FAIL("strict: %s", why.c_str());
}

TEST(a_font_dict_without_subroutines_draws_nothing_that_calls_one)
{
    CidCff cff = two_dict_cid();
    cff.fd_select = fdselect0({0, 0, 1, 0, 1});
    cff.fd_subrs[1].clear(); // Font DICT 1's Private DICT names no Subrs
    cff.charstrings[4] = cs_square(5, 6, 70);
    const Bytes file = cid_font(cff).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    check_box(font, 1, FD0_BOX);
    check_box(font, 2, NO_BOX); // calls subroutine 0 of none
    check_box(font, 4, GlyphBounds{5, 6, 75, 76});
}

TEST(the_predefined_expert_charsets_name_glyphs)
{
    MiniCff cff;
    cff.charstrings = {cs_square(0, 0, 100), cs_square(0, 0, 200), cs_square(0, 0, 300),
                       cs_square(0, 0, 400)};
    {
        MiniCff expert = cff;
        dict_int(expert.extra_top, 1);
        put8(expert.extra_top, 15);
        const Bytes file = cff_font(expert).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK(!font.is_cid_keyed());
        CHECK_EQ(font.glyph_name(0), ".notdef");
        CHECK_EQ(font.glyph_name(1), "space");
        CHECK_EQ(font.glyph_name(2), "exclamsmall");
        CHECK_EQ(font.glyph_name(3), "Hungarumlautsmall");
        std::vector<std::string> names;
        REQUIRE(font.glyph_order_names(names));
        check_names(names, {".notdef", "space", "exclamsmall", "Hungarumlautsmall"});
    }
    {
        MiniCff subset = cff;
        dict_int(subset.extra_top, 2);
        put8(subset.extra_top, 15);
        const Bytes file = cff_font(subset).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.glyph_name(1), "space");
        CHECK_EQ(font.glyph_name(2), "dollaroldstyle");
        CHECK_EQ(font.glyph_name(3), "dollarsuperior");
    }
    {
        // No charset operator is ISOAdobe: a glyph's id is its string id.
        const Bytes file = cff_font(cff).build();
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.glyph_name(1), "space");
        CHECK_EQ(font.glyph_name(2), "exclam");
        CHECK_EQ(font.glyph_name(3), "quotedbl");
    }
}

// --- 7. Type 2 operators ---------------------------------------------------------------------

namespace
{

struct Glyph {
    const char* charstring;
    GlyphBounds box;
};

// A CFF font whose glyph 1 + i is `glyphs[i]`, with `global` and `local`
// subroutines.
Bytes charstring_font(const std::vector<Glyph>& glyphs, const std::vector<Bytes>& local = {},
                      const std::vector<Bytes>& global = {})
{
    MiniCff cff;
    cff.charstrings.push_back(cs_square(0, 0, 10));
    for (const Glyph& g : glyphs)
        cff.charstrings.push_back(cs_asm(g.charstring));
    cff.local_subrs = local;
    cff.global_subrs = global;
    return cff_font(cff).build();
}

void check_charstrings(const std::vector<Glyph>& glyphs)
{
    const Bytes file = charstring_font(glyphs);
    OpenTypeFont font;
    std::string why;
    if (!font.load(file.data(), file.size(), &why)) {
        FAIL("load: %s", why.c_str());
        return;
    }
    for (size_t i = 0; i < glyphs.size(); ++i) {
        CHECK_CONTEXT("%s", glyphs[i].charstring);
        check_box(font, static_cast<uint32_t>(i + 1), glyphs[i].box);
    }
    for (const std::string& warning : font.warnings())
        FAIL("warning: %s", warning.c_str());
}

// What follows an expression pair: a corner of 10 by 10 from where they moved to.
#define CORNER " rmoveto 10 hlineto 10 vlineto endchar"

} // namespace

TEST(type2_arithmetic_operators_compute_the_coordinates_they_should)
{
    check_charstrings({
        {"100 2 div 1 4 div" CORNER, {50, 0.25, 60, 10.25}},
        {"30 12 add 30 12 sub" CORNER, {42, 18, 52, 28}},
        {"6 7 mul 25 neg" CORNER, {42, -25, 52, -15}},
        {"-25 abs 144 sqrt" CORNER, {25, 12, 35, 22}},
        {"25 abs -7 neg" CORNER, {25, 7, 35, 17}},
        {"2.25 sqrt 0.5 0.25 mul" CORNER, {1.5, 0.125, 11.5, 10.125}},
        {"-7 2 div 1 -8 div" CORNER, {-3.5, -0.125, 6.5, 9.875}},
        {"100.5 20" CORNER, {100.5, 20, 110.5, 30}},
        {"-0.5 -100.25" CORNER, {-0.5, -100.25, 9.5, -90.25}},
    });
}

TEST(type2_stack_operators_move_what_they_should)
{
    check_charstrings({
        // dup: 7 7 mul; exch: 3 10 -> 10 3, 10 - 3.
        {"7 dup mul 3 10 exch sub" CORNER, {49, 7, 59, 17}},
        {"40 99 drop 60" CORNER, {40, 60, 50, 70}},
        // index n copies the element n below the top: 11 22 33 | 22.
        {"11 22 33 1 index add add add 0" CORNER, {88, 0, 98, 10}},
        {"11 22 33 0 index add add add 0" CORNER, {99, 0, 109, 10}},
        {"11 22 33 2 index add add add 0" CORNER, {77, 0, 87, 10}},
        // A negative index copies the top.
        {"11 22 33 -1 index add add add 0" CORNER, {99, 0, 109, 10}},
    });
}

TEST(type2_roll_shifts_towards_the_top_when_positive)
{
    // Two lines from the origin by the four operands, taken in pairs.
    check_charstrings({
        {"0 0 rmoveto 10 20 -30 70 rlineto endchar", {-20, 0, 10, 90}},
        // 4 1 roll: 70 10 20 -30.
        {"0 0 rmoveto 10 20 -30 70 4 1 roll rlineto endchar", {0, -20, 90, 10}},
        // 4 -1 roll: 20 -30 70 10.
        {"0 0 rmoveto 10 20 -30 70 4 -1 roll rlineto endchar", {0, -30, 90, 0}},
        // 3 1 roll: 10 | 70 20 -30.
        {"0 0 rmoveto 10 20 -30 70 3 1 roll rlineto endchar", {0, 0, 30, 70}},
        // 4 2 roll: -30 70 10 20.
        {"0 0 rmoveto 10 20 -30 70 4 2 roll rlineto endchar", {-30, 0, 0, 90}},
        // A shift is taken modulo the count: 5 and -7 are 1; 0 and 4 are none.
        {"0 0 rmoveto 10 20 -30 70 4 5 roll rlineto endchar", {0, -20, 90, 10}},
        {"0 0 rmoveto 10 20 -30 70 4 -7 roll rlineto endchar", {0, -20, 90, 10}},
        {"0 0 rmoveto 10 20 -30 70 4 0 roll rlineto endchar", {-20, 0, 10, 90}},
        {"0 0 rmoveto 10 20 -30 70 4 4 roll rlineto endchar", {-20, 0, 10, 90}},
        // Rolling nothing, and rolling one, change nothing.
        {"0 0 rmoveto 10 20 -30 70 0 3 roll rlineto endchar", {-20, 0, 10, 90}},
        {"0 0 rmoveto 10 20 -30 70 1 1 roll rlineto endchar", {-20, 0, 10, 90}},
    });
}

TEST(type2_storage_and_conditional_operators_answer_as_specified)
{
    check_charstrings({
        {"42 0 put 7 31 put 0 get 31 get" CORNER, {42, 7, 52, 17}},
        // A later put replaces; a slot holds a fraction.
        {"1 5 put 2.5 5 put 5 get 0" CORNER, {2.5, 0, 12.5, 10}},
        {"5 3 and 60 mul 5 0 and 60 mul" CORNER, {60, 0, 70, 10}},
        {"0 0 and 60 mul 0 7 or 60 mul" CORNER, {0, 60, 10, 70}},
        {"0 0 or 60 mul -2 0 or 60 mul" CORNER, {0, 60, 10, 70}},
        {"0 not 60 mul 9 not 60 mul" CORNER, {60, 0, 70, 10}},
        {"4 4 eq 60 mul 4 5 eq 60 mul" CORNER, {60, 0, 70, 10}},
        // ifelse: s1 s2 v1 v2 leaves s1 if v1 <= v2, else s2.
        {"11 22 3 4 ifelse 11 22 4 3 ifelse" CORNER, {11, 22, 21, 32}},
        {"11 22 4 4 ifelse 11 22 -4 -5 ifelse" CORNER, {11, 22, 21, 32}},
    });
}

TEST(type2_operators_that_are_not_read_or_are_misused_leave_the_glyph_without_a_box)
{
    // Each draws a square first: a charstring that fails has no box at all.
#define SQUARE "0 0 rmoveto 50 hlineto 50 vlineto "
    const char* const bad[] = {
        SQUARE "random endchar",
        SQUARE "5 0 div endchar",
        SQUARE "-4 sqrt endchar",
        SQUARE "1 32 put endchar",
        SQUARE "1 0.5 put endchar",
        SQUARE "1 -1 put endchar",
        SQUARE "32 get endchar",
        SQUARE "0.5 get endchar",
        SQUARE "-1 get endchar",
        SQUARE "1 2 3 5 0 roll endchar",  // five of three
        SQUARE "1 2 3 4 1 roll endchar",  // four of three
        SQUARE "1 2 3 -1 0 roll endchar", // a negative count
        SQUARE "1 2 3 1.5 1 roll endchar",
        SQUARE "1 2 3 3 0.5 roll endchar",
        SQUARE "1 2 3 3 index endchar", // the fourth of three
        SQUARE "add endchar",
        SQUARE "1 add endchar",
        SQUARE "sub endchar",
        SQUARE "1 mul endchar",
        SQUARE "1 div endchar",
        SQUARE "neg endchar",
        SQUARE "abs endchar",
        SQUARE "sqrt endchar",
        SQUARE "dup endchar",
        SQUARE "1 exch endchar",
        SQUARE "drop endchar",
        SQUARE "1 index endchar",
        SQUARE "1 roll endchar",
        SQUARE "1 put endchar",
        SQUARE "get endchar",
        SQUARE "1 and endchar",
        SQUARE "1 or endchar",
        SQUARE "not endchar",
        SQUARE "1 eq endchar",
        SQUARE "1 2 3 ifelse endchar",
    };
    std::vector<Glyph> glyphs = {{SQUARE "endchar", {0, 0, 50, 50}}};
    for (const char* text : bad)
        glyphs.push_back({text, GlyphBounds{}});
#undef SQUARE
    const Bytes file = charstring_font(glyphs);
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK(font.warnings().empty());
    check_box(font, 1, glyphs[0].box);
    for (size_t i = 1; i < glyphs.size(); ++i) {
        CHECK_CONTEXT("%s", glyphs[i].charstring);
        const size_t before = font.warnings().size();
        check_box(font, static_cast<uint32_t>(i + 1), GlyphBounds{});
        if (i == 1) {
            CHECK(font.warnings().size() > before);
            CHECK(warned(font.warnings(), "operator that is not read"));
        }
    }
    // The font is still a font, and its good glyph is still good.
    CHECK(font.loaded());
    check_box(font, 1, glyphs[0].box);
    CHECK_EQ(font.advance(2), 600);

    // A reserved escaped operator, which the assembler has no name for.
    MiniCff cff;
    cff.charstrings = {cs_square(0, 0, 10), cs_asm("0 0 rmoveto 50 hlineto 50 vlineto")};
    cs_esc(cff.charstrings[1], 99);
    cs_op(cff.charstrings[1], 14);
    const Bytes reserved = cff_font(cff).build();
    REQUIRE(font.load(reserved.data(), reserved.size(), &why));
    check_box(font, 1, GlyphBounds{});
    CHECK(warned(font.warnings(), "operator that is not read"));
}

// The arithmetic operators work in doubles, and a charstring can square a
// number until it is no number a coordinate can be. Whatever such a glyph is
// taken for, its box is not infinite and not NaN: layout multiplies it.
TEST(type2_arithmetic_that_overflows_gives_no_box_that_is_not_finite)
{
    // 32767 squared seven times is past the largest double.
#define HUGE_X "32767 dup mul dup mul dup mul dup mul dup mul dup mul dup mul"
    const char* const glyphs[] = {
        // An infinite x.
        HUGE_X " 0 rmoveto 10 hlineto 10 vlineto endchar",
        // Infinity minus infinity, which is NaN, for x.
        HUGE_X " dup sub 0 rmoveto 10 hlineto 10 vlineto endchar",
        // A square that is fine, then a line to infinity, and one to NaN.
        "0 0 rmoveto 50 hlineto 50 vlineto " HUGE_X " hlineto endchar",
        "0 0 rmoveto 50 hlineto 50 vlineto " HUGE_X " dup sub hlineto endchar",
        // Zero times infinity.
        "0 0 rmoveto 50 hlineto 50 vlineto 0 " HUGE_X " mul vlineto endchar",
        // A curve with an infinite control point.
        "0 0 rmoveto 0 " HUGE_X " 60 0 0 -90 rrcurveto endchar",
        // Finite, and as large as a double gets: squared six times.
        "32767 dup mul dup mul dup mul dup mul dup mul dup mul 0 rmoveto 10 hlineto endchar",
    };
#undef HUGE_X
    MiniCff cff;
    cff.charstrings.push_back(cs_square(0, 0, 10));
    for (const char* text : glyphs)
        cff.charstrings.push_back(cs_asm(text));
    const Bytes file = cff_font(cff).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    for (uint32_t g = 1; g < font.glyph_count(); ++g) {
        CHECK_CONTEXT("%s", glyphs[g - 1]);
        const GlyphBounds box = font.bounds(g);
        CHECK(std::isfinite(box.x_min) && std::isfinite(box.x_max));
        CHECK(std::isfinite(box.y_min) && std::isfinite(box.y_max));
        CHECK(box.x_min <= box.x_max && box.y_min <= box.y_max);
    }
    // And through layout, at a size.
    FontLibrary library;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "huge", &why));
    const FontMetrics* m = library.metrics(Face::PROSE, 16.0, &why);
    REQUIRE(m != nullptr);
    for (uint32_t cp = 'A'; cp < 'A' + font.glyph_count() - 1; ++cp) {
        CHECK_CONTEXT("U+%04X", cp);
        const GlyphBBox box = m->bbox(cp);
        CHECK(std::isfinite(box.x_min) && std::isfinite(box.x_max));
        CHECK(std::isfinite(box.y_min) && std::isfinite(box.y_max));
    }
    // A glyph whose coordinates are not numbers is not one for a rasteriser:
    // the strict pass bounds how far an outline reaches, and neither of these
    // is within any bound. Infinite first, then NaN alone.
    why.clear();
    CHECK(!font.strict_check(&why));
    for (const size_t only : {size_t(1), size_t(3)}) {
        CHECK_CONTEXT("%s", glyphs[only]);
        MiniCff one;
        one.charstrings = {cs_square(0, 0, 10), cs_asm(glyphs[only])};
        const Bytes single = cff_font(one).build();
        OpenTypeFont nan;
        REQUIRE(nan.load(single.data(), single.size(), &why));
        why.clear();
        CHECK(!nan.strict_check(&why));
    }
}

TEST(type2_dup_stops_at_the_stack_limit_and_not_past_it)
{
    // 1, then dup until the stack is full. The reader's stack is 96 deep
    // (twice the specification's 48): 95 dups fill it and the 96th fails.
    const auto dups = [](int count) {
        Bytes cs = cs_asm("0 0 rmoveto 50 hlineto 50 vlineto 1");
        for (int i = 0; i < count; ++i)
            cs_esc(cs, 27);
        // Everything but two operands goes to an rlineto, which wants pairs.
        cs_op(cs, 5);
        cs_op(cs, 14);
        return cs;
    };
    MiniCff cff;
    cff.charstrings = {cs_square(0, 0, 10), dups(47), dups(95), dups(96), dups(400)};
    const Bytes file = cff_font(cff).build();
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    // 48 ones: 24 lines of (1, 1) from (50, 50).
    check_box(font, 1, GlyphBounds{0, 0, 74, 74});
    // 96 ones: 48 lines.
    check_box(font, 2, GlyphBounds{0, 0, 98, 98});
    check_box(font, 3, GlyphBounds{});
    check_box(font, 4, GlyphBounds{});
}

TEST(type2_flex_operators_draw_two_curves)
{
    check_charstrings({
        // rrcurveto, for the curve arithmetic itself: from (0, 0) up to
        // control points at y = 90 and back down to (60, 0). y(t) = 270 t (1 - t),
        // greatest at t = 1/2, where it is 67.5.
        {"0 0 rmoveto 0 90 60 0 0 -90 rrcurveto endchar", {0, 0, 60, 67.5}},
        // flex: the same curve, then a straight run of 30; the thirteenth
        // operand is the flex depth and draws nothing.
        {"0 0 rmoveto 0 90 60 0 0 -90 10 0 10 0 10 0 50 flex endchar", {0, 0, 90, 67.5}},
        // flex with every coordinate monotone on each curve, so the box is
        // the end points': (10,20) -> (40,50) -> (70,20).
        {"10 20 rmoveto 10 0 10 30 10 0 10 0 10 -30 10 0 50 flex endchar", {10, 20, 70, 50}},
        // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6; up by dy2 and back down by it.
        {"10 20 rmoveto 10 10 30 10 10 10 10 hflex endchar", {10, 20, 70, 50}},
        {"10 20 rmoveto 10 10 -30 10 10 10 10 hflex endchar", {10, -10, 70, 20}},
        // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6; the last point returns
        // to the starting y: 20 + 10 + 20 = 50 at the join, then 40, then 20.
        {"10 20 rmoveto 10 10 10 20 10 10 10 -10 10 hflex1 endchar", {10, 20, 70, 50}},
        // flex1, wider than tall (dx 50, dy 20): the last operand is an x
        // offset and y returns to where it started.
        {"10 20 rmoveto 10 10 10 20 10 0 10 0 10 -10 15 flex1 endchar", {10, 20, 75, 50}},
        // flex1, taller than wide (dx 15, dy 70): the last operand is a y
        // offset and x returns to where it started.
        {"10 20 rmoveto 30 10 0 20 0 10 -10 10 -5 20 15 flex1 endchar", {10, 20, 40, 105}},
    });

    // Too few operands for each.
    const Bytes file = charstring_font({
        {"0 0 rmoveto 50 hlineto 1 2 3 4 5 6 7 8 9 10 11 flex endchar", {}},
        {"0 0 rmoveto 50 hlineto 1 2 3 4 5 6 hflex endchar", {}},
        {"0 0 rmoveto 50 hlineto 1 2 3 4 5 6 7 8 hflex1 endchar", {}},
        {"0 0 rmoveto 50 hlineto 1 2 3 4 5 6 7 8 9 10 flex1 endchar", {}},
    });
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    for (uint32_t g = 1; g <= 4; ++g)
        check_box(font, g, GlyphBounds{});
}

TEST(type2_callgsubr_calls_a_global_subroutine_and_callsubr_a_local_one)
{
    // Subroutine 0 of one; the bias for fewer than 1,240 is 107.
    const Bytes file = charstring_font(
        {
            {"5 6 rmoveto -107 callgsubr endchar", {5, 6, 205, 206}},
            {"5 6 rmoveto -107 callsubr endchar", {5, 6, 75, 76}},
            {"5 6 rmoveto -106 callsubr endchar", {5, 6, 35, 36}},
            // An index that is not there.
            {"5 6 rmoveto -105 callsubr endchar", {}},
            {"5 6 rmoveto -106 callgsubr endchar", {}},
            {"5 6 rmoveto -108 callgsubr endchar", {}},
            // The index computed: 107 - 214 = -107.
            {"5 6 rmoveto 107 214 sub callgsubr endchar", {5, 6, 205, 206}},
        },
        {subr_square(70), subr_square(30)}, {subr_square(200)});
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    check_box(font, 1, GlyphBounds{5, 6, 205, 206});
    check_box(font, 2, GlyphBounds{5, 6, 75, 76});
    check_box(font, 3, GlyphBounds{5, 6, 35, 36});
    check_box(font, 4, GlyphBounds{});
    check_box(font, 5, GlyphBounds{});
    check_box(font, 6, GlyphBounds{});
    check_box(font, 7, GlyphBounds{5, 6, 205, 206});
}

// --- 8. The 'kern' table ---------------------------------------------------------------------

namespace
{

// Six squares, 'A'..'E' glyphs 1..5, with a kern table and perhaps a GPOS.
Bytes kerned(const Bytes& kern, const Bytes& gpos = {})
{
    Sfnt sfnt = squares(6).sfnt();
    if (!kern.empty())
        sfnt.table(tag("kern")) = kern;
    if (!gpos.empty())
        sfnt.table(tag("GPOS")) = gpos;
    return sfnt.build();
}

void check_legacy(const OpenTypeFont& font, const std::vector<KernPair>& want)
{
    const std::vector<uint32_t>& keys = font.legacy_kern_keys();
    const std::vector<int32_t>& values = font.legacy_kern_values();
    REQUIRE_EQ(keys.size(), want.size());
    REQUIRE_EQ(values.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK_CONTEXT("pair %zu", i);
        CHECK_EQ(keys[i], (want[i].left << 16) | want[i].right);
        CHECK_EQ(values[i], want[i].value);
        CHECK_EQ(font.pair_kern(want[i].left, want[i].right), want[i].value);
        CHECK_EQ(font.pair_kern_accumulated(want[i].left, want[i].right), want[i].value);
    }
}

} // namespace

TEST(a_version_0_kern_table_kerns_a_font_whose_gpos_does_not)
{
    const Bytes file = kerned(kern_table({kern_subtable({{1, 2, -50}, {2, 1, 30}, {4, 5, 7}})}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK(font.legacy_kern_used());
    CHECK_EQ(font.kern_subtable_count(), size_t(0));
    check_legacy(font, {{1, 2, -50}, {2, 1, 30}, {4, 5, 7}});
    CHECK_EQ(font.pair_kern(1, 3), 0);
    CHECK_EQ(font.pair_kern(2, 2), 0);
    CHECK_EQ(font.pair_kern(0x10001, 2), 0);
    CHECK_EQ(font.pair_kern(1, 0x10002), 0);
    CHECK(font.warnings().empty());
    CHECK(font.strict_check(&why));

    // Through layout: 1000 units to the em, set at 1000.
    FontLibrary library;
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "kern", &why));
    const FontMetrics* m = library.metrics(Face::PROSE, 1000.0, &why);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->kern('A', 'B'), -50.0);
    CHECK_EQ(m->kern('B', 'A'), 30.0);
    CHECK_EQ(m->kern('A', 'C'), 0.0);
}

TEST(the_kern_table_is_not_read_when_the_option_is_off)
{
    const Bytes file = kerned(kern_table({kern_subtable({{1, 2, -50}})}));
    FontLoadOptions options;
    options.legacy_kern = false;
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why, options));
    CHECK(!font.legacy_kern_used());
    CHECK(font.legacy_kern_keys().empty());
    CHECK(font.legacy_kern_values().empty());
    CHECK_EQ(font.pair_kern(1, 2), 0);
    CHECK_EQ(font.pair_kern_accumulated(1, 2), 0);

    FontLibrary library;
    CHECK(library.legacy_kern());
    library.set_legacy_kern(false);
    CHECK(!library.legacy_kern());
    REQUIRE(library.register_font(Face::PROSE, file.data(), file.size(), "kern", &why));
    CHECK(library.face_data(Face::PROSE)->font().legacy_kern_keys().empty());
    const FontMetrics* m = library.metrics(Face::PROSE, 1000.0, &why);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->kern('A', 'B'), 0.0);
    // It applies to fonts registered after the call, and back on it is read.
    library.set_legacy_kern(true);
    REQUIRE(library.register_font(Face::CODE, file.data(), file.size(), "kern", &why));
    m = library.metrics(Face::CODE, 1000.0, &why);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->kern('A', 'B'), -50.0);
}

TEST(gpos_pair_kerning_is_preferred_to_the_kern_table)
{
    const Bytes kern = kern_table({kern_subtable({{1, 2, -50}, {2, 1, 30}})});
    const Bytes file = kerned(kern, gpos_assemble({0}, {pairpos1(1, 2, -70)}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.kern_subtable_count(), size_t(1));
    CHECK(!font.legacy_kern_used());
    CHECK_EQ(font.pair_kern(1, 2), -70);
    // A pair only the kern table has is not kerned: the two are not mixed.
    CHECK_EQ(font.pair_kern(2, 1), 0);
    CHECK_EQ(font.pair_kern_accumulated(2, 1), 0);
    // The table was read all the same, and says what it holds.
    REQUIRE_EQ(font.legacy_kern_keys().size(), size_t(2));
    CHECK_EQ(font.legacy_kern_values()[0], -50);

    // A GPOS whose 'kern' feature leads to no pair subtable does not count.
    const Bytes hollow = kerned(kern, gpos_assemble({5}, {pairpos1(1, 2, -70)}));
    REQUIRE(font.load(hollow.data(), hollow.size(), &why));
    CHECK_EQ(font.kern_subtable_count(), size_t(0));
    CHECK(font.legacy_kern_used());
    CHECK_EQ(font.pair_kern(1, 2), -50);
    CHECK_EQ(font.pair_kern(2, 1), 30);
}

TEST(kern_subtables_add_up_and_one_with_the_override_bit_replaces)
{
    OpenTypeFont font;
    std::string why;
    {
        const Bytes file = kerned(
            kern_table({kern_subtable({{1, 2, -50}}), kern_subtable({{1, 2, -20}, {1, 3, 5}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -70}, {1, 3, 5}});
    }
    {
        // Coverage 0x09: horizontal, override.
        const Bytes file = kerned(kern_table(
            {kern_subtable({{1, 2, -50}, {1, 3, 5}}), kern_subtable({{1, 2, -20}}, 0x09)}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -20}, {1, 3, 5}});
    }
    {
        // And what comes after an override adds to it.
        const Bytes file =
            kerned(kern_table({kern_subtable({{1, 2, -50}}), kern_subtable({{1, 2, -20}}, 0x09),
                               kern_subtable({{1, 2, 7}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -13}});
    }
    {
        // An override first replaces nothing.
        const Bytes file =
            kerned(kern_table({kern_subtable({{1, 2, -20}}, 0x09), kern_subtable({{1, 2, 7}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -13}});
    }
}

TEST(a_pair_listed_twice_in_one_kern_subtable_keeps_its_last_value)
{
    OpenTypeFont font;
    std::string why;
    {
        const Bytes file =
            kerned(kern_table({kern_subtable({{1, 2, -50}, {3, 4, 9}, {1, 2, -10}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -10}, {3, 4, 9}});
    }
    {
        // After another subtable: 5 + (-10), not 5 + (-50) + (-10).
        const Bytes file = kerned(
            kern_table({kern_subtable({{1, 2, 5}}), kern_subtable({{1, 2, -50}, {1, 2, -10}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -5}});
    }
    {
        // And before one: (-10) + 5.
        const Bytes file = kerned(
            kern_table({kern_subtable({{1, 2, -50}, {1, 2, -10}}), kern_subtable({{1, 2, 5}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -5}});
    }
}

TEST(kern_subtables_that_are_not_kerning_along_the_line_are_not_read)
{
    struct Skipped {
        const char* what;
        uint32_t coverage, format;
    };
    const Skipped skipped[] = {
        {"vertical", 0x00, 0},          {"minimum values", 0x03, 0}, {"cross-stream", 0x05, 0},
        {"vertical override", 0x08, 0}, {"format 2", 0x01, 2},       {"format 1", 0x01, 1},
    };
    for (const Skipped& s : skipped) {
        CHECK_CONTEXT("%s", s.what);
        OpenTypeFont font;
        std::string why;
        const Bytes alone =
            kerned(kern_table({kern_subtable({{1, 2, -50}}, s.coverage, s.format)}));
        REQUIRE(font.load(alone.data(), alone.size(), &why));
        CHECK(font.legacy_kern_keys().empty());
        CHECK(!font.legacy_kern_used());
        CHECK_EQ(font.pair_kern(1, 2), 0);
        // It is stepped over by its length, and the subtables around it are read.
        const Bytes between = kerned(kern_table({kern_subtable({{1, 2, 4}}),
                                                 kern_subtable({{1, 2, -50}}, s.coverage, s.format),
                                                 kern_subtable({{1, 2, 3}, {2, 3, 8}})}));
        REQUIRE(font.load(between.data(), between.size(), &why));
        check_legacy(font, {{1, 2, 7}, {2, 3, 8}});
        CHECK(font.warnings().empty());
    }
}

TEST(a_kern_subtable_is_as_long_as_its_pair_count_says_whatever_its_length_field_says)
{
    OpenTypeFont font;
    std::string why;
    for (const int length : {0, 6, 14, 15}) {
        CHECK_CONTEXT("length field %d", length);
        const Bytes file =
            kerned(kern_table({kern_subtable({{1, 2, -50}, {1, 3, 6}}, 0x01, 0, length),
                               kern_subtable({{1, 2, 9}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -41}, {1, 3, 6}});
        CHECK(font.warnings().empty());
    }
    {
        // 11,000 pairs are 66,014 bytes, which the 16-bit field holds as 478.
        std::vector<KernPair> pairs;
        for (uint32_t k = 0; k < 11000; ++k)
            pairs.push_back({1 + k / 100, k % 100, static_cast<int>(k) - 5000});
        const Bytes big = kern_subtable(pairs);
        REQUIRE_EQ(big.size(), size_t(66014));
        REQUIRE_EQ(rd16(big, 2), 478u);
        const Bytes file = kerned(kern_table({big, kern_subtable({{1, 2, 1000}, {900, 901, 3}})}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        REQUIRE_EQ(font.legacy_kern_keys().size(), size_t(11001));
        CHECK_EQ(font.pair_kern(1, 0), -5000);
        CHECK_EQ(font.pair_kern(1, 2), -4998 + 1000);
        CHECK_EQ(font.pair_kern(110, 99), 5999);
        CHECK_EQ(font.pair_kern(900, 901), 3);
        CHECK(font.warnings().empty());
    }
}

TEST(the_apple_form_of_the_kern_table_is_ignored_with_a_warning)
{
    // Version 1.0 as a 32-bit fixed, a 32-bit table count, then a subtable
    // with a 32-bit length.
    Bytes kern;
    put32(kern, 0x00010000u);
    put32(kern, 1);
    put32(kern, 8 + 8 + 6);
    put16(kern, 0);
    put16(kern, 0);
    put16(kern, 1);
    put16(kern, 0);
    put16(kern, 0);
    put16(kern, 0);
    put16(kern, 1);
    put16(kern, 2);
    put16(kern, static_cast<uint32_t>(-50));
    const Bytes file = kerned(kern);
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK(font.legacy_kern_keys().empty());
    CHECK(!font.legacy_kern_used());
    CHECK_EQ(font.pair_kern(1, 2), 0);
    CHECK(warned(font.warnings(), "kern table is not version 0"));
}

TEST(a_truncated_kern_table_keeps_its_whole_subtables_and_says_so)
{
    const Bytes whole = kern_table(
        {kern_subtable({{1, 2, -50}}), kern_subtable({{1, 2, -20}, {1, 3, 5}, {2, 3, 6}})});
    OpenTypeFont font;
    std::string why;
    // Cut inside the second subtable's pairs, inside its header, and inside
    // the six bytes every subtable begins with.
    for (const size_t cut : {size_t(1), size_t(8), size_t(18), size_t(22)}) {
        CHECK_CONTEXT("%zu bytes cut", cut);
        Bytes kern = whole;
        kern.resize(kern.size() - cut);
        const Bytes file = kerned(kern);
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -50}});
        CHECK(warned(font.warnings(), "kern table is truncated"));
    }
    {
        // More subtables stated than there are.
        Bytes kern = whole;
        wr16(kern, 2, 9);
        const Bytes file = kerned(kern);
        REQUIRE(font.load(file.data(), file.size(), &why));
        check_legacy(font, {{1, 2, -70}, {1, 3, 5}, {2, 3, 6}});
        CHECK(warned(font.warnings(), "kern table is truncated"));
    }
    {
        // Nothing but the header, and less than the header.
        const Bytes file = kerned(kern_table({}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK(font.legacy_kern_keys().empty());
        CHECK(font.warnings().empty());
        const Bytes stub = kerned(Bytes{0, 0, 0});
        REQUIRE(font.load(stub.data(), stub.size(), &why));
        CHECK(font.legacy_kern_keys().empty());
    }
}

// --- 9. GPOS null offsets and classes --------------------------------------------------------

namespace
{

Bytes gpos_font(const Bytes& gpos)
{
    return with(squares(8), tag("GPOS"), gpos);
}

} // namespace

TEST(a_pairpos_class_subtable_without_a_second_classdef_puts_every_second_glyph_in_class_0)
{
    // Glyph 1 is first-class 1. Rows are first classes, columns second classes.
    const Bytes sub = pairpos2({1, 2}, classdef2({{1, 1, 1}}), Bytes(), {{11, 22}, {33, 44}});
    REQUIRE_EQ(rd16(sub, 10), 0u); // the ClassDef2 offset
    const Bytes file = gpos_font(gpos_assemble({0}, {sub}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    REQUIRE_EQ(font.kern_subtable_count(), size_t(1));
    CHECK_EQ(font.kern_subtable_format(0), 2);
    for (const uint32_t second : {0u, 1u, 2u, 5u, 7u, 600u}) {
        CHECK_CONTEXT("second glyph %u", second);
        CHECK_EQ(font.pair_kern(1, second), 33);
        CHECK_EQ(font.pair_kern(2, second), 11); // first-class 0
    }
    CHECK_EQ(font.pair_kern(3, 1), 0); // not covered
    CHECK(font.warnings().empty());

    // The same without the first: every covered glyph is first-class 0.
    const Bytes no_first = pairpos2({1, 2}, Bytes(), classdef2({{4, 5, 1}}), {{11, 22}, {33, 44}});
    REQUIRE_EQ(rd16(no_first, 8), 0u);
    const Bytes second_file = gpos_font(gpos_assemble({0}, {no_first}));
    REQUIRE(font.load(second_file.data(), second_file.size(), &why));
    CHECK_EQ(font.pair_kern(1, 4), 22);
    CHECK_EQ(font.pair_kern(2, 5), 22);
    CHECK_EQ(font.pair_kern(1, 3), 11);
}

TEST(a_pairpos_subtable_without_coverage_is_ignored)
{
    for (const int format : {1, 2}) {
        CHECK_CONTEXT("format %d", format);
        Bytes sub = format == 1 ? pairpos1(1, 2, -70)
                                : pairpos2({1}, classdef2({{1, 1, 1}}), classdef2({{2, 2, 1}}),
                                           {{11, 22}, {33, 44}});
        wr16(sub, 2, 0);
        // A second subtable, whole, is still read.
        const Bytes file = gpos_font(gpos_assemble({0, 1}, {sub, pairpos1(3, 4, -9)}));
        OpenTypeFont font;
        std::string why;
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.kern_subtable_count(), size_t(1));
        CHECK_EQ(font.pair_kern(1, 2), 0);
        CHECK_EQ(font.pair_kern(3, 4), -9);
        CHECK(warned(font.warnings(), "pair subtable is malformed"));
    }
}

TEST(a_null_pair_set_offset_leaves_that_first_glyph_without_pairs)
{
    const std::vector<PairSet> sets = {
        {1, {{4, -10}, {5, -11}}, false}, {2, {{4, -20}}, true}, {3, {{4, -30}}, false}};
    const Bytes sub = pairpos1_sets(sets);
    REQUIRE_EQ(rd16(sub, 12), 0u); // the second set's offset
    REQUIRE(rd16(sub, 10) != 0 && rd16(sub, 14) != 0);
    const Bytes file = gpos_font(gpos_assemble({0}, {sub}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    REQUIRE_EQ(font.kern_subtable_count(), size_t(1));
    CHECK_EQ(font.kern_subtable_format(0), 1);
    CHECK_EQ(font.pair_kern(1, 4), -10);
    CHECK_EQ(font.pair_kern(1, 5), -11);
    CHECK_EQ(font.pair_kern(2, 4), 0);
    CHECK_EQ(font.pair_kern(3, 4), -30);
    CHECK(font.warnings().empty());
}

TEST(a_classdef_range_of_class_0_does_not_take_back_an_earlier_class)
{
    // Glyphs 1..3 are class 3; then a range says glyph 2 is class 0.
    const Bytes classes = classdef2({{1, 3, 3}, {2, 2, 0}});
    const Bytes sub = pairpos2({1, 2, 3}, classes, Bytes(), {{100}, {101}, {102}, {103}});
    const Bytes file = gpos_font(gpos_assemble({0}, {sub}));
    OpenTypeFont font;
    std::string why;
    REQUIRE(font.load(file.data(), file.size(), &why));
    CHECK_EQ(font.pair_kern(1, 5), 103);
    CHECK_EQ(font.pair_kern(2, 5), 103);
    CHECK_EQ(font.pair_kern(3, 5), 103);

    // A later range of another class does replace.
    const Bytes replaced = pairpos2({1, 2, 3}, classdef2({{1, 3, 3}, {2, 2, 1}}), Bytes(),
                                    {{100}, {101}, {102}, {103}});
    const Bytes second = gpos_font(gpos_assemble({0}, {replaced}));
    REQUIRE(font.load(second.data(), second.size(), &why));
    CHECK_EQ(font.pair_kern(1, 5), 103);
    CHECK_EQ(font.pair_kern(2, 5), 101);
}

TEST(pair_kern_takes_the_first_subtable_and_pair_kern_accumulated_adds_the_lookups)
{
    OpenTypeFont font;
    std::string why;
    {
        // Two lookups, each kerning (1, 2).
        const Bytes file = gpos_font(gpos_lookups(
            {0, 1}, {{pairpos1(1, 2, -70)}, {pairpos1(1, 2, -20), pairpos1(3, 4, 5)}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        REQUIRE_EQ(font.kern_subtable_count(), size_t(3));
        CHECK_EQ(font.pair_kern(1, 2), -70);
        CHECK_EQ(font.pair_kern_accumulated(1, 2), -90);
        CHECK_EQ(font.pair_kern(3, 4), 5);
        CHECK_EQ(font.pair_kern_accumulated(3, 4), 5);
        CHECK_EQ(font.pair_kern_accumulated(2, 1), 0);
    }
    {
        // Two subtables in one lookup: a shaper stops at the first that applies.
        const Bytes file =
            gpos_font(gpos_lookups({0}, {{pairpos1(1, 2, -70), pairpos1(1, 2, -20)}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        REQUIRE_EQ(font.kern_subtable_count(), size_t(2));
        CHECK_EQ(font.pair_kern(1, 2), -70);
        CHECK_EQ(font.pair_kern_accumulated(1, 2), -70);
    }
    {
        // Three lookups, the middle one with two subtables.
        const Bytes file =
            gpos_font(gpos_lookups({0, 1, 2}, {{pairpos1(1, 2, 1)},
                                               {pairpos1(1, 2, 10), pairpos1(1, 2, 100)},
                                               {pairpos1(1, 2, 1000)}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.pair_kern(1, 2), 1);
        CHECK_EQ(font.pair_kern_accumulated(1, 2), 1011);
    }
    {
        // A class subtable answers for every pair it covers, zero included,
        // so within its lookup nothing after it is reached.
        const Bytes classes = pairpos2({1}, Bytes(), Bytes(), {{0}});
        const Bytes file =
            gpos_font(gpos_lookups({0, 1}, {{classes, pairpos1(1, 2, -20)}, {pairpos1(1, 2, -3)}}));
        REQUIRE(font.load(file.data(), file.size(), &why));
        CHECK_EQ(font.pair_kern(1, 2), 0);
        CHECK_EQ(font.pair_kern_accumulated(1, 2), -3);
    }
}
