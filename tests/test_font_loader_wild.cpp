// src/font_loader over the real-font corpus: a deterministic sample of the
// faces tests/golden/font-wild names, each read by src/fontlib, handed to a
// headless ImGui atlas through add_face_to_atlas, and baked at 16 px by
// whichever rasteriser this build of ImGui has (FreeType, or stb_truetype in
// the `stb` configuration; the test prints which). It is to be run under both.
//
// What it establishes, per face:
//   - the atlas takes the face (the strict pass refuses no installed font, and
//     ImGui's loader accepts the single-face file build_atlas_font writes,
//     from a collection too);
//   - ImFont::IsGlyphInFont agrees with FaceData::has for a sample of
//     codepoints the face maps, codepoints it does not, and the codepoints
//     minted for it. This is the evidence for decision D27: the rasteriser
//     reads the cmap written from layout's own view of the font, so the two
//     cannot disagree about what the font has;
//   - a glyph whose ink box is a pixel or more each way at that size bakes to
//     a visible glyph whose texels have coverage.
//
// The fonts are not in the repository: a face whose file is absent or is not
// the file that was dumped is skipped and counted, and with none present the
// test passes having compared nothing, and says so.
//
// By default every LOADER_STRIDE-th face is visited, and every
// COLLECTION_STRIDE-th face that lives in a collection as well (those are
// the CID-keyed CFF faces) and every face with a MATH table (those have
// minted codepoints): over 300 faces of this corpus.
// IMTYPESET_WILD_ALL=1 visits every face; IMTYPESET_WILD_STRIDE=n sets both
// strides; IMTYPESET_WILD_TRACE=1 prints each face before it is loaded.

#include "golden.h"
#include "wild_corpus.h"

#include "font_loader.h"
#include "fontlib.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr long LOADER_STRIDE = 9;
constexpr long COLLECTION_STRIDE = 5;
constexpr float BAKE_SIZE = 16.0f;
// Codepoints of a face's own cmap that are sampled, beside ASCII.
constexpr size_t CMAP_SAMPLE = 160;

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

// A frame, because an ImFontBaked is only good inside one.
struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

struct Tally {
    long visited = 0, skipped_absent = 0, skipped_changed = 0;
    long refused_by_reader = 0, refused_by_atlas = 0, baked = 0;
    // Faces with a glyph whose header box the atlas's copy has raised
    // (OpenTypeFont::atlas_box_repairs), and how many glyphs.
    long box_repair_faces = 0, box_repair_glyphs = 0;
    std::string box_repair_example;
    long collection_faces = 0, cid_faces = 0, cff_faces = 0, minting_faces = 0;
    long codepoints = 0, present = 0, absent = 0, minted = 0, past_last_glyph = 0;
    long disagreements = 0;
    long inked = 0, inked_invisible = 0, inked_without_coverage = 0;
    long faces_without_a_sampled_ink_glyph = 0;
    std::map<std::string, std::pair<long, std::string>> reader_reasons, atlas_reasons;
    std::string loader = "(no face was baked)";
};

