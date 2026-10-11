// The Document of include/imtypeset.h: blocks a host describes, over
// src/blocks' lazy Document. See "Documents" in src/context.h for how the two
// fit and where a block's material and its selection model are kept.
//
// The pieces a block is made of are a paragraph's, from src/imtypeset.cpp
// (namespace core): the same text-to-material function, the same index-to-byte
// map, the same line boxes and selection model. What is the document's own is
// here: resolving a block's description against the document's style, making
// material late, placing blocks from the unit tree, and the questions that
// cross blocks.

#include "imtypeset_document.h"

#include "imtypeset.h"

#include "context.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ImTypeset::detail;
using namespace ImTypeset::detail::core;

namespace
{

// How far outside the clip rectangle a unit is still laid out and drawn: a
// glyph's bitmap can reach a pixel past the box its line was measured from.
constexpr double WINDOW_MARGIN_PX = 2.0;
// Formula boxes no block names any more are dropped, all at once, when there
// are this many and more than those still named.
constexpr size_t DEAD_BOXES_MIN = 4096;
// A document keeps a view for each block the last draw showed, twice over, so
// that a steady frame builds none: never fewer than MIN_VIEWS before it reuses
// one and never more than MAX_VIEWS, whatever a clip rectangle shows.
constexpr size_t MIN_VIEWS = 32;
constexpr size_t MAX_VIEWS = 256;
// The most bytes of a marker that are set.
constexpr size_t MAX_MARKER_BYTES = 256;

ImTypesetDocument* owned(ImTypesetDocument* d)
{
    return d != nullptr && d->ctx != nullptr ? d : nullptr;
}

const ImTypesetDocument* owned(const ImTypesetDocument* d)
{
    return d != nullptr && d->ctx != nullptr ? d : nullptr;
}

bool no_document()
{
    return fail(ImTypesetError_InvalidArgument, "no document was given");
}

bool is_text(ImTypesetBlockKind kind)
{
    return kind == ImTypesetBlockKind_Paragraph || kind == ImTypesetBlockKind_Heading ||
           kind == ImTypesetBlockKind_ListItem;
}

bool is_code(ImTypesetBlockKind kind)
{
    return kind == ImTypesetBlockKind_Code;
}

bool is_host(ImTypesetBlockKind kind)
{
    return kind == ImTypesetBlockKind_Host || kind == ImTypesetBlockKind_Table;
}

// A length a host gave, in pixels: never negative, never past MAX_DIMEN.
Scaled length_sp(const ImTypesetDocument& d, float pixels)
{
    const double px = clamped(static_cast<double>(pixels), 0.0, 1.0e9, 0.0);
    return clamped_sp(sp_from_px(px, d.style.px_per_inch), 0, MAX_DIMEN);
}

// A coordinate a host gave, in pixels, either side of zero.
Scaled coordinate_sp(const ImTypesetDocument& d, double pixels)
{
    const double px = clamped(pixels, -1.0e9, 1.0e9, 0.0);
    return sp_from_px(px, d.style.px_per_inch);
}

double px_of(const ImTypesetDocument& d, Scaled length)
{
    return px_from_sp(length, d.style.px_per_inch);
}

float pxf(const ImTypesetDocument& d, Scaled length)
{
    return static_cast<float>(px_of(d, length));
}

bool same_style(const ResolvedStyle& a, const ResolvedStyle& b)
{
    return a.face == b.face && a.em == b.em && a.spacing == b.spacing && a.align == b.align &&
           a.left_indent == b.left_indent && a.first_line_indent == b.first_line_indent &&
           a.baselineskip == b.baselineskip && a.lineskip == b.lineskip &&
           a.line_break == b.line_break && a.px_per_inch == b.px_per_inch;
}

// How long a block's text is, for positions in it. A code block ends at its
// last character that is set: the blank lines at its end are dropped.
int text_size_of(const DocBlock& b)
{
    return static_cast<int>(is_code(b.kind) ? b.code_end : b.text.size());
}

// Counts a code block's lines and finds where what is set of it ends
// (DocBlock::code_lines, code_end): src/blocks' walk, which needs no font.
void measure_code(DocBlock& b)
{
    const CodeExtent extent = code_extent(b.text);
    b.code_lines = extent.lines;
    b.code_end = extent.end;
}

// How many views the document keeps, from how many blocks the last draw showed.
size_t view_limit(const ImTypesetDocument& d)
{
    return std::min(std::max(MIN_VIEWS, 2 * d.shown_blocks + 8), MAX_VIEWS);
}

// The longest prefix of `size` bytes of `text` that is no longer than `limit`
// and does not end inside a character.
size_t cut_at_character(const char* text, size_t size, size_t limit)
{
    if (size <= limit)
        return size;
    size_t kept = limit;
    while (kept > 0 && (static_cast<unsigned char>(text[kept]) & 0xC0) == 0x80)
        --kept;
    return kept;
}

// What stands in for a block's material in the height it is taken to have
// before it is laid out (Block::length_hint): a text's bytes, or the lines of
// a code block.
uint32_t length_hint(const DocBlock& b)
{
    if (is_code(b.kind))
        return b.code_lines;
    return static_cast<uint32_t>(std::min<size_t>(b.text.size(), 0xFFFFFFFFu));
}

// Lays out what a window shows: Document::layout_window, and before the units
// it answers any that stand at its top edge with no height yet. A block nobody
// has laid out may be taken to be nothing tall (an empty one, a host block of
// no height), and the search by height then starts after it.
UnitRange layout_window(ImTypesetDocument& d, Scaled top, Scaled bottom)
{
    UnitRange range = d.doc.layout_window(d.composer, top, bottom);
    while (range.first > 0) {
        const DocumentUnit& above = d.doc.unit(range.first - 1);
        if (above.exact || above.extent != 0)
            break;
        --range.first;
        d.doc.layout_unit(d.composer, range.first);
    }
    return range;
}

// --- Describing blocks -----------------------------------------------------------------

Composition composition_of(const ImTypesetDocument& d)
{
    Composition composition;
    composition.spacing = d.style.spacing;
    composition.rules = d.ctx->tex_rules;
    return composition;
}

// The containers [first, end) of `d.containers` as the block layer's quotes
// and lists. A container's parent is an earlier one, so one pass suffices.
// What is wrong with one is cut to what is right and noted in `trouble` if
// that holds nothing yet.
void declare_containers(ImTypesetDocument& d, size_t first, ErrorState& trouble)
{
    const auto note = [&](size_t container, const char* what) {
        if (trouble.ok())
            trouble.set(ImTypesetError_DescriptionCut,
                        "container " + std::to_string(container) + ": " + what);
    };
    d.container_quote.resize(d.containers.size(), NO_CONTAINER);
    d.container_list.resize(d.containers.size(), NO_CONTAINER);
    for (size_t c = first; c < d.containers.size(); ++c) {
        const ImTypesetContainer& given = d.containers[c];
        const int parent =
            given.Parent >= 1 && static_cast<size_t>(given.Parent) <= c ? given.Parent - 1 : -1;
        if (given.Parent != 0 && parent < 0)
            note(c, "its Parent is not an earlier container; it is inside none");
        if (given.Kind != ImTypesetContainerKind_List && given.Kind != ImTypesetContainerKind_Quote)
            note(c, "its Kind is not a container kind; it is a quote");
        const int32_t outer_quote =
            parent >= 0 ? d.container_quote[static_cast<size_t>(parent)] : NO_CONTAINER;
        const int32_t outer_list =
            parent >= 0 ? d.container_list[static_cast<size_t>(parent)] : NO_CONTAINER;
        if (given.Kind == ImTypesetContainerKind_List) {
            List list;
            list.parent = outer_list;
            list.indent = length_sp(d, given.Indent);
            list.tight = (given.Flags & ImTypesetContainerFlags_Tight) != 0;
            d.container_list[c] = d.doc.add_list(list);
            d.container_quote[c] = outer_quote;
        } else {
            Quote quote;
            quote.parent = outer_quote;
            quote.list = outer_list;
            quote.bar_x = length_sp(d, given.Indent);
            quote.ink = given.Color;
            d.container_quote[c] = d.doc.add_quote(quote);
            d.container_list[c] = outer_list;
        }
    }
}

// Copies one block's description out of what the host handed over. What is
// wrong with it is cut to what is right and noted in `trouble` if that holds
// nothing yet.
void describe(ImTypesetDocument& d, DocBlock& b, const ImTypesetDocumentContent& content,
              const ImTypesetBlock& in, int number, ErrorState& trouble)
{
    const auto note = [&](const char* what) {
        if (trouble.ok())
            trouble.set(ImTypesetError_DescriptionCut,
                        "block " + std::to_string(number) + ": " + what);
    };
    const int size = content.Text != nullptr && content.TextSize > 0 ? content.TextSize : 0;
    const auto stretch = [&](int start, int end, const char* what, int* out_start) {
        int s = std::min(std::max(start, 0), size);
        int e = std::min(end, size);
        if (start < 0 || end > size || end < start)
            note(what);
        if (e < s)
            e = s;
        *out_start = s;
        return static_cast<size_t>(e - s);
    };

    b = DocBlock();
    b.kind = in.Kind >= ImTypesetBlockKind_Paragraph && in.Kind <= ImTypesetBlockKind_Table
                 ? in.Kind
                 : ImTypesetBlockKind_Paragraph;
    if (b.kind != in.Kind)
        note("its Kind is not a block kind; it is set as a paragraph");
    if (b.kind == ImTypesetBlockKind_Table)
        note("tables are not laid out yet; the block is set as an empty host block");
    b.flags = in.Flags & (ImTypesetBlockFlags_HardBreaks | ImTypesetBlockFlags_Math);
    b.level = in.Level;
    b.item = in.Item;
    b.quote_depth = std::min(std::max(in.QuoteDepth, 0), static_cast<int>(MAX_QUOTE_DEPTH));
    b.left_indent = in.LeftIndent;
    b.extent = in.Extent;
    b.style = -1;
    if (!d.styles.empty()) {
        if (in.Style >= 0 && static_cast<size_t>(in.Style) < d.styles.size())
            b.style = in.Style;
        else
            note("its Style is not an index of the styles; the document's style is used");
    }
    b.container = -1;
    if (in.Container != 0) {
        if (in.Container >= 1 && static_cast<size_t>(in.Container) <= d.containers.size())
            b.container = in.Container - 1;
        else
            note("its Container is not one of the containers; it is in none");
    }

    int text_start = 0;
    const size_t text_bytes =
        stretch(in.TextStart, in.TextEnd, "its text is not inside the text given", &text_start);
    const char* text = size > 0 ? content.Text + text_start : "";
    const ImTypesetRun* runs = nullptr;
    int run_count = 0;
    if (is_text(b.kind) && in.RunCount > 0) {
        if (content.Runs != nullptr && in.RunStart >= 0 && in.RunStart <= content.RunCount &&
            in.RunCount <= content.RunCount - in.RunStart) {
            runs = content.Runs + in.RunStart;
            run_count = in.RunCount;
        } else {
            note("its runs are not inside the runs given; it is set without them");
        }
    }
    if (is_text(b.kind)) {
        // The paragraph's own validation, in the scratch paragraph.
        ImTypesetParagraph& w = d.work;
        set_text(w, text, text_bytes, runs, run_count, b.flags);
        b.text.swap(w.text);
        b.runs.swap(w.runs);
        b.text_error = w.text_error;
        w.text.clear();
        w.runs.clear();
    } else if (b.kind != ImTypesetBlockKind_Rule && b.kind != ImTypesetBlockKind_Table) {
        const size_t kept = cut_at_character(text, text_bytes, static_cast<size_t>(MAX_TEXT_BYTES));
        if (kept < text_bytes)
            b.text_error.set(ImTypesetError_TextTooLong,
                             "the text is longer than 16 MiB and was cut there",
                             static_cast<int>(kept));
        b.text.assign(text, kept);
    }
    // What was found wrong with the text as it was set (runs out of order, a
    // text past the limit) is the block's from here on, and the edit's.
    if (trouble.ok() && !b.text_error.ok())
        trouble.set(b.text_error.code,
                    "block " + std::to_string(number) + ": " + b.text_error.message,
                    b.text_error.byte_pos, b.text_error.codepoint_pos);
    b.error = b.text_error;
    if (is_code(b.kind))
        measure_code(b);

    if (b.kind != ImTypesetBlockKind_Rule && !is_host(b.kind)) {
        int marker_start = 0;
        const size_t marker_bytes = stretch(
            in.MarkerStart, in.MarkerEnd, "its marker is not inside the text given", &marker_start);
        const char* marker = size > 0 ? content.Text + marker_start : "";
        const size_t kept = cut_at_character(marker, marker_bytes, MAX_MARKER_BYTES);
        if (kept < marker_bytes)
            note("its marker is longer than 256 bytes and was cut there");
        b.marker.assign(marker, kept);
    }
}

// What a block's description comes to against the document's style.
BlockSpec resolve_spec(ImTypesetDocument& d, const DocBlock& b)
{
    ImTypesetContext& ctx = *d.ctx;
    const ResolvedStyle& base = d.style;
    const ImTypesetBlockStyle* own = b.style >= 0 && static_cast<size_t>(b.style) < d.styles.size()
                                         ? &d.styles[static_cast<size_t>(b.style)]
                                         : nullptr;
    BlockSpec spec;
    spec.type =
        is_code(b.kind) ? BlockType::CODE : (is_text(b.kind) ? BlockType::TEXT : BlockType::RULE);

    Face face = is_code(b.kind) ? Face::CODE : base.face;
    if (own != nullptr && role_of(own->Face) >= 0)
        face = static_cast<Face>(role_of(own->Face));
    // Code set in a face nothing was added for is set in the document's.
    if (is_code(b.kind) && !usable(ctx, static_cast<int>(face)))
        face = base.face;
    Scaled em = base.em;
    if (own != nullptr && own->Scale > 0.0f)
        em = clamped_sp(
            round_to_scaled(static_cast<double>(base.em) *
                            clamped(decimal_of(own->Scale), MIN_RUN_SCALE, MAX_RUN_SCALE, 1.0)),
            1, MAX_EM);
    spec.font.face = face;
    spec.font.size = em;
    spec.marker_font = spec.font;
    if (own != nullptr && role_of(own->MarkerFace) >= 0 && usable(ctx, role_of(own->MarkerFace)))
        spec.marker_font.face = static_cast<Face>(role_of(own->MarkerFace));

    Scaled baselineskip = base.baselineskip;
    Scaled lineskip = base.lineskip;
    if (em != base.em || (own != nullptr && own->LineHeight > 0.0f)) {
        const double ratio = line_height_of(ctx, own != nullptr && own->LineHeight > 0.0f
                                                     ? own->LineHeight
                                                     : d.given_style.LineHeight);
        baselineskip = clamped_sp(round_to_scaled(static_cast<double>(em) * ratio), 1, MAX_DIMEN);
        lineskip = clamped_sp(round_to_scaled(static_cast<double>(em) * 0.1), 0, MAX_DIMEN);
    }
    spec.interline.baselineskip.width = baselineskip;
    spec.interline.lineskip.width = lineskip;
    spec.strut = Strut::filling(baselineskip);

    spec.align = own != nullptr ? align_of(own->Align) : base.align;
    spec.left_indent = clamp_dimen(base.left_indent + length_sp(d, b.left_indent));
    spec.first_line_indent =
        own != nullptr ? length_sp(d, own->FirstLineIndent) : base.first_line_indent;
    // The reference narrows the first line by the indent as well as putting
    // the box there (docs/divergences.md); the product does not.
    spec.first_line =
        d.reference_rules ? FirstLineIndent::BOX_AND_SHAPE : FirstLineIndent::BOX_ONLY;
    spec.marker_gap =
        own != nullptr && own->MarkerGap > 0.0f ? length_sp(d, own->MarkerGap) : em / 2;
    if (own != nullptr) {
        spec.space_above = length_sp(d, own->SpaceAbove);
        spec.space_below = length_sp(d, own->SpaceBelow);
    }
    if (b.kind == ImTypesetBlockKind_Rule)
        spec.extent = b.extent > 0.0f ? length_sp(d, b.extent) : length_sp(d, 1.0f);
    else if (b.kind == ImTypesetBlockKind_Host)
        spec.extent = length_sp(d, b.extent);
    spec.quote_depth = static_cast<uint32_t>(b.quote_depth);
    return spec;
}

// Writes block `i`'s description into the block layer. Its layout is stale
// from here on and its material is made again when it is next laid out.
void apply_spec(ImTypesetDocument& d, size_t i)
{
    ImTypesetContext& ctx = *d.ctx;
    DocBlock& b = d.blocks[i];
    const BlockSpec spec = resolve_spec(d, b);

    const ImTypesetBlockStyle* own = b.style >= 0 && static_cast<size_t>(b.style) < d.styles.size()
                                         ? &d.styles[static_cast<size_t>(b.style)]
                                         : nullptr;
    b.colour = own != nullptr ? own->Color : 0;
    b.marker_colour = own != nullptr ? own->MarkerColor : 0;
    b.font = spec.font;
    b.align = spec.align;
    b.first_line_indent = spec.first_line_indent;
    // Not resolved until the block layer has all of it: see the end.
    b.has_spec = false;
    b.has_material = false;
    d.dead_boxes += b.atom_boxes;
    d.live_boxes -= std::min<size_t>(d.live_boxes, b.atom_boxes);
    b.atom_boxes = 0;

    Block& block = d.doc.edit(i);
    block.clear_content();
    block.type = spec.type;
    block.style = BlockStyle();
    block.style.align = spec.align;
    block.style.left_indent = spec.left_indent;
    block.style.first_line_indent = spec.first_line_indent;
    block.style.first_line = spec.first_line;
    block.style.marker_gap = spec.marker_gap;
    block.style.interline = spec.interline;
    block.style.strut = spec.strut;
    block.style.ragged_total_fit = d.style.line_break == ImTypesetLineBreak_TotalFit;
    block.space_above = spec.space_above;
    block.space_below = spec.space_below;
    block.quote_depth = spec.quote_depth;
    block.quote = NO_CONTAINER;
    block.list = NO_CONTAINER;
    if (b.container >= 0 && static_cast<size_t>(b.container) < d.container_quote.size()) {
        block.quote = d.container_quote[static_cast<size_t>(b.container)];
        block.list = d.container_list[static_cast<size_t>(b.container)];
    }
    block.item = static_cast<uint32_t>(std::max(b.item, 0));
    block.thickness = spec.extent;
    block.length_hint = length_hint(b);
    if (!b.marker.empty() && usable(ctx, static_cast<int>(spec.marker_font.face))) {
        std::string& why = ctx.scratch;
        why.clear();
        set_marker(block.text, b.marker, spec.marker_font, composition_of(d), ctx.resolve, &why);
    }
    b.has_spec = true;
}

// The rules, the theme and the breaking parameters every block is laid out
// under, from the options and the style.
void apply_rules(ImTypesetDocument& d)
{
    ImTypesetContext& ctx = *d.ctx;
    DocumentTheme theme;
    // A default DocumentRules is the reference's behaviour throughout.
    DocumentRules rules;
    if (!d.reference_rules) {
        rules = DocumentRules::product(d.style.em);
        rules.code_overflow =
            d.options.CodeOverflow == ImTypesetCodeOverflow_Clip
                ? CodeOverflow::CLIP
                : (d.options.CodeOverflow == ImTypesetCodeOverflow_Grow ? CodeOverflow::GROW
                                                                        : CodeOverflow::WRAP);
    }
    theme.quote_indent =
        d.options.QuoteIndent > 0.0f ? length_sp(d, d.options.QuoteIndent) : d.style.em;
    theme.quote_rule_thickness = d.options.QuoteBarThickness > 0.0f
                                     ? length_sp(d, d.options.QuoteBarThickness)
                                     : std::max(d.style.em / 8, length_sp(d, 1.0f));
    d.doc.set_rules(rules);
    d.doc.set_theme(theme);
    LineBreakParams params = prose_breaking();
    params.rules = ctx.tex_rules;
    if (d.style.line_break == ImTypesetLineBreak_FirstFit)
        params.first_fit = true;
    d.composer.params = params;
}

// DocumentComposer::materialize: block `number` is about to be laid out.
void materialize(void* user, size_t number, Block& block)
{
    ImTypesetDocument& d = *static_cast<ImTypesetDocument*>(user);
    if (number >= d.blocks.size())
        return;
    DocBlock& b = d.blocks[number];
    if (b.has_material)
        return;
    ImTypesetContext& ctx = *d.ctx;
    if (is_text(b.kind) || is_code(b.kind))
        ++d.stats.MaterialBuilds;
    b.has_material = true;
    b.fallback = false;
    b.display_block = false;
    b.error = b.text_error;
    if (is_code(b.kind)) {
        // Not taken for built if the build is unwound through.
        Building building(b.has_material);
        std::string& why = ctx.scratch;
        why.clear();
        // The byte each node came from is found by the walk that makes the
        // node, so the two cannot disagree about what a character is.
        if (!set_code(block.code, b.text, b.font, composition_of(d), ctx.resolve, &why,
                      &b.source_offsets) ||
            !usable(ctx, static_cast<int>(b.font.face))) {
            block.code.clear();
            b.source_offsets.clear();
            b.fallback = true;
            if (b.error.ok())
                b.error.set(ImTypesetError_NoFont, std::string("no font has been added for ") +
                                                       public_face(static_cast<int>(b.font.face)));
        }
        building.finished = true;
        return;
    }
    if (!is_text(b.kind))
        return;

    ImTypesetParagraph& w = d.work;
    w.ctx = &ctx;
    // The block's text and runs are lent to the scratch paragraph for the
    // build and come back whichever way the build is left: a host can unwind
    // through it (an allocation that fails where exceptions are enabled), and
    // the block must not lose its text to that, nor be taken for built. The
    // document's arena is taken back to where the build found it: boxes of a
    // formula that was never finished are nobody's, and a build cut short can
    // leave the arena's tables out of step until it is.
    struct Lent {
        DocBlock& block;
        ImTypesetParagraph& work;
        NodeArena& arena;
        const NodeArena::Mark before;
        bool finished = false;
        Lent(DocBlock& b_, ImTypesetParagraph& w_, NodeArena& arena_)
            : block(b_)
            , work(w_)
            , arena(arena_)
            , before(arena_.mark())
        {
            work.text.swap(block.text);
            work.runs.swap(block.runs);
        }
        ~Lent()
        {
            work.text.swap(block.text);
            work.runs.swap(block.runs);
            if (!finished) {
                block.has_material = false;
                arena.rewind(before);
            }
        }
        Lent(const Lent&) = delete;
        Lent& operator=(const Lent&) = delete;
    } lent(b, w, d.doc.arena());
    w.text_error = b.text_error;
    w.flags = b.flags;
    w.material_arena = &d.doc.arena();
    ++w.text_version;
    ResolvedStyle style = d.style;
    style.face = b.font.face;
    style.em = b.font.size;
    style.align = b.align;

    build_material_now(ctx, w, style);
    const NodeArena::Mark after = d.doc.arena().mark();
    b.atom_boxes = after.boxes - lent.before.boxes;
    d.live_boxes += b.atom_boxes;

    block.text.run.nodes.swap(w.block.run.nodes);
    block.text.run.space_factor = w.block.run.space_factor;
    w.block.run.nodes.clear();
    b.source_offsets.swap(w.source_offsets);
    b.ink_spans.swap(w.ink_spans);
    b.atoms.swap(w.atoms);
    b.display_text.swap(w.display_text);
    b.display_map.swap(w.display_map);
    b.display_block = w.display_block;
    b.fallback = w.fallback;
    b.error = w.material_error;
    // A display formula is centred and not indented: the reference's
    // `_display_math` replaces those two values of the block's style.
    block.style.align = b.display_block ? Align::CENTER : b.align;
    block.style.first_line_indent = b.display_block ? 0 : b.first_line_indent;
    lent.finished = true;
}

// Formula boxes of replaced material stay in the arena until every block that
// has any is made again: done here, between layouts, when the dead outnumber
// the living.
void collect_boxes(ImTypesetDocument& d)
{
    if (d.dead_boxes < DEAD_BOXES_MIN || d.dead_boxes <= d.live_boxes)
        return;
    for (size_t i = 0; i < d.blocks.size(); ++i) {
        DocBlock& b = d.blocks[i];
        if (b.atom_boxes == 0)
            continue;
        b.atom_boxes = 0;
        b.has_material = false;
        if (b.has_spec)
            d.doc.edit_text(i).text.run.nodes.clear();
    }
    d.doc.arena().clear();
    d.dead_boxes = 0;
    d.live_boxes = 0;
}

// Takes the style and the width a layout is asked for; describes again what
// that makes stale.
void take_style(ImTypesetDocument& d, const ImTypesetStyle& given, float width)
{
    ImTypesetContext& ctx = *d.ctx;
    const ResolvedStyle resolved = resolve_style(ctx, given);
    const bool changed =
        !d.has_style || !same_style(resolved, d.style) || d.generation != ctx.library.generation();
    const bool rescaled = d.has_style && resolved.px_per_inch != d.style.px_per_inch;
    d.given_style = given;
    d.has_style = true;
    if (changed) {
        d.style = resolved;
        d.generation = ctx.library.generation();
        if (rescaled && !d.containers.empty()) {
            // The containers' lengths were converted at the old scale and the
            // block layer keeps no way to change one: declared afresh.
            const size_t count = d.blocks.size();
            d.doc.clear();
            d.doc.splice(0, 0, count);
            // What was wrong with a container was said when it was handed over.
            ErrorState said;
            declare_containers(d, 0, said);
            for (DocBlock& b : d.blocks)
                b.atom_boxes = 0;
            d.dead_boxes = 0;
            d.live_boxes = 0;
        }
        apply_rules(d);
        for (DocBlock& b : d.blocks)
            b.has_spec = false;
        d.specs_stale = true;
    }
    const Scaled w = resolve_width(width, d.style);
    d.width_px = width;
    if (w != d.width) {
        d.width = w;
    }
    d.doc.set_width(d.width);
}

// Everything a question about the document's layout needs first.
void ready(ImTypesetDocument& d)
{
    ImTypesetContext& ctx = *d.ctx;
    ++d.tick;
    if (!d.has_style || d.generation != ctx.library.generation())
        take_style(d, d.has_style ? d.given_style : ctx.style, d.width_px);
    d.composer.measure = ctx.measure();
    d.composer.materialize = &materialize;
    d.composer.materialize_user = &d;
    if (d.specs_stale) {
        for (size_t i = 0; i < d.blocks.size(); ++i) {
            if (!d.blocks[i].has_spec)
                apply_spec(d, i);
        }
        d.spec_visits += d.blocks.size();
        d.specs_stale = false;
    }
    collect_boxes(d);
}

// --- A block as it was laid out ---------------------------------------------------------

// Where each index of a code block's commands came from in its source. The
// characters of the lines are its glyphs in order; any glyphs before them are
// the marker's.
void map_code_indices(const DocBlock& b, const BlockLayout& layout, std::vector<uint32_t>& out,
                      uint32_t* marker_items)
{
    const uint32_t items = layout.item_count;
    out.assign(static_cast<size_t>(items) + 1, 0xFFFFFFFFu);
    out[items] = b.code_end;
    size_t glyphs = 0;
    for (const DrawCmd& cmd : layout.cmds)
        glyphs += cmd.is(CmdType::GLYPH) ? 1 : 0;
    const size_t nodes = b.source_offsets.size();
    const size_t lead = glyphs >= nodes ? glyphs - nodes : 0;
    size_t seen = 0;
    uint32_t marker_end = 0;
    for (const DrawCmd& cmd : layout.cmds) {
        if (!cmd.is(CmdType::GLYPH))
            continue;
        const size_t ordinal = seen++;
        if (ordinal < lead) {
            marker_end = std::max(marker_end, cmd.index + 1);
            continue;
        }
        if (ordinal - lead < nodes && cmd.index < items)
            out[cmd.index] = std::min(out[cmd.index], b.source_offsets[ordinal - lead]);
    }
    for (uint32_t index = items; index-- > 0;) {
        if (out[index] > out[index + 1])
            out[index] = out[index + 1];
    }
    if (marker_items != nullptr)
        *marker_items = marker_end;
}

// Brings what is kept beside a block's layout up to date with it.
void sync(ImTypesetDocument& d, size_t i)
{
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    if (b.seen == layout.serial)
        return;
    // Not up to date until all of it is: see the end. (No layout has serial 0.)
    b.seen = 0;
    index_lines(layout.cmds.data(), layout.cmds.size(), b.lines);
    b.inks.clear();
    b.marker_items = 0;
    if (is_text(b.kind)) {
        b.marker_items = layout.marker_items;
        ink_ranges(layout.shipped, b.ink_spans, d.inks_scratch);
    } else {
        d.inks_scratch.clear();
        if (is_code(b.kind) && !b.marker.empty()) {
            std::vector<uint32_t>& bytes = d.ctx->offsets;
            map_code_indices(b, layout, bytes, &b.marker_items);
        }
    }
    if (b.marker_colour != 0 && b.marker_items > 0)
        b.inks.push_back(InkRange{0, b.marker_items, b.marker_colour});
    for (InkRange ink : d.inks_scratch) {
        if (!b.inks.empty() && ink.start < b.inks.back().end)
            ink.start = b.inks.back().end;
        if (ink.start < ink.end)
            b.inks.push_back(ink);
    }
    if (!layout.ok() && b.error.ok())
        b.error.set(ImTypesetError_LayoutFailed,
                    "a character could not be measured; its line is set without it");
    b.seen = layout.serial;
}

// Lays out the unit a block is in. False if there is no such block.
bool layout_block(ImTypesetDocument& d, int block)
{
    if (block < 0 || static_cast<size_t>(block) >= d.blocks.size())
        return false;
    const size_t i = static_cast<size_t>(block);
    d.doc.prepare_if_needed(d.composer);
    const size_t u = d.doc.unit_of(i);
    d.doc.layout_unit(d.composer, u);
    sync(d, i);
    return d.doc.laid_out(i);
}

// The block as a paragraph: its commands, its index-to-byte map, and from
// those, on demand, its lines and its selection model. The block is laid out.
const ImTypesetParagraph& view_of(ImTypesetDocument& d, size_t i)
{
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    DocView* found = nullptr;
    DocView* spare = nullptr;
    for (const std::unique_ptr<DocView>& held : d.views) {
        if (held->block == static_cast<int>(i)) {
            found = held.get();
            break;
        }
        if (spare == nullptr || held->used < spare->used)
            spare = held.get();
    }
    if (found != nullptr && found->serial == layout.serial) {
        found->used = d.tick;
        return found->p;
    }
    if (found == nullptr) {
        const size_t limit = view_limit(d);
        if (spare == nullptr || (d.views.size() < limit && spare->block >= 0) ||
            spare->used == d.tick) {
            // Owned before it is listed: listing it can fail.
            std::unique_ptr<DocView> made(new DocView());
            d.views.push_back(std::move(made));
            spare = d.views.back().get();
        }
        found = spare;
    }
    found->block = static_cast<int>(i);
    found->serial = layout.serial;
    found->used = d.tick;

    ImTypesetParagraph& p = found->p;
    p.ctx = d.ctx;
    p.has_material = true;
    p.has_layout = true;
    p.fallback = false;
    p.has_lines = false;
    p.has_model = false;
    p.line_boxes.clear();
    p.layout_style = d.style;
    p.text.assign(b.text);
    p.cmds.assign(layout.cmds.begin(), layout.cmds.end());
    p.lines.spans.assign(b.lines.spans.begin(), b.lines.spans.end());
    p.lines.sorted = b.lines.sorted;
    p.layout.item_count = layout.item_count;
    p.layout.marker_items = layout.marker_items;
    p.layout.shipped.assign(layout.shipped.begin(), layout.shipped.end());
    p.layout.hard_breaks.assign(layout.hard_breaks.begin(), layout.hard_breaks.end());
    p.layout.originals.assign(layout.originals.begin(), layout.originals.end());
    p.atoms.assign(b.atoms.begin(), b.atoms.end());
    p.display_text.assign(b.display_text);
    p.display_block = b.display_block;
    if (is_text(b.kind)) {
        map_index_bytes(layout.shipped, b.source_offsets, layout.item_count,
                        static_cast<uint32_t>(b.text.size()), p.index_bytes);
    } else if (is_code(b.kind)) {
        map_code_indices(b, layout, p.index_bytes, nullptr);
    } else {
        p.index_bytes.assign(static_cast<size_t>(layout.item_count) + 1, 0);
    }
    return p;
}

// The index of the item at a byte of a block's text. In text an item the text
// did not contain (a discretionary's glue, the fill of a line) carries the
// byte of the character after it, so the character is the LAST item at its
// byte: the paragraph's rule. In code it is the other way about: every
// item after the marker is a character, and the several spaces a tab is set
// as all carry the tab's byte, so the tab begins at the FIRST item at its
// byte.
uint32_t index_of(const DocBlock& b, const ImTypesetParagraph& p, int byte_offset)
{
    if (!is_code(b.kind))
        return index_at_byte(p, byte_offset);
    const std::vector<uint32_t>& bytes = p.index_bytes;
    if (bytes.empty())
        return 0;
    const uint32_t wanted = byte_offset <= 0 ? 0u : static_cast<uint32_t>(byte_offset);
    const uint32_t last = static_cast<uint32_t>(bytes.size() - 1);
    const uint32_t found =
        static_cast<uint32_t>(std::lower_bound(bytes.begin(), bytes.end(), wanted) - bytes.begin());
    return std::min(std::max(found, std::min(b.marker_items, last)), last);
}

// The items a range of a block's text covers.
Selection span_of(const DocBlock& b, const ImTypesetParagraph& p, const ImTypesetTextRange& range)
{
    Selection out;
    if (range.Start >= range.End)
        return out;
    out.start = index_of(b, p, range.Start);
    out.end = index_of(b, p, range.End);
    if (out.end < out.start)
        out.end = out.start;
    return out;
}

// The part of a document range that is in block `i`, as bytes of its text.
// False if none of it is. `whole` is set when the block lies strictly inside
// the range or the range covers all its text.
bool local_range(const ImTypesetDocument& d, const ImTypesetDocumentRange& range, size_t i,
                 ImTypesetTextRange& out, bool* whole = nullptr)
{
    const int block = static_cast<int>(i);
    if (block < range.Start.Block || block > range.End.Block)
        return false;
    const int size = text_size_of(d.blocks[i]);
    int start = block == range.Start.Block ? range.Start.Offset : 0;
    int end = block == range.End.Block ? range.End.Offset : size;
    start = std::min(std::max(start, 0), size);
    end = std::min(std::max(end, 0), size);
    out.Start = start;
    out.End = end;
    const bool inside = block > range.Start.Block && block < range.End.Block;
    if (whole != nullptr)
        *whole = inside || (start == 0 && end == size && range.Start.Block != range.End.Block);
    return start < end || inside || (size == 0 && block < range.End.Block);
}

bool before(const ImTypesetPosition& a, const ImTypesetPosition& b)
{
    return a.Block < b.Block || (a.Block == b.Block && a.Offset < b.Offset);
}

// A range in order and inside the document; empty if it was empty.
ImTypesetDocumentRange ordered(const ImTypesetDocument& d, ImTypesetDocumentRange range)
{
    const int count = static_cast<int>(d.blocks.size());
    const ImTypesetDocumentRange none = {{0, 0}, {0, 0}};
    if (count == 0 || !before(range.Start, range.End))
        return none;
    if (range.Start.Block < 0)
        range.Start = ImTypesetPosition{0, 0};
    if (range.End.Block >= count)
        range.End = ImTypesetPosition{count - 1, text_size_of(d.blocks.back())};
    if (range.Start.Block >= count || range.End.Block < 0 || !before(range.Start, range.End))
        return none;
    return range;
}

// Gives up the views used longest ago beyond what the document keeps.
void trim_views(ImTypesetDocument& d)
{
    const size_t limit = view_limit(d);
    if (d.views.size() <= limit)
        return;
    std::nth_element(d.views.begin(), d.views.begin() + static_cast<std::ptrdiff_t>(limit),
                     d.views.end(),
                     [](const std::unique_ptr<DocView>& a, const std::unique_ptr<DocView>& b) {
                         return a->used > b->used;
                     });
    d.views.resize(limit);
}

// --- Drawing ----------------------------------------------------------------------------

struct Target {
    ImDrawList* list = nullptr;
    DrawBuffers* buffers = nullptr;
    ImVec4 clip = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
};

void emit(ImTypesetContext& ctx, const Target& target, const DrawRun& run, const DrawArgs& args)
{
    if (target.list != nullptr)
        ctx.drawer.draw_runs(target.list, &run, 1, args, ctx.fonts, ctx.resolve);
    else
        ctx.drawer.draw_runs(*target.buffers, target.clip, &run, 1, args, ctx.fonts, ctx.resolve);
}

// Puts `over` on top of the ranges of `inks`, which are in order and do not
// overlap, and leaves them so.
void overlay(std::vector<InkRange>& inks, InkRange over, std::vector<InkRange>& scratch)
{
    if (over.start >= over.end)
        return;
    scratch.clear();
    bool placed = false;
    for (const InkRange& ink : inks) {
        if (ink.end <= over.start) {
            scratch.push_back(ink);
            continue;
        }
        if (!placed) {
            if (ink.start < over.start)
                scratch.push_back(InkRange{ink.start, over.start, ink.colour});
            scratch.push_back(over);
            placed = true;
        }
        if (ink.end > over.end)
            scratch.push_back(InkRange{std::max(ink.start, over.end), ink.end, ink.colour});
    }
    if (!placed)
        scratch.push_back(over);
    inks.swap(scratch);
}

ImU32 shown(ImU32 colour)
{
    return ImGui::GetColorU32(colour);
}

// One block, with its top-left corner at (`left`, `top`) on the screen.
void draw_block(ImTypesetDocument& d, size_t i, double left, double top, const Target& target,
                const ImTypesetDocumentDraw* draw, const ImTypesetDocumentRange& selection,
                DrawArgs& args)
{
    ImTypesetContext& ctx = *d.ctx;
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    ++d.stats.BlocksDrawn;
    // A view is used for one block at a time here: the one before may be reused.
    ++d.tick;

    const ImU32 ink = b.colour != 0
                          ? shown(b.colour)
                          : (d.given_style.Color != 0 ? shown(d.given_style.Color)
                                                      : ImGui::GetColorU32(ImGuiCol_Text));
    args.origin = ImVec2(static_cast<float>(left), static_cast<float>(top));
    args.palette.rule = ink;
    if (b.kind == ImTypesetBlockKind_Rule)
        args.palette.rule = d.options.RuleColor != 0 ? shown(d.options.RuleColor)
                                                     : ImGui::GetColorU32(ImGuiCol_Separator);
    args.fills = nullptr;
    args.fill_count = 0;
    args.highlights = nullptr;
    args.highlight_count = 0;

    d.fills.clear();
    d.highlights.clear();
    d.inks.clear();
    bool own_inks = false;
    const bool alpha = ImGui::GetStyle().Alpha < 1.0f;
    if (alpha && !b.inks.empty()) {
        d.inks.assign(b.inks.begin(), b.inks.end());
        for (InkRange& range : d.inks)
            range.colour = shown(range.colour);
        own_inks = true;
    }

    const bool textual = is_text(b.kind) || is_code(b.kind);
    ImTypesetTextRange local = {0, 0};
    bool whole = false;
    if (local_range(d, selection, i, local, &whole)) {
        if (textual && local.Start < local.End) {
            const ImTypesetParagraph& view = view_of(d, i);
            ensure_model(view);
            view.model.rects(span_of(b, view, local), d.highlights);
        } else if (is_host(b.kind) && (whole || local.Start < local.End)) {
            // A host block in a selection is selected whole: its rectangle.
            const Scaled x = d.doc.block(i).style.left_indent;
            d.highlights.push_back(Rect{x, 0, std::max<Scaled>(d.width - x, 0), layout.extent});
        }
    }
    if (draw != nullptr && draw->Marks != nullptr && textual) {
        for (int m = 0; m < draw->MarkCount; ++m) {
            const ImTypesetMark& mark = draw->Marks[m];
            if (static_cast<int>(i) < mark.Range.Start.Block ||
                static_cast<int>(i) > mark.Range.End.Block ||
                !before(mark.Range.Start, mark.Range.End))
                continue;
            if (!local_range(d, mark.Range, i, local) || local.Start >= local.End)
                continue;
            const ImTypesetParagraph& view = view_of(d, i);
            const Selection span = span_of(b, view, local);
            if (span.is_empty())
                continue;
            if (mark.Place == ImTypesetMarkPlace_Ink) {
                if (!own_inks) {
                    d.inks.assign(b.inks.begin(), b.inks.end());
                    own_inks = true;
                }
                overlay(d.inks, InkRange{span.start, span.end, shown(mark.Color)}, d.inks_scratch);
                continue;
            }
            ensure_model(view);
            ctx.rects.clear();
            view.model.rects(span, ctx.rects);
            if (mark.Place == ImTypesetMarkPlace_Behind) {
                for (const Rect& rect : ctx.rects)
                    d.fills.push_back(Fill{rect, shown(mark.Color)});
                continue;
            }
            // Under: a bar below the baseline of each line the range is on.
            ensure_lines(view);
            const Scaled em = b.font.size;
            const Scaled thickness = mark.Thickness > 0.0f ? length_sp(d, mark.Thickness)
                                                           : std::max(em / 16, length_sp(d, 1.0f));
            const Scaled offset = mark.Offset > 0.0f ? length_sp(d, mark.Offset) : 2 * thickness;
            for (const Rect& rect : ctx.rects) {
                const Scaled middle = rect.y + rect.height / 2;
                const auto line =
                    std::partition_point(view.line_boxes.begin(), view.line_boxes.end(),
                                         [&](const LineBox& box) { return box.bottom <= middle; });
                const Scaled baseline =
                    line != view.line_boxes.end() ? line->baseline : rect.y + rect.height;
                d.fills.push_back(Fill{Rect{rect.x, baseline + offset, rect.width, thickness},
                                       shown(mark.Color)});
            }
        }
    }

    args.inks = own_inks ? d.inks.data() : b.inks.data();
    args.ink_count = own_inks ? d.inks.size() : b.inks.size();
    if (!d.fills.empty()) {
        args.fills = d.fills.data();
        args.fill_count = d.fills.size();
    }
    if (!d.highlights.empty()) {
        args.highlights = d.highlights.data();
        args.highlight_count = d.highlights.size();
    }

    DrawRun run;
    run.ink = ink;
    if (is_host(b.kind)) {
        // Nothing of its own to draw: the rule that gives it its height is not shown.
        if (d.highlights.empty())
            return;
        run.cmds = nullptr;
        run.count = 0;
        emit(ctx, target, run, args);
        return;
    }
    run.cmds = layout.cmds.data();
    run.count = layout.cmds.size();
    run.lines = &b.lines;
    // The clip rectangle is popped however the draw is left: a host can
    // unwind through it, and its draw list must not keep a rectangle of ours.
    struct Clip {
        ImDrawList* list = nullptr;
        ~Clip()
        {
            if (list != nullptr)
                IMTYPESET_NO_UNWIND(list->PopClipRect());
        }
    } clipped;
    if (layout.clipped && target.list != nullptr) {
        const Scaled x = d.doc.block(i).style.left_indent;
        target.list->PushClipRect(
            ImVec2(static_cast<float>(left + px_of(d, x)), static_cast<float>(top)),
            ImVec2(static_cast<float>(left + px_of(d, d.width)),
                   static_cast<float>(top + px_of(d, layout.extent))),
            true);
        clipped.list = target.list;
    }
    emit(ctx, target, run, args);
}

// Why a document cannot be drawn now, as a failure; true if it can.
bool can_draw(ImTypesetDocument* document)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    ImTypesetContext& ctx = *d->ctx;
    if (!bound(ctx))
        return not_bound();
    if (!backend_has_textures())
        return no_textures();
    validate_fonts(ctx);
    return true;
}

