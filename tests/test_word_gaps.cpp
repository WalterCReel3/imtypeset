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
    // What the product's rules place the span's word from, when anything:
    // the last glyph of the text before it, or the edge of the formula before
    // it. And the formula whose glyphs open the span, if a formula's do.
    size_t word_led_by_glyph = NONE;
    size_t word_led_by_formula = NONE;
    size_t formula = NONE;
};

// A formula as the commands have it: its box, the glyphs it shipped, which
// are side by side in `Drawn::glyphs`, and the ends of its rules with where
// they were drawn.
struct FormulaOf {
    Scaled x = 0;
    Scaled width = 0;
    size_t first = 0;
    size_t end = 0;
    struct End {
        Scaled x = 0;
        double pen = 0.0;
    };
    std::vector<End> rule_ends;
    // The part of it furthest right in the layout: a glyph's origin or a
    // rule's right-hand end, or the box's left edge if it has neither. What
    // comes after the formula is placed from this under the product's rules.
    Scaled rightmost = 0;
    // What the product's rules place its edge from, as for a span's word.
    size_t led_by_glyph = SpanOf::NONE;
    size_t led_by_formula = SpanOf::NONE;
};

// A glyph of text and a formula side by side on a line, with glue between
// them or with none.
struct Joint {
    size_t text = 0;
    size_t formula = 0;
    bool text_first = false;
    bool spaced = false;
};

// Two formulas side by side likewise.
struct FormulaPair {
    size_t before = 0;
    size_t after = 0;
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
    std::vector<FormulaOf> formulas;
    std::vector<Joint> joints;
    std::vector<FormulaPair> formula_pairs;
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
    out.formulas.clear();
    out.joints.clear();
    out.formula_pairs.clear();
    // What stands before on the line with nothing but glue since: nothing, a
    // glyph of text (the last in `glyphs`), or the formula `formula_at`.
    enum { NOTHING, TEXT, FORMULA } before = NOTHING;
    bool glue_since = false;
    size_t formula_at = SpanOf::NONE;
    // The product's leader: see SpanOf.
    size_t lead_glyph = SpanOf::NONE;
    size_t lead_formula = SpanOf::NONE;
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
                if (g.formula)
                    out.spans.back().formula = formula_at;
            }
            out.spans.back().end = at + 1;
            if (g.formula) {
                out.formulas[formula_at].end = at + 1;
                out.formulas[formula_at].rightmost =
                    std::max(out.formulas[formula_at].rightmost, cmd.x);
            } else {
                if (at == out.spans.back().first || out.glyphs[at - 1].formula) {
                    out.spans.back().word_led_by_glyph = lead_glyph;
                    out.spans.back().word_led_by_formula = lead_formula;
                }
                if (before == FORMULA)
                    out.joints.push_back(Joint{at, formula_at, false, glue_since});
                before = TEXT;
                glue_since = false;
            }
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
            if (in_formula) {
                // A rule under a pixel long may have been widened to one, by
                // its right-hand end.
                out.formulas[formula_at].rule_ends.push_back(
                    FormulaOf::End{cmd.x, static_cast<double>(ops[op].x0)});
                if (px_from_sp(cmd.width, out.px_per_inch) >= 1.0)
                    out.formulas[formula_at].rule_ends.push_back(
                        FormulaOf::End{cmd.x + cmd.width, static_cast<double>(ops[op].x1)});
            }
            if (in_formula) {
                out.formulas[formula_at].rightmost =
                    std::max(out.formulas[formula_at].rightmost, cmd.x + cmd.width);
            } else {
                before = NOTHING;
                lead_glyph = SpanOf::NONE;
                lead_formula = SpanOf::NONE;
            }
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
        if (cmd.is(CmdType::SET_GLUE) || cmd.is(CmdType::ATOM)) {
            if (span_open && !out.glyphs.back().formula) {
                lead_glyph = out.glyphs.size() - 1;
                lead_formula = SpanOf::NONE;
            }
            glue_since = glue_since || cmd.is(CmdType::SET_GLUE);
        } else {
            before = NOTHING;
            lead_glyph = SpanOf::NONE;
            lead_formula = SpanOf::NONE;
        }
        if (cmd.is(CmdType::ATOM)) {
            FormulaOf f;
            f.x = cmd.x;
            f.width = cmd.width;
            f.rightmost = cmd.x;
            f.first = f.end = out.glyphs.size();
            f.led_by_glyph = lead_glyph;
            f.led_by_formula = lead_formula;
            const size_t now = out.formulas.size();
            if (before == TEXT)
                out.joints.push_back(Joint{out.glyphs.size() - 1, now, true, glue_since});
            else if (before == FORMULA)
                out.formula_pairs.push_back(FormulaPair{formula_at, now});
            out.formulas.push_back(f);
            formula_at = now;
            before = FORMULA;
            glue_since = false;
            lead_glyph = SpanOf::NONE;
            lead_formula = now;
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
    // ADVANCE, and every glyph is held within a pixel of its laid-out
    // distance from the word's first: when one passes that, the gap rounded
    // furthest that way among those into the glyphs that have stood on that
    // side of their places without a break is rounded the other way; and
    // once more at the word's end if its last glyph is more than `bound` out.
    // DrawRules::word_glyphs_held.
    HELD_GLYPHS,
};

