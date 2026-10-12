// Scratch probe for docs/vector-output.md. Public API only (imtypeset.h and
// imgui.h), plus the tree's software renderer for the pictures. Not part of
// the library; lives under build/vector-output/.
//
//   bench_zoom <output directory for PNGs>

#include "bench_common.h"
#include "support/soft_raster.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{

const float MEASURE_PX = bench::px_of_pt(bench::MEASURE_PT);
const int BLOCKS = 1000;

struct Doc {
    std::string text;
    std::vector<ImTypesetBlock> blocks;
    ImTypesetBlockStyle style;
    ImTypesetDocument* doc = nullptr;
};

// "Picture" zoom: every pixel length times z AND pixels per inch times z, so
// the layout in scaled points is meant to be the same at every z.
// "Text" zoom: the em times z, the measure and the pixels per inch as they
// were, as a browser's text zoom: lines are broken again.
ImTypesetStyle style_at(double z, bool picture)
{
    ImTypesetStyle s;
    s.EmSize = static_cast<float>(bench::EM_PX * z);
    s.PixelsPerInch = static_cast<float>(picture ? bench::PX_PER_INCH * z : bench::PX_PER_INCH);
    return s;
}

void describe(Doc& d, double skip_px)
{
    d.style = ImTypesetBlockStyle();
    d.style.SpaceBelow = static_cast<float>(skip_px);
    ImTypesetDocumentContent c;
    c.Text = d.text.data();
    c.TextSize = static_cast<int>(d.text.size());
    c.Blocks = d.blocks.data();
    c.BlockCount = static_cast<int>(d.blocks.size());
    c.Styles = &d.style;
    c.StyleCount = 1;
    ImTypeset::SetDocumentBlocks(d.doc, &c);
}

void build(Doc& d, const std::vector<std::string>& paragraphs, int count)
{
    for (int i = 0; i < count; ++i) {
        const std::string& p = paragraphs[static_cast<size_t>(i) % paragraphs.size()];
        ImTypesetBlock b;
        b.Kind = ImTypesetBlockKind_Paragraph;
        b.TextStart = static_cast<int>(d.text.size());
        d.text += p;
        b.TextEnd = static_cast<int>(d.text.size());
        d.blocks.push_back(b);
    }
    d.doc = ImTypeset::CreateDocument();
}

struct Breaks {
    std::vector<int> line_counts;
    std::vector<int> ends;      // every line's TextEnd, block after block
    std::vector<float> right;   // every line's Max.x
    std::vector<float> base;    // every line's Baseline (block-relative)
    std::vector<float> top;     // every block's FullMin.y
    float height = 0.0f;
};

Breaks settle(Doc& d, double z, bool picture, bool redescribe)
{
    if (redescribe)
        describe(d, bench::PARAGRAPH_SKIP_PX * z);
    const ImTypesetStyle s = style_at(z, picture);
    ImTypeset::LayoutDocument(d.doc, static_cast<float>(picture ? MEASURE_PX * z : MEASURE_PX), &s);
    ImTypeset::LayoutDocumentWindow(d.doc, 0.0f, 1.0e9f);
    Breaks out;
    const int n = ImTypeset::GetDocumentBlockCount(d.doc);
    for (int b = 0; b < n; ++b) {
        const ImTypesetBlockGeometry g = ImTypeset::GetDocumentBlockGeometry(d.doc, b);
        out.line_counts.push_back(g.LineCount);
        out.top.push_back(g.FullMin.y);
        for (int l = 0; l < g.LineCount; ++l) {
            const ImTypesetLine line = ImTypeset::GetDocumentBlockLine(d.doc, b, l);
            out.ends.push_back(line.TextEnd);
            out.right.push_back(line.Max.x);
            out.base.push_back(line.Baseline);
        }
    }
    out.height = ImTypeset::GetDocumentSize(d.doc).y;
    return out;
}

void atlas_line(const char* when)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    int w = 0, h = 0;
    if (atlas->TexData != nullptr) {
        w = atlas->TexData->Width;
        h = atlas->TexData->Height;
    }
    std::printf("- atlas %s: %d x %d texels (%.1f MiB as RGBA8), %d texture(s) listed\n", when, w,
                h, static_cast<double>(w) * h * 4.0 / (1024.0 * 1024.0), atlas->TexList.Size);
}

