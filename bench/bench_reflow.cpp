// What layout costs: text to material, material to lines at a width, and the
// two workloads where layout is on the per-frame path — a resize drag and text
// that arrives a word at a time.
//
// Everything is driven through the public API. LayoutParagraph is the whole
// of a reflow for one paragraph: break the material into lines, pack them,
// stack them, ship them out as draw commands and index those by line. The
// selection model is built at the first hit test after it, and has a row of
// its own. The reference's scripts/bench_reflow.py times the same three
// stages as `compose`, `ship` and `selection index`.
//
// A paragraph keeps one layout, the last. So a reflow is forced by laying out
// at a width other than the last one: samples alternate between the stated
// measure and one pixel more, which is also what a drag does.
//
// The last table is the growth of the breaker's cost with paragraph length:
// the sample's last paragraph (635 nodes in the reference) concatenated 1, 4,
// 16 and 64 times, justified and ragged right, by total fit and by first fit.
// Ragged right by total fit is the case with no work budget: a `fil` right
// skip makes every line feasible, so nothing prunes the active list.
//
// That table is taken three ways. First as a host gets it: the product's
// rules and the default work budget. Then the optimiser alone, its budget at
// the maximum so that it is not cut short, with one active node per line
// count (the reference's classes) and with tex.web's easy_line
// (TexRules::easy_line, which the product's rules have on). Those two reach
// into the context for the rule and the budget; nothing else here does.
//
// Take numbers from a Release build only.

#include "bench_common.h"

#include "context.h"
#include "linebreak.h"
#include "tex_rules.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

struct Paragraphs {
    std::vector<ImTypesetParagraph*> items;

    explicit Paragraphs(const std::vector<std::string>& texts)
    {
        for (const std::string& text : texts) {
            ImTypesetParagraph* p = ImTypeset::CreateParagraph();
            ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
            items.push_back(p);
        }
    }
    ~Paragraphs()
    {
        for (ImTypesetParagraph* p : items)
            ImTypeset::DestroyParagraph(p);
    }
    Paragraphs(const Paragraphs&) = delete;
    Paragraphs& operator=(const Paragraphs&) = delete;

    int layout(float width, const ImTypesetStyle* style = nullptr)
    {
        int lines = 0;
        for (ImTypesetParagraph* p : items) {
            ImTypeset::LayoutParagraph(p, width, style);
            lines += ImTypeset::GetParagraphLineCount(p);
        }
        return lines;
    }
};

std::string counts(ImU64 layouts_before, ImU64 builds_before, const char* more = "")
{
    const ImTypesetStats stats = ImTypeset::GetStats();
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "%d layouts, %d text builds in all%s",
                  static_cast<int>(stats.Layouts - layouts_before),
                  static_cast<int>(stats.TextBuilds - builds_before), more);
    return buffer;
}

void bench_document(const std::vector<std::string>& texts)
{
    char heading[160];
    std::snprintf(heading, sizeof heading, "The whole sample, %zu paragraphs", texts.size());
    bench::table_head(heading);
    Paragraphs doc(texts);
    const float measure = bench::px_of_pt(bench::MEASURE_PT);
    char what[160];
    char note[160];

    {
        const ImU64 layouts = ImTypeset::GetStats().Layouts;
        const ImU64 builds = ImTypeset::GetStats().TextBuilds;
        bench::Samples s = bench::sample(300, 5, [&]() {
            for (size_t i = 0; i < texts.size(); ++i)
                ImTypeset::SetParagraphText(doc.items[i], texts[i].data(),
                                            texts[i].data() + texts[i].size());
            doc.layout(measure);
        });
        bench::row("text to lines: SetParagraphText + LayoutParagraph, 420 pt", s,
                   counts(layouts, builds).c_str());
    }
    static const double widths[] = {300.0, 420.0, 800.0};
    for (const double pt : widths) {
        const float width = bench::px_of_pt(pt);
        int lines = 0;
        int flip = 0;
        doc.layout(width + 1.0f);
        bench::Samples s = bench::sample(400, 6, [&]() {
            lines = doc.layout(width + static_cast<float>(flip));
            flip ^= 1;
        });
        std::snprintf(what, sizeof what, "reflow at a new width, %.0f pt", pt);
        std::snprintf(note, sizeof note, "%d lines", lines);
        bench::row(what, s, note);
    }
    {
        int flip = 0;
        int sink = 0;
        bench::Samples s = bench::sample(400, 6, [&]() {
            doc.layout(measure + static_cast<float>(flip));
            flip ^= 1;
            for (ImTypesetParagraph* p : doc.items)
                sink += ImTypeset::HitTestParagraph(p, ImVec2(10.0f, 10.0f)).TextOffset;
        });
        bench::row("reflow, 420 pt, and the selection model of every paragraph", s,
                   sink < 0 ? "?" : "");
    }
    {
        doc.layout(measure);
        const ImU64 layouts = ImTypeset::GetStats().Layouts;
        bench::Samples s = bench::sample(20000, 10, [&]() { doc.layout(measure); });
        bench::row("LayoutParagraph with nothing changed, all paragraphs", s,
                   counts(layouts, ImTypeset::GetStats().TextBuilds).c_str());
    }
}

