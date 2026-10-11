// What a host building a grid of code on the Document asked for, each pinned
// through the public header: a block whose lines keep an exact pitch whatever
// glyphs they hold (ImTypesetBlockFlags_FixedLineHeight), whether a role's own
// font has a glyph (FaceHasGlyph), the tag of the run under a point of a
// paragraph (ImTypesetHit::Tag), how a text's natural width is read when it is
// not left-aligned, and a document drawn in slices under clip rectangles that
// meet (each glyph in one slice only).
//
// src/context.h is included for the one test that draws under the reference's
// device rules to show what the product's rule changes, and
// src/imtypeset_document.h for the line boxes in scaled points, where "exact"
// can be held to the unit.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imtypeset_document.h"

#include <cmath>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

struct Stage {
    Stage()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        ImTypeset::GetStyle().EmSize = 16.0f;
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

// One code block, its lines asked to be `pitch` px apart at a 16 px em.
ImTypesetDocument* code_document(const std::string& text, ImTypesetBlockFlags flags,
                                 float pitch = 20.0f)
{
    ImTypesetBlock block;
    block.Kind = ImTypesetBlockKind_Code;
    block.Flags = flags;
    block.TextStart = 0;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetBlockStyle style;
    style.LineHeight = pitch / 16.0f;
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    content.Styles = &style;
    content.StyleCount = 1;
    ImTypesetDocumentOptions options;
    options.CodeOverflow = ImTypesetCodeOverflow_Grow;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    ImTypeset::SetDocumentOptions(document, &options);
    ImTypeset::SetDocumentBlocks(document, &content);
    ImTypeset::LayoutDocument(document, 600.0f);
    return document;
}

// U+2320 (the top half of an integral sign) and U+27E8 are not in the bundled
// code face and are set from the math face; U+0416 from the regular face.
const char* const TALL_LINES = "plain\n"
                               "a \xE2\x8C\xA0 b \xE2\x9F\xA8 c \xD0\x96\n"
                               "below\n"
                               "last";

// The block's line boxes as layout holds them, in scaled points.
const std::vector<LineDims>& line_boxes(ImTypesetDocument* document)
{
    return block_document(*document).layout(0).lines;
}

int vertices_in(ImTypesetDocument* document, float clip_top, float clip_bottom)
{
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f),
                                     ImVec4(-100.0f, clip_top, 4000.0f, clip_bottom), &vertices,
                                     &indices);
    return vertices.Size;
}

} // namespace

// --- R1: an exact line height -------------------------------------------------------------

TEST(without_the_flag_a_line_holding_a_borrowed_glyph_is_taller_and_moves_the_lines_below)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // The premise: the code face lacks the two characters and another has them.
    REQUIRE(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x2320));
    REQUIRE(ImTypeset::FaceHasGlyph(ImTypesetFace_Math, 0x2320));

    ImTypesetDocument* document = code_document(TALL_LINES, 0);
    REQUIRE(document != nullptr);
    const ImTypesetBlockGeometry geometry = ImTypeset::GetDocumentBlockGeometry(document, 0);
    REQUIRE_EQ(geometry.LineCount, 4);
    const ImTypesetLine plain = ImTypeset::GetDocumentBlockLine(document, 0, 0);
    const ImTypesetLine tall = ImTypeset::GetDocumentBlockLine(document, 0, 1);
    const ImTypesetLine last = ImTypeset::GetDocumentBlockLine(document, 0, 3);
    CHECK_NEAR(plain.Max.y - plain.Min.y, 20.0f, 1.0e-4f);
    // The finding this flag answers: the line is taller than was asked, and
    // the last line is no longer three line heights down.
    CHECK(tall.Max.y - tall.Min.y > 20.5f);
    CHECK(last.Min.y > 60.5f);
    ImTypeset::DestroyDocument(document);
}

