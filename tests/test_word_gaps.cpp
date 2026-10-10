// Word spaces as they are drawn against word spaces as they were laid out.
//
// Layout gives every glue of a line a width. The draw loop never reads it: a
// space on the screen is what is left between the last glyph of one rigid
// span and the anchor of the next, so whatever a word gathered by rounding
// its gaps comes out of the space after it. This file measures that, for the
// reference's rule and for the product's, and holds the product's to a bound.
//
// IMTYPESET_WORD_GAPS_REPORT=1 prints the distributions the bound was chosen
// from (several candidate rules, worked from the commands by the arithmetic
// in `pens_under`); IMTYPESET_WORD_GAPS_SHOTS=<directory> writes pictures.

#include "golden.h"

#include "imtypeset.h"

#include "context.h"
#include "draw.h"
#include "shipout.h"
#include "units.h"

#include "support/soft_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
        io.DisplaySize = ImVec2(1600.0f, 1200.0f);
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

// The paragraphs of the three screenshots the collapsed spaces were seen in
// (docs/images/py-bundle-pptmstr-pane.png, demo-math.png, demo-paragraph.png),
// as plain text with formulas.
const char* const PANE_1 =
    "The breaker keeps a list of active nodes and, for every legal breakpoint, asks each of "
    "them how bad the line from there to here would be. With badness $b$, penalty $p$ and line "
    "penalty $l$ the demerits are $d = (l + b)^2 + p^2$ when $p \\geq 0$, and the total-fit "
    "path is the one that minimises $\\sum_i d_i$ over the whole paragraph.";
const char* const PANE_2 =
    "The measured cost is what matters to a pane that redraws every frame: a paragraph that "
    "has not changed should cost one call, not one call per styled run, and a paragraph that "
    "has changed should cost one layout. See the notes for the "
    "numbers, and $e^{i\\pi} + 1 = 0$ for no reason at all.";
const char* const DEMO_MATH =
    "Set in text style, $\\frac{1}{2} + x$ is a box on the baseline among the words of its "
    "sentence, and the line breaker moves it whole.";
const char* const DEMO_PARAGRAPH =
    "A formula written between dollar signs is set by the math engine and sits in its line "
    "as one box. The subset is small on purpose: enough for everyday output, with no macro "
    "processor behind it. Everything on this page is ordinary text, converted with math "
    "switched on, which is off unless a host asks for it.";
const char* const DEMO_LINES =
    "Lines are chosen for the whole paragraph at once. A greedy breaker fills each line "
    "as full as it will go and leaves the last decision to chance; this one weighs every "
    "way of breaking the paragraph and keeps the one whose worst line is least bad.";

std::vector<std::string> sample_paragraphs()
{
    std::vector<std::string> out;
    std::vector<uint8_t> bytes;
    const std::string path = golden::data_path("../../bench/data/sample.txt");
    if (!golden::read_file(path, bytes))
        return out;
    std::string now;
    std::string line;
    const auto flush = [&]() {
        if (now.size() > 200)
            out.push_back(now);
        now.clear();
    };
    for (size_t i = 0; i <= bytes.size(); ++i) {
        if (i < bytes.size() && bytes[i] != '\n') {
            line += static_cast<char>(bytes[i]);
            continue;
        }
        if (line.empty()) {
            flush();
        } else {
            if (!now.empty())
                now += ' ';
            now += line;
        }
        line.clear();
    }
    flush();
    return out;
}

// One glyph of a draw: where the commands have it and where it went.
struct Placed {
    Scaled x = 0;
    uint32_t codepoint = 0;
    double pen = 0.0;
    bool formula = false;
    // Its quad's two sides less its pen; both zero for a glyph with no quad.
    double ink0 = 0.0;
    double ink1 = 0.0;
    bool inked = false;
};

// A rigid span as the draw loop makes one: the glyphs between two things
// that are not glyphs.
struct SpanOf {
    size_t first = 0;
    size_t end = 0;
    // The glyph a led span is placed from: the last of the span before, when
    // that is a word's and only glue lies between. NONE otherwise.
    size_t led_by = NONE;
    static constexpr size_t NONE = static_cast<size_t>(-1);
};

// A glue with a glyph of its own line on each side.
struct Space {
    size_t before = 0; // the glyph before it
    size_t after = 0;  // and after
    Scaled width = 0;
    Scaled natural = 0;
    bool beside_formula = false;
};

struct Drawn {
    std::vector<Placed> glyphs;
    std::vector<SpanOf> spans;
    std::vector<Space> spaces;
    float origin_x = 0.0f;
    double px_per_inch = 96.0;
};

double px_of(Scaled length, double px_per_inch)
{
    return px_from_sp(length, px_per_inch);
}

// Reads the draw the context last made against the commands it came of. The
// draw list must hold that draw and nothing else.
bool read_draw(const std::vector<DrawCmd>& cmds, const std::vector<DrawOp>& ops,
               const ImDrawList& list, Drawn& out)
{
    out.glyphs.clear();
    out.spans.clear();
    out.spaces.clear();
    size_t op = 0;
    int quad = 0;
    bool span_open = false;
    bool in_formula = false;
    uint32_t formula_index = 0;
    bool glue_pending = false;
    DrawCmd glue;
    bool have_glyph_on_line = false;
    size_t leader = SpanOf::NONE;
    for (const DrawCmd& cmd : cmds) {
        if (cmd.is(CmdType::GLYPH)) {
            if (op >= ops.size() || ops[op].type != OpType::TEXT)
                return false;
            Placed g;
            g.x = cmd.x;
            g.codepoint = cmd.codepoint;
            g.pen = static_cast<double>(ops[op].x0);
            g.formula = in_formula && cmd.index == formula_index;
            if (in_formula && !g.formula)
                in_formula = false;
            // Every glyph but a space has a bitmap in these faces, and the
            // clip rectangle is the whole plane, so it has a quad.
            if (cmd.codepoint != ' ' && cmd.codepoint != 0xA0) {
                if (quad + 3 >= list.VtxBuffer.Size)
                    return false;
                g.ink0 = static_cast<double>(list.VtxBuffer[quad].pos.x) - g.pen;
                g.ink1 = static_cast<double>(list.VtxBuffer[quad + 2].pos.x) - g.pen;
                g.inked = true;
                quad += 4;
            }
            ++op;
            const size_t at = out.glyphs.size();
            if (!span_open) {
                out.spans.push_back(SpanOf{at, at, leader});
                span_open = true;
            }
            out.spans.back().end = at + 1;
            if (glue_pending && have_glyph_on_line) {
                Space s;
                s.before = at - 1;
                s.after = at;
                s.width = glue.width;
                s.natural = glue.a;
                s.beside_formula = g.formula || out.glyphs[at - 1].formula;
                out.spaces.push_back(s);
            }
            glue_pending = false;
            have_glyph_on_line = true;
            out.glyphs.push_back(g);
            continue;
        }
        if (cmd.is(CmdType::RULE_RECT)) {
            if (op >= ops.size() || ops[op].type != OpType::RECT_FILLED)
                return false;
            ++op;
            quad += 4;
            // Under the product's rules a rule inside a formula does not end
            // the span; a rule anywhere else does, and so does one under the
            // reference's. A formula's glyphs are not re-placed by anything
            // here, so one answer serves.
            if (!in_formula) {
                span_open = false;
                leader = SpanOf::NONE;
            }
            continue;
        }
        if (cmd.is(CmdType::SET_GLUE)) {
            if (span_open)
                leader = out.glyphs.back().formula ? SpanOf::NONE : out.glyphs.size() - 1;
        } else {
            leader = SpanOf::NONE;
        }
        span_open = false;
        if (cmd.is(CmdType::SET_GLUE)) {
            // Two glues in a row, or a formula between: not one space.
            glue_pending = !glue_pending && have_glyph_on_line;
            glue = cmd;
            in_formula = false;
        } else if (cmd.is(CmdType::ATOM)) {
            in_formula = true;
            formula_index = cmd.index;
        } else {
            in_formula = false;
            glue_pending = false;
            have_glyph_on_line = false;
        }
    }
    return op == ops.size() && quad == list.VtxBuffer.Size;
}

