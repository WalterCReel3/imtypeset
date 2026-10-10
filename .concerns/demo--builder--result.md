# demo — result (builder, 2026-10-10)

Worked WITHOUT the board claim, on the lead's instruction, on the narrowed file set. `build/demo*`
is removed; everything below was run in this session, every binary under
`ulimit -v 6000000; timeout 600` (sanitised: `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout 600`), `-j4`.

## How to run

At a real display, from the repository root, in an ordinary shell (this sandbox cannot reach one):

```sh
cmake -S examples/demo_sdl2 -B build/demo -G Ninja \
    -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug
cmake --build build/demo -j4
build/demo/demo_sdl2                       # or: build/demo/demo_sdl2 --backend opengl3
```

These are the commands I ran last, from a directory that did not exist (32 edges, 23 s). The window
holds **ImTypeset Demo** and a small **Host** window. The first line printed must say
`video_driver=x11` (or `wayland`); `offscreen` means SDL found no display and there is no window.
Fonts come from `tests/data/fonts` of the tree it was built from (`--fonts DIR`).

Unattended (what I ran; `tools/shot.sh` gives an X window on Xvfb, `grab.sh` photographs it):

```sh
SDL_VIDEODRIVER=dummy build/demo/demo_sdl2 --dpi-scale 1 --open all --frames 20 --screenshot out.bmp
tools/shot.sh -- examples/demo_sdl2/grab.sh build/demo/demo_sdl2 OUT --backend opengl3 \
    --size 1280x800 --dpi-scale 1 --open "Paragraph,Math"
```

Host options for unattended runs: `--open SECTIONS|all`, `--fill`, `--scroll Y`,
`--drag X0,Y0,X1,Y1` (through `io.AddMouse*Event`), `--fonts DIR`, `--imgui-demo`, and shot-rig's
`--frames N --screenshot F --stay --backend --size --dpi-scale --sdl-renderer`.

Without a window: `example_demo_headless [DIR]` and `test_demo`, built by the root build
(`-DIMTYPESET_TESTS=test_demo -DIMTYPESET_EXAMPLES=demo_headless`); the host is also in the root
build behind `-DIMTYPESET_BUILD_DEMO_SDL2=ON` (adds the ctest test `demo_sdl2_dummy`).

## Defects seen in the images (none is in a file of mine; nothing was worked around)

All at FontSize 19 px (about a 16 px em), Roboto + Latin Modern Math, FreeType 2.12.1, read off
4-5x enlargements.

1. **The word space after an inline formula that ends in a fraction is short.** "…style, $x =
   \frac{-b \pm \sqrt{b^2-4ac}}{2a}$ is a box": 5 px between the comma and `x`, 3 px between the
   right end of the fraction bar and "is". Same after `$\sum_{i=1}^{n} i = \frac{n(n+1)}{2}$`.
   (`docs/images/demo-math.png`, `demo-selection.png`.)
2. **`\int f(x) dx`**: about 12 px between the integral sign and `f` inline, more in display. I
   believe it is the sign's italic correction added to its width plus the thin space; not held
   against the reference.
3. **Selection highlight seam** above/below a line holding a formula (the known finding 1 of
   `.concerns/example--builder--visual-findings.md`); visible in
   `docs/images/demo-sdl2-sdlrenderer-dummy.png`.
4. **Rules against their content, small sizes**: inline `\sqrt{\frac{a}{b}}` — the radical's rule
   touches the top of `a`; inline `\frac{a+b+c+d}{\frac{x}{y}}` — the inner `x` touches the inner
   bar. Both at script-script size (about 5 px type).
