// What it costs to get fonts ready: once per process, before the first frame.
//
// Every sample is taken in a fresh ImGui context and a fresh ImTypeset
// context, so nothing is resident but the operating system's file cache: the
// files are read from disk by the first sample and from memory by the rest.
//
//   AddFontFromFileTTF   the public call, per face: read the file, parse the
//                        tables layout needs, validate, rewrite the cmap in
//                        memory where the face has glyphs with no codepoint
//                        (the math face's variants), hand the bytes to the
//                        atlas. No glyph is rasterised here; ImGui 1.92 bakes
//                        on first use.
//   the math face, by    the same work through the internals, so that the
//   stage                reader, the cmap rewrite and the atlas have a row
//                        each. The reference does the rewrite with fontTools
//                        and saves a file; the design note records about a
//                        second for it.
//   first use            the first formula (builds the math face's metrics at
//                        three sizes) and the first frame that draws the
//                        sample (rasterises every glyph it uses, once).
//
// Take numbers from a Release build only.

#include "bench_common.h"

#include "context.h"
#include "font_loader.h"
#include "fontlib.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr int CONTEXTS = 40;

struct Fresh {
    Fresh()
    {
        bench::create_imgui(4000.0f, 4000000.0f);
        ImTypeset::CreateContext();
        bench::reference_style();
    }
    ~Fresh()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Fresh(const Fresh&) = delete;
    Fresh& operator=(const Fresh&) = delete;
};

bool bench_public()
{
    const std::vector<bench::FontFile>& files = bench::font_files();
    std::vector<bench::Samples> per_face(files.size());
    bench::Samples together;
    bool ok = true;
    for (int i = -2; i < CONTEXTS; ++i) {
        Fresh fresh;
        double sum = 0.0;
        for (size_t f = 0; f < files.size(); ++f) {
            const std::string path = bench::font_path(files[f].file);
            const double start = bench::now_us();
            ImFont* font = ImTypeset::AddFontFromFileTTF(files[f].face, path.c_str());
            const double took = bench::now_us() - start;
            ok = ok && font != nullptr;
            sum += took;
            if (i >= 0)
                per_face[f].add(took);
        }
        if (i >= 0)
            together.add(sum);
    }
    bench::table_head("AddFontFromFileTTF, a fresh context per sample");
    for (size_t f = 0; f < files.size(); ++f) {
        std::string what = std::string(files[f].file);
        long bytes = 0;
        if (std::FILE* file = std::fopen(bench::font_path(files[f].file).c_str(), "rb")) {
            std::fseek(file, 0, SEEK_END);
            bytes = std::ftell(file);
            std::fclose(file);
        }
        char note[80];
        std::snprintf(note, sizeof note, "%ld bytes", bytes);
        bench::row(what.c_str(), per_face[f], note);
    }
    bench::row("all six faces", together);
    return ok;
}

bool bench_stages()
{
    const std::string path = bench::font_path("latinmodern-math.otf");
    bench::Samples read;
    bench::Samples rewrite;
    bench::Samples atlas;
    bool ok = true;
    size_t rewritten = 0;
    for (int i = -2; i < CONTEXTS; ++i) {
        bench::create_imgui(800.0f, 600.0f);
        {
            FontLibrary library;
            std::string why;
            double start = bench::now_us();
            ok = library.register_font(Face::MATH, path.c_str(), &why) && ok;
            const double read_us = bench::now_us() - start;
            const FaceData* data = library.face_data(Face::MATH);
            if (data == nullptr) {
                std::fprintf(stderr, "%s\n", why.c_str());
                ImGui::DestroyContext();
                return false;
            }
            std::vector<uint8_t> bytes;
            start = bench::now_us();
            ok = build_atlas_font(*data, ATLAS_WCHAR_BITS, bytes, &why) && ok;
            const double rewrite_us = bench::now_us() - start;
            rewritten = bytes.size();
            start = bench::now_us();
            ok = add_face_to_atlas(ImGui::GetIO().Fonts, *data, 16.0f, nullptr, &why) != nullptr &&
                 ok;
            const double atlas_us = bench::now_us() - start;
            if (i >= 0) {
                read.add(read_us);
                rewrite.add(rewrite_us);
                atlas.add(atlas_us);
            }
        }
        ImGui::DestroyContext();
    }
    bench::table_head("The math face by stage (latinmodern-math.otf), internals");
    bench::row("FontLibrary::register_font: read and parse for layout", read);
    char note[100];
    std::snprintf(note, sizeof note, "%zu bytes out, in memory, no file written", rewritten);
    bench::row("build_atlas_font: the cmap rewrite alone", rewrite, note);
    bench::row("add_face_to_atlas: the rewrite again, validation, AddFontFromMemoryTTF", atlas);
    return ok;
}