// A pen rule for the glyphs of a word. A formula's glyphs stay where the draw
// put them under every rule.
enum class Rule {
    // The reference's: each gap rounded, added to where the glyph before went.
    ADVANCE,
    // Each glyph at the anchor's pixel plus its own rounded distance from it.
    WHOLE,
    // ADVANCE, and a gap that takes the word past `bound` from its laid-out
    // width is rounded the other way.
    GREEDY,
    // ADVANCE, and then, while the word is more than `bound` wider or
    // narrower than it was laid out, the gap that was rounded furthest in
    // that direction is rounded the other way.
    NEAREST_TIE,
};

struct RuleSet {
    const char* name;
    Rule rule;
    double bound;
    // A word after a space is anchored by the rounded space from the word
    // before, held to the two pixels either side of its own place, in place
    // of the pixel nearest its own place.
    bool follow;
};

double snap(double v)
{
    return std::floor(v + 0.5);
}

// The pens of `drawn`'s glyphs under `rule`, by the draw loop's arithmetic.
std::vector<double> pens_under(const Drawn& drawn, const RuleSet& set)
{
    std::vector<double> pens(drawn.glyphs.size());
    for (size_t i = 0; i < pens.size(); ++i)
        pens[i] = drawn.glyphs[i].pen;

    const double density = drawn.px_per_inch;
    for (const SpanOf& span : drawn.spans) {
        // The word: from the first glyph of the span that is not a formula's.
        size_t first = span.first;
        while (first < span.end && drawn.glyphs[first].formula)
            ++first;
        if (first >= span.end)
            continue;
        // Its first glyph is placed as the draw placed it: the anchor, or the
        // rounded gap from a formula's last glyph.
        if (set.follow && first == span.first && span.led_by != SpanOf::NONE) {
            const double place = device_coordinate(drawn.origin_x, drawn.glyphs[first].x, density);
            const double led =
                pens[span.led_by] +
                snap(px_of(drawn.glyphs[first].x - drawn.glyphs[span.led_by].x, density));
            pens[first] = std::min(std::max(led, std::floor(place)), std::ceil(place));
        } else if (first == span.first) {
            pens[first] = snap(device_coordinate(drawn.origin_x, drawn.glyphs[first].x, density));
        }
        const double anchor_pen = pens[first];
        const Scaled anchor_x = drawn.glyphs[first].x;
        // As the draw loop keeps them: in single precision.
        std::vector<float> excess(span.end - first, 0.0f);
        for (size_t i = first + 1; i < span.end; ++i) {
            const double step = px_of(drawn.glyphs[i].x - drawn.glyphs[i - 1].x, density);
            const double exact = px_of(drawn.glyphs[i].x - anchor_x, density);
            double rounded = snap(step);
            if (set.rule == Rule::WHOLE) {
                pens[i] = anchor_pen + snap(exact);
                continue;
            }
            if (set.rule == Rule::GREEDY) {
                const double drift = pens[i - 1] + rounded - anchor_pen - exact;
                if (drift > set.bound)
                    rounded -= 1.0;
                else if (drift < -set.bound)
                    rounded += 1.0;
            }
            pens[i] = pens[i - 1] + rounded;
            excess[i - first] = static_cast<float>(rounded - step);
        }
        if (set.rule != Rule::NEAREST_TIE)
            continue;
        const size_t last = span.end - 1;
        double drift = pens[last] - anchor_pen - px_of(drawn.glyphs[last].x - anchor_x, density);
        while (std::fabs(drift) > set.bound) {
            const float sign = drift > 0.0 ? 1.0f : -1.0f;
            size_t pick = 0;
            float most = 0.0f;
            for (size_t k = 1; k < excess.size(); ++k) {
                if (sign * excess[k] > most) {
                    most = sign * excess[k];
                    pick = k;
                }
            }
            if (pick == 0)
                break;
            // Never chosen again: it now leans the other way by more than half.
            excess[pick] -= sign;
            for (size_t i = first + pick; i < span.end; ++i)
                pens[i] -= static_cast<double>(sign);
            drift -= static_cast<double>(sign);
        }
    }
    return pens;
}

struct Tally {
    // Spaces between two words of text.
    long spaces = 0;
    long under_60 = 0; // drawn under 60 % of the width layout gave it
    long under_75 = 0;
    long off_by_over_1 = 0; // more than a pixel from it
    long off_by_over_15 = 0;
    double least_ratio = 1.0e9;
    double worst_narrow = 0.0; // px, the most a space lost
    double worst_wide = 0.0;
    double sum_sq = 0.0;
    long ink_under_2 = 0; // spaces whose ink-to-ink distance is under 2 px
    long ink_under_3 = 0;
    double least_ink = 1.0e9;
    // Pairs of glyphs side by side in a word.
    long pairs = 0;
    long uneven = 0; // more than half a pixel from their laid-out distance
    long uneven_60 = 0;
    long uneven_75 = 0;
    // The furthest a glyph of a word stands from its own place.
    double worst_place = 0.0;
    double worst_pair = 0.0;
    // Words.
    long words = 0;
    long words_off_1 = 0;
    double worst_word = 0.0;
    // The narrowest space, for the arithmetic.
    std::string narrowest;
    // What layout did, whatever the rule: spaces it set at under three
    // quarters of their natural width, and the narrowest it set.
    long set_under_75 = 0;
    double least_set = 1.0e9;
    // Spaces with a formula on one side or both.
    long by_formula = 0;
    long by_formula_over_1 = 0;
    double by_formula_narrow = 0.0;
    double by_formula_wide = 0.0;
};