// What in view could not be set as asked: the first such block's error.
bool draw_document(ImTypesetDocument& d, const Target& target, const ImVec2& pos,
                   const ImVec4& clip, const ImTypesetDocumentDraw* draw)
{
    ImTypesetContext& ctx = *d.ctx;
    ready(d);
    const double left = is_finite(pos.x) ? static_cast<double>(pos.x) : 0.0;
    const double top = is_finite(pos.y) ? static_cast<double>(pos.y) : 0.0;
    const Scaled window_top =
        coordinate_sp(d, static_cast<double>(clip.y) - top - WINDOW_MARGIN_PX);
    const Scaled window_bottom =
        coordinate_sp(d, static_cast<double>(clip.w) - top + WINDOW_MARGIN_PX);
    if (!(clip.w > clip.y) || !(clip.z > clip.x) || d.blocks.empty())
        return succeed();

    const UnitRange range = layout_window(d, window_top, window_bottom);
    const ImTypesetDocumentRange selection =
        draw != nullptr ? ordered(d, draw->Selection) : ImTypesetDocumentRange{{0, 0}, {0, 0}};

    DrawArgs args;
    args.px_per_inch = d.style.px_per_inch;
    args.rules = ctx.draw_rules;
    args.cull_lines = true;
    args.debug = draw != nullptr && (draw->Flags & ImTypesetDocumentDrawFlags_Debug) != 0;
    args.palette.highlight = d.given_style.SelectionColor != 0
                                 ? shown(d.given_style.SelectionColor)
                                 : ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    const ErrorState* trouble = nullptr;
    size_t blocks = 0;
    Scaled y = range.first < range.end ? d.doc.unit_top(range.first) : 0;
    for (size_t u = range.first; u < range.end; ++u) {
        const DocumentUnit& unit = d.doc.unit(u);
        const double unit_top = top + px_of(d, y);
        for (uint32_t k = 0; k < unit.bar_count; ++k) {
            const Bar& bar = d.doc.bar(unit.first_bar + k);
            if (!(bar.extent > 0 && bar.thickness > 0))
                continue;
            d.bar_cmds.clear();
            d.bar_cmds.push_back(DrawCmd::rule_rect(bar.x, bar.top, bar.thickness, bar.extent));
            ImU32 colour = d.options.QuoteBarColor;
            if (bar.quote >= 0) {
                const Structure structure = d.doc.structure();
                if (static_cast<size_t>(bar.quote) < structure.quote_count &&
                    structure.quotes[bar.quote].ink != 0)
                    colour = structure.quotes[bar.quote].ink;
            }
            DrawArgs bar_args;
            bar_args.px_per_inch = args.px_per_inch;
            bar_args.rules = args.rules;
            bar_args.origin = ImVec2(static_cast<float>(left), static_cast<float>(unit_top));
            bar_args.palette.rule =
                colour != 0 ? shown(colour) : ImGui::GetColorU32(ImGuiCol_Separator);
            DrawRun run;
            run.cmds = d.bar_cmds.data();
            run.count = d.bar_cmds.size();
            run.ink = bar_args.palette.rule;
            emit(ctx, target, run, bar_args);
        }
        for (size_t i = unit.first_block; i < unit.first_block + unit.block_count; ++i) {
            sync(d, i);
            ++blocks;
            if (trouble == nullptr && !d.blocks[i].error.ok())
                trouble = &d.blocks[i].error;
            draw_block(d, i, left, unit_top + px_of(d, d.doc.top_in_unit(i)), target, draw,
                       selection, args);
        }
        y += unit.extent;
        if (u + 1 < range.end)
            y += d.doc.unit(u + 1).gap;
    }
    d.shown_blocks = blocks;
    trim_views(d);
    ++ctx.stats.DrawCalls;
    return trouble != nullptr ? report(*trouble) : succeed();
}

