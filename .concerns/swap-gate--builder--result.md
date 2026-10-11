# swap-gate — result (builder, 2026-10-11, 06:19 to 07:55)

GCC 12.2.0, Linux x86-64, FreeType 2.12.1 (stb where said), `-j4`, one heavy thing at a time, every
binary and every Python process under `( ulimit -v 6000000; timeout … )` or, sanitised,
`ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`. Nothing staged or committed. Nothing written
outside the repository (`../pptmstr` clean at `4974657` before and after; nothing written in
`../imgui-adv-text`, whose own working tree has 138 changed paths that are not mine).
`build/advtext-reference` and `.deps/` untouched. Step by step:
`.concerns/swap-gate--builder--progress.md`. Words: **RAN**, **READ**, **NOT RUN**.

## The preset table (the FINAL pass: after the last change to anything a preset compiles or reads)

`rm -rf build/X; cmake --preset X; cmake --build --preset X -j4; ctest --preset X -j4`, in this
order, one at a time. "Warnings" = lines matching "warning" in the build log.

| preset | configure | build: exit, time, warnings | ctest |
|---|---|---|---|
| `debug` | 0 | 0, 57 s, 0 | **51 of 51** (112 s), `consumer_projects` Passed |
| `release` | 0 | 0, 87 s, 0 | **50 of 50** (64 s) |
| `asan` | 0 | 0, 110 s, 0 | **50 of 50** (212 s) |
| `stb` | 0 | 0, 59 s, 0 | **50 of 50** (79 s) |
| `wchar16` | 0 | 0, 57 s, 0 | **50 of 50** (70 s) |
| `amalgamation` | 0 | 0, 58 s, 0 | **50 of 50** (85 s) |
| `fp-host` | 0 | 0, 80 s, 0 | **50 of 50** (46 s) |
| `exceptions` | 0 | 0, 104 s, 0 of ours; 4 `-Wterminate` | **50 of 50** (231 s) |
| `bundle-config` | 0 | 0, 67 s, 0 of ours; 4 `-Wterminate` | **50 of 50** (86 s); `test_exception_safety` SKIPS here (below) |
| `imgui-next` (`../imgui` 19298, `2859f6723`) | 0, the expected "newer than tested" warning | 0, 56 s, 0 | **50 of 50** (79 s) |
| `python-host` | 0, SDL2 from `.deps/sdl2` | 0, 53 s, 0 | **52 of 52** (86 s), `python_binding` and `python_demo_sections` Passed |

**Nothing red.** The 492 regular files of the tree outside `.concerns/`, `build/`, `.deps/` hashed
the same before and after this pass. The tree it ran on differs from HEAD `b9a5091` in three files:
`src/blocks.cpp`, `tests/test_exception_safety.cpp`, `examples/demo_headless.cpp`. **Written after
the pass, nothing a preset compiles or reads:** `README.md`, `docs/distribution.md`,
`docs/hardening.md`, `docs/decisions.md` (D65), seven `docs/images/demo-*.png`. Nothing was re-run
for them except `clang-format --dry-run` (124 files, exit 0) and `manifest.py --check` (160 match).

The new tests were each seen `Passed` in every preset of the first pass (`test_align`,
`test_vsplit`, `test_columns`, `test_api_tables`, `test_api_tables_golden`, `test_api_columns`,
`test_api_requests`, `test_api_answers`, `test_specimen`); the final pass was checked by its counts
and by `test_exception_safety`, `example_demo_headless`, the Python tests and `consumer_projects`.
The eight `-Wterminate` are in ImGui's `~ImDrawListSharedData`, `~ImFontAtlas`, `~ImGuiContext`,
`~ImGuiWindow` (READ in the first pass's logs; counted in the final's). `compile_commands.json` of
all eleven (first pass): `-ffp-contract=off` on every entry except by design the three drop-in
files of `fp-host`; no fast-math; `release` and `fp-host` at `-O3`.

**Three passes, to be exact about it.** `debug` alone at 06:23 on HEAD (51 of 51). Then the fix
below; then all eleven (06:29 to 06:57): green with the counts above. Then one example edited
(`examples/demo_headless.cpp`); then all eleven again (07:15 to 07:45): the table.

## What I fixed, and why

