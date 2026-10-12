# Zoomable vector output: what it would ask of the core

An assessment for the owner to decide from. **Nothing was built**: no file under
`src/`, `include/` or `tests/` changed. It answers request H14 of
`../imgui-adv-text/HANDOFF-imtypeset.md` and replaces the framing of item 8 in
`docs/advtext-swap-status.md` ("Border questions"), which weighed a public
glyph query against moving advtext's litmus pictures here. The owner said that
is not the choice. Their words, 2026-10-11:

> "Zoomable vector image may be important if we ever decide to render mermaid
> diagrams inline."

Written by an agent (task `vector-output`). Every statement below is marked
*measured* (a program was run; how is in the last section), *read* (from the
sources, not run) or *an agent's opinion*.

---

## The answer

**The door is open, and nothing public today has to be undone to keep it
open.** A paragraph or a document, once laid out, is held in resolution-
independent units; the pixel grid is applied only when it is drawn. Measured:
the same 1,000 blocks laid out at 309 different zoom factors broke every one of
2,185 lines at the same byte, and every line stood where the zoom factor times
its place at 1x says, to 0.0002 px.

**Zooming what the core sets, inside ImGui, already works through the public
API**, sharp at every size, by saying the size again and laying out again.
Measured at 1,000 blocks: 2 to 4 ms per zoom step for what is in view. It has
three rough edges, listed below; one small addition would remove two of them.

**Labels in a diagram the application draws already work too**: a label is a
paragraph, measured and drawn where the application puts it. 500 labels cost
about 3 ms to lay out again at a new zoom and 1 to 2 ms a frame to draw.

**Export to a vector file (SVG, PDF) with no ImGui is the only one of the three
that needs something new and public**, and the core is closer to it than the
earlier assessment said: its font reader already walks every glyph's curves, in
both outline formats, to measure them.

**"Vector" on screen is a matter of words.** ImGui draws triangles and glyph
bitmaps; it has no vector text. What a person means by a zoomable vector image
in an ImGui window (it stays sharp when enlarged, and nothing moves relative to
anything else) is delivered by setting the text again at the new size, not by
drawing curves. Only a file needs real curves.

### What can be decided now, at no cost

