# Hardening: what broke, what was done, what holds it

A library offered to other people's programs meets fonts and text nobody
vetted. Two reviewers read the code and handed over located lists
(`.concerns/review-layout-core--reviewer-3--findings.md`, items A to F and K;
`.concerns/review-font-stack--reviewer-2--findings.md`, items S1 to S9); the
`bench` task measured the growth of total fit. This file is what became of
each item, and of what the fuzzers found afterwards.

The method, per item: write the smallest input that reproduces it and run it
under ASan + UBSan against the tree as it stood; if it does not reproduce,
say what was tried and change nothing; fix at the cause; keep the input as a
test. "Observed" below is what that first run printed, against the sources
snapshotted before any change (GCC 12.2, x86-64, Debug, `-fsanitize=address,undefined`).

The decisions taken along the way are D33 in `docs/decisions.md`; what now
differs from the reference is `## stress` in `docs/divergences.md`.

## Layout core

Tests are in `tests/test_stress_layout.cpp` unless a row says otherwise.

| id | input, and what it broke (observed) | fix | test |
|---|---|---|---|
| A | A BOX node whose index is the box about to be made, given to `add_box` as its contents (the reviewer's snippet), then `ship`. No contract fired; **ASan: stack-overflow** in `NodeArena::holds` under `hlist_out`/`descend`. The same through `add_disc`, and through a node kept across `rewind`. | `NodeArena::add_box` and `push_disc` refuse contents that name a box or discretionary the arena does not hold yet and store none (`nesting_of` answers `NAMES_NOTHING`, one pass with the nesting it already computed). `ship`, `import` and `deep_equal` also count their own depth, because a `Mark` made by hand can still close a cycle around the arena: `ShipError::TOO_DEEP`. | `item_a_a_box_cannot_be_made_to_contain_itself`, `item_a_a_node_kept_across_a_rewind_cannot_close_a_cycle`, `item_a_a_cycle_made_around_the_arena_is_walked_to_a_bound` |
| B | 20 levels of a box named twice (42 arena nodes), `ship` with the default `max_items`: no error, **2,097,153 commands, 134 MB, 1.0 s**. 32 levels is 256 GB. | The default is proportional to what was handed over: 16 visits per node of the arena and the list, plus 65,536 (`DEFAULT_SHIP_VISITS_PER_NODE`, `DEFAULT_SHIP_VISITS_FLOOR`), then `TOO_MANY_ITEMS`. Not exactly one visit per node, which the review proposed: `tests/test_shipout.cpp` and `tests/test_paragraph.cpp` each ship a list that names one box several times, and failed with it. | `item_b_the_default_bound_of_ship_follows_the_size_of_the_tree` |
| C | The same 20-level tree: `import` made **4,194,302 nodes and 2,097,151 boxes from 42 nodes in 4.6 s**; `deep_equal` took 0.6 s. Both double per level. | `import` copies each source box and discretionary once (a memo for the one call), which also keeps the sharing. `deep_equal` remembers which pairs of boxes it has found equal. 40 levels: 41 boxes in, 41 out. | `item_c_import_and_deep_equal_visit_a_shared_box_once`, `item_c_import_and_deep_equal_take_a_stray_index_as_empty` |
| D | Widths of `INT64_MAX` in kerns, glue and rules: **UBSan: signed integer overflow** at 18 sites — `hpack` and `vpack` sums, `set_glue`'s excess, `GlueTotal::add`, ship-out's `cur_h`, `cur_v`, rule thickness and glue width, `ParShape::hanging`. | `add_checked`/`sub_checked` in `src/units.h`: the sum stops at ±`SCALED_MAX` and says so. `hpack`/`vpack` report `PackError::DIMENSION_OVERFLOW` (see D33 for how that costs a well-formed list nothing), `ship` reports `ShipError::DIMENSION_OVERFLOW`, the rest saturate. | `item_d_sums_that_leave_the_range_are_reported_and_defined` |
| D' | Not in the review; the `bench` report pointed at it. A glue node whose order byte is 4: **ASan: stack-buffer-overflow** in `GlueTotal::add` from `hpack` (an order of 200 lands in mapped stack and goes unseen). | Orders are masked into range wherever they index (`GlueTotal`, `add_glue` in `src/pack.cpp`), as the breaker's running totals already did. | `item_d_a_glue_order_outside_the_enum_is_masked` |
| E | Ragged right by total fit, through the public API (`bench_reflow --only growth`, release): **5.9 ms, 106 ms, 1.76 s, 29.7 s** for 1, 4, 16, 64 copies of a 635-node paragraph; justified 0.10, 0.98, 10.6, 142 ms. | A work budget (next section). Past it the paragraph is first fit's and says so. | `item_e_total_fit_stops_at_its_work_budget_and_first_fit_takes_over`, `item_e_below_the_budget_the_breaking_is_the_optimisers`, `item_e_the_budget_counts_the_length_of_a_candidate_that_is_walked`; `a_hundred_thousand_words_are_broken_within_the_work_budget` in `tests/test_stress_text.cpp` |
| F | A BOX node with index 77 in an empty arena: `hpack` error NONE, `ship` error NONE, `ship` of a kern as root NONE, **three contract violations** (an assertion each in a debug build). | `NodeArena::find_box`/`find_disc` for nodes out of somebody else's list. `PackError::NODE_NOT_IN_ARENA`, `ShipError::NODE_NOT_IN_ARENA` and `ROOT_NOT_A_BOX`; the breaker stands an empty rule where such a node was and reports `MALFORMED_LIST`. `NodeArena::box`/`disc` keep their contract for a node the caller made itself. | `item_f_an_index_the_arena_does_not_hold_is_a_status_and_not_an_assertion`; `shipping_something_that_is_not_a_box_of_the_arena_is_an_error` in `tests/test_shipout.cpp` |
| K | Read, not run: `core_golden::token` indexed its tables with `% 3` and `& 3`. | A value outside the enum prints as `<not-a-member>`, which no golden holds. | `item_k_a_corrupt_enum_has_no_legal_token` |
| — | From the public-api report: `interword_glue(MAX_DIMEN, 3000)` answers **width 1,431,655,764 and stretch 1,610,612,736** (`MAX_DIMEN` is 1,073,741,823); with a space width nobody clamped, **UBSan: signed integer overflow** in `stretch * space_factor`. | `interword_glue` in `src/style.cpp` holds the width it is given and the three components it returns to ±`MAX_DIMEN`. | `interword_glue_stays_within_max_dimen` |
| — | From the port-hazards note: `tests/test_linebreak.cpp` replaced one form of `operator new`. A `std::stable_sort` in that executable: **ASan: alloc-dealloc-mismatch (operator new vs free)**. | The full replacement, as `tests/test_paragraph.cpp` has it. | `the_counting_allocator_replaces_every_form` in `tests/test_linebreak.cpp` |
| — | Consolidation ruled for `src/selection.cpp`: its own copy of the `isalnum` and `isspace` tables. | Removed; `is_alnum`/`is_space` read `src/unicode_tables.h`. | `tests/test_selection.cpp` compares both against the golden over every codepoint, as before |

## Draw

A third review (`.concerns/review-draw-para-style--reviewer--findings.md`,
items D1 to D7) arrived while this task was open. Tests are in
`tests/test_draw.cpp`.

| id | input, and what it broke (observed) | fix | test |
|---|---|---|---|
| D1 | `DrawCmd::glyph(FontKey{~0ull}, 'a', ...)` as the first glyph of a run: all ones was `place_commands`' own "no key yet" value, so the glyph never got a key slot. **ASan: SEGV reading `_latin`** (`src/draw.cpp`, the `seen` lookup). Not reachable through `include/imtypeset.h`: `FontKey::make` keeps bit 63 clear. | A flag says whether there has been a key; any bits may be one. | `a_first_glyph_whose_key_is_all_ones_gets_a_key_slot_like_any_other`; the key is also first in `commands_nobody_shipped_draw_nothing_and_break_nothing` |
| D2 | No face bound, key drawn in ImGui's current font, 'a' then U+4E00 (which that font lacks): **glyphs 1, missing 1**; U+4E00 first: glyphs 2. `bake()` filled in the key's font and `has_glyph` then took the key for a bound one. (The review's own example, an em dash, does not show it in this test's stage: the current font there is Roboto, which has one.) | `KeyState::bound` is set once, in `bind`, and is what `has_glyph` and `bake` read: an unbound key is never asked whether it has a codepoint, as the reference never asks. The absent one is drawn as the font's fallback glyph. | `an_unbound_face_draws_every_codepoint_whichever_comes_first` |
| D5 | Tests weaker than they read. | `the_vertices_are_those_of_one_add_text_per_glyph` asserts, when the loader is FreeType, that no glyph box differs and that every case was compared with the reference's buffers (22 of 22), and says it is not gated otherwise. `commands_nobody_shipped_...`: the two assertions that could not fail are replaced by one that can (every index written names a vertex written), and the count of missing glyphs it expects includes D1's key. | as named |
| D3 | A draw list on shared data of its own, white texel at (0.25, 0.75), drawn into through `Drawer::draw_runs` with a fill, a highlight and the overlay: every rectangle carried the **current context's** white texel. | Rectangles are written through ImGui's public `PrimRect`, which reads the texel from the list's own shared data. No `imgui_internal.h` in `src/draw.cpp`. Buffers have no list to ask and keep the current context's. | `tests/test_harden.cpp` `d3_a_rectangle_takes_the_white_texel_of_the_list_it_is_written_to` |
| D4 | 40,000 rectangles (160,000 vertices), 16-bit indices, `ImDrawListFlags_AllowVtxOffset`: against a reservation per rectangle the vertices are identical and so is every triangle once its command's `VtxOffset` is added; a draw command ends **up to 94 quads earlier** (bound: 1,023, one less than `BATCH_QUADS`, D39 item 9; the test allows 1,024). | Kept, and stated at `BATCH_QUADS` and in `src/draw.h`. | `d4_past_64k_vertices_the_triangles_are_those_of_a_reservation_per_quad` |
| D6 | **Settled by a run.** Atlas capped at 512 x 1024; one paragraph in two faces drawn at another of 27 sizes each frame for 120 frames: 224 bakes discarded, the texture replaced under a draw in 24 frames, **every one of 11,040 quads on a glyph rectangle of the atlas as it stood after the draw**, under ASan. As read before: a bake used this frame is never discarded (`ImFontAtlasBuildDiscardBakes` is called with 1 or 2 frames). An atlas too small for one frame's glyphs is ImGui's assertion ("Out of texture memory"), from under the draw's glyph load, before anything is reserved. | None needed. | `d6_an_atlas_that_fills_and_is_repacked_during_a_draw_leaves_every_quad_on_its_glyph`; the assertion in `tests/test_exception_safety.cpp` |
| D7 | `$a+b+...+\sqrt{x}$` with 0 to 60 terms before the root, six sizes, sixteen sub-pixel origins (576 draws): the bar's left edge **from 3 px inside the sign's quad to 3 px clear of it**, growing with the number of glyphs before the sign; rendered and looked at, the sign stands visibly apart from its bar at 13 px after sixty terms, and by one pixel at 24 px after thirteen. Shared with the reference: a formula's glyphs carry the rounding of every gap since the span's first, and a rule is snapped on its own. The same for a fraction: under the reference's rules its bar's ends are **up to 6 px** from the numerator's as drawn after forty terms. | `DrawRules::formula_placed_whole`, on under `product()` (the public API), off under `reference()` (the goldens): every glyph and rule end of a formula is placed from the pixel the formula's left edge was put on, by its own distance from that edge rounded once; a formula's rule takes its top and bottom from the formula's baseline and does not end the span (D47). It replaces `rule_joins_span`, which joined a rule's ends to the glyph nearest along the line and picked glyphs of another row. | `d7_a_radical_sign_meets_its_bar_however_far_along_a_formula_it_stands` (576 draws: the joint the same at every origin, within a pixel of the formula with nothing before the sign, never open); `d7_a_formula_is_placed_glyph_by_glyph_by_the_reference_and_whole_here` (both rule sets worked by hand on a radical, an overline, a fraction, rules that are no formula's, four origins); `d7_a_rule_under_a_wider_row_is_placed_with_its_own_box_and_not_the_row_above` (review-3a's cases 1a, 1c and 3 by hand); `d7_every_rule_of_a_formula_stands_where_it_should_against_the_ink_of_its_own_box` (14 formulas, 2,352 draws under each rule set: ends and centre within 1 px, every item within 0.5 px of its place, one picture at every origin); `d7_pictures_...` writes, when `IMTYPESET_HARDEN_SHOTS` names a directory, 28 pictures with the reference's rules above the product's. `d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn` is gone: its four formulas are in the 14, measured against their own boxes. (`tests/test_harden.cpp`) |

Row D7 is as task `d7-fix` left it (D47); task `draw-seams` then bounded where a formula meets
the text beside it (D50, and `tests/test_word_gaps.cpp`).

## The work budget of total fit

One unit of work is one candidate line judged from the running totals
(`LineBreaker::trial`); a candidate that has to be laid out and walked — a
paragraph with a discretionary, a rule of running width or no measure — costs
one unit and one more per sixteen nodes of it. Counted over all passes of one
paragraph. Measured through the public API with `bench_reflow --only growth`
in a Release build, the budget lifted, on this machine:

| the sample's last paragraph | lines | justified, total fit | work | ragged right, total fit | work |
|---|---|---|---|---|---|
| x1 (635 nodes) | 8 | 0.100 ms | 792 | 5.9 ms | 75,153 |
| x4 | 30-32 | 0.98 ms | 9,676 | 106 ms | 1,363,730 |
| x16 | 120-128 | 10.6 ms | 118,915 | 1.76 s | 22,380,790 |
| x64 | 480-512 | 142 ms | 1,656,882 | 29.7 s | 364,876,340 |

About 80 ns a unit. The budget is `DEFAULT_LINE_BREAK_WORK_BUDGET` = 2^20 =
1,048,576 units, about 85 ms: nine times what a justified paragraph of 120
lines needs, a hundred times the "ordinary long paragraph" (x4). It ends
justified total fit at roughly 400 lines and ragged total fit at roughly 28.
With it, the same rows: justified x64 and ragged x4, x16, x64 come back as
first fit's breaking in the time first fit takes after the first layout
(0.15, 0.60, 2.9 ms), because a paragraph that ran out is not offered to the
optimiser again until its text, fonts or style change; the first layout of
each costs the budget once.

What says so: `Paragraph::budget_exhausted` with `pass_used ==
BreakPass::FIRST_FIT` (`src/linebreak.h`); through the public API,
`GetParagraphError(p)->Message` contains "first fit" while `Code` stays
`ImTypesetError_None` — the header has no status that is not an error, and
giving it one is `api-revise`'s.

Held by: every budget from 0 to two past what a 60-word paragraph needs gives
either first fit's breaking exactly or the optimiser's exactly, never a
partial one; 20 paragraphs broken with the default budget and with the
largest are equal in every line; 20,000 words ragged come back in 1.8 s under
the sanitisers by both ways of judging a candidate; 100,000 words justified
through the public API in 3.3 s under the sanitisers. A paragraph of thousands
of one- and two-letter words reaches the budget far sooner than prose does
(2,500 of them do): more breakpoints to a line.

## Font stack

Tests are in `tests/test_stress_font.cpp` unless a row says otherwise. Run
on x86-64 Linux, GCC 12, the system FreeType 2.12.1, ImGui v1.92.9. Symptoms
marked *read* were not run against the old code.

| id | input, and what it broke (observed) | fix | test |
|---|---|---|---|
| S1 | Roboto with a 2.0 MB GPOS: one Extension lookup, 256 offsets to ONE PairPos format 2 subtable (1000 x 1000 classes). Old code: 256 subtables kept, **+495 MB peak RSS, 7.40 s** (ASan); linear in the count, so about 2 GB at the cap of 1,024. Now: 1 subtable, +3 MB, 0.04 s. | `src/opentype.cpp` `OpenTypeFont::read_gpos`: lookups and subtables are deduplicated by offset; one budget for the whole table (4 units per GPOS byte + 4,096, a unit being a pair or a matrix cell), 32 MiB of per-glyph class storage, subtable offsets examined <= bytes/2; a warning when any of it cuts. | `test_stress_font`: `s1_gpos_subtables_that_share_one_offset_are_read_once`, `s1_gpos_subtables_that_overlap_are_bounded_by_the_table` (1,024 distinct overlapping subtables: 8 kept, 7.9 MB, warning) |
| S2 | Three-glyph font, `loca[1] = 0x00100000`, `loca[2] = 0x00100040`, cmap 'A' -> glyph 1. Registers (empty box, warning). Old `add_face_to_atlas` accepted it; first `FindGlyph('A')` under stb_truetype: **ASan SEGV, READ, `ttSHORT` <- `stbtt_GetGlyphBox` <- `ImGui_ImplStbTrueType_FontBakedLoadGlyph`**. The reviewer's exact shape (`0x7FFFFFF0`/`0x7FFFFFF8`) did **not** reproduce: stb's offset goes negative as an `int` and it takes the glyph for empty. FreeType: no fault with either. | `OpenTypeFont::strict_check(std::string* why) const` (new; `strict_reason`, `strict_glyf`, `strict_glyf_glyph`, `strict_cff`), called by `add_face_to_atlas` (`src/font_loader.cpp`), which refuses with "<name> cannot be given to a rasteriser: <reason>". What it checks is listed in `src/opentype.h`. `add_face_to_atlas` also refuses, instead of reaching an ImGui assertion: null or locked atlas, `RasterizerDensity <= 0`, a file of <= 100 bytes. | `test_stress_font`: `s2_structured_hostile_fonts_end_where_they_should_and_nowhere_worse` (63 cases when this row was written, 146 on the tree of 2026-10-10 by the test's own census line; outcome of each asserted, all taken to a baked glyph), `s2_the_atlas_refuses_a_font_with_a_wild_loca_pair_and_takes_the_same_font_intact`; `test_font_loader`: `a_face_with_a_damaged_glyph_measures_and_is_refused_by_the_atlas` |
| S3 | Roboto (184,260 bytes) plus 1,000 directory records each covering the whole file: old `build_atlas_font` returned ok with **184,445,608 bytes** (1,001x); 11,500 records is the reviewer's 2 GiB. Duplicate tags were kept. | `build_atlas_font`: tables are copied only while their lengths sum to no more than the file, else "the font's tables overlap"; duplicate tags refused. `OpenTypeFont::read_directory`: a tag named twice fails the load ("the table directory names a table twice"). `strict_check` refuses overlap as well. | `s3_overlapping_directory_records_cannot_inflate_the_atlas_copy` (1, 1,000, 11,500 records), `s3_a_table_named_twice_is_refused_by_the_reader` |
| S4 | **Did not reproduce: 64-bit only here.** `cells * record` cannot wrap a 64-bit `size_t`. Tried class counts 65535 x 65535 with a 16-byte record: refused as malformed before and after. | `read_gpos`: the product is formed in `uint64_t` and compared with the bytes left before anything is sized. Correct by construction; never run on a 32-bit target. | `s4_gpos_class_counts_whose_product_passes_32_bits_are_refused` |
| S5 | Measured before changing: a 768-byte CFF font (ten levels of subroutine, fan-out 5, a leaf of 16 curves per operator): one glyph's ink box **0.871 s** (ASan) before the budget stopped it. Now 0.165 s (ASan), 0.02 s (release), and the failure is remembered per glyph. | `CffState::move/line/curve` each cost one unit of `CFF_WORK_BUDGET` (was operators only). The strict pass runs every charstring with a budget of 65,536 and refuses a font with one that exhausts it. | `s5_cff_work_is_counted_in_segments_not_operators` |
| S6 | A 'kern' feature naming lookup 0 five thousand times, then lookup 1: the cap of 4,096 was applied while collecting, so lookup 1 was never read (*read*, not run on the old code). | `read_gpos`: lookups are collected into a set (feature tables may overlap: indices read are bounded by bytes/2), then capped at `MAX_KERN_LOOKUPS` with the warning "... name more lookups than are read". | `s6_the_kern_lookup_cap_counts_distinct_lookups_and_warns_when_it_cuts` |
| S7 | A face with 5 minted codepoints, 10 supplementary ones of its own and 3 free private-use slots: ascending order gave all 3 to U+1D400.., none to the minted ones, silently (*read*). | `FaceData::build_stand_ins` (`src/fontlib.cpp`): free slots are counted first; minted codepoints are served first in mint order, then the cmap's supplementary codepoints ascending; `FaceData::atlas_stand_ins_unserved()` and one line in `FaceData::warnings()` (both new). Stand-ins still ascend with the codepoint stood in for, so the 16-bit goldens are unchanged. | `s7_stand_ins_serve_minted_codepoints_first_and_report_the_shortfall` (3 free slots, none, enough, and 7,000 minted against 6,400 slots; each also through the whole chain) |
| S8 | Tests that printed. | `test_font_loader`: `glyph_boxes_match_the_reference_rasteriser_under_freetype` (renamed) gates `differing == 0` and ascent/descent equal under FreeType, says it is not gated under stb, and requires the exact box count in both ImWchar widths (7,510; 7,505 at 16 bits). `test_opentype`: `names.size() == 7`. The `add_face_to_atlas` failure paths now run (strict refusal, null atlas, locked atlas, bad density; ImGui's own refusal runs under FreeType in the fuzz, 21 times in the 10x run). | as named |
| S9 | One object per exact size, never freed (*read*; RSS not measured on the old code). | `FontLibrary::metrics` (`src/fontlib.cpp`): two generations of `METRICS_SIZES_KEPT` = 256 sizes per role; a size not asked for across that many admissions expires. **Expired objects are not freed by `metrics()`**: only by `release_expired_metrics()`, which the owner calls when it holds no pointer. Contract in `src/fontlib.h`. One edit outside my files makes the context call it (below). | `test_fontlib`: `the_metrics_cache_is_bounded_and_no_pointer_dies_before_it_is_released` (100,000 sizes for PROSE, 5,000 for MATH because its chains cost too much under ASan; live <= 512, expired <= 256 between releases; a pointer held across 3 x 256 other sizes with no release still answers) |
| N1 | A 628-byte font whose cmap is one format 13 group over U+0000..U+10FFFF: 1,114,112 ranges of 12 bytes, a **13,369,960-byte** font for the atlas, +70 MB peak RSS (release). | `CmapRange::one_glyph`: the group is one range in the reader, in `FaceData` and in the atlas's cmap, which is written as format 13. Now 1 range, a 628-byte font for the atlas, no measurable time or memory. | `n1_a_format_13_cmap_over_the_whole_code_space_stays_one_range` |
| W-D1 | cmap format 12 group `{first 0x41, last 0x44, glyph 0xFFFFFFFE}`: `lookup` added in 32 bits and answered glyphs 0xFFFFFFFE, 0xFFFFFFFF, **0, 1**; the atlas's cmap left the group out, so the atlas and layout disagreed about what the font has. 27 fonts of the dense fuzz had such a group (release run). | `read_cmap_subtable`: the group is cut at the last codepoint whose id fits, with a warning. | `cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero` (`tests/test_opentype_formats.cpp`); the fuzz asserts no range's ids wrap and no longer sets such fonts aside |
| W-D2 | A `kern` table of 65,535 one-pair subtables: load **0.663 s** against 0.033 s for 16,384 (19.8 times for 4 times the input; release). The cause was a `reserve()` to the exact size at each subtable, which reallocates and moves the whole vector every time, not the sort. | `read_legacy_kern`: no exact `reserve()`. Now 0.001 s release, 0.052 s ASan, 4.1 to 4.2 times for 4 times the input. | `n3_a_kern_table_of_65535_subtables_is_read_in_one_pass` |
| W-D3 | Type 2 charstring `32767 dup mul` seven times over, then `rmoveto`: ink box x **inf**; with `dup sub` after it, **NaN**, and the NaN passed `strict_check` because `std::max` drops a NaN operand and the reach test never saw it. | `CffState::finite`, cleared when the pen is sent to a point that is not finite: the glyph has no box, and `strict_cff` refuses the font ("a CFF charstring's arithmetic puts a point where no number is"). | `type2_arithmetic_that_overflows_gives_no_box_that_is_not_finite` (`tests/test_opentype_formats.cpp`) |
| W-D4 | A simple glyph whose glyf header says yMin -100, yMax 0 and whose outline is a square from 100 to 400: passed the strict pass; stb_truetype sizes the bitmap by the header and fails `STBTT_assert(z->ey >= scan_y_top)` in `stbtt__rasterize_sorted_edges` (4 failures at every size from 8 to 96 px, counted by calling stb_truetype directly). Installed fonts have the shape: NanumBarunGothic's glyph 17929 fails the assertion at 200 and 240 px. | Repaired, not refused (decision D38.5): `strict_glyf_glyph` notes the glyph in `OpenTypeFont::atlas_box_repairs()` and `build_atlas_font` raises the header's yMax over the outline in the atlas's copy. Refused only when no 16-bit yMax holds a composite's bound. | `a_glyph_drawn_above_a_header_box_at_the_baseline_does_not_reach_a_rasteriser`: six shapes baked at 16 and 200 px under whichever rasteriser the build has, and a seventh refused; the fuzz no longer keeps such glyphs from stb_truetype and checks each raised header in the rebuilt font |
| W-K | 16,384 `kern` subtables kerning one pair by 2,000 each: `pair_kern` answers 32,768,000 units, which at 12 pt in a 1000-unit em is a length 24 times `MAX_DIMEN`. The text path clamped it (`rounded_dimen`, `src/style.cpp`); the math path rounds with `round_to_scaled` alone. | `FontMetrics::kern` (`src/fontlib.cpp`) holds the length to ±`MAX_DIMEN`; a value inside the bound is unchanged. | in `n3_...`: both signs, and a pair with no kern still exactly 0 |

### What the strict pass costs

Release, quiet machine: latinmodern-math.otf (4,802 charstrings) 2.85-3.0
ms; the six TrueType faces 0.17-0.27 ms each. It runs once per face in
`add_face_to_atlas` and the answer is remembered. `bench_font`:
`add_face_to_atlas` on the math face 1.24 ms (docs/performance.md) -> 4.15
ms; `register_font` 0.83-0.88 ms (was 0.73-1.1 in that document: unchanged).
Slowest of the installed fonts: UKIJCJK.ttf, 17.8 ms. Through the public API (`bench_font`, same
build): `AddFontFromFileTTF` of the math face 6.2 ms where
`docs/performance.md` has 2.9 ms; all six faces 9.7 ms.

### The metrics cache's contract (S9)

A pointer from `FontLibrary::metrics` is valid until the owner calls
`release_expired_metrics()` or destroys the library, however long it is held.
`metrics()` itself frees nothing: existing holders (`MathFonts::_faces`,
`_last_text_metrics`, `CharMeasureCache::_last_metrics`) keep pointers
indefinitely, so automatic eviction would have been a use-after-free reachable
from the public API. The owner calls the release where it holds none. Never
calling it is safe and as unbounded as before.
`tests/test_stress_text.cpp` `an_animated_zoom_keeps_a_bounded_number_of_metrics`
holds it from the outside: 900 sizes that never repeat, through
`LayoutParagraph` with a formula, leave the count of live blocks level
(3,734, 4,286, 3,937 at the half, three quarters and end), and ASan sees no
read of anything released.

## Fuzz census

Three executables, each a deterministic xorshift sequence from fixed seeds.
`IMTYPESET_STRESS_ITERS=n` multiplies the iteration counts; n = 1 is what
`ctest` runs.

### Fonts: `tests/test_stress_font.cpp` (22 tests)

Every mutant goes: `FontLibrary::register_font` -> `FaceData` (minting,
stand-ins) -> metrics, kerns, ink boxes, math parameters and variants,
minted and stand-in round trips, each asserted in range -> `strict_check`
(and `add_face_to_atlas` asserted to refuse what it refuses) ->
`build_atlas_font` at this build's ImWchar width (the other width one time
in four), size bound asserted, the result reloaded by the reader ->
`add_face_to_atlas` into a real atlas -> `IsGlyphInFont` asserted equal to
"layout resolves the codepoint to a real glyph", `FindGlyph` for ASCII, cmap
range ends, minted codepoints and random ones; under stb the baked advance is
asserted equal to layout's and the box inside the strict pass's bound.

Honest split: a parse of a bundled font costs 10-60 ms under ASan at `-O0`,
so "thousands per face in 25 s" is not reachable there. The count depends on
the build (the sanitiser build runs a prefix of the same seeded sequence):

| build | random mutants per bundled font (math face: half) | of which go to the atlas | mutants per synthetic seed (53 seeds, all to the atlas) |
|---|---|---|---|
| ASan + UBSan | 120 | 1 in 5 | 80 |
| debug | 450 | 1 in 5 | 600 |
| release | 3,000 | 1 in 5 | 3,000 |

The 53 seeds are 21 glyf, CFF and MATH fonts, 7 collections, 4 CID-keyed
fonts, 3 Type 2 charstring fonts, 4 with a `kern` table, 3 with `post` names
and 11 cmap shapes; a mutant of a collection goes through once per face.
Plus, per bundled font, the file itself and a cut at both ends of every table;
146 structured cases; and 188 mutants through
`ImTypeset::AddFontFromMemoryTTF`. `IMTYPESET_STRESS_ITERS=n` multiplies the
random counts.

Default run, census, ASan + UBSan with FreeType, 2026-10-10:

| stage | structured (146) | bundled fonts (7) | synthetic seeds (53) |
|---|---|---|---|
| mutants | 146 | 1,033 | 4,883 |
| refused by the reader | 34 | 335 | 1,840 |
| refused by the strict pass | 46 | 170 | 708 |
| refused by build_atlas_font | 0 | 0 | 0 |
| refused by ImGui | 0 | 0 | 3 |
| accepted | 66 | 528 (353 stopped before the atlas) | 2,332 |
| glyphs baked / visible | 7,630 / 852 | 11,349 / 10,606 | 110,007 / 11,796 |

The same executable in the other builds, default run, 0 failed checks in each:
release with FreeType 19,753 bundled-font mutants and 182,976 seed mutants;
debug with stb_truetype (ImGui's assertions live) 3,178 and 36,596, none
refused by ImGui; debug with FreeType and a 16-bit ImWchar 3,178 and 36,608.

**10x** (`IMTYPESET_STRESS_ITERS=10`), ASan + UBSan with FreeType: 8,053
bundled-font mutants and 48,800 seed mutants, 22 tests, 0 failed checks, 0
sanitiser reports; the two fuzz tests took 118.6 s and 139.8 s.
ASan + UBSan with stb_truetype: the same 8,053 and 48,800, 22 tests, 0 failed
checks, 0 sanitiser reports, none refused by ImGui; 134.9 s and 145.3 s.

**Installed fonts** (optional test `fuzz_fonts_named_by_the_environment`,
`IMTYPESET_STRESS_FONT_LIST`; 2,910 `.ttf`/`.otf` files read in place): stb
ASan and FreeType ASan with hinting off: 2,905 accepted as shipped (5 refused
by the reader: 4 CID-keyed, 1 without outlines), **0 refused by the strict
pass**, 176,920 glyph lookups with the atlas agreeing with layout on which
codepoints the font has; 11,640 mutants of them, 0 failed checks, 0 reports.
FreeType ASan with hinting on aborts on a mutant (see below).
Since `font-wild` the reader takes collections and CID-keyed CFF, and the
corpus loader test covers every installed face: `tests/test_font_loader_wild.cpp`
with `IMTYPESET_WILD_ALL=1`, 2,991 faces, 2,990 baked and none refused by the
atlas, `IsGlyphInFont` agreeing with `FaceData::has` on all 939,193 codepoints
asked, under FreeType (release, and ASan + UBSan), under stb_truetype (debug)
and under FreeType with a 16-bit ImWchar (921,962 asked).
The whole corpus was also baked under ASan + UBSan with stb_truetype: the same
counts, 0 failed checks, 0 sanitiser reports.

Run once more at 10x on the tree as it ends, at the final constants (ASan +
UBSan, FreeType): 8,053 + 16,800 + 484 mutants, 7,268,161 checks, 0 failed,
0 sanitiser reports, 143 s; ImGui itself refused 13 of the synthetic mutants.

### Text through the public API: `tests/test_stress_text.cpp` (10 tests)

Only `include/imtypeset.h` and `imgui.h`. Default counts, ASan + UBSan:

| what | default | at `IMTYPESET_STRESS_ITERS=10` |
|---|---|---|
| fixed invalid-UTF-8 cases (truncated, overlong, surrogate, NULs in the length, ...) x 8 flag sets | 26 x 8 | the same |
| random hostile paragraphs (bytes, runs, styles, widths; each laid out three ways and asked everything) | 450 | 4,500 |
| 1 MiB with no space in it | 1 alignment | 4 |
| 100,000 words in one paragraph | justified by total fit and by first fit, each at two widths | 5 modes |
| floats that are not lengths (16 values x 6 style fields x 4 alignments, the width, the position, a run's scale) | all | all |
| pathological formula sources | 45, nesting 2,000 deep | 45, nesting 20,000 deep |
| paragraph lives (create, set, lay out, draw, copy, destroy) | 1,200 | 12,000 |
| relayouts and retexts of one paragraph | 1,000 | 10,000 |
| sizes of an animated zoom | 900 | 9,000 |
| frames of a widget whose text changes every frame | 1,500 | 15,000 |

Allocation counts are of live blocks: every form of `operator new` and ImGui's
allocator, allocations minus frees. After each batch of paragraph lives: 497,
six times out of six, at 1x and at 10x. Relayouts: 593, 593, 593. The widget
holds at most 1,328 paragraphs for two new texts a frame.

The 10x run (ASan + UBSan, FreeType, the tree as it ends): 1,934,851 checks,
0 failed, 0 sanitiser reports, 159 s; live blocks after each batch of 2,000
lives 497 six times, relayouts 593 three times, the zoom 4,141, 3,451, 3,683. The default run takes 25 s there, 11 s in a debug build and
2.3 s in release. Under stb_truetype with the sanitisers the default run
passes as well.

### Layout core: `tests/test_stress_layout.cpp` (16 tests)

| what | default | at `IMTYPESET_STRESS_ITERS=20` |
|---|---|---|
| well-formed random paragraphs (characters, glue of every order, kerns of every kind, penalties, math, rules, nested boxes, discretionaries in one of four), broken by total fit or first fit, by either way of judging a candidate, against uniform, hanging and indented shapes | 800 (4,283 lines, 87,459 commands) | 16,000 (82,809 lines, 1,731,476 commands) |
| hostile lists: any byte in any field, types past the enum, indices naming nothing, marks made by hand, `INT64_MAX` and `INT64_MIN`, NaN and infinite glue ratios, every parameter of the breaker | 1,500 | 30,000 |

Invariants held on every well-formed paragraph: one break per line, positions
strictly ascending, the last at the paragraph's end; every character, box and
rule of the input in exactly one line, in order (for a list without
discretionaries); each line's packing equal to a sum made by the test; its
set glue plus everything else within one scaled point of the line's width
when the glue could be set; reading indices consecutive; one LINE command a
line; each paragraph inside a time box. A hostile list only has to come back,
with `out` untouched when ship fails.

The 20x run (ASan + UBSan, the tree as it ends): 2,955,032 checks, 0 failed,
0 reports, 70 s. The
default run: 10 s there, 4.3 s debug, 1.0 s release.

The check count of this file was not constant from run to run (150,963, 150,966, 150,977 on one
binary). Cause: `hostile_measure`, the measure that lies, seeded itself from its `user` pointer,
and `fuzz_hostile_lists_come_back` passed the address of a stack object, which the loader moves
between runs; what the font answered decided which lines reported an error, and a check follows
each error. It is now the round number. Four consecutive runs of one debug binary: 150,967 each.
The fuzz then gained the product's rules on every other list (`TexRules::product()`, for the
second walk of the active list that `easy_line` is): 151,037 checks in debug, the same in every
run. No time-boxed loop was involved; the clock is only compared against.

### The three together

Default, ASan + UBSan, one after another, as last run: 19.4 s (fonts), 25.0 s
(text) and 9.3 s (layout), 54 s in all. Under `ctest -j` on a loaded machine
each takes up to half as long again.

## Defects found by fuzzing, beyond the reviewers' lists

Fonts:

1. **stb_truetype reads one byte past its vertex array** (ASan
   heap-buffer-overflow READ 1, `stbtt__GetGlyphShapeTT`,
   imstb_truetype.h:1782) for a glyph whose last contour is a single
   off-curve point. Found by the dense synthetic fuzz on a font that passed
   the first strict pass. Fix: `strict_glyf_glyph` refuses "a glyph ends in a
   contour of one off-curve point". (The same contour followed by another is
   read in bounds and is left alone: Mukti.ttf has one.) Test: structured
   cases "glyph ending in a contour of one off-curve point" and "contour of
   one off-curve point before another contour".
2. **FreeType sizes a font by OS/2 typo metrics when `fsSelection` has
   USE_TYPO_METRICS; layout and stb size it by hhea.** Found as an ImGui
   assertion on a mutant whose OS/2 record pointed into glyf; it is also
   wrong for real fonts: 66 of 2,917 installed set the bit with a different
   extent (ratio 0.675 to 1.65). Measured: Lohit-Tamil at 32 px under
   FreeType, U+0B85 baked advance 18 px against layout's 29.3. Fix:
   `build_atlas_font` clears that bit in the atlas's copy of OS/2 (bundled
   fonts do not set it, so their rebuilt bytes are unchanged); after, 29
   against 29.3. Test: `the_atlas_sizes_a_font_by_hhea_whatever_os2_says`
   (fails without the fix under FreeType; passes trivially under stb).
3. **A variant chain's codepoint can be U+0000** (a cmap may map it to the
   glyph). Not a defect of the port; the test's assertion was wrong and says
   so now.
