# hidpi (H19) — result

Builder-39, an agent, 2026-10-12, Linux x86-64, GCC 12.2, FreeType 2.12.1. **There is no Mac
here and nothing below was seen on one.** "Measured" means a headless ImGui frame with
`io.DisplayFramebufferScale` set, rasterised by `tests/support/soft_raster.h`; "read" means read.

## The short answer

- **Before this task the library was wrong on a Retina display, by a run:** its glyphs were the
  bitmaps of density 1 stretched (0.500 texels per framebuffer pixel at density 2, where ImGui's
  own text in the same window has 1.000), and every position was a whole unit, so each bound the
  draw rules state was twice as many framebuffer pixels.
- **It is fixed in the tree, behind one product draw rule, with no new API and nothing for a
  host to do.** `reference()` is untouched; no golden changed; at density 1 the vertices are the
  same floats.
- **Only a person at a Mac can confirm it.** `examples/python/hidpi_check.py` is what they run;
  the checklist is at the end.

## 0. Did the test support honour the framebuffer scale?

Yes, already (*read*, then used): `soft_raster::Renderer::render` multiplies vertex positions and
scissor rectangles by `ImDrawData::FramebufferScale`, as `imgui_impl_opengl3.cpp` does. Nothing
in `tests/support` was changed.

## 1. What the library did, measured (`tests/test_hidpi.cpp`, `hidpi_report`)

