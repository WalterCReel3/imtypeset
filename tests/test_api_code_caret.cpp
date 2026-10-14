// The caret and the hit test at the end of a line of a Code block (imscribe's
// H27). A source line's terminator -- its "\n", or the "\r" of "\r\n", or a
// lone "\r" -- has no item of layout, so the end of a line and the start of
// the next were one index. The caret at a terminator's byte stands at the end
// of that source line's last visual line, a blank line's included, and a point
// past a line's last character answers its terminator's byte and the line
// under the point. Worked by hand: every x is a whole number of cells, and is
// compared with the caret before the same cell of a one-line document set
// alike, so the comparison is exact.
//
// The C entries pass these calls through unchanged; one check of each is here
// to hold them to that. The Python package converts bytes to characters and
// back around the same C calls.

#include "check.h"
#include "golden.h"

#include "imtypeset.h"
#include "imtypeset_c.h"

#include <string>
#include <vector>

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

// One code block of `text`, laid out at `width` px.
struct CodeDoc {
    CodeDoc(const std::string& source, int overflow, float width)
        : text(source)
    {
        ImTypesetBlock block;
        block.Kind = ImTypesetBlockKind_Code;
        block.TextStart = 0;
        block.TextEnd = static_cast<int>(text.size());
        ImTypesetDocumentContent content;
        content.Text = text.data();
        content.TextSize = static_cast<int>(text.size());
        content.Blocks = &block;
        content.BlockCount = 1;
        ImTypesetDocumentOptions options;
        options.CodeOverflow = overflow;
        document = ImTypeset::CreateDocument();
        if (document == nullptr)
            return;
        ImTypeset::SetDocumentOptions(document, &options);
        ImTypeset::SetDocumentBlocks(document, &content);
        ImTypeset::LayoutDocument(document, width);
    }
    ~CodeDoc()
    {
        if (document != nullptr)
            ImTypeset::DestroyDocument(document);
    }
    CodeDoc(const CodeDoc&) = delete;
    CodeDoc& operator=(const CodeDoc&) = delete;

    ImVec4 caret(int offset) const
    {
        return ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, offset});
    }

    std::string text;
    ImTypesetDocument* document = nullptr;
};

// The x of the caret before cell `k` of a line that starts at the block's
// left edge.
struct Cells {
    Cells()
        : row(std::string(40, 'm'), ImTypesetCodeOverflow_Grow, 2000.0f)
    {
    }
    float x(int k) const { return row.caret(k).x; }
    CodeDoc row;
};

struct Want {
    int offset;
    int cell;
    int line;
};

void check_carets(const CodeDoc& doc, const Cells& cells, const std::vector<Want>& wanted)
{
    for (const Want& w : wanted) {
        CHECK_CONTEXT("offset %d", w.offset);
        const ImVec4 rect = doc.caret(w.offset);
        const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(doc.document, 0, w.line);
        CHECK_EQ(rect.x, cells.x(w.cell));
        CHECK_EQ(rect.z, rect.x);
        CHECK_EQ(rect.y, line.Min.y);
        CHECK_EQ(rect.w, line.Max.y);
    }
}

// The middle of visual line `line` in y.
float middle(const CodeDoc& doc, int line)
{
    const ImTypesetLine box = ImTypeset::GetDocumentBlockLine(doc.document, 0, line);
    return (box.Min.y + box.Max.y) / 2.0f;
}

void check_hit(const CodeDoc& doc, float x, float y, int offset, int line)
{
    CHECK_CONTEXT("hit at (%g, %g)", static_cast<double>(x), static_cast<double>(y));
    const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(doc.document, ImVec2(x, y));
    CHECK_EQ(hit.Position.Block, 0);
    CHECK_EQ(hit.Position.Offset, offset);
    CHECK_EQ(hit.Line, line);
}

const int OVERFLOWS[] = {ImTypesetCodeOverflow_Grow, ImTypesetCodeOverflow_Wrap,
                         ImTypesetCodeOverflow_Clip};

} // namespace

TEST(the_caret_at_a_newline_stands_at_the_end_of_its_line)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    REQUIRE(cells.x(1) > cells.x(0));
    for (const int overflow : OVERFLOWS) {
        CHECK_CONTEXT("overflow %d", overflow);
        const CodeDoc doc("abcdef\nxy", overflow, 600.0f);
        REQUIRE(doc.document != nullptr);
        REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(doc.document, 0).LineCount, 2);
        check_carets(doc, cells,
                     {{0, 0, 0},
                      {1, 1, 0},
                      {5, 5, 0},
                      {6, 6, 0}, // the newline: after 'f', on the first line
                      {7, 0, 1},
                      {8, 1, 1},
                      {9, 2, 1},
                      {50, 2, 1}});
    }
}