std::string word_at(const Drawn& drawn, size_t glyph)
{
    for (const SpanOf& span : drawn.spans) {
        if (glyph < span.first || glyph >= span.end)
            continue;
        std::string out;
        for (size_t i = span.first; i < span.end; ++i) {
            const uint32_t c = drawn.glyphs[i].codepoint;
            out += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
        }
        return out;
    }
    return "";
}

void tally(const Drawn& drawn, const std::vector<double>& pens, const char* where, Tally& t)
{
    const double density = drawn.px_per_inch;
    for (const Space& s : drawn.spaces) {
        const Placed& a = drawn.glyphs[s.before];
        const Placed& b = drawn.glyphs[s.after];
        const double ideal = px_of(s.width, density);
        const double error = (pens[s.after] - pens[s.before]) - px_of(b.x - a.x, density);
        const double got = ideal + error;
        if (!(ideal > 0.0))
            continue;
        if (s.beside_formula) {
            ++t.by_formula;
            t.by_formula_over_1 += std::fabs(error) > 1.0;
            t.by_formula_narrow = std::min(t.by_formula_narrow, error);
            t.by_formula_wide = std::max(t.by_formula_wide, error);
            continue;
        }
        t.set_under_75 += ideal < 0.75 * px_of(s.natural, density);
        t.least_set = std::min(t.least_set, ideal);
        ++t.spaces;
        t.sum_sq += error * error;
        const double ratio = got / ideal;
        t.under_60 += ratio < 0.6;
        t.under_75 += ratio < 0.75;
        t.off_by_over_1 += std::fabs(error) > 1.0;
        t.off_by_over_15 += std::fabs(error) > 1.5;
        t.worst_narrow = std::min(t.worst_narrow, error);
        t.worst_wide = std::max(t.worst_wide, error);
        if (a.inked && b.inked) {
            const double ink = (pens[s.after] + b.ink0) - (pens[s.before] + a.ink1);
            t.ink_under_2 += ink < 2.0;
            t.ink_under_3 += ink < 3.0;
            t.least_ink = std::min(t.least_ink, ink);
        }
        if (ratio < t.least_ratio) {
            t.least_ratio = ratio;
            // Where the last glyph of the word before stands against its
            // place, split into its anchor's rounding and what the word
            // gathered; and the same for the anchor after.
            size_t first = s.before;
            for (const SpanOf& span : drawn.spans)
                if (s.before >= span.first && s.before < span.end)
                    first = span.first;
            const double x0 =
                static_cast<double>(drawn.origin_x) + px_of(drawn.glyphs[first].x, density);
            const double anchor_before = pens[first] - x0;
            const double gathered =
                (pens[s.before] - pens[first]) - px_of(a.x - drawn.glyphs[first].x, density);
            const double anchor_after =
                pens[s.after] - (static_cast<double>(drawn.origin_x) + px_of(b.x, density));
            t.narrowest = check::format(
                "%s: \"%s\" | \"%s\": glue natural %.3f set %.3f px; drawn %.3f = set "
                "%+.3f (anchor after) - %+.3f (anchor before) - %+.3f (gathered over %zu gaps)",
                where, word_at(drawn, s.before).c_str(), word_at(drawn, s.after).c_str(),
                px_of(s.natural, density), ideal, got, anchor_after, anchor_before, gathered,
                s.before - first);
        }
    }
    for (const SpanOf& span : drawn.spans) {
        size_t first = span.first;
        while (first < span.end && drawn.glyphs[first].formula)
            ++first;
        if (first >= span.end)
            continue;
        ++t.words;
        for (size_t i = first + 1; i < span.end; ++i) {
            const double e =
                (pens[i] - pens[i - 1]) - px_of(drawn.glyphs[i].x - drawn.glyphs[i - 1].x, density);
            ++t.pairs;
            t.uneven += std::fabs(e) > 0.5 + 1.0e-9;
            t.uneven_60 += std::fabs(e) > 0.6;
            t.uneven_75 += std::fabs(e) > 0.75;
            t.worst_pair = std::max(t.worst_pair, std::fabs(e));
        }
        for (size_t i = first; i < span.end; ++i)
            t.worst_place =
                std::max(t.worst_place, std::fabs(pens[i] - (static_cast<double>(drawn.origin_x) +
                                                             px_of(drawn.glyphs[i].x, density))));
        const size_t last = span.end - 1;
        const double drift = (pens[last] - pens[first]) -
                             px_of(drawn.glyphs[last].x - drawn.glyphs[first].x, density);
        t.words_off_1 += std::fabs(drift) >= 1.0;
        t.worst_word = std::max(t.worst_word, std::fabs(drift));
    }
}

void print_header()
{
    std::printf("    %-16s %7s %6s %6s %6s %6s %7s %7s %7s %5s | %5s %5s %6s | %7s %6s %6s | "
                "%6s %6s %6s | %5s %5s %5s\n",
                "rule", "spaces", "<60%", "<75%", ">1px", ">1.5", "least%", "narrow", "wide", "rms",
                "ink<2", "ink<3", "least", "pairs", ">.5px", "worst", "words", ">=1px", "worst",
                ">.6", ">.75", "place");
}

void print_row(const char* name, const Tally& t)
{
    std::printf("    %-16s %7ld %6ld %6ld %6ld %6ld %7.1f %7.2f %7.2f %5.2f | %5ld %5ld %6.1f | "
                "%7ld %6ld %6.2f | %6ld %6ld %6.2f | %5ld %5ld %5.2f\n",
                name, t.spaces, t.under_60, t.under_75, t.off_by_over_1, t.off_by_over_15,
                100.0 * t.least_ratio, t.worst_narrow, t.worst_wide,
                t.spaces > 0 ? std::sqrt(t.sum_sq / static_cast<double>(t.spaces)) : 0.0,
                t.ink_under_2, t.ink_under_3, t.least_ink, t.pairs, t.uneven, t.worst_pair, t.words,
                t.words_off_1, t.worst_word, t.uneven_60, t.uneven_75, t.worst_place);
}

