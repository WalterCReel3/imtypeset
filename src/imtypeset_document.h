// What stands behind ImTypesetDocument, the third opaque type of
// include/imtypeset.h. Internal: src/imtypeset_document.cpp and a test include
// it, a host never does. Kept apart from src/context.h because it needs
// src/blocks.h, whose names (Document, Block) a file that only wants the
// context should not have to share a namespace with.

#pragma once

#include "blocks.h"
#include "context.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// --- Documents ------------------------------------------------------------------------
//
// An ImTypesetDocument is src/blocks' Document with, beside each of its blocks,
// what the host said (`DocBlock`): the text, the runs, the marker, which style.
// A block's MATERIAL is made when the block layer first lays the block out
// (DocumentComposer::materialize), by the function that makes a paragraph's,
// so a document of many blocks is set at the cost of copying its text.
//
// What a question about a block needs beyond its commands -- the lines, the
// index-to-byte map, the selection model -- is a paragraph's, and is kept the
// same way: in an ImTypesetParagraph filled from the block's layout
// (`DocView`). Views are few and reused: the blocks a selection or the mouse
// touches, not the document.
//
// REFERENCE RULES. A document lays out under DocumentRules::product() and the
// context's TexRules. `ImTypesetDocument::reference_rules` selects the
// reference's stacking rules and first-line shape instead, as
// ImTypesetContext::tex_rules selects its line breaking: a test that replays
// the reference's goldens through the public calls sets both, and no public
// call sets either. Every length of such a replay goes in through the public
// description, as float pixels, and is held to the golden's scaled points
// exactly (tests/document_reference.h).

namespace ImTypeset::detail
{

// A block's style with every length in scaled points: what the public
// ImTypesetBlock, its ImTypesetBlockStyle and the document's style come to.
struct BlockSpec {
    BlockType type = BlockType::TEXT;
    FontSpec font;
    FontSpec marker_font;
    Align align = Align::JUSTIFY;
    Scaled left_indent = 0;
    Scaled first_line_indent = 0;
    FirstLineIndent first_line = FirstLineIndent::BOX_ONLY;
    Scaled marker_gap = 0;
    InterlineSkip interline;
    Strut strut;
    Scaled space_above = 0;
    Scaled space_below = 0;
    // RULE: the thickness. A host block: its height.
    Scaled extent = 0;
    uint32_t quote_depth = 0;
};

// One cell of a table, as bytes of its block's text.
struct DocCell {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t row = 0;
    uint32_t column = 0;
};

struct DocBlock {
    ImTypesetBlockKind kind = ImTypesetBlockKind_Paragraph;
    ImTypesetBlockFlags flags = 0;
    int level = 0;
    int style = -1;
    int container = -1;
    int item = 0;
    int quote_depth = 0;
    float left_indent = 0.0f;
    float extent = 0.0f;
    std::string text;
    std::string marker;
    std::vector<StyledRun> runs;
    ErrorState text_error;
    ErrorState error;
    // The ink of the block and of its marker, as given (0: the document's).
    ImU32 colour = 0;
    ImU32 marker_colour = 0;
    FontSpec font;

    // --- Material
    bool has_material = false;
    bool fallback = false;
    bool display_block = false;
    // Per node of the material (TEXT) or of the code lines (CODE): the byte
    // of `text` it came from.
    std::vector<uint32_t> source_offsets;
    std::vector<InkSpan> ink_spans;
    std::vector<AtomSource> atoms;
    std::string display_text;
    std::vector<uint32_t> display_map;
    // CODE: one past the last byte of the last character that is set, and how
    // many lines there are once blank ones at the end are dropped. Both are
    // read off the source alone, when the block is described.
    uint32_t code_end = 0;
    uint32_t code_lines = 0;
    // How many of the document arena's boxes are this block's formulas.
    uint32_t atom_boxes = 0;

