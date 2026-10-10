# harden-2: exact additions for the shared documents (for `fold-docs`)

Written by builder-2 (harden-2). Nothing below has been applied. Cross-references written `D<harden>`
are the decision number fold-docs assigns to the `docs/decisions.md` entry in section 1.
Numbers are from this machine (GCC 12.2, x86-64, FreeType 2.12.1, ImGui 1.92.9); "release" is
`-O3 -DNDEBUG -ffp-contract=off -fno-exceptions -fno-rtti`.

---------------------------------------------------------------------------------------------------

## 1. `docs/decisions.md` — append one entry

```markdown
## D<harden>. Hardening, second pass: formulas, selection, the draw loop, and a host that unwinds

Task `harden-2`. The items are a reviewer's reading of `src/mathtext`, `src/math_mlist`,
`src/selection` and `src/draw` (H1 to H8, D3, D4, D6, D7) and five of the lead's (X1 to X5). The
table of what each was, what was observed and what holds it is `docs/hardening.md`, "Second pass".

**Decided**

1. *A formula has a longest source: 524,288 bytes (`MAX_SOURCE_BYTES`, `src/mathtext.h`).* A longer
   one is refused before it is decoded, kind `LAYOUT`, "the formula is too large to set: it is
   longer than 524288 bytes, which is the longest that is read". The reference has no bound. The
   value is the smallest power of two above the reference's own table of hostile sources (450,000
   bytes), which `tests/test_mathtext.cpp` replays at its real sizes and which must go on being
   answered as the reference answers. A smaller bound (64 KiB was tried) is cheaper and would have
   meant rewriting that replay.
2. *"Too large" is not the font's fault.* `MathError::TOO_LARGE` and `TOO_DEEP` become a refusal of
   kind `LAYOUT` whose reason starts "the formula is too large to set: ", not kind `FONT` with "the
   math face could not set it". No new `RefusalKind`: the eighteen kinds are the reference's, and
   `LAYOUT` is its word for a source in the subset that the engine would not set.
3. *No box of a formula is outside ±`MAX_DIMEN`.* `Converter::node_of` (every box `mlist_to_hlist`
   stores) and `set_formula` (the formula's own box) answer `TOO_LARGE` with the dimension in
   `needed` and `MAX_DIMEN` in `largest`; nothing is stored. Refused rather than clamped: a box
   whose stated width is not the width of what it holds is a second defect. No new `MathError`:
   `tests/math_golden.h` switches over the enum.
4. *A reason is a C string.* A reason quotes the source, and the source may hold U+0000 (a
   character like any other to the parser, as to the reference). Where a reason enters a public
   struct (`ImTypesetFormula::Reason`, `ImTypesetErrorInfo::Message`) each NUL is written as the
   four characters `\x00`, Python's spelling (`c_string_reason`). `detail::Refusal::reason` is
   unchanged and still the reference's byte for byte, NUL included. The alternative, a length
   beside every `const char*`, was not taken: it changes two public structs for one character.
5. *The proportion in a layout refusal is written without `printf("%.2f")`*, whose decimal
   separator is the host's `LC_NUMERIC`. `quotient_to_two_places` forms the same double the
   reference does and writes its exact value to two places, ties to even, with integers.
6. *`SelectionModel` takes any buffer and any `TextData`.* Coordinates read from a command buffer
   are held to a quarter of `SCALED_LIMIT` (2^60), so no sum or difference the model forms can
   overflow. Each `TextData` array is checked for order once per `text()` call and searched entry
   by entry if it is out of order: same answer, more time. The contract ("sorted, no index twice")
   stands as the fast path; nothing asserts.
7. *The debug overlay's outlines are the library's own rectangles*, one pixel thick, not ImGui's
   `AddRect`/`AddLine` (whose geometry is anti-aliased, depends on the list's flags and changed in
   ImGui 1.93). They are written into buffers too, which used to leave them out. The two debug
   goldens are compared at the calls (`ops`) and no longer at the vertices: own rectangles cannot
   equal `AddRect`'s (136 and 1,324 vertices against the reference's 120 and 1,268).
8. *The draw loop keeps three `[Internal]` members of `ImDrawList` for glyph quads*
   (`_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx`), and reads `_CmdHeader.ClipRect`. Measured
   (X3, `docs/performance.md`): every quad through the public `PrimRectUV` is 15 % slower for a
   draw into a list; through the public inline `PrimWriteVtx`/`PrimWriteIdx` 3 % slower, and that
   route still has to read `_VtxCurrentIdx`. Neither is within the 1 % noise of the measurement, so
   by the lead's ruling they stay. Rectangles do go through the public `PrimRect`, which is also
   what makes them take the white texel of the list's own shared data (D3). `_CmdHeader.ClipRect`
   stays because the public `GetClipRectMin()` reads `_ClipRectStack.back()`, which is undefined on
   a list whose clip stack is empty, and `AddText` itself reads `_CmdHeader`.
9. *Batched reservation stays (D4).* Past 64k vertices with 16-bit indices a draw command ends up
   to 1,024 quads earlier than with a reservation per glyph. Same vertices, same triangles
   (tested); a different place where one `ImDrawCmd` stops. Reproducing the reference's boundary
   would need the list's vertex counter before every quad and ImGui's own order of reserve and
   cull, for something no renderer shows.
10. *`DrawRules`: `reference()` and `product()` for the draw loop* (`src/draw.h`), the same pattern
    as the layout rules. One rule so far, `rule_joins_span` (D7): under `product()`, which the
    public API draws with (`ImTypesetContext::draw_rules`), a rule inside a formula belongs to the
    formula's rigid span. Its left end is placed from the nearest glyph of the formula placed
    before it, its right end from the nearest placed before or after it (64 glyphs each way, by
    origin), each by the gap in scaled points rounded once; top and bottom from the span's
    baseline; the span goes on. The ends are settled when the span ends, when the glyphs after the
    rule have been placed. Under `reference()`, the default of `DrawArgs` and what every golden
    replay runs under, a rule is snapped alone as the prototype does. No golden changed.
    The first form of the rule placed both ends from the glyph placed before the rule. That is
    right for a radical's bar and wrong for a fraction's, whose last numerator glyph is at the
    bar's far end: `py-host`'s builder saw the bar of the quadratic formula stand 3 px off its
    numerator, and the form above is the correction. Left end from earlier glyphs only, because a
    radicand's first glyph begins nearer the bar's start than the sign's origin does, and the
    joint that shows is the sign's.
11. *The library is safe to unwind through, without catching anything.* A host's `IM_ASSERT` may
    throw (imgui_bundle's Python build), and with exceptions enabled so may `operator new`. The
    guarantee is the basic one at every public entry: nothing leaks, the draw list holds whole
    quads, the widget cache counts what it holds, and the same call made again does what it would
    have done. Held by ordering and by destructors (`Building` in `src/imtypeset.cpp`,
    `std::unique_ptr` until ownership has passed, the pool's room reserved when the context is
    made); there is no `try` or `catch` in `src/`. In the draw loop nothing fallible happens
    between a `PrimReserve` and its `PrimUnreserve`: every glyph is loaded before anything is
    reserved, so there is nothing to guard there (a guard object was tried and measured, not kept).
12. *Not done, on purpose (X4):* trusting the content hash instead of comparing the text (the one
    thing that would bring a content-keyed widget call to the cost of a pointer-keyed one), a
    one-entry memo of `resolve_style`, and a flat table in place of `std::unordered_map` for the
    widget cache. The first changes a guarantee and is the user's call; the other two are worth
    some tens of nanoseconds of a call by the profile and were not tried.

**Measured**

- Parsing a run of `x` asks for 208 bytes of heap per byte of source (109 MB at the bound).
- H1 before the bound: a 100 MB source passed 2.5 GB resident and was still growing when killed
  (the lead's measurement); at 524,289 bytes the unbounded parser simply parsed it.
- D7 before the rule, 576 draws (6 sizes, 0 to 60 terms before `\sqrt{x}`, 16 sub-pixel origins):
  the bar's left edge was from 3 px inside the sign's quad to 3 px clear of it, its top from 1 px
  above the sign's to 1 px below. After: the same offset across in all 576, within a pixel down.
- D7 on fractions, 144 draws under each set of rules (four fractions, one of them the quadratic
  formula; 13, 17 and 24 px; 0, 13 and 40 terms before; four origins): against the glyph at that
  end as it was drawn, a fraction bar's end is up to 6.00 px from where the commands put it under
  `reference()` and at most 1.00 px under `product()`; a radical's bar inside the numerator 4.91 px
  and 0.42 px from its sign.
- Pictures under both rules were rendered with the software renderer and looked at
  (`IMTYPESET_HARDEN_SHOTS`): under the reference's, a radical sign plainly apart from its bar at
  13 px after sixty terms and by a pixel at 24 px after thirteen, and after forty terms the
  quadratic formula's numerator 5 px right of its bar with the sign printed over the `b`; under the
  product's, the same fraction whether nothing or forty terms come before it.
- D6 by a run: an atlas capped at 512 x 1024, 120 frames each at another size out of 27: 224 bakes
  discarded, the texture replaced under a draw in 24 frames, and all 11,040 quads on a glyph
  rectangle of the atlas as it then stood.
- X1: allocation-failure sweeps unwound `SetParagraphText` + `LayoutParagraph` from 148 points,
  `DrawParagraph` from 49, `TextUnformatted` from 145, `AddFontFromFileTTF` from 84 (sampled past
  the 48th), `CreateContext`/`CreateParagraph` from 5; ImGui's "Out of texture memory" assertion was
  thrown from under `DrawParagraph` and under `TextUnformatted`. ASan + UBSan + LeakSanitizer: no
  report.
- The library and tests also build, with no warning in this tree's sources, against the bundle's
  real configuration header (`../imgui_bundle/imgui_bundle_cmake/imgui_bundle_config.h` with
  `IMGUI_BUNDLE_BUILD_PYTHON`: `IM_ASSERT` throws `std::runtime_error`, `ImDrawIdx` is 32 bits);
  `smoke`, `test_api`, `test_draw`, `test_harden` pass there. `../imgui_bundle/litgen_imconfig.h`
  is ImGui's stock `imconfig.h` with nothing enabled but a Windows `IMGUI_API`; it needs nothing.

**Read, not run**

- That nothing between `PrimReserve` and `PrimUnreserve` in `Drawer::emit` can throw: read from
  `ImDrawList::PrimRect` in the pinned ImGui (no assertion) and from the loop (no container of
  ImGui's indexed, no font asked).
- ImGui's own state after it throws "Out of texture memory" from inside its atlas: the test holds
  that this library and the draw list are whole and that a later, smaller text is laid out; it
  does not hold that the atlas itself is as it was.

**Not measured / not verified**

- MSVC, clang, Windows. ImGui 1.93 (the overlay change is meant for it and was not built against
  it here).
- An exception thrown by anything but `operator new` and `IM_ASSERT`, and one thrown inside a
  destructor (ImGui's own destructors assert, and a throwing `IM_ASSERT` there is `std::terminate`:
  GCC warns of four such sites in ImGui's sources).
- `FontLibrary` and the font loader under allocation failure beyond what the `AddFontFromFileTTF`
  sweep reaches (those files belong to `font-wild-finish`).
```