const RuleSet CANDIDATES[] = {
    {"advance", Rule::ADVANCE, 0.0, false},
    {"whole", Rule::WHOLE, 0.0, false},
    {"greedy 0.5", Rule::GREEDY, 0.5, false},
    {"greedy 1.0", Rule::GREEDY, 1.0, false},
    {"tie 0.5", Rule::NEAREST_TIE, 0.5, false},
    {"tie 0.75", Rule::NEAREST_TIE, 0.75, false},
    {"tie 1.0", Rule::NEAREST_TIE, 1.0, false},
    {"advance+follow", Rule::ADVANCE, 0.0, true},
    {"tie 0.5+follow", Rule::NEAREST_TIE, 0.5, true},
    {"tie 0.75+follow", Rule::NEAREST_TIE, 0.75, true},
    {"tie 1.0+follow", Rule::NEAREST_TIE, 1.0, true},
};
constexpr size_t CANDIDATE_COUNT = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

struct Case {
    const char* name;
    std::string text;
    float width;
};

std::vector<Case> cases()
{
    std::vector<Case> out;
    out.push_back({"pane-1", PANE_1, 626.0f});
    out.push_back({"pane-2", PANE_2, 626.0f});
    out.push_back({"demo-math", DEMO_MATH, 460.0f});
    out.push_back({"demo-paragraph", DEMO_PARAGRAPH, 250.0f});
    out.push_back({"demo-lines", DEMO_LINES, 870.0f});
    for (const std::string& paragraph : sample_paragraphs()) {
        out.push_back({"sample", paragraph, 520.0f});
        out.push_back({"sample", paragraph, 337.0f});
    }
    return out;
}

// Lays out and draws one case at one size; false if it could not be read.
bool draw_case(Stage& stage, ImTypesetParagraph* p, const Case& c, float size, bool as_em,
               float origin_x, Drawn& drawn)
{
    ImTypesetStyle style;
    if (as_em)
        style.EmSize = size;
    else
        style.FontSize = size;
    style.Color = IM_COL32(0, 0, 0, 255);
    if (!ImTypeset::SetParagraphText(p, c.text.c_str(), nullptr, nullptr, 0,
                                     ImTypesetTextFlags_Math))
        return false;
    if (!ImTypeset::LayoutParagraph(p, c.width, &style))
        return false;
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    list->PushClipRect(ImVec2(-1.0e6f, -1.0e6f), ImVec2(1.0e6f, 1.0e6f));
    const bool ok = ImTypeset::DrawParagraph(p, list, ImVec2(origin_x, 8.0f), nullptr);
    list->PopClipRect();
    drawn.origin_x = origin_x;
    drawn.px_per_inch = p->layout_style.px_per_inch;
    const bool read = ok && read_draw(shipped_commands(*p), stage.ctx->drawer.ops(), *list, drawn);
    ImGui::Render();
    return read;
}

// What DrawRules::product() is, in the terms of `pens_under`.
const RuleSet PRODUCT = {"product", Rule::NEAREST_TIE, WORD_WIDTH_BOUND_PX, true};

long pens_that_differ(const Drawn& drawn, const RuleSet& set)
{
    const std::vector<double> pens = pens_under(drawn, set);
    long differ = 0;
    for (size_t i = 0; i < pens.size(); ++i)
        differ += pens[i] != drawn.glyphs[i].pen;
    return differ;
}

} // namespace

TEST(word_spaces_measured_under_each_rule)
{
    // The numbers the product's rule was chosen from, printed with
    // IMTYPESET_WORD_GAPS_REPORT set: the three paragraphs the closed spaces
    // were seen in and the benchmark's sample, justified, at 13 to 20 px as
    // an em and as a font size, at two origins. Each rule's row is worked
    // from the commands by `pens_under`, but for the last, which is what the
    // draw loop did under the product's rules. What is checked is that the
    // arithmetic of `pens_under` is the draw loop's: it reproduces every pen
    // of a draw under the reference's rules and of one under the product's.
    const bool report = std::getenv("IMTYPESET_WORD_GAPS_REPORT") != nullptr;
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const std::vector<Case> all = cases();
    REQUIRE(all.size() > 5);
    const size_t rows = CANDIDATE_COUNT + 1;
    for (const bool as_em : {true, false}) {
        std::vector<Tally> total(rows);
        for (int size = 13; size <= 20; ++size) {
            std::vector<Tally> at_size(rows);
            for (const Case& c : all) {
                for (const float origin : {8.0f, 8.375f}) {
                    CHECK_CONTEXT("%s at %d px (%s), width %g", c.name, size,
                                  as_em ? "em" : "font size", static_cast<double>(c.width));
                    const std::string where =
                        check::format("%s %d px %s w%g o%g", c.name, size, as_em ? "em" : "fs",
                                      static_cast<double>(c.width), static_cast<double>(origin));
                    Drawn drawn;
                    stage.ctx->draw_rules = DrawRules::reference();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    CHECK_EQ(pens_that_differ(drawn, CANDIDATES[0]), 0L);
                    for (size_t r = 0; r < CANDIDATE_COUNT; ++r) {
                        const std::vector<double> pens = pens_under(drawn, CANDIDATES[r]);
                        tally(drawn, pens, where.c_str(), at_size[r]);
                        tally(drawn, pens, where.c_str(), total[r]);
                    }
                    stage.ctx->draw_rules = DrawRules::product();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    CHECK_EQ(pens_that_differ(drawn, PRODUCT), 0L);
                    std::vector<double> pens(drawn.glyphs.size());
                    for (size_t i = 0; i < pens.size(); ++i)
                        pens[i] = drawn.glyphs[i].pen;
                    tally(drawn, pens, where.c_str(), at_size[CANDIDATE_COUNT]);
                    tally(drawn, pens, where.c_str(), total[CANDIDATE_COUNT]);
                }
            }
            if (report) {
                std::printf("  %s %d px\n", as_em ? "em" : "font size", size);
                print_header();
                for (size_t r = 0; r < rows; ++r)
                    print_row(r < CANDIDATE_COUNT ? CANDIDATES[r].name : "product() drawn",
                              at_size[r]);
                std::printf("    narrowest under advance: %s\n", at_size[0].narrowest.c_str());
                std::printf("    narrowest under product: %s\n",
                            at_size[CANDIDATE_COUNT].narrowest.c_str());
            }
        }
        if (report) {
            std::printf("  %s, 13 to 20 px together\n", as_em ? "em" : "font size");
            print_header();
            for (size_t r = 0; r < rows; ++r) {
                print_row(r < CANDIDATE_COUNT ? CANDIDATES[r].name : "product() drawn", total[r]);
                std::printf("      narrowest: %s\n", total[r].narrowest.c_str());
            }
            const Tally& before = total[0];
            const Tally& after = total[CANDIDATE_COUNT];
            std::printf("    layout, whatever the rule: %ld of %ld spaces set under 75 %% of "
                        "their natural width, the narrowest set at %.2f px\n",
                        before.set_under_75, before.spaces, before.least_set);
            std::printf("    beside a formula, %ld spaces: reference %ld over a pixel off, "
                        "%+.2f to %+.2f px; product %ld, %+.2f to %+.2f px\n",
                        before.by_formula, before.by_formula_over_1, before.by_formula_narrow,
                        before.by_formula_wide, after.by_formula_over_1, after.by_formula_narrow,
                        after.by_formula_wide);
        }
    }
    ImTypeset::DestroyParagraph(p);
}

