# Board mirror and task specifications

The coordination board lives in a session and was LOST once (2026-10-10, about 00:56) together with
every running worker. This file is the durable copy: status of every task, and the full
specification of every task not yet done. **The lead updates it whenever a task is declared or
completes.** A task's board `detail` only points here.

Rules for every task are in `CLAUDE.md`; rulings and what earlier tasks established are in
`docs/port-hazards.md` (it overrides anything older) and `docs/decisions.md` (D1-D37; D32 direction,
D33 hardening, D34 product shape, D36 revised API, D37 block layer). Each finished task's report is
`.concerns/<task>--<role>--result.md`. Finish every task by writing that file.

## Rules added after the SECOND session loss (2026-10-10, about 06:11) — every task, no exceptions

Both losses line up with two kernel OOM kills (`oom_kill 2` in /proc/vmstat; no swap; ~14 GB free).
Measured 06:20: `test_harden --filter=h1_a_source_past` (a 100 MB formula against the unfixed
parser) passes 2.5 GB resident and keeps climbing; harden-2 ran it just before each loss. That it is
what took the session down is an inference from the timing, not something observed.

1. **Never run a test, bench or example binary without a memory guard and a timeout.**
   - sanitised builds: `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout 600 <binary>` (checked: it stops
     the H1 repro), and the same variable exported for `ctest`;
   - everything else: `( ulimit -v 6000000; timeout 600 <binary> )` (ASan cannot run under `ulimit -v`).
   A test of hostile input is written so the UNFIXED code fails it cheaply: bound the input (1 MB, not
   100 MB) or run the unbounded case only after the fix is in.
2. **Build with `-j4`** (`cmake --build <dir> -j4`, `ctest -j4`); several workers share 16 cores and
   14 GB. One sanitised build at a time per worker.
3. **Progress survives a crash only on disk.** After each item, append a line to
   `.concerns/<task>--<role>--progress.md` (what is done, what is verified, what is next). The result
   file is still written at the end.
4. **Shared documents while tasks run in parallel**: `font-wild-finish`, `harden-2` and `c-api` do NOT
   edit `docs/decisions.md`, `docs/divergences.md`, `docs/performance.md`, `docs/hardening.md` or
   `THIRD-PARTY.md`. Each writes the exact text to add, per file, in
   `.concerns/<task>--<role>--doc-additions.md`; the `fold-docs` task applies them. Where a spec
   below lists one of those files for these three tasks, this rule replaces it.
5. Remove your own `build/<task>*` directories when you finish (`build/` is 8 GB).

## Board after the second loss

font-wild-finish, harden-2, c-api run together; tex-fixes after harden-2 (shared sources); py-host
after c-api; fold-docs after the first four; demo after fold-docs and py-host is NOT required for it;
api-document after demo + py-host; dist and advtext-rework-plan after api-document; docs; release-gate.

### USER PRIORITY, 2026-10-10 about 08:10 (the user's words, mid-session)

"It looks like evaluation of python usage is leaning heavily on imgui_bundle adopting the library.
That's a longer term question. My NEED is being able to use imtypeset in advtext and, importantly,
pptmstr."  Consequences (lead):
- Both applications run on the installed imgui_bundle wheel (checked: ../pptmstr imports imgui_bundle
  in 20 files and advtext in none). The target is therefore a sidecar library inside an UNMODIFIED
  wheel — task `bundle-route` (spec on the board; result `.concerns/bundle-route--*--result.md`).
  Upstream adoption / litgen (docs/bindings-notes.md, bindings/imgui_bundle/) is backlog.
- Blocked on a person: the fork's headers. In ../imgui_bundle:
  `git submodule update --init external/imgui/imgui` then check out the commit the wheel was built
  from (claimed c9a0a84f8a4ddae794a9e1acf6a93afce73d5057; bundle-route re-derives it).
- `advtext-rework-plan` is re-read as: plan AND spike for advtext and pptmstr, hosted on the wheel
  through whatever route bundle-route establishes — not on python/host's self-hosted SDL2 window.
  pptmstr comes first where the two compete. Read bundle-route's result before starting.
- `api-document`'s Python work is judged by whether pptmstr's panes and advtext's views can use it
  on the wheel; the self-hosted host stays as the test rig.
- `docs`: the README's Python story leads with advtext/pptmstr and says exactly how far it runs.

### USER PRIORITY 2, about 08:25: "Prioritize the demo now."