---------------------------------------------------------------------------------------------------

## 2. `docs/hardening.md`

### 2a. In the `## Draw` table, REPLACE the rows `D6`, `D3`, `D4`, `D7` with:

```markdown
| D3 | A draw list on shared data of its own, white texel at (0.25, 0.75), drawn into through `Drawer::draw_runs` with a fill, a highlight and the overlay: every rectangle carried the **current context's** white texel. | Rectangles are written through ImGui's public `PrimRect`, which reads the texel from the list's own shared data. No `imgui_internal.h` in `src/draw.cpp`. Buffers have no list to ask and keep the current context's. | `tests/test_harden.cpp` `d3_a_rectangle_takes_the_white_texel_of_the_list_it_is_written_to` |
| D4 | 40,000 rectangles (160,000 vertices), 16-bit indices, `ImDrawListFlags_AllowVtxOffset`: against a reservation per rectangle the vertices are identical and so is every triangle once its command's `VtxOffset` is added; a draw command ends **up to 94 quads earlier** (bound: 1,024). | Kept, and stated at `BATCH_QUADS` and in `src/draw.h`. | `d4_past_64k_vertices_the_triangles_are_those_of_a_reservation_per_quad` |
| D6 | **Settled by a run.** Atlas capped at 512 x 1024; one paragraph in two faces drawn at another of 27 sizes each frame for 120 frames: 224 bakes discarded, the texture replaced under a draw in 24 frames, **every one of 11,040 quads on a glyph rectangle of the atlas as it stood after the draw**, under ASan. As read before: a bake used this frame is never discarded (`ImFontAtlasBuildDiscardBakes` is called with 1 or 2 frames). An atlas too small for one frame's glyphs is ImGui's assertion ("Out of texture memory"), from under the draw's glyph load, before anything is reserved. | None needed. | `d6_an_atlas_that_fills_and_is_repacked_during_a_draw_leaves_every_quad_on_its_glyph`; the assertion in `tests/test_exception_safety.cpp` |
| D7 | `$a+b+...+\sqrt{x}$` with 0 to 60 terms before the root, six sizes, sixteen sub-pixel origins (576 draws): the bar's left edge **from 3 px inside the sign's quad to 3 px clear of it**, growing with the number of glyphs before the sign; rendered and looked at, the sign stands visibly apart from its bar at 13 px after sixty terms, and by one pixel at 24 px after thirteen. Shared with the reference: a formula's glyphs carry the rounding of every gap since the span's first, and a rule is snapped on its own. The same for a fraction: under the reference's rules its bar's ends are **up to 6 px** from the numerator's as drawn after forty terms. | `DrawRules::rule_joins_span`, on under `product()` (the public API), off under `reference()` (the goldens): a formula's rule joins the formula's span, its left end placed from the nearest earlier glyph and its right end from the nearest glyph, and does not end the span (D<harden>, item 10). | `d7_a_radical_sign_meets_its_bar_however_far_along_a_formula_it_stands` (576 draws, one offset across, within a pixel down); `d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn` (144 draws under each set of rules: within 1 px, and a radical inside the numerator within half); `d7_a_rule_in_a_rigid_span_is_snapped_alone_by_the_reference_and_with_the_span_here` (both rules on a radical, a fraction and a rule alone, worked by hand); `d7_pictures_...` writes the enlarged joint when `IMTYPESET_HARDEN_SHOTS` names a directory |
```

