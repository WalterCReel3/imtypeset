// What stands behind the two opaque types of include/imtypeset.h: the context
// and the paragraph. Internal; a test or the math layer includes it, a host
// never does.
//
// ONE VALIDATING ENTRY. Everything a host passes as a length goes through
// `resolve_style` and `resolve_width`, which turn pixels into scaled points and
// clamp: NaN to a default, the rest into ranges that keep every dimension that
// reaches a node within MAX_DIMEN (src/units.h). What a font answers is held
// to the same bound: a character's dimensions, a space's width and a kern by
// src/style as it reads them (CharMeasureCache, append_text), and the glue
// made from a space, which a space factor can widen past the bound again, as
// the material is built here. Below this file nothing re-checks.
//
// WHAT A PARAGRAPH CACHES, and what invalidates each stage:
//
//   material   the text as nodes       text, runs, flags; face, em, spacing,
//                                      alignment; the font registry
//   layout     lines, shipped commands the material; width, indents, leading,
//              the line index          layout flags, pixels per inch
//   selection  the SelectionModel      the layout; built at the first query
//
// Colours are read at draw time and invalidate nothing.
//
// ERRORS. One process-wide slot is what ImTypeset::GetLastError() answers
// from; every reporting entry sets or clears it. A paragraph keeps its own
// three: what set_text found, what building the material found on top of
// that, and what the last layout found on top of that. Each stage starts from
// the one before, so laying out again at another width cannot leave a layout
// failure behind.
//
// TEXT WIDGETS keep their paragraphs in the context, keyed by ImGui's ID stack
// and a hash of the text (or a string ID the caller gave), on a list ordered
// in order of arrival, gone over a few at a time: `evict` in src/imtypeset.cpp.
//
// MATH. With ImTypesetTextFlags_Math on a text, `build_material` in
// src/imtypeset.cpp finds the formulas
// (`scan_math`, the reference's inline rule; `display_block`, its block rule),
// sets each through src/mathtext and src/math_mlist at the size of the text
// around it, and puts the box in the material as one atom. See
// `ImTypesetParagraph::atoms` for how an atom copies as its source and
// `ImTypesetParagraph::formulas` for what is reported about each.
//
// `InlineMath` is the same seam with the two halves replaceable: a scanner
// that says where the next formula is and a builder that sets its source as
// one atom box. One installed with `set_inline_math` is used in place of the
// built-in pair; a test does that.

#pragma once

#include "imtypeset.h"

