# review-4-draw findings (filed by the lead from the reviewer's concern, 2026-10-10)

ALL READ-DERIVED: source plus hand arithmetic at 72.27 px/in (1 pt = 1 px), origin 0. Nothing run.
Symbols rather than line numbers: dist was renaming.

F1. A word glued to the END of a formula is placed unclamped (Drawer::place_commands, the
    `if (in_formula) { … else { in_formula = false; if (!span.word) { span.prev_pen += snap_px(…)`
    block; the led clamp is the only clamp and this path skips it). Chain: formula edge ±0.5, last
    shipped formula glyph ±0.5, gap to the text glyph ±0.5, width hold ±0.5; the next word is clamped
    to its own pixel, so up to 2.0 px comes out of the following space.
    Input `$x$-th a`: ATOM x=10.6 -> 11; last formula glyph offset 5.6 -> 17 (layout 16.2);
    '-' gap 4.6 -> 22 (20.8); 't' gap 4.6 -> 27 (25.4); 'h' gap 4.95 -> 32 (30.35); glue 36..39;
    'a' layout 39.0, led 41, clamped to 39. Drawn h..a = 7 against 8.65: a 3.0 px space drawn 1.35.
    Same path: in `$\frac{n(n+1)}{2}$)` the paren is placed from the denominator's last glyph:
    denominator offset 20.49 -> 20; gap 26.49 -> 26; paren at edge+46 while the bar ends at edge+47.
    The corpus has no `$..$` directly followed by a letter, so the 37,778-space test cannot see it.
    Reviewer's suggestion (not a ruling): place the first text glyph after a formula from the
    formula's own grid (formula_pen + snap(px(x - formula_x))).
F2. Text straight BEFORE a formula with no glue ("($x$)", "f($x$"): product < 1.5 (2.0 for a
    multi-glyph word) against the reference's 1.0. Input: previous word's last glyph layout 29.55
    drawn 31; "(" layout 39.02 led to 40; formula box 44.49 -> 44; drawn 4 px against 5.47.
F3. "space within 1.5 px" does not hold beside a formula: by hand < 2.0 (word led +0.98, held +0.5,
    formula edge −0.5); a justified 2.45 px space before `$x$` can be drawn 0.47 px. The author's own
    report shows +1.56 measured. The `word_spaces_led` comment in src/draw.h says "always".
F4. Demo context guard (src/imtypeset_demo.cpp ShowDemoWindow,
    `context != g_demo_context || stats.TextBuilds < g_context_text_builds`):
    (a) use-after-free: show in context A; collapse the window (`!Begin` early return is before the
        guard); host destroys A, creates B at the same address and typesets >= as many strings;
        expand: guard silent, g_paragraphs[] / g_document dangle.
    (b) leak: two live contexts each showing the demo every frame: each call drops the slots without
        destroying them and recreates them.
F5. tests/test_demo.cpp is order-dependent through file-statics: the drag test presses at (30, 60),
    by the builder's geometry inside the "Font size" slider, leaving g_font_size at 40 px (and
    copies at 1000) for later tests. Moderate confidence (geometry computed, not run).
F6. tests/test_word_gaps.cpp `a_word_after_a_space_is_placed_from_the_word_before…` last sub-case:
    comment says 18 + snap(3.4) = 21; the distance is 3.60, so led gives 22 = own place; the
    whole-pixel clamp (high == low) is never exercised.
F7. Low: settle_word uses std::nth_element (quadratic worst case on libc++; clang never built here);
    `_gaps` never shrinks (a megabyte word leaves ~8 MB in the Drawer).
F8. Low: `in_formula` outlives the formula for a foreign RULE_RECT (hand-built lists only).
Also: a mid-word glyph is bounded by neither rule set (2.8 px measured by word-gaps), so a caret or
highlight edge can cut 2-3 px into a neighbour and a click on a narrow glyph can hit its neighbour
(selection.cpp not read). Pre-existing: Drawer::key_slot is a linear search over keys.
Confirmed by reading: reference() isolation; the test_api one-liner; word hold arithmetic; settle_word
linear on libstdc++; led clamp; formula_placed_whole hand cases.