struct RuleSet {
    const char* name;
    Rule rule;
    double bound;
    // A word after a space is anchored by the rounded space from the word
    // before, held to the two pixels either side of its own place, in place
    // of the pixel nearest its own place.
    bool follow;
    // And so is text glued to the end of a formula, and a word after a space
    // after one, from the formula's edge; and a formula's edge is placed the
    // same way from what stands before it, with every glyph of the formula
    // at its own rounded distance from that edge.
    bool joints;
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
    // One pixel from the place of whatever leads it, and no further from its
    // own place than the pixel either side.
    const auto led_to = [&](double lead_pen, Scaled lead_x, Scaled x) {
        const double place = device_coordinate(drawn.origin_x, x, density);
        const double led = lead_pen + snap(px_of(x - lead_x, density));
        return std::min(std::max(led, std::floor(place)), std::ceil(place));
    };
    std::vector<double> edges(drawn.formulas.size(), 0.0);
    size_t edges_known = 0;
    // Where a formula's rightmost part went: its own rounded distance from
    // the edge, like every part of a formula.
    const auto rightmost_pen = [&](size_t f) {
        return edges[f] + snap(px_of(drawn.formulas[f].rightmost - drawn.formulas[f].x, density));
    };
    for (const SpanOf& span : drawn.spans) {
        if (set.joints) {
            // The edge of every formula that begins before this span ends:
            // what leads one is before it.
            for (; edges_known < drawn.formulas.size() &&
                   drawn.formulas[edges_known].first <= span.first;
                 ++edges_known) {
                const FormulaOf& f = drawn.formulas[edges_known];
                if (f.led_by_glyph != SpanOf::NONE)
                    edges[edges_known] =
                        led_to(pens[f.led_by_glyph], drawn.glyphs[f.led_by_glyph].x, f.x);
                else if (f.led_by_formula != SpanOf::NONE)
                    edges[edges_known] = led_to(rightmost_pen(f.led_by_formula),
                                                drawn.formulas[f.led_by_formula].rightmost, f.x);
                else
                    edges[edges_known] = snap(device_coordinate(drawn.origin_x, f.x, density));
            }
        }
        // The word: from the first glyph of the span that is not a formula's.
        size_t first = span.first;
        while (first < span.end && drawn.glyphs[first].formula) {
            if (set.joints)
                pens[first] =
                    edges[span.formula] +
                    snap(px_of(drawn.glyphs[first].x - drawn.formulas[span.formula].x, density));
            ++first;
        }
        if (first >= span.end)
            continue;
        // Its first glyph is placed as the draw placed it: the anchor, or the
        // rounded gap from a formula's last glyph.
        if (set.joints) {
            const Scaled x = drawn.glyphs[first].x;
            if (span.word_led_by_glyph != SpanOf::NONE)
                pens[first] =
                    led_to(pens[span.word_led_by_glyph], drawn.glyphs[span.word_led_by_glyph].x, x);
            else if (span.word_led_by_formula != SpanOf::NONE)
                pens[first] = led_to(rightmost_pen(span.word_led_by_formula),
                                     drawn.formulas[span.word_led_by_formula].rightmost, x);
            else
                pens[first] = snap(device_coordinate(drawn.origin_x, x, density));
        } else if (set.follow && first == span.first && span.led_by != SpanOf::NONE) {
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
        if (set.rule == Rule::HELD_GLYPHS) {
            // The best gap of the run of glyphs at or right of their places
            // that ends at the glyph placed last, and of the run at or left:
            // which gap of the word, and how far it was rounded that way.
            size_t up = 0, down = 0;
            float up_most = 0.0f, down_most = 0.0f;
            size_t placed = first;
            const auto give = [&](size_t gap, double sign) {
                excess[gap] -= static_cast<float>(sign);
                for (size_t i = first + gap; i <= placed; ++i)
                    pens[i] -= sign;
            };
            for (size_t i = first + 1; i < span.end; ++i) {
                const size_t gap = i - first;
                const double step = px_of(drawn.glyphs[i].x - drawn.glyphs[i - 1].x, density);
                const double rounded = snap(step);
                pens[i] = pens[i - 1] + rounded;
                excess[gap] = static_cast<float>(rounded - step);
                placed = i;
                const float lean = excess[gap];
                const double drift =
                    pens[i] - anchor_pen - px_of(drawn.glyphs[i].x - anchor_x, density);
                if (drift < 0.0) {
                    up_most = 0.0f;
                } else if (lean > up_most) {
                    up_most = lean;
                    up = gap;
                }
                if (drift > 0.0) {
                    down_most = 0.0f;
                } else if (-lean > down_most) {
                    down_most = -lean;
                    down = gap;
                }
                if (drift > WORD_GLYPH_BOUND_PX && up_most > 0.0f) {
                    const bool own = up == gap;
                    give(up, 1.0);
                    down_most = 0.0f;
                    up_most = !own && lean > 0.0f ? lean : 0.0f;
                    up = gap;
                } else if (drift < -WORD_GLYPH_BOUND_PX && down_most > 0.0f) {
                    const bool own = down == gap;
                    give(down, -1.0);
                    up_most = 0.0f;
                    down_most = !own && lean < 0.0f ? -lean : 0.0f;
                    down = gap;
                }
            }
            const size_t last = span.end - 1;
            const double drift =
                pens[last] - anchor_pen - px_of(drawn.glyphs[last].x - anchor_x, density);
            if (drift > set.bound && up_most > 0.0f)
                give(up, 1.0);
            else if (drift < -set.bound && down_most > 0.0f)
                give(down, -1.0);
            continue;
        }
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
    {"advance", Rule::ADVANCE, 0.0, false, false},
    {"whole", Rule::WHOLE, 0.0, false, false},
    {"greedy 0.5", Rule::GREEDY, 0.5, false, false},
    {"greedy 1.0", Rule::GREEDY, 1.0, false, false},
    {"tie 0.5", Rule::NEAREST_TIE, 0.5, false, false},
    {"tie 0.75", Rule::NEAREST_TIE, 0.75, false, false},
    {"tie 1.0", Rule::NEAREST_TIE, 1.0, false, false},
    {"advance+follow", Rule::ADVANCE, 0.0, true, false},
    {"tie 0.5+follow", Rule::NEAREST_TIE, 0.5, true, false},
    {"tie 0.75+follow", Rule::NEAREST_TIE, 0.75, true, false},
    {"tie 1.0+follow", Rule::NEAREST_TIE, 1.0, true, false},
    {"held 0.5+follow", Rule::HELD_GLYPHS, 0.5, true, false},
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
const RuleSet PRODUCT = {"product", Rule::HELD_GLYPHS, WORD_WIDTH_BOUND_PX, true, true};

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

// --- Where text meets a formula ---------------------------------------------------------

namespace
{

// Text glued to a formula on either side and on both, and formulas with a
// space beside them, in prose long enough to be justified over several lines.
// Not "$n$th": a dollar with a letter, a digit, a brace or a bracket straight
// after it closes no formula (scan_math in src/imtypeset.cpp, after the
// reference), so that is text, dollars and all. What can be glued to the end
// of a formula is punctuation and what follows it.
const char* const JOINTS_1 =
    "The $n$-th term of the series lies on the $x$-axis, and $f$'s value there ($x$) is "
    "written f($x$) when it is $x$, or $y$; so the $k$-th root of ($a+b$) is $r$, where the "
    "$x$-axis meets the $y$-axis, and $g$'s slope ($m$) is g($t$) for $t$, not $s$. The "
    "$i$-th and the $j$-th of them, ($p$) and ($q$), are h($u$) and h($v$): $h$'s own.";
const char* const JOINTS_2 =
    "The sum ($\\frac{n(n+1)}{2}$) of the first $n$ integers is $\\frac{n(n+1)}{2}$, which "
    "is $x^2$-like for large $n$; the $i$-th partial sum f($\\frac{1}{2}$) and ($\\sqrt{x}$)'s "
    "value, $\\sqrt{2}$, meet at ($x_1$, $x_2$), the $m$-th and the $(m+1)$-st, with "
    "g($x^2 + y^2$) for $\\frac{a}{b}$'s $k$-th power and ($\\sqrt{a^2 + b^2}$)-long sides.";
const char* const JOINTS_3 =
    "It is $x$ if it is $y$ and it is $z$, so $a$ is $b$, $c$ is $d$ and $e$ is $f$; then "
    "$a$ $b$ $c$ are $d$ $e$ $f$, as $p$, $q$, $r$ is $s$, $t$, $u$. What is $w$ is $w$, "
    "what is $v$ was $u$, and it is $x$-wise the $n$-th ($n$) of f($n$), is $n$, was $n$.";

std::vector<Case> joint_cases()
{
    std::vector<Case> out;
    for (const float width : {520.0f, 337.0f, 250.0f}) {
        out.push_back({"joints-1", JOINTS_1, width});
        out.push_back({"joints-2", JOINTS_2, width});
        out.push_back({"joints-3", JOINTS_3, width});
    }
    return out;
}

// The four ways a glyph of text stands beside a formula.
const char* const JOINT_KINDS[4] = {
    "text, formula",
    "text, space, formula",
    "formula, text",
    "formula, space, text",
};

// How far joints are drawn from how they were laid out, in pixels: the drawn
// distance less the laid-out one, so below zero is closer together.
struct JointTally {
    long count = 0;
    // From the glyph of text to the part of the formula that stands nearest
    // it in the layout: for text before the formula the glyph origin or rule
    // end furthest left, for text after it the one furthest right.
    double near_low = 0.0;
    double near_high = 0.0;
    long near_over_half = 0;
    long near_over_1 = 0;
    // To whichever glyph or end of a rule of the formula is furthest out.
    double any = 0.0;
    long any_over_15 = 0;
    // To the edge of the formula's box, where its glyphs and rules all stand
    // at their own rounded distances from one pixel: the product's rules.
    double edge_low = 0.0;
    double edge_high = 0.0;
    long edge_over_1 = 0;
};

// The pixel a formula's parts were placed from, by its first glyph; false if
// it has none or its parts were not placed from one pixel.
bool edge_of(const Drawn& drawn, const FormulaOf& f, double* edge)
{
    if (f.first >= f.end)
        return false;
    const double density = drawn.px_per_inch;
    *edge = drawn.glyphs[f.first].pen - snap(px_of(drawn.glyphs[f.first].x - f.x, density));
    for (size_t i = f.first; i < f.end; ++i)
        if (drawn.glyphs[i].pen != *edge + snap(px_of(drawn.glyphs[i].x - f.x, density)))
            return false;
    return true;
}

// Adds the joints of one draw to `kinds`, and answers the furthest any was
// from its laid-out distance at the formula's edge (zero where `edges` is
// false: under the reference's rules a formula has none).
double tally_joints(const Drawn& drawn, bool edges, JointTally kinds[4])
{
    const double density = drawn.px_per_inch;
    double worst_edge = 0.0;
    for (const Joint& j : drawn.joints) {
        const FormulaOf& f = drawn.formulas[j.formula];
        if (f.first >= f.end)
            continue;
        const Placed& text = drawn.glyphs[j.text];
        JointTally& t = kinds[(j.text_first ? 0 : 2) + (j.spaced ? 1 : 0)];
        const auto off = [&](Scaled x, double pen) {
            return j.text_first ? (pen - text.pen) - px_of(x - text.x, density)
                                : (text.pen - pen) - px_of(text.x - x, density);
        };
        ++t.count;
        FormulaOf::End nearest{drawn.glyphs[f.first].x, drawn.glyphs[f.first].pen};
        const auto nearer = [&](Scaled x, double pen) {
            if (j.text_first ? x < nearest.x : x > nearest.x)
                nearest = FormulaOf::End{x, pen};
        };
        double any = 0.0;
        for (size_t i = f.first; i < f.end; ++i) {
            any = std::max(any, std::fabs(off(drawn.glyphs[i].x, drawn.glyphs[i].pen)));
            nearer(drawn.glyphs[i].x, drawn.glyphs[i].pen);
        }
        for (const FormulaOf::End& end : f.rule_ends) {
            any = std::max(any, std::fabs(off(end.x, end.pen)));
            nearer(end.x, end.pen);
        }
        const double near = off(nearest.x, nearest.pen);
        t.near_low = std::min(t.near_low, near);
        t.near_high = std::max(t.near_high, near);
        t.near_over_half += std::fabs(near) > 0.5 + 1.0e-9;
        t.near_over_1 += std::fabs(near) > 1.0;
        t.any = std::max(t.any, any);
        t.any_over_15 += any > 1.5;
        double edge = 0.0;
        if (edges && edge_of(drawn, f, &edge)) {
            const double at_edge = off(f.x, edge);
            t.edge_low = std::min(t.edge_low, at_edge);
            t.edge_high = std::max(t.edge_high, at_edge);
            t.edge_over_1 += std::fabs(at_edge) > 1.0;
            worst_edge = std::max(worst_edge, std::fabs(at_edge));
        }
    }
    return worst_edge;
}

void print_joints(const char* rules, const JointTally kinds[4], bool edges)
{
    for (int k = 0; k < 4; ++k) {
        const JointTally& t = kinds[k];
        std::printf("    %-9s %-21s %6ld: nearest part %+.2f to %+.2f px (%ld over 0.5, %ld "
                    "over 1); furthest part %.2f (%ld over 1.5)",
                    rules, JOINT_KINDS[k], t.count, t.near_low, t.near_high, t.near_over_half,
                    t.near_over_1, t.any, t.any_over_15);
        if (edges)
            std::printf("; box edge %+.2f to %+.2f (%ld over 1)", t.edge_low, t.edge_high,
                        t.edge_over_1);
        std::printf("\n");
    }
}

} // namespace

TEST(under_the_products_rules_a_joint_of_text_and_formula_is_within_a_pixel_and_a_half_too)
{
    // The one bound of DrawRules::product(), over prose in which text is
    // glued to formulas on both sides ("$n$-th", "$x$-axis", "$f$'s", "($x$)",
    // "f($x$)", "is $x$", "$x$,"), justified at three widths, at 13 to 20 px
    // as an em and as a font size, at three origins: two neighbours on a line
    // — a word and a word across a space, a glyph of text and a formula with
    // a space between them or none, two formulas — are drawn less than a
    // pixel and a half from their laid-out distance apart, a word taken at
    // the glyph nearest the neighbour and a formula at the edge of its box.
    // Inside a formula every glyph is within half a pixel of its laid-out
    // distance from that edge. And the product's rules against the
    // reference's at the same joints, by the glyphs and rule ends
    // themselves, which is a measure the reference's rules have too.
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const std::vector<Case> all = joint_cases();
    JointTally reference[4], product[4];
    Tally reference_spaces, product_spaces;
    double worst_pair_of_formulas = 0.0;
    double reference_place = 0.0, product_place = 0.0;
    for (const bool as_em : {true, false}) {
        for (int size = 13; size <= 20; ++size) {
            for (const Case& c : all) {
                for (const float origin : {8.0f, 8.375f, 8.6875f}) {
                    CHECK_CONTEXT("%s at %d px (%s), width %g, origin %g", c.name, size,
                                  as_em ? "em" : "font size", static_cast<double>(c.width),
                                  static_cast<double>(origin));
                    Drawn drawn;
                    stage.ctx->draw_rules = DrawRules::reference();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    std::vector<double> pens(drawn.glyphs.size());
                    for (size_t i = 0; i < pens.size(); ++i) {
                        pens[i] = drawn.glyphs[i].pen;
                        reference_place = std::max(
                            reference_place,
                            std::fabs(pens[i] - (static_cast<double>(origin) +
                                                 px_of(drawn.glyphs[i].x, drawn.px_per_inch))));
                    }
                    tally(drawn, pens, "", reference_spaces);
                    tally_joints(drawn, false, reference);

                    stage.ctx->draw_rules = DrawRules::product();
                    REQUIRE(draw_case(stage, p, c, static_cast<float>(size), as_em, origin, drawn));
                    CHECK_EQ(pens_that_differ(drawn, PRODUCT), 0L);
                    long off_grid = 0;
                    for (size_t i = 0; i < pens.size(); ++i) {
                        pens[i] = drawn.glyphs[i].pen;
                        off_grid += pens[i] != std::floor(pens[i]);
                        product_place = std::max(
                            product_place,
                            std::fabs(pens[i] - (static_cast<double>(origin) +
                                                 px_of(drawn.glyphs[i].x, drawn.px_per_inch))));
                    }
                    CHECK_EQ(off_grid, 0L);
                    Tally here;
                    tally(drawn, pens, "", here);
                    tally(drawn, pens, "", product_spaces);
                    CHECK(here.worst_word <= WORD_WIDTH_BOUND_PX);
                    CHECK(here.worst_narrow > -1.5);
                    CHECK(here.worst_wide < 1.5);
                    CHECK(!drawn.joints.empty());
                    CHECK(tally_joints(drawn, true, product) < 1.5);
                    for (const FormulaOf& f : drawn.formulas) {
                        double edge = 0.0;
                        CHECK(edge_of(drawn, f, &edge));
                    }
                    for (const FormulaPair& pair : drawn.formula_pairs) {
                        const FormulaOf& a = drawn.formulas[pair.before];
                        const FormulaOf& b = drawn.formulas[pair.after];
                        double edge_a = 0.0, edge_b = 0.0;
                        if (!edge_of(drawn, a, &edge_a) || !edge_of(drawn, b, &edge_b))
                            continue;
                        const double off = (edge_b - edge_a) - px_of(b.x - a.x, drawn.px_per_inch);
                        worst_pair_of_formulas = std::max(worst_pair_of_formulas, std::fabs(off));
                        CHECK(std::fabs(off) < 1.5);
                    }
                }
            }
        }
    }
    print_joints("reference", reference, false);
    print_joints("product", product, true);
    std::printf("    spaces between two words: reference %ld, %+.2f to %+.2f px, %ld over 1; "
                "product %+.2f to %+.2f px, %ld over 1\n",
                reference_spaces.spaces, reference_spaces.worst_narrow, reference_spaces.worst_wide,
                reference_spaces.off_by_over_1, product_spaces.worst_narrow,
                product_spaces.worst_wide, product_spaces.off_by_over_1);
    std::printf("    two formulas side by side, edge to edge: product %.2f px at the worst\n",
                worst_pair_of_formulas);
    std::printf("    a glyph against its own place, which is where a caret before it and the "
                "edge of a highlight stand: reference %.2f px at the worst, product %.2f; in "
                "words alone %.2f and %.2f\n",
                reference_place, product_place, reference_spaces.worst_place,
                product_spaces.worst_place);
    std::printf("    pairs of glyphs in words over half a pixel off: reference %ld of %ld, "
                "product %ld\n",
                reference_spaces.uneven, reference_spaces.pairs, product_spaces.uneven);
    for (int k = 0; k < 4; ++k) {
        CHECK_CONTEXT("%s", JOINT_KINDS[k]);
        CHECK(product[k].count > 100);
        // By the parts themselves, which is a measure both sets of rules
        // have: no part of a formula further out than under the reference's
        // rules at any kind of joint.
        CHECK(product[k].any <= reference[k].any);
        CHECK(product[k].any_over_15 <= reference[k].any_over_15);
        // The part of the formula nearest the text: a pixel and a half and
        // the half a pixel that part has against the box's edge.
        CHECK(product[k].near_low > -2.0);
        CHECK(product[k].near_high < 2.0);
        // And against the reference's rules, for all but text straight
        // after a formula: no further out at the worst, and over half a
        // pixel out less often.
        if (k != 2) {
            CHECK(product[k].near_low >= reference[k].near_low);
            CHECK(product[k].near_high <= reference[k].near_high);
            CHECK(product[k].near_over_half <= reference[k].near_over_half);
            CHECK(product[k].near_over_1 <= reference[k].near_over_1);
        }
    }
    // Text straight after a formula is the one kind the reference's rules
    // place more evenly: by one rounded gap from the glyph the formula
    // shipped last, with nothing to hold it to its own place. Here it is
    // placed from the formula's rightmost part by one rounded gap too, and
    // then held beside its own place, which is what keeps the space after it;
    // where that hold moves it, it is a pixel nearer or further.
    CHECK(product[2].near_low > -1.5);
    CHECK(product[2].near_high < 1.5);
    CHECK(product[2].near_over_1 * 100 < product[2].count);
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
    // rounded up to 3, are 1.98 px too wide together, and two gaps give.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < 200; ++i)
        commands.push_back(DrawCmd::glyph(key, i % 2 == 0 ? 'a' : 'M', hundredths(149) * i, y, 7));
    DrawStats stats;
    // With the word's two ends alone held: the first two.
    const std::vector<float> ends = pens_of(commands, only_held(), 0.0f, &stats);
    REQUIRE_EQ(ends.size(), size_t(100));
    CHECK_EQ(stats.missing, 100);
    CHECK_EQ(ends[1], 2.0f);
    CHECK_EQ(ends[2], 4.0f);
    for (size_t i = 3; i < ends.size(); ++i)
        CHECK_EQ(ends[i], static_cast<float>(3 * i - 2));
    // With every glyph held: the fiftieth glyph after the first is the first
    // to stand more than a pixel right of its place (fifty gaps each rounded
    // up by 0.02002: a hundredth here is 655 scaled points and not 655.36),
    // and the first gap gives, the earliest of fifty alike. From there the
    // last glyph gathers 0.98 px more, which is past the half a word's end
    // is held to, and the earliest gap of its run gives: the fiftieth, the
    // one into the glyph the first give brought back.
    stats = DrawStats();
    const std::vector<float> pens = pens_of(commands, DrawRules::product(), 0.0f, &stats);
    REQUIRE_EQ(pens.size(), size_t(100));
    CHECK_EQ(stats.missing, 100);
    for (size_t i = 1; i < pens.size(); ++i)
        CHECK_EQ(pens[i], static_cast<float>(i < 50 ? 3 * i - 1 : 3 * i - 2));
}

TEST(a_word_of_twenty_thousand_glyphs_is_held_and_the_work_is_not_its_length_times_its_drift)
{
    // Every gap is 1.45 px and rounds to 1: the word is some nine thousand
    // pixels narrower than it was laid out, and that many gaps give. Placing
    // it a hundred times takes no time worth measuring when each is one pass
    // over the word, and minutes when each is a pass a pixel.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const uint32_t glyphs = 20000;
    std::vector<DrawCmd> commands;
    for (uint32_t i = 0; i < glyphs; ++i)
        commands.push_back(DrawCmd::glyph(key, 'a', hundredths(145) * i, y, i));
    const double laid_out = px_from_sp(commands.back().x, PT_PER_INCH);
    const size_t wanted = static_cast<size_t>(std::ceil(laid_out - 19999.0 - WORD_WIDTH_BOUND_PX));

    // The word's two ends alone held: the gaps are all rounded alike, so
    // those that give are the first that many.
    std::vector<float> pens;
    for (int pass = 0; pass < 100; ++pass)
        pens = pens_of(commands, only_held(), 0.0f);
    REQUIRE_EQ(pens.size(), size_t(glyphs));
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
    CHECK_EQ(wide, wanted);

    // Every glyph held: as many give, spread along the word, and no glyph is
    // more than a pixel from its laid-out distance to the first. Under the
    // rule above the middle of this word is four and a half thousand pixels
    // from it.
    for (int pass = 0; pass < 100; ++pass)
        pens = pens_of(commands, DrawRules::product(), 0.0f);
    REQUIRE_EQ(pens.size(), size_t(glyphs));
    CHECK(std::fabs(static_cast<double>(pens.back()) - laid_out) <= WORD_WIDTH_BOUND_PX);
    wide = 0;
    bool steps = true;
    double furthest = 0.0;
    for (size_t i = 1; i < pens.size(); ++i) {
        const float gap = pens[i] - pens[i - 1];
        steps = steps && (gap == 1.0f || gap == 2.0f);
        wide += gap == 2.0f ? 1 : 0;
        furthest = std::max(furthest, std::fabs(static_cast<double>(pens[i]) -
                                                px_from_sp(commands[i].x, PT_PER_INCH)));
    }
    CHECK(steps);
    CHECK_EQ(wide, wanted);
    CHECK(furthest <= WORD_GLYPH_BOUND_PX);
}

TEST(the_gaps_that_give_are_the_ones_rounded_furthest_whatever_their_roundings_are)
{
    // Three hundred gaps of 1 px and something under a half, the something
    // from a generator and so in no order, with many equal: each rounds to 1
    // and the word is some seventy pixels narrow. Those that give, found
    // here by sorting every gap by how far it was rounded and then by where
    // it is, are the ones the draw moves: the selection of the many from
    // the more is exact, ties and all.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    for (const bool narrow : {true, false}) {
        CHECK_CONTEXT("%s", narrow ? "too narrow" : "too wide");
        std::vector<DrawCmd> commands;
        std::vector<float> rounding(1, 0.0f);
        Scaled x = 0;
        uint32_t state = 12345;
        commands.push_back(DrawCmd::glyph(key, 'a', x, y, 0));
        double laid_out = 0.0;
        for (uint32_t i = 1; i <= 300; ++i) {
            state = state * 1664525u + 1013904223u;
            // A fraction of 0.02 to 0.48 in steps of a fiftieth, so that
            // many gaps are rounded alike; above a half for a word too wide.
            const int64_t fiftieths = 1 + (state >> 16) % 24;
            const Scaled gap = UNITY + (narrow ? fiftieths : 50 - fiftieths) * UNITY / 50;
            x += gap;
            commands.push_back(DrawCmd::glyph(key, 'a', x, y, i));
            const double step = px_from_sp(gap, PT_PER_INCH);
            rounding.push_back(static_cast<float>(std::floor(step + 0.5) - step));
            laid_out += step;
        }
        const double natural = narrow ? 300.0 : 600.0;
        const double sign = narrow ? -1.0 : 1.0;
        const size_t wanted =
            static_cast<size_t>(std::ceil(std::fabs(natural - laid_out) - WORD_WIDTH_BOUND_PX));
        REQUIRE(wanted > 20);
        REQUIRE(wanted < 280);
        std::vector<size_t> order;
        for (size_t i = 1; i <= 300; ++i)
            order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return static_cast<float>(sign) * rounding[a] > static_cast<float>(sign) * rounding[b];
        });
        std::vector<bool> gives(301, false);
        for (size_t n = 0; n < wanted; ++n)
            gives[order[n]] = true;
        std::vector<float> expected(1, 0.0f);
        for (size_t i = 1; i <= 300; ++i) {
            const float rounded = narrow ? 1.0f : 2.0f;
            expected.push_back(expected.back() + rounded -
                               (gives[i] ? static_cast<float>(sign) : 0.0f));
        }
        const std::vector<float> pens = pens_of(commands, only_held(), 0.0f);
        CHECK(pens == expected);
        REQUIRE_EQ(pens.size(), size_t(301));
        CHECK(std::fabs(static_cast<double>(pens.back()) - laid_out) <= WORD_WIDTH_BOUND_PX);
    }
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
        // Led from g, 18 + snap(3.6) = 22, which is its own place too.
        DrawCmd::glyph(key, 'h', hundredths(1200), y, 12),
        DrawCmd::line(0, y, 0, 0, 0, 13, 16),
        // j at snap(10.5) = 11, half a pixel right of its place.
        DrawCmd::glyph(key, 'j', hundredths(50), y, 13),
        glue(200, 14),
        // k is 3.5 px on, and its place is a whole pixel, 14. Led it would
        // be at 11 + snap(3.5) = 15; a place on a whole pixel has no pixel
        // either side of it, and k is held to 14.
        DrawCmd::glyph(key, 'k', hundredths(400), y, 15),
    };
    REQUIRE_EQ(commands[commands.size() - 3].x, UNITY / 2);
    REQUIRE_EQ(commands.back().x, 4 * UNITY);
    CHECK(
        pens_of(commands, DrawRules::reference(), 10.0f) ==
        (std::vector<float>{10.0f, 11.0f, 15.0f, 16.0f, 20.0f, 15.0f, 18.0f, 22.0f, 11.0f, 14.0f}));
    CHECK(
        pens_of(commands, only_led(), 10.0f) ==
        (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f, 19.0f, 15.0f, 18.0f, 22.0f, 11.0f, 14.0f}));
    CHECK(
        pens_of(commands, DrawRules::product(), 10.0f) ==
        (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f, 19.0f, 15.0f, 18.0f, 22.0f, 11.0f, 14.0f}));
    // Without the hold to its own place k would have been led to 15: the same
    // two, a hundredth of a pixel further along, where 14.01 has 15 beside it.
    std::vector<DrawCmd> beside = {
        DrawCmd::glyph(key, 'j', hundredths(51), y, 0),
        glue(200, 1),
        DrawCmd::glyph(key, 'k', hundredths(401), y, 2),
    };
    CHECK(pens_of(beside, only_led(), 10.0f) == (std::vector<float>{11.0f, 15.0f}));
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