// --- 1. Is a picture zoom the same layout? ------------------------------------------

void invariance(const std::vector<std::string>& paragraphs)
{
    std::printf("\n### 1. Picture zoom: is the layout at z the layout at 1, times z?\n\n");
    std::printf("%d blocks, %.3f px measure and a 16 px em at z = 1; at z every pixel length "
                "and PixelsPerInch are multiplied by z (as floats). Compared with z = 1: the "
                "byte at which every line ends, and each line's right edge and baseline and "
                "each block's top against z times the value at 1.\n\n",
                BLOCKS, static_cast<double>(MEASURE_PX));
    Doc d;
    build(d, paragraphs, BLOCKS);
    const Breaks one = settle(d, 1.0, true, true);
    std::printf("z = 1: %zu lines, height %.0f px\n\n", one.ends.size(),
                static_cast<double>(one.height));
    std::printf("| z | blocks whose lines end elsewhere | max right-edge error px | max baseline "
                "error px | max block-top error px | block-top error as a fraction of z |\n"
                "|---|---|---|---|---|---|\n");
    std::vector<double> zs = {0.5, 0.75, 1.1, 1.25, 1.5, 1.7320508, 2.0, 3.0, 4.0};
    int total_bad_sweep = 0, sweep = 0;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            zs.clear();
            for (int k = 1; k <= 300; ++k)
                zs.push_back(0.5 + 0.0117 * k); // 0.51 .. 4.01, none a round number
        }
        for (const double z : zs) {
            const Breaks at = settle(d, z, true, true);
            int bad = 0;
            double e_right = 0.0, e_base = 0.0, e_top = 0.0;
            size_t line = 0, line_at = 0;
            for (size_t b = 0; b < one.line_counts.size(); ++b) {
                bool same = one.line_counts[b] == at.line_counts[b];
                if (same)
                    for (int l = 0; l < one.line_counts[b]; ++l)
                        if (one.ends[line + static_cast<size_t>(l)] !=
                            at.ends[line_at + static_cast<size_t>(l)])
                            same = false;
                if (same) {
                    for (int l = 0; l < one.line_counts[b]; ++l) {
                        const size_t i = line + static_cast<size_t>(l);
                        const size_t j = line_at + static_cast<size_t>(l);
                        e_right = std::max(e_right, std::fabs(at.right[j] - z * one.right[i]));
                        e_base = std::max(e_base, std::fabs(at.base[j] - z * one.base[i]));
                    }
                } else {
                    ++bad;
                }
                e_top = std::max(e_top, std::fabs(at.top[b] - z * one.top[b]));
                line += static_cast<size_t>(one.line_counts[b]);
                line_at += static_cast<size_t>(at.line_counts[b]);
            }
            if (pass == 0) {
                std::printf("| %.7g | %d | %.4f | %.4f | %.4f | %.4f |\n", z, bad, e_right, e_base,
                            e_top, e_top / z);
            } else {
                ++sweep;
                total_bad_sweep += bad > 0 ? 1 : 0;
                static double w_right = 0, w_base = 0, w_top = 0;
                w_right = std::max(w_right, e_right);
                w_base = std::max(w_base, e_base);
                w_top = std::max(w_top, e_top / z);
                if (sweep == 300)
                    std::printf("\nsweep of 300 values of z from 0.5117 to 4.01 in steps of "
                                "0.0117: %d had any block whose lines end elsewhere; worst "
                                "right-edge error %.4f px, baseline %.4f px, block top %.4f "
                                "px per unit of z (over a %.0f px column at z = 1)\n",
                                total_bad_sweep, w_right, w_base, w_top,
                                static_cast<double>(one.height));
            }
        }
    }
    ImTypeset::DestroyDocument(d.doc);
    std::fflush(stdout);
}

// --- 2. What a zoom step costs -------------------------------------------------------

