#include "blocks.h"

#include "fontmetrics.h"
#include "unicode_tables.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ImTypeset::detail
{

namespace
{

// A document's blocks, their layouts and their slots are three sequences of
// one length. Each is grown by its own length and not by another's: when the
// allocation for one throws (a host's build may have exceptions on), the next
// call finds the others already grown and makes this one their length.
template<class Items>
void grow_to(Items& items, size_t count)
{
    if (items.size() < count)
        items.resize(count);
}

void note(PackError& error, PackError found)
{
    if (error == PackError::NONE)
        error = found;
}

// A sum of heights, which goes wrong only for a list nobody could display.
Scaled plus(Scaled a, Scaled b, PackError& error)
{
    bool overflowed = false;
    const Scaled sum = add_checked(a, b, overflowed);
    if (overflowed)
        note(error, PackError::DIMENSION_OVERFLOW);
    return sum;
}

// What an item of a vertical list occupies: the reference's `_extent`. Only
// what the stacking puts in one: boxes, the gap kern and interline glue, which
// is at its natural width because nothing here packs a vertical list to a
// target.
Scaled extent_of(const NodeArena& arena, const Node& node, PackError& error)
{
    switch (node.type) {
    case NodeType::BOX: {
        const Box* const box = arena.find_box(node);
        return box != nullptr ? plus(box->height, box->depth, error) : 0;
    }
    case NodeType::KERN:
    case NodeType::GLUE:
        return node.width();
    default:
        return 0;
    }
}

int32_t valid_quote(int32_t id, const Structure& structure, const Derived& derived)
{
    if (id < 0 || structure.quotes == nullptr)
        return NO_CONTAINER;
    const size_t index = static_cast<size_t>(id);
    return index < structure.quote_count && index < derived.quote_depth.size() ? id : NO_CONTAINER;
}

int32_t valid_list(int32_t id, const Structure& structure, const Derived& derived)
{
    if (id < 0 || structure.lists == nullptr)
        return NO_CONTAINER;
    const size_t index = static_cast<size_t>(id);
    return index < structure.list_count && index < derived.list_depth.size() ? id : NO_CONTAINER;
}

// A container's parent, which is one declared before it or none: so a chain of
// parents ends.
template<typename Container>
int32_t parent_of(const Container* table, int32_t id)
{
    const int32_t parent = table[id].parent;
    return parent >= 0 && parent < id ? parent : NO_CONTAINER;
}

// The quote a block is in at `level` (1 for the outermost): its own quote or
// the ancestor of it that deep. NO_CONTAINER for a block that names no quote.
int32_t quote_at(const Block& block, uint32_t level, const Structure& structure,
                 const Derived& derived)
{
    int32_t quote = valid_quote(block.quote, structure, derived);
    while (quote != NO_CONTAINER && derived.quote_depth[static_cast<size_t>(quote)] > level)
        quote = parent_of(structure.quotes, quote);
    return quote;
}

// Cuts an indent back so that the least measure is left (rule 8).
Scaled capped(Scaled indent, const DocumentRules& rules, Scaled width)
{
    if (rules.min_measure <= 0)
        return indent;
    const Scaled room = clamp_dimen(width) - clamp_dimen(rules.min_measure);
    const Scaled cap = room > 0 ? room : 0;
    return indent > cap ? cap : indent;
}

Scaled shift_of(int32_t list, const Structure& structure, const Derived& derived)
{
    const int32_t id = valid_list(list, structure, derived);
    return id == NO_CONTAINER ? 0 : derived.list_shift[static_cast<size_t>(id)];
}

// The width one node of a code line adds to it.
Scaled advance_of(const NodeArena& arena, const Node& node, CharMeasure measure)
{
    if (node.type == NodeType::CHAR)
        return measure ? measure(node.font(), node.codepoint()).width : 0;
    return hpack(arena, NodeView(&node, 1), 0, PackMode::ADDITIONAL, measure).natural;
}

bool blocks_is_space(const Node& node)
{
    return node.type == NodeType::CHAR && node.codepoint() == 0x20;
}

// A block's interline skip and strut with every length inside MAX_DIMEN, which
// is what keeps the sums over a block's lines inside a Scaled.
InterlineSkip blocks_clamped(const InterlineSkip& skip)
{
    InterlineSkip out = skip;
    for (GlueSpec* spec : {&out.baselineskip, &out.lineskip}) {
        spec->width = clamp_dimen(spec->width);
        spec->stretch = clamp_dimen(spec->stretch);
        spec->shrink = clamp_dimen(spec->shrink);
    }
    out.lineskiplimit = clamp_dimen(out.lineskiplimit);
    return out;
}

Strut blocks_clamped(const Strut& strut)
{
    // RUNNING is a rule's sentinel and no height.
    const auto length = [](Scaled value) { return value == RUNNING ? 0 : clamp_dimen(value); };
    Strut out;
    out.height = length(strut.height);
    out.depth = length(strut.depth);
    out.exact = strut.exact;
    return out;
}

Scaled clamp_extent(Scaled extent)
{
    if (extent < 0)
        return 0;
    return extent > MAX_BLOCK_EXTENT ? MAX_BLOCK_EXTENT : extent;
}

} // namespace

// --- Rules ----------------------------------------------------------------------------

DocumentRules DocumentRules::product(Scaled em)
{
    em = clamp_dimen(em);
    if (em < 0)
        em = 0;
    DocumentRules rules;
    rules.skip_empty_blocks = true;
    rules.quote_any_block = true;
    rules.bar_by_depth = true;
    rules.separate_quotes = true;
    rules.floor_rule_width = true;
    rules.code_overflow = CodeOverflow::WRAP;
    rules.code_continuation_indent = clamp_dimen(2 * em);
    rules.min_measure = clamp_dimen(8 * em);
    rules.list_spacing_in_quotes = true;
    rules.structured_list_spacing = true;
    rules.marker_on_any_block = true;
    rules.widen_marker_gutter = true;
    rules.rule_in_container = true;
    rules.break_between_blocks = true;
    rules.keep_table_header = true;
    return rules;
}

// --- Code -----------------------------------------------------------------------------

NodeView CodeLines::line(size_t i) const
{
    if (i >= ends.size())
        return NodeView();
    const size_t first = i == 0 ? 0 : ends[i - 1];
    const size_t end = ends[i];
    if (first > end || end > nodes.size())
        return NodeView();
    return NodeView(nodes.data() + first, end - first);
}

void CodeLines::clear()
{
    nodes.clear();
    ends.clear();
}

uint64_t table_stamp()
{
    // One thread at a time for the whole library.
    static uint64_t last = 0;
    return ++last;
}

NodeView TableCells::cell(size_t i) const
{
    if (i >= cell_end.size())
        return NodeView();
    const size_t first = i == 0 ? 0 : cell_end[i - 1];
    const size_t end = cell_end[i];
    if (first > end || end > nodes.size())
        return NodeView();
    return NodeView(nodes.data() + first, end - first);
}

size_t TableCells::columns() const
{
    const size_t rows = row_first.size();
    const size_t last = rows >= 2 ? row_first[rows - 1] - row_first[rows - 2] : 0;
    return std::max<size_t>(longest_closed, last);
}

void TableCells::start_row()
{
    const uint32_t count = static_cast<uint32_t>(cell_end.size());
    if (row_first.empty())
        row_first.push_back(0);
    // A row with no cell yet is the one to fill.
    if (row_first.size() >= 2 && row_first.back() == row_first[row_first.size() - 2] &&
        row_first.back() == count)
        return;
    // The row that ends here is closed: nothing is added to it again.
    if (row_first.size() >= 2)
        longest_closed =
            std::max(longest_closed, row_first.back() - row_first[row_first.size() - 2]);
    row_first.back() = count;
    row_first.push_back(count);
    row_stamp.push_back(table_stamp());
}

void TableCells::end_cell(Scaled natural_width)
{
    if (row_first.empty())
        start_row();
    cell_end.push_back(static_cast<uint32_t>(nodes.size()));
    natural.push_back(natural_width);
    row_first.back() = static_cast<uint32_t>(cell_end.size());
    row_stamp.back() = table_stamp();
}

void TableCells::reopen_last_cell()
{
    if (cell_end.empty())
        return;
    cell_end.pop_back();
    natural.pop_back();
    nodes.resize(cell_end.empty() ? 0 : cell_end.back());
    row_first.back() = static_cast<uint32_t>(cell_end.size());
    row_stamp.back() = table_stamp();
}

void TableCells::clear()
{
    nodes.clear();
    cell_end.clear();
    natural.clear();
    row_first.clear();
    row_stamp.clear();
    epoch = table_stamp();
    longest_closed = 0;
}

void TableLayout::clear()
{
    columns.clear();
    valid = false;
    assembled = false;
    rows.clear();
    cmds.clear();
    shipped.clear();
    cell_items.clear();
    word.clear();
    word_stamp.clear();
    widest.clear();
    narrowest.clear();
    row_top.clear();
    row_index.clear();
    cell_index.clear();
    stated = false;
    rows_set = 0;
    rows_kept = 0;
    widest_closed.clear();
    widest_rows = 0;
    widest_epoch = 0;
    narrowest_closed.clear();
    narrowest_rows = 0;
    narrowest_epoch = 0;
    narrowest_padding = 0;
    epoch = 0;
    resumed_from = 0;
    resumed_rows = 0;
}

void Block::clear_content()
{
    text.clear();
    code.clear();
    table.clear();
}

namespace
{

// The walk of a code block's characters: the reference's `_newlines`,
// `str.expandtabs` and `split("\n")` in one. `character(i, codepoint)` is
// called once for each character a line holds, in order, `i` being the index
// in `text` of the codepoint it came from: a tab is its spaces, each with the
// tab's index. `ends` is set to how many characters precede the end of each
// line. The column counts characters and starts again at each newline. Blank
// lines at the end are dropped, so the last character is the last line's.
template<typename Character>
void walk_code(const std::vector<uint32_t>& text, std::vector<uint32_t>& ends,
               Character&& character)
{
    ends.clear();
    uint32_t count = 0;
    uint32_t column = 0;
    bool open = false; // characters, or a newline, since the last line ended
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t codepoint = text[i];
        if (codepoint == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                continue;
            codepoint = '\n';
        }
        if (codepoint == '\n') {
            ends.push_back(count);
            column = 0;
            open = false;
            continue;
        }
        open = true;
        if (codepoint == '\t') {
            const uint32_t spaces = TAB_WIDTH - column % TAB_WIDTH;
            for (uint32_t s = 0; s < spaces; ++s) {
                character(i, uint32_t{0x20});
                ++count;
            }
            column += spaces;
            continue;
        }
        character(i, codepoint);
        ++count;
        ++column;
    }
    // What follows the last newline is a line unless it is empty.
    if (open)
        ends.push_back(count);
    while (!ends.empty()) {
        const size_t last = ends.size() - 1;
        const uint32_t first = last == 0 ? 0 : ends[last - 1];
        if (ends[last] != first)
            break;
        ends.pop_back();
    }
}

} // namespace