void bench_heaviest(const std::vector<std::string>& texts)
{
    const std::string& text = texts.back();
    const float measure = bench::px_of_pt(bench::MEASURE_PT);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
    ImTypeset::LayoutParagraph(p, measure);
    char heading[200];
    std::snprintf(heading, sizeof heading,
                  "The sample's last paragraph alone, 420 pt: %zu bytes, %d lines", text.size(),
                  ImTypeset::GetParagraphLineCount(p));
    bench::table_head(heading);

    {
        bench::Samples s = bench::sample(3000, 5, [&]() {
            ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
            ImTypeset::LayoutParagraph(p, measure);
        });
        bench::row("text to lines", s);
    }
    static const struct {
        const char* what;
        ImTypesetAlign align;
        ImTypesetLineBreak line_break;
        int count;
    } modes[] = {
        {"reflow, justified, total fit (the default)", ImTypesetAlign_Justify,
         ImTypesetLineBreak_Auto, 3000},
        {"reflow, justified, first fit", ImTypesetAlign_Justify, ImTypesetLineBreak_FirstFit, 3000},
        {"reflow, ragged right, first fit (the default)", ImTypesetAlign_Left,
         ImTypesetLineBreak_Auto, 3000},
        // Three orders of magnitude slower than the rows above: fewer samples.
        {"reflow, ragged right, total fit", ImTypesetAlign_Left, ImTypesetLineBreak_TotalFit, 100},
    };
    for (const auto& mode : modes) {
        ImTypesetStyle style = ImTypeset::GetStyle();
        style.Align = mode.align;
        style.LineBreak = mode.line_break;
        int flip = 0;
        bench::Samples s = bench::sample(mode.count, 6, [&]() {
            ImTypeset::LayoutParagraph(p, measure + static_cast<float>(flip), &style);
            flip ^= 1;
        });
        char note[60];
        std::snprintf(note, sizeof note, "%d lines", ImTypeset::GetParagraphLineCount(p));
        bench::row(mode.what, s, note);
    }
    ImTypeset::DestroyParagraph(p);

    // A paragraph with formulas in it: the text build parses and sets each.
    {
        const std::string math =
            "The roots of $ax^2 + bx + c = 0$ are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$, "
            "and the sum $\\sum_{k=0}^{n} k = \\frac{n(n+1)}{2}$ is what every proof by "
            "induction starts with; neither is hard, and both are set here among the words of "
            "a justified paragraph, at the size of the text around them.";
        ImTypesetParagraph* m = ImTypeset::CreateParagraph();
        ImTypeset::SetParagraphText(m, math.data(), math.data() + math.size(), nullptr, 0,
                                    ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(m, measure);
        int refused = 0;
        const int formulas = ImTypeset::GetParagraphFormulaCount(m);
        for (int i = 0; i < formulas; ++i) {
            if (ImTypeset::GetParagraphFormula(m, i).Refused)
                ++refused;
        }
        bench::Samples built = bench::sample(3000, 5, [&]() {
            ImTypeset::SetParagraphText(m, math.data(), math.data() + math.size(), nullptr, 0,
                                        ImTypesetTextFlags_Math);
            ImTypeset::LayoutParagraph(m, measure);
        });
        char note[100];
        std::snprintf(note, sizeof note, "%d formulas, %d refused, %d lines; math fonts resident",
                      formulas, refused, ImTypeset::GetParagraphLineCount(m));
        bench::row("text to lines, a paragraph with three formulas", built, note);
        int flip = 0;
        bench::Samples reflow = bench::sample(3000, 6, [&]() {
            ImTypeset::LayoutParagraph(m, measure + static_cast<float>(flip));
            flip ^= 1;
        });
        bench::row("reflow, the same paragraph", reflow);
        ImTypeset::DestroyParagraph(m);
    }
}

// Where each word of a text ends, in bytes.
std::vector<size_t> word_ends(const std::string& text)
{
    std::vector<size_t> ends;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != ' ' && (i + 1 == text.size() || text[i + 1] == ' '))
            ends.push_back(i + 1);
    }
    return ends;
}

