// What include/imtypeset.h says in a sentence and nothing else held: blank
// lines under ImTypesetTextFlags_HardBreaks, a table with no cells, the range a
// Scale is taken in, the white space a paragraph collapses, and what Dear
// ImGui does with a role's font that the host removes while it is the default.
// None of these has an oracle: each is worked by hand from the code it names.

#include "check.h"
#include "golden.h"

#include "imtypeset.h"

#include "unicode_tables.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{

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
        const std::string path = golden::data_path("fonts/Roboto-Regular.ttf");
        regular = ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, path.c_str());
        if (regular == nullptr)
            FAIL("Roboto-Regular.ttf: %s", ImTypeset::GetLastError()->Message);
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
    ImFont* regular = nullptr;
};

struct Paragraph {
    Paragraph()
        : p(ImTypeset::CreateParagraph())
    {
    }
    ~Paragraph() { ImTypeset::DestroyParagraph(p); }
    Paragraph(const Paragraph&) = delete;
    Paragraph& operator=(const Paragraph&) = delete;

    bool set(const std::string& text, ImTypesetTextFlags flags = 0,
             const ImTypesetRun* runs = nullptr, int runs_count = 0, float width = 1.0e6f)
    {
        return ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), runs,
                                           runs_count, flags) &&
               ImTypeset::LayoutParagraph(p, width);
    }
    std::string copy() const
    {
        ImGuiTextBuffer buffer;
        ImTypesetTextRange all;
        all.Start = 0;
        all.End = 1 << 20;
        ImTypeset::AppendParagraphRangeText(p, all, &buffer);
        return std::string(buffer.c_str());
    }
    // The natural width of the first line: where its content starts and ends.
    float natural() const
    {
        const ImTypesetLine line = ImTypeset::GetParagraphLine(p, 0);
        return line.Max.x - line.Min.x;
    }

    ImTypesetParagraph* p;
};

std::string utf8(uint32_t c)
{
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

// Python's str.isspace() set, by range, as the header lists it.
const uint32_t WHITE_SPACE[] = {
    0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x001C, 0x001D, 0x001E, 0x001F, 0x0020,
    0x0085, 0x00A0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006,
    0x2007, 0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000,
};

} // namespace

// --- H41: blank lines under HardBreaks ---------------------------------------------------------

