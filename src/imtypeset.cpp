// The public surface of include/imtypeset.h over the internal modules: fonts
// into src/fontlib and the atlas, text into src/style's material, a width into
// src/paragraph's lines, src/shipout's commands into src/draw's vertices and
// src/selection's answers. See src/context.h for what is cached and for the
// one place host input is validated.

#include "imtypeset.h"

#include "context.h"
#include "font_loader.h"
#include "unicode_tables.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace ImTypeset::detail;

// What the public functions of this file are made of. It has a name, and not
// internal linkage, for src/imtypeset_document.cpp, which builds a document's
// blocks from the same pieces: src/context.h declares the ones it uses.
namespace ImTypeset::detail::core
{

ImTypesetContext* g_context = nullptr;

// How many contexts this process has made: the last one's serial.
ImU64 g_contexts_made = 0;

// What ImTypeset::GetLastError() answers from: the outcome of the last call
// of a reporting entry, whichever context it was made in, or none.
ErrorState& last_error()
{
    static ErrorState state;
    return state;
}

// Why the last IMTYPESET_CHECKVERSION() failed; empty when none has. While it
// is not empty no context is made.
std::string& layout_mismatch()
{
    static std::string reason;
    return reason;
}

// The three ways a reporting entry ends.
bool fail(ImTypesetError code, std::string message, int byte_pos, int codepoint_pos)
{
    last_error().set(code, std::move(message), byte_pos, codepoint_pos);
    return false;
}

bool succeed()
{
    last_error().clear();
    return true;
}

bool report(const ErrorState& state)
{
    if (state.ok())
        return succeed();
    // Not the same object: a paragraph's error is reported as it stands.
    if (&state != &last_error())
        last_error() = state;
    return false;
}

bool no_context()
{
    return fail(ImTypesetError_NoContext, "there is no current ImTypeset context");
}

bool is_finite(double value)
{
    return value == value && value != HUGE_VAL && value != -HUGE_VAL;
}

double clamped(double value, double low, double high, double otherwise)
{
    if (!is_finite(value))
        return otherwise;
    return value < low ? low : (value > high ? high : value);
}

Scaled clamped_sp(Scaled value, Scaled low, Scaled high)
{
    return value < low ? low : (value > high ? high : value);
}

// A float a host wrote as a decimal, as the double the same decimal is. The
// reference computes a leading as round(size * 1.35) with the double 1.35; the
// float 1.35f widened is 1.35000002384, and the product then rounds the other
// way for about one size in fifty million. The shortest decimal that reads
// back as the same float is the one that was written.
double decimal_of(float value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.7g", static_cast<double>(value));
    const double parsed = std::strtod(text, nullptr);
    return static_cast<float>(parsed) == value ? parsed : static_cast<double>(value);
}

// The leading a host asked for, in ems. Zero, like anything that is not a
// positive number, is the default. The few values a host uses are remembered:
// a heading and its body alternate, and the conversion prints and parses.
double line_height_of(ImTypesetContext& ctx, float given)
{
    constexpr double DEFAULT_LINE_HEIGHT = 1.35;
    if (!(given > 0.0f))
        return DEFAULT_LINE_HEIGHT;
    for (const ImTypesetContext::LineHeight& known : ctx.line_heights) {
        if (known.given == given)
            return known.value;
    }
    ImTypesetContext::LineHeight& slot =
        ctx.line_heights[ctx.line_height_next++ % IM_ARRAYSIZE(ctx.line_heights)];
    slot.given = given;
    slot.value = clamped(decimal_of(given), MIN_LINE_HEIGHT, MAX_LINE_HEIGHT, DEFAULT_LINE_HEIGHT);
    return slot.value;
}

// The role a public face names, as an index of the registry's; -1 for
// ImTypesetFace_Default and for anything that is not a face.
int role_of(ImTypesetFace face)
{
    return face >= ImTypesetFace_Regular && face < ImTypesetFace_COUNT ? face - 1 : -1;
}

// A role as the header names it, for a message a host reads.
const char* public_face(int role)
{
    static const char* const NAMES[FACE_COUNT] = {
        "ImTypesetFace_Regular",    "ImTypesetFace_Bold", "ImTypesetFace_Italic",
        "ImTypesetFace_BoldItalic", "ImTypesetFace_Code", "ImTypesetFace_CodeBold",
        "ImTypesetFace_Math"};
    return role >= 0 && role < FACE_COUNT ? NAMES[role] : "a face that is not one";
}

// `face` is a registry index.
bool usable(ImTypesetContext& ctx, int face)
{
    return face >= 0 && face < FACE_COUNT &&
           ctx.library.face_data(static_cast<Face>(face)) != nullptr;
}

Spacing spacing_of(ImTypesetSpacing spacing)
{
    switch (spacing) {
    case ImTypesetSpacing_French:
        return Spacing::FRENCH;
    case ImTypesetSpacing_TeX:
        return Spacing::NONFRENCH;
    default:
        return Spacing::EXTRA_SPACE_ONLY;
    }
}

Align align_of(ImTypesetAlign align)
{
    switch (align) {
    case ImTypesetAlign_Left:
        return Align::LEFT;
    case ImTypesetAlign_Center:
        return Align::CENTER;
    case ImTypesetAlign_Right:
        return Align::RIGHT;
    default:
        return Align::JUSTIFY;
    }
}

bool same_material_style(const ResolvedStyle& a, const ResolvedStyle& b)
{
    return a.face == b.face && a.em == b.em && a.spacing == b.spacing && a.align == b.align &&
           a.same_device(b);
}

bool same_layout_style(const ResolvedStyle& a, const ResolvedStyle& b)
{
    return same_material_style(a, b) && a.left_indent == b.left_indent &&
           a.first_line_indent == b.first_line_indent && a.baselineskip == b.baselineskip &&
           a.lineskip == b.lineskip && a.line_break == b.line_break &&
           a.px_per_inch == b.px_per_inch;
}

bool is_newline(uint32_t codepoint)
{
    return codepoint == '\n' || codepoint == '\r';
}

// --- Text ----------------------------------------------------------------------------

void validate_runs(std::vector<StyledRun>& out, const ImTypesetRun* runs, int runs_count,
                   size_t size, uint32_t floor, ErrorState& error)
{
    if (runs != nullptr && runs_count > 0) {
        for (int i = 0; i < runs_count; ++i) {
            const ImTypesetRun& given = runs[i];
            int64_t start = given.Start;
            int64_t end = given.End;
            start = std::max<int64_t>(start, floor);
            end = std::min<int64_t>(end, static_cast<int64_t>(size));
            if (start >= end) {
                if (given.Start < static_cast<int>(floor) && given.End > given.Start &&
                    error.code == ImTypesetError_None)
                    error.set(ImTypesetError_DescriptionCut,
                              "runs must be in ascending order and must not overlap: one that "
                              "does not reach past the end of the run before it is left out",
                              std::max(given.Start, 0));
                continue;
            }
            StyledRun run;
            run.start = static_cast<uint32_t>(start);
            run.end = static_cast<uint32_t>(end);
            run.face = role_of(given.Face);
            // Zero is "as the paragraph", like every zero of a struct a host fills.
            run.scale = given.Scale > 0.0f
                            ? clamped(decimal_of(given.Scale), MIN_RUN_SCALE, MAX_RUN_SCALE, 1.0)
                            : 1.0;
            run.colour = given.Color;
            run.breakable = (given.Flags & ImTypesetRunFlags_NoBreak) == 0;
            run.tag = given.Tag;
            run.formula = (given.Flags & ImTypesetRunFlags_DisplayFormula) != 0
                              ? 2
                              : ((given.Flags & ImTypesetRunFlags_Formula) != 0 ? 1 : 0);
            out.push_back(run);
            floor = run.end;
        }
    }
}

// Stores the text and its runs. The runs are brought into order here, so that
// nothing later has to doubt them; offsets are moved onto codepoint starts
// when the material is built, where the text is decoded anyway.
bool set_text(ImTypesetParagraph& p, const char* text, size_t size, const ImTypesetRun* runs,
              int runs_count, ImTypesetTextFlags flags)
{
    // What was built from the old text is given up first, and the size a
    // widget compares by is no size until the new text is all in: if this is
    // unwound part way, the paragraph is rebuilt before it is used and a
    // widget takes its text for another.
    ++p.text_version;
    p.has_material = false;
    p.has_layout = false;
    p.has_model = false;
    p.given_size = static_cast<size_t>(-1);
    const size_t given_size = size;
    p.text_error.clear();
    if (size > static_cast<size_t>(MAX_TEXT_BYTES)) {
        size = static_cast<size_t>(MAX_TEXT_BYTES);
        // Not in the middle of a character.
        while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
            --size;
        p.text_error.set(ImTypesetError_TextTooLong,
                         "the text is longer than 16 MiB and was cut there",
                         static_cast<int>(size));
    }
    p.text.assign(text, size);
    p.flags = flags;
    p.runs.clear();
    validate_runs(p.runs, runs, runs_count, size, 0, p.text_error);
    p.given_size = given_size;
    return p.text_error.code == ImTypesetError_None;
}

// Brings glue and kerns inside the invariant of src/units.h. src/style holds a
// space's width and a kern to MAX_DIMEN as it reads them from the font, but
// a sentence space is then widened and its stretch multiplied (tex.web section
// 1044), which can take it past the bound again.
void clamp_nodes(std::vector<Node>& nodes, size_t first)
{
    for (size_t i = first; i < nodes.size(); ++i) {
        Node& node = nodes[i];
        if (node.type == NodeType::GLUE) {
            node.a = clamp_dimen(node.a);
            node.b = clamp_dimen(node.b);
            node.c = clamp_dimen(node.c);
        } else if (node.type == NodeType::KERN) {
            node.a = clamp_dimen(node.a);
        }
    }
}

// --- Formulas ------------------------------------------------------------------------

// The reference's reason for a `$$` block whose closing line is not there.
const char* const UNCLOSED_BLOCK = "the display block has not been closed yet";

// The math face at the sizes a formula in text of `size` needs. Null, with a
// reason, if the registry cannot give them.
MathFonts* math_fonts_at(ImTypesetContext& ctx, Scaled size, std::string* why)
{
    constexpr size_t REMEMBERED_SIZES = 64;
    if (ctx.math_fonts_generation != ctx.library.generation() ||
        ctx.math_fonts.size() >= REMEMBERED_SIZES) {
        ctx.math_fonts.clear();
        ctx.math_fonts_generation = ctx.library.generation();
    }
    const auto found = ctx.math_fonts.find(size);
    if (found != ctx.math_fonts.end()) {
        found->second->rules = ctx.tex_rules;
        return found->second.get();
    }
    if (!ctx.library.is_registered(Face::MATH)) {
        // What the reference's registry raises for a role with no file.
        *why = "MissingFontError: math has no bundled file and none is registered";
        return nullptr;
    }
    std::unique_ptr<MathFonts> fonts(new MathFonts());
    if (!fonts->init(size, fontlib_math_metrics(ctx.library), ctx.resolve, why))
        return nullptr;
    fonts->rules = ctx.tex_rules;
    return ctx.math_fonts.emplace(size, std::move(fonts)).first->second.get();
}

// One formula, without its delimiters, as an atom box of `arena`: the
// reference's `typeset` and `atom_box`. False with `refusal` filled.
bool build_formula(ImTypesetContext& ctx, std::string_view source, bool display, Scaled size,
                   NodeArena& arena, Node* atom, Refusal& refusal)
{
    refusal = Refusal();
    if (!parse(source, display, ctx.formula, refusal))
        return false;
    std::string& why = ctx.reason;
    why.clear();
    MathFonts* fonts = math_fonts_at(ctx, size, &why);
    if (fonts == nullptr) {
        refusal.kind = RefusalKind::FONT;
        refusal.reason = "the math face could not set it: " + why;
        return false;
    }
    Node box;
    MathStatus status;
    if (!set_formula(ctx.formula, *fonts, arena, box, status)) {
        refusal = refusal_from(status, source, ctx.formula, *fonts);
        return false;
    }
    const NodeArena::Mark before = arena.mark();
    *atom = atom_box(arena, box);
    if (!atom->is(NodeType::BOX)) {
        arena.rewind(before);
        refusal.kind = RefusalKind::LAYOUT;
        refusal.reason = "the formula's boxes nest more deeply than a paragraph can hold";
        return false;
    }
    return true;
}

// The face a refused formula's source is set in: the code face, as the
// reference does, at the size of the text around it; the text's own face when
// nothing has been added for code.
FontSpec refused_font(ImTypesetContext& ctx, const ResolvedStyle& style, FontSpec font)
{
    if (usable(ctx, static_cast<int>(Face::CODE)))
        return style.font(Face::CODE, font.size);
    return font;
}

// How many codepoints of the text start before byte `byte`. `offsets` holds
// `count` starts and the text's size.
int codepoints_before(const std::vector<uint32_t>& offsets, uint32_t byte)
{
    return static_cast<int>(std::lower_bound(offsets.begin(), offsets.end() - 1, byte) -
                            offsets.begin());
}

bool is_line_end(uint32_t codepoint)
{
    return codepoint == '\n' || codepoint == '\r';
}

// If the text, between `begin` and `end`, is one display block, sets it and
// answers true. The block rule is the reference's `_math_block` held to a
// whole text: a line that is `$$` opens it and the next such line closes it,
// or one line is `$$ ... $$` with something between and no `$$` inside. A
// block followed by anything else is not one here, where a text is one block.
//
// The source goes to the typesetter with each run of white space as one
// space, which is also how it copies. A block with no closing line is its
// source so far in the code face, and is not offered to the typesetter.
bool display_block(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style,
                   const std::vector<uint32_t>& cps, const std::vector<uint32_t>& offs,
                   size_t begin, size_t end)
{
    const auto dollars = [&](size_t at) {
        return at + 1 < end && cps[at] == '$' && cps[at + 1] == '$';
    };
    // [from, to) without the white space at its ends.
    const auto trim = [&](size_t& from, size_t& to) {
        while (from < to && py_isspace(cps[from]))
            ++from;
        while (to > from && py_isspace(cps[to - 1]))
            --to;
    };
    if (!dollars(begin))
        return false;
    size_t line_end = begin;
    while (line_end < end && !is_line_end(cps[line_end]))
        ++line_end;
    size_t first = begin;
    size_t last = line_end;
    trim(first, last);

    size_t content_begin = 0;
    size_t content_end = 0;
    size_t closer = end; // Where the closing marker starts; `end` when there is none.
    if (last - first != 2) {
        if (line_end < end || last - first < 5 || !dollars(last - 2))
            return false;
        content_begin = first + 2;
        content_end = last - 2;
        for (size_t at = content_begin; at + 1 < content_end; ++at) {
            if (cps[at] == '$' && cps[at + 1] == '$')
                return false;
        }
        closer = content_end;
        trim(content_begin, content_end);
        if (content_begin == content_end)
            return false;
    } else {
        content_begin = std::min(line_end + 1, end);
        content_end = end;
        size_t line = content_begin;
        while (line < end) {
            size_t stop = line;
            while (stop < end && !is_line_end(cps[stop]))
                ++stop;
            size_t from = line;
            size_t to = stop;
            trim(from, to);
            if (to - from == 2 && dollars(from)) {
                if (to != end)
                    return false;
                closer = from;
                content_end = line;
                break;
            }
            line = stop + 1;
        }
    }
    const bool closed = closer != end;

    // `$$`, the source with its white space collapsed, `$$`.
    std::string& shown = p.display_text;
    std::vector<uint32_t>& map = p.display_map;
    shown.assign("$$");
    map.push_back(offs[begin]);
    map.push_back(offs[begin] + 1);
    size_t gap = content_end; // The first white space since the last word, if any.
    for (size_t at = content_begin; at < content_end; ++at) {
        if (py_isspace(cps[at])) {
            if (gap == content_end)
                gap = at;
            continue;
        }
        if (gap != content_end && shown.size() > 2) {
            shown.push_back(' ');
            map.push_back(offs[gap]);
        }
        gap = content_end;
        for (uint32_t byte = offs[at]; byte < offs[at + 1]; ++byte) {
            shown.push_back(p.text[byte]);
            map.push_back(byte);
        }
    }
    const size_t source_size = shown.size() - 2;
    if (closed) {
        shown.append("$$");
        map.push_back(offs[closer]);
        map.push_back(offs[closer] + 1);
    }
    map.push_back(offs[closed ? closer + 2 : end]);

    p.display_block = true;
    Run& run = p.block.run;
    const FontSpec font = style.font(style.face, style.em);
    FormulaRecord record;
    record.begin = offs[begin];
    record.end = map.back();
    record.display = true;

    Node atom;
    Refusal refusal;
    if (closed &&
        build_formula(ctx, std::string_view(shown).substr(2, source_size), true, font.size,
                      p.material_arena != nullptr ? *p.material_arena : p.arena, &atom, refusal)) {
        AtomSource source;
        source.node = static_cast<uint32_t>(run.nodes.size());
        source.begin = 0;
        source.end = static_cast<uint32_t>(shown.size());
        source.in_display_text = true;
        run.nodes.push_back(atom);
        run.space_factor = NEUTRAL_SPACE_FACTOR;
        p.source_offsets.push_back(offs[begin]);
        p.atoms.push_back(source);
        p.formulas.push_back(record);
        return true;
    }

    record.refused = true;
    if (closed) {
        record.reason = c_string_reason(refusal.reason);
        record.byte_pos = static_cast<int>(map[std::min(2 + refusal.byte_position, shown.size())]);
    } else {
        record.reason = UNCLOSED_BLOCK;
        record.byte_pos = static_cast<int>(offs[begin]);
    }
    record.codepoint_pos = codepoints_before(offs, static_cast<uint32_t>(record.byte_pos));
    if (p.material_error.code == ImTypesetError_None)
        p.material_error.set(ImTypesetError_MathRefused, record.reason, record.byte_pos,
                             record.codepoint_pos);
    p.formulas.push_back(std::move(record));

    // Its source, which may be long and is a block of its own: breakable.
    Composition composition;
    composition.spacing = style.spacing;
    composition.rules = ctx.tex_rules;
    const size_t first_offset = p.source_offsets.size();
    run.source_base = 0;
    append_text(run, shown, refused_font(ctx, style, font), true, composition, ctx.resolve,
                &ctx.reason);
    clamp_nodes(run.nodes, 0);
    for (size_t at = first_offset; at < p.source_offsets.size(); ++at)
        p.source_offsets[at] = map[std::min<size_t>(p.source_offsets[at], shown.size())];
    return true;
}

// The text as material: the nodes of one paragraph, before any width.
//
// The text is decoded once and walked as codepoints. It is cut into pieces at
// run boundaries, at newlines when they are hard breaks, and at formulas, and
// each piece goes to src/style's append_text with its own font.
//
// WHITE SPACE COLLAPSES ACROSS THE PIECES, which append_text alone would not
// do: it makes one glue for a run of white space within one call, and two
// calls that meet in the middle of such a run would make two. So a piece that
// follows white space, or starts a line, has its own leading white space
// removed before it is handed over. White space at the ends of the text is
// dropped, and at the ends of each line when newlines are hard breaks.
//
// FORMULAS are found before anything is set (`scan_math`, or an installed
// layer's scanner). One that sets is a single node, an atom box at the size
// of the text it starts in, and the space factor after it is neutral, as
// after any box (tex.web section 1076). One that is refused is its source,
// delimiters included, in the code face with no break inside. A backslash
// that escapes a dollar is left out of the piece it is in, which is otherwise
// set as one piece, so the dollar kerns with what is before it.
void build_material_now(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style);

void build_material(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style)
{
    Building building(p.has_material);
    build_material_now(ctx, p, style);
    p.on_device = false;
    for (const Node& node : p.block.run.nodes) {
        FontSpec spec;
        if (node.is(NodeType::CHAR) && parse_font_key(node.font(), &spec) && spec.device != 0) {
            p.on_device = true;
            break;
        }
    }
    building.finished = true;
}

void build_material_now(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style)
{
    ++ctx.stats.TextBuilds;
    // Metrics the registry has aged out are freed here, where no build is
    // under way: the two caches that keep pointers to them go first.
    if (ctx.library.metrics_expired() != 0) {
        forget_measurements(ctx);
        ctx.math_fonts.clear();
        ctx.library.release_expired_metrics();
    }
    p.block.clear();
    p.source_offsets.clear();
    p.block.run.source_offsets = &p.source_offsets;
    p.ink_spans.clear();
    p.atoms.clear();
    p.formulas.clear();
    p.display_block = false;
    p.display_text.clear();
    p.display_map.clear();
    // A document's block keeps its formulas' boxes in the document's arena,
    // where its lines are made; that arena is the document's to empty.
    NodeArena& arena = p.material_arena != nullptr ? *p.material_arena : p.arena;
    if (p.material_arena == nullptr)
        arena.clear();
    p.material_error = p.text_error;
    p.fallback = false;
    p.has_material = true;
    p.has_layout = false;
    p.material_style = style;
    p.material_text_version = p.text_version;
    p.material_generation = ctx.library.generation();

    if (!usable(ctx, static_cast<int>(style.face))) {
        p.fallback = true;
        p.material_error.set(ImTypesetError_NoFont, std::string("no font has been added for ") +
                                                        public_face(static_cast<int>(style.face)));
        p.material_mark = arena.mark();
        return;
    }

    const std::string_view text(p.text);
    std::vector<uint32_t>& cps = ctx.codepoints;
    std::vector<uint32_t>& offs = ctx.offsets;
    cps.clear();
    offs.clear();
    decode_utf8(text, cps, &offs);
    offs.push_back(static_cast<uint32_t>(text.size()));

    size_t begin = 0;
    size_t end = cps.size();
    while (begin < end && py_isspace(cps[begin]))
        ++begin;
    while (end > begin && py_isspace(cps[end - 1]))
        --end;

    const bool hard = (p.flags & ImTypesetTextFlags_HardBreaks) != 0;
    const bool wants_math = (p.flags & ImTypesetTextFlags_Math) != 0;
    const bool layered = wants_math && ctx.math.scan != nullptr && ctx.math.build != nullptr;
    const bool built_in = wants_math && !layered;
    Composition composition;
    composition.spacing = style.spacing;
    composition.rules = ctx.tex_rules;
    Run& run = p.block.run;
    std::string& why = ctx.scratch;

    std::vector<FoundFormula>& found = ctx.found;
    std::vector<uint32_t>& dropped = ctx.dropped;
    found.clear();
    dropped.clear();
    if (built_in) {
        if (display_block(ctx, p, style, cps, offs, begin, end)) {
            run.source_base = 0;
            p.material_mark = arena.mark();
            return;
        }
        scan_math(cps.data(), offs.data(), begin, end, found, dropped);
    } else if (layered) {
        size_t from = offs[begin];
        while (from < text.size()) {
            size_t b = 0;
            size_t e = 0;
            if (!ctx.math.scan(ctx.math.user, text, from, &b, &e) || b < from || e <= b ||
                e > text.size())
                break;
            found.push_back(FoundFormula{static_cast<uint32_t>(b), static_cast<uint32_t>(e), 0, 0});
            from = e;
        }
    }
    // The formulas the host marked itself (ImTypesetRunFlags_Formula): each
    // run is one, whole, and a dollar formula that overlaps one gives way.
    bool marked = false;
    for (const StyledRun& styled : p.runs) {
        if (styled.formula == 0 || styled.start >= styled.end)
            continue;
        found.erase(std::remove_if(found.begin(), found.end(),
                                   [&](const FoundFormula& f) {
                                       return f.explicit_kind == 0 && f.begin < styled.end &&
                                              styled.start < f.end;
                                   }),
                    found.end());
        found.push_back(FoundFormula{styled.start, styled.end, 0, styled.formula});
        marked = true;
    }
    if (marked)
        std::sort(found.begin(), found.end(),
                  [](const FoundFormula& a, const FoundFormula& b) { return a.begin < b.begin; });

    size_t run_index = 0;
    size_t formula = 0;
    size_t drop = 0;
    bool strip = true;
    size_t i = begin;
    while (i < end) {
        if (hard && is_newline(cps[i])) {
            const uint32_t at = offs[i];
            i += (cps[i] == '\r' && i + 1 < end && cps[i + 1] == '\n') ? 2 : 1;
            hard_break(run, style.align, at);
            strip = true;
            continue;
        }

        // The style at this character, and where it ends.
        while (run_index < p.runs.size() && p.runs[run_index].end <= offs[i])
            ++run_index;
        const StyledRun* styled = nullptr;
        size_t boundary = text.size();
        if (run_index < p.runs.size()) {
            if (p.runs[run_index].start <= offs[i]) {
                styled = &p.runs[run_index];
                boundary = styled->end;
            } else {
                boundary = p.runs[run_index].start;
            }
        }
        FontSpec font;
        font.face = style.face;
        font.size = style.em;
        bool breakable = true;
        ImU32 colour = 0;
        if (styled != nullptr) {
            if (styled->face >= 0) {
                if (usable(ctx, styled->face)) {
                    font.face = static_cast<Face>(styled->face);
                } else if (p.material_error.code == ImTypesetError_None) {
                    p.material_error.set(ImTypesetError_NoFont,
                                         std::string("no font has been added for ") +
                                             public_face(styled->face) +
                                             "; the run is set in the paragraph's face",
                                         static_cast<int>(offs[i]), static_cast<int>(i));
                }
            }
            if (styled->scale != 1.0)
                font.size = clamped_sp(
                    round_to_scaled(static_cast<double>(style.em) * styled->scale), 1, MAX_EM);
            breakable = styled->breakable;
            colour = styled->colour;
        }
        // Measured as the style measures the face the piece came to.
        font = style.font(font.face, font.size);

        while (formula < found.size() && found[formula].end <= offs[i])
            ++formula;
        if (formula < found.size() && offs[i] >= found[formula].begin) {
            const FoundFormula span = found[formula++];
            const std::string_view source = text.substr(span.begin, span.end - span.begin);
            FormulaRecord record;
            record.begin = span.begin;
            record.end = span.end;
            record.display = span.explicit_kind != 0 ? span.explicit_kind == 2 : span.marker == 2;
            Node atom;
            uint32_t byte_pos = 0;
            uint32_t codepoint_pos = 0;
            bool built = false;
            why.clear();
            if (layered && span.explicit_kind == 0) {
                built = ctx.math.build(ctx.math.user, source, font, arena, ctx.measure(),
                                       ctx.resolve, &atom, &why, &byte_pos, &codepoint_pos);
            } else {
                Refusal refusal;
                built =
                    build_formula(ctx, source.substr(span.marker, source.size() - 2 * span.marker),
                                  record.display, font.size, arena, &atom, refusal);
                if (!built) {
                    why = refusal.reason;
                    byte_pos = static_cast<uint32_t>(span.marker + refusal.byte_position);
                    codepoint_pos = static_cast<uint32_t>(span.marker + refusal.position);
                }
            }
            const size_t first = run.nodes.size();
            if (built) {
                run.nodes.push_back(atom);
                run.space_factor = NEUTRAL_SPACE_FACTOR;
                p.source_offsets.push_back(span.begin);
                AtomSource copied;
                copied.node = static_cast<uint32_t>(first);
                copied.begin = span.begin;
                copied.end = span.end;
                p.atoms.push_back(copied);
            } else {
                // Refused: say where, and set the source as the text it is.
                record.refused = true;
                record.reason = c_string_reason(why);
                record.byte_pos =
                    static_cast<int>(std::min<size_t>(span.begin + byte_pos, span.end));
                record.codepoint_pos = static_cast<int>(i + codepoint_pos);
                if (p.material_error.code == ImTypesetError_None)
                    p.material_error.set(ImTypesetError_MathRefused, why, record.byte_pos,
                                         record.codepoint_pos);
                run.source_base = span.begin;
                why.clear();
                append_text(run, source, refused_font(ctx, style, font), false, composition,
                            ctx.resolve, &why);
                clamp_nodes(run.nodes, first);
            }
            p.formulas.push_back(std::move(record));
            if (colour != 0 && run.nodes.size() > first)
                p.ink_spans.push_back(InkSpan{static_cast<uint32_t>(first),
                                              static_cast<uint32_t>(run.nodes.size()), colour});
            while (i < end && offs[i] < span.end)
                ++i;
            strip = false;
            continue;
        }
        if (formula < found.size())
            boundary = std::min<size_t>(boundary, found[formula].begin);

        size_t j = i;
        while (j < end && offs[j] < boundary && !(hard && is_newline(cps[j])))
            ++j;
        if (j == i)
            ++j; // A boundary inside a character: the character goes with this piece.
        size_t k = i;
        if (strip) {
            while (k < j && py_isspace(cps[k]))
                ++k;
        }
        if (k < j) {
            const size_t first = run.nodes.size();
            const size_t first_offset = p.source_offsets.size();
            std::string_view piece = text.substr(offs[k], offs[j] - offs[k]);
            while (drop < dropped.size() && dropped[drop] < k)
                ++drop;
            ctx.drop_at.clear();
            if (drop < dropped.size() && dropped[drop] < j) {
                // The piece without the backslashes that escape its dollars.
                ctx.piece.clear();
                for (size_t c = k; c < j; ++c) {
                    if (drop < dropped.size() && dropped[drop] == c) {
                        ctx.drop_at.push_back(static_cast<uint32_t>(ctx.piece.size()));
                        ++drop;
                        continue;
                    }
                    ctx.piece.append(p.text, offs[c], offs[c + 1] - offs[c]);
                }
                piece = ctx.piece;
            }
            run.source_base = offs[k];
            why.clear();
            if (!piece.empty() &&
                !append_text(run, piece, font, breakable, composition, ctx.resolve, &why)) {
                if (p.material_error.code == ImTypesetError_None)
                    p.material_error.set(ImTypesetError_NoFont, why, static_cast<int>(offs[k]),
                                         static_cast<int>(k));
            }
            // An offset past a backslash that was left out is that many bytes
            // further on in the text than in the piece.
            for (size_t at = first_offset; !ctx.drop_at.empty() && at < p.source_offsets.size();
                 ++at) {
                const uint32_t local = p.source_offsets[at] - offs[k];
                p.source_offsets[at] += static_cast<uint32_t>(
                    std::upper_bound(ctx.drop_at.begin(), ctx.drop_at.end(), local) -
                    ctx.drop_at.begin());
            }
            clamp_nodes(run.nodes, first);
            if (colour != 0 && run.nodes.size() > first)
                p.ink_spans.push_back(InkSpan{static_cast<uint32_t>(first),
                                              static_cast<uint32_t>(run.nodes.size()), colour});
            strip = py_isspace(cps[j - 1]);
        }
        i = j;
    }
    run.source_base = 0;
    p.material_mark = arena.mark();
}

// --- Layout --------------------------------------------------------------------------

float ceil_px(double pixels)
{
    // A length within a scaled point of a whole pixel is that pixel: a width
    // given as 384 px comes back as 384.00001.
    const double up = std::ceil(pixels - 1.0e-4);
    return static_cast<float>(clamped(up, 0.0, 1.0e7, 0.0));
}

bool is_item(const DrawCmd& cmd)
{
    return cmd.type == CmdType::GLYPH || cmd.type == CmdType::SET_GLUE || cmd.type == CmdType::ATOM;
}

int byte_under(const ImTypesetParagraph& p, const LineBox& line, Scaled x, int fallback)
{
    // A GLYPH command carries no width, so the item is found by where the
    // items start: within a line they are shipped left to right.
    bool found = false;
    Scaled best = 0;
    uint32_t index = 0;
    const size_t end = std::min<size_t>(line.end, p.cmds.size());
    for (size_t c = static_cast<size_t>(line.first) + 1; c < end; ++c) {
        const DrawCmd& cmd = p.cmds[c];
        if (!is_item(cmd) || cmd.x > x || (found && cmd.x < best))
            continue;
        found = true;
        best = cmd.x;
        index = cmd.index;
    }
    return found ? byte_at_index(p, index) : fallback;
}

// The lines of what was shipped, one per span of the line index that a LINE
// command opens.
//
// A line's content runs from its first character or formula to the glue that
// closes the line: the right skip, and before it on the last line the
// paragraph's fill. That glue is told from glue inside the line by its index,
// which is past the last character's; what a formula ships carries the
// formula's own index and is never past it.
void ensure_lines(const ImTypesetParagraph& p)
{
    if (p.has_lines)
        return;
    Building building(p.has_lines);
    p.has_lines = true;
    p.line_boxes.clear();
    for (const LineSpan& span : p.lines.spans) {
        if (span.first >= span.end || span.end > p.cmds.size() ||
            !p.cmds[span.first].is(CmdType::LINE))
            continue;
        const DrawCmd& line = p.cmds[span.first];
        LineBox box;
        box.first = span.first;
        box.end = span.end;
        box.baseline = line.y;
        box.top = line.y - line.height;
        box.bottom = line.y + line.depth;
        box.index = line.index;
        box.index_end = std::max(line.end, line.index);
        box.x0 = line.x;
        box.x1 = line.x + line.width;

        bool inked = false;
        uint32_t first_ink = 0;
        uint32_t last_ink = 0;
        for (uint32_t at = span.first + 1; at < span.end; ++at) {
            const DrawCmd& cmd = p.cmds[at];
            if (cmd.type != CmdType::GLYPH && cmd.type != CmdType::ATOM)
                continue;
            if (!inked || cmd.index < first_ink) {
                first_ink = cmd.index;
                box.x0 = cmd.x;
            }
            if (!inked || cmd.index > last_ink)
                last_ink = cmd.index;
            inked = true;
        }
        bool closed = false;
        for (uint32_t at = span.first + 1; at < span.end; ++at) {
            const DrawCmd& cmd = p.cmds[at];
            if (cmd.type != CmdType::SET_GLUE || (inked && cmd.index <= last_ink))
                continue;
            if (!closed || cmd.x < box.x1)
                box.x1 = cmd.x;
            closed = true;
            if (!inked && cmd.x < box.x0)
                box.x0 = cmd.x;
        }
        if (!inked)
            box.x0 = box.x1; // A blank line: nothing wide, where its fill begins.
        if (box.x1 < box.x0)
            box.x1 = box.x0;
        p.line_boxes.push_back(box);
    }
    building.finished = true;
}

void layout_paragraph_now(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style,
                          Scaled width);

// Index to source byte: each mapped node gives its item the byte it came
// from; an item the text did not contain takes the next one's.
void map_index_bytes(const std::vector<uint32_t>& shipped_at,
                     const std::vector<uint32_t>& source_offsets, uint32_t items,
                     uint32_t text_size, std::vector<uint32_t>& out)
{
    out.assign(static_cast<size_t>(items) + 1, 0xFFFFFFFFu);
    out[items] = text_size;
    const size_t mapped = std::min(shipped_at.size(), source_offsets.size());
    for (size_t m = 0; m < mapped; ++m) {
        const uint32_t index = shipped_at[m];
        if (index < items && source_offsets[m] < out[index])
            out[index] = std::min(source_offsets[m], text_size);
    }
    for (uint32_t index = items; index-- > 0;) {
        if (out[index] > out[index + 1])
            out[index] = out[index + 1];
    }
}

// A run's colour, as the range of indices its nodes shipped as.
void ink_ranges(const std::vector<uint32_t>& shipped_at, const std::vector<InkSpan>& spans,
                std::vector<InkRange>& out)
{
    out.clear();
    for (const InkSpan& span : spans) {
        uint32_t low = NOT_SHIPPED;
        uint32_t high = 0;
        for (size_t m = span.first; m < span.end && m < shipped_at.size(); ++m) {
            const uint32_t index = shipped_at[m];
            if (index == NOT_SHIPPED)
                continue;
            if (low == NOT_SHIPPED)
                low = index;
            high = std::max(high, index + 1);
        }
        if (low == NOT_SHIPPED)
            continue;
        if (!out.empty() && low < out.back().end)
            low = out.back().end;
        if (low < high)
            out.push_back(InkRange{low, high, span.colour});
    }
}

void layout_paragraph(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style,
                      Scaled width)
{
    Building building(p.has_layout);
    layout_paragraph_now(ctx, p, style, width);
    // Whatever it was left as: laid out, or not for want of anything to lay.
    building.finished = true;
}

void layout_paragraph_now(ImTypesetContext& ctx, ImTypesetParagraph& p,
                          const ResolvedStyle& resolved, Scaled width)
{
    const ResolvedStyle style =
        p.has_material ? keeping_device(ctx, resolved, p.material_style) : resolved;
    const bool rebuilt = !p.has_material || p.material_text_version != p.text_version ||
                         p.material_generation != ctx.library.generation() ||
                         !same_material_style(p.material_style, style);
    if (rebuilt)
        build_material(ctx, p, style);
    if (p.has_layout && p.layout_width == width && same_layout_style(p.layout_style, style))
        return;

    // Total fit ran out of its work budget on this material the last time, and
    // nothing but the width has changed since: it would run out again, at the
    // same cost, on every step of a resize. First fit is asked for outright.
    const bool over_budget = !rebuilt && p.has_layout && !p.fallback &&
                             p.layout.broken.budget_exhausted &&
                             same_layout_style(p.layout_style, style);

    ++ctx.stats.Layouts;
    p.has_layout = true;
    p.has_model = false;
    p.layout_width = width;
    p.layout_style = style;
    p.cmds.clear();
    p.lines.clear();
    p.inks.clear();
    p.index_bytes.clear();
    p.line_boxes.clear();
    p.has_lines = false;
    p.size = ImVec2(0.0f, 0.0f);
    p.line_count = 0;
    // What is wrong with the material is wrong with every layout of it; what
    // is wrong with one layout is found again, or not, by the next.
    p.error = p.material_error;
    if (p.fallback) {
        p.layout.broken.budget_exhausted = false;
        return;
    }

    p.arena.rewind(p.material_mark);
    BlockStyle block;
    // A display formula is centred and not indented: the reference's
    // `_display_math` replaces those two values of the block's style.
    block.align = p.display_block ? Align::CENTER : style.align;
    block.left_indent = style.left_indent;
    block.first_line_indent = p.display_block ? 0 : style.first_line_indent;
    // tex.web section 1091: the indent is one box at the head of the
    // paragraph. The reference also narrows the first line by it.
    block.first_line = FirstLineIndent::BOX_ONLY;
    block.interline.baselineskip.width = style.baselineskip;
    block.interline.lineskip.width = style.lineskip;
    block.strut = Strut::filling(style.baselineskip);
    block.ragged_total_fit = style.line_break == ImTypesetLineBreak_TotalFit;
    LineBreakParams params = prose_breaking();
    params.rules = ctx.tex_rules;
    if (style.line_break == ImTypesetLineBreak_FirstFit)
        params.first_fit = true;
    // The same material and the same skips as the attempt that ran out, so the
    // lines are the ones its fallback produced.
    if (over_budget)
        params.first_fit = true;

    const CharMeasure measure = ctx.measure();
    const bool composed =
        ctx.composer.compose(p.arena, p.block, block, width, params, measure, p.layout);
    if (over_budget)
        p.layout.broken.budget_exhausted = true;
    const ShipError shipped = ship(p.arena, p.layout, Point{0, 0}, measure, p.cmds);
    if ((!composed || shipped != ShipError::NONE) && p.error.ok())
        p.error.set(ImTypesetError_LayoutFailed,
                    "a character could not be measured; its line is set without it");
    index_lines(p.cmds.data(), p.cmds.size(), p.lines);
    p.line_count = static_cast<int>(std::min<size_t>(p.layout.lines.size(), 0x7FFFFFFF));

    if (p.layout.box.is(NodeType::BOX)) {
        const Box& box = p.arena.box(p.layout.box);
        Scaled block_width = box.width;
        // Ragged-right text is as wide as its longest line; every other
        // alignment places its lines across the whole measure.
        if (block.align == Align::LEFT) {
            ensure_lines(p);
            block_width = 0;
            for (const LineBox& line : p.line_boxes)
                block_width = std::max(block_width, line.x1);
            block_width = std::min(block_width, box.width);
        }
        p.size.x = ceil_px(px_from_sp(block_width, style.px_per_inch));
        p.size.y = ceil_px(px_from_sp(box.height + box.depth, style.px_per_inch));
    }

    map_index_bytes(p.layout.shipped, p.source_offsets, p.layout.item_count,
                    static_cast<uint32_t>(p.text.size()), p.index_bytes);
    ink_ranges(p.layout.shipped, p.ink_spans, p.inks);
}

// A colour a host gave, or ImGui's, with ImGui's style.Alpha applied either
// way: text inside BeginDisabled() dims whatever colour it was given.
ImU32 ink_of(const ImTypesetParagraph& p)
{
    return p.colour != 0 ? ImGui::GetColorU32(p.colour) : ImGui::GetColorU32(ImGuiCol_Text);
}

ImU32 highlight_of(const ImTypesetParagraph& p)
{
    return p.selection_colour != 0 ? ImGui::GetColorU32(p.selection_colour)
                                   : ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
}

void remember_colours(ImTypesetParagraph& p, const ImTypesetStyle& style)
{
    p.colour = style.Color;
    p.selection_colour = style.SelectionColor;
}

void ensure_model(const ImTypesetParagraph& p)
{
    if (p.has_model)
        return;
    Building building(p.has_model);
    p.has_model = true;
    p.model.build(p.cmds);
    // Ascending by index: the two lists are, and no index is in both.
    p.separators.clear();
    size_t tab = 0;
    for (const uint32_t index : p.layout.hard_breaks) {
        for (; tab < p.cell_breaks.size() && p.cell_breaks[tab] < index; ++tab)
            p.separators.push_back(IndexedText{p.cell_breaks[tab], "\t", 1});
        p.separators.push_back(IndexedText{index, "\n", 1});
    }
    for (; tab < p.cell_breaks.size(); ++tab)
        p.separators.push_back(IndexedText{p.cell_breaks[tab], "\t", 1});
    p.atom_texts.clear();
    for (const AtomSource& atom : p.atoms) {
        if (atom.node >= p.layout.shipped.size() || p.layout.shipped[atom.node] == NOT_SHIPPED)
            continue;
        const std::string& bytes = atom.in_display_text ? p.display_text : p.text;
        p.atom_texts.push_back(IndexedText{p.layout.shipped[atom.node], bytes.data() + atom.begin,
                                           atom.end - atom.begin});
    }
    // What a stand-in was written as, encoded once; the pointers are taken
    // when the bytes have stopped moving.
    p.originals.clear();
    p.original_bytes.clear();
    for (const Original& original : p.layout.originals) {
        char utf8[5];
        const uint32_t c = original.codepoint;
        size_t n = 0;
        if (c < 0x80) {
            utf8[n++] = static_cast<char>(c);
        } else if (c < 0x800) {
            utf8[n++] = static_cast<char>(0xC0 | (c >> 6));
            utf8[n++] = static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            utf8[n++] = static_cast<char>(0xE0 | (c >> 12));
            utf8[n++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            utf8[n++] = static_cast<char>(0x80 | (c & 0x3F));
        } else {
            utf8[n++] = static_cast<char>(0xF0 | ((c >> 18) & 0x07));
            utf8[n++] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            utf8[n++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            utf8[n++] = static_cast<char>(0x80 | (c & 0x3F));
        }
        IndexedText entry;
        entry.index = original.index;
        entry.size = n;
        // The offset, for now, where the pointer will be.
        entry.text = nullptr;
        p.originals.push_back(entry);
        p.original_bytes.append(utf8, n);
    }
    size_t offset = 0;
    for (IndexedText& entry : p.originals) {
        entry.text = p.original_bytes.data() + offset;
        offset += entry.size;
    }
    building.finished = true;
}

// The index of the item at a byte offset of the text: the first item that
// came from that byte or a later one. Items the text did not contain carry
// the offset of the character after them, so the character itself is the
// last of those that share it. One past the last item for an offset at or
// past the end of the text.
uint32_t index_at_byte(const ImTypesetParagraph& p, int byte_offset)
{
    const std::vector<uint32_t>& bytes = p.index_bytes;
    if (bytes.empty())
        return 0;
    const uint32_t wanted = byte_offset <= 0 ? 0u : static_cast<uint32_t>(byte_offset);
    const auto found = std::lower_bound(bytes.begin(), bytes.end(), wanted);
    if (found == bytes.end())
        return static_cast<uint32_t>(bytes.size() - 1);
    const auto last = std::upper_bound(found, bytes.end(), *found) - 1;
    return static_cast<uint32_t>(last - bytes.begin());
}

int byte_at_index(const ImTypesetParagraph& p, uint32_t index)
{
    if (p.index_bytes.empty())
        return 0;
    const size_t last = p.index_bytes.size() - 1;
    return static_cast<int>(p.index_bytes[std::min<size_t>(index, last)]);
}

// The last line that starts at or before an item. `line_boxes` is not empty.
std::vector<LineBox>::const_iterator line_of_index(const ImTypesetParagraph& p, uint32_t index)
{
    auto line =
        std::upper_bound(p.line_boxes.begin(), p.line_boxes.end(), index,
                         [](uint32_t wanted, const LineBox& box) { return wanted < box.index; });
    if (line != p.line_boxes.begin())
        --line;
    return line;
}

// Whether there is anything laid out to ask about.
bool laid_out(const ImTypesetParagraph* p)
{
    return p != nullptr && p->ctx != nullptr && p->has_layout && !p->fallback;
}

// Fills `args` for one paragraph; `rects` receives the selection's rectangles.
void draw_args(ImTypesetContext& ctx, const ImTypesetParagraph& p, const ImVec2& pos,
               const ImTypesetTextRange* selection, DrawArgs& args)
{
    args.origin = ImVec2(is_finite(pos.x) ? pos.x : 0.0f, is_finite(pos.y) ? pos.y : 0.0f);
    args.px_per_inch = p.layout_style.px_per_inch;
    args.rules = ctx.draw_rules;
    args.palette.rule = ink_of(p);
    args.inks = p.inks.data();
    args.ink_count = p.inks.size();
    if (!p.inks.empty() && ImGui::GetStyle().Alpha < 1.0f) {
        ctx.inks.assign(p.inks.begin(), p.inks.end());
        for (InkRange& ink : ctx.inks)
            ink.colour = ImGui::GetColorU32(ink.colour);
        args.inks = ctx.inks.data();
    }
    args.cull_lines = true;
    ctx.rects.clear();
    if (selection != nullptr && selection->Start < selection->End) {
        ensure_model(p);
        p.model.rects(selection_of(p, *selection), ctx.rects);
        args.palette.highlight = highlight_of(p);
        args.highlights = ctx.rects.data();
        args.highlight_count = ctx.rects.size();
    }
}

void draw_to_list(ImTypesetContext& ctx, const ImTypesetParagraph& p, ImDrawList* draw_list,
                  const ImVec2& pos, const ImTypesetTextRange* selection)
{
    DrawArgs args;
    draw_args(ctx, p, pos, selection, args);
    DrawRun run;
    run.cmds = p.cmds.data();
    run.count = p.cmds.size();
    run.ink = ink_of(p);
    run.lines = &p.lines;
    const int before = draw_list->VtxBuffer.Size;
    ctx.drawer.draw_runs(draw_list, &run, 1, args, ctx.fonts, ctx.resolve);
    ++ctx.stats.DrawCalls;
    ctx.stats.VerticesEmitted += static_cast<ImU64>(draw_list->VtxBuffer.Size - before);
}

// --- The ImGui side: which context, which backend, which fonts ----------------------------

void refresh_fonts(ImTypesetContext& ctx)
{
    for (int i = 0; i < FACE_COUNT; ++i) {
        const Face backing = ctx.library.backing_face(static_cast<Face>(i));
        ctx.fonts.faces[i].font = ctx.added[static_cast<int>(backing)];
        ctx.fonts.faces[i].data =
            ctx.fonts.faces[i].font != nullptr ? ctx.library.face_data(backing) : nullptr;
    }
    // Measurements memoised from a file that is no longer the role's are not
    // repaired by the registry; every paragraph rebuilds on the generation
    // change.
    forget_measurements(ctx);
}

// Whether the ImGui context this one was created in is the current one.
// Everything that touches the atlas, a draw list or ImGui's style needs it.
bool bound(const ImTypesetContext& ctx)
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetCurrentContext() == ctx.imgui;
}

bool still_the_roles(const ImFontAtlas* atlas, ImFont* font, ImGuiID id, ImU64 mark);

bool role_font_in_atlas(const ImTypesetContext& ctx, int role)
{
    if (role < 0 || role >= FACE_COUNT)
        return false;
    const int backing = static_cast<int>(ctx.library.backing_face(static_cast<Face>(role)));
    ImFont* const font = ctx.fonts.faces[role].font;
    return font != nullptr && font == ctx.added[backing] &&
           still_the_roles(ImGui::GetIO().Fonts, font, ctx.added_id[backing],
                           ctx.added_mark[backing]);
}

bool not_bound()
{
    return fail(ImTypesetError_NoContext,
                ImGui::GetCurrentContext() == nullptr
                    ? "there is no current ImGui context"
                    : "the ImGui context this ImTypeset context was created in is not the "
                      "current one");
}

bool backend_has_textures()
{
    return (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
}

bool no_textures()
{
    return fail(ImTypesetError_BackendUnsupported,
                "the renderer backend does not announce ImGuiBackendFlags_RendererHasTextures: "
                "glyphs are baked into the font atlas as they are first drawn, which needs a "
                "backend that updates textures (Dear ImGui 1.92 or later)");
}

// --- Which fonts in the atlas are this library's ----------------------------------------
//
// A font added here is marked: the last FONT_MARK_BYTES bytes of its
// ImFontConfig::Name, past the terminator of the name ImGui shows, hold a
// number no other font added in this process is given, and a last byte that
// is never zero. The atlas copies the configuration whole and keeps it for as
// long as the font. A host's font has its name's terminator or its padding in
// that last byte, so it cannot carry a mark, wherever it stands and whatever
// its FontId.
constexpr size_t FONT_MARK_BYTES = 8;
constexpr size_t FONT_MARK_AT = sizeof(ImFontConfig::Name) - FONT_MARK_BYTES;
constexpr unsigned char FONT_MARK_LAST = 0xFF;
static_assert(sizeof(ImFontConfig::Name) >= 24, "ImFontConfig::Name has no room for the mark");

// How many fonts this process has marked: the last one's number.
ImU64 g_fonts_marked = 0;

void write_font_mark(ImFontConfig& cfg, ImU64 mark)
{
    unsigned char bytes[FONT_MARK_BYTES];
    for (size_t i = 0; i + 1 < FONT_MARK_BYTES; ++i)
        bytes[i] = static_cast<unsigned char>((mark >> (8 * i)) & 0xFF);
    bytes[FONT_MARK_BYTES - 1] = FONT_MARK_LAST;
    std::memcpy(cfg.Name + FONT_MARK_AT, bytes, FONT_MARK_BYTES);
}

// The mark a font of the atlas carries; 0, which is nobody's, if it has none.
ImU64 read_font_mark(const ImFont& font)
{
    if (font.Sources.Size < 1 || font.Sources[0] == nullptr)
        return 0;
    unsigned char bytes[FONT_MARK_BYTES];
    std::memcpy(bytes, font.Sources[0]->Name + FONT_MARK_AT, FONT_MARK_BYTES);
    if (bytes[FONT_MARK_BYTES - 1] != FONT_MARK_LAST)
        return 0;
    ImU64 mark = 0;
    for (size_t i = 0; i + 1 < FONT_MARK_BYTES; ++i)
        mark |= static_cast<ImU64>(bytes[i]) << (8 * i);
    return mark;
}

// Whether the font a role was given is in the atlas still. `font` is looked
// for by address first and read only when the atlas holds it.
bool still_the_roles(const ImFontAtlas* atlas, ImFont* font, ImGuiID id, ImU64 mark)
{
    if (atlas == nullptr || font == nullptr || mark == 0 || !atlas->Fonts.contains(font))
        return false;
    return font->FontId == id && read_font_mark(*font) == mark;
}

// Drops every role whose ImFont is no longer in the atlas, so that nothing
// below dereferences it, hands it out or removes it. The role is then as if
// nothing had been added for it. Call with the context bound.
//
// That covers a host that removed the font or cleared the atlas, and equally
// an ImGui context destroyed under this one with another made at its address
// (bound() compares addresses, which is all imgui.h gives to know a context
// by): the atlas is then another atlas, none of its fonts carries a mark of
// this context's, and every role is found empty.
//
// Looked at once a frame, and again whenever the atlas's fonts are not the
// ones it had: a paragraph is drawn thousands of times a frame. What tells is
// how many fonts the atlas holds and the FontId of the last of them. ImGui
// gives each font it adds the next number of a counter that nothing sets back
// (ImFontAtlas::FontNextUniqueID: ClearFonts() leaves it) and adds it at the
// end, so no font leaves one atlas, alone or with another put in its place,
// without one of the two changing: a host that empties the atlas between two
// frames and fills it with as many fonts is seen. Another atlas at the same
// address can agree in all of that, frame included, so the look is skipped
// only while one of the roles' fonts, the first there is, still stands in the
// atlas with its mark.
void validate_fonts(ImTypesetContext& ctx)
{
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const int frame = ImGui::GetFrameCount();
    const int count = atlas != nullptr ? atlas->Fonts.Size : -1;
    const ImGuiID newest = count > 0 ? atlas->Fonts.back()->FontId : 0;
    if (frame == ctx.fonts_checked_frame && count == ctx.fonts_checked_count &&
        newest == ctx.fonts_checked_newest && atlas == ctx.fonts_checked_atlas) {
        int witness = 0;
        while (witness < FACE_COUNT && ctx.added[witness] == nullptr)
            ++witness;
        if (witness == FACE_COUNT ||
            still_the_roles(atlas, ctx.added[witness], ctx.added_id[witness],
                            ctx.added_mark[witness]))
            return;
    }
    ctx.fonts_checked_frame = frame;
    ctx.fonts_checked_count = count;
    ctx.fonts_checked_newest = newest;
    ctx.fonts_checked_atlas = atlas;
    bool lost = false;
    for (int i = 0; i < FACE_COUNT; ++i) {
        if (ctx.added[i] == nullptr)
            continue;
        if (still_the_roles(atlas, ctx.added[i], ctx.added_id[i], ctx.added_mark[i]))
            continue;
        ctx.added[i] = nullptr;
        ctx.added_id[i] = 0;
        ctx.added_mark[i] = 0;
        ctx.library.unregister_font(static_cast<Face>(i));
        lost = true;
    }
    if (lost) {
        ctx.math_fonts.clear();
        refresh_fonts(ctx);
    }
}

// Why a paragraph cannot be drawn now, as a failure; true if it can.
bool can_draw(const ImTypesetParagraph* paragraph)
{
    if (paragraph == nullptr || paragraph->ctx == nullptr)
        return fail(ImTypesetError_InvalidArgument, "no paragraph was given");
    ImTypesetContext& ctx = *paragraph->ctx;
    if (!bound(ctx))
        return not_bound();
    if (!backend_has_textures())
        return no_textures();
    validate_fonts(ctx);
    if (!paragraph->has_layout)
        return fail(ImTypesetError_InvalidArgument,
                    "the paragraph has not been laid out since its text was set");
    if (paragraph->material_generation != ctx.library.generation())
        return fail(ImTypesetError_InvalidArgument,
                    "the fonts have changed since the paragraph was laid out: call "
                    "LayoutParagraph again");
    if (paragraph->on_device && !device_current(ctx, paragraph->material_style))
        return fail(ImTypesetError_InvalidArgument,
                    "the display's density or the font atlas's rasterisation has changed since "
                    "the paragraph was laid out on the device's advances: call LayoutParagraph "
                    "again");
    if (paragraph->fallback)
        return report(paragraph->material_error);
    return true;
}

// --- The text widgets' cache -----------------------------------------------------------

constexpr int FORGET_AFTER_FRAMES = 600;
// Paragraphs dropped from the cache are kept to be used again, buffers and
// all, so that text which changes every frame does not allocate every frame:
// so many of them, and none that holds more than this.
constexpr size_t POOL_LIMIT = 16;
constexpr size_t POOL_FOOTPRINT_MAX = size_t(256) << 10;

// Eight bytes at a time; every byte and the length reach every bit of the
// result. Only ever a key into the cache: a paragraph found by it has its text
// compared before it is used.
uint64_t hash_bytes(const char* data, size_t size, uint64_t seed)
{
    uint64_t h = seed ^ (static_cast<uint64_t>(size) * 0x9E3779B97F4A7C15ull);
    while (size >= 8) {
        uint64_t word;
        std::memcpy(&word, data, 8);
        h = (h ^ word) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 29;
        data += 8;
        size -= 8;
    }
    uint64_t tail = 0;
    if (size != 0)
        std::memcpy(&tail, data, size);
    h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 32;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 29;
    return h;
}

uint64_t mix(uint64_t key, uint64_t with)
{
    uint64_t h = (key ^ (with * 0x9E3779B97F4A7C15ull)) * 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 31;
    return h;
}

// What a widget's paragraph holds, near enough to hold a limit to.
size_t footprint_of(const ImTypesetParagraph& p)
{
    return sizeof(ImTypesetParagraph) + p.text.capacity() +
           p.block.run.nodes.capacity() * sizeof(Node) + p.cmds.capacity() * sizeof(DrawCmd) +
           (p.index_bytes.capacity() + p.source_offsets.capacity()) * sizeof(uint32_t) +
           p.line_boxes.capacity() * sizeof(LineBox) +
           p.layout.shipped.capacity() * sizeof(uint32_t) +
           (p.layout.lines.capacity() + p.layout.vlist.capacity()) * sizeof(Node);
}

void unlink(ImTypesetContext& ctx, ImTypesetParagraph& p)
{
    (p.newer != nullptr ? p.newer->older : ctx.newest) = p.older;
    (p.older != nullptr ? p.older->newer : ctx.oldest) = p.newer;
    p.newer = nullptr;
    p.older = nullptr;
}

void link_newest(ImTypesetContext& ctx, ImTypesetParagraph& p)
{
    p.newer = nullptr;
    p.older = ctx.newest;
    (ctx.newest != nullptr ? ctx.newest->newer : ctx.oldest) = &p;
    ctx.newest = &p;
}

void recount(ImTypesetContext& ctx, ImTypesetParagraph& p)
{
    const size_t now = footprint_of(p);
    ctx.stats.CachedBytes += now;
    ctx.stats.CachedBytes -= p.footprint;
    p.footprint = now;
}

void drop_oldest(ImTypesetContext& ctx)
{
    ImTypesetParagraph* p = ctx.oldest;
    unlink(ctx, *p);
    ctx.widgets.erase(p->key);
    ctx.stats.CachedBytes -= p->footprint;
    --ctx.stats.CachedParagraphs;
    if (ctx.focused_key == p->key)
        ctx.focused_key = 0;
    // The pool's room was made when the context was (CreateContext), so that
    // putting one back cannot fail with the paragraph in nobody's hands.
    if (ctx.pool.size() < POOL_LIMIT && ctx.pool.size() < ctx.pool.capacity() &&
        p->footprint <= POOL_FOOTPRINT_MAX) {
        p->footprint = 0;
        ctx.pool.push_back(p);
    } else {
        delete p;
    }
}

// Forgets what has not been submitted for some seconds, and what is over the
// limits. Never what this frame or the last one submitted: a frame that shows
// more than the limit keeps it all, and lays nothing out twice.
//
// The list is in order of arrival, not of use: finding a paragraph again does
// not move it, because moving it writes to its two neighbours, and a frame of
// thousands of paragraphs would touch thousands of cold ones to keep an order
// nobody reads while nothing is over a limit. Instead the oldest arrival is
// looked at here: one still in use is sent round to the front (the second
// chance of a clock), one that is not is dropped if it is stale or the cache
// is over a limit. Within the limits that is a few arrivals a frame, at the
// frame's first call, so that a steady frame's other calls touch nothing but
// their own paragraph and the whole list is still gone over in time. Over a
// limit the whole list is gone over at once, once a frame, if there can be
// anything in it to drop: either that brings the cache within the limit, or
// everything left is in use and going over it again in the same frame would
// find the same.
void evict(ImTypesetContext& ctx, int frame)
{
    constexpr ImU64 LOOKED_AT_A_FRAME = 16;
    const auto over_a_limit = [&ctx] {
        return ctx.stats.CachedParagraphs > ctx.cache_max_paragraphs ||
               ctx.stats.CachedBytes > ctx.cache_max_bytes;
    };
    ImU64 budget = 0;
    // Going over the whole list finds something to drop only if the cache
    // holds more than this frame and the last submitted between them. A frame
    // that shows more than the limit, steadily, does not: it is told so by
    // two counts and the list is left alone.
    if (over_a_limit() && ctx.evicted_all_frame != frame &&
        ctx.stats.CachedParagraphs > ctx.submitted_now + ctx.submitted_before) {
        ctx.evicted_all_frame = frame;
        ctx.evicted_some_frame = frame;
        budget = ctx.stats.CachedParagraphs;
    } else if (ctx.evicted_some_frame != frame) {
        ctx.evicted_some_frame = frame;
        budget = LOOKED_AT_A_FRAME;
    }
    for (ImU64 n = 0; n < budget && ctx.oldest != nullptr; ++n) {
        ImTypesetParagraph& p = *ctx.oldest;
        const int age = frame - p.last_frame;
        if (age >= 0 && age <= 1) {
            if (ctx.newest == &p)
                break;
            unlink(ctx, p);
            link_newest(ctx, p);
            continue;
        }
        const bool stale = age < 0 || age > FORGET_AFTER_FRAMES;
        const bool over = over_a_limit();
        if (!stale && !over)
            break;
        if (over && !stale)
            ++ctx.stats.CacheEvictions;
        drop_oldest(ctx);
    }
}

ImTypesetParagraph* widget_slot(ImTypesetContext& ctx, uint64_t key)
{
    const auto found = ctx.widgets.find(key);
    if (found != ctx.widgets.end())
        return found->second;
    // One from the pool stays the pool's, and a new one this function's,
    // until the cache has taken it: making its place there can fail.
    std::unique_ptr<ImTypesetParagraph> made;
    ImTypesetParagraph* p = nullptr;
    if (!ctx.pool.empty()) {
        p = ctx.pool.back();
    } else {
        made.reset(new ImTypesetParagraph());
        p = made.get();
        p->ctx = &ctx;
    }
    p->key = key;
    p->last_frame = -1;
    p->uses_this_frame = 0;
    p->selection = ImTypesetTextRange{0, 0};
    ctx.widgets.emplace(key, p);
    if (made)
        made.release();
    else
        ctx.pool.pop_back();
    link_newest(ctx, *p);
    ++ctx.stats.CachedParagraphs;
    return p;
}

// The paragraph for a key this frame. The same key submitted again in one
// frame (one string in two table columns, with no PushID between) gets a slot
// of its own for each use, so two widths do not fight over one layout.
ImTypesetParagraph* widget_for(ImTypesetContext& ctx, uint64_t key, int frame)
{
    if (ctx.submitted_frame != frame) {
        ctx.submitted_before = ctx.submitted_frame == frame - 1 ? ctx.submitted_now : 0;
        ctx.submitted_now = 0;
        ctx.submitted_frame = frame;
    }
    ImTypesetParagraph* first = widget_slot(ctx, key);
    if (first->last_frame != frame) {
        first->last_frame = frame;
        first->uses_this_frame = 1;
        ++ctx.submitted_now;
        return first;
    }
    const uint64_t use = static_cast<uint64_t>(first->uses_this_frame++);
    const uint64_t mixed = mix(key, use);
    ImTypesetParagraph* again = widget_slot(ctx, mixed != key ? mixed : mixed + 1);
    if (again->last_frame != frame)
        ++ctx.submitted_now;
    again->last_frame = frame;
    return again;
}

// The text as ImGui sets it, wrapped at the width asked for: what a host sees
// while typesetting is not possible.
void imgui_text(const char* text, size_t size, float wrap_width)
{
    // Popped however ImGui's own text is left.
    struct WrapPos {
        explicit WrapPos(float at) { ImGui::PushTextWrapPos(at); }
        ~WrapPos() { IMTYPESET_NO_UNWIND(ImGui::PopTextWrapPos()); }
    } wrapped(wrap_width > 0.0f ? ImGui::GetCursorPosX() + wrap_width : 0.0f);
    ImGui::TextUnformatted(text, text + size);
}

struct WidgetArgs {
    const char* str_id = nullptr; // Null: found by content.
    const char* text = nullptr;
    const char* text_end = nullptr;
    ImTypesetTextFlags flags = 0;
    float wrap_width = 0.0f;
    const ImTypesetStyle* style = nullptr;
    bool selectable = false;
    ImTypesetTextRange* selection = nullptr;
};

// A SelectableText's mouse and keyboard, after its item was submitted.
void select_with_mouse(ImTypesetContext& ctx, ImTypesetParagraph& p, const ImVec2& pos)
{
    const ImVec2 mouse = ImGui::GetMousePos();
    const ImVec2 local(mouse.x - pos.x, mouse.y - pos.y);
    if (ImGui::IsItemActivated()) {
        const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
        p.granularity = clicks >= 3   ? ImTypesetGranularity_Line
                        : clicks == 2 ? ImTypesetGranularity_Word
                                      : ImTypesetGranularity_Character;
        p.anchor = local;
        ctx.focused_key = p.key;
    }
    if (ImGui::IsItemActive()) {
        p.selection = ImTypeset::SelectParagraphRange(&p, p.anchor, local, p.granularity);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered()) {
        p.selection = ImTypesetTextRange{0, 0};
        if (ctx.focused_key == p.key)
            ctx.focused_key = 0;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (ctx.focused_key != p.key || !ImGui::IsWindowFocused())
        return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
        p.selection = ImTypesetTextRange{0, static_cast<int>(p.text.size())};
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) &&
        p.selection.Start < p.selection.End) {
        ImGuiTextBuffer copy;
        ImTypeset::AppendParagraphRangeText(&p, p.selection, &copy);
        ImGui::SetClipboardText(copy.c_str());
    }
}

bool text_widget(const WidgetArgs& given)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr)
        return no_context();
    if (!bound(*ctx))
        return not_bound();
    // NewFrame invalidates the draw data and Render makes it valid again:
    // the one sign imgui.h gives of being inside a frame.
    if (ImGui::GetFrameCount() <= 0 || ImGui::GetDrawData() != nullptr)
        return fail(ImTypesetError_InvalidArgument,
                    "a text widget was called outside a frame: call it between "
                    "ImGui::NewFrame() and ImGui::Render()");
    const char* text = given.text != nullptr ? given.text : "";
    const char* text_end = given.text != nullptr ? given.text_end : nullptr;
    if (text_end != nullptr && text_end < text)
        text_end = text;
    const size_t size =
        text_end != nullptr ? static_cast<size_t>(text_end - text) : std::strlen(text);
    const float wrap = is_finite(given.wrap_width) && given.wrap_width > 0.0f
                           ? given.wrap_width
                           : ImGui::GetContentRegionAvail().x;