bool bench_first_use()
{
    std::vector<std::string> texts;
    if (!bench::load_sample(texts))
        return false;
    const float measure = bench::px_of_pt(bench::MEASURE_PT);
    const std::string math = "The sum $\\sum_{k=0}^{n} k = \\frac{n(n+1)}{2}$ is the first one.";
    bench::Samples formula;
    bench::Samples formula_again;
    bench::Samples first_frame;
    bench::Samples second_frame;
    bench::Samples math_frame;
    bool ok = true;
    for (int i = -1; i < CONTEXTS; ++i) {
        Fresh fresh;
        ok = bench::add_fonts() && ok;

        ImTypesetParagraph* m = ImTypeset::CreateParagraph();
        double start = bench::now_us();
        ImTypeset::SetParagraphText(m, math.data(), math.data() + math.size(), nullptr, 0,
                                    ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(m, measure);
        const double formula_us = bench::now_us() - start;
        const ImTypesetFormula found = ImTypeset::GetParagraphFormula(m, 0);
        ok = found.TextEnd > found.TextStart && !found.Refused && ok;
        start = bench::now_us();
        ImTypeset::SetParagraphText(m, math.data(), math.data() + math.size(), nullptr, 0,
                                    ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(m, measure);
        const double again_us = bench::now_us() - start;

        std::vector<ImTypesetParagraph*> paragraphs;
        std::vector<float> tops;
        float y = 0.0f;
        for (const std::string& text : texts) {
            ImTypesetParagraph* p = ImTypeset::CreateParagraph();
            ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
            tops.push_back(y);
            ImTypeset::LayoutParagraph(p, measure);
            y += ImTypeset::GetParagraphSize(p).y + bench::PARAGRAPH_SKIP_PX;
            paragraphs.push_back(p);
        }
        double frame_us[2] = {0.0, 0.0};
        for (double& us : frame_us) {
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            start = bench::now_us();
            for (size_t k = 0; k < paragraphs.size(); ++k)
                ImTypeset::DrawParagraph(paragraphs[k], list, ImVec2(16.0f, 16.0f + tops[k]));
            us = bench::now_us() - start;
            ImGui::Render();
        }
        ImGui::NewFrame();
        start = bench::now_us();
        ImTypeset::DrawParagraph(m, ImGui::GetBackgroundDrawList(), ImVec2(16.0f, 16.0f));
        const double math_us = bench::now_us() - start;
        ImGui::Render();
        if (i >= 0) {
            formula.add(formula_us);
            formula_again.add(again_us);
            first_frame.add(frame_us[0]);
            second_frame.add(frame_us[1]);
            math_frame.add(math_us);
        }
    }
    bench::table_head("First use, a fresh context per sample");
    bench::row("the first formula: text to lines", formula,
               "builds the math face's metrics at three sizes");
    bench::row("the same text again", formula_again);
    bench::row("the first frame that draws the sample x1", first_frame,
               "rasterises every glyph it uses");
    bench::row("the second frame", second_frame);
    bench::row("the first frame that draws the formula's paragraph", math_frame,
               "the prose glyphs are baked already");
    return ok;
}

} // namespace

int main()
{
    {
        Fresh fresh;
        bench::header("bench_font");
        std::printf("- %d fresh contexts per row; font files under tests/data/fonts, in the "
                    "file cache after the first read\n",
                    CONTEXTS);
    }
    bool ok = bench_public();
    ok = bench_stages() && ok;
    ok = bench_first_use() && ok;
    if (!ok)
        std::fprintf(stderr, "bench_font: a font was refused or a formula was not set\n");
    return ok ? 0 : 1;
}
