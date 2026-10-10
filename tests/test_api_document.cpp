// The Document of include/imtypeset.h: styled blocks in; layout, drawing and
// selection out. Everything goes in through the public header;
// src/imtypeset_document.h is included to install the hook that replays the
// reference's goldens (reference rules, exact scaled points) and to look at
// the block layer underneath.
//
// What is tested, and what each stands on:
//   - the description: every kind of block, runs, tags, markers, containers;
//   - laziness and the cost of edits, by work counters and not by a clock,
//     at N = 10,000 blocks and again at a smaller N to show the count does
//     not depend on N;
//   - positions as (block, byte), across blocks, and what a copy gives;
//   - a steady frame allocates nothing;
//   - tests/golden/blocks through SetDocumentBlocks(), exactly.
//
// Every document here is bounded by construction: the largest is 10,000
// blocks of about 120 bytes, and a test that lays all of one out says so.

#include "blocks_golden.h"
#include "core_golden.h"
#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "imtypeset_document.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

// --- Counting allocations: as tests/test_api.cpp does -----------------------------
#if defined(__GNUC__)
#define TEST_DOC_NOINLINE __attribute__((noinline))
#else
#define TEST_DOC_NOINLINE
#endif

static size_t g_allocations = 0;

TEST_DOC_NOINLINE void* operator new(size_t size)
{
    ++g_allocations;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}
TEST_DOC_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}
TEST_DOC_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}
TEST_DOC_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}
TEST_DOC_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
TEST_DOC_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_DOC_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
TEST_DOC_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
TEST_DOC_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_DOC_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace
{

void* counted_alloc(size_t size, void*)
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}

void counted_free(void* memory, void*)
{
    std::free(memory);
}

const struct {
    ImTypesetFace face;
    const char* file;
} DEFAULT_FONTS[] = {
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
        ImGui::SetAllocatorFunctions(&counted_alloc, &counted_free, nullptr);
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
        for (const auto& entry : DEFAULT_FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
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

    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

// What a host assembles before it hands a document over.
struct Content {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    std::vector<ImTypesetRun> runs;
    std::vector<ImTypesetBlockStyle> styles;
    std::vector<ImTypesetContainer> containers;

    ImTypesetBlock& add(ImTypesetBlockKind kind, const std::string& body, int style = 0)
    {
        ImTypesetBlock block;
        block.Kind = kind;
        block.Style = style;
        block.TextStart = static_cast<int>(text.size());
        text += body;
        block.TextEnd = static_cast<int>(text.size());
        block.RunStart = static_cast<int>(runs.size());
        blocks.push_back(block);
        return blocks.back();
    }
    // A run of the last block, by the bytes of its own text.
    ImTypesetRun& run(int start, int end)
    {
        ImTypesetRun r;
        r.Start = start;
        r.End = end;
        runs.push_back(r);
        ++blocks.back().RunCount;
        return runs.back();
    }
    void marker(const std::string& body)
    {
        blocks.back().MarkerStart = static_cast<int>(text.size());
        text += body;
        blocks.back().MarkerEnd = static_cast<int>(text.size());
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
        out.Containers = containers.data();
        out.ContainerCount = static_cast<int>(containers.size());
        return out;
    }
};

const char* const SENTENCE =
    "Stored letters and other symbols are retrieved and ordered according to a language's "
    "orthography for visual display.";

// `count` paragraphs, each SENTENCE with its number in front.
Content paragraphs(int count)
{
    Content content;
    content.text.reserve(static_cast<size_t>(count) * 130);
    ImTypesetBlockStyle body;
    body.SpaceBelow = 8.0f;
    content.styles.push_back(body);
    for (int i = 0; i < count; ++i)
        content.add(ImTypesetBlockKind_Paragraph, std::to_string(i) + " " + SENTENCE);
    return content;
}

std::string copied(ImTypesetDocument* document, ImTypesetPosition start, ImTypesetPosition end,
                   const char* separator = nullptr)
{
    ImGuiTextBuffer buffer;
    const ImTypesetDocumentRange range = {start, end};
    ImTypeset::AppendDocumentRangeText(document, range, &buffer, separator);
    return std::string(buffer.c_str());
}

struct Buffers {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
};

// Draws a window of the document `height` pixels tall that starts `scroll`
// pixels down it, into buffers that are emptied first.
bool draw_window(ImTypesetDocument* document, Buffers& into, float scroll, float height,
                 const ImTypesetDocumentDraw* draw = nullptr)
{
    into.vertices.resize(0);
    into.indices.resize(0);
    return ImTypeset::DrawDocumentToBuffers(document, ImVec2(10.0f, 20.0f - scroll),
                                            ImVec4(0.0f, 20.0f, 2000.0f, 20.0f + height),
                                            &into.vertices, &into.indices, draw);
}

} // namespace

TEST(every_kind_of_block_is_described_laid_out_and_found_again)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;

    Content content;
    ImTypesetBlockStyle body;
    body.SpaceBelow = 10.0f;
    body.Align = ImTypesetAlign_Left;
    ImTypesetBlockStyle heading = body;
    heading.Face = ImTypesetFace_Bold;
    heading.Scale = 1.5f;
    ImTypesetBlockStyle code = body;
    code.Face = ImTypesetFace_Code;
    content.styles = {body, heading, code};

    content.add(ImTypesetBlockKind_Heading, "A heading", 1).Level = 1;                      // 0
    content.add(ImTypesetBlockKind_Paragraph, "Plain words, a link here, and more words."); // 1
    ImTypesetRun& link = content.run(15, 24);
    link.Tag = 7;
    link.Color = IM_COL32(0, 0, 255, 255);
    ImTypesetContainer list;
    list.Kind = ImTypesetContainerKind_List;
    list.Indent = 24.0f;
    list.Flags = ImTypesetContainerFlags_Tight;
    content.containers.push_back(list);
    for (int item = 0; item < 2; ++item) { // 2, 3
        ImTypesetBlock& block =
            content.add(ImTypesetBlockKind_ListItem, item == 0 ? "first item" : "second item");
        block.LeftIndent = 24.0f;
        block.Container = 1;
        block.Item = item;
        content.marker(item == 0 ? "1." : "2.");
    }
    content.add(ImTypesetBlockKind_Code, "int main()\n{\n\treturn 0;\n}\n", 2); // 4
    content.add(ImTypesetBlockKind_Rule, "");                                   // 5
    ImTypesetBlock& host = content.add(ImTypesetBlockKind_Host, "[a widget]");  // 6
    host.Extent = 40.0f;
    content.add(ImTypesetBlockKind_Paragraph, "The end."); // 7

    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 8);
    CHECK_EQ(ImTypeset::GetDocumentStyleCount(document), 3);
    CHECK_EQ(ImTypeset::GetDocumentContainerCount(document), 1);
    CHECK(ImTypeset::LayoutDocument(document, 400.0f));
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 100000.0f));
    const ImVec2 size = ImTypeset::GetDocumentSize(document);
    CHECK_EQ(size.x, 400.0f);
    CHECK(size.y > 150.0f);
    CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksDeferred, 0);

    // Blocks follow one another down the page, each inside the column.
    ImTypesetBlockGeometry above = ImTypeset::GetDocumentBlockGeometry(document, 0);
    CHECK(above.LaidOut);
    CHECK_EQ(above.FullMin.y, 0.0f);
    for (int block = 1; block < 8; ++block) {
        CHECK_CONTEXT("block %d", block);
        const ImTypesetBlockGeometry here = ImTypeset::GetDocumentBlockGeometry(document, block);
        CHECK(here.LaidOut);
        CHECK(here.FullMin.y >= above.FullMax.y - 0.01f);
        CHECK(here.FullMax.y >= here.FullMin.y);
        CHECK(here.Min.x >= -0.01f);
        CHECK(here.Max.x <= 400.01f);
        above = here;
    }
    // The heading is set larger than the body, the items are indented, the two
    // items of a tight list touch, and the host block is as tall as it said.
    const ImTypesetBlockGeometry title = ImTypeset::GetDocumentBlockGeometry(document, 0);
    const ImTypesetBlockGeometry para = ImTypeset::GetDocumentBlockGeometry(document, 1);
    CHECK(title.FullMax.y - title.FullMin.y > para.FullMax.y - para.FullMin.y);
    const ImTypesetBlockGeometry first = ImTypeset::GetDocumentBlockGeometry(document, 2);
    const ImTypesetBlockGeometry second = ImTypeset::GetDocumentBlockGeometry(document, 3);
    CHECK_NEAR(first.FullMin.x, 24.0f, 0.01f);
    CHECK_NEAR(second.FullMin.y, first.FullMax.y, 0.01f);
    CHECK_NEAR(first.FullMin.y - para.FullMax.y, 10.0f, 0.01f);
    const ImTypesetBlockGeometry widget = ImTypeset::GetDocumentBlockGeometry(document, 6);
    CHECK_NEAR(widget.FullMax.y - widget.FullMin.y, 40.0f, 0.01f);
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 4).LineCount, 4);
    CHECK(!ImTypeset::GetDocumentBlockGeometry(document, 8).LaidOut);
    CHECK(!ImTypeset::GetDocumentBlockGeometry(document, -1).LaidOut);

    // A point on the link answers its block, a byte inside it, and its tag.
    const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(document, 1, 0);
    CHECK_EQ(line.TextStart, 0);
    const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{1, 18});
    CHECK(caret.y >= para.FullMin.y - 0.01f);
    CHECK(caret.w <= para.FullMax.y + 0.01f);
    const ImTypesetDocumentHit on_link =
        ImTypeset::HitTestDocument(document, ImVec2(caret.x + 1.0f, (caret.y + caret.w) / 2));
    CHECK_EQ(on_link.Position.Block, 1);
    CHECK(on_link.Position.Offset >= 18 && on_link.Position.Offset <= 19);
    CHECK(on_link.Inside);
    CHECK(on_link.InBlock);
    CHECK_EQ(on_link.Tag, 7);
    const ImVec4 plain = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{1, 3});
    CHECK_EQ(
        ImTypeset::HitTestDocument(document, ImVec2(plain.x + 1.0f, (plain.y + plain.w) / 2)).Tag,
        0);
    // Between two blocks: the lower one, and not in it.
    const ImTypesetDocumentHit between =
        ImTypeset::HitTestDocument(document, ImVec2(30.0f, para.FullMax.y + 5.0f));
    CHECK_EQ(between.Position.Block, 2);
    CHECK(!between.InBlock);
    // Past either end.
    CHECK_EQ(ImTypeset::HitTestDocument(document, ImVec2(5.0f, -50.0f)).Position.Block, 0);
    const ImTypesetDocumentHit below = ImTypeset::HitTestDocument(document, ImVec2(5.0f, 1.0e6f));
    CHECK_EQ(below.Position.Block, 7);
    CHECK(below.Position.Offset <= 8);

    // What a copy gives: text as written, the marker never, code as its source
    // bytes, the host block's text whole, a rule nothing, and the separator
    // the caller names between blocks.
    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{7, 8}),
             std::string("A heading\n\nPlain words, a link here, and more words.\n\nfirst item"
                         "\n\nsecond item\n\nint main()\n{\n\treturn 0;\n}\n\n[a widget]\n\n"
                         "The end."));
    CHECK_EQ(copied(document, ImTypesetPosition{1, 13}, ImTypesetPosition{2, 5}, " | "),
             std::string("a link here, and more words. | first"));
    CHECK_EQ(copied(document, ImTypesetPosition{4, 4}, ImTypesetPosition{4, 16}),
             std::string("main()\n{\n\tre"));
    CHECK_EQ(copied(document, ImTypesetPosition{3, 4}, ImTypesetPosition{3, 4}), std::string());
    CHECK_EQ(copied(document, ImTypesetPosition{5, 0}, ImTypesetPosition{6, 0}), std::string());

    // Rectangles for a range across blocks: at least one a block, in order.
    ImVector<ImVec4> rects;
    const ImTypesetDocumentRange across = {{1, 15}, {4, 5}};
    const int count = ImTypeset::GetDocumentRangeRects(document, across, &rects);
    CHECK_EQ(count, rects.Size);
    CHECK(count >= 4);
    for (int i = 1; i < rects.Size; ++i)
        CHECK(rects[i].y >= rects[i - 1].y - 0.01f);

    // A drag from the heading to the code selects from one to the other,
    // whichever way it went; by word it takes the whole words at both ends.
    const ImVec2 from(title.FullMin.x + 3.0f, (title.FullMin.y + title.FullMax.y) / 2);
    const ImVec2 to(caret.x + 1.0f, (caret.y + caret.w) / 2);
    const ImTypesetDocumentRange drag = ImTypeset::SelectDocumentRange(document, from, to);
    const ImTypesetDocumentRange back = ImTypeset::SelectDocumentRange(document, to, from);
    CHECK_EQ(drag.Start.Block, 0);
    CHECK_EQ(drag.End.Block, 1);
    CHECK_EQ(back.Start.Block, drag.Start.Block);
    CHECK_EQ(back.Start.Offset, drag.Start.Offset);
    CHECK_EQ(back.End.Offset, drag.End.Offset);
    const ImTypesetDocumentRange words =
        ImTypeset::SelectDocumentRange(document, from, to, ImTypesetGranularity_Word);
    CHECK_EQ(copied(document, words.Start, words.End),
             std::string("A heading\n\nPlain words, a link"));
    const ImTypesetDocumentRange lines =
        ImTypeset::SelectDocumentRange(document, to, to, ImTypesetGranularity_Line);
    CHECK_EQ(lines.Start.Block, 1);
    CHECK_EQ(lines.Start.Offset, 0);
    CHECK_EQ(lines.End.Offset, 41);

    // It draws, to a draw list and to buffers, with a selection and marks.
    ImTypesetMark marks[2];
    marks[0].Range = ImTypesetDocumentRange{{1, 15}, {1, 24}};
    marks[0].Place = ImTypesetMarkPlace_Under;
    marks[0].Color = IM_COL32(0, 0, 255, 255);
    marks[1].Range = ImTypesetDocumentRange{{7, 0}, {7, 3}};
    marks[1].Place = ImTypesetMarkPlace_Behind;
    marks[1].Color = IM_COL32(255, 255, 0, 128);
    ImTypesetDocumentDraw with;
    with.Selection = across;
    with.Marks = marks;
    with.MarkCount = 2;
    Buffers plain_buffers;
    Buffers marked;
    CHECK(draw_window(document, plain_buffers, 0.0f, 1000.0f));
    CHECK(draw_window(document, marked, 0.0f, 1000.0f, &with));
    CHECK(plain_buffers.vertices.Size > 300);
    // One quad for the underline, one behind "The", and the selection's.
    CHECK(marked.vertices.Size >= plain_buffers.vertices.Size + 4 * (2 + count));
    ImGui::SetNextWindowSize(ImVec2(500.0f, 500.0f));
    ImGui::Begin("w");
    const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    CHECK(ImTypeset::DrawDocument(document, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                                  &with));
    CHECK(ImGui::GetWindowDrawList()->VtxBuffer.Size > before + 300);
    ImGui::End();

    ImTypeset::DestroyDocument(document);
}

