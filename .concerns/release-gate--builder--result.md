# release-gate — result (builder, 2026-10-10, 13:50 to 15:40)

GCC 12.2.0, Linux x86-64, FreeType 2.12.1 (stb where said), `-j4`, one preset at a time, every
binary under `( ulimit -v 6000000; timeout … )` or `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`.
Nothing committed or staged. Nothing written outside the repository (scratch under `$TMPDIR` in a
`mktemp -d` directory, removed). The step-by-step record is
`.concerns/release-gate--builder--progress.md`.

## First: the unchecked interaction (soft errors against the patch's three-failing-frames rule)

**Established by running, in pptmstr's venv, through a freshly built sidecar: it is not a
defect.** A standing soft error does not count as a failing frame.

New scenario `soft-standing` in `python/spike_bundle/pptmstr_patch_faults.py`: one paragraph of
the page is described with its runs out of order, which the library applies and reports as
`DescriptionCut`. Result, 70 frames: `Document.draw` answered False in 70 of 70, `last_error`
code 12 each time, `RichState.typeset_failures` never above 0, 0 frames by the classic renderer,
imtypeset in use at the end, `reason()` empty, `trouble()` holding the library's sentence
("block 1: runs must be in ascending order …"). Why: `python/imtypeset` returns False and does
not raise for the SOFT codes, and the overlay's `_emit` does not read `draw`'s return value; only
an exception increments the counter. No Python change was needed and the patch was not
regenerated. Not covered: a `Table` block (the overlay never emits one; tables are `Host`
blocks), and a soft error from a call other than `draw`/`layout`.

## Is the tree green

**Yes, in these configurations, each from a build directory that did not exist before it:**

| preset | configure | build (warnings from our sources) | ctest |
|---|---|---|---|
| `debug` | 0 | 0 | **42 of 42** (run twice: at the start, and again on the final tree) |
| `release` (-O3) | 0 | 0 | **41 of 41** (twice, likewise) |
| `asan` | 0 | 0 | **41 of 41** (tests 618 s) |
| `stb` | 0 | 0 | **41 of 41** |
| `wchar16` | 0 | 0 | **41 of 41** |
| `amalgamation` | 0 | 0 | **41 of 41** |
| `fp-host` | 0 | 0 | **41 of 41** |
| `exceptions` | 0 | 0 of ours; 4 `-Wterminate` | **41 of 41** |
| `bundle-config` | 0 | 0 of ours; 4 `-Wterminate` | **41 of 41** |
| `imgui-next` (`../imgui` 19298, `2859f6723`) | 0, the expected "newer than tested" warning | 0 | **41 of 41** |
| `python-host` | 0 | 0 | **42 of 42**, `python_binding` Passed (twice: with SDL2 from the sibling project, and after the SDL2 change from `.deps/sdl2`) |
| private, not a preset: `-O2` (`RelWithDebInfo`) | 0 | 0 | **41 of 41** |

The eight `-Wterminate` warnings were read: all where a throwing `IM_ASSERT` is expanded in Dear
ImGui's own destructors (`~ImDrawListSharedData`, `~ImFontAtlas`, `~ImGuiContext`,
`~ImGuiWindow`). `-ffp-contract=off` is on every C++ compile line of ours in every database
read (debug, release, asan, stb, wchar16, amalgamation, -O2), and by design not on the three
drop-in files in `fp-host`. No fast-math flag anywhere.

**Which state of the tree each run saw.** `asan` … `imgui-next` were built after my only edit
under `src/` (a comment in `src/draw.h`) and before four later edits that cannot change what
they compile or read as records: comment lines in six `tests/golden/tables` files, a comment in
`examples/CMakeLists.txt`, the `python-host` entry of `CMakePresets.json`, documents. `debug`,
`release` and `python-host` were run again after those. I did not re-run the other eight.

## What is not green, or not known

Nothing failed that I could not fix. Not known / not run:

- **clang, MSVC, macOS, Windows, a real display, a GPU, a real mouse**: not available here.
- **`build/demo/demo_sdl2` with no `--frames`** (a window left open): no display; it prints
  `video_driver=offscreen`.
- **The pptmstr patch applied in `../pptmstr` and `make run`**: may not write there. What ran is
  below.
- **Benchmarks as timings.** `bench_font` (x3) and `bench_draw` exit 0, but the load average was
  5.0 to 5.5 with nothing of mine running, so no number is a result (full draw x1 read 180.8 us).
  The lead's question whether `bench_font`/`bench_reflow` read high when started cold is **not
  answered**; `bench_reflow` was not run.