// The codepoints a face is asked about: ASCII; a spread of what its cmap
// maps; the codepoint after each sampled one, which is mostly one it does not
// map; what was minted for it; and a few no font is likely to have.
std::vector<uint32_t> sample_codepoints(const FaceData& face)
{
    std::vector<uint32_t> out;
    for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        out.push_back(cp);
    uint64_t mapped = 0;
    for (const CmapRange& range : face.font().cmap())
        mapped += uint64_t(range.last) - range.first + 1;
    const uint64_t step = std::max<uint64_t>(1, mapped / CMAP_SAMPLE);
    uint64_t seen = 0;
    for (const CmapRange& range : face.font().cmap()) {
        // The first codepoint of the range at or after the next multiple of `step`.
        uint64_t next = (seen + step - 1) / step * step;
        for (; next < seen + (uint64_t(range.last) - range.first + 1); next += step) {
            const uint32_t cp = range.first + static_cast<uint32_t>(next - seen);
            out.push_back(cp);
            out.push_back(cp + 1);
        }
        seen += uint64_t(range.last) - range.first + 1;
    }
    size_t taken = 0;
    for (const SyntheticCodepoint& minted : face.synthetic_codepoints())
        if (taken++ % 4 == 0)
            out.push_back(minted.codepoint);
    for (uint32_t cp :
         {0x0378u, 0xE000u, 0xF8FFu, 0xFFFEu, 0x1FFFEu, 0xEFFFFu, 0xF0000u, 0xFFFFDu, 0x10FFFFu})
        out.push_back(cp);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void visit(const wild::CorpusFace& face, wild::CorpusFile& file, Tally& tally)
{
    const std::string label = face.label();
    if (wild::env_flag("IMTYPESET_WILD_TRACE")) {
        std::printf("FACE %s\n", label.c_str());
        std::fflush(stdout);
    }
    ++tally.visited;
    const int state = file.open(face);
    if (state != 0) {
        ++(state == 1 ? tally.skipped_absent : tally.skipped_changed);
        return;
    }
    CHECK_CONTEXT("%s", label.c_str());

    FontLibrary library;
    std::string why;
    if (!library.register_font(Face::PROSE, file.bytes.data(), file.bytes.size(), label.c_str(),
                               &why, face.index)) {
        ++tally.refused_by_reader;
        auto& entry = tally.reader_reasons[why];
        if (entry.first++ == 0)
            entry.second = label;
        return;
    }
    const FaceData* data = library.face_data(Face::PROSE);
    REQUIRE(data != nullptr);
    const OpenTypeFont& font = data->font();

    Context context;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* im_font = add_face_to_atlas(atlas, *data, BAKE_SIZE, nullptr, &why);
    if (im_font == nullptr) {
        ++tally.refused_by_atlas;
        auto& entry = tally.atlas_reasons[why];
        if (entry.first++ == 0)
            entry.second = label;
        return;
    }
    ++tally.baked;
    if (!font.atlas_box_repairs().empty()) {
        if (tally.box_repair_faces++ == 0)
            tally.box_repair_example = label;
        tally.box_repair_glyphs += static_cast<long>(font.atlas_box_repairs().size());
    }
    tally.collection_faces += face.face_count > 1;
    tally.cff_faces += font.is_cff();
    tally.cid_faces += font.is_cid_keyed();
    tally.minting_faces += !data->synthetic_codepoints().empty();

    const std::vector<uint32_t> sample = sample_codepoints(*data);
    Frame frame;
    ImFontBaked* baked = im_font->GetFontBaked(BAKE_SIZE);
    REQUIRE(baked != nullptr);
    if (atlas->FontLoaderName != nullptr)
        tally.loader = atlas->FontLoaderName;

    // The codepoints whose glyphs have ink, as the atlas names them.
    std::vector<ImWchar> inked;
    for (const uint32_t cp : sample) {
        uint32_t glyph = 0;
        const bool has = data->lookup(cp, &glyph);
        // What the atlas is asked for: the codepoint itself, or in a 16-bit
        // ImWchar build the stand-in of a supplementary one. Zero is "this
        // build cannot name it", which is not a disagreement.
        const uint32_t asked = atlas_codepoint(*data, cp);
        if (asked == 0)
            continue;
        // A codepoint the file maps past its last glyph is one the reader
        // reports as the file has it and the atlas's cmap leaves out.
        const bool drawable = has && glyph < font.glyph_count();
        tally.past_last_glyph += has && !drawable;
        ++tally.codepoints;
        tally.present += drawable;
        tally.absent += !drawable;
        tally.minted += cp >= SYNTHETIC_LAST && cp <= SYNTHETIC_FIRST && drawable;
        const bool in_font = im_font->IsGlyphInFont(static_cast<ImWchar>(asked));
        if (in_font != drawable) {
            ++tally.disagreements;
            FAIL("U+%04X (asked of the atlas as U+%04X): IsGlyphInFont %d, FaceData::has %d", cp,
                 asked, in_font ? 1 : 0, drawable ? 1 : 0);
        }
        if (!drawable)
            continue;
        const GlyphBounds box = font.bounds(glyph);
        // Ink a pixel or more each way at this size. Less than that may bake
        // to nothing and be right: a font's own hinting program can close a
        // stroke under a pixel thick altogether (the underscore of
        // Sahadeva, 0.47 px at 16 px, under FreeType).
        const double pixel = static_cast<double>(font.hhea_ascender() - font.hhea_descender()) /
                             static_cast<double>(BAKE_SIZE);
        // U+0020 is left out: ImGui marks the space glyph hidden whatever it
        // holds (ImFontAtlasBuildSetupFontBakedBlanks, imgui_draw.cpp), and
        // four fonts of the corpus have ink in theirs.
        if (cp != 0x20 && box.x_max - box.x_min >= pixel && box.y_max - box.y_min >= pixel &&
            inked.size() < 96)
            inked.push_back(static_cast<ImWchar>(asked));
    }
    // Everything is asked for before any texel is read: the texture may be
    // replaced by a larger one while glyphs are still arriving.
    for (const ImWchar c : inked)
        (void)baked->FindGlyph(c);
    ImTextureData* texture = atlas->TexData;
    REQUIRE(texture != nullptr);
    REQUIRE(texture->Pixels != nullptr);
    if (inked.empty())
        ++tally.faces_without_a_sampled_ink_glyph;
    for (const ImWchar c : inked) {
        ++tally.inked;
        const ImFontGlyph* glyph = baked->FindGlyph(c);
        if (glyph == nullptr || !glyph->Visible) {
            ++tally.inked_invisible;
            FAIL("U+%04X has an ink box and is not a visible glyph in the atlas",
                 static_cast<unsigned>(c));
            continue;
        }
        const float w = static_cast<float>(texture->Width), h = static_cast<float>(texture->Height);
        const int x0 = std::max(0, static_cast<int>(glyph->U0 * w + 0.5f));
        const int y0 = std::max(0, static_cast<int>(glyph->V0 * h + 0.5f));
        const int x1 = std::min(texture->Width, static_cast<int>(glyph->U1 * w + 0.5f));
        const int y1 = std::min(texture->Height, static_cast<int>(glyph->V1 * h + 0.5f));
        long covered = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const unsigned char* texel =
                    static_cast<const unsigned char*>(texture->GetPixelsAt(x, y));
                // Alpha is the last byte of an RGBA32 texel and the only byte
                // of an Alpha8 one.
                covered += texel[texture->BytesPerPixel - 1] != 0;
            }
        }
        if (covered == 0) {
            ++tally.inked_without_coverage;
            FAIL("U+%04X is a visible glyph with no coverage in the atlas texture",
                 static_cast<unsigned>(c));
        }
    }
}

} // namespace