bool set_code(CodeLines& out, std::string_view source, FontSpec font,
              const Composition& composition, MetricsResolver resolve, std::string* why,
              std::vector<uint32_t>* node_bytes)
{
    out.clear();
    if (node_bytes != nullptr)
        node_bytes->clear();
    const FontKey key = font.key();
    const FontMetrics* const metrics = resolve(key);
    if (metrics == nullptr) {
        if (why != nullptr)
            *why = "the resolver does not know the face of " + font_key_text(font.face, font.size);
        return false;
    }
    out.codepoints.clear();
    out.offsets.clear();
    decode_utf8(source, out.codepoints, node_bytes != nullptr ? &out.offsets : nullptr);

    walk_code(out.codepoints, out.ends, [&](size_t i, uint32_t codepoint) {
        if (node_bytes != nullptr)
            node_bytes->push_back(out.offsets[i]);
        if (metrics->has(codepoint)) {
            out.nodes.push_back(char_node(key, codepoint));
            return;
        }
        const StandIn shown = stand_in(composition, resolve, key, codepoint);
        out.nodes.push_back(shown.codepoint == codepoint
                                ? char_node(shown.font, codepoint)
                                : replaced_char(shown.font, shown.codepoint, codepoint));
    });
    return true;
}

CodeExtent code_extent(std::string_view source)
{
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> ends;
    decode_utf8(source, codepoints, &offsets);
    const size_t none = codepoints.size();
    size_t last = none;
    walk_code(codepoints, ends, [&](size_t i, uint32_t) { last = i; });
    CodeExtent extent;
    extent.lines = static_cast<uint32_t>(std::min<size_t>(ends.size(), 0xFFFFFFFFu));
    if (last != none) {
        const size_t limit = 0xFFFFFFFFu;
        extent.end = last + 1 < offsets.size()
                         ? offsets[last + 1]
                         : static_cast<uint32_t>(std::min(source.size(), limit));
    }
    return extent;
}

// --- What the blocks and their containers say -------------------------------------------

Scaled marker_need(const NodeArena& arena, const Block& block, CharMeasure measure)
{
    if (block.text.marker.nodes.empty())
        return 0;
    const PackResult packed =
        hpack(arena, block.text.marker.nodes, 0, PackMode::ADDITIONAL, measure);
    return add_saturating(clamp_dimen(packed.natural), clamp_dimen(block.style.marker_gap));
}

void derive(const Block* blocks, size_t count, const Scaled* needs, const Structure& structure,
            const DocumentRules& rules, Derived& out)
{
    const size_t quotes = structure.quotes != nullptr ? structure.quote_count : 0;
    out.quote_depth.assign(quotes, 0);
    for (size_t q = 0; q < quotes; ++q) {
        const int32_t parent = parent_of(structure.quotes, static_cast<int32_t>(q));
        const uint32_t above =
            parent == NO_CONTAINER ? 0 : out.quote_depth[static_cast<size_t>(parent)];
        out.quote_depth[q] =
            static_cast<uint16_t>(above < MAX_QUOTE_DEPTH ? above + 1 : MAX_QUOTE_DEPTH);
    }

    const size_t lists = structure.lists != nullptr ? structure.list_count : 0;
    out.list_depth.assign(lists, 0);
    out.list_shift.assign(lists, 0);
    out.list_need.assign(lists, 0);
    if (lists == 0)
        return;
    if (rules.widen_marker_gutter && needs != nullptr) {
        for (size_t i = 0; i < count; ++i) {
            const int32_t list = blocks[i].list;
            if (list < 0 || static_cast<size_t>(list) >= lists)
                continue;
            // Only a marker that is set asks for room.
            if (blocks[i].type == BlockType::RULE ||
                (blocks[i].type == BlockType::CODE && !rules.marker_on_any_block))
                continue;
            if (needs[i] > out.list_need[static_cast<size_t>(list)])
                out.list_need[static_cast<size_t>(list)] = needs[i];
        }
    }
    for (size_t l = 0; l < lists; ++l) {
        int32_t parent = parent_of(structure.lists, static_cast<int32_t>(l));
        if (parent != NO_CONTAINER && out.list_depth[static_cast<size_t>(parent)] >= MAX_LIST_DEPTH)
            parent = NO_CONTAINER;
        out.list_depth[l] = static_cast<uint16_t>(
            parent == NO_CONTAINER ? 1 : out.list_depth[static_cast<size_t>(parent)] + 1);
        const Scaled excess = out.list_need[l] - clamp_dimen(structure.lists[l].indent);
        const Scaled inherited =
            parent == NO_CONTAINER ? 0 : out.list_shift[static_cast<size_t>(parent)];
        out.list_shift[l] = clamp_dimen(add_saturating(inherited, excess > 0 ? excess : 0));
    }
}

Scaled effective_indent(const Block& block, const Derived& derived, const DocumentRules& rules,
                        Scaled width)
{
    Scaled indent = clamp_dimen(block.style.left_indent);
    if (block.type == BlockType::RULE && !rules.rule_in_container)
        return indent;
    // Zero unless the gutters were derived under widen_marker_gutter.
    if (block.list >= 0 && static_cast<size_t>(block.list) < derived.list_shift.size())
        indent = clamp_dimen(indent + derived.list_shift[static_cast<size_t>(block.list)]);
    return capped(indent, rules, width);
}

uint32_t quote_level(const Block& block, const Structure& structure, const Derived& derived,
                     const DocumentRules& rules)
{
    if (block.type != BlockType::TEXT && !rules.quote_any_block)
        return 0;
    const int32_t quote = valid_quote(block.quote, structure, derived);
    if (quote != NO_CONTAINER)
        return derived.quote_depth[static_cast<size_t>(quote)];
    return block.quote_depth < MAX_QUOTE_DEPTH ? block.quote_depth : MAX_QUOTE_DEPTH;
}

bool sets_box(const Block& block, const DocumentRules& rules)
{
    if (block.type == BlockType::TABLE)
        return block.table.rows() != 0 || block.length_hint != 0;
    if (block.type != BlockType::CODE)
        return true;
    // Lines that are made late are counted by what the block says of them.
    return block.code.count() != 0 || block.length_hint != 0 ||
           (rules.marker_on_any_block && !block.text.marker.nodes.empty());
}

Scaled block_gap(const Block& above, const Block& below, uint32_t level, const Structure& structure,
                 const Derived& derived, const DocumentRules& rules)
{
    const Scaled sum = clamp_dimen(above.space_below) + clamp_dimen(below.space_above);
    if (!rules.structured_list_spacing || (level > 0 && !rules.list_spacing_in_quotes))
        return sum;
    int32_t a = valid_list(above.list, structure, derived);
    int32_t b = valid_list(below.list, structure, derived);
    if (a == NO_CONTAINER || b == NO_CONTAINER)
        return sum;
    // Two blocks of one item are paragraphs of it.
    if (a == b && above.item == below.item)
        return sum;
    // Otherwise the innermost list both are in decides: an item and the one
    // after it, an item and the list nested in it, a nested list and the item
    // that follows it.
    while (a != b) {
        if (a == NO_CONTAINER || b == NO_CONTAINER)
            return sum;
        const uint16_t depth_a = derived.list_depth[static_cast<size_t>(a)];
        const uint16_t depth_b = derived.list_depth[static_cast<size_t>(b)];
        if (depth_a >= depth_b)
            a = depth_a > 1 ? parent_of(structure.lists, a) : NO_CONTAINER;
        if (depth_b >= depth_a)
            b = depth_b > 1 ? parent_of(structure.lists, b) : NO_CONTAINER;
    }
    if (a == NO_CONTAINER)
        return sum;
    return structure.lists[a].tight ? 0 : sum;
}

size_t run_end(const Block* blocks, size_t first, size_t end, uint32_t level,
               const Structure& structure, const Derived& derived, const DocumentRules& rules)
{
    if (first >= end)
        return end;
    if (quote_level(blocks[first], structure, derived, rules) <= level)
        return first + 1;
    const int32_t quote = quote_at(blocks[first], level + 1, structure, derived);
    size_t next = first + 1;
    while (
        next < end && quote_level(blocks[next], structure, derived, rules) > level &&
        (!rules.separate_quotes || quote_at(blocks[next], level + 1, structure, derived) == quote))
        ++next;
    return next;
}

// --- One block as boxes -----------------------------------------------------------------

bool BlockComposer::block_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                                const LineBreakParams& params, CharMeasure measure,
                                const DocumentRules& rules, BlockBoxes& out)
{
    out.lines.clear();
    out.continues.clear();
    out.resumed.clear();
    out.clipped = false;
    out.hung = Node();
    out.pack_error = PackError::NONE;
    out.break_error = LineBreakError::NONE;
    out.budget_exhausted = false;

    width = clamp_dimen(width);
    if (width < 0)
        width = 0;
    indent = clamp_dimen(indent);

    switch (block.type) {
    case BlockType::TEXT: {
        BlockStyle style = block.style;
        style.left_indent = indent;
        style.first_line_indent = clamp_dimen(style.first_line_indent);
        style.marker_gap = clamp_dimen(style.marker_gap);
        style.strut = blocks_clamped(style.strut);
        _paragraph.set_lines(arena, block.text, style, width, params, measure, out.para);
        out.lines.assign(out.para.lines.begin(), out.para.lines.end());
        out.hung = out.para.hung;
        out.pack_error = out.para.pack_error;
        out.break_error = out.para.break_error;
        out.budget_exhausted = out.para.broken.budget_exhausted;
        return out.ok();
    }
    case BlockType::CODE:
        code_boxes(arena, block, indent, width, measure, rules, out);
        break;
    case BlockType::RULE:
        rule_box(arena, block, indent, width, measure, rules, out);
        break;
    case BlockType::TABLE:
        table_boxes(arena, block, indent, width, params, measure, out);
        break;
    }
    // Not a text block: nothing of an earlier one is left to be read as this
    // block's.
    out.para.lines.clear();
    out.para.shipped.clear();
    out.para.mapped = false;
    out.para.item_count = 0;
    out.para.marker_items = 0;
    out.para.originals.clear();
    out.para.hard_breaks.clear();
    return out.ok();
}

