# vector-output (H14): result

Builder-36, task `vector-output`. One file written in the tree:
`docs/vector-output.md` (untracked; not added, per the task and D4). Nothing
under `src/`, `include/`, `tests/` or `bench/` was touched, and
`docs/advtext-swap-status.md` was not edited. Scratch only under
`build/vector-output/` (ignored).

## The H14 row, for `docs/advtext-swap-status.md`

> | H14 | output that is not ImGui | **needs the user**: reassessed with the user's words in `docs/vector-output.md`; nothing built. Not a choice between A and B. What was found (an agent's assessment, task `vector-output`, at `3a81315`): (1) nothing public today has to be undone to keep vector output possible: a laid-out paragraph or document is in scaled points and the pixel rules are applied only at draw; measured, 1,000 blocks laid out at 309 zoom factors broke all 2,185 lines at the same bytes and placed them to 0.0002 px of z times their 1x place. (2) Zoom inside ImGui works now through the public API (every pixel length and `PixelsPerInch` times z): sharp at each size, 2 to 4 ms per step for the view at 1,000 blocks in release (load 9 to 15, so high-side figures). Its limits: the host describes its blocks again each step, ImGui bakes type at whole-pixel sizes so a slow zoom moves the type in jumps, and glyphs shift about a pixel between zoom levels. (3) Labels for a diagram the application draws work now: a label is a `Paragraph`; a diagram inline in a document is a host block. Graph layout and the shapes are the application's. (4) Export to SVG or PDF is the one part that needs something new; the font reader already walks every glyph's curves in both outline formats, and an export can hand out curves without ever making glyph numbers public. Proposed, not decided: record three rules (no pixel decision in a laid-out object; pixel rules belong to draw; glyph identity stays private) and build nothing until the user answers: who draws the diagram, is zoom stepped or continuous, is export to a file a requirement, does advtext keep its litmus renderer. For advtext meanwhile: no call of yours changes; keeping or dropping the SVG litmus chain is not decided by this. |

A shorter form if the table needs one:

> | H14 | output that is not ImGui | **needs the user**: reassessed in `docs/vector-output.md`, nothing built. Nothing public forecloses vector output (measured: one layout at 309 zoom factors); zoom inside ImGui and labels for an application-drawn diagram work through the public API today; export to a file is the one part needing new surface. Four questions for the user are at the top of that document. |

## What was measured, and how

Commit `3a81315`, `git archive`d into `build/vector-output/tree` and built
there (Release, GCC 12.2.0, `-O3 -DNDEBUG -ffp-contract=off`, FreeType 2.12.1,
ImGui 1.92.9, `-j4`, `ulimit -v 6000000`, `timeout 600`), headless. Probe
sources: `build/vector-output/probe/`; raw outputs and seven PNGs:
`build/vector-output/out/`.