// --- Questions ----------------------------------------------------------------------------

// The block a height falls in, laid out; -1 in an empty document. `inside`
// says whether the height is between the block's top and bottom, and
// `block_top` is where the block starts.
int block_at(ImTypesetDocument& d, Scaled y, bool* inside, Scaled* block_top)
{
    *inside = false;
    *block_top = 0;
    if (d.blocks.empty())
        return -1;
    const UnitRange range = d.doc.layout_window(d.composer, y, y + 1);
    size_t u = range.first;
    if (u >= d.doc.unit_count())
        u = d.doc.unit_count() - 1;
    d.doc.layout_unit(d.composer, u);
    const DocumentUnit& unit = d.doc.unit(u);
    const Scaled unit_top = d.doc.unit_top(u);
    // The first block of the unit whose bottom is below the point, or its last.
    size_t found = unit.first_block + unit.block_count - 1;
    for (size_t i = unit.first_block; i < unit.first_block + unit.block_count; ++i) {
        const Scaled bottom = unit_top + d.doc.top_in_unit(i) + d.doc.layout(i).extent;
        if (y < bottom) {
            found = i;
            break;
        }
    }
    sync(d, found);
    *block_top = unit_top + d.doc.top_in_unit(found);
    *inside = y >= *block_top && y < *block_top + d.doc.layout(found).extent;
    return static_cast<int>(found);
}

