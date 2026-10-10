// The node vocabulary: characters, boxes, rules, glue, kerns, penalties,
// discretionaries and math brackets. Mirrors advtext/nodes.py. Nothing here
// lays anything out; hpack, the line breaker and ship-out are written against
// these types.
//
// WHAT OF tex.web THIS IS. Part 10, "Data structures for boxes and their
// friends" (sections 133 to 161), less the kinds listed under NOT HERE:
//
//   section 134        char_node                        CHAR, char_node()
//   sections 135, 137  hlist_node, vlist_node and the   BOX, Box, BoxKind,
//                      fields of a box; glue_sign's     GlueSign
//                      normal, stretching, shrinking
//   section 138        rule_node, null_flag,            RULE, rule(), RUNNING,
//                      is_running                       is_running()
//   section 145        disc_node: pre_break,            DISC, Disc
//                      post_break, replace_count
//   section 147        math_node, subtypes before       MATH_ON, MATH_OFF
//                      and after
//   section 148        precedes_break,                  is_discardable(),
//                      non_discardable                  permits_break_at_...
//   sections 149, 150  glue_node and the glue           GLUE, glue(), GlueSpec,
//                      specification                    GlueOrder (units.h)
//   section 155        kern_node, subtypes normal,      KERN, kern(), KernKind
//                      explicit, acc_kern
//   section 157        penalty_node, inf_penalty,       PENALTY, penalty(),
//                      eject_penalty                    INF_PENALTY, EJECT_...
//
// and, from part 33, section 646's total_stretch and total_shrink (GlueTotal).
//
// tex.web's nodes are variable-size records in one array, mem, linked by
// pointers, and a glue node points at a reference-counted specification that
// many nodes share (section 150). Here a list is contiguous, a node is a
// value, and a glue node holds its specification: there is no zero_glue to be
// identical to, only a spec that is equal to another.
//
// Height and depth are measured from the baseline, not from a bounding box. A
// box's reference point is on the baseline at its left edge; height extends
// upward and depth downward. shift_amount is positive downward in an hlist and
// rightward in a vlist.
//
// REPRESENTATION
//
// The reference has nine frozen classes and tuples of them. Here a node is one
// 32-byte trivially copyable value, `Node`, with a type tag, and a list is a
// contiguous run of them: a `std::vector<Node>` being built, or a `NodeView`
// over storage somebody else owns. The line breaker scans one flat list many
// times and that scan touches two nodes per cache line and nothing else.
//
// Seven of the nine kinds are complete in the Node itself. Glue carries its
// whole spec inline, so there is no spec table to chase and "the same spec"
// is equality of values, as it is in the reference. Two kinds have contents and
// keep them in a `NodeArena`:
//
//   - A BOX node holds an index into the arena's boxes; the `Box` there has the
//     dimensions, the glue setting and a `NodeSpan` naming its contents, which
//     are a contiguous run of the arena's nodes.
//   - A DISC node holds an index into the arena's discretionaries; the `Disc`
//     has two spans (pre-break, post-break) and the replace count.
//
// Everything that refers into an arena is a 32-bit index, never a pointer, so
// an arena can grow while it is referred to and a Node can be copied anywhere.
// What a BOX or DISC node means depends on the arena it was made in: nodes of
// two arenas are not mixed in one list. `NodeArena::import` copies a node, and
// what it contains, from another arena.
//
// A box is immutable in the reference and a packing returns a new one. Here
// `add_box` appends a Box and returns the node for it; changing a field of an
// existing Box changes it for every node that names it, so code that needs
// "the same box, shifted" adds a copy of the Box (the contents span is shared,
// which is the sharing the reference's tuples have).
//
// To build a list whose items are themselves boxes: build the inner list in a
// scratch vector, `add_box(header, view)` it (the contents are copied into the
// arena, contiguously), and push the returned node onto the outer scratch
// vector. Only box and disc contents ever need to live in the arena; a top-level
// list can stay in its vector.
//
// Nesting is bounded where it is created. An arena knows how deep each box and
// discretionary nests and refuses to make one deeper than MAX_NESTING. A box or
// discretionary can only name contents that existed before it: a BOX or DISC
// node among its contents whose index the arena does not hold yet is a
// malformed list, and it is stored with no contents. So an arena that was only
// ever added to has no cycles and its recursive walks are no deeper than
// MAX_NESTING. A hand-made Mark can break that (see rewind), which is why
// ship-out, import and deep_equal count their own depth as well.
//
// The same box may be named by many nodes, and a tree that does that at every
// level has exponentially many paths from few nodes. import and deep_equal
// visit each box once however many paths lead to it; ship-out has to emit
// every path and is bounded by its max_items.
//
// Lifetimes: a NodeSpan stays valid for the life of the arena (until `clear` or
// a `rewind` past it). A NodeView or a `const Box&` obtained from the arena is
// invalidated by the next call that adds to it.
//
// FONT KEYS
//
// The reference's CharNode names its font by the string "face@size" (for
// example "prose-bold@789381": a face name and an em size in scaled points) and
// the layout core never looks inside it. Here the key is a 64-bit value,
// `FontKey`, holding a 15-bit face id and a 48-bit size. No table is needed to
// make one or to read one back, two keys are equal exactly when the strings
// would be, and the draw loop gets the face and the size without parsing.
// Which face an id means is the font registry's business (src/fontlib); the
// core only carries the number. Nothing in the core orders by key.
//
// REPLACED CHARACTERS
//
// A character no face of the theme has is set as a stand-in (U+FFFD) and the
// reference remembers what was written in a CharNode subclass, ReplacedChar,
// so that copy and find return the original. Here every CHAR node has an
// `original` codepoint and a flag saying whether it differs in kind from an
// ordinary character; measuring, packing, breaking and shipping read only
// `font` and `codepoint`.
//
// NOT HERE
//
// Ligature (section 143), insert (140), mark (141), adjust (142), whatsit
// (146) and unset (159) nodes, leaders (a glue node's leader_ptr, section
// 149), and metrics on a character node: a CHAR names a font and a codepoint
// and its dimensions come from the metrics interface at pack time, where
// tex.web reads them from the font's char_info.