4. **Recorded, not fixed (not ours to fix): two ways to reach ImGui's "Out of
   texture memory" assertion**, which aborts where IM_ASSERT is compiled in
   and drops the glyph where it is not. Neither is a memory error.
   (a) ImGui 1.92.9: a glyph wider than the atlas texture fails to pack when
   the atlas first makes room by repacking: `ImFontAtlasPackInit` resets
   `MaxRectSize`, the pending rectangle's width is forgotten, the texture
   grows in height only (512x128 -> 512x512) and the four attempts run out.
   Seen with a 532 x 6 px glyph (gdb). Reachable by any font with a glyph
   wider than the texture after 20 % of the atlas has been discarded. The
   tests set `TexMinWidth = 1024`.
   (b) FreeType runs a TrueType font's hinting program; a mutated `fpgm` moves
   points so that a glyph is thousands of pixels across. The strict pass
   cannot bound that. With `ImGuiFreeTypeLoaderFlags_NoHinting` the same run
   is clean. Documented in `src/font_loader.h`.
5. **Recorded, not refused:** stb_truetype reads an hmtx advance as signed.
   An advance past 32,767 units is negative there, ImGui clamps it to 0 and
   recentres the box; layout and FreeType read it unsigned. Refusing it
   would refuse real fonts (NachlieliCLM-Bold has two glyphs at 65,530,
   meaning -6). Structured case "advance width of 33,368 units".

