# b6-map-shipped — result (builder-11, 2026-10-10)

## The finding: confirmed as latent, not reachable in the tree

`ParagraphComposer::map_shipped` (src/paragraph.cpp) restated the left-skip rule as
`_line_params.left_skip != GlueSpec()`, the reference's test, while the breaker builds lines by
`LineBreaker::left_skip_omitted()`, which under `left_skip_omitted_when_zero` is section 887's.
The two part for a skip with zero amounts and a non-default order.

**Not reachable by any internal entry.** `map_shipped` is private and its only caller is
`set_lines`, which sets `_line_params = skips(style.align, params)`; `skips()` overwrites the left
skip with `GlueSpec()` or `RAGGED_RIGHT` ({0, 1pt, FIL, 0, NORMAL}), and both tests agree on both.
No other file in `src/` reads `left_skip` outside linebreak/paragraph (grep). So no test in the
tree as it stands can fail on it, and I did not change `skips()` to make one.

**Reproduced in a scratch variant instead** (never in the tree): a copy of paragraph.cpp in
`build/b6-repro/` whose `skips()` leaves the caller's left skip alone for JUSTIFY, linked ahead of
the library into the new test, with a caller's skip of {0, 0, FIL, 0, NORMAL}:
- old predicate, product(): `shipped[1]` 1 where 0 is right, `item_count` 4 where 3 is right —
  the off-by-one the reviewer predicted;
- new predicate, product(): both right.
(Both variants also fail one check under reference(), `has_left == RIGHT||CENTER`, which is the
variant's own doing: the reference rule keeps that skip.)

## The fix: one copy of the rule

- src/linebreak.h: `LineBreaker::left_skip_omitted()` moved from private to public, documented as
  the answer for the last paragraph broken. Its definition (src/linebreak.cpp) is untouched;
  `_params` is a member copy that outlives `break_paragraph`.
- src/paragraph.cpp: `map_shipped` takes `left` from `_breaker.left_skip_omitted()`; its comment
  says the rule is asked, not restated.
- src/paragraph.h: not changed.
- reference() behaviour unchanged (the two predicates are the same expression there); no golden
  touched (no file under tests/golden modified during the task, by mtime; git cannot say, the
  tree being uncommitted and partly untracked); no tolerance.

One edge read, not run: `break_paragraph` returns before setting `_params` for an empty ParShape,
so the answer would be the previous paragraph's; there are then no lines and `left` is unread.

## Tests (tests/test_tex_rules.cpp)

- `b6_the_breaker_says_whether_its_lines_begin_with_the_left_skip`: five skips x both settings of
  the switch; the line's first node and length follow the breaker's answer, and the answer differs
  from equality-with-default exactly for the two zero-amount skips with a non-default order under
  the switch.
- `b6_the_shipped_map_counts_the_left_skip_the_breaker_put_on_the_line`: `set_lines` for the four
  alignments under reference() and product(), passing the awkward skip (which `skips()` replaces):
  the glue's shipped index and `item_count` follow the breaker's answer. **This passes before the
  fix too** (see above); it is the test that fails in the scratch variant, and would fail the day
  the composer stops overwriting the skip and someone restates the rule.
- Renamed `easy_line_the_last_resort_is_forced_from_the_cheapest_chain` to
  `easy_line_the_last_resort_is_forced_from_the_one_active_node_left`. Reviewer's reading confirmed
  against tex.web section 854: the test is `link(r)=last_active and prev_r=active`, r is the only
  node left; cost plays no part. Body and comment unchanged (the comment already said so).
  The old name is still cited in docs/divergences.md (B3a row) and
  .concerns/tex-fixes--builder--doc-additions.md — not my files.

## What ran

Private dirs `build/b6` (Debug, -Werror) and `build/b6-asan` (ASan+UBSan), all modules, five tests,
every binary guarded, -j4. Same counts in both:

| test | tests | checks | failed |
|---|---|---|---|
| test_tex_rules | 22 | 42,608 | 0 |
| test_paragraph | 20 | 767,445 | 0 |
| test_linebreak | 13 | 3,553 | 0 |
| test_api | 40 | 71,574 | 0 |
| test_blocks | 23 | 128,674 | 0 |

clang-format --dry-run clean on the three files. Not run: release, stb, wchar16, the presets, any
other test. Build dirs removed.
