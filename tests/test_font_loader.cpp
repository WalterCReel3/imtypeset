// src/font_loader: a face added to an ImGui atlas through it rasterises every
// codepoint layout can name, the minted ones included, and everything the font
// already encoded comes out exactly as ImGui's own loading of the file gives
// it. Each test builds and destroys its own headless context, so a leak in the
// atlas path shows under the sanitiser build.

#include "golden.h"

#include "font_loader.h"
#include "opentype.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

std::string font_path(const std::string& name)
{
    return golden::data_path("fonts/" + name);
}

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

    ImFontAtlas* atlas() const { return ImGui::GetIO().Fonts; }
    bool freetype() const
    {
        const char* name = atlas()->FontLoaderName;
        return name != nullptr && std::strncmp(name, "FreeType", 8) == 0;
    }
};

// A frame, because an ImFontBaked is only good inside one.
struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

// The codepoints a face's metrics were sampled at by the oracle, plus every
// one it minted: enough of the cmap to compare two loadings of a font.
std::vector<uint32_t> sample_codepoints(const FaceData& face)
{
    std::vector<uint32_t> out;
    for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        out.push_back(cp);
    for (uint32_t cp = 0xA0; cp < 0x100; ++cp)
        out.push_back(cp);
    size_t seen = 0;
    for (const CmapRange& range : face.font().cmap())
        for (uint32_t cp = range.first; cp <= range.last; ++cp)
            if (seen++ % 3 == 0)
                out.push_back(cp);
    return out;
}

bool same_glyph(const ImFontGlyph* a, const ImFontGlyph* b)
{
    return a->Visible == b->Visible && a->AdvanceX == b->AdvanceX && a->X0 == b->X0 &&
           a->Y0 == b->Y0 && a->X1 == b->X1 && a->Y1 == b->Y1;
}

} // namespace

TEST(the_font_handed_to_imgui_is_the_file_with_a_cmap_layout_agrees_with)
{
    FontLibrary library;
    register_defaults(library);
    REQUIRE(library.register_font(Face::CODE_BOLD, font_path("DroidSans.ttf").c_str()));
    for (int i = 0; i < FACE_COUNT; ++i) {
        const Face role = static_cast<Face>(i);
        const FaceData* face = library.face_data(role);
        REQUIRE(face != nullptr);
        CHECK_CONTEXT("%s", face->name().c_str());
        for (const int wchar_bits : {32, 16}) {
            CHECK_CONTEXT("%d-bit ImWchar", wchar_bits);
            std::vector<uint8_t> bytes;
            std::string why;
            REQUIRE(build_atlas_font(*face, wchar_bits, bytes, &why));
            CHECK_EQ(bytes.size() % 4, size_t(0));

            // Read back with our own reader: a font, with nothing lost.
            OpenTypeFont rebuilt;
            REQUIRE(rebuilt.load(bytes.data(), bytes.size(), &why));
            CHECK(rebuilt.warnings().empty());
            const OpenTypeFont& original = face->font();
            CHECK_EQ(rebuilt.glyph_count(), original.glyph_count());
            CHECK_EQ(rebuilt.units_per_em(), original.units_per_em());
            CHECK_EQ(rebuilt.hhea_ascender(), original.hhea_ascender());
            CHECK_EQ(rebuilt.os2_x_height(), original.os2_x_height());
            CHECK_EQ(rebuilt.has_math(), original.has_math());
            CHECK_EQ(rebuilt.kern_subtable_count(), original.kern_subtable_count());
            CHECK_EQ(rebuilt.cmap_platform(), 3);
            CHECK_EQ(rebuilt.cmap_encoding(), 10);
            CHECK_EQ(rebuilt.cmap_format(), 12);
            for (uint32_t g = 0; g < original.glyph_count(); g += 7) {
                CHECK_EQ(rebuilt.advance(g), original.advance(g));
                CHECK(rebuilt.bounds(g) == original.bounds(g));
                CHECK_EQ(rebuilt.pair_kern(g, (g * 5 + 1) % original.glyph_count()),
                         original.pair_kern(g, (g * 5 + 1) % original.glyph_count()));
            }

            // Every codepoint the file mapped still maps to the same glyph.
            size_t mapped = 0;
            for (const CmapRange& range : original.cmap()) {
                for (uint32_t cp = range.first; cp <= range.last; ++cp) {
                    uint32_t glyph = 0xFFFFFFFFu;
                    if (!rebuilt.lookup(cp, &glyph) || glyph != range.glyph_of(cp))
                        FAIL("U+%04X lost or moved", cp);
                    ++mapped;
                }
            }
            // The minted ones are now in the cmap itself.
            for (const SyntheticCodepoint& minted : face->synthetic_codepoints()) {
                uint32_t glyph = 0;
                CHECK(rebuilt.lookup(minted.codepoint, &glyph));
                CHECK_EQ(glyph, minted.glyph);
                ++mapped;
            }
            // And for a 16-bit atlas, so is every stand-in.
            size_t stand_ins = 0;
            for (const auto& stand_in : face->atlas_stand_ins()) {
                uint32_t glyph = 0;
                CHECK_EQ(rebuilt.lookup(stand_in.first, &glyph), wchar_bits < 32);
                if (wchar_bits < 32) {
                    CHECK_EQ(glyph, face->glyph(stand_in.second));
                    ++stand_ins;
                }
            }
            // Nothing else was added.
            double total = 0;
            for (const CmapRange& range : rebuilt.cmap())
                total += static_cast<double>(range.last - range.first) + 1.0;
            CHECK_EQ(total, static_cast<double>(mapped + stand_ins));
        }
    }
}