// The document arrives a word at a time. Each arrival is one frame's work for
// the host: the paragraph being written is given its text again and laid out;
// the paragraphs before it are finished and are not touched.
void bench_stream(const std::vector<std::string>& texts)
{
    const float measure = bench::px_of_pt(bench::MEASURE_PT);
    constexpr int ROUNDS = 20;
    bench::Samples all;
    bench::Samples all_drawn;
    bench::Samples last;
    bench::Samples tail;
    int appends = 0;
    int last_words = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int round = -1; round < ROUNDS; ++round) {
            ImTypesetParagraph* p = ImTypeset::CreateParagraph();
            for (size_t t = 0; t < texts.size(); ++t) {
                const std::string& text = texts[t];
                const std::vector<size_t> ends = word_ends(text);
                for (size_t w = 0; w < ends.size(); ++w) {
                    ImGui::NewFrame();
                    ImDrawList* list = ImGui::GetBackgroundDrawList();
                    const double start = bench::now_us();
                    ImTypeset::SetParagraphText(p, text.data(), text.data() + ends[w]);
                    ImTypeset::LayoutParagraph(p, measure);
                    if (pass == 1)
                        ImTypeset::DrawParagraph(p, list, ImVec2(16.0f, 16.0f));
                    const double stop = bench::now_us();
                    ImGui::Render();
                    if (round < 0)
                        continue;
                    if (pass == 1) {
                        all_drawn.add(stop - start);
                        continue;
                    }
                    all.add(stop - start);
                    if (t + 1 == texts.size()) {
                        last.add(stop - start);
                        if (w + 10 >= ends.size())
                            tail.add(stop - start);
                    }
                    if (round == 0) {
                        ++appends;
                        last_words = static_cast<int>(ends.size());
                    }
                }
            }
            ImTypeset::DestroyParagraph(p);
        }
    }
    char heading[200];
    std::snprintf(heading, sizeof heading,
                  "Streaming: one word appended per frame, 420 pt, %d appends over %zu "
                  "paragraphs, %d rounds",
                  appends, texts.size(), ROUNDS);
    bench::table_head(heading);
    char note[120];
    std::snprintf(note, sizeof note, "max %.1f us", all.max());
    bench::row("append: SetParagraphText + LayoutParagraph, every append", all, note);
    std::snprintf(note, sizeof note, "max %.1f us", all_drawn.max());
    bench::row("append, then DrawParagraph of that paragraph, every append", all_drawn, note);
    std::snprintf(note, sizeof note, "%d words; max %.1f us", last_words, last.max());
    bench::row("append, the last paragraph's appends only", last, note);
    std::snprintf(note, sizeof note, "the paragraph at its longest; max %.1f us", tail.max());
    bench::row("append, the last ten words of the last paragraph", tail, note);
}