### 2b. Before `## Counts`, ADD a section:

```markdown
## Second pass: formulas, selection, and a host that unwinds

A fourth review (`.concerns/review-math-selection--reviewer--findings.md`, items H1 to H8) and the
lead's X1 to X5. Tests are in `tests/test_harden.cpp` unless a row says otherwise; "observed" is the
first run of each against the tree as it stood, Debug with ASan + UBSan, every run under
`ASAN_OPTIONS=hard_rss_limit_mb=...` (H1's first repro is what the rule is for).

| id | input, and what it broke (observed) | fix | test |
|---|---|---|---|
| H1 | 524,289 and 1,048,576 bytes of `x`: **parsed**; a list of 2^20 + 1 atoms: refused as kind **`FONT`**, "the math face could not set it: the math list is too large". (100 MB of `x`: past 2.5 GB resident and climbing when killed. Do not run that.) | `MAX_SOURCE_BYTES` = 524,288, refused in `parse` before decoding; `TOO_LARGE` and `TOO_DEEP` are kind `LAYOUT`, "the formula is too large to set: ...". | `h1_a_source_past_the_stated_length_is_refused_before_it_is_read` (also holds the cost at the bound under 512 bytes of heap per byte; 208 measured), `h1_a_list_the_engine_finds_too_large_is_not_blamed_on_the_font`, `h1_the_longest_sources_...`, `h1_through_the_public_api_...` |
| H2 | 25,000 x `\qquad` at 12 pt: **set, box width 36 x `MAX_DIMEN`**, stored; through the public API a BOX node wider than `MAX_DIMEN` in the paragraph. No sanitiser report followed from it in the breaker or ship-out. | Refused (`TOO_LARGE`, kind `LAYOUT`) in `set_formula` and, for a box inside the formula that negative spaces then cancel, in `Converter::node_of`. | `h2_a_formula_wider_than_max_dimen_is_refused_and_not_stored`, `h2_through_the_public_api_...`; `tests/test_math_mlist.cpp` `math_glue_stays_inside_the_dimension_invariant` now expects the refusal for a fraction rule 2^60 thick, which it used to accept |
| H3 | `LC_NUMERIC=de_DE.UTF-8`: "needs a radical sign **1,25** times as tall". | `quotient_to_two_places`: integers, no `printf` of a double. | `h3_a_layout_refusal_writes_its_proportion_with_a_point_in_every_locale`: eight cases by hand, then about 314,000 quotients (every exact half of a hundredth up to 20 among them) against `printf` in the C locale, then the same under a decimal comma if the machine has one |
| H4 | Nine sources with a NUL where the parser quotes what it read: `ImTypesetFormula::Reason` read **`\`** where the reason is `\<NUL> is not in the subset`. | `c_string_reason` where a reason enters a public struct: `\x00`. | `h4_no_reason_carries_a_nul_or_ends_early_in_the_public_structs` |
| H5 | A command buffer with coordinates at both ends of `int64`: **UBSan: signed integer overflow at 12 sites** in `SelectionModel::build`, `beside`, `fragment_at`, `boundary_in`, `rects`. `TextData` arrays given in descending order: **`"abd egh ijkl"` where the same data in order copies `"Abd\n\ne$x$gh\tiJkL"`**. | `held()` on what `build` reads; `ByIndex`/`Breaks` check the order and search either way. | `h5_a_buffer_with_coordinates_at_the_ends_of_the_number_line_...`, `h5_text_data_given_out_of_order_copies_as_it_does_in_order`, `h5_what_the_public_api_hands_the_selection_model_is_in_order` |
| H6 | 24 x `\frac`, ` \sin`, 24 x ` x`: **a list 50 levels deep**, where the header and the test said 49. | The argument in `src/mathtext.h` is `2 * MAX_DEPTH + 2`, which the `static_assert` already said. | `tests/test_mathtext.cpp`: depth is measured over every field of every item (`list_depth`); `the_deepest_list_...` holds 50; `no_list_the_parser_lets_through_is_deeper_than_the_stated_bound` tries 1,620 combinations of openers and innermost pieces (188 parse) |
| H7 | `replay_formulas` asserted `set > 1500` and printed the rest. | `set == 1950`, `refusals == 21`, `by_hand == 66`, each exact. `arbitrary_sources_...` now also makes one source in 32 of up to 2,000 pieces that go together (937, all of which parse and set). | `tests/test_mathtext.cpp` |
| H8 | No test set `LC_NUMERIC`. | — | `h8_a_paragraph_is_laid_out_the_same_under_a_decimal_comma` (passes before and after: `decimal_of` prints and parses in the same locale); `h3` above |
| X1 | `IM_ASSERT` defined to throw, exceptions enabled, `operator new` made to fail at its n-th call: `CreateParagraph` **leaked the paragraph** (LeakSanitizer); a paragraph unwound in mid-layout **shipped 3 commands of 215 from then on** (`has_layout` was set before the layout was made, and likewise `has_material`, `has_lines`, `has_model`); `AddFontFromFileTTF` unwound from the registry **left a font in the atlas that no role named**. Read, not reproduced: a widget's paragraph leaked if the cache's map could not take it; a widget text that grew by a suffix taken for unchanged after `set_text` was cut short. | `Building`, `std::unique_ptr`, the pool's room reserved, `InAtlas`, `set_text` invalidates first and states its size last. No `try`/`catch`. | `tests/test_exception_safety.cpp`, 7 tests, in a build with `-DIMTYPESET_ENABLE_EXCEPTIONS=ON -DIMTYPESET_IMGUI_USER_CONFIG=<repo>/tests/imconfig_throwing.h`; one test saying so in every other build |
| X2 | The overlay's line boxes and baselines went through `AddRect`/`AddLine`. | Own rectangles (decision above, item 7). | `d3_...` pins the geometry; `tests/test_draw.cpp` replays the overlay as the library defines it and compares the two debug goldens at the calls |
| X3 | — | Measured; the members stay (decision item 8). | `docs/performance.md` |
| X4 | `gprof` over `bench_draw --only x100`: with 3,400 paragraphs shown against a cache limit of 2,048, `evict` **went over the whole arrival list every frame** (3,400 relinks) for nothing: everything in it is in use. | Two counts (`submitted_now`, `submitted_before`) say whether anything can be dropped; if not, the list is left to its sixteen-a-frame clock. | `tests/test_api.cpp` `a_steady_frame_larger_than_the_cache_limit_does_not_go_over_the_whole_cache` (fails against the old condition) |
| X5 | `examples/demo_sdl2/main.cpp` called `EnableMath()` and the old `Text(text, nullptr, 0)`. | The two edits the api-revise report gives. | Built with `IMTYPESET_DEMO_WITH_LIBRARY=ON` against `../example-project`'s SDL2 and run under SDL's `dummy` driver: `--page paragraph --frames 3 --screenshot`, exit 0, 22,692 dark pixels in the frame |

Four premises in the test file as this task found it were wrong, and were corrected in the test
rather than in the library: a `\qquad` is not exactly 24 pt at 12 pt (the face makes 36 mu of it);
an original of no bytes copies as nothing, not as a space; malformed UTF-8 is drawn and copied as
U+FFFD, as `include/imtypeset.h` says; and two consecutive hard breaks copy as one newline (see
"Not verified").
```