TEST(with_the_flag_every_line_of_a_code_block_is_on_the_pitch_whatever_it_holds)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetDocument* document = code_document(TALL_LINES, ImTypesetBlockFlags_FixedLineHeight);
    REQUIRE(document != nullptr);
    const ImTypesetBlockGeometry geometry = ImTypeset::GetDocumentBlockGeometry(document, 0);
    REQUIRE_EQ(geometry.LineCount, 4);
    // In scaled points, exactly: every box is the pitch, the tall line's too.
    // The pitch is 20 px as layout holds it, a whole number of scaled points.
    const std::vector<LineDims>& boxes = line_boxes(document);
    REQUIRE_EQ(boxes.size(), size_t(4));
    const Scaled pitch = boxes[0].height + boxes[0].depth;
    for (const LineDims& box : boxes) {
        CHECK_EQ(box.height, boxes[0].height);
        CHECK_EQ(box.depth, boxes[0].depth);
    }
    CHECK_EQ(block_document(*document).layout(0).extent, 4 * pitch);
    // In pixels: 20 to within the scaled point the pitch was rounded to.
    CHECK_NEAR(geometry.FullMax.y - geometry.FullMin.y, 80.0f, 1.0e-3f);
    const float first = ImTypeset::GetDocumentBlockLine(document, 0, 0).Baseline;
    for (int line = 0; line < 4; ++line) {
        CHECK_CONTEXT("line %d", line);
        const ImTypesetLine box = ImTypeset::GetDocumentBlockLine(document, 0, line);
        CHECK_NEAR(box.Min.y, 20.0f * static_cast<float>(line), 1.0e-3f);
        CHECK_NEAR(box.Max.y, 20.0f * static_cast<float>(line + 1), 1.0e-3f);
        // The baselines are the pitch apart, the tall line's included.
        CHECK_NEAR(box.Baseline, first + 20.0f * static_cast<float>(line), 1.0e-3f);
    }
    // A selection's rectangle on the tall line is the pitch tall.
    ImVector<ImVec4> rects;
    const ImTypesetDocumentRange range = {{0, 6}, {0, 12}};
    REQUIRE_EQ(ImTypeset::GetDocumentRangeRects(document, range, &rects), 1);
    CHECK_NEAR(rects[0].y, 20.0f, 1.0e-3f);
    CHECK_NEAR(rects[0].w, 40.0f, 1.0e-3f);
    // The same text is drawn: the flag moves lines, it drops no glyph.
    ImTypesetDocument* loose = code_document(TALL_LINES, 0);
    REQUIRE(loose != nullptr);
    Frame frame;
    const int fixed_vertices = vertices_in(document, -100.0f, 1000.0f);
    CHECK(fixed_vertices > 0);
    CHECK_EQ(fixed_vertices, vertices_in(loose, -100.0f, 1000.0f));
    ImTypeset::DestroyDocument(loose);
    ImTypeset::DestroyDocument(document);
}

TEST(two_code_documents_with_the_flag_stay_in_step_over_many_lines)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    std::string plain;
    std::string tall;
    for (int line = 0; line < 500; ++line) {
        plain += "x = 1\n";
        tall += line % 7 == 3 ? "\xE2\x8C\xA0 \xE4\xB8\xAD \xD0\x96\n" : "x = 1\n";
    }
    ImTypesetDocument* a = code_document(plain, ImTypesetBlockFlags_FixedLineHeight);
    ImTypesetDocument* b = code_document(tall, ImTypesetBlockFlags_FixedLineHeight);
    REQUIRE(a != nullptr && b != nullptr);
    REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(a, 0).LineCount, 500);
    REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(b, 0).LineCount, 500);
    for (int line = 0; line < 500; line += 49) {
        CHECK_CONTEXT("line %d", line);
        // The two documents agree to the bit; arithmetic finds the row to
        // well under a pixel (the pitch is 20 px rounded to a scaled point).
        CHECK_EQ(ImTypeset::GetDocumentBlockLine(a, 0, line).Min.y,
                 ImTypeset::GetDocumentBlockLine(b, 0, line).Min.y);
        CHECK_NEAR(ImTypeset::GetDocumentBlockLine(b, 0, line).Min.y,
                   20.0f * static_cast<float>(line), 0.01f);
    }
    CHECK_EQ(block_document(*a).layout(0).extent, block_document(*b).layout(0).extent);
    CHECK_EQ(ImTypeset::GetDocumentSize(a).y, 10000.0f);
    CHECK_EQ(ImTypeset::GetDocumentSize(b).y, 10000.0f);
    // Without the flag the same two are out of step by the end.
    ImTypesetDocument* loose = code_document(tall, 0);
    REQUIRE(loose != nullptr);
    CHECK(ImTypeset::GetDocumentBlockLine(loose, 0, 499).Min.y >
          ImTypeset::GetDocumentBlockLine(a, 0, 499).Min.y + 20.0f);
    ImTypeset::DestroyDocument(loose);
    ImTypeset::DestroyDocument(a);
    ImTypeset::DestroyDocument(b);
}