// Each newline is a forced break, so a line with nothing on it is SET: "a\n\nb"
// is three lines, as tall as "a\nb\nc". A copy gives one newline for the two.
TEST(each_newline_under_hard_breaks_is_a_line_and_a_copy_gives_one)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Paragraph two;
    Paragraph blank;
    Paragraph three;
    REQUIRE(two.set("a\nb", ImTypesetTextFlags_HardBreaks));
    REQUIRE(blank.set("a\n\nb", ImTypesetTextFlags_HardBreaks));
    REQUIRE(three.set("a\nb\nc", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(two.p), 2);
    CHECK_EQ(ImTypeset::GetParagraphLineCount(blank.p), 3);
    CHECK_EQ(ImTypeset::GetParagraphSize(blank.p).y, ImTypeset::GetParagraphSize(three.p).y);
    CHECK(ImTypeset::GetParagraphSize(blank.p).y > ImTypeset::GetParagraphSize(two.p).y);
    // The copy: one newline where two were written.
    CHECK_EQ(two.copy(), std::string("a\nb"));
    CHECK_EQ(blank.copy(), std::string("a\nb"));

    // Three in a row are two blank lines; one that holds only white space is
    // blank too; "\r\n" is one newline and a lone "\r" is one.
    Paragraph more;
    REQUIRE(more.set("a\n\n\nb", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 4);
    CHECK_EQ(more.copy(), std::string("a\nb"));
    REQUIRE(more.set("a\n \t \nb", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 3);
    REQUIRE(more.set("a\r\n\r\nb", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 3);
    REQUIRE(more.set("a\r\rb", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 3);
    // Newlines at either end are white space at the end, dropped.
    REQUIRE(more.set("\n\na\nb\n\n", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 2);
    // Without the flag every newline is white space: one line.
    REQUIRE(more.set("a\n\nb"));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 1);
    CHECK_EQ(more.copy(), std::string("a b"));
    // U+2028 and the other white space that is not "\n" or "\r" do not break.
    REQUIRE(more.set("a" + utf8(0x2028) + "b" + utf8(0x0B) + "c", ImTypesetTextFlags_HardBreaks));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(more.p), 1);

    // A document's text block with ImTypesetBlockFlags_HardBreaks: the same.
    const std::string text = "a\n\nb";
    ImTypesetBlock block;
    block.Flags = ImTypesetBlockFlags_HardBreaks;
    block.TextEnd = static_cast<int>(text.size());
    ImTypesetDocumentContent content;
    content.Text = text.data();
    content.TextSize = block.TextEnd;
    content.Blocks = &block;
    content.BlockCount = 1;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    REQUIRE(ImTypeset::SetDocumentBlocks(document, &content));
    REQUIRE(ImTypeset::LayoutDocument(document, 600.0f));
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount, 3);
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange range = {ImTypesetPosition{0, 0},
                                          ImTypesetPosition{0, block.TextEnd}};
    ImTypeset::AppendDocumentRangeText(document, range, &buffer);
    CHECK_EQ(std::string(buffer.c_str()), std::string("a\nb"));
    ImTypeset::DestroyDocument(document);
}

// --- H38: a table with no cells ----------------------------------------------------------------

// No rows, no columns and no height; not an error; nothing drawn; the blocks
// around it are where they would be without it, but for the space it asks
// for above and below.
TEST(a_table_with_no_cells_has_no_rows_and_no_height)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "Above.Below.";
    for (const ImTypesetBlockFlags flags : {0, static_cast<int>(ImTypesetBlockFlags_TableHeader)}) {
        CHECK_CONTEXT("flags %d", flags);
        ImTypesetBlock blocks[3];
        blocks[0].TextEnd = 6;
        blocks[1].Kind = ImTypesetBlockKind_Table;
        blocks[1].Flags = flags;
        blocks[1].TextStart = 6;
        blocks[1].TextEnd = 6;
        blocks[2].TextStart = 6;
        blocks[2].TextEnd = 12;
        ImTypesetDocumentContent content;
        content.Text = text.data();
        content.TextSize = static_cast<int>(text.size());
        content.Blocks = blocks;
        content.BlockCount = 3;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        CHECK(ImTypeset::SetDocumentBlocks(document, &content));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));
        CHECK(ImTypeset::LayoutDocument(document, 600.0f));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_None));

        const ImTypesetTableGeometry table = ImTypeset::GetDocumentTableGeometry(document, 1);
        CHECK_EQ(table.RowCount, 0);
        CHECK_EQ(table.ColumnCount, 0);
        CHECK_EQ(table.CellCount, 0);
        CHECK_EQ(table.Max.y - table.Min.y, 0.0f);
        const ImTypesetBlockGeometry block = ImTypeset::GetDocumentBlockGeometry(document, 1);
        CHECK_EQ(block.FullMax.y - block.FullMin.y, 0.0f);
        CHECK_EQ(block.LineCount, 0);

        // The two paragraphs are as far apart as with nothing between them.
        ImTypesetBlock alone[2] = {blocks[0], blocks[2]};
        ImTypesetDocumentContent without = content;
        without.Blocks = alone;
        without.BlockCount = 2;
        ImTypesetDocument* plain = ImTypeset::CreateDocument();
        REQUIRE(plain != nullptr);
        REQUIRE(ImTypeset::SetDocumentBlocks(plain, &without));
        REQUIRE(ImTypeset::LayoutDocument(plain, 600.0f));
        CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 2).Min.y,
                 ImTypeset::GetDocumentBlockGeometry(plain, 1).Min.y);
        CHECK_EQ(ImTypeset::GetDocumentSize(document).y, ImTypeset::GetDocumentSize(plain).y);

        // Nothing of it is drawn: the vertices are the two paragraphs'.
        ImVector<ImDrawVert> with_table;
        ImVector<ImDrawVert> without_table;
        ImVector<ImDrawIdx> indices;
        const ImVec4 clip(-100.0f, -100.0f, 4000.0f, 4000.0f);
        ImGui::NewFrame();
        CHECK(ImTypeset::DrawDocumentToBuffers(document, ImVec2(0.0f, 0.0f), clip, &with_table,
                                               &indices));
        CHECK(ImTypeset::DrawDocumentToBuffers(plain, ImVec2(0.0f, 0.0f), clip, &without_table,
                                               &indices));
        ImGui::Render();
        CHECK_EQ(with_table.Size, without_table.Size);
        ImTypeset::DestroyDocument(plain);
        ImTypeset::DestroyDocument(document);
    }
}