void BlockComposer::code_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                               CharMeasure measure, const DocumentRules& rules, BlockBoxes& out)
{
    const Strut strut = blocks_clamped(block.style.strut);
    const bool marked = rules.marker_on_any_block && !block.text.marker.nodes.empty();
    // The room a line has, never less than a point: src/paragraph's `shape`
    // gives a text line the same floor.
    const Scaled available = width - indent > UNITY ? width - indent : UNITY;
    Scaled hang = clamp_dimen(rules.code_continuation_indent);
    if (hang < 0)
        hang = 0;
    if (hang > available - UNITY)
        hang = available - UNITY;

    // One box of the block: `contents` packed, moved right by `shift`, the
    // marker hung on it if it is the block's first, and set on the strut.
    const auto add = [&](NodeView contents, Scaled shift, Scaled room, bool fit, bool continues) {
        PackResult packed = hpack(arena, contents, 0, PackMode::ADDITIONAL, measure);
        note(out.pack_error, packed.error);
        if (fit && packed.natural > room) {
            // Too long and not to be wrapped further: a box as wide as its
            // room, which its contents run past.
            packed = hpack(arena, contents, room, PackMode::EXACTLY, measure);
            out.clipped = true;
        }
        Box box = packed.box;
        box.shift_amount = shift;
        Node line;
        if (marked && out.lines.empty()) {
            line = hang_marker(arena, arena.add_box(box, contents), block.text.marker.nodes, indent,
                               clamp_dimen(block.style.marker_gap), measure, _scratch,
                               &out.pack_error);
            out.hung = line;
            Box hung = arena.box(line);
            if (strut.changes(hung)) {
                strut.apply_to(hung);
                line = arena.add_box(hung);
            }
        } else {
            strut.apply_to(box);
            line = arena.add_box(box, contents);
        }
        out.lines.push_back(line);
        if (rules.code_overflow == CodeOverflow::WRAP)
            out.continues.push_back(continues ? 1 : 0);
    };

    const size_t count = block.code.count();
    for (size_t l = 0; l < count; ++l) {
        const NodeView source = block.code.line(l);
        if (rules.code_overflow != CodeOverflow::WRAP) {
            add(source, indent, available, rules.code_overflow == CodeOverflow::CLIP, false);
            continue;
        }
        // Wrapped: as many characters as fit, cut after the last space among
        // them when there is one past the line's own indentation, and what is
        // left on lines that hang (tex.web section 849's hanging indentation).
        // Each line takes at least one character, so the loop ends.
        size_t start = 0;
        bool first = true;
        do {
            const Scaled room = first ? available : available - hang;
            Scaled used = 0;
            size_t stop = start;
            size_t after_space = 0;
            bool inked = false;
            while (stop < source.size()) {
                const Scaled advance = advance_of(arena, source[stop], measure);
                if (stop > start && used + advance > room)
                    break;
                used = add_saturating(used, advance);
                if (blocks_is_space(source[stop])) {
                    if (inked)
                        after_space = stop + 1;
                } else {
                    inked = true;
                }
                ++stop;
            }
            if (stop < source.size() && after_space > start)
                stop = after_space;
            add(source.sub(start, stop - start), first ? indent : indent + hang, room, true,
                !first);
            start = stop;
            first = false;
        } while (start < source.size());
    }
    // A marker with no line to hang on gets an empty one.
    if (out.lines.empty() && marked)
        add(NodeView(), indent, available, false, false);
    bool wrapped = false;
    for (const uint8_t flag : out.continues)
        wrapped = wrapped || flag != 0;
    if (!wrapped)
        out.continues.clear();
}

void BlockComposer::rule_box(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                             CharMeasure measure, const DocumentRules& rules, BlockBoxes& out)
{
    Scaled before = indent;
    Scaled across = width - indent;
    if (rules.floor_rule_width) {
        before = indent < 0 ? 0 : (indent > width ? width : indent);
        across = width - before;
    }
    const Node nodes[2] = {kern(before), rule(across, clamp_dimen(block.thickness), 0)};
    const PackResult packed = hpack(arena, NodeView(nodes, 2), 0, PackMode::ADDITIONAL, measure);
    note(out.pack_error, packed.error);
    out.lines.push_back(arena.add_box(packed.box, NodeView(nodes, 2)));
}

namespace
{

// What src/align reads of a table block set at `indent`.
TableStyle table_style(const Block& block, Scaled indent)
{
    TableStyle style;
    style.left_indent = indent;
    style.cell_padding = clamp_dimen(block.table.cell_padding);
    if (style.cell_padding < 0)
        style.cell_padding = 0;
    style.rule_thickness = clamp_dimen(block.table.rule_thickness);
    if (style.rule_thickness < 0)
        style.rule_thickness = 0;
    style.interline = blocks_clamped(block.style.interline);
    style.strut = blocks_clamped(block.style.strut);
    return style;
}

// The policy a table's widths are solved under at a measure of `width`.
// `scratch` holds the held widths as they are used: each at least one scaled
// point. `stated` says the widths are the caller's or the held ones.
ColumnPolicy table_policy(const TableCells& table, size_t columns, Scaled width, Scaled indent,
                          std::vector<Scaled>& scratch, bool& stated)
{
    ColumnPolicy policy;
    stated = false;
    if (table.held) {
        // The reference's `StreamView._sized_to`: the held widths while they
        // sum to no more than the measure less the indent, and otherwise the
        // default apportioning.
        if (table.widths.size() != columns || columns == 0)
            return policy;
        scratch.assign(table.widths.begin(), table.widths.end());
        const Scaled available = std::max<Scaled>(clamp_dimen(width) - indent, UNITY);
        Scaled sum = 0;
        for (Scaled& w : scratch) {
            w = std::max<Scaled>(clamp_dimen(w), 1);
            sum = add_saturating(sum, w);
        }
        if (sum > available)
            return policy;
        policy.kind = WidthPolicy::WIDTHS;
        policy.widths = scratch.data();
        policy.count = scratch.size();
        stated = true;
        return policy;
    }
    if (table.policy == WidthPolicy::WIDTHS && !table.widths.empty()) {
        policy.kind = WidthPolicy::WIDTHS;
        policy.widths = table.widths.data();
        policy.count = table.widths.size();
    } else if (table.policy == WidthPolicy::FRACTIONS && !table.fractions.empty()) {
        policy.kind = WidthPolicy::FRACTIONS;
        policy.fractions = table.fractions.data();
        policy.count = table.fractions.size();
    }
    return policy;
}

} // namespace

// The reference's `_table_boxes`: the whole table set at once, nothing kept.
void BlockComposer::table_boxes(NodeArena& arena, const Block& block, Scaled indent, Scaled width,
                                const LineBreakParams& params, CharMeasure measure, BlockBoxes& out)
{
    const TableCells& cells = block.table;
    _cells.clear();
    for (size_t i = 0; i < cells.cells(); ++i)
        _cells.push_back(cells.cell(i));
    TableInput table;
    table.cells = _cells.data();
    table.cell_count = _cells.size();
    table.row_first = cells.row_first.data();
    table.rows = cells.rows();
    table.has_header = cells.has_header;
    table.aligns = cells.aligns.data();
    table.align_count = cells.aligns.size();
    bool stated = false;
    table.policy = table_policy(cells, cells.columns(), width, indent, _scratch_widths, stated);
    table.style = table_style(block, indent);
    _aligner.fin_align(arena, table, width, params, measure, _aligned);
    note(out.pack_error, _aligned.pack_error);
    out.break_error = _aligned.break_error;
    for (const TableBox& box : _aligned.boxes) {
        out.lines.push_back(box.box);
        out.resumed.push_back(box.prev_depth_carried);
    }
}

// --- Stacking ---------------------------------------------------------------------------

Stacker::Stacker()
    : _levels(MAX_QUOTE_DEPTH + 1)
{
}

void Stacker::stack(NodeArena& arena, const StackInput& in, Stacked& out)
{
    out.vlist.clear();
    out.extents.assign(in.count, Extent());
    out.bars.clear();
    out.trace.clear();
    out.pack_error = PackError::NONE;
    out.extent = 0;
    Derived none;
    StackInput checked = in;
    if (checked.blocks == nullptr || checked.line_first == nullptr || checked.indents == nullptr)
        checked.count = 0;
    if (checked.derived == nullptr) {
        // No containers were derived: none is named.
        checked.derived = &none;
        checked.structure = Structure();
    }
    stacked(arena, checked, 0, checked.count, 0, 0, out.vlist, out);
    out.extent = 0;
    for (const Node& node : out.vlist)
        out.extent = plus(out.extent, extent_of(arena, node, out.pack_error), out.pack_error);
}

void Stacker::stacked(NodeArena& arena, const StackInput& in, size_t first, size_t end,
                      uint32_t level, Scaled base, std::vector<Node>& vlist, Stacked& out)
{
    const Structure& structure = in.structure;
    const Derived& derived = *in.derived;
    const auto trace = [&](StackEvent::Kind kind, Scaled a = 0, Scaled b = 0, Scaled c = 0,
                           Scaled d = 0) -> size_t {
        if (!out.tracing)
            return 0;
        StackEvent event;
        event.kind = kind;
        event.a = a;
        event.b = b;
        event.c = c;
        event.d = d;
        out.trace.push_back(event);
        return out.trace.size() - 1;
    };

    vlist.clear();
    if (out.tracing) {
        Scaled runs = 0;
        for (size_t i = first; i < end;
             i = run_end(in.blocks, i, end, level, structure, derived, in.rules))
            ++runs;
        trace(StackEvent::STACK, level, runs);
    }

    // tex.web's prev_depth (section 212). The pen is how far down the list
    // has come: the reference's bookkeeping for `extents`, kept as the list
    // grows.
    Scaled prev_depth = IGNORE_DEPTH;
    Scaled pen = base;
    const Block* above = nullptr;
    // `resumed`, if given, is the prev_depth to go on from where that is not
    // the box's depth (a table row whose cells wrapped).
    const auto append = [&](const Node& box, const InterlineSkip& skip,
                            const Scaled* resumed = nullptr) -> Scaled {
        const size_t before = vlist.size();
        const Scaled handed = prev_depth;
        prev_depth = append_to_vlist(arena, vlist, box, prev_depth, skip);
        if (resumed != nullptr)
            prev_depth = *resumed;
        for (size_t k = before; k < vlist.size(); ++k)
            pen = plus(pen, extent_of(arena, vlist[k], out.pack_error), out.pack_error);
        return handed;
    };

    for (size_t i = first; i < end;) {
        const size_t next = run_end(in.blocks, i, end, level, structure, derived, in.rules);
        const Block& head = in.blocks[i];
        const bool quoted = quote_level(head, structure, derived, in.rules) > level;
        trace(StackEvent::RUN, static_cast<Scaled>(in.first_number + i),
              static_cast<Scaled>(next - i), quoted ? 1 : 0);

        if (in.rules.skip_empty_blocks) {
            bool empty = true;
            for (size_t k = i; k < next && empty; ++k)
                empty = !sets_box(in.blocks[k], in.rules);
            if (empty) {
                for (size_t k = i; k < next; ++k)
                    out.extents[k] = Extent{pen, pen};
                trace(StackEvent::END_RUN);
                i = next;
                continue;
            }
        }

        if (above != nullptr) {
            // The gap, and a fresh rhythm after it: the kern is the space, and
            // interline glue on top of it would be a second one. The list
            // holding nothing yet means every run so far set no box, and then
            // there is no gap either.
            const bool holds = !vlist.empty();
            const Scaled gap =
                holds ? block_gap(*above, head, level, structure, derived, in.rules) : 0;
            trace(StackEvent::GAP, above->space_below, head.space_above, gap, holds ? 0 : 1);
            if (holds) {
                if (gap != 0) {
                    vlist.push_back(kern(gap));
                    pen = plus(pen, gap, out.pack_error);
                    const size_t slot = trace(StackEvent::KERN);
                    if (out.tracing)
                        out.trace[slot].node = vlist.back();
                }
                prev_depth = IGNORE_DEPTH;
            }
        }

        const Scaled top = pen;
        if (quoted) {
            const size_t appended = trace(StackEvent::APPEND);
            const size_t shown = trace(StackEvent::BARRED);
            const Node box = barred(arena, in, i, next, level + 1, top, out);
            trace(StackEvent::END_BARRED);
            const Scaled handed = append(box, blocks_clamped(head.style.interline));
            if (out.tracing) {
                out.trace[appended].a = handed;
                out.trace[appended].b = prev_depth;
                out.trace[shown].node = box;
            }
        } else {
            const uint32_t line_first = in.line_first[i];
            const uint32_t line_end = in.line_first[i + 1];
            const InterlineSkip skip = blocks_clamped(head.style.interline);
            for (uint32_t l = line_first; l < line_end; ++l) {
                const size_t before = vlist.size();
                const Scaled handed =
                    append(in.lines[l], skip, in.resumed != nullptr ? in.resumed + l : nullptr);
                trace(StackEvent::APPEND, handed, prev_depth);
                if (out.tracing && vlist.size() - before == 2) {
                    const size_t slot = trace(StackEvent::GLUE);
                    out.trace[slot].node = vlist[before];
                }
                trace(StackEvent::REF, static_cast<Scaled>(in.first_number + i), l - line_first);
            }
            out.extents[i] = Extent{top, pen};
        }
        trace(StackEvent::END_RUN);
        above = &in.blocks[next - 1];
        i = next;
    }
    trace(StackEvent::END_STACK);
}