Because of 1 and 4 the reach bound was set from data: `STRICT_REACH` = 16
ascender-to-descender heights; the furthest-reaching glyph in 2,905 installed
fonts is between 6 and 8 (Amiri's ligatures).

Text and layout:

6. **A run given out of order reported a position outside the text.**
   `SetParagraphText` with a run whose `Start` is negative (the fuzz passes
   `INT_MIN`) answered `ImTypesetError_InvalidArgument` with `BytePos` equal
   to that `Start`. Fix: the position is held at 0 (`set_text` in
   `src/imtypeset.cpp`). Test: every answer's `BytePos` is checked against
   the text in `interrogate`, `tests/test_stress_text.cpp`.
7. **Row D' above** (a glue order of 4 writes past `GlueTotal`'s array) is
   the one memory error the layout fuzz reaches in the tree as it stood.
8. **Recorded, not fixed: ImGui's atlas runs out for large type.** One
   paragraph of ASCII and Latin-1 (188 different glyphs) in the Regular face
   at `EmSize` 1024, drawn once: ImGui asserts "Out of texture memory" in its
   font loader (`ImGui_ImplFreeType_FontBakedLoadGlyph`); at 700 it fits.
   Where `IM_ASSERT` is off the glyph is dropped. This is route (c) to the
   assertion of item 4, and the one a host can reach with a bundled font and
   a zoom slider. `tests/test_stress_text.cpp` lays out at every size and
   draws whole texts only up to 40 px (`large_type`). See D33.