TEST(a_formula_is_led_by_its_boxs_edge_and_leads_by_its_rightmost_part)
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
        // A formula 3.4 px on from b, its box at 14.9: led as a word would
        // be, 11 + 3 = 14, which is beside 14.9. Its two glyphs from the
        // box's edge: 14 + snap(0) and 14 + snap(1.45) = 15.
        DrawCmd::atom(hundredths(490), y, hundredths(300), hundredths(300), 0, 0, 3),
        DrawCmd::glyph(key, 'x', hundredths(490), y, 3),
        DrawCmd::glyph(key, 'y', hundredths(635), y, 3),
        // A comma straight after it is a word of one glyph in the formula's
        // span, placed from the formula's rightmost part, the y: 1.45 px on,
        // 15 + 1 = 16, which is not beside its place, 17.8, and it is held
        // to 17.
        DrawCmd::glyph(key, ',', hundredths(780), y, 4),
        glue(850, 5),
        // And it leads: 4.1 px on, 17 + 4 = 21, its place 21.9.
        DrawCmd::glyph(key, 'c', hundredths(1190), y, 6),
        glue(1300, 7),
        // A box whose place is a whole pixel, 24, is on it: led from c it
        // would be at 21 + snap(2.1) = 23.
        DrawCmd::atom(hundredths(1400), y, hundredths(100), hundredths(100), 0, 0, 8),
        DrawCmd::glyph(key, 'z', hundredths(1400), y, 8), // 24
        glue(1500, 9),
        // A word after a space after a formula is led from the formula's
        // rightmost part, here the z: 3.9 px on, 24 + 4 = 28, its place 27.9.
        DrawCmd::glyph(key, 'd', hundredths(1790), y, 10),
        glue(1900, 11),
        // Two formulas. The first is led from d, 2.4 px on: 28 + 2 = 30, its
        // place 30.3. The second from the first's rightmost part, which is
        // its one glyph, 5.4 px on: 30 + 5 = 35, beside its place at 35.7.
        DrawCmd::atom(hundredths(2030), y, hundredths(100), hundredths(100), 0, 0, 12),
        DrawCmd::glyph(key, 'p', hundredths(2030), y, 12),
        glue(2200, 13),
        DrawCmd::atom(hundredths(2570), y, hundredths(100), hundredths(100), 0, 0, 14),
        DrawCmd::glyph(key, 'q', hundredths(2570), y, 14),
    };
    CHECK(
        pens_of(commands, DrawRules::product(), 10.0f) ==
        (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f, 17.0f, 21.0f, 24.0f, 28.0f, 30.0f, 35.0f}));
    // Under the reference's rules each of them is on the pixel nearest its
    // own place, and the y and the comma a rounded gap on from the glyph
    // before.
    CHECK(
        pens_of(commands, DrawRules::reference(), 10.0f) ==
        (std::vector<float>{10.0f, 11.0f, 15.0f, 16.0f, 17.0f, 22.0f, 24.0f, 28.0f, 30.0f, 36.0f}));
    // A formula's glyphs are no part of a word: the gap between the x and
    // the y is 1.45 px and rounds to 1 whatever the words either side do.
}

