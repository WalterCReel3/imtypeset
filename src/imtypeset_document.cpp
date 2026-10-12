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
#include <limits>
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
    return kind == ImTypesetBlockKind_Host;
}

bool is_table(ImTypesetBlockKind kind)
{
    return kind == ImTypesetBlockKind_Table;
}

// A block whose commands are lines of items that came from its text: one a
// position, a selection, a mark and a copy go into.
bool is_lined(ImTypesetBlockKind kind)
{
    return is_text(kind) || is_code(kind) || is_table(kind);
}

// The flags of a block that are a text's (ImTypesetTextFlags_ has the same
// values for them).
ImTypesetTextFlags text_flags(const DocBlock& b)
{
    return b.flags & (ImTypesetBlockFlags_HardBreaks | ImTypesetBlockFlags_Math);
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
    if (is_table(b.kind))
        return b.row_count();
    return static_cast<uint32_t>(std::min<size_t>(b.text.size(), 0xFFFFFFFFu));
}

// Adds cells to a table: `count` of `given`, as bytes of the block's text,
// which is `size` long. A cell that starts before the one before it ends is
// cut there; cells past the limits are left out. What was wrong is said
// through `note`, once.
template<typename Note>
void add_cells(DocBlock& b, const ImTypesetCell* given, int count, size_t size, Note&& note)
{
    if (given == nullptr || count <= 0)
        return;
    bool cut = false;
    bool too_many = false;
    bool too_wide = false;
    uint32_t floor = b.cells.empty() ? 0u : b.cells.back().end;
    for (int k = 0; k < count; ++k) {
        const ImTypesetCell& in = given[k];
        const int64_t limit = static_cast<int64_t>(size);
        int64_t start = std::min<int64_t>(std::max<int64_t>(in.TextStart, floor), limit);
        int64_t end = std::min<int64_t>(in.TextEnd, limit);
        if (in.TextStart < static_cast<int64_t>(floor) || in.TextEnd > limit || end < start)
            cut = true;
        if (end < start)
            end = start;
        const bool new_row = b.row_first.empty() || (in.Flags & ImTypesetCellFlags_RowStart) != 0;
        if (b.cells.size() >= MAX_TABLE_CELLS) {
            too_many = true;
            break;
        }
        if (new_row) {
            if (b.row_first.empty())
                b.row_first.push_back(0);
            // An empty row cannot be said, so a row always has its first cell.
            b.row_first.push_back(static_cast<uint32_t>(b.cells.size()));
            b.next_column = 0;
        }
        if (b.next_column >= MAX_TABLE_COLUMNS) {
            too_wide = true;
            continue;
        }
        DocCell cell;
        cell.start = static_cast<uint32_t>(start);
        cell.end = static_cast<uint32_t>(end);
        cell.row = b.row_count() - 1;
        cell.column = b.next_column++;
        b.cells.push_back(cell);
        b.row_first.back() = static_cast<uint32_t>(b.cells.size());
        b.columns = std::max(b.columns, cell.column + 1);
        floor = cell.end;
    }
    if (cut)
        note("a cell is not inside the text given or starts before the cell before it ends; "
             "it was cut to what is left");
    if (too_wide)
        note("a row has more than 1,024 cells; the rest of it is left out");
    if (too_many)
        note("the table has more than 262,144 cells; the rest are left out");
}