No other defect: the layout fuzz, the public-API fuzz and the pathological
formulas produced no sanitiser report and no failed invariant in the tree
with the reviewers' items fixed, at 1x or scaled up. Thousands of very short
words do reach the line breaker's budget (2,500 two-letter words), which is
the budget working and is how the pathological-formula test met it. One
defect was in a fix and never in the tree: the sentence-space clamp first
used `clamp_dimen`, which lets the RUNNING sentinel through, and the test
written for the item caught a stretch of -2^30 from it.

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
| X1 | `IM_ASSERT` defined to throw, exceptions enabled, `operator new` made to fail at its n-th call: `CreateParagraph` **leaked the paragraph** (LeakSanitizer); a paragraph unwound in mid-layout **shipped 3 commands of 215 from then on** (`has_layout` was set before the layout was made, and likewise `has_material`, `has_lines`, `has_model`); `AddFontFromFileTTF` unwound from the registry **left a font in the atlas that no role named**. Read, not reproduced: a widget's paragraph leaked if the cache's map could not take it; a widget text that grew by a suffix taken for unchanged after `set_text` was cut short. | `Building`, `std::unique_ptr`, the pool's room reserved, `InAtlas`, `set_text` invalidates first and states its size last. No `try`/`catch` in `src/` outside `src/imtypeset_c.cpp` and the `IMTYPESET_NO_UNWIND` macro of `src/context.h` (D39 item 11). | `tests/test_exception_safety.cpp`, 7 tests, in a build with `-DIMTYPESET_ENABLE_EXCEPTIONS=ON -DIMTYPESET_IMGUI_USER_CONFIG=<repo>/tests/imconfig_throwing.h`; one test saying so in every other build |
| X6 (found by the gate of 2026-10-11, D65) | The same build as X1. A document given blocks with a table among them, laid out, cut into columns and drawn, with the n-th allocation throwing, then the same calls again: **ASan, heap-buffer-overflow, a write of 68 bytes in `Document::reset_block`** under `splice` under `SetDocumentBlocks`. `splice` and `add_block` grew the blocks, their layouts and their slots in one loop that looked at the blocks' length alone; a throw from the second or third left the blocks longer, and the next call wrote a slot that was not there. Nothing in it is a table's or a column's: any `SetDocumentBlocks` or `ReplaceDocumentBlocks` unwound from and made again could do it, and the earlier sweeps described their documents before arming the allocator. | Each of the three sequences is grown by its own length (`grow_to`, `src/blocks.cpp`). | `tests/test_exception_safety.cpp` `a_throw_from_anywhere_in_a_table_cut_into_columns_leaves_the_document_usable`: in order and balanced, 346 and 349 allocations unwound from, the document laid out, hit-tested and drawn as it stands after each, then described again and held to the vertices an undisturbed document draws (8,191 checks). Not swept: `AppendDocumentTableCells`, `AppendDocumentText` into a cell, `SetDocumentTableColumns`, `DocumentView` in columns |
| X2 | The overlay's line boxes and baselines went through `AddRect`/`AddLine`. | Own rectangles (D39, item 7). | `d3_...` pins the geometry; `tests/test_draw.cpp` replays the overlay as the library defines it and compares the two debug goldens at the calls |
| X3 | — | Measured; the members stay (D39, item 8). | `docs/performance.md` |
| X4 | `gprof` over `bench_draw --only x100`: with 3,400 paragraphs shown against a cache limit of 2,048, `evict` **went over the whole arrival list every frame** (3,400 relinks) for nothing: everything in it is in use. | Two counts (`submitted_now`, `submitted_before`) say whether anything can be dropped; if not, the list is left to its sixteen-a-frame clock. | `tests/test_api.cpp` `a_steady_frame_larger_than_the_cache_limit_does_not_go_over_the_whole_cache` (fails against the old condition) |
| X5 | `examples/demo_sdl2/main.cpp` called `EnableMath()` and the old `Text(text, nullptr, 0)`. | The two edits the api-revise report gives. | Built with `IMTYPESET_DEMO_WITH_LIBRARY=ON` against `../example-project`'s SDL2 and run under SDL's `dummy` driver: `--page paragraph --frames 3 --screenshot`, exit 0, 22,692 dark pixels in the frame |
| K1 (left open by `harden-2`) | A MATH table whose parameters cancel: `big_op_spacing5` a million ems and `big_op_spacing1..4` minus a million. §750 states the height and depth of a stack of limits, in which the padding and the distances cancel, so `\sum_i^n` in display style was SET, an ordinary box holding kerns of ±18,000,000,000 sp (`MAX_DIMEN` is 1,073,741,823). The same with `default_rule_thickness` minus a million ems: a fraction's rule and the kern above it cancel inside the height §747 states. | `Converter::push` (`src/math_mlist.cpp`): a kern or a rule with a dimension outside ±`MAX_DIMEN` is `TOO_LARGE` with `MathStatus::inside`; nothing is stored. Every node the conversion makes goes through `push`. | `tests/test_math_mlist.cpp`: `two_kerns_that_cancel_inside_an_ordinary_box_are_refused_and_not_stored`, `a_rule_that_cancels_against_its_kerns_is_refused_and_not_stored`. Both fail with the bound taken out (16 failed checks): checked. |