TEST(the_caret_before_a_tab_that_opens_a_line_is_at_its_start_and_the_newline_at_the_end)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    // imscribe's case: cells 0, 1, 2, 0, 4, 5.
    for (const int overflow : OVERFLOWS) {
        CHECK_CONTEXT("overflow %d", overflow);
        const CodeDoc doc("ab\n\ty", overflow, 600.0f);
        REQUIRE(doc.document != nullptr);
        check_carets(doc, cells,
                     {{0, 0, 0}, {1, 1, 0}, {2, 2, 0}, {3, 0, 1}, {4, 4, 1}, {5, 5, 1}});
    }
}

TEST(a_blank_code_line_holds_a_caret_and_a_click_on_it_answers_it)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    for (const int overflow : OVERFLOWS) {
        CHECK_CONTEXT("overflow %d", overflow);
        const CodeDoc doc("a\n\nb", overflow, 600.0f);
        REQUIRE(doc.document != nullptr);
        REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(doc.document, 0).LineCount, 3);
        check_carets(doc, cells, {{0, 0, 0}, {1, 1, 0}, {2, 0, 1}, {3, 0, 2}, {4, 1, 2}});
        // A click anywhere on the blank line is its newline, on that line.
        check_hit(doc, cells.x(0) + 1.0f, middle(doc, 1), 2, 1);
        check_hit(doc, cells.x(10), middle(doc, 1), 2, 1);
        // Past the end of the first line: its newline. Past the last line's
        // end: the block's end, which has no newline in what is set.
        check_hit(doc, cells.x(10), middle(doc, 0), 1, 0);
        check_hit(doc, cells.x(10), middle(doc, 2), 4, 2);
        // On a character, as before.
        check_hit(doc, cells.x(0) + 1.0f, middle(doc, 0), 0, 0);
        check_hit(doc, cells.x(0) + 1.0f, middle(doc, 2), 3, 2);
    }
}

TEST(a_crlf_ends_a_line_at_its_cr_and_both_of_its_bytes_are_the_line_end)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    for (const int overflow : OVERFLOWS) {
        CHECK_CONTEXT("overflow %d", overflow);
        const CodeDoc doc("ab\r\ncd\r\rz", overflow, 600.0f);
        REQUIRE(doc.document != nullptr);
        REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(doc.document, 0).LineCount, 4);
        check_carets(doc, cells,
                     {{0, 0, 0},
                      {2, 2, 0},
                      {3, 2, 0},
                      {4, 0, 1},
                      {6, 2, 1}, // a lone CR ends a line too
                      {7, 0, 2}, // and a second makes a blank one
                      {8, 0, 3},
                      {9, 1, 3}});
        check_hit(doc, cells.x(10), middle(doc, 0), 2, 0);
        check_hit(doc, cells.x(10), middle(doc, 1), 6, 1);
        check_hit(doc, cells.x(10), middle(doc, 2), 7, 2);
    }
}

TEST(a_hit_past_a_lines_last_character_answers_its_newline_and_the_line_under_the_point)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    for (const int overflow : OVERFLOWS) {
        CHECK_CONTEXT("overflow %d", overflow);
        const CodeDoc doc("abcdef\nxy", overflow, 600.0f);
        REQUIRE(doc.document != nullptr);
        // Left of the middle of 'f' is before it; right of it, the line end.
        check_hit(doc, cells.x(5) + 1.0f, middle(doc, 0), 5, 0);
        check_hit(doc, cells.x(6) - 1.0f, middle(doc, 0), 6, 0);
        check_hit(doc, cells.x(30), middle(doc, 0), 6, 0);
        check_hit(doc, cells.x(0) + 1.0f, middle(doc, 1), 7, 1);
        check_hit(doc, cells.x(30), middle(doc, 1), 9, 1);
        // The caret there is where the point was taken to be.
        const ImTypesetDocumentHit hit =
            ImTypeset::HitTestDocument(doc.document, ImVec2(cells.x(30), middle(doc, 0)));
        const ImVec4 rect = doc.caret(hit.Position.Offset);
        CHECK(middle(doc, 0) > rect.y && middle(doc, 0) < rect.w);
        // A drag from the start past the first line's end selects the line
        // and its newline, and not the next line's first character.
        const ImTypesetDocumentRange range = ImTypeset::SelectDocumentRange(
            doc.document, ImVec2(cells.x(0), middle(doc, 0)), ImVec2(cells.x(30), middle(doc, 0)));
        CHECK_EQ(range.Start.Offset, 0);
        CHECK_EQ(range.End.Offset, 6);
    }
}

