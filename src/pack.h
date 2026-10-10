// hpack and vpack: measuring a list into a box and deciding how its glue is
// set, and tex.web section 679's interline glue. Mirrors advtext/pack.py.
//
// WHAT OF tex.web THIS IS. Part 33, "Packaging" (sections 644 to 679):
//
//   section 644        exactly, additional, natural     PackMode
//   section 646        total_stretch, total_shrink,     GlueTotal (nodes.h),
//                      last_badness                     PackResult::badness
//   sections 649-656   hpack: the walk over the list    hpack
//   sections 657-667   the glue setting of an hbox,     PackResult's box,
//                      and how good it is               fitness and badness
//   sections 668-671   vpackage (vpack is its special   vpack
//                      case with max_dimen as the
//                      depth limit): the walk
//   sections 672-678   the glue setting of a vbox       the same code as 657-667
//   section 679        append_to_vlist                  interline_glue,
//                                                       append_to_vlist
//
// and, from elsewhere: section 212's ignore_depth (IGNORE_DEPTH) and section
// 1056's rule in vertical mode, which append_to_vlist folds in.
//
// WHERE THIS IS NOT tex.web. hpack and vpack there build the box node and
// return it; here they measure and the caller stores (below). There is no
// adjust_tail and nothing is moved out of the list (sections 647, 655). The
// messages of sections 660 to 667 and 674 to 678, and the hbadness, vbadness,
// hfuzz and vfuzz that gate them, are replaced by PackResult, which reports
// every time. The overfull rule of section 666 is not added. A list that
// tex.web would stop at with confusion(), or pack with a null_flag in a sum,
// is packed with the offending node left out and an error named.
//
// The glue setting lives on the box, not in the glue. Packing records three
// things -- glue_set, glue_sign, glue_order -- and leaves every glue node as it
// is; ship-out multiplies the two back together. That is what lets one list be
// packed to two widths without being copied.
//
// So packing here does not build anything. hpack and vpack read a list
// wherever it is -- a vector, part of one, a span of an arena -- and answer a
// PackResult whose `box` has every field settled except `contents`. Nothing is
// allocated and the arena is not written, which is the shape the line breaker
// needs to try a candidate line. A caller that wants the box as a node makes it:
//
//     const PackResult packed = hpack(arena, nodes, width, PackMode::EXACTLY, measure);
//     const Node line = arena.add_box(packed.box, nodes);
//
// The order hierarchy decides everything: if any fil stretch is present, finite
// stretch contributes nothing at all. The orders are accumulated apart
// (GlueTotal) and the dominant one is chosen once, after the whole list has
// been walked.
//
// Fonts are injected. A CHAR node names a font and a codepoint and carries no
// metrics; packing a list that contains one needs a CharMeasure.
//
// Where the reference raises, packing reports. A list reaches here from code
// that built it and, through the public API, from a host that built it by hand,
// so a malformed list is data: PackResult::error names what was wrong, the
// offending node contributes nothing, and the rest of the list is packed.
//
// Not here: kerning and ligature building (kerns are nodes by the time a list
// is packed), the hbadness/vbadness/hfuzz reporting gates (PackResult reports
// unconditionally), vadjust, inserts, marks, leaders.

#pragma once

#include "nodes.h"
#include "units.h"

#include <cstdint>
#include <vector>

namespace ImTypeset::detail
{

// The badness of an overfull box: the 1000000 that tex.web sections 664 and
// 676 store in last_badness. Not a badness in section 108's sense, which never
// exceeds 10000: a sentinel that dominates every real one. The length
// overflowed by is PackResult::overfull_by (what sections 666 and 677 print).
constexpr int OVERFULL_BADNESS = 1000000;

// The prev_depth that suppresses interline glue: tex.web's ignore_depth
// (section 212), exactly -1000 pt, used as a sentinel. A vertical list starts
// at it (section 215) so that its first box gets no glue above it.
constexpr Scaled IGNORE_DEPTH = -65536000;

// Whether the target is the final size or an amount added to the natural one:
// tex.web's exactly and additional (section 644), with its values. Its
// `natural` is ADDITIONAL with a target of zero, which is the default here.
enum class PackMode : uint8_t { EXACTLY = 0, ADDITIONAL };

// How the contents met the target. Not a type of tex.web's, which has the
// words only in its messages: LOOSE and UNDERFULL are section 660's two
// (badness up to 100, and above), TIGHT is section 667's, OVERFULL section
// 666's. INFINITE_GLUE and EMPTY are the two cases sections 658 and 664
// decline to judge: glue set at an infinite order always reaches the target,
// and an empty box packed to a size is a spacer somebody wrote.
enum class PackFitness : uint8_t {
    EXACT = 0,
    LOOSE,
    UNDERFULL,
    TIGHT,
    OVERFULL,
    INFINITE_GLUE,
    EMPTY,
};

// What was wrong with a list, where the reference raises ValueError. tex.web
// has no counterpart but confusion("vpack") for the third.
enum class PackError : uint8_t {
    NONE = 0,
    // hpack met a CHAR node and was given no CharMeasure.
    CHAR_WITHOUT_MEASURE,
    // A rule's width is never RUNNING inside an hlist. tex.web never makes
    // such a rule (section 138 says which dimensions may run in which list)
    // and section 653 would add the flag to the width unexamined.
    RUNNING_WIDTH_IN_HLIST,
    // A character cannot appear in a vertical list (section 669 stops with
    // confusion("vpack")).
    CHAR_IN_VLIST,
    // A rule's height and depth are never RUNNING inside a vlist.
    RUNNING_HEIGHT_OR_DEPTH_IN_VLIST,
    // A BOX or DISC node names nothing the arena holds. It contributes nothing.
    NODE_NOT_IN_ARENA,
    // A sum of the list's dimensions left the range of a Scaled, which no list
    // honouring MAX_DIMEN can do (src/units.h). The sum stops at the end of the
    // range, so the box's dimensions and glue setting are defined and wrong.
    DIMENSION_OVERFLOW,
};

// What packing needs to know about one glyph: its advance and its ink extents,
// height upward and depth downward from the baseline.
struct CharDimens {
    Scaled width = 0;
    Scaled height = 0;
    Scaled depth = 0;
};

// How a caller tells hpack how big a character is: a function and the context
// it wants back. No allocation, no captures; an empty one means "no measure".
// The same measure has to be given to ship-out, which looks advances up again.
struct CharMeasure {
    using Function = CharDimens (*)(void* user, FontKey font, uint32_t codepoint);