TEST(under_the_products_rules_a_space_is_within_a_pixel_and_a_half_of_its_laid_out_width)
{
    // What DrawRules::product() promises of prose, on the same paragraphs,
    // sizes and origins: every word as wide as it was laid out to half a
    // pixel; every space between two words less than a pixel and a half from
    // the width layout gave it; every glyph on a whole pixel and no pair of
    // glyphs more than a pixel from its distance apart. And what it is for:
    // under the reference's rules a space is drawn at under 60 % of its
    // width many times more often.
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const std::vector<Case> all = cases();
    REQUIRE(all.size() > 5);
    Tally reference, product;
    for (const bool as_em : {true, false}) {
        for (int size = 13; size <= 20; ++size) {
            for (const Case& c : all) {
                for (const float origin : {8.0f, 8.375f}) {
                    CHECK_CONTEXT("%s at %d px (%s), width %g, origin %g", c.name, size,
                                  as_em ? "em" : "font size", static_cast<double>(c.width),
                                  static_cast<double>(origin));
                    Drawn drawn;
                    stage.ctx->draw_rules = DrawRules::reference();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    std::vector<double> pens(drawn.glyphs.size());
                    for (size_t i = 0; i < pens.size(); ++i)
                        pens[i] = drawn.glyphs[i].pen;
                    tally(drawn, pens, "", reference);

                    stage.ctx->draw_rules = DrawRules::product();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    for (size_t i = 0; i < pens.size(); ++i)
                        pens[i] = drawn.glyphs[i].pen;
                    Tally here;
                    tally(drawn, pens, "", here);
                    tally(drawn, pens, "", product);
                    CHECK(here.spaces > 0);
                    CHECK(here.worst_word <= WORD_WIDTH_BOUND_PX);
                    CHECK(here.worst_narrow > -1.5);
                    CHECK(here.worst_wide < 1.5);
                    CHECK(here.worst_pair <= 1.0);
                    long off_grid = 0;
                    for (const double pen : pens)
                        off_grid += pen != std::floor(pen);
                    CHECK_EQ(off_grid, 0L);
                }
            }
        }
    }
    std::printf("    reference: %ld of %ld spaces under 60 %% of their width, %ld over a pixel "
                "off, the narrowest %.0f %%, %.2f px lost; of %ld pairs of glyphs %ld over half "
                "a pixel off\n",
                reference.under_60, reference.spaces, reference.off_by_over_1,
                100.0 * reference.least_ratio, -reference.worst_narrow, reference.pairs,
                reference.uneven);
    std::printf("    product:   %ld of %ld spaces under 60 %% of their width, %ld over a pixel "
                "off, the narrowest %.0f %%, %.2f px lost; of %ld pairs of glyphs %ld over half "
                "a pixel off (%ld over 0.6, %ld over 0.75)\n",
                product.under_60, product.spaces, product.off_by_over_1,
                100.0 * product.least_ratio, -product.worst_narrow, product.pairs, product.uneven,
                product.uneven_60, product.uneven_75);
    CHECK_EQ(reference.uneven, 0L);
    CHECK(reference.under_60 > 100);
    CHECK(product.under_60 * 10 < reference.under_60);
    CHECK(product.off_by_over_1 * 5 < reference.off_by_over_1);
    ImTypeset::DestroyParagraph(p);
}

TEST(under_the_products_rules_a_word_is_one_picture_wherever_it_stands)
{
    // Which gaps of a word are rounded the other way depends on the word
    // alone: at eight origins a sixteenth of a pixel apart, and so on either
    // side of every rounding of an anchor, each word's glyphs stand at the
    // same distances from its first.
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    stage.ctx->draw_rules = DrawRules::product();
    const std::vector<Case> all = cases();
    for (size_t n = 0; n < all.size() && n < 9; ++n) {
        for (const float size : {13.0f, 16.0f, 19.0f}) {
            CHECK_CONTEXT("%s at %g px", all[n].name, static_cast<double>(size));
            std::vector<double> first_shape;
            for (int step = 0; step < 8; ++step) {
                Drawn drawn;
                REQUIRE(draw_case(stage, p, all[n], size, false,
                                  8.0f + 0.0625f * static_cast<float>(2 * step + 1), drawn));
                std::vector<double> shape;
                for (const SpanOf& span : drawn.spans) {
                    size_t first = span.first;
                    while (first < span.end && drawn.glyphs[first].formula)
                        ++first;
                    for (size_t i = first; i < span.end; ++i)
                        shape.push_back(drawn.glyphs[i].pen - drawn.glyphs[first].pen);
                }
                if (step == 0)
                    first_shape = shape;
                CHECK(shape == first_shape);
            }
        }
    }
    ImTypeset::DestroyParagraph(p);
}

// --- By hand ----------------------------------------------------------------------------

namespace
{

// Every glyph but 'M' is in the face, baked at 20 px with an ascent of 12.
class PlainBinder : public FontBinder
{
public:
    void begin() override {}
    Binding bind(uint32_t, FontKey) override { return Binding::BOUND; }
    bool has_glyph(uint32_t, uint32_t, uint32_t codepoint) override { return codepoint != 'M'; }
    bool bake(uint32_t, FontKey, float* size, float* ascent) override
    {
        *size = 20.0f;
        *ascent = 12.0f;
        return true;
    }
};

// Hundredths of a pixel in scaled points, rounded down to one, at 72.27 px
// per inch, where a scaled point is 1/65536 px. The cases below keep clear of
// the ties where that rounding could tell.
Scaled hundredths(int64_t n)
{
    return n * 16384 / 25;
}

std::vector<float> pens_of(const std::vector<DrawCmd>& commands, const DrawRules& rules,
                           float origin_x, DrawStats* stats = nullptr)
{
    DrawRun run;
    run.cmds = commands.data();
    run.count = commands.size();
    run.ink = 0xFF000000u;
    DrawArgs args;
    args.px_per_inch = PT_PER_INCH;
    args.origin = ImVec2(origin_x, 0.0f);
    args.rules = rules;
    Drawer drawer;
    PlainBinder binder;
    const DrawStats got = drawer.place(&run, 1, args, binder);
    if (stats != nullptr)
        *stats = got;
    std::vector<float> out;
    for (const DrawOp& op : drawer.ops())
        if (op.type == OpType::TEXT)
            out.push_back(op.x0);
    return out;
}

DrawRules only_held()
{
    DrawRules rules;
    rules.word_width_held = true;
    return rules;
}

DrawRules only_led()
{
    DrawRules rules;
    rules.word_spaces_led = true;
    return rules;
}

} // namespace