TEST(
    text_glued_to_the_end_of_a_formula_is_kept_beside_its_place_and_the_space_after_keeps_its_width)
{
    // "$x^2$-th a", origin 0. The box at 10.6 goes to 11, and the formula's
    // last glyph, 5.6 px along it, to 11 + 6 = 17, 0.8 right of its place.
    // The hyphen is 4.6 px on from that glyph, 17 + 5 = 22, and its place is
    // 20.8: 22 is not beside it, and the hyphen is held to 21. Then gaps of
    // 4.6 and 4.95: 26 and 31 (25.4 and 30.35; the word is 10 wide against
    // 9.55). The a, on a whole pixel at 39, is on it, and the space between
    // h and a is 8 px from pen to pen against 8.65.
    //
    // Not held, the hyphen is at 22 and the h at 32, 1.65 px right of its
    // place, and all of that comes out of the space: 7 against 8.65.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const std::vector<DrawCmd> commands = {
        DrawCmd::atom(hundredths(1060), y, hundredths(1020), hundredths(1020), 0, 0, 0),
        DrawCmd::glyph(key, 'x', hundredths(1060), y, 0),
        DrawCmd::glyph(key, '2', hundredths(1620), y, 0),
        DrawCmd::glyph(key, '-', hundredths(2080), y, 1),
        DrawCmd::glyph(key, 't', hundredths(2540), y, 2),
        DrawCmd::glyph(key, 'h', hundredths(3035), y, 3),
        DrawCmd::set_glue(hundredths(3600), y, hundredths(300), hundredths(300), 0, 0, 4),
        DrawCmd::glyph(key, 'a', hundredths(3900), y, 5),
    };
    REQUIRE_EQ(commands.back().x, 39 * UNITY);
    CHECK(pens_of(commands, DrawRules::reference(), 0.0f) ==
          (std::vector<float>{11.0f, 17.0f, 22.0f, 27.0f, 32.0f, 39.0f}));
    const std::vector<float> pens = pens_of(commands, DrawRules::product(), 0.0f);
    CHECK(pens == (std::vector<float>{11.0f, 17.0f, 21.0f, 26.0f, 31.0f, 39.0f}));
    REQUIRE_EQ(pens.size(), size_t(6));
    CHECK(std::fabs(static_cast<double>(pens[5] - pens[4]) - 8.65) < 1.5);
    // The hyphen against the box's edge: 10 px against 10.2.
    CHECK(std::fabs(static_cast<double>(pens[2] - pens[0]) - 10.2) < 1.5);
}

