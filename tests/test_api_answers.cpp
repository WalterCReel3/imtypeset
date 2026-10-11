// Answers to what a host met at the edges of the Document, each pinned: what a
// copy holds around a block that gives no text, what a mark over part of a
// formula draws, what a code block's text is when it ends in blank lines, what
// happens to a context whose ImGui context was destroyed under it, and how a
// host draws its own text in a role's font instead of adding the file twice.
//
// Everything goes in through the public header. src/context.h is included for
// the one test that looks at which ImFont a role holds.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

// ImGui's allocations, with two things a test can ask of them: how many bytes
// are held now, and that one block, once freed, be handed out again for the
// next request of its size -- which is how an ImGui context comes back at the
// address of one that was destroyed, here on purpose and under any allocator.
std::map<void*, size_t>* g_blocks = nullptr;
size_t g_bytes_held = 0;
struct Recycled {
    void* block;
    size_t size;
    bool free;
    bool armed;
};
std::vector<Recycled>* g_recycled = nullptr;

void* tracked_alloc(size_t size, void*)
{
    if (g_recycled != nullptr) {
        for (Recycled& kept : *g_recycled) {
            if (kept.free && kept.armed && kept.size == size) {
                kept.free = false;
                return kept.block;
            }
        }
    }
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory != nullptr && g_blocks != nullptr) {
        (*g_blocks)[memory] = size;
        g_bytes_held += size;
    }
    return memory;
}

void tracked_free(void* memory, void*)
{
    if (memory == nullptr)
        return;
    if (g_recycled != nullptr) {
        for (Recycled& kept : *g_recycled) {
            if (kept.block == memory) {
                kept.free = true;
                return;
            }
        }
    }
    if (g_blocks != nullptr) {
        const auto found = g_blocks->find(memory);
        if (found != g_blocks->end()) {
            g_bytes_held -= found->second;
            g_blocks->erase(found);
        }
    }
    std::free(memory);
}

struct Tracking {
    Tracking()
    {
        g_blocks = &blocks;
        g_recycled = &recycled;
        g_bytes_held = 0;
        ImGui::SetAllocatorFunctions(&tracked_alloc, &tracked_free, nullptr);
    }
    ~Tracking()
    {
        g_blocks = nullptr;
        g_recycled = nullptr;
        for (const Recycled& kept : recycled) {
            if (kept.free)
                std::free(kept.block);
        }
    }
    Tracking(const Tracking&) = delete;
    Tracking& operator=(const Tracking&) = delete;

    // From now on `block`, when ImGui frees it, is kept and handed out again
    // for the next request of its size.
    // Not before `arm(block)` if `armed` is false: for a block whose size
    // other requests share.
    void recycle(void* block, bool armed = true)
    {
        const auto found = blocks.find(block);
        if (found == blocks.end()) {
            FAIL("recycle: not a block ImGui allocated");
            return;
        }
        recycled.push_back(Recycled{block, found->second, false, armed});
    }
    void arm(void* block)
    {
        for (Recycled& kept : recycled) {
            if (kept.block == block)
                kept.armed = true;
        }
    }
    std::map<void*, size_t> blocks;
    std::vector<Recycled> recycled;
};

const struct {
    ImTypesetFace face;
    const char* file;
} FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_CodeBold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

std::string font_path(const char* file)
{
    return golden::data_path(std::string("fonts/") + file);
}

void new_imgui_context()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

bool add_fonts()
{
    bool all = true;
    for (const auto& entry : FONTS) {
        if (ImTypeset::AddFontFromFileTTF(entry.face, font_path(entry.file).c_str()) == nullptr) {
            FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
            all = false;
        }
    }
    return all;
}