### 2c. In `## Not verified`, ADD:

```markdown
- Two newlines in a row under `ImTypesetTextFlags_HardBreaks` are one line break, drawn and
  copied: the blank line between them is not kept (`src/paragraph.cpp` drops a hard break at the
  index the last one was at, as the reference's `_after_hard_breaks` does). Seen while writing
  `h5_what_the_public_api_...`; not looked into further. `include/imtypeset.h` says "one per
  newline", which a host will read as keeping blank lines.
- How far a denominator can stand from the middle of its bar under `DrawRules::product()`: it is
  placed from the numerator's last glyph, as every glyph is placed from the one before it, so by
  as much as the numerator's own gaps rounded away from end to end. Looked at for one wide
  denominator and the quadratic formula; no number was put on it.
- Whether a list of 524,288 source bytes can reach `MAX_MATH_ITEMS` (2^20). Nine dense patterns at
  the bound were tried and none does; no argument was made that none can. If one does, the answer
  is the `LAYOUT` refusal of H1, which is what the bound is for.
- X1 under anything but GCC 12 with ASan; see the decision's own list.
```

### 2d. `## Counts` — the numbers this task adds (fold-docs: add to whatever that section totals):
`tests/test_harden.cpp` 21 tests (326,071 checks under FreeType); `tests/test_exception_safety.cpp`
7 tests in the throwing build (52,730 checks) and 1 elsewhere; `tests/test_mathtext.cpp` 17 tests
(was 16); `tests/test_api.cpp` 40 tests (was 39).