void step_costs(const std::vector<std::string>& paragraphs)
{
    std::printf("\n### 2. What one zoom step costs today, %d blocks\n\n", BLOCKS);
    std::printf("A step is a new z. The view is a clip rectangle %.0f px tall a quarter of the "
                "way down the document. Each row is one sample per step. 'new sizes': 40 steps "
                "of z *= 1.03 from 0.6, so nearly every step asks the atlas for sizes it has "
                "not baked. 'sizes seen': the same 40 values of z again straight after (ImGui "
                "may have dropped some of them from the atlas in between).\n",
                static_cast<double>(bench::WINDOW_PX));
    for (int mode = 0; mode < 2; ++mode) {
        const bool picture = mode == 0;
        Doc d;
        build(d, paragraphs, BLOCKS);
        describe(d, bench::PARAGRAPH_SKIP_PX);
        std::vector<double> zs;
        double z = 0.6;
        for (int k = 0; k < 40; ++k, z *= 1.03)
            zs.push_back(z);
        for (int pass = 0; pass < 2; ++pass) {
            bench::Samples t_describe, t_layout, t_first, t_again, t_all, t_frame;
            int glyphs = 0;
            for (const double at : zs) {
                const ImTypesetStyle s = style_at(at, picture);
                const float width = static_cast<float>(picture ? MEASURE_PX * at : MEASURE_PX);
                double a = bench::now_us();
                if (picture)
                    describe(d, bench::PARAGRAPH_SKIP_PX * at);
                double b = bench::now_us();
                ImTypeset::LayoutDocument(d.doc, width, &s);
                double c = bench::now_us();
                t_describe.add(b - a);
                t_layout.add(c - b);
                const float top = ImTypeset::GetDocumentSize(d.doc).y * 0.25f;
                for (int frame = 0; frame < 3; ++frame) {
                    ImGui::NewFrame();
                    ImDrawList* list = ImGui::GetBackgroundDrawList();
                    const ImU64 before = ImTypeset::GetStats().VerticesEmitted;
                    list->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(8000.0f, bench::WINDOW_PX));
                    const double t0 = bench::now_us();
                    ImTypeset::DrawDocument(d.doc, list, ImVec2(0.0f, -top));
                    const double t1 = bench::now_us();
                    list->PopClipRect();
                    ImGui::Render();
                    glyphs = static_cast<int>((ImTypeset::GetStats().VerticesEmitted - before) / 4);
                    if (frame == 0) {
                        t_first.add(t1 - t0);
                        t_frame.add(t1 - a);
                    } else if (frame == 2) {
                        t_again.add(t1 - t0);
                    }
                }
                a = bench::now_us();
                ImTypeset::LayoutDocumentWindow(d.doc, 0.0f, 1.0e9f);
                t_all.add(bench::now_us() - a);
            }
            char title[160];
            std::snprintf(title, sizeof title, "%s zoom, %s", picture ? "picture" : "text",
                          pass == 0 ? "new sizes" : "sizes seen");
            bench::table_head(title);
            if (picture)
                bench::row("describe the 1,000 blocks again (their spacing is in pixels)",
                           t_describe);
            bench::row("LayoutDocument (says the width and style; lays nothing out)", t_layout);
            char note[96];
            std::snprintf(note, sizeof note, "%d quads at the last step", glyphs);
            bench::row("first DrawDocument of the view after the step", t_first, note);
            bench::row("third DrawDocument of the same view", t_again);
            bench::row("the step as the user waits for it: describe + layout + first draw",
                       t_frame);
            bench::row("then lay out everything not in view (exact scroll extent)", t_all);
            atlas_line(title);
        }
        ImTypeset::DestroyDocument(d.doc);
    }
    std::fflush(stdout);
}

// --- 3. Labels for a host-drawn diagram ----------------------------------------------