#pragma once

#include "badness.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// A penalty that forbids a break: tex.web's inf_penalty (section 157), which
// that section defines as inf_bad. Penalties and badness meet in the demerits
// formula (section 859).
constexpr Scaled INF_PENALTY = INF_BAD;

// A penalty that forces a break: tex.web's eject_penalty (section 157).
constexpr Scaled EJECT_PENALTY = -INF_PENALTY;

// The kinds of node. The order is that of tex.web's type codes (sections 135
// to 157) with the kinds this port lacks left out: everything before MATH_ON
// is non-discardable and may precede a glue breakpoint (section 148),
// everything from MATH_ON on is discardable.
//
// Names that differ from tex.web's: BOX is hlist_node and vlist_node together
// (BoxKind tells them apart); MATH_ON and MATH_OFF are one type there,
// math_node, with the subtypes before and after (section 147); CHAR is not a
// type code there at all, a char_node being recognised by its address
// (is_char_node, section 134).
enum class NodeType : uint8_t {
    CHAR = 0,
    BOX,
    RULE,
    DISC,
    MATH_ON,
    MATH_OFF,
    GLUE,
    KERN,
    PENALTY,
};

// Whether a box's contents run horizontally or vertically: tex.web's
// hlist_node and vlist_node. One Box type with a kind, as tex.web has one
// seven-word layout under two type codes (sections 135 and 137).
enum class BoxKind : uint8_t { HLIST = 0, VLIST };

// Which direction a packed box's glue was set in: tex.web's glue_sign, with
// its values normal, stretching and shrinking (section 135). glue_set is
// stored positive for shrink as well as stretch; the direction lives here and
// nowhere else.
enum class GlueSign : uint8_t { NORMAL = 0, STRETCHING, SHRINKING };

// Where a kern came from: tex.web's kern subtypes normal, explicit and
// acc_kern (section 155), ACCENT being the last. A break at the kern is legal
// only for EXPLICIT with glue following (section 866's kern_break); a break at
// glue after the kern is legal only when the kern is not EXPLICIT (section
// 868).
enum class KernKind : uint8_t { NORMAL = 0, EXPLICIT, ACCENT };