// The counters after an operation, less those before it.
struct Work {
    ImTypesetDocumentStats stats;
    size_t allocations;

    static Work now(const ImTypesetDocument* document)
    {
        return Work{ImTypeset::GetDocumentStats(document), g_allocations};
    }
};

TEST(ten_thousand_blocks_are_laid_out_only_where_a_window_shows_them)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    // What one window costs must not depend on how long the document is: the
    // same steps at two lengths, and the counts compared.
    ImU64 laid_out_at[2] = {0, 0};
    ImU64 material_at[2] = {0, 0};
    const int LENGTHS[2] = {500, 10000};
    for (int pass = 0; pass < 2; ++pass) {
        const int count = LENGTHS[pass];
        CHECK_CONTEXT("%d blocks", count);
        const Content content = paragraphs(count);
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        const ImTypesetDocumentContent view = content.view();
        CHECK(ImTypeset::SetDocumentBlocks(document, &view));
        CHECK(ImTypeset::LayoutDocument(document, 480.0f));
        // Handing the blocks over builds nothing and lays nothing out.
        ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
        CHECK_EQ(stats.MaterialBuilds, ImU64(0));
        CHECK_EQ(stats.BlocksLaidOut, ImU64(0));
        CHECK_EQ(stats.BlocksDeferred, count);
        const float estimate = ImTypeset::GetDocumentSize(document).y;
        CHECK(estimate > static_cast<float>(count) * 20.0f);

        // A window at the top: what it shows, and no more.
        Buffers buffers;
        CHECK(draw_window(document, buffers, 0.0f, 600.0f));
        stats = ImTypeset::GetDocumentStats(document);
        CHECK(stats.BlocksLaidOut >= 5 && stats.BlocksLaidOut <= 20);
        CHECK_EQ(stats.MaterialBuilds, stats.BlocksLaidOut);
        CHECK(buffers.vertices.Size > 1000);
        const ImU64 first_window = stats.BlocksLaidOut;

        // The same window again: nothing, and not one allocation.
        Work before = Work::now(document);
        CHECK(draw_window(document, buffers, 0.0f, 600.0f));
        Work after = Work::now(document);
        CHECK_EQ(after.stats.BlocksLaidOut, before.stats.BlocksLaidOut);
        CHECK_EQ(after.stats.MaterialBuilds, before.stats.MaterialBuilds);
        CHECK_EQ(after.stats.StructureVisits, before.stats.StructureVisits);
        CHECK_EQ(after.allocations, before.allocations);

        // A window in the middle lays out what it shows and nothing above it.
        const float middle = ImTypeset::GetDocumentSize(document).y / 2.0f;
        CHECK(draw_window(document, buffers, middle, 600.0f));
        stats = ImTypeset::GetDocumentStats(document);
        CHECK(stats.BlocksLaidOut - first_window >= 5);
        CHECK(stats.BlocksLaidOut - first_window <= 20);
        CHECK_EQ(stats.BlocksDeferred, count - static_cast<int>(stats.BlocksLaidOut));
        const ImTypesetDocumentHit hit =
            ImTypeset::HitTestDocument(document, ImVec2(50.0f, middle));
        CHECK(hit.Position.Block > count / 4 && hit.Position.Block < 3 * count / 4);

        // A new width lays nothing out until a window asks.
        const ImU64 at_width = ImTypeset::GetDocumentStats(document).BlocksLaidOut;
        CHECK(ImTypeset::LayoutDocument(document, 360.0f));
        CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksLaidOut, at_width);
        CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksDeferred, count);
        CHECK(draw_window(document, buffers, 0.0f, 600.0f));
        stats = ImTypeset::GetDocumentStats(document);
        CHECK(stats.BlocksLaidOut - at_width >= 5 && stats.BlocksLaidOut - at_width <= 20);
        // The text was not built again: only broken again.
        CHECK(stats.MaterialBuilds < stats.BlocksLaidOut);

        laid_out_at[pass] = stats.BlocksLaidOut;
        material_at[pass] = stats.MaterialBuilds;
        ImTypeset::DestroyDocument(document);
    }
    // Within a block or two: the middle window does not land on the same
    // line of a block at both lengths.
    CHECK(laid_out_at[1] <= laid_out_at[0] + 3 && laid_out_at[0] <= laid_out_at[1] + 3);
    CHECK(material_at[1] <= material_at[0] + 3 && material_at[0] <= material_at[1] + 3);
    std::printf("document: %d blocks cost %llu layouts and %llu text builds over four windows; "
                "%d blocks cost %llu and %llu\n",
                LENGTHS[0], static_cast<unsigned long long>(laid_out_at[0]),
                static_cast<unsigned long long>(material_at[0]), LENGTHS[1],
                static_cast<unsigned long long>(laid_out_at[1]),
                static_cast<unsigned long long>(material_at[1]));
}