    if (!backend_has_textures()) {
        imgui_text(text, size, given.wrap_width > 0.0f ? wrap : 0.0f);
        return no_textures();
    }
    validate_fonts(*ctx);

    const ImTypesetStyle& asked = given.style != nullptr ? *given.style : ctx->style;
    const ResolvedStyle style = resolve_style(*ctx, asked);
    const Scaled width = resolve_width(wrap, style);
    const int frame = ImGui::GetFrameCount();
    const ImTypesetTextFlags flags =
        given.flags & (ImTypesetTextFlags_HardBreaks | ImTypesetTextFlags_Math);

    // By the ID stack and a string ID, or by the ID stack and the content.
    uint64_t key = 0;
    if (given.str_id != nullptr) {
        key = mix(static_cast<uint64_t>(ImGui::GetID(given.str_id)), 0x7374725F6964ull);
    } else {
        const uint64_t content = hash_bytes(text, size, static_cast<uint64_t>(flags));
        const ImGuiID scoped = ImGui::GetID(static_cast<int>(static_cast<uint32_t>(content)));
        key = (static_cast<uint64_t>(scoped) << 32) | (content >> 32);
        ctx->stats.BytesHashed += size;
    }
    ImTypesetParagraph& p = *widget_for(*ctx, key, frame);
    // Found is not yet the same: the text is compared, always.
    const size_t kept = std::min(size, static_cast<size_t>(MAX_TEXT_BYTES));
    ctx->stats.BytesHashed += std::min(kept, p.text.size());
    if (p.given_size != size || p.flags != flags || p.text.size() > kept ||
        std::memcmp(text, p.text.data(), p.text.size()) != 0) {
        set_text(p, text, size, nullptr, 0, flags);
        p.selection = ImTypesetTextRange{0, 0};
    }