// The reference's `_barred`. The run is stacked into a vbox first, because a
// rule beside a quote has to be exactly as tall as the quote; the rule is then
// built from that vbox's own height and depth and put beside it with a
// negative kern, which is how two things share a horizontal space. The vbox
// stays at x = 0: its lines carry their own indent.
Node Stacker::barred(NodeArena& arena, const StackInput& in, size_t first, size_t end,
                     uint32_t level, Scaled top, Stacked& out)
{
    const Structure& structure = in.structure;
    const Derived& derived = *in.derived;
    const size_t slot = out.bars.size();
    out.bars.push_back(Bar());

    std::vector<Node>& list = _levels[level <= MAX_QUOTE_DEPTH ? level : MAX_QUOTE_DEPTH];
    stacked(arena, in, first, end, level, top, list, out);
    const PackResult inner = vpack(arena, list);
    note(out.pack_error, inner.error);
    const Node inner_node = arena.add_box(inner.box, list);

    const Scaled quote_indent = clamp_dimen(in.theme.quote_indent);
    const int32_t quote = quote_at(in.blocks[first], level, structure, derived);
    Scaled bar_x = 0;
    if (in.rules.bar_by_depth && quote != NO_CONTAINER) {
        const Quote& named = structure.quotes[quote];
        bar_x =
            capped(clamp_dimen(clamp_dimen(named.bar_x) + shift_of(named.list, structure, derived)),
                   in.rules, in.width);
    } else {
        // The least indent of the run's text, less the quote's own indent:
        // once whatever the level in the reference, and once for each level
        // from the bar's down to the block's under bar_by_depth.
        bool found = false;
        Scaled least = 0;
        for (size_t k = first; k < end; ++k) {
            const Block& block = in.blocks[k];
            if (block.type != BlockType::TEXT && !in.rules.quote_any_block)
                continue;
            Scaled edge = in.indents[k];
            if (in.rules.bar_by_depth) {
                const uint32_t deep = quote_level(block, structure, derived, in.rules);
                const Scaled levels = deep >= level ? static_cast<Scaled>(deep - level) + 1 : 1;
                edge -= levels * quote_indent;
            } else {
                edge -= quote_indent;
            }
            if (!found || edge < least)
                least = edge;
            found = true;
        }
        bar_x = found ? least : 0;
    }
    if (bar_x < 0)
        bar_x = 0;

    const Scaled thickness = clamp_dimen(in.theme.quote_rule_thickness);
    const Node nodes[4] = {kern(bar_x), rule(thickness, inner.box.height, inner.box.depth),
                           kern(-(bar_x + thickness)), inner_node};
    const PackResult packed = hpack(arena, NodeView(nodes, 4), 0, PackMode::ADDITIONAL, in.measure);
    note(out.pack_error, packed.error);

    Bar& bar = out.bars[slot];
    bar.first_block = static_cast<uint32_t>(in.first_number + first);
    bar.block_count = static_cast<uint32_t>(end - first);
    bar.level = level;
    bar.quote = quote;
    bar.x = bar_x;
    bar.box_top = top;
    // Where ship-out puts a rule in an hbox: its height above the baseline.
    bar.top = sub_saturating(add_saturating(top, packed.box.height), inner.box.height);
    bar.thickness = thickness;
    bar.extent = plus(inner.box.height, inner.box.depth, out.pack_error);
    bar.box_width = packed.box.width;
    bar.box_height = packed.box.height;
    bar.box_depth = packed.box.depth;
    return arena.add_box(packed.box, NodeView(nodes, 4));
}

// --- A whole document at once -------------------------------------------------------------

bool DocumentComposer::compose_blocks(NodeArena& arena, const Block* blocks, size_t count,
                                      const Structure& structure, const DocumentTheme& theme,
                                      const DocumentRules& rules, Scaled width, Composed& out)
{
    out.pack_error = PackError::NONE;
    out.break_error = LineBreakError::NONE;
    if (blocks == nullptr)
        count = 0;

    _needs.resize(count);
    for (size_t i = 0; i < count; ++i)
        _needs[i] = marker_need(arena, blocks[i], measure);
    derive(blocks, count, _needs.data(), structure, rules, _derived);

    _indents.resize(count);
    _line_first.clear();
    _lines.clear();
    _resumed.clear();
    for (size_t i = 0; i < count; ++i) {
        _indents[i] = effective_indent(blocks[i], _derived, rules, width);
        _line_first.push_back(static_cast<uint32_t>(_lines.size()));
        _block.block_boxes(arena, blocks[i], _indents[i], width, params, measure, rules, _boxes);
        ++blocks_composed;
        for (size_t l = 0; l < _boxes.lines.size(); ++l) {
            _lines.push_back(_boxes.lines[l]);
            _resumed.push_back(l < _boxes.resumed.size() ? _boxes.resumed[l]
                                                         : arena.box(_boxes.lines[l]).depth);
        }
        note(out.pack_error, _boxes.pack_error);
        if (out.break_error == LineBreakError::NONE)
            out.break_error = _boxes.break_error;
    }
    _line_first.push_back(static_cast<uint32_t>(_lines.size()));

    StackInput in;
    in.blocks = blocks;
    in.count = count;
    in.line_first = _line_first.data();
    in.lines = _lines.data();
    in.resumed = _resumed.data();
    in.indents = _indents.data();
    in.structure = structure;
    in.derived = &_derived;
    in.theme = theme;
    in.rules = rules;
    in.measure = measure;
    in.width = width;
    _stacker.stack(arena, in, out.stacked);
    note(out.pack_error, out.stacked.pack_error);

    // The reference's `compose_blocks`: vpack at natural height (tex.web
    // section 668).
    out.packed = vpack(arena, out.stacked.vlist);
    note(out.pack_error, out.packed.error);
    out.box = arena.add_box(out.packed.box, out.stacked.vlist);
    out.packed.box.contents = arena.box(out.box).contents;
    return out.ok();
}

namespace
{

// Takes an arena back to where it stood when the scope is left, however it is
// left. What a layout adds to the arena is scratch; and a host can unwind
// through one (an allocation that fails where exceptions are enabled), which
// can leave the arena's parallel tables out of step until it is taken back.
struct Rewound {
    explicit Rewound(NodeArena& arena_)
        : arena(arena_)
        , mark(arena_.mark())
    {
    }
    ~Rewound() { arena.rewind(mark); }
    Rewound(const Rewound&) = delete;
    Rewound& operator=(const Rewound&) = delete;

    NodeArena& arena;
    NodeArena::Mark mark;
};

} // namespace

void DocumentComposer::layout_block(NodeArena& arena, const Block& block, Scaled indent,
                                    Scaled width, const DocumentRules& rules, BlockLayout& out)
{
    if (block.type == BlockType::TABLE) {
        layout_table(arena, block, indent, width, out);
        return;
    }
    const Rewound rewound(arena);
    _block.block_boxes(arena, block, indent, width, params, measure, rules, _boxes);
    ++blocks_composed;
    out.serial = ++_serial;

    out.pack_error = _boxes.pack_error;
    out.break_error = _boxes.break_error;
    out.ship_error = ShipError::NONE;
    out.budget_exhausted = _boxes.budget_exhausted;
    out.clipped = _boxes.clipped;
    out.continues.assign(_boxes.continues.begin(), _boxes.continues.end());
    out.shipped.assign(_boxes.para.shipped.begin(), _boxes.para.shipped.end());
    out.mapped = _boxes.para.mapped;
    out.marker_items = _boxes.para.marker_items;
    out.originals.assign(_boxes.para.originals.begin(), _boxes.para.originals.end());
    out.hard_breaks.assign(_boxes.para.hard_breaks.begin(), _boxes.para.hard_breaks.end());

    // The block alone, as the stack would hold it: its boxes under its own
    // interline skip from a fresh prev_depth (tex.web section 679), packed at
    // natural height and shipped from its top-left corner.
    out.lines.clear();
    _vlist.clear();
    Scaled prev_depth = IGNORE_DEPTH;
    const InterlineSkip skip = blocks_clamped(block.style.interline);
    for (const Node& line : _boxes.lines) {
        prev_depth = append_to_vlist(arena, _vlist, line, prev_depth, skip);
        const Box& box = arena.box(line);
        out.lines.push_back(
            LineDims{box.width, box.height, box.depth, box.shift_amount, box.depth});
    }
    Scaled extent = 0;
    for (const Node& node : _vlist)
        extent = plus(extent, extent_of(arena, node, out.pack_error), out.pack_error);
    out.extent = clamp_extent(extent);

    out.cmds.clear();
    out.box_width = 0;
    out.item_count = 0;
    if (!_vlist.empty()) {
        const PackResult packed = vpack(arena, _vlist);
        note(out.pack_error, packed.error);
        out.box_width = packed.box.width;
        const Node box = arena.add_box(packed.box, _vlist);
        out.ship_error = ship(arena, box, Point{0, packed.box.height}, measure, out.cmds);
        for (const DrawCmd& cmd : out.cmds) {
            if (cmd.is(CmdType::LINE) && cmd.end > out.item_count)
                out.item_count = cmd.end;
        }
    }
}

