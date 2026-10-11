# docs — result (builder, 2026-10-10)

Files written: `README.md` (rewritten), `docs/spec-status.md` (new), `CLAUDE.md`,
`docs/decisions.md` (D47 to D50 appended; amendments marked in D1, D32.3, D39, D40, D42),
`docs/hardening.md`, `docs/divergences.md`, `docs/performance.md`, `docs/bindings-notes.md`,
`docs/advtext-migration.md` (one dated note each), `tools/oracle/FORMAT-doc.md` (one sentence).
No source file, no CMake file, no image. Nothing `git add`ed or committed (`docs/spec-status.md`
is therefore untracked). Nothing written outside the repository. `build/docs*` removed.

Everything below was run on this machine (GCC 12.2.0, CMake, FreeType 2.12.1, Python 3.11.2),
`-j3`, every binary under `( ulimit -v 6000000; timeout … )`, each build directory new. The
release build was configured at 13:21, after the last write to `src/draw.*` (12:51) and to any
file under `src/` or `include/` (13:14), so it is the tree with draw-seams, demo-guard and
doc-fixes-now all in; `find … -newer` at the end shows no source changed after it.

## First: three things the lead should hear

1. **`bench_draw` reads 1.7 times high when started cold, and I nearly published it.** The same
   binary prints 208 to 213 us for "full draw, x1" after an idle spell and 125 to 128 us after ten
   seconds of load; the first six to eight seconds of a run are slow, the later rows are not.
   Byte-identical binaries (same md5) from two build directories gave both figures. Written up
   with every run in `docs/performance.md`, "A benchmark started cold reads high". Likely the
   processor's frequency coming up (governor `schedutil`; an inference). word-gaps' and
   draw-seams' comparisons were alternated runs on a busy machine and so were warm; their ratios
   stand. **For the gate:** "re-derive cheaply" by one run of `bench_draw` on a now-quiet machine
   will read 210 us and look like a 70 % regression. It is not. Warm the machine first, or the
   benchmark wants a warm-up (not my file).
2. **Lead ruling 1 is not what the tree says.** `try`/`catch` are in `src/imtypeset_c.cpp` AND in
   the macro `IMTYPESET_NO_UNWIND` of `src/context.h` (moved there by doc-fixes; four uses in
   `src/imtypeset.cpp` and `src/imtypeset_document.cpp`). D39 item 11, D40, hardening row X1 and
   CLAUDE.md say both.
3. **Lead ruling 2, worked out: 1,023.** ImGui starts a new command when
   `_VtxCurrentIdx + reserved >= 65536`. A reservation here is at most `BATCH_QUADS` = 1,024
   quads (1,021 for a batch of glyphs only: the loop stops while four more might not fit), so the
   earliest it forces the split is at 65,536 - 4,096 vertices, where a quad at a time goes on to
   65,532: 1,023 quads earlier. `docs/divergences.md` was right; D39 item 9 and hardening row D4
   now say 1,023 with the derivation. **`src/draw.h` ("up to 1,024 quads earlier") and the
   comment at `BATCH_QUADS` in `src/draw.cpp` ("up to this many") still say 1,024**: not my
   files. The test's bound (`<= 1024 * 6` indices) is loose by one and still true.

## What was run for each README command

