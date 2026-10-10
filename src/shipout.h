// Ship-out: flattening a packed box tree into absolute-positioned draw
// commands. Mirrors advtext/shipout.py, which is tex.web's hlist_out and
// vlist_out (sections 619 to 634) with the DVI output removed: what is left is
// the recursive coordinate walk.
//
// WHAT OF tex.web THIS IS. From part 32, "Shipping pages out" (sections 592
// to 643):
//
//   sections 619-625   hlist_out                        Shipper::hlist_out
//   section 625        the setting of one glue node;    set_glue_width,
//                      billion, vet_glue                BILLION, vet_glue
//   sections 629-634   vlist_out                        Shipper::vlist_out
//   sections 638, 640  ship_out: the walk is started    ship
//                      at the page's top-left corner
//
// `ship` is tex.web's ship_out with a shorter name, the reference's. cur_h,
// cur_v, cur_g, cur_glue, left_edge and base_line are tex.web's names for the
// same things.
//
// This is the boundary of the layout core. Above it everything is a tree in
// layout coordinates; below it everything is one flat buffer of trivially
// copyable commands that a backend walks without understanding. The draw loop
// reads the buffer every frame and hit-testing indexes it, so a command is a
// fixed 64 bytes and the buffer is contiguous.
//
// COORDINATES are absolute integer scaled points, x rightward and y DOWNWARD,
// which is tex.web's own convention (cur_v grows down the page). `origin` is the
// root box's reference point: on its baseline, at its left edge. So
// ship(..., Point{0, box.height}, ...) puts the box's top-left corner at the
// coordinate origin, which is where section 640 starts a page (cur_v at the
// box's height) before its offsets are added.
//
// Nothing here rounds to pixels. The coordinates are final and absolute, so a
// backend rounds each one once as it draws and nothing accumulates.
//
// GLUE (section 625) is not set node by node. A running total of the stretch
// (or shrink) seen so far is multiplied by the box's glue ratio, the *total* is
// rounded, and a glue node's width is its natural width plus the difference
// between this rounded total and the previous one. The rounding error therefore
// never accumulates, and after the last glue of a line the set glue adds up to
// exactly the excess the box was packed to absorb. The rounding is Pascal's,
// ties away from zero (pascal_round), not the half-to-even of units.h.
//
// READING ORDER is a contract with the selection code. Every command a reader
// can point at carries an index; indices start at 0 for each call of ship,
// increase strictly, and follow the order of traversal. Three things take one:
//
//   - a CHAR node, which becomes a GLYPH;
//   - a GLUE node in a horizontal list, which becomes a SET_GLUE: the interword
//     space a reader expects to select across;
//   - an atom box in a horizontal list (is_atom: an hbox whose contents open
//     with MATH_ON and close with MATH_OFF), which becomes one ATOM and takes
//     one index however much is inside. This is an inline formula. The glyphs
//     inside are shipped after the ATOM and carry its index; glue inside takes
//     none and emits nothing; and the hboxes of a vlist inside are not lines.
//     An atom box inside an atom is an ordinary box.
//
// Nothing else takes an index: vertical glue, kerns, penalties,
// discretionaries, rules.
//
// An index is a position in the shipped sequence, not an offset into any
// source text: the line breaker has discarded glue and added skips before
// ship-out sees a list.
//
// LINES. An hbox that is an item of a vertical list is a line and is announced
// by a LINE command placed immediately before the commands it covers, holding
// the half-open range of indices it contains. A bare hbox handed to ship counts
// as one line. LINE's height and depth are the box's own; giving every line of
// a block the same extent is the strut's job, in the style layer.
//
// ERRORS. Where the reference raises, ship returns a ShipError and leaves the
// output buffer as it found it: a list reaches here from code that built it
// and, through the public API, from a host that built it by hand, so a
// malformed list is data.
//
// WHERE THIS IS NOT tex.web. The reference's departures: no leaders (sections
// 626 to 628 and 635 to 637); none of the DVI machinery (push, pop,
// prune_movements, synch_h, synch_v, font changes); no whatsits, inserts or
// marks; no page offset (section 640's h_offset and v_offset: the caller places
// the page with `origin`); a box with nothing in it is walked like any other,
// where sections 623 and 632 step over it. The index, the LINE and ATOM
// commands and SET_GLUE are not in tex.web at all, which emits nothing for
// glue. A list tex.web would stop at with confusion("vlistout") (section 630),
// or ship with a null_flag for a length, is an error here.
//
// This port's own (decision D33): every position is a checked sum, the walk
// is bounded by `max_items` and by NodeArena::MAX_NESTING, and a failed ship
// leaves the output as it was. tex.web bounds none of these, and what it
// refuses is a page larger than max_dimen (section 641).