- **ImGui 1.92.0 and 1.92.8 floors** (`docs/distribution.md` section 6): not re-run.
- **`tests/`, `bench/`, `examples/` under clang-tidy**: only `src/*.cpp` was linted.
- The "no SDL2 anywhere" configure failure with `.deps/sdl2` absent; a system SDL2 package.

## Command -> result

| what | command (abridged) | result |
|---|---|---|
| presets | `cmake --preset X && cmake --build --preset X -j4 && ctest --preset X -j4` after `rm -rf build/X` | table above |
| format | `clang-format --dry-run -Werror` (14.0.6) over 106 `*.cpp`/`*.h` | 1 red (`bindings/imgui_bundle/pybind_imtypeset.cpp`, a blank line), fixed; 0 |
| lint | `clang-tidy -p build/debug --checks='-*,bugprone-*,performance-*,clang-analyzer-*'`, 19 `src/*.cpp` | **ran** against the g++ database; 0 errors; 80 distinct warnings: 116 hits of `easily-swappable-parameters`, struct padding, and 10 others each read (constant widenings, a bounded `6 * depth`, an intended string continuation, `unique_ptr::release()` after the map took the pointer, one dead store in `linebreak.cpp`, one copy). None read as a defect; nothing changed |
| goldens | all 13 `tools/oracle/dump_*.py`, `-P -B`, into a `mktemp -d` copy with the output directory deleted first, `diff -r` | 12 directories byte-identical (font corpus: 2,991 faces, 87 files, 474 s). `tables`: records identical, 9 comment lines a file differ (see "Fixed") |
| manifest | `manifest.py` (reference interpreter), then `--check` | 157 files match |
| corpus, release | `IMTYPESET_WILD_ALL=1 test_opentype_wild`, `test_font_loader_wild` | 2,991 visited; **2,990 read equal, 1 refused (NotoColorEmoji, bitmaps), 0 mismatched**; 8,389,075 ink boxes, 441,304 kern pairs; loader: 0 disagreements over 939,193 codepoints |
| corpus, ASan+UBSan | the same two from `build/asan` | exit 0, exit 0 (237 s, 328 s) |
| stress x10, ASan, FreeType | `IMTYPESET_STRESS_ITERS=10` text / layout / font | exit 0: 1,810,658 / 1,477,124 / 21,901,056 checks |
| stress x10, ASan, stb | the same from a private asan+stb build | exit 0, same check counts for text and layout; font 48,800 mutants |
| consumer projects | ctest `consumer_projects` in `debug` | Passed (both runs) |
| drop-in files | `tools/amalgamate.py --check`; `c++ -std=c++17 -c imtypeset.cpp -I<imgui>` | current (5 files; `imtypeset.cpp` 29,231 lines); exit 0 |
| sidecars | README 6 and 7: `build_bundle_library.sh` for each venv | exit 0 twice; gate accepted by probe, 22 probes; **78** `ImTypeset_*`, 8 `ImTypesetBundle_*` exported, 51 ImGui symbols undefined |
| Python, both wheels | `check_document_edits.py` | 17 PASS, 0 FAIL in each |
| | `font_lifetime.py` x 7 scenarios | all exit 0 in each wheel, `application_fonts_removed` 0, `same-count-cycle` included |
| | `bundle_host.py --frames 30` on Xvfb | `selection: (74, 284) layouts: 2`, exit 0, each |
| Python, pptmstr wheel | `pptmstr_patch_faults.py`, 12 scenarios | all exit 0 (`soft-standing` above) |
| patch | `git -C ../pptmstr apply --check` at `4974657` (clean) | exit 0; `--numstat` 750 added, 19 removed |
| | patch applied to a `git archive` copy under `build/` | patched files byte-equal to the overlay's; ruff 0, black 0, mypy clean (47 files), pytest 2,569 passed, 3 xfailed |
| proof run | plan "Start here": real `pptmstr.app.main --fake` with the overlay, Xvfb | exit 0, `imtypeset: in use`, wheel 1.92.801, **3.02** library calls per `rich_pane.draw` |
| demo, README 1 | two configure/build lines (+ `-j4`), then the three run commands | built twice from clean (33 steps, 0 warnings); `--frames 3` exit 0; `SDL_VIDEODRIVER=dummy` exit 0, 8,240 vertices; offscreen exit 0; Xvfb grab exit 0, `video_driver=x11`, llvmpipe, X grab 79 px then 46 px from the read-back |
| examples, README 2 | `example_minimal`, `example_specimen [dir]`, `example_demo_headless [dir]` | `3 frames, 1 layout(s), 878 vertices`; exit 0, seven PNGs; `9 sections, ok`, nine PNGs; exit 1 for a missing directory |
| Python host, README 5 | `cmake -S python …`, ctest, `minimal_host.py --frames 30` (dummy, and Xvfb) | built twice from clean; `python_binding` passed; `selection: (74, 284) layouts: 2`, exit 0 |
| README numbers | `IMTYPESET_WORD_GAPS_REPORT=1 test_word_gaps`; `test_harden --filter=d7_every -v` (release) | 530 and 11 of 37,778; 13,348 of 150,624; 673 and 280 of 8,208; 13 over 1 px, worst 1.26; 18,990 joints; 0.95 against 10.17; 20,392 of 121,408: all reproduce |
| licensing | greps | no attribution line anywhere but the rule in CLAUDE.md; GPL/LGPL only in policy text; of 11,421 lines of `tex.web` of 45+ characters, 9 occur in the tree, all the notice quoted in THIRD-PARTY.md |