TEST(appending_to_the_last_block_costs_one_block_whatever_the_documents_length)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const int LENGTHS[2] = {200, 10000};
    for (int pass = 0; pass < 2; ++pass) {
        const int count = LENGTHS[pass];
        CHECK_CONTEXT("%d blocks", count);
        const Content content = paragraphs(count);
        ImTypesetDocument* document = ImTypeset::CreateDocument();
        REQUIRE(document != nullptr);
        const ImTypesetDocumentContent view = content.view();
        CHECK(ImTypeset::SetDocumentBlocks(document, &view));
        CHECK(ImTypeset::LayoutDocument(document, 480.0f));
        Buffers buffers;
        // The tail of the document, as a pane that follows a stream shows it.
        const auto draw_tail = [&]() {
            const float height = ImTypeset::GetDocumentSize(document).y;
            return draw_window(document, buffers, height - 300.0f, 300.0f);
        };
        // Twice: the first corrects the estimate the tail's place came from.
        CHECK(draw_tail());
        CHECK(draw_tail());
        CHECK(draw_tail());

        // A selection in the block being written, to see that it survives.
        const ImTypesetPosition word_start = {count - 1, 0};
        const ImTypesetPosition word_end = {count - 1, 12};
        const std::string selected = copied(document, word_start, word_end);
        CHECK(!selected.empty());

        const Work before = Work::now(document);
        for (int step = 0; step < 25; ++step) {
            CHECK(ImTypeset::AppendDocumentText(document, " more words arrive"));
            CHECK(draw_tail());
        }
        const Work after = Work::now(document);
        // One block built and laid out per append, and no pass over the document.
        CHECK_EQ(after.stats.MaterialBuilds - before.stats.MaterialBuilds, ImU64(25));
        CHECK_EQ(after.stats.BlocksLaidOut - before.stats.BlocksLaidOut, ImU64(25));
        CHECK_EQ(after.stats.StructureVisits, before.stats.StructureVisits);
        CHECK_EQ(ImTypeset::GetDocumentBlockTextSize(document, count - 1),
                 static_cast<int>(std::to_string(count - 1).size() + 1 + std::strlen(SENTENCE) +
                                  25 * 18));
        CHECK_EQ(copied(document, word_start, word_end), selected);
        const std::string tail =
            copied(document, ImTypesetPosition{count - 1, 0}, ImTypesetPosition{count - 1, 100000});
        CHECK(tail.size() > 400);
        CHECK_EQ(tail.substr(tail.size() - 18), std::string(" more words arrive"));
        ImTypeset::DestroyDocument(document);
    }
}

TEST(replacing_blocks_keeps_the_layout_of_every_block_it_does_not_name)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const int count = 10000;
    const Content content = paragraphs(count);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 480.0f));
    Buffers buffers;
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    const ImTypesetDocumentStats shown = ImTypeset::GetDocumentStats(document);
    const ImTypesetBlockGeometry fourth = ImTypeset::GetDocumentBlockGeometry(document, 4);
    const std::string fourth_text =
        copied(document, ImTypesetPosition{4, 0}, ImTypesetPosition{4, 1000});

    // Two blocks become three, in view. Only the three are built and laid out.
    Content inserted;
    inserted.add(ImTypesetBlockKind_Paragraph, "A replacement, first of three.");
    inserted.add(ImTypesetBlockKind_Code, "x = 1\ny = 2");
    inserted.add(ImTypesetBlockKind_Paragraph, "A replacement, third of three.");
    const ImTypesetDocumentContent replacement = inserted.view();
    CHECK(ImTypeset::ReplaceDocumentBlocks(document, 1, 2, &replacement));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), count + 1);
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(stats.MaterialBuilds - shown.MaterialBuilds, ImU64(3));
    CHECK(stats.BlocksLaidOut - shown.BlocksLaidOut <= 4);
    // The blocks below moved by one number and kept their text and their size.
    CHECK_EQ(copied(document, ImTypesetPosition{5, 0}, ImTypesetPosition{5, 1000}), fourth_text);
    const ImTypesetBlockGeometry moved = ImTypeset::GetDocumentBlockGeometry(document, 5);
    CHECK_NEAR(moved.FullMax.y - moved.FullMin.y, fourth.FullMax.y - fourth.FullMin.y, 0.01f);
    CHECK_EQ(copied(document, ImTypesetPosition{1, 0}, ImTypesetPosition{3, 1000}, "|"),
             std::string("A replacement, first of three.|x = 1\ny = 2|A replacement, third of "
                         "three."));

    // Far out of view: nothing is built at all until it is looked at.
    const ImTypesetDocumentStats quiet = ImTypeset::GetDocumentStats(document);
    CHECK(ImTypeset::ReplaceDocumentBlocks(document, 9000, 1, &replacement));
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    stats = ImTypeset::GetDocumentStats(document);
    CHECK_EQ(stats.MaterialBuilds, quiet.MaterialBuilds);
    CHECK_EQ(stats.BlocksLaidOut, quiet.BlocksLaidOut);
    // The pass over the blocks that such an edit costs is counted, and is the
    // only work that grows with the document.
    CHECK(stats.StructureVisits - quiet.StructureVisits >= static_cast<ImU64>(count));
    CHECK(stats.StructureVisits - quiet.StructureVisits <= static_cast<ImU64>(2 * count + 8));

    // Removal, and appending whole blocks at the end.
    CHECK(ImTypeset::ReplaceDocumentBlocks(document, 0, 1, nullptr));
    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{0, 1000}),
             std::string("A replacement, first of three."));
    const int end = ImTypeset::GetDocumentBlockCount(document);
    CHECK(ImTypeset::ReplaceDocumentBlocks(document, end, 0, &replacement));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), end + 3);
    CHECK_EQ(copied(document, ImTypesetPosition{end + 1, 0}, ImTypesetPosition{end + 1, 1000}),
             std::string("x = 1\ny = 2"));
    // What the pass over all blocks costs on a clock, printed and not
    // asserted on (a timing means something in a release build only): one
    // block appended to the end and the tail looked at, a hundred times.
    {
        Content one;
        one.add(ImTypesetBlockKind_Paragraph, "One more block, committed at the end.");
        const ImTypesetDocumentContent appended = one.view();
        const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            ImTypeset::ReplaceDocumentBlocks(document, ImTypeset::GetDocumentBlockCount(document),
                                             0, &appended);
            const float height = ImTypeset::GetDocumentSize(document).y;
            draw_window(document, buffers, height - 300.0f, 300.0f);
        }
        const double micros =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
                .count() /
            100.0;
        const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
        std::printf(
            "document: appending a block to %d and drawing the tail: %.0f us each, %llu "
            "blocks visited by the pass, %llu laid out\n",
            count, micros,
            static_cast<unsigned long long>((after.StructureVisits - before.StructureVisits) / 100),
            static_cast<unsigned long long>((after.BlocksLaidOut - before.BlocksLaidOut) / 100));
        CHECK(ImTypeset::ReplaceDocumentBlocks(
            document, end + 3, ImTypeset::GetDocumentBlockCount(document) - (end + 3), nullptr));
    }
    // What is not a range of the document is refused and changes nothing.
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, end + 4, 0, &replacement));
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, -1, 1, &replacement));
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, 0, end + 100, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), end + 3);
    ImTypeset::DestroyDocument(document);
}

