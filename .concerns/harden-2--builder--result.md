# harden-2 — result (builder-2, the third holder of the task)

Hardening round 2: H1-H8, D3/D4/D6/D7, X1-X5. Every item was run against the tree as found before
anything was changed, each binary under a memory guard. The exact text for the shared documents is
in `.concerns/harden-2--builder--doc-additions.md` (nothing in `docs/` was edited); the crash log of
this task is `.concerns/harden-2--builder--progress.md`.

**No fix was in the tree when I started.** The earlier holders had written `tests/test_harden.cpp`
against fixes that did not exist (it compiled only with `-DHARDEN_REPRO`) and the CMake options for
the throwing build. I shrank the 100 MB case first (to one byte and to twice the bound) and never
ran the 100 MB one.

## One row per item

| item | reproduced? (observed, before any change) | fixed where | test |
|---|---|---|---|
| H1 | Yes. 524,289 and 1,048,576 bytes of `x` parse; a list of 2^20+1 atoms is refused as kind FONT, "the math face could not set it". | `src/mathtext.{h,cpp}`: `MAX_SOURCE_BYTES` = 2^19, refused in `parse` before decoding; `TOO_LARGE`/`TOO_DEEP` are kind LAYOUT. | `test_harden` `h1_*` (4 tests) |
| H2 | Yes. 25,000 x `\qquad` is set and stored at 36 x `MAX_DIMEN`; a BOX node that wide reaches the paragraph through the public API. No sanitiser report downstream. | `set_formula` (`src/mathtext.cpp`) and `Converter::node_of` (`src/math_mlist.cpp`) refuse (`TOO_LARGE`, with the dimension). | `h2_*` (2); `tests/test_math_mlist.cpp` `math_glue_stays_inside_the_dimension_invariant` changed (see below) |
| H3 | Yes, with a `de_DE.UTF-8` compiled by `localedef` (`LOCPATH`): "1,25 times as tall". | `quotient_to_two_places` (`src/mathtext.cpp`), no `%.2f`. | `h3_*`: 8 cases by hand, ~314,000 quotients against C-locale `printf`, then under the comma |
| H4 | Yes. `ImTypesetFormula::Reason` reads `\` for a reason that is `\<NUL> is not in the subset`. | `c_string_reason` (`src/mathtext.*`), applied in `ErrorState::set` and at the two `FormulaRecord::reason` sites (`src/imtypeset.cpp`): `\x00`. | `h4_*` |
| H5 | Yes. 12 UBSan signed-overflow sites in `src/selection.cpp` on a buffer with extreme coordinates; `TextData` out of order copies `abd egh ijkl` for `Abd\n\ne$x$gh\tiJkL`. | `src/selection.{h,cpp}`: `held()` in `build`; `ByIndex`/`Breaks` check order and search either way. | `h5_*` (3) |
| H6 | Yes: the reviewer's input is 50 levels deep. | Comment in `src/mathtext.h` (the `static_assert` already said +2). | `tests/test_mathtext.cpp`: `list_depth` over every field; two tests |
| H7 | Yes (read): `set > 1500`, `by_hand` printed only. | `tests/test_mathtext.cpp`: 1950 / 21 / 66 exact; long arbitrary sources added. | same file |
| H8 | Nothing to reproduce: the new test passes before and after. | — | `h8_*` |
| D3 | Yes: rectangles on a list with its own shared data carry the current context's white texel. | `src/draw.cpp`: rectangles through public `ImDrawList::PrimRect`. The scope note's question: `_Data` is declared in `imgui.h` but its type is complete only in `imgui_internal.h`; the texel IS reachable publicly, through `PrimRect`. | `d3_*` |
| D4 | Yes: same vertices and triangles, a draw command ends up to 94 quads earlier (bound 1,024). | Decided: keep. Stated at `BATCH_QUADS` and in `src/draw.h`. | `d4_*` |
| D6 | Settled by a run: atlas capped at 512x1024, 120 frames, 224 bakes discarded, texture replaced under a draw in 24 frames, all 11,040 quads on live glyph rectangles. No defect. | — | `d6_*`; the too-small atlas (ImGui's own assertion) is in `test_exception_safety` |
| D7 | Yes, measured and LOOKED AT (software renderer, enlarged). Radical: bar from 3 px inside the sign's quad to 3 px clear of it over 576 draws. Fraction (found by py-host's builder against my first fix, then measured under the reference's rules too): bar ends up to 6 px from the numerator as drawn. | `DrawRules::rule_joins_span` (`src/draw.{h,cpp}`), `reference()` by default, `product()` for the public API (`ImTypesetContext::draw_rules`). See "D7, twice" below. | `d7_*` (4, one of them the opt-in pictures) |
| X1 | Yes: `CreateParagraph` leaks under a failed allocation (LeakSanitizer); a paragraph unwound mid-layout ships 3 commands of 215 for good; a font is left in the atlas with no role. | `src/imtypeset.cpp` (`Building`, `unique_ptr`, `InAtlas`, `WrapPos`, `set_text` order, pool reserved). No `try`/`catch` in `src/`. | `tests/test_exception_safety.cpp` (7 tests in the throwing build, 1 "skipped" test elsewhere) |
| X2 | — | `src/draw.cpp` `write_frame`/`write_segment`: own 1 px rectangles, in lists and buffers. | `d3_*` pins the geometry; `tests/test_draw.cpp` replay changed |
| X3 | Measured: `PrimRectUV` per quad +15 %, inline `PrimWriteVtx/Idx` +3 % (and still reads `_VtxCurrentIdx`); noise 1 %. | Kept the three write-position members for glyph quads; rectangles use `PrimRect`. | doc-additions §4 |
| X4 | Found with `gprof`: over the cache limit (3,400 shown, limit 2,048) `evict` relinked the whole arrival list every frame. | `src/imtypeset.cpp` `evict`/`widget_for`, `src/context.h` (`submitted_now`/`submitted_before`). | `tests/test_api.cpp` `a_steady_frame_larger_than_the_cache_limit_does_not_go_over_the_whole_cache` (fails with the old condition: checked) |
| X5 | Yes (does not compile). | `examples/demo_sdl2/main.cpp`, the two edits api-revise gave. | Built with the library against `../example-project`'s SDL2; ran `--page paragraph --frames 3 --screenshot` under SDL's dummy driver: exit 0, 22,692 dark pixels |

### D7, twice

My first product rule placed a formula's rule from the glyph placed before it. That fixed the
radical and broke fractions: the glyph before a fraction bar is the numerator's LAST, so the bar
took the rounding of the whole numerator at its left end. py-host's builder saw it in the quadratic
formula (`.concerns/harden-2--py-host-builder--fraction-bar-moved.md`); their attribution was right.
Reproduced by hand (`d7_a_rule_in_a_rigid_span_...`: 46 where 48 is right) and corrected:

- a rule inside a formula joins the formula's span and does not end it;
- its LEFT end is placed from the nearest formula glyph placed BEFORE it, its RIGHT end from the
  nearest placed before OR after it (by origin, 64 glyphs each way), each by the gap in scaled
  points rounded once; the ends are settled when the span ends;
- top and bottom from the span's baseline.

Left from earlier glyphs only because a radicand's first glyph begins nearer the bar's start than
the sign's origin does, and the joint that shows is the sign's. Measured, 144 draws per rule set
(quadratic formula, wide numerator, wide denominator, `\frac{a+b}{c+d}`; 13/17/24 px; 0/13/40 terms
before; 4 origins): bar end against the end glyph as drawn, reference up to 6.00 px, product at most
1.00 px; radical inside the numerator against its sign, 4.91 px and 0.42 px. Pictures of both rule
sets written by `IMTYPESET_HARDEN_SHOTS=<dir> test_harden --filter=d7_pictures` and looked at: under
the reference's rules after 40 terms the numerator stands 5 px right of its bar and the radical sign
prints over the `b`; under the product's the fraction is the same at 0 and at 40 terms, bar over the
numerator, `2a` under its middle, sign joined. `docs/images/py-host.png` still shows the first form.

What the product rule does NOT do: move any glyph. A denominator is still placed from the glyph
before it (the numerator's last), so it stands under its bar to within the rounding its numerator
gathered end to end; the wide-denominator picture looks right, and I did not put a number on it.

### X2: exactly what is no longer compared with the reference

`tests/golden/draw/draw.golden` has two cases with `debug 1`: `span-edges-by-hand-debug` and
`debug-overlay`. For those two only, the `v` records (120 and 1,268 vertices), the `tri` records
(68 and 662) and the one `drawcmd` record each are no longer compared (`tests/test_draw.cpp`,
`the_vertices_are_those_of_one_add_text_per_glyph`: `if (c.args.debug) { ++debug_at_pen_level;
continue; }`, and the count is asserted to be exactly 2 under FreeType). They hold what ImGui's
`AddRect`/`AddLine` made of 2 + 7 `rect` and 2 + 7 `linesegment` calls; the port's lists for the
same cases have 136 and 1,324 vertices, so no arrangement of own rectangles can equal them. Still
compared for those two cases: `stats`, `keys`, `boxes` and every `ops` record (`text`,
`rectfilled`, `rect`, `linesegment`), and, under any rasteriser, the port's list against a replay
of the same calls through ImGui with the overlay drawn as `src/draw.h` defines it. All records of
the other twenty cases are compared as before (22 of 22 "outright" still printed and asserted).
No golden file was touched.

## The lead's later question: font answers on the math path past MAX_DIMEN

font-wild-finish's builder read that `src/math_mlist.cpp` rounds what the math face answers with
`round_to_scaled` (bounded at `SCALED_LIMIT`) where the text path uses `rounded_dimen`
(`MAX_DIMEN`). True as read. NOT reproduced as a defect: with H2 in, nothing I could build gets a
dimension past `MAX_DIMEN` into a formula that is set. Two tests hold what was tried, under ASan +
UBSan:

- `h2_at_the_largest_sizes_a_formula_is_set_inside_the_bound_or_refused_as_too_large`: the bundled
  math face at base sizes `MAX_DIMEN`, /2, /8, /64, fifteen formulas, text and display: 96 set with
  every box, kern, glue and rule in the tree inside the bound, 24 refused `LAYOUT` for their size.
- `h2_a_math_face_with_a_parameter_out_of_all_proportion_...`: a math face whose axis is a million
  ems below the baseline (clearances that large above and below a fraction's rule, which I expected
  to cancel inside an ordinary box). They do not survive: section 746's adjustment turns the
  clearance into a shift, the box is then deeper than `MAX_DIMEN`, and H2 refuses it; the same for
  big operators and delimiters. Everything that set was in bound.

So I changed nothing in `rounded()` (I tried clamping it there and took it out again: with no
failing input it would have been a fix for something not shown, and a clamp on a parameter does
not bound the kerns computed from two of them anyway). Not excluded: a MATH table built to make
two huge kerns cancel inside a box whose stated dimensions stay small. If someone wants that
closed without a repro, the place is a clamp where the converter makes a kern (`push(kern(...))`,
fifteen sites) — `tex-fixes` owns the file next. The public API also holds the em to
`MAX_DIMEN / 16`.

## Tests of other owners that I changed, and why

- `tests/test_math_mlist.cpp` `math_glue_stays_inside_the_dimension_invariant` (tex-fixes inherits
  this file): it asserted that a fraction with a rule 2^60 thick converts `.ok()`. With H2 that
  fraction's box is taller than `MAX_DIMEN` and is refused; the test now expects `TOO_LARGE` there
  and keeps a thick-but-legal rule to hold the clamp. This is a behaviour change of `stress`'s D33
  on a hand-built list, not on anything the parser emits.
- `tests/test_mathtext.cpp` (H6, H7 name it) and `tests/test_draw.cpp` (the lead's dist note gives
  it to this task): described above.
- Four premises of the inherited `tests/test_harden.cpp` were wrong and were corrected in the test:
  `\qquad` is not exactly 24 pt; an empty original copies as nothing; malformed UTF-8 copies as
  U+FFFD (as the header says); two hard breaks in a row copy as one newline.

## What was run

Six private configurations, each configured from scratch on the final sources, built `-j4` with
`-Werror`, and run with `ctest -j4`, every binary under a guard (`ASAN_OPTIONS=hard_rss_limit_mb=3000`
for the sanitised two, `ulimit -v 6000000` for the rest) and `UBSAN_OPTIONS=halt_on_error=1`, with
`LOCPATH` pointing at a `de_DE.UTF-8` compiled by `localedef` so that the comma-locale halves of
`h3`/`h8` ran. NARROWED: modules = every `src/*.cpp` but `imtypeset_c`; tests = `test_api`,
`test_api_math`, `test_badness`, `test_blocks`, `test_draw`, `test_harden`, `test_header_pod`,
`test_linebreak`, `test_linebreak_totals`, `test_math_mlist`, `test_mathtext`, `test_nodes`,
`test_pack`, `test_paragraph`, `test_selection`, `test_shipout`, `test_soft_raster`,
`test_stress_layout`, `test_stress_text`, `test_style`, `test_units`, `test_exception_safety`,
`test_fontlib`, `test_font_loader`, `test_opentype`, `smoke`; examples `minimal`, `specimen`;
benches `bench_draw`, `bench_reflow` (built, and `bench_draw` run in release).

| configuration | build | ctest | warnings |
|---|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | ok | 28 of 28 | 0 |
| ASan + UBSan | ok | 28 of 28, no sanitiser report | 0 |
| release | ok | 28 of 28 | 0 |
| stb_truetype (`-DIMTYPESET_USE_FREETYPE=OFF`) | ok | 28 of 28 | 0 |
| 16-bit ImWchar (`-DIMTYPESET_IMGUI_WCHAR32=OFF`) | ok | 28 of 28 | 0 |
| ASan + UBSan, exceptions ON, `tests/imconfig_throwing.h` | ok | 28 of 28, no sanitiser or leak report | 4, all `-Wterminate` in ImGui's own destructors |

In the ASan run: `test_harden` 19 tests, 325,909 checks (21 tests, 326,071 with the two added afterwards for the lead's later question, which were run under ASan + UBSan only, in a narrowed build of `test_harden`, `test_math_mlist`, `test_mathtext`, `test_api_math`: all pass); `test_mathtext` 17, 201,809;
`test_math_mlist` 29, 219,185; `test_api` 40, 71,574; `test_api_math` 19, 28,841; `test_draw` 16,
17,135 (22 of 22 cases outright, 2 of them at the calls only). In the throwing build
`test_exception_safety` 7 tests, 52,730 checks; in the other five it is one test that prints why it
skipped. After that run two comments in `src/draw.cpp` were corrected (no code): debug rebuilt,
`test_draw`, `test_harden`, `test_api` rerun, pass.

The demo (X5) was built and run once, before the D7 rework; `examples/demo_sdl2/main.cpp` has not
changed since, but that binary was not rebuilt on the final library.

Benchmarks: release, three interleaved rounds of separately built binaries; numbers in
doc-additions §4. In short: `evict` fix -15 % on the 3,400-call widget frame, x10 unchanged; the
draw into a list is 8 % (1.3 ns a glyph) SLOWER than before this task, into buffers 1 % faster.

An earlier full run of the same six configurations, before the D7 rework, was also 28 of 28 each.

Not run: the presets (`build/debug` ...), an un-narrowed tree (`imtypeset_c`, `test_opentype_formats`,
`test_opentype_wild`, `test_font_loader_wild`, `test_stress_font` and the C API tests are other
tasks' and were left out of every build of mine), clang, MSVC, Windows, ImGui 1.93, the Xvfb/OpenGL
path of the demo. `tools/shot.sh` was not used. The font goldens were not touched.

Also built, once, not kept: the library and `smoke`, `test_api`, `test_draw`, `test_harden` against
the bundle's real configuration `../imgui_bundle/imgui_bundle_cmake/imgui_bundle_config.h` with
`-DIMGUI_BUNDLE_BUILD_PYTHON` (IM_ASSERT throws `std::runtime_error`, 32-bit `ImDrawIdx`): 0
warnings in this tree's sources, all four pass (`d4_` skips: one command per list with 32-bit
indices). `../imgui_bundle/litgen_imconfig.h`, which the scope note names, is ImGui's stock
`imconfig.h` with only a Windows `IMGUI_API` enabled: it builds clean and needs nothing; the
throwing assertion is in the other file.

## Every source file changed

`src/mathtext.h`, `src/mathtext.cpp`, `src/math_mlist.h`, `src/math_mlist.cpp`, `src/selection.h`,
`src/selection.cpp`, `src/draw.h`, `src/draw.cpp`, `src/imtypeset.cpp`, `src/context.h`,
`examples/demo_sdl2/main.cpp`.
Tests: `tests/test_harden.cpp`, `tests/test_exception_safety.cpp` (new), `tests/test_api.cpp` (one
new test), `tests/test_mathtext.cpp`, `tests/test_math_mlist.cpp` (one test), `tests/test_draw.cpp`,
`tests/imconfig_throwing.h` (formatting only).
Not changed by me: `src/paragraph.*` (in my `touches`, nothing needed), `bench/bench_draw.cpp`,
`CMakeLists.txt` (the two options were already there), `include/imtypeset.h`.
For `tex-fixes`: in `src/math_mlist.cpp` only `Converter::node_of`, `dimension_outside_max_dimen`
and the `TOO_LARGE` message changed; `src/imtypeset.cpp` gained `build_material`/`layout_paragraph`
wrappers around `*_now` bodies (put a new rule's line in the `_now` body).

## For whoever owns them

- `include/imtypeset.h` should say (a) `ImTypesetFormula::Reason` and `ImTypesetErrorInfo::Message`
  spell a NUL as `\x00`; (b) a formula source longer than 524,288 bytes is refused; (c) what two
  newlines in a row do under `HardBreaks` (one break, the blank line is not kept — drawn and
  copied — which "one per newline" does not suggest). I did not edit the header.
- `docs/images/py-host.png` shows the first, wrong form of the D7 rule and needs re-shooting.
- I ran `git add tests/test_exception_safety.cpp` by mistake and took it back out of the index
  (`git rm --cached`); the file is untracked like its neighbours.

## To run it again

- The comma locale is not installed on this machine; I used one compiled into my build directory,
  which is gone with it: `localedef -i de_DE -f UTF-8 <dir>/de_DE.UTF-8`, then `LOCPATH=<dir>`.
  Without it `h3` checks the C locale and `printf` only, and `h8` prints that it skipped.
- The throwing build: `-DIMTYPESET_ENABLE_EXCEPTIONS=ON
  -DIMTYPESET_IMGUI_USER_CONFIG=$PWD/tests/imconfig_throwing.h` plus the ASan flags. `dist` or the
  gate should make it a preset; nothing does yet, so `test_exception_safety` is a skip everywhere else.
- The pictures: `IMTYPESET_HARDEN_SHOTS=<dir> test_harden --filter=d7_pictures` (18 PNGs, about
  1 MB each, both rule sets).
- All `build/harden-2*` directories, logs and scripts are removed.