TEST(the_flag_holds_the_lines_of_a_text_block_with_a_tall_formula)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text =
        "Before the sum $\\sum_{k=0}^{n} \\frac{k^2}{2}$ and a good deal of text "
        "after it so that the paragraph runs to several lines at this width, "
        "each of which must stand where arithmetic says it does.";
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_CONTEXT("pass %d", pass);
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Paragraph;
        block.Flags =
            ImTypesetBlockFlags_Math | (pass == 1 ? ImTypesetBlockFlags_FixedLineHeight : 0);
        block.TextEnd = static_cast<int>(text.size());
        ImTypesetDocumentContent content;
        content.Text = text.data();
        content.TextSize = static_cast<int>(text.size());
        content.Blocks = &block;
        content.BlockCount = 1;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        ImTypeset::SetDocumentBlocks(document, &content);
        ImTypeset::LayoutDocument(document, 300.0f);
        const int lines = ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount;
        REQUIRE(lines >= 3);
        const ImTypesetLine first = ImTypeset::GetDocumentBlockLine(document, 0, 0);
        const ImTypesetLine final_line = ImTypeset::GetDocumentBlockLine(document, 0, lines - 1);
        const float pitch = 16.0f * 1.35f;
        if (pass == 0) {
            CHECK(first.Max.y - first.Min.y > pitch + 0.5f);
        } else {
            CHECK_NEAR(first.Max.y - first.Min.y, pitch, 1.0e-3f);
            CHECK_NEAR(final_line.Min.y, pitch * static_cast<float>(lines - 1), 1.0e-2f);
        }
        ImTypeset::DestroyDocument(document);
    }
}

// --- R1b: does a role's own font have a glyph ---------------------------------------------

TEST(face_has_glyph_answers_for_the_role_alone)
{
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Regular, 'A')); // no context
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 'A'));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0xFFFD));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Regular, 'A'));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Default, 'A'));
    // In code these are set from the math and the regular face: the code
    // face alone lacks them.
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x2320));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x0416));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Regular, 0x0416));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Regular, 0x2320));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Math, 0x1D465)); // mathematical italic x
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x1D465));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0x110000));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Code, 0xFFFFFFFFu));
    // CodeBold has no font of its own here and answers for Bold's.
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_CodeBold, 0x0416));
    CHECK(ImTypeset::FaceHasGlyph(ImTypesetFace_Bold, 0x0416));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_COUNT, 'A'));
    CHECK(!ImTypeset::FaceHasGlyph(-3, 'A'));
    // It needs no ImGui context and no frame; a role without a font has no glyph.
    REQUIRE(ImTypeset::RemoveFont(ImTypesetFace_Italic));
    CHECK(!ImTypeset::FaceHasGlyph(ImTypesetFace_Italic, 'A'));
}

// --- Note 1: the tag under a point of a paragraph -----------------------------------------

TEST(a_paragraph_hit_answers_the_tag_of_the_run_under_the_point)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "plain linked words and bold";
    ImTypesetRun runs[2];
    runs[0].Start = 6;
    runs[0].End = 18; // "linked words"
    runs[0].Tag = 41;
    runs[1].Start = 23;
    runs[1].End = 27; // "bold"
    runs[1].Face = ImTypesetFace_Bold;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    REQUIRE(ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), runs, 2));
    ImTypesetStyle style;
    style.EmSize = 16.0f;
    style.Align = ImTypesetAlign_Left;
    REQUIRE(ImTypeset::LayoutParagraph(p, 1000.0f, &style));
    const ImTypesetLine line = ImTypeset::GetParagraphLine(p, 0);
    const float y = (line.Min.y + line.Max.y) / 2.0f;
    const auto x_of = [&](int byte) { return ImTypeset::GetParagraphCaretRect(p, byte).x; };

    const ImTypesetHit on_plain = ImTypeset::HitTestParagraph(p, ImVec2(x_of(2) + 1.0f, y));
    CHECK(on_plain.Inside);
    CHECK_EQ(on_plain.Tag, 0);
    // Every character of the link, its space included, at its left and right halves.
    for (int byte = 6; byte < 18; ++byte) {
        CHECK_CONTEXT("byte %d", byte);
        const float left = x_of(byte);
        const float right = x_of(byte + 1);
        CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(left + (right - left) * 0.25f, y)).Tag, 41);
        CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(left + (right - left) * 0.75f, y)).Tag, 41);
    }
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(x_of(20), y)).Tag, 0);
    CHECK_EQ(ImTypeset::HitTestParagraph(p, ImVec2(x_of(24) + 1.0f, y)).Tag, 0); // a run, no tag
    // Beside the text there is no run.
    const ImTypesetHit beside = ImTypeset::HitTestParagraph(p, ImVec2(line.Max.x + 50.0f, y));
    CHECK(!beside.Inside);
    CHECK_EQ(beside.Tag, 0);
    const ImTypesetHit above = ImTypeset::HitTestParagraph(p, ImVec2(x_of(8), line.Min.y - 30.0f));
    CHECK_EQ(above.Tag, 0);
    ImTypeset::DestroyParagraph(p);
}