TEST(a_steady_frame_with_a_selection_and_marks_allocates_nothing)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content = paragraphs(300);
    content.add(ImTypesetBlockKind_Code, "for (;;) {\n    step();\n}");
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 480.0f));
    ImTypesetMark marks[3];
    marks[0].Range = ImTypesetDocumentRange{{2, 4}, {2, 20}};
    marks[0].Place = ImTypesetMarkPlace_Behind;
    marks[0].Color = IM_COL32(255, 255, 0, 128);
    marks[1].Range = ImTypesetDocumentRange{{3, 10}, {4, 6}};
    marks[1].Place = ImTypesetMarkPlace_Ink;
    marks[1].Color = IM_COL32(255, 0, 0, 255);
    marks[2].Range = ImTypesetDocumentRange{{3, 30}, {3, 50}};
    marks[2].Place = ImTypesetMarkPlace_Under;
    marks[2].Color = IM_COL32(0, 0, 255, 255);
    ImTypesetDocumentDraw with;
    with.Selection = ImTypesetDocumentRange{{1, 5}, {6, 9}};
    with.Marks = marks;
    with.MarkCount = 3;
    Buffers buffers;
    for (int warm = 0; warm < 3; ++warm)
        CHECK(draw_window(document, buffers, 0.0f, 600.0f, &with));
    // The blocks the mouse will cross have each been asked about once.
    for (int frame_number = 0; frame_number < 10; ++frame_number)
        (void)ImTypeset::HitTestDocument(
            document, ImVec2(100.0f, 100.0f + 7.0f * static_cast<float>(frame_number)));
    const int vertices = buffers.vertices.Size;
    const Work before = Work::now(document);
    for (int frame_number = 0; frame_number < 10; ++frame_number) {
        CHECK(ImTypeset::LayoutDocument(document, 480.0f));
        CHECK(draw_window(document, buffers, 0.0f, 600.0f, &with));
        (void)ImTypeset::HitTestDocument(
            document, ImVec2(100.0f, 100.0f + 7.0f * static_cast<float>(frame_number)));
    }
    const Work after = Work::now(document);
    CHECK_EQ(after.allocations, before.allocations);
    CHECK_EQ(after.stats.BlocksLaidOut, before.stats.BlocksLaidOut);
    CHECK_EQ(after.stats.MaterialBuilds, before.stats.MaterialBuilds);
    CHECK_EQ(buffers.vertices.Size, vertices);
    ImTypeset::DestroyDocument(document);
}

TEST(a_formula_copies_as_its_source_whether_found_or_marked_and_a_refusal_is_reported)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content;
    content.add(ImTypesetBlockKind_Paragraph, "Dollars: $a^2+b^2$ here.").Flags =
        ImTypesetBlockFlags_Math;
    content.add(ImTypesetBlockKind_Paragraph, "Marked: \\frac{1}{2} here.");
    content.run(8, 19).Flags = ImTypesetRunFlags_Formula;
    content.add(ImTypesetBlockKind_Paragraph, "Refused: \\begin{matrix}1\\end{matrix} here.");
    content.run(9, 36).Flags = ImTypesetRunFlags_Formula;
    content.add(ImTypesetBlockKind_Paragraph, "Not math: $a$ stays text.");
    content.add(ImTypesetBlockKind_Code, "\tx := 1\r\n\ty := 2\n\n");
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 400.0f));
    // The refusal is found when the block is laid out, and is that block's.
    CHECK(!ImTypeset::LayoutDocumentWindow(document, 0.0f, 10000.0f));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 2)->Code,
             static_cast<int>(ImTypesetError_MathRefused));
    CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 2)->BytePos, 9);
    CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 0)->Code,
             static_cast<int>(ImTypesetError_None));
    CHECK_EQ(ImTypeset::GetDocumentBlockError(document, 1)->Code,
             static_cast<int>(ImTypesetError_None));

    CHECK_EQ(copied(document, ImTypesetPosition{0, 0}, ImTypesetPosition{0, 1000}),
             std::string("Dollars: $a^2+b^2$ here."));
    CHECK_EQ(copied(document, ImTypesetPosition{1, 0}, ImTypesetPosition{1, 1000}),
             std::string("Marked: \\frac{1}{2} here."));
    // A formula is one thing: a range that starts inside it has none of it,
    // one that covers its first byte has all of it.
    CHECK_EQ(copied(document, ImTypesetPosition{1, 10}, ImTypesetPosition{1, 1000}),
             std::string(" here."));
    CHECK_EQ(copied(document, ImTypesetPosition{1, 8}, ImTypesetPosition{1, 9}),
             std::string("\\frac{1}{2}"));
    CHECK_EQ(copied(document, ImTypesetPosition{2, 0}, ImTypesetPosition{2, 1000}),
             std::string("Refused: \\begin{matrix}1\\end{matrix} here."));
    CHECK_EQ(copied(document, ImTypesetPosition{3, 0}, ImTypesetPosition{3, 1000}),
             std::string("Not math: $a$ stays text."));
    // The formulas are boxes: the first two blocks are one line each and
    // hold fewer glyph positions than their sources have bytes.
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount, 1);
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 1).LineCount, 1);
    // Code: positions are bytes of the source, tabs and CR LF as written, and
    // the blank lines at its end are not part of it.
    CHECK_EQ(ImTypeset::GetDocumentBlockGeometry(document, 4).LineCount, 2);
    CHECK_EQ(copied(document, ImTypesetPosition{4, 0}, ImTypesetPosition{4, 1000}),
             std::string("\tx := 1\r\n\ty := 2"));
    const ImTypesetLine second = ImTypeset::GetDocumentBlockLine(document, 4, 1);
    CHECK_EQ(second.TextStart, 9);
    CHECK_EQ(second.TextEnd, 16);
    ImTypeset::DestroyDocument(document);
}

TEST(what_is_wrong_with_a_description_is_cut_to_what_is_right_and_said)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    // No document, no content, nothing in it.
    CHECK(!ImTypeset::SetDocumentBlocks(nullptr, nullptr));
    CHECK(ImTypeset::SetDocumentBlocks(document, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 0);
    CHECK(ImTypeset::LayoutDocument(document, 300.0f));
    CHECK_EQ(ImTypeset::GetDocumentSize(document).y, 0.0f);
    Buffers buffers;
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    CHECK_EQ(buffers.vertices.Size, 0);
    CHECK_EQ(ImTypeset::HitTestDocument(document, ImVec2(1.0f, 1.0f)).Position.Block, 0);
    CHECK(!ImTypeset::AppendDocumentText(document, "x"));

    Content content;
    content.add(ImTypesetBlockKind_Paragraph, "some text");
    content.blocks.back().TextEnd += 100; // past the buffer
    content.blocks.back().Style = 5;      // no styles were given: ignored
    content.blocks.back().Container = 3;  // no such container
    content.blocks.back().RunStart = 2;   // no such runs
    content.blocks.back().RunCount = 4;
    content.add(ImTypesetBlockKind_Table, "a|b");
    content.blocks.back().TableRows = 1;
    content.add(99, "an unknown kind");
    const ImTypesetDocumentContent view = content.view();
    CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 3);
    CHECK(ImTypeset::LayoutDocument(document, 300.0f));
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 10000.0f));
    CHECK(ImTypeset::GetDocumentBlockGeometry(document, 0).LineCount >= 1);
    // The table is an empty host block for now, and says so.
    const ImTypesetBlockGeometry table = ImTypeset::GetDocumentBlockGeometry(document, 1);
    CHECK_EQ(table.FullMax.y - table.FullMin.y, 0.0f);
    CHECK_EQ(copied(document, ImTypesetPosition{2, 0}, ImTypesetPosition{2, 100}),
             std::string("an unknown kind"));
    // Lengths nobody vetted: clamped, never a crash and never a negative size.
    CHECK(ImTypeset::LayoutDocument(document, std::nanf("")));
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    CHECK(ImTypeset::LayoutDocument(document, -5.0f));
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    CHECK(ImTypeset::LayoutDocument(document, 1.0e30f));
    CHECK(draw_window(document, buffers, 0.0f, 600.0f));
    CHECK(ImTypeset::GetDocumentSize(document).y >= 0.0f);
    CHECK(!ImTypeset::SetDocumentBlockExtent(document, 0, 10.0f));
    CHECK(!ImTypeset::SetDocumentBlockExtent(document, 77, 10.0f));
    ImTypeset::DestroyDocument(document);
    ImTypeset::DestroyDocument(nullptr);
}