    // A selection the host keeps is the selection.
    if (given.selection != nullptr)
        p.selection = *given.selection;

    const ImVec2 pos = ImGui::GetCursorScreenPos();

    // Out of view and only the width has changed: the lines it has stand in
    // until it is seen. The item keeps its height, so nothing above or below
    // moves until then.
    const bool only_width_changed = p.has_layout && !p.fallback && p.has_material &&
                                    p.layout_width != width &&
                                    p.material_text_version == p.text_version &&
                                    p.material_generation == ctx->library.generation() &&
                                    same_layout_style(p.layout_style, style);
    if (only_width_changed && p.size.y > 0.0f) {
        const float full = ceil_px(static_cast<double>(wrap));
        const ImVec2 stand_in(style.align == Align::LEFT ? std::min(p.size.x, full) : full,
                              p.size.y);
        if (!ImGui::IsRectVisible(pos, ImVec2(pos.x + stand_in.x, pos.y + stand_in.y))) {
            ++ctx->stats.DeferredLayouts;
            ++ctx->stats.CulledCalls;
            ImGui::Dummy(stand_in);
            if (given.selection != nullptr)
                *given.selection = p.selection;
            evict(*ctx, frame);
            return report(p.error);
        }
    }

    const ImU64 layouts = ctx->stats.Layouts;
    layout_paragraph(*ctx, p, style, width);
    remember_colours(p, asked);
    // What it holds changes when it is laid out, and not otherwise.
    if (ctx->stats.Layouts != layouts)
        recount(*ctx, p);
    evict(*ctx, frame);