1. **A real defect, older than this work: `src/blocks.cpp`, `Document::splice` and
   `Document::add_block`.** `test_exception_safety` swept no table and no column (READ), so I
   added one small sweep (`a_throw_from_anywhere_in_a_table_cut_into_columns_leaves_the_document_usable`:
   `SetDocumentBlocks` with a table, `LayoutDocument`, `SetDocumentColumns` in order and balanced,
   `GetDocumentColumnCount`, `DrawDocument`). Its first run in the exceptions configuration (RAN,
   private directory): **ASan heap-buffer-overflow, WRITE of size 68, `Document::reset_block` <-
   `splice` <- `ReplaceDocumentBlocks` <- `SetDocumentBlocks`** (the report:
   `.concerns/swap-gate--builder--asan-reset_block.txt`). Cause: the blocks, their layouts and
   their slots were grown in one loop keyed on the blocks' length; a `bad_alloc` from the second or
   third growth left the blocks longer, and the next call wrote a slot that was not there. It is
   not a table's or a column's defect: any `SetDocumentBlocks`/`ReplaceDocumentBlocks` unwound
   from and repeated reaches it, and the older sweeps described their documents before arming the
   allocator. Fix: each sequence grown by its own length (`grow_to`), both sites; no behaviour
   changes where nothing throws. After: 346 (in order) + 349 (balanced) unwinds, 8,191 checks; the
   document is also laid out, hit-tested and drawn as it stands after every throw, then described
   again and held to the vertices of an undisturbed document. Whole test in `exceptions`: 10 tests.
   **Reaches a user only in a build with exceptions on that survives `bad_alloc`** — which is the
   Python sidecar's configuration. Recorded: D65, `docs/hardening.md` X6.
   NOT swept: `AppendDocumentTableCells`, `AppendDocumentText` into a cell,
   `SetDocumentTableColumns`, `DocumentView` over columns.
2. **`examples/demo_headless.cpp`**: the Known limits page was cut off at the bottom of its 660 px
   (a bullet was added since; now 800), and no page showed the table or columns: new page
   `demo-document-columns` (Blocks slider to its left end, Columns to 2, wheel sideways to the last
   column). 17 pages.
3. **`docs/images`**: six committed demo pages were stale against a fresh render (document 56,294
   px, limits, math 558, math-stream 558, section-math 1,964, stress 39) and are replaced; one
   added. Each is 0 px from the release preset's render (lossless recompress). Ten were already
   pixel-identical.
4. **Documents.** `README.md`: preset table from this run, eleven demo sections, seventeen pages,
   88 public functions (was 72), 24 probe rows (was 22), `bundle_demo.py`, the swap status file,
   advtext being rewritten, tables/columns rough edges as seen, a false sentence ("a cut takes no
   account of … a table's header": D63 keeps it), "No test replays it yet" (one does), golden
   checks of today, which sections were re-run today and which were not. `docs/distribution.md`:
   section 7's table; 95 + 8 exports and 117 undefined ImGui symbols with the counting method (was
   78 / 51, method unstated); **"`test_exception_safety` does its work only here and in the next"
   was false for `bundle-config`** and is corrected.

## What is red

Nothing. Found and NOT changed:

- **`bundle-config` runs `test_exception_safety` as a skip** (RAN: `build/bundle-config/tests/test_exception_safety`
  prints `SKIPPED: this build's IM_ASSERT does not throw, or it has no exceptions`, `1 test(s), 0
  check(s)`). The test keys on a macro only `tests/imconfig_throwing.h` defines and catches that
  header's exception type; the bundle's header throws `std::runtime_error`. So it was at every
  earlier gate; the document claimed otherwise. Preset left alone (D65 says what changing it takes).
- **`src/imtypeset_demo.cpp`**: `(float)(int)(left.x + 0.5f)` (clang-tidy `bugprone-incorrect-roundings`;
  the spelling D18 forbids), placing the second letter of a kerning pair in the Specimen. Demo
  only, positive width; changing it can move a drawn letter. And `memcmp` of two `ImTypesetStyle`
  in `SameSetting` (twelve 4-byte members, no padding: READ, not a defect).
- `docs/advtext-swap-status.md` was not edited (as instructed). **It needs, from the lead:** the
  commit; the preset table above in "Build from"; and one line for the other session — a
  `SetDocumentBlocks`/`ReplaceDocumentBlocks` that raised `MemoryError` could corrupt the document
  on the next call before this commit.
