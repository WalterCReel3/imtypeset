// The font specimen the Python reference builds (tests/golden/specimen,
// tools/oracle/FORMAT-specimen.md), replayed in scaled points: the width of
// one line in each of six faces, of one face up a ladder of sizes, and of
// seven pairs of letters with and without their kern.
//
// The text goes in through the public header, as the demo's Specimen section
// hands it over, and the answers are read inside the library as integers. A
// public answer is a float pixel, which from 256 px up steps by more than a
// scaled point: it cannot carry what is compared here.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "nodes.h"
#include "pack.h"
#include "style.h"
#include "units.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

// A Face of the golden as the public header names it; -1 if it is none.
ImTypesetFace face_of(const std::string& name)
{
    static const struct {
        const char* name;
        ImTypesetFace face;
    } FACES[] = {
        {"prose", ImTypesetFace_Regular},       {"prose_bold", ImTypesetFace_Bold},
        {"prose_italic", ImTypesetFace_Italic}, {"prose_bold_italic", ImTypesetFace_BoldItalic},
        {"code", ImTypesetFace_Code},           {"math", ImTypesetFace_Math},
    };
    for (const auto& entry : FACES) {
        if (name == entry.name)
            return entry.face;
    }
    return -1;
}

struct Host {
    Host()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx != nullptr)
            paragraph = ImTypeset::CreateParagraph();
    }
    ~Host()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    ImTypesetContext* ctx = nullptr;
    ImTypesetParagraph* paragraph = nullptr;
};

// One call of the reference's `_set`, as the golden records it.
struct Set {
    std::string role;
    ImTypesetFace face = -1;
    Scaled size = 0;
    bool kerned = false;
    std::string text;
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    int line = 0;
    // What the box holds, in order.
    struct Item {
        char kind = 0; // 'c', 'k' or 'g'
        uint32_t codepoint = 0;
        Scaled a = 0; // width or amount
        Scaled b = 0; // height, or stretch
        Scaled c = 0; // depth, or shrink
        int line = 0;
    };
    std::vector<Item> items;
};

// The pixels that give `size` scaled points at `px_per_inch`, as the float a
// host hands over. set_line() checks that the library came back to `size`.
float pixels_for(Scaled size, double px_per_inch)
{
    const double px = static_cast<double>(size) / 65536.0 * px_per_inch / 72.27;
    return static_cast<float>(px);
}

// `set.text` laid out on one line in its face at its size, left-aligned.
// Returns the natural width of the line, and checks on the way that the size
// the library set it at is the golden's integer.
bool set_line(Host& host, const Set& set, double px_per_inch, Scaled* width)
{
    ImTypesetStyle style;
    style.Face = set.face;
    style.EmSize = pixels_for(set.size, px_per_inch);
    style.PixelsPerInch = static_cast<float>(px_per_inch);
    style.Align = ImTypesetAlign_Left;
    style.FirstLineIndent = 0.0f;
    // The golden is the reference's, which measures every face by its font.
    // The product's default measures the Code faces on the device and sets
    // their spaces as cells that do not stretch (docs/decisions.md, D77 and
    // D79): the specimen's code row has the font's widths there only because
    // its em is a whole number of pixels, and its glue is not the
    // reference's at all.
    style.Advances = ImTypesetAdvances_Font;
    ImTypesetParagraph* const p = host.paragraph;
    if (!ImTypeset::SetParagraphText(p, set.text.c_str())) {
        FAIL("SetParagraphText: %s", ImTypeset::GetLastError()->Message);
        return false;
    }
    if (!ImTypeset::LayoutParagraph(p, 100000.0f, &style)) {
        FAIL("LayoutParagraph: %s", ImTypeset::GetLastError()->Message);
        return false;
    }
    if (!CHECK_EQ(ImTypeset::GetParagraphLineCount(p), 1))
        return false;
    // The size is the row's integer, or nothing below compares like with like.
    if (!CHECK_EQ(p->layout_style.em, set.size))
        return false;
    // The line's box is found at the first question about it.
    (void)ImTypeset::GetParagraphLine(p, 0);
    if (!CHECK_EQ(p->line_boxes.size(), size_t(1)))
        return false;
    *width = p->line_boxes[0].x1 - p->line_boxes[0].x0;
    return true;
}