    if (p.fallback) {
        imgui_text(p.text.data(), p.text.size(), given.wrap_width > 0.0f ? wrap : 0.0f);
        if (given.selection != nullptr)
            *given.selection = ImTypesetTextRange{0, 0};
        return report(p.error);
    }

    const ImVec2 far(pos.x + p.size.x, pos.y + p.size.y);
    const bool interactive = given.selectable && p.size.x > 0.0f && p.size.y > 0.0f;
    if (interactive) {
        ImGui::InvisibleButton(given.str_id, p.size);
        select_with_mouse(*ctx, p, pos);
    } else {
        ImGui::Dummy(p.size);
    }
    if (given.selection != nullptr)
        *given.selection = p.selection;
    if (!ImGui::IsRectVisible(pos, far)) {
        ++ctx->stats.CulledCalls;
        return report(p.error);
    }
    draw_to_list(*ctx, p, ImGui::GetWindowDrawList(), pos, interactive ? &p.selection : nullptr);
    return report(p.error);
}

// --- Fonts ---------------------------------------------------------------------------

// A whole file, by a UTF-8 name. ImGui's own file functions are internal to
// it; this does what they do for a name on Windows, where `fopen` reads it in
// the process's code page.
bool read_file(const char* filename, std::vector<uint8_t>& out)
{
    std::FILE* file = nullptr;
#if defined(_WIN32)
    const int wide_size = ::MultiByteToWideChar(CP_UTF8, 0, filename, -1, nullptr, 0);
    if (wide_size > 0) {
        std::vector<wchar_t> wide(static_cast<size_t>(wide_size));
        ::MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide.data(), wide_size);
        file = ::_wfopen(wide.data(), L"rb");
    }