TEST(under_clip_the_line_end_is_the_texts_end_not_the_rooms)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    // Four cells of room; the first line is eight long and runs past it.
    const float room = cells.x(4) - cells.x(0);
    const CodeDoc doc("abcdefgh\nxy", ImTypesetCodeOverflow_Clip, room);
    REQUIRE(doc.document != nullptr);
    REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(doc.document, 0).LineCount, 2);
    check_carets(doc, cells, {{7, 7, 0}, {8, 8, 0}, {9, 0, 1}, {11, 2, 1}});
    // Past the drawn edge, on the clipped characters, and past their end.
    check_hit(doc, cells.x(6) + 1.0f, middle(doc, 0), 6, 0);
    check_hit(doc, cells.x(20), middle(doc, 0), 8, 0);
}

TEST(under_wrap_a_continuation_keeps_its_rule_and_the_newline_ends_the_last_piece)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    // Ten cells of room: "abcdefghij" and then "klmn", hung by the cells
    // nearest two ems (four), then "xy".
    const float room = cells.x(10) - cells.x(0);
    const CodeDoc doc("abcdefghijklmn\nxy", ImTypesetCodeOverflow_Wrap, room);
    REQUIRE(doc.document != nullptr);
    REQUIRE_EQ(ImTypeset::GetDocumentBlockGeometry(doc.document, 0).LineCount, 3);
    // The break between the pieces answers the start of the later piece, as
    // GetParagraphCaretRect says; the newline the end of the later piece.
    check_carets(doc, cells,
                 {{9, 9, 0}, {10, 4, 1}, {13, 7, 1}, {14, 8, 1}, {15, 0, 2}, {17, 2, 2}});
    check_hit(doc, cells.x(30), middle(doc, 1), 14, 1);
    check_hit(doc, cells.x(4) + 1.0f, middle(doc, 1), 10, 1);
    // Past the first piece's end is the break, which is the later piece's
    // start: no newline is there.
    CHECK_EQ(ImTypeset::HitTestDocument(doc.document, ImVec2(cells.x(30), middle(doc, 0)))
                 .Position.Offset,
             10);
}

TEST(range_rectangles_and_copies_of_code_are_as_they_were)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    const CodeDoc doc("abcdef\nxy", ImTypesetCodeOverflow_Grow, 600.0f);
    REQUIRE(doc.document != nullptr);
    // [0, 7) is the first line and its newline: one rectangle, over the six
    // characters.
    ImVector<ImVec4> rects;
    REQUIRE_EQ(ImTypeset::GetDocumentRangeRects(doc.document, {{0, 0}, {0, 7}}, &rects), 1);
    CHECK_EQ(rects[0].x, cells.x(0));
    CHECK_EQ(rects[0].z, cells.x(6));
    rects.resize(0);
    REQUIRE_EQ(ImTypeset::GetDocumentRangeRects(doc.document, {{0, 0}, {0, 8}}, &rects), 2);
    CHECK_EQ(rects[1].x, cells.x(0));
    CHECK_EQ(rects[1].z, cells.x(1));
    const auto copy = [&](int start, int end) {
        ImGuiTextBuffer out;
        ImTypeset::AppendDocumentRangeText(doc.document, {{0, start}, {0, end}}, &out);
        return std::string(out.c_str());
    };
    CHECK_EQ(copy(0, 9), std::string("abcdef\nxy"));
    CHECK_EQ(copy(0, 7), std::string("abcdef\n"));
    CHECK_EQ(copy(6, 7), std::string("\n"));
    CHECK_EQ(copy(3, 8), std::string("def\nx"));
    const CodeDoc blank("a\n\nb", ImTypesetCodeOverflow_Grow, 600.0f);
    ImGuiTextBuffer out;
    ImTypeset::AppendDocumentRangeText(blank.document, {{0, 0}, {0, 4}}, &out);
    CHECK_EQ(std::string(out.c_str()), std::string("a\n\nb"));
}

TEST(the_c_entries_answer_what_the_cpp_ones_do)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Cells cells;
    const CodeDoc doc("ab\n\ty", ImTypesetCodeOverflow_Grow, 600.0f);
    REQUIRE(doc.document != nullptr);
    ImTypeset_Document* const handle = doc.document;
    const ImTypeset_Position at = {0, 2};
    ImTypeset_Vec4 rect = {0.0f, 0.0f, 0.0f, 0.0f};
    REQUIRE(ImTypeset_GetDocumentCaretRect(handle, &at, &rect) != 0);
    CHECK_EQ(rect.x, cells.x(2));
    CHECK_EQ(rect.y, doc.caret(2).y);
    const ImTypeset_Vec2 point = {cells.x(30), middle(doc, 0)};
    ImTypeset_DocumentHit hit;
    REQUIRE(ImTypeset_HitTestDocument(handle, &point, &hit) != 0);
    CHECK_EQ(hit.Position.Offset, 2);
    CHECK_EQ(hit.Line, 0);
}