// --- H37: the range of a Scale -----------------------------------------------------------------

// A run's Scale and a block style's: zero, below zero and not finite are 1;
// the rest is clamped to 0.1..10.
TEST(a_scale_that_is_not_finite_is_one_and_the_rest_is_clamped_to_a_tenth_and_ten)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "Hamburgefonstiv";
    const auto width_at = [&](float scale) {
        ImTypesetRun run;
        run.Start = 0;
        run.End = static_cast<int>(text.size());
        run.Scale = scale;
        Paragraph p;
        if (!p.set(text, 0, &run, 1))
            FAIL("scale %g: %s", static_cast<double>(scale), ImTypeset::GetLastError()->Message);
        return p.natural();
    };
    const float one = width_at(1.0f);
    const float tenth = width_at(0.1f);
    const float ten = width_at(10.0f);
    CHECK(tenth < one && one < ten);
    CHECK_EQ(width_at(0.0f), one);
    CHECK_EQ(width_at(-3.0f), one);
    CHECK_EQ(width_at(std::numeric_limits<float>::quiet_NaN()), one);
    CHECK_EQ(width_at(std::numeric_limits<float>::infinity()), one);
    CHECK_EQ(width_at(-std::numeric_limits<float>::infinity()), one);
    CHECK_EQ(width_at(1.0e-20f), tenth);
    CHECK_EQ(width_at(0.05f), tenth);
    CHECK_EQ(width_at(11.0f), ten);
    CHECK_EQ(width_at(3.0e38f), ten);
    CHECK(width_at(2.0f) > one && width_at(2.0f) < ten);

    // A block style's Scale, the same.
    const auto block_width_at = [&](float scale) {
        ImTypesetBlock block;
        block.TextEnd = static_cast<int>(text.size());
        ImTypesetBlockStyle style;
        style.Scale = scale;
        style.Align = ImTypesetAlign_Left;
        ImTypesetDocumentContent content;
        content.Text = text.data();
        content.TextSize = block.TextEnd;
        content.Blocks = &block;
        content.BlockCount = 1;
        content.Styles = &style;
        content.StyleCount = 1;
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        float width = -1.0f;
        if (document != nullptr && ImTypeset::SetDocumentBlocks(document, &content) &&
            ImTypeset::LayoutDocument(document, 1.0e5f)) {
            const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(document, 0, 0);
            width = line.Max.x - line.Min.x;
        }
        ImTypeset::DestroyDocument(document);
        return width;
    };
    const float block_one = block_width_at(1.0f);
    CHECK(block_one > 0.0f);
    CHECK_EQ(block_width_at(0.0f), block_one);
    CHECK_EQ(block_width_at(-1.0f), block_one);
    CHECK_EQ(block_width_at(std::numeric_limits<float>::quiet_NaN()), block_one);
    CHECK_EQ(block_width_at(std::numeric_limits<float>::infinity()), block_one);
    CHECK_EQ(block_width_at(0.01f), block_width_at(0.1f));
    CHECK_EQ(block_width_at(50.0f), block_width_at(10.0f));
}

// --- H42: what white space is ------------------------------------------------------------------