TEST(what_the_font_already_encoded_bakes_exactly_as_imguis_own_loading_of_the_file)
{
    FontLibrary library;
    register_defaults(library);
    Context context;
    // The same configuration for both, so the only difference is the route.
    ImFontConfig config;
    config.OversampleH = 1;
    struct Pair {
        const FaceData* face;
        ImFont* ours;
        ImFont* theirs;
    };
    std::vector<Pair> pairs;
    for (const Face role : {Face::PROSE, Face::PROSE_BOLD_ITALIC, Face::CODE, Face::MATH}) {
        const FaceData* face = library.face_data(role);
        REQUIRE(face != nullptr);
        std::string why;
        ImFont* ours = add_face_to_atlas(context.atlas(), *face, 16.0f, &config, &why);
        ImFont* theirs =
            context.atlas()->AddFontFromFileTTF(font_path(face->name()).c_str(), 16.0f, &config);
        REQUIRE(ours != nullptr);
        REQUIRE(theirs != nullptr);
        pairs.push_back({face, ours, theirs});
    }
    Frame frame;
    int compared = 0;
    for (const Pair& pair : pairs) {
        CHECK_CONTEXT("%s", pair.face->name().c_str());
        const std::vector<uint32_t> codepoints = sample_codepoints(*pair.face);
        for (const float size : {13.0f, 16.0f, 19.0f, 29.0f}) {
            CHECK_CONTEXT("%g px", static_cast<double>(size));
            ImFontBaked* ours = pair.ours->GetFontBaked(size);
            ImFontBaked* theirs = pair.theirs->GetFontBaked(size);
            CHECK_EQ(ours->Ascent, theirs->Ascent);
            CHECK_EQ(ours->Descent, theirs->Descent);
            for (const uint32_t cp : codepoints) {
                if (cp > IM_UNICODE_CODEPOINT_MAX)
                    continue; // ImGui's loading of the file cannot be asked
                CHECK_CONTEXT("U+%04X", cp);
                const ImWchar c = static_cast<ImWchar>(cp);
                const bool in_ours = pair.ours->IsGlyphInFont(c);
                CHECK_EQ(in_ours, pair.theirs->IsGlyphInFont(c));
                if (in_ours)
                    CHECK(same_glyph(ours->FindGlyph(c), theirs->FindGlyph(c)));
                ++compared;
            }
        }
    }
    CHECK(compared > 4000);
}