TEST(a_bracket_after_a_fraction_stands_clear_of_the_bar_it_follows)
{
    // "$\frac{n}{2}$)" with the box at 0: a numerator's glyph at 2.0, the bar
    // from 0 to 46.6, the denominator's glyph at 20.49, which is the last
    // glyph the formula ships, and the bracket at the box's far edge, 46.98.
    // The bar ends at snap(46.6) = 47 and is the formula's rightmost part;
    // the bracket, 0.38 px on from it, is at 47 + 0 = 47. From the
    // denominator's glyph, at 20, by the rounded gap of 26.49 it is at 46, a
    // pixel inside the bar: where the reference's rules put it.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const std::vector<DrawCmd> commands = {
        DrawCmd::atom(0, y, hundredths(4698), hundredths(4698), 0, 0, 0),
        DrawCmd::glyph(key, 'n', hundredths(200), y - 6 * UNITY, 0),
        DrawCmd::rule_rect(0, y - 4 * UNITY, hundredths(4660), UNITY),
        DrawCmd::glyph(key, '2', hundredths(2049), y + 6 * UNITY, 0),
        DrawCmd::glyph(key, ')', hundredths(4698), y, 1),
    };
    for (const bool product : {false, true}) {
        CHECK_CONTEXT("%s", product ? "product" : "reference");
        DrawRun run;
        run.cmds = commands.data();
        run.count = commands.size();
        run.ink = 0xFF000000u;
        DrawArgs args;
        args.px_per_inch = PT_PER_INCH;
        args.rules = product ? DrawRules::product() : DrawRules::reference();
        Drawer drawer;
        PlainBinder binder;
        drawer.place(&run, 1, args, binder);
        const std::vector<DrawOp>& ops = drawer.ops();
        REQUIRE_EQ(ops.size(), size_t(4));
        REQUIRE(ops[1].type == OpType::RECT_FILLED);
        CHECK_EQ(ops[0].x0, 2.0f);
        CHECK_EQ(ops[1].x0, 0.0f);
        CHECK_EQ(ops[1].x1, 47.0f);
        CHECK_EQ(ops[2].x0, 20.0f);
        CHECK_EQ(ops[3].x0, product ? 47.0f : 46.0f);
    }
}

