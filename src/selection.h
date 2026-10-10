// Selection: turning shipped draw commands back into carets, ranges,
// rectangles and text. Mirrors the geometric core of advtext/selection.py.
//
// A widget cannot make its text selectable because it never tells anyone where
// a glyph landed. Ship-out did: so hit testing here is arithmetic over the
// command buffer, with no font, no metrics and no ImGui.
//
// It needs no metrics because of how the commands are shaped. A GLYPH carries
// no advance, so the right edge of an item is the x of the next item on the
// same line, and for the last item the line's right edge. That is exact, and it
// puts a kern between two glyphs inside the left one's cell, so no click falls
// in a gap.
//
// BOUNDARIES, NOT GLYPHS. A caret sits between items, so a position is an
// integer in [0, n]: boundary b is immediately before item b, and n is past the
// end. Boundaries are the reading-order indices ship-out assigns (src/shipout.h),
// which are positions in the shipped sequence and not offsets into any source
// text. The left half of an item gives the boundary before it and the right
// half the one after.
//
// There is no caret affinity. The boundary at the end of line k and at the
// start of line k+1 are the same number, and it never shows, because rectangles
// are derived from the items a range covers and not from its endpoints.
//
// LINES. Each LINE command is a visual line; lines nest where boxes nested, and
// an item belongs to the innermost line containing it. A line that holds only
// other lines is a container and not a line of text, and neither is one that
// holds a rule and no item (a thematic break). What is left are the model's
// *fragments*, in reading order. A fragment nested in another line remembers
// the outermost one as its `outer`: the row it is part of.
//
// An ATOM is one item. The GLYPHs that follow it carrying its index are its
// ink and are not items, so there is no boundary inside a formula.
//
// FILL is a SET_GLUE whose natural width is zero: the glue that makes a line
// ragged or centres a cell, not a space anybody typed. It is an item and takes
// an index, but it is left out of a highlight at either end of what is
// selected, and out of copied text except where it stands between two
// characters on one line, where any run of it is one space. An ATOM is never
// fill, whatever its width.
//
// TEXT. A line break made by layout is copied as a single space, never as a
// newline: a paragraph wrapped into five lines is one paragraph. Which breaks
// are content -- the end of a block, a line of code, a table cell -- the
// commands do not say, so the layer that built the document passes it as data
// (TextData), along with what an atom copies as and what a replaced character
// was written as.
//
// COST. A model is built once per shipped buffer, in time linear in it, and
// holds no reference to the buffer afterwards. Point queries (fragment_at, hit,
// word_at, line_at, select_at) allocate nothing. Where lines are stacked down
// a column they take O(log lines + log items in the line): fragments are
// grouped into *clusters*, runs in reading order that no other fragment
// overlaps vertically, the cluster is found by bisection, and the reference's
// rule is then applied inside it. A table row whose cells wrapped, or columns
// side by side, are one cluster each and are scanned. The answers are the
// reference's whatever the layout.
//
// Not here, as in the reference: a caret rectangle, editing, bidi, and the
// reference's UnitModel, which belongs to the views this port does not have.

#pragma once