    // --- A table (see "Tables" in include/imtypeset.h)
    // The cells, in the order given: ascending in the text and row after row.
    std::vector<DocCell> cells;
    // Row r is cells row_first[r] to row_first[r + 1]: one entry more than
    // there are rows, or empty with no cells. `columns` is the longest row's
    // cells, and `next_column` the column the next cell of the last row takes
    // (which is past the limit once cells of the row have been left out).
    std::vector<uint32_t> row_first;
    uint32_t columns = 0;
    uint32_t next_column = 0;
    std::vector<ImTypesetTableColumn> column_specs;
    // The cells [0, cells_built) have their material in the block layer. Of
    // the document arena's boxes, last_cell_boxes are the last such cell's,
    // and its part of `display_text` starts at last_cell_display.
    uint32_t cells_built = 0;
    uint32_t last_cell_boxes = 0;
    uint32_t last_cell_display = 0;
    // With ImTypesetBlockFlags_TableHeld: the widest each column's cells have
    // wanted at any layout, in scaled points.
    std::vector<Scaled> held;
    // From the layout whose serial is `seen`: the byte of `text` each index of
    // the table's commands stands for, and one past the end.
    std::vector<uint32_t> table_bytes;

    uint32_t row_count() const
    {
        return row_first.empty() ? 0 : static_cast<uint32_t>(row_first.size() - 1);
    }

    // --- What the description came to against the document's style
    bool has_spec = false;
    Align align = Align::JUSTIFY;
    Scaled first_line_indent = 0;

    // --- From the layout whose serial is `seen`
    uint64_t seen = 0;
    // How many of its first indices are the marker's.
    uint32_t marker_items = 0;
    std::vector<InkRange> inks;
    LineIndex lines;
};

// A block as a paragraph: see the top of this section.
struct DocView {
    ImTypesetParagraph p;
    int block = -1;
    uint64_t serial = 0;
    uint64_t used = 0;
};

} // namespace ImTypeset::detail

struct ImTypesetDocument {
    ImTypesetContext* ctx = nullptr;
    size_t slot = 0;

    ImTypeset::detail::Document doc;
    ImTypeset::detail::DocumentComposer composer;
    std::vector<ImTypeset::detail::DocBlock> blocks;
    std::vector<ImTypesetBlockStyle> styles;
    std::vector<ImTypesetContainer> containers;
    // Per container: its id among the block layer's quotes or lists.
    std::vector<int32_t> container_quote;
    std::vector<int32_t> container_list;
    uint64_t spec_visits = 0;
    ImTypesetDocumentOptions options;
    // See REFERENCE RULES above. Read when the rules are next applied and a
    // block's description next resolved: set it before the blocks.
    bool reference_rules = false;

    // --- As of the last LayoutDocument
    bool has_style = false;
    ImTypesetStyle given_style;
    ImTypeset::detail::ResolvedStyle style;
    float width_px = 0.0f;
    ImTypeset::detail::Scaled width = 0;
    uint32_t generation = 0;
    // Blocks were added, removed or described again since their specs were
    // last resolved against the style.
    bool specs_stale = true;

    // --- Scratch and caches
    // The paragraph a block's material is built in.
    ImTypesetParagraph work;
    std::vector<std::unique_ptr<ImTypeset::detail::DocView>> views;
    uint64_t tick = 0;
    size_t shown_blocks = 0;
    std::vector<ImTypeset::detail::DrawCmd> bar_cmds;
    std::vector<ImTypeset::detail::Fill> fills;
    std::vector<ImTypeset::detail::Rect> highlights;
    std::vector<ImTypeset::detail::InkRange> inks;
    std::vector<ImTypeset::detail::InkRange> inks_scratch;
    // Formula boxes of the arena that no block's material names any more.
    size_t dead_boxes = 0;
    size_t live_boxes = 0;

    ImTypesetDocumentStats stats = {};
    ImTypeset::detail::ErrorState no_error;

    // --- DocumentView's state
    ImTypesetDocumentRange selection = {{0, 0}, {0, 0}};
    ImVec2 anchor = ImVec2(0.0f, 0.0f);
    ImTypesetGranularity granularity = ImTypesetGranularity_Character;
};

namespace ImTypeset::detail
{

// The block layer under a document, for a test to compare: flatten() it.
Document& block_document(ImTypesetDocument& document);
DocumentComposer& block_composer(ImTypesetDocument& document);

} // namespace ImTypeset::detail