TEST(a_bracket_before_a_formula_is_no_further_from_it_than_under_the_references_rules)
{
    // "ab ($x$)", origin 0. The word before: a at snap(27.51) = 28, b a gap
    // of 1.51 on at 30, 0.98 right of its place at 29.02. The bracket is
    // 10.0 px on from b: led to 40, and 40 is beside its place, 39.02. The
    // box, 5.47 px on from the bracket with its place at 44.49, is led from
    // the bracket: 40 + 5 = 45, and so is the x in it; 5 px from the bracket
    // against 5.47, which is what the reference's rules make of it (39 and
    // 44). On the pixel nearest its own place the box would be at 44, 4 px
    // from a bracket that was led to 40. The closing bracket, 5.47 px on
    // from the x: 45 + 5 = 50, its place 49.96.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(key, 'a', hundredths(2751), y, 0),
        DrawCmd::glyph(key, 'b', hundredths(2902), y, 1),
        DrawCmd::set_glue(hundredths(3052), y, hundredths(850), hundredths(850), 0, 0, 2),
        DrawCmd::glyph(key, '(', hundredths(3902), y, 3),
        DrawCmd::atom(hundredths(4449), y, hundredths(547), hundredths(547), 0, 0, 4),
        DrawCmd::glyph(key, 'x', hundredths(4449), y, 4),
        DrawCmd::glyph(key, ')', hundredths(4996), y, 5),
    };
    const std::vector<float> reference = pens_of(commands, DrawRules::reference(), 0.0f);
    const std::vector<float> product = pens_of(commands, DrawRules::product(), 0.0f);
    CHECK(reference == (std::vector<float>{28.0f, 30.0f, 39.0f, 44.0f, 49.0f}));
    CHECK(product == (std::vector<float>{28.0f, 30.0f, 40.0f, 45.0f, 50.0f}));
    REQUIRE_EQ(product.size(), size_t(5));
    REQUIRE_EQ(reference.size(), size_t(5));
    CHECK(std::fabs(static_cast<double>(product[3] - product[2]) - 5.47) <=
          std::fabs(static_cast<double>(reference[3] - reference[2]) - 5.47));
    CHECK(std::fabs(static_cast<double>(product[4] - product[3]) - 5.47) <=
          std::fabs(static_cast<double>(reference[4] - reference[3]) - 5.47));
}

