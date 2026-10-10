// What a frame costs once layout is done: shipped commands to vertices.
//
// The document is bench/data/sample.txt (see bench_common.h), one
// ImTypesetParagraph per paragraph, set as the reference sets its body text and
// at its measure, stacked a paragraph skip apart, and concatenated 1, 10 and
// 100 times. The reference's benchmark, scripts/bench_draw.py, takes a
// `--source`; handed the same file it ships the same paragraphs, so the two
// sets of rows describe one page.
//
//   full draw            DrawParagraph for every paragraph, nothing clipped
//   culled, host skips   a 768 px window: the host finds the first paragraph
//                        in it by binary search over the tops it keeps, and
//                        stops at the first one below. This is what the
//                        reference's CommandIndex.visible does before its draw.
//   culled, all offered  the same window with every paragraph handed to
//                        DrawParagraph: the library skips by line, but is
//                        still called once per paragraph
//   widget frame         Begin, TextUnformatted per paragraph, End: the hash
//                        of the text, the cache lookup, the text comparison,
//                        Dummy, IsRectVisible and the draw of what is visible;
//                        and the same with TextWithID, which does not hash
//
// Then where a full draw's time goes, on the same commands through
// detail::Drawer (draw list; the same calls made on ImGui one AddText at a
// time, which is ImGui's own share; placement with a binder that asks ImGui
// nothing; buffers), the same rows on a page of mixed faces and sizes tiled
// from tests/golden/draw, and hit testing and selection.
//
// Take numbers from a Release build only.

// The golden reader comes with the test harness, which would supply a main().
#define IMTYPESET_CHECK_NO_MAIN
#include "core_golden.h"
#include "support/imgui_compat.h"

#include "bench_common.h"

#include "context.h"
#include "draw.h"
#include "font_loader.h"
#include "fontlib.h"
#include "style.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr float MARGIN_PX = 16.0f;

struct Document {
    std::vector<ImTypesetParagraph*> paragraphs;
    std::vector<float> tops;
    float height = 0.0f;
    int glyphs = 0;
    int lines = 0;
    size_t commands = 0;

    void destroy()
    {
        for (ImTypesetParagraph* p : paragraphs)
            ImTypeset::DestroyParagraph(p);
        paragraphs.clear();
    }
};

int glyphs_of(const ImTypesetParagraph* p)
{
    int count = 0;
    for (const DrawCmd& cmd : shipped_commands(*p))
        count += cmd.is(CmdType::GLYPH) ? 1 : 0;
    return count;
}

Document document_of(const std::vector<std::string>& texts, int copies, float width)
{
    Document doc;
    float y = 0.0f;
    for (int copy = 0; copy < copies; ++copy) {
        for (const std::string& text : texts) {
            ImTypesetParagraph* p = ImTypeset::CreateParagraph();
            ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
            ImTypeset::LayoutParagraph(p, width);
            const ImVec2 size = ImTypeset::GetParagraphSize(p);
            doc.paragraphs.push_back(p);
            doc.tops.push_back(y);
            y += size.y + bench::PARAGRAPH_SKIP_PX;
            doc.glyphs += glyphs_of(p);
            doc.lines += ImTypeset::GetParagraphLineCount(p);
            doc.commands += shipped_commands(*p).size();
        }
    }
    doc.height = y;
    return doc;
}

void draw_all(const Document& doc, ImDrawList* list)
{
    for (size_t i = 0; i < doc.paragraphs.size(); ++i)
        ImTypeset::DrawParagraph(doc.paragraphs[i], list,
                                 ImVec2(MARGIN_PX, MARGIN_PX + doc.tops[i]));
}

// The paragraphs a window [top, top + WINDOW_PX) of the page touches, found
// the way a host that keeps the tops would.
void draw_window(const Document& doc, ImDrawList* list, float top)
{
    const float bottom = top + bench::WINDOW_PX;
    size_t first = static_cast<size_t>(std::upper_bound(doc.tops.begin(), doc.tops.end(), top) -
                                       doc.tops.begin());
    if (first > 0)
        --first;
    for (size_t i = first; i < doc.paragraphs.size() && doc.tops[i] < bottom; ++i)
        ImTypeset::DrawParagraph(doc.paragraphs[i], list,
                                 ImVec2(MARGIN_PX, MARGIN_PX + doc.tops[i]));
}