// Each column's longest unbreakable piece with its padding, into
// `kept.narrowest`: the reference's `_longest_word` over every cell, a cell of
// a row that has not changed since it was measured being read from
// `kept.word`.
void DocumentComposer::table_narrowest(NodeArena& arena, const Block& block, TableLayout& kept)
{
    const TableCells& cells = block.table;
    Aligner& aligner = _block.aligner();
    const size_t rows = cells.rows();
    const size_t columns = std::min(cells.columns(), MAX_TABLE_COLUMNS);
    Scaled padding = clamp_dimen(cells.cell_padding);
    if (padding < 0)
        padding = 0;
    kept.word.resize(cells.cells(), 0);
    kept.word_stamp.resize(rows, 0);
    // The rows before the last cannot change under one epoch: what fin_col
    // made of them is kept, and only the rows closed since are added to it.
    const size_t closed = rows == 0 ? 0 : rows - 1;
    if (kept.narrowest_epoch != cells.epoch || kept.word_serial != params_serial ||
        kept.narrowest_padding != padding || kept.narrowest_rows > closed) {
        kept.narrowest_closed.clear();
        kept.narrowest_rows = 0;
    }
    const auto measured = [&](size_t r) {
        const size_t first = cells.row_first[r];
        const size_t count = std::min<size_t>(cells.row_first[r + 1] - first, columns);
        ++table_rows_visited;
        if (kept.word_stamp[r] != cells.row_stamp[r] || kept.word_serial != params_serial) {
            for (size_t c = 0; c < count; ++c) {
                kept.word[first + c] =
                    aligner.longest_word(arena, cells.cell(first + c), params, measure);
                ++table_words_measured;
            }
            kept.word_stamp[r] = cells.row_stamp[r];
        }
        return count;
    };
    // A change of the parameters measures every row again, the last first
    // or not: the serial is taken once all of them are.
    for (size_t r = kept.narrowest_rows; r < closed; ++r) {
        const size_t first = cells.row_first[r];
        const size_t count = measured(r);
        for (size_t c = 0; c < count; ++c)
            fin_col(kept.narrowest_closed, c, add_saturating(kept.word[first + c], 2 * padding));
    }
    kept.narrowest_rows = closed;
    kept.narrowest_epoch = cells.epoch;
    kept.narrowest_padding = padding;
    kept.narrowest.assign(kept.narrowest_closed.begin(), kept.narrowest_closed.end());
    kept.narrowest.resize(columns, 0);
    if (rows != 0) {
        const size_t first = cells.row_first[rows - 1];
        const size_t count = measured(rows - 1);
        for (size_t c = 0; c < count; ++c)
            fin_col(kept.narrowest, c, add_saturating(kept.word[first + c], 2 * padding));
    }
    kept.word_serial = params_serial;
}

Scaled Document::table_floor(DocumentComposer& composer, size_t block, size_t column)
{
    if (block >= _count || _blocks[block].type != BlockType::TABLE || !slot_valid(block))
        return 0;
    TableLayout& kept = _layouts[block].table;
    if (kept.narrowest.empty() && _blocks[block].table.rows() != 0) {
        const NodeArena::Mark before = _arena.mark();
        composer.table_narrowest(_arena, _blocks[block], kept);
        _arena.rewind(before);
    }
    return column < kept.narrowest.size() ? kept.narrowest[column] : 0;
}

// A table, with the rows it was last set as kept: what tex.web's fin_align
// (section 800) does in one go, done so that a table to which a row was added
// costs that row. Three things are kept in `out.table` from one layout to the
// next and each is checked before it is used: a cell's longest word, by its
// row's stamp; a row as it was shipped alone, by its row's stamp and by
// everything else it was set from (the column widths, the indent, the table's
// configuration, the breaking parameters); and nothing else. The rows are
// stacked by arithmetic on their dimensions, which is what vpack at natural
// height and ship-out make of the list `Aligner::fin_align` builds: the
// goldens hold the two to the same commands.
void DocumentComposer::layout_table(NodeArena& arena, const Block& block, Scaled indent,
                                    Scaled width, BlockLayout& out)
{
    const Rewound rewound(arena);
    const TableCells& cells = block.table;
    TableLayout& kept = out.table;
    Aligner& aligner = _block.aligner();
    const TableStyle style = table_style(block, indent);
    const size_t rows = cells.rows();
    const size_t columns = std::min(cells.columns(), MAX_TABLE_COLUMNS);

    ++blocks_composed;
    const uint64_t previous = out.serial;
    out.serial = ++_serial;
    out.budget_exhausted = false;
    out.clipped = false;
    out.continues.clear();
    out.hard_breaks.clear();
    out.marker_items = 0;

    // The widest cell of each column: the second half of tex.web section 796
    // (fin_col here) over what was measured when the cells were made. The
    // rows before the last cannot change under one epoch of the cells, so
    // what they came to is kept and only the rows closed since are added.
    const size_t closed = rows == 0 ? 0 : rows - 1;
    if (kept.widest_epoch != cells.epoch || kept.widest_rows > closed) {
        kept.widest_closed.clear();
        kept.widest_rows = 0;
        kept.widest_epoch = cells.epoch;
    }
    const auto widen = [&](std::vector<Scaled>& widths, size_t r) {
        ++table_rows_visited;
        const size_t first = cells.row_first[r];
        const size_t count = std::min<size_t>(cells.row_first[r + 1] - first, columns);
        for (size_t c = 0; c < count; ++c)
            fin_col(widths, c, cells.natural[first + c]);
    };
    for (size_t r = kept.widest_rows; r < closed; ++r)
        widen(kept.widest_closed, r);
    kept.widest_rows = closed;
    kept.widest.assign(kept.widest_closed.begin(), kept.widest_closed.end());
    kept.widest.resize(columns, 0);
    if (rows != 0)
        widen(kept.widest, rows - 1);

    // The widths (section 801, and the reference's `_column_widths`). The
    // longest words are measured only if the solve asks for them, and then
    // only for the cells of rows that changed since they last were.
    bool stated = false;
    const ColumnPolicy policy = table_policy(cells, columns, width, indent, _widths, stated);
    kept.narrowest.clear();
    if (rows != 0 &&
        !column_widths(policy, kept.widest.data(), nullptr, columns, width, indent, kept.solve)) {
        table_narrowest(arena, block, kept);
        column_widths(policy, kept.widest.data(), kept.narrowest.data(), columns, width, indent,
                      kept.solve);
    } else if (rows == 0) {
        kept.solve = ColumnSolve();
    }
    if (kept.word_stamp.size() > rows)
        kept.word_stamp.resize(rows);
    kept.stated = stated || kept.solve.regime == SolveRegime::GIVEN;
    const std::vector<Scaled>& widths = kept.solve.columns;

    // Which rows stand as they were set.
    const bool same = kept.valid && kept.config == cells.config &&
                      kept.params_serial == params_serial && kept.indent == indent &&
                      kept.columns == widths;
    size_t standing = 0;
    if (same) {
        const size_t most = std::min(rows, kept.rows.size());
        if (kept.epoch == cells.epoch && most != 0) {
            // Under one epoch every kept row but the last is the row it was.
            standing = most - 1;
            if (kept.rows[standing].stamp == cells.row_stamp[standing])
                ++standing;
        } else {
            while (standing < most && kept.rows[standing].stamp == cells.row_stamp[standing])
                ++standing;
            table_rows_visited += standing;
        }
    }
    // The rows that stand are also where the last layout put them, with the
    // indices it gave them: what the block holds of them is kept, and the
    // stacking goes on from the last of them.
    const bool resumed = standing != 0 && kept.assembled;
    kept.resumed_from = resumed ? previous : 0;
    kept.resumed_rows = resumed ? static_cast<uint32_t>(standing) : 0;
    kept.epoch = cells.epoch;
    kept.valid = false;
    kept.assembled = false;
    if (!resumed) {
        out.pack_error = PackError::NONE;
        out.break_error = LineBreakError::NONE;
        out.ship_error = ShipError::NONE;
        out.originals.clear();
        out.lines.clear();
        out.cmds.clear();
        out.shipped.clear();
        out.box_width = 0;
        out.mapped = true;
        kept.row_top.clear();
        kept.row_index.clear();
        kept.cell_index.clear();
    }
    kept.rows.resize(standing);
    kept.cmds.resize(standing == 0 ? 0 : kept.rows[standing - 1].cmd_end);
    kept.shipped.resize(standing == 0 ? 0 : kept.rows[standing - 1].shipped_end);
    kept.cell_items.resize(standing == 0 ? 0 : kept.rows[standing - 1].cell_end);
    kept.columns.assign(widths.begin(), widths.end());
    kept.config = cells.config;
    kept.params_serial = params_serial;
    kept.indent = indent;
    kept.rows_kept = static_cast<uint32_t>(standing);
    kept.rows_set = static_cast<uint32_t>(rows - standing);

    // Each row that does not: set to the widths (sections 807 to 810), put in
    // a list of its own and shipped from its top-left corner.
    for (size_t r = standing; r < rows; ++r) {
        const NodeArena::Mark before = arena.mark();
        const size_t first = cells.row_first[r];
        const size_t count = std::min<size_t>(cells.row_first[r + 1] - first, columns);
        _cells.clear();
        for (size_t c = 0; c < count; ++c)
            _cells.push_back(cells.cell(first + c));
        RowInput input;
        input.cells = _cells.data();
        input.cell_count = count;
        input.natural = cells.natural.data() + first;
        input.columns = widths.data();
        input.column_count = widths.size();
        input.aligns = cells.aligns.data();
        input.align_count = cells.aligns.size();
        aligner.set_row(arena, input, style, params, measure, _row);
        ++table_rows_set;
        note(out.pack_error, _row.pack_error);
        if (out.break_error == LineBreakError::NONE)
            out.break_error = _row.break_error;

        TableRowLayout row;
        row.stamp = cells.row_stamp[r];
        row.wrapped = _row.wrapped;
        row.mapped = _row.mapped;
        row.item_count = _row.item_count;
        const Box& box = arena.box(_row.box);
        row.dims = LineDims{box.width, box.height, box.depth, box.shift_amount, _row.prev_depth};
        const Node list[1] = {_row.box};
        const PackResult packed = vpack(arena, NodeView(list, 1));
        note(out.pack_error, packed.error);
        const Node alone = arena.add_box(packed.box, NodeView(list, 1));
        const ShipError shipped =
            ship(arena, alone, Point{0, packed.box.height}, measure, kept.cmds);
        if (out.ship_error == ShipError::NONE)
            out.ship_error = shipped;
        kept.shipped.insert(kept.shipped.end(), _row.shipped.begin(), _row.shipped.end());
        kept.cell_items.insert(kept.cell_items.end(), _row.cell_index_end.begin(),
                               _row.cell_index_end.end());
        row.cmd_end = static_cast<uint32_t>(kept.cmds.size());
        row.shipped_end = static_cast<uint32_t>(kept.shipped.size());
        row.cell_end = static_cast<uint32_t>(kept.cell_items.size());
        kept.rows.push_back(row);
        arena.rewind(before);
    }
    kept.valid = true;

    // The stack: the reference's `_stacked` for this block, with tex.web
    // section 679's glue computed from each box's dimensions and the
    // prev_depth each row carries. A box's commands are its own, moved down
    // to its top and renumbered from the indices before it.
    const InterlineSkip skip = style.interline;
    Scaled prev_depth = IGNORE_DEPTH;
    Scaled pen = 0;
    uint32_t index = 0;
    if (resumed) {
        const TableRowLayout& last = kept.rows[standing - 1];
        prev_depth = last.depth_after;
        pen = last.pen_after;
        index = last.index_after;
        out.cmds.resize(last.out_cmd_end);
        out.lines.resize(last.out_line_end);
        while (!out.originals.empty() && out.originals.back().index >= index)
            out.originals.pop_back();
        kept.row_top.resize(standing);
        kept.row_index.resize(standing);
        kept.cell_index.resize(standing * columns);
    }
    const size_t nodes_standing = resumed && cells.row_first[standing] != 0
                                      ? cells.cell_end[cells.row_first[standing] - 1]
                                      : 0;
    out.shipped.resize(nodes_standing);
    out.shipped.resize(cells.nodes.size(), NOT_SHIPPED);
    const auto place = [&](const LineDims& dims) -> Scaled {
        Box box;
        box.kind = BoxKind::HLIST;
        box.width = dims.width;
        box.height = dims.height;
        box.depth = dims.depth;
        box.shift_amount = dims.shift;
        Node glue_node;
        if (interline_glue(prev_depth, box, skip, glue_node))
            pen = plus(pen, glue_node.width(), out.pack_error);
        const Scaled top = pen;
        pen = plus(pen, plus(dims.height, dims.depth, out.pack_error), out.pack_error);
        prev_depth = dims.resumed;
        out.lines.push_back(dims);
        out.box_width = std::max(out.box_width, dims.width + dims.shift);
        return top;
    };
    const auto renumbered = [&](size_t from, Scaled top) {
        for (size_t k = from; k < out.cmds.size(); ++k) {
            DrawCmd& cmd = out.cmds[k];
            cmd.y += top;
            if (cmd.is(CmdType::RULE_RECT))
                continue;
            cmd.index += index;
            if (cmd.is(CmdType::LINE))
                cmd.end += index;
        }
    };
    const bool ruled = cells.has_header && style.rule_thickness != 0;
    for (size_t r = resumed ? standing : 0; r < rows; ++r) {
        ++table_rows_visited;
        TableRowLayout& row = kept.rows[r];
        const Scaled top = place(row.dims);
        kept.row_top.push_back(top);
        kept.row_index.push_back(index);
        const size_t cmd_first = r == 0 ? 0 : kept.rows[r - 1].cmd_end;
        const size_t from = out.cmds.size();
        out.cmds.insert(out.cmds.end(), kept.cmds.begin() + static_cast<std::ptrdiff_t>(cmd_first),
                        kept.cmds.begin() + static_cast<std::ptrdiff_t>(row.cmd_end));
        renumbered(from, top);
        // The row's nodes are the table's from its first cell's on.
        const size_t ship_first = r == 0 ? 0 : kept.rows[r - 1].shipped_end;
        const size_t node_first =
            cells.row_first[r] == 0 ? 0 : cells.cell_end[cells.row_first[r] - 1];
        for (size_t k = ship_first; k < row.shipped_end; ++k) {
            const size_t node = node_first + (k - ship_first);
            if (node < out.shipped.size() && kept.shipped[k] != NOT_SHIPPED)
                out.shipped[node] = kept.shipped[k] + index;
        }
        out.mapped = out.mapped && row.mapped;
        const size_t cell_first = r == 0 ? 0 : kept.rows[r - 1].cell_end;
        uint32_t cell_start = 0;
        for (size_t c = 0; c < columns; ++c) {
            kept.cell_index.push_back(index + cell_start);
            if (cell_first + c < row.cell_end)
                cell_start = kept.cell_items[cell_first + c];
        }
        index += row.item_count;
        if (r == 0 && ruled) {
            const NodeArena::Mark before = arena.mark();
            const Node rule_box =
                aligner.head_rule(arena, widths.data(), widths.size(), style, &out.pack_error);
            const Box& box = arena.box(rule_box);
            const LineDims dims{box.width, box.height, box.depth, box.shift_amount, box.depth};
            const Scaled rule_top = place(dims);
            const Node list[1] = {rule_box};
            const PackResult packed = vpack(arena, NodeView(list, 1));
            const Node alone = arena.add_box(packed.box, NodeView(list, 1));
            const size_t rule_from = out.cmds.size();
            const ShipError shipped =
                ship(arena, alone, Point{0, packed.box.height}, measure, out.cmds);
            if (out.ship_error == ShipError::NONE)
                out.ship_error = shipped;
            renumbered(rule_from, rule_top);
            arena.rewind(before);
        }
        row.pen_after = pen;
        row.depth_after = prev_depth;
        row.index_after = index;
        row.out_cmd_end = static_cast<uint32_t>(out.cmds.size());
        row.out_line_end = static_cast<uint32_t>(out.lines.size());
    }
    kept.assembled = true;
    kept.row_top.push_back(pen);
    kept.row_index.push_back(index);
    kept.cell_index.push_back(index);
    out.item_count = index;
    out.extent = clamp_extent(pen);

    // What a stand-in was written as, by the index it shipped at.
    for (size_t node = nodes_standing; node < cells.nodes.size(); ++node) {
        if (cells.nodes[node].is_replaced() && out.shipped[node] != NOT_SHIPPED)
            out.originals.push_back(Original{out.shipped[node], cells.nodes[node].original()});
    }
}