TEST(a_host_block_changes_height_without_anything_else_being_laid_out)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content = paragraphs(50);
    content.add(ImTypesetBlockKind_Host, "widget").Extent = 30.0f;
    content.add(ImTypesetBlockKind_Paragraph, "after the widget");
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 480.0f));
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 100000.0f));
    const float height = ImTypeset::GetDocumentSize(document).y;
    const ImTypesetBlockGeometry last = ImTypeset::GetDocumentBlockGeometry(document, 51);
    const ImTypesetDocumentStats before = ImTypeset::GetDocumentStats(document);
    CHECK(ImTypeset::SetDocumentBlockExtent(document, 50, 130.0f));
    CHECK(ImTypeset::LayoutDocumentWindow(document, 0.0f, 100000.0f));
    const ImTypesetDocumentStats after = ImTypeset::GetDocumentStats(document);
    CHECK_NEAR(ImTypeset::GetDocumentSize(document).y, height + 100.0f, 1.01f);
    CHECK_NEAR(ImTypeset::GetDocumentBlockGeometry(document, 51).FullMin.y, last.FullMin.y + 100.0f,
               0.01f);
    CHECK_EQ(after.BlocksLaidOut - before.BlocksLaidOut, ImU64(1));
    CHECK_EQ(after.MaterialBuilds, before.MaterialBuilds);
    CHECK_EQ(after.StructureVisits, before.StructureVisits);
    ImTypeset::DestroyDocument(document);
}

// --- tests/golden/blocks through the public calls -----------------------------------
//
// The reference's blocks are described through ImTypesetBlock and its
// companions and handed to SetDocumentBlocks(); the document is laid out by
// LayoutDocumentWindow(); and the block layer underneath is flattened and
// compared with the reference's commands, exactly.
//
// Two things stand between a public description and the reference's numbers,
// and the DocumentHook supplies both: the reference's rules (the public
// default is the product's), and scaled points. The reference's lengths are
// integers of scaled points and the header's are float pixels, which do not
// carry every one of them, so the hook replaces each length the description
// came to by the golden's, AFTER checking that the two are within two scaled
// points (1/32768 of a point): what the public description says is the
// reference's block, to the precision a float has.

namespace
{

bool space_at(const std::string& text, bool at_end)
{
    if (text.empty())
        return false;
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    return is_space(at_end ? cps.back() : cps.front());
}

bool all_space(const std::string& text)
{
    std::vector<uint32_t> cps;
    decode_utf8(text, cps);
    for (const uint32_t c : cps) {
        if (!is_space(c))
            return false;
    }
    return true;
}

struct Replay {
    int cases = 0;
    int compositions = 0;
    int blocks = 0;
    size_t cmds = 0;
    int lengths = 0;
    int lengths_exact = 0;
    std::map<std::string, int> skipped;
};

struct HookData {
    const std::vector<blocks_golden::BlockIn>* inputs = nullptr;
    const blocks_golden::ThemeIn* theme = nullptr;
    std::vector<std::vector<FontSpec>> run_fonts;
    Replay* tally = nullptr;
};

// One length as the public description resolved it against the golden's.
void take(HookData& data, Scaled& resolved, Scaled exact, const char* what)
{
    ++data.tally->lengths;
    if (resolved == exact)
        ++data.tally->lengths_exact;
    const Scaled apart = resolved > exact ? resolved - exact : exact - resolved;
    if (apart > 2)
        FAIL("%s: the description came to %lld sp and the reference has %lld", what,
             static_cast<long long>(resolved), static_cast<long long>(exact));
    resolved = exact;
}

void golden_block(void* user, int number, BlockSpec& spec)
{
    HookData& data = *static_cast<HookData*>(user);
    if (number < 0 || static_cast<size_t>(number) >= data.inputs->size())
        return;
    const blocks_golden::BlockIn& in = (*data.inputs)[static_cast<size_t>(number)];
    CHECK_CONTEXT("block %d (line %d)", number, in.line);
    CHECK(spec.type == in.type);
    take(data, spec.left_indent, in.left_indent, "left indent");
    take(data, spec.space_above, in.space_above, "space above");
    take(data, spec.space_below, in.space_below, "space below");
    if (in.type == BlockType::RULE) {
        take(data, spec.extent, in.thickness, "thickness");
        return;
    }
    const FontSpec font = blocks_golden::spec_of(in.font);
    CHECK(spec.font.face == font.face);
    take(data, spec.font.size, font.size, "size");
    take(data, spec.interline.baselineskip.width, in.interline.baselineskip.width, "leading");
    take(data, spec.interline.lineskip.width, in.interline.lineskip.width, "lineskip");
    spec.interline = in.interline;
    spec.strut = in.strut;
    if (in.type == BlockType::TEXT) {
        CHECK(spec.align == in.align);
        take(data, spec.first_line_indent, in.first_line_indent, "first-line indent");
        // The reference narrows the first line by the indent as well as
        // putting the box there (docs/divergences.md); the product does not.
        spec.first_line = FirstLineIndent::BOX_AND_SHAPE;
        CHECK_EQ(spec.quote_depth, in.quote_depth);
    }
    take(data, spec.marker_gap, data.theme->marker_gap, "marker gap");
    if (in.has_marker) {
        CHECK(spec.marker_font.face == in.marker_font.face);
        take(data, spec.marker_font.size, in.marker_font.size, "marker size");
    }
}

void golden_run(void* user, int number, int run_index, FontSpec& font)
{
    HookData& data = *static_cast<HookData*>(user);
    if (number < 0 || static_cast<size_t>(number) >= data.run_fonts.size())
        return;
    const std::vector<FontSpec>& fonts = data.run_fonts[static_cast<size_t>(number)];
    if (!CHECK(run_index >= 0 && static_cast<size_t>(run_index) < fonts.size()))
        return;
    const FontSpec exact = fonts[static_cast<size_t>(run_index)];
    CHECK(font.face == exact.face);
    take(data, font.size, exact.size, "run size");
}

float px_float(Scaled length, double ppi)
{
    return static_cast<float>(px_from_sp(length, ppi));
}

// One `blocks` case. Returns why it cannot be described, or null.
const char* replay_blocks_case(Stage& stage, const golden::File& file, const core_golden::Case& c,
                               Replay& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");
    const blocks_golden::ThemeIn theme = blocks_golden::read_theme(reader);
    blocks_golden::Recorded recorded;
    recorded.read(reader, context);
    core_golden::read_measure(reader, context);
    const std::vector<blocks_golden::BlockIn> inputs = blocks_golden::read_blocks(reader, context);
    if (theme.composition.regular != Face::PROSE || theme.composition.code != Face::CODE)
        return "a theme whose stand-in faces are not the defaults";

    const double ppi = theme.px_per_inch;
    // The document's size: the first block's that has one. Every block says
    // its own as a multiple of it.
    Scaled base = 0;
    for (const blocks_golden::BlockIn& in : inputs) {
        if (in.type != BlockType::RULE && base == 0)
            base = blocks_golden::spec_of(in.font).size;
    }
    if (base == 0)
        base = 12 * UNITY;

    ImTypesetStyle style;
    style.PixelsPerInch = static_cast<float>(ppi);
    style.EmSize = px_float(base, ppi);
    style.Spacing =
        theme.composition.spacing == Spacing::FRENCH
            ? ImTypesetSpacing_French
            : (theme.composition.spacing == Spacing::NONFRENCH ? ImTypesetSpacing_TeX
                                                               : ImTypesetSpacing_Sentence);

    HookData data;
    data.inputs = &inputs;
    data.theme = &theme;
    data.tally = &tally;
    data.run_fonts.resize(inputs.size());

    Content content;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const blocks_golden::BlockIn& in = inputs[i];
        if (in.space_above < 0 || in.space_below < 0)
            return "a negative space, which the header does not take";
        ImTypesetBlockStyle own;
        own.SpaceAbove = px_float(in.space_above, ppi);
        own.SpaceBelow = px_float(in.space_below, ppi);
        own.MarkerGap = px_float(theme.marker_gap, ppi);
        if (in.type == BlockType::RULE) {
            content.styles.push_back(own);
            ImTypesetBlock& block = content.add(ImTypesetBlockKind_Rule, "", static_cast<int>(i));
            block.LeftIndent = px_float(in.left_indent, ppi);
            block.Extent = px_float(in.thickness, ppi);
            if (in.thickness <= 0)
                return "a rule of no thickness, which the header reads as the default";
            continue;
        }
        const FontSpec font = blocks_golden::spec_of(in.font);
        own.Face = static_cast<ImTypesetFace>(font.face) + 1;
        own.Scale = static_cast<float>(static_cast<double>(font.size) / static_cast<double>(base));
        own.LineHeight = static_cast<float>(static_cast<double>(in.interline.baselineskip.width) /
                                            static_cast<double>(font.size));
        own.Align = in.align == Align::LEFT     ? ImTypesetAlign_Left
                    : in.align == Align::CENTER ? ImTypesetAlign_Center
                    : in.align == Align::RIGHT  ? ImTypesetAlign_Right
                                                : ImTypesetAlign_Justify;
        own.FirstLineIndent = px_float(in.first_line_indent, ppi);
        if (in.has_marker)
            own.MarkerFace = static_cast<ImTypesetFace>(in.marker_font.face) + 1;
        if (in.has_marker && in.marker_font.size != font.size)
            return "a marker of another size than its block";
        content.styles.push_back(own);

        if (in.type == BlockType::CODE) {
            ImTypesetBlock& block =
                content.add(ImTypesetBlockKind_Code, in.source, static_cast<int>(i));
            block.LeftIndent = px_float(in.left_indent, ppi);
            continue;
        }
        // The text: the runs' texts end to end, a hard break as a newline.
        std::string text;
        struct Piece {
            int start;
            int end;
            const blocks_golden::Event* event;
        };
        std::vector<Piece> pieces;
        bool any_hard = false;
        for (const blocks_golden::Event& event : in.events)
            any_hard = any_hard || event.hard_break;
        bool nothing_yet = true;
        bool after_space = false;
        bool line_start = false;
        for (const blocks_golden::Event& event : in.events) {
            if (event.hard_break) {
                if (after_space)
                    return "white space before a hard break, which the API drops";
                text += "\n";
                line_start = true;
                after_space = false;
                continue;
            }
            if (any_hard && event.text.find_first_of("\r\n") != std::string::npos)
                return "a newline inside a run beside hard breaks";
            if (event.text.empty())
                continue;
            if (space_at(event.text, false) && (nothing_yet || after_space || line_start))
                return "white space the API collapses and the reference's raw runs do not";
            pieces.push_back(Piece{static_cast<int>(text.size()),
                                   static_cast<int>(text.size() + event.text.size()), &event});
            text += event.text;
            if (!all_space(event.text)) {
                nothing_yet = false;
                line_start = false;
            }
            after_space = space_at(event.text, true);
        }
        if (after_space)
            return "white space the API collapses and the reference's raw runs do not";
        const bool quoted_or_listed = in.kind != "paragraph";
        ImTypesetBlock& block =
            content.add(quoted_or_listed && in.has_marker ? ImTypesetBlockKind_ListItem
                                                          : ImTypesetBlockKind_Paragraph,
                        text, static_cast<int>(i));
        block.LeftIndent = px_float(in.left_indent, ppi);
        block.QuoteDepth = static_cast<int>(in.quote_depth);
        if (any_hard)
            block.Flags |= ImTypesetBlockFlags_HardBreaks;
        for (const Piece& piece : pieces) {
            ImTypesetRun& run = content.run(piece.start, piece.end);
            run.Face = static_cast<ImTypesetFace>(piece.event->font.face) + 1;
            if (piece.event->font.size != font.size)
                run.Scale = static_cast<float>(static_cast<double>(piece.event->font.size) /
                                               static_cast<double>(font.size));
            if (!piece.event->breakable)
                run.Flags |= ImTypesetRunFlags_NoBreak;
            data.run_fonts[i].push_back(piece.event->font);
        }
        if (in.has_marker)
            content.marker(in.marker_text);
    }