TEST(a_minted_codepoint_is_a_visible_glyph_that_agrees_with_the_metrics)
{
    FontLibrary library;
    register_defaults(library);
    const FaceData* face = library.face_data(Face::MATH);
    REQUIRE(face != nullptr);
    Context context;
    std::string why;
    ImFont* ours = add_face_to_atlas(context.atlas(), *face, 16.0f, nullptr, &why);
    ImFont* file_as_shipped =
        context.atlas()->AddFontFromFileTTF(font_path(face->name()).c_str(), 16.0f);
    REQUIRE(ours != nullptr);
    REQUIRE(file_as_shipped != nullptr);
    REQUIRE_EQ(face->synthetic_codepoints().size(), size_t(227));

    Frame frame;
    // ImGui sizes a font by pixel height and layout by em; this is the em at
    // which the face is `size` px tall, as draw converts it.
    const float size = 36.0f;
    const FontMetrics* m =
        library.metrics(Face::MATH, library.em_size_for_pixel_height(Face::MATH, size));
    REQUIRE(m != nullptr);
    ImFontBaked* baked = ours->GetFontBaked(size);
    double worst_advance = 0, worst_box = 0;
    int visible = 0;
    for (const SyntheticCodepoint& minted : face->synthetic_codepoints()) {
        CHECK_CONTEXT("U+%05X glyph %u", minted.codepoint, minted.glyph);
        const uint32_t asked = atlas_codepoint(*face, minted.codepoint);
        REQUIRE(asked != 0);
        REQUIRE(asked <= IM_UNICODE_CODEPOINT_MAX);
        const ImWchar c = static_cast<ImWchar>(asked);
        CHECK(ours->IsGlyphInFont(c));
        // The file as shipped has no such codepoint: this is what the cmap is for.
        if (asked == minted.codepoint)
            CHECK(!file_as_shipped->IsGlyphInFont(c));

        const ImFontGlyph* glyph = baked->FindGlyph(c);
        const GlyphBBox box = m->bbox(minted.codepoint);
        const bool has_ink = box.width() > 0.0 && box.height() + box.depth() > 0.0;
        CHECK_EQ(glyph->Visible != 0, has_ink);
        // A rasteriser hints and rounds; it does not move a glyph by more than
        // a pixel or so from where the outline is.
        worst_advance =
            std::max(worst_advance, std::fabs(glyph->AdvanceX - m->advance(minted.codepoint)));
        if (glyph->Visible) {
            ++visible;
            const double deltas[] = {
                glyph->X0 - box.x_min,
                glyph->X1 - box.x_max,
                glyph->Y0 - (baked->Ascent - box.y_max),
                glyph->Y1 - (baked->Ascent - box.y_min),
            };
            for (const double delta : deltas)
                worst_box = std::max(worst_box, std::fabs(delta));
        }
    }
    CHECK_EQ(visible, 227);
    CHECK(worst_advance <= 1.0);
    CHECK(worst_box <= 2.0);
    std::printf("font_loader: 227 minted glyphs at %g px under %s: advance within %.3f px of "
                "the metrics, ink box within %.3f px\n",
                static_cast<double>(size), context.atlas()->FontLoaderName, worst_advance,
                worst_box);
}