// A face at a size: what the reference spells "face@size". See FONT KEYS above.
struct FontKey {
    static constexpr int FACE_BITS = 15;
    static constexpr int SIZE_BITS = 48;
    static constexpr uint32_t MAX_FACE = (uint32_t(1) << FACE_BITS) - 1;
    static constexpr Scaled MAX_SIZE = (Scaled(1) << SIZE_BITS) - 1;

    // Bit 63 is always clear, so the value also fits a Scaled.
    uint64_t bits = 0;

    // A face id above MAX_FACE or a size outside [0, MAX_SIZE] is a contract
    // violation; the value is clamped into range.
    static FontKey make(uint32_t face, Scaled size)
    {
        if (!IMTYPESET_CONTRACT(face <= MAX_FACE, "FontKey: face id out of range"))
            face = MAX_FACE;
        if (!IMTYPESET_CONTRACT(size >= 0 && size <= MAX_SIZE, "FontKey: size out of range"))
            size = size < 0 ? 0 : MAX_SIZE;
        return FontKey{(static_cast<uint64_t>(face) << SIZE_BITS) | static_cast<uint64_t>(size)};
    }

    constexpr uint32_t face() const { return static_cast<uint32_t>(bits >> SIZE_BITS); }
    constexpr Scaled size() const
    {
        return static_cast<Scaled>(bits & static_cast<uint64_t>(MAX_SIZE));
    }

    friend constexpr bool operator==(FontKey a, FontKey b) { return a.bits == b.bits; }
    friend constexpr bool operator!=(FontKey a, FontKey b) { return a.bits != b.bits; }
};

// A glue specification (tex.web section 150), under its field names: width,
// stretch, shrink, stretch_order, shrink_order. stretch and shrink are
// magnitudes; shrinking is `shrink` with a SHRINKING glue sign, not a negative
// stretch. Field order is the reference's. A value here and a shared,
// reference-counted record there.
struct GlueSpec {
    Scaled width = 0;
    Scaled stretch = 0;
    GlueOrder stretch_order = GlueOrder::NORMAL;
    Scaled shrink = 0;
    GlueOrder shrink_order = GlueOrder::NORMAL;

    friend constexpr bool operator==(const GlueSpec& a, const GlueSpec& b)
    {
        return a.width == b.width && a.stretch == b.stretch && a.stretch_order == b.stretch_order &&
               a.shrink == b.shrink && a.shrink_order == b.shrink_order;
    }
    friend constexpr bool operator!=(const GlueSpec& a, const GlueSpec& b) { return !(a == b); }
};

// Stretch or shrink accumulated across a list, kept separately per order of
// infinity: one of tex.web's two arrays total_stretch and total_shrink
// (section 646). Orders never add to one another while accumulating. Section
// 646 gives the case that shows why: a list whose fill components cancel has
// to be set by its fil, which is lost if a lower order is dropped the moment
// a higher one appears. The winner is chosen once, by dominant_order(), at the
// end (sections 659 and 665).
struct GlueTotal {
    Scaled amounts[4] = {0, 0, 0, 0};

    // An order comes from a node somebody built and is masked into range
    // before it indexes. A total that leaves the range of a Scaled stops at the
    // end of it; the answer is whether it stayed inside.
    constexpr bool add(Scaled amount, GlueOrder order)
    {
        bool overflowed = false;
        Scaled& total = amounts[static_cast<unsigned>(order) & 3u];
        total = add_checked(total, amount, overflowed);
        return !overflowed;
    }
    constexpr GlueTotal& operator+=(const GlueTotal& other)
    {
        for (int i = 0; i < 4; ++i)
            amounts[i] = add_saturating(amounts[i], other.amounts[i]);
        return *this;
    }
    constexpr Scaled operator[](GlueOrder order) const
    {
        return amounts[static_cast<unsigned>(order) & 3u];
    }