struct Stage {
    Stage()
    {
        new_imgui_context();
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        add_fonts();
        ImTypeset::GetStyle().EmSize = 16.0f;
        ImTypeset::GetStyle().Color = IM_COL32(0x11, 0x22, 0x33, 0xFF);
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    Tracking tracking;
    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

struct Content {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetRun> runs;
    std::vector<ImTypesetBlockStyle> styles;

    ImTypesetBlock& add(ImTypesetBlockKind kind, const std::string& body)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        block.RunStart = static_cast<int>(runs.size());
        blocks.push_back(block);
        return blocks.back();
    }
    ImTypesetRun& run(int start, int end)
    {
        ImTypesetRun r;
        r.Start = start;
        r.End = end;
        runs.push_back(r);
        ++blocks.back().RunCount;
        return runs.back();
    }
    ImTypesetDocumentContent view() const
    {
        ImTypesetDocumentContent out;
        out.Text = text.data();
        out.TextSize = static_cast<int>(text.size());
        out.Blocks = blocks.data();
        out.BlockCount = static_cast<int>(blocks.size());
        out.Runs = runs.data();
        out.RunCount = static_cast<int>(runs.size());
        out.Styles = styles.data();
        out.StyleCount = static_cast<int>(styles.size());
        return out;
    }
};

ImTypesetDocument* document_of(const Content& content, float width = 400.0f)
{
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return nullptr;
    const ImTypesetDocumentContent view = content.view();
    ImTypeset::SetDocumentBlocks(document, &view);
    ImTypeset::LayoutDocument(document, width);
    return document;
}

std::string copied(ImTypesetDocument* document, ImTypesetPosition start, ImTypesetPosition end,
                   const char* separator = nullptr)
{
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange range = {start, end};
    const int appended = ImTypeset::AppendDocumentRangeText(document, range, &buffer, separator);
    CHECK_EQ(appended, buffer.size());
    return std::string(buffer.c_str());
}

struct Buffers {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;

    int with_colour(ImU32 colour) const
    {
        int count = 0;
        for (const ImDrawVert& v : vertices)
            count += v.col == colour ? 1 : 0;
        return count;
    }
    bool operator==(const Buffers& other) const
    {
        return vertices.Size == other.vertices.Size && indices.Size == other.indices.Size &&
               (vertices.Size == 0 ||
                std::memcmp(vertices.Data, other.vertices.Data,
                            static_cast<size_t>(vertices.Size) * sizeof(ImDrawVert)) == 0) &&
               (indices.Size == 0 ||
                std::memcmp(indices.Data, other.indices.Data,
                            static_cast<size_t>(indices.Size) * sizeof(ImDrawIdx)) == 0);
    }
};

bool draw_all(ImTypesetDocument* document, Buffers& into,
              const ImTypesetDocumentDraw* draw = nullptr)
{
    into.vertices.resize(0);
    into.indices.resize(0);
    return ImTypeset::DrawDocumentToBuffers(document, ImVec2(10.0f, 20.0f),
                                            ImVec4(0.0f, 0.0f, 4000.0f, 4000.0f), &into.vertices,
                                            &into.indices, draw);
}

} // namespace

// --- (a) A copy around a block that gives no text ----------------------------------------