`demo` was started at once by a builder working WITHOUT the board claim (the entry stays blocked on
tex-fixes and fold-docs; the board refuses re-declaration). Narrowed files: src/imtypeset_demo.cpp,
examples/imtypeset_demo.h (temporary declaration), examples/demo_headless.cpp, examples/demo_sdl2/*,
examples/CMakeLists.txt, tests/test_demo.cpp, docs/images/demo-*.png. The header line, C counterpart,
map row and THIRD-PARTY entry go to `.concerns/demo--builder--doc-additions.md` for api-document.
First milestone: `.concerns/demo--builder--how-to-run.md`. After a session loss: resume from
`.concerns/demo--builder--progress.md`; do not start api-document before this builder reports.

### bundle-route DONE (08:40) — `.concerns/bundle-route--builder--result.md`

The sidecar runs inside both stock wheels (advtext's venv: imgui_bundle 1.92.900; pptmstr's OWN
.venv: 1.92.801 / ImGui 1.92.8) on STAND-IN upstream headers, accepted by a field-level probe
(python/imtypeset/_bundle.py), not by ImGui's own layout check (sizeof(ImGuiStyle) differs).
Lead ruling: left as built (probe default, IMTYPESET_BUNDLE_STRICT=1 opt-in) pending the user; the
user was told. Consequences for later tasks: D6's exact pin must become a floor (1.92.8 builds);
src/imtypeset_demo.cpp cannot link in the wheel (fork's InputText* take std::function); hello_imgui
clears the atlas after post_init — add fonts in load_additional_fonts; docs/bindings-notes.md §6B and
`.concerns/py-host--lead--rulings.md` ("cannot be driven inside the wheel") are superseded.
New task `word-gaps` (after d7-fix): collapsed word gaps at 16 px seen in the pptmstr pane.

### AFTER dist (lead, later the same day) — three tasks in parallel, disjoint files

review-4-draw (`.concerns/review-4-draw--reviewer--findings.md`, read-derived) produced:
`draw-seams` (src/draw.*, tests/test_word_gaps.cpp, test_harden.cpp, bench_draw.cpp: text glued to a
formula loses up to 2 px of the next space; product worse than reference at "($x$)") and `demo-guard`
(src/imtypeset_demo.cpp, tests/test_demo.cpp, examples/demo_headless.cpp: context-guard
use-after-free and leak sequences; test order dependence). Specs are the board details. The board
did NOT make them wait for dist, but they must: dist is running its final preset suites from the
live tree. Start them, with `doc-fixes`, when dist reports. Then docs, then release-gate (which must
also wait for these although the board does not say so). review-4-document is still running.

### ORDER FROM HERE (lead, 13:35)

Running: word-gaps (src/draw.*; in-flight state hangs test_stress_text — told), advtext-rework-plan
(re-read for pptmstr + advtext on their wheels). Then, one at a time because each writes src/:
dist (after word-gaps; knows one test is red) -> doc-fixes (board made it wait for dist; spec on the
board: tab-in-code range defect, header mismatches, DescriptionCut review) -> docs -> release-gate.
`doc-fixes-now` is a duplicate the lead declared by mistake: close it as superseded when claimable.
Lead ruling: the C ABI keeps (text, text_end, n) for the two Text*Offset functions; C++ is
(text, n, text_end = NULL).

### d7-fix (declared 08:00 after review-3a; spec is the board detail, mirrored here in short)

Check, then fix, the D7 product rule placement (`DrawRules::rule_joins_span`, src/draw.*): review-3a
(`.concerns/review-3a--reviewer--findings.md`, READ-DERIVED) says a rule end is joined to the nearest
glyph origin even when that glyph is on another row (radical in a denominator, nested fraction),
that the narrower part sits off-centre, and that past 64 glyphs the left end drifts. Reproduce with
every rule measured against its own box's ink; fix at the cause with reference() untouched; re-measure
bench_draw. Runs after tex-fixes (shared math_mlist). `demo` must not start before it completes.
Files: src/draw.*, src/shipout.*, src/math_mlist.*, tests/test_harden.cpp, test_draw.cpp, test_shipout.cpp.

## Done (reports in `.concerns/`)

foundation, oracle-core, oracle-font, oracle-draw, oracle-math, oracle-para, oracle-font-wild,
oracle-core-thresholds, oracle-math-gaps, oracle-blocks, oracle-tables, oracle-doc-extras,
core-nodes, pack, linebreak, shipout, font-reader, font-loader, style, para, draw, selection,
math-mlist, mathtext, soft-raster, public-api, api-math, example, bench, stress, font-wild-census,
shot-rig, dist-research, docs-hygiene, bundle-research, advtext-map, tex-web-pass, blocks,
api-revise, dist (all eleven presets green incrementally; floor ImGui 1.92.0; D44; ten mechanical renames in src/), review-4-draw + review-4-document (findings filed), word-gaps (15:20; product draw rules word_width_held + word_spaces_led; spaces within 1.5 px of layout; +10 % public draw; 8.9 % of in-word glyph pairs >0.5 px off; full suite 40/40 in five configs on the live tree; remaining tight lines are the line breaker shrinking spaces — not chased), advtext-rework-plan (14:25; docs/advtext-rework-plan.md; pptmstr patch at python/spike_bundle/pptmstr_overlay/pptmstr.patch run in the real app), api-document (13:30; Document in C++, C, Python; runs in both wheels; D42, D43; a failing test left in tree: test_api_document_golden tab-in-code), d7-fix (10:35; rule_joins_span REPLACED by DrawRules::formula_placed_whole — a formula is placed whole from its box edge; all four review items reproduced; worst rule end now 0.95 px; trade: 16.8 % of adjacent glyph pairs in formulas are >0.5 px from layout distance; lead accepts, user told; layout causes found and NOT chased: \int italic correction double-counted, no null delimiter space after a fraction, sqrt-over-fraction clearance), b6-map-shipped (10:15; predicate unified; defect unreachable in-tree, shown only in a scratch build), review-round-3 (10:00; findings in .concerns/review-round-3--reviewer--findings.md; new task b6-map-shipped), demo (09:45; all eight sections, hosts, test_demo; header line + C counterpart pending in .concerns/demo--builder--doc-additions.md for api-document), fold-docs (09:30; D38-D41 assigned; two pairs left for `docs`: .concerns/docs--lead--from-fold-docs.md), tex-fixes (09:20; all eight ruled departures + easy_line behind TexRules::product(), reference() unchanged; hand-worked answers, no TeX engine — under review in review-round-3), bundle-route (08:40), harden-2 (07:50; draw into an ImDrawList is 8 % slower, kept and recorded; D7 is a product draw rule its builder calls a heuristic — under review in review-3a), font-wild-finish (07:25; D4 done as a REPAIR of the atlas copy, not a refusal — lead accepts: a refusal cost installed faces the atlas bakes correctly, measured by the builder over the corpus), c-api (2026-10-10 06:45), py-host (07:10; part B gated and skipped; screenshot shows the fraction-bar finding routed to harden-2). Reviews: plan, layout core, font stack, public API, draw/paragraph/style,
math/selection.

## Interrupted by the session loss — partial work is in the tree

| task | state found on 2026-10-10 06:00 |
|---|---|
| font-wild | reader, loader, corpus tests and docs written; whole corpus 2,990 read / 1 refused / 0 mismatched in six configurations (builder's interim report). Its helper then found FOUR reader defects and left a failing test for each (`tests/test_opentype_formats.cpp`, `tests/test_stress_font.cpp`); the builder was fixing them. No result file. |
| harden-2 | just started: `tests/imconfig_throwing.h`, `tests/test_harden.cpp` (655 lines) exist; build dirs `build/harden-2*` with logs. No result file. |
| c-api | just started: `include/imtypeset_c.h` (570 lines), `src/imtypeset_c.cpp` (938), `tests/c_api_host.cpp` exist, unverified. No result file. |

## Remaining tasks, in dependency order

```
font-wild-finish ─┐
harden-2 ─────────┼─> demo ──┐
c-api ────────────┤          ├─> api-document ─> advtext-rework-plan ─┐
        tex-fixes ┤ py-host ─┘                                        ├─> docs ─> release-gate
                  └────────────────────────────────> dist ────────────┘
```

Backlog, NOT scheduled (D32.4, D34): tables and columns in the core (oracle exists:
`tests/golden/tables`), a public math-list entry, a C++ markdown adapter.

---

### font-wild-finish

Take over `font-wild`. Read `.concerns/font-wild-census--builder-5--result.md`,
`.concerns/review-font-stack--reviewer-2--findings.md` (W1-W14), `docs/font-support.md`,
`docs/hardening.md`, D25/D27/D32/D33, and `.concerns/docs-hygiene--builder-20--notice-for-opentype.md`.
First establish what is true NOW: build the font stack in a private dir and run `test_opentype`,
`test_fontlib`, `test_font_loader`, `test_opentype_wild` (IMTYPESET_WILD_ALL=1),
`test_opentype_formats`, `test_stress_font`, `test_font_loader_wild` if present.
Then fix the four defects the helper found (each has a failing test already; make it pass by fixing
the READER, not the test):
- D1 cmap format 12 glyph ids wrap past 2^32 (`OpenTypeFont::lookup`, `read_cmap_subtable`): a group
  `{first 0x41, last 0x44, glyph 0xFFFFFFFE}` answers glyph 0 / 1. Tests:
  `cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero`; the dense fuzz's
  `no_glyph_ids_wrapped` count must become 0 and its suppression be removed.
- D2 `read_legacy_kern` is quadratic in the subtable count (65,535 one-pair subtables: 0.7-0.9 s
  release, ~30 s ASan). Test `n3_a_kern_table_of_65535_subtables_is_read_in_one_pass`.
- D3 Type 2 arithmetic overflow gives inf/NaN ink boxes, and NaN passes `strict_check`. Test
  `type2_arithmetic_that_overflows_gives_no_box_that_is_not_finite`.
- D4 `strict_glyf` lets through a simple glyph whose header box has yMax <= 0 while its outline lies
  above the baseline; stb_truetype then asserts (`imstb_truetype.h:3358`). Test
  `a_glyph_drawn_above_a_header_box_at_the_baseline_does_not_reach_a_rasteriser`; the fuzz's
  `nothing_drawn_above_its_box` suppression must go.
Also decide and record: cmap format 13 with one group over the whole code space becomes 13 MB of
ranges and a 13 MB atlas font from a 28-byte table — keep ranges as ranges end to end (no expansion)
or cap with a reason; legacy kern sums reach layout unclamped — clamp to MAX_DIMEN at the metrics
boundary. Check what remains of the original task: W1-W14 each reproduced/fixed/not; the legacy `kern`
extension tested against fontTools (`tools/oracle/dump_kern_legacy.py`, `tests/golden/font-wild-kern`);
the loader run over >= 300 corpus faces under BOTH rasterisers with IsGlyphInFont agreeing with
`FaceData::has` (`tests/test_font_loader_wild.cpp`, item W14); the fontTools MIT notice applied to
`src/opentype.cpp` and `tools/oracle/check_font_bounds.py` and the THIRD-PARTY.md edit that file
describes; the measurement of how many sampled corpus kern pairs would differ if kern lookups
accumulated as OpenType says (590 faces have more than one); `docs/font-support.md` final census and
feature table; update the stale counts in `docs/hardening.md` (22 tests, 146 structured cases, 53
seeds). DONE: corpus zero mismatches in release, debug, ASan+UBSan; bundled goldens exact;
`test_stress_font` scaled 10x clean under ASan+UBSan with FreeType and with stb; full suite unnarrowed
in debug, release, ASan+UBSan, stb, 16-bit ImWchar.
Files: src/opentype.*, src/fontlib.*, src/font_loader.*, tests/test_opentype*.cpp, tests/test_fontlib.cpp,
tests/test_font_loader*.cpp, tests/test_stress_font.cpp, tests/font_builders.h, docs/font-support.md,
docs/hardening.md (font rows), tools/oracle/dump_kern_legacy.py, tests/golden/font-wild-kern,
tools/oracle/check_font_bounds.py, THIRD-PARTY.md (the fontTools entry).

### harden-2

Specification: `.concerns/harden-2--lead--scope.md` (items H1-H8, D3/D4/D6/D7, X1-X5, files, priority)
plus `.concerns/review-math-selection--reviewer--findings.md` and
`.concerns/review-draw-para-style--reviewer--findings.md`. Partial work exists
(`tests/test_harden.cpp`, `tests/imconfig_throwing.h`, logs in `build/harden-2*.log`): read it first,
establish which items it already covers by RUNNING it, and continue. Method as in `docs/hardening.md`:
reproduce first, fix at the cause, keep the repro, one row per item.

### c-api

A C ABI over the whole public API (`include/imtypeset.h` as revised, D36): `include/imtypeset_c.h`
compiles as C99 (-Wall -Wextra -Wpedantic) and as C++, includes no C++ header; opaque handles; POD
structs mirrored field for field with static_asserts in the .cpp proving size and offsets; enums as
int constants; text always (begin, end-or-NULL) or pointer+length; every output through
caller-provided buffers with a capacity and a returned required size; ImVec2/ImU32 as plain
struct/uint32; ImDrawList*/ImFont* opaque; a status return wherever the C++ entry can fail; a
build-facts function (version, sizeof ImWchar/ImDrawVert/ImDrawIdx, FreeType or stb) a binding calls
first. No exception crosses the boundary: under `#if defined(__cpp_exceptions)` each entry turns a C++
exception into an error status with a retrievable message (the one place try/catch is allowed).
`src/imtypeset_c.cpp` is thin. Tests: `tests/test_c_api.c` compiled BY A C COMPILER driving a headless
frame through `tests/c_api_host.cpp`, same vertices as the C++ API for the same paragraph;
`tests/test_c_api_map.cpp` walks a mapping table so a public declaration without a C counterpart
fails the build. Minimal edit to tests/CMakeLists.txt (enable C, one target) — say exactly what.
Update `docs/bindings-notes.md` §5 and `bindings/imgui_bundle/*` to the revised header. Partial work
exists (570 + 938 lines, unverified): read it, build it, continue. Configurations: debug, release,
ASan+UBSan, stb, 16-bit ImWchar, one with exceptions enabled. Read
`.concerns/api-revise--builder-23--result.md`, `.concerns/bundle-research--builder-21--result.md`,
`.concerns/py-host--lead--rulings.md`.
Files: include/imtypeset_c.h, src/imtypeset_c.cpp, tests/test_c_api.c, tests/test_c_api_map.cpp,
tests/c_api_host.cpp, tests/CMakeLists.txt (minimal), docs/bindings-notes.md, bindings/imgui_bundle.