#include "draw.h"
#include "fontlib.h"
#include "math_mlist.h"
#include "mathtext.h"
#include "nodes.h"
#include "paragraph.h"
#include "selection.h"
#include "shipout.h"
#include "style.h"
#include "tex_rules.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ImTypeset::detail
{

// The limits the validating entry enforces.
constexpr int MAX_TEXT_BYTES = 1 << 24;
constexpr double MIN_EM_PX = 1.0;
constexpr double MAX_EM_PX = 1024.0;
constexpr double MIN_PX_PER_INCH = 24.0;
constexpr double MAX_PX_PER_INCH = 2400.0;
constexpr double MIN_LINE_HEIGHT = 0.5;
constexpr double MAX_LINE_HEIGHT = 10.0;
constexpr double MIN_RUN_SCALE = 0.1;
constexpr double MAX_RUN_SCALE = 10.0;
// An em and a run's size stay well under MAX_DIMEN, so that a glyph a few ems
// across still does.
constexpr Scaled MAX_EM = MAX_DIMEN / 16;

// A style with every length in scaled points and every value in range.
struct ResolvedStyle {
    Face face = Face::PROSE;
    Scaled em = 0;
    Scaled left_indent = 0;
    Scaled first_line_indent = 0;
    Scaled baselineskip = 0;
    Scaled lineskip = 0;
    Align align = Align::JUSTIFY;
    Spacing spacing = Spacing::EXTRA_SPACE_ONLY;
    // ImTypesetLineBreak_, validated.
    int line_break = 0;
    double px_per_inch = DEFAULT_PX_PER_INCH;
};

// Inline formulas, for the layer that has a math typesetter.
struct InlineMath {
    // The next formula at or after byte `from` of `text`: [*begin, *end) with
    // both delimiters. False if there is none.
    bool (*scan)(void* user, std::string_view text, size_t from, size_t* begin,
                 size_t* end) = nullptr;
    // Sets `source`, a formula with its delimiters, as one atom box of `arena`
    // (src/shipout's `atom_box`) at `font`. False to refuse it, with a reason
    // and where in `source` the reason applies, in bytes and in codepoints.
    bool (*build)(void* user, std::string_view source, FontSpec font, NodeArena& arena,
                  CharMeasure measure, MetricsResolver resolve, Node* atom, std::string* why,
                  uint32_t* byte_pos, uint32_t* codepoint_pos) = nullptr;
    void* user = nullptr;
};

// One styled stretch of a paragraph, validated: offsets on codepoint starts,
// in order, not overlapping.
struct StyledRun {
    uint32_t start = 0;
    uint32_t end = 0;
    int face = -1; // -1: the paragraph's
    double scale = 1.0;
    ImU32 colour = 0;
    bool breakable = true;
    // ImTypesetRun::Tag, and whether the run is one formula the host marked:
    // 0 no, 1 in text style, 2 in display style.
    int tag = 0;
    uint8_t formula = 0;
};

// Nodes [first, end) of the material are drawn in `colour`.
struct InkSpan {
    uint32_t first = 0;
    uint32_t end = 0;
    ImU32 colour = 0;
};

// A formula's atom: node `node` of the material copies as bytes [begin, end)
// of the text, or, for a display block, of ImTypesetParagraph::display_text.
struct AtomSource {
    uint32_t node = 0;
    uint32_t begin = 0;
    uint32_t end = 0;
    bool in_display_text = false;
};

// A formula the scanner found, as bytes of the text, delimiters included.
// `marker` is the length of one delimiter, or 0 when an installed InlineMath
// found it and only that layer knows.
struct FoundFormula {
    uint32_t begin = 0;
    uint32_t end = 0;
    uint32_t marker = 0;
    // Not found but given, as a run (StyledRun::formula): the bytes are the
    // source and there are no delimiters.
    uint32_t explicit_kind = 0;
};

// What became of one formula: ImTypesetFormula, owned.
struct FormulaRecord {
    uint32_t begin = 0;
    uint32_t end = 0;
    bool display = false;
    bool refused = false;
    int byte_pos = -1;
    int codepoint_pos = -1;
    std::string reason;
};

// One line of a laid-out paragraph, in the scaled points of its commands.
struct LineBox {
    Scaled x0 = 0; // where its content starts
    Scaled x1 = 0; // and ends: the left edge of the glue that closes the line
    Scaled top = 0;
    Scaled bottom = 0;
    Scaled baseline = 0;
    uint32_t first = 0;     // its LINE command
    uint32_t end = 0;       // one past its last command
    uint32_t index = 0;     // the first index on it
    uint32_t index_end = 0; // one past the last
};

struct ErrorState {
    ImTypesetError code = ImTypesetError_None;
    int byte_pos = -1;
    int codepoint_pos = -1;
    std::string message;

    void clear();
    void set(ImTypesetError code_, std::string text, int byte_pos_ = -1, int codepoint_pos_ = -1);
    bool ok() const { return code == ImTypesetError_None; }
    // The state as the public struct. Valid until this object next changes.
    const ImTypesetErrorInfo* get() const;

private:
    mutable ImTypesetErrorInfo _info = {ImTypesetError_None, -1, -1, ""};
};

} // namespace ImTypeset::detail

struct ImTypesetParagraph {
    ImTypesetContext* ctx = nullptr;