std::string note_of(int glyphs, double median_us, const char* more = "")
{
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "%d glyphs, %.1f ns/glyph%s", glyphs,
                  glyphs > 0 ? median_us * 1000.0 / glyphs : 0.0, more);
    return buffer;
}

int drawn_since(ImU64 vertices_before)
{
    return static_cast<int>((ImTypeset::GetStats().VerticesEmitted - vertices_before) / 4);
}

void bench_document(const std::vector<std::string>& texts, int copies, float width, int frames)
{
    Document doc = document_of(texts, copies, width);
    char title[200];
    std::snprintf(title, sizeof title,
                  "The sample x%d: %zu paragraphs, %d lines, %d glyphs, %zu commands, %.0f px "
                  "tall",
                  copies, doc.paragraphs.size(), doc.lines, doc.glyphs, doc.commands, doc.height);
    bench::table_head(title);
    char what[160];

    {
        bench::Samples s =
            bench::sample_frames(frames, 5, [&](ImDrawList* list) { draw_all(doc, list); });
        std::snprintf(what, sizeof what, "full draw, x%d", copies);
        bench::row(what, s, note_of(doc.glyphs, s.median()).c_str());
    }

    static const float fractions[] = {0.0f, 0.25f, 0.5f, 0.75f};
    for (const float fraction : fractions) {
        const float top = MARGIN_PX + doc.height * fraction;
        int drawn = 0;
        bench::Samples s = bench::sample_frames(2000, 5, [&](ImDrawList* list) {
            const ImU64 before = ImTypeset::GetStats().VerticesEmitted;
            list->PushClipRect(ImVec2(0.0f, top), ImVec2(4000.0f, top + bench::WINDOW_PX));
            draw_window(doc, list, top - MARGIN_PX);
            list->PopClipRect();
            drawn = drawn_since(before);
        });
        std::snprintf(what, sizeof what, "culled, host skips, x%d, scroll %.2f", copies,
                      static_cast<double>(fraction));
        bench::row(what, s, note_of(drawn, s.median()).c_str());
    }

    {
        const float top = MARGIN_PX + doc.height * 0.25f;
        int drawn = 0;
        bench::Samples s =
            bench::sample_frames(copies >= 100 ? 500 : 2000, 5, [&](ImDrawList* list) {
                const ImU64 before = ImTypeset::GetStats().VerticesEmitted;
                list->PushClipRect(ImVec2(0.0f, top), ImVec2(4000.0f, top + bench::WINDOW_PX));
                draw_all(doc, list);
                list->PopClipRect();
                drawn = drawn_since(before);
            });
        std::snprintf(what, sizeof what, "culled, all offered, x%d, scroll 0.25", copies);
        bench::row(what, s, note_of(drawn, s.median()).c_str());
    }

    // The widget, in a window as wide as the measure and 768 px tall: found
    // by its content, which reads every byte twice whether it is in view or
    // not, and found by an ID, which reads them once.
    for (int pass = 0; pass < 2; ++pass) {
        const ImGuiWindowFlags plain = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
        float scroll = 0.0f;
        int drawn = 0;
        int culled = 0;
        bench::Samples s;
        const int count = copies >= 100 ? 500 : 2000;
        for (int frame = -8; frame < count; ++frame) {
            ImGui::NewFrame();
            const ImTypesetStats before = ImTypeset::GetStats();
            const double start = bench::now_us();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(width, bench::WINDOW_PX));
            ImGui::SetNextWindowScroll(ImVec2(0.0f, scroll));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin(pass == 0 ? "widgets" : "widgets, by ID", nullptr, plain);
            for (int copy = 0; copy < copies; ++copy) {
                ImGui::PushID(copy);
                int n = 0;
                for (const std::string& text : texts) {
                    if (pass == 0) {
                        ImTypeset::TextUnformatted(text.data(), text.data() + text.size());
                    } else {
                        ImGui::PushID(n++);
                        ImTypeset::TextWithID("##p", text.data(), text.data() + text.size());
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
            scroll = ImGui::GetScrollMaxY() * 0.25f;
            ImGui::End();
            ImGui::PopStyleVar();
            const double stop = bench::now_us();
            ImGui::Render();
            if (frame >= 0)
                s.add(stop - start);
            const ImTypesetStats after = ImTypeset::GetStats();
            drawn = static_cast<int>((after.VerticesEmitted - before.VerticesEmitted) / 4);
            culled = static_cast<int>(after.CulledCalls - before.CulledCalls);
            if (frame >= 0 &&
                (after.Layouts != before.Layouts || after.TextBuilds != before.TextBuilds)) {
                std::fprintf(stderr, "a steady frame laid text out again\n");
            }
        }
        char more[80];
        std::snprintf(more, sizeof more, "; %d of %zu calls clipped", culled,
                      doc.paragraphs.size());
        std::snprintf(what, sizeof what, "widget frame%s, x%d, scroll 0.25",
                      pass == 0 ? "" : ", TextWithID", copies);
        bench::row(what, s, note_of(drawn, s.median(), more).c_str());
    }
    doc.destroy();
}

// --- Where a draw's time goes --------------------------------------------------------

DrawCmd command_of(const core_golden::Cmd& cmd)
{
    const uint32_t index = static_cast<uint32_t>(cmd.index);
    switch (cmd.kind) {
    case core_golden::Cmd::GLYPH:
        return DrawCmd::glyph(cmd.font, cmd.codepoint, cmd.x, cmd.y, index);
    case core_golden::Cmd::RULERECT:
        return DrawCmd::rule_rect(cmd.x, cmd.y, cmd.width, cmd.height);
    case core_golden::Cmd::SETGLUE:
        return DrawCmd::set_glue(cmd.x, cmd.y, cmd.width, cmd.natural, cmd.height, cmd.depth,
                                 index);
    case core_golden::Cmd::ATOM:
        return DrawCmd::atom(cmd.x, cmd.y, cmd.width, cmd.natural, cmd.height, cmd.depth, index);
    case core_golden::Cmd::LINE:
        return DrawCmd::line(cmd.x, cmd.y, cmd.width, cmd.height, cmd.depth,
                             static_cast<uint32_t>(cmd.start), static_cast<uint32_t>(cmd.end));
    }
    return DrawCmd();
}

// The first run's commands of one case of draw.golden.
std::vector<DrawCmd> commands_of(const golden::File& file, const char* name)
{
    std::vector<DrawCmd> out;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.name != name)
            continue;
        core_golden::Reader reader(file, c);
        core_golden::Context context;
        while (!reader.done() && !reader.next_is("cmds"))
            reader.take();
        if (reader.done())
            break;
        for (const core_golden::Cmd& cmd : core_golden::read_cmds(reader, context))
            out.push_back(command_of(cmd));
    }
    return out;
}