// Python's str.isspace(): 29 characters. Each, between two words, is one
// interword space, the same as " "; a run of several of them is one; and a
// no-break space is among them, so it breaks a line and collapses.
TEST(white_space_is_pythons_isspace_and_a_no_break_space_breaks)
{
    // The table the library reads is the set the header lists, and no more.
    int count = 0;
    for (uint32_t c = 0; c <= 0x10FFFF; ++c) {
        if (ImTypeset::detail::py_isspace(c))
            ++count;
    }
    CHECK_EQ(count, 29);
    for (const uint32_t c : WHITE_SPACE) {
        CHECK_CONTEXT("U+%04X", c);
        CHECK(ImTypeset::detail::py_isspace(c));
    }

    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Paragraph reference;
    REQUIRE(reference.set("word word"));
    const float spaced = reference.natural();
    for (const uint32_t c : WHITE_SPACE) {
        CHECK_CONTEXT("U+%04X", c);
        Paragraph p;
        REQUIRE(p.set("word" + utf8(c) + "word"));
        CHECK_EQ(p.natural(), spaced);
        REQUIRE(p.set("word" + utf8(c) + utf8(0x20) + utf8(c) + "word"));
        CHECK_EQ(p.natural(), spaced);
        CHECK_EQ(p.copy(), std::string("word word"));
    }
    // Not white space: a zero-width space and a word joiner are characters.
    for (const uint32_t c : {0x200Bu, 0x2060u, 0xFEFFu}) {
        CHECK_CONTEXT("U+%04X", c);
        CHECK(!ImTypeset::detail::py_isspace(c));
    }
    // A no-break space is a breakpoint: two words joined by one break apart
    // on a line too narrow for both, as with a space.
    Paragraph narrow;
    REQUIRE(narrow.set("word" + utf8(0x00A0) + "word", 0, nullptr, 0, spaced * 0.75f));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(narrow.p), 2);
    REQUIRE(narrow.set("word" + utf8(0x202F) + "word", 0, nullptr, 0, spaced * 0.75f));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(narrow.p), 2);
    REQUIRE(narrow.set("word" + utf8(0x2007) + "word", 0, nullptr, 0, spaced * 0.75f));
    CHECK_EQ(ImTypeset::GetParagraphLineCount(narrow.p), 2);
    // What keeps two words together is a NoBreak run over them.
    const std::string tied = "word" + utf8(0x00A0) + "word";
    ImTypesetRun run;
    run.Start = 0;
    run.End = static_cast<int>(tied.size());
    run.Flags = ImTypesetRunFlags_NoBreak;
    narrow.set(tied, 0, &run, 1, spaced * 0.75f); // overfull: false is allowed
    CHECK_EQ(ImTypeset::GetParagraphLineCount(narrow.p), 1);
}

// --- H25: a role's font removed while it is the default ----------------------------------------

namespace
{

// Runs a frame that draws a line of ImGui's own text, and answers how many
// vertices the frame drew.
int a_frame_of_imgui_text()
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
    ImGui::Begin("w");
    ImGui::TextUnformatted("still drawn");
    ImGui::End();
    ImGui::Render();
    const ImDrawData* const data = ImGui::GetDrawData();
    return data != nullptr ? data->TotalVtxCount : 0;
}

} // namespace

// Read in Dear ImGui 1.92.9 (imgui_draw.cpp, ImFontAtlas::RemoveFont and
// ImFontAtlasBuildNotifySetFont): removing a font puts the atlas's first
// remaining font wherever a context of the atlas holds it: io.FontDefault, the
// current font, the font stack. Shown here between frames, for the default
// and the current font, by both routes: the host's ImFontAtlas::RemoveFont,
// and ImTypeset::RemoveFont, which also clears io.FontDefault (null is "the
// atlas's first font" to ImGui).
TEST(a_roles_font_removed_while_it_is_the_default_is_replaced)
{
    for (const bool by_the_host : {true, false}) {
        CHECK_CONTEXT(by_the_host ? "ImFontAtlas::RemoveFont" : "ImTypeset::RemoveFont");
        Stage stage;
        REQUIRE(stage.ctx != nullptr);
        REQUIRE(stage.regular != nullptr);
        ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
        ImFont* const hosts =
            atlas->AddFontFromFileTTF(golden::data_path("fonts/Roboto-Bold.ttf").c_str());
        REQUIRE(hosts != nullptr);
        REQUIRE(atlas->Fonts.Size == 2);
        REQUIRE(atlas->Fonts[0] == stage.regular);
        ImGui::GetIO().FontDefault = stage.regular;
        CHECK(a_frame_of_imgui_text() > 0);
        // Between frames, the role's font is the default and the current one.
        REQUIRE(ImGui::GetFont() == stage.regular);
        if (by_the_host) {
            atlas->RemoveFont(ImTypeset::GetFont(ImTypesetFace_Regular));
            CHECK(ImGui::GetIO().FontDefault == hosts);
        } else {
            REQUIRE(ImTypeset::RemoveFont(ImTypesetFace_Regular));
            CHECK(ImGui::GetIO().FontDefault == nullptr);
        }
        CHECK_EQ(atlas->Fonts.Size, 1);
        CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == nullptr);
        CHECK(ImGui::GetFont() == hosts);
        // A frame runs and draws with what took its place.
        CHECK(a_frame_of_imgui_text() > 0);
        CHECK(ImGui::GetFont() == hosts);
    }
}