TEST(a_minted_glyph_has_coverage_in_the_atlas_texture)
{
    FontLibrary library;
    register_defaults(library);
    const FaceData* face = library.face_data(Face::MATH);
    REQUIRE(face != nullptr);
    Context context;
    ImFont* font = add_face_to_atlas(context.atlas(), *face, 16.0f);
    REQUIRE(font != nullptr);
    Frame frame;
    // The display-size summation: the first vertical variant of U+2211.
    const MathFontMetrics* m = library.math_metrics(16.0);
    REQUIRE(m != nullptr);
    const GlyphVariants chain = m->vertical_variants(0x2211);
    REQUIRE(chain.size >= 2);
    const uint32_t display = chain[1].codepoint;
    CHECK(display >= SYNTHETIC_LAST && display <= SYNTHETIC_FIRST);

    ImFontBaked* baked = font->GetFontBaked(36.0f);
    const ImFontGlyph* glyph =
        baked->FindGlyph(static_cast<ImWchar>(atlas_codepoint(*face, display)));
    REQUIRE(glyph->Visible);
    // Bigger than the text-size summation, which is the point of the variant.
    const ImFontGlyph* body = baked->FindGlyph(static_cast<ImWchar>(0x2211));
    CHECK(glyph->Y1 - glyph->Y0 > body->Y1 - body->Y0);

    ImTextureData* texture = context.atlas()->TexData;
    REQUIRE(texture != nullptr);
    REQUIRE(texture->Pixels != nullptr);
    const int x0 = static_cast<int>(std::lround(glyph->U0 * static_cast<float>(texture->Width)));
    const int x1 = static_cast<int>(std::lround(glyph->U1 * static_cast<float>(texture->Width)));
    const int y0 = static_cast<int>(std::lround(glyph->V0 * static_cast<float>(texture->Height)));
    const int y1 = static_cast<int>(std::lround(glyph->V1 * static_cast<float>(texture->Height)));
    REQUIRE(x0 >= 0 && y0 >= 0 && x1 <= texture->Width && y1 <= texture->Height);
    CHECK(x1 - x0 > 8);
    CHECK(y1 - y0 > 20);
    // Alpha is the last byte of an RGBA32 texel and the only byte of an Alpha8 one.
    long covered = 0, solid = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const unsigned char* texel =
                static_cast<const unsigned char*>(texture->GetPixelsAt(x, y));
            const int alpha = texel[texture->BytesPerPixel - 1];
            covered += alpha > 0;
            solid += alpha > 200;
        }
    }
    const long area = static_cast<long>(x1 - x0) * (y1 - y0);
    // A summation sign is strokes on a mostly empty box: some of it solid ink,
    // most of it not.
    CHECK(covered > area / 20);
    CHECK(solid > 0);
    CHECK(covered < area);
}

namespace
{

struct FreeTypeVersion {
    int major = 0, minor = 0, patch = 0;

    friend bool operator==(const FreeTypeVersion& a, const FreeTypeVersion& b)
    {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
    }
};

// The FreeType this build runs, from the loader's name as imgui_freetype.cpp
// writes it: "FreeType (<compiled against>; <linked>)".
bool linked_freetype(const char* loader_name, FreeTypeVersion* out)
{
    FreeTypeVersion built;
    return loader_name != nullptr &&
           std::sscanf(loader_name, "FreeType (%d.%d.%d; %d.%d.%d)", &built.major, &built.minor,
                       &built.patch, &out->major, &out->minor, &out->patch) == 6;
}

// Releases under which this test has been run and every box of
// draw/boxes.golden came back as the reference has it. 2.13.3 is the
// reference's own.
const FreeTypeVersion FREETYPE_REPRODUCING_THE_GOLDEN[] = {{2, 12, 1}, {2, 13, 2}, {2, 13, 3}};

// The boxes FreeType 2.14.3 gives another edge than 2.13.3 does, by the
// golden's own keys: Inconsolata's quotation mark, one pixel narrower on the
// left at 16 px and one wider on the right at 37 px. Which boxes, and not
// what they become: the numbers stay the reference's.
struct KnownBox {
    const char* face;
    double size;
    uint32_t codepoint;
};
const KnownBox BOXES_FREETYPE_2_14_3_MOVES[] = {{"code", 16.0, 0x22}, {"code", 37.0, 0x22}};

} // namespace