struct Probe {
    int block = -1;
    bool in_block = false;
    Point local;
};

Probe probe(ImTypesetDocument& d, const ImVec2& local_pos)
{
    Probe out;
    const Scaled x = coordinate_sp(d, is_finite(local_pos.x) ? local_pos.x : 0.0);
    const Scaled y = coordinate_sp(d, is_finite(local_pos.y) ? local_pos.y : 0.0);
    Scaled top = 0;
    out.block = block_at(d, y, &out.in_block, &top);
    out.local.x = x;
    out.local.y = y - top;
    return out;
}

ImTypesetDocumentHit hit_test(ImTypesetDocument& d, const ImVec2& local_pos)
{
    ImTypesetDocumentHit out = {{0, 0}, -1, 0, false, false};
    const Probe at = probe(d, local_pos);
    if (at.block < 0)
        return out;
    const size_t i = static_cast<size_t>(at.block);
    const DocBlock& b = d.blocks[i];
    out.Position.Block = at.block;
    out.InBlock = at.in_block;
    if (!is_text(b.kind) && !is_code(b.kind)) {
        out.Position.Offset = 2 * at.local.y < d.doc.layout(i).extent ? 0 : text_size_of(b);
        return out;
    }
    const ImTypesetParagraph& p = view_of(d, i);
    ensure_lines(p);
    if (p.line_boxes.empty())
        return out;
    ensure_model(p);
    const uint32_t index = p.model.hit(at.local);
    out.Position.Offset = std::min(byte_at_index(p, index), text_size_of(b));
    out.Line = static_cast<int>(line_of_index(p, index) - p.line_boxes.begin());
    const auto under =
        std::partition_point(p.line_boxes.begin(), p.line_boxes.end(),
                             [&](const LineBox& box) { return box.bottom <= at.local.y; });
    out.Inside = under != p.line_boxes.end() && at.local.y >= under->top &&
                 at.local.x >= under->x0 && at.local.x < under->x1;
    if (out.Inside) {
        // The run the character under the point is in.
        const uint32_t byte = static_cast<uint32_t>(out.Position.Offset);
        uint32_t under_byte = byte;
        // The caret may be after the character the point is on: the item
        // whose box holds the point is the one at or before the caret.
        for (uint32_t c = under->first + 1; c < under->end; ++c) {
            const DrawCmd& cmd = p.cmds[c];
            if (!is_item(cmd))
                continue;
            if (cmd.x <= at.local.x && at.local.x < cmd.x + cmd.width) {
                under_byte = static_cast<uint32_t>(byte_at_index(p, cmd.index));
                break;
            }
        }
        const auto run =
            std::upper_bound(b.runs.begin(), b.runs.end(), under_byte,
                             [](uint32_t wanted, const StyledRun& r) { return wanted < r.end; });
        if (run != b.runs.end() && run->start <= under_byte)
            out.Tag = run->tag;
    }
    return out;
}

