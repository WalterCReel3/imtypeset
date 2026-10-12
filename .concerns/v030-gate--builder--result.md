# v030-gate — result (builder, 2026-10-12, 01:03 to 02:30)

GCC 12.2.0, Linux x86-64 (Debian 12, glibc 2.36), FreeType 2.12.1 (stb where said), CPython
3.11.2, `-j4`, one heavy thing at a time, every binary and every Python process under
`( ulimit -v 6000000; timeout … )` or, sanitised, `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`
(one slip, below). Load average 4.3 to 5.5 throughout, from other sessions. Nothing staged,
committed, tagged or pushed. Nothing written outside this repository. `build/advtext-reference`
and `.deps/` untouched. Step by step: `.concerns/v030-gate--builder--progress.md`; the lines the
tables rest on: `.concerns/v030-gate--builder--gate.log.txt`; the hashed file list:
`.concerns/v030-gate--builder--fingerprint.txt`. Words: **RAN**, **READ**, **NOT RUN**.

**The tree this is about is HEAD `9e899e7` plus 30 modified files that are not committed** (list
under "What I changed"). No file was created outside `.concerns/` and `build/`.

## The preset table (one pass, after the last change to anything a preset compiles or reads)

`rm -rf build/X; cmake --preset X; cmake --build --preset X -j4; ctest --preset X -j4 --timeout 600`,
in this order, one at a time. "Warnings" = lines matching "warning" in the build log.

| preset | configure | build: exit, time, warnings | ctest |
|---|---|---|---|
| `debug` | 0 | 0, 63 s, 0 | **53 of 53** (156 s), `consumer_projects` Passed |
| `release` | 0 | 0, 115 s, 0 | **52 of 52** (60 s) |
| `asan` | 0 | 0, 123 s, 0 | **52 of 52** (274 s) |
| `stb` | 0 | 0, 62 s, 0 | **52 of 52** (105 s) |
| `wchar16` | 0 | 0, 66 s, 0 | **52 of 52** (82 s) |
| `amalgamation` | 0 | 0, 57 s, 0 | **52 of 52** (93 s) |
| `fp-host` | 0 | 0, 87 s, 0 | **52 of 52** (50 s) |
| `exceptions` | 0 | 0, 120 s, 0 of ours; 4 `-Wterminate` | **52 of 52** (257 s) |
| `bundle-config` | 0 | 0, 71 s, 0 of ours; 4 `-Wterminate` | **52 of 52** (97 s); `test_exception_safety` SKIPS here, as at the last gate |
| `imgui-next` (`../imgui` 19298, `2859f6723`) | 0, the expected "newer than tested" warning | 0, 63 s, 0 | **52 of 52** (86 s) |
| `python-host` | 0, SDL2 from `.deps/sdl2` | 0, 55 s, 0 | **54 of 54** (89 s), `python_binding` and `python_demo_sections` Passed |

**Nothing red.** Two more tests than at the swap gate: `test_api_handoff` and `test_hidpi`; each
has a `Passed` line in all eleven logs, as has `amalgamation`; no test line in any log is anything
but `Passed` (counted). The eight `-Wterminate` are in ImGui's `~ImDrawListSharedData`,
`~ImFontAtlas`, `~ImGuiContext`, `~ImGuiWindow` (READ in the `exceptions` log; counted in both).
`compile_commands.json` of all eleven: `-ffp-contract=off` on every entry except, by design, the
three drop-in files of `fp-host`; no fast-math; `release` at `-O3`. `test_exception_safety` run by
hand afterwards: `exceptions` 10 tests, 71,570 checks; `bundle-config` prints SKIPPED, 0 checks.