// The characters, kerns and interword glue of the paragraph's material against
// the reference's box, one for one: where a width disagrees, this says at
// which glyph.
void check_material(Host& host, const Set& set)
{
    const ImTypesetParagraph& p = *host.paragraph;
    const CharMeasure measure = host.ctx->measure();
    size_t next = 0;
    for (const Node& node : p.block.run.nodes) {
        if (!node.is(NodeType::CHAR) && !node.is(NodeType::KERN) && !node.is(NodeType::GLUE))
            continue;
        if (next >= set.items.size()) {
            FAIL("the material holds more than the reference's %zu items", set.items.size());
            return;
        }
        const Set::Item& item = set.items[next++];
        CHECK_CONTEXT("golden line %d", item.line);
        if (node.is(NodeType::CHAR)) {
            REQUIRE_EQ(item.kind, 'c');
            CHECK_EQ(node.codepoint(), item.codepoint);
            const CharDimens dimens = measure(node.font(), node.codepoint());
            CHECK_EQ(dimens.width, item.a);
            CHECK_EQ(dimens.height, item.b);
            CHECK_EQ(dimens.depth, item.c);
        } else if (node.is(NodeType::KERN)) {
            REQUIRE_EQ(item.kind, 'k');
            CHECK_EQ(node.width(), item.a);
        } else {
            REQUIRE_EQ(item.kind, 'g');
            CHECK_EQ(node.width(), item.a);
            CHECK_EQ(node.stretch(), item.b);
            CHECK_EQ(node.shrink(), item.c);
            CHECK(node.stretch_order == GlueOrder::NORMAL);
            CHECK(node.shrink_order == GlueOrder::NORMAL);
        }
    }
    CHECK_EQ(next, set.items.size());
}

} // namespace

TEST(the_specimens_widths_are_the_references_to_the_scaled_point)
{
    const std::string path = golden::path("specimen/specimen.golden");
    golden::File file;
    REQUIRE(file.load(path));
    Host host;
    REQUIRE(host.ctx != nullptr && host.paragraph != nullptr);

    double px_per_inch = 0.0;
    int faces_added = 0;
    int rows = 0, pairs = 0, sets = 0, sums = 0;
    bool in_set = false;
    Set set;
    // The row the sets that follow belong to, and its pair.
    Scaled row_size = 0, row_width = 0;
    ImTypesetFace row_face = -1;
    bool has_pair = false;
    Scaled pair_kern = 0, pair_kerned = 0, pair_unkerned = 0;
    Scaled got_kerned = 0, got_left = 0, got_right = 0;
    int pair_parts = 0;

    for (const golden::Record& r : file) {
        CHECK_CONTEXT("%s line %d", path.c_str(), r.line);
        if (r.is("theme")) {
            px_per_inch = r.f64(0);
        } else if (r.is("face")) {
            const ImTypesetFace face = face_of(r.str(0));
            REQUIRE(face >= 0);
            const std::string font = golden::data_path("fonts/" + r.str(3));
            if (ImTypeset::AddFontFromFileTTF(face, font.c_str()) == nullptr)
                FAIL("%s: %s", font.c_str(), ImTypeset::GetLastError()->Message);
            else
                ++faces_added;
        } else if (r.is("row")) {
            golden::Cursor c(r);
            (void)c.str();
            row_face = face_of(c.str());
            row_size = c.i64();
            row_width = c.i64();
            (void)c.i64();
            has_pair = c.boolean();
            pair_parts = 0;
            ++rows;
        } else if (r.is("pair")) {
            pair_kern = r.i64(2);
            pair_kerned = r.i64(3);
            pair_unkerned = r.i64(4);
            // The golden's own sum, so that a misread field is not taken for
            // a disagreement of the library's.
            CHECK_EQ(pair_unkerned + pair_kern, pair_kerned);
            CHECK_EQ(row_width, pair_kerned);
            ++pairs;
        } else if (r.is("set")) {
            golden::Cursor c(r);
            set = Set();
            set.line = r.line;
            set.role = c.str();
            set.face = face_of(c.str());
            set.size = c.i64();
            set.kerned = c.boolean();
            set.text = c.str();
            set.width = c.i64();
            set.height = c.i64();
            set.depth = c.i64();
            in_set = true;
        } else if (in_set && r.is("char")) {
            Set::Item item;
            item.kind = 'c';
            item.codepoint = r.u32(0);
            item.a = r.i64(1);
            item.b = r.i64(2);
            item.c = r.i64(3);
            item.line = r.line;
            set.items.push_back(item);
        } else if (in_set && r.is("kern")) {
            Set::Item item;
            item.kind = 'k';
            item.a = r.i64(0);
            item.line = r.line;
            set.items.push_back(item);
        } else if (in_set && r.is("glue")) {
            Set::Item item;
            item.kind = 'g';
            item.a = r.i64(0);
            item.b = r.i64(1);
            item.c = r.i64(3);
            item.line = r.line;
            CHECK_EQ(r.str(2), std::string("normal"));
            CHECK_EQ(r.str(4), std::string("normal"));
            set.items.push_back(item);
        } else if (r.is("endset")) {
            in_set = false;
            ++sets;
            CHECK_CONTEXT("set %s \"%s\" (golden line %d)", set.role.c_str(), set.text.c_str(),
                          set.line);
            REQUIRE(px_per_inch > 0.0);
            REQUIRE(set.face >= 0);
            CHECK_EQ(set.face, row_face);
            CHECK_EQ(set.size, row_size);
            // The reference set this one with kerning off, which nothing in
            // the library does: it is held to below as the two letters apart.
            if (!set.kerned) {
                CHECK_EQ(set.role, std::string("unkerned"));
                CHECK_EQ(set.width, pair_unkerned);
                continue;
            }
            Scaled width = 0;
            if (!set_line(host, set, px_per_inch, &width))
                continue;
            CHECK_EQ(width, set.width);
            check_material(host, set);
            if (set.role == "sample") {
                CHECK(!has_pair);
                CHECK_EQ(width, row_width);
            } else if (set.role == "kerned") {
                CHECK_EQ(width, pair_kerned);
                got_kerned = width;
                ++pair_parts;
            } else if (set.role == "left") {
                got_left = width;
                ++pair_parts;
            } else if (set.role == "right") {
                got_right = width;
                ++pair_parts;
                // The last of a pair's four: the letters set apart come to
                // the unkerned setting, and the pair set together differs
                // from that by the font's kern, with its sign.
                REQUIRE_EQ(pair_parts, 3);
                CHECK_EQ(got_left + got_right, pair_unkerned);
                CHECK_EQ(got_kerned - (got_left + got_right), pair_kern);
                ++sums;
            } else {
                FAIL("a set of a role this test does not know: %s", set.role.c_str());
            }
        }
    }
    // What the golden holds, so that a reader that skipped it does not pass.
    CHECK_EQ(faces_added, 6);
    CHECK_EQ(rows, 19);
    CHECK_EQ(pairs, 7);
    CHECK_EQ(sets, 40);
    CHECK_EQ(sums, 7);
}