void labels(const std::vector<std::string>& paragraphs)
{
    const int COUNT = 500;
    std::printf("\n### 3. %d labels as paragraphs of their own\n\n", COUNT);
    // Labels of one to five words taken from the sample, every third a formula.
    std::vector<std::string> words;
    for (const std::string& p : paragraphs) {
        size_t at = 0;
        while (at < p.size()) {
            size_t end = p.find(' ', at);
            if (end == std::string::npos)
                end = p.size();
            if (end > at)
                words.push_back(p.substr(at, end - at));
            at = end + 1;
        }
    }
    std::vector<std::string> texts;
    size_t w = 0;
    for (int i = 0; i < COUNT; ++i) {
        std::string t;
        const int n = 1 + i % 5;
        for (int k = 0; k < n; ++k) {
            if (k)
                t += ' ';
            t += words[w++ % words.size()];
        }
        if (i % 3 == 0)
            t += " $\\frac{n}{k+1}$";
        texts.push_back(t);
    }
    const float BOX_PX = 140.0f; // the widest a label may be at z = 1
    std::vector<ImTypesetParagraph*> ps(static_cast<size_t>(COUNT), nullptr);
    std::vector<float> natural(static_cast<size_t>(COUNT), 0.0f);
    std::vector<ImVec2> size_one(static_cast<size_t>(COUNT));
    std::vector<int> lines_one(static_cast<size_t>(COUNT), 0);

    const auto set_at = [&](double z, bool measure_natural) {
        ImTypesetStyle s = style_at(z, true);
        s.Align = ImTypesetAlign_Center;
        for (int i = 0; i < COUNT; ++i) {
            const size_t k = static_cast<size_t>(i);
            float width = static_cast<float>(BOX_PX * z);
            if (measure_natural) {
                ImTypeset::LayoutParagraph(ps[k], 1.0e6f, &s);
                const ImTypesetLine l = ImTypeset::GetParagraphLine(ps[k], 0);
                natural[k] = (l.Max.x - l.Min.x) / static_cast<float>(z);
            }
            // As wide as the text if that is narrower than the box.
            width = std::min(width, std::ceil(natural[k] * static_cast<float>(z)) + 1.0f);
            ImTypeset::LayoutParagraph(ps[k], width, &s);
        }
    };

    double t0 = bench::now_us();
    for (int i = 0; i < COUNT; ++i) {
        const size_t k = static_cast<size_t>(i);
        ps[k] = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(ps[k], texts[k].c_str(), nullptr, nullptr, 0,
                                    ImTypesetTextFlags_Math);
    }
    double t1 = bench::now_us();
    set_at(1.0, true);
    double t2 = bench::now_us();
    std::printf("- create and set text, %d labels: %.0f us; measure each (a layout at 1e6 px "
                "for the natural width, then a layout at the box): %.0f us, once\n",
                COUNT, t1 - t0, t2 - t1);
    for (int i = 0; i < COUNT; ++i) {
        const size_t k = static_cast<size_t>(i);
        size_one[k] = ImTypeset::GetParagraphSize(ps[k]);
        lines_one[k] = ImTypeset::GetParagraphLineCount(ps[k]);
    }

    bench::Samples t_layout, t_draw_first, t_draw;
    int changed_lines = 0, grew = 0, steps = 0;
    double worst_w = 0.0, worst_h = 0.0;
    double z = 0.6;
    for (int step = 0; step < 40; ++step, z *= 1.03) {
        double a = bench::now_us();
        set_at(z, false);
        t_layout.add(bench::now_us() - a);
        ++steps;
        for (int i = 0; i < COUNT; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (ImTypeset::GetParagraphLineCount(ps[k]) != lines_one[k])
                ++changed_lines;
            const ImVec2 sz = ImTypeset::GetParagraphSize(ps[k]);
            // Sizes are whole pixels rounded up at each z: allow one pixel.
            const double dw = sz.x - z * size_one[k].x, dh = sz.y - z * size_one[k].y;
            worst_w = std::max(worst_w, std::fabs(dw));
            worst_h = std::max(worst_h, std::fabs(dh));
            if (dh > z + 1.0)
                ++grew;
        }
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            a = bench::now_us();
            for (int i = 0; i < COUNT; ++i) {
                const float x = static_cast<float>(static_cast<double>(i % 25) * (BOX_PX + 20.0) * z);
                const float y = static_cast<float>(static_cast<double>(i / 25) * 70.0 * z);
                ImTypeset::DrawParagraph(ps[static_cast<size_t>(i)], list, ImVec2(x, y));
            }
            const double b = bench::now_us();
            ImGui::Render();
            if (frame == 0)
                t_draw_first.add(b - a);
            else if (frame == 2)
                t_draw.add(b - a);
        }
    }
    bench::table_head("500 labels, picture zoom, 40 steps of z *= 1.03 from 0.6");
    bench::row("lay all 500 out again at the new z", t_layout);
    bench::row("first draw of all 500 at the new z", t_draw_first);
    bench::row("third draw of all 500", t_draw);
    std::printf("\n- over %d steps x %d labels: %d had another number of lines than at z = 1; "
                "%d were taller than z times their height at 1 by more than a line's rounding; "
                "worst |size - z * size at 1|: %.2f px wide, %.2f px tall (sizes are whole "
                "pixels rounded up, and the width given is the natural width rounded up plus "
                "one)\n",
                steps, COUNT, changed_lines, grew, worst_w, worst_h);
    for (ImTypesetParagraph* p : ps)
        ImTypeset::DestroyParagraph(p);
    std::fflush(stdout);
}