TEST(a_justified_space_before_a_formula_is_within_a_pixel_and_a_half_of_its_laid_out_width)
{
    // "ab cd $x$", origin 0. As above, b is at 30 and 0.98 right of its
    // place; c, 10.0 px on, is led to 40, 0.98 right of 39.02; d a gap of
    // exactly 1.5 on at 42, 1.48 right of 40.52, and a word half a pixel
    // wide of its laid-out width is left alone. Then a space squeezed to
    // 2.45 px and a formula whose box, 3.97 px on from d, has its place at
    // 44.49. Led from d it would be at 46; that is not beside its place, and
    // it is held to 45: 3 px from d against 3.97. On the pixel nearest its
    // own place, 44, it is 2 px from d and the 2.45 px space is drawn 0.48
    // wide.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(key, 'a', hundredths(2751), y, 0),
        DrawCmd::glyph(key, 'b', hundredths(2902), y, 1),
        DrawCmd::set_glue(hundredths(3052), y, hundredths(850), hundredths(850), 0, 0, 2),
        DrawCmd::glyph(key, 'c', hundredths(3902), y, 3),
        DrawCmd::glyph(key, 'd', hundredths(4052), y, 4),
        DrawCmd::set_glue(hundredths(4204), y, hundredths(245), hundredths(338), 0, 0, 5),
        DrawCmd::atom(hundredths(4449), y, hundredths(547), hundredths(547), 0, 0, 6),
        DrawCmd::glyph(key, 'x', hundredths(4449), y, 6),
    };
    REQUIRE_EQ(commands[4].x - commands[3].x, 3 * UNITY / 2);
    CHECK(pens_of(commands, DrawRules::reference(), 0.0f) ==
          (std::vector<float>{28.0f, 30.0f, 39.0f, 41.0f, 44.0f}));
    const std::vector<float> pens = pens_of(commands, DrawRules::product(), 0.0f);
    CHECK(pens == (std::vector<float>{28.0f, 30.0f, 40.0f, 42.0f, 45.0f}));
    REQUIRE_EQ(pens.size(), size_t(5));
    CHECK(std::fabs(static_cast<double>(pens[4] - pens[3]) - 3.97) < 1.5);
}

