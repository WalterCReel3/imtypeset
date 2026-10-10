# para — result

Paragraph assembly is ported: `src/paragraph.h`, `src/paragraph.cpp`,
`tests/test_paragraph.cpp`. Also added, outside the task's listed files (see
"Files outside `touches`"): `tools/oracle/dump_para_map.py`,
`tests/golden/para-map/map.golden`, and a `## para` section in
`docs/divergences.md`.

## What `public-api` and `api-math` inherit

All in `namespace ImTypeset::detail`, `src/paragraph.h`.

- **Material** is still built with `src/style`'s `append_text` into
  `TextBlock::run` (a `Run`). `para` adds `hard_break(run, align, source_offset)`
  (= the reference's `_unskip` + `_fill_and_break`), `unskip(run)`,
  `set_marker(block, text, font, composition, resolve, why)` (a separate
  unbreakable run with its own space factor) and `TextBlock::clear()`.
  `Run::source_offsets` is kept in step by `unskip`/`hard_break`.
- **`BlockStyle`** is every value composition reads: `align`, `left_indent`
  (enclosing + own, already summed), `first_line_indent`, `first_line`
  (`FirstLineIndent::BOX_AND_SHAPE` | `BOX_ONLY`), `marker_gap`, `interline`
  (`InterlineSkip`), `strut`, `ragged_total_fit`.
  **The default `first_line` is `BOX_AND_SHAPE`, the reference's double
  indent**, because that is what the goldens hold. Per the lead's ruling
  `public-api` must set `BOX_ONLY` (tex.web §1091) explicitly.
- **`prose_breaking()`** is the reference's `PROSE_BREAKING` (tolerance 1000,
  emergency stretch 20 pt). `compose` takes the params as an argument; it does
  not default them. `skips(align, params)`, `shape(width, left_indent,
  first_line_indent)` → `BlockShape` (owns up to two `LineSpec`s;
  `par_shape()` views them), `unshrinkable(view, out)`, `hang_marker(...)`
  are callable alone.
- **`ParagraphComposer`** owns a `LineBreaker` and scratch.
  `compose(arena, block, style, width, params, measure, layout)` does break →
  hang marker → strut → vlist → vpack; `set_lines(...)` stops before the
  vlist, for a caller stacking several blocks (space above/below and the
  `prev_depth` reset between blocks are NOT here — that is `_stacked`'s
  multi-block half, and `public-api` writes it over `ParagraphLayout::lines`
  with `append_to_vlist`).