TEST(a_copy_has_one_separator_between_two_texts_and_none_beside_a_block_that_gives_nothing)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;

    Content content;
    content.add(ImTypesetBlockKind_Paragraph, "one");                // 0
    content.add(ImTypesetBlockKind_Rule, "");                        // 1
    content.add(ImTypesetBlockKind_Host, "").Extent = 20.0f;         // 2: a widget with no text
    content.add(ImTypesetBlockKind_Paragraph, "");                   // 3
    content.add(ImTypesetBlockKind_Code, "");                        // 4
    content.add(ImTypesetBlockKind_Code, "\n\n");                    // 5: nothing but blank lines
    content.add(ImTypesetBlockKind_Rule, "a rule's text is not it"); // 6
    content.add(ImTypesetBlockKind_Paragraph, "two");                // 7
    content.add(ImTypesetBlockKind_Host, "[w]").Extent = 20.0f;      // 8
    content.add(ImTypesetBlockKind_Rule, "");                        // 9
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);

    const ImTypesetPosition top = {0, 0};
    const ImTypesetPosition bottom = {9, 0};
    // Six blocks between "one" and "two" give nothing: one separator, not seven.
    CHECK_EQ(copied(document, top, bottom, " | "), std::string("one | two | [w]"));
    CHECK_EQ(copied(document, top, bottom), std::string("one\n\ntwo\n\n[w]"));
    CHECK_EQ(copied(document, top, bottom, ""), std::string("onetwo[w]"));
    // A range that ends, or starts, in such a block has no separator at that end.
    CHECK_EQ(copied(document, top, ImTypesetPosition{1, 0}, " | "), std::string("one"));
    CHECK_EQ(copied(document, top, ImTypesetPosition{6, 5}, " | "), std::string("one"));
    CHECK_EQ(copied(document, ImTypesetPosition{1, 0}, ImTypesetPosition{7, 3}, " | "),
             std::string("two"));
    CHECK_EQ(copied(document, ImTypesetPosition{2, 0}, ImTypesetPosition{6, 0}, " | "),
             std::string());
    // Nor does a text block the range only touches the edge of give one.
    CHECK_EQ(copied(document, ImTypesetPosition{0, 3}, ImTypesetPosition{7, 0}, " | "),
             std::string());
    CHECK_EQ(copied(document, ImTypesetPosition{0, 3}, ImTypesetPosition{7, 1}, " | "),
             std::string("t"));
    CHECK_EQ(copied(document, ImTypesetPosition{0, 2}, ImTypesetPosition{7, 0}, " | "),
             std::string("e"));
    // A host block that has text gives it whole, behind a separator, and one
    // without text gives neither.
    CHECK_EQ(copied(document, ImTypesetPosition{7, 1}, ImTypesetPosition{9, 0}, " | "),
             std::string("wo | [w]"));

    // The buffer's own contents are not looked at: the separator is between
    // blocks of THIS call (AppendParagraphRangeText is the one that looks).
    ImGuiTextBuffer buffer;
    buffer.append("held");
    const ImTypesetDocumentRange all = {top, bottom};
    CHECK_EQ(ImTypeset::AppendDocumentRangeText(document, all, &buffer, " | "), 15);
    CHECK_EQ(std::string(buffer.c_str()), std::string("heldone | two | [w]"));

    ImTypeset::DestroyDocument(document);
}

// --- (b) A mark over part of a formula ---------------------------------------------------

TEST(a_mark_takes_a_formula_whole_when_it_holds_its_first_byte_and_not_at_all_otherwise)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;

    // The same sentence twice: the formula as a run, and between dollars.
    // `first` and `end` are the formula's bytes in each, delimiters included.
    struct Case {
        const char* name;
        const char* text;
        bool as_run;
        int first;
        int end;
    };
    const Case cases[] = {
        {"run", "ab x^2+1 cd", true, 3, 8},
        {"dollars", "ab $x^2+1$ cd", false, 3, 10},
    };
    const ImU32 red = IM_COL32(255, 0, 0, 255);
    for (const Case& c : cases) {
        CHECK_CONTEXT("%s", c.name);
        Content content;
        ImTypesetBlockStyle left;
        left.Align = ImTypesetAlign_Left;
        content.styles.push_back(left);
        ImTypesetBlock& block = content.add(ImTypesetBlockKind_Paragraph, c.text);
        if (c.as_run)
            content.run(c.first, c.end).Flags = ImTypesetRunFlags_Formula;
        else
            block.Flags = ImTypesetBlockFlags_Math;
        ImTypesetDocument* document = document_of(content);
        REQUIRE(document != nullptr);
        CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 1000.0f));
        CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 0)->Code,
                 static_cast<int>(ImTypesetError_None));
        const int size = static_cast<int>(std::strlen(c.text));

        Buffers plain;
        CHECK(draw_all(document, plain));
        const auto marked = [&](int from, int to, ImTypesetMarkPlace place, Buffers& into) {
            ImTypesetMark mark;
            mark.Range = ImTypesetDocumentRange{{0, from}, {0, to}};
            mark.Color = red;
            mark.Place = place;
            ImTypesetDocumentDraw draw;
            draw.Marks = &mark;
            draw.MarkCount = 1;
            CHECK(draw_all(document, into, &draw));
        };
        const auto rects = [&](int from, int to) {
            ImVector<ImVec4> out;
            const ImTypesetDocumentRange range = {{0, from}, {0, to}};
            ImTypeset::GetDocumentRangeRects(document, range, &out);
            std::vector<float> flat;
            for (const ImVec4& r : out) {
                flat.push_back(r.x);
                flat.push_back(r.y);
                flat.push_back(r.z);
                flat.push_back(r.w);
            }
            return flat;
        };

        for (const ImTypesetMarkPlace place :
             {ImTypesetMarkPlace_Ink, ImTypesetMarkPlace_Behind, ImTypesetMarkPlace_Under}) {
            CHECK_CONTEXT("place %d", static_cast<int>(place));
            Buffers whole;
            Buffers got;
            marked(c.first, c.end, place, whole);
            CHECK(!(whole == plain));
            CHECK(whole.with_colour(red) >= 4);

            // Strictly inside the formula: nothing is marked.
            marked(c.first + 1, c.end - 1, place, got);
            CHECK(got == plain);
            marked(c.first + 1, c.end, place, got);
            CHECK(got == plain);
            // Its first byte alone: all of it.
            marked(c.first, c.first + 1, place, got);
            CHECK(got == whole);
            // From the text before it to somewhere inside it: the text and all of it.
            Buffers reference;
            marked(1, c.end, place, reference);
            marked(1, c.first + 2, place, got);
            CHECK(got == reference);
            // From inside it to the text after it: the text after it only.
            marked(c.end, size, place, reference);
            marked(c.first + 2, size, place, got);
            CHECK(got == reference);
            CHECK(!(got == plain));
        }
        // A selection's rectangles and a copy follow the same rule.
        CHECK(rects(c.first + 1, c.end - 1).empty());
        CHECK(rects(c.first, c.first + 1) == rects(c.first, c.end));
        CHECK(!rects(c.first, c.end).empty());
        CHECK_EQ(
            copied(document, ImTypesetPosition{0, c.first + 1}, ImTypesetPosition{0, c.end - 1}),
            std::string());
        CHECK_EQ(copied(document, ImTypesetPosition{0, c.first}, ImTypesetPosition{0, c.first + 1}),
                 std::string(c.text + c.first, static_cast<size_t>(c.end - c.first)));
        ImTypeset::DestroyDocument(document);
    }
}

