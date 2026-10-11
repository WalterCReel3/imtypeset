// Columns: a Document's vertical list cut into columns that are read foot of
// one to head of the next. Mirrors the rest of advtext/columns.py
// (`split_into_columns` as `compose_into_columns` applies it to a composed
// document); the cut itself is src/vsplit, which this is a caller of.
//
// WHAT OF tex.web THIS IS. Nothing new: every cut is vert_break (sections 970
// to 976) after prune_page_top (sections 968, 969), as vsplit (section 977)
// puts them together. tex.web has no columns (docs/divergences.md,
// "columns-core", V8), and what is here beyond the reference's loop -- a list
// made a unit at a time, cuts kept and taken back, balancing -- is neither's.
//
// THE DOCUMENT IS NOT CUT. A Document keeps one layout per block and places
// them down one column (src/blocks.h). Nothing of that changes: a column here
// is a statement about that one column -- where the stretch of it begins that
// the column shows, and how far down the column's own box it is put. Because
// a document's list has no stretch or shrink in it, the box vsplit packs
// `exactly` to the column's height sets every glue at its natural width, and
// what a column shows is that stretch of the uncut document moved, not set
// again: `flatten` is held to the reference's shipped columns for it
// (tests/test_api_columns.cpp). A list with stretch in it would be spread to
// the column's height by section 977 and is not what this can show; no block
// makes one (src/blocks.h: the gaps are kerns, the interline glue a block's
// own, which no public description gives stretch).
//
// THE LIST. Document::unit_vlist gives a unit's part of the list by dimension,
// a box as a RULE node of its height and depth: the scan reads nothing else of
// a box (sections 969 and 973 take boxes and rules alike), and a rule carries
// its dimensions in the node, so the list needs no arena and none of it
// outlives a cut.
//
// TWO WAYS OF FILLING.
//
//   FILL     Every column is ColumnRules::height tall and takes what vert_break
//            gives it; there are as many columns as the document needs. This is
//            the reference. A column is cut when it is first asked for, from
//            the units it reads: the ones it holds and the one after, where
//            the scan finds the page too full. So the first columns of a long
//            document cost what they show; column k costs everything before
//            it, once, since where it starts is where the others ended.
//   BALANCE  ColumnRules::balance columns of one height, the least at which
//            that many hold the document: section 977's cut tried at one
//            height after another (a bisection, each trial one pass over the
//            list). It needs every unit's height, so every block is laid out,
//            and any change to the document cuts all of it again. Not the
//            reference's, which does not balance.
//
// WHAT IS KEPT, AND WHEN IT IS TAKEN BACK. A cut column is kept with the first
// unit its scan did not read. Document::take_changed says from which unit on
// the document is no longer what it was; the columns whose scans read that far
// are dropped and cut again when asked for, and the ones before them stand.
// Text appended to the last block cuts the last column or two again.
//
// ERRORS. Everything is data: a height that is not positive is one scaled
// point, and a count is at most MAX_BALANCED_COLUMNS.
//
// Not here: where columns go across a page (a gutter, an x: the caller's),
// rules between columns, pages of columns, keeping a heading with what follows
// it (that is which nodes the list has: DocumentRules::break_between_blocks
// and Block::keep_with_next), widows and orphans (section 974 has nothing to
// weigh them with in a list that cannot stretch: every short page costs
// `deplorable` whatever its penalty).

#pragma once

#include "blocks.h"
#include "nodes.h"
#include "shipout.h"
#include "units.h"
#include "vsplit.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// The most columns a document is balanced into.
constexpr uint32_t MAX_BALANCED_COLUMNS = 4096;

struct ColumnRules {
    // FILL: how tall every column is.
    Scaled height = 0;
    // If not zero, BALANCE: how many columns; `height` is not read.
    uint32_t balance = 0;
    // tex.web's \splittopskip and \splitmaxdepth.
    GlueSpec split_top_skip = SPLIT_TOP_SKIP;
    Scaled split_max_depth = MAX_DIMEN;

    friend bool operator==(const ColumnRules& a, const ColumnRules& b)
    {
        return a.height == b.height && a.balance == b.balance &&
               a.split_top_skip == b.split_top_skip && a.split_max_depth == b.split_max_depth;
    }
    friend bool operator!=(const ColumnRules& a, const ColumnRules& b) { return !(a == b); }
};