#else
    file = std::fopen(filename, "rb");
#endif
    if (file == nullptr)
        return false;
    out.clear();
    uint8_t block[1 << 16];
    for (;;) {
        const size_t got = std::fread(block, 1, sizeof block, file);
        out.insert(out.end(), block, block + got);
        if (got < sizeof block)
            break;
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

// Whether the registry takes the index of a face in a collection.
template<typename Library, typename = void>
struct takes_face_index : std::false_type {
};
template<typename Library>
struct takes_face_index<
    Library, std::void_t<decltype(std::declval<Library&>().register_font(
                 Face::PROSE, static_cast<const void*>(nullptr), size_t(0),
                 static_cast<const char*>(nullptr), static_cast<std::string*>(nullptr), 0))>>
    : std::true_type {
};

// Hands a font to a registry, the face index with it. A registry that takes
// no index is given index 0 only; it is the registry that accepts or refuses
// a collection.
template<typename Library>
bool register_in(Library& library, Face role, const void* data, size_t size, const char* name,
                 int face_index, std::string* why)
{
    if constexpr (takes_face_index<Library>::value) {
        return library.register_font(role, data, size, name, why, face_index);
    } else {
        if (face_index != 0) {
            *why = "face index " + std::to_string(face_index) +
                   " was asked for, and the font registry reads single-face fonts only";
            return false;
        }
        return library.register_font(role, data, size, name, why);
    }
}

ImFont* add_font(ImTypesetFace face, const char* filename, const void* data, int data_size,
                 int face_index, const ImFontConfig* config)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr) {
        no_context();
        return nullptr;
    }
    if (!bound(*ctx)) {
        not_bound();
        return nullptr;
    }
    const auto refuse = [](ImTypesetError code, std::string why) -> ImFont* {
        fail(code, std::move(why));
        return nullptr;
    };
    const int role_index = role_of(face);
    if (role_index < 0)
        return refuse(ImTypesetError_InvalidArgument,
                      "not a face a font can be added for: " + std::to_string(face));
    if (filename == nullptr && (data == nullptr || data_size <= 0))
        return refuse(ImTypesetError_InvalidArgument, "no font data was given");
    if (filename != nullptr && filename[0] == '\0')
        return refuse(ImTypesetError_InvalidArgument, "no font file was named");
    if (config != nullptr && config->MergeMode)
        return refuse(ImTypesetError_InvalidArgument,
                      "ImFontConfig::MergeMode is refused: a font added here has its codepoints "
                      "laid out for typesetting and cannot be merged into another");
    const ImGuiIO& io = ImGui::GetIO();
    // A renderer that has introduced itself and does not update textures. One
    // that has not introduced itself yet is found out at the first frame.
    if (io.BackendRendererName != nullptr && !backend_has_textures()) {
        no_textures();
        return nullptr;
    }
    ImFontAtlas* atlas = io.Fonts;
    if (atlas == nullptr || atlas->Locked)
        return refuse(ImTypesetError_InvalidArgument,
                      "the ImGui font atlas is locked: add fonts outside a frame, or use a "
                      "renderer backend with ImGuiBackendFlags_RendererHasTextures");
    validate_fonts(*ctx);

    std::vector<uint8_t> file;
    const char* name = face_value(static_cast<Face>(role_index));
    if (filename != nullptr) {
        if (!read_file(filename, file))
            return refuse(ImTypesetError_FileNotFound,
                          std::string("cannot open the font file ") + filename);
        if (file.empty())
            return refuse(ImTypesetError_FontRefused,
                          std::string("the font file is empty: ") + filename);
        data = file.data();
        // The file's own name: the atlas has room for 31 bytes of it beside
        // the mark, and a path's first 31 are its directories.
        name = filename;
        for (const char* at = filename; *at != '\0'; ++at) {
            if (*at == '/' || *at == '\\')
                name = at + 1;
        }
    }
    const size_t size = filename != nullptr ? file.size() : static_cast<size_t>(data_size);

    const Face role = static_cast<Face>(role_index);
    // Everything that can refuse the font is tried on a registry of its own
    // first, so that a refusal leaves the context's as it was.
    std::string why;
    ImFont* font = nullptr;
    ImU64 mark = 0;
    {
        FontLibrary probe;
        if (!register_in(probe, role, data, size, name, face_index, &why))
            return refuse(ImTypesetError_FontRefused, why);
        if (role == Face::MATH && probe.metrics(Face::MATH, 1000.0, &why) == nullptr)
            return refuse(ImTypesetError_FontRefused, why);
        const FaceData* probed = probe.face_data(role, &why);
        if (probed == nullptr)
            return refuse(ImTypesetError_FontRefused, why);
        ImFontConfig cfg = config != nullptr ? *config : ImFontConfig();
        // The file handed to the atlas is rebuilt with one face in it.
        cfg.FontNo = 0;
        cfg.MergeMode = false;
        // The name ImGui shows ends before the mark, which takes the rest.
        if (cfg.Name[0] == '\0')
            std::snprintf(cfg.Name, FONT_MARK_AT, "%s", name);
        std::memset(cfg.Name + FONT_MARK_AT - 1, 0, FONT_MARK_BYTES + 1);
        mark = ++g_fonts_marked;
        write_font_mark(cfg, mark);
        const float size_pixels =
            is_finite(cfg.SizePixels) && cfg.SizePixels > 0.0f ? cfg.SizePixels : 0.0f;
        cfg.SizePixels = size_pixels;
        font = add_face_to_atlas(atlas, *probed, size_pixels, &cfg, &why);
        if (font == nullptr)
            return refuse(ImTypesetError_FontRefused, why);
        // A Dear ImGui that does not keep the configuration's bytes as they
        // were handed over would have every role found empty at the next
        // look, with nothing to say why.
        if (!still_the_roles(atlas, font, font->FontId, mark)) {
            if (atlas->Fonts.contains(font))
                IMTYPESET_NO_UNWIND(atlas->RemoveFont(font));
            return refuse(ImTypesetError_BackendUnsupported,
                          "the Dear ImGui in this process did not keep ImFontConfig::Name as it "
                          "was given, which is where a font added here is told from the host's");
        }
    }
    {
        // In the atlas and not yet the role's: taken out again if the
        // registry does not take the face, by refusing it or by not returning.
        struct InAtlas {
            ImFontAtlas* atlas;
            ImFont* font;
            ~InAtlas()
            {
                if (font != nullptr && atlas->Fonts.contains(font))
                    IMTYPESET_NO_UNWIND(atlas->RemoveFont(font));
            }
        } added{atlas, font};
        if (!register_in(ctx->library, role, data, size, name, face_index, &why)) {
            // The same bytes were just accepted.
            return refuse(ImTypesetError_FontRefused, why);
        }
        added.font = nullptr;
    }

    // The font this one replaces leaves the atlas with the role.
    ImFont* const old = ctx->added[role_index];
    ctx->added[role_index] = font;
    ctx->added_id[role_index] = font->FontId;
    ctx->added_mark[role_index] = mark;
    // Everything of the context's that names the role's font is brought up to
    // date before the old one is taken out of the atlas: if that last step is
    // unwound out of (an IM_ASSERT that throws), the old font is left in the
    // atlas unused and nothing here still points at it.
    ctx->math_fonts.clear();
    ctx->fonts_checked_frame = -1;
    refresh_fonts(*ctx);
    if (old != nullptr && old != font && atlas->Fonts.contains(old)) {
        if (ImGui::GetIO().FontDefault == old)
            ImGui::GetIO().FontDefault = font;
        atlas->RemoveFont(old);
    }
    succeed();
    return font;
}