    // --- What every call reads, kept together at the front: a frame goes
    // over thousands of these, most of them only to find that nothing has
    // changed or that nothing is in view, and each should cost one or two
    // cache lines and not one per field.
    bool has_material = false;
    bool has_layout = false;
    // The text could not be set at all (no font): a widget draws it with
    // ImGui instead.
    bool fallback = false;
    ImTypesetTextFlags flags = 0;
    uint32_t text_version = 1;
    uint32_t material_text_version = 0;
    uint32_t material_generation = 0;
    int line_count = 0;
    ImVec2 size = ImVec2(0.0f, 0.0f);
    ImTypeset::detail::Scaled layout_width = 0;
    // Colours, as of the last layout call.
    ImU32 colour = 0;
    ImU32 selection_colour = 0;
    // A text widget's: its key in the cache and when it was last submitted.
    uint64_t key = 0;
    int last_frame = -1;
    int uses_this_frame = 0;
    // How many bytes the caller gave, before the text was cut to a limit.
    size_t given_size = 0;
    // The paragraph's error, as asked for: what is wrong with its material,
    // or failing that what its last layout found.
    ImTypeset::detail::ErrorState error;
    std::string text;
    ImTypeset::detail::ResolvedStyle material_style;
    ImTypeset::detail::ResolvedStyle layout_style;
    std::vector<ImTypeset::detail::DrawCmd> cmds;
    ImTypeset::detail::LineIndex lines;
    std::vector<ImTypeset::detail::InkRange> inks;

    // --- What was given
    std::vector<ImTypeset::detail::StyledRun> runs;
    // What set_text found, and that or what build_material found first.
    ImTypeset::detail::ErrorState text_error;
    ImTypeset::detail::ErrorState material_error;
    // Where it is in ImTypesetContext::paragraphs; not meaningful for a
    // widget's paragraph.
    size_t slot = 0;

    // --- Material
    ImTypeset::detail::TextBlock block;
    std::vector<uint32_t> source_offsets; // per node of block.run
    std::vector<ImTypeset::detail::InkSpan> ink_spans;
    std::vector<ImTypeset::detail::AtomSource> atoms;
    std::vector<ImTypeset::detail::FormulaRecord> formulas;
    // The text is one display block: it is centred and takes no first-line
    // indent, whatever the style says.
    bool display_block = false;
    // A display block as it copies and as it is set when refused: `$$`, its
    // source with every run of white space one space, `$$`. `display_map` has
    // one entry per byte and one past the end: the byte of `text` it stands for.
    std::string display_text;
    std::vector<uint32_t> display_map;
    ImTypeset::detail::NodeArena arena;
    ImTypeset::detail::NodeArena::Mark material_mark;
    // If not null, where the material's boxes go instead of `arena`, which is
    // then not cleared when the material is built: a document's block.
    ImTypeset::detail::NodeArena* material_arena = nullptr;

    // --- Layout
    ImTypeset::detail::ParagraphLayout layout;
    std::vector<uint32_t> index_bytes; // item_count + 1 entries, never decreasing
    // A table's (a document's view of one): the first index of each cell but
    // the first of its row, ascending. A copy puts a tab before each; the
    // first index of each row but the first is in layout.hard_breaks.
    std::vector<uint32_t> cell_breaks;
    // The lines, found in the commands at the first question about them.
    mutable std::vector<ImTypeset::detail::LineBox> line_boxes;
    mutable bool has_lines = false;

    // --- Selection, built at the first query after a layout
    mutable ImTypeset::detail::SelectionModel model;
    mutable bool has_model = false;
    mutable std::vector<ImTypeset::detail::IndexedText> separators;
    mutable std::vector<ImTypeset::detail::IndexedText> atom_texts;
    mutable std::vector<ImTypeset::detail::IndexedText> originals;
    mutable std::string original_bytes;