Four premises in the test file as this task found it were wrong, and were corrected in the test
rather than in the library: a `\qquad` is not exactly 24 pt at 12 pt (the face makes 36 mu of it);
an original of no bytes copies as nothing, not as a space; malformed UTF-8 is drawn and copied as
U+FFFD, as `include/imtypeset.h` says; and two consecutive hard breaks copy as one newline (see
"Not verified").

- **C ABI** (`src/imtypeset_c.cpp`): every pointer the layer itself dereferences is checked for
  null and refused with `ImTypeset_Error_InvalidArgument`; capacities are honoured exactly (tests
  hold a sentinel byte after each buffer); a C++ exception is turned into an error at every entry
  when the file is compiled with exceptions. Handles are not validated: a stale handle is a use
  after free here as in the C++ API.

## Counts

| | items | how each ended |
|---|---|---|
| reviewers' items | 16 (A-F, K; S1-S9) | 10 reproduced under the sanitisers or measured against the old code, then fixed (A, B, C, D, E, F, S1, S2, S3, S5); 5 fixed from a reading, each with a test of the new behaviour (K, S6, S7, S8, S9); 1 did not reproduce on a 64-bit target and was changed by construction (S4). The exact shape the review gave for S2 did not reproduce either; one a megabyte away did. |
| other items handed over | 3 (the sentence space, `operator new` in `tests/test_linebreak.cpp`, the Unicode tables in `src/selection.cpp`) | the first two reproduced and fixed; the third is a consolidation |
| the draw review, arrived late | 7 (D1-D7) | D1 and D2 reproduced and fixed; D5 done; D6 answered by reading ImGui in the first pass and settled by a run in the second; D3, D4 and D7 done in the second pass (rows D3, D4, D7 of `## Draw`), D7's rule then replaced (D47) |
| defects found beyond the lists | 6 | 4 fixed: stb_truetype's one-byte read, FreeType sizing by OS/2 typo metrics, the run position outside the text, the glue order past `GlueTotal`. 2 recorded and not fixed: ImGui's "Out of texture memory" assertion (three routes), stb_truetype's signed advance. |