void forget_material(ImTypesetContext& ctx)
{
    for (ImTypesetParagraph* p : ctx.paragraphs)
        p->has_material = false;
    for (const auto& entry : ctx.widgets)
        entry.second->has_material = false;
    if (ctx.measured != nullptr)
        ctx.measured->has_material = false;
}

ImTypesetParagraph* owned(ImTypesetParagraph* paragraph)
{
    return paragraph != nullptr && paragraph->ctx != nullptr ? paragraph : nullptr;
}

const ErrorState& no_paragraph()
{
    static ErrorState state = [] {
        ErrorState s;
        s.set(ImTypesetError_InvalidArgument, "no paragraph was given");
        return s;
    }();
    return state;
}

Point local_point(const ImTypesetParagraph& p, const ImVec2& pos)
{
    const double ppi = p.layout_style.px_per_inch;
    Point point;
    point.x = sp_from_px(is_finite(pos.x) ? static_cast<double>(pos.x) : 0.0, ppi);
    point.y = sp_from_px(is_finite(pos.y) ? static_cast<double>(pos.y) : 0.0, ppi);
    return point;
}

float px_of(const ImTypesetParagraph& p, Scaled length)
{
    return static_cast<float>(px_from_sp(length, p.layout_style.px_per_inch));
}

// Offsets counted as decode_utf8 decodes, which is how every codepoint
// position the library reports is counted.
int codepoint_to_byte(std::string_view text, int codepoint_offset)
{
    text = text.substr(0, std::min(text.size(), size_t(0x7FFFFFFF)));
    if (codepoint_offset <= 0)
        return 0;
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
    decode_utf8(text, codepoints, &offsets);
    if (static_cast<size_t>(codepoint_offset) >= offsets.size())
        return static_cast<int>(text.size());
    return static_cast<int>(offsets[static_cast<size_t>(codepoint_offset)]);
}

int byte_to_codepoint(std::string_view text, int byte_offset)
{
    text = text.substr(0, std::min(text.size(), size_t(0x7FFFFFFF)));
    if (byte_offset <= 0)
        return 0;
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
    decode_utf8(text, codepoints, &offsets);
    return static_cast<int>(
        std::lower_bound(offsets.begin(), offsets.end(), static_cast<uint32_t>(byte_offset)) -
        offsets.begin());
}

// The text a pair of pointers names.
size_t text_size(const char*& text, const char* text_end)
{
    if (text == nullptr) {
        text = "";
        return 0;
    }
    if (text_end == nullptr)
        return std::strlen(text);
    return text_end > text ? static_cast<size_t>(text_end - text) : 0;
}

} // namespace ImTypeset::detail::core

using namespace ImTypeset::detail::core;

// --- src/context.h -------------------------------------------------------------------

namespace ImTypeset::detail
{

void ErrorState::clear()
{
    code = ImTypesetError_None;
    byte_pos = -1;
    codepoint_pos = -1;
    message.clear();
}

void ErrorState::set(ImTypesetError code_, std::string text, int byte_pos_, int codepoint_pos_)
{
    code = code_;
    byte_pos = byte_pos_;
    codepoint_pos = codepoint_pos_;
    // Read through a `const char*`: a NUL in what a message quotes is spelled.
    message = text.find('\0') == std::string::npos ? std::move(text) : c_string_reason(text);
}

const ImTypesetErrorInfo* ErrorState::get() const
{
    _info.Code = code;
    _info.BytePos = byte_pos;
    _info.CodepointPos = codepoint_pos;
    _info.Message = message.c_str();
    return &_info;
}

ResolvedStyle resolve_style(ImTypesetContext& ctx, const ImTypesetStyle& style)
{
    ResolvedStyle out;
    // Zero, like anything that is not a positive number, is the default.
    out.px_per_inch = style.PixelsPerInch > 0.0f
                          ? clamped(static_cast<double>(style.PixelsPerInch), MIN_PX_PER_INCH,
                                    MAX_PX_PER_INCH, DEFAULT_PX_PER_INCH)
                          : DEFAULT_PX_PER_INCH;
    const int role = role_of(style.Face);
    out.face = role >= 0 ? static_cast<Face>(role) : Face::PROSE;
    out.align = align_of(style.Align);
    out.spacing = spacing_of(style.Spacing);
    out.line_break = style.LineBreak == ImTypesetLineBreak_FirstFit ||
                             style.LineBreak == ImTypesetLineBreak_TotalFit
                         ? style.LineBreak
                         : ImTypesetLineBreak_Auto;

    double em_px = 0.0;
    if (is_finite(style.EmSize) && style.EmSize > 0.0f) {
        em_px = static_cast<double>(style.EmSize);
    } else {
        double height = static_cast<double>(style.FontSize);
        if (!(is_finite(height) && height > 0.0))
            height = ImGui::GetCurrentContext() != nullptr
                         ? static_cast<double>(ImGui::GetFontSize())
                         : 0.0;
        if (!(is_finite(height) && height > 0.0))
            height = 13.0;
        // One em for every face of a paragraph, taken from the Regular face
        // when there is one, so that a bold or a code run is the same size of
        // type and not the same height of line.
        const Face basis = ctx.library.is_registered(Face::PROSE) ? Face::PROSE : out.face;
        em_px = ctx.library.em_size_for_pixel_height(basis, height);
    }
    em_px = clamped(em_px, MIN_EM_PX, MAX_EM_PX, 16.0);
    out.em = clamped_sp(sp_from_px(em_px, out.px_per_inch), 1, MAX_EM);

    const auto length = [&](float pixels) {
        const double px = clamped(static_cast<double>(pixels), 0.0, 1.0e9, 0.0);
        return clamped_sp(sp_from_px(px, out.px_per_inch), 0, MAX_DIMEN);
    };
    out.left_indent = length(style.LeftIndent);
    out.first_line_indent = length(style.FirstLineIndent);

    // The reference's `_leading`: round(size * ratio), and a tenth of the size
    // as the floor under two lines that would otherwise touch.
    const double ratio = line_height_of(ctx, style.LineHeight);
    out.baselineskip =
        clamped_sp(round_to_scaled(static_cast<double>(out.em) * ratio), 1, MAX_DIMEN);
    out.lineskip = clamped_sp(round_to_scaled(static_cast<double>(out.em) * 0.1), 0, MAX_DIMEN);
    resolve_device(ctx, out,
                   style.Advances == ImTypesetAdvances_Font ||
                           style.Advances == ImTypesetAdvances_Device
                       ? style.Advances
                       : ImTypesetAdvances_Default);
    out.align = grid_align(ctx, out.align, out.font(out.face, out.em));
    return out;
}

bool device_askable(ImTypesetContext& ctx)
{
    return bound(ctx) && backend_has_textures();
}

ResolvedStyle keeping_device(ImTypesetContext& ctx, ResolvedStyle style, const ResolvedStyle& held)
{
    if (held.device == 0 || style.advances != held.advances || device_askable(ctx))
        return style;
    // The epoch is the table's now: a table emptied since is another table.
    if (held.device_epoch != ctx.devices.epoch || style.px_per_inch != held.px_per_inch)
        return style;
    style.device = held.device;
    style.device_faces = held.device_faces;
    style.device_epoch = held.device_epoch;
    style.align = grid_align(ctx, style.align, style.font(style.face, style.em));
    return style;
}

Align grid_align(ImTypesetContext& ctx, Align align, const FontSpec& font)
{
    if (align != Align::JUSTIFY || font.device == 0)
        return align;
    const FontMetrics* const metrics = ctx.resolve(font.key());
    return metrics != nullptr && metrics->cell() > 0.0 ? Align::LEFT : align;
}

void resolve_device(ImTypesetContext& ctx, ResolvedStyle& style, int advances)
{
    style.advances = advances;
    style.device = 0;
    style.device_faces = 0;
    style.device_epoch = 0;
    if (advances == ImTypesetAdvances_Font ||
        (advances == ImTypesetAdvances_Default && !ctx.tex_rules.code_on_device))
        return;
    const uint16_t device = device_number(ctx, style.px_per_inch);
    if (device == 0)
        return;
    // A role whose font the host has taken out of the atlas is found empty
    // here, before any of its glyphs is asked for.
    validate_fonts(ctx);
    style.device_faces = device_faces(ctx, advances, device);
    if (style.device_faces == 0)
        return;
    style.device = device;
    style.device_epoch = ctx.devices.epoch;
}

bool device_current(ImTypesetContext& ctx, const ResolvedStyle& style)
{
    ResolvedStyle now = style;
    resolve_device(ctx, now, style.advances);
    return now.same_device(style);
}

Scaled resolve_width(float width, const ResolvedStyle& style)
{
    const double px = clamped(static_cast<double>(width), 0.0, 1.0e9, 0.0);
    return clamped_sp(sp_from_px(px, style.px_per_inch), 0, MAX_DIMEN);
}

void set_inline_math(ImTypesetContext& ctx, const InlineMath& math)
{
    ctx.math = math;
    forget_material(ctx);
}

void scan_math(const uint32_t* cps, const uint32_t* offsets, size_t begin, size_t end,
               std::vector<FoundFormula>& found, std::vector<uint32_t>& dropped)
{
    size_t at = begin;
    while (at < end) {
        if (cps[at] == '\\') {
            // markdown-it's escape rule: the backslash and what follows it are
            // one piece of text, whatever follows.
            if (at + 1 < end && cps[at + 1] == '$')
                dropped.push_back(static_cast<uint32_t>(at));
            at += 2;
            continue;
        }
        if (cps[at] != '$') {
            ++at;
            continue;
        }
        const size_t marker = at + 1 < end && cps[at + 1] == '$' ? 2 : 1;
        const auto marker_at = [&](size_t where) {
            return where + marker <= end && cps[where] == '$' &&
                   (marker == 1 || cps[where + 1] == '$');
        };
        const size_t start = at + marker;
        size_t close = end; // `end`: this opener opens nothing.
        const bool hard_against_a_word = at > begin && py_isalnum(cps[at - 1]);
        if (start < end && !py_isspace(cps[start]) && !hard_against_a_word) {
            size_t scan = start + 1;
            for (;;) {
                while (scan < end && !marker_at(scan))
                    ++scan;
                if (scan >= end)
                    break;
                // Stops at the opener at the latest, which is a dollar.
                size_t backslashes = 0;
                while (cps[scan - 1 - backslashes] == '\\')
                    ++backslashes;
                if (backslashes % 2 != 0) {
                    ++scan;
                    continue;
                }
                const size_t after = scan + marker;
                const bool opens_something =
                    after < end &&
                    (py_isalnum(cps[after]) || cps[after] == '{' || cps[after] == '(');
                if (!py_isspace(cps[scan - 1]) && !opens_something)
                    close = scan;
                break;
            }
        }
        if (close == end) {
            // Text, and so is the marker: nothing else is offered it.
            at = start;
            continue;
        }
        found.push_back(
            FoundFormula{offsets[at], offsets[close + marker], static_cast<uint32_t>(marker)});
        at = close + marker;
    }
}

const std::vector<DrawCmd>& shipped_commands(const ImTypesetParagraph& paragraph)
{
    return paragraph.cmds;
}

Selection selection_of(const ImTypesetParagraph& paragraph, const ImTypesetTextRange& range)
{
    Selection out;
    if (range.Start >= range.End)
        return out;
    out.start = index_at_byte(paragraph, range.Start);
    out.end = index_at_byte(paragraph, range.End);
    if (out.end < out.start)
        out.end = out.start;
    return out;
}

ImTypesetTextRange text_range_of(const ImTypesetParagraph& paragraph, Selection selection)
{
    ImTypesetTextRange out = {0, 0};
    if (selection.is_empty())
        return out;
    out.Start = byte_at_index(paragraph, selection.start);
    out.End = byte_at_index(paragraph, selection.end);
    return out;
}

} // namespace ImTypeset::detail

ImTypesetContext::ImTypesetContext()
    : resolve(context_metrics(*this))
    , cache(resolve)
{
}

ImTypesetContext::~ImTypesetContext()
{
    for (ImTypesetParagraph* p : paragraphs)
        delete p;
    destroy_documents(*this);
    for (const auto& entry : widgets)
        delete entry.second;
    for (ImTypesetParagraph* p : pool)
        delete p;
}

CharMeasure ImTypesetContext::measure()
{
    return cache.measure();
}

// --- include/imtypeset.h -------------------------------------------------------------