    // --- A text widget's bookkeeping
    // Its place among the context's widget paragraphs, in order of arrival,
    // and what it was last counted as holding.
    ImTypesetParagraph* newer = nullptr;
    ImTypesetParagraph* older = nullptr;
    size_t footprint = 0;
    // SelectableText: the range selected, and the gesture that is selecting.
    ImTypesetTextRange selection = {0, 0};
    ImVec2 anchor = ImVec2(0.0f, 0.0f);
    ImTypesetGranularity granularity = ImTypesetGranularity_Character;
};

struct ImTypesetContext {
    ImTypesetContext();
    ~ImTypesetContext();
    ImTypesetContext(const ImTypesetContext&) = delete;
    ImTypesetContext& operator=(const ImTypesetContext&) = delete;

    ImGuiContext* imgui = nullptr;
    // ImTypeset::GetContextSerial(): this context's alone, in this process.
    ImU64 serial = 0;
    ImTypesetStyle style;
    ImTypesetStats stats = {};

    ImTypeset::detail::FontLibrary library;
    ImTypeset::detail::MetricsResolver resolve;
    ImTypeset::detail::CharMeasureCache cache;
    ImTypeset::detail::FaceFonts fonts;
    // The ImFont added for each role that has a file of its own, with the
    // atlas's identifier for it and the mark it was given (font_mark in
    // imtypeset.cpp). A pointer here is dereferenced only once
    // validate_fonts has found it in the current atlas carrying that mark:
    // the host may have removed the font, or destroyed the ImGui context and
    // made another at its address, and a font of the host's can stand where
    // one of these did with the same FontId, since each atlas numbers its
    // fonts from one.
    ImFont* added[ImTypeset::detail::FACE_COUNT] = {};
    ImGuiID added_id[ImTypeset::detail::FACE_COUNT] = {};
    ImU64 added_mark[ImTypeset::detail::FACE_COUNT] = {};
    // When they were last looked for in the atlas: see validate_fonts.
    int fonts_checked_frame = -1;
    int fonts_checked_count = -1;
    ImGuiID fonts_checked_newest = 0;
    const ImFontAtlas* fonts_checked_atlas = nullptr;

    // What the layout core measures characters with.
    ImTypeset::detail::CharMeasure measure();

    ImTypeset::detail::InlineMath math;
    // The math face at the three sizes of a formula, per size of the text
    // around it, for the registry generation they were read from.
    std::unordered_map<ImTypeset::detail::Scaled, std::unique_ptr<ImTypeset::detail::MathFonts>>
        math_fonts;
    uint32_t math_fonts_generation = 0;
    ImTypeset::detail::Formula formula;
    std::vector<ImTypeset::detail::FoundFormula> found;
    // Codepoint indices of the backslashes that escape a dollar and are not set.
    std::vector<uint32_t> dropped;
    std::vector<uint32_t> drop_at;
    std::string piece;
    std::string reason;

    ImTypeset::detail::ParagraphComposer composer;
    ImTypeset::detail::Drawer drawer;
    ImTypeset::detail::DrawBuffers buffers;
    std::vector<ImTypeset::detail::Rect> rects;
    // A paragraph's run colours with ImGui's style.Alpha applied, for one draw.
    std::vector<ImTypeset::detail::InkRange> inks;
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
    std::string scratch;

    // What Text() formatted, and the paragraph CalcTextSize() measures with.
    std::string formatted;
    std::unique_ptr<ImTypesetParagraph> measured;

    // The decimals a host wrote for ImTypesetStyle::LineHeight, as doubles;
    // see line_height_of.
    struct LineHeight {
        float given = 0.0f;
        double value = 0.0;
    };
    LineHeight line_heights[4];
    unsigned line_height_next = 0;