    // The highest order with a nonzero amount, or NORMAL if every order is
    // zero: a total that cancels everywhere makes the box unsettable in that
    // direction, which is the correct reading and not an error.
    constexpr GlueOrder dominant_order() const
    {
        if (amounts[3] != 0)
            return GlueOrder::FILLL;
        if (amounts[2] != 0)
            return GlueOrder::FILL;
        if (amounts[1] != 0)
            return GlueOrder::FIL;
        return GlueOrder::NORMAL;
    }

    // The amount at dominant_order(): the only stretchability that counts.
    constexpr Scaled dominant() const { return (*this)[dominant_order()]; }

    friend constexpr bool operator==(const GlueTotal& a, const GlueTotal& b)
    {
        return a.amounts[0] == b.amounts[0] && a.amounts[1] == b.amounts[1] &&
               a.amounts[2] == b.amounts[2] && a.amounts[3] == b.amounts[3];
    }
};

// One item of a horizontal or vertical list. Which fields mean what depends on
// `type`; the named constructors below and the accessors are the vocabulary,
// and every field a kind does not use is zero, so two nodes of the same kind
// are equal exactly when all fields are.
//
//   type      u            a          b            c           kind / orders
//   CHAR      codepoint    font bits  original     -           kind: 1 if replaced
//   BOX       box index    -          -            -
//   RULE      -            width      height       depth
//   DISC      disc index   -          -            -
//   MATH_ON   -            width      -            -
//   MATH_OFF  -            width      -            -
//   GLUE      -            width      stretch      shrink      both orders
//   KERN      -            width      -            -           kind: KernKind
//   PENALTY   -            value      -            -
//
// So `a` is the amount a horizontal list advances by for RULE, MATH_ON,
// MATH_OFF, GLUE (naturally) and KERN.
struct Node {
    NodeType type = NodeType::PENALTY;
    uint8_t kind = 0;
    GlueOrder stretch_order = GlueOrder::NORMAL;
    GlueOrder shrink_order = GlueOrder::NORMAL;
    uint32_t u = 0;
    Scaled a = 0;
    Scaled b = 0;
    Scaled c = 0;

    bool is(NodeType t) const { return type == t; }

    // The accessors assert the kind they are for: read on another kind they
    // would answer a field that means something else, silently.

    // --- CHAR
    FontKey font() const
    {
        IM_ASSERT(type == NodeType::CHAR);
        return FontKey{static_cast<uint64_t>(a)};
    }
    uint32_t codepoint() const
    {
        IM_ASSERT(type == NodeType::CHAR);
        return u;
    }
    // What was written. Equal to codepoint() unless is_replaced().
    uint32_t original() const
    {
        IM_ASSERT(type == NodeType::CHAR);
        return static_cast<uint32_t>(b);
    }
    bool is_replaced() const { return type == NodeType::CHAR && kind != 0; }

    // --- BOX, DISC: an index into the arena the node was made in
    uint32_t index() const
    {
        IM_ASSERT(type == NodeType::BOX || type == NodeType::DISC);
        return u;
    }

    // --- RULE, MATH_ON, MATH_OFF, GLUE (natural), KERN. Not BOX: a box's width
    // is in the arena.
    Scaled width() const
    {
        IM_ASSERT(type == NodeType::RULE || type == NodeType::MATH_ON ||
                  type == NodeType::MATH_OFF || type == NodeType::GLUE || type == NodeType::KERN);
        return a;
    }

    // --- RULE. Any of the three may be RUNNING.
    Scaled height() const
    {
        IM_ASSERT(type == NodeType::RULE);
        return b;
    }
    Scaled depth() const
    {
        IM_ASSERT(type == NodeType::RULE);
        return c;
    }

    // --- GLUE
    Scaled stretch() const
    {
        IM_ASSERT(type == NodeType::GLUE);
        return b;
    }
    Scaled shrink() const
    {
        IM_ASSERT(type == NodeType::GLUE);
        return c;
    }
    GlueSpec spec() const
    {
        IM_ASSERT(type == NodeType::GLUE);
        return GlueSpec{a, b, stretch_order, c, shrink_order};
    }

    // --- KERN
    KernKind kern_kind() const
    {
        IM_ASSERT(type == NodeType::KERN);
        return static_cast<KernKind>(kind);
    }

    // --- PENALTY
    Scaled penalty() const
    {
        IM_ASSERT(type == NodeType::PENALTY);
        return a;
    }