| README section | command as run (directory names are mine) | outcome |
|---|---|---|
| 1 demo window, build | `cmake -S examples/demo_sdl2 -B build/docs-demo -G Ninja -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug`; `cmake --build build/docs-demo -j3` | 33 steps, 33 s, no warning |
| 1 plain run | `build/docs-demo/demo_sdl2 --frames 3` | exit 0, `video_driver=offscreen`: **no display in this sandbox**. Without `--frames` not run |
| 1 unattended | `SDL_VIDEODRIVER=dummy … --open "Paragraph,Faces and sizes" --fill --frames 10 --screenshot …` | exit 0, 8,240 vertices |
| | `SDL_VIDEODRIVER=offscreen … --open "Paragraph,Math" --frames 30 --screenshot …` | exit 0 |
| | `sh tools/shot.sh -- examples/demo_sdl2/grab.sh build/docs-demo/demo_sdl2 <prefix> --backend opengl3 --size 1280x800 --dpi-scale 1 --open "Paragraph,Math"` | exit 0, `video_driver=x11`, llvmpipe; xwd grab 46 px from the read-back; **looked at** |
| 2 examples | `example_minimal` | `3 frames, 1 layout(s), 878 vertices`, exit 0 |
| | `example_specimen`, `example_specimen DIR` | exit 0; seven PNGs. With a DIR that does not exist: exit 1 (said in the README) |
| | `example_demo_headless`, `… DIR` | `9 sections, ok`, exit 0; nine PNGs; Paragraph and Math **looked at** |
| 3 suite, release | `cmake --preset release -B build/docs-release`; `cmake --build … -j3`; `ctest --test-dir … -j3` | build 1 min 31 s, 0 warnings; **41 of 41**, 51 s |
| 3 suite, debug | the same with `--preset debug -B build/docs-debug` | build 54 s; **42 of 42** (with `consumer_projects`), 2 min 22 s |
| 4 benchmarks | `bench_draw`, `bench_font`, `bench_reflow` from the release build | exit 0; 12.9 s, 1.3 s, 63.7 s |
| 5 Python host | `cmake -S python -B build/docs-py -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMTYPESET_SDL2_DIR=…`; build; `ctest --test-dir build/docs-py` | 25 s; `python_binding` Passed, 9 s |
| | `IMTYPESET_LIBRARY=… SDL_VIDEODRIVER=dummy python -P -B examples/python/minimal_host.py --frames 30 --screenshot …` (venv python and `/usr/bin/python3`) | `selection: (74, 284) layouts: 2`, exit 0 |
| | the same under `sh tools/shot.sh --` | `host: self-hosted x11 opengl`, exit 0; **looked at** |
| 6 bundle | `IMTYPESET_BUNDLE_OUT=build/docs-bundle-advtext python/host/build_bundle_library.sh ../imgui_bundle ../imgui-adv-text/.venv/bin/python` | 31 s; stand-in `v1.92.9-docking`; accepted, 22 probes |
| | the same for `../pptmstr/.venv/bin/python` | 29 s; stand-in `v1.92.8-docking`; accepted, 22 probes |
| | `bundle_host.py --frames 30` on Xvfb in each venv | `selection: (74, 284) layouts: 2`, exit 0, both |
| | pptmstr's library imported in advtext's venv | ImportError naming both paths |
| | `bundle_host.py` with no library | exit 77 |
| 7 pptmstr | `git -C ../pptmstr apply --check …/pptmstr.patch` (reads only) | passes at `4974657`; `--numstat`: 750 added, 19 removed |
| | the plan's proof run (`pptmstr_app_shot.py --rich --focus CONTEXT -- --fake`, overlay on `PYTHONPATH`, `XDG_CONFIG_HOME` in my build directory) | `imtypeset: in use`, wheel 1.92.801, 3.02 calls per draw; **looked at**. `git status` of `../pptmstr` clean afterwards |

Counts from the release build, for anyone quoting them: `test_word_gaps` 19 tests / 23,541 checks,
`test_harden` 22 / 474,784, `test_draw` 16 / 17,135, `test_demo` 14 / 176, `test_api` 42 / 71,955,
`test_api_document` 24 / 26,800, `test_api_document_golden` 5 / 28,253, `test_c_api` 480 checks,
`test_c_api_map` 7 / 699. The sidecar exports 78 `ImTypeset_*` (dist wrote 77, before
`GetContextSerial`), 8 `ImTypesetBundle_*`, 51 undefined ImGui symbols.