**One unchanged tree.** The 533 files git tracks or would track outside `.concerns/` (the
sandbox's dot-files left out) hashed the same before `release` and after `python-host`, and again
after the oracle runs. To be exact about the "before": the first listing, taken before `debug`,
used `find` and included sandbox artefacts; the 533-file list was taken right after `debug`
finished, and every one of its 533 hashes equals the hash that file had in the first listing.
**Written after the pass: `README.md`, `docs/distribution.md`, `docs/sidecar.md`** (the numbers of
these runs). Nothing compiles or reads them (READ: `tools/version.py` reads the header, its
copies and the changelog; the wheel takes `THIRD-PARTY.md`; `tests/python` read
`docs/python-errors.md`). Re-run after them: `tools/version.py --check` (agrees) and
`tools/release.sh` on a scratch copy (below). The final tree differs from the hashed one in those
three files and no other (diffed).

Before the pass, in private directories since removed: whole debug suite 52 of 52 (no
`consumer_projects` there), `test_demo` in release, the handoff test.

## What I changed (all before the pass, except the three documents named above)

1. **F12 is real and wider than the finding; fixed** (D76). `tests/test_api_handoff.cpp`, new
   `a_bar_under_a_line_above_the_window_is_drawn_where_it_hangs_into_the_window`: a 30 px window
   moved a pixel at a time, each draw held to a draw whose clip shows everything. RAN before the
   fix: 5,195 failed checks, then 7,823 with more cases. Three things dropped an `Under` mark's
   bar, not one: the bytes-in-view culling of a block with more than 16 marks (F12: 280 of 811
   windows at an offset of 30 px, all at 75); a **block wholly above the clip** (258 of 896 at
   12 px, all at 30: any number of marks); a **table row above the clip** (the same figures). A
   bar 5 px thick at the library's own offset was enough under a one-line paragraph. Fix,
   `src/imtypeset_document.cpp` (`bars_hang`, `bar_thickness`, `bar_offset`) and one field in
   `src/imtypeset_document.h` (`largest_em`): the top of the window a draw works in is raised by
   how far the lowest bar among its marks can end below a baseline. One pass over the marks a
   draw; nothing changes with no Under mark. **Unbounded on purpose:** an absurd `Offset` makes a
   draw visit the whole document above the clip; said in the header at `ImTypesetMark`
   (`include/imtypeset.h`, comment only). After: 47,530 checks, 0 failed; the older equivalence
   test now looks at 15,388 marks where it looked at 15,302.
   NOT run: the new test in columns (READ: a bar past a column's foot is still dropped with the
   cut); the cost of the extra pass by alternating builds.
2. **`src/imtypeset_demo.cpp`, the forbidden rounding.** The specification said "the units.h
   helper". **The demo cannot include `src/units.h`**: it is a drop-in file of its own and its
   first comment promises `imgui.h` and `imtypeset.h` only. It has a file-local `SnapPx`
   (floor of the value plus a half, in double). RAN: all seventeen headless pages 0 px from the
   same build with HEAD's line. Two other `(float)(int)(x * 0.5f)` in the file truncate and are
   not that spelling; left.
3. **Two false sentences in the demo, corrected** (not in the specification; seen in the
   pictures). Specimen: "The library ... says nothing about a face"; Formula gallery: "The library
   does not say what a font covers". `GetFontMetrics` and `FaceHasGlyph` answer both; the pages
   name them. Four pages change by it.
4. **`docs/images/demo-*.png`: all seventeen replaced**, each 0 px from the `release` preset's
   render of the final tree (RAN after the pass, `compare -metric AE`). Against the committed
   ones: eleven differed by 8 px (the version line, 0.2.0 to 0.3.0), `demo-math` 32,299,
   `demo-math-stream` 41,688, `demo-math-subset` 1,092,850, `demo-section-math` 100,768; two were
   identical until change 3.
5. **`docs/decisions.md`**: D73, D74, D75 (gate-fixes' text, as left in its result file), D76
   (mine); D71 amended in two places to point at them. **`docs/CHANGELOG.md`**: gate-fixes' four
   lines, "in a source checkout only" on the probe line, a section for this gate.
6. **Probe rows: 25** (RAN: every sidecar and wheel build prints it). `README.md`,
   `docs/distribution.md`, `docs/sidecar.md` section 1.
7. **`README.md`**: status and dates; the wheel route as section 8 with commands that I ran **as
   written** (`python3` being `/usr/bin/python3`); **`IMTYPESET_BUNDLE_HEADERS=stand-in` on the
   two build commands, which without it stop at the fetch on this machine** (the README still
   showed the old default); the import gate, dense displays, font metrics, 90 functions (was 88);
   "This is not a wheel, a `pyproject.toml`" removed (false); what never ran, said in the status
   paragraph, the Python section, the preset section and "Missing before publication"; the preset
   table from this pass. **`docs/distribution.md`**: section 1's commands and header account
   brought to the script (it still described the fallback and a submodule checkout), 97 + 9
   exports (was 95 + 8), section 7's table. **`docs/sidecar.md`**: a third dated table in
   section 12. **`docs/spec-status.md`**: 90 functions, the wheel under §6, four items under
   "did not foresee", a dated note saying which sections were not re-read.
   **`docs/bindings-notes.md`**: a dated note at the top (what section 5 lacks). All by reading.
8. **`docs/performance.md`**: a `bench_marks` section. **Not a quiet machine** (load 4.4 to 5.0)
   and it says so: two runs, medians 37.0 / 70.8 / 97.2 / 72.5 us and 36.2 / 68.0 / 92.7 / 67.3 us
   for no marks / Ink in order / Ink shuffled / Behind, from a private release directory.

Not edited: `docs/advtext-swap-status.md` (nothing in it, tables or prose), `CLAUDE.md`,
`.github/`, any Python, any golden.

## What is red

Nothing in the presets, the Python route or the oracle. Found and NOT changed:

- **`--screenshot` fails where numpy is absent.** In `../pptmstr/.venv` (no numpy),
  `examples/python/bundle_host.py --screenshot F` and `hidpi_check.py --screenshot F` print their
  result line and then exit 1: `TypeError: could not export nanobind::ndarray:
  ModuleNotFoundError: No module named 'numpy'` from `hello_imgui.final_app_window_screenshot()`
  in `examples/python/bundle_support.py`. Without the option both exit 0. **This matters for the
  Mac checklist**: `hidpi`'s instruction to a person at a Mac is `hidpi_check.py --screenshot
  hidpi-mac.png`; in an environment without numpy they get `PASS` and then a traceback and no
  picture. Not fixed: it is a write after the pass to a file a person will run on a system I
  cannot try. One line in the checklist ("needs numpy") or a caught error would do.
- `shellcheck` beyond the scripts named: `tools/shot.sh` 4 notes (SC2016, SC2086 three times),
  `python/spike_bundle/measure_pptmstr.sh` 2 warnings (SC3045 `ulimit -v` in POSIX sh, SC2034).
- `clang-format`: three tracked scratch sources under `.concerns/vector-output--probes/` are not
  formatted. They are not project sources and no gate has counted them.
- `tools/release.sh` runs `python3` from `PATH` unless `PYTHON` is set; here that is another
  project's virtual environment. I ran it with `PYTHON=/usr/bin/python3`.
- My own slips: `python3` from `PATH` (pptmstr's venv, `-I -B`, nothing installed or written)
  for the first source edit, `/usr/bin/python3` after; one run of the sanitised
  `test_exception_safety` under `ulimit -v`, which dumped core at start as ASan must, re-run
  properly (10 tests ok; no core file left); `tools/oracle/pin_reference.sh` once through `sh`
  (it is a bash script; it stopped at its `set -o pipefail` and did nothing), then run as itself.

## Command -> result (all RAN after the preset pass, on the hashed tree)

| what | result |
|---|---|
| both sidecars from clean, `IMTYPESET_BUNDLE_HEADERS=stand-in python/host/build_bundle_library.sh` (advtext venv 1.92.900 / 1.92.9; pptmstr venv 1.92.801 / 1.92.8) | exit 0, 37 s each, 0 warning lines, accepted by probe, **25 of 25**, attestation written; 97 `ImTypeset_*` + 9 `ImTypesetBundle_*` exported, 117 Dear ImGui symbols undefined |
| in BOTH venvs | `run_gate` accepted; `mutate_gate` 31 offsets, 90 mutants, 12 crashed, **0 survived**; `check_document_edits` 23 PASS 0 FAIL; `check_swap_answers` 18 PASS 0 FAIL; handoff-core's new-calls script all passed |
| in BOTH venvs, Xvfb through `tools/shot.sh` | `bundle_host.py --frames 30`: `selection: (74, 284) layouts: 2`; `bundle_demo.py` 8 runs each: exit 0, `last error: 0`; `hidpi_check.py` at density 1 and `--assume-density` 1.5, 2, 3: `1.000 to 1.000 texels per framebuffer pixel, 0 off the grid: PASS`; `font_lifetime` seven scenarios: exit 0, `application_fonts_removed` 0. (pptmstr venv: the three runs given `--screenshot` exit 1, above; re-run without, exit 0) |
| both wheels, `tools/wheel/build_wheel.py --headers stand-in` | 38 s, 40 s: `imtypeset-0.3.0.192900.dev0+standin-cp311-none-manylinux_2_28_x86_64.whl`, `…192801.dev0+standin…`, 589 kB, 11 files, 25 probe rows, tag measured |
| `check_wheel.py`; `--release --tag v0.3.0` | both ok; both refused ("NOT A RELEASE: built against stand-in headers") |
| `local_env.py` x 2, `pip install --no-cache-dir --no-index --find-links <both> --pre imtypeset imgui-bundle==X`, `pip check` (pip 26.2.1) | each environment got its own line; clean; `import imtypeset` with no variable, from another directory: route `installed`, checked in `attestation` |
| `check_installed.py` (`--other-python`, `--dev-library`, `--self-hosted-library`) | **92 PASS, 0 FAIL, 0 SKIP in each** |
| the README's section 8, typed as written, 1.92.900 | wheel built, `ok`, installed, `0.3.0 installed`, `check_installed.py`: all passed |
| `tools/ci/test_wheel.py` (`/usr/bin/python3`; a throwaway environment's, `packaging` 26.3) | 24 tests OK, twice |
| `tools/ci/test_fetch_headers.py`; `check_workflows.py` (PyYAML, `/usr/bin/python3`); `tools/version.py --check` | 17 tests OK; 3 files ok; every copy agrees |
| `tools/ci/sidecar_probe.py --also-lookup --only-stand-in`, 1.92.900 | exit 0: "H1 YES" by `link`, "H2 not attempted" |
| `pptmstr_patch_faults.py`, 12 scenarios, pptmstr venv, Xvfb | all exit 0 |
| `git -C ../pptmstr apply --check` of the patch | exit 0 at `4974657`; 750 added, 19 removed; `../pptmstr` clean before and after |
| `tools/oracle/pin_reference.sh` | `32e3953`, 179 files, matches `REFERENCE.lock` |
| oracle, pinned tree, reference interpreter `-P -B` | `--verify`: `dump_core` 599 cases / 6,624 checks, `dump_para` 253 / 2,088, `dump_selection` 57 / 25,267, `dump_blocks` 69 / 284, `dump_tables` 64 / 262, `dump_doc_extras` 28,735 records (84 s), `dump_math` 8,555 / 10,728, `dump_specimen`; `--check`: `dump_doc_tables`, `dump_specimen` identical; `gen_mathtext_tables.py --check` up to date: all exit 0. `manifest.py --check`: 160 match |
| `clang-format` 14.0.6 `--dry-run -Werror`, the 127 tracked `*.cpp`/`*.h`/`*.c` outside `.concerns/` | exit 0, before and after the pass |
| `shellcheck` 0.9.0 on `tools/ci/*.sh`, `python/host/*.sh`, `tools/release.sh`; `sh -n` and `bash -n` on every `.sh` under `tools/`, `python/host`, `examples/demo_sdl2` | clean |
| `build/demo` from clean by the README's two lines (+ `-j4`) | SDL2 from `.deps/sdl2`, 36 steps, 0 warnings; `--frames 3` exit 0 (`video_driver=offscreen`); the `SDL_VIDEODRIVER=dummy` command exit 0 (7,826 vertices); `--open all --frames 5` exit 0 |
| `example_demo_headless` (release preset) | `17 pages, ok`; a missing directory: exit 1; `example_minimal`: 878 vertices, exit 0; `example_specimen` exit 0 |

## The release pre-flight: `sh tools/release.sh v0.3.0`

READ: it checks, in order, stopping at the first failure: (1) `tools/version.py --check --tag`:
the tag is `v` + the header's version, every copy agrees, the changelog has a section; (2) `git
status --porcelain --untracked-files=no` is empty; (3) the tag does not exist; (4)
`tools/amalgamate.py` generates into a scratch directory; (5) `tools/ci/check_workflows.py`
(skipped with a note without PyYAML) and `tools/ci/test_wheel.py`. It runs no preset and builds no
wheel. **It creates nothing**: its last act is to print two commands.

RAN on this tree (`PYTHON=/usr/bin/python3`), exit 1, as it must with 30 files uncommitted:

```
version 0.3.0: every copy agrees, and the tag v0.3.0
release.sh: the working tree has uncommitted changes; a tag names a commit
 M README.md
 … (the 30 files)
```

RAN on a copy of the 533 files committed in a throwaway repository under `build/` (removed
since), after the last document was written, exit 0:

```
version 0.3.0: every copy agrees, and the tag v0.3.0
the drop-in files generate (5 files)
ok   .github/workflows/ci.yml
ok   .github/workflows/sidecar-probe.yml
ok   .github/workflows/wheels.yml
........................
Ran 24 tests in 0.520s
OK

v0.3.0: everything this script checks holds at 54394bd6a590461f097ae6dc5eb43d454ef43afb.
Not checked here: the presets, and the wheels (CI builds and checks them for the tag).

To release, a person runs:
    git tag -a v0.3.0 -m "imtypeset 0.3.0" 54394bd6a590461f097ae6dc5eb43d454ef43afb
    git push origin v0.3.0
and the workflow "wheels" attaches the wheels to the GitHub Release of v0.3.0.
```

(`54394bd…` is the throwaway commit, not one of this repository.) `v0.2.0` there: refused, exit 1.
`git tag -l` in this repository: empty before and after. **Step 2 ignores untracked files**: a
new file nobody added would not stop it. I created none outside `.concerns/`.

## What has never run (said again, plainly)

- **Anything on macOS.** Not the library, not a test, not the sidecar's link line, not the Mach-O
  half of the import gate, not dyld's image list, not `hidpi_check.py` on a Retina panel. Every
  macOS branch is written from memory of Apple's tools.
- **Any GitHub Actions workflow** (`ci`, `sidecar-probe`, `wheels`): what `check_workflows.py`
  holds is their shape. The action commit ids and the runner labels have met no runner.
- **The wheel's real headers.** No fetch of the fork from GitHub, no library compiled against the
  fork's headers, Dear ImGui's own layout check passing nowhere, so no wheel that `--release`
  accepts. Every sidecar and wheel above is a **stand-in** build accepted by the field probe.
  The fork's two checksums are unrecorded: a release build is refused until someone pastes them
  from the first CI run.
- Clang, MSVC, Windows, arm64; CPython other than 3.11.2; an install from an index (imgui-bundle
  was present by links); a real application freezer; an update under an installed application;
  a GPU, a real pointer, a dense display.
- Here, this time: **the font corpus** (forbidden); the five dump scripts with no read-only mode;
  `clang-tidy`; the `-O2` build; benchmarks on a quiet machine; `pip wheel .`;
  `check_installed.py --old-library`; `tests/python` against a sidecar (they need the self-hosted
  library and ran against it in `python-host`); the real pptmstr application on the overlay;
  the Xvfb grab of `demo_sdl2`; README sections 4 and 5 by hand; Python tests under ASan.

## Pictures looked at, defects first

**The seventeen headless pages** (private release build, 900 px wide; byte-compared 0 px with
the `release` preset's render afterwards). All seventeen opened.

- **demo-math-subset** (and the table at the foot of demo-math, demo-math-stream,
  demo-section-math): the formula columns are whole now (the stale picture had them cut at the
  cell's edge). **Still cut: a source with no space in it, by the column after it**:
  `\frac{\frac{a}{b}}{\frac…` ("a fraction of fractions"), `\frac{a+b+c+d}{\sqrt{x}}` ("bars over
  parts", whose inline formula starts where the source is cut off); `\sum\nolimits_{i=1}^{n}` and
  `\left(\frac{a}{b}\right)` end hard against the edge. The last row is cut by the page's foot
  (the page ends there; a scripted scroll, not a defect of the table). handoff-core listed the
  cut source as known.
- **demo-math**: the refused formula (`\hat{x}`) shows as its source with the reason in orange,
  twice; the stream is at "Piece 7 of 124". demo-math-stream: the stream at piece 19, one formula
  set. Both show "Not here: a table cell".
- **demo-inspector, demo-limits**: the fraction of fractions in running text is a few pixels tall
  and unreadable; the Known limits page says so itself. All seven bullets inside the page.
- **demo-paragraph, demo-faces, demo-selection, demo-stress**: justified lines with tight and
  loose spacing side by side (the line breaker's; README). Nothing else.
- **demo-document**: the top of 2,001 blocks, one column, the word "formula" marked in each
  paragraph. **demo-document-columns**: ten blocks, two columns in view of six cut; the table
  split across the columns, header and rule and first row at one foot, two rows at the next
  head; "The golden / ratio" and "Text that / arrives" wrap; the counter line reads "In 6
  columns" beside a slider at 2.
- **demo-fonts**: shows the absolute path `/home/wreel/Source/imtypeset/CMakeLists.txt` in its
  File field (the refused-file demonstration). It was in the committed picture already.
- **demo-specimen, demo-section-specimen, demo-formulas, demo-section-formulas**: the two
  corrected "Not here" notes read as written (crops looked at); `\oint_\gamma`'s subscript stands
  off the sign; nothing else.

**Python on Xvfb + llvmpipe** (19 pictures made: 11 in advtext's venv, 8 in pptmstr's, where the three screenshots failed; three opened): `hidpi-2` (advtext venv,
`--assume-density 2`): line 1 reads `io.DisplayFramebufferScale 1 x 1`, the PASS line, the two
copies of the sentence equally sharp, as they must be at density 1: **this picture shows nothing
about a dense display**. `demo-window-document` (advtext venv): the demo window with the Document
open; **justified lines end hard against the view's scrollbar** and the last line is cut by the
view's foot (both known). `demo-page-math` (pptmstr venv): the Math page beside the sidebar,
right. The two environments' demo pictures differ by 0, 0, 0, 162, 6, 112, 112, 112 pixels; the
other sixteen were not opened. No Python picture shows the table or more than one column.

None of this shows a GPU, HiDPI or a real pointer.

## For the status file, under "Build from"

> **Build from the commit that holds this gate's tree** (`9e899e7` plus the gate's changes; the
> user pushes, and the tag `v0.3.0` is made on it). *Run on that tree by an agent, 2026-10-12,
> GCC 12.2 on Linux x86-64, one machine:* every preset from a clean directory, one at a time, on
> one unchanged tree (533 files hashed before and after): `debug` 53 of 53, `python-host` 54 of
> 54, `release`, `asan`, `stb`, `wchar16`, `amalgamation`, `fp-host`, `exceptions`,
> `bundle-config`, `imgui-next` 52 of 52 each, no warning from our sources. Both sidecars and
> both wheels rebuilt against **stand-in headers** (`IMTYPESET_BUNDLE_HEADERS=stand-in`; without
> it the build stops at a fetch nobody has made) and accepted by the field probe, **25 rows**;
> each wheel installed into a throwaway environment and imported with no variable (92 of 92
> installed-package checks in each). The stand-in wheels are `0.3.0.192900.dev0+standin` and
> `0.3.0.192801.dev0+standin`; no release wheel exists. **Fixed since `9e899e7`, relevant to you:**
> the bar of an `Under` mark was not drawn where it hung into the clip rectangle from a line, a
> block or a table row above it; a draw now looks above the clip as far as its lowest bar can end
> (so keep a mark's `Offset` small: it costs every draw that many lines). No declaration changed
> since `9e899e7`; library and package must come from one commit. **Never run, anywhere:** macOS;
> any of the three GitHub workflows; a build against the wheel's own headers; Clang; CPython
> other than 3.11; an install from an index. `examples/python/hidpi_check.py --screenshot` needs
> numpy in the environment (it prints its PASS line first and then fails without).

## Stale in `docs/advtext-swap-status.md` (not edited; the lead holds it)

- "Build from" names `c9f8902` and its row "what it is" gives the swap gate's counts (51 / 52 /
  50) and "24 probe rows"; line 30 names `03b5cd4`. advtext's file, changed at 01:48 today while
  this gate ran, points out that the two disagree and has moved its pin to `03b5cd4`.
- H19's row says **queued**; `hidpi` is in `9e899e7` (its row text is in its result file).
- H3's row gives `IMTYPESET_LIBRARY still wins` and `+standin`; gate-fixes' result has the
  replacement rows for H3, H6, H15, H18 and T7 to T10.
- "two probe rows more (24)" in the API-changes table is true of its commit; 25 since `9e899e7`.

## Left behind

`build/`: the eleven preset directories, `build/demo`, `build/bundle-advtext`,
`build/pptmstr-bundle`, all from this run and this tree; `build/v030-gate` (3.6 MB: every log,
the two wheels, the Xvfb pictures, the three scripts the run used). `build/py-dist/cp311-1.92.900`
was overwritten by the README's wheel command (its default work directory). Mine removed:
`build/v030-f12`, `v030-pre`, `v030-rel`, `v030-try`, the throwaway environments, the scratch
repository. Not mine, not removed: `build/gate-fixes*`, `build/hidpi-*`, `build/py-dist*`,
`build/oracle-fixes`, `build/vector-output`, `build/lead-*`, `build/bundle-headers`.
**Nothing new needs `git add`** outside `.concerns/`.

## Least sure of

1. **The fix for the bars is two hours old and has one test.** It moves the top of the window
   every document draw works in whenever a draw has an Under mark, which is every frame of a host
   that underlines links. The equivalence it is held to is against a draw with a huge clip, in
   one column, at density 1, with marks I chose. Columns were read, not run. If a draw with
   underlined links now looks different or slower in advtext, this is where to look first.
2. **Everything about macOS**, which is the owner's goal and which this gate did not move by
   one line: a green table here says nothing about it.
3. The load average was 4.3 to 5.5 from other sessions for the whole run; a timeout-sensitive
   test that passed here is not shown to pass on a busier or slower runner, and the `bench_marks`
   figures are not a result.
4. Per-preset, I counted `Passed` lines and looked for any line that was not; I did not read
   the output of the fifty-odd tests of each preset.