    friend constexpr bool operator==(const Node& x, const Node& y)
    {
        return x.type == y.type && x.kind == y.kind && x.stretch_order == y.stretch_order &&
               x.shrink_order == y.shrink_order && x.u == y.u && x.a == y.a && x.b == y.b &&
               x.c == y.c;
    }
    friend constexpr bool operator!=(const Node& x, const Node& y) { return !(x == y); }
};

static_assert(sizeof(Node) == 32, "Node is two to a cache line; see REPRESENTATION");

// One glyph, named and not measured: tex.web's char_node (section 134), whose
// two fields are font and character.
inline Node char_node(FontKey font, uint32_t codepoint)
{
    Node n;
    n.type = NodeType::CHAR;
    n.u = codepoint;
    n.a = static_cast<Scaled>(font.bits);
    n.b = static_cast<Scaled>(codepoint);
    return n;
}

// A character standing in for `original`, which no face has: `font` and
// `codepoint` are what is set. Never equal to the char_node with the same font
// and codepoint, as the reference's ReplacedChar is not equal to its CharNode.
inline Node replaced_char(FontKey font, uint32_t codepoint, uint32_t original)
{
    Node n = char_node(font, codepoint);
    n.kind = 1;
    n.b = static_cast<Scaled>(original);
    return n;
}

// A solid rectangle: tex.web's rule_node (section 138). The defaults are
// new_rule's (section 139), every dimension running. Width is never RUNNING in
// an hlist; height and depth are never RUNNING in a vlist.
inline Node rule(Scaled width = RUNNING, Scaled height = RUNNING, Scaled depth = RUNNING)
{
    Node n;
    n.type = NodeType::RULE;
    n.a = width;
    n.b = height;
    n.c = depth;
    return n;
}

// Stretchable, shrinkable, discardable space: tex.web's glue_node (section
// 149), as new_glue makes one (section 153). No leaders and no subtype.
inline Node glue(const GlueSpec& spec)
{
    Node n;
    n.type = NodeType::GLUE;
    n.a = spec.width;
    n.b = spec.stretch;
    n.c = spec.shrink;
    n.stretch_order = spec.stretch_order;
    n.shrink_order = spec.shrink_order;
    return n;
}

// Fixed space: tex.web's kern_node (section 155), as new_kern makes one
// (section 156).
inline Node kern(Scaled width = 0, KernKind kind = KernKind::NORMAL)
{
    Node n;
    n.type = NodeType::KERN;
    n.kind = static_cast<uint8_t>(kind);
    n.a = width;
    return n;
}

// The cost of breaking here: tex.web's penalty_node (section 157), as
// new_penalty makes one (section 158).
inline Node penalty(Scaled value = 0)
{
    Node n;
    n.type = NodeType::PENALTY;
    n.a = value;
    return n;
}

// The start and end of an inline formula: tex.web's math_node with subtype
// before (MATH_ON) and after (MATH_OFF), as new_math makes one (section 147).
// `width` is the math surround.
inline Node math_on(Scaled width = 0)
{
    Node n;
    n.type = NodeType::MATH_ON;
    n.a = width;
    return n;
}

inline Node math_off(Scaled width = 0)
{
    Node n;
    n.type = NodeType::MATH_OFF;
    n.a = width;
    return n;
}

// Whether a penalty value makes a break illegal, or compels one: the two
// halves of the test try_break opens with (tex.web section 831).
constexpr bool forbids_break(Scaled penalty_value)
{
    return penalty_value >= INF_PENALTY;
}
constexpr bool forces_break(Scaled penalty_value)
{
    return penalty_value <= EJECT_PENALTY;
}

// Whether any dimension of a RULE node is still to be resolved against an
// enclosing box. tex.web's is_running (section 138) asks it of one dimension.
inline bool is_running(const Node& rule_node)
{
    return rule_node.a == RUNNING || rule_node.b == RUNNING || rule_node.c == RUNNING;
}