Second pass (task `harden-2`), the tests it adds: `tests/test_harden.cpp` 21 tests (326,071 checks
under FreeType); `tests/test_exception_safety.cpp` 7 tests in the throwing build (52,730 checks)
and 1 elsewhere; `tests/test_mathtext.cpp` 17 tests (was 16); `tests/test_api.cpp` 40 tests (was
39). The draw review's D3, D4, D6 and D7 were taken up again in that pass; the `## Draw` table
has how each stands.

Those are the counts of the day the pass ended. On the tree of 2026-10-10, 13:30, by
`grep -c '^TEST('`: `tests/test_harden.cpp` 22, `tests/test_exception_safety.cpp` 10,
`tests/test_mathtext.cpp` 17, `tests/test_api.cpp` 42. Check counts were not taken again here.

## Not verified

- Any 32-bit target, MSVC, clang, a non-x86 machine, a FreeType other than
  2.12.1, an ImGui with `IM_ASSERT` compiled out.
- The preset directories (`build/debug` and the rest) were not built; every
  run was in `build/stress*` and `build/stress-font*`.
- `src/draw.cpp` and `src/selection.cpp` were fuzzed through the public API
  only, where every dimension is within `MAX_DIMEN`. A hand-built command
  list with dimensions outside it was not given to either. (Since done for
  `src/selection.cpp`: row H5 of the second pass.)