---------------------------------------------------------------------------------------------------

## 3. `docs/divergences.md`

### 3a. Under `## draw`, ADD to the list of what the port does that the reference does not:

```markdown
- **The debug overlay's outlines are rectangles, not `AddRect` and `AddLine`.** The reference
  draws a line's box with `ImDrawList::AddRect` and its baseline with `AddLine`; ImGui makes
  anti-aliased polylines of them, whose vertices depend on the list's flags and are not the same
  in ImGui 1.93 as in 1.92.9. The port writes the frame of the box as up to four rectangles one
  pixel thick, inside it, and the baseline as the row of pixels below it — the same pixels'
  worth of outline, hard-edged, under every version and into buffers as well as lists. The calls
  (`ops`) are still the reference's and are compared; of the two cases of
  `tests/golden/draw/draw.golden` that have `debug 1`, `span-edges-by-hand-debug` and
  `debug-overlay`, the `v`, `tri` and `drawcmd` records (120 + 1,268 vertices, 68 + 662 triangles,
  one draw command each) are no longer compared, because they hold what `AddRect` and `AddLine`
  made (the port's lists have 136 and 1,324 vertices). Their `stats`, `keys`, `boxes` and `ops`
  records (`text`, `rectfilled`, `rect`, `linesegment`) are compared as before, and so is every
  record of the other twenty cases.
- **Under `DrawRules::product()`, a rule inside a rigid span belongs to the span.** The reference
  snaps every rule by its own corners and lets it end the span, so the glyphs of a formula —
  placed by adding rounded gaps from the span's first — drift from its fraction bars and radical
  bars: a radical sign 3 px from its bar after sixty terms at 13 px to the em, a numerator 5 px
  off its fraction bar after forty at 17. With the rule on (the public API), a rule in a formula
  is placed from the formula's glyphs — its left end from the nearest one placed before it, its
  right end from the nearest one — by the gap in scaled points rounded once, and from the span's
  baseline, and the span goes on through it; so the glyphs after a rule in a formula are also not
  where the reference puts them (it starts a new span there). With it off (`reference()`, the
  default of `DrawArgs`, every golden replay) the port does what the reference does. Worked by
  hand in `tests/test_harden.cpp`.
- **Past 64k vertices with 16-bit indices a draw command ends earlier** than the reference's, by
  up to 1,024 quads: space is reserved for a run of quads at a time. The vertices and the
  triangles are the same (`tests/test_harden.cpp`, `d4_...`).
- **Rectangles take the white texel of the list they are written to** (`PrimRect`), as the
  reference's `AddRectFilled` does; glyph quads are written directly.
```