// --- (d) A code block that ends in blank lines -------------------------------------------

TEST(a_code_blocks_text_ends_at_its_last_character_and_a_copy_and_its_positions_say_so)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;

    struct Case {
        const char* given;
        const char* held; // What the block's text is: `given` up to its last character.
        int lines;
    };
    const Case cases[] = {
        {"a\nb", "a\nb", 2},
        {"a\nb\n", "a\nb", 2},
        {"a\nb\n\n\n", "a\nb", 2},
        {"a\r\nb\r\n\r\n", "a\r\nb", 2},
        // A line of spaces is not blank, and blank lines elsewhere are kept.
        {"a\n  \n", "a\n  ", 2},
        {"\n\na\n\nb\n\n", "\n\na\n\nb", 5},
        {"a\n\t\n\n", "a\n\t", 2},
        {"\n\n\n", "", 0},
        {"", "", 0},
    };
    for (const Case& c : cases) {
        CHECK_CONTEXT("given %d bytes, held %d", static_cast<int>(std::strlen(c.given)),
                      static_cast<int>(std::strlen(c.held)));
        Content content;
        content.add(ImTypesetBlockKind_Paragraph, "above");
        content.add(ImTypesetBlockKind_Code, c.given);
        content.add(ImTypesetBlockKind_Paragraph, "below");
        ImTypesetDocument* document = document_of(content);
        REQUIRE(document != nullptr);
        CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 10000.0f));
        const std::string held(c.held);
        const int size = static_cast<int>(held.size());
        const int given = static_cast<int>(std::strlen(c.given));

        // The bytes the block holds are all that were handed in: an append
        // goes on from there. Positions end before the dropped line ends.
        CHECK_EQ(ImTypeset::GetDocumentBlockTextSize(document, 1), given);
        CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 1).LineCount, c.lines);
        // A range to the end of what was GIVEN copies what is HELD.
        CHECK_EQ(copied(document, ImTypesetPosition{1, 0}, ImTypesetPosition{1, given}), held);
        CHECK_EQ(copied(document, ImTypesetPosition{1, 0}, ImTypesetPosition{1, 1 << 20}), held);
        // Across blocks the dropped lines do not come back as text either.
        const std::string across =
            copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{2, 5}, "|");
        CHECK_EQ(across, held.empty() ? std::string("above|below") : "above|" + held + "|below");
        // No position the library answers is past the held text.
        const ImTypesetBlockGeometry where = ImTypeset::GetDocumentBlockGeometry(document, 1);
        for (float y = where.FullMin.y - 2.0f; y <= where.FullMax.y + 2.0f; y += 1.0f) {
            for (const float x : {-5.0f, 3.0f, 200.0f, 5000.0f}) {
                const ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(document, ImVec2(x, y));
                if (hit.Position.Block == 1)
                    CHECK(hit.Position.Offset >= 0 && hit.Position.Offset <= size);
            }
        }
        const ImTypesetDocumentRange everything = ImTypeset::SelectDocumentRange(
            document, ImVec2(0.0f, where.FullMin.y + 1.0f), ImVec2(5000.0f, where.FullMax.y - 1.0f),
            ImTypesetGranularity_Line);
        if (c.lines > 0 && everything.Start.Block == 1 && everything.End.Block == 1)
            CHECK_EQ(everything.End.Offset, size);
        for (int line = 0; line < c.lines; ++line) {
            const ImTypesetLine l = ImTypeset::GetDocumentBlockLine(document, 1, line);
            CHECK(l.TextStart >= 0 && l.TextStart <= l.TextEnd && l.TextEnd <= size);
        }
        ImTypeset::DestroyDocument(document);
    }
    // Blank lines at the end stop being that when text arrives after them:
    // the block's text is then all of it, and positions in what was there
    // already keep their meaning.
    Content content;
    content.add(ImTypesetBlockKind_Code, "a\nb\n\n");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{0, 100}),
             std::string("a\nb"));
    CHECK(ImTypeset::AppendDocumentText(document, "c\n"));
    CHECK_EQ(ImTypeset::GetDocumentBlockTextSize(document, 0), 7);
    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{0, 100}),
             std::string("a\nb\n\nc"));
    CHECK_EQ(copied(document, ImTypesetPosition{0, 2}, ImTypesetPosition{0, 3}), std::string("b"));
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount, 4);
    ImTypeset::DestroyDocument(document);
}

