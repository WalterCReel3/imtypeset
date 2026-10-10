# blocks — result (builder-27)

`src/blocks.h`, `src/blocks.cpp`, `tests/blocks_golden.h`, `tests/test_blocks.cpp`;
appended `## blocks` to `docs/divergences.md` and D37 to `docs/decisions.md`.
No other file was written. Nothing public changed.

## What was ported

`_block_boxes`, `_rule_box`, `_runs`, `_quote_depth`, `_stacked`, `_barred`,
`compose_blocks` of `advtext/ui/__init__.py`, and `_code_lines` + `_newlines`
of `advtext/markdown.py` — per the lead's correction, not
`advtext/blocklayout.py`. Not ported: `LayoutCache`, `layout_units`,
`RunComposer`, `ui/units.py` (`Document` replaces them), tables, `HostBlock`.

## The internal API (`namespace ImTypeset::detail`, `src/blocks.h`)

- **Description.** `Block {type TEXT|CODE|RULE, BlockStyle style, space_above,
  space_below, quote_depth, quote, list, item, thickness, ink, TextBlock text,
  CodeLines code}`. `Quote {parent, list, bar_x, ink}`, `List {parent, indent,
  tight}`, viewed through `Structure`. `DocumentTheme {quote_indent,
  quote_rule_thickness}`. `set_code(CodeLines&, source, font, composition,
  resolve, why)` builds code lines (tabs to 4 columns, CR LF, stand-ins,
  trailing blank lines dropped). Text material and markers are
  `src/style`'s `append_text` and `src/paragraph`'s `hard_break`,
  `set_marker`, unchanged.
- **Pieces.** `BlockComposer::block_boxes` (one block → its boxes),
  `Stacker::stack` (`StackInput` → `Stacked`: vlist, per-block `Extent`,
  `Bar`s, optional `StackEvent` trace), and the helpers both paths share:
  `derive`, `effective_indent`, `quote_level`, `sets_box`, `block_gap`,
  `run_end`, `marker_need`.
- **All at once.** `DocumentComposer::compose_blocks(arena, blocks, count,
  structure, theme, rules, width, Composed&)` — the reference's
  `compose_blocks`; ship `Composed::box` from `Point{0, packed.box.height}`.
- **Lazily.** `Document`: `add_block`/`edit`/`add_quote`/`add_list`/`clear`,
  `set_rules`/`set_theme`/`set_width`, `prepare`, `layout_window(composer, top,
  bottom) → UnitRange`, `layout_all`, `settled`, `height`, `content_width`,
  `unit(u)`/`unit_top(u)`/`unit_at(y)`/`unit_of(block)`/`bar(i)`,
  `laid_out(block)`/`layout(block)`/`block_top(block)`,
  `flatten(cmds, spans)`. `DocumentComposer` carries `measure`, `params`
  (default `prose_breaking()`), and the counters `blocks_composed`,
  `units_assembled`.

## DocumentRules: each item, its switch, its test

Default = the reference (goldens). `DocumentRules::product(em)` = all on,
code wrapped, `min_measure` 8 em, continuation indent 2 em. Tests are in
`tests/test_blocks.cpp`; the table with what/why is in `docs/divergences.md`.

| # | switch | test |
|---|---|---|
| 1 | none (follows `compose_blocks`; item 2 removes the case) | `an_empty_block_contributes_no_gap` |
| 2 | `skip_empty_blocks` | `an_empty_block_contributes_no_gap` |
| 3 | `quote_any_block` | `code_and_rules_inside_a_quote_keep_its_bar_unbroken` |
| 4 | `bar_by_depth` (+ `Quote::bar_x`) | `a_bar_stands_where_its_own_level_of_quoting_is` |
| 5 | `separate_quotes` (+ `Block::quote`) | `two_quotes_in_a_row_have_a_bar_each` |
| 6 | `floor_rule_width` | `a_rule_is_never_negative_and_its_box_is_the_column` |
| 7 | `code_overflow` GROW/CLIP/WRAP, `code_continuation_indent` | `a_code_line_longer_than_the_measure_grows_clips_or_wraps` |
| 8 | `min_measure` | `deep_nesting_in_a_narrow_column_keeps_a_stated_measure` |
| 9 | `list_spacing_in_quotes` | `a_list_is_spaced_by_what_it_says_in_a_quote_or_out_of_one` |
| 10 | `structured_list_spacing` (+ `List::tight`, `Block::item`) | same |
| 11 | `marker_on_any_block` | `an_item_holding_only_code_or_nothing_keeps_its_marker` |
| 12 | `widen_marker_gutter` (+ `List::indent`) | `the_marker_gutter_widens_to_the_widest_marker_of_its_list` |
| 13 | `rule_in_container` | `a_rule_in_a_list_is_indented_with_its_list` |
| 14 | none (data in every mode) | `which_block_an_item_belongs_to_is_where_it_is_stored` |
| 15 | none (data; the layer makes no marker) | `a_marker_is_whatever_text_the_block_says` |