TEST(a_rule_that_lies_outside_a_formulas_box_is_not_the_formulas)
{
    // Not what ship-out makes: a rule straight after a formula's glyph, with
    // no glue, line or text between, that is nowhere near the formula's box.
    // It is snapped by its own two edges, as a rule of the page is, it ends
    // the span, and nothing is led across it. A rule that touches the box is
    // the formula's and stands at its rounded distance from the box's edge.
    const FontKey key = font_key(Face::PROSE, 20 * UNITY);
    const Scaled y = 20 * UNITY;
    for (const bool foreign : {true, false}) {
        CHECK_CONTEXT("%s", foreign ? "a rule elsewhere" : "a rule in the box");
        // The box at 10.4 goes to 10. A rule in it from 12.0 to 16.8 is 1.6
        // and 6.4 px from the box's edge: 10 + 2 = 12 to 10 + 6 = 16. One
        // from 80.3 to 90.4 goes to 80 and 90 by its own edges.
        const std::vector<DrawCmd> commands = {
            DrawCmd::atom(hundredths(1040), y, hundredths(1000), hundredths(1000), 0, 0, 0),
            DrawCmd::glyph(key, 'x', hundredths(1040), y, 0),
            foreign ? DrawCmd::rule_rect(hundredths(8030), y, hundredths(1010), UNITY)
                    : DrawCmd::rule_rect(hundredths(1200), y, hundredths(480), UNITY),
            DrawCmd::glyph(key, 'y', hundredths(2575), y, 0),
        };
        DrawRun run;
        run.cmds = commands.data();
        run.count = commands.size();
        run.ink = 0xFF000000u;
        DrawArgs args;
        args.px_per_inch = PT_PER_INCH;
        args.rules = DrawRules::product();
        Drawer drawer;
        PlainBinder binder;
        drawer.place(&run, 1, args, binder);
        const std::vector<DrawOp>& ops = drawer.ops();
        REQUIRE_EQ(ops.size(), size_t(3));
        CHECK_EQ(ops[0].x0, 10.0f);
        REQUIRE(ops[1].type == OpType::RECT_FILLED);
        CHECK_EQ(ops[1].x0, foreign ? 80.0f : 12.0f);
        CHECK_EQ(ops[1].x1, foreign ? 90.0f : 16.0f);
        // The y carries the formula's index and is 15.35 px along the box:
        // 10 + 15 = 25 as a glyph of the formula. After a rule that is not
        // the formula's it is an anchor: snap(25.75) = 26.
        CHECK_EQ(ops[2].x0, foreign ? 26.0f : 25.0f);
    }
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
        // Text glued to formulas.
        {"joints-1", JOINTS_1, 16.0f, 337.0f},
        {"joints-1", JOINTS_1, 19.0f, 460.0f},
        {"joints-2", JOINTS_2, 16.0f, 460.0f},
        {"joints-2", JOINTS_2, 19.0f, 520.0f},
        {"joints-3", JOINTS_3, 13.0f, 250.0f},
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