    ImTypesetDocument* document = ImTypeset::CreateDocument();
    if (document == nullptr)
        return "no document";
    DocumentHook hook;
    hook.reference_rules = true;
    hook.quote_indent = theme.theme.quote_indent;
    hook.quote_rule_thickness = theme.theme.quote_rule_thickness;
    hook.block = &golden_block;
    hook.run = &golden_run;
    hook.user = &data;
    set_document_hook(*document, hook);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), static_cast<int>(inputs.size()));
    const ResolvedStyle resolved = resolve_style(*stage.ctx, style);

    bool any = false;
    std::vector<DrawCmd> flat;
    const std::vector<golden::Record>& records = file.records();
    for (size_t at = reader.position(); at < c.end; ++at) {
        if (!records[at].is("compose"))
            continue;
        const Scaled width = records[at].i64(1);
        CHECK_CONTEXT("compose %s (line %d)", records[at].str(0).c_str(), records[at].line);
        size_t cmds_at = at + 1;
        Scaled vbox_extent = -1;
        while (cmds_at < c.end && !records[cmds_at].is("cmds") &&
               !records[cmds_at].is("endcompose")) {
            if (records[cmds_at].is("vbox"))
                vbox_extent = records[cmds_at].i64(1) + records[cmds_at].i64(2);
            ++cmds_at;
        }
        if (!CHECK(cmds_at < c.end && records[cmds_at].is("cmds")))
            break;
        core_golden::Case tail = c;
        tail.first = cmds_at;
        core_golden::Reader cmds_reader(file, tail);
        core_golden::read_cmds(cmds_reader, context);
        const std::vector<std::string> expected =
            cmds_reader.lines(cmds_at, cmds_reader.position());
        at = cmds_reader.position();

        const float width_px = px_float(width, ppi);
        if (resolve_width(width_px, resolved) != width) {
            ++tally.skipped["a width that is not a float number of pixels"];
            continue;
        }
        CHECK(ImTypeset::LayoutDocument(document, width_px, &style));
        const bool laid = ImTypeset::LayoutDocumentWindow(document, 0.0f, 1.0e9f);
        if (!laid)
            FAIL("LayoutDocumentWindow: %s", ImTypeset::GetLastError()->Message);
        Document& inner = block_document(*document);
        if (!CHECK(inner.settled()))
            continue;
        if (vbox_extent >= 0)
            CHECK_EQ(inner.height(), vbox_extent);
        flat.clear();
        if (!CHECK(inner.flatten(flat)))
            continue;
        CHECK_LINES_EQ(expected, blocks_golden::written(context, flat),
                       "the document, described through the public header and flattened");
        any = true;
        ++tally.compositions;
        tally.cmds += flat.size();
    }
    ImTypeset::DestroyDocument(document);
    if (any) {
        ++tally.cases;
        tally.blocks += static_cast<int>(inputs.size());
    }
    return nullptr;
}

} // namespace

TEST(the_block_goldens_described_through_the_public_header_compose_to_the_references_commands)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // The goldens are the reference's: its departures from tex.web stay in.
    stage.ctx->tex_rules = TexRules::reference();
    Frame frame;
    const std::vector<std::string> files = golden::list("blocks");
    REQUIRE(!files.empty());
    Replay tally;
    int total = 0;
    for (const std::string& path : files) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!CHECK(file.load(path)))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            if (c.kind != "blocks")
                continue;
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            ++total;
            if (const char* why = replay_blocks_case(stage, file, c, tally))
                ++tally.skipped[why];
        }
    }
    std::printf("document: %d of %d block cases through the public header: %d compositions, %d "
                "blocks, %zu commands; %d of %d lengths came to the reference's scaled point "
                "exactly, the rest within 2 sp\n",
                tally.cases, total, tally.compositions, tally.blocks, tally.cmds,
                tally.lengths_exact, tally.lengths);
    for (const auto& entry : tally.skipped)
        std::printf("document:   not expressible (%d): %s\n", entry.second, entry.first.c_str());
    // A replay that quietly shrank must not read as a pass.
    CHECK_EQ(total, 69);
    CHECK(tally.cases >= 64);
    CHECK(tally.compositions >= 200);
    CHECK(tally.cmds > 34000);
}

namespace
{

// How many vertices of a colour a draw made, and the box they span.
struct Coloured {
    int count = 0;
    float min_x = 1.0e9f;
    float min_y = 1.0e9f;
    float max_x = -1.0e9f;
    float max_y = -1.0e9f;
};

Coloured coloured(const Buffers& buffers, ImU32 colour)
{
    Coloured out;
    for (const ImDrawVert& v : buffers.vertices) {
        if (v.col != colour)
            continue;
        ++out.count;
        out.min_x = std::min(out.min_x, v.pos.x);
        out.min_y = std::min(out.min_y, v.pos.y);
        out.max_x = std::max(out.max_x, v.pos.x);
        out.max_y = std::max(out.max_y, v.pos.y);
    }
    return out;
}

} // namespace