TEST(a_word_too_narrow_has_its_largest_shortfalls_rounded_up)
{
    // Gaps of 1.40, 1.45, 1.30 and 1.48 px each round to 1, and the word
    // comes out 4 px wide where it was laid out 5.63. Two gaps must give for
    // it to be within half a pixel: the two rounded down by the most, 1.48
    // and then 1.45, are rounded up, and the word is 6 wide, 0.37 over.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const int64_t at[] = {0, 140, 285, 415, 563};
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < 5; ++i)
        commands.push_back(DrawCmd::glyph(key, 'a' + i, hundredths(at[i]), y, i));
    CHECK(pens_of(commands, DrawRules::reference(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 12.0f, 13.0f, 14.0f}));
    CHECK(pens_of(commands, only_held(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 13.0f, 14.0f, 16.0f}));
    CHECK(pens_of(commands, DrawRules::product(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 13.0f, 14.0f, 16.0f}));
    // The same picture from an anchor that rounds the other way.
    CHECK(pens_of(commands, only_held(), 10.6f) ==
          (std::vector<float>{11.0f, 12.0f, 14.0f, 15.0f, 17.0f}));
    // A word that ends at the end of the commands, at a line, at a rule or at
    // glue is held alike.
    for (int ending = 0; ending < 3; ++ending) {
        std::vector<DrawCmd> ended = commands;
        if (ending == 0)
            ended.push_back(DrawCmd::line(0, y, 0, 0, 0, 5, 5));
        else if (ending == 1)
            ended.push_back(DrawCmd::rule_rect(0, y, UNITY, UNITY));
        else
            ended.push_back(DrawCmd::set_glue(hundredths(700), y, UNITY, UNITY, 0, 0, 5));
        ended.push_back(DrawCmd::glyph(key, 'z', hundredths(2000), y, 6));
        CHECK(pens_of(ended, only_held(), 10.0f) ==
              (std::vector<float>{10.0f, 11.0f, 13.0f, 14.0f, 16.0f, 30.0f}));
    }
}

TEST(a_word_too_wide_has_its_largest_excesses_rounded_down_the_earliest_of_equals_first)
{
    // Five gaps of 1.55 px each round to 2: 10 wide against 7.75 laid out,
    // 2.25 over. Two give, the first two, and it is 8 wide, 0.25 over.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < 6; ++i)
        commands.push_back(DrawCmd::glyph(key, 'a' + i, hundredths(155) * i, y, i));
    CHECK(pens_of(commands, DrawRules::reference(), 0.0f) ==
          (std::vector<float>{0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f}));
    CHECK(pens_of(commands, only_held(), 0.0f) ==
          (std::vector<float>{0.0f, 1.0f, 2.0f, 4.0f, 6.0f, 8.0f}));
    // A word exactly half a pixel out is left alone: one gap of 1.5.
    std::vector<DrawCmd> half = {DrawCmd::glyph(key, 'a', 0, y, 0),
                                 DrawCmd::glyph(key, 'b', hundredths(150), y, 1)};
    REQUIRE_EQ(half[1].x, 3 * UNITY / 2);
    CHECK(pens_of(half, only_held(), 0.0f) == (std::vector<float>{0.0f, 2.0f}));
}

TEST(a_glyph_that_is_not_drawn_is_not_a_gap_of_the_word)
{
    // 'M' is missing and takes no op: the gap either side of it is one gap,
    // of 2.9 px, rounded to 3. With the two of 1.45 that round to 1 the word
    // is 5 wide against 5.8; one gap gives, the first of the two.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const Scaled step = hundredths(145);
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(key, 'a', 0, y, 0),        DrawCmd::glyph(key, 'M', step, y, 1),
        DrawCmd::glyph(key, 'c', 2 * step, y, 2), DrawCmd::glyph(key, 'd', 3 * step, y, 3),
        DrawCmd::glyph(key, 'e', 4 * step, y, 4),
    };
    DrawStats stats;
    CHECK(pens_of(commands, DrawRules::reference(), 0.0f, &stats) ==
          (std::vector<float>{0.0f, 3.0f, 4.0f, 5.0f}));
    CHECK_EQ(stats.missing, 1);
    CHECK(pens_of(commands, only_held(), 0.0f) == (std::vector<float>{0.0f, 3.0f, 5.0f, 6.0f}));
}

TEST(a_list_with_an_index_repeated_and_half_its_glyphs_missing_is_held_like_any_other)
{
    // Not what ship-out makes: every command carries one index and every
    // other glyph is missing. The hold works from the glyphs that were
    // placed and nothing else: a hundred of them 2.98 px apart, each gap
    // rounded up to 3, are 1.98 px too wide together, and the first two gaps
    // give.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < 200; ++i)
        commands.push_back(DrawCmd::glyph(key, i % 2 == 0 ? 'a' : 'M', hundredths(149) * i, y, 7));
    DrawStats stats;
    const std::vector<float> pens = pens_of(commands, DrawRules::product(), 0.0f, &stats);
    REQUIRE_EQ(pens.size(), size_t(100));
    CHECK_EQ(stats.missing, 100);
    CHECK_EQ(pens[1], 2.0f);
    CHECK_EQ(pens[2], 4.0f);
    for (size_t i = 3; i < pens.size(); ++i)
        CHECK_EQ(pens[i], static_cast<float>(3 * i - 2));
}