// --- Document -----------------------------------------------------------------------------

Document::Document() = default;

void Document::clear()
{
    // The blocks and their layouts stay, emptied when a number is given out
    // again: a document filled afresh reuses their buffers.
    _arena.clear();
    _count = 0;
    _quotes.clear();
    _lists.clear();
    _units.clear();
    _unit_of.clear();
    _bars.clear();
    _tree.clear();
    _inexact = 0;
    _prepared = false;
    _changed = 0;
    ++_epoch;
}

size_t Document::add_block()
{
    const size_t index = _count;
    if (index >= MAX_DOCUMENT_BLOCKS)
        return index;
    const bool spare = _blocks.size() > index;
    grow_to(_blocks, index + 1);
    grow_to(_layouts, index + 1);
    grow_to(_slots, index + 1);
    if (spare)
        reset_block(index);
    ++_count;
    _prepared = false;
    return index;
}

void Document::reset_block(size_t index)
{
    Block& block = _blocks[index];
    block.clear_content();
    block.type = BlockType::TEXT;
    block.style = BlockStyle();
    block.space_above = 0;
    block.space_below = 0;
    block.quote_depth = 0;
    block.quote = NO_CONTAINER;
    block.list = NO_CONTAINER;
    block.item = 0;
    block.thickness = 0;
    block.ink = 0;
    block.length_hint = 0;
    block.keep_with_next = false;
    _slots[index] = Slot();
}

bool Document::splice(size_t first, size_t removed, size_t inserted)
{
    if (first > _count || removed > _count - first)
        return false;
    if (_count - removed > MAX_DOCUMENT_BLOCKS || inserted > MAX_DOCUMENT_BLOCKS ||
        _count - removed + inserted > MAX_DOCUMENT_BLOCKS)
        return false;
    // From the unit the first block touched is in, the document is not the
    // one that stood: said here, where it is known, and not left to what
    // `prepare` can tell from the units' numbers and heights.
    if (_prepared && (removed != 0 || inserted != 0))
        note_changed(first < _unit_of.size() ? _unit_of[first] : _units.size());
    // The spare blocks kept past `_count` take the place of what is inserted
    // and what is removed joins them, so that neither loses its buffers: the
    // three sequences are rotated alike.
    const size_t old_count = _count;
    const size_t new_count = old_count - removed + inserted;
    grow_to(_blocks, new_count);
    grow_to(_layouts, new_count);
    grow_to(_slots, new_count);
    const auto rotate = [&](auto& items) {
        const auto begin = items.begin();
        if (inserted >= removed) {
            // Bring `inserted - removed` spares from the end of the document
            // to just after the removed range.
            const size_t grow = inserted - removed;
            std::rotate(begin + static_cast<std::ptrdiff_t>(first + removed),
                        begin + static_cast<std::ptrdiff_t>(old_count),
                        begin + static_cast<std::ptrdiff_t>(old_count + grow));
        } else {
            // The blocks removed beyond those reused go to the end, as spares.
            std::rotate(begin + static_cast<std::ptrdiff_t>(first + inserted),
                        begin + static_cast<std::ptrdiff_t>(first + removed),
                        begin + static_cast<std::ptrdiff_t>(old_count));
        }
    };
    rotate(_blocks);
    rotate(_layouts);
    rotate(_slots);
    for (size_t i = first; i < first + inserted; ++i)
        reset_block(i);
    _count = new_count;
    _prepared = false;
    return true;
}

Block& Document::edit_text(size_t i)
{
    if (!IMTYPESET_CONTRACT(i < _count, "Document::edit_text: no such block")) {
        _spare_block = Block();
        return _spare_block;
    }
    _slots[i].valid = false;
    if (_prepared && i < _unit_of.size()) {
        DocumentUnit& unit = _units[_unit_of[i]];
        if (unit.exact) {
            unit.exact = false;
            ++_inexact;
        }
        note_changed(_unit_of[i]);
    }
    return _blocks[i];
}

Block& Document::material(size_t i)
{
    if (!IMTYPESET_CONTRACT(i < _count, "Document::material: no such block")) {
        _spare_block = Block();
        return _spare_block;
    }
    return _blocks[i];
}

const Block& Document::block(size_t i) const
{
    if (!IMTYPESET_CONTRACT(i < _count, "Document::block: no such block"))
        return _spare_block;
    return _blocks[i];
}

const BlockLayout& Document::layout(size_t block) const
{
    if (!IMTYPESET_CONTRACT(block < _count, "Document::layout: no such block"))
        return _spare_layout;
    return _layouts[block];
}

const DocumentUnit& Document::unit(size_t u) const
{
    if (!IMTYPESET_CONTRACT(u < _units.size(), "Document::unit: no such unit"))
        return _spare_unit;
    return _units[u];
}