## Fixed (file, why)

| file | what and why |
|---|---|
| `tools/oracle/dump_font_wild.py` | **Red:** exits 1 under `-P` (`ModuleNotFoundError: dump_font`); alone among the dump scripts it did not put its own directory on `sys.path`. One line, as the others do |
| `tests/golden/tables/*.golden` (6), `MANIFEST.sha256`, `PROVENANCE.md` | The reference was committed to today (history rewritten; `a3c589a` to `30496e1`, nothing under `advtext/`). The regenerated tables differ only in the comment header naming the commit; taken as regenerated so that goldens and provenance name one commit. D51 |
| `bindings/imgui_bundle/pybind_imtypeset.cpp` | clang-format: one blank line |
| `src/draw.h` | comment: 1,023 quads, with the derivation (checked against `imgui_draw.cpp` and `BATCH_QUADS`); `src/draw.cpp` holds no such sentence |
| `python/spike_bundle/pptmstr_patch_faults.py` | scenario `soft-standing` |
| `docs/distribution.md` | 78 exports (was 77); preset table now "from an empty directory", -O2; SDL2 |
| `docs/advtext-rework-plan.md` "Start here" | 750 added (672 not blank); `typeset.py` 131 lines (was 128); pytest 2,569 passed 3 xfailed; `soft-standing` |
| `docs/spec-status.md` | the `imgui_internal.h` grep finds two comment lines, not one (three verdicts spot-checked: `imgui_internal.h` not needed, no `ImFontLoader`, `snap_px` against `IM_ROUND`: all hold) |
| `README.md` | preset table filled; golden paragraph; picture statements; X-grab pixel count; SDL2 prerequisite and commands |
| `docs/images/demo-stress.png` | re-rendered (below) |
| SDL2 (the lead's relayed request): `examples/demo_sdl2/CMakeLists.txt`, `python/CMakeLists.txt`, `examples/CMakeLists.txt` (comment), `CMakePresets.json`, `tools/vendor_sdl2.sh` (new), `THIRD-PARTY.md`, `docs/decisions.md` D51 | `.deps/sdl2` is the fallback after an installed SDL2; explicit `IMTYPESET_SDL2_DIR` wins over both. `git grep example-project` outside `.concerns`: only two historical lines (`docs/hardening.md`, `docs/port-hazards.md`). `grep -c example-project` in `CMakeCache.txt` and `build.ninja` of `build/demo`, `build/py-host`, `build/python-host`: 0 in all six. The script, run in scratch copies, reproduces the hand-made `.deps/sdl2` file for file from the build tree (it skips `SDL2_image/mixer/ttf`, which a first version copied) and from an install prefix, refuses without `--force`, refuses a directory that is not SDL2 |

## Lead's notes, each

- `amalgamation` after demo-guard's statics-to-structs: 41 of 41 from clean.
- Not re-run by doc-fixes (exceptions, stb, wchar16): all 41 of 41.
- try/catch: two sites in `src/` (`imtypeset_c.cpp`, the macro in `context.h`); the documents say so.
- Quad bound: 1,023; comment fixed.
- demo-guard's unrecorded sanitised runs: `test_demo` passed in `asan` and `exceptions` here.
- "Start here" numbers: corrected.
- 77/78 exports: 78; corrected.
- **"All nine demo images are stale": refuted for eight.** A fresh render from the final tree is
  pixel-identical to the committed file for eight pages (raw-pixel hashes equal). Only
  `demo-stress.png` differed (627 px: a counter, and an inline formula and the word after it
  moved by the draw-seams rule) and was replaced.
- bench cold start: not answered (machine loaded).

## Images looked at, and what they show (defects first)

All nine `docs/images/demo-*.png` as freshly rendered, 900 px wide, soft raster:

- **paragraph, document, math (the sentence under "$...$ in a sentence")**: tight justified
  lines. "this page is ordinary text, converted" and "macro processor behind it" stand with
  spaces nearly closed while neighbouring lines are loose. This is the line breaker's shrink
  (README, rough edges), and it is the first thing an eye lands on.
- **math**: the subset table clips long sources at the column edge and clips two wide display
  formulas at the right edge of their cell (`a +` cut off); `\int_0^1` has its limits standing
  well clear of the sign; the nested fraction in text style is a few pixels tall and barely
  legible; refusals show reason and byte in orange as meant.
- **limits**: the "Word gaps" item still describes the reference's pen rule ("the remainder
  falls into the gap after it"), not the rule the page is drawn with. **Stale text in
  `src/imtypeset_demo.cpp`, not changed** (a visible string; now said in the README).
  Replacement characters for CJK and emoji as described.
- **faces**: the first justified line has wide gaps round `code()`; otherwise the five faces,
  sizes and the coloured run are right.
- **selection, inspector, stress, fonts**: nothing wrong seen. Highlights cover whole formulas;
  line boxes and baselines line up; stress shows 0 layouts in the last frame; a non-font file is
  refused with the reason.

Others (copies beside this file, `release-gate--builder--*.png`):

- `demo-sdl2-opengl3-xvfb.png`: the SDL2 demo in an X window, OpenGL 3 by llvmpipe. Same content
  as the soft raster; Host window reads x11, FreeType, 32-bit. In the SDL software read-back
  (not kept) ImGui's own separator lines change shade halfway along; that is the SDL software
  renderer on ImGui's line, not this library.
- `py-host-xvfb.png` (self-hosted Python host): wrapped ImGui text beside the justified
  paragraph, selection highlighted across two inline formulas, display formula centred, the
  small `Document` with heading, link colour, list item and code. No defect seen.
- `py-bundle-advtext.png` (bundle host, advtext's wheel, dark theme): the same page without the
  `Document` part. README 6 calls it "the page of 5"; it lacks that section.
- `pptmstr-proof-run.png`: the real pptmstr, CONTEXT pane in rich mode on the right: four lines
  in dim, white and cyan. It shows very little text, so it proves the path and not the look.

None of these shows a GPU, HiDPI or a real pointer.

## Observations nobody asked for

- **The session nearly died once, by my doing.** `dump_font_wild.py` starts 16 workers; started
  beside the `asan` ctest, available memory fell from 10.8 GB to 1.38 GB. It could not be
  stopped (the stop tool is refused in this session and the process is invisible from another
  shell). It recovered. The corpus dump must be run alone; the script has no `--jobs`.
- Dear ImGui's own five files are not compiled with `-ffp-contract=off` (only ours are). No
  effect on baseline x86-64; CLAUDE.md's "Everything is built with" overstates it.
- `tests/test_c_api.c` (C) does not carry the flag either.
- The reference moved today (`scripts/bench_draw.py` among the files): the reference figures in
  `docs/performance.md` were taken with the earlier script.
- `test_harden` prints the furthest glyph pair as 1.00 px where the README says "all under 1 px".

## Left in `build/`

Preset directories (all eleven, fresh). `build/demo` (the demo the README builds, from
`.deps/sdl2`), `build/bundle-advtext` and `build/pptmstr-bundle` (the two sidecars, fresh from
this tree): kept on purpose, they are what the README's commands produce. Mine removed:
`build/gate-o2`, `build/gate-asan-stb`, `build/gate-demo-root`, `build/py-host`, the oracle
scratch copy. `build/gate` (logs of this task) is removed with this report. **Not mine and not
removed**: `build/dist-research` (3.9 GB), `bundle-research`, `shot-rig*`, `oracle-draw`, the
`api-revise*` and `stress-*` logs; documents cite some of them as evidence.

## Priorities reached

1 presets: done. 2 Python route: done. 3 demo: done. 4 goldens: done. 5: format, lint,
licensing, README numbers, corpus, stress done; benchmarks not usable. 6: done for my own.