namespace ImTypeset
{

bool DebugCheckVersionAndDataLayout(const char* version, size_t sz_imwchar, size_t sz_imdrawvert,
                                    size_t sz_imdrawidx, size_t sz_style, size_t sz_run,
                                    size_t sz_range, size_t sz_line, size_t sz_hit,
                                    size_t sz_formula, size_t sz_stats, size_t sz_error)
{
    std::string& mismatch = layout_mismatch();
    mismatch.clear();
    if (version == nullptr || std::strcmp(version, IMTYPESET_VERSION) != 0) {
        mismatch = std::string("imtypeset.h is version ") + (version != nullptr ? version : "?") +
                   " and the library is " IMTYPESET_VERSION;
    } else if (sz_imwchar != sizeof(ImWchar)) {
        mismatch = "ImWchar is " + std::to_string(sz_imwchar * 8) +
                   " bits where IMTYPESET_CHECKVERSION() is called and " +
                   std::to_string(sizeof(ImWchar) * 8) +
                   " bits in the imtypeset library: both must be compiled with the same "
                   "IMGUI_USE_WCHAR32 setting";
    } else if (sz_imdrawvert != sizeof(ImDrawVert) || sz_imdrawidx != sizeof(ImDrawIdx)) {
        mismatch = "ImDrawVert or ImDrawIdx has a different size where IMTYPESET_CHECKVERSION() "
                   "is called than in the imtypeset library: the two were compiled against "
                   "different imconfig.h";
    } else {
        const struct {
            const char* name;
            size_t given;
            size_t own;
        } structs[] = {
            {"ImTypesetStyle", sz_style, sizeof(ImTypesetStyle)},
            {"ImTypesetRun", sz_run, sizeof(ImTypesetRun)},
            {"ImTypesetTextRange", sz_range, sizeof(ImTypesetTextRange)},
            {"ImTypesetLine", sz_line, sizeof(ImTypesetLine)},
            {"ImTypesetHit", sz_hit, sizeof(ImTypesetHit)},
            {"ImTypesetFormula", sz_formula, sizeof(ImTypesetFormula)},
            {"ImTypesetStats", sz_stats, sizeof(ImTypesetStats)},
            {"ImTypesetErrorInfo", sz_error, sizeof(ImTypesetErrorInfo)},
        };
        for (const auto& entry : structs) {
            if (entry.given == entry.own)
                continue;
            mismatch = std::string(entry.name) + " is " + std::to_string(entry.given) +
                       " bytes where IMTYPESET_CHECKVERSION() is called and " +
                       std::to_string(entry.own) +
                       " in the imtypeset library: the two were compiled against different "
                       "imtypeset.h";
            break;
        }
    }
    if (!mismatch.empty())
        return fail(ImTypesetError_VersionMismatch, mismatch);
    return succeed();
}

ImTypesetContext* CreateContext()
{
    if (!layout_mismatch().empty()) {
        fail(ImTypesetError_VersionMismatch, layout_mismatch());
        return nullptr;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        fail(ImTypesetError_NoContext, "ImTypeset::CreateContext needs a current ImGui context");
        return nullptr;
    }
    if (std::strcmp(ImGui::GetVersion(), IMGUI_VERSION) != 0) {
        fail(ImTypesetError_VersionMismatch,
             std::string("the imtypeset library was compiled against Dear ImGui ") + IMGUI_VERSION +
                 " and is linked with " + ImGui::GetVersion());
        return nullptr;
    }
    std::unique_ptr<ImTypesetContext> made(new ImTypesetContext());
    made->pool.reserve(POOL_LIMIT);
    ImTypesetContext* ctx = made.release();
    ctx->imgui = ImGui::GetCurrentContext();
    ctx->serial = ++g_contexts_made;
    if (g_context == nullptr)
        g_context = ctx;
    succeed();
    return ctx;
}

void DestroyContext(ImTypesetContext* ctx)
{
    if (ctx == nullptr)
        ctx = g_context;
    if (ctx == nullptr)
        return;
    if (g_context == ctx)
        g_context = nullptr;
    delete ctx;
}

ImTypesetContext* GetCurrentContext()
{
    return g_context;
}

ImU64 GetContextSerial()
{
    return g_context != nullptr ? g_context->serial : 0;
}

void SetCurrentContext(ImTypesetContext* ctx)
{
    g_context = ctx;
}

const char* GetVersion()
{
    return IMTYPESET_VERSION;
}

int GetImGuiVersionNum()
{
    return IMGUI_VERSION_NUM;
}

const ImTypesetErrorInfo* GetLastError()
{
    return last_error().get();
}

ImTypesetStats GetStats()
{
    return g_context != nullptr ? g_context->stats : ImTypesetStats{};
}

ImTypesetStyle& GetStyle()
{
    // With no context there is still something to write to, as there is
    // nothing to read it.
    static ImTypesetStyle orphan;
    return g_context != nullptr ? g_context->style : orphan;
}

void SetTextCacheLimits(int max_paragraphs, int max_bytes)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr)
        return;
    if (max_paragraphs > 0)
        ctx->cache_max_paragraphs = static_cast<size_t>(max_paragraphs);
    if (max_bytes > 0)
        ctx->cache_max_bytes = static_cast<size_t>(max_bytes);
}

ImFont* AddFontFromFileTTF(ImTypesetFace face, const char* filename, int face_index,
                           const ImFontConfig* font_cfg)
{
    if (filename == nullptr)
        filename = "";
    return add_font(face, filename, nullptr, 0, face_index, font_cfg);
}

ImFont* AddFontFromMemoryTTF(ImTypesetFace face, const void* font_data, int font_data_size,
                             int face_index, const ImFontConfig* font_cfg)
{
    return add_font(face, nullptr, font_data, font_data_size, face_index, font_cfg);
}

bool RemoveFont(ImTypesetFace face)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr)
        return no_context();
    if (!bound(*ctx))
        return not_bound();
    const int role = role_of(face);
    if (role < 0)
        return fail(ImTypesetError_InvalidArgument, "not a face: " + std::to_string(face));
    validate_fonts(*ctx);
    ImFont* font = ctx->added[role];
    if (font == nullptr)
        return fail(ImTypesetError_NoFont,
                    std::string("no font has been added for ") + public_face(role));
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas == nullptr || atlas->Locked)
        return fail(ImTypesetError_InvalidArgument,
                    "the ImGui font atlas is locked: remove fonts outside a frame");
    ctx->added[role] = nullptr;
    ctx->added_id[role] = 0;
    ctx->added_mark[role] = 0;
    ctx->library.unregister_font(static_cast<Face>(role));
    if (ImGui::GetIO().FontDefault == font)
        ImGui::GetIO().FontDefault = nullptr;
    atlas->RemoveFont(font);
    ctx->math_fonts.clear();
    ctx->fonts_checked_frame = -1;
    refresh_fonts(*ctx);
    return succeed();
}

ImFont* GetFont(ImTypesetFace face)
{
    if (g_context == nullptr)
        return nullptr;
    const int role = face == ImTypesetFace_Default ? 0 : role_of(face);
    if (role < 0)
        return nullptr;
    // The pointer is into the atlas of the ImGui context this one was created
    // in: with that context not current nothing here can vouch for it.
    if (!bound(*g_context))
        return nullptr;
    validate_fonts(*g_context);
    return g_context->fonts.faces[role].font;
}

bool FaceHasGlyph(ImTypesetFace face, unsigned int codepoint)
{
    if (g_context == nullptr)
        return false;
    const int role = face == ImTypesetFace_Default ? 0 : role_of(face);
    if (role < 0)
        return false;
    const FaceData* const data = g_context->library.face_data(static_cast<Face>(role));
    return data != nullptr && data->has(codepoint);
}

namespace
{

// The most pixels an em may be for a metrics query: far past any size type is
// set at, and small enough that every answer is a finite float.
constexpr float MAX_QUERY_EM_PX = 1.0e6f;

// The file behind a role, for a query about the role's own font at a size.
// Null, with the failure said, when there is none to answer from.
const FaceData* queried_face(ImTypesetFace face, float em_size)
{
    if (g_context == nullptr) {
        no_context();
        return nullptr;
    }
    const int role = face == ImTypesetFace_Default ? 0 : role_of(face);
    if (role < 0) {
        fail(ImTypesetError_InvalidArgument, "not a face: " + std::to_string(face));
        return nullptr;
    }
    if (!(is_finite(static_cast<double>(em_size)) && em_size > 0.0f &&
          em_size <= MAX_QUERY_EM_PX)) {
        fail(ImTypesetError_InvalidArgument,
             "the em size is not a finite number above zero and at most 1,000,000");
        return nullptr;
    }
    const FaceData* const data = g_context->library.face_data(static_cast<Face>(role));
    if (data == nullptr || data->units_per_em() <= 0) {
        fail(ImTypesetError_NoFont, std::string("no font has been added for ") + public_face(role));
        return nullptr;
    }
    return data;
}

} // namespace

bool GetFontMetrics(ImTypesetFace face, float em_size, ImTypesetFontMetrics* out, size_t out_size)
{
    if (out == nullptr)
        return fail(ImTypesetError_InvalidArgument, "no ImTypesetFontMetrics was given");
    if (out_size != sizeof(ImTypesetFontMetrics))
        return fail(ImTypesetError_VersionMismatch,
                    "ImTypesetFontMetrics is " + std::to_string(out_size) +
                        " bytes where GetFontMetrics() is called and " +
                        std::to_string(sizeof(ImTypesetFontMetrics)) +
                        " in the imtypeset library: the two were compiled against different "
                        "imtypeset.h");
    *out = ImTypesetFontMetrics();
    const FaceData* const data = queried_face(face, em_size);
    if (data == nullptr)
        return false;
    // The reference's order (fontlib's metrics): one scale, formed once, and a
    // product a length. The same doubles layout's FontMetrics answers.
    const double size = static_cast<double>(em_size);
    const double scale = size / data->units_per_em();
    const double ascent = data->ascent() * scale;
    const double descent = data->descent() * scale;
    out->EmSize = em_size;
    out->FontSize = static_cast<float>(ascent + descent);
    out->Ascent = static_cast<float>(ascent);
    out->Descent = static_cast<float>(descent);
    out->LineGap = static_cast<float>(data->font().hhea_line_gap() * scale);
    out->XHeight = static_cast<float>(data->x_height() * scale);
    out->SpaceAdvance = static_cast<float>(data->font().advance(data->glyph(0x20)) * scale);
    return succeed();
}

float GetGlyphAdvance(ImTypesetFace face, float em_size, unsigned int codepoint)
{
    const FaceData* const data = queried_face(face, em_size);
    if (data == nullptr)
        return 0.0f;
    const double scale = static_cast<double>(em_size) / data->units_per_em();
    succeed();
    return static_cast<float>(data->font().advance(data->glyph(codepoint)) * scale);
}

float GetDeviceGlyphAdvance(ImTypesetFace face, unsigned int codepoint, const ImTypesetStyle* style,
                            float scale)
{
    ImTypesetContext* const ctx = g_context;
    if (ctx == nullptr) {
        no_context();
        return 0.0f;
    }
    const int role = face == ImTypesetFace_Default ? 0 : role_of(face);
    if (role < 0) {
        fail(ImTypesetError_InvalidArgument, "not a face: " + std::to_string(face));
        return 0.0f;
    }
    if (!bound(*ctx)) {
        not_bound();
        return 0.0f;
    }
    if (!backend_has_textures()) {
        no_textures();
        return 0.0f;
    }
    validate_fonts(*ctx);
    const FaceData* const data = ctx->library.face_data(static_cast<Face>(role));
    if (data == nullptr || ctx->fonts.faces[role].font == nullptr) {
        fail(ImTypesetError_NoFont, std::string("no font has been added for ") + public_face(role));
        return 0.0f;
    }
    char named[16];
    std::snprintf(named, sizeof named, "U+%04X", codepoint);
    if (!data->has(codepoint)) {
        fail(ImTypesetError_InvalidArgument,
             std::string("the font added for ") + public_face(role) + " has no glyph for " + named);
        return 0.0f;
    }
    ImTypesetStyle given = style != nullptr ? *style : ctx->style;
    given.Advances = ImTypesetAdvances_Device;
    const ResolvedStyle resolved = resolve_style(*ctx, given);
    // A block's size and a run's: the style's em times the scale, rounded once.
    Scaled size = resolved.em;
    if (is_finite(static_cast<double>(scale)) && scale > 0.0f && scale != 1.0f)
        size = clamped_sp(
            round_to_scaled(static_cast<double>(resolved.em) *
                            clamped(decimal_of(scale), MIN_RUN_SCALE, MAX_RUN_SCALE, 1.0)),
            1, MAX_EM);
    const FontSpec spec = resolved.font(static_cast<Face>(role), size);
    const FontMetrics* const font = ctx->library.metrics(spec.face, static_cast<double>(spec.size));
    double pixels = 0.0;
    if (spec.device == 0 || font == nullptr ||
        !device_advance_px(*ctx, *font, spec.size, ctx->devices.table[spec.device - 1u], codepoint,
                           &pixels)) {
        fail(ImTypesetError_InvalidArgument, std::string("the font atlas has no glyph for ") +
                                                 named + " in " + public_face(role) +
                                                 " at this size");
        return 0.0f;
    }
    // What layout holds for it: the memo's width, a whole number of
    // framebuffer pixels rounded UP to a whole number of scaled points
    // (src/device.cpp). Less than a scaled point above the whole pixel, so
    // rounding to the nearest pixel gives the pixels back, which is the
    // atlas's own number; the caret and the range rectangles, which are
    // read off layout, keep the residue.
    const Device device = ctx->devices.table[spec.device - 1u];
    const CharDimens dimens = ctx->cache(spec.key(), codepoint);
    const double framebuffer =
        py_round(px_from_sp(dimens.width, device.px_per_inch * device.density));
    succeed();
    return static_cast<float>(framebuffer / device.density);
}

bool TextUnformatted(const char* text, const char* text_end, ImTypesetTextFlags flags,
                     float wrap_width, const ImTypesetStyle* style)
{
    WidgetArgs args;
    args.text = text;
    args.text_end = text_end;
    args.flags = flags;
    args.wrap_width = wrap_width;
    args.style = style;
    return text_widget(args);
}

bool TextV(const char* fmt, va_list args)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr)
        return no_context();
    if (fmt == nullptr)
        fmt = "";
    std::string& out = ctx->formatted;
    va_list measure;
    va_copy(measure, args);
    const int needed = std::vsnprintf(nullptr, 0, fmt, measure);
    va_end(measure);
    if (needed < 0)
        return fail(ImTypesetError_InvalidArgument, "the format string could not be formatted");
    out.resize(static_cast<size_t>(needed) + 1);
    std::vsnprintf(&out[0], out.size(), fmt, args);
    out.resize(static_cast<size_t>(needed));
    WidgetArgs widget;
    widget.text = out.data();
    widget.text_end = out.data() + out.size();
    return text_widget(widget);
}

bool Text(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const bool ok = TextV(fmt, args);
    va_end(args);
    return ok;
}

bool TextWithID(const char* str_id, const char* text, const char* text_end,
                ImTypesetTextFlags flags, float wrap_width, const ImTypesetStyle* style)
{
    WidgetArgs args;
    args.str_id = str_id != nullptr ? str_id : "";
    args.text = text;
    args.text_end = text_end;
    args.flags = flags;
    args.wrap_width = wrap_width;
    args.style = style;
    return text_widget(args);
}

bool SelectableText(const char* str_id, const char* text, const char* text_end,
                    ImTypesetTextFlags flags, float wrap_width, const ImTypesetStyle* style,
                    ImTypesetTextRange* selection)
{
    WidgetArgs args;
    args.text = text;
    args.text_end = text_end;
    args.flags = flags;
    args.wrap_width = wrap_width;
    args.style = style;
    // An item that takes the mouse needs an ID ImGui accepts. Without one the
    // words are still set, as plain text, and the call says what is missing.
    if (str_id == nullptr || str_id[0] == '\0') {
        text_widget(args);
        return fail(ImTypesetError_InvalidArgument,
                    "SelectableText needs a str_id that is not empty");
    }
    args.str_id = str_id;
    args.selectable = true;
    args.selection = selection;
    return text_widget(args);
}