    Function function = nullptr;
    void* user = nullptr;

    explicit operator bool() const { return function != nullptr; }
    CharDimens operator()(FontKey font, uint32_t codepoint) const
    {
        return function(user, font, codepoint);
    }
};

// A packing: the box, and what packing it was like.
//
// `box` has its kind, dimensions and glue setting; its `contents` are empty
// (see the top of this file). `natural` is the size the contents came to on
// their own and `excess` is target minus natural, positive when the glue had to
// stretch; both are along the packing axis (the two values tex.web's x holds
// in turn, section 657). `badness` is tex.web's last_badness (section 646): 0
// when nothing had to be made up or no finite judgement applies, and
// OVERFULL_BADNESS when the box does not fit. `overfull_by` is zero unless
// `fitness` is OVERFULL.
struct PackResult {
    Box box;
    PackFitness fitness = PackFitness::EXACT;
    PackError error = PackError::NONE;
    Scaled natural = 0;
    Scaled excess = 0;
    int badness = 0;
    Scaled overfull_by = 0;
};

// The three parameters section 679 consults, which are meaningless apart:
// tex.web's baseline_skip, line_skip and line_skip_limit.
struct InterlineSkip {
    GlueSpec baselineskip;
    GlueSpec lineskip;
    Scaled lineskiplimit = 0;
};

// Packages a horizontal list (tex.web sections 649-667). Widths add; heights
// and depths take the maximum, each from the baseline. A box shifted down by s
// contributes height - s and depth + s. `arena` is where the list's BOX nodes
// live. `measure` is needed only if the list contains a CHAR node. `target`
// and `mode` are tex.web's w and m.
PackResult hpack(const NodeArena& arena, NodeView nodes, Scaled target = 0,
                 PackMode mode = PackMode::ADDITIONAL, CharMeasure measure = CharMeasure());

// Packages a vertical list (tex.web sections 668-678). This is tex.web's
// vpackage, of which its vpack is the case max_depth = max_dimen: the default
// here. Heights and depths chain: each item adds the previous item's depth and
// its own height, and the last item's depth becomes the box's. Widths take the
// maximum, a box's shift_amount moving it right. Depth past `max_depth` is
// converted into height, not discarded.
PackResult vpack(const NodeArena& arena, NodeView nodes, Scaled target = 0,
                 PackMode mode = PackMode::ADDITIONAL, Scaled max_depth = MAX_DIMEN);

// The glue that goes above `box` in a vertical list (tex.web section 679).
// False means none: prev_depth is still IGNORE_DEPTH (or below), so this is the
// first box. Otherwise `glue_out` is a GLUE node: the baselineskip with its
// width replaced by what keeps the baselines that far apart (its stretch and
// shrink survive), or, when that gap would fall below lineskiplimit, the
// lineskip exactly as it is.
bool interline_glue(Scaled prev_depth, const Box& box, const InterlineSkip& skip, Node& glue_out);

// Appends `item`, a BOX or RULE node, to `vlist` with its interline glue, and
// returns the new prev_depth. Start a list at IGNORE_DEPTH.
//
// For a box this is tex.web's append_to_vlist (section 679). tex.web never
// hands that procedure a rule: a rule in vertical mode is appended by
// main_control, which sets prev_depth to ignore_depth (section 1056). That
// case is folded in here. A rule therefore suppresses interline glue on both
// sides of itself: none goes above it and it answers IGNORE_DEPTH, so none
// goes below it either. A document that wants space around a rule says so with
// explicit glue.
//
// An item of any other kind is a contract violation; it is appended as it is
// and prev_depth is answered unchanged.
Scaled append_to_vlist(const NodeArena& arena, std::vector<Node>& vlist, const Node& item,
                       Scaled prev_depth, const InterlineSkip& skip);

} // namespace ImTypeset::detail