// --- 4. The same paragraph at 1x, at 2x, and at 2x from the 1x picture ---------------

struct Ink {
    long inked = 0;   // pixels darker than the page at all
    long solid = 0;   // at least 90 % ink
    long mid = 0;     // 10 % to 90 %
};

Ink ink_of(const soft_raster::Image& image)
{
    Ink out;
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const int v = image.rgba[i]; // red of black ink on white
        const double cover = (255 - v) / 255.0;
        if (cover <= 0.02)
            continue;
        ++out.inked;
        if (cover >= 0.9)
            ++out.solid;
        else if (cover >= 0.1)
            ++out.mid;
    }
    return out;
}

const char* const PICTURE_TEXT =
    "The difference between a good paragraph and a poor one is the evenness of its word "
    "spaces, and that is decided for the whole paragraph at once. A formula such as "
    "$\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$ is a box on the baseline among the words, and "
    "the affinity of efficient typography is in the fi and ffi of official offices.";

struct Quads {
    std::vector<float> left, top, width;
};

Quads quads_of(ImTypesetParagraph* p)
{
    ImVector<ImDrawVert> v;
    ImVector<ImDrawIdx> ix;
    Quads q;
    ImGui::NewFrame();
    ImTypeset::DrawParagraphToBuffers(p, ImVec2(0.0f, 0.0f), ImVec4(-1e6f, -1e6f, 1e6f, 1e6f), &v,
                                      &ix);
    ImGui::Render();
    for (int i = 0; i + 3 < v.Size; i += 4) {
        q.left.push_back(v[i].pos.x);
        q.top.push_back(v[i].pos.y);
        q.width.push_back(v[i + 1].pos.x - v[i].pos.x);
    }
    return q;
}