// What a word or a line under a point covers, as bytes of the block's text.
ImTypesetTextRange unit_at(ImTypesetDocument& d, const Probe& at, Granularity granularity)
{
    ImTypesetTextRange out = {0, 0};
    const size_t i = static_cast<size_t>(at.block);
    const DocBlock& b = d.blocks[i];
    if (!is_text(b.kind) && !is_code(b.kind)) {
        out.End = text_size_of(b);
        return out;
    }
    const ImTypesetParagraph& p = view_of(d, i);
    ensure_model(p);
    const Selection found = p.model.select_at(at.local, at.local, granularity);
    out.Start = byte_at_index(p, found.start);
    out.End = byte_at_index(p, found.end);
    const int size = text_size_of(b);
    out.Start = std::min(out.Start, size);
    out.End = std::min(std::max(out.End, out.Start), size);
    return out;
}

bool too_many_blocks()
{
    return fail(ImTypesetError_InvalidArgument,
                "a document holds at most " + std::to_string(MAX_DOCUMENT_BLOCKS) + " blocks");
}

// Where a position is once blocks [at, at + removed) have been replaced by
// `incoming` blocks. Below the edit nothing moves; above it a block moves by
// the difference; a block replaced one for one keeps its number. A position
// in blocks that are gone is the nearest that is left: the start of a range
// moves to after what took their place, its end to before it.
ImTypesetPosition moved_by_edit(ImTypesetPosition position, size_t at, size_t removed,
                                size_t incoming, bool is_end)
{
    if (position.Block < 0 || static_cast<size_t>(position.Block) < at)
        return position;
    const size_t block = static_cast<size_t>(position.Block);
    if (block >= at + removed)
        return ImTypesetPosition{static_cast<int>(block - removed + incoming), position.Offset};
    if (incoming == removed)
        return position;
    return ImTypesetPosition{static_cast<int>(is_end ? at : at + incoming), 0};
}