**Where I did not follow "every one behind a named switch":** 1, 14 and 15
have none, because there is no reference behaviour in this layer to switch
back to (14: the port's per-block ranges equal the reference's bisected spans
on all 799 golden `span` records, so a reference mode would be dead code).
**9 and 13 are front-end defects in the reference**; their switches gate the
part this layer acts on (D37 says exactly what). If the lead reads 9 or 13
differently, that is the place to look.

## What `api-document` inherits

- Use `DocumentRules::product(em)`, and set `BlockStyle::first_line` to
  `BOX_ONLY` (the paragraph layer's default is still the reference's double
  indent; `blocks` passes the style through untouched).
- Give every list and quote an id when it opens (`add_list`, `add_quote`) and
  stamp `Block::list`, `Block::item`, `Block::quote` — with ids, bars, gutters
  and tight/loose are exact; with `quote_depth` alone the reference's
  heuristics apply. `Quote::bar_x` is the indent in force where the quote
  opens; `List::indent` is the gutter the list adds; `Block::style.left_indent`
  stays absolute.
- Per frame: `layout_window(composer, top, bottom)`; then for `u` in the
  range, `y = unit_top(first)` once and `y += extent + next gap` after
  (`unit_top` per unit is O(log n) each); per block `layout(b).cmds` drawn at
  `(0, y + (block_top(b) - unit_top(u)))` with `Block::ink`; per unit its
  `Bar`s (`bar.x, bar.top, bar.thickness, bar.extent`, tops from the unit's
  top) with `Quote::ink`. RULE_RECT commands inside a block's own commands
  are thematic rules.
- Positions are `(block, index)`; `BlockLayout::shipped`, `originals`,
  `hard_breaks`, `marker_items` are `ParagraphLayout`'s, local to the block:
  one `SelectionModel` per block. For code, `continues[i]` says line i is a
  wrap continuation (no newline in copy). `tests/golden/doc` (oracle-doc-extras)
  has cross-block copy expectations; nothing here reads it.
- `clipped` means "clip this block to its box when drawing".
- `height()` is exact only when `settled()`. Units above a window are never
  touched by `layout_window`; scrolling up into never-laid-out units changes
  heights above what is on screen — anchoring is yours (`block_top` before
  and after).
- Inline formula boxes go in `Document::arena()` before the block is laid
  out; they are not reclaimed until `clear()`.
- Errors are per block: `BlockLayout::ok()`, `pack_error`, `break_error`,
  `ship_error`, `budget_exhausted`. `add_block` answers `size()` unchanged at
  `MAX_DOCUMENT_BLOCKS` (2^22).
- `Document::clear()` keeps buffers; rebuilding the same document every frame
  allocates nothing but also throws the layouts away — keep the document and
  `edit` what changed.

## What I ran

Private dirs `build/blocks` (debug), `build/blocks-release`, `build/blocks-asan`
(ASan+UBSan), GCC 12, `-Werror`, zero warnings; clang-format 14 on my four
source files.

- `test_blocks`: 23 tests, 128,674 checks, 0 failed, in each of the three.
  No sanitizer report (the hostile-data test produced seven UBSan overflows
  on first run; fixed by clamping struts and interline skips and saturating
  the placements).