// --- (c) A context whose ImGui context was destroyed under it -----------------------------

namespace
{

// A context with its fonts, a document and a paragraph laid out and drawn for
// two frames: what a host holds when it destroys its ImGui context without
// destroying this library's first.
struct Kept {
    ImTypesetContext* ctx = nullptr;
    ImTypesetDocument* document = nullptr;
    ImTypesetParagraph* paragraph = nullptr;
    ImGuiContext* imgui = nullptr;
    void* atlas = nullptr;

    bool make()
    {
        new_imgui_context();
        if (!IMTYPESET_CHECKVERSION())
            return false;
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr || !add_fonts())
            return false;
        ImTypeset::GetStyle().EmSize = 16.0f;
        Content content;
        content.add(ImTypesetBlockKind_Paragraph, "Kept words, across the end of a context.");
        content.add(ImTypesetBlockKind_Code, "kept = 1");
        document = document_of(content);
        paragraph = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(paragraph, "A kept paragraph.");
        ImTypeset::LayoutParagraph(paragraph, 300.0f);
        Buffers buffers;
        for (int f = 0; f < 2; ++f) {
            Frame frame;
            if (!draw_all(document, buffers) || buffers.vertices.Size == 0)
                return false;
        }
        imgui = ImGui::GetCurrentContext();
        atlas = ImGui::GetIO().Fonts;
        return document != nullptr && paragraph != nullptr;
    }
};

bool refused_for_no_context(const char* part)
{
    return ImTypeset::GetLastError()->Code == static_cast<int>(ImTypesetError_NoContext) &&
           std::strstr(ImTypeset::GetLastError()->Message, part) != nullptr;
}

} // namespace