#pragma once

#include "nodes.h"
#include "pack.h"
#include "units.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace ImTypeset::detail
{

// tex.web's billion (section 625), the bound of its vet_glue: a glue product
// is clamped to plus or minus this before it is rounded, so that a wildly
// stretched infinite glue cannot round to something that overflows.
constexpr double BILLION = 1000000000.0;

// Pascal's round: to nearest, ties away from zero. Section 625 rounds the
// accumulated glue product this way, and the product lands on an exact half
// routinely, glue_set being a ratio of two integers. Computed as the reference
// computes it -- floor(v + 0.5) for v >= 0, ceil(v - 0.5) otherwise -- so that
// 0.49999999999999994 gives 1, as it does there.
//
// The reference raises for NaN and infinities. Here NaN gives 0 and a result
// beyond +-SCALED_LIMIT gives that limit; ship itself never passes either,
// because vet_glue has clamped the value first.
inline Scaled pascal_round(double value)
{
    if (value != value)
        return 0;
    const double rounded = value >= 0.0 ? std::floor(value + 0.5) : std::ceil(value - 0.5);
    const double limit = static_cast<double>(SCALED_LIMIT);
    if (rounded >= limit)
        return SCALED_LIMIT;
    if (rounded <= -limit)
        return -SCALED_LIMIT;
    return static_cast<Scaled>(rounded);
}

// Section 625's vet_glue, with the reference's max(-BILLION, min(BILLION, x)):
// each comparison is written so that NaN comes out as +BILLION, as it does
// there.
inline double vet_glue(double product)
{
    const double capped = product < BILLION ? product : BILLION;
    return capped > -BILLION ? capped : -BILLION;
}

// An absolute position in scaled points, x rightward and y downward.
struct Point {
    Scaled x = 0;
    Scaled y = 0;
};

// A device rectangle in the same scaled points: top-left anchored, covering
// [x, x + width) by [y, y + height). What a highlight or a fill is given in;
// here so that the draw and selection code share it.
struct Rect {
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled height = 0;

    friend constexpr bool operator==(const Rect& a, const Rect& b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend constexpr bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
};

// What a command is. ATOM has SET_GLUE's fields exactly (in the reference it is
// a subclass that adds none) and is a tag of its own so that a consumer can
// tell one pointable box from a space without a second table.
enum class CmdType : uint8_t {
    GLYPH = 0,
    RULE_RECT,
    SET_GLUE,
    ATOM,
    LINE,
};

// One ship-out command. Which fields mean something follows the type:
//
//   GLYPH      font(), codepoint, x, y, index. One character, placed. x is the
//              left edge of its advance and y is ON THE BASELINE, not the top
//              of the glyph.
//   RULE_RECT  x, y, width, height. A filled rectangle; an ordinary top-left
//              one, covering [y, y + height), since a rule has no baseline.
//              Not pointable: no index.
//   SET_GLUE   x, y, width, natural(), height, depth, index. A horizontal glue
//              at the width this packing gave it; natural() is what its spec
//              asked for. y, height and depth are the enclosing box's, so its
//              rectangle is the full line height: [y - height, y + depth).
//   ATOM       the same fields. A box that is one pointable item, with nothing
//              of its own to draw; width and natural() are both the box's
//              width. Its ink follows as ordinary commands whose glyphs carry
//              this index.
//   LINE       x, y, width, height, depth, start(), end. An hbox that was an
//              item of a vertical list. y is its baseline; [start(), end) is
//              the range of indices it contains, empty when it holds nothing
//              pointable.
//
// Fields a type does not use are zero, so two commands are the same command
// exactly when every field is equal.
struct DrawCmd {
    CmdType type = CmdType::GLYPH;
    uint32_t codepoint = 0;
    Scaled x = 0;
    Scaled y = 0;
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    // GLYPH: the font key's bits. SET_GLUE, ATOM: the natural width.
    Scaled a = 0;
    // GLYPH, SET_GLUE, ATOM: the reading index. LINE: the first index it holds.
    uint32_t index = 0;
    // LINE: one past the last index it holds.
    uint32_t end = 0;

    bool is(CmdType t) const { return type == t; }

    FontKey font() const
    {
        IM_ASSERT(type == CmdType::GLYPH);
        return FontKey{static_cast<uint64_t>(a)};
    }
    Scaled natural() const
    {
        IM_ASSERT(type == CmdType::SET_GLUE || type == CmdType::ATOM);
        return a;
    }
    uint32_t start() const
    {
        IM_ASSERT(type == CmdType::LINE);
        return index;
    }
    // Whether this is a SET_GLUE or an ATOM: the reference's
    // isinstance(cmd, SetGlue), which is true of both.
    bool is_set_glue() const { return type == CmdType::SET_GLUE || type == CmdType::ATOM; }

    static DrawCmd glyph(FontKey font, uint32_t codepoint, Scaled x, Scaled y, uint32_t index)
    {
        DrawCmd cmd;
        cmd.type = CmdType::GLYPH;
        cmd.codepoint = codepoint;
        cmd.x = x;
        cmd.y = y;
        cmd.a = static_cast<Scaled>(font.bits);
        cmd.index = index;
        return cmd;
    }
    static DrawCmd rule_rect(Scaled x, Scaled y, Scaled width, Scaled height)
    {
        DrawCmd cmd;
        cmd.type = CmdType::RULE_RECT;
        cmd.x = x;
        cmd.y = y;
        cmd.width = width;
        cmd.height = height;
        return cmd;
    }
    static DrawCmd set_glue(Scaled x, Scaled y, Scaled width, Scaled natural, Scaled height,
                            Scaled depth, uint32_t index)
    {
        DrawCmd cmd;
        cmd.type = CmdType::SET_GLUE;
        cmd.x = x;
        cmd.y = y;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        cmd.a = natural;
        cmd.index = index;
        return cmd;
    }
    static DrawCmd atom(Scaled x, Scaled y, Scaled width, Scaled natural, Scaled height,
                        Scaled depth, uint32_t index)
    {
        DrawCmd cmd = set_glue(x, y, width, natural, height, depth, index);
        cmd.type = CmdType::ATOM;
        return cmd;
    }
    static DrawCmd line(Scaled x, Scaled y, Scaled width, Scaled height, Scaled depth,
                        uint32_t start, uint32_t end)
    {
        DrawCmd cmd;
        cmd.type = CmdType::LINE;
        cmd.x = x;
        cmd.y = y;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        cmd.index = start;
        cmd.end = end;
        return cmd;
    }

    friend constexpr bool operator==(const DrawCmd& p, const DrawCmd& q)
    {
        return p.type == q.type && p.codepoint == q.codepoint && p.x == q.x && p.y == q.y &&
               p.width == q.width && p.height == q.height && p.depth == q.depth && p.a == q.a &&
               p.index == q.index && p.end == q.end;
    }
    friend constexpr bool operator!=(const DrawCmd& p, const DrawCmd& q) { return !(p == q); }
};

static_assert(sizeof(DrawCmd) == 64, "a command is one cache line; see the top of this file");
static_assert(std::is_trivially_copyable<DrawCmd>::value, "commands are copied as bytes");

// Why ship produced nothing. The first four are the reference's ValueErrors.
enum class ShipError : uint8_t {
    NONE = 0,
    // The tree holds a CHAR and no measure was given: a character carries no
    // advance of its own.
    CHAR_WITHOUT_MEASURE,
    // A rule's width is RUNNING inside an hlist. tex.web never makes such a
    // rule (section 138) and section 624 does not look.
    RUNNING_WIDTH_IN_HLIST,
    // A CHAR is an item of a vertical list, where section 630 stops with
    // confusion.
    CHAR_IN_VLIST,
    // A rule's height or depth is RUNNING inside a vlist.
    RUNNING_HEIGHT_OR_DEPTH_IN_VLIST,
    // The walk visited more nodes than `max_items`. With the default this needs
    // a tree whose boxes share contents; the reference would carry on until it
    // ran out of memory.
    TOO_MANY_ITEMS,
    // A BOX node names no box of the arena, in the tree or as its root.
    NODE_NOT_IN_ARENA,
    // The node given as the root is not a BOX.
    ROOT_NOT_A_BOX,
    // Boxes nest deeper than NodeArena::MAX_NESTING, which an arena that was
    // only added to never holds: the tree has a cycle or was assembled around
    // the arena.
    TOO_DEEP,
    // A position left the range of a Scaled, which nothing built within
    // MAX_DIMEN can do (src/units.h).
    DIMENSION_OVERFLOW,
};

// As `max_items`, "in proportion to what the arena and the list hold", which is
// the default: DEFAULT_SHIP_VISITS_PER_NODE visits for each of their nodes and
// DEFAULT_SHIP_VISITS_FLOOR more. A tree in which no box is named twice needs
// one visit per node; the allowance is for a box set several times over (the
// same formula on every line), and it is what keeps a tree that shares at
// every level from being walked path by path.
constexpr uint32_t MAX_SHIP_ITEMS = 0xFFFFFFFFu;
constexpr uint64_t DEFAULT_SHIP_VISITS_PER_NODE = 16;
constexpr uint64_t DEFAULT_SHIP_VISITS_FLOOR = 65536;

// Whether a box ships as one pointable item: an hbox whose contents are at
// least two nodes, the first a MATH_ON and the last a MATH_OFF. The bracket is
// inside the box; tex.web puts the pair in the paragraph's own list around an
// inline formula (section 147), where the line breaker may discard a MATH_ON
// that starts a line (section 879), so one inside the box is what survives to
// ship-out.
bool is_atom(const NodeArena& arena, const Box& box);

// `box` (a BOX node of `arena`) wrapped so that it ships as one item: an hbox
// holding MATH_ON, the box, MATH_OFF, with the box's width and with a height
// and depth that cover the box where its shift_amount puts it. It packs and
// breaks exactly as the box did. Returns the BOX node of the wrapper.
Node atom_box(NodeArena& arena, const Node& box);

// Flattens a packed box into absolute-positioned commands, appended to `out`
// in reading order with each LINE immediately before what it covers.
//
// `box` gives the kind, the dimensions and the glue setting, and `contents` the
// nodes: a PackResult's box and the list that was packed, as they stand, or
// the Box and contents of a BOX node. BOX and DISC nodes in `contents` are
// `arena`'s. `origin` is the box's reference point.
//
// `measure` must be the CharMeasure the tree was packed with; it is needed only
// if the tree holds a CHAR.
//
// Indices start at 0 whatever `out` already holds. On an error `out` is left
// at the size it had on entry and nothing of this box is in it.
//
// `max_items` bounds the work: the walk visits each node of the tree once per
// path to it, and gives up with TOO_MANY_ITEMS after that many. A tree in which
// no box is named twice has at most as many as the arena and `contents` have
// nodes. Boxes that share contents can make it exponentially many -- twenty
// levels of a box named twice are a million visits from forty nodes -- so the
// default (MAX_SHIP_ITEMS) is proportional to the node count, and a caller
// whose tree shares more heavily than that on purpose says how many visits it
// expects, and gets at most that many commands.
//
// Recursion is as deep as the boxes nest and stops with TOO_DEEP past
// NodeArena::MAX_NESTING.
ShipError ship(const NodeArena& arena, const Box& box, NodeView contents, Point origin,
               CharMeasure measure, std::vector<DrawCmd>& out, uint32_t max_items = MAX_SHIP_ITEMS);

// The same for a BOX node of `arena`.
ShipError ship(const NodeArena& arena, const Node& box, Point origin, CharMeasure measure,
               std::vector<DrawCmd>& out, uint32_t max_items = MAX_SHIP_ITEMS);

} // namespace ImTypeset::detail
