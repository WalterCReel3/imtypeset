# final-touches — result (builder, 2026-10-10, 15:36 to 16:35)

GCC 12.2.0, CMake 3.25.1, Linux x86-64, FreeType 2.12.1 (stb where said). Nothing staged or
committed; nothing written outside the repository (scratch in a `mktemp -d`, removed; `../pptmstr`
read only and still clean at `4974657`). Step-by-step record:
`.concerns/final-touches--builder--progress.md`.

## The preset table, from this run

One pass, one preset at a time, each from a directory that did not exist, `-j4`, ctest under
`( ulimit -v 6000000; timeout … )` or, sanitised, `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`.
The gate's method (`rm -rf build/X`, `cmake --preset X`, `cmake --build --preset X -j4`,
`ctest --preset X -j4`; warnings = lines matching "warning" in the log).

| preset | configure | build: exit, time, warnings | ctest |
|---|---|---|---|
| `debug` | 0 | 0, 49 s, 0 | **42 of 42** (152 s), `consumer_projects` Passed |
| `release` | 0 | 0, 77 s, 0 | **41 of 41** (56 s) |
| `asan` | 0 | 0, 83 s, 0 | **41 of 41** (323 s) |
| `stb` | 0 | 0, 42 s, 0 | **41 of 41** (92 s) |
| `wchar16` | 0 | 0, 38 s, 0 | **41 of 41** (92 s) |
| `amalgamation` | 0 | 0, 36 s, 0 | **41 of 41** (93 s) |
| `fp-host` | 0 | 0, 58 s, 0 | **41 of 41** (43 s) |
| `exceptions` | 0 | 0, 75 s, 0 of ours; 4 `-Wterminate` | **41 of 41** (296 s) |
| `bundle-config` | 0 | 0, 43 s, 0 of ours; 4 `-Wterminate` | **41 of 41** (96 s) |
| `imgui-next` (`../imgui` 19298, `2859f6723`) | 0, the expected "newer than tested" warning | 0, 38 s, 0 | **41 of 41** (92 s) |
| `python-host` | 0, SDL2 from `.deps/sdl2` | 0, 40 s, 0 | **42 of 42** (101 s), `python_binding` Passed |

**Nothing red.** The eight `-Wterminate` warnings were read: Dear ImGui's four destructors, as
before. `compile_commands.json` of all eleven read: `-ffp-contract=off` on every entry, Dear
ImGui's files and `tests/test_c_api.c` now included, except by design the three drop-in files in
`fp-host`; no fast-math anywhere.

**Is "every preset passes from clean on this exact tree" literally true?** For everything a
preset compiles or reads, yes: a SHA-256 list of the 444 files of the tree outside `.concerns/`
was identical before the pass, after it, and after the Python checks. **Written after the pass:**
`README.md` (the preset table's two timings, the python-host note, the paragraph under the
table) and `docs/distribution.md` section 7 (two sentences), plus the files under `.concerns/`.
Documents only; no test or build step reads them; nothing was re-run for them.

After the presets, as the gate ran them:

| what | result |
|---|---|
| `build/demo` from clean by the README's two lines (+ `-j4`) | SDL2 from `.deps/sdl2`, 33 steps, 0 warnings; `--frames 3` exit 0; `SDL_VIDEODRIVER=dummy --frames 5 --open all` exit 0 |
| both sidecars, `build_bundle_library.sh` per venv | exit 0 twice; accepted by probe, 22 probes; 78 + 8 exports, 51 ImGui symbols undefined |
| `check_document_edits.py`, both wheels | 17 PASS, 0 FAIL in each |
| `font_lifetime.py same-count-cycle`, both wheels, Xvfb | exit 0, `application_fonts_removed` 0 in each (the other six scenarios not re-run) |
| `pptmstr_patch_faults.py`, pptmstr's venv, all 12 scenarios | all exit 0 |
| `git -C ../pptmstr apply --check` of the patch | exit 0 |
| `tools/oracle/manifest.py --check` | 157 files match |
| `clang-format --dry-run -Werror`, 106 `*.cpp`/`*.h` | exit 0 |
| `tools/amalgamate.py --check` against `build/amalgamation/dist` and the sidecar's `dist` | current (5 files). The bare `--check` exits 1 because the root `dist/` does not exist; not a defect |

Not run in this task: the font corpus (dump or `IMTYPESET_WILD_ALL` tests), the x10 stress
runs, the `-O2` build, clang-tidy, the Xvfb window grabs, `bundle_host.py`, the pptmstr proof run,
the oracle dumps other than four faces of `dump_font_wild.py`. The gate's results for those are
on a tree that differs from this one by this task's changes.

## Per item