TEST(glyph_boxes_match_the_reference_rasteriser_under_freetype)
{
    FontLibrary library;
    register_defaults(library);
    golden::File file;
    REQUIRE(file.load(golden::path("draw/boxes.golden")));
    Context context;
    ImFont* fonts[FACE_COUNT] = {};
    for (int i = 0; i < FACE_COUNT; ++i) {
        const Face role = static_cast<Face>(i);
        if (library.backing_face(role) != role)
            continue;
        // The reference loads with ImGui's default configuration.
        ImFontConfig config;
        config.OversampleH = 0;
        std::vector<uint8_t> bytes;
        REQUIRE(build_atlas_font(*library.face_data(role), ATLAS_WCHAR_BITS, bytes));
        void* owned = IM_ALLOC(bytes.size());
        std::memcpy(owned, bytes.data(), bytes.size());
        fonts[i] = context.atlas()->AddFontFromMemoryTTF(owned, static_cast<int>(bytes.size()),
                                                         16.0f, &config);
        REQUIRE(fonts[i] != nullptr);
    }
    Frame frame;
    const FaceData* face = nullptr;
    ImFont* font = nullptr;
    ImFontBaked* baked = nullptr;
    int boxes = 0, differing = 0, unexpected = 0, advances = 0, presence = 0, metrics = 0,
        sizes = 0;
    std::string face_name;
    double size = 0.0;
    // Each box that differs, for the report: a count alone does not say
    // whether another FreeType moved an edge by a pixel or lost a glyph.
    std::string report;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("face")) {
            Face role = Face::PROSE;
            face_name = c.str();
            REQUIRE(face_from_value(face_name, &role));
            face = library.face_data(role);
            font = fonts[static_cast<int>(role)];
            REQUIRE(font != nullptr);
        } else if (r.is("baked")) {
            REQUIRE(font != nullptr);
            // The size ImGui rounds a request to does not depend on the rasteriser.
            const double requested = c.f64();
            CHECK_EQ(c.f64(),
                     static_cast<double>(font->GetFontBaked(static_cast<float>(requested))->Size));
        } else if (r.is("size")) {
            REQUIRE(font != nullptr);
            size = c.f64();
            baked = font->GetFontBaked(static_cast<float>(size));
            ++sizes;
            if (c.f64() != static_cast<double>(baked->Ascent) ||
                c.f64() != static_cast<double>(baked->Descent))
                ++metrics;
        } else if (r.is("box")) {
            REQUIRE(baked != nullptr);
            const uint32_t asked = atlas_codepoint(*face, c.u32());
            if (asked == 0 || asked > IM_UNICODE_CODEPOINT_MAX)
                continue;
            const ImWchar ch = static_cast<ImWchar>(asked);
            ++boxes;
            const bool in_font = c.boolean();
            // Which codepoints a font has is the cmap's business, not the
            // rasteriser's, so this much is checked under either.
            if (font->IsGlyphInFont(ch) != in_font) {
                ++presence;
                continue;
            }
            if (!in_font)
                continue;
            const ImFontGlyph* glyph = baked->FindGlyph(ch);
            const bool visible = c.boolean();
            c.boolean();
            const double x0 = c.f64(), y0 = c.f64(), x1 = c.f64(), y1 = c.f64();
            if (c.f64() != static_cast<double>(glyph->AdvanceX))
                ++advances;
            if ((glyph->Visible != 0) != visible || glyph->X0 != x0 || glyph->Y0 != y0 ||
                glyph->X1 != x1 || glyph->Y1 != y1) {
                bool expected = false;
                for (const KnownBox& known : BOXES_FREETYPE_2_14_3_MOVES)
                    expected = expected || (face_name == known.face && size == known.size &&
                                            asked == known.codepoint);
                unexpected += expected ? 0 : 1;
                if (++differing <= 40) {
                    char line[320];
                    std::snprintf(line, sizeof line,
                                  "    %s at %g px, U+%04X (golden line %d): the reference's box "
                                  "is (%g, %g)-(%g, %g)%s, this build's (%g, %g)-(%g, %g)%s\n",
                                  face_name.c_str(), size, static_cast<unsigned>(asked), r.line, x0,
                                  y0, x1, y1, visible ? "" : " not visible",
                                  static_cast<double>(glyph->X0), static_cast<double>(glyph->Y0),
                                  static_cast<double>(glyph->X1), static_cast<double>(glyph->Y1),
                                  glyph->Visible != 0 ? "" : " not visible");
                    report += line;
                }
            }
        }
    }
    // Every box of the golden that this build's ImWchar can ask for: all of
    // them at 32 bits; at 16, five fewer, the ones atlas_codepoint answers 0
    // for because the build has no codepoint to ask by.
    CHECK_EQ(boxes, ATLAS_WCHAR_BITS < 32 ? 7505 : 7510);
    CHECK_EQ(presence, 0);
    // The reference's numbers were taken under FreeType 2.13.3, and a glyph's
    // box is the rasteriser's answer: which FreeType is linked decides what
    // can be asserted (decision D7).
    //  - a release measured to reproduce every box: every box, exactly;
    //  - 2.14.3: every box exactly but the ones it is measured to move, and
    //    those must differ, so that the list cannot outlive its reason;
    //  - any other release: reported and not gated;
    //  - stb_truetype: another rasteriser's numbers, reported and not gated.
    const char* const loader = context.atlas()->FontLoaderName;
    std::printf("font_loader: %d of %d glyph boxes and %d advances differ from the reference's "
                "(FreeType 2.13.3); ascent or descent differs at %d of %d sizes; this build's "
                "loader is %s\n",
                differing, boxes, advances, metrics, sizes, loader);
    std::printf("%s", report.c_str());
    if (!context.freetype()) {
        std::printf("    SKIPPED the box comparison: not FreeType's numbers under %s\n", loader);
        return;
    }
    FreeTypeVersion linked;
    REQUIRE(linked_freetype(loader, &linked));
    bool reproduces = false;
    for (const FreeTypeVersion& v : FREETYPE_REPRODUCING_THE_GOLDEN)
        reproduces = reproduces || v == linked;
    if (reproduces) {
        CHECK_EQ(differing, 0);
        CHECK_EQ(advances, 0);
        CHECK_EQ(metrics, 0);
    } else if (linked == FreeTypeVersion{2, 14, 3}) {
        CHECK_EQ(unexpected, 0);
        // Fewer at 16 bits would be a box the build cannot ask for; none of
        // the listed ones is.
        CHECK_EQ(differing, static_cast<int>(sizeof BOXES_FREETYPE_2_14_3_MOVES /
                                             sizeof BOXES_FREETYPE_2_14_3_MOVES[0]));
        CHECK_EQ(advances, 0);
        CHECK_EQ(metrics, 0);
    } else {
        std::printf("    SKIPPED the box comparison: this build links FreeType %d.%d.%d, the "
                    "golden was written under 2.13.3, and %d.%d.%d has not been measured against "
                    "it (2.12.1, 2.13.2 and 2.13.3 reproduce it; 2.14.3 moves two boxes)\n",
                    linked.major, linked.minor, linked.patch, linked.major, linked.minor,
                    linked.patch);
    }
}