#include "shipout.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ImTypeset::detail
{

// A caret position: immediately before item `b`, in [0, length].
using Boundary = uint32_t;

// A half-open range of items, [start, end). Empty when start >= end, which is
// what a plain click gives.
struct Selection {
    Boundary start = 0;
    Boundary end = 0;

    constexpr bool is_empty() const { return start >= end; }
    constexpr bool covers(uint32_t index) const { return start <= index && index < end; }

    friend constexpr bool operator==(Selection a, Selection b)
    {
        return a.start == b.start && a.end == b.end;
    }
    friend constexpr bool operator!=(Selection a, Selection b) { return !(a == b); }
};

// How much one gesture selects: a click, a double click, a triple click. A
// drag keeps the granularity it began with, which is why it is a value.
enum class Granularity : uint8_t { CHARACTER = 0, WORD, LINE };

// What kind of thing an item is.
enum class ItemKind : uint8_t {
    GLYPH = 0, // a character
    SPACE,     // a SET_GLUE with a natural width
    FILL,      // a SET_GLUE with none
    ATOM,      // a formula
};

// One visual line with items of its own (or a blank line of text).
struct Fragment {
    // Where its LINE is in the command buffer the model was built from, and
    // where the outermost LINE it is nested in is, or -1 if it stands alone.
    uint32_t line = 0;
    int32_t outer = -1;
    // The line's index range. A fragment's own items need not fill it: a line
    // that encloses another owns only what is outside the inner one.
    Boundary start = 0;
    Boundary end = 0;
    // The line's left edge and width, and the band it occupies:
    // [top, bottom) is [y - height, y + depth).
    Scaled x = 0;
    Scaled width = 0;
    Scaled top = 0;
    Scaled bottom = 0;
    // The band of the row it is part of: its outer line's, or its own.
    Scaled reach_top = 0;
    Scaled reach_bottom = 0;
    // Its items: `item_count` of them starting at `first_item` in the model's
    // item arrays, and `item_count + 1` edges starting at `first_edge`.
    uint32_t first_item = 0;
    uint32_t item_count = 0;
    uint32_t first_edge = 0;
    // Whether the right edges of its items never decrease left to right, which
    // is what lets a point be found by bisection.
    bool edges_sorted = true;
};

// A piece of text keyed by a boundary or an item's index.
struct IndexedText {
    uint32_t index = 0;
    const char* text = "";
    size_t size = 0;
};

// What `text` cannot know from the commands. Each array is sorted by index,
// ascending, with no index twice: the reference's are a set and three
// dictionaries. `text` checks the order of each, once a call and in one pass
// over it, and one that is out of order is searched entry by entry instead of
// by bisection, so the answer is the same and only the time is not. Of two
// entries with one index the first is used.
struct TextData {
    // Boundaries at which a line begins that is a line of the source: each
    // means a newline before the item there, if that item starts its line.
    const uint32_t* hard_breaks = nullptr;
    size_t hard_break_count = 0;
    // What belongs before the item at a boundary: "\n\n" at the start of a
    // block, "\t" at the start of a table cell, " " after a list marker.
    // Everything not named is a wrap and joins with one space.
    const IndexedText* separators = nullptr;
    size_t separator_count = 0;
    // What an ATOM copies as, by its index: a formula's source with its
    // dollars. One not named copies as nothing.
    const IndexedText* atoms = nullptr;
    size_t atom_count = 0;
    // What a glyph was written as, where that is not what was drawn (a
    // character set as U+FFFD). One not named is its own codepoint.
    const IndexedText* originals = nullptr;
    size_t original_count = 0;
};

class SelectionModel
{
public:
    // Indexes a shipped command buffer. The buffer is not kept. Calling it
    // again replaces what the model holds and reuses its storage. Any buffer
    // is indexed: coordinates and extents past a quarter of SCALED_LIMIT,
    // which ship-out never writes, are taken as that.
    void build(const DrawCmd* cmds, size_t count);
    void build(const std::vector<DrawCmd>& cmds) { build(cmds.data(), cmds.size()); }

    // One past the last index of any fragment: how many items the reading order
    // has, as far as this model is concerned.
    Boundary length() const { return _length; }

    // The fragments, in reading order (by `start`, and in command order among
    // equal starts).
    size_t fragment_count() const { return _fragments.size(); }
    const Fragment& fragment(size_t ordinal) const { return _fragments[ordinal]; }
    // A fragment's own items: their indices, their kinds, and item_count + 1
    // edges, of which edges[k] is the left edge of item k and the last is the
    // line's right edge.
    const Boundary* indices(const Fragment& f) const { return _indices.data() + f.first_item; }
    const ItemKind* kinds(const Fragment& f) const { return _kinds.data() + f.first_item; }
    const uint32_t* codepoints(const Fragment& f) const
    {
        return _codepoints.data() + f.first_item;
    }
    const Scaled* edges(const Fragment& f) const { return _edges.data() + f.first_edge; }

    // How many clusters the fragments fall into; see COST above. A column of
    // text has as many as it has lines.
    size_t cluster_count() const { return _clusters.size(); }

    // The line a point belongs to, as an ordinal into the fragments, or -1 if
    // there are none. y decides first, then x:
    //   1. among the lines whose row (reach) contains y and whose horizontal
    //      extent contains x, the one whose own band contains y, else the
    //      nearest of them in y;
    //   2. else, among the lines whose band contains y, the nearest in x;
    //   3. else the nearest line in y, x breaking a tie.
    // Ties go to the earlier line in reading order. So a point above the first
    // line belongs to the first and one below the last to the last.
    int fragment_at(Point point) const;

    // The nearest boundary to a point: left of an item's midpoint the boundary
    // before it, otherwise the one after; left of the line its first item's,
    // past its right edge the one after its last. 0 in an empty model.
    Boundary hit(Point point) const;

    // The range between two boundaries given in either order, each clamped into
    // [0, length].
    Selection select(int64_t anchor, int64_t active) const;

    // What a drag between two points selects. At WORD and LINE it is the union
    // of the units under each end, so it never cuts one in half.
    Selection select_at(Point anchor, Point active, Granularity granularity) const;

    // The run of like items under a point: alphanumerics and '_' together,
    // spaces together, anything else together, and an ATOM by itself. Past the
    // end of a line, the last run on it.
    Selection word_at(Point point) const;

    // The whole visual line under a point.
    Selection line_at(Point point) const;

    // Appends one rectangle for each fragment the selection touches, in reading
    // order: from the first selected thing on it that is not fill to the last,
    // at the full height of the line. An empty selection and a blank line give
    // none. Nor does a line whose items' indices do not ascend, where the
    // reference raises: ship-out never makes one.
    void rects(Selection selection, std::vector<Rect>& out) const;

    // Appends the selected text, UTF-8, to `out`. See TEXT above and TextData.
    // A separator is written only between two pieces of selected text. A
    // codepoint that is not a Unicode scalar value is written as U+FFFD.
    void text(Selection selection, const TextData& data, std::string& out) const;
    void text(Selection selection, std::string& out) const { text(selection, TextData(), out); }

private:
    struct Cluster {
        uint32_t first = 0; // fragment ordinal
        uint32_t count = 0;
        Scaled low = 0;  // the least top or reach top of its fragments
        Scaled high = 0; // the greatest bottom or reach bottom
    };

    struct LineInfo {
        uint32_t cmd = 0;
        int32_t outer = -1; // ordinal of the outermost enclosing line
        uint32_t items = 0;
        uint32_t first_item = 0;
        bool has_child = false;
        bool ruled = false;
    };

    Boundary boundary_in(const Fragment& f, Scaled x) const;
    uint32_t offset_in(const Fragment& f, Scaled x) const;
    bool owned_range(const Fragment& f, int64_t first, int64_t last, uint32_t& low,
                     uint32_t& high) const;
    void candidates(Selection selection, size_t& first, size_t& end) const;
    void build_clusters();

    std::vector<Fragment> _fragments;
    std::vector<Boundary> _indices;
    std::vector<ItemKind> _kinds;
    std::vector<uint32_t> _codepoints;
    std::vector<Scaled> _edges;
    std::vector<Cluster> _clusters;
    // The greatest `end` among fragments 0..i, for finding where a range starts.
    std::vector<Boundary> _reach_end;
    Boundary _length = 0;

    // Scratch for build, kept so that rebuilding does not allocate.
    std::vector<LineInfo> _lines;
    std::vector<uint32_t> _stack;
    std::vector<uint32_t> _owner;
    std::vector<uint32_t> _item_cmd;
};

// Whether Python's str.isalnum() and str.isspace() hold for a codepoint: what
// word granularity classifies by. Unicode 14.0.0, the reference's.
bool is_alnum(uint32_t codepoint);
bool is_space(uint32_t codepoint);

} // namespace ImTypeset::detail