TEST(marks_are_drawn_in_the_order_given_and_an_underline_has_its_own_thickness)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content;
    content.add(ImTypesetBlockKind_Paragraph, "aaaa bbbb");
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 400.0f));
    const ImU32 red = IM_COL32(255, 0, 0, 255);
    const ImU32 green = IM_COL32(0, 255, 0, 255);
    const ImU32 blue = IM_COL32(0, 0, 255, 255);
    const ImU32 yellow = IM_COL32(255, 255, 0, 255);
    ImTypesetMark marks[4];
    marks[0].Range = ImTypesetDocumentRange{{0, 0}, {0, 6}}; // "aaaa b"
    marks[0].Color = red;
    marks[1].Range = ImTypesetDocumentRange{{0, 2}, {0, 4}}; // the last two of them, over it
    marks[1].Color = green;
    marks[2].Range = ImTypesetDocumentRange{{0, 5}, {0, 9}}; // "bbbb"
    marks[2].Place = ImTypesetMarkPlace_Under;
    marks[2].Color = blue;
    marks[2].Thickness = 3.0f;
    marks[2].Offset = 2.0f;
    marks[3].Range = ImTypesetDocumentRange{{0, 0}, {0, 4}};
    marks[3].Place = ImTypesetMarkPlace_Behind;
    marks[3].Color = yellow;
    ImTypesetDocumentDraw with;
    with.Marks = marks;
    with.MarkCount = 4;
    Buffers buffers;
    CHECK(draw_window(document, buffers, 0.0f, 600.0f, &with));
    // Two glyphs are the later mark's, three the earlier one's, three nobody's.
    CHECK_EQ(coloured(buffers, green).count, 8);
    CHECK_EQ(coloured(buffers, red).count, 12);
    CHECK_EQ(coloured(buffers, IM_COL32(0x11, 0x22, 0x33, 0xFF)).count, 12);
    // The underline: one quad three pixels thick, under the word it marks and
    // below every glyph's baseline; the fill: one quad behind the first word.
    const Coloured under = coloured(buffers, blue);
    CHECK_EQ(under.count, 4);
    CHECK_EQ(under.max_y - under.min_y, 3.0f);
    const Coloured behind = coloured(buffers, yellow);
    CHECK_EQ(behind.count, 4);
    CHECK(behind.max_x <= under.min_x);
    CHECK(under.min_y > behind.min_y && under.min_y < behind.max_y + 4.0f);
    const ImVec4 caret = ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{0, 5});
    CHECK_NEAR(under.min_x, 10.0f + caret.x, 1.01f);
    // A mark that names no text of the document draws nothing and breaks nothing.
    marks[0].Range = ImTypesetDocumentRange{{5, 0}, {9, 6}};
    marks[1].Range = ImTypesetDocumentRange{{0, 4}, {0, 2}}; // backwards: empty
    CHECK(draw_window(document, buffers, 0.0f, 600.0f, &with));
    CHECK_EQ(coloured(buffers, green).count, 0);
    CHECK_EQ(coloured(buffers, red).count, 0);
    ImTypeset::DestroyDocument(document);
}

TEST(quotes_have_their_bars_and_a_tight_list_its_marker_gutter)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    Content content;
    ImTypesetBlockStyle body;
    body.SpaceBelow = 10.0f;
    body.Align = ImTypesetAlign_Left;
    body.MarkerColor = IM_COL32(9, 9, 9, 255);
    content.styles.push_back(body);
    ImTypesetContainer quote;
    quote.Kind = ImTypesetContainerKind_Quote;
    quote.Indent = 4.0f;
    quote.Color = IM_COL32(200, 0, 0, 255);
    ImTypesetContainer second = quote;
    second.Color = IM_COL32(0, 0, 200, 255);
    ImTypesetContainer list;
    list.Kind = ImTypesetContainerKind_List;
    list.Indent = 20.0f;
    list.Flags = ImTypesetContainerFlags_Tight;
    content.containers = {quote, second, list};
    content.add(ImTypesetBlockKind_Paragraph, "Before.");
    for (int i = 0; i < 2; ++i) { // 1, 2: the first quote
        ImTypesetBlock& block = content.add(ImTypesetBlockKind_Paragraph, "Quoted once.");
        block.Container = 1;
        block.LeftIndent = 20.0f;
    }
    ImTypesetBlock& other = content.add(ImTypesetBlockKind_Paragraph, "Another quote."); // 3
    other.Container = 2;
    other.LeftIndent = 20.0f;
    for (int i = 0; i < 3; ++i) { // 4, 5, 6
        ImTypesetBlock& item = content.add(ImTypesetBlockKind_ListItem, "An item of the list.");
        item.Container = 3;
        item.Item = i;
        item.LeftIndent = 20.0f;
        // The last marker is wider than the gutter the list said it had.
        content.marker(i == 2 ? "(iii)" : "-");
    }
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    ImTypesetDocumentOptions options;
    options.QuoteBarThickness = 3.0f;
    CHECK(ImTypeset::SetDocumentOptions(document, &options));
    CHECK(ImTypeset::LayoutDocument(document, 400.0f));
    Buffers buffers;
    CHECK(draw_window(document, buffers, 0.0f, 1000.0f));

    // Two quotes in a row are two bars, each its own colour, each as tall as
    // its blocks and where its quote said.
    const Coloured first_bar = coloured(buffers, IM_COL32(200, 0, 0, 255));
    const Coloured second_bar = coloured(buffers, IM_COL32(0, 0, 200, 255));
    CHECK_EQ(first_bar.count, 4);
    CHECK_EQ(second_bar.count, 4);
    CHECK_EQ(first_bar.max_x - first_bar.min_x, 3.0f);
    CHECK_NEAR(first_bar.min_x, 10.0f + 4.0f, 0.51f);
    const ImTypesetBlockGeometry q1 = ImTypeset::GetDocumentBlockGeometry(document, 1);
    const ImTypesetBlockGeometry q2 = ImTypeset::GetDocumentBlockGeometry(document, 2);
    const ImTypesetBlockGeometry q3 = ImTypeset::GetDocumentBlockGeometry(document, 3);
    CHECK_NEAR(first_bar.max_y - first_bar.min_y, q2.FullMax.y - q1.FullMin.y, 1.01f);
    CHECK_NEAR(second_bar.max_y - second_bar.min_y, q3.FullMax.y - q3.FullMin.y, 1.01f);
    CHECK(second_bar.min_y >= first_bar.max_y);

    // The list is tight: its items touch. Its widest marker does not fit the
    // gutter, so every item moves right by the same amount to make room.
    const ImTypesetBlockGeometry i0 = ImTypeset::GetDocumentBlockGeometry(document, 4);
    const ImTypesetBlockGeometry i1 = ImTypeset::GetDocumentBlockGeometry(document, 5);
    const ImTypesetBlockGeometry i2 = ImTypeset::GetDocumentBlockGeometry(document, 6);
    CHECK_NEAR(i1.FullMin.y, i0.FullMax.y, 0.01f);
    CHECK_NEAR(i2.FullMin.y, i1.FullMax.y, 0.01f);
    CHECK(i0.FullMin.x > 20.5f);
    CHECK_EQ(i0.FullMin.x, i2.FullMin.x);
    // The markers are drawn in their own colour, left of the text, and are not text.
    const Coloured markers = coloured(buffers, IM_COL32(9, 9, 9, 255));
    CHECK_EQ(markers.count, 4 * (1 + 1 + 5));
    CHECK(markers.max_x <= 10.0f + i0.FullMin.x + 1.0f);
    CHECK_EQ(copied(document, ImTypesetPosition{4, 0}, ImTypesetPosition{6, 1000}, "|"),
             std::string("An item of the list.|An item of the list.|An item of the list."));
    ImTypeset::DestroyDocument(document);
}

TEST(the_view_scrolls_a_document_in_a_child_and_lays_out_only_what_it_shows)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const Content content = paragraphs(2000);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    // Outside a frame it is refused.
    CHECK(!ImTypeset::DocumentView("doc", document));
    ImTypesetDocumentRange selection = {{0, 2}, {1, 7}};
    for (int frame_number = 0; frame_number < 4; ++frame_number) {
        Frame frame;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
        ImGui::Begin("host", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        CHECK(ImTypeset::DocumentView("doc", document, ImVec2(0.0f, 0.0f), &selection));
        ImGui::End();
    }
    // Four frames of a 400-pixel window: a screenful, not two thousand blocks.
    const ImTypesetDocumentStats stats = ImTypeset::GetDocumentStats(document);
    CHECK(stats.BlocksLaidOut >= 3 && stats.BlocksLaidOut <= 20);
    CHECK(stats.BlocksDrawn >= 3);
    // (The scroll bar appears after the first frame and narrows the column,
    // so what is in view is laid out twice.)
    CHECK(stats.BlocksDeferred >= 1980 && stats.BlocksDeferred <= 1997);
    // The selection the host keeps is the selection, and came back unchanged.
    CHECK_EQ(selection.Start.Offset, 2);
    CHECK_EQ(selection.End.Block, 1);
    ImTypeset::DestroyDocument(document);
}