void append_px(const ImTypesetDocument& d, const Rect& rect, Scaled top, ImVector<ImVec4>* out)
{
    out->push_back(ImVec4(pxf(d, rect.x), pxf(d, top + rect.y), pxf(d, rect.x + rect.width),
                          pxf(d, top + rect.y + rect.height)));
}

} // namespace

// --- src/context.h -------------------------------------------------------------------

namespace ImTypeset::detail
{

void destroy_documents(ImTypesetContext& ctx)
{
    for (ImTypesetDocument* d : ctx.documents)
        delete d;
    ctx.documents.clear();
}

Document& block_document(ImTypesetDocument& document)
{
    return document.doc;
}

DocumentComposer& block_composer(ImTypesetDocument& document)
{
    return document.composer;
}

} // namespace ImTypeset::detail

// --- include/imtypeset.h -------------------------------------------------------------

namespace ImTypeset
{

bool DebugCheckDocumentDataLayout(size_t sz_block, size_t sz_block_style, size_t sz_container,
                                  size_t sz_content, size_t sz_options, size_t sz_position,
                                  size_t sz_range, size_t sz_mark, size_t sz_draw, size_t sz_hit,
                                  size_t sz_geometry, size_t sz_stats)
{
    const struct {
        const char* name;
        size_t given;
        size_t own;
    } structs[] = {
        {"ImTypesetBlock", sz_block, sizeof(ImTypesetBlock)},
        {"ImTypesetBlockStyle", sz_block_style, sizeof(ImTypesetBlockStyle)},
        {"ImTypesetContainer", sz_container, sizeof(ImTypesetContainer)},
        {"ImTypesetDocumentContent", sz_content, sizeof(ImTypesetDocumentContent)},
        {"ImTypesetDocumentOptions", sz_options, sizeof(ImTypesetDocumentOptions)},
        {"ImTypesetPosition", sz_position, sizeof(ImTypesetPosition)},
        {"ImTypesetDocumentRange", sz_range, sizeof(ImTypesetDocumentRange)},
        {"ImTypesetMark", sz_mark, sizeof(ImTypesetMark)},
        {"ImTypesetDocumentDraw", sz_draw, sizeof(ImTypesetDocumentDraw)},
        {"ImTypesetDocumentHit", sz_hit, sizeof(ImTypesetDocumentHit)},
        {"ImTypesetBlockGeometry", sz_geometry, sizeof(ImTypesetBlockGeometry)},
        {"ImTypesetDocumentStats", sz_stats, sizeof(ImTypesetDocumentStats)},
    };
    std::string& mismatch = layout_mismatch();
    for (const auto& entry : structs) {
        if (entry.given == entry.own)
            continue;
        mismatch = std::string(entry.name) + " is " + std::to_string(entry.given) +
                   " bytes where IMTYPESET_CHECKVERSION() is called and " +
                   std::to_string(entry.own) +
                   " in the imtypeset library: the two were compiled against different "
                   "imtypeset.h";
        return fail(ImTypesetError_VersionMismatch, mismatch);
    }
    return succeed();
}

ImTypesetDocument* CreateDocument()
{
    ImTypesetContext* ctx = g_context;
    if (ctx == nullptr) {
        no_context();
        return nullptr;
    }
    std::unique_ptr<ImTypesetDocument> made(new ImTypesetDocument());
    made->ctx = ctx;
    made->slot = ctx->documents.size();
    ctx->documents.push_back(made.get());
    succeed();
    return made.release();
}

void DestroyDocument(ImTypesetDocument* document)
{
    if (document == nullptr || document->ctx == nullptr)
        return;
    std::vector<ImTypesetDocument*>& all = document->ctx->documents;
    if (document->slot >= all.size() || all[document->slot] != document)
        return;
    all[document->slot] = all.back();
    all[document->slot]->slot = document->slot;
    all.pop_back();
    delete document;
}

bool ReplaceDocumentBlocks(ImTypesetDocument* document, int first, int count,
                           const ImTypesetDocumentContent* content)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    static const ImTypesetDocumentContent nothing;
    const ImTypesetDocumentContent& given = content != nullptr ? *content : nothing;
    const size_t held = d->blocks.size();
    if (first < 0 || count < 0 || static_cast<size_t>(first) > held ||
        static_cast<size_t>(count) > held - static_cast<size_t>(first))
        return fail(ImTypesetError_InvalidArgument,
                    "the blocks to replace are not blocks of the document");
    const size_t incoming =
        given.Blocks != nullptr && given.BlockCount > 0 ? static_cast<size_t>(given.BlockCount) : 0;
    const size_t at = static_cast<size_t>(first);
    const size_t removed = static_cast<size_t>(count);
    if (!d->doc.splice(at, removed, incoming))
        return too_many_blocks();

    for (size_t i = at; i < at + removed; ++i) {
        d->dead_boxes += d->blocks[i].atom_boxes;
        d->live_boxes -= std::min<size_t>(d->live_boxes, d->blocks[i].atom_boxes);
    }
    // The same rotation the block layer made, so that a block and what is
    // kept beside it stay together.
    if (incoming >= removed) {
        d->blocks.insert(d->blocks.begin() + static_cast<std::ptrdiff_t>(at + removed),
                         incoming - removed, DocBlock());
    } else {
        d->blocks.erase(d->blocks.begin() + static_cast<std::ptrdiff_t>(at + incoming),
                        d->blocks.begin() + static_cast<std::ptrdiff_t>(at + removed));
    }
    // A view names its block by number, and the numbers have moved.
    for (const std::unique_ptr<DocView>& view : d->views)
        view->block = -1;
    // So have those of the selection DocumentView() keeps.
    if (before(d->selection.Start, d->selection.End)) {
        d->selection.Start = moved_by_edit(d->selection.Start, at, removed, incoming, false);
        d->selection.End = moved_by_edit(d->selection.End, at, removed, incoming, true);
    }
    if (!before(d->selection.Start, d->selection.End))
        d->selection = ImTypesetDocumentRange{{0, 0}, {0, 0}};

    ErrorState trouble;
    const size_t containers_before = d->containers.size();
    if (given.Styles != nullptr && given.StyleCount > 0)
        d->styles.insert(d->styles.end(), given.Styles, given.Styles + given.StyleCount);
    if (given.Containers != nullptr && given.ContainerCount > 0) {
        d->containers.insert(d->containers.end(), given.Containers,
                             given.Containers + given.ContainerCount);
        declare_containers(*d, containers_before, trouble);
    }

    for (size_t k = 0; k < incoming; ++k) {
        describe(*d, d->blocks[at + k], given, given.Blocks[k], static_cast<int>(at + k), trouble);
        if (d->has_style && !d->specs_stale)
            apply_spec(*d, at + k);
    }
    if (!d->has_style)
        d->specs_stale = true;
    return report(trouble);
}

bool SetDocumentBlocks(ImTypesetDocument* document, const ImTypesetDocumentContent* content)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    // Refused before anything is given up: the one way the replacement below
    // can fail.
    if (content != nullptr && content->Blocks != nullptr && content->BlockCount > 0 &&
        static_cast<size_t>(content->BlockCount) > MAX_DOCUMENT_BLOCKS)
        return too_many_blocks();
    d->doc.clear();
    d->blocks.clear();
    d->styles.clear();
    d->containers.clear();
    d->container_quote.clear();
    d->container_list.clear();
    d->dead_boxes = 0;
    d->live_boxes = 0;
    d->selection = ImTypesetDocumentRange{{0, 0}, {0, 0}};
    return ReplaceDocumentBlocks(document, 0, 0, content);
}

bool AppendDocumentText(ImTypesetDocument* document, const char* text, const char* text_end,
                        const ImTypesetRun* runs, int runs_count)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    if (d->blocks.empty())
        return fail(ImTypesetError_InvalidArgument, "the document has no block to append to");
    const size_t i = d->blocks.size() - 1;
    DocBlock& b = d->blocks[i];
    if (!is_text(b.kind) && !is_code(b.kind))
        return fail(ImTypesetError_InvalidArgument, "the last block is not one that holds text");
    // What THIS call found wrong, which is what it answers: what an earlier
    // call found is the block's (GetDocumentBlockError) and was said then.
    ErrorState trouble;
    const size_t given = text_size(text, text_end);
    const size_t room = b.text.size() < static_cast<size_t>(MAX_TEXT_BYTES)
                            ? static_cast<size_t>(MAX_TEXT_BYTES) - b.text.size()
                            : 0;
    const size_t size = cut_at_character(text, given, room);
    if (size < given)
        trouble.set(ImTypesetError_TextTooLong, "the text is longer than 16 MiB and was cut there",
                    static_cast<int>(b.text.size() + size));
    if (size > 0)
        b.text.append(text, size);
    if (is_text(b.kind)) {
        ErrorState run_trouble;
        validate_runs(b.runs, runs, runs_count, b.text.size(),
                      b.runs.empty() ? 0 : b.runs.back().end, run_trouble);
        if (trouble.ok())
            trouble = run_trouble;
    }
    if (b.text_error.ok())
        b.text_error = trouble;
    const uint32_t lines_before = b.code_lines;
    if (is_code(b.kind))
        measure_code(b);
    // Whether a code block sets a box at all is part of what the gaps round
    // it were computed from.
    const bool appeared = is_code(b.kind) && (lines_before == 0) != (b.code_lines == 0);
    b.error = b.text_error;
    b.has_material = false;
    d->dead_boxes += b.atom_boxes;
    d->live_boxes -= std::min<size_t>(d->live_boxes, b.atom_boxes);
    b.atom_boxes = 0;
    if (b.has_spec) {
        Block& block = appeared ? d->doc.edit(i) : d->doc.edit_text(i);
        block.text.run.nodes.clear();
        block.code.clear();
        block.length_hint = length_hint(b);
    }
    return report(trouble);
}