### 3b. Under `## mathtext`, REPLACE item 5 ("The catch-all") with:

```markdown
5. *The catch-all, and what is too large.* The reference's last `except` gives `the math face could
   not set it: <ExceptionType>: <message>`, kind `FONT`. Here the same prefix is followed by
   `math_error_message`, with no type name, for `NOT_IMPLEMENTED` and `MALFORMED`, which the parser
   emits no list for. `TOO_DEEP` and `TOO_LARGE` are this port's own bounds and are not the font's
   doing: kind `LAYOUT`, "the formula is too large to set: ...". The parser cannot reach `TOO_DEEP`
   — the deepest list it lets through (24 `\frac`s, each the argument of the one before, with an
   operator name innermost) nests 50 levels against a bound of 64, and a test sets it — but a
   source it reads can reach `TOO_LARGE`: a row of `\qquad` wider than `MAX_DIMEN` is a box no
   node may hold, and is refused where the reference sets it.
```

### 3c. Under `## mathtext`, ADD items:

```markdown
7. *A source has a longest length.* `parse` refuses a source of more than 524,288 bytes
   (`MAX_SOURCE_BYTES`) before decoding it: kind `LAYOUT`, position 0. The reference reads any
   length; its own hostile-source table (up to 450,000 bytes) is inside the bound and is answered
   as it answers.
8. *The proportion in a layout refusal does not follow the C locale.* Python's `:.2f` never does;
   `printf` does. The digits are the same as the reference's for every quotient tried (about 314,000,
   every exact half of a hundredth up to 20 among them).
9. *A NUL in a reason.* `Refusal::reason` carries it, as the reference's string does. The public
   structs spell it `\x00`.
```