TEST(a_word_of_twenty_thousand_glyphs_is_held_and_the_work_is_not_its_length_times_its_drift)
{
    // Every gap is 1.45 px and rounds to 1: the word is some nine thousand
    // pixels narrower than it was laid out, and that many gaps give. They are
    // all rounded alike, so they are the first that many. Placing it a
    // hundred times takes no time worth measuring when each is one pass over
    // the word, and minutes when each is a pass a pixel.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const uint32_t glyphs = 20000;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < glyphs; ++i)
        commands.push_back(DrawCmd::glyph(key, 'a', hundredths(145) * i, y, i));
    std::vector<float> pens;
    for (int pass = 0; pass < 100; ++pass)
        pens = pens_of(commands, DrawRules::product(), 0.0f);
    REQUIRE_EQ(pens.size(), size_t(glyphs));
    const double laid_out = px_from_sp(commands.back().x, PT_PER_INCH);
    CHECK(std::fabs(static_cast<double>(pens.back()) - laid_out) <= WORD_WIDTH_BOUND_PX);
    size_t wide = 0;
    bool in_order = true;
    for (size_t i = 1; i < pens.size(); ++i) {
        const float gap = pens[i] - pens[i - 1];
        if (gap == 2.0f) {
            in_order = in_order && wide == i - 1;
            ++wide;
        } else {
            in_order = in_order && gap == 1.0f;
        }
    }
    CHECK(in_order);
    CHECK_EQ(wide, static_cast<size_t>(std::ceil(laid_out - 19999.0 - WORD_WIDTH_BOUND_PX)));
}

TEST(a_word_after_a_space_is_placed_from_the_word_before_and_kept_beside_its_own_place)
{
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const auto glue = [&](int64_t at, uint32_t index) {
        return DrawCmd::set_glue(hundredths(at), y, UNITY, UNITY, 0, 0, index);
    };
    const std::vector<DrawCmd> commands = {
        // "ab": anchored at snap(10.3) = 10; b a gap of 1.2 on, at 11, half a
        // pixel left of its place at 11.5.
        DrawCmd::glyph(key, 'a', hundredths(30), y, 0),
        DrawCmd::glyph(key, 'b', hundredths(150), y, 1),
        glue(250, 2),
        // "cd": c is 3.4 px on from b. Led: 11 + 3 = 14, and 14.9 lies
        // between 14 and 15, so 14. On its own it is at snap(14.9) = 15, and
        // the space is a pixel wider than it was laid out.
        DrawCmd::glyph(key, 'c', hundredths(490), y, 3),
        DrawCmd::glyph(key, 'd', hundredths(630), y, 4), // 1.4 on: 15, its place 16.3
        glue(730, 5),
        glue(800, 6), // two glues are still only space
        // "e": 3.4 px on from d. Led: 15 + 3 = 18, which is not beside its
        // place at 19.7: held to 19.
        DrawCmd::glyph(key, 'e', hundredths(970), y, 7),
        DrawCmd::line(0, y, 0, 0, 0, 8, 12),
        // A line starts from its own place: snap(14.9) = 15.
        DrawCmd::glyph(key, 'f', hundredths(490), y, 8),
        glue(600, 9),
        DrawCmd::rule_rect(hundredths(600), y, UNITY, UNITY),
        // After glue and a rule, its own place: snap(18.4) = 18. Led from f it
        // would be 15 + snap(3.5) = 19.
        DrawCmd::glyph(key, 'g', hundredths(840), y, 10),
        glue(900, 11),
        // A place on a whole pixel has no pixel either side: 10 + 12 = 22.
        // Led from g it would be 18 + snap(3.4) = 21.
        DrawCmd::glyph(key, 'h', hundredths(1200), y, 12),
    };
    CHECK(pens_of(commands, DrawRules::reference(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 15.0f, 16.0f, 20.0f, 15.0f, 18.0f, 22.0f}));
    CHECK(pens_of(commands, only_led(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f, 19.0f, 15.0f, 18.0f, 22.0f}));
    CHECK(pens_of(commands, DrawRules::product(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f, 19.0f, 15.0f, 18.0f, 22.0f}));
}

TEST(a_word_is_led_from_where_the_word_before_ended_after_it_was_held)
{
    // "abc": two gaps of 1.45 round to 1, 2 wide against 2.9; the first
    // gives: 0, 2, 3. The next word is 3.6 px on from c, its place 6.5: led
    // from 3 it is at 7. Led from 2, where c stood before the word was held,
    // it would be at 6, which is beside its place too.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < 3; ++i)
        commands.push_back(DrawCmd::glyph(key, 'a' + i, hundredths(145) * i, y, i));
    commands.push_back(DrawCmd::set_glue(hundredths(400), y, UNITY, UNITY, 0, 0, 3));
    commands.push_back(DrawCmd::glyph(key, 'e', 2 * hundredths(145) + hundredths(360), y, 4));
    CHECK(pens_of(commands, DrawRules::product(), 0.0f) ==
          (std::vector<float>{0.0f, 2.0f, 3.0f, 7.0f}));
    CHECK(pens_of(commands, only_led(), 0.0f) == (std::vector<float>{0.0f, 1.0f, 2.0f, 6.0f}));
}

TEST(a_formula_neither_leads_a_word_nor_is_led_and_its_glyphs_are_not_of_the_word)
{
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const auto glue = [&](int64_t at, uint32_t index) {
        return DrawCmd::set_glue(hundredths(at), y, UNITY, UNITY, 0, 0, index);
    };
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(key, 'a', hundredths(30), y, 0),  // 10
        DrawCmd::glyph(key, 'b', hundredths(150), y, 1), // 11
        glue(250, 2),
        // A formula 3.4 px on from b, its box at 14.9: on its own pixel, 15,
        // where a word would have been led to 14. Its two glyphs from the
        // box's edge: 15 + snap(0) and 15 + snap(1.45) = 16.
        DrawCmd::atom(hundredths(490), y, hundredths(300), hundredths(300), 0, 0, 3),
        DrawCmd::glyph(key, 'x', hundredths(490), y, 3),
        DrawCmd::glyph(key, 'y', hundredths(635), y, 3),
        // A comma straight after it is a word of one glyph in the formula's
        // span, placed from the formula's last glyph: 16 + snap(1.45) = 17.
        DrawCmd::glyph(key, ',', hundredths(780), y, 4),
        glue(850, 5),
        // And it leads: 4.1 px on, 17 + 4 = 21, its place 21.9.
        DrawCmd::glyph(key, 'c', hundredths(1190), y, 6),
        glue(1300, 7),
        DrawCmd::atom(hundredths(1400), y, hundredths(100), hundredths(100), 0, 0, 8),
        DrawCmd::glyph(key, 'z', hundredths(1400), y, 8), // 24
        glue(1500, 9),
        // After a formula with nothing after it, a word's own place:
        // snap(27.9) = 28. Led from z it would be 24 + snap(3.4) = 27.
        DrawCmd::glyph(key, 'd', hundredths(1790), y, 10),
    };
    CHECK(pens_of(commands, DrawRules::product(), 10.0f) ==
          (std::vector<float>{10.0f, 11.0f, 15.0f, 16.0f, 17.0f, 21.0f, 24.0f, 28.0f}));
}