bool SetDocumentBlockExtent(ImTypesetDocument* document, int block, float extent)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    if (block < 0 || static_cast<size_t>(block) >= d->blocks.size())
        return fail(ImTypesetError_InvalidArgument, "not a block of the document");
    DocBlock& b = d->blocks[static_cast<size_t>(block)];
    if (b.kind != ImTypesetBlockKind_Rule && b.kind != ImTypesetBlockKind_Host)
        return fail(ImTypesetError_InvalidArgument,
                    "only a rule or a host block has an extent to set");
    b.extent = extent;
    if (b.has_spec) {
        Block& changed = d->doc.edit_text(static_cast<size_t>(block));
        changed.thickness = b.kind == ImTypesetBlockKind_Rule && !(extent > 0.0f)
                                ? length_sp(*d, 1.0f)
                                : length_sp(*d, extent);
    }
    return succeed();
}

bool SetDocumentOptions(ImTypesetDocument* document, const ImTypesetDocumentOptions* options)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    d->options = options != nullptr ? *options : ImTypesetDocumentOptions();
    if (d->has_style)
        apply_rules(*d);
    return succeed();
}

int GetDocumentBlockCount(const ImTypesetDocument* document)
{
    const ImTypesetDocument* d = owned(document);
    return d != nullptr ? static_cast<int>(d->blocks.size()) : 0;
}

int GetDocumentStyleCount(const ImTypesetDocument* document)
{
    const ImTypesetDocument* d = owned(document);
    return d != nullptr ? static_cast<int>(d->styles.size()) : 0;
}

int GetDocumentContainerCount(const ImTypesetDocument* document)
{
    const ImTypesetDocument* d = owned(document);
    return d != nullptr ? static_cast<int>(d->containers.size()) : 0;
}

int GetDocumentBlockTextSize(const ImTypesetDocument* document, int block)
{
    const ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size())
        return 0;
    return static_cast<int>(d->blocks[static_cast<size_t>(block)].text.size());
}

bool LayoutDocument(ImTypesetDocument* document, float width, const ImTypesetStyle* style)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    ImTypesetContext& ctx = *d->ctx;
    if (bound(ctx))
        validate_fonts(ctx);
    take_style(*d, style != nullptr ? *style : ctx.style, width);
    return succeed();
}

bool LayoutDocumentWindow(ImTypesetDocument* document, float top, float bottom)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    ready(*d);
    const UnitRange range = layout_window(*d, coordinate_sp(*d, is_finite(top) ? top : 0.0),
                                          coordinate_sp(*d, is_finite(bottom) ? bottom : 0.0));
    const ErrorState* trouble = nullptr;
    for (size_t u = range.first; u < range.end; ++u) {
        const DocumentUnit& unit = d->doc.unit(u);
        for (size_t i = unit.first_block; i < unit.first_block + unit.block_count; ++i) {
            sync(*d, i);
            if (trouble == nullptr && !d->blocks[i].error.ok())
                trouble = &d->blocks[i].error;
        }
    }
    return trouble != nullptr ? report(*trouble) : succeed();
}

ImVec2 GetDocumentSize(ImTypesetDocument* document)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return ImVec2(0.0f, 0.0f);
    ready(*d);
    d->doc.prepare_if_needed(d->composer);
    return ImVec2(ceil_px(px_of(*d, d->width)), ceil_px(px_of(*d, d->doc.height())));
}

const ImTypesetErrorInfo* GetDocumentBlockError(const ImTypesetDocument* document, int block)
{
    static const ErrorState none;
    const ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size())
        return none.get();
    return d->blocks[static_cast<size_t>(block)].error.get();
}

ImTypesetDocumentStats GetDocumentStats(const ImTypesetDocument* document)
{
    ImTypesetDocumentStats out = {};
    const ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return out;
    out = d->stats;
    out.BlocksLaidOut = d->composer.blocks_composed;
    out.UnitsStacked = d->composer.units_assembled;
    out.StructureVisits = d->composer.blocks_prepared + d->spec_visits;
    out.Blocks = static_cast<int>(d->blocks.size());
    for (size_t i = 0; i < d->blocks.size(); ++i)
        out.BlocksDeferred += d->doc.laid_out(i) ? 0 : 1;
    return out;
}

bool DrawDocument(ImTypesetDocument* document, ImDrawList* draw_list, const ImVec2& pos,
                  const ImTypesetDocumentDraw* draw)
{
    if (draw_list == nullptr)
        return fail(ImTypesetError_InvalidArgument, "DrawDocument: a draw list is needed");
    if (!can_draw(document))
        return false;
    Target target;
    target.list = draw_list;
    const int before = draw_list->VtxBuffer.Size;
    const bool ok = draw_document(*document, target, pos, draw_list->_CmdHeader.ClipRect, draw);
    document->ctx->stats.VerticesEmitted += static_cast<ImU64>(draw_list->VtxBuffer.Size - before);
    return ok;
}

bool DrawDocumentToBuffers(ImTypesetDocument* document, const ImVec2& pos, const ImVec4& clip_rect,
                           ImVector<ImDrawVert>* out_vertices, ImVector<ImDrawIdx>* out_indices,
                           const ImTypesetDocumentDraw* draw)
{
    if (out_vertices == nullptr || out_indices == nullptr)
        return fail(ImTypesetError_InvalidArgument,
                    "DrawDocumentToBuffers: a document and two buffers are needed");
    if (!can_draw(document))
        return false;
    ImTypesetContext& ctx = *document->ctx;
    Target target;
    target.buffers = &ctx.buffers;
    target.clip = clip_rect;
    ctx.buffers.clear();
    const bool ok = draw_document(*document, target, pos, clip_rect, draw);
    const ErrorState outcome = last_error();

    const int base = out_vertices->Size;
    const int count = ctx.buffers.vertices.Size;
    const int index_count = ctx.buffers.indices.Size;
    const uint64_t index_limit = sizeof(ImDrawIdx) >= 4 ? 0x7FFFFFFFull : 0xFFFFull;
    if (static_cast<uint64_t>(base) + static_cast<uint64_t>(count) >
            std::min<uint64_t>(index_limit + 1, 0x7FFFFFFFull) ||
        static_cast<uint64_t>(out_indices->Size) + static_cast<uint64_t>(index_count) >
            0x7FFFFFFFull) {
        return fail(ImTypesetError_TooManyVertices,
                    "the document needs " + std::to_string(count) + " vertices after the " +
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
    ctx.stats.VerticesEmitted += static_cast<ImU64>(count);
    return ok ? succeed() : report(outcome);
}

ImTypesetDocumentHit HitTestDocument(ImTypesetDocument* document, const ImVec2& local_pos)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return ImTypesetDocumentHit{{0, 0}, -1, 0, false, false};
    ready(*d);
    return hit_test(*d, local_pos);
}

ImTypesetDocumentRange SelectDocumentRange(ImTypesetDocument* document, const ImVec2& anchor,
                                           const ImVec2& active, ImTypesetGranularity granularity)
{
    ImTypesetDocumentRange out = {{0, 0}, {0, 0}};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return out;
    ready(*d);
    if (d->blocks.empty())
        return out;
    if (granularity != ImTypesetGranularity_Word && granularity != ImTypesetGranularity_Line) {
        const ImTypesetPosition a = hit_test(*d, anchor).Position;
        const ImTypesetPosition b = hit_test(*d, active).Position;
        out.Start = before(b, a) ? b : a;
        out.End = before(b, a) ? a : b;
        return out;
    }
    const Granularity unit =
        granularity == ImTypesetGranularity_Word ? Granularity::WORD : Granularity::LINE;
    const Probe first = probe(*d, anchor);
    const ImTypesetTextRange first_unit = unit_at(*d, first, unit);
    const Probe second = probe(*d, active);
    const ImTypesetTextRange second_unit = unit_at(*d, second, unit);
    const ImTypesetPosition starts[2] = {{first.block, first_unit.Start},
                                         {second.block, second_unit.Start}};
    const ImTypesetPosition ends[2] = {{first.block, first_unit.End},
                                       {second.block, second_unit.End}};
    out.Start = before(starts[1], starts[0]) ? starts[1] : starts[0];
    out.End = before(ends[0], ends[1]) ? ends[1] : ends[0];
    return out;
}

int GetDocumentRangeRects(ImTypesetDocument* document, const ImTypesetDocumentRange& range,
                          ImVector<ImVec4>* out_rects)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || out_rects == nullptr)
        return 0;
    ready(*d);
    const ImTypesetDocumentRange wanted = ordered(*d, range);
    if (!before(wanted.Start, wanted.End))
        return 0;
    int count = 0;
    for (int block = wanted.Start.Block; block <= wanted.End.Block; ++block) {
        const size_t i = static_cast<size_t>(block);
        ImTypesetTextRange local = {0, 0};
        bool whole = false;
        if (!local_range(*d, wanted, i, local, &whole) || !layout_block(*d, block))
            continue;
        // One block's view at a time: a range over the whole document must
        // not keep a view of every block.
        ++d->tick;
        const DocBlock& b = d->blocks[i];
        const Scaled top = d->doc.block_top(i);
        if (is_host(b.kind)) {
            if (whole || local.Start < local.End) {
                const Scaled x = d->doc.block(i).style.left_indent;
                append_px(*d,
                          Rect{x, 0, std::max<Scaled>(d->width - x, 0), d->doc.layout(i).extent},
                          top, out_rects);
                ++count;
            }
            continue;
        }
        if ((!is_text(b.kind) && !is_code(b.kind)) || local.Start >= local.End)
            continue;
        const ImTypesetParagraph& p = view_of(*d, i);
        ensure_model(p);
        ImTypesetContext& ctx = *d->ctx;
        ctx.rects.clear();
        p.model.rects(span_of(b, p, local), ctx.rects);
        for (const Rect& rect : ctx.rects)
            append_px(*d, rect, top, out_rects);
        count += static_cast<int>(ctx.rects.size());
    }
    return count;
}