const Bar& Document::bar(size_t i) const
{
    if (!IMTYPESET_CONTRACT(i < _bars.size(), "Document::bar: no such bar"))
        return _spare_bar;
    return _bars[i];
}

size_t Document::unit_of(size_t block) const
{
    if (!IMTYPESET_CONTRACT(block < _unit_of.size(), "Document::unit_of: no such block"))
        return _units.size();
    return _unit_of[block];
}

Block& Document::edit(size_t i)
{
    if (!IMTYPESET_CONTRACT(i < _count, "Document::edit: no such block")) {
        _spare_block = Block();
        return _spare_block;
    }
    Slot& slot = _slots[i];
    slot.valid = false;
    slot.need_known = false;
    _prepared = false;
    return _blocks[i];
}

int32_t Document::add_quote(const Quote& quote)
{
    _quotes.push_back(quote);
    _prepared = false;
    return static_cast<int32_t>(_quotes.size() - 1);
}

int32_t Document::add_list(const List& list)
{
    _lists.push_back(list);
    _prepared = false;
    return static_cast<int32_t>(_lists.size() - 1);
}

Structure Document::structure() const
{
    Structure out;
    out.quotes = _quotes.data();
    out.quote_count = _quotes.size();
    out.lists = _lists.data();
    out.list_count = _lists.size();
    return out;
}

void Document::set_rules(const DocumentRules& rules)
{
    _rules = rules;
    ++_epoch;
    _prepared = false;
}

void Document::set_theme(const DocumentTheme& theme)
{
    _theme = theme;
    ++_epoch;
    _prepared = false;
}

void Document::set_width(Scaled width)
{
    width = clamp_dimen(width);
    if (width < 0)
        width = 0;
    if (width == _width)
        return;
    _width = width;
    _prepared = false;
}

bool Document::slot_valid(size_t block) const
{
    const Slot& slot = _slots[block];
    return slot.valid && slot.width == _width && slot.epoch == _epoch &&
           slot.indent == effective_indent(_blocks[block], _derived, _rules, _width);
}

bool Document::laid_out(size_t block) const
{
    return block < _count && slot_valid(block);
}

// A guess at a block nobody has laid out, from what it says: its lines at its
// leading. Text is taken to average three eighths of its leading a character.
// Only ever a placeholder: it is replaced by the block's height the first time
// a window shows it.
Scaled Document::estimate(size_t block) const
{
    const Block& b = _blocks[block];
    const double leading = static_cast<double>(blocks_clamped(b.style.strut).total());
    double lines = 1.0;
    switch (b.type) {
    case BlockType::TEXT: {
        const Scaled indent = effective_indent(b, _derived, _rules, _width);
        const Scaled room = _width - indent > UNITY ? _width - indent : UNITY;
        const size_t length = b.text.run.nodes.empty() ? b.length_hint : b.text.run.nodes.size();
        lines +=
            std::floor(static_cast<double>(length) * leading * 0.375 / static_cast<double>(room));
        break;
    }
    case BlockType::CODE:
        lines = static_cast<double>(b.code.count() != 0 ? b.code.count() : b.length_hint);
        if (lines == 0.0 && sets_box(b, _rules))
            lines = 1.0;
        break;
    case BlockType::RULE:
        return clamp_extent(clamp_dimen(b.thickness));
    case BlockType::TABLE:
        lines = static_cast<double>(b.table.rows() != 0 ? b.table.rows() : b.length_hint);
        // The rule under a header is a box of the stack like a row: the
        // interline glue of tex.web section 679 puts its foot one
        // \baselineskip below the header's baseline and the next row's
        // baseline one below that.
        if (lines > 0.0 && b.table.has_header && b.table.rule_thickness > 0)
            lines += 1.0;
        break;
    }
    const double extent = lines * leading;
    if (!(extent > 0.0))
        return 0;
    return extent >= static_cast<double>(MAX_BLOCK_EXTENT) ? MAX_BLOCK_EXTENT
                                                           : static_cast<Scaled>(extent);
}

void Document::prepare(DocumentComposer& composer)
{
    const size_t count = _count;
    const Structure containers = structure();

    composer.blocks_prepared += count;
    composer._needs.resize(count);
    for (size_t i = 0; i < count; ++i) {
        Slot& slot = _slots[i];
        if (!slot.need_known) {
            slot.need = marker_need(_arena, _blocks[i], composer.measure);
            slot.need_known = true;
        }
        composer._needs[i] = slot.need;
    }
    derive(_blocks.data(), count, composer._needs.data(), containers, _rules, _derived);

    // The units and the gaps between them: Stacker::stacked's loop at level 0,
    // with what each run would add to the list taken from what its blocks say.
    _units_before.swap(_units);
    _units.clear();
    _bars.clear();
    _unit_of.resize(count);
    _inexact = 0;
    const Block* above = nullptr;
    bool holds = false;
    for (size_t i = 0; i < count;) {
        const size_t next = run_end(_blocks.data(), i, count, 0, containers, _derived, _rules);
        DocumentUnit unit;
        unit.first_block = static_cast<uint32_t>(i);
        unit.block_count = static_cast<uint32_t>(next - i);
        unit.barred = quote_level(_blocks[i], containers, _derived, _rules) > 0;
        bool empty = true;
        for (size_t k = i; k < next; ++k) {
            empty = empty && !sets_box(_blocks[k], _rules);
            _unit_of[k] = static_cast<uint32_t>(_units.size());
        }
        if (!(_rules.skip_empty_blocks && empty)) {
            if (above != nullptr && holds)
                unit.gap = block_gap(*above, _blocks[i], 0, containers, _derived, _rules);
            above = &_blocks[next - 1];
            // A quoted run is a box whatever is in it.
            holds = holds || !empty || unit.barred;
        }

        Slot& head = _slots[i];
        if (!unit.barred) {
            unit.exact = slot_valid(i);
            unit.extent = head.ever ? _layouts[i].extent : estimate(i);
        } else if (head.run_count == unit.block_count) {
            // A quoted run is stacked again when a window shows it; until
            // then, the height it had.
            unit.extent = head.run_extent;
        } else {
            Scaled extent = 0;
            for (size_t k = i; k < next; ++k) {
                extent += _slots[k].ever ? _layouts[k].extent : estimate(k);
                if (k > i)
                    extent +=
                        block_gap(_blocks[k - 1], _blocks[k], 1, containers, _derived, _rules);
            }
            unit.extent = clamp_extent(extent);
        }
        if (!unit.exact)
            ++_inexact;
        // Whether it is the unit that stood here: the same blocks at the
        // same height under the same gap, laid out then and now, and from
        // the same layout (a block of another shape can be as tall).
        if (!unit.barred && unit.exact)
            unit.serial = _layouts[i].serial;
        const size_t index = _units.size();
        if (index >= _units_before.size()) {
            note_changed(index);
        } else {
            const DocumentUnit& was = _units_before[index];
            if (!(was.exact && unit.exact && was.first_block == unit.first_block &&
                  was.block_count == unit.block_count && was.barred == unit.barred &&
                  was.gap == unit.gap && was.extent == unit.extent && was.serial == unit.serial))
                note_changed(index);
        }
        _units.push_back(unit);
        i = next;
    }
    if (_units.size() != _units_before.size())
        note_changed(std::min(_units.size(), _units_before.size()));

    const size_t units = _units.size();
    _tree.assign(units + 1, 0);
    for (size_t k = 1; k <= units; ++k) {
        _tree[k] += _units[k - 1].gap + _units[k - 1].extent;
        const size_t parent = k + (k & (~k + 1));
        if (parent <= units)
            _tree[parent] += _tree[k];
    }
    _prepared = true;
}

Scaled Document::prefix(size_t u) const
{
    Scaled sum = 0;
    for (size_t k = u < _tree.size() ? u : (_tree.empty() ? 0 : _tree.size() - 1); k > 0;
         k -= k & (~k + 1))
        sum += _tree[k];
    return sum;
}

void Document::tree_add(size_t u, Scaled delta)
{
    for (size_t k = u + 1; k < _tree.size(); k += k & (~k + 1))
        _tree[k] += delta;
}

void Document::set_extent(size_t u, Scaled extent)
{
    extent = clamp_extent(extent);
    DocumentUnit& unit = _units[u];
    if (extent != unit.extent) {
        tree_add(u, extent - unit.extent);
        unit.extent = extent;
    }
}

Scaled Document::height() const
{
    return prefix(_units.size());
}

Scaled Document::content_width() const
{
    Scaled widest = 0;
    for (size_t i = 0; i < _count; ++i) {
        if (slot_valid(i) && _layouts[i].box_width > widest)
            widest = _layouts[i].box_width;
    }
    return widest;
}

Scaled Document::unit_top(size_t u) const
{
    if (u >= _units.size())
        return height();
    return prefix(u) + _units[u].gap;
}

size_t Document::unit_at(Scaled y) const
{
    const size_t units = _units.size();
    if (units == 0)
        return units;
    size_t step = 1;
    while (step * 2 <= units)
        step *= 2;
    size_t at = 0;
    // No sum of heights is past this, and it leaves room to subtract in.
    const Scaled bound = Scaled(1) << 61;
    const Scaled wanted = y > bound ? bound : (y < -bound ? -bound : y);
    Scaled left = wanted;
    for (; step > 0; step /= 2) {
        if (at + step <= units && _tree[at + step] <= left) {
            at += step;
            left -= _tree[at];
        }
    }
    // The descent assumes heights that only grow. A gap more negative than
    // its unit is tall breaks that; stepping back restores "starts at or
    // above y".
    while (at > 0 && prefix(at) > wanted)
        --at;
    return at;
}

Scaled Document::block_top(size_t block) const
{
    if (block >= _unit_of.size())
        return 0;
    return add_saturating(unit_top(_unit_of[block]), _slots[block].top);
}

void Document::ensure_unit(DocumentComposer& composer, size_t u)
{
    DocumentUnit& unit = _units[u];
    if (unit.exact)
        return;
    const size_t first = unit.first_block;
    const size_t end = first + unit.block_count;
    for (size_t b = first; b < end; ++b) {
        if (slot_valid(b))
            continue;
        if (composer.materialize != nullptr)
            composer.materialize(composer.materialize_user, b, _blocks[b]);
        const Scaled indent = effective_indent(_blocks[b], _derived, _rules, _width);
        composer.layout_block(_arena, _blocks[b], indent, _width, _rules, _layouts[b]);
        Slot& slot = _slots[b];
        slot.valid = true;
        slot.ever = true;
        slot.width = _width;
        slot.indent = indent;
        slot.epoch = _epoch;
    }
    if (unit.barred) {
        assemble(composer, u);
    } else {
        _slots[first].top = 0;
        set_extent(u, _layouts[first].extent);
        unit.serial = _layouts[first].serial;
    }
    unit.exact = true;
    --_inexact;
}