TEST(pictures_of_the_three_paragraphs_for_whoever_sets_IMTYPESET_WORD_GAPS_SHOTS)
{
    // Not a check: pixels to look at. With IMTYPESET_WORD_GAPS_SHOTS=<directory>
    // it writes, for each of the paragraphs the closed spaces were seen in at
    // the size and width they were seen at, the paragraph under the
    // reference's rules above the same under the product's, three times
    // enlarged, and the two at their own size side by side.
    const char* const directory = std::getenv("IMTYPESET_WORD_GAPS_SHOTS");
    if (directory == nullptr || directory[0] == '\0') {
        std::printf("    SKIPPED: IMTYPESET_WORD_GAPS_SHOTS is not set\n");
        return;
    }
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    soft_raster::Renderer renderer;
    renderer.init();
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const struct {
        const char* name;
        const char* text;
        float font_size;
        float width;
    } shots[] = {
        {"pane", PANE_1, 16.0f, 626.0f},
        {"pane-2", PANE_2, 16.0f, 626.0f},
        {"demo-math", DEMO_MATH, 19.0f, 460.0f},
        {"demo-paragraph", DEMO_PARAGRAPH, 19.0f, 250.0f},
        {"demo-math-refused",
         "Set in text style, $\\frac{1}{2} + \\hat{x}$ is a box on the baseline among the words of "
         "its sentence, and the line breaker moves it whole.",
         19.0f, 460.0f},
        {"pane", PANE_1, 13.0f, 520.0f},
        {"demo-paragraph", DEMO_PARAGRAPH, 17.0f, 250.0f},
    };
    const int zoom = 3;
    for (const auto& shot : shots) {
        ImTypesetStyle style;
        style.FontSize = shot.font_size;
        style.Color = IM_COL32(0xE6, 0xE6, 0xE6, 255);
        // Either answers false for a formula that was refused, and the text
        // is laid out all the same, with the formula as its source.
        ImTypeset::SetParagraphText(p, shot.text, nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, shot.width, &style);
        const ImVec2 size = ImTypeset::GetParagraphSize(p);
        const int width = static_cast<int>(shot.width) + 16;
        const int height = static_cast<int>(size.y) + 16;
        soft_raster::Image pages[2];
        for (const bool product : {false, true}) {
            stage.ctx->draw_rules = product ? DrawRules::product() : DrawRules::reference();
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            list->PushClipRect(ImVec2(-1.0e6f, -1.0e6f), ImVec2(1.0e6f, 1.0e6f));
            REQUIRE(ImTypeset::DrawParagraph(p, list, ImVec2(8.0f, 8.0f), nullptr));
            list->PopClipRect();
            // Every space of the paragraph: the words either side, the width
            // layout set it to, the width it was drawn at, and the ink to ink
            // distance across it.
            Drawn drawn;
            drawn.origin_x = 8.0f;
            drawn.px_per_inch = p->layout_style.px_per_inch;
            REQUIRE(read_draw(shipped_commands(*p), stage.ctx->drawer.ops(), *list, drawn));
            std::printf("    %s, font size %g, %s:\n", shot.name,
                        static_cast<double>(shot.font_size), product ? "product" : "reference");
            const Space* narrowest = nullptr;
            double least = 1.0e9;
            for (const Space& s : drawn.spaces) {
                const Placed& before = drawn.glyphs[s.before];
                const Placed& after = drawn.glyphs[s.after];
                const double set = px_of(s.width, drawn.px_per_inch);
                const double got =
                    set + (after.pen - before.pen) - px_of(after.x - before.x, drawn.px_per_inch);
                std::printf(
                    "      %-14s | %-14s natural %.2f set %.2f drawn %5.2f (%+.2f) ink %.0f%s\n",
                    word_at(drawn, s.before).c_str(), word_at(drawn, s.after).c_str(),
                    px_of(s.natural, drawn.px_per_inch), set, got, got - set,
                    (after.pen + after.ink0) - (before.pen + before.ink1),
                    s.beside_formula ? "  (beside a formula)" : "");
                if (!s.beside_formula && got < least) {
                    least = got;
                    narrowest = &s;
                }
            }
            // The narrowest, glyph by glyph: where layout has each glyph of
            // the word before it, where it went, and the same for the first
            // glyph after.
            if (narrowest != nullptr) {
                size_t first = narrowest->before;
                for (const SpanOf& span : drawn.spans)
                    if (narrowest->before >= span.first && narrowest->before < span.end)
                        first = span.first;
                std::printf("      the narrowest, \"%s\" | \"%s\": glyph, its place, its pen\n",
                            word_at(drawn, narrowest->before).c_str(),
                            word_at(drawn, narrowest->after).c_str());
                for (size_t g = first; g <= narrowest->after; ++g) {
                    if (g > narrowest->before && g < narrowest->after)
                        continue;
                    const uint32_t c = drawn.glyphs[g].codepoint;
                    std::printf("        %c  %9.3f  %6.0f\n",
                                c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?',
                                8.0 + px_of(drawn.glyphs[g].x, drawn.px_per_inch),
                                drawn.glyphs[g].pen);
                }
            }
            ImGui::Render();
            soft_raster::Image page(1600, 1200, IM_COL32(0x1E, 0x1E, 0x1E, 255));
            renderer.render(ImGui::GetDrawData(), page);
            pages[product ? 1 : 0] = page;
        }
        soft_raster::Image tall(width * zoom, 2 * height * zoom, IM_COL32(0x1E, 0x1E, 0x1E, 255));
        soft_raster::Image wide(2 * width, height, IM_COL32(0x1E, 0x1E, 0x1E, 255));
        for (int half = 0; half < 2; ++half) {
            for (int y = 0; y < height * zoom; ++y)
                for (int x = 0; x < width * zoom; ++x)
                    std::memcpy(tall.pixel(x, half * height * zoom + y),
                                pages[half].pixel(x / zoom, y / zoom), 4);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    std::memcpy(wide.pixel(half * width + x, y), pages[half].pixel(x, y), 4);
        }
        const std::string stem =
            check::format("%s/word-gaps-%s-fs%.0f-w%.0f", directory, shot.name,
                          static_cast<double>(shot.font_size), static_cast<double>(shot.width));
        CHECK(soft_raster::write_png((stem + "-x3.png").c_str(), tall));
        CHECK(soft_raster::write_png((stem + ".png").c_str(), wide));
        std::printf("    wrote %s-x3.png (the reference's rules above, the product's below) and "
                    "%s.png (left, right)\n",
                    stem.c_str(), stem.c_str());
    }
    ImTypeset::DestroyParagraph(p);
    renderer.shutdown();
}