// The cell a byte of a table's text is in: the last that starts at or before
// it, or the one after if the byte is past that cell's end (between two
// cells). -1 for a table with no cells.
int cell_at(const DocBlock& b, int offset)
{
    if (b.cells.empty())
        return -1;
    const uint32_t wanted = offset <= 0 ? 0u : static_cast<uint32_t>(offset);
    const auto after =
        std::upper_bound(b.cells.begin(), b.cells.end(), wanted,
                         [](uint32_t value, const DocCell& cell) { return value < cell.start; });
    if (after == b.cells.begin())
        return 0;
    const size_t k = static_cast<size_t>(after - b.cells.begin()) - 1;
    if (wanted > b.cells[k].end && k + 1 < b.cells.size())
        return static_cast<int>(k + 1);
    return static_cast<int>(k);
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
    b.flags = in.Flags & (ImTypesetBlockFlags_HardBreaks | ImTypesetBlockFlags_Math |
                          ImTypesetBlockFlags_FixedLineHeight);
    if (is_table(b.kind))
        b.flags = in.Flags & (ImTypesetBlockFlags_Math | ImTypesetBlockFlags_FixedLineHeight |
                              ImTypesetBlockFlags_TableHeader | ImTypesetBlockFlags_TableHeld);
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
    if ((is_text(b.kind) || is_table(b.kind)) && in.RunCount > 0) {
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
        set_text(w, text, text_bytes, runs, run_count, text_flags(b));
        b.text.swap(w.text);
        b.runs.swap(w.runs);
        b.text_error = w.text_error;
        w.text.clear();
        w.runs.clear();
    } else if (b.kind != ImTypesetBlockKind_Rule) {
        const size_t kept = cut_at_character(text, text_bytes, static_cast<size_t>(MAX_TEXT_BYTES));
        if (kept < text_bytes)
            b.text_error.set(ImTypesetError_TextTooLong,
                             "the text is longer than 16 MiB and was cut there",
                             static_cast<int>(kept));
        b.text.assign(text, kept);
    }
    if (is_table(b.kind)) {
        ErrorState run_trouble;
        validate_runs(b.runs, runs, run_count, b.text.size(), 0, run_trouble);
        if (b.text_error.ok())
            b.text_error = run_trouble;
        if (in.CellCount > 0) {
            if (content.Cells != nullptr && in.CellStart >= 0 &&
                in.CellStart <= content.CellCount &&
                in.CellCount <= content.CellCount - in.CellStart)
                add_cells(b, content.Cells + in.CellStart, in.CellCount, b.text.size(), note);
            else
                note("its cells are not inside the cells given; it is set without them");
        }
        if (in.ColumnCount > 0) {
            if (content.Columns != nullptr && in.ColumnStart >= 0 &&
                in.ColumnStart <= content.ColumnCount &&
                in.ColumnCount <= content.ColumnCount - in.ColumnStart)
                b.column_specs.assign(
                    content.Columns + in.ColumnStart,
                    content.Columns + in.ColumnStart +
                        std::min<size_t>(static_cast<size_t>(in.ColumnCount), MAX_TABLE_COLUMNS));
            else
                note("its columns are not inside the columns given; it is set without them");
        }
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

    if (b.kind != ImTypesetBlockKind_Rule && !is_host(b.kind) && !is_table(b.kind)) {
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
    spec.type = is_code(b.kind)    ? BlockType::CODE
                : is_text(b.kind)  ? BlockType::TEXT
                : is_table(b.kind) ? BlockType::TABLE
                                   : BlockType::RULE;

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
    spec.strut.exact = (b.flags & ImTypesetBlockFlags_FixedLineHeight) != 0;

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

// What a table's columns say, into the block layer: the alignments, and who
// decides the widths. True if an alignment changed.
bool apply_columns(const ImTypesetDocument& d, const DocBlock& b, TableCells& table)
{
    const size_t columns = b.columns;
    const size_t said = std::min(b.column_specs.size(), columns);
    std::vector<ColumnAlign> aligns(columns, ColumnAlign::LEFT);
    bool widths = said == columns && columns != 0;
    bool shares = widths;
    for (size_t c = 0; c < said; ++c) {
        const ImTypesetTableColumn& column = b.column_specs[c];
        aligns[c] = column.Align == ImTypesetAlign_Center  ? ColumnAlign::CENTER
                    : column.Align == ImTypesetAlign_Right ? ColumnAlign::RIGHT
                                                           : ColumnAlign::LEFT;
        widths = widths && column.Width > 0.0f && is_finite(column.Width);
        shares = shares && column.Share > 0.0f && is_finite(column.Share);
    }
    const bool changed = aligns != table.aligns;
    table.aligns.swap(aligns);
    table.held = (b.flags & ImTypesetBlockFlags_TableHeld) != 0;
    table.policy = WidthPolicy::APPORTIONED;
    table.fractions.clear();
    if (table.held) {
        table.widths.assign(b.held.begin(), b.held.end());
        return changed;
    }
    table.widths.clear();
    if (widths) {
        table.policy = WidthPolicy::WIDTHS;
        for (size_t c = 0; c < columns; ++c)
            table.widths.push_back(std::max<Scaled>(length_sp(d, b.column_specs[c].Width), 1));
    } else if (shares) {
        table.policy = WidthPolicy::FRACTIONS;
        for (size_t c = 0; c < columns; ++c)
            table.fractions.push_back(decimal_of(b.column_specs[c].Share));
    }
    return changed;
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
    d.largest_em = std::max(d.largest_em, spec.font.size);
    // Ink may stand outside a block of exact lines: see `exact_reach`.
    if (spec.strut.exact)
        d.exact_reach = std::max(d.exact_reach, 4 * std::max<Scaled>(spec.font.size, 0));
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
    block.keep_with_next = b.kind == ImTypesetBlockKind_Heading;
    block.length_hint = length_hint(b);
    b.cells_built = 0;
    b.last_cell_boxes = 0;
    if (is_table(b.kind)) {
        TableCells& table = block.table;
        table.config = table_stamp();
        table.has_header = (b.flags & ImTypesetBlockFlags_TableHeader) != 0;
        // The reference's Theme: half a body em of padding, and a rule of six
        // hundredths of one, at least a scaled point.
        const double body = static_cast<double>(d.style.em);
        table.cell_padding = d.options.TableCellPadding > 0.0f
                                 ? length_sp(d, d.options.TableCellPadding)
                                 : (d.options.TableCellPadding < 0.0f
                                        ? 0
                                        : clamped_sp(round_to_scaled(body * 0.5), 0, MAX_DIMEN));
        table.rule_thickness = d.options.TableRuleThickness > 0.0f
                                   ? length_sp(d, d.options.TableRuleThickness)
                                   : (d.options.TableRuleThickness < 0.0f
                                          ? 0
                                          : clamped_sp(round_to_scaled(body * 0.06), 1, MAX_DIMEN));
        apply_columns(d, b, table);
    }
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
    ++d.composer.params_serial;
}

// A table's cells that have no material yet, each made as a paragraph's text
// is and measured: the packaging of tex.web section 796, when the entry
// arrives. Cells before DocBlock::cells_built stand as they are, so a table
// that was given more cells pays for those.
void build_cells(ImTypesetDocument& d, DocBlock& b, Block& block)
{
    ImTypesetContext& ctx = *d.ctx;
    TableCells& table = block.table;
    NodeArena& arena = d.doc.arena();
    ImTypesetParagraph& w = d.work;
    // If the build is unwound through, the table is made again from its first
    // cell: what it had put in the arena is given up as dead.
    struct Guard {
        ImTypesetDocument& d;
        DocBlock& b;
        TableCells& table;
        NodeArena& arena;
        const NodeArena::Mark before;
        bool finished = false;
        ~Guard()
        {
            if (finished)
                return;
            arena.rewind(before);
            table.clear();
            d.dead_boxes += b.atom_boxes;
            d.live_boxes -= std::min<size_t>(d.live_boxes, b.atom_boxes);
            b.atom_boxes = 0;
            b.cells_built = 0;
            b.has_material = false;
        }
    } guard{d, b, table, arena, arena.mark()};

    const uint32_t built_before = b.cells_built;
    if (b.cells_built == 0) {
        table.clear();
        b.source_offsets.clear();
        b.ink_spans.clear();
        b.atoms.clear();
        b.display_text.clear();
        b.display_map.clear();
        b.last_cell_boxes = 0;
        b.last_cell_display = 0;
    }
    ResolvedStyle style = d.style;
    style.face = b.font.face;
    style.em = b.font.size;
    style.align = Align::LEFT;
    const CharMeasure measure = ctx.measure();
    for (size_t i = b.cells_built; i < b.cells.size(); ++i) {
        const DocCell& cell = b.cells[i];
        if (i == 0 || cell.row != b.cells[i - 1].row)
            table.start_row();
        // The cell as a paragraph of its own: its stretch of the text, and
        // the block's runs cut to it.
        w.ctx = &ctx;
        w.text.assign(b.text, cell.start, cell.end - cell.start);
        w.runs.clear();
        auto run =
            std::upper_bound(b.runs.begin(), b.runs.end(), cell.start,
                             [](uint32_t start, const StyledRun& r) { return start < r.end; });
        for (; run != b.runs.end() && run->start < cell.end; ++run) {
            StyledRun inside = *run;
            inside.start = std::max(inside.start, cell.start) - cell.start;
            inside.end = std::min(inside.end, cell.end) - cell.start;
            if (inside.start < inside.end)
                w.runs.push_back(inside);
        }
        w.text_error.clear();
        w.flags = b.flags & ImTypesetBlockFlags_Math;
        w.material_arena = &arena;
        ++w.text_version;
        const NodeArena::Mark before = arena.mark();
        build_material_now(ctx, w, style);
        ++d.stats.TableCellsMeasured;
        b.last_cell_boxes = arena.mark().boxes - before.boxes;
        b.atom_boxes += b.last_cell_boxes;
        d.live_boxes += b.last_cell_boxes;

        const uint32_t node_base = static_cast<uint32_t>(table.nodes.size());
        const uint32_t display_base = static_cast<uint32_t>(b.display_text.size());
        b.last_cell_display = display_base;
        table.nodes.insert(table.nodes.end(), w.block.run.nodes.begin(), w.block.run.nodes.end());
        for (const uint32_t offset : w.source_offsets)
            b.source_offsets.push_back(offset + cell.start);
        b.source_offsets.resize(table.nodes.size(), cell.end);
        for (const InkSpan& span : w.ink_spans)
            b.ink_spans.push_back(
                InkSpan{span.first + node_base, span.end + node_base, span.colour});
        for (AtomSource atom : w.atoms) {
            const uint32_t base = atom.in_display_text ? display_base : cell.start;
            atom.node += node_base;
            atom.begin += base;
            atom.end += base;
            b.atoms.push_back(atom);
        }
        b.display_text.append(w.display_text);
        b.fallback = b.fallback || w.fallback;
        if (b.error.ok() && !w.material_error.ok()) {
            const int at = w.material_error.byte_pos >= 0
                               ? w.material_error.byte_pos + static_cast<int>(cell.start)
                               : -1;
            b.error.set(w.material_error.code, w.material_error.message, at,
                        at >= 0 ? ImTypeset::TextByteToCodepointOffset(
                                      b.text.data(), at, b.text.data() + b.text.size())
                                : -1);
        }
        const size_t first = table.cell_end.empty() ? 0 : table.cell_end.back();
        table.end_cell(
            natural_width(arena, NodeView(table.nodes.data() + first, table.nodes.size() - first),
                          table.cell_padding, measure));
        b.cells_built = static_cast<uint32_t>(i + 1);
    }
    w.block.run.nodes.clear();
    w.text.clear();
    w.runs.clear();

    // The widths held are each column's widest so far (the reference's
    // `StreamView._held`): never narrower as cells arrive, and started afresh
    // when the number of columns changes or what they were measured under
    // does. They already hold every cell built before this call, so only the
    // cells built by it are added: a row that arrives costs its own cells.
    if (table.held) {
        const DocBlock::HeldUnder under{b.font.face, b.font.size, table.cell_padding, d.generation};
        size_t from = built_before;
        if (!(b.held_under == under) || b.held.size() != b.columns) {
            b.held.assign(b.columns, 0);
            from = 0;
        } else if (b.held_cells < built_before) {
            from = 0;
        }
        b.held_under = under;
        for (size_t k = from; k < b.cells.size() && k < table.natural.size(); ++k) {
            const DocCell& cell = b.cells[k];
            if (cell.column < b.held.size())
                b.held[cell.column] = std::max(b.held[cell.column], table.natural[k]);
        }
        b.held_cells = b.cells_built;
        table.widths.assign(b.held.begin(), b.held.end());
    }
    guard.finished = true;
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
    if (is_lined(b.kind))
        ++d.stats.MaterialBuilds;
    if (is_table(b.kind)) {
        // What was found wrong with the cells that stand is kept.
        if (b.cells_built == 0) {
            b.fallback = false;
            b.error = b.text_error;
        }
        b.display_block = false;
        b.has_material = true;
        build_cells(d, b, block);
        return;
    }
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
    w.flags = text_flags(b);
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
        b.cells_built = 0;
        if (b.has_spec) {
            Block& block = d.doc.edit_text(i);
            block.text.run.nodes.clear();
            block.table.clear();
        }
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

// What SetDocumentColumns said, against the style: whether the document is
// shown in columns, and how they are cut.
void apply_column_rules(ImTypesetDocument& d)
{
    const ImTypesetColumns& given = d.columns_given;
    const bool balance = (given.Flags & ImTypesetColumnFlags_Balance) != 0 && given.Count >= 2;
    // A view gives its own height to columns that state none.
    const float height =
        given.Height > 0.0f ? given.Height : (given.Count >= 2 ? d.view_height : 0.0f);
    d.columned = balance || height > 0.0f;
    if (!d.columned)
        return;
    ColumnRules rules;
    rules.balance =
        balance ? std::min(static_cast<uint32_t>(given.Count), MAX_BALANCED_COLUMNS) : 0u;
    rules.height = balance ? 0 : std::max<Scaled>(length_sp(d, height), 1);
    // The reference's \splittopskip is ten points whatever the type. The
    // product's is the height of a line of the document's style above its
    // baseline, so that a column headed by something shorter than a line (a
    // rule, a table's rule, small type) has it where a line's baseline would
    // be, and one headed by a line or anything taller is flush.
    //
    // The reference's \splitmaxdepth is \maxdimen, as plain TeX's is: the
    // last box of a column may be as deep as it likes below the foot, and a
    // table row whose cells wrapped is a box one line high and all its other
    // lines deep. The product's is the depth of a line of the document's
    // style, so what is deeper counts as height (tex.web section 971) and a
    // column holds the whole of its last row or does not take it.
    if (!d.reference_rules) {
        const Strut line = Strut::filling(d.style.baselineskip);
        rules.split_top_skip.width = line.height;
        rules.split_max_depth = line.depth;
    }
    d.columns.set_rules(rules);
    d.gutter = given.Gutter < 0.0f
                   ? 0
                   : (given.Gutter > 0.0f ? length_sp(d, given.Gutter) : d.style.baselineskip);
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
    apply_column_rules(d);
}

// Where a height of the uncut column is shown on the page: the column, and
// what is added to a point of the uncut column to put it there. Cuts the
// columns up to that one.
struct Shown {
    size_t column = 0;
    Scaled dx = 0;
    Scaled dy = 0;
};

// Whether the position a point in a table answers is held to the cell whose
// rectangle holds the point (the product's rule) or is the reference's
// index's.
bool product_hits(const ImTypesetDocument& d)
{
    return !d.reference_rules || d.product_table_hits;
}

Scaled pitch_of(const ImTypesetDocument& d)
{
    return std::max<Scaled>(add_saturating(d.width, d.gutter), 1);
}

Shown shown_at(ImTypesetDocument& d, Scaled y)
{
    Shown out;
    if (!d.columned)
        return out;
    const size_t k = d.columns.column_at(d.doc, d.composer, y);
    if (k >= d.columns.count())
        return out;
    const DocumentColumn& column = d.columns.column(k);
    if (column.empty)
        return out;
    out.column = k;
    out.dx = static_cast<Scaled>(k) * pitch_of(d);
    out.dy = sub_saturating(column.head, column.top);
    return out;
}

// The point of the uncut column that a point of the page shows. A point in
// the space after a column is that column's; one above a column's first line
// is at that line's top and one below its last line at that line's foot, so
// that what is nearest in the column is what is found; a point past the last
// column is the last column's.
Point uncut_of(ImTypesetDocument& d, Scaled x, Scaled y)
{
    if (!d.columned)
        return Point{x, y};
    const Scaled pitch = pitch_of(d);
    const Scaled across = x <= 0 ? 0 : floor_div(x, pitch);
    size_t k = static_cast<size_t>(std::min<Scaled>(across, Scaled(1) << 40));
    d.columns.cut_through(d.doc, d.composer, k);
    const size_t count = d.columns.count();
    if (count == 0)
        return Point{x, y};
    k = std::min(k, count - 1);
    while (k > 0 && d.columns.column(k).empty)
        --k;
    const DocumentColumn& column = d.columns.column(k);
    if (column.empty)
        return Point{x, y};
    const Scaled last = column.bottom > column.top ? column.bottom - 1 : column.top;
    const Scaled down = add_saturating(column.top, sub_saturating(y, column.head));
    return Point{sub_saturating(x, static_cast<Scaled>(k) * pitch),
                 std::min(std::max(down, column.top), last)};
}

// The first position set at or below a height of the uncut column.
ImTypesetPosition position_at(ImTypesetDocument& d, Scaled y);

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

// Where each index of a table's commands came from in its text
// (DocBlock::table_bytes). An item that came from a character is that
// character's byte. One the text does not hold -- the glue that places a cell
// in its column, the skips of a wrapped cell's lines -- is, before the cell's
// first character, that character's byte, and after its last, the END of the
// cell's text: a caret at the end of a cell is the cell's own end, not the
// next cell's start. A cell the row lacks is where the cell before it ended.
// The map is made from row `from_row` on, whose cells' nodes begin at
// `from_node`: the rows before it keep what they have, which a layout that
// went on from them left where it was.
void map_table_indices(DocBlock& b, const BlockLayout& layout, size_t from_row, size_t from_node)
{
    constexpr uint32_t UNSET = 0xFFFFFFFFu;
    const uint32_t items = layout.item_count;
    const TableLayout& table = layout.table;
    const size_t rows = table.rows.size();
    const size_t columns = rows != 0 ? (table.cell_index.size() - 1) / rows : 0;
    std::vector<uint32_t>& bytes = b.table_bytes;
    // What stands: the indices of the rows before `from_row`, whose bytes
    // are below every later one's, and the nodes of their cells.
    uint32_t first_index = 0;
    size_t first_node = 0;
    uint32_t last_end = 0;
    if (from_row != 0 && from_row <= rows && from_row < b.row_first.size() &&
        b.row_first[from_row] != 0) {
        first_index = std::min(table.rows[from_row - 1].index_after, items);
        const size_t cell = b.row_first[from_row] - 1;
        last_end = b.cells[cell].end;
        first_node = from_node;
    } else {
        from_row = 0;
    }
    bytes.resize(first_index);
    bytes.resize(static_cast<size_t>(items) + 1, UNSET);
    const size_t nodes = std::min(layout.shipped.size(), b.source_offsets.size());
    for (size_t n = first_node; n < nodes; ++n) {
        const uint32_t index = layout.shipped[n];
        if (index < items && index >= first_index)
            bytes[index] = std::min(bytes[index], b.source_offsets[n]);
    }
    for (size_t r = from_row; r < rows && r + 1 < b.row_first.size(); ++r) {
        const size_t have = b.row_first[r + 1] - b.row_first[r];
        for (size_t c = 0; c < columns; ++c) {
            const size_t k = r * columns + c;
            const uint32_t first = std::min(table.cell_index[k], items);
            const uint32_t end = std::min(table.cell_index[k + 1], items);
            if (c < have)
                last_end = b.cells[b.row_first[r] + c].end;
            uint32_t next = last_end;
            for (uint32_t index = end; index-- > first;) {
                if (bytes[index] == UNSET || bytes[index] > next)
                    bytes[index] = next;
                else
                    next = bytes[index];
            }
        }
    }
    bytes[items] = static_cast<uint32_t>(b.text.size());
    for (uint32_t index = items; index-- > first_index;) {
        if (bytes[index] > bytes[index + 1])
            bytes[index] = bytes[index + 1];
    }
}

// A table whose layout went on from the rows of the layout this block last
// saw: what is kept beside it is brought up to date from the first row that
// was set again, and the rows that stand are not gone over. False, with
// nothing changed, if the block's layout is not one of those.
bool sync_grown_table(ImTypesetDocument& d, size_t i, uint64_t had)
{
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    const TableLayout& table = layout.table;
    const TableCells& cells = d.doc.block(i).table;
    const size_t standing = table.resumed_rows;
    if (had == 0 || table.resumed_from != had || standing == 0 || standing > table.rows.size() ||
        standing >= cells.row_first.size() || standing >= b.row_first.size() || !b.lines.sorted)
        return false;
    const TableRowLayout& last = table.rows[standing - 1];
    const size_t cmd_from = last.out_cmd_end;
    const uint32_t index_from = last.index_after;
    const size_t cell_from = cells.row_first[standing];
    const size_t node_from = cell_from == 0 ? 0 : cells.cell_end[cell_from - 1];
    const uint32_t items = layout.item_count;
    if (cmd_from > layout.cmds.size() || index_from > items || b.table_bytes.size() <= index_from ||
        node_from > layout.shipped.size())
        return false;

    // The lines: the spans of the rows that stand end where the first row
    // set again begins, which is a LINE.
    std::vector<LineSpan>& spans = b.lines.spans;
    while (!spans.empty() && spans.back().first >= cmd_from)
        spans.pop_back();
    if (!spans.empty() && spans.back().end > cmd_from)
        return false;
    LineIndex& tail = d.lines_scratch;
    index_lines(layout.cmds.data() + cmd_from, layout.cmds.size() - cmd_from, tail);
    for (LineSpan span : tail.spans) {
        span.first += static_cast<uint32_t>(cmd_from);
        span.end += static_cast<uint32_t>(cmd_from);
        if (!span.bounded ||
            (!spans.empty() && (span.top < spans.back().top || span.bottom < spans.back().bottom)))
            b.lines.sorted = false;
        spans.push_back(span);
    }
    b.lines.sorted = b.lines.sorted && tail.sorted;
    if (d.doc.block(i).style.strut.exact)
        b.lines.overhang = std::max(b.lines.overhang, ink_overhang(layout.cmds.data() + cmd_from,
                                                                   layout.cmds.size() - cmd_from));

    // The colours: the ranges of the rows that stand end at or before their
    // last index, and a run is one cell's.
    while (!b.inks.empty() && b.inks.back().end > index_from)
        b.inks.pop_back();
    const auto first_span =
        std::partition_point(b.ink_spans.begin(), b.ink_spans.end(),
                             [&](const InkSpan& span) { return span.first < node_from; });
    for (auto span = first_span; span != b.ink_spans.end(); ++span) {
        uint32_t low = NOT_SHIPPED;
        uint32_t high = 0;
        for (size_t m = span->first; m < span->end && m < layout.shipped.size(); ++m) {
            const uint32_t index = layout.shipped[m];
            if (index == NOT_SHIPPED)
                continue;
            if (low == NOT_SHIPPED)
                low = index;
            high = std::max(high, index + 1);
        }
        if (low == NOT_SHIPPED)
            continue;
        if (!b.inks.empty() && low < b.inks.back().end)
            low = b.inks.back().end;
        if (low < high)
            b.inks.push_back(InkRange{low, high, span->colour});
    }

    map_table_indices(b, layout, standing, node_from);
    d.table_rows_synced += table.rows.size() - standing;
    return true;
}

// Brings what is kept beside a block's layout up to date with it.
void sync(ImTypesetDocument& d, size_t i)
{
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    if (b.seen == layout.serial)
        return;
    const uint64_t had = b.seen;
    // Not up to date until all of it is: see the end. (No layout has serial 0.)
    b.seen = 0;
    if (is_table(b.kind) && sync_grown_table(d, i, had)) {
        if (!layout.ok() && b.error.ok())
            b.error.set(ImTypesetError_LayoutFailed,
                        "a character could not be measured; its line is set without it");
        b.seen = layout.serial;
        return;
    }
    index_lines(layout.cmds.data(), layout.cmds.size(), b.lines);
    // Lines set to an exact height do not hold what is in them.
    if (d.doc.block(i).style.strut.exact)
        b.lines.overhang = ink_overhang(layout.cmds.data(), layout.cmds.size());
    b.inks.clear();
    b.marker_items = 0;
    if (is_text(b.kind)) {
        b.marker_items = layout.marker_items;
        ink_ranges(layout.shipped, b.ink_spans, d.inks_scratch);
    } else if (is_table(b.kind)) {
        ink_ranges(layout.shipped, b.ink_spans, d.inks_scratch);
        map_table_indices(b, layout, 0, 0);
        d.table_rows_synced += layout.table.rows.size();
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

// Where block `i` starts in the uncut column, for something that is about to
// be put on the page. Shown in columns, a block's top is exact only once
// every unit above it is laid out, and cutting the columns is what lays them
// out: read before the cut it is an estimate, and the column the cut then
// names is not the one that height was in. So the columns are cut past the
// block's unit first. The block is laid out.
Scaled settled_top(ImTypesetDocument& d, size_t i)
{
    if (d.columned)
        d.columns.cut_past_unit(d.doc, d.composer, d.doc.unit_of(i));
    return d.doc.block_top(i);
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
    p.cell_breaks.clear();
    if (is_table(b.kind)) {
        // A copy writes a newline before each row but the first and a tab
        // before each cell but the first of its row (the reference's
        // `_separators_within`): every cell owns an index, an empty one too.
        p.index_bytes.assign(b.table_bytes.begin(), b.table_bytes.end());
        const TableLayout& table = layout.table;
        const size_t rows = table.rows.size();
        const size_t columns = rows != 0 ? (table.cell_index.size() - 1) / rows : 0;
        p.layout.hard_breaks.clear();
        for (size_t r = 0; r < rows; ++r) {
            if (r != 0)
                p.layout.hard_breaks.push_back(table.row_index[r]);
            for (size_t c = 1; c < columns; ++c)
                p.cell_breaks.push_back(table.cell_index[r * columns + c]);
        }
    } else if (is_text(b.kind)) {
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

// --- A table's cells, where they are ------------------------------------------------------

// One cell of a laid-out table: its rectangle, down and across from the
// block's top-left corner (the column's edges, the row's top and bottom), the
// indices its items have, and its number among the block's cells.
struct CellBox {
    size_t row = 0;
    size_t column = 0;
    size_t cell = 0;
    Scaled left = 0;
    Scaled right = 0;
    Scaled top = 0;
    Scaled bottom = 0;
    uint32_t first = 0;
    uint32_t end = 0;
};

// How many rows of a table are both described and laid out, and how many
// columns each has in the layout's cell index.
size_t table_rows(const DocBlock& b, const TableLayout& table)
{
    const size_t rows = std::min<size_t>(table.rows.size(), b.row_count());
    return table.row_top.size() > rows ? rows : 0;
}

size_t table_columns(const TableLayout& table)
{
    const size_t rows = table.rows.size();
    const size_t columns =
        rows != 0 && !table.cell_index.empty() ? (table.cell_index.size() - 1) / rows : 0;
    return std::min(columns, table.columns.size());
}

// The cell of `row` in `column`, or the row's last if it has no cell there.
// False for a row with no cells and for a table that is not laid out.
bool cell_box(const DocBlock& b, const TableLayout& table, size_t row, size_t column, CellBox& out)
{
    const size_t rows = table_rows(b, table);
    const size_t columns = table_columns(table);
    if (row >= rows || columns == 0)
        return false;
    const size_t have = std::min<size_t>(b.row_first[row + 1] - b.row_first[row], columns);
    if (have == 0)
        return false;
    column = std::min(column, have - 1);
    out.row = row;
    out.column = column;
    out.cell = b.row_first[row] + column;
    out.left = table.indent;
    for (size_t k = 0; k < column; ++k)
        out.left = add_saturating(out.left, table.columns[k]);
    out.right = add_saturating(out.left, table.columns[column]);
    out.top = table.row_top[row];
    out.bottom = add_saturating(
        out.top, add_saturating(table.rows[row].dims.height, table.rows[row].dims.depth));
    out.first = table.cell_index[row * columns + column];
    out.end = table.cell_index[row * columns + column + 1];
    return true;
}

// The cell whose rectangle holds a point of the block, or the nearest one: a
// point between two rows (in the space the rule under a header takes, say) is
// the nearer row's and the upper one's when it is as near to both; a point
// beside the table is the first or the last column's; a point in a column the
// row has no cell for is the row's last cell's.
bool cell_under(const DocBlock& b, const TableLayout& table, Point at, CellBox& out)
{
    const size_t rows = table_rows(b, table);
    const size_t columns = table_columns(table);
    if (rows == 0 || columns == 0)
        return false;
    const auto bottom_of = [&](size_t r) {
        return add_saturating(table.row_top[r],
                              add_saturating(table.rows[r].dims.height, table.rows[r].dims.depth));
    };
    // The last row that starts at or above the point.
    size_t row = static_cast<size_t>(
        std::upper_bound(table.row_top.begin(),
                         table.row_top.begin() + static_cast<std::ptrdiff_t>(rows), at.y) -
        table.row_top.begin());
    row = row == 0 ? 0 : row - 1;
    if (row + 1 < rows && at.y >= bottom_of(row) &&
        table.row_top[row + 1] - at.y < at.y - bottom_of(row) + 1)
        ++row;
    size_t column = 0;
    Scaled right = add_saturating(table.indent, table.columns[0]);
    while (column + 1 < columns && at.x >= right) {
        ++column;
        right = add_saturating(right, table.columns[column]);
    }
    return cell_box(b, table, row, column, out);
}

// The bytes of a table's text a caret may stand at and be in `cell`: from the
// cell's start to its end. A cell that ends where the next one starts (text
// written with nothing between two cells) does not own that byte, which is
// the later cell's start: its last position is then the start of its last
// item, or its own start if it is empty.
void own_bytes(const DocBlock& b, const ImTypesetParagraph& p, const CellBox& cell, uint32_t& low,
               uint32_t& high)
{
    const DocCell& given = b.cells[cell.cell];
    low = given.start;
    high = std::max(given.end, given.start);
    if (cell.cell + 1 >= b.cells.size() || b.cells[cell.cell + 1].start != high || high == low)
        return;
    uint32_t last = low;
    const size_t end = std::min<size_t>(cell.end, p.index_bytes.size());
    for (size_t index = cell.first; index < end; ++index) {
        const uint32_t byte = p.index_bytes[index];
        if (byte >= low && byte < high && byte > last)
            last = byte;
    }
    high = last;
}

// The padding a table's cells were set with.
Scaled padding_of(const ImTypesetDocument& d, size_t i)
{
    return std::max<Scaled>(clamp_dimen(d.doc.block(i).table.cell_padding), 0);
}

// The rectangles of a stretch of a block's text, in the block's own
// coordinates, appended to `out`. `from` and `to` are heights down from the
// block's top: a table's rows wholly outside them may be left out.
//
// PRODUCT RULE (docs/decisions.md D61). In a table the rectangles are made
// cell by cell and kept inside each cell's text area. The reference's are
// made line by line, and a row set on one line is one line: a range through
// several of its cells is one rectangle over the blanks and the padding
// between them, while a wrapped row's is one per cell. Under reference rules
// that is what is answered.
void range_rects(const ImTypesetDocument& d, size_t i, const DocBlock& b,
                 const ImTypesetParagraph& p, const ImTypesetTextRange& range,
                 std::vector<Rect>& out, Scaled from = 0, Scaled to = MAX_BLOCK_EXTENT + 1)
{
    if (range.Start >= range.End)
        return;
    if (!is_table(b.kind) || d.reference_rules) {
        p.model.rects(span_of(b, p, range), out);
        return;
    }
    const TableLayout& table = d.doc.layout(i).table;
    const size_t rows = table_rows(b, table);
    const size_t columns = table_columns(table);
    const int first_cell = cell_at(b, range.Start);
    const int last_cell = cell_at(b, range.End);
    if (rows == 0 || columns == 0 || first_cell < 0 || last_cell < 0)
        return;
    const Scaled padding = padding_of(d, i);
    const size_t last_row = std::min<size_t>(b.cells[static_cast<size_t>(last_cell)].row, rows - 1);
    for (size_t r = b.cells[static_cast<size_t>(first_cell)].row; r <= last_row; ++r) {
        const Scaled row_top = table.row_top[r];
        const Scaled row_bottom = add_saturating(
            row_top, add_saturating(table.rows[r].dims.height, table.rows[r].dims.depth));
        if (row_bottom <= from || row_top >= to)
            continue;
        const size_t have = std::min<size_t>(b.row_first[r + 1] - b.row_first[r], columns);
        for (size_t c = 0; c < have; ++c) {
            CellBox cell;
            if (!cell_box(b, table, r, c, cell))
                continue;
            const DocCell& given = b.cells[cell.cell];
            const int low = std::max(range.Start, static_cast<int>(given.start));
            const int high = std::min(range.End, static_cast<int>(given.end));
            if (low >= high)
                continue;
            Selection span;
            span.start = std::min(std::max(index_of(b, p, low), cell.first), cell.end);
            span.end = high >= static_cast<int>(given.end)
                           ? cell.end
                           : std::min(std::max(index_of(b, p, high), span.start), cell.end);
            const size_t before = out.size();
            p.model.rects(span, out);
            // A rectangle of the model runs to the next item's left edge,
            // which for a cell's last item is across the padding.
            const Scaled area_left = add_saturating(cell.left, padding);
            const Scaled area_right = std::max(sub_saturating(cell.right, padding), area_left);
            size_t kept = before;
            for (size_t k = before; k < out.size(); ++k) {
                Rect rect = out[k];
                const Scaled x0 = std::max(rect.x, area_left);
                const Scaled x1 = std::min(rect.x + rect.width, area_right);
                if (x1 <= x0)
                    continue;
                rect.x = x0;
                rect.width = x1 - x0;
                out[kept++] = rect;
            }
            out.resize(kept);
        }
    }
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
    // After everything there: marks handed over in the order of the text.
    if (inks.empty() || inks.back().end <= over.start) {
        inks.push_back(over);
        return;
    }
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

// --- Marks, found by where they are ----------------------------------------------------------

// A block with more marks on it than this has them looked for by the bytes of
// the lines the clip rectangle shows; with fewer, each is cheaper to work out
// than the lines are to go over.
constexpr size_t MARKS_WORKED_OUT_WHOLE = 16;

// Brings the order the marks are looked for in up to date with the marks of a
// draw: nothing to do when their ranges are the ranges it was made from.
void order_marks(MarkOrder& m, const ImTypesetMark* marks, size_t count)
{
    const auto same_position = [](const ImTypesetPosition& a, const ImTypesetPosition& b) {
        return a.Block == b.Block && a.Offset == b.Offset;
    };
    bool same = m.given.size() == count;
    for (size_t k = 0; same && k < count; ++k)
        same = same_position(m.given[k].Start, marks[k].Range.Start) &&
               same_position(m.given[k].End, marks[k].Range.End);
    if (same)
        return;
    // Nothing is kept as "made from these" until all of it is made.
    m.given.clear();
    ++m.made;
    bool sorted = true;
    for (size_t k = 1; sorted && k < count; ++k)
        sorted = !before(marks[k].Range.Start, marks[k - 1].Range.Start);
    m.order.clear();
    if (!sorted) {
        m.order.resize(count);
        for (size_t k = 0; k < count; ++k)
            m.order[k] = static_cast<uint32_t>(k);
        std::stable_sort(m.order.begin(), m.order.end(), [&](uint32_t a, uint32_t b) {
            return before(marks[a].Range.Start, marks[b].Range.Start);
        });
    }
    m.reach.resize(count);
    for (size_t k = 0; k < count; ++k) {
        const ImTypesetPosition end = marks[m.order.empty() ? k : m.order[k]].Range.End;
        m.reach[k] = k != 0 && !before(m.reach[k - 1], end) ? m.reach[k - 1] : end;
    }
    m.given.resize(count);
    for (size_t k = 0; k < count; ++k)
        m.given[k] = marks[k].Range;
}

// The marks that can have something on bytes `low` to `high` of block `i`,
// into `m.picked`, in the order of `m`: every mark that starts at or before
// the last of those bytes and ends at or after the first. `marks` are the
// marks `m` was last ordered for.
void pick_marks(MarkOrder& m, const ImTypesetMark* marks, size_t i, int low, int high)
{
    m.picked.clear();
    const ImTypesetPosition from = {static_cast<int>(i), low};
    const ImTypesetPosition to = {static_cast<int>(i), high};
    const size_t count = m.reach.size();
    const auto mark_at = [&](size_t place) {
        return m.order.empty() ? static_cast<uint32_t>(place) : m.order[place];
    };
    // No mark before this place reaches `from`; none from `end` on starts by `to`.
    size_t first = 0;
    size_t end = count;
    while (first < end) {
        const size_t middle = first + (end - first) / 2;
        if (before(m.reach[middle], from))
            first = middle + 1;
        else
            end = middle;
    }
    size_t last = first;
    end = count;
    while (last < end) {
        const size_t middle = last + (end - last) / 2;
        if (!before(to, marks[mark_at(middle)].Range.Start))
            last = middle + 1;
        else
            end = middle;
    }
    for (size_t place = first; place < last; ++place) {
        const uint32_t number = mark_at(place);
        if (!before(marks[number].Range.End, from))
            m.picked.push_back(number);
    }
}

// An Under mark's bar on a line of a block whose em is `em`: how thick it is,
// and how far its top is below the line's baseline.
Scaled bar_thickness(const ImTypesetDocument& d, const ImTypesetMark& mark, Scaled em)
{
    return mark.Thickness > 0.0f ? length_sp(d, mark.Thickness)
                                 : std::max(em / 16, length_sp(d, 1.0f));
}

Scaled bar_offset(const ImTypesetDocument& d, const ImTypesetMark& mark, Scaled thickness)
{
    return mark.Offset > 0.0f ? length_sp(d, mark.Offset) : 2 * thickness;
}

// How far below the baseline of its line the bar of an Under mark among
// `marks` can end, and so how far below the line's box and its block's: zero
// with no such mark. A draw looks that much further above the clip rectangle
// for the blocks, the lines and the marks it shows, because a bar a host
// hangs well under a line is inside a clip rectangle the line is wholly above.
Scaled bars_hang(const ImTypesetDocument& d, const ImTypesetMark* marks, size_t count)
{
    Scaled hang = 0;
    for (size_t k = 0; k < count; ++k) {
        if (marks[k].Place != ImTypesetMarkPlace_Under)
            continue;
        const Scaled thickness = bar_thickness(d, marks[k], d.largest_em);
        hang = std::max(hang, add_saturating(bar_offset(d, marks[k], thickness), thickness));
    }
    return hang;
}

// The bytes of block `i` that the lines between two heights of it hold, the
// heights taken generously (by what a draw looks past a clip rectangle for,
// and an em more for a bar under a line): every item of those lines has a
// byte from `low` to `high`, and a range of the text wholly before `low` or
// wholly after `high` covers none of them. False when that cannot be told,
// which is when lines are not in order down the block or the bytes are not in
// order along the items; `low` above `high` when the lines hold no item.
bool bytes_shown(ImTypesetDocument& d, size_t i, const ImTypesetParagraph& view, Scaled from,
                 Scaled to, int& low, int& high)
{
    DocBlock& b = d.blocks[i];
    const BlockLayout& layout = d.doc.layout(i);
    const std::vector<LineSpan>& spans = b.lines.spans;
    const std::vector<uint32_t>& bytes = view.index_bytes;
    if (!b.lines.sorted || spans.empty() || !spans[0].bounded || bytes.empty())
        return false;
    if (b.bytes_seen != layout.serial) {
        b.bytes_sorted = std::is_sorted(bytes.begin(), bytes.end());
        b.bytes_seen = layout.serial;
    }
    if (!b.bytes_sorted)
        return false;
    const Scaled reach =
        add_saturating(add_saturating(b.lines.overhang, coordinate_sp(d, CULL_MARGIN_PX)),
                       std::max<Scaled>(b.font.size, 0));
    const Scaled top = sub_saturating(from, reach);
    const Scaled bottom = add_saturating(to, reach);
    const auto first = std::partition_point(
        spans.begin(), spans.end(), [&](const LineSpan& span) { return span.bottom < top; });
    const auto end = std::partition_point(first, spans.end(),
                                          [&](const LineSpan& span) { return span.top <= bottom; });
    low = 1;
    high = 0;
    if (first == end)
        return true;
    const size_t cmd_end = std::min<size_t>((end - 1)->end, layout.cmds.size());
    uint32_t least = UINT32_MAX;
    uint32_t most = 0;
    for (size_t c = first->first; c < cmd_end; ++c) {
        const DrawCmd& cmd = layout.cmds[c];
        if (cmd.type == CmdType::RULE_RECT)
            continue;
        if (cmd.type == CmdType::LINE) {
            // A line says which indices it holds; one that holds none says none.
            if (cmd.index < cmd.end) {
                least = std::min(least, cmd.index);
                most = std::max(most, cmd.end - 1);
            }
            continue;
        }
        least = std::min(least, cmd.index);
        most = std::max(most, cmd.index);
    }
    if (least > most)
        return true;
    const size_t last = bytes.size() - 1;
    const uint32_t int_max = static_cast<uint32_t>(std::numeric_limits<int>::max());
    // The byte before the one the first item has: a range that ends on the
    // first item's own byte can still end past an item that shares it.
    size_t before_first = std::min<size_t>(least, last);
    while (before_first > 0 && bytes[before_first - 1] == bytes[std::min<size_t>(least, last)])
        --before_first;
    low = before_first > 0 ? static_cast<int>(std::min<uint32_t>(bytes[before_first - 1], int_max))
                           : 0;
    high = static_cast<int>(
        std::min<uint32_t>(bytes[std::min<size_t>(static_cast<size_t>(most) + 1, last)], int_max));
    return true;
}

// One block, with its top-left corner at (`left`, `top`) on the screen.
// `from` and `to` are heights down from the block's top: only its lines whose
// tops are at or after the one and before the other are drawn, which is all
// of them unless a column is cut inside the block. `see_from` and `see_to`
// are the heights the clip rectangle shows: what is highlighted or marked
// wholly outside them need not be worked out.
void draw_block(ImTypesetDocument& d, size_t i, double left, double top, const Target& target,
                const ImTypesetDocumentDraw* draw, const ImTypesetDocumentRange& selection,
                DrawArgs& args, Scaled see_from, Scaled see_to, Scaled from = 0,
                Scaled to = MAX_BLOCK_EXTENT + 1)
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
    // The rule under a table's header; a formula's rule in a cell is ink.
    args.rule_between = 0;
    if (is_table(b.kind))
        args.rule_between = d.options.TableRuleColor != 0 ? shown(d.options.TableRuleColor)
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

    const bool textual = is_lined(b.kind);
    ImTypesetTextRange local = {0, 0};
    bool whole = false;
    if (local_range(d, selection, i, local, &whole)) {
        if (textual && local.Start < local.End) {
            const ImTypesetParagraph& view = view_of(d, i);
            ensure_model(view);
            range_rects(d, i, b, view, local, d.highlights, see_from, see_to);
        } else if (is_host(b.kind) && (whole || local.Start < local.End)) {
            // A host block in a selection is selected whole: its rectangle.
            const Scaled x = d.doc.block(i).style.left_indent;
            d.highlights.push_back(Rect{x, 0, std::max<Scaled>(d.width - x, 0), layout.extent});
        }
    }
    if (draw != nullptr && draw->Marks != nullptr && draw->MarkCount > 0 && textual) {
        // The marks on this block, and of those, when there are many, the
        // ones on the lines the clip rectangle shows.
        MarkOrder& marks = d.marks;
        pick_marks(marks, draw->Marks, i, 0, std::numeric_limits<int>::max());
        if (marks.picked.size() > MARKS_WORKED_OUT_WHOLE && !marks.every_mark) {
            int low = 0;
            int high = 0;
            if (bytes_shown(d, i, view_of(d, i), see_from, see_to, low, high)) {
                if (low <= high)
                    pick_marks(marks, draw->Marks, i, low, high);
                else
                    marks.picked.clear();
            }
        }
        // In the order they were given: a later mark is put over an earlier.
        if (!marks.order.empty())
            std::sort(marks.picked.begin(), marks.picked.end());
        marks.looked_at += marks.picked.size();
        for (const uint32_t number : marks.picked) {
            const ImTypesetMark& mark = draw->Marks[number];
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
            range_rects(d, i, b, view, local, ctx.rects, see_from, see_to);
            if (mark.Place == ImTypesetMarkPlace_Behind) {
                for (const Rect& rect : ctx.rects)
                    d.fills.push_back(Fill{rect, shown(mark.Color)});
                continue;
            }
            // Under: a bar below the baseline of each line the range is on.
            ensure_lines(view);
            const Scaled thickness = bar_thickness(d, mark, b.font.size);
            const Scaled offset = bar_offset(d, mark, thickness);
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

    // A column cut inside the block: what is drawn behind and under its
    // lines goes with the lines.
    const bool cut = from > 0 || to < layout.extent;
    if (cut) {
        const auto outside = [&](const Rect& rect) {
            const Scaled middle = rect.y + rect.height / 2;
            return middle < from || middle >= to;
        };
        d.highlights.erase(std::remove_if(d.highlights.begin(), d.highlights.end(), outside),
                           d.highlights.end());
        d.fills.erase(std::remove_if(d.fills.begin(), d.fills.end(),
                                     [&](const Fill& fill) { return outside(fill.rect); }),
                      d.fills.end());
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
    if (cut) {
        // The commands of the lines that are the column's: each line's start
        // with its LINE, which ends whatever span the one before left open,
        // so a list can be entered at any of them.
        const std::vector<LineSpan>& spans = b.lines.spans;
        const auto first_at = [&](size_t begin, Scaled height) {
            if (b.lines.sorted)
                return static_cast<size_t>(
                    std::partition_point(spans.begin() + static_cast<std::ptrdiff_t>(begin),
                                         spans.end(),
                                         [&](const LineSpan& span) { return span.top < height; }) -
                    spans.begin());
            size_t at = begin;
            while (at < spans.size() && spans[at].top < height)
                ++at;
            return at;
        };
        const size_t first = first_at(0, from);
        const size_t end = first_at(first, to);
        const size_t first_cmd = first < spans.size() ? spans[first].first : layout.cmds.size();
        const size_t end_cmd = end < spans.size() ? spans[end].first : layout.cmds.size();
        run.cmds = layout.cmds.data() + std::min(first_cmd, layout.cmds.size());
        run.count = end_cmd > first_cmd ? end_cmd - first_cmd : 0;
        run.lines = nullptr;
        if (run.count == 0 && d.highlights.empty() && d.fills.empty())
            return;
    }
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

// The bars of a unit whose top-left corner is at (`left`, `unit_top`).
void draw_bars(ImTypesetDocument& d, const DocumentUnit& unit, double left, double unit_top,
               const Target& target, const DrawArgs& args)
{
    ImTypesetContext& ctx = *d.ctx;
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
}

// The columns the clip rectangle shows, each the stretch of the uncut column
// it holds, moved to its place on the page. `window_top` and `window_bottom`
// are the clip's, down from the page's top.
void draw_columns(ImTypesetDocument& d, const Target& target, double left, double top,
                  const ImVec4& clip, Scaled window_top, Scaled window_bottom,
                  const ImTypesetDocumentDraw* draw, const ImTypesetDocumentRange& selection,
                  DrawArgs& args, const ErrorState*& trouble, size_t& blocks)
{
    const Scaled pitch = pitch_of(d);
    const Scaled margin = coordinate_sp(d, WINDOW_MARGIN_PX);
    const Scaled x0 = coordinate_sp(d, static_cast<double>(clip.x) - left);
    const Scaled x1 = coordinate_sp(d, static_cast<double>(clip.z) - left);
    // Column k is [k * pitch, k * pitch + width): the first whose right edge
    // is past the clip's left, to the last whose left edge is before its right.
    const Scaled first_k = std::max<Scaled>(
        floor_div(sub_saturating(x0, add_saturating(d.width, margin)), pitch) + 1, 0);
    const Scaled last_k = ceil_div(add_saturating(x1, margin), pitch) - 1;
    for (Scaled at = first_k; at <= last_k; ++at) {
        const size_t k = static_cast<size_t>(at);
        if (!d.columns.cut_through(d.doc, d.composer, k))
            break;
        // A copy: laying a block out below can cut further columns.
        const DocumentColumn column = d.columns.column(k);
        if (column.empty)
            continue;
        const Scaled dy = sub_saturating(column.head, column.top);
        const Scaled from = std::max(column.top, sub_saturating(window_top, dy));
        const Scaled to = std::min(column.limit, sub_saturating(window_bottom, dy));
        if (from >= to)
            continue;
        const double column_left = left + px_of(d, static_cast<Scaled>(k) * pitch);
        size_t u = std::max<size_t>(column.first_unit, d.doc.unit_at(from));
        if (u >= d.doc.unit_count())
            continue;
        Scaled y = d.doc.unit_top(u);
        for (; u < d.doc.unit_count() && y < to; ++u) {
            const DocumentUnit& unit = d.doc.unit(u);
            // A quoted run is one box, in one column.
            if (y >= column.top)
                draw_bars(d, unit, column_left, top + px_of(d, add_saturating(y, dy)), target,
                          args);
            for (size_t i = unit.first_block; i < unit.first_block + unit.block_count; ++i) {
                const Scaled block_top = add_saturating(y, d.doc.top_in_unit(i));
                const Scaled extent = d.doc.layout(i).extent;
                // Wholly an earlier column's, or a later one's.
                if (block_top >= column.limit ||
                    (add_saturating(block_top, extent) <= column.top && block_top < column.top))
                    continue;
                sync(d, i);
                ++blocks;
                if (trouble == nullptr && !d.blocks[i].error.ok())
                    trouble = &d.blocks[i].error;
                draw_block(d, i, column_left, top + px_of(d, add_saturating(block_top, dy)), target,
                           draw, selection, args, sub_saturating(from, block_top),
                           sub_saturating(to, block_top), sub_saturating(column.top, block_top),
                           sub_saturating(column.limit, block_top));
            }
            y += unit.extent;
            if (u + 1 < d.doc.unit_count())
                y += d.doc.unit(u + 1).gap;
        }
    }
}

// What in view could not be set as asked: the first such block's error.
bool draw_document(ImTypesetDocument& d, const Target& target, const ImVec2& pos,
                   const ImVec4& clip, const ImTypesetDocumentDraw* draw)
{
    ImTypesetContext& ctx = *d.ctx;
    ready(d);
    const double left = is_finite(pos.x) ? static_cast<double>(pos.x) : 0.0;
    const double top = is_finite(pos.y) ? static_cast<double>(pos.y) : 0.0;
    if (!(clip.w > clip.y) || !(clip.z > clip.x) || d.blocks.empty())
        return succeed();
    const bool marked = draw != nullptr && draw->Marks != nullptr && draw->MarkCount > 0;
    const Scaled hang =
        marked ? bars_hang(d, draw->Marks, static_cast<size_t>(draw->MarkCount)) : 0;
    const Scaled window_top = sub_saturating(
        sub_saturating(coordinate_sp(d, static_cast<double>(clip.y) - top - WINDOW_MARGIN_PX),
                       d.exact_reach),
        hang);
    const Scaled window_bottom = add_saturating(
        coordinate_sp(d, static_cast<double>(clip.w) - top + WINDOW_MARGIN_PX), d.exact_reach);

    const UnitRange range = d.columned ? UnitRange() : layout_window(d, window_top, window_bottom);
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

    d.marks.looked_at = 0;
    if (marked)
        order_marks(d.marks, draw->Marks, static_cast<size_t>(draw->MarkCount));

    const ErrorState* trouble = nullptr;
    size_t blocks = 0;
    if (d.columned)
        draw_columns(d, target, left, top, clip, window_top, window_bottom, draw, selection, args,
                     trouble, blocks);
    Scaled y = range.first < range.end ? d.doc.unit_top(range.first) : 0;
    for (size_t u = range.first; u < range.end; ++u) {
        const DocumentUnit& unit = d.doc.unit(u);
        const double unit_top = top + px_of(d, y);
        draw_bars(d, unit, left, unit_top, target, args);
        for (size_t i = unit.first_block; i < unit.first_block + unit.block_count; ++i) {
            sync(d, i);
            ++blocks;
            if (trouble == nullptr && !d.blocks[i].error.ok())
                trouble = &d.blocks[i].error;
            const Scaled block_top = add_saturating(y, d.doc.top_in_unit(i));
            draw_block(d, i, left, unit_top + px_of(d, d.doc.top_in_unit(i)), target, draw,
                       selection, args, sub_saturating(window_top, block_top),
                       sub_saturating(window_bottom, block_top));
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
    const Point at = uncut_of(d, x, y);
    Scaled top = 0;
    out.block = block_at(d, at.y, &out.in_block, &top);
    out.local.x = at.x;
    out.local.y = at.y - top;
    return out;
}

ImTypesetPosition position_at(ImTypesetDocument& d, Scaled y)
{
    ImTypesetPosition out = {0, 0};
    bool inside = false;
    Scaled top = 0;
    const int block = block_at(d, y, &inside, &top);
    if (block < 0)
        return out;
    out.Block = block;
    const size_t i = static_cast<size_t>(block);
    const DocBlock& b = d.blocks[i];
    if (!is_lined(b.kind))
        return out;
    const ImTypesetParagraph& p = view_of(d, i);
    ensure_lines(p);
    const Scaled local = y - top;
    const auto line = std::partition_point(p.line_boxes.begin(), p.line_boxes.end(),
                                           [&](const LineBox& box) { return box.bottom <= local; });
    const int size = text_size_of(b);
    out.Offset = line != p.line_boxes.end() ? std::min(byte_at_index(p, line->index), size) : size;
    return out;
}

// PRODUCT RULE (docs/decisions.md D60). In a table a point answers the cell
// whose rectangle holds it. The reference's model splits every item of a row
// at its middle, the glue that fills a cell included, and lets an item reach
// to the next one across the padding and the column's edge: the far half of a
// cell's blank answers the next cell and, in the last column, the next row.
// That index is still what `SelectionModel::hit` returns. Here only the
// cell's own items are asked, on the line of the cell nearest the point, each
// split at its middle as the reference splits it, the last of them ending
// where the cell's text area does; and the byte is held between the cell's
// own first and last.
uint32_t hit_in_cell(const DocBlock& b, const ImTypesetParagraph& p, const TableLayout& table,
                     const CellBox& cell, Scaled padding, Point at)
{
    const size_t cmd_first = cell.row == 0 ? 0 : table.rows[cell.row - 1].out_cmd_end;
    const size_t cmd_end = std::min<size_t>(table.rows[cell.row].out_cmd_end, p.cmds.size());
    const auto in_cell = [&](uint32_t index) { return index >= cell.first && index < cell.end; };
    // The line: in a row set on one line, the row's own, which is the row's
    // first command and of which the cell has a stretch; in a wrapped row,
    // the nearest in height of the cell's lines.
    const bool whole_row = !table.rows[cell.row].wrapped;
    size_t line = cmd_end;
    Scaled line_far = 0;
    for (size_t c = cmd_first; c < cmd_end; ++c) {
        const DrawCmd& cmd = p.cmds[c];
        if (!cmd.is(CmdType::LINE))
            continue;
        if (whole_row) {
            line = c;
            break;
        }
        if (c == cmd_first || !in_cell(cmd.index) || cmd.end > cell.end)
            continue;
        const Scaled far = std::max<Scaled>(
            std::max(cmd.y - cmd.height - at.y, at.y - (cmd.y + cmd.depth) + 1), 0);
        if (line == cmd_end || far < line_far) {
            line = c;
            line_far = far;
        }
    }
    uint32_t low = 0;
    uint32_t high = 0;
    own_bytes(b, p, cell, low, high);
    if (line == cmd_end)
        return low;
    const DrawCmd& held = p.cmds[line];
    const Scaled text_end =
        whole_row ? sub_saturating(cell.right, padding) : add_saturating(held.x, held.width);

    // The boundary: before the first item for a point left of it, after the
    // last for one right of the text's end, and otherwise at the nearer edge
    // of the item under the point.
    uint32_t boundary = cell.end;
    bool any = false;
    bool settled = false;
    Scaled left = 0;
    uint32_t index = 0;
    for (size_t c = line + 1; c < cmd_end && !settled; ++c) {
        const DrawCmd& cmd = p.cmds[c];
        if (cmd.is(CmdType::LINE)) {
            if (whole_row)
                continue;
            break;
        }
        if (!is_item(cmd) || !in_cell(cmd.index) || (any && cmd.index == index))
            continue;
        if (!any) {
            if (at.x < cmd.x) {
                boundary = cmd.index;
                settled = true;
            }
        } else if (at.x < cmd.x) {
            boundary = index + (at.x - left < cmd.x - at.x ? 0u : 1u);
            settled = true;
        }
        any = true;
        left = cmd.x;
        index = cmd.index;
    }
    if (any && !settled) {
        const Scaled right = std::max(text_end, left);
        boundary = at.x < right ? index + (at.x - left < right - at.x ? 0u : 1u) : index + 1;
    }
    const uint32_t byte =
        boundary < cell.end && boundary < p.index_bytes.size() ? p.index_bytes[boundary] : high;
    return std::min(std::max(byte, low), high);
}

// Where a caret at a byte of a table's text stands: in the cell the byte is
// in (the later of two that share it), on the line of that cell the byte's
// item is on and as tall as that line, not the row. The byte that ends a
// cell's text is the right edge of the last thing the text set, wherever the
// alignment put it; in an empty cell, where its text would begin. False if
// the table has no such cell laid out.
bool table_caret(const ImTypesetDocument& d, size_t i, const DocBlock& b,
                 const ImTypesetParagraph& p, int offset, Scaled& x, Scaled& top, Scaled& bottom)
{
    const BlockLayout& layout = d.doc.layout(i);
    const TableLayout& table = layout.table;
    const int k = cell_at(b, offset);
    if (k < 0)
        return false;
    const DocCell& given = b.cells[static_cast<size_t>(k)];
    CellBox cell;
    if (!cell_box(b, table, given.row, given.column, cell) || cell.column != given.column ||
        cell.first >= cell.end || cell.end >= p.index_bytes.size())
        return false;
    const uint32_t cell_end = std::max(given.end, given.start);
    const uint32_t byte =
        std::min(std::max(offset <= 0 ? 0u : static_cast<uint32_t>(offset), given.start), cell_end);
    // The cell's items carry ascending bytes, and the item a byte names is
    // the last of those at the first byte at or after it: a character
    // follows the glue that shares its byte.
    const auto first = p.index_bytes.begin() + cell.first;
    const auto end = p.index_bytes.begin() + cell.end;
    auto found = std::lower_bound(first, end, byte);
    const bool at_end = found == end || *found >= cell_end;
    if (!at_end)
        found = std::upper_bound(found, end, *found) - 1;
    const uint32_t index = at_end ? 0 : static_cast<uint32_t>(found - p.index_bytes.begin());

    const bool whole_row = !table.rows[cell.row].wrapped;
    const Scaled padding = padding_of(d, i);
    const size_t cmd_first = cell.row == 0 ? 0 : table.rows[cell.row - 1].out_cmd_end;
    const size_t cmd_end = std::min<size_t>(table.rows[cell.row].out_cmd_end, p.cmds.size());
    // Where the text of the line whose LINE command is at `line` may reach.
    const auto area = [&](size_t line, Scaled& from, Scaled& to) {
        const DrawCmd& held = p.cmds[line];
        from = whole_row ? add_saturating(cell.left, padding) : held.x;
        to = whole_row ? sub_saturating(cell.right, padding) : add_saturating(held.x, held.width);
        to = std::max(to, from);
    };
    const auto is_text = [](const DrawCmd& cmd) {
        return cmd.is(CmdType::GLYPH) || cmd.is(CmdType::ATOM) ||
               (cmd.is(CmdType::SET_GLUE) && cmd.natural() != 0);
    };
    size_t line = cmd_end;       // the innermost LINE the walk is in
    size_t caret_line = cmd_end; // and the one the caret is on
    size_t first_line = cmd_end; // the first that holds an item of the cell
    bool placed = false;
    bool texted = false;
    bool followed = false;
    for (size_t c = cmd_first; c < cmd_end; ++c) {
        const DrawCmd& cmd = p.cmds[c];
        if (cmd.is(CmdType::LINE)) {
            line = c;
            continue;
        }
        if (!is_item(cmd) || cmd.index < cell.first || cmd.index >= cell.end || line == cmd_end)
            continue;
        if (first_line == cmd_end)
            first_line = line;
        if (!at_end) {
            if (cmd.index == index) {
                x = cmd.x;
                caret_line = line;
                placed = true;
                break;
            }
            continue;
        }
        if (is_text(cmd)) {
            texted = true;
            followed = false;
            caret_line = line;
        } else if (texted && !followed && line == caret_line) {
            // The glue after the text: the text ends where it begins.
            x = cmd.x;
            followed = true;
        }
    }
    if (at_end) {
        Scaled from = 0;
        Scaled to = 0;
        if (texted) {
            area(caret_line, from, to);
            if (!followed)
                x = to;
        } else {
            if (first_line == cmd_end)
                return false;
            caret_line = first_line;
            area(caret_line, from, to);
            const std::vector<ColumnAlign>& aligns = d.doc.block(i).table.aligns;
            const ColumnAlign align =
                cell.column < aligns.size() ? aligns[cell.column] : ColumnAlign::LEFT;
            x = align == ColumnAlign::RIGHT
                    ? to
                    : (align == ColumnAlign::CENTER ? from + (to - from) / 2 : from);
        }
        placed = true;
    }
    if (!placed || caret_line == cmd_end)
        return false;
    const DrawCmd& held = p.cmds[caret_line];
    top = held.y - held.height;
    bottom = held.y + held.depth;
    return true;
}

ImTypesetDocumentHit hit_test(ImTypesetDocument& d, const ImVec2& local_pos)
{
    ImTypesetDocumentHit out = {{0, 0}, -1, 0, false, false, -1, -1};
    const Probe at = probe(d, local_pos);
    if (at.block < 0)
        return out;
    const size_t i = static_cast<size_t>(at.block);
    const DocBlock& b = d.blocks[i];
    out.Position.Block = at.block;
    out.InBlock = at.in_block;
    if (!is_lined(b.kind)) {
        out.Position.Offset = 2 * at.local.y < d.doc.layout(i).extent ? 0 : text_size_of(b);
        return out;
    }
    const ImTypesetParagraph& p = view_of(d, i);
    ensure_lines(p);
    if (p.line_boxes.empty())
        return out;
    ensure_model(p);
    uint32_t index = p.model.hit(at.local);
    out.Position.Offset = std::min(byte_at_index(p, index), text_size_of(b));
    CellBox under_cell;
    if (is_table(b.kind) && cell_under(b, d.doc.layout(i).table, at.local, under_cell)) {
        // Row and Column are the cell whose rectangle holds the point. The
        // position is a byte of that cell under the product's rule, and the
        // reference's index's, whatever cell that is in, under its own.
        out.Row = static_cast<int>(under_cell.row);
        out.Column = static_cast<int>(under_cell.column);
        if (product_hits(d)) {
            out.Position.Offset =
                std::min(static_cast<int>(hit_in_cell(b, p, d.doc.layout(i).table, under_cell,
                                                      padding_of(d, i), at.local)),
                         text_size_of(b));
            index = index_of(b, p, out.Position.Offset);
        }
    }
    out.Line = static_cast<int>(line_of_index(p, index) - p.line_boxes.begin());
    const auto under =
        std::partition_point(p.line_boxes.begin(), p.line_boxes.end(),
                             [&](const LineBox& box) { return box.bottom <= at.local.y; });
    out.Inside = under != p.line_boxes.end() && at.local.y >= under->top &&
                 at.local.x >= under->x0 && at.local.x < under->x1;
    LineBox line_under = under != p.line_boxes.end() ? *under : LineBox();
    if (is_table(b.kind) && under != p.line_boxes.end() && at.local.y >= under->top) {
        // A row is one span of commands, and where a cell wrapped it holds
        // the lines of every cell, side by side. The line the point is on is
        // the innermost LINE whose box holds it: a nested line follows the
        // line it is in, so the last such command.
        // A line that holds only lines (the row itself, then) is not one.
        bool found = false;
        const size_t end = std::min<size_t>(under->end, p.cmds.size());
        for (size_t c = under->first; c < end; ++c) {
            const DrawCmd& cmd = p.cmds[c];
            if (!cmd.is(CmdType::LINE) || at.local.x < cmd.x || at.local.x >= cmd.x + cmd.width ||
                at.local.y < cmd.y - cmd.height || at.local.y >= cmd.y + cmd.depth)
                continue;
            if (c + 1 < end && p.cmds[c + 1].is(CmdType::LINE))
                continue;
            found = true;
            line_under.first = static_cast<uint32_t>(c);
            size_t next = c + 1;
            while (next < end && !p.cmds[next].is(CmdType::LINE))
                ++next;
            line_under.end = static_cast<uint32_t>(next);
        }
        out.Inside = found;
    }
    if (out.Inside) {
        // The run the character under the point is in, which is not always
        // the one the caret is before.
        const uint32_t under_byte = static_cast<uint32_t>(
            std::max(byte_under(p, line_under, at.local.x, out.Position.Offset), 0));
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
    if (!is_lined(b.kind)) {
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

// A table's last cell is to be made again: its text grew. What its material
// left beside the block is taken back to where the cell began.
void reopen_last_cell(ImTypesetDocument& d, size_t i)
{
    DocBlock& b = d.blocks[i];
    b.has_material = false;
    if (!b.has_spec || b.cells_built != b.cells.size() || b.cells_built == 0)
        return;
    Block& block = d.doc.edit_text(i);
    block.table.reopen_last_cell();
    const uint32_t nodes = static_cast<uint32_t>(block.table.nodes.size());
    b.source_offsets.resize(std::min<size_t>(b.source_offsets.size(), nodes));
    while (!b.ink_spans.empty() && b.ink_spans.back().first >= nodes)
        b.ink_spans.pop_back();
    while (!b.atoms.empty() && b.atoms.back().node >= nodes)
        b.atoms.pop_back();
    b.display_text.resize(std::min<size_t>(b.display_text.size(), b.last_cell_display));
    d.dead_boxes += b.last_cell_boxes;
    d.live_boxes -= std::min<size_t>(d.live_boxes, b.last_cell_boxes);
    b.atom_boxes -= std::min(b.atom_boxes, b.last_cell_boxes);
    b.last_cell_boxes = 0;
    --b.cells_built;
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

// A rectangle of a block whose top is at `top` of the uncut column, where it
// is shown: in a column, by the column its middle is in.
void append_px(ImTypesetDocument& d, const Rect& rect, Scaled top, ImVector<ImVec4>* out)
{
    const Shown at = shown_at(d, top + rect.y + rect.height / 2);
    const Scaled x = rect.x + at.dx;
    const Scaled y = top + rect.y + at.dy;
    out->push_back(ImVec4(pxf(d, x), pxf(d, y), pxf(d, x + rect.width), pxf(d, y + rect.height)));
}

// How many columns a document shown in columns has: see GetDocumentColumnCount.
size_t column_count(ImTypesetDocument& d)
{
    // Balanced columns are all cut or none; in order, the first is enough to
    // bring the cuts up to date with the document.
    d.columns.cut_through(d.doc, d.composer, 0);
    // With every block laid out, cutting the rest lays nothing out, and the
    // count is then the count: the estimate is the height left over in
    // columns, which takes a piece that cannot be cut and is many columns
    // tall (a table row that wrapped) for that many columns, and a view
    // scrolled to the end of such a page would show columns that are not
    // there.
    if (!d.columns.finished() && d.doc.settled())
        d.columns.cut_all(d.doc, d.composer);
    return std::max<size_t>(d.columns.estimated_count(d.doc), 1);
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

DocumentColumns& block_columns(ImTypesetDocument& document)
{
    ready(document);
    return document.columns;
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

bool DebugCheckTableDataLayout(size_t sz_cell, size_t sz_column, size_t sz_table,
                               size_t sz_column_geometry, size_t sz_row_geometry,
                               size_t sz_cell_position)
{
    const struct {
        const char* name;
        size_t given;
        size_t own;
    } structs[] = {
        {"ImTypesetCell", sz_cell, sizeof(ImTypesetCell)},
        {"ImTypesetTableColumn", sz_column, sizeof(ImTypesetTableColumn)},
        {"ImTypesetTableGeometry", sz_table, sizeof(ImTypesetTableGeometry)},
        {"ImTypesetTableColumnGeometry", sz_column_geometry, sizeof(ImTypesetTableColumnGeometry)},
        {"ImTypesetTableRowGeometry", sz_row_geometry, sizeof(ImTypesetTableRowGeometry)},
        {"ImTypesetCellPosition", sz_cell_position, sizeof(ImTypesetCellPosition)},
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

bool DebugCheckColumnDataLayout(size_t sz_columns, size_t sz_column_geometry)
{
    const struct {
        const char* name;
        size_t given;
        size_t own;
    } structs[] = {
        {"ImTypesetColumns", sz_columns, sizeof(ImTypesetColumns)},
        {"ImTypesetColumnGeometry", sz_column_geometry, sizeof(ImTypesetColumnGeometry)},
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
    // The widths a held table had, for the table that takes its place.
    std::vector<Scaled> held_widths;
    DocBlock::HeldUnder held_under;
    if (removed == 1 && incoming == 1 && is_table(d->blocks[at].kind) &&
        (d->blocks[at].flags & ImTypesetBlockFlags_TableHeld) != 0) {
        held_widths.swap(d->blocks[at].held);
        held_under = d->blocks[at].held_under;
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
        DocBlock& described = d->blocks[at + k];
        if (is_table(described.kind) && (described.flags & ImTypesetBlockFlags_TableHeld) != 0 &&
            held_widths.size() == described.columns) {
            described.held.swap(held_widths);
            described.held_under = held_under;
        }
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
    if (!is_lined(b.kind))
        return fail(ImTypesetError_InvalidArgument, "the last block is not one that holds text");
    if (is_table(b.kind) && (b.cells.empty() || b.cells.back().end != b.text.size()))
        return fail(ImTypesetError_InvalidArgument,
                    "the table's last cell does not end where its text ends: there is no cell "
                    "to append to");
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
    if (is_text(b.kind) || is_table(b.kind)) {
        ErrorState run_trouble;
        validate_runs(b.runs, runs, runs_count, b.text.size(),
                      b.runs.empty() ? 0 : b.runs.back().end, run_trouble);
        if (trouble.ok())
            trouble = run_trouble;
    }
    if (is_table(b.kind)) {
        b.cells.back().end = static_cast<uint32_t>(b.text.size());
        if (b.text_error.ok())
            b.text_error = trouble;
        reopen_last_cell(*d, i);
        return report(trouble);
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

bool AppendDocumentTableCells(ImTypesetDocument* document, const char* text, const char* text_end,
                              const ImTypesetCell* cells, int cells_count, const ImTypesetRun* runs,
                              int runs_count)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    if (d->blocks.empty() || !is_table(d->blocks.back().kind))
        return fail(ImTypesetError_InvalidArgument, "the last block is not a table");
    const size_t i = d->blocks.size() - 1;
    DocBlock& b = d->blocks[i];
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
    ErrorState run_trouble;
    validate_runs(b.runs, runs, runs_count, b.text.size(), b.runs.empty() ? 0 : b.runs.back().end,
                  run_trouble);
    if (trouble.ok())
        trouble = run_trouble;
    const uint32_t rows_before = b.row_count();
    const uint32_t columns_before = b.columns;
    add_cells(b, cells, cells_count, b.text.size(), [&](const char* what) {
        if (trouble.ok())
            trouble.set(ImTypesetError_DescriptionCut, "block " + std::to_string(i) + ": " + what);
    });
    if (b.text_error.ok())
        b.text_error = trouble;
    b.has_material = false;
    if (b.has_spec) {
        // Whether a table sets a box at all is part of what the gaps round it
        // were computed from.
        Block& block =
            rows_before == 0 && b.row_count() != 0 ? d->doc.edit(i) : d->doc.edit_text(i);
        block.length_hint = length_hint(b);
        if (b.columns != columns_before && apply_columns(*d, b, block.table))
            block.table.config = table_stamp();
    }
    return report(trouble);
}

bool SetDocumentTableColumns(ImTypesetDocument* document, int block,
                             const ImTypesetTableColumn* columns, int columns_count)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    if (block < 0 || static_cast<size_t>(block) >= d->blocks.size() ||
        !is_table(d->blocks[static_cast<size_t>(block)].kind))
        return fail(ImTypesetError_InvalidArgument, "not a table of the document");
    DocBlock& b = d->blocks[static_cast<size_t>(block)];
    b.column_specs.clear();
    if (columns != nullptr && columns_count > 0)
        b.column_specs.assign(
            columns,
            columns + std::min<size_t>(static_cast<size_t>(columns_count), MAX_TABLE_COLUMNS));
    if (b.has_spec) {
        Block& changed = d->doc.edit_text(static_cast<size_t>(block));
        if (apply_columns(*d, b, changed.table))
            changed.table.config = table_stamp();
    }
    return succeed();
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
    // A table's padding and rule are the options'.
    for (DocBlock& b : d->blocks) {
        if (is_table(b.kind) && b.has_spec) {
            b.has_spec = false;
            d->specs_stale = true;
        }
    }
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
    // A height a view lent the columns is the view's to lend again.
    d->view_height = 0.0f;
    take_style(*d, style != nullptr ? *style : ctx.style, width);
    return succeed();
}

bool LayoutDocumentWindow(ImTypesetDocument* document, float top, float bottom)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    ready(*d);
    const Scaled window_bottom = coordinate_sp(*d, is_finite(bottom) ? bottom : 0.0);
    const UnitRange range =
        layout_window(*d, coordinate_sp(*d, is_finite(top) ? top : 0.0), window_bottom);
    // The columns that show those heights.
    if (d->columned)
        d->columns.column_at(d->doc, d->composer, window_bottom);
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
    if (d->columned) {
        const Scaled count = static_cast<Scaled>(column_count(*d));
        const Scaled across =
            add_saturating(count * d->width, (count - 1) * std::max<Scaled>(d->gutter, 0));
        // As tall as the columns, and as what an overfull one holds: the
        // whole of the page is then inside what a view scrolls over.
        return ImVec2(ceil_px(px_of(*d, across)), ceil_px(px_of(*d, d->columns.reach())));
    }
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
    out.TableRowsSet = d->composer.table_rows_set;
    out.ColumnsCut = d->columns.columns_cut;
    out.BalanceHeightsTried = d->columns.heights_tried;
    out.Blocks = static_cast<int>(d->blocks.size());
    for (size_t i = 0; i < d->blocks.size(); ++i)
        out.BlocksDeferred += d->doc.laid_out(i) ? 0 : 1;
    return out;
}

bool DrawDocument(ImTypesetDocument* document, ImDrawList* draw_list, const ImVec2& pos,
                  const ImTypesetDocumentDraw* draw)
{
    if (!can_draw(document))
        return false;
    if (draw_list == nullptr) {
        // Null is the current window's. NewFrame invalidates the draw data and
        // Render makes it valid again: the one sign imgui.h gives of being
        // inside a frame, which is where there is a current window.
        if (ImGui::GetFrameCount() <= 0 || ImGui::GetDrawData() != nullptr)
            return fail(ImTypesetError_InvalidArgument,
                        "DrawDocument: no draw list was given, which means the current "
                        "window's, and this is outside a frame");
        draw_list = ImGui::GetWindowDrawList();
    }
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
        return ImTypesetDocumentHit{{0, 0}, -1, 0, false, false, -1, -1};
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
        const Scaled top = settled_top(*d, i);
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
        if ((!is_lined(b.kind)) || local.Start >= local.End)
            continue;
        const ImTypesetParagraph& p = view_of(*d, i);
        ensure_model(p);
        ImTypesetContext& ctx = *d->ctx;
        ctx.rects.clear();
        range_rects(*d, i, b, p, local, ctx.rects);
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
    const Scaled top = settled_top(*d, i);
    if (!is_lined(b.kind)) {
        const Scaled extent = d->doc.layout(i).extent;
        const Scaled x = d->doc.block(i).style.left_indent;
        const Scaled y = position.Offset > 0 ? extent : 0;
        const Shown at = shown_at(*d, top + (y > 0 ? y - 1 : 0));
        return ImVec4(pxf(*d, x + at.dx), pxf(*d, top + y + at.dy), pxf(*d, x + at.dx),
                      pxf(*d, top + y + at.dy));
    }
    const ImTypesetParagraph& p = view_of(*d, i);
    ensure_lines(p);
    if (p.line_boxes.empty())
        return none;
    if (is_table(b.kind)) {
        Scaled x = 0;
        Scaled line_top = 0;
        Scaled line_bottom = 0;
        if (table_caret(*d, i, b, p, position.Offset, x, line_top, line_bottom)) {
            const Shown at = shown_at(*d, top + line_top + (line_bottom - line_top) / 2);
            return ImVec4(pxf(*d, x + at.dx), pxf(*d, top + line_top + at.dy), pxf(*d, x + at.dx),
                          pxf(*d, top + line_bottom + at.dy));
        }
    }
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
    const Shown at = shown_at(*d, top + line->top + (line->bottom - line->top) / 2);
    return ImVec4(pxf(*d, x + at.dx), pxf(*d, top + line->top + at.dy), pxf(*d, x + at.dx),
                  pxf(*d, top + line->bottom + at.dy));
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
        } else if ((is_text(b.kind) || is_table(b.kind)) && local.Start < local.End &&
                   layout_block(*d, block)) {
            const ImTypesetParagraph& p = view_of(*d, i);
            ensure_model(p);
            TextData data;
            data.separators = p.separators.data();
            data.separator_count = p.separators.size();
            data.atoms = p.atom_texts.data();
            data.atom_count = p.atom_texts.size();
            data.originals = p.originals.data();
            data.original_count = p.originals.size();
            if (is_table(b.kind) && !d->reference_rules) {
                // Cell by cell, each as the model gives the part of the range
                // in it, with a newline before a cell that starts a row and a
                // tab before any other: every cell from the one the range
                // starts in to the one it ends in, an empty one included, so
                // that a copy of a table written as "Tables" advises is the
                // table's text. The reference writes a separator only between
                // two texts (an empty first cell loses its tab) and pads a row
                // with tabs for the cells it lacks: that is the branch below,
                // which the replays of its goldens take.
                int first = cell_at(b, local.Start);
                int last = cell_at(b, local.End);
                // A range that ends where a cell's text starts has nothing
                // of the cell, unless the cell is empty and that is all of it.
                if (last > first) {
                    const DocCell& end_cell = b.cells[static_cast<size_t>(last)];
                    if (static_cast<uint32_t>(local.End) <= end_cell.start &&
                        end_cell.start < end_cell.end)
                        --last;
                }
                data.separators = nullptr;
                data.separator_count = 0;
                std::string piece;
                for (int k = first; k >= 0 && k <= last; ++k) {
                    const DocCell& cell = b.cells[static_cast<size_t>(k)];
                    if (k != first)
                        ctx.scratch += cell.column == 0 ? '\n' : '\t';
                    ImTypesetTextRange inside;
                    inside.Start = std::max(local.Start, static_cast<int>(cell.start));
                    inside.End = std::min(local.End, static_cast<int>(cell.end));
                    if (inside.Start >= inside.End)
                        continue;
                    piece.clear();
                    p.model.text(selection_of(p, inside), data, piece);
                    ctx.scratch += piece;
                }
            } else {
                p.model.text(selection_of(p, local), data, ctx.scratch);
            }
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
    if (is_lined(b.kind)) {
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
    if (!is_lined(b.kind))
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

ImTypesetTableGeometry GetDocumentTableGeometry(ImTypesetDocument* document, int block)
{
    ImTypesetTableGeometry out = {0, 0, 0, ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), false, false};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size() ||
        !is_table(d->blocks[static_cast<size_t>(block)].kind))
        return out;
    ready(*d);
    if (!layout_block(*d, block))
        return out;
    const size_t i = static_cast<size_t>(block);
    const DocBlock& b = d->blocks[i];
    const TableLayout& table = d->doc.layout(i).table;
    const Scaled top = d->doc.block_top(i);
    Scaled across = 0;
    for (const Scaled width : table.columns)
        across = add_saturating(across, width);
    const size_t rows = table.rows.size();
    out.RowCount = static_cast<int>(rows);
    out.ColumnCount = static_cast<int>(table.columns.size());
    out.CellCount = static_cast<int>(b.cells.size());
    out.Min = ImVec2(pxf(*d, table.indent), pxf(*d, top));
    Scaled bottom = 0;
    if (rows != 0)
        bottom = table.row_top[rows - 1] + table.rows[rows - 1].dims.height +
                 table.rows[rows - 1].dims.depth;
    out.Max = ImVec2(pxf(*d, table.indent + across), pxf(*d, top + bottom));
    out.Stated = table.stated;
    out.LaidOut = true;
    return out;
}

ImTypesetTableColumnGeometry GetDocumentTableColumn(ImTypesetDocument* document, int block,
                                                    int column)
{
    ImTypesetTableColumnGeometry out = {0.0f, 0.0f, 0.0f, 0.0f};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size() ||
        !is_table(d->blocks[static_cast<size_t>(block)].kind) || column < 0)
        return out;
    ready(*d);
    if (!layout_block(*d, block))
        return out;
    const size_t i = static_cast<size_t>(block);
    const size_t c = static_cast<size_t>(column);
    const Scaled floor = d->doc.table_floor(d->composer, i, c);
    const TableLayout& table = d->doc.layout(i).table;
    if (c >= table.columns.size())
        return out;
    Scaled left = table.indent;
    for (size_t k = 0; k < c; ++k)
        left = add_saturating(left, table.columns[k]);
    out.Min = pxf(*d, left);
    out.Max = pxf(*d, add_saturating(left, table.columns[c]));
    out.Natural = c < table.widest.size() ? pxf(*d, table.widest[c]) : 0.0f;
    out.Floor = pxf(*d, floor);
    return out;
}

ImTypesetTableRowGeometry GetDocumentTableRow(ImTypesetDocument* document, int block, int row)
{
    ImTypesetTableRowGeometry out = {0.0f, 0.0f, 0, 0, false};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size() ||
        !is_table(d->blocks[static_cast<size_t>(block)].kind) || row < 0)
        return out;
    ready(*d);
    if (!layout_block(*d, block))
        return out;
    const size_t i = static_cast<size_t>(block);
    const size_t r = static_cast<size_t>(row);
    const DocBlock& b = d->blocks[i];
    const TableLayout& table = d->doc.layout(i).table;
    if (r >= table.rows.size() || r + 1 >= b.row_first.size())
        return out;
    const Scaled top = d->doc.block_top(i) + table.row_top[r];
    out.Min = pxf(*d, top);
    out.Max = pxf(*d, top + table.rows[r].dims.height + table.rows[r].dims.depth);
    out.CellStart = static_cast<int>(b.row_first[r]);
    out.CellCount = static_cast<int>(b.row_first[r + 1] - b.row_first[r]);
    out.Wrapped = table.rows[r].wrapped;
    return out;
}

ImTypesetCellPosition GetDocumentTableCell(const ImTypesetDocument* document,
                                           const ImTypesetPosition& position)
{
    ImTypesetCellPosition out = {-1, 0, 0, 0};
    const ImTypesetDocument* d = owned(document);
    if (d == nullptr || position.Block < 0 ||
        static_cast<size_t>(position.Block) >= d->blocks.size())
        return out;
    const DocBlock& b = d->blocks[static_cast<size_t>(position.Block)];
    if (!is_table(b.kind))
        return out;
    const int k = cell_at(b, position.Offset);
    if (k < 0)
        return out;
    const DocCell& cell = b.cells[static_cast<size_t>(k)];
    const int64_t offset = static_cast<int64_t>(position.Offset) - cell.start;
    out.Row = static_cast<int>(cell.row);
    out.Column = static_cast<int>(cell.column);
    out.Cell = k;
    out.Offset =
        static_cast<int>(std::min<int64_t>(std::max<int64_t>(offset, 0), cell.end - cell.start));
    return out;
}

ImTypesetPosition GetDocumentTablePosition(const ImTypesetDocument* document, int block, int row,
                                           int column, int offset)
{
    ImTypesetPosition out = {block, 0};
    const ImTypesetDocument* d = owned(document);
    if (d == nullptr || block < 0 || static_cast<size_t>(block) >= d->blocks.size())
        return out;
    const DocBlock& b = d->blocks[static_cast<size_t>(block)];
    if (!is_table(b.kind) || row < 0 || static_cast<size_t>(row) + 1 >= b.row_first.size())
        return out;
    const size_t first = b.row_first[static_cast<size_t>(row)];
    const size_t count = b.row_first[static_cast<size_t>(row) + 1] - first;
    if (count == 0)
        return out;
    // A column the row has no cell for is where its last cell ends.
    if (column < 0 || static_cast<size_t>(column) >= count) {
        out.Offset =
            static_cast<int>(column < 0 ? b.cells[first].start : b.cells[first + count - 1].end);
        return out;
    }
    const DocCell& cell = b.cells[first + static_cast<size_t>(column)];
    const int64_t length = static_cast<int64_t>(cell.end) - cell.start;
    out.Offset = static_cast<int>(cell.start + std::min<int64_t>(std::max(offset, 0), length));
    return out;
}

bool SetDocumentColumns(ImTypesetDocument* document, const ImTypesetColumns* columns)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return no_document();
    ImTypesetColumns given;
    if (columns != nullptr) {
        given.Count = std::min(std::max(columns->Count, 0), static_cast<int>(MAX_BALANCED_COLUMNS));
        given.Height = is_finite(columns->Height) ? columns->Height : 0.0f;
        given.Gutter = is_finite(columns->Gutter) ? columns->Gutter : 0.0f;
        given.Flags = columns->Flags;
    }
    // The height a view lent is the view's while what is said stands: a host
    // may say the same columns every frame and ask before the view is drawn.
    const ImTypesetColumns& had = d->columns_given;
    if (had.Count != given.Count || had.Height != given.Height || had.Gutter != given.Gutter ||
        had.Flags != given.Flags)
        d->view_height = 0.0f;
    d->columns_given = given;
    return succeed();
}

int GetDocumentColumnCount(ImTypesetDocument* document)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return 1;
    ready(*d);
    if (!d->columned)
        return 1;
    return static_cast<int>(std::min<size_t>(column_count(*d), 0x7FFFFFFF));
}

ImTypesetColumnGeometry GetDocumentColumn(ImTypesetDocument* document, int column)
{
    ImTypesetColumnGeometry out = {ImVec2(0.0f, 0.0f),
                                   ImVec2(0.0f, 0.0f),
                                   ImVec2(0.0f, 0.0f),
                                   ImVec2(0.0f, 0.0f),
                                   {0, 0},
                                   {0, 0},
                                   0.0f,
                                   false,
                                   false};
    ImTypesetDocument* d = owned(document);
    if (d == nullptr || column < 0)
        return out;
    ready(*d);
    d->doc.prepare_if_needed(d->composer);
    const ImTypesetPosition end = {d->blocks.empty() ? 0 : static_cast<int>(d->blocks.size()) - 1,
                                   d->blocks.empty() ? 0 : text_size_of(d->blocks.back())};
    if (!d->columned) {
        if (column != 0)
            return out;
        out.Max = ImVec2(pxf(*d, d->width), pxf(*d, d->doc.height()));
        out.ContentMax = out.Max;
        out.End = end;
        out.Exists = true;
        return out;
    }
    const size_t k = static_cast<size_t>(column);
    if (!d->columns.cut_through(d->doc, d->composer, k))
        return out;
    // A copy: finding the positions below lays blocks out.
    const DocumentColumn cut = d->columns.column(k);
    const Scaled left = static_cast<Scaled>(k) * pitch_of(*d);
    const Scaled right = add_saturating(left, d->width);
    out.Min = ImVec2(pxf(*d, left), 0.0f);
    out.Max = ImVec2(pxf(*d, right), pxf(*d, d->columns.height()));
    out.Exists = true;
    if (cut.empty) {
        out.ContentMin = out.Min;
        out.ContentMax = ImVec2(out.Max.x, out.Min.y);
        out.Start = end;
        out.End = end;
        out.UncutTop = pxf(*d, d->doc.height());
        return out;
    }
    out.ContentMin = ImVec2(pxf(*d, left), pxf(*d, cut.head));
    out.ContentMax = ImVec2(pxf(*d, right), pxf(*d, cut.used));
    out.UncutTop = pxf(*d, cut.top);
    out.Overfull = cut.overfull;
    out.Start = position_at(*d, cut.top);
    out.End = cut.next_unit < d->doc.unit_count() ? position_at(*d, cut.limit) : end;
    return out;
}

ImVec2 GetDocumentColumnPos(ImTypesetDocument* document, const ImVec2& uncut_pos)
{
    ImTypesetDocument* d = owned(document);
    if (d == nullptr)
        return uncut_pos;
    ready(*d);
    if (!d->columned)
        return uncut_pos;
    const float given_y = is_finite(uncut_pos.y) ? uncut_pos.y : 0.0f;
    const Scaled x = coordinate_sp(*d, is_finite(uncut_pos.x) ? uncut_pos.x : 0.0);
    const Scaled y = coordinate_sp(*d, given_y);
    // The top of a column's first line, answered in pixels and handed back,
    // is that line's column: a float of pixels does not hold a scaled point,
    // so the column is the one that shows the next height a float can say.
    const Scaled next =
        coordinate_sp(*d, std::nextafter(given_y, std::numeric_limits<float>::infinity()));
    const Shown at = shown_at(*d, std::max(y, next));
    return ImVec2(pxf(*d, x + at.dx), pxf(*d, y + at.dy));
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
    // Columns filled in order go on to the right: the view scrolls sideways.
    const ImTypesetColumns& columns = d.columns_given;
    const bool balanced = (columns.Flags & ImTypesetColumnFlags_Balance) != 0 && columns.Count >= 2;
    const bool across = balanced || columns.Height > 0.0f || columns.Count >= 2;
    // ImGui refuses an empty ID by assertion: none given is the view's own.
    const bool open =
        ImGui::BeginChild(str_id != nullptr && str_id[0] != '\0' ? str_id : "##document", size, 0,
                          across && !balanced ? ImGuiWindowFlags_HorizontalScrollbar : 0);
    child.begun = true;
    bool ok = true;
    if (open) {
        const ImVec2 room = ImGui::GetContentRegionAvail();
        float width = room.x;
        d.view_height = 0.0f;
        if (across) {
            // Count columns across the view: what the gutters leave, shared.
            const ImTypesetStyle& given = style != nullptr ? *style : ctx.style;
            const ResolvedStyle resolved = resolve_style(ctx, given);
            const double gutter = columns.Gutter < 0.0f
                                      ? 0.0
                                      : (columns.Gutter > 0.0f ? static_cast<double>(columns.Gutter)
                                                               : px_from_sp(resolved.baselineskip,
                                                                            resolved.px_per_inch));
            const int count = std::max(columns.Count, 1);
            width = static_cast<float>(
                std::max((static_cast<double>(room.x) - gutter * (count - 1)) / count, 1.0));
            if (!balanced && !(columns.Height > 0.0f))
                d.view_height = std::max(room.y, 1.0f);
        }
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
