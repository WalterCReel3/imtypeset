# Lead rulings for task `blocks` (from the oracle-blocks result)

Read `.concerns/oracle-blocks--builder-22--result.md` and `tools/oracle/FORMAT-blocks.md` first.

Correction to the `blocks` board spec: `advtext/blocklayout.py` is a row/column UI demo, NOT the
document block layer; the thing to port is `_stacked` and its helpers in
`../imgui-adv-text/advtext/ui/__init__.py` (around lines 1132-1500), as the oracle drove them.

How the reference stacks (measured by the oracle builder): the gap between blocks is ONE kern of
space_below(previous) + space_above(next) — a sum, no collapsing, omitted when zero, may be negative;
prev_depth resets at every run so there is never interline glue between blocks; the first block's
space above and the last block's space below are never applied; a quote run is one hbox (kern, rule,
negative kern, vbox of the nested stack) by recursion; rules and bars ship as rule rectangles; ink is
per run by role with index ranges.

RULING — same pattern as the paragraph indent (decisions D32.3): the internal port reproduces the
reference EXACTLY so the 69 golden cases match stage by stage, and every one of the fifteen prototype
shortcuts the oracle builder listed is behind a named switch in a `DocumentRules` struct whose DEFAULT
is the reference's behaviour (goldens) and whose `DocumentRules::product()` preset selects the fixed
behaviour the public Document API will use. Each fixed behaviour gets a direct test of its own (no
golden exists for it) and a line in docs/divergences.md saying what the prototype does, what the
product does, and why. The fixes, by the oracle report's numbering:
 2  an empty block contributes no gap (no two kerns back to back around nothing);
 3  code blocks and rules inside a quote keep the quote's bar unbroken;
 4  a quote bar's x depends on its nesting depth;
 5  two adjacent but separate quotes get separate bars (a gap in the bar where the blocks' gap is);
 6  a rule's width is floored at zero (and at the column's width);
 7  code lines: a per-document choice of overflow (reference), clip to the measure, or wrap at the
    measure with a hanging continuation indent; the document box never grows past the measure unless
    overflow is chosen;
 8  deep nesting in a narrow column: the floor on line width is stated and tested, not accidental;
 9  a list item inside a quote keeps list-item spacing;
10  tight and loose lists differ (paragraphs within one item are spaced as paragraphs; items of a
    tight list are spaced as lines) — the block description carries `tight`;
11  an item holding only code keeps its marker; an empty item is kept as an empty line with its marker;
12  the marker gutter widens to the widest marker in its list (so "100." does not overprint);
13  a rule inside a list or quote is indented to its container;
14  which block an item belongs to is carried as data, never found by bisecting baselines;
15  markers are data on the block (any text), not fixed to a bullet and "N.".
 1  (`compose_blocks` vs `layout_units` disagree when the first block sets no box): the product
    follows `compose_blocks` — the goldens — and item 2's fix removes the case that made them differ;
    say so in docs/divergences.md.
Not in this task: tables, formulas-as-blocks beyond what a paragraph run already carries, links,
`HostBlock`, incremental relayout policy beyond per-block caching (the board spec's lazy-layout
requirement stands), selection/copy across blocks (design the per-block index ranges so a later task
can add it: one SelectionModel per block, positions as (block, byte offset)).
You also write tests/blocks_golden.h (the C++ reader for the new records).