### 3d. Under `## api-revise` (or `## public-api`), ADD:

```markdown
- **A formula's refusal reason and an error's message never hold a NUL**: each is written `\x00`
  (the reference's reason holds the character itself).
- **The widget cache does not go over its whole list in a frame in which nothing can be dropped**
  (no counterpart in the reference, which has no such cache).
```

---------------------------------------------------------------------------------------------------

## 4. `docs/performance.md` — ADD a section before `## Not measured`

```markdown
## After the second hardening pass (task `harden-2`)

Release, GCC 12.2, the same machine as above, with two other builders at work on it (load average
2 to 4). Every number is the minimum over three runs, the binaries compared being run in turn
(a, b, c, a, b, c, ...); rows that no change touches moved by about 1 % between those runs, which
is the noise to read the rest against.

### X3: writing a quad through ImGui's public functions

`bench_draw --only anatomy`, the row "sample: Drawer into a draw list" (3,918 glyphs, one key), us.
Taken on the loop as it stood half way through the task, the three differing in nothing else:

| how a glyph's quad is written | min of 3 | against the first row |
|---|---|---|
| own pointers into the reserved space; `_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx` set when a batch ends (kept) | 70.2 | — |
| `ImDrawList::PrimRectUV` per quad (public, not inline) | 80.9 | +15 % (2.7 ns a glyph) |
| `PrimWriteIdx` x 6 and `PrimWriteVtx` x 4 per quad (public, inline; still reads `_VtxCurrentIdx`) | 72.5 | +3 % (0.6 ns a glyph) |

The members stay.

### What this task cost the draw loop

The same section, before the task and at its end, us:

| row | before | after | |
|---|---|---|---|
| sample: placement alone, no vertices | 41.5 | 43.0 | +0.4 ns a glyph |
| sample: Drawer into a draw list | 66.6 | 71.7 | +8 %, 1.3 ns a glyph |
| sample: Drawer into buffers | 68.8 | 68.1 | -1 % |
| full draw, x1 (the public path) | 102 to 107 | 112 | one run each, hours apart: not a comparison |

Placement now asks of every glyph whether it is a formula's (`DrawRules::rule_joins_span`). The
emit loop now also writes the overlay's outlines and sends rectangles through `PrimRect`; with that
in it GCC's code for the glyph path into a list is about 0.9 ns a glyph slower however the rest
was arranged (a cursor held by value, the test for a glyph first, a guard object or none: 67.9 to
74.2 over seven arrangements, none back to 66.6), while the same loop into buffers got faster.
Not chased further; it is the first thing to look at if a host needs the nanosecond back.

### X4: where a widget call's time goes

`gprof` (`-O3 -pg`) over `bench_draw --only x100` and `--only x10`; self time per call, which
`-pg` inflates and which rests on few samples at x10: read as proportions.

| function | x10: 340 calls a frame | x100: 3,400 calls a frame |
|---|---|---|
| `text_widget` itself (hash, compare, the paragraph's fields) | 44 ns | 194 ns |
| `widget_slot` (the cache's `std::unordered_map`) | 22 ns | 145 ns |
| `evict` | below one sample | **139 ns** |
| `resolve_style` | 37 ns | 32 ns |
| ImGui's `ItemSize`, `ItemAdd`, `IsRectVisible` | about 15 ns | about 23 ns |

At 3,400 calls the cache is over its limit of 2,048 paragraphs with every one of them in use, and
`evict` went over the whole list at the first call of every frame: 3,400 unlinks and links, each a
few cache misses. That is now skipped when the two frames' counts show nothing can be dropped. The
rest of the growth from x10 to x100 is the same code missing the cache: the paragraph's fields, the
map's nodes, and the text itself, which is read twice a call (hashed where the host keeps it,
compared where the cache keeps it) at about 600 bytes a paragraph — 4 MB a frame at x100.

"widget frame", us, before and after the change to `evict`, three rounds in turn:

| row | before | after | |
|---|---|---|---|
| x100, by content | 1544, 1558, 1562 | 1314, 1335, 1334 | -15 % |
| x100, `TextWithID` | 1503, 1470, 1515 | 1299, 1282, 1280 | -14 % |
| x10, by content | 112.5, 111.1, 111.8 | 112.5, 112.6, 113.5 | unchanged: under the limit |
| x10, `TextWithID` | 105.6, 104.5, 105.2 | 106.2, 105.6, 106.6 | unchanged |

(The x100 rows read 935 and 901 in the one run made before this task began, on a quieter machine
and with a bimodal spread, minimum 935 and median 1,589; they are not comparable with the table,
whose two columns were taken minutes apart.)

What a content-keyed call costs over one keyed by an ID is the hash: 6 to 7 us of 340 calls at
x10, about 20 ns a call at 600 bytes. The comparison of the text is paid by both. What is left of
the difference from a pointer-keyed call is that comparison and the cache misses above; see the
decision for what was not done about them.
```

In `## Not measured`, ADD:

```markdown
- The draw loop against ImGui 1.93.
- What a one-entry memo of `resolve_style` or a flat table for the widget cache would save.
```