// One column: a stretch of the uncut document, and where it is put.
struct DocumentColumn {
    // The unit its first box is of.
    uint32_t first_unit = 0;
    // One past the last unit the scan that cut it read.
    uint32_t read_end = 0;
    // Its first box's place in the document's whole vertical list, and where
    // the cut fell, counted in the list the column was handed (its glue is
    // 0): the reference's `start` and `place`.
    uint64_t first_node = 0;
    size_t place = 0;
    // The top of its first box in the uncut document; where its last box
    // ends there, depth included; and the top of the next column's first
    // box. A line whose top is at or after `top` and before `limit` is this
    // column's.
    Scaled top = 0;
    Scaled bottom = 0;
    Scaled limit = 0;
    // The glue above its first box: section 969's.
    Scaled head = 0;
    // How far down the column its material reaches, the glue and its last
    // box's depth included: section 971's best_height_plus_depth.
    Scaled used = 0;
    int64_t least_cost = 0;
    ScanStop stop = ScanStop::END;
    // The unit the next column's first box is of, and the place of that
    // unit's first node in the whole list: where cutting goes on from.
    uint32_t next_unit = 0;
    uint64_t next_unit_node = 0;
    // It holds something taller than itself: its one legal cut was already
    // past its height.
    bool overfull = false;
    // Nothing was left for it.
    bool empty = false;
};

// A document's columns. One per Document, kept beside it; not thread-safe.
class DocumentColumns
{
public:
    // Every column is cut again after a change.
    void set_rules(const ColumnRules& rules);
    const ColumnRules& rules() const { return _rules; }

    // Work done since this was made: columns cut (each one scan of section
    // 970), units whose part of the list was read, and heights tried by a
    // balancing.
    uint64_t columns_cut = 0;
    uint64_t units_read = 0;
    uint64_t heights_tried = 0;

    // Brings the columns up to date with the document and cuts as far as
    // column `k`: under FILL the columns up to it and no further, under
    // BALANCE all of them. True if there is a column `k`.
    bool cut_through(Document& document, DocumentComposer& composer, size_t k);
    // The same, as far as the column that shows height `y` of the uncut
    // document, which is answered: the last column whose top is at or above
    // it, the first for anything above that. Zero for a document with
    // nothing in it.
    size_t column_at(Document& document, DocumentComposer& composer, Scaled y);
    // Cuts every column.
    void cut_all(Document& document, DocumentComposer& composer);

    // What follows answers for the columns as last brought up to date.

    // How many columns are cut, and whether those are all there are.
    size_t count() const { return _columns.size(); }
    bool finished() const { return _finished; }
    // How many there will be: count() when finished, and otherwise that plus
    // what the document's height below the last cut, estimates included,
    // comes to in columns.
    size_t estimated_count(const Document& document) const;
    const DocumentColumn& column(size_t k) const;
    // How tall a column is: the rule's height, or the balanced one, which is
    // how far the tallest column's material reaches.
    Scaled height() const { return _height; }
    // The least height at which the balanced columns held the document: what
    // the cut was made at. Zero under FILL.
    Scaled balanced_at() const { return _balanced_at; }

    // Column `k` as one command sequence, as shipping the reference's column
    // box from its top-left corner gives it: the commands of the stretch of
    // the document it shows, moved to the column and renumbered from zero.
    // False, with nothing appended, if there is no such column cut.
    bool flatten(const Document& document, size_t k, std::vector<DrawCmd>& out) const;

    // The part of the document's vertical list the working copy holds, for a
    // test that compares it with the reference's: the nodes of the units
    // read since the last column kept, a box as a RULE.
    const std::vector<Node>& working_list() const { return _list; }

private:
    static constexpr size_t NONE = static_cast<size_t>(-1);

    void drop_from(size_t k);
    void bring_up_to_date(Document& document, DocumentComposer& composer);
    void load_unit(Document& document, DocumentComposer& composer);
    size_t next_box(Document& document, DocumentComposer& composer, size_t from);
    void open(Document& document, DocumentComposer& composer);
    bool cut_next(Document& document, DocumentComposer& composer, Scaled height);
    bool holds(size_t first, Scaled height, uint32_t count);
    void balance(Document& document, DocumentComposer& composer);
    Node head_glue(size_t box) const;
    size_t unit_first(uint32_t unit) const;

    ColumnRules _rules;
    std::vector<DocumentColumn> _columns;
    bool _finished = false;
    Scaled _height = 0;
    Scaled _balanced_at = 0;

    // The working copy of the list: the nodes from _list_first_node on, with
    // the top of each in the uncut document and the unit it is of. `_start`
    // is the first box of the column to cut next, or NONE when it has not
    // been looked for.
    std::vector<Node> _list;
    std::vector<Scaled> _tops;
    std::vector<uint32_t> _units;
    // Per unit read, in order: the unit, and where its nodes begin in _list.
    struct UnitFirst {
        uint32_t unit = 0;
        size_t first = 0;
    };
    std::vector<UnitFirst> _unit_first;
    uint64_t _list_first_node = 0;
    uint32_t _next_unit = 0;
    size_t _start = NONE;
    bool _open = false;
    VertBreak _scan;
    NodeArena _no_boxes;
    std::vector<Node> _unit_nodes;
};

} // namespace ImTypeset::detail