struct Block {
    std::vector<DrawCmd> commands;
    Scaled height = 0;
    uint32_t indices = 0;
    int glyphs = 0;
};

Block block_of(std::vector<DrawCmd> commands)
{
    Block block;
    for (const DrawCmd& cmd : commands) {
        if (cmd.is(CmdType::LINE)) {
            block.height = std::max(block.height, cmd.y + cmd.depth);
            block.indices = std::max(block.indices, cmd.end);
        } else if (cmd.is(CmdType::GLYPH)) {
            ++block.glyphs;
        }
    }
    block.commands = std::move(commands);
    return block;
}

// Blocks stacked down a page, a paragraph skip apart, until there are at least
// `glyphs` glyph commands. Reading indices run on from block to block.
std::vector<DrawCmd> page_of(const std::vector<Block>& blocks, int glyphs, int* placed)
{
    std::vector<DrawCmd> page;
    Scaled y = 0;
    uint32_t index = 0;
    *placed = 0;
    for (size_t i = 0; *placed < glyphs; ++i) {
        const Block& block = blocks[i % blocks.size()];
        for (DrawCmd cmd : block.commands) {
            cmd.y += y;
            if (cmd.is(CmdType::LINE))
                cmd.end += index;
            if (!cmd.is(CmdType::RULE_RECT))
                cmd.index += index;
            page.push_back(cmd);
        }
        y += block.height + 12 * UNITY;
        index += block.indices;
        *placed += block.glyphs;
    }
    return page;
}

