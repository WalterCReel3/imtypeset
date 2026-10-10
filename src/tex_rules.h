// Where the layout core can be told to do what tex.web does and the reference
// does not.
//
// The reference is a prototype: in a handful of places it departs from
// tex.web without saying so (docs/divergences.md, the register's "undeclared"
// entries). The goldens hold the port to the reference, so each of those
// places is a switch here and not a change of behaviour. TexRules::reference()
// is the prototype's, exactly, and is what every replay of its goldens runs
// under; TexRules::product() is what the public API lays out with. A default
// TexRules is the reference.
//
// The rules are read in three places, each of which carries a TexRules where
// it already carried its parameters: LineBreakParams::rules (src/linebreak.h),
// Composition::rules (src/style.h) and MathFonts::rules (src/math_mlist.h).
// The device-space rules are DrawRules (src/draw.h) and the document layer's
// are DocumentRules (src/blocks.h); this is the same shape.
//
// Section numbers are tex.web's. No TeX engine was available to check the
// product rules against: each is tested against an answer worked by hand from
// the section it cites (tests/test_tex_rules.cpp), and that is all that is
// claimed for it.

#pragma once

namespace ImTypeset::detail
{

struct TexRules {
    // --- Breaking paragraphs into lines (src/linebreak.*) -------------------

    // Sections 827 and 868: no glue that enters a line has infinite shrink.
    // On, the left skip, the right skip and the par-fill glue are made
    // shrink-finite as check_shrinkage makes them: an infinite order with a
    // shrink that is not zero becomes that shrink at the normal order. Off,
    // the three go into each line as given, and a line holding infinite
    // shrink through one of them always fits.
    bool skips_made_shrink_finite = false;

    // Section 887: the left skip is left off a line when it is zero_glue,
    // which a glue parameter is whenever its width, stretch and shrink are
    // all zero, whatever their orders (section 1229). Off, it is left off
    // only when it equals an all-zero spec in all five fields, so a zero skip
    // with an infinite order puts an empty glue at the head of every line.
    bool left_skip_omitted_when_zero = false;

    // Section 877: after a break at a discretionary whose post-break list is
    // not empty, the next line begins with that list and nothing after it is
    // pruned (post_disc_break, section 884). Off, discardable nodes are
    // pruned after every break.
    bool post_break_text_stops_pruning = false;

    // Sections 835, 848 and 850: easy_line. Every line number past the last
    // specially shaped line is one class, so a breakpoint keeps at most one
    // active node per fitness class however many lines led to it, and the
    // active list is in tex.web's order (section 845: new nodes linked in
    // ahead of the next class). Off, each line number is a class of its own
    // and the list is in the reference's order. The fewest total demerits are
    // the same either way while some chain of feasible lines reaches the
    // paragraph's end. What changes: which of several breakings with equal
    // demerits is chosen; section 854's last resort, which is forced from the
    // only node left and so, off, from the chain with the most lines; and the
    // cost, which off grows with the number of line counts alive at once.
    bool easy_line = false;

    // --- The space between words (src/style.*) ------------------------------

    // Section 1044: a sentence space's stretch is multiplied by the space
    // factor over 1000 and its shrink by 1000 over the space factor with
    // xn_over_d (section 107), which truncates. Off, both are rounded half to
    // even.
    bool sentence_space_truncates = false;

    // --- Math lists (src/math_mlist.*) --------------------------------------

    // Section 755: the italic correction of a math character is followed by
    // a kern only when the atom has no subscript, and delta is then zero;
    // with a subscript no kern is made and delta goes to make_scripts, which
    // sets a lone subscript directly after the character and a superscript
    // delta to the right of the subscript (section 759). Off, the kern is
    // always made and delta is passed on as well, so a subscript sits one
    // correction too far right and a superscript above it two.
    bool italic_kern_only_without_subscript = false;

    // Section 735: make_under adds one default_rule_thickness to the height
    // plus depth of the vlist, so the box reaches that far below the rule.
    // Off, nothing is added.
    bool underline_clearance = false;

    // Sections 754 and 720: a nucleus that is a list is packed into one box,
    // and clean_box uses what it converted as it stands when that is already
    // one unshifted box. Off, the list's nodes are spliced into the outer
    // list and clean_box always packs. The dimensions are the same either
    // way; the boxes are not.
    bool sublist_nucleus_packed = false;

    // Section 703: cur_mu is x_over_n(math_quad, 18) (section 106), which
    // truncates, of the quad as a whole number of scaled points. Off, it is
    // the unrounded quad over eighteen, rounded half to even.
    bool mu_truncates = false;

    static constexpr TexRules reference() { return TexRules(); }
    static constexpr TexRules product()
    {
        TexRules rules;
        rules.skips_made_shrink_finite = true;
        rules.left_skip_omitted_when_zero = true;
        rules.post_break_text_stops_pruning = true;
        rules.easy_line = true;
        rules.sentence_space_truncates = true;
        rules.italic_kern_only_without_subscript = true;
        rules.underline_clearance = true;
        rules.sublist_nucleus_packed = true;
        rules.mu_truncates = true;
        return rules;
    }
};

} // namespace ImTypeset::detail
