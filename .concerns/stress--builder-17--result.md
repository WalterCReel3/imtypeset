# stress — result (builder-17)

One row per item is in `docs/hardening.md`; decisions are D33 in
`docs/decisions.md`; what now differs from the reference is `## stress` in
`docs/divergences.md`. The font half (S1-S9 and `tests/test_stress_font.cpp`)
was written by a helper agent under this claim; I re-ran the whole suite on
the result in six configurations, re-ran its 10x fuzz, and did one mutation
check (strict pass disabled: the S2 test fails). Its statements that I did
not re-run are marked below.

## Done / not done, per item

| id | state |
|---|---|
| A | done: reproduced (ASan stack-overflow), fixed in `add_box`/`push_disc` + depth guards in `ship`/`import`/`deep_equal`, 3 tests |
| B | done: reproduced (2,097,153 commands / 134 MB from 42 nodes), default bound 16 x nodes + 65,536. NOT the exact "one visit per node" of the review: that failed two existing tests that ship one box several times |
| C | done: reproduced (42 nodes -> 4.2 M nodes, 4.6 s), `import` memoises and keeps sharing, `deep_equal` remembers equal pairs |
| D | done: reproduced (18 UBSan sites), checked arithmetic; `PackError::DIMENSION_OVERFLOW`, `ShipError::DIMENSION_OVERFLOW`. Plus a glue order byte of 4 = ASan stack-buffer-overflow in `GlueTotal::add` (not in the review), masked |
| E | done: budget 2^20 candidate lines (~85 ms release), first-fit fallback, `Paragraph::budget_exhausted` + `BreakPass::FIRST_FIT`; public: `GetParagraphError()->Message` with `Code` None; both candidate paths covered; below the budget equal to an unbudgeted breaker line for line; all goldens exact |
| F | done: reproduced (3 contract violations, error NONE), `find_box`/`find_disc`, `NODE_NOT_IN_ARENA`, `ROOT_NOT_A_BOX` |
| K | done from a reading (not run before): a corrupt enum prints `<not-a-member>` |
| S1 | done (helper): reproduced (+495 MB, 7.4 s), dedupe by offset + one GPOS budget |
| S2 | done (helper): reproduced under stb (ASan SEGV in `stbtt_GetGlyphBox`) with a loca pair 1 MB past glyf; the reviewer's exact 0x7FFFFFF0 shape did NOT reproduce. `OpenTypeFont::strict_check`, `add_face_to_atlas` refuses. Mutation-checked by me |
| S3 | done (helper): reproduced (184 KB -> 184 MB), overlap and duplicate tags refused |
| S4 | did not reproduce (64-bit); product formed in `uint64_t` by construction |
| S5 | done (helper): measured first (0.871 s a glyph under ASan), budget counts segments |
| S6 | done (helper) from a reading; test holds the new behaviour |
| S7 | done (helper) from a reading; minted first, shortfall counted and warned |
| S8 | done (helper): the printing tests assert; exact counts |
| S9 | done (helper) from a reading: 256-512 sizes kept per role, freed only on the owner's say-so; held from outside by a public-API zoom test under ASan |
| sentence space past MAX_DIMEN | done: reproduced (width 1.33x, stretch 1.5x MAX_DIMEN; UBSan in the product), clamped in `interword_glue` |
| `operator new` in test_linebreak | done: reproduced (ASan alloc-dealloc-mismatch), full replacement |
| Unicode tables in selection.cpp | done: one copy, `src/unicode_tables.h` |
| three stress tests | done: `test_stress_font` (16 tests), `test_stress_text` (10, public API only), `test_stress_layout` (16). Default under ASan+UBSan, sequential: 19.4 + 25.0 + 9.3 = 54 s |
| font mutants all the way, both rasterisers | done (helper; re-run by me in FreeType/ASan, stb/ASan, 16-bit) |
| scaled-up run | done: fonts 10x, text 10x, layout 20x, all 0 failed / 0 sanitiser reports (numbers below) |

| D1 (draw review, arrived late) | done: reproduced (ASan SEGV reading `_latin`), a flag replaces the all-ones sentinel |
| D2 | done: reproduced with a codepoint the current font really lacks (U+4E00: glyphs 1 / missing 1, or 2 / 0 by order; the review's em dash does not show it under the test stage's Roboto); `KeyState::bound` |
| D5 | done: buffer comparison asserted under FreeType (22 of 22 cases, 0 boxes differing); the two unfalsifiable assertions replaced |
| D6 | answered by reading ImGui 1.92.9 (a bake stamped this frame is never discarded by `MakeSpace`); no run |
| D3, D4, D7 | NOT DONE (reasons in docs/hardening.md `## Draw`): D3 needs `imgui_internal.h`, which D30 excludes; D4 changes only draw-command boundaries past 64k vertices; D7 not looked at |