TEST(a_context_whose_imgui_context_is_gone_refuses_to_draw_and_can_still_be_destroyed)
{
    Tracking tracking;
    Kept kept;
    REQUIRE(kept.make());
    ImGui::DestroyContext();

    // No ImGui context at all: everything that needs one says so.
    Buffers buffers;
    const ImVec4 clip(0.0f, 0.0f, 4000.0f, 4000.0f);
    CHECK(!draw_all(kept.document, buffers));
    CHECK(refused_for_no_context("no current ImGui context"));
    CHECK_EQ(buffers.vertices.Size, 0);
    CHECK(!ImTypeset::DrawParagraphToBuffers(kept.paragraph, ImVec2(0.0f, 0.0f), clip,
                                             &buffers.vertices, &buffers.indices));
    CHECK(refused_for_no_context("no current ImGui context"));
    CHECK(!ImTypeset::LayoutDocumentWindow(kept.document, 0.0f, 1000.0f) ||
          ImTypeset::GetLastError()->Code == static_cast<int>(ImTypesetError_None));
    CHECK(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular,
                                        font_path("Roboto-Regular.ttf").c_str()) == nullptr);
    CHECK(refused_for_no_context("no current ImGui context"));
    CHECK(!ImTypeset::RemoveFont(ImTypesetFace_Regular));
    CHECK(refused_for_no_context("no current ImGui context"));
    // The ImFont of a role was the atlas's, and the atlas is gone.
    for (int face = 0; face < ImTypesetFace_COUNT; ++face)
        CHECK(ImTypeset::GetFont(face) == nullptr);
    // What needs no ImGui context still answers.
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(kept.document), 2);
    CHECK_EQ(copied(kept.document, ImTypesetPosition{1, 0}, ImTypesetPosition{1, 8}),
             std::string("kept = 1"));
    CHECK(ImTypeset::GetParagraphLineCount(kept.paragraph) > 0);

    // Another ImGui context is not this one's either. Two are made so that one
    // of them is certain to be at another address than the one destroyed.
    ImGuiContext* const first = ImGui::CreateContext();
    ImGuiContext* const second = ImGui::CreateContext();
    ImGuiContext* const other = first != kept.imgui ? first : second;
    ImGui::SetCurrentContext(other);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(800.0f, 600.0f);
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    {
        Frame frame;
        CHECK(!draw_all(kept.document, buffers));
        CHECK(refused_for_no_context("not the current one"));
        CHECK(ImTypeset::GetFont(ImTypesetFace_Regular) == nullptr);
        ImGui::Begin("w");
        CHECK(!ImTypeset::DrawDocument(kept.document, ImGui::GetWindowDrawList(),
                                       ImVec2(0.0f, 0.0f)));
        CHECK(refused_for_no_context("not the current one"));
        CHECK(!ImTypeset::DocumentView("view", kept.document, ImVec2(200.0f, 100.0f)));
        ImGui::End();
    }
    ImGui::SetCurrentContext(first);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::DestroyContext(first);
    ImGui::SetCurrentContext(second);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::DestroyContext(second);

    // The one thing still to do with it: destroy it, with everything it holds.
    // That touches nothing of ImGui's.
    ImTypeset::DestroyContext(kept.ctx);
    CHECK(ImTypeset::GetCurrentContext() == nullptr);
}

// An address is all imgui.h gives to know an ImGui context by, and a new one
// is often made where the last one was freed. The library then takes it for
// the one it was created in. What it holds of the old atlas are the roles'
// fonts, and those are found gone.
TEST(a_new_imgui_context_at_the_address_of_the_one_destroyed_finds_every_role_empty)
{
    Tracking tracking;
    Kept kept;
    REQUIRE(kept.make());
    tracking.recycle(kept.imgui);
    tracking.recycle(kept.atlas);
    ImGui::DestroyContext();
    new_imgui_context();
    REQUIRE(ImGui::GetCurrentContext() == kept.imgui);
    REQUIRE(ImGui::GetIO().Fonts == kept.atlas);
    ImFont* const hosts =
        ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path("Roboto-Bold.ttf").c_str());
    REQUIRE(hosts != nullptr);

    Buffers buffers;
    for (int face = 0; face < ImTypesetFace_COUNT; ++face)
        CHECK(ImTypeset::GetFont(face) == nullptr);
    {
        Frame frame;
        // Nothing is drawn with a font of the atlas that is gone.
        ImTypeset::LayoutDocument(kept.document, 300.0f);
        CHECK(!draw_all(kept.document, buffers));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_NoFont));
        CHECK_EQ(buffers.vertices.Size, 0);
        CHECK(!ImTypeset::DrawParagraphToBuffers(kept.paragraph, ImVec2(0.0f, 0.0f),
                                                 ImVec4(0.0f, 0.0f, 4000.0f, 4000.0f),
                                                 &buffers.vertices, &buffers.indices));
        CHECK_EQ(buffers.vertices.Size, 0);
    }
    // Given their fonts again, the same context and what it kept work on.
    CHECK(add_fonts());
    CHECK(ImGui::GetIO().Fonts->Fonts.contains(hosts));
    CHECK_EQ(ImGui::GetIO().Fonts->Fonts.Size, 1 + static_cast<int>(IM_ARRAYSIZE(FONTS)));
    {
        Frame frame;
        CHECK(ImTypeset::LayoutDocument(kept.document, 300.0f));
        CHECK(draw_all(kept.document, buffers));
        CHECK(buffers.vertices.Size > 100);
        CHECK(ImTypeset::LayoutParagraph(kept.paragraph, 300.0f));
        ImGui::Begin("w");
        CHECK(ImTypeset::DrawParagraph(kept.paragraph, ImGui::GetWindowDrawList(),
                                       ImGui::GetCursorScreenPos()));
        ImGui::End();
    }
    ImTypeset::DestroyContext(kept.ctx);
    ImGui::DestroyContext();
}