// `easy_line` is the rule the total-fit rows are taken under. With
// `whole_budget` the breaker's work budget is at its maximum and only the
// total-fit rows are taken; otherwise it is the default and a row that runs
// out says so.
void bench_growth(const std::vector<std::string>& texts, const char* title, bool easy_line,
                  bool whole_budget)
{
    const float measure = bench::px_of_pt(bench::MEASURE_PT);
    bench::table_head(title);
    ImTypesetContext* const ctx = ImTypeset::GetCurrentContext();
    ImTypeset::detail::LineBreaker& breaker = ctx->composer.breaker();
    ctx->tex_rules = ImTypeset::detail::TexRules::product();
    ctx->tex_rules.easy_line = easy_line;
    breaker.set_work_budget(whole_budget ? ImTypeset::detail::MAX_LINE_BREAK_WORK_BUDGET
                                         : ImTypeset::detail::DEFAULT_LINE_BREAK_WORK_BUDGET);
    static const struct {
        const char* what;
        ImTypesetAlign align;
        ImTypesetLineBreak line_break;
    } modes[] = {
        {"justified, total fit", ImTypesetAlign_Justify, ImTypesetLineBreak_Auto},
        {"justified, first fit", ImTypesetAlign_Justify, ImTypesetLineBreak_FirstFit},
        {"ragged right, first fit", ImTypesetAlign_Left, ImTypesetLineBreak_Auto},
        {"ragged right (fil right skip), total fit", ImTypesetAlign_Left,
         ImTypesetLineBreak_TotalFit},
    };
    static const int copies[] = {1, 4, 16, 64};
    for (const auto& mode : modes) {
        const bool total_fit = mode.line_break != ImTypesetLineBreak_FirstFit &&
                               (mode.align == ImTypesetAlign_Justify ||
                                mode.line_break == ImTypesetLineBreak_TotalFit);
        if (whole_budget && !total_fit)
            continue;
        ImTypesetStyle style = ImTypeset::GetStyle();
        style.Align = mode.align;
        style.LineBreak = mode.line_break;
        double previous_us = 0.0;
        for (const int count : copies) {
            char what[160];
            std::snprintf(what, sizeof what, "%s, x%d", mode.what, count);
            // Four times the text has cost up to sixty-four times as long.
            // Past this a row would take the afternoon.
            if (previous_us > 120.0e6) {
                std::printf("| %s | 0 | | | | | not run: the row before took %.0f s |\n", what,
                            previous_us / 1.0e6);
                continue;
            }
            std::string text;
            for (int i = 0; i < count; ++i) {
                if (i > 0)
                    text += ' ';
                text += texts.back();
            }
            ImTypesetParagraph* p = ImTypeset::CreateParagraph();
            ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size());
            int flip = 1;
            const double start = bench::now_us();
            ImTypeset::LayoutParagraph(p, measure + 1.0f, &style);
            const double first_us = bench::now_us() - start;
            // About two seconds a row, three samples at least; a single one
            // when one takes longer than ten seconds.
            int n = static_cast<int>(2.0e6 / (first_us > 1.0 ? first_us : 1.0));
            n = n > 2000 ? 2000 : n < 3 ? 3 : n;
            bench::Samples s;
            if (first_us > 10.0e6) {
                s.add(first_us);
            } else {
                s = bench::sample(n, first_us > 1.0e5 ? 0 : 2, [&]() {
                    flip ^= 1;
                    ImTypeset::LayoutParagraph(p, measure + static_cast<float>(flip), &style);
                });
            }
            previous_us = s.median();
            char note[240];
            const int lines = ImTypeset::GetParagraphLineCount(p);
            const bool fell_back =
                (ImTypeset::GetParagraphStatus(p) & ImTypesetStatusFlags_FirstFitFallback) != 0;
            // What the last layout cost the breaker, in its own units: a
            // layout after one that fell back asks for first fit outright.
            std::snprintf(note, sizeof note, "%zu bytes, %d lines, %.2f us/line, work %llu, %s%s",
                          text.size(), lines, lines > 0 ? previous_us / lines : 0.0,
                          static_cast<unsigned long long>(breaker.work_done()),
                          ImTypeset::GetParagraphError(p)->Code == ImTypesetError_None
                              ? "no error"
                              : ImTypeset::GetParagraphError(p)->Message,
                          fell_back ? "; over the work budget, broken by first fit" : "");
            bench::row(what, s, note);
            ImTypeset::DestroyParagraph(p);
        }
    }
    ctx->tex_rules = ImTypeset::detail::TexRules::product();
    breaker.set_work_budget(ImTypeset::detail::DEFAULT_LINE_BREAK_WORK_BUDGET);
}

} // namespace

int main(int argc, char** argv)
{
    bench::create_imgui(4000.0f, 4000.0f);
    if (ImTypeset::CreateContext() == nullptr || !bench::add_fonts()) {
        std::fprintf(stderr, "%s\n", ImTypeset::GetLastError()->Message);
        ImGui::DestroyContext();
        return 1;
    }
    bench::reference_style();
    bench::header("bench_reflow");
    std::printf("- measure %.0f pt = %.2f px at %.0f px per inch unless a row says otherwise; em "
                "%.0f px\n",
                bench::MEASURE_PT, static_cast<double>(bench::px_of_pt(bench::MEASURE_PT)),
                bench::PX_PER_INCH, static_cast<double>(bench::EM_PX));
    // Sections: document, heaviest, stream, growth. `--skip growth` leaves the
    // last table out: its largest rows take minutes.
    const bench::Sections sections(argc, argv);

    std::vector<std::string> texts;
    int status = 0;
    if (!bench::load_sample(texts)) {
        status = 1;
    } else {
        if (sections.wants("document"))
            bench_document(texts);
        if (sections.wants("heaviest"))
            bench_heaviest(texts);
        if (sections.wants("stream"))
            bench_stream(texts);
        if (sections.wants("growth")) {
            bench_growth(texts,
                         "Growth with paragraph length: the last paragraph concatenated, 420 pt "
                         "(product rules, default work budget)",
                         true, false);
            bench_growth(texts,
                         "Growth, total fit alone: a class per line count (the reference's), "
                         "work budget at its maximum",
                         false, true);
            bench_growth(texts,
                         "Growth, total fit alone: easy_line (the product's), work budget at its "
                         "maximum",
                         true, true);
        }
    }
    ImTypeset::DestroyContext();
    ImGui::DestroyContext();
    return status;
}