TEST(a_document_hit_answers_the_tag_of_the_character_the_point_is_on)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "plain linked words and bold";
    ImTypesetRun run;
    run.Start = 6;
    run.End = 18;
    run.Tag = 41;
    ImTypesetBlock block;
    block.TextEnd = static_cast<int>(text.size());
    block.RunCount = 1;
    ImTypesetBlockStyle style;
    style.Align = ImTypesetAlign_Left;
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = static_cast<int>(text.size());
    content.Blocks = &block;
    content.BlockCount = 1;
    content.Runs = &run;
    content.RunCount = 1;
    content.Styles = &style;
    content.StyleCount = 1;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::SetDocumentBlocks(document, &content));
    ImTypeset::LayoutDocument(document, 1000.0f);
    const auto x_of = [&](int byte) {
        return ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, byte}).x;
    };
    const float y = 10.0f;
    for (int byte = 0; byte < static_cast<int>(text.size()); ++byte) {
        CHECK_CONTEXT("byte %d", byte);
        const float left = x_of(byte);
        const float right = x_of(byte + 1);
        const int expected = byte >= 6 && byte < 18 ? 41 : 0;
        CHECK_EQ(ImTypeset::HitTestDocument(document, ImVec2(left + (right - left) * 0.25f, y)).Tag,
                 expected);
        CHECK_EQ(ImTypeset::HitTestDocument(document, ImVec2(left + (right - left) * 0.75f, y)).Tag,
                 expected);
    }
    ImTypeset::DestroyDocument(document);
}

// --- Note 2: the natural width of a text that is not left-aligned -------------------------

TEST(the_natural_width_of_a_paragraph_is_read_off_its_line_whatever_the_alignment)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "A cell of a table, with bold in it.";
    ImTypesetRun run;
    run.Start = 24;
    run.End = 28;
    run.Face = ImTypesetFace_Bold;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    REQUIRE(ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), &run, 1));
    float natural = 0.0f;
    const ImTypesetAlign aligns[] = {ImTypesetAlign_Left, ImTypesetAlign_Justify,
                                     ImTypesetAlign_Center, ImTypesetAlign_Right};
    for (const ImTypesetAlign align : aligns) {
        CHECK_CONTEXT("align %d", align);
        ImTypesetStyle style;
        style.EmSize = 16.0f;
        style.Align = align;
        REQUIRE(ImTypeset::LayoutParagraph(p, 1.0e6f, &style));
        REQUIRE_EQ(ImTypeset::GetParagraphLineCount(p), 1);
        const ImTypesetLine line = ImTypeset::GetParagraphLine(p, 0);
        const float width = line.Max.x - line.Min.x;
        if (align == ImTypesetAlign_Left) {
            natural = width;
            CHECK(natural > 100.0f && natural < 400.0f);
            // Left-aligned, the paragraph's size is the same number, rounded up.
            CHECK_EQ(ImTypeset::GetParagraphSize(p).x, std::ceil(natural));
        } else {
            // A coordinate near a million pixels carries a sixteenth of one.
            CHECK_NEAR(width, natural, 0.07f);
            // The size is the wrap width: what a host sizing columns trips on.
            CHECK(ImTypeset::GetParagraphSize(p).x > 100.0f * natural);
        }
    }
    ImTypeset::DestroyParagraph(p);
}

// --- Note 3: slices under clip rectangles that meet ---------------------------------------