    // Every paragraph a host created, each at its `slot`.
    std::vector<ImTypesetParagraph*> paragraphs;
    // Every document a host created, each at its `slot`.
    std::vector<ImTypesetDocument*> documents;
    // The text widgets' paragraphs by key, and in order of arrival.
    std::unordered_map<uint64_t, ImTypesetParagraph*> widgets;
    ImTypesetParagraph* newest = nullptr;
    ImTypesetParagraph* oldest = nullptr;
    std::vector<ImTypesetParagraph*> pool;
    size_t cache_max_paragraphs = 2048;
    size_t cache_max_bytes = size_t(64) << 20;
    // The frames in which the list was last gone over, whole for being over
    // a limit and in part for what is stale: see evict.
    // The device-space rules every draw of this context is made under.
    ImTypeset::detail::DrawRules draw_rules = ImTypeset::detail::DrawRules::product();
    // Which of the reference's departures from tex.web every layout of this
    // context keeps: none of those that are switches (src/tex_rules.h). Read
    // when a paragraph's material is built and when it is laid out; changing
    // it does not relay what is cached.
    ImTypeset::detail::TexRules tex_rules = ImTypeset::detail::TexRules::product();
    int evicted_all_frame = -1;
    int evicted_some_frame = -1;
    // How many of the cache's paragraphs have been submitted in the frame
    // `submitted_frame`, and how many were in the frame before it: between
    // them, all that the cache may not drop.
    int submitted_frame = -1;
    size_t submitted_now = 0;
    size_t submitted_before = 0;
    // The SelectableText that was last clicked in: the one Ctrl+C copies from.
    uint64_t focused_key = 0;
};

// A statement run from a destructor, which must not unwind: where exceptions
// are enabled a host's IM_ASSERT may throw from inside ImGui, and an exception
// leaving a destructor ends the program. What the statement was undoing is
// then left as it is. Without exceptions this is the statement.
#if defined(__cpp_exceptions)
#define IMTYPESET_NO_UNWIND(statement) \
    try {                              \
        statement;                     \
    } catch (...) {                    \
    }
#else
#define IMTYPESET_NO_UNWIND(statement) statement
#endif

namespace ImTypeset::detail
{

// What src/imtypeset.cpp's public functions are made of, for the document.
namespace core
{

// A flag that says something derived is built, for the scope that builds it:
// the flag ends false unless the scope reached `finished`. The library throws
// nothing, but a host can unwind through it — an IM_ASSERT defined to throw,
// or std::bad_alloc where exceptions are enabled — and a flag set before the
// work it describes would then vouch for work that was cut short, for good:
// every later call would find it "built" and leave it alone. In a build
// without exceptions the destructor runs at the scope's end and does nothing.
struct Building {
    explicit Building(bool& built)
        : flag(built)
    {
    }
    ~Building()
    {
        if (!finished)
            flag = false;
    }
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;