// The sizes the demo's Specimen section sets its ladder at are floats reached
// by dividing and multiplying from 16 by 1.25 (src/imtypeset_demo.cpp,
// ShowDemoSpecimen). Each must come to the scaled points the reference set
// its rung at, or the section shows other widths than the golden's.
TEST(the_demos_ladder_sizes_are_the_references_scaled_points)
{
    const std::string path = golden::path("specimen/specimen.golden");
    golden::File file;
    REQUIRE(file.load(path));
    Host host;
    REQUIRE(host.ctx != nullptr && host.paragraph != nullptr);
    REQUIRE(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                          golden::data_path("fonts/Roboto-Regular.ttf").c_str()) !=
            nullptr);

    float ladder[6];
    ladder[2] = 16.0f;
    for (int i = 1; i >= 0; --i)
        ladder[i] = ladder[i + 1] / 1.25f;
    for (int i = 3; i < 6; ++i)
        ladder[i] = ladder[i - 1] * 1.25f;

    int rung = 0;
    bool in_ladder = false;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("%s line %d", path.c_str(), r.line);
        if (r.is("section"))
            in_ladder = r.str(0) == "A size ladder";
        if (!in_ladder || !r.is("row"))
            continue;
        REQUIRE(rung < 6);
        ImTypesetStyle style;
        style.EmSize = ladder[rung];
        style.Align = ImTypesetAlign_Left;
        ImTypeset::SetParagraphText(host.paragraph, "Hamburgefonstiv");
        REQUIRE(ImTypeset::LayoutParagraph(host.paragraph, 100000.0f, &style));
        CHECK_EQ(host.paragraph->layout_style.em, r.i64(2));
        (void)ImTypeset::GetParagraphLine(host.paragraph, 0);
        REQUIRE_EQ(host.paragraph->line_boxes.size(), size_t(1));
        CHECK_EQ(host.paragraph->line_boxes[0].x1 - host.paragraph->line_boxes[0].x0, r.i64(3));
        ++rung;
    }
    CHECK_EQ(rung, 6);
}