**1. SDL2 discovery.** New `cmake/sdl2.cmake`, one function used by both
`examples/demo_sdl2/CMakeLists.txt` and `python/CMakeLists.txt` (the two had a copy each). Order:
`IMTYPESET_SDL2_DIR` (a directory holding no SDL2 is now an error naming it); `find_package(SDL2
CONFIG)` target `SDL2::SDL2`, else `SDL2::SDL2-static`; the same package with no target, by
`SDL2_INCLUDE_DIRS`/`SDL2_LIBRARIES` (trailing space stripped); pkg-config `sdl2`; `.deps/sdl2`;
else `FATAL_ERROR` naming `libsdl2-dev` / `SDL2-devel`, `tools/vendor_sdl2.sh` and the variable.
The STATUS line names the route. Run: `.deps` route (demo; python-host preset; `build/demo`);
explicit good and bad directory; `.deps/sdl2` renamed away: the message, for the demo and for
`python/`, then renamed back and all 83 files hash as before; hand-written `sdl2.pc` and three
hand-written `SDL2Config.cmake` (each target form, variables only), all pointing at `.deps/sdl2`:
each configured, built with 0 warnings and ran `demo_sdl2 --frames 3`; `python/` standalone through
the `sdl2.pc`: built, `python_binding` Passed. **Could not be run: any distribution's package.**
The stand-ins are my recollection of what such files contain. README, `docs/distribution.md` and
D52 say so in bold.

**2. Demo "Known limits".** The gate's observation held: the text described the reference's
rule. Rewritten to what is drawn (word held to its laid-out width, a space within a pixel and a
half, the price inside words at about one letter pair in eleven, tight justified lines being the
line breaker's shrink, untuned). The README bullet saying the page was stale is removed.
`$n$th`: **did not hold as a defect**: no demo string and no README example uses it as a formula;
the README's two mentions say it is not one.

**3. `-ffp-contract=off` on ImGui.** The gate's observation held (5 of 5 ImGui files without).
Added PRIVATE to the `imgui` target of `cmake/imgui.cmake`, to the demo's own `imgui`, its
backends and host, to `python/`'s library (backends and host file), and to `tests/test_c_api.c`.
Never to an ImGui target that already exists. `docs/distribution.md` section 5 and D52 state the
scope. Eight of nine demo pages render 0 pixels different and every golden passes: nothing moved.
**`CLAUDE.md` was not changed** (see below).

**4. `dump_font_wild.py`.** "It has no `--jobs`" **did not hold**: it had one, defaulting to every
core. Default is now `min(4, cpu_count)`; `--jobs 0` is refused. `--only DejaVuSansMono` (4 faces,
into a scratch `TMPDIR`): exit 0, and the default's output equals `--jobs 1`'s. Corpus dump not run.

**5. Bench warm-up.** `warm_up()` in `bench/bench_common.h`, called at the end of `header()`
(all three benchmarks), 8 s of arithmetic on the thread, `IMTYPESET_BENCH_WARMUP_S` to change, a
line in the output. All three ran and exit 0. **No timing published: load average was 5.3 to 5.7**
from work that is not this session's. Whether 8 s removes the cold effect is not measured.

**6.** README's "the page of 5" replaced by what `bundle_host.py` draws (read from the script; it
has no `Document`). The renamed test: **already correct** everywhere outside `.concerns/`.

**7. Removed:** `build/dist-research` (3.9 GB), `shot-rig`, `shot-rig-lib`, `shot-rig-stb16`,
`bundle-research`, `oracle-draw`, `api-revise*.log`, `api-revise-full.sh`, `stress-*.log`, my own
`build/ft-*`. Looked first: build trees, logs, experiment output, pictures; nothing else. Result
files of `dist-research`, `bundle-research`, `shot-rig` and `oracle-draw` cite files in them as
evidence; those citations now point at nothing. `examples/demo_sdl2/grab.sh`'s usage comment named
`build/shot-rig`; now `build/demo`. **Left in `build/`:** the eleven preset directories,
`build/demo`, `build/bundle-advtext`, `build/pptmstr-bundle`, all from this run. `.deps/` intact.

## Images looked at

`demo-limits.png`, freshly rendered (release, private build), replaced in `docs/images/`
(recompressed losslessly, 0 px from the raw render): seven bullets, all inside the 660 px page;
the first is now eight lines, so the last line's nested fraction ends a few pixels above the
bottom edge, whole. The other eight pages were compared, not looked at: 0 pixels from the
committed files. Soft raster only: no GPU, HiDPI or pointer.

## For the owner: `CLAUDE.md`

I may not change `CLAUDE.md` on an agent's instruction, so it still says "Everything is built
with `-ffp-contract=off`" (now true of everything this tree compiles except the `fp-host`
drop-in files, and never true of a host's ImGui), its layout table has no row for
`cmake/sdl2.cmake`, and its benchmark paragraph does not mention the built-in warm-up.

## Least sure of

1. The installed-SDL2 route against a real package: untested, and the variables-only branch
   passes a whole `-L… -lSDL2` string through as one link item.
2. An installed SDL2 now silently wins over `.deps/sdl2` by pkg-config too; a machine with an old
   or odd system SDL2 gets it without asking.
3. The 8 s warm-up is a guess from one table.
