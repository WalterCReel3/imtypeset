// Scratch probe for docs/vector-output.md, built ONLY against the patched
// private copy build/vector-output/tree-drawscale, in which a paragraph's draw
// multiplies its layout's pixels per inch by a global (one line of
// src/imtypeset.cpp). Question: is "the z = 1 layout drawn at z" the same
// vertices as "laid out at z and drawn", and what does each cost?
#include "bench_common.h"

#include <cmath>

extern double imtypeset_scratch_draw_scale;

namespace
{
const char* const TEXT =
    "The difference between a good paragraph and a poor one is the evenness of its word "
    "spaces, and that is decided for the whole paragraph at once. A formula such as "
    "$\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$ is a box on the baseline among the words, and "
    "the affinity of efficient typography is in the fi and ffi of official offices.";

void buffers(ImTypesetParagraph* p, ImVector<ImDrawVert>& v, ImVector<ImDrawIdx>& ix)
{
    v.resize(0);
    ix.resize(0);
    ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(-1e6f, -1e6f, 1e6f, 1e6f), &v,
                                      &ix);
}
} // namespace

int main()
{
    bench::create_imgui(8000.0f, 4000.0f);
    if (!IMTYPESET_CHECKVERSION() || ImTypeset::CreateContext() == nullptr || !bench::add_fonts())
        return 1;
    std::vector<std::string> paragraphs;
    if (!bench::load_sample(paragraphs))
        return 1;
    bench::header("draw-scale probe (scratch, patched private copy)");

    // The one paragraph, and the 34 paragraphs of the sample, each compared.
    std::vector<std::string> texts(paragraphs);
    texts.push_back(TEXT);
    std::vector<ImTypesetParagraph*> ps;
    for (const std::string& t : texts) {
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(p, t.c_str(), nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ps.push_back(p);
    }
    const float W1 = bench::px_of_pt(bench::MEASURE_PT);
    const auto style_at = [](double z) {
        ImTypesetStyle s;
        s.EmSize = static_cast<float>(bench::EM_PX * z);
        s.PixelsPerInch = static_cast<float>(bench::PX_PER_INCH * z);
        return s;
    };

    std::printf("\n| z | paragraphs | vertices | vertices that differ in position | worst "
                "difference px | in uv or colour |\n|---|---|---|---|---|---|\n");
    ImVector<ImDrawVert> a, b;
    ImVector<ImDrawIdx> ia, ib;
    for (const double z : {0.5, 0.75, 1.1, 1.25, 1.5, 1.7320508, 2.0, 3.0, 4.0}) {
        long total = 0, moved = 0, other = 0;
        double worst = 0.0;
        for (ImTypesetParagraph* p : ps) {
            ImGui::NewFrame();
            // (a) laid out at z, drawn as laid out
            imtypeset_scratch_draw_scale = 1.0;
            const ImTypesetStyle sz = style_at(z);
            ImTypeset::LayoutParagraph(p, static_cast<float>(W1 * z), &sz);
            buffers(p, a, ia);
            // (b) laid out at 1, drawn at z
            const ImTypesetStyle s1 = style_at(1.0);
            ImTypeset::LayoutParagraph(p, W1, &s1);
            imtypeset_scratch_draw_scale = z;
            buffers(p, b, ib);
            imtypeset_scratch_draw_scale = 1.0;
            ImGui::Render();
            if (a.Size != b.Size) {
                std::printf("| %g | vertex counts differ: %d against %d |\n", z, a.Size, b.Size);
                continue;
            }
            for (int i = 0; i < a.Size; ++i) {
                ++total;
                const double d = std::max(std::fabs(a[i].pos.x - b[i].pos.x),
                                          std::fabs(a[i].pos.y - b[i].pos.y));
                if (d > 0.0)
                    ++moved;
                worst = std::max(worst, d);
                if (a[i].uv.x != b[i].uv.x || a[i].uv.y != b[i].uv.y || a[i].col != b[i].col)
                    ++other;
            }
        }
        std::printf("| %g | %zu | %ld | %ld | %.4f | %ld |\n", z, ps.size(), total, moved, worst,
                    other);
    }

    // Cost of a zoom step for the 35 paragraphs, each way.
    bench::Samples relayout, redraw_only;
    double z = 0.6;
    for (int step = 0; step < 40; ++step, z *= 1.03) {
        const ImTypesetStyle sz = style_at(z);
        ImGui::NewFrame();
        double t0 = bench::now_us();
        for (ImTypesetParagraph* p : ps) {
            ImTypeset::LayoutParagraph(p, static_cast<float>(W1 * z), &sz);
            buffers(p, a, ia);
        }
        relayout.add(bench::now_us() - t0);
        ImGui::Render();
    }
    const ImTypesetStyle s1 = style_at(1.0);
    for (ImTypesetParagraph* p : ps)
        ImTypeset::LayoutParagraph(p, W1, &s1);
    z = 0.6;
    for (int step = 0; step < 40; ++step, z *= 1.03) {
        ImGui::NewFrame();
        imtypeset_scratch_draw_scale = z;
        double t0 = bench::now_us();
        for (ImTypesetParagraph* p : ps)
            buffers(p, a, ia);
        redraw_only.add(bench::now_us() - t0);
        imtypeset_scratch_draw_scale = 1.0;
        ImGui::Render();
    }
    bench::table_head("one zoom step over 35 paragraphs (the sample's 34 and one with a formula), "
                      "first draw at the new z into buffers");
    bench::row("lay out at z, then draw (what the public API does today)", relayout);
    bench::row("keep the z = 1 layout, draw at z (the patched copy)", redraw_only);
    for (ImTypesetParagraph* p : ps)
        ImTypeset::DestroyParagraph(p);
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return 0;
}