// tex.web's non_discardable (section 148), negated: math nodes, glue, kerns
// and penalties. This is not the breakpoint rule; see
// permits_break_at_following_glue. Nor is it all of what is dropped from the
// start of a line: section 879 keeps a kern that is not explicit, and the line
// breaker has its own test for that.
constexpr bool is_discardable(const Node& node)
{
    return node.type >= NodeType::MATH_ON;
}

// Whether a glue node immediately after `node` is a legal breakpoint (tex.web
// section 868, whose three tests are is_char_node, section 148's
// precedes_break, and a kern that is not explicit): after a character, box,
// rule or discretionary, and after a kern that is not EXPLICIT. The kern
// branch is what makes this wider than !is_discardable: a font kern is
// discardable and still permits the break. Math nodes fail every branch; the
// break a formula offers is taken at its MATH_OFF. The line breaker's
// auto_breaking flag is the caller's.
constexpr bool permits_break_at_following_glue(const Node& node)
{
    if (node.type == NodeType::KERN)
        return static_cast<KernKind>(node.kind) != KernKind::EXPLICIT;
    return node.type < NodeType::MATH_ON;
}

// A run of an arena's nodes.
struct NodeSpan {
    uint32_t first = 0;
    uint32_t count = 0;

    friend constexpr bool operator==(NodeSpan a, NodeSpan b)
    {
        return a.first == b.first && a.count == b.count;
    }
};

// A list somebody else owns: a vector, part of one, or a span of an arena.
class NodeView
{
public:
    constexpr NodeView() = default;
    constexpr NodeView(const Node* data, size_t count)
        : _data(data)
        , _count(count)
    {
    }
    NodeView(const std::vector<Node>& nodes)
        : _data(nodes.data())
        , _count(nodes.size())
    {
    }

    constexpr const Node* data() const { return _data; }
    constexpr size_t size() const { return _count; }
    constexpr bool empty() const { return _count == 0; }
    constexpr const Node* begin() const { return _data; }
    constexpr const Node* end() const { return _data + _count; }
    constexpr const Node& operator[](size_t i) const { return _data[i]; }

    // The nodes [first, first + count), clipped to the view.
    constexpr NodeView sub(size_t first, size_t count) const
    {
        if (first > _count)
            first = _count;
        if (count > _count - first)
            count = _count - first;
        return NodeView(_data + first, count);
    }

private:
    const Node* _data = nullptr;
    size_t _count = 0;
};

// A packed list with settled dimensions: tex.web's hlist_node or vlist_node
// (sections 135 and 137), under its field names: width, height, depth,
// shift_amount, glue_set, glue_sign, glue_order; `contents` is its list_ptr.
// glue_set is the one float in the scaled-integer system (a glue_ratio,
// section 109): a ratio, stored positive, applied at ship-out to the glue
// whose order is glue_order.
struct Box {
    BoxKind kind = BoxKind::HLIST;
    GlueSign glue_sign = GlueSign::NORMAL;
    GlueOrder glue_order = GlueOrder::NORMAL;
    NodeSpan contents;
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
    Scaled shift_amount = 0;
    double glue_set = 0.0;
};

// A discretionary break: tex.web's disc_node (section 145), under its field
// names. pre_break ends the line, post_break starts the next, and
// replace_count nodes following the DISC node are what those two replace when
// the break is taken. An empty pre_break marks an explicit opportunity,
// charged the explicit-hyphen penalty (section 869).
//
// replace_count is carried as given. It comes from whoever built the list, so
// the line breaker bounds it against the list before using it.
struct Disc {
    NodeSpan pre_break;
    NodeSpan post_break;
    int32_t replace_count = 0;
};

// Storage for boxes, discretionaries and their contents. See REPRESENTATION.
// Not thread-safe; one arena belongs to one piece of work at a time.
class NodeArena
{
public:
    // A point to rewind to: everything added after it is dropped together.
    struct Mark {
        uint32_t nodes = 0;
        uint32_t boxes = 0;
        uint32_t discs = 0;
    };

    // Copies a list into contiguous arena storage. `nodes` may view this
    // arena's own storage.
    NodeSpan add_nodes(NodeView nodes);

    // The deepest a box or discretionary may nest: a box of leaves is 1, a box
    // holding such a box is 2. Making a deeper one is a contract violation and
    // yields it with no contents.
    static constexpr int MAX_NESTING = 1024;