// Nothing a host hands over is trusted. Every count, offset, index and length
// below is wrong in some way; none may read outside what was given, loop
// without end or ask for memory in proportion to a number that names none.
// The document is small by construction: the largest count that is honoured
// is the 64 blocks actually pointed at.
TEST(a_description_nobody_vetted_is_bounded_by_what_was_actually_handed_over)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const float NAN_F = std::nanf("");
    const float INF_F = std::numeric_limits<float>::infinity();

    // Counts with no arrays behind them, and negative ones.
    ImTypesetDocumentContent empty;
    empty.BlockCount = 1 << 30;
    empty.RunCount = 1 << 30;
    empty.StyleCount = 1 << 30;
    empty.ContainerCount = 1 << 30;
    empty.TextSize = 1 << 30;
    CHECK(ImTypeset::SetDocumentBlocks(document, &empty));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 0);
    CHECK_EQ(ImTypeset::GetDocumentStyleCount(document), 0);
    empty.BlockCount = -5;
    empty.TextSize = -5;
    CHECK(ImTypeset::SetDocumentBlocks(document, &empty));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 0);

    Content content;
    ImTypesetBlockStyle wild;
    wild.Scale = INF_F;
    wild.LineHeight = NAN_F;
    wild.Align = 99;
    wild.Face = 99;
    wild.FirstLineIndent = -INF_F;
    wild.SpaceAbove = 1.0e30f;
    wild.SpaceBelow = NAN_F;
    wild.MarkerGap = -1.0f;
    wild.MarkerFace = -3;
    content.styles.push_back(wild);
    ImTypesetContainer loop;
    loop.Kind = 7;
    loop.Parent = 1; // itself
    loop.Indent = NAN_F;
    ImTypesetContainer forward = loop;
    forward.Parent = 50; // not one
    ImTypesetContainer negative = loop;
    negative.Parent = -4;
    content.containers = {loop, forward, negative};
    for (int i = 0; i < 64; ++i) {
        ImTypesetBlock& block = content.add(i % 7, "hostile \xff\xfe text $x^{$ \t\r\n\n end");
        block.Flags = -1;
        block.Level = 1 << 30;
        block.LeftIndent = i % 2 == 0 ? 1.0e30f : NAN_F;
        block.QuoteDepth = i % 3 == 0 ? 1 << 30 : -7;
        block.Container = i % 5 - 1; // -1, 0, 1, 2, 3
        block.Item = -(1 << 30);
        block.Extent = i % 4 == 0 ? INF_F : -3.0f;
        block.TableRows = 1 << 30;
        block.TableColumns = -1;
        block.MarkerStart = -100;
        block.MarkerEnd = 1 << 30;
        block.RunStart = i % 2 == 0 ? -3 : 1 << 30;
        block.RunCount = 1 << 30;
        if (i % 8 == 1) {
            block.TextStart = 1 << 30;
            block.TextEnd = -(1 << 30);
        }
    }
    // Runs that overlap, run backwards and point outside the block.
    for (int i = 0; i < 16; ++i) {
        ImTypesetRun run;
        run.Start = 20 - 3 * i;
        run.End = i % 2 == 0 ? 1 << 30 : -5;
        run.Face = 1000;
        run.Scale = NAN_F;
        run.Flags = -1;
        content.runs.push_back(run);
    }
    content.blocks[10].RunStart = 0;
    content.blocks[10].RunCount = 16;
    const ImTypesetDocumentContent view = content.view();
    CHECK(!ImTypeset::SetDocumentBlocks(document, &view));
    CHECK_EQ(ImTypeset::GetLastError()->Code, static_cast<int>(ImTypesetError_DescriptionCut));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 64);

    ImTypesetDocumentOptions options;
    options.QuoteIndent = NAN_F;
    options.QuoteBarThickness = INF_F;
    options.CodeOverflow = 77;
    CHECK(ImTypeset::SetDocumentOptions(document, &options));
    const float WIDTHS[] = {300.0f, 0.0f, -1.0f, NAN_F, INF_F, 1.0e30f, 0.01f};
    Buffers buffers;
    for (const float width : WIDTHS) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        CHECK(ImTypeset::LayoutDocument(document, width));
        (void)ImTypeset::LayoutDocumentWindow(document, -INF_F, INF_F);
        (void)ImTypeset::LayoutDocumentWindow(document, NAN_F, NAN_F);
        const ImVec2 size = ImTypeset::GetDocumentSize(document);
        CHECK(size.x >= 0.0f && size.y >= 0.0f);
        CHECK(std::isfinite(size.x) && std::isfinite(size.y));
        buffers.vertices.resize(0);
        buffers.indices.resize(0);
        (void)ImTypeset::DrawDocumentToBuffers(document, ImVec2(NAN_F, -INF_F),
                                               ImVec4(-INF_F, NAN_F, INF_F, INF_F),
                                               &buffers.vertices, &buffers.indices);
        (void)draw_window(document, buffers, 0.0f, 400.0f);
        for (const ImDrawVert& v : buffers.vertices)
            CHECK(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));
        (void)ImTypeset::HitTestDocument(document, ImVec2(NAN_F, INF_F));
        (void)ImTypeset::HitTestDocument(document, ImVec2(-1.0e30f, -1.0e30f));
        const ImTypesetDocumentRange all = {{-(1 << 30), -5}, {1 << 30, 1 << 30}};
        ImGuiTextBuffer text;
        CHECK(ImTypeset::AppendDocumentRangeText(document, all, &text) >= 0);
        ImVector<ImVec4> rects;
        CHECK(ImTypeset::GetDocumentRangeRects(document, all, &rects) >= 0);
        for (const ImVec4& rect : rects)
            CHECK(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.z) &&
                  std::isfinite(rect.w));
        for (int block = -2; block < 66; ++block) {
            (void)ImTypeset::GetDocumentBlockGeometry(document, block);
            (void)ImTypeset::GetDocumentBlockLine(document, block, 1 << 30);
            (void)ImTypeset::GetDocumentCaretRect(document, ImTypesetPosition{block, 1 << 30});
            CHECK(ImTypeset::GetDocumentBlockError(document, block) != nullptr);
            (void)ImTypeset::SetDocumentBlockExtent(document, block,
                                                    block % 2 == 0 ? NAN_F : INF_F);
        }
    }
    // Marks and a selection that name nothing real.
    ImTypesetMark marks[3];
    marks[0].Range = ImTypesetDocumentRange{{-9, -9}, {1 << 30, 1 << 30}};
    marks[0].Place = 55;
    marks[0].Thickness = NAN_F;
    marks[1].Range = ImTypesetDocumentRange{{3, 1 << 30}, {3, -4}};
    marks[2].Range = ImTypesetDocumentRange{{2, 1}, {40, 2}};
    marks[2].Place = ImTypesetMarkPlace_Under;
    marks[2].Thickness = INF_F;
    marks[2].Offset = -INF_F;
    ImTypesetDocumentDraw with;
    with.Selection = ImTypesetDocumentRange{{1 << 30, 0}, {-3, 2}};
    with.Marks = marks;
    with.MarkCount = 3;
    with.Flags = -1;
    CHECK(ImTypeset::LayoutDocument(document, 300.0f));
    (void)draw_window(document, buffers, 0.0f, 400.0f, &with);
    with.Marks = nullptr;
    with.MarkCount = 1 << 30;
    (void)draw_window(document, buffers, 0.0f, 400.0f, &with);

    // Edits at places that are not places.
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, 1 << 30, 1, &view));
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, 0, -1, &view));
    CHECK(!ImTypeset::ReplaceDocumentBlocks(document, 60, 1 << 30, nullptr));
    CHECK_EQ(ImTypeset::GetDocumentBlockCount(document), 64);
    (void)ImTypeset::AppendDocumentText(document, nullptr, nullptr, content.runs.data(), 16);
    (void)ImTypeset::AppendDocumentText(document, "x", nullptr, nullptr, 1 << 30);
    // An end before its beginning is no text.
    static const char BACKWARDS[] = "abc";
    (void)ImTypeset::AppendDocumentText(document, BACKWARDS + 2, BACKWARDS, nullptr, -1);
    ImTypeset::DestroyDocument(document);
}

// A question about the whole document lays the whole document out, which is
// said; it must not also keep, for every block, what a question about one
// block needs. 3,000 blocks of about 120 bytes.
TEST(a_range_over_the_whole_document_keeps_a_bounded_number_of_views)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const int count = 3000;
    const Content content = paragraphs(count);
    ImTypesetDocument* document = ImTypeset::CreateDocument();
    REQUIRE(document != nullptr);
    const ImTypesetDocumentContent view = content.view();
    CHECK(ImTypeset::SetDocumentBlocks(document, &view));
    CHECK(ImTypeset::LayoutDocument(document, 480.0f));
    const ImTypesetPosition start = {0, 0};
    const ImTypesetPosition end = {count - 1, 1 << 20};
    const std::string all = copied(document, start, end, "\n");
    // Every block's text, each once, with one separator between two.
    size_t expected = 0;
    for (int i = 0; i < count; ++i)
        expected += std::to_string(i).size() + 1 + std::strlen(SENTENCE);
    CHECK_EQ(all.size(), expected + static_cast<size_t>(count - 1));
    CHECK_EQ(ImTypeset::GetDocumentStats(document).BlocksDeferred, 0);
    ImVector<ImVec4> rects;
    const ImTypesetDocumentRange range = {start, end};
    CHECK(ImTypeset::GetDocumentRangeRects(document, range, &rects) >= count);
    // Drawn with all of it selected, a screenful at a time.
    ImTypesetDocumentDraw with;
    with.Selection = range;
    Buffers buffers;
    for (int step = 0; step < 20; ++step)
        CHECK(draw_window(document, buffers, 2000.0f * static_cast<float>(step), 600.0f, &with));
    CHECK(document->views.size() <= 64);
    std::printf("document: %zu views held after a copy, the rectangles and 20 windows of a "
                "selection over %d blocks\n",
                document->views.size(), count);
    ImTypeset::DestroyDocument(document);
}