void pictures(const char* out_dir, soft_raster::Renderer& renderer)
{
    std::printf("\n### 4. One paragraph: laid out and drawn at z, against the z = 1 drawing "
                "enlarged\n\n");
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, PICTURE_TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    const float W1 = 420.0f;
    const ImU32 WHITE = IM_COL32(255, 255, 255, 255);

    const auto layout_at = [&](double z) {
        ImTypesetStyle s = style_at(z, true);
        s.Color = IM_COL32(0, 0, 0, 255);
        ImTypeset::LayoutParagraph(p, static_cast<float>(W1 * z), &s);
    };
    // Draws the paragraph as laid out, enlarging the frame's vertices by
    // `enlarge` before it is rasterised: what scaling a draw list does.
    const auto render = [&](double enlarge, const char* name) {
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        const int w = static_cast<int>(std::ceil((size.x + 16.0f) * enlarge));
        const int h = static_cast<int>(std::ceil((size.y + 16.0f) * enlarge));
        soft_raster::Image image(w, h, WHITE);
        for (int frame = 0; frame < 2; ++frame) { // the first frame bakes
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            ImTypeset::DrawParagraph(p, list, ImVec2(8.0f, 8.0f));
            ImGui::Render();
            ImDrawData* data = ImGui::GetDrawData();
            if (enlarge != 1.0) {
                for (ImDrawList* dl : data->CmdLists) {
                    for (ImDrawVert& vert : dl->VtxBuffer) {
                        vert.pos.x = static_cast<float>(vert.pos.x * enlarge);
                        vert.pos.y = static_cast<float>(vert.pos.y * enlarge);
                    }
                    for (ImDrawCmd& cmd : dl->CmdBuffer)
                        cmd.ClipRect = ImVec4(-1.0e5f, -1.0e5f, 1.0e5f, 1.0e5f);
                }
            }
            image.clear(WHITE);
            renderer.render(data, image);
        }
        const std::string path = std::string(out_dir) + "/" + name;
        soft_raster::write_png(path.c_str(), image);
        const Ink ink = ink_of(image);
        std::printf("| %s | %d x %d | %ld | %.1f %% | %.1f %% |\n", name, w, h, ink.inked,
                    100.0 * static_cast<double>(ink.solid) / static_cast<double>(ink.inked),
                    100.0 * static_cast<double>(ink.mid) / static_cast<double>(ink.inked));
    };

    std::printf("| picture | pixels | inked pixels | of them at least 90 %% ink | 10 to 90 %% |\n"
                "|---|---|---|---|---|\n");
    layout_at(1.0);
    const Quads q1 = quads_of(p);
    const int lines1 = ImTypeset::GetParagraphLineCount(p);
    render(1.0, "para-z1.png");
    render(2.0, "para-z2-enlarged-from-z1.png");
    render(1.5, "para-z1.5-enlarged-from-z1.png");
    render(4.0, "para-z4-enlarged-from-z1.png");
    for (const double z : {2.0, 1.5, 4.0}) {
        layout_at(z);
        char name[64];
        std::snprintf(name, sizeof name, "para-z%g-set-at-z.png", z);
        render(1.0, name);
    }
    std::printf("\nWhere each quad stands when set at z, against z times where it stands at "
                "z = 1 (the left edge of its quad, in pixels of the z picture; a quad's left "
                "edge is its pen plus the bitmap's own bearing, which is a whole pixel at each "
                "size, so up to about a pixel of this is the bitmap's and not the pen's):\n\n"
                "| z | lines (z = 1: %d) | quads (z = 1: %zu) | mean abs px | max px | over 1 px "
                "| over 2 px | gap between neighbours on a line: mean abs px | max px |\n"
                "|---|---|---|---|---|---|---|---|---|\n",
                lines1, q1.left.size());
    for (const double z : {1.25, 1.5, 2.0, 3.0, 4.0}) {
        layout_at(z);
        const Quads q = quads_of(p);
        if (q.left.size() != q1.left.size()) {
            std::printf("| %g | %d | %zu | quad counts differ | | | | | |\n", z,
                        ImTypeset::GetParagraphLineCount(p), q.left.size());
            continue;
        }
        double sum = 0.0, worst = 0.0, gsum = 0.0, gworst = 0.0;
        int over1 = 0, over2 = 0, gaps = 0;
        for (size_t i = 0; i < q.left.size(); ++i) {
            const double d = std::fabs(q.left[i] - z * q1.left[i]);
            sum += d;
            worst = std::max(worst, d);
            over1 += d > 1.0;
            over2 += d > 2.0;
            if (i > 0 && q1.left[i] > q1.left[i - 1] &&
                std::fabs(q1.top[i] - q1.top[i - 1]) < 12.0f) {
                const double g =
                    std::fabs((q.left[i] - q.left[i - 1]) - z * (q1.left[i] - q1.left[i - 1]));
                gsum += g;
                gworst = std::max(gworst, g);
                ++gaps;
            }
        }
        std::printf("| %g | %d | %zu | %.3f | %.3f | %d | %d | %.3f | %.3f |\n", z,
                    ImTypeset::GetParagraphLineCount(p), q.left.size(),
                    sum / static_cast<double>(q.left.size()), worst, over1, over2,
                    gaps ? gsum / gaps : 0.0, gworst);
    }

    // A continuous zoom: how many type sizes are there between z = 1 and 1.25?
    std::printf("\nA slow zoom, z from 1.00 to 1.25 in steps of 0.01. The layout's width "
                "follows z; the type is drawn at whichever whole-pixel size ImGui bakes. Sum of "
                "the widths of the first 40 glyph quads (a measure of the drawn type size), "
                "per z:\n\n| z | sum of quad widths px | the same over z |\n|---|---|---|\n");
    std::map<long, int> distinct;
    for (int k = 0; k <= 25; ++k) {
        const double z = 1.0 + 0.01 * k;
        layout_at(z);
        const Quads q = quads_of(p);
        double sum = 0.0;
        for (size_t i = 0; i < q.width.size() && i < 40; ++i)
            sum += q.width[i];
        ++distinct[std::lround(sum * 16.0)];
        std::printf("| %.2f | %.1f | %.2f |\n", z, sum, sum / z);
    }
    std::printf("\n%zu distinct drawings of the type over the 26 values of z\n", distinct.size());
    ImTypeset::DestroyParagraph(p);
    std::fflush(stdout);
}