One sentence in Roboto Regular at a 16-unit pixel height, three times in a 560 x 150 window:
the host's line (`ImGui::TextUnformatted`, the face added by `io.Fonts->AddFontFromFileTTF(path,
16)` with a default `ImFontConfig`, which is how hello_imgui adds one: *read*,
`hello_imgui_font.cpp`), the library's line (`DrawParagraph`, `FontSize` 16), and a line with a
formula. Off the vertices: atlas texels across each quad over the framebuffer pixels it covers,
and corners not on a framebuffer pixel (to 1/64). Off the image: ink area in square units, and
the share of the ink in pixels at least 98 % black ("solid").

**Before** (commit `4ba28cf`; pictures `.concerns/hidpi--builder--shot-today-*.png`):

| density | line | texels / fb pixel | quads off the grid | ink | solid |
|---|---|---|---|---|---|
| 1 | host | 1.000 | 0 of 56 | 1226.6 | 24.5 % |
| 1 | library | 1.000 | 0 of 56 | 1226.5 | 24.5 % |
| 1.5 | host | 1.000 | 0 | 1082.5 | 38.8 % |
| 1.5 | library | **0.667** | **53 of 56** | 1158.3 | **0.0 %** |
| 1.5 | formula | 0.667 | 33 of 33, both rules | 540.2 | 21.4 % |
| 2 | host | 1.000 | 0 | 1115.4 | 61.2 % |
| 2 | library | **0.500** | 0 (whole units) | 1165.2 | **0.0 %** |
| 2 | formula | 0.500 | 0 | 519.4 | 16.7 % |
| 3 | host | 1.000 | 0 | 1121.5 | 70.7 % |
| 3 | library | **0.333** | 0 | 1171.9 | 7.3 % |

**After** (`.concerns/hidpi--builder--report-built.txt`; pictures `…shot-built-*.png`):

| density | line | texels / fb pixel | off the grid | ink | solid |
|---|---|---|---|---|---|
| 1 | library | 1.000 | 0 | 1226.5 | 24.5 % (unchanged) |
| 1.5 | library | 1.000 | 0 | 1082.5 | 38.8 % |
| 2 | library | 1.000 | 0 | 1115.4 | 61.2 % |
| 3 | library | 1.000 | 0 | 1121.5 | 70.7 % |
| 1.5 / 2 / 3 | formula | 1.000 | 0, rules too | 463.9 / 461.3 / 482.8 | 37.8 / 58.4 / 69.2 % |

At every density the library's line now has the host's ink to 0.1 square unit and the same solid
share: the same bitmaps.

**Pictures looked at** (I opened these): `today-2.0x` and `today-1.5x` — the host's line sharp,
the library's line under it visibly soft and its letters crowded ("blackquartz"), the formula
grey; `built-1.5x`, `built-2.0x`, `built-3.0x` — the two lines indistinguishable in sharpness and
weight, the formula's bars thin and joined to the radical; `built-stb-2.0x` (stb_truetype) — the
same. `today-1.0x`, `today-3.0x`, `built-1.0x` are there and were not opened.

**Why** (*read*, `.deps/imgui/imgui.cpp` `SetCurrentWindow`, `UpdateCurrentFontSize`;
`imgui_draw.cpp` `ImFont::GetFontBaked`, `ImFontAtlas::AddFont`): ImGui raises a font's density
to the viewport's only for the font that is *current*; `GetFontBaked(size)` without a density
uses the font's own; the draw loop asks that of fonts it never pushes. So `RasterizerDensity
1.0` in the font config, which advtext pointed at, is **not** the cause and is right as it is
(the field is marked legacy and multiplies the baked density).

**In the stock wheel too** (imgui-bundle 1.92.900, stand-in headers, Xvfb + llvmpipe):
`hidpi_check.py --assume-density 2` reads **0.500, FAIL** against a sidecar built from an
archive of `4ba28cf`, and **1.000, PASS** against this tree's; the same PASS in pptmstr's
1.92.801 wheel. `--assume-density` sets the scale only around a `draw_to_buffers` call: it
exercises the library and the script's arithmetic, not a display.

## 2. The decision (D72 in `docs/decisions.md`; agent decisions)

**`DrawRules::device_pixels`, on in `product()`, off in `reference()`.** The whole of placement
is done in framebuffer pixels: origin and lengths times the density, each face baked at its size
in framebuffer pixels at ImGui's density 1, the existing rules applied to those numbers as they
stand, the ops divided by the density at the end. Layout stays in units.

- **Bounds are in framebuffer pixels**: half a pixel for a word's width, a pixel and a half
  between neighbours, a pixel between two parts of a formula, a rule within a pixel of its
  thickness and never under one. A hairline is one framebuffer pixel (half a unit at density 2).
- **`ImTypesetStyle::PixelsPerInch` is not the knob**: it is a layout quantity; 192 makes the
  text twice as large in units and breaks other lines. Right for Windows-style scaling, wrong
  for Retina.
- **The density comes from `io.DisplayFramebufferScale.x` at draw time; no API.** Weighed
  against a per-draw argument (one more thing to pass, and it would be this field) and against
  ImGui's per-viewport `GImGui->FontRasterizerDensity` (behind `imgui_internal.h`, in a struct
  the sidecar's probe cannot check against a wheel without headers). **Given up:** with
  multi-viewports on and displays of different density, a platform window on the other display
  is placed for the main one's.
- A scale not above 1, above 16, or not a number is 1.
- **Baked at the framebuffer size and density 1, not at the unit size and density d**: same
  bitmaps where both exist (measured), but the size then rounds to a framebuffer pixel instead
  of a unit, and bitmaps hang from whole framebuffer pixels under both of ImGui's loaders (the
  stb loader keeps the ascent to a whole *unit*, which at 1.5 would be half a pixel off).

## 3. What was built

`src/draw.h`, `src/draw.cpp`: the rule, `DrawArgs::density` (0 = the display's; for tests),
`MAX_DENSITY`, `Drawer::density_of`, `Drawer::ops_to_units`; the binder bakes at
`GetFontBaked(size_in_fb_px, 1.0f)` when the density is not 1 and makes the call it made when it
is; the debug overlay's outlines are a framebuffer pixel thick. `src/imtypeset.cpp`,
`src/imtypeset_document.cpp`, `src/font_loader.cpp`, the C ABI: **not touched** — the draw
entry points pass default `DrawArgs` and get the display's density.

`tests/test_hidpi.cpp` (10 tests, 37,123 checks; runs under FreeType and under stb_truetype):

| test | what it holds |
|---|---|
| `the_host_s_own_text_is_baked_at_the_framebuffer_s_density` | the yardstick, at 1, 1.5, 2, 3 |
| `glyphs_and_rules_meet_the_framebuffer_pixel_for_pixel_at_every_density` | 1.000 texel per fb pixel, nothing off the grid, the host's ink (the ink comparison is FreeType-only and says so under stb) |
| `a_selection_and_the_debug_overlay_are_on_the_framebuffer_s_grid` | highlights, line boxes, baselines |
| `placing_at_a_density_is_placing_in_framebuffer_pixels` | a list placed at density d equals, number for number, the list placed at d x the pixels per inch, divided by d: so what `test_word_gaps` and `test_harden` prove holds in fb pixels |
| `the_bounds_of_the_product_s_rules_are_in_framebuffer_pixels` | the four bounds above, off the ops, x density, over > 2,000 words, > 1,500 spaces, > 500 formula parts, > 50 rules, 5 sizes, 4 densities |
| `without_the_rule_a_bound_is_that_many_units_…` | the contrast: rule off at density 2, some word is > 0.75 fb pixel out; rule on, none > 0.5 |
| `at_density_one_the_rule_changes_nothing_and_the_reference_knows_no_density` | density 1: vertices bit-identical (pos, uv, colour) with and without the rule; `reference()` at density 2 = at density 1; rule off at 2 is 0.500 texels |
| `layout_sizes_and_hit_tests_do_not_change_with_the_density` | size, lines, a line, a hit, a caret |
| `which_densities_the_rule_follows` | 0.5, 1, 32, inf, NaN, negative are 1; no context is 1 |
| `hidpi_report` | the table and pictures, with `IMTYPESET_HIDPI_OUT=<dir>` |

A float tolerance appears in two places and neither is a golden: 1/64 px for "on the grid" (at
1.5 and 3 a framebuffer pixel is not a float32 in units) and 1e-3 on a ratio of float32 texture
coordinates.

**The sidecar.** Reading `io.DisplayFramebufferScale` is a new direct field read, so
`python/host/imtypeset_bundle_shim.cpp` has `FACT(ImGuiIO, DisplayFramebufferScale)` and
`python/imtypeset/_bundle.py`'s `_probe()` has a row for it: **25 probe rows, was 24.** No new
ImGui symbol. These two files were outside the board's write set for this task; the read made
them necessary. `gate-fixes` (builder-40) is rewriting the rest of `_bundle.py` and knows.

## 4. Advice for advtext and applications (for the handoff channel)

1. **On macOS do nothing.** No density to pass, no font option to set. Keep
   `RasterizerDensity` 1.0. Do **not** multiply `PixelsPerInch` by the framebuffer scale.
2. advtext's "96 px per inch in five places" is right on macOS: a unit there is the same nominal
   size whatever the panel, and the panel's density is the framebuffer scale. It is a question only where the platform scales in units — Windows and
   Linux under hello_imgui, which sets `style.FontScaleDpi` to the monitor's scale (*read*). A
   host that takes its type size from `imgui.get_font_size()` already follows. One that uses
   fixed sizes can pass `PixelsPerInch = 96 * style.font_scale_dpi`; pixel lengths inside a
   document's description (spacing, indents, widths) are the host's to scale. *Not run.*
3. **Vertices can be at fractions of a unit now** (halves at density 2). A host renderer fed by
   `DrawParagraphToBuffers` / `Document.draw_to_buffers` must project by the framebuffer scale
   as an ImGui backend does and must not round positions.
4. **pptmstr's P5** (labels through hello_imgui and typeset text in one pane): measured headless,
   the same face at the same pixel height now has the same ink as ImGui's at every density.
   One caveat: the library rounds a size to a framebuffer pixel and ImGui to a unit, so at
   density 2 a size that is not a whole number of units (16.3) can be one framebuffer pixel
   taller than ImGui's text "at the same size". Whole sizes are identical.
5. Do not rely on pushing the library's fonts for sharpness. (`examples/python/bundle_host.py`
   pushes one; before this change that push is what would have made that face sharp on a Mac,
   by accident: it leaves the display's density in the font.)
6. **Library and package from one tree**: 25 probe rows.

## 5. What a person at a Mac must look at

```
python/host/build_bundle_library.sh
IMTYPESET_LIBRARY=build/py-host-bundle/libimtypeset_bundle.dylib \
    python3 -P -B examples/python/hidpi_check.py --screenshot hidpi-mac.png