**Not run by me, and the README says so where it matters:** any window on a real display; the
patch applied in pptmstr and `make run`; presets `asan`, `stb`, `wchar16`, `amalgamation`,
`fp-host`, `exceptions`, `bundle-config`, `imgui-next`, `python-host` (the README's table says
"not re-run" for each and points at dist's incremental results); the oracle scripts; the font
corpus with `IMTYPESET_WILD_ALL=1`; `bench_host.py`; `font_lifetime.py`,
`check_document_edits.py`, `pptmstr_patch_faults.py` (the gate's list).

## Statements I could not verify, and how each is marked

- Every figure taken from another task's document (performance headline ratios, the font census,
  the ImGui floor table, the plan's pptmstr/advtext timings, word-gaps' and draw-seams' counts)
  is attributed in place to the document it is in. `docs/spec-status.md` marks the few I re-ran
  "(run for this file)" and says at its end what was not.
- The demo images under `docs/images/` are referenced, not described pixel by pixel; the caption
  says they predate the last draw-rule changes and that the gate re-renders them.
- "Not shown by anyone: a real display, GPU, mouse": true of every result file I read.
- `docs/spec-status.md` marks as *not checkable here*: `imgui_md`'s capabilities, the
  prototype's line counts, the note's FreeType-flags table. Marked *open*: FreeType 2.13.3 glyph
  boxes against the goldens' 2.12.1, loader flags, the name never checked against other projects.

## Doc-additions applied, and where

| from | to |
|---|---|
| d7-fix | **D47**; hardening row D7 (fix and test cells replaced); performance "After `d7-fix`"; divergences `## draw` bullet replaced; its section 5 (layout findings) as divergences `## d7-fix: layout findings…` |
| word-gaps | **D48**; divergences bullet; performance "After `word-gaps`" |
| demo-guard (no additions file; from its result) | **D49**, superseding D42's second "Decided" |
| draw-seams | **D50**, with the three amended sentences of D47/D48 stated in it and marked at both; divergences; performance "After `draw-seams`" |
| doc-fixes | divergences `## api-document` clause; README error-model facts; CLAUDE.md stale-object hazard (taken) |
| demo | already applied by api-document (header, C counterpart, map row, THIRD-PARTY SDL2, D42): checked, not re-applied |
| c-api, harden-2, font-wild-finish, tex-fixes | applied by fold-docs: checked by symbol, not re-applied |

Lead's items 1 to 8: 1 and 2 above; 3 D39's review ids qualified ("review item D3" …); 4 hardening
`## Counts` row, S2's "63 cases" (146 now, by the test's census line, run), three "Not verified"
bullets, the overlay sentence in divergences, the register's "these stay", D32.3 marker; 5
bindings-notes 6B and a note at its head (D6 already carried dist's "superseded by D44"); 6 README;
7 B3a test name, "linear at a fixed measure" and the first-fit fallback in performance; 8 done
with the d7-fix additions.

## Stale claims fixed in CLAUDE.md

Port -> product (D32, D34); Scope rewritten (no custom `ImFontLoader`, blocks and Document native,
C ABI, Python); "pinned exactly" -> floor 1.92.0 / tested 1.92.9 (D44); "only `src/` may include
`imgui_internal.h`" -> nothing in `src/` or `include/` does; the two places `try`/`catch` live;
`reference()`/`product()` rules and what a test of a product rule is; "do what the reference does"
qualified by D41; the eleven presets; the Layout table; `-P` in the reference invocation;
`fp-host` as the one exception to `-ffp-contract=off`; four later "trust but verify" cases; the
stale-object hazard; `tools/shot.sh`; benchmark timings under load and cold. "Run every binary
under a memory guard" and the no-attribution rule are untouched.

## Left for the gate

1. **Re-render `docs/images/demo-*.png`** (and the other pictures the README links, if wanted):
   `example_demo_headless DIR` writes uncompressed PNGs of 1.9 to 5.4 MB; the committed ones were
   recompressed.
2. **README numbers that should be re-derived on the final tree**, by section:
   - "Running", 3: the preset table's "not re-run" cells can become counts.
   - "Known rough edges": 8.9 % / 13,348 of 150,624; 11 of 37,778; 16.8 %; 0.95 px; 8.2 % against
     3.4 %; 1,774 spaces under 75 %. All print from `IMTYPESET_WORD_GAPS_REPORT=1 test_word_gaps`
     and `test_harden --filter=d7_every`. I re-ran the tests (they pass) and not the reports.
   - "Performance": 126 to 128 us (mine, warm); the rest is `docs/performance.md`'s.
3. `tests/golden/PROVENANCE.md` and the manifest: not regenerated, not checked by me.
4. `docs/distribution.md` is as dist left it and I changed nothing in it: its "77 `ImTypeset_*`"
   is now 78.

## Noticed, outside the task

- **`.concerns/demo-guard--builder--result.md` has unfilled placeholders** in its table
  (`RESULT_ASAN`, `RESULT_ASAN16`, `RESULT_DBG`, `RESULT_NODEMO`, each with `_EX`). Its progress
  file has those runs for the 13-test state, before the switch to the serial. So the sanitised
  runs of the FINAL demo are not recorded anywhere. D49 says so.
- **`docs/advtext-rework-plan.md` "Start here" disagrees with what runs**, in small ways: "672
  lines added" is the count of non-blank added lines (`git apply --numstat`: 750); "typeset.py NEW,
  128 lines" is 131; "theme.py: two lines" is 7; and "pytest: 2,567 passed, 2 failed" is from
  before py-fixes, whose result says 2,569 passed, 3 xfailed at the newer commit. Not my file to
  rewrite (py-fixes edited it today); the README quotes numstat.
- `test_harden`'s `d7_every_rule…` computes a formula's edge as `snap_px(own place)`, which only
  holds because every formula there starts its line (draw-seams said so too).
- The session rule "git add a file as soon as you create it" and CLAUDE.md's "do not git add"
  contradict each other; three earlier builders tripped on it. I followed CLAUDE.md and the task.