// --- (f) One copy of a face in the atlas -------------------------------------------------

// The text a host's own widgets draw, in each face that is not the math one.
const char* const HOST_WORDS = "Hamburgefonstiv 0123456789 AVATAR To. fi (x) [y] {z}";

TEST(a_host_draws_its_own_text_in_a_roles_font_and_it_is_the_files_text)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
    // Each role's ImFont is an ordinary one in the atlas, where GetFont says.
    for (const auto& entry : FONTS) {
        CHECK_CONTEXT("%s", entry.file);
        ImFont* const font = ImTypeset::GetFont(entry.face);
        REQUIRE(font != nullptr);
        CHECK(atlas->Fonts.contains(font));
    }
    const int roles_only = atlas->Fonts.Size;
    CHECK_EQ(roles_only, static_cast<int>(IM_ARRAYSIZE(FONTS)));

    // The same files added by the host, as a host does it: what sharing saves.
    std::vector<ImFont*> hosts;
    for (const auto& entry : FONTS) {
        hosts.push_back(atlas->AddFontFromFileTTF(font_path(entry.file).c_str()));
        REQUIRE(hosts.back() != nullptr);
    }
    Frame frame;
    int compared = 0;
    for (size_t i = 0; i < IM_ARRAYSIZE(FONTS); ++i) {
        if (FONTS[i].face == ImTypesetFace_Math)
            continue;
        CHECK_CONTEXT("%s", FONTS[i].file);
        ImFont* const role = ImTypeset::GetFont(FONTS[i].face);
        for (const float size : {13.0f, 16.0f, 24.0f}) {
            CHECK_CONTEXT("size %g", static_cast<double>(size));
            ImGui::PushFont(role, size);
            const ImVec2 as_role = ImGui::CalcTextSize(HOST_WORDS);
            ImGui::PopFont();
            ImGui::PushFont(hosts[i], size);
            const ImVec2 as_host = ImGui::CalcTextSize(HOST_WORDS);
            ImGui::PopFont();
            CHECK(as_role.x > 100.0f);
            // The role's font and the host's copy of the file set the same
            // words to the same width and height.
            CHECK_EQ(as_role.x, as_host.x);
            CHECK_EQ(as_role.y, as_host.y);
            ++compared;
        }
        for (const char* at = HOST_WORDS; *at != '\0'; ++at)
            CHECK(role->IsGlyphInFont(static_cast<ImWchar>(*at)));
    }
    CHECK_EQ(compared, 18);
}