    // Appends a Box whose `contents` already name a span of this arena, and
    // returns the BOX node for it. A span reaching outside the arena is a
    // contract violation and is replaced by an empty one.
    Node add_box(const Box& box);

    // Copies `contents` into the arena, appends `header` with its contents set
    // to that copy, and returns the BOX node.
    Node add_box(const Box& header, NodeView contents);

    // Appends a discretionary, copying both lists into the arena, and returns
    // the DISC node.
    Node add_disc(NodeView pre_break, NodeView post_break, int32_t replace_count = 0);

    // The Box or Disc a node names. A node of the wrong type or with an index
    // this arena does not hold is a contract violation and answers an empty
    // one. For a node the caller made itself.
    const Box& box(const Node& node) const;
    const Disc& disc(const Node& node) const;

    // The same for a node out of a list somebody else built, where a wrong
    // index is data and not a mistake of the caller's: null, and no contract.
    const Box* find_box(const Node& node) const
    {
        return node.type == NodeType::BOX && node.u < _boxes.size() ? &_boxes[node.u] : nullptr;
    }
    const Disc* find_disc(const Node& node) const
    {
        return node.type == NodeType::DISC && node.u < _discs.size() ? &_discs[node.u] : nullptr;
    }

    // A span as a view. A span reaching outside the arena is a contract
    // violation and answers an empty view.
    NodeView view(NodeSpan span) const;

    // The contents of the box a BOX node names.
    NodeView contents(const Node& box_node) const { return view(box(box_node).contents); }

    // How deep a node nests: 0 for anything but a BOX or DISC.
    int nesting(const Node& node) const;

    // Copies `node` from `source` into this arena, with everything it
    // contains, and returns the equivalent node here. `source` may be this
    // arena. A box or discretionary of `source` is copied once and every node
    // that named it names the copy, so what was shared stays shared and the
    // work is bounded by what `source` holds. A BOX or DISC node `source` does
    // not hold, or one past MAX_NESTING levels down, comes across as an empty
    // one.
    Node import(const NodeArena& source, const Node& node);

    Mark mark() const;
    // Drops everything added since `mark` was taken. Spans and nodes made after
    // it must not be used again. `mark` is one mark() returned: a Mark put
    // together by hand can keep a box while dropping the nodes it names, and
    // what is added next then lands inside that box.
    void rewind(Mark mark);
    // Drops everything and keeps the capacity, for reuse across paragraphs.
    void clear();

    size_t node_count() const { return _nodes.size(); }
    size_t box_count() const { return _boxes.size(); }
    size_t disc_count() const { return _discs.size(); }

private:
    struct ImportMemo;
    Node import_node(const NodeArena& source, const Node& node, ImportMemo& memo, int depth);
    NodeSpan import_span(const NodeArena& source, NodeSpan span, ImportMemo& memo, int depth);
    // Appends a Disc whose spans already name nodes of this arena.
    Node push_disc(Disc disc);
    bool holds(NodeSpan span) const;
    int nesting_of(NodeSpan span) const;

    std::vector<Node> _nodes;
    std::vector<Box> _boxes;
    std::vector<Disc> _discs;
    // Parallel to _boxes and _discs: how deep each nests.
    std::vector<uint16_t> _box_nesting;
    std::vector<uint16_t> _disc_nesting;
};

// Structural equality: the reference's `==` on nodes. Characters, rules, glue,
// kerns, penalties and math nodes compare by value; a box compares its fields
// (glue_set exactly) and its contents in order; a discretionary its two lists
// and its count. Each node is read in its own arena. A pair of boxes found
// equal is not compared again, so shared contents cost what they hold and not
// one walk per path. A BOX or DISC node its arena does not hold compares as an
// empty one, and anything past NodeArena::MAX_NESTING levels down as unequal.
bool deep_equal(const NodeArena& arena_a, const Node& a, const NodeArena& arena_b, const Node& b);
bool deep_equal(const NodeArena& arena_a, NodeView a, const NodeArena& arena_b, NodeView b);

} // namespace ImTypeset::detail