| what | result | load average |
|---|---|---|
| same layout under a picture zoom: 1,000 blocks, 9 + 300 zoom factors 0.5..4.01, whole document laid out each time | 0 blocks with a line ending elsewhere; right edges within 0.0002 px, baselines 0.0001 px of z times the 1x value | 12.6 |
| zoom step cost, 1,000 blocks, 768 px view, 40 steps of 3 %, two runs (medians of each pass) | picture zoom: describe again 1.0 to 1.9 ms; first draw of the view 0.9 to 1.3 ms; the step 2.1 to 3.8 ms (worst p95 5.2); later frames 0.09 to 0.19 ms; settle all 1,000 blocks 34 to 80 ms. Text zoom step 1.6 to 2.0 ms (worst p95 6.8) | 12.7 to 15.3; 9.1 |
| atlas over those passes | 512 x 128 texels after every pass | same |
| 500 labels as paragraphs, a third with a formula | measure once 7 to 7.5 ms; relayout per step 2.4 to 3.3 ms; draw all 1.3 to 1.8 ms a frame; 0 of 20,000 label-steps changed line count | same |
| one paragraph, set at z against the 1x frame enlarged (software renderer) | pixels at least 90 % ink, of inked: 51 % set at 2x against 2 % enlarged; 73 % against 5 % at 4x | 12.6 |
| quad left edges at z against z times the 1x edge, 246 quads | 2x: mean 1.1 px, max 5 px, 72 over a pixel (includes each bitmap's whole-pixel bearing) | 12.6 |
| slow zoom 1.00..1.25 in 0.01 | 5 distinct drawings of the type in 26 steps; drawn width per unit of z varies 8 % | 12.6 |
| deep zoom of the paragraph, two runs | atlas 1 MiB at 8x, 4 MiB at 16x, 16 MiB at 24x; first draw at 24x 11 to 13 ms | 9.5; 7.6 |
| a 1x layout drawn at z against laid out at z, 35 paragraphs, 9 zoom factors, **in a second private copy with one line of `src/imtypeset.cpp` changed** | 0 of 16,656 vertices differ (position, uv, colour); step 0.56 to 0.64 ms against 1.1 to 1.8 ms | 12.2; 7.3 |
| walk every outline of the 7 test fonts with the internal reader, best of 5, two runs | 11,711 inked of 11,838 glyphs; 0.5 to 1.7 ms per text font, 3.4 to 4.9 ms for the CFF math font | 15.1; 6.9 |

Read, not run: `DrawArgs::px_per_inch` being a per-draw argument; the public
draw passing the layout's own; `GetRoundedFontSize` and
`ImFontAtlasBuildDiscardBakes` in the pinned ImGui; the clamps on
`PixelsPerInch` and the em; the outline walker's shape (one receiver with
move, line, quadratic, cubic); every statement about the header.

Not measured: anything on a quiet machine; a GPU; macOS; a density other than
1; stb_truetype; tables, code, lists or columns under zoom; a document under
the draw-scale patch; any SVG or path output; any animation.

## What I am least sure of

1. **The timings.** The machine was at load 7 to 15 the whole time (the other
   builders compiling). The two runs differ by up to 2x in places. The
   conclusion I draw from them, a zoom step is a few milliseconds and inside a
   frame, has a large margin; the individual figures do not.
2. **"Nothing forecloses it" rests on prose and one formula.** I did not zoom
   a table, a code block, a list with markers, a quote or columns. Their
   lengths go through the same conversion (read), but held table widths and
   container indents have their own handling on a change of scale
   (`take_style` in `src/imtypeset_document.cpp` declares containers afresh
   when the pixels-per-inch changes) and I did not exercise it.
3. **The glyph-shift figures mix two things**: where the pen went and the
   bitmap's own whole-pixel bearing at each size. From the public API I could
   not separate them. The direction is sound; "mean 1.1 px" is an upper
   figure for the pen.
4. **Whether a draw scale is the right small addition.** For paragraphs it is
   bit-identical in a scratch patch. A document converts block positions,
   culling and container lengths in more places, and hit tests and
   rectangles would need a stated rule. I recommend it conditionally and did
   not size it.
5. **My reading of the user's sentence.** I took "vector" on screen to mean
   "stays sharp and stays put when enlarged", which re-setting at the new
   size delivers. If the user means real curves on screen (text as filled
   outlines through ImGui's draw list), that is a fourth thing I did not
   assess: ImGui has no fill rule for shapes with holes, so it would need a
   tessellator.

## Noticed, not asked

- The probes live under `build/`, which is ignored: the evidence behind the
  document disappears with that directory. If the "one layout at every zoom"
  property is to be held, it wants a test (I was told to add none).
- H19 (Retina) and zoom are the same lever. Whoever answers H19 should read
  the "foreclose" table in the document.
- `FontSize = 0` (the default) takes its em from ImGui's font size, which
  ImGui rounds to a whole pixel. A host that zooms by `PushFont` sizes
  therefore gets whole-pixel steps in the LAYOUT, not only in the drawing.
  `EmSize` avoids it. The header does not say so.