// What a second copy of every face costs the atlas, measured: bytes ImGui
// holds for the fonts themselves, and for the glyphs once the same words have
// been drawn in each. Printed, not asserted beyond its sign.
TEST(what_a_second_copy_of_every_face_costs_the_atlas)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
    const auto texture_bytes = [&]() {
        size_t total = 0;
        for (const ImTextureData* texture : atlas->TexList)
            total += static_cast<size_t>(texture->Width) * static_cast<size_t>(texture->Height) *
                     static_cast<size_t>(texture->BytesPerPixel);
        return total;
    };
    const auto draw_in = [&](const std::vector<ImFont*>& fonts) {
        Frame frame;
        ImGui::Begin("w");
        for (ImFont* font : fonts) {
            for (const float size : {13.0f, 16.0f, 24.0f}) {
                ImGui::PushFont(font, size);
                ImGui::TextUnformatted(HOST_WORDS);
                ImGui::TextUnformatted("the quick brown fox jumps over the lazy dog, THE QUICK "
                                       "BROWN FOX JUMPS OVER THE LAZY DOG; 0.5 + 1/2 = 100%?!");
                ImGui::PopFont();
            }
        }
        ImGui::End();
    };
    std::vector<ImFont*> roles;
    for (const auto& entry : FONTS)
        roles.push_back(ImTypeset::GetFont(entry.face));
    const size_t held_roles = g_bytes_held;
    draw_in(roles);
    const size_t held_roles_drawn = g_bytes_held;
    const size_t texture_roles = texture_bytes();

    std::vector<ImFont*> hosts;
    size_t files = 0;
    for (const auto& entry : FONTS) {
        std::string bytes;
        REQUIRE(golden::read_file(font_path(entry.file), bytes));
        files += bytes.size();
        hosts.push_back(atlas->AddFontFromFileTTF(font_path(entry.file).c_str()));
        REQUIRE(hosts.back() != nullptr);
    }
    const size_t held_both = g_bytes_held;
    draw_in(hosts);
    const size_t held_both_drawn = g_bytes_held;
    const size_t texture_both = texture_bytes();

    CHECK(held_both > held_roles_drawn);
    std::printf("answers: %d faces, %zu bytes of font files. In ImGui's allocations: the roles' "
                "fonts hold %zu bytes before a glyph is drawn and %zu after the test words at "
                "three sizes; a second copy of each file added by the host adds %zu bytes, and "
                "%zu more once the same words are drawn in them. Atlas textures: %zu bytes "
                "with the roles' fonts drawn, %zu with both.\n",
                static_cast<int>(IM_ARRAYSIZE(FONTS)), files, held_roles, held_roles_drawn,
                held_both - held_roles_drawn, held_both_drawn - held_both, texture_roles,
                texture_both);
}

// --- C1: no draw list is the current window's --------------------------------------------

TEST(no_draw_list_is_the_current_windows_and_is_refused_outside_a_frame)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Content content;
    content.add(ImTypesetBlockKind_Paragraph, "Words drawn into whichever window is current.");
    ImTypesetDocument* document = document_of(content);
    REQUIRE(document != nullptr);
    ImTypesetParagraph* paragraph = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(paragraph, "A paragraph.");
    CHECK(ImTypeset::LayoutParagraph(paragraph, 300.0f));

    // Before the first frame and between two frames there is no current window.
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_CONTEXT("pass %d", pass);
        CHECK(!ImTypeset::DrawDocument(document, nullptr, ImVec2(0.0f, 0.0f)));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
        CHECK(std::strstr(ImTypeset::GetLastError()->Message, "outside a frame") != nullptr);
        CHECK(!ImTypeset::DrawParagraph(paragraph, nullptr, ImVec2(0.0f, 0.0f)));
        CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_InvalidArgument));
        if (pass == 1)
            break;
        Frame frame;
        ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
        ImGui::Begin("one");
        ImDrawList* const list = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const int before = list->VtxBuffer.Size;
        CHECK(ImTypeset::DrawDocument(document, list, pos));
        const int named = list->VtxBuffer.Size - before;
        CHECK(named > 100);
        // Null: the same window's list, the same vertices again.
        CHECK(ImTypeset::DrawDocument(document, nullptr, pos));
        CHECK_EQ(list->VtxBuffer.Size - before, 2 * named);
        CHECK(0 == std::memcmp(&list->VtxBuffer[before], &list->VtxBuffer[before + named],
                               static_cast<size_t>(named) * sizeof(ImDrawVert)));
        const int with_document = list->VtxBuffer.Size;
        CHECK(ImTypeset::DrawParagraph(paragraph, nullptr, pos));
        CHECK(list->VtxBuffer.Size > with_document);
        ImGui::End();
    }
    ImTypeset::DestroyParagraph(paragraph);
    ImTypeset::DestroyDocument(document);
}