- `CLAUDE.md` not edited. Its preset table does not say `bundle-config` skips the unwinding test.

## Command -> result (all RAN, on the tree with the `blocks.cpp` fix; the Python and demo rows before the `demo_headless.cpp` edit, which none of them builds)

| what | result |
|---|---|
| `clang-format` 14.0.6 `--dry-run -Werror`, 124 `*.cpp`/`*.h`/`*.c` (no `.inc`) | exit 0, at the start and at the end |
| `clang-tidy` 14.0.6, `-p build/debug`, `-*,bugprone-*,performance-*,clang-analyzer-*`, 22 `src/*.cpp` | 0 errors, 100 distinct warnings: 87 swappable-parameters, 2 padding, 5 implicit widening, macro-parentheses, unused-return-value, a dead store, a copy, and the two new ones above. `tests/`, `examples/`, `bench/` not linted |
| licensing: `tex.web` paste | all 11,421 lines of 45+ characters against 399 source/doc files: 9 hits, all the notice in `THIRD-PARTY.md`. The 246 lines of 25 to 44 characters from sections 768-812 and 967-979 against `src/align.*`, `src/vsplit.*`, `src/columns.*`: 0 |
| `tools/oracle/pin_reference.sh` | `32e3953`, 179 files, matches `REFERENCE.lock` |
| oracle, pinned tree, reference interpreter `-P -B` | `--verify`: `dump_core` 599 cases / 6,624 checks, `dump_para` 253 / 2,088, `dump_selection` 57 / 25,267, `dump_blocks` 69 / 284, `dump_tables` 64 / 262, `dump_doc_extras` (91 s), `dump_math` 8,555 / 10,728; `--check`: `dump_doc_tables`, `dump_specimen` identical; `dump_specimen --verify`; `gen_mathtext_tables.py --check`: all exit 0. `manifest.py --check`: 160 match |
| both sidecars from clean, `build_bundle_library.sh` (advtext venv 1.92.900 / 1.92.9; pptmstr venv 1.92.801 / 1.92.8) | exit 0, 35 s and 34 s, 0 warnings, stand-in headers, accepted by probe, 24 of 24 |
| in BOTH venvs | `run_gate` accepted; `mutate_gate` 30 offsets, 87 mutants, 12 crashed, **0 survived**; `check_document_edits` 23 PASS 0 FAIL; `check_swap_answers` 18 PASS 0 FAIL; `font_lifetime` all seven scenarios exit 0, `application_fonts_removed` 0; `bundle_host.py --frames 30` on Xvfb: `selection: (74, 284) layouts: 2`, exit 0; `bundle_demo.py` 8 runs on Xvfb: exit 0, `last error: 0` |
| pptmstr venv | `pptmstr_patch_faults.py`, all 12 scenarios: exit 0 |
| the patch | `git -C ../pptmstr apply --check` exit 0 at `4974657`; numstat 750 added / 19 removed; applied to a `git archive` copy: `rich_pane.py`, `typeset.py` byte-equal to the overlay's |
| **does the overlay still work against this tree?** | **Yes.** Proof run (real `pptmstr.app.main --fake`, overlay, Xvfb, 240 frames): exit 0, `imtypeset: in use`, wheel 1.92.801, 3.02 library calls per `rich_pane.draw`, median 75.3 us. The overlay reads hits by field name and uses `Host` blocks, so neither struct change reaches it. **Patch not regenerated** |
| `build/demo` by the README's two lines (+ `-j4`) | SDL2 from `.deps/sdl2`, 36 steps, 0 warnings; `--frames 3` exit 0; `SDL_VIDEODRIVER=dummy` command exit 0; `--open all --frames 5` exit 0; Xvfb grab exit 0, x11, llvmpipe, **301 px** between X grab and read-back (README had 46 and 79; not looked into, grab not looked at) |
| `example_demo_headless` (release preset) | `17 pages, ok`; missing directory exit 1 |

## Not run, and why

- **The font corpus** (dump or `IMTYPESET_WILD_ALL` tests): forbidden / memory.
- `dump_draw`, `dump_font`, `dump_font_synth`, `dump_para_map`, `dump_kern_legacy`: no read-only
  mode; they would rewrite goldens in place. The manifest says the files are unchanged; that they
  still regenerate identically was last shown at `oracle-freeze`.