    bool& flag;
    bool finished = false;
};

extern ImTypesetContext* g_context;
ErrorState& last_error();
std::string& layout_mismatch();
bool fail(ImTypesetError code, std::string message, int byte_pos = -1, int codepoint_pos = -1);
bool succeed();
bool report(const ErrorState& state);
bool no_context();
bool is_finite(double value);
double clamped(double value, double low, double high, double otherwise);
Scaled clamped_sp(Scaled value, Scaled low, Scaled high);
double line_height_of(ImTypesetContext& ctx, float given);
int role_of(ImTypesetFace face);
const char* public_face(int role);
bool usable(ImTypesetContext& ctx, int face);
Align align_of(ImTypesetAlign align);
double decimal_of(float value);
// Appends to `out` the runs of `runs` that are in order from `floor` on and
// inside a text of `size` bytes, validated. The first that is out of order is
// left out and reported in `error`, if that holds nothing yet, as
// ImTypesetError_DescriptionCut: the others are still taken.
void validate_runs(std::vector<StyledRun>& out, const ImTypesetRun* runs, int runs_count,
                   size_t size, uint32_t floor, ErrorState& error);
bool set_text(ImTypesetParagraph& p, const char* text, size_t size, const ImTypesetRun* runs,
              int runs_count, ImTypesetTextFlags flags);
void build_material_now(ImTypesetContext& ctx, ImTypesetParagraph& p, const ResolvedStyle& style);
float ceil_px(double pixels);
bool is_item(const DrawCmd& cmd);
// The byte of the text that the item under `x` on `line` came from: the last
// of the line's items that starts at or left of `x`. A caret position is the
// boundary NEAREST a point and so is the next character's when the point is on
// a character's right half; this is the character the point is on. `fallback`
// if the line holds no item there.
int byte_under(const ImTypesetParagraph& p, const LineBox& line, Scaled x, int fallback);
void ensure_lines(const ImTypesetParagraph& p);
void ensure_model(const ImTypesetParagraph& p);
void map_index_bytes(const std::vector<uint32_t>& shipped_at,
                     const std::vector<uint32_t>& source_offsets, uint32_t items,
                     uint32_t text_size, std::vector<uint32_t>& out);
void ink_ranges(const std::vector<uint32_t>& shipped_at, const std::vector<InkSpan>& spans,
                std::vector<InkRange>& out);
uint32_t index_at_byte(const ImTypesetParagraph& p, int byte_offset);
int byte_at_index(const ImTypesetParagraph& p, uint32_t index);
std::vector<LineBox>::const_iterator line_of_index(const ImTypesetParagraph& p, uint32_t index);
bool bound(const ImTypesetContext& ctx);
bool not_bound();
bool backend_has_textures();
bool no_textures();
void validate_fonts(ImTypesetContext& ctx);
size_t text_size(const char*& text, const char* text_end);

} // namespace core

// Destroys every document of a context: src/imtypeset_document.cpp, which
// alone knows what one is.
void destroy_documents(ImTypesetContext& ctx);

} // namespace ImTypeset::detail

namespace ImTypeset::detail
{

// The validating entry. `ctx` supplies the font registry (for the em of a
// pixel height) and ImGui's current font size.
ResolvedStyle resolve_style(ImTypesetContext& ctx, const ImTypesetStyle& style);
Scaled resolve_width(float width, const ResolvedStyle& style);

// Installs, or with a default-constructed value removes, a formula layer that
// replaces the built-in one. Every paragraph's material is rebuilt at its next
// layout.
void set_inline_math(ImTypesetContext& ctx, const InlineMath& math);

// The formulas of a text of `count` codepoints, of which [begin, end) is what
// is left when white space at both ends is dropped; `offsets` has the byte
// each codepoint starts at and one entry past the last. Appends to `found`, in
// order, and to `dropped` the index of every backslash that escapes a dollar.
//
// This is the reference's inline rule (advtext/markdown.py, `_math_inline`)
// over plain text, with markdown-it's backslash escape before it and none of
// its other rules: a backslash takes the character after it out of
// consideration; `$$` is tried before `$`; an opener is not followed by white
// space and not preceded by a letter or a digit; its closer is the first later
// marker not preceded by an odd number of backslashes, and closes only if it
// is not preceded by white space and not followed by a letter, a digit, `{` or
// `(`. When that first candidate fails the opener is text and the search does
// not go on.
void scan_math(const uint32_t* codepoints, const uint32_t* offsets, size_t begin, size_t end,
               std::vector<FoundFormula>& found, std::vector<uint32_t>& dropped);

// What a paragraph shipped at its last layout, for a test to compare.
const std::vector<DrawCmd>& shipped_commands(const ImTypesetParagraph& paragraph);

// The index range a range of text bytes covers, and the bytes an index range
// stands for: what the public API converts with. An item is covered when the
// byte it came from is in the range.
Selection selection_of(const ImTypesetParagraph& paragraph, const ImTypesetTextRange& range);
ImTypesetTextRange text_range_of(const ImTypesetParagraph& paragraph, Selection selection);

} // namespace ImTypeset::detail