TEST(a_document_drawn_in_slices_that_meet_at_a_line_top_emits_each_glyph_in_one_slice)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    std::string text;
    for (int line = 0; line < 12; ++line)
        text += "some (code) on line " + std::to_string(line) + " jgy|\n";
    // Lines far enough apart that no glyph's bitmap leaves its line's box:
    // a cut between two lines then cuts no glyph, and every glyph belongs to
    // one slice.
    ImTypesetDocument* document = code_document(text, ImTypesetBlockFlags_FixedLineHeight, 32.0f);
    REQUIRE(document != nullptr);
    Frame frame;
    const float height = ImTypeset::GetDocumentSize(document).y;
    REQUIRE_EQ(height, 384.0f);
    // The document at height `offset`, drawn as the slice above `cut` and
    // the slice from it down.
    const auto sliced = [&](float offset, float cut) {
        ImVector<ImDrawVert> vertices;
        ImVector<ImDrawIdx> indices;
        const ImVec2 pos(0.0f, offset);
        ImTypeset::DrawDocumentToBuffers(document, pos, ImVec4(-100.0f, -50.0f, 4000.0f, cut),
                                         &vertices, &indices);
        ImTypeset::DrawDocumentToBuffers(document, pos, ImVec4(-100.0f, cut, 4000.0f, 1000.0f),
                                         &vertices, &indices);
        return vertices.Size;
    };
    const int whole = vertices_in(document, -50.0f, 1000.0f);
    REQUIRE(whole > 0);

    // The finding, under the reference's rule: a cut on the pixel row a
    // line's pen is on (the top of its font's cell, some rows below the
    // line's top) has that line emitted by the slice above as well, every
    // vertex of it under that slice's clip rectangle. The product's rule
    // emits it once. Every whole-pixel cut through the second line is tried,
    // so the test does not depend on where the face's ascent puts the pen.
    int worst_reference = 0;
    int worst_product = 0;
    for (int row = 32; row < 64; ++row) {
        const float cut = static_cast<float>(row);
        stage.ctx->draw_rules = DrawRules::reference();
        const int reference = sliced(0.0f, cut);
        stage.ctx->draw_rules = DrawRules::product();
        const int product = sliced(0.0f, cut);
        CHECK(product <= reference);
        worst_reference = std::max(worst_reference, reference - product);
        worst_product = std::max(worst_product, product - whole);
    }
    CHECK(worst_reference > 0);
    // A cut through the middle of a line cuts glyphs, and those are rightly
    // in both slices: never more than the line's own.
    CHECK(worst_product <= whole / 12);

    // The product's rule: slices that meet at a line's top, wherever the
    // document is on the pixel grid.
    stage.ctx->draw_rules = DrawRules::product();
    CHECK_EQ(vertices_in(document, -50.0f, 1000.0f), whole);
    for (int step = 0; step < 16; ++step) {
        const float offset = static_cast<float>(step);
        for (int line = 0; line <= 12; ++line) {
            CHECK_CONTEXT("offset %d, cut at the top of line %d", step, line);
            CHECK_EQ(sliced(offset, offset + 32.0f * static_cast<float>(line)), whole);
        }
    }
    ImTypeset::DestroyDocument(document);
}

// --- A null draw list, and where there is no current window ------------------------------

TEST(a_null_draw_list_is_refused_where_imgui_says_there_is_no_frame)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    REQUIRE(ImTypeset::SetParagraphText(p, "some words"));
    REQUIRE(ImTypeset::LayoutParagraph(p, 300.0f));
    ImTypesetDocument* document = code_document("x = 1\n", 0);
    REQUIRE(document != nullptr);
    const ImVec2 at(0.0f, 0.0f);
    // Before the first frame.
    CHECK(!ImTypeset::DrawParagraph(p, nullptr, at));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    CHECK(!ImTypeset::DrawDocument(document, nullptr, at));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    // Inside one: the current window's.
    ImGui::NewFrame();
    CHECK(ImTypeset::DrawParagraph(p, nullptr, at));
    CHECK(ImTypeset::DrawDocument(document, nullptr, at));
    // After Render(), until the next NewFrame().
    ImGui::Render();
    CHECK(!ImTypeset::DrawParagraph(p, nullptr, at));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    CHECK(!ImTypeset::DrawDocument(document, nullptr, at));
    CHECK(!ImTypeset::TextUnformatted("a widget"));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
    // NOT COVERED, because imgui.h cannot tell it from the inside of a frame:
    // after ImGui::EndFrame() with no ImGui::Render(). There is no current
    // window then, and a null draw list or a widget is ImGui's own null
    // window, as ImGui::GetWindowDrawList() is. See DrawParagraph in the
    // header and docs/decisions.md D56.
    ImTypeset::DestroyDocument(document);
    ImTypeset::DestroyParagraph(p);
}