- The x10 stress runs, the `-O2` build, benchmarks as timings (load average 4 to 9 from other
  work), ImGui 1.92.0/1.92.8 floors, `tools/amalgamate.py --check` by hand (the `amalgamation`
  ctest ran in every preset).
- ruff/black/mypy/pytest on the patched pptmstr copy (patch unchanged since the release gate).
- README sections 4 (benchmarks) and 5 (`cmake -S python` standalone, `minimal_host.py`) by hand;
  the `python-host` preset built and tested the same library.
- `demo_sdl2` without `--frames`; clang, MSVC, macOS, Windows, a GPU, a real mouse, HiDPI.
- The rewritten advtext itself; `make run` in a patched pptmstr.
- Python tests under ASan.

## Pictures looked at, defects first

**Headless demo pages, all seventeen** (release, soft raster, 900 px wide):

- **demo-limits** (before the fix): the sixth bullet sliced by the page's bottom edge, the seventh
  missing. Fixed (800 px); all seven inside.
- **demo-document**: shows the top of a 2,001-block document, one column; the table and columns
  are not in it. Hence the new page.
- **demo-document-columns** (new): ten blocks in two 440 px columns, view at the right end. The
  table is cut across the columns: header, rule and first row at one column's foot, two rows at
  the next one's head, no header repeated. The table is wider than a column, so it is squeezed and
  "The golden / ratio", "Text that / arrives" wrap. The gap under the header is two leadings with
  the rule nearer the body (the reference's, T20).
- **demo-math-subset**: wide display formulas and the long inline one are clipped at their cell's
  right edge ("a long one", "all of it at once", "bars over parts", "rules"); long sources wrap.
  Known.
- **demo-formulas / demo-section-formulas**: `\oint_\gamma`'s subscript stands off the sign
  (the reference's); script-script type in the continued fraction is a few pixels tall.
- **demo-paragraph, demo-math (sentence), demo-faces**: tight and loose justified lines side by
  side (the line breaker's shrink; README).
- **demo-math, demo-math-stream, demo-section-math**: right; the new line "Not here: a table
  cell. The table that ends the Document section has one." is what changed in them.
- **demo-specimen, demo-section-specimen, demo-selection, demo-inspector, demo-stress,
  demo-fonts**: nothing wrong seen.

**The Document section with controls moved** (swap-fixes' scratch driver against `build/release`,
1100 x 720, ten blocks; copies `.concerns/swap-gate--builder--shot-document-*.png`): `c1-end` the
table at the end of one column, right-aligned Value column sharing an edge; `c2`, `c2-end` two
columns, table whole in the last; `c3` three in order: **"gutter." alone at a column's head and a
code block's first line alone at a foot**, first baseline of the heading's column lower (all three
are open border questions); `b3` balanced into three, taller than the view, scrolls down. Nothing
new wrong.

**Python, Xvfb + llvmpipe** (advtext venv: window-document, window-formulas, page-document,
page-specimen, page-formulas, page-math; pptmstr venv: window): demo window and pages draw as the
headless ones do; version line reads 1.92.9 / 1.92.8. Seen: **in the Document page and window
justified lines end hard against the view's scrollbar**; the page's view is about 340 px of a 760
px page; `--scroll` does not move the document view. The other nine were compared, not looked at
(0 to 162 px from their twins). **No Python picture shows the table or more than one column.**
`pptmstr-proof-run`: CONTEXT pane in rich mode, four lines in dim, white, cyan; too little text to
judge the look.

None of this shows a GPU, HiDPI or a real pointer.

## Left behind

`build/`: the eleven preset directories, `build/demo`, `build/bundle-advtext`,
`build/pptmstr-bundle`, all from this run and this tree; `build/advtext-reference`. Mine removed:
`build/gate-exc`, `build/gate` (logs, scratch renders). Not mine, not removed: `build/oracle-fixes`,
`build/lead-*`. **Untracked and needing `git add` by the lead:**
`docs/images/demo-document-columns.png`.

## Least sure of

1. **How far the unwinding guarantee really goes for documents.** One sweep of one sequence found
   a memory error on its first run in code two gates had passed. The append paths and the view over
   columns are unswept, and I would not bet they are clean.
2. The final pass's per-test `Passed` lines were read for a handful of tests, not all fifty; the
   counts and exit codes are what the table rests on.
3. The X grab's 301 px against 46 and 79 before: unexplained.