NOT done from the original list: nothing. Not fixed on purpose: ImGui's "Out of texture
memory" assertion (below); stb_truetype reading an advance as signed.

## Counts

- Font mutants, default ASan run: 1,033 (bundled, 7 fonts) + 1,680 (21
  synthetic seeds) + 106 (public entry) + 63 structured cases, in each of
  FreeType and stb. Release: 19,753 + 63,000. My 10x re-run (ASan, FreeType,
  final tree): 8,053 + 16,800 + 484, 7,268,161 checks, 0 failed, 143 s.
  Helper's 10x (at 1.25x constants): FreeType ASan and stb ASan 10,003 +
  21,000 + 484 each, 16-bit debug 29,503 + 126,000 + 484; and 2,910
  installed fonts read in place, 11,640 mutants of them (not re-run by me).
- Text 10x (ASan): 4,500 hostile paragraphs, 12,000 paragraph lives, 10,000
  relayouts, 9,000 zoom sizes, 15,000 widget frames: 1,934,851 checks, 0
  failed, 159 s. Layout 20x (ASan): 16,000 well-formed paragraphs, 30,000
  hostile lists: 2,955,032 checks, 0 failed, 70 s.
- Reviewer items: 16; 10 reproduced/measured then fixed, 5 fixed from a
  reading, 1 not reproducible on 64 bits. Other items handed over: 3, fixed.
- Defects found beyond the lists: 6. Fixed 4 (stb_truetype one-byte read
  past its vertex array for a glyph ending in a one-point off-curve contour;
  FreeType sizing by OS/2 typo metrics, 66 of 2,917 installed fonts; a run
  error position outside the text; glue order past `GlueTotal`). Recorded,
  not fixed, 2 (ImGui atlas assertion by three routes; stb's signed advance).

## What I ran

Full suite, unnarrowed, before (debug, release, ASan+UBSan: 23/24, only
`test_opentype_wild`) and after, in private dirs: `build/stress` (debug),
`-release`, `-asan`, `-stb`, `-stb-asan`, `-wchar16`: 26/27 in each, only
`test_opentype_wild` failing. Zero warnings under `-Werror`. clang-format
clean on every file touched. Reflow benchmark A/B against the tree before, same
sitting, alternating: 462-473 us before, 460-475 us after (it was +6% until
`hpack` got its guarded fast path; D33). `bench_font`: the math face through
the public API 2.9 ms -> 6.2 ms (the strict pass).

## Files outside the lists I was given

`src/units.h` (checked arithmetic), `src/pack.h` (two `PackError` values),
`tests/core_golden.h` (item K), `tests/test_draw.cpp` (given by the lead's
later note), `tests/test_shipout.cpp` and
`tests/test_pack.cpp` (a slug each; the test of a non-box root now expects a
status), `docs/decisions.md` (D33), `docs/divergences.md` (`## stress`).
Helper: one edit in `src/imtypeset.cpp` (`build_material` releases expired
metrics). `include/imtypeset.h` and `src/context.h` are byte-identical to
before.

## Not verified

- 32-bit targets, MSVC, clang, non-x86, FreeType other than 2.12.1, ImGui
  with assertions compiled out. The preset dirs were not built.
- `draw.cpp` and `selection.cpp` with hand-built command lists outside
  MAX_DIMEN (fuzzed through the public API only). `paragraph.cpp` and
  `math_mlist.cpp` keep unchecked sums behind the style/API clamps.
- `test_opentype_wild` is unchanged by the helper's account (stride 8: 359
  equal, 12 refused, 8 mismatched); I saw only that it still fails in the
  same test.
- The helper's installed-font runs, S1/S2/S3/S5 "before" numbers and the
  strict-pass timing are its measurements, not repeated by me (the
  `bench_font` rows are mine).
- No golden was touched or regenerated.

## For later tasks

- **api-revise**: concern posted on the board (fallback status needs a real
  field; `docs/performance.md` growth section is stale; ImGui atlas
  assertion at large type; `tests/test_stress_text.cpp` is yours to update).
- **font-wild**: concern posted on the board (extend `strict_check` with
  every structure you add; `docs/font-support.md` is behind).
- **tex-web-pass**: new code with no TeX name: `Guarded`/`Checked` in
  `src/pack.cpp`, the work budget in `src/linebreak.*`; `set_glue` still
  cites §659 for the zero-stretch case (your item G); `prepare` now takes
  the arena.
- **docs**: `docs/hardening.md` is new; `THIRD-PARTY.md` needs nothing from
  this task.