// --- 5. A deep zoom: what the atlas holds ---------------------------------------------

void deep(soft_raster::Renderer& renderer)
{
    std::printf("\n### 5. A deep zoom of one paragraph (5 lines, 246 quads at z = 1)\n\n"
                "| z | em px | LayoutParagraph | first draw ms | third draw us | drew | atlas "
                "texels after | MiB as RGBA8 |\n|---|---|---|---|---|---|---|---|\n");
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, PICTURE_TEXT, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    soft_raster::Image tiny(4, 4, IM_COL32(255, 255, 255, 255));
    for (const double z : {1.0, 2.0, 4.0, 8.0, 16.0, 24.0}) {
        ImTypesetStyle s = style_at(z, true);
        const bool laid = ImTypeset::LayoutParagraph(p, static_cast<float>(420.0 * z), &s);
        double first = 0.0, third = 0.0;
        bool drew = false;
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            const double a = bench::now_us();
            drew = ImTypeset::DrawParagraph(p, list, ImVec2(0.0f, 0.0f));
            const double b = bench::now_us();
            ImGui::Render();
            renderer.render(ImGui::GetDrawData(), tiny); // honours the texture requests
            (frame == 0 ? first : third) = b - a;
        }
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        const int w = atlas->TexData ? atlas->TexData->Width : 0;
        const int h = atlas->TexData ? atlas->TexData->Height : 0;
        std::printf("| %g | %g | %s | %.2f | %.0f | %s | %d x %d | %.1f |\n", z, 16.0 * z,
                    laid ? "ok" : "false", first * 1.0e-3, third, drew ? "yes" : "NO", w, h,
                    static_cast<double>(w) * h * 4.0 / (1024.0 * 1024.0));
        std::fflush(stdout);
    }
    ImTypeset::DestroyParagraph(p);
}

} // namespace

int main(int argc, char** argv)
{
    const char* out_dir = argc > 1 ? argv[1] : ".";
    bench::create_imgui(8000.0f, 4000.0f);
    soft_raster::Renderer renderer;
    renderer.init();
    if (!IMTYPESET_CHECKVERSION() || ImTypeset::CreateContext() == nullptr || !bench::add_fonts()) {
        std::fprintf(stderr, "setup: %s\n", ImTypeset::GetLastError()->Message);
        return 1;
    }
    std::vector<std::string> paragraphs;
    if (!bench::load_sample(paragraphs))
        return 1;
    bench::header("zoom probe (scratch, build/vector-output)");
    atlas_line("at start");

    const bench::Sections sections(argc, argv);
    if (sections.wants("invariance"))
        invariance(paragraphs);
    if (sections.wants("steps"))
        step_costs(paragraphs);
    if (sections.wants("labels"))
        labels(paragraphs);
    if (sections.wants("pictures"))
        pictures(out_dir, renderer);
    if (sections.wants("deep"))
        deep(renderer);
    atlas_line("at end");

    renderer.shutdown();
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return 0;
}