Three rules, which the tree already keeps. Deciding them means writing them
into `docs/decisions.md` so that later work cannot break them by accident.
*(An agent's proposal.)*

1. **A laid-out object never holds a pixel decision.** Positions in it are
   scaled points. No new call returns layout geometry that has been snapped to
   pixels, and none stores a pixel size the object cannot be re-expressed
   without.
2. **Pixel rules belong to the draw step and only to it.** Zoom, if it becomes
   a parameter, is a parameter of drawing. So is "no pixel rules at all",
   which is what a vector file wants.
3. **Glyph identity stays private.** If outlines are ever handed out they are
   handed out as curves. No public call names a glyph by its number in a font
   file. (This is the promise the earlier assessment was right to fear, and it
   can simply never be made.)

And one cheap guard, which is a test and so was not written under this task:
the 309-zoom measurement above as a permanent test, so the property is held
and not merely true today.

### The questions only the owner can answer

1. **Who draws the diagram: the application, or the core?** Everything here
   assumes the application (advtext or pptmstr) lays the graph out and draws
   the boxes and arrows with ImGui's own drawing calls, and the core sets the
   labels. That needs nothing new. A core that draws diagrams is a different
   and much larger thing: laying out a graph is not typesetting.
2. **Is zoom stepped or continuous?** Stepped (a zoom menu, Ctrl +/-, a wheel
   click) is fully served today. Continuous (a pinch, an animated zoom) meets
   the one real limit: ImGui bakes type at whole-pixel sizes, so type grows in
   jumps while the layout grows smoothly. Measured below.
3. **Is export to a file a requirement?** "Save this diagram as SVG", "print
   this page as PDF". If yes, that is the piece to design. If it is only "it
   should look sharp when I zoom", nothing about files is needed.
4. **Does advtext keep its SVG litmus renderer?** It draws the *reference*
   core's layout, not this one's. Nothing in the owner's sentence needs it
   (advtext's lead read it the same way, and this assessment agrees). Whether
   it is retired is still the owner's call; it is listed here so that it is
   not decided by default.

### Recommendation (an agent's)

- **Now:** record the three rules. Build nothing.
- **When diagram work starts:** use what exists (a host block for the
  rectangle, paragraphs for labels, the pixels-per-inch lever for zoom). Add
  one thing only if the edges below are felt: a **draw scale**, a single number
  on the draw call. A scratch patch of one line showed it gives exactly the
  vertices that laying out again gives (0 of 16,656 differ).
- **Export:** nothing until question 3 is answered yes. If it is, start with a
  program in this tree that uses the internals and writes SVG, which promises
  nothing to any binding; make it public only when an application needs to
  call it at run time.

**What would change this.** A yes to question 3 moves export from "later" to
"design it". A "continuous" to question 2 makes the type-size steps a defect
to solve rather than a limit to state. A "the core" to question 1 changes the
border of the project and needs its own assessment.

---

## The three things the sentence can mean

A diagram is boxes, lines, arrows and labels. The labels are text, sometimes
math: that is the core's part. "Zoomable" means the same picture at another
scale without blur. That separates into three features with different costs.

### (a) Zooming what the core set, inside ImGui

**How it works today.** The public style has two numbers that matter:
`EmSize` (the type size in pixels) and `PixelsPerInch` (how many pixels make
an inch, default 96). Layout is done in TeX's scaled points; pixels are
converted through `PixelsPerInch`. So there are two different zooms a host can
ask for, and both exist:

| | what the host changes | what happens |
|---|---|---|
| **picture zoom** | every pixel length times z, *and* `PixelsPerInch` times z | the layout in points is the same; the same lines, the same breaks; the page is z times as large |
| **text zoom** | `EmSize` times z only | the type is larger in the same column; lines are broken again, as a browser's text zoom does |

A diagram wants the first: a label must not re-break inside its box when the
diagram is enlarged, because the graph was arranged around the label's size.

**Measured: a picture zoom is the same layout.** 1,000 blocks (the sample
prose, cycled), a 558 px measure and a 16 px em at 1x, laid out whole at nine
chosen zoom factors from 0.5 to 4 and then at 300 more from 0.51 to 4.01:

- blocks whose lines ended at a different byte than at 1x: **0**, at all 309;
- worst difference between a line's right edge and z times its right edge at
  1x: **0.0002 px**; baselines 0.0001 px;
- block tops: within 0.007 px per unit of z over a 55,988 px column, which is
  the grain of a 32-bit float at that height, not a layout difference.

**Measured: it is sharp, and enlarging the 1x picture is not.** One five-line
justified paragraph with a formula, rendered by the tree's software renderer:

| picture | inked pixels that are at least 90 % ink | 10 to 90 % (soft edge) |
|---|---|---|
| set at 1x | 28 % | 66 % |
| set at 2x | 51 % | 43 % |
| the 1x picture enlarged to 2x | 2 % | 88 % |
| set at 4x | 73 % | 24 % |
| the 1x picture enlarged to 4x | 5 % | 84 % |

"Enlarged" is what scaling ImGui's vertex buffer does, and what a host with a
zoomable canvas does if it knows nothing about the text: every glyph becomes a
stretched bitmap. The pictures are in `build/vector-output/out/` (not tracked).

**Measured: what a zoom step costs.** 1,000 blocks, release build, a view
768 px tall a quarter of the way down, 40 steps of 3 % each. Two runs, on a
16-thread machine with other builds running (load average 9 to 15), so these
are on the high side and the spread is wide; the ranges are the medians of the
two runs:

| | per zoom step |
|---|---|
| picture zoom, what the user waits for (describe the blocks again, then the first draw of the view, which lays out what is in view and bakes the new size) | **2.1 to 3.8 ms** (worst 95th percentile 5.2 ms) |
| text zoom, the same without describing again | 1.6 to 2.0 ms (worst 95th percentile 6.8 ms) |
| every later frame at that zoom | 0.09 to 0.19 ms |
| laying out the other 1,000 blocks so the scroll bar is exact | 34 to 80 ms; can be deferred, the document is lazy |

A frame at 60 Hz is 16.7 ms. So for the view, **"zoom = say the size again
and lay out again" is adequate as it stands.** The 34 to 80 ms is paid only by
a host that insists on an exact scroll extent at every step.

The atlas did not grow with the number of sizes: it was 512 x 128 texels at
the end of each 40-step pass (sampled per pass, not per step). ImGui drops
sizes not used for two frames when it needs room (*read*:
`ImFontAtlasBuildDiscardBakes` in the pinned ImGui).

**The three rough edges.**

1. **A picture zoom makes the host describe every block again.** Spacing,
   indents, rule thickness, column widths and mark sizes are given in pixels,
   so they must all be multiplied and handed over again. Measured: 1.0 to
   1.9 ms per 1,000 blocks, and every layout is thrown away although the
   result in points is identical. *Read, not tried:* the header says a
   streaming table's held column widths are given up when its size changes
   and survive only certain edits, so a table being streamed during a zoom
   may shift.
2. **The type size moves in whole pixels.** ImGui 1.92 rounds the size it
   bakes to a whole pixel (*read*: `GetRoundedFontSize`; its own comment says
   this may be lifted later). The layout follows z smoothly; the glyphs do
   not. Measured over a slow zoom from 1.00 to 1.25 in 26 steps: **5 distinct
   drawings of the type**, and the drawn type's width per unit of zoom varied
   by 8 % (290 to 314 on the measure used) between its smallest and largest.
   Between steps the letters sit a little loose or a little tight. At a fixed
   zoom this is the same trade the reference documents at 1x; it is visible
   only while zoom is changing slowly.
3. **Glyphs shift by a pixel or so between zoom levels.** Each zoom level is
   snapped to its own pixel grid by the product draw rules (D47, D48, D50),
   which promise neighbours within 1.5 px of their laid-out distance *at that
   size*. Measured, where a glyph stands at 2x against twice where it stands
   at 1x: mean 1.1 px, worst 5 px, 72 of 246 more than a pixel (up to about a
   pixel of each figure is the bitmap's own whole-pixel bearing, which cannot
   be separated from outside). At rest nobody sees this. In an animated zoom
   it reads as shimmer.

A host can hide both 2 and 3 during a gesture by enlarging the vertices while
the fingers move (soft but smooth) and setting the text properly when they
stop. That is the application's choice and needs nothing from the core.

**Limits of this route** (*measured* for the paragraph above, *read* for the
clamps): the atlas holds 1 MiB at 8x, 4 MiB at 16x, 16 MiB at 24x (a 384 px
em), and the first draw at 24x took 11 to 13 ms. `PixelsPerInch` is clamped
to 24..2400, so a picture zoom spans 0.25x to 25x around the default; the em
is clamped to 1024 px; ImGui's atlas stops at 8192 x 8192 texels. Enough for
reading a diagram; not a microscope.

**The smallest surface that would remove edges 1 and part of 3: a draw
scale.** One number that says "draw this layout at z times its size".

- *Read:* the draw loop already takes the pixels-per-inch as an argument of
  each draw, separate from the layout (`DrawArgs::px_per_inch` in
  `src/draw.h`); the public calls pass the layout's own value.
- *Measured, in a scratch copy with that one line changed for paragraphs:*
  the 1x layout drawn at z gave **the same vertices, bit for bit**, as laying
  out at z and drawing (9 zoom factors, 35 paragraphs, 16,656 vertices, 0
  differ in position, texture coordinate or colour). The step cost fell from
  1.1 to 1.8 ms to about 0.6 ms for those 35 paragraphs: the layout is no
  longer redone.
- *Not tried:* the same for a document, whose draw converts block positions
  to pixels in several more places (`src/imtypeset_document.cpp`); and hit
  tests, selection rectangles and sizes under a draw scale, which would stay
  in layout pixels for the host to multiply.

What it would promise forever: one float, zero meaning 1. For a document it is
a field appended to `ImTypesetDocumentDraw`, which the header already says
grows that way; for a paragraph it is one more drawing function. Cost when
unused: a multiplication by one per draw call, nothing per glyph. It does not
touch layout, the goldens or the reference rules.

It does **not** fix edge 2. Nothing in the core can: fractional type sizes are
ImGui's to give.

### (b) Labels in a diagram the host draws

The shapes are the host's: ImGui's draw list has lines, curves and filled
polygons, antialiased at any scale. The core's part is the label.

**What exists and is enough** (*read* from `include/imtypeset.h`, and
*measured* with 500 labels of one to five words, a third with a formula):

| the diagram needs | the public API today |
|---|---|
| a label's size before the graph is arranged | lay the paragraph out very wide and read its one line: the natural width, unrounded. Then lay it out at the box width. 500 labels: 7 to 7.5 ms, once |
| the label drawn at a point | `DrawParagraph(paragraph, draw_list, pos)`, any draw list, any clip |
| centred, left, right | the style's alignment |
| math in a label | the same paragraph with the math flag |
| a click on a label; selecting and copying its text | `HitTestParagraph`, the range and copy calls |
| the diagram inline in a document | a host block: the document leaves a rectangle of the height the host says and reports where it is |
| zoom | the picture zoom above. 500 labels: 2.4 to 3.3 ms to lay out again per step, 1.3 to 1.8 ms a frame to draw all of them (none culled). Over 40 steps no label changed its number of lines |

**What is not there.**

- **Rotation.** A label along a slanted edge. ImGui draws no rotated text;
  a host can rotate the vertices a draw call appended, which is ImGui's own
  idiom and works on what `DrawParagraph` emits, but the pixel snapping then
  no longer lines up with the screen and the text is soft. Mermaid's labels
  are horizontal, so this may never matter.
- **A transform in the call.** Covered by the draw scale above if wanted;
  translation is the position argument.
- **Sizes are rounded up to whole pixels** by `GetParagraphSize`. A host
  that multiplies a 1x size by z is off by up to z pixels; it should measure
  from the line, which is unrounded, or measure at z. A small trap, documented
  in the header, not a defect.

**What stays the application's, whatever is decided:** parsing the diagram's
source; arranging the graph (ranks, routes, crossings); the shapes, arrowheads
and colours; panning and the zoom gesture; deciding the label's box. None of
it is typesetting, and a typesetting core that took it on would be promising a
graph-layout algorithm to every binding.

### (c) Export to a vector format, with no ImGui

A file needs two things per glyph: where it is, and its shape as curves.

**Where it is: the core has it, exactly.** *Read:* layout ships a flat list
of commands in scaled points (a glyph is a face, a size, a character and an x
and a baseline y; a rule is a rectangle), before any pixel rule
(`src/shipout.h`). A vector file wants exactly these positions, unsnapped:
the earlier assessment's question "which positions" has one answer for a file.

**Its shape: the core is most of the way there.** *Read:* the font reader
(`src/opentype.h`) walks every glyph's outline, for both formats a font can
use (TrueType `glyf`, simple and composite; and CFF, with a charstring
interpreter), feeding moves, lines, quadratic and cubic curves to a small
receiver that today only measures their extent. Emitting paths is a second
receiver on the same walk. *Measured:* it walks every outline of the seven
test fonts (11,711 glyphs with ink of 11,838 in all, 6 `glyf` fonts and 1
CFF) in 0.5 to 1.7 ms per text font and 3.4 to 4.9 ms for the math font:
about a microsecond a glyph.

What it does not read, so could not export (*read*, from the same header):
variable-font instances other than the default, CFF2, bitmap-only and colour
fonts, and composite parts placed by point matching. These are already the
reader's stated limits for layout.

Licensing (D24): the reader is written from the OpenType specification, with
the curve-extent arithmetic following fontTools (MIT) and saying so. A path
emitter and an SVG writer would be written from the OpenType and SVG
specifications the same way. PDF is a larger matter: a PDF that keeps text
selectable has to embed and subset fonts, which is a project of its own;
a PDF of filled curves is not much more than the SVG.

**"With no ImGui" needs a caveat.** *Read:* a context cannot be made without
an ImGui context, and fonts are given through ImGui's atlas. But an ImGui
context with no window, no display and no renderer is one call, and layout
needs nothing more (every probe behind this document ran that way). So
"export from a program with no window" works today in that sense; "a library
that does not link ImGui at all" would be a second public header, since every
type in the present one is ImGui's. Nothing suggests the owner needs that.

**Three sizes of surface, smallest promise first.**

| | what it is | what it promises to every binding, forever | cost when unused |
|---|---|---|---|
| **1. A program in this tree** | an exporter built on the internals, as the image tools under `examples/` are | nothing | none: not in the library |
| **2. Outlines as data** | one call per paragraph or document range: append the placed curves (points, a verb per point, a colour per shape) to arrays the host owns, in points, unsnapped. The host writes the SVG or PDF | the meaning of five verbs, the fill rule, the unit and the axis direction. **No glyph numbers, no font tables** | none in the draw loop: it is a separate call that walks the shipped list; the outline receiver costs nothing until called |
| **3. A writer** | `WriteDocumentSVG(...)` | all of 2, plus a file dialect and its choices (fonts as paths or as references, how colour and selection are shown) | the writer's code in every binary that links the library |

The earlier assessment sized a positioned-glyph query at "two or three structs,
four to six functions" and objected that it would make the reader's view of a
font's glyphs public. Size 2 avoids that objection by handing out the curves
instead of the glyph's identity. It does lose something a real SVG would want:
each repeated letter is repeated as curves. An opaque shape number, valid for
one call, would let the host write each shape once and refer to it; that is a
refinement to decide when someone is writing the exporter.

---

## Does anything public today foreclose this?

Read through `include/imtypeset.h` (all of it) with that one question.
**Nothing has to be undone.** What is there to know:

| what | finding | consequence |
|---|---|---|
| Lengths are `float` pixels throughout; layout is in scaled points | *read and measured*: the conversion is a pure scale through `PixelsPerInch`, exact enough that 309 zooms gave one layout | none. The header already describes the lever: "a host drawing at twice that scale says 192" |
| `ImTypesetLine`, block, table and column geometry | *read*: "layout's own", not snapped; *measured* for lines and block tops: they scale exactly with z (tables and columns not tried) | these are the answers a zoomed or exported picture needs, and they are right |
| `GetParagraphSize`, `GetDocumentSize`, `CalcTextSize` | *read*: whole pixels, rounded up | the one place a pixel rule is in a layout answer. Documented, and the unrounded figure is available from the lines. Keep; do not copy the habit into new calls |
| A laid-out object is drawn at the pixels-per-inch it was laid out with | *read*: the public draw passes the layout's own value | additive to change (the draw scale); nothing to undo |
| Pixel lengths inside a document's description (spacing, indents, widths, mark thickness) | *measured*: a picture zoom must describe the blocks again, 1 to 2 ms per 1,000 | an ergonomic cost, removed by a draw scale; nothing to undo |
| `FontSize = 0` follows ImGui's font size | *read*: ImGui rounds that size to a whole pixel, so the default style's em moves in whole-pixel steps of line height | a host that zooms sets `EmSize`, which is taken as given. Worth a sentence in the header when zoom is documented |
| The draw rules (reference or product) | *read*: a property of the draw call internally (`DrawRules` in `DrawArgs`); the public API always draws with the product's | a vector output simply does not run them. Rule 2 above keeps it so |
| Glyphs are named by codepoint publicly (`FaceHasGlyph`); math variants live at private codepoints of plane 15 | *read* | no glyph number is public today. Rule 3 above keeps it so |
| Fonts are added once, at no size | *read* | right for zoom: sizes are baked on demand |
| Fonts are given only through ImGui's atlas; a context needs an ImGui context | *read* | fine for export from an application; see the caveat under (c) |
| Zoom range | *read*: `PixelsPerInch` 24..2400, em up to 1024 px, baked size up to 2048 px | 0.25x to 25x; stated, not a defect |

Related, and not answered here: H19 (Retina) asks whether one logical pixel
is assumed to be one device pixel. A dense display and a zoom are the same
lever (`PixelsPerInch`, and ImGui's rasteriser density), and whatever answers
H19 should be checked against rule 2.

---

## What was measured, and how

All at commit `3a81315` (a `git archive` of it into `build/vector-output/tree`,
built there so that other sessions' unsaved edits could not affect it), GCC
12.2.0, `-O3 -DNDEBUG -ffp-contract=off`, FreeType 2.12.1, Dear ImGui 1.92.9,
32-bit `ImWchar`, Linux x86-64, AMD Ryzen 7 PRO 4750U (16 threads). Headless:
an ImGui context with no window. Built with `-j4` under a 6 GB address-space
limit and a 600 s timeout.

| probe | what it uses | load average when run |
|---|---|---|
| `bench_zoom`, sections 1 and 4 (same layout at 309 zooms; the pictures; quad positions; the slow zoom) | public API only, and `tests/support/soft_raster.h` for the pictures | 12.6 (not a timing) |
| `bench_zoom`, sections 2 and 3 (zoom step costs; 500 labels), 8 s warm-up, two runs | public API only | 12.7 rising to 15.3; then 9.1 |
| `bench_zoom`, section 5 (deep zoom, atlas size), two runs | public API only | 9.5; 7.6 |
| `bench_drawscale` (a layout drawn at z against laid out at z) | a **second** private copy, `build/vector-output/tree-drawscale`, with one line of `src/imtypeset.cpp` changed; the repository's `src/` is untouched | 12.2; 7.3 |
| `bench_outlines` (walk every outline of the seven fonts) | the internal reader `src/opentype.h` | 15.1; 6.9 |

The sources are `build/vector-output/probe/*.cpp` and the outputs
`build/vector-output/out/`. `build/` is not tracked: they are scratch, and are
gone when that directory is. If any of these numbers is to be kept current,
the probe belongs in `bench/`, which this task was told not to add to.

**Not measured, and not to be read into the above:**

- No timing was taken on a quiet machine. The load was 7 to 15 throughout;
  the figures are honest about order of magnitude (a few milliseconds, well
  inside a frame) and not to be quoted to two digits.
- Nothing ran on a GPU, on macOS, at a display density other than 1, or with
  stb_truetype in place of FreeType.
- The sample is prose and one formula. No tables, code blocks, columns or
  lists were zoomed; the "same layout" result is for paragraphs in a document
  and for stand-alone paragraphs.
- The draw scale was tried for paragraphs only, by a one-line patch in a
  scratch copy. It is evidence that the idea is sound, not an implementation.
- No SVG or path was written. That the outline walk can emit paths is read
  from its structure; only its coverage and its cost were measured.
- "Shimmer" during an animated zoom is inferred from the position figures.
  No animation was looked at by anyone.
- The pictures were looked at by the agent that made them (the 2x pair), not
  by a person. Whether set-at-z text is good enough at each size is the
  owner's judgement, as it was for the draw rules.