std::string utf8_of(uint32_t codepoint)
{
    char buffer[5] = {0, 0, 0, 0, 0};
    if (codepoint < 0x80) {
        buffer[0] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        buffer[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buffer[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        buffer[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buffer[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        buffer[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        buffer[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return std::string(buffer);
}

// One AddText or AddRectFilled per op, prepared outside the timed part.
struct Call {
    ImFont* font = nullptr;
    float size = 0.0f;
    ImVec2 a;
    ImVec2 b;
    ImU32 colour = 0;
    char text[5] = {0, 0, 0, 0, 0};
    bool is_text = false;
};

std::vector<Call> calls_of(const Drawer& drawer, const FaceFonts& fonts)
{
    std::vector<Call> calls;
    for (const DrawOp& op : drawer.ops()) {
        Call call;
        call.a = ImVec2(op.x0, op.y0);
        call.b = ImVec2(op.x1, op.y1);
        call.colour = op.colour;
        if (op.type == OpType::TEXT) {
            const Drawer::Glyph& glyph = drawer.glyphs()[op.glyph];
            const Drawer::Key& key = drawer.keys()[glyph.key];
            FontSpec spec;
            parse_font_key(key.key, &spec);
            const FaceFont& face = fonts.faces[static_cast<int>(spec.face)];
            const std::string text = utf8_of(atlas_codepoint(*face.data, glyph.codepoint));
            std::snprintf(call.text, sizeof call.text, "%s", text.c_str());
            call.font = face.font;
            call.size = key.size;
            call.is_text = true;
        } else if (op.type != OpType::RECT_FILLED) {
            continue;
        }
        calls.push_back(call);
    }
    return calls;
}

// Answers without asking anything of ImGui, so that placing can be timed alone.
class FixedBinder : public FontBinder
{
public:
    void begin() override {}
    Binding bind(uint32_t, FontKey) override { return Binding::BOUND; }
    bool has_glyph(uint32_t, uint32_t, uint32_t) override { return true; }
    bool bake(uint32_t, FontKey, float* size, float* ascent) override
    {
        *size = 19.0f;
        *ascent = 16.0f;
        return true;
    }
};

// `tag` starts every row's name, so that the rows of two pages can be told apart.
void bench_page(const char* title, const char* tag, const std::vector<DrawCmd>& page, int glyphs,
                int frames)
{
    const auto named = [tag](const char* what) { return std::string(tag) + ": " + what; };
    ImTypesetContext& ctx = *ImTypeset::GetCurrentContext();
    LineIndex index;
    index_lines(page.data(), page.size(), index);

    DrawRun run;
    run.cmds = page.data();
    run.count = page.size();
    run.ink = rgba(0x11, 0x11, 0x11);
    DrawRun indexed = run;
    indexed.lines = &index;
    DrawArgs args;
    args.origin = ImVec2(MARGIN_PX, MARGIN_PX);
    DrawArgs culling = args;
    culling.cull_lines = true;

    Drawer drawer;
    DrawBuffers buffers;
    int vertices = 0;
    char heading[200];
    std::snprintf(heading, sizeof heading, "%s: %d glyphs, %zu commands, %zu lines", title, glyphs,
                  page.size(), index.spans.size());
    bench::table_head(heading);

    bench::Samples direct = bench::sample_frames(frames, 5, [&](ImDrawList* list) {
        drawer.draw_runs(list, &run, 1, args, ctx.fonts, ctx.resolve);
        vertices = list->VtxBuffer.Size;
    });
    bench::row(named("Drawer into a draw list").c_str(), direct,
               note_of(glyphs, direct.median()).c_str());
    const int keys = static_cast<int>(drawer.keys().size());

    const std::vector<Call> calls = calls_of(drawer, ctx.fonts);
    int replayed = 0;
    bench::Samples imgui = bench::sample_frames(frames, 5, [&](ImDrawList* list) {
        list->Flags |= imgui_compat::TextNoPixelSnap;
        for (const Call& call : calls) {
            if (call.is_text)
                list->AddText(call.font, call.size, call.a, call.colour, call.text);
            else
                list->AddRectFilled(call.a, call.b, call.colour);
        }
        replayed = list->VtxBuffer.Size;
    });
    bench::row(named("the same calls, one AddText each (ImGui's share)").c_str(), imgui,
               note_of(glyphs, imgui.median()).c_str());

    FixedBinder fixed;
    bench::Samples placed =
        bench::sample_frames(frames, 5, [&](ImDrawList*) { drawer.place(&run, 1, args, fixed); });
    bench::row(named("placement alone, no vertices").c_str(), placed,
               note_of(glyphs, placed.median()).c_str());

    bench::Samples buffered = bench::sample_frames(frames, 5, [&](ImDrawList* list) {
        buffers.clear();
        drawer.draw_runs(buffers, list->_CmdHeader.ClipRect, &run, 1, args, ctx.fonts, ctx.resolve);
    });
    bench::row(named("Drawer into buffers").c_str(), buffered,
               note_of(glyphs, buffered.median()).c_str());

    // A window 768 px tall a third of the way down the page.
    const float top = MARGIN_PX + static_cast<float>(px_from_sp(page.back().y)) / 3.0f;
    const ImVec2 clip_min(0.0f, top);
    const ImVec2 clip_max(4000.0f, top + bench::WINDOW_PX);
    int visible = 0;
    bench::Samples clipped = bench::sample_frames(frames, 5, [&](ImDrawList* list) {
        list->PushClipRect(clip_min, clip_max, false);
        drawer.draw_runs(list, &run, 1, args, ctx.fonts, ctx.resolve);
        list->PopClipRect();
    });
    bench::row(named("768 px window, no line index (every command visited)").c_str(), clipped,
               note_of(glyphs, clipped.median()).c_str());
    bench::Samples culled = bench::sample_frames(frames, 5, [&](ImDrawList* list) {
        list->PushClipRect(clip_min, clip_max, false);
        const DrawStats stats =
            drawer.draw_runs(list, &indexed, 1, culling, ctx.fonts, ctx.resolve);
        visible = stats.glyphs;
        list->PopClipRect();
    });
    bench::row(named("768 px window, by line index").c_str(), culled,
               note_of(visible, culled.median()).c_str());
    std::printf("\n%d font keys; %d vertices from the draw list, %d from the AddText calls, %d in "
                "the buffers.\n",
                keys, vertices, replayed, buffers.vertices.Size);
}

// --- Hit testing and selection -------------------------------------------------------

void bench_selection(const std::vector<std::string>& texts, float width)
{
    // The heaviest paragraph of the sample: its last.
    const std::string& text = texts.back();
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
    ImTypeset::LayoutParagraph(p, width);
    const ImVec2 size = ImTypeset::GetParagraphSize(p);
    const int length = static_cast<int>(text.size());
    const int glyphs = glyphs_of(p);

    constexpr int POINTS = 1024;
    std::vector<ImVec2> points;
    uint32_t state = 12345u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    };
    for (int i = 0; i < POINTS; ++i)
        points.push_back(ImVec2(next() * size.x, next() * size.y));

    char heading[200];
    std::snprintf(heading, sizeof heading,
                  "Hit testing and selection, the sample's last paragraph: %d glyphs, %d lines, "
                  "%d bytes",
                  glyphs, ImTypeset::GetParagraphLineCount(p), length);
    bench::table_head(heading);

    int sink = 0;
    {
        bench::Samples s;
        for (int i = -5; i < 300; ++i) {
            ImTypeset::LayoutParagraph(p, width + 1.0f);
            ImTypeset::LayoutParagraph(p, width);
            const double start = bench::now_us();
            sink += ImTypeset::HitTestParagraph(p, points[0]).TextOffset;
            const double stop = bench::now_us();
            if (i >= 0)
                s.add(stop - start);
        }
        bench::row("first hit test after a layout (builds the selection model)", s);
    }
    {
        bench::Samples s = bench::sample(500, 5, [&]() {
            for (const ImVec2& point : points)
                sink += ImTypeset::HitTestParagraph(p, point).TextOffset;
        });
        for (double& value : s.us)
            value /= POINTS;
        bench::row("hit test, per call", s, "1,024 points over the paragraph per sample");
    }
    static const struct {
        ImTypesetGranularity granularity;
        const char* what;
    } units[] = {
        {ImTypesetGranularity_Character, "select a range by character, per call"},
        {ImTypesetGranularity_Word, "select a range by word, per call"},
        {ImTypesetGranularity_Line, "select a range by line, per call"},
    };
    for (const auto& unit : units) {
        bench::Samples s = bench::sample(500, 5, [&]() {
            for (int i = 0; i + 1 < POINTS; i += 2)
                sink += ImTypeset::SelectParagraphRange(p, points[static_cast<size_t>(i)],
                                                        points[static_cast<size_t>(i) + 1],
                                                        unit.granularity)
                            .End;
        });
        for (double& value : s.us)
            value /= POINTS / 2;
        bench::row(unit.what, s, "512 drags per sample");
    }
    const ImTypesetTextRange all = {0, length};
    {
        ImVector<ImVec4> rects;
        int count = 0;
        bench::Samples s = bench::sample(5000, 5, [&]() {
            rects.resize(0);
            count = ImTypeset::GetParagraphRangeRects(p, all, &rects);
        });
        char note[60];
        std::snprintf(note, sizeof note, "%d rectangles", count);
        bench::row("selection rectangles, everything selected", s, note);
    }
    {
        bench::Samples plain = bench::sample_frames(2000, 5, [&](ImDrawList* list) {
            ImTypeset::DrawParagraph(p, list, ImVec2(MARGIN_PX, MARGIN_PX));
        });
        bench::row("draw the paragraph", plain, note_of(glyphs, plain.median()).c_str());
        bench::Samples selected = bench::sample_frames(2000, 5, [&](ImDrawList* list) {
            ImTypeset::DrawParagraph(p, list, ImVec2(MARGIN_PX, MARGIN_PX), &all);
        });
        bench::row("draw the paragraph, everything selected", selected,
                   note_of(glyphs, selected.median()).c_str());
    }
    {
        ImGuiTextBuffer copy;
        bench::Samples s = bench::sample(2000, 5, [&]() {
            copy.clear();
            ImTypeset::AppendParagraphRangeText(p, all, &copy);
        });
        char note[60];
        std::snprintf(note, sizeof note, "%d bytes", copy.size());
        bench::row("copy the whole paragraph", s, note);
    }
    if (sink == 0x7FFFFFFF)
        std::printf("\n");
    ImTypeset::DestroyParagraph(p);
}

} // namespace

int main(int argc, char** argv)
{
    // Sections: x1, x10, x100, anatomy, mixed, selection.
    const bench::Sections sections(argc, argv);
    // A display tall enough that nothing is clipped unless a row says so.
    bench::create_imgui(4000.0f, 4000000.0f);
    int status = 0;
    if (ImTypeset::CreateContext() == nullptr || !bench::add_fonts()) {
        std::fprintf(stderr, "%s\n", ImTypeset::GetLastError()->Message);
        ImGui::DestroyContext();
        return 1;
    }
    bench::reference_style();
    bench::header("bench_draw");
    const float width = bench::px_of_pt(bench::MEASURE_PT);
    std::printf("- measure %.0f pt = %.2f px at %.0f px per inch; em %.0f px; window %.0f px "
                "tall; the widget rows are in a window %.2f px wide\n",
                bench::MEASURE_PT, static_cast<double>(width), bench::PX_PER_INCH,
                static_cast<double>(bench::EM_PX), static_cast<double>(bench::WINDOW_PX),
                static_cast<double>(width));

    std::vector<std::string> texts;
    golden::File file;
    if (!bench::load_sample(texts)) {
        status = 1;
    } else {
        if (sections.wants("x1"))
            bench_document(texts, 1, width, 3000);
        if (sections.wants("x10"))
            bench_document(texts, 10, width, 500);
        if (sections.wants("x100"))
            bench_document(texts, 100, width, 60);

        if (sections.wants("anatomy")) {
            Document doc = document_of(texts, 1, width);
            std::vector<Block> blocks;
            for (const ImTypesetParagraph* p : doc.paragraphs)
                blocks.push_back(block_of(shipped_commands(*p)));
            int glyphs = 0;
            const std::vector<DrawCmd> page = page_of(blocks, doc.glyphs, &glyphs);
            bench_page("Where a full draw goes, the sample x1 as one command list", "sample", page,
                       glyphs, 3000);
            doc.destroy();
        }
        if (!sections.wants("mixed")) {
        } else if (!file.load(golden::path("draw/draw.golden"))) {
            status = 1;
        } else {
            std::vector<Block> blocks;
            blocks.push_back(block_of(commands_of(file, "mixed-faces-and-sizes")));
            blocks.push_back(block_of(commands_of(file, "justified-paragraph")));
            if (blocks[0].glyphs == 0 || blocks[1].glyphs == 0) {
                std::fprintf(stderr, "the golden cases this benchmark tiles are missing\n");
                status = 1;
            } else {
                int glyphs = 0;
                const std::vector<DrawCmd> page = page_of(blocks, 4362, &glyphs);
                bench_page("Mixed faces and sizes, tiled from tests/golden/draw", "mixed", page,
                           glyphs, 3000);
            }
        }
        if (sections.wants("selection"))
            bench_selection(texts, width);
    }
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return status;
}