- Goldens, per stage, both metric passes (recorded answers: 0 questions the
  reference did not ask; font reader): 69 cases, 215 compositions, 260 blocks
  (226 text, 21 code, 13 rule) — material / code lines; `lines` (break group,
  hung ×211, strutted dims; 1,831 line records); `stack` trace (585 gaps, 5
  list-empty, 3 negative; 65 bars); `extents`; `vbox`; `cmds` (34,836); then
  the same `vbox`/`cmds` from `compose_blocks`, and from `Document` after
  `flatten` plus 746 units and 799 spans. All equal on the first run of the
  port; no golden touched.
- 400 random documents × random rules: `Document::flatten` equals
  `ship(compose_blocks)` (8,641 blocks, 3,652 bars, 537,987 commands).
- Mutation: 35 single-line mutations of `src/blocks.cpp`; 1 did not compile;
  32 of 34 failed at once; 1 was a test gap (fixed, now fails); 1 equivalent
  (removed).
- Full suite, unnarrowed, 30 ctest entries, in each of the three dirs: see
  the table at the end of this file.

## Lazy-layout evidence (counters, not timers)

`ten_thousand_blocks_are_laid_out_only_where_a_window_shows_them`: 10,000
paragraphs; `prepare` → `blocks_composed` 0; window [0, 300 pt) → 10, units
[0, 10); same window again → +0; window at mid-height → +10..12, first unit
between 4000 and 6000, the unit above it not laid out; `set_width` + `prepare`
→ +0 and nothing `laid_out`; window → +8; edit a shown block → +1; edit block
9000 → +0. `a_quote_is_one_unit_...`: one block of a three-block quote edited
→ +1 block composed, the unit restacked. `laying_out_again_allocates_nothing`:
0 allocations over three widths × (window + all + flatten), with the content
kept and with it rebuilt through `clear()`.

## Not verified

- No timing of any kind; O(log units + shown) rests on the counters and the
  loops. `prepare` is O(blocks) after every change, including one
  `add_block` — a streaming document pays that each time.
- `stb` and `wchar16` builds: not built (nothing here touches a rasteriser
  or `ImWchar`). MSVC, clang, 32-bit: not built.
- Product rules have no oracle: each is tested against hand-computed
  numbers on a fake face (every character half an em), plus the
  two-paths-agree test. Nobody has looked at a rendering of them.
- Product choices made without measurement: 8 em least measure, 2 em
  continuation indent, wrap after the last space, the tight/loose rule for
  nested lists (D37).
- Height estimates on real text (the test's is exact by construction).
- `tests/golden/tables`, `tests/golden/doc`: not read.
- Markers on RULE blocks are ignored under every rule.
- With negative gaps larger than a unit, `unit_at` steps back linearly.

## Noticed, not mine

- `test_opentype_formats`
  (`cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero`)
  and `test_stress_font` (`fuzz_small_synthetic_fonts_densely_all_the_way`)
  fail in all three configurations; both are `font-wild`'s files and the
  board shows that task stranded. `test_stress_font` now takes 330 s in
  debug and about 10 minutes under ASan.
- The board shows `api-revise` stranded too; `docs/divergences.md` refers to
  a D36 that is not in `docs/decisions.md` (mine is D37 for that reason).

## Full suite, unnarrowed (30 ctest entries: 27 tests, smoke, 2 examples)

Run twice per configuration (once before my last two edits to `src/blocks.*`,
once after), then the two failing tests three more times in debug a minute
apart: five attempts, same answer each time.

| dir | configuration | result |
|---|---|---|
| `build/blocks` | debug | 28 of 30 pass; zero warnings |
| `build/blocks-release` | release | 28 of 30 pass; zero warnings |
| `build/blocks-asan` | debug + ASan + UBSan | 28 of 30 pass; zero warnings; no sanitizer report in any test's output |

The two that fail, in all three, are not mine and were being edited while I
ran (`tests/test_stress_font.cpp` changed between my first and second run):

- `test_opentype_formats`:
  `cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero`
  (3 checks), and from the second run on
  `type2_arithmetic_that_overflows_gives_no_box_that_is_not_finite` (11).
- `test_stress_font`: `fuzz_small_synthetic_fonts_densely_all_the_way` (1),
  and from the second run on
  `n3_a_kern_table_of_65535_subtables_is_read_in_one_pass` (1 to 3).

`git status` shows `src/opentype.cpp` and `src/fontlib.cpp` as untracked
(`??`) at the time of writing; I did not touch them.