### tex-fixes

Specification: `.concerns/tex-fixes--lead--rulings.md` (mechanism: reference()/product() rule presets,
no golden changes; which departures to fix, which not; the `easy_line` evaluation with numbers; the
flaky check count in test_stress_layout) with `.concerns/tex-web-pass--builder-25--result.md`, its
`--undeclared-departures.md` and `docs/divergences.md`. Render before/after images of `f_i`, `f_i^2`,
`\underline{x}` to `docs/images/texfix-*.png` via `tests/support/soft_raster.h` and LOOK at them.
Files: as that ruling lists (linebreak, pack, style, paragraph, blocks, math_mlist, their tests,
tests/test_tex_rules.cpp, tests/test_stress_layout.cpp, bench/bench_reflow.cpp, docs/performance.md,
docs/divergences.md, docs/decisions.md, one line in src/imtypeset.cpp).

### demo

A fully native runnable demo (user requirement). `ImTypeset::ShowDemoWindow(bool* p_open = NULL)` in
`src/imtypeset_demo.cpp` (own translation unit; `IMTYPESET_DISABLE_DEMO` compiles it to an empty
function; uses ONLY imgui.h and imtypeset.h; written as documentation), declared in
include/imtypeset.h and given its C counterpart in include/imtypeset_c.h. Sections, each a collapsing
header with a short comment: Paragraph (ImGui::TextWrapped / ragged / justified side by side, width
slider, alignment, line-break mode); Faces and sizes (mixed and coloured runs); Math (editable source
typeset live, inline and display, refusals with reason and position, the formulas of
`../imgui-adv-text/demo/math.md` the subset accepts); Selection (SelectableText, word/line
granularity, clipboard, the byte range shown); Inspector (line geometry, caret, hit test under the
mouse, debug overlay); Stress (document x1..x1000 in a scrolling child with the stats counters:
zero layouts in steady state, O(visible) draw); Fonts (registered faces, load a font from disk with
the refusal reason shown); Known limits (rendered with the library: word gaps from the pixel rule,
no fallback faces, no markdown). Hosts: `examples/demo_headless.cpp` drives it with scripted input
(`io.Add*Event`) and writes one PNG per section through the soft rasteriser when given a directory
(none: writes nothing, exits 0 — it runs under ctest); `examples/demo_sdl2/` (exists from shot-rig:
main.cpp, CMakeLists.txt, grab.sh) hooked to the library and the demo window, built against
`../example-project`'s SDL2 (`.concerns/shot-rig--builder-18--result.md`,
`.concerns/demo--lead--sdl2-is-available.md`), run through `tools/shot.sh` on Xvfb with the OpenGL3
and SDL_Renderer backends and through SDL's offscreen driver, each photographed; compare the three
rasterisations of one scripted frame. LOOK at every image and report defects first. Also the stb and
16-bit ImWchar builds of the host. `tests/test_demo.cpp`: every section opened for several frames in
debug+asan, default/stb/16-bit; steady frames do zero layouts unless controls changed; with
IMTYPESET_DISABLE_DEMO the symbol exists and draws nothing. Report what the Python prototype's
14-page demo has that this lacks.
Files: src/imtypeset_demo.cpp, include/imtypeset.h (one declaration), include/imtypeset_c.h +
src/imtypeset_c.cpp (its counterpart), examples/demo_headless.cpp, examples/demo_sdl2/*,
examples/CMakeLists.txt, tests/test_demo.cpp, docs/images/demo-*.png, THIRD-PARTY.md (SDL2 entry).

### py-host

A minimal runnable Python host with direct hooks into imtypeset (user requirement). Specification:
`.concerns/py-host--lead--rulings.md` — A. self-hosted (REQUIRED, runnable here): `python/imtypeset/`
is a pure-Python ctypes package over `include/imtypeset_c.h` (loads the library, runs the build-facts
check first, typed wrappers taking Python `str` and converting offsets with the core's helpers,
errors as exceptions carrying the core's reason); `python/host/` builds one shared library with
ImGui 1.92.9 + SDL2 (from `../example-project`) + imtypeset and a SMALL C surface for the host side
only (window, frame, events, screenshot, the dozen ImGui calls the demo needs — not a general ImGui
binding, and it says so); `examples/python/minimal_host.py` (about 60 lines: ImGui wrapped text
beside an imtypeset justified paragraph with inline and display formulas, a width slider,
drag-selection with copy; `--frames N --screenshot PATH`), run through `tools/shot.sh`, photographed
to `docs/images/py-host.png`, LOOKED at; per-frame cost measured from Python over 1,000 steady
frames. B. bundle-hosted (GATED): the same package inside an imgui_bundle app
(`examples/python/bundle_host.py`), built only if `../imgui_bundle/external/imgui/imgui/imgui.h`
exists at the commit the installed wheel pins (c9a0a84f8a4ddae794a9e1acf6a93afce73d5057); otherwise
skip with the exact commands a person must run. One package for both; `imtypeset.host_info()` says
which ImGui it resolved. `tests/python/test_binding.py` run headless with
`../imgui-adv-text/.venv/bin/python -P -B` (install nothing into that venv), wired into ctest as a
test that SKIPS with a reason when the interpreter is absent. Say plainly what this is not: a
packaged, portable distribution. CMake: a self-contained `python/CMakeLists.txt` behind an option.
Files: python/imtypeset, python/host, python/CMakeLists.txt, examples/python/*, tests/python/*,
docs/images/py-host.png.

### api-document

The host-neutral seam (D34): styled blocks in; layout, draw and selection out. Read
`.concerns/api-document--lead--notes.md` (requirements R1-R11 from `docs/advtext-migration.md` §3 are
the acceptance test; the coordinate-space ruling; product rules to take; goldens to replay),
`.concerns/blocks--builder-27--result.md` and `src/blocks.h` (`DocumentRules::product()`),
`.concerns/api-revise--builder-23--result.md` (how handles and positions generalise),
`.concerns/tex-fixes--*--result.md`. Public Document object: create/destroy; set or replace blocks
from an array of POD block descriptions (kind: paragraph, heading level, list item, code, quote
container, rule, host rectangle; nesting; marker text; tight; alignment; per-block style and ink;
text as byte ranges into one buffer with styled runs carrying face, size, colour, a host tag, inline
math and no-break flags; explicit formula ranges); INCREMENTAL edits costing O(changed) — replace a
range of blocks, append to the last block's text (streaming; note blocks' `prepare` is one O(blocks)
pass per change: make an append not pay it, or measure and justify); lazy layout (only blocks asked
for; unlaid heights estimated and corrected, scrollbar consequence documented); draw a clip window
in O(log blocks + visible) to an ImDrawList or buffers, with per-run colours, per-range fills and
highlights given as (block, byte) ranges, and the debug overlay; hit test and selection across
blocks in (block, byte offset) with word/line granularity, rectangles, caret, copy with stated
separators; per-block geometry; the host tag under the mouse (links); a widget that scrolls and
culls a document in a child window. Positions are (block, byte offset into the text the host handed
in). Then carry it through include/imtypeset_c.h (+ mapping test), python/imtypeset
(`Document.from_blocks([...])`, ONE binding call per edit), a Document section in the demo window
(long multi-block document, live width, find highlights, a streaming toggle showing O(1) blocks laid
out per frame) and `examples/python/minimal_host.py`. Tests: `tests/test_api_document.cpp` — the 69
block goldens and `tests/golden/doc` through the public API with the reference rules selected by a
TEST-ONLY hook, exact; lazy layout and incremental edits proven with work counters at N = 10,000;
steady frames allocate nothing; streaming append re-lays-out one block; copy across blocks incl. a
formula and code; the same through C and through Python. Tables: reserve the fields. Re-run and
re-photograph both demos.
Files: include/imtypeset.h, src/imtypeset.cpp, src/context.h, include/imtypeset_c.h,
src/imtypeset_c.cpp, python/imtypeset, src/imtypeset_demo.cpp, examples/python/minimal_host.py,
tests/test_api_document.cpp, tests/test_c_api_map.cpp, tests/python/*.

### advtext-rework-plan

User: "ensure that a plan for adv-text rework for imtypeset is created that will be fully optimized
for the performance gains of the typesetter." Measure first, then plan. SPIKE under
`python/spike_advtext/` (never in the reference's tree): take the reference's own parsed document
(`advtext.markdown.build_document` on `../imgui-adv-text/demo/sample.md`, imported read-only),
convert its blocks to imtypeset block descriptions in ONE binding call, draw in a frame through
`python/imtypeset`; measure parse, convert, layout, full draw, culled draw at x1/x10/x100, streaming
append, find per keystroke, resize reflow — each beside the reference's figure taken the same day
(`docs/performance.md` shows how the reference's bench scripts were run) — and break a frame into
Python view logic, binding-call overhead (calls per frame, cost of an empty call), native work,
ImGui; count what did not fit a block description. PLAN `docs/advtext-rework-plan.md`: per-view
target architecture with a budget of Python->native calls per frame (O(1) per view); ordered steps
each leaving advtext working and faster, with the gain expected FROM THE SPIKE and the benchmark
row that confirms it; what Python stays per-frame and what to do about each; costs the typesetter
does not remove (markdown-it per edit, difflib, find) ranked by measured share with options; core
API requests with the measured gain behind each
(`.concerns/advtext-rework-plan--<role>--core-requests.md`); the facade name by name
(`docs/advtext-migration.md`: `../pptmstr` imports nothing from advtext; 27 of 94 names cannot keep
their signature) and the map's alternative (a new thin layer rather than a retrofit); step 0 the
oracle freeze with the exact commands for a person to run in `../imgui-adv-text`; behaviour changes
users will see (`docs/divergences.md`); risks with the evidence that retires each. Every number
from a run in the session, naming script and command; estimates labelled and kept out of totals.
Files: docs/advtext-rework-plan.md, python/spike_advtext.

### dist

Ship the way Dear ImGui and hello_imgui do. Read `docs/distribution-notes.md`,
`.concerns/dist-research--builder-19--result.md`, `.concerns/dist--lead--from-dist-research.md`
(rulings 1-8), `.concerns/py-host--lead--rulings.md` ('For dist'), `docs/bindings-notes.md`.
Deliver: `docs/distribution.md`; `tools/amalgamate.py` -> `dist/imtypeset.h`, `dist/imtypeset.cpp`,
`dist/imtypeset_demo.cpp`, `dist/imtypeset_c.h/.cpp` (generated, gitignored, `--check`), compiled by
one plain compiler line as a ctest test (fix the anonymous-namespace collisions a unity build hits);
the floating-point question tested (ruling 7: the amalgamation built at -O3 -march=native with the
compiler's default contraction against the goldens; pragmas; `#error` under `__FAST_MATH__`);
CMake install + package config (`find_package(imtypeset CONFIG)`), never installing ImGui, target
contract `imgui::imgui` then `imgui`, fail rather than build a second ImGui inside a host; the build
system's ImGui check made a floor agreeing with the header; presets for the pinned ImGui, for
`../imgui` (1.93 WIP) when present, for exceptions-enabled with the bundle's config header when
present, and a unity configuration; tests that fail on 1.93 for the tests' own reasons fixed
(ruling 2); `vcpkg.json` and a port sketch (untested: say so); a consumer project test for both
add_subdirectory and the installed package. Full suite at the end in every preset.
Files: CMakeLists.txt, CMakePresets.json, cmake/*, tools/amalgamate.py, tests/CMakeLists.txt,
tests/smoke.cpp, tests/test_soft_raster.cpp, docs/distribution.md, vcpkg.json, ports/, .gitignore,
docs/decisions.md, mechanical symbol renames in src/ for the unity build.

### docs

README.md rewritten for an ImGui-community reader (pitch; a specimen and both demo screenshots; a
usage example COPIED from a compiled example and saying so; the three integration routes from
`docs/distribution.md`; the ImGui version floor and what was run against 1.93; what is and is not
supported incl. the known-gaps list of ruling R10 in `.concerns/api-revise--lead--rulings.md` and
the word-gap note with the MEASURED numbers in `.concerns/example--lead--word-gap-observation.md`;
performance citing `docs/performance.md` with its caveats; robustness citing `docs/hardening.md`;
fonts citing `docs/font-support.md`; the Python story exactly as far as it goes
(`docs/bindings-notes.md`, the py-host result); the relationship to advtext (D32, D34) and how to
regenerate goldens (`tests/golden/PROVENANCE.md`); licence status — none chosen, the user's
decision; THIRD-PARTY.md; what is missing before publication). `docs/spec-status.md`: the design
note `../imgui-adv-text/docs/cpp-extension.md` walked section by section — established, refuted
(§2's "unreachable from Python" is refuted for the font path; the custom ImFontLoader was not
built, D27; "snap_px matches IM_ROUND" is false), still open — every number traceable to a file or
command here; unverifiable claims marked. Fix stale statements found on the way (D1's scope
paragraph and "ImGui is pinned exactly" in CLAUDE.md are superseded by D32/D34/D36: update
CLAUDE.md to what is true now).
Files: README.md, docs/spec-status.md, CLAUDE.md.

### release-gate

The one terminal gate; its result is what the lead reports. From clean build dirs (keep `.deps`):
every preset configured, built, ctest, with all modules and tests; zero warnings; -O2 and -O3
(`-ffp-contract=off` visible in compile_commands.json); FreeType and stb; 16-bit ImWchar;
exceptions-enabled; unity/amalgamation; ImGui 1.93 from `../imgui` when present; clang-format
--dry-run -Werror over *.cpp and *.h (never *.inc); clang-tidy bugprone-*, performance-*,
clang-analyzer-* if it runs against the g++ database, else what blocked it; every
`tools/oracle/dump_*.py` re-run with `-P -B` and tests/golden byte-identical against a copy under a
`mktemp -d` directory (the corpus dump too if under ~15 minutes), then `tools/oracle/manifest.py`
regenerated and `--check`ed; stress tests scaled up once under ASan in FreeType and stb; the
real-font corpus with IMTYPESET_WILD_ALL=1 in release and ASan; the consumer-project check; BOTH
demos run unattended (`--frames N --screenshot`) — native SDL2 host and Python host — and both
screenshots LOOKED at; every number in README.md and docs/*.md that can be re-derived cheaply
re-derived, listing any that do not reproduce; licensing hygiene (THIRD-PARTY.md complete; grep
for pasted runs of tex.web by sampling distinctive lines; GPL/LGPL markers); private build dirs
removed. Fix what is red; report what cannot be fixed with the exact command and output. Final
report: `.concerns/release-gate--<role>--result.md` — command -> result table, test counts per
preset, corpus census, both screenshots, anything skipped with the reason. Do not commit.

## State at the last update (lead)

DONE since: draw-seams (one bound: neighbours < 1.5 px from laid-out distance; text straight after a formula still less even than reference — owner's call; 41/41 x5 on live tree), demo-guard (all three findings reproduced and fixed via GetContextSerial; demo images stale), doc-fixes (D45, D46; GetContextSerial added; font lifetime fixed in validate_fonts), py-fixes. docs DONE (README rewritten with run instructions; D47-D50). RUNNING: release-gate (the last task). Gate notes: .concerns/release-gate--lead--notes.md. (was: doc-fixes,
py-fixes, draw-seams, demo-guard (specs: board details; findings files `.concerns/review-4-*.md`).
THEN: docs (notes: `.concerns/docs--lead--from-fold-docs.md`, incl. the user's README request for
run instructions) -> release-gate (must wait for all four fix tasks AND docs, whatever the board
says; `amalgamation` preset must stay in the gate; clean builds, not incremental).

## After the gate (lead)

release-gate: GREEN from clean in all eleven presets (41/41; debug and python-host 42/42), zero
warnings from our sources; `.concerns/release-gate--builder--result.md`. SDL2 vendored in `.deps/sdl2`
(D51). RUNNING: `final-touches` (spec on the board: SDL2 pkg-config fallback, demo "Known limits"
text, fp-contract on our ImGui targets, dump_font_wild --jobs, bench warm-up, housekeeping, then every
preset from clean on the final tree). OPEN FOR THE USER, not scheduled: tight justified lines (the
line breaker shrinking spaces at screen sizes) — the most visible flaw in every picture; the
stand-in-headers policy for the wheel sidecar; the letter-evenness trade; text-after-formula joint;
licence. Git: one commit pushed by the user (abad89c), amended locally to drop an attribution trailer
(5310ff3, needs the user's force-push); everything since is uncommitted. No attribution, ever.

## advtext FULL SWAP — the core's half (user direction relayed 2026-10-10 late; lead)

The request, the border ruling and the eight items are summarised in `docs/advtext-swap-status.md`
(the file the other session reads; keep it current) — the border: imtypeset owns everything TeX does
(line breaking, packing, math, ALIGNMENT = tables, vsplit = COLUMNS), fonts, selection and drawing of
what it set, and the specimen and formula galleries; advtext owns markdown, streaming, DiffView, find,
themes, view interaction, hosted blocks. Committed base: `5654eee` (local; the user pushes).
Board (specs are the board details): RUNNING oracle-freeze, swap-api-answers, galleries;
THEN tables-core -> columns-core; diffview primitives when
`../imgui-adv-text/.concerns/FS-S1--builder--result.md` exists (also FS-S2 = table/column
requirements, FS-S3 = inventory: check for them); a reviewer after tables-core; a terminal gate
(all presets from clean) and a commit named in the status file after each landed stage.
Git: the LEAD commits at stage boundaries, no attribution; stage by explicit file list — the repo
root holds sandbox device dot-files (.idea .vscode .zprofile .zshrc …) that `git add -A` trips on.