TEST(a_font_can_be_removed_and_the_atlas_torn_down_with_nothing_left_behind)
{
    FontLibrary library;
    register_defaults(library);
    for (int round = 0; round < 3; ++round) {
        Context context;
        ImFont* prose = add_face_to_atlas(context.atlas(), *library.face_data(Face::PROSE), 16.0f);
        ImFont* math = add_face_to_atlas(context.atlas(), *library.face_data(Face::MATH), 16.0f);
        REQUIRE(prose != nullptr);
        REQUIRE(math != nullptr);
        {
            Frame frame;
            // Bake something from each, so there is atlas state to tear down.
            CHECK(prose->GetFontBaked(16.0f)->FindGlyph('A')->Visible);
            CHECK(math->GetFontBaked(24.0f)->FindGlyph(static_cast<ImWchar>(0x2211))->Visible);
        }
        // One removed by hand between frames, the other left for the context.
        context.atlas()->RemoveFont(math);
        {
            Frame frame;
            CHECK(prose->GetFontBaked(20.0f)->FindGlyph('g')->Visible);
        }
    }
    // The library outlives every atlas and still answers.
    CHECK(library.metrics(Face::PROSE, 16.0) != nullptr);
}

TEST(the_font_built_has_the_checksum_the_format_defines)
{
    FontLibrary library;
    register_defaults(library);
    for (const Face role : {Face::PROSE, Face::CODE, Face::MATH}) {
        const FaceData* face = library.face_data(role);
        REQUIRE(face != nullptr);
        CHECK_CONTEXT("%s", face->name().c_str());
        std::vector<uint8_t> bytes;
        REQUIRE(build_atlas_font(*face, ATLAS_WCHAR_BITS, bytes));
        // With head.checkSumAdjustment applied, the big-endian words of the
        // whole file sum to this constant.
        uint32_t sum = 0;
        for (size_t i = 0; i + 4 <= bytes.size(); i += 4)
            sum += (uint32_t(bytes[i]) << 24) | (uint32_t(bytes[i + 1]) << 16) |
                   (uint32_t(bytes[i + 2]) << 8) | uint32_t(bytes[i + 3]);
        CHECK_EQ(sum, 0xB1B0AFBAu);
    }
}