- `src/paragraph.cpp` and `src/math_mlist.cpp` keep unchecked sums; their
  inputs come through `src/style`'s clamps and the public API's. (Since the
  second pass no box of a formula leaves `src/math_mlist.cpp` outside
  `MAX_DIMEN`: row H2, D39 item 3.)
- The font half's 10x runs were made at slightly larger constants than the
  final ones (150/100 against 120/80 per unit), so they cover more than ten
  times the default.
- S6, S7 and S9 were not run against the old code.
- Two newlines in a row under `ImTypesetTextFlags_HardBreaks` are one line break, drawn and
  copied: the blank line between them is not kept (`src/paragraph.cpp` drops a hard break at the
  index the last one was at, as the reference's `_after_hard_breaks` does). Seen while writing
  `h5_what_the_public_api_...`; not looked into further. `include/imtypeset.h` says "one per
  newline", which a host will read as keeping blank lines.
- (Answered by `d7-fix`, D47: under the rule `harden-2` wrote, a narrower part stood up to
  4.82 px from the middle of its bar; placed whole, 0.77 px at worst over the 14 formulas
  measured.)
- Whether a list of 524,288 source bytes can reach `MAX_MATH_ITEMS` (2^20). Nine dense patterns at
  the bound were tried and none does; no argument was made that none can. If one does, the answer
  is the `LAYOUT` refusal of H1, which is what the bound is for.
- X1 under anything but GCC 12 with ASan; see D39's own list.

## A side bearing that moves an outline out of range (D83, 2026-10-12)

A third route to the "Out of texture memory" assertion of item 4, and one the
strict pass can close: a `glyf` glyph whose `hmtx` left side bearing moves its
outline past 16 bits comes back from FreeType's hinter as a bitmap wider than
the atlas (1046 px at 16 px, from a 596-byte font). The strict pass refuses
it. Reproduce: `tests/test_stress_font.cpp`, the four side-bearing cases; the
file that found it is `.concerns/clang-clean--glyf-lsb-7fff.ttf`. The `fpgm`
route of item 4 is unchanged: not fixable by the strict pass.