5. **Rule placement (review-3a's concern): not reproduced beyond 1 px here.** Tried, inline and
   display: `\frac{a+b+c+d}{\sqrt{x}}`, `\frac{\sqrt{x}}{a+b+c+d}`, `\frac{a+b+c+d}{\frac{x}{y}}`,
   `\sqrt{x}^2`, `\sqrt{x^2+y^2}`, `\frac{1}{11111111}`, the quadratic formula, the nested
   fraction. Bars start and end with the wider part; the narrower part is centred to the pixel I
   can read; the radical rule over a lone `x` in a numerator/denominator sits about 1 px right of
   the `x` inline. One size, one origin: this does not clear the finding, it says these cases at
   this size do not show it. They are rows of the Math section's table, so they are in every
   re-shot.
6. Ragged column, "no macro processor  behind it": one visibly wider gap at natural spacing (the
   pixel rule's word drift, as the lead's word-gap note describes).
7. Mine, cosmetic, left: display formulas wider than their table cell are clipped by the cell
   (they overhang by design); `ImGui::Bullet()` sits about 3 px high beside 19 px typeset text.

Three rasterisations of one frame (1280x800, `--fill`, "Paragraph,Math" unscripted and "Selection"
with a drag; soft raster from `example_demo_headless --frame`, the others from `demo_sdl2`):
- opengl3 on Xvfb (llvmpipe) vs SDL_Renderer/GL offscreen: **0 px**.
- soft raster vs either GL: 96,510 / 232,298 px differ by exactly 1/255 (blend rounding of the
  translucent window background), **61 / 41 px by more**, all on the close button's cross and the
  end caps of the margin hairlines (AA lines). In the ragged text column: 0 px differ at all.
- soft raster vs SDL software (dummy): 12,674 / 8,048 px by more than 5 %, on AA geometry
  (borders, separators, hairlines); 0 px above 2/255 in the text column. As shot-rig found: SDL's
  software renderer is faithful for glyph quads, not for ImGui's AA lines.
- 16-bit ImWchar vs 32-bit, soft raster, Math / Stress / Selection pages: **0 px**.
- xwd/import of the X window vs the host's own read-back: 0 px with `--fill`; 47-78 px without,
  all in the Host window's frames-per-second line (a later frame).

## What was built

| file | what |
|---|---|
| `src/imtypeset_demo.cpp` | `ImTypeset::ShowDemoWindow(bool* p_open)`; only `imgui.h`, `imtypeset.h`, `<string.h>`; `IMTYPESET_DISABLE_DEMO` leaves an empty function. Adds no font. |
| `examples/imtypeset_demo.h` | the temporary declaration |
| `examples/demo_sdl2/main.cpp`, `CMakeLists.txt` | the host now always links the library and shows the demo window; standalone project or from the root build |
| `examples/demo_headless.cpp` | scripted input per section through `io.Add*Event`, soft raster, one PNG per section when given a directory; no argument: writes nothing, exits 0, fails if three idle frames lay anything out; `--frame` mirrors the SDL host for comparison |
| `examples/CMakeLists.txt` | `option(IMTYPESET_BUILD_DEMO_SDL2 … OFF)` + `add_subdirectory(demo_sdl2)`; nothing changes when it is off |
| `tests/test_demo.cpp` | 8 tests, 84 checks (1 test with `IMTYPESET_DISABLE_DEMO`) |
| `docs/images/demo-*.png` | 8 section pages (soft raster, scripted), 3 host shots (opengl3 on Xvfb; stb + 16-bit on Xvfb; SDL software on dummy with a drag) |
| `.concerns/demo--builder--doc-additions.md` | header line, C counterpart, map row, THIRD-PARTY SDL2 entry, a decisions entry |

Sections: Paragraph (TextWrapped / ragged / justified, every ImTypesetStyle field); Faces and sizes
(runs, sizes, FontSize vs EmSize); Math (live source inline and display, refusal reason and byte
position, 25 table rows of which 2 refused, prices); Selection (SelectableText + by hand with
granularity and copy); Inspector (line boxes, baselines, caret under the pointer, line table);
Stress (x1..x1000, only the copies in view submitted, stats); Fonts (roles, add from disk with the
refusal reason); Known limits.

## What was run

| configuration | test_demo | example_demo_headless | demo_sdl2 |
|---|---|---|---|
| debug, FreeType, 32-bit | 8/84 ok, 2.7 s, 109 MB peak RSS | ok | — |
| release (root build, `IMTYPESET_BUILD_DEMO_SDL2=ON`), ctest | ok 0.55 s | ok 6 s | `demo_sdl2_dummy` ok |
| ASan+UBSan, FreeType, 32-bit | 8/84 ok | ok | — |
| ASan+UBSan, stb, 16-bit | 8/84 ok | ok | — |
| debug, stb | 8/84 ok | ok | — |
| debug, 16-bit ImWchar | 8/84 ok | ok | — |
| debug, `-DIMTYPESET_DISABLE_DEMO` | 1/3 ok | ok (draws nothing) | — |
| standalone RelWithDebInfo, FreeType, 32-bit | — | — | dummy, offscreen, Xvfb opengl3, Xvfb sdlrenderer |
| standalone, stb, 16-bit | — | — | dummy, Xvfb opengl3 |

Stress at x1000 (6,000 paragraphs), 400 frames of wheel scrolling in `test_demo`: at most 24
widget calls and 24 layouts in a frame, the cache within its limits with evictions counted, zero
layouts once idle; the whole test binary peaks at 109 MB. Steady state in every section: 0 layouts,
0 text builds.

NOT run: a real display, a GPU, HiDPI; separate ASan runs of stb-only and 16-bit-only; the full
test suite (only `test_demo`; my one new `src/` file adds a translation unit and touches no other);
MSVC or clang; ImGui 1.93; the C counterpart in the doc-additions (not compiled); `find_package(SDL2)`
(no system SDL2 here). The context-change guard was not shown to FAIL without itself: the
four-context test passes under ASan with it, and I did not build a variant without it.

## API gaps (nothing added; each section is built on what exists)

1. **No public debug overlay.** The Inspector draws its own from `GetParagraphLine`,
   `HitTestParagraph`, `GetParagraphCaretRect`, `GetParagraphRangeRects`; the library's internal
   overlay (boxes, glue) is not reachable.
2. **No way to ask whether a paragraph handle's context is alive.** The demo compares the context
   pointer and falls back on `ImTypesetStats::TextBuilds` going down. A context generation, or
   `IsParagraphValid`, would replace a hack on a debug counter.
3. **Nothing says "the fonts changed"** (an epoch), and nothing describes a face (file, MATH table
   or not). The ImFont's debug name is the first 39 bytes of the PATH, so the Fonts table shows
   `/home/wreel/Source/imtypeset/tests/data` for all six; ImGui's own loader keeps the file name.
4. **The widgets report no status**: line count and `FirstFitFallback` exist only for paragraph
   objects.
5. **A long document is the host's to cull.** Stress does it by hand with `CalcTextSize`, which
   keeps one answer (the call before), so K texts are K layouts each time anything changes.
   `api-document` is the answer; until then this is the pattern a user copies.
6. **SelectableText has no granularity or copy hook** and no way to get the selection's text as it
   would be copied (the host can cut its own bytes, which is not the same thing for a formula or
   collapsed white space); by hand it needs a paragraph object.
7. **Header vs behaviour**: `imtypeset.h` says a character the face lacks "is drawn as its missing
   glyph"; what I see (and `stand_in` in `src/style.cpp` does) is Regular, then Math, then Code,
   then U+FFFD. The demo's Known limits says the latter.
8. No conversion between `FontSize` and `EmSize` for a face (the demo says "about 16 px").
The spec's three worries: the font-from-disk refusal reason EXISTS (`GetLastError()` after
`AddFontFromFileTTF`: "not an OpenType or TrueType font"); the stats counters exist; the overlay
does not (1).