TEST(a_face_with_a_damaged_glyph_measures_and_is_refused_by_the_atlas)
{
    std::vector<uint8_t> bytes;
    REQUIRE(golden::read_file(font_path("Roboto-Regular.ttf"), bytes));
    // The loca entries of 'A', aimed past the end of glyf. loca is in the
    // short format in this file: an entry is half the offset.
    const auto be32 = [&](size_t at) {
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
               (uint32_t(bytes[at + 2]) << 8) | uint32_t(bytes[at + 3]);
    };
    size_t loca = 0, loca_size = 0;
    const size_t tables = (size_t(bytes[4]) << 8) | bytes[5];
    for (size_t i = 0; i < tables; ++i) {
        if (std::memcmp(bytes.data() + 12 + i * 16, "loca", 4) == 0) {
            loca = be32(12 + i * 16 + 8);
            loca_size = be32(12 + i * 16 + 12);
        }
    }
    FontLibrary intact;
    REQUIRE(intact.register_font(Face::PROSE, bytes.data(), bytes.size(), "Roboto-Regular.ttf"));
    const FaceData* whole = intact.face_data(Face::PROSE);
    REQUIRE_EQ(loca_size, (size_t(whole->font().glyph_count()) + 1) * 2);
    const size_t glyph = whole->glyph('A');
    bytes[loca + glyph * 2] = 0xFF;
    bytes[loca + glyph * 2 + 1] = 0xF0;
    bytes[loca + glyph * 2 + 2] = 0xFF;
    bytes[loca + glyph * 2 + 3] = 0xFF;

    FontLibrary library;
    std::string why;
    // For layout the damage is one glyph's: it has an advance and no ink.
    REQUIRE(library.register_font(Face::PROSE, bytes.data(), bytes.size(), "damaged.ttf", &why));
    const FaceData* face = library.face_data(Face::PROSE);
    const FontMetrics* m = library.metrics(Face::PROSE, 16.0);
    REQUIRE(m != nullptr);
    CHECK(m->advance('A') > 0.0);
    CHECK_EQ(m->bbox('A').width(), 0.0);
    CHECK(m->bbox('H').width() > 0.0);

    // For the atlas it is the whole font's: a rasteriser would follow it.
    Context context;
    const int before = context.atlas()->Fonts.Size;
    ImFont* font = add_face_to_atlas(context.atlas(), *face, 16.0f, nullptr, &why);
    CHECK(font == nullptr);
    CHECK(why.find("damaged.ttf cannot be given to a rasteriser") != std::string::npos);
    CHECK_EQ(context.atlas()->Fonts.Size, before);
    CHECK(!face->font().strict_check());
    // The atlas is as usable as before.
    ImFont* good = add_face_to_atlas(context.atlas(), *intact.face_data(Face::PROSE), 16.0f);
    REQUIRE(good != nullptr);
    Frame frame;
    CHECK(good->GetFontBaked(16.0f)->FindGlyph('A')->Visible);
}