// A quoted run from its blocks' layouts: the same stacking as a whole document
// gets, over boxes that have the lines' dimensions and nothing in them.
void Document::assemble(DocumentComposer& composer, size_t u)
{
    DocumentUnit& unit = _units[u];
    const size_t first = unit.first_block;
    const size_t count = unit.block_count;
    const Rewound rewound(_arena);

    composer._line_first.clear();
    composer._lines.clear();
    composer._resumed.clear();
    composer._indents.clear();
    for (size_t b = first; b < first + count; ++b) {
        composer._line_first.push_back(static_cast<uint32_t>(composer._lines.size()));
        composer._indents.push_back(_slots[b].indent);
        for (const LineDims& dims : _layouts[b].lines) {
            Box box;
            box.kind = BoxKind::HLIST;
            box.width = dims.width;
            box.height = dims.height;
            box.depth = dims.depth;
            box.shift_amount = dims.shift;
            composer._lines.push_back(_arena.add_box(box));
            composer._resumed.push_back(dims.resumed);
        }
    }
    composer._line_first.push_back(static_cast<uint32_t>(composer._lines.size()));

    StackInput in;
    in.blocks = _blocks.data() + first;
    in.count = count;
    in.first_number = static_cast<uint32_t>(first);
    in.line_first = composer._line_first.data();
    in.lines = composer._lines.data();
    in.resumed = composer._resumed.data();
    in.indents = composer._indents.data();
    in.structure = structure();
    in.derived = &_derived;
    in.theme = _theme;
    in.rules = _rules;
    in.measure = composer.measure;
    in.width = _width;
    composer._stacked.tracing = false;
    composer._stacker.stack(_arena, in, composer._stacked);
    ++composer.units_assembled;

    for (size_t k = 0; k < count; ++k)
        _slots[first + k].top = composer._stacked.extents[k].top;
    unit.first_bar = static_cast<uint32_t>(_bars.size());
    unit.bar_count = static_cast<uint32_t>(composer._stacked.bars.size());
    _bars.insert(_bars.end(), composer._stacked.bars.begin(), composer._stacked.bars.end());
    set_extent(u, composer._stacked.extent);
    _slots[first].run_extent = unit.extent;
    _slots[first].run_count = unit.block_count;
}

UnitRange Document::layout_window(DocumentComposer& composer, Scaled top, Scaled bottom)
{
    if (!_prepared)
        prepare(composer);
    const size_t units = _units.size();
    UnitRange range;
    range.first = unit_at(top);
    range.end = range.first;
    if (range.first >= units)
        return range;
    // Downward from the first unit the window touches. Laying a unit out
    // changes its height and so moves only what is below it, which this loop
    // has not reached yet.
    Scaled pen = prefix(range.first);
    size_t u = range.first;
    for (; u < units; ++u) {
        const Scaled unit_top_edge = pen + _units[u].gap;
        if (u > range.first && unit_top_edge >= bottom)
            break;
        ensure_unit(composer, u);
        pen = unit_top_edge + _units[u].extent;
    }
    range.end = u;
    return range;
}

void Document::layout_unit(DocumentComposer& composer, size_t u)
{
    if (!_prepared)
        prepare(composer);
    if (u < _units.size())
        ensure_unit(composer, u);
}

Scaled Document::top_in_unit(size_t block) const
{
    return block < _count ? _slots[block].top : 0;
}

void Document::layout_all(DocumentComposer& composer)
{
    if (!_prepared)
        prepare(composer);
    for (size_t u = 0; u < _units.size(); ++u)
        ensure_unit(composer, u);
}

size_t Document::take_changed()
{
    const size_t changed = _changed;
    _changed = UNCHANGED;
    return changed;
}

namespace
{

// A block's boxes as they stand in the list: `each(line, top)` for every box,
// with the interline glue above it (if any) through `glue(node, top)`.
// tex.web section 679, from a fresh prev_depth, with the prev_depth a table
// row whose cells wrapped carries (LineDims::resumed): what layout_block and
// the stack do with the boxes themselves.
template<typename Glue, typename Line>
void walk_lines(const Block& block, const BlockLayout& layout, Glue&& glue_at, Line&& line_at)
{
    const InterlineSkip skip = blocks_clamped(block.style.interline);
    Scaled prev_depth = IGNORE_DEPTH;
    Scaled pen = 0;
    for (size_t l = 0; l < layout.lines.size(); ++l) {
        const LineDims& dims = layout.lines[l];
        Box box;
        box.height = dims.height;
        box.depth = dims.depth;
        Node above;
        if (interline_glue(prev_depth, box, skip, above)) {
            glue_at(above, pen);
            pen = add_saturating(pen, above.width());
        }
        line_at(l, dims, pen);
        pen = add_saturating(pen, add_saturating(dims.height, dims.depth));
        prev_depth = dims.resumed;
    }
}

} // namespace

void Document::block_line_tops(size_t block, std::vector<Scaled>& tops) const
{
    tops.clear();
    if (block >= _count)
        return;
    walk_lines(
        _blocks[block], _layouts[block], [](const Node&, Scaled) {},
        [&](size_t, const LineDims&, Scaled top) { tops.push_back(top); });
}

void Document::unit_vlist(size_t u, std::vector<Node>& out) const
{
    if (!IMTYPESET_CONTRACT(u < _units.size(), "Document::unit_vlist: no such unit"))
        return;
    const DocumentUnit& unit = _units[u];
    const size_t first = unit.first_block;
    const bool boxes = unit.barred || (unit.block_count != 0 && !_layouts[first].lines.empty());

    // The gap. In the stack it is a kern, there when it is not zero. What it
    // follows is the nearest block above that has something in it: a text
    // with nothing in it is a blank line or no line at all, and a heading
    // above it is kept with this unit as it would be without it.
    bool kept_with_above = false;
    for (size_t above = first; above-- > 0;) {
        const Block& block = _blocks[above];
        const bool nothing =
            slot_valid(above) && (_layouts[above].lines.empty() ||
                                  (block.type == BlockType::TEXT && block.text.run.nodes.empty() &&
                                   block.text.marker.nodes.empty()));
        if (!nothing) {
            kept_with_above = block.keep_with_next;
            break;
        }
    }
    const bool breakable =
        _rules.break_between_blocks && u > 0 && boxes && first > 0 && !kept_with_above;
    if (breakable)
        out.push_back(glue(GlueSpec{unit.gap, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL}));
    else if (unit.gap != 0)
        out.push_back(kern(unit.gap));

    // A box is a rule of its height and depth: a height that is the rule's
    // own sentinel is no height.
    const auto box = [&](Scaled width, Scaled height, Scaled depth) {
        out.push_back(rule(width == RUNNING ? 0 : width, height == RUNNING ? 0 : height,
                           depth == RUNNING ? 0 : depth));
    };
    if (unit.barred) {
        // A quoted run is one box: its outermost bar's.
        if (unit.bar_count != 0 && unit.first_bar < _bars.size()) {
            const Bar& bar = _bars[unit.first_bar];
            box(bar.box_width, bar.box_height, bar.box_depth);
        } else {
            box(_width, unit.extent, 0);
        }
        return;
    }
    if (unit.block_count == 0)
        return;
    // Rule 17: the boxes a header is kept with are the one after it (its
    // rule, or the next row of a table that draws none) and, after a rule,
    // the row under that.
    const Block& block = _blocks[first];
    size_t kept = 0;
    if (_rules.keep_table_header && block.type == BlockType::TABLE && block.table.has_header)
        kept = clamp_dimen(block.table.rule_thickness) > 0 ? 2 : 1;
    size_t boxes_out = 0;
    walk_lines(
        block, _layouts[first],
        [&](const Node& above, Scaled) {
            out.push_back(boxes_out <= kept ? kern(above.width()) : above);
        },
        [&](size_t, const LineDims& dims, Scaled) {
            box(dims.width, dims.height, dims.depth);
            ++boxes_out;
        });
}

bool Document::flatten(std::vector<DrawCmd>& out, std::vector<BlockSpan>* spans) const
{
    if (!settled())
        return false;
    const size_t base = out.size();
    if (spans != nullptr)
        spans->assign(_count, BlockSpan());

    // The quotes open round the block being written: where each one's LINE is,
    // and the block its run ends before.
    struct Open {
        size_t slot = 0;
        size_t end_block = 0;
    };
    Open open[MAX_QUOTE_DEPTH + 1];
    size_t depth = 0;
    uint64_t index = 0;
    Scaled pen = 0;
    for (const DocumentUnit& unit : _units) {
        const Scaled top = pen + unit.gap;
        size_t bar = unit.first_bar;
        const size_t bar_end = bar + unit.bar_count;
        const size_t first = unit.first_block;
        const size_t end = first + unit.block_count;
        for (size_t b = first; b <= end; ++b) {
            while (depth > 0 && open[depth - 1].end_block <= b) {
                out[open[depth - 1].slot].end = static_cast<uint32_t>(index);
                --depth;
            }
            if (b == end)
                break;
            // A quote that starts here: its box is a line of the list it is
            // in, and its bar the first thing in that line.
            for (; bar < bar_end && _bars[bar].first_block == b && depth <= MAX_QUOTE_DEPTH;
                 ++bar) {
                const Bar& found = _bars[bar];
                open[depth].slot = out.size();
                open[depth].end_block = static_cast<size_t>(found.first_block) + found.block_count;
                ++depth;
                out.push_back(DrawCmd::line(
                    0, add_saturating(add_saturating(top, found.box_top), found.box_height),
                    found.box_width, found.box_height, found.box_depth,
                    static_cast<uint32_t>(index), static_cast<uint32_t>(index)));
                if (found.extent > 0 && found.thickness > 0)
                    out.push_back(DrawCmd::rule_rect(found.x, add_saturating(top, found.top),
                                                     found.thickness, found.extent));
            }
            const BlockLayout& layout = _layouts[b];
            const Scaled block_top_edge = add_saturating(top, _slots[b].top);
            const size_t start = out.size();
            for (DrawCmd cmd : layout.cmds) {
                cmd.y = add_saturating(cmd.y, block_top_edge);
                if (!cmd.is(CmdType::RULE_RECT)) {
                    cmd.index += static_cast<uint32_t>(index);
                    if (cmd.is(CmdType::LINE))
                        cmd.end += static_cast<uint32_t>(index);
                }
                out.push_back(cmd);
            }
            if (spans != nullptr) {
                BlockSpan& span = (*spans)[b];
                span.cmd_start = static_cast<uint32_t>(start - base);
                span.cmd_end = static_cast<uint32_t>(out.size() - base);
                span.index_start = static_cast<uint32_t>(index);
                span.index_end = static_cast<uint32_t>(index + layout.item_count);
                span.top = block_top_edge;
            }
            index += layout.item_count;
            if (index > 0xFFFFFFFFull) {
                out.resize(base);
                return false;
            }
        }
        pen = top + unit.extent;
    }
    return true;
}

} // namespace ImTypeset::detail