TEST(the_atlas_and_the_metrics_agree_on_every_sampled_codepoint_of_the_installed_fonts)
{
    const std::vector<wild::CorpusFace> faces = wild::corpus_faces();
    REQUIRE(!faces.empty());

    long stride = wild::env_flag("IMTYPESET_WILD_ALL") ? 1 : LOADER_STRIDE;
    long collection_stride = wild::env_flag("IMTYPESET_WILD_ALL") ? 1 : COLLECTION_STRIDE;
    if (const char* v = std::getenv("IMTYPESET_WILD_STRIDE"))
        if (std::atol(v) > 0)
            stride = collection_stride = std::atol(v);

    Tally tally;
    wild::CorpusFile file;
    for (const wild::CorpusFace& face : faces) {
        const bool sampled = face.ordinal % stride == 0 || face.has_math ||
                             (face.face_count > 1 && face.ordinal % collection_stride == 0);
        if (sampled)
            visit(face, file, tally);
    }

    const long present = tally.visited - tally.skipped_absent - tally.skipped_changed;
    std::printf("LOADER-WILD rasteriser: %s; ImWchar %d bits\n", tally.loader.c_str(),
                ATLAS_WCHAR_BITS);
    std::printf("LOADER-WILD faces in the goldens %zu; strides %ld and %ld; visited %ld; "
                "skipped: absent %ld, changed %ld\n",
                faces.size(), stride, collection_stride, tally.visited, tally.skipped_absent,
                tally.skipped_changed);
    std::printf("LOADER-WILD given to the atlas and baked %ld (in collections %ld; CFF %ld, of "
                "them CID-keyed %ld; with minted codepoints %ld); refused by the reader %ld; "
                "REFUSED BY THE ATLAS %ld\n",
                tally.baked, tally.collection_faces, tally.cff_faces, tally.cid_faces,
                tally.minting_faces, tally.refused_by_reader, tally.refused_by_atlas);
    std::printf("LOADER-WILD codepoints asked %ld (the face has %ld, of them minted %ld; has not "
                "%ld; mapped past the last glyph %ld); IsGlyphInFont DISAGREES with has() on %ld\n",
                tally.codepoints, tally.present, tally.minted, tally.absent, tally.past_last_glyph,
                tally.disagreements);
    std::printf("LOADER-WILD glyphs with ink baked %ld; NOT VISIBLE %ld; VISIBLE WITHOUT COVERAGE "
                "%ld; faces with no sampled glyph that has ink %ld\n",
                tally.inked, tally.inked_invisible, tally.inked_without_coverage,
                tally.faces_without_a_sampled_ink_glyph);
    std::printf("LOADER-WILD faces with a glyph box raised over its outline for the atlas %ld "
                "(%ld glyphs)%s%s\n",
                tally.box_repair_faces, tally.box_repair_glyphs,
                tally.box_repair_faces != 0 ? "; e.g. " : "", tally.box_repair_example.c_str());
    for (const auto& entry : tally.reader_reasons)
        std::printf("LOADER-WILD reader refused %ld | %s | e.g. %s\n", entry.second.first,
                    entry.first.c_str(), entry.second.second.c_str());
    for (const auto& entry : tally.atlas_reasons)
        std::printf("LOADER-WILD atlas refused %ld | %s | e.g. %s\n", entry.second.first,
                    entry.first.c_str(), entry.second.second.c_str());
    if (present == 0)
        std::printf("LOADER-WILD no font of the corpus is present here: nothing was loaded\n");
    std::fflush(stdout);

    // A font the reader accepted and the atlas would not take is a font a
    // host can measure and not draw.
    CHECK_EQ(tally.refused_by_atlas, 0L);
    CHECK_EQ(tally.disagreements, 0L);
    CHECK_EQ(tally.inked_invisible, 0L);
    CHECK_EQ(tally.inked_without_coverage, 0L);
    // The sample is the point: where the corpus is installed, it is over 300
    // faces unless a stride was asked for.
    if (present > 0 && tally.skipped_absent + tally.skipped_changed == 0 &&
        std::getenv("IMTYPESET_WILD_STRIDE") == nullptr)
        CHECK(tally.baked + tally.refused_by_reader >= 300);
}