- **`ParagraphLayout`**: `broken` (the breaker's `Paragraph`), `hung`, `lines`
  (strutted BOX nodes), `vlist`, `packed`, `box` (the vbox node), and the copy
  data below. `ship(arena, layout, top_left, measure, out)` ships `box` from an
  origin on its first baseline.
- **Copy data** (`shipped`, `mapped`, `item_count`, `marker_items`,
  `originals`, `hard_breaks`):
  - `shipped[m]` is the ship-out index of node `m` of `TextBlock::run.nodes`,
    or `NOT_SHIPPED`. Indices are local to the block (0-based, marker first);
    a caller shipping several blocks into one sequence adds its base.
  - `originals` is `{index, codepoint}` ascending — codepoints, not UTF-8;
    `public-api` encodes them into `selection`'s `IndexedText`.
  - **`hard_breaks` must be passed to `SelectionModel::text` as separators of
    `"\n"`, not as `TextData::hard_breaks`.** The reference's `ui/units.py`
    does exactly that (`separators[index] = LINE_BREAK`). `TextData::hard_breaks`
    only fires when the index equals the fragment's start, and in a block set
    ragged left or centred the line starts one index earlier, at the left
    skip's SET_GLUE — a hard break would copy as a space.
  - `marker_items` is the index where the reference puts the `" "` separator
    after a list marker (`start + 1 + index_count(marker)`).
  - With `Run::source_offsets`, `shipped` is also the index → source-byte
    map `public-api` was told to provide.
  - `mapped` is false, and nothing is mapped, when a break was taken at a
    DISC (the line then holds pre/post-break nodes that are not material) or
    when the walk over shared boxes exceeds its budget.
- **Inline formulas** (`api-math`): append the atom box (in the same arena) to
  `block.run.nodes`. It maps to one index, its ATOM. A non-atom box maps to
  the first index inside it.
- **Errors**: `compose` returns `layout.ok()`; `pack_error` is the first pack
  error (a CHAR with no measure in practice), `break_error` the breaker's.
  Lines are still produced.
- **Allocation**: with one `TextBlock`, `ParagraphComposer`, `ParagraphLayout`
  and an arena rewound to a mark, steady-state composition (append text, hard
  break, marker, compose, ship) makes zero allocations — tested by counting
  `operator new`.
- `shape` clamps its three inputs to ±`MAX_DIMEN`; nothing else here clamps.
  `BlockStyle` lengths are expected within `MAX_DIMEN` (the "one validating
  entry" ruling puts that on `public-api`).

## What was run

Private build dirs, modules
`units;badness;nodes;pack;linebreak;shipout;style;fontlib;opentype;fontmetrics;unicode_tables;paragraph`,
tests `test_paragraph`:

| dir | config | result |
|---|---|---|
| `build/para` | default (debug) | 20 tests, 767,445 checks, 0 failed |
| `build/para-asan` | Debug + ASan + UBSan | 20 tests, 767,445 checks, 0 failed |
| `build/para-release` | Release | 20 tests, 767,445 checks, 0 failed (0.94 s) |

Zero warnings in all three. `ctest` run in `build/para` and `build/para-asan`.

What the tests hold:

- All 243 `para` cases of `tests/golden/para/{edge,whitespace,standin,runs,prose,sample}.golden`,
  710 widths, 4,902 lines, 90,419 glyphs — material, marker, shape, params,
  the unshrinkable list, the broken paragraph, the hung line, strutted dims,
  vlist, vbox, origin and shipped commands, each compared as text with the
  golden — **twice**: once with metrics answered only from the case's records
  (43,251 answers; a question the reference did not ask fails the test; 0
  misses), once with the real font reader (`FontLibrary` + `fontlib_metrics` +
  `CharMeasureCache`). In the font-reader pass the 172 `text`/`text-hard-breaks`
  cases are also rebuilt from the source text with `append_text_block`.
- `tables.golden`: constants, `_skips`, `_fill_and_break`, `_shape`,
  `_unshrinkable`, `Strut`, and the 828 `_stand_in` records with recorded
  `has` answers (0 misses).
- `para-map/map.golden` (new): 36 blocks × 3 widths; `shipped`, item count,
  hard-break boundaries and originals equal the reference's
  `spans.map_text_block`, `units._after_hard_breaks`, `units._originals_within`.
- Invariants of the map against the shipped commands on every one of the 710
  + 108 compositions (`check_map`).
- Hand-written: `BOX_ONLY`, alignment, `ragged_total_fit`, `unskip`, the
  space-before-hard-break defect, atoms, a DISC break, malformed UTF-8 byte
  recovery, empty blocks, no measure, `set_lines`, zero allocation.

Mutation check: 20 single-line changes to `src/paragraph.cpp`; 19 failed the
suite. The survivor was a condition that turned out to be redundant
(`m + 1 < size` in the hard-break walk — a forced break that ends the material
has nothing after it anyway) and was removed.

## Not verified

- `stb` and `wchar16` presets: not built. Nothing here touches the rasteriser
  or `ImWchar`.
- The preset builds (`build/debug` etc.) and the rest of the test suite: not
  run. Only `test_paragraph` was built against my change.
- `tests/golden/para/unicode.golden` is not replayed here: `test_style`
  replays the same tables from `tests/golden/math/unicode.golden` and the
  oracle script asserts the two files agree.
- `ragged_total_fit = true` has no golden (FORMAT-para.md §7 says so); only a
  hand test that the optimiser runs and the material is handed over unchanged.
- `BOX_ONLY` has no golden; hand test only.
- More than one block, quote bars, code blocks, rules, tables: not ported
  (not in the task).
- No timing was taken beyond the release test's wall time.
- `dump_para.py --verify` was not re-run; I did not touch its directory.

## FINDINGS TO CHECK — what I found

- "generate `src/unicode_tables.inc` with `tools/gen_unicode_tables.py`":
  superseded, as `.concerns/style--builder-6--shared-files.md` says.
  `src/unicode_tables.h` exists and `style` uses it; I created neither file.
- "Malformed UTF-8 … original bytes recoverable for copy — same shape as
  ReplacedChar": not the same shape. `style` (lead accepted) sets it as an
  ordinary U+FFFD with no `original`. The bytes are recoverable another way,
  through `Run::source_offsets` + `shipped`; a test shows it.
- [R] `_text_boxes` at `ui/__init__.py:1405-1418`: it is at 1369-1422 in the
  tree as it stands; behaviour as described.
- Lead ruling cites tex.web §1091 for the indent box: confirmed by counting
  sections (`new_graf`). Also checked: §679 `append_to_vlist`, §816
  par-fill, §847 `\parshape`, §886 right skip, §887 left skip, §1105
  `delete_last` (`\unskip`).
- The reference's `_unskip` loops (drops *every* trailing glue, each with a
  guarding `INF_PENALTY`); the task text reads as if it drops one.

## Decisions

1. Default `first_line` is the reference's (`BOX_AND_SHAPE`). Reason: the
   module "reproduces the reference for the goldens"; the public API opts into
   TeX's.
2. The indent box is added to the arena at compose time and is not a node of
   `TextBlock::run`, so `shipped` and `Run::source_offsets` index the same
   list. (In the reference it is `material[0]`.)
3. The index map is computed from the breaks rather than by porting
   `advtext/spans.py`. Reason: the breaker already knows which nodes each line
   holds; the reference has to rediscover it. Pinned against the reference by
   the new golden.
4. A strutted line that the strut does not change is the breaker's own BOX
   node; otherwise a second `Box` header over the same contents span. No node
   is copied.
5. `ragged_total_fit` is a `BlockStyle` flag, where the reference subclasses
   `LineBreakParams`.

## Files outside `touches`

- `tools/oracle/dump_para_map.py` and `tests/golden/para-map/map.golden`
  (36 cases, 37,646 bytes; regenerates byte-identically). A new directory, not
  `tests/golden/para/`, because `dump_para.py --verify` globs that directory
  and warns about files it did not write. The record format is in the script's
  docstring; `tools/oracle/FORMAT-para.md` is not mine and does not mention it.
- `docs/divergences.md`: appended `## para`.
- `docs/decisions.md`: not touched. D20 already records the D1 carve-out.

## Noticed, not mine

- `tests/test_linebreak.cpp` replaces `operator new(size_t)` and two deletes
  only. Under ASan that is an alloc/dealloc mismatch as soon as anything in
  the executable uses a nothrow `new` — `std::stable_sort`'s temporary buffer
  does (`src/opentype.cpp`, `read_coverage`). `test_linebreak` does not load a
  font today, so it does not trip. `test_paragraph` replaces every form.