```

1. **Line 1 of the window says `io.DisplayFramebufferScale 2 x 2` and `style.FontScaleDpi 1`.**
   If it says 1 on a Retina panel the runner is not reporting density and nothing else counts.
2. **The printed line ends `PASS`**: `… 1.000 to 1.000 texels per framebuffer pixel, 0 off the
   grid`. `0.500 … FAIL` is the old defect.
3. **By eye, the two copies of the sentence are equally sharp and equally heavy.** The upper is
   `imgui.text`, the lower is imtypeset, same face, same size. This is the check that counts.
4. **The formula's bars are hairlines**, thinner than the separator under them, and the
   radical's bar meets its sign. Drag across the justified paragraph: sharp highlight edges.
5. If there is a non-Retina second display, drag the window onto it: line 1 becomes 1, still
   PASS, still as sharp as the application's text.

Send back the printed lines and `hidpi-mac.png` (written at the framebuffer's resolution).

## 6. For `docs/advtext-swap-status.md` (the lead holds it)

> | H19 | Retina: font density and pixel snapping | **built, not confirmed on a Mac** (task `hidpi`, not pushed; D72). *Measured on Linux by an agent, headless, framebuffer scale set to 1, 1.5, 2 and 3 and the draw data rasterised:* before, the library's glyphs were density-1 bitmaps stretched (0.500 atlas texels per framebuffer pixel at 2x against 1.000 for ImGui's own text in the same window) and every position a whole unit; the cause is not `RasterizerDensity` 1.0, which is right, but that ImGui raises a font's density only while it is the current font. Now each draw reads `io.DisplayFramebufferScale` and places and rasterises in framebuffer pixels: 1.000 texels per pixel, nothing off the grid, the same ink as ImGui's line at every scale; the draw rules' bounds are in framebuffer pixels. No call added or changed; layout, sizes and hit tests unchanged; at scale 1 the vertices are bit-identical. **A host does nothing on macOS** and must not put the scale into `PixelsPerInch`. The sidecar's probe has a 25th row: rebuild library and package together. *Needs a person at a Mac:* `examples/python/hidpi_check.py` (five things to look at, in its docstring). |

API lines: **none.** Behaviour: `DrawParagraph`, `DrawDocument`, the `*ToBuffers` calls and the
widgets can emit vertices at fractions of a unit when `io.DisplayFramebufferScale.x > 1`.

## 7. What ran on the final tree

| | |
|---|---|
| `python-host` preset (debug + Python), `build/hidpi-pyhost`, unnarrowed | 54 of 54: `test_hidpi`, `amalgamation`, `python_binding`, `python_demo_sections` among them; 0 warnings. `python_binding`, `python_demo_sections`, `amalgamation`, `test_hidpi` run again at the end: passed |
| `asan` preset (ASan + UBSan), `build/hidpi-asan` | 52 of 52; no sanitiser report |
| `release` preset, `build/hidpi-release` | 52 of 52; 0 warnings |
| `test_hidpi` under stb_truetype (narrowed private build) | 10 tests, 0 failed |
| sidecar, advtext's venv (wheel 1.92.900), `IMTYPESET_BUNDLE_HEADERS=stand-in` | exit 0, 0 warning lines, accepted by probe, 25 of 25 rows; `run_gate` accepted; `mutate_gate` 31 offsets, 90 mutants, 12 crashed, 0 survived; `check_document_edits` 0 FAIL; `check_swap_answers` all passed; `bundle_host.py --frames 30` `selection: (74, 284) layouts: 2`; `hidpi_check.py` PASS at 1 and assumed 2 and 3 |
| sidecar, pptmstr's venv (wheel 1.92.801) | the same figures |
| `clang-format --dry-run -Werror` on the five C++ files | clean |

Logs: `.concerns/hidpi--builder--gate-final.log.txt`. The private build directories are removed;
`build/hidpi-bundle-advtext`, `build/hidpi-bundle-pptmstr` and `build/hidpi-out` are left.

**On the Python results:** `gate-fixes` (builder-40) was rewriting `python/imtypeset/*.py`,
`tools/` and the build script during this task. Builder-40 then reported its three package files
whole; after that notice the `python-host` preset was configured, built and run again from an
empty directory: **54 of 54**. The two sidecars above were built at 00:44 and 00:45, after the
last change to the package (00:31), the build script (00:42) and `src/` (00:24). `gate-fixes`
was still working in `tools/` and on wheels when I stopped; these results are of the tree as it
stood then.

## 8. Not run, not measured

- **macOS, a GPU, a Retina panel, hello_imgui's GLFW backend actually reporting 2.**
- The `stb`, `wchar16`, `amalgamation` (as a preset), `fp-host`, `exceptions`, `bundle-config`,
  `imgui-next` presets in full. Only `test_hidpi` under stb.
- `bench_draw`: the cost at density 1 (a product with one per coordinate, one comparison per
  call) and at 2 (one more pass over the ops, a division per coordinate) is argued, not timed.
- A window dragged between displays; multi-viewports; a density that changes mid-session
  (read: the next draw simply reads the new value and bakes the other size).
- The sidecar against the fork's own headers (no network). The hello_imgui read was of
  `../hello_imgui` at `v1.92.900-98-g3989373`, with the two facts used checked at tag
  `v1.92.900`; `../imgui_bundle`'s own submodule is not checked out.
- `hidpi_check.py` on a display: here it ran on Xvfb at density 1.

## 9. Files

Changed: `src/draw.cpp`, `src/draw.h`, `include/imtypeset.h` (comments only: UNITS / DISPLAY
DENSITY, `ImTypesetLine`, `PixelsPerInch`), `python/host/imtypeset_bundle_shim.cpp` (one line),
`python/imtypeset/_bundle.py` (one probe row in `_probe()`), `docs/decisions.md` (D72 appended),
`docs/CHANGELOG.md` ("Dense displays" under 0.3.0).
New: `tests/test_hidpi.cpp`, `examples/python/hidpi_check.py`.
Under `.concerns/`: this file, `hidpi--builder--shot-*.png` (10), `…report-built.txt`,
`…gate-final.log.txt`.
Not mine, also modified in `git status`: everything else there is `gate-fixes`'.
Nothing was `git add`ed or committed.

## 10. Noticed, not asked

- **Stale counts**: "24 probes" in `README.md` (two rows of the wheel table), `docs/distribution.md`
  (two rows), `docs/sidecar.md` (the status table), `docs/advtext-swap-status.md` ("two probe
  rows more (24)"). It is 25 from this tree. I left them: they describe runs that were 24.
- **Xvfb does not start in this sandbox** (`/tmp` is read-only and a unix socket cannot be
  bound; a display at `:1` exists and refuses). What worked: `unshare -Urm`, a tmpfs on `/tmp`,
  `Xvfb :97 -listen tcp -nolisten unix`, `DISPLAY=127.0.0.1:97`. It races when run back to back
  (one `glfwInitSuccess` failure that passed on the re-run was this, not the library).
- ImGui rounds every font size to a whole unit, so its own text cannot be 16.5 units tall at
  density 2. The library's now can (33 framebuffer pixels). See advice 4.
- A host that pushes one of the library's fonts gets that face baked twice at density 2 (two
  keys, same bitmaps): atlas memory, not correctness.