Also seen: `CachedBytes` reports about 33 KiB per laid-out paragraph of roughly 300 bytes (12
layouts, 400 KiB), so the default 64 MiB limit is about 2,000 such paragraphs.

## What the Python prototype's demo has that this lacks

Its 14 pages (`../imgui-adv-text/advtext/ui/pages`): Specimen, Document, Blocks, Formula, Columns,
Math in text, Stream, Chat, Diff, Find, Themes, Tables, Inspector, Stress.
- **Not here at all**: Document (markdown on a page sheet), Columns, Tables, Stream (token replay
  under two commit policies), Chat, Diff, Find (search box, match marks, next/previous), Themes
  (palettes, face sets, pixel size) — all on the Python side of D1 or waiting for `api-document`;
  Blocks (a toolbar centred by fil glue) and Formula (a hand-built gallery of math-mode rules) —
  both need a public node / math-list entry, which is backlog.
- **Partly here**: Specimen (faces and sizes yes; its kerning specimen no); Math in text (inline,
  display, refusals, the subset table yes; formulas in headings, lists, quotes, tables, links and
  in a stream no); Inspector (theirs: a block's source, role, indices, unit and what a relayout
  reused; ours: line geometry, hit and caret of one paragraph); Stress (theirs: 1 to 16 panes with
  the per-frame cost in milliseconds; ours: one document x1..x1000 with the counters, no timings).
- Also theirs: a shared debug-overlay toggle, a sidebar with deep links (`--tab name:option`), link
  hit areas.

## Decisions taken unattended

- The demo adds no font and keeps the UI in ImGui's own (the hosts call `AddFontDefault()` first).
- Sections are opened from outside for screenshots and tests through the window's state storage
  (`Begin("ImTypeset Demo"); GetStateStorage()->SetInt(GetID(label), 1)`), so the demo has no
  test hooks in it.
- `IMTYPESET_DEMO_WITH_LIBRARY` and `--page paragraph` (shot-rig's scaffolding) are gone from the
  host: it is the demo's host and always links the library.
- "Submit everything" in Stress is held to 20 copies; the default mode is O(visible) by construction.
- No `git add` (the launch message and CLAUDE.md both say so; the role text said otherwise).

## Least sure of

1. **Scripted coordinates.** `demo_headless`'s scripts and `test_demo`'s x1000 test address controls
   by pixel (header k at y = 77 + 23k, the copies slider at y = 215). A change in section order,
   in ImGui's default style or in the top of the window moves them. The x1000 test would then fail
   loudly (`Layouts > 1200`); the headless scripts would silently act on something else and only
   the images would show it.
2. **The context guard** (API gap 2): correct for the cases tested, by a route I would not defend
   as a design.
3. **A real display**: never run. Xvfb + llvmpipe is the nearest thing here.