ImVec2 CalcTextSize(const char* text, const char* text_end, float wrap_width,
                    ImTypesetTextFlags flags, const ImTypesetStyle* style)
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr) {
        no_context();
        return ImVec2(0.0f, 0.0f);
    }
    if (bound(*ctx))
        validate_fonts(*ctx);
    if (ctx->measured == nullptr) {
        ctx->measured.reset(new ImTypesetParagraph());
        ctx->measured->ctx = ctx;
    }
    ImTypesetParagraph& p = *ctx->measured;
    const size_t size = text_size(text, text_end);
    flags &= ImTypesetTextFlags_HardBreaks | ImTypesetTextFlags_Math;
    const size_t kept = std::min(size, static_cast<size_t>(MAX_TEXT_BYTES));
    if (p.given_size != size || p.flags != flags || p.text.size() > kept ||
        std::memcmp(text, p.text.data(), p.text.size()) != 0)
        set_text(p, text, size, nullptr, 0, flags);

    ImTypesetStyle given = style != nullptr ? *style : ctx->style;
    const bool wrapped = is_finite(wrap_width) && wrap_width > 0.0f;
    if (!wrapped) {
        // One line as long as it comes: ragged, so that nothing is stretched
        // to a margin that is not there.
        given.Align = ImTypesetAlign_Left;
        given.LineBreak = ImTypesetLineBreak_FirstFit;
    }
    const ResolvedStyle resolved = resolve_style(*ctx, given);
    layout_paragraph(*ctx, p, resolved, wrapped ? resolve_width(wrap_width, resolved) : MAX_DIMEN);
    report(p.error);
    return p.size;
}

ImTypesetParagraph* CreateParagraph()
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr) {
        no_context();
        return nullptr;
    }
    std::unique_ptr<ImTypesetParagraph> made(new ImTypesetParagraph());
    made->ctx = ctx;
    made->slot = ctx->paragraphs.size();
    ctx->paragraphs.push_back(made.get());
    succeed();
    return made.release();
}

void DestroyParagraph(ImTypesetParagraph* paragraph)
{
    if (paragraph == nullptr || paragraph->ctx == nullptr)
        return;
    // Its own context's, whichever is current, and found by where it says it
    // is: the last paragraph takes its place.
    std::vector<ImTypesetParagraph*>& all = paragraph->ctx->paragraphs;
    if (paragraph->slot >= all.size() || all[paragraph->slot] != paragraph)
        return; // A widget's, or the measuring one: not the host's to destroy.
    all[paragraph->slot] = all.back();
    all[paragraph->slot]->slot = paragraph->slot;
    all.pop_back();
    delete paragraph;
}

bool SetParagraphText(ImTypesetParagraph* paragraph, const char* text, const char* text_end,
                      const ImTypesetRun* runs, int runs_count, ImTypesetTextFlags flags)
{
    ImTypesetParagraph* p = owned(paragraph);
    if (p == nullptr)
        return fail(ImTypesetError_InvalidArgument, "no paragraph was given");
    const size_t size = text_size(text, text_end);
    set_text(*p, text, size, runs, runs_count,
             flags & (ImTypesetTextFlags_HardBreaks | ImTypesetTextFlags_Math));
    p->material_error = p->text_error;
    p->error = p->text_error;
    return report(p->error);
}

bool LayoutParagraph(ImTypesetParagraph* paragraph, float width, const ImTypesetStyle* style)
{
    ImTypesetParagraph* p = owned(paragraph);
    if (p == nullptr)
        return fail(ImTypesetError_InvalidArgument, "no paragraph was given");
    ImTypesetContext& ctx = *p->ctx;
    if (bound(ctx))
        validate_fonts(ctx);
    const ImTypesetStyle& given = style != nullptr ? *style : ctx.style;
    const ResolvedStyle resolved = resolve_style(ctx, given);
    layout_paragraph(ctx, *p, resolved, resolve_width(width, resolved));
    remember_colours(*p, given);
    return report(p->error);
}

const ImTypesetErrorInfo* GetParagraphError(const ImTypesetParagraph* paragraph)
{
    if (paragraph == nullptr)
        return no_paragraph().get();
    return paragraph->error.get();
}

ImTypesetStatusFlags GetParagraphStatus(const ImTypesetParagraph* paragraph)
{
    if (!laid_out(paragraph))
        return ImTypesetStatusFlags_None;
    ImTypesetStatusFlags flags = ImTypesetStatusFlags_LaidOut;
    if (paragraph->layout.broken.budget_exhausted)
        flags |= ImTypesetStatusFlags_FirstFitFallback;
    if (paragraph->display_block)
        flags |= ImTypesetStatusFlags_DisplayMath;
    return flags;
}

int GetParagraphFormulaCount(const ImTypesetParagraph* paragraph)
{
    if (paragraph == nullptr)
        return 0;
    return static_cast<int>(std::min<size_t>(paragraph->formulas.size(), 0x7FFFFFFF));
}

ImTypesetFormula GetParagraphFormula(const ImTypesetParagraph* paragraph, int index)
{
    ImTypesetFormula out = {0, 0, false, false, -1, -1, ""};
    if (paragraph == nullptr || index < 0 ||
        static_cast<size_t>(index) >= paragraph->formulas.size())
        return out;
    const FormulaRecord& record = paragraph->formulas[static_cast<size_t>(index)];
    out.TextStart = static_cast<int>(record.begin);
    out.TextEnd = static_cast<int>(record.end);
    out.Display = record.display;
    out.Refused = record.refused;
    out.BytePos = record.byte_pos;
    out.CodepointPos = record.codepoint_pos;
    out.Reason = record.reason.c_str();
    return out;
}

ImVec2 GetParagraphSize(const ImTypesetParagraph* paragraph)
{
    return paragraph != nullptr ? paragraph->size : ImVec2(0.0f, 0.0f);
}

int GetParagraphLineCount(const ImTypesetParagraph* paragraph)
{
    return paragraph != nullptr ? paragraph->line_count : 0;
}

ImTypesetLine GetParagraphLine(const ImTypesetParagraph* paragraph, int line)
{
    ImTypesetLine out = {ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), 0.0f, 0, 0};
    if (!laid_out(paragraph) || line < 0)
        return out;
    const ImTypesetParagraph& p = *paragraph;
    ensure_lines(p);
    if (static_cast<size_t>(line) >= p.line_boxes.size())
        return out;
    const LineBox& box = p.line_boxes[static_cast<size_t>(line)];
    out.Min = ImVec2(px_of(p, box.x0), px_of(p, box.top));
    out.Max = ImVec2(px_of(p, box.x1), px_of(p, box.bottom));
    out.Baseline = px_of(p, box.baseline);
    out.TextStart = byte_at_index(p, box.index);
    out.TextEnd = static_cast<size_t>(line) + 1 < p.line_boxes.size()
                      ? byte_at_index(p, p.line_boxes[static_cast<size_t>(line) + 1].index)
                      : static_cast<int>(p.text.size());
    if (out.TextEnd < out.TextStart)
        out.TextEnd = out.TextStart;
    return out;
}

bool DrawParagraph(const ImTypesetParagraph* paragraph, ImDrawList* draw_list, const ImVec2& pos,
                   const ImTypesetTextRange* selection)
{
    if (!can_draw(paragraph))
        return false;
    if (draw_list == nullptr) {
        // Null is the current window's. NewFrame invalidates the draw data and
        // Render makes it valid again: the one sign imgui.h gives of being
        // inside a frame, which is where there is a current window.
        if (ImGui::GetFrameCount() <= 0 || ImGui::GetDrawData() != nullptr)
            return fail(ImTypesetError_InvalidArgument,
                        "DrawParagraph: no draw list was given, which means the current "
                        "window's, and this is outside a frame");
        draw_list = ImGui::GetWindowDrawList();
    }
    draw_to_list(*paragraph->ctx, *paragraph, draw_list, pos, selection);
    return succeed();
}

bool DrawParagraphToBuffers(const ImTypesetParagraph* paragraph, const ImVec2& pos,
                            const ImVec4& clip_rect, ImVector<ImDrawVert>* out_vertices,
                            ImVector<ImDrawIdx>* out_indices, const ImTypesetTextRange* selection)
{
    if (out_vertices == nullptr || out_indices == nullptr)
        return fail(ImTypesetError_InvalidArgument,
                    "DrawParagraphToBuffers: a paragraph and two buffers are needed");
    if (!can_draw(paragraph))
        return false;
    ImTypesetContext& ctx = *paragraph->ctx;
    const ImTypesetParagraph& p = *paragraph;
    DrawArgs args;
    draw_args(ctx, p, pos, selection, args);
    DrawRun run;
    run.cmds = p.cmds.data();
    run.count = p.cmds.size();
    run.ink = ink_of(p);
    run.lines = &p.lines;
    ctx.buffers.clear();
    ctx.drawer.draw_runs(ctx.buffers, clip_rect, &run, 1, args, ctx.fonts, ctx.resolve);

    const int base = out_vertices->Size;
    const int count = ctx.buffers.vertices.Size;
    const int index_count = ctx.buffers.indices.Size;
    const uint64_t index_limit = sizeof(ImDrawIdx) >= 4 ? 0x7FFFFFFFull : 0xFFFFull;
    if (static_cast<uint64_t>(base) + static_cast<uint64_t>(count) >
            std::min<uint64_t>(index_limit + 1, 0x7FFFFFFFull) ||
        static_cast<uint64_t>(out_indices->Size) + static_cast<uint64_t>(index_count) >
            0x7FFFFFFFull) {
        return fail(ImTypesetError_TooManyVertices,
                    "the paragraph needs " + std::to_string(count) + " vertices after the " +
                        std::to_string(base) +
                        " already in the buffer, more than ImDrawIdx can index: narrow the "
                        "clip rectangle, start a new buffer, or build ImGui with a 32-bit "
                        "ImDrawIdx");
    }
    if (count > 0) {
        out_vertices->resize(base + count);
        std::memcpy(out_vertices->Data + base, ctx.buffers.vertices.Data,
                    static_cast<size_t>(count) * sizeof(ImDrawVert));
    }
    const int index_base = out_indices->Size;
    out_indices->resize(index_base + index_count);
    for (int i = 0; i < index_count; ++i)
        out_indices->Data[index_base + i] =
            static_cast<ImDrawIdx>(static_cast<unsigned int>(base) + ctx.buffers.indices.Data[i]);
    ++ctx.stats.DrawCalls;
    ctx.stats.VerticesEmitted += static_cast<ImU64>(count);
    return succeed();
}

ImTypesetHit HitTestParagraph(const ImTypesetParagraph* paragraph, const ImVec2& local_pos)
{
    ImTypesetHit out = {0, -1, false, 0};
    if (!laid_out(paragraph))
        return out;
    const ImTypesetParagraph& p = *paragraph;
    ensure_lines(p);
    if (p.line_boxes.empty())
        return out;
    ensure_model(p);
    const Point point = local_point(p, local_pos);
    const uint32_t index = p.model.hit(point);
    out.TextOffset = byte_at_index(p, index);
    // The line whose items the boundary is among: at the end of a line that
    // is the glue that closes it, which is still that line's.
    out.Line = static_cast<int>(line_of_index(p, index) - p.line_boxes.begin());
    // Lines are stacked: the first whose bottom is below the point is the
    // only one that can hold it.
    const auto under =
        std::partition_point(p.line_boxes.begin(), p.line_boxes.end(),
                             [&](const LineBox& box) { return box.bottom <= point.y; });
    out.Inside = under != p.line_boxes.end() && point.y >= under->top && point.x >= under->x0 &&
                 point.x < under->x1;
    if (out.Inside && !p.runs.empty()) {
        // The run of the character under the point, which is not always the
        // one the caret is before.
        const uint32_t under_byte =
            static_cast<uint32_t>(std::max(byte_under(p, *under, point.x, out.TextOffset), 0));
        const auto run =
            std::upper_bound(p.runs.begin(), p.runs.end(), under_byte,
                             [](uint32_t wanted, const StyledRun& r) { return wanted < r.end; });
        if (run != p.runs.end() && run->start <= under_byte)
            out.Tag = run->tag;
    }
    return out;
}

ImTypesetTextRange SelectParagraphRange(const ImTypesetParagraph* paragraph, const ImVec2& anchor,
                                        const ImVec2& active, ImTypesetGranularity granularity)
{
    if (!laid_out(paragraph))
        return ImTypesetTextRange{0, 0};
    ensure_model(*paragraph);
    Granularity unit = Granularity::CHARACTER;
    if (granularity == ImTypesetGranularity_Word)
        unit = Granularity::WORD;
    else if (granularity == ImTypesetGranularity_Line)
        unit = Granularity::LINE;
    return text_range_of(*paragraph,
                         paragraph->model.select_at(local_point(*paragraph, anchor),
                                                    local_point(*paragraph, active), unit));
}

int GetParagraphRangeRects(const ImTypesetParagraph* paragraph, const ImTypesetTextRange& range,
                           ImVector<ImVec4>* out_rects)
{
    if (!laid_out(paragraph) || out_rects == nullptr)
        return 0;
    const ImTypesetParagraph& p = *paragraph;
    ImTypesetContext& ctx = *p.ctx;
    ensure_model(p);
    ctx.rects.clear();
    p.model.rects(selection_of(p, range), ctx.rects);
    for (const Rect& rect : ctx.rects) {
        out_rects->push_back(ImVec4(px_of(p, rect.x), px_of(p, rect.y),
                                    px_of(p, rect.x + rect.width), px_of(p, rect.y + rect.height)));
    }
    return static_cast<int>(ctx.rects.size());
}

ImVec4 GetParagraphCaretRect(const ImTypesetParagraph* paragraph, int byte_offset)
{
    if (!laid_out(paragraph))
        return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    const ImTypesetParagraph& p = *paragraph;
    ensure_lines(p);
    if (p.line_boxes.empty())
        return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    const uint32_t index = index_at_byte(p, byte_offset);
    const auto line = line_of_index(p, index);
    Scaled x = index >= line->index_end ? line->x1 : line->x0;
    for (uint32_t at = line->first + 1; at < line->end; ++at) {
        const DrawCmd& cmd = p.cmds[at];
        if (is_item(cmd) && cmd.index == index) {
            x = cmd.x;
            break;
        }
    }
    return ImVec4(px_of(p, x), px_of(p, line->top), px_of(p, x), px_of(p, line->bottom));
}

int AppendParagraphRangeText(const ImTypesetParagraph* paragraph, const ImTypesetTextRange& range,
                             ImGuiTextBuffer* out)
{
    if (!laid_out(paragraph) || out == nullptr)
        return 0;
    const ImTypesetParagraph& p = *paragraph;
    ImTypesetContext& ctx = *p.ctx;
    ensure_model(p);
    TextData data;
    data.separators = p.separators.data();
    data.separator_count = p.separators.size();
    data.atoms = p.atom_texts.data();
    data.atom_count = p.atom_texts.size();
    data.originals = p.originals.data();
    data.original_count = p.originals.size();
    ctx.scratch.clear();
    p.model.text(selection_of(p, range), data, ctx.scratch);
    if (ctx.scratch.empty())
        return 0;
    const int before = out->size();
    // The separator the document's structure puts before a paragraph.
    if (before > 0)
        out->append("\n\n");
    out->append(ctx.scratch.data(), ctx.scratch.data() + ctx.scratch.size());
    return out->size() - before;
}

int TextCodepointToByteOffset(const char* text, int codepoint_offset, const char* text_end)
{
    const size_t size = text_size(text, text_end);
    return codepoint_to_byte(std::string_view(text, size), codepoint_offset);
}

int TextByteToCodepointOffset(const char* text, int byte_offset, const char* text_end)
{
    const size_t size = text_size(text, text_end);
    return byte_to_codepoint(std::string_view(text, size), byte_offset);
}

int ParagraphCodepointToByteOffset(const ImTypesetParagraph* paragraph, int codepoint_offset)
{
    return paragraph != nullptr ? codepoint_to_byte(paragraph->text, codepoint_offset) : 0;
}

int ParagraphByteToCodepointOffset(const ImTypesetParagraph* paragraph, int byte_offset)
{
    return paragraph != nullptr ? byte_to_codepoint(paragraph->text, byte_offset) : 0;
}

} // namespace ImTypeset