ImVec4 GetDocumentCaretRect(ImTypesetDocument* document, const ImTypesetPosition& position)
{
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return none;
    ready(*d);
    if (!layout_block(*d, position.Block))
        return none;
    const size_t i = static_cast<size_t>(position.Block);
    const DocBlock& b = d->blocks[i];
    const Scaled top = d->doc.block_top(i);
    if (!is_text(b.kind) && !is_code(b.kind)) {
        const Scaled x = d->doc.block(i).style.left_indent;
        const Scaled y = position.Offset > 0 ? d->doc.layout(i).extent : 0;
        return ImVec4(pxf(*d, x), pxf(*d, top + y), pxf(*d, x), pxf(*d, top + y));
    }
    const ImTypesetParagraph& p = view_of(*d, i);
    ensure_lines(p);
    if (p.line_boxes.empty())
        return none;
    const uint32_t index = index_of(b, p, position.Offset);
    const auto line = line_of_index(p, index);
    Scaled x = index >= line->index_end ? line->x1 : line->x0;
    for (uint32_t at = line->first + 1; at < line->end; ++at) {
        const DrawCmd& cmd = p.cmds[at];
        if (is_item(cmd) && cmd.index == index) {
            x = cmd.x;
            break;
        }
    }
    return ImVec4(pxf(*d, x), pxf(*d, top + line->top), pxf(*d, x), pxf(*d, top + line->bottom));
}

int AppendDocumentRangeText(ImTypesetDocument* document, const ImTypesetDocumentRange& range,
                            ImGuiTextBuffer* out, const char* block_separator)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || out == nullptr)
        return 0;
    ready(*d);
    const ImTypesetDocumentRange wanted = ordered(*d, range);
    if (!before(wanted.Start, wanted.End))
        return 0;
    const char* separator = block_separator != nullptr ? block_separator : "\n\n";
    ImTypesetContext& ctx = *d->ctx;
    const int start_size = out->size();
    bool any = false;
    for (int block = wanted.Start.Block; block <= wanted.End.Block; ++block) {
        const size_t i = static_cast<size_t>(block);
        ImTypesetTextRange local = {0, 0};
        bool whole = false;
        if (!local_range(*d, wanted, i, local, &whole))
            continue;
        const DocBlock& b = d->blocks[i];
        ++d->tick;
        ctx.scratch.clear();
        if (is_host(b.kind)) {
            if (whole || local.Start < local.End)
                ctx.scratch.assign(b.text);
        } else if (is_code(b.kind)) {
            if (local.Start < local.End && layout_block(*d, block)) {
                // Its size is known only once its lines are: cut again.
                const int size = text_size_of(b);
                const int from = std::min(local.Start, size);
                const int to = std::min(local.End, size);
                if (from < to)
                    ctx.scratch.assign(b.text, static_cast<size_t>(from),
                                       static_cast<size_t>(to - from));
            }
        } else if (is_text(b.kind) && local.Start < local.End && layout_block(*d, block)) {
            const ImTypesetParagraph& p = view_of(*d, i);
            ensure_model(p);
            TextData data;
            data.separators = p.separators.data();
            data.separator_count = p.separators.size();
            data.atoms = p.atom_texts.data();
            data.atom_count = p.atom_texts.size();
            data.originals = p.originals.data();
            data.original_count = p.originals.size();
            p.model.text(selection_of(p, local), data, ctx.scratch);
        }
        if (ctx.scratch.empty())
            continue;
        if (any)
            out->append(separator);
        out->append(ctx.scratch.data(), ctx.scratch.data() + ctx.scratch.size());
        any = true;
    }
    return out->size() - start_size;
}

ImTypesetBlockGeometry GetDocumentBlockGeometry(ImTypesetDocument* document, int block)
{
    ImTypesetBlockGeometry out = {
        ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), 0, false};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return out;
    ready(*d);
    if (!layout_block(*d, block))
        return out;
    const size_t i = static_cast<size_t>(block);
    const DocBlock& b = d->blocks[i];
    const BlockLayout& layout = d->doc.layout(i);
    const Scaled top = d->doc.block_top(i);
    const Scaled left =
        effective_indent(d->doc.block(i), d->doc.derived(), d->doc.rules(), d->width);
    const Scaled right = std::max(d->width, left);
    out.LaidOut = true;
    out.FullMin = ImVec2(pxf(*d, left), pxf(*d, top));
    out.FullMax = ImVec2(pxf(*d, right), pxf(*d, top + layout.extent));
    out.Min = out.FullMin;
    out.Max = out.FullMax;
    if (is_text(b.kind) || is_code(b.kind)) {
        const ImTypesetParagraph& p = view_of(*d, i);
        ensure_lines(p);
        out.LineCount = static_cast<int>(std::min<size_t>(p.line_boxes.size(), 0x7FFFFFFF));
        if (!p.line_boxes.empty()) {
            Scaled x0 = p.line_boxes.front().x0;
            Scaled x1 = p.line_boxes.front().x1;
            for (const LineBox& line : p.line_boxes) {
                x0 = std::min(x0, line.x0);
                x1 = std::max(x1, line.x1);
            }
            // Text that is not ragged right is placed across the measure.
            if (is_text(b.kind) && d->doc.block(i).style.align != Align::LEFT)
                x1 = std::max(x1, right);
            out.Min = ImVec2(pxf(*d, std::min(x0, left)), pxf(*d, top + p.line_boxes.front().top));
            out.Max = ImVec2(pxf(*d, x1), pxf(*d, top + p.line_boxes.back().bottom));
        }
    }
    return out;
}

ImTypesetLine GetDocumentBlockLine(ImTypesetDocument* document, int block, int line)
{
    ImTypesetLine out = {ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), 0.0f, 0, 0};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || line < 0)
        return out;
    ready(*d);
    if (!layout_block(*d, block))
        return out;
    const size_t i = static_cast<size_t>(block);
    const DocBlock& b = d->blocks[i];
    if (!is_text(b.kind) && !is_code(b.kind))
        return out;
    const ImTypesetParagraph& p = view_of(*d, i);
    ensure_lines(p);
    if (static_cast<size_t>(line) >= p.line_boxes.size())
        return out;
    const LineBox& box = p.line_boxes[static_cast<size_t>(line)];
    const Scaled left =
        effective_indent(d->doc.block(i), d->doc.derived(), d->doc.rules(), d->width);
    out.Min = ImVec2(pxf(*d, box.x0 - left), pxf(*d, box.top));
    out.Max = ImVec2(pxf(*d, box.x1 - left), pxf(*d, box.bottom));
    out.Baseline = pxf(*d, box.baseline);
    const int size = text_size_of(b);
    out.TextStart = std::min(byte_at_index(p, box.index), size);
    out.TextEnd =
        static_cast<size_t>(line) + 1 < p.line_boxes.size()
            ? std::min(byte_at_index(p, p.line_boxes[static_cast<size_t>(line) + 1].index), size)
            : size;
    if (out.TextEnd < out.TextStart)
        out.TextEnd = out.TextStart;
    return out;
}

bool DocumentView(const char* str_id, ImTypesetDocument* document, const ImVec2& size,
                  ImTypesetDocumentRange* selection, const ImTypesetDocumentDraw* draw,
                  const ImTypesetStyle* style)
{
    if (!can_draw(document))
        return false;
    ImTypesetDocument& d = *document;
    ImTypesetContext& ctx = *d.ctx;
    // NewFrame invalidates the draw data and Render makes it valid again:
    // the one sign imgui.h gives of being inside a frame.
    if (ImGui::GetFrameCount() <= 0 || ImGui::GetDrawData() != nullptr)
        return fail(ImTypesetError_InvalidArgument,
                    "DocumentView was called outside a frame: call it between "
                    "ImGui::NewFrame() and ImGui::Render()");
    // The child is ended however the view is left: a host can unwind through
    // it (an IM_ASSERT that throws, a failed allocation), and its window stack
    // must be what it was. Left in the ordinary way, EndChild() is called as
    // any other call is and may itself throw.
    struct Child {
        bool begun = false;
        ~Child()
        {
            if (begun)
                IMTYPESET_NO_UNWIND(ImGui::EndChild());
        }
    } child;
    const bool open = ImGui::BeginChild(str_id != nullptr ? str_id : "##document", size);
    child.begun = true;
    bool ok = true;
    if (open) {
        const float width = ImGui::GetContentRegionAvail().x;
        take_style(d, style != nullptr ? *style : ctx.style, width);
        if (selection != nullptr)
            d.selection = *selection;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 extent = ImTypeset::GetDocumentSize(&d);
        const uint64_t key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&d));
        if (extent.x > 0.0f && extent.y > 0.0f) {
            ImGui::InvisibleButton("##text", extent);
            const ImVec2 mouse = ImGui::GetMousePos();
            const ImVec2 local(mouse.x - pos.x, mouse.y - pos.y);
            if (ImGui::IsItemActivated()) {
                const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
                d.granularity = clicks >= 3   ? ImTypesetGranularity_Line
                                : clicks == 2 ? ImTypesetGranularity_Word
                                              : ImTypesetGranularity_Character;
                d.anchor = local;
                ctx.focused_key = key;
            }
            if (ImGui::IsItemActive()) {
                d.selection = ImTypeset::SelectDocumentRange(&d, d.anchor, local, d.granularity);
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered()) {
                d.selection = ImTypesetDocumentRange{{0, 0}, {0, 0}};
                if (ctx.focused_key == key)
                    ctx.focused_key = 0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            if (ctx.focused_key == key && ImGui::IsWindowFocused()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A) && !d.blocks.empty())
                    d.selection =
                        ImTypesetDocumentRange{{0, 0},
                                               {static_cast<int>(d.blocks.size()) - 1,
                                                static_cast<int>(d.blocks.back().text.size())}};
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) &&
                    before(d.selection.Start, d.selection.End)) {
                    ImGuiTextBuffer copy;
                    ImTypeset::AppendDocumentRangeText(&d, d.selection, &copy);
                    ImGui::SetClipboardText(copy.c_str());
                }
            }
        } else {
            ImGui::Dummy(extent);
        }
        ImTypesetDocumentDraw with;
        if (draw != nullptr)
            with = *draw;
        with.Selection = d.selection;
        Target target;
        target.list = ImGui::GetWindowDrawList();
        const int vertices_before = target.list->VtxBuffer.Size;
        ok = draw_document(d, target, pos, target.list->_CmdHeader.ClipRect, &with);
        ctx.stats.VerticesEmitted +=
            static_cast<ImU64>(target.list->VtxBuffer.Size - vertices_before);
        if (selection != nullptr)
            *selection = d.selection;
    }
    const ErrorState outcome = last_error();
    child.begun = false;
    ImGui::EndChild();
    return ok ? succeed() : report(outcome);
}

} // namespace ImTypeset
