# Draw goldens: `tests/golden/draw/*.golden`

Written by `tools/oracle/dump_draw.py` from the Python reference's ImDrawList
backend (`build/advtext-reference/advtext/backends/imgui_draw.py`), running inside a
real, headless ImGui frame. Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_draw.py

Two runs write identical bytes. The reference's `imgui_font_file` leaves one
`advtext-fonts-*` directory under `$TMPDIR` per run; that is its behaviour.

The lexical layer and the draw-command records (`cmds` ... `endcmds`) are
`tools/oracle/FORMAT.md` sections 1 and 4, verbatim. Colours are ImGui's packed
`ImU32` (`IM_COL32` order: alpha in the high byte), written as unsigned decimal.
Every float in these files is a float32 value unless stated, written exactly.

| file | case kinds | what |
|---|---|---|
| `tables.golden` | `table` `setup` | the pure helpers, the palettes, the font setup |
| `draw.golden` | `draw` | `draw_runs`: inputs, the calls it made, the vertices that resulted |
| `boxes.golden` | `boxes` | what ImGui's rasteriser reported, apart from any drawing |

## The three layers of a `draw` case

A case records the same drawing three ways, so that a port which disagrees can
say where.

1. **`ops`: the calls the reference made on the draw list**, in order, with
   their arguments. This is the pen-level golden. It depends on the `key`
   records (ImGui's baked size and ascent for each font key, given as INPUT)
   and on which codepoints the face has, and on nothing else a rasteriser
   produced. It is exact and must be matched exactly.
2. **`box`: the glyph box ImGui reported** for every (font key, codepoint) the
   case draws. This is the rasteriser's answer alone.
3. **`vtx` / `tri` / `drawcmd`: what ended up in the draw list.** Layer 3 is
   layers 1 and 2 combined by ImGui by the rule under "From ops to vertices".
   The script asserts that rule on every glyph and filled rectangle it writes,
   so a port whose ops match and whose boxes match has matching vertices.

The script also recomputes every op of every case from the INPUT records with
an implementation that uses nothing of the reference (`replay` in the script),
and fails if one differs. `replay` is therefore the arithmetic of "From inputs
to ops" below as running code.

## From inputs to ops

All arithmetic is in doubles except where a value passes through an `ImVec2`,
which holds float32. Write `f32(v)` for "round to float32, then widen".

    px(len)      = len * (ppi / 72.27) / 65536           (ratio formed first)
    snap(v)      = floor(v + 0.5)
    at(x, y)     = ( f32(origin.x + px(x)), f32(origin.y + px(y)) )
    span(a, b)   = lo = snap(a); hi = snap(b);
                   (lo, lo + 1) if hi <= lo and b > a, else (lo, hi)
    area(x, y, w, h) = n = at(x, y); f = at(x + w, y + h);
                   corners (span(n.x, f.x), span(n.y, f.y))

`origin` is the float32 pair in the `origin` record. `span`'s `b > a` compares
the float32-rounded values `at` returned.

In order:

1. each `fill`: `rectfilled area(rect)` in its own colour;
2. each `highlight`: `rectfilled area(rect)` in the palette's highlight;
3. each run's commands, with no span open at the start of a run:
   - `glyph`: colour is the run's ink unless an `ink` range holds the glyph's
     index (the last range whose start is `<=` the index, if the index is `<`
     its end). If the face is bound and does not have the codepoint
     (`box ... in_font = 0`): count it missing and do nothing else — it neither
     opens nor closes a span. Otherwise, with `ascent` the key's baked ascent:
     - no span open: `b = at(x, y)`; open one with anchor `(x, y, ascent)`,
       `top0 = snap(b.y - ascent)`, `prev_x = x`, `prev_pen = snap(b.x)`;
     - `pen_y = top0 + snap(px(y - anchor.y) - (ascent - anchor.ascent))`;
     - `pen_x = prev_pen + snap(px(x - prev_x))`; then `prev_x = x`,
       `prev_pen = pen_x`. The differences are integer scaled points, taken
       before `px`;
     - `text` at `(pen_x, pen_y)`, size the key's **baked** size, that colour.
       Count it a glyph, whether or not ImGui then draws anything.
   - `rulerect`: close the span; `rectfilled area(x, y, width, height)` in the
     palette's rule colour; count it a rule.
   - `setglue`, `atom`: close the span; with `debug`, and only if
     `width != natural`, `rectfilled area(x, y - height, width, height + depth)`
     in `debug_stretched` if `width > natural`, else `debug_shrunk`.
   - `line`: close the span; with `debug`, `rect area(x, y - height, width,
     height + depth)` in `debug_box`, then `linesegment` between the two
     corners of `area(x, y, width, 0)` in `debug_baseline`, thickness 1.

`ImDrawListFlags_TextNoPixelSnap` is set on the list for the duration of the
call and the list's flags are restored after (`listflags`).

## From ops to vertices

For a `text` op on a bound face, with the glyph's `box` and the list's `clip`
rectangle, **no** quad is appended when, tested in this order:

- the colour's alpha is zero (`ImDrawList::AddText` returns);
- the codepoint is 0 (the string is empty), U+000A or U+000D (`RenderText`
  treats them as a line break and a skipped character);
- `pen_y > clip.y1`, or `f32(pen_y + size) < clip.y0` (the line is below or
  above the clip rect);
- the box is not `visible` (a space, a no-break space);
- not (`x0 <= clip.x1` and `x1 >= clip.x0`), for the quad's `x0`, `x1` below.

Otherwise four vertices in the op's colour, at

    x0 = f32(pen_x + box.x0)   y0 = f32(pen_y + box.y0)
    x1 = f32(pen_x + box.x1)   y1 = f32(pen_y + box.y1)

in the order `(x0,y0) (x1,y0) (x1,y1) (x0,y1)`, and two triangles `0 1 2`,
`0 2 3` relative to the first. There is no partial clipping: a quad that
straddles the clip rect is appended whole. The size passed is the baked size,
so ImGui's `size / baked->Size` scale is exactly 1.

A `rectfilled` op appends nothing when its colour's alpha is zero; otherwise
four vertices `(x0,y0) (x1,y0) (x1,y1) (x0,y1)` at the op's corners, sharing
the atlas's white-pixel texture coordinate, and the same two triangles. A
rectangle of no extent is still four vertices.

`rect` and `linesegment` (the debug overlay) go through ImGui's `AddRect` and
`AddLine`, whose geometry depends on the list's anti-aliasing flags. Their
vertices are recorded and not checked by the script; a port calls ImGui for
them and compares ops.

For the one case with `fonts none` the reference draws in ImGui's current face
at its current size with the three-argument `add_text`; the op's size is
written as 0 and its vertices are recorded and not checked.

### What a different rasteriser changes

`pen_y` contains `- ascent`, and a FreeType or stb_truetype glyph box's `y0`
contains `+ ascent`. The baked ascent is a whole number of pixels for every key
drawn (asserted), so it cancels: a quad's y depends on the rasteriser only
through the glyph's own bitmap box. `pen_x` does not depend on the rasteriser
at all: a glyph's `AdvanceX` is never read, because every glyph is its own
`add_text` call. So between rasterisers each quad moves and resizes by the
difference of two boxes, independently of every other quad; nothing
accumulates.

Measured on this machine against `boxes.golden` (7540 boxes: six files, five
sizes, ASCII, Latin-1, and for the math face every minted codepoint and every
variant base), with a scratch program linked against the pinned ImGui:

| this build | boxes with a different quad | baked ascent / descent | rounded size |
|---|---|---|---|
| FreeType 2.12.1 (system) | 0 of 7540 | same at every size | same |
| stb_truetype, default `ImFontConfig` | 6657 of 7540, by at most 2 px | same | same |
| stb_truetype, `OversampleH = 1` | 4046 of 7540, by at most 2 px | same | same |

Under stb_truetype with the default `ImFontConfig`, `OversampleH` resolves to 2
below 36 px and 776 of the prose face's visible boxes have a fractional `x0`
(366 a fractional width): the quads are then off the pixel grid, which the
reference's device-space rules exist to prevent. With `OversampleH = 1` (or
`PixelSnapH`) every box is integral again. The reference never met this
because it only ever ran on FreeType.

## `draw` case records

INPUT, in this order:

    note <string>
    fonts <loaded|unpatched-math|none>
    origin <x> <y>
    ppi <px_per_inch>
    debug <bool>
    clip <x0> <y0> <x1> <y1>
    listflags <flags before and after the call> <the TextNoPixelSnap bit>
    palette <highlight> <rule> <debug_box> <debug_baseline> <debug_stretched> <debug_shrunk>
    keys <n>
      key <font key:string> <role:string> <size:sp> <bound:bool>
          <metrics ascent:double> <metrics descent:double> <metrics em:double>
          <requested size px:double> <baked size> <baked ascent> <baked descent>
    boxes <n>
      box <font key:string> <codepoint> <in_font:bool> <visible:bool> <colored:bool>
          <x0> <y0> <x1> <y1> <advance_x>
    fills <n>
      fill <x> <y> <width> <height> <colour>              (scaled points)
    highlights <n>
      highlight <x> <y> <width> <height>
    inks <n>
      ink <start> <end> <colour>                          (half-open glyph indices)
    runs <n>
      run <ink>
      cmds <n> ... endcmds

`fonts`: `loaded` is the reference's own `load_faces(ALL_FACES)`;
`unpatched-math` binds the math role to the font file as shipped, whose cmap
has none of the minted codepoints; `none` answers `None` for every role.

`clip` is the draw list's current clip rectangle: the display, or what the case
pushed. `origin` is what the `ImVec2` held, so `16.37` appears as its float32.

`key`: one per distinct font key among the glyph commands, in first-use order.
The three metrics are `fontlib_metrics(key)`'s (doubles, in scaled points);
`requested` is `baked_size_px(px(size), ascent, descent, em)` =
`px(size) * (ascent + descent) / em` (double); the baked values are
`ImFont::GetFontBaked(requested)`'s `Size`, `Ascent`, `Descent`. ImGui rounds
the request with `IM_ROUND` in float32: `(float)(int)((float)requested + 0.5f)`.
A port's test injects the baked size and ascent from here. With `bound = 0` the
baked values are those of ImGui's current face.

`box`: one per distinct (key, codepoint), in first-use order. `in_font` is
`ImFont::IsGlyphInFont`; the rest is `ImFontBaked::FindGlyph` and is zero when
`in_font` is 0. No glyph in these files is `colored`.

EXPECTED:

    stats <glyphs> <missing> <rules>
    ops <n>
      rectfilled <x0> <y0> <x1> <y1> <colour> <first vertex> <vertices appended>
      text <font key:string> <codepoint> <glyph index> <size> <x> <y> <colour> <first vertex> <vertices appended>
      rect <x0> <y0> <x1> <y1> <colour> <first vertex> <vertices appended>
      linesegment <x0> <y0> <x1> <y1> <colour> <thickness> <first vertex> <vertices appended>
    vtx <n>
      v <x> <y> <colour>
    tris <n>
      tri <a> <b> <c>
    drawcmds <n>
      drawcmd <elem_count> <idx_offset> <vtx_offset> <clip x0> <clip y0> <clip x1> <clip y1>
    uvs <n> <white u> <white v>                   optional; three cases only
      uv <u> <v>

Vertex numbers count from the first vertex the case appended; each case runs in
a frame of its own on the background draw list, which starts empty. The last
two fields of an op are what ImGui did with it and belong to layer 3; the
fields before them are layer 1.

`uvs` is written for `single-line`, `rules-by-hand` and
`math-display-summation` only. Texture coordinates depend on the order glyphs
were baked in and on the atlas packer, so a port's differ.

## The cases

| case | what it is for |
|---|---|
| `single-line` | one line at a whole-pixel origin |
| `justified-paragraph` | several justified lines |
| `...-fractional-origin`, `...-half-origin` | the same commands; the anchors move, the spans keep their shape; halves round up |
| `...-negative-origin` | negative pens: `snap` must floor, and 62 glyphs are dropped by the display's clip rect |
| `ragged-paragraph` | glue at its natural width |
| `mixed-faces-and-sizes` | two heading sizes, bold, italic, bold italic, code, `**bold**abutted` in one span |
| `rules-from-markdown` | a thematic break, a quote bar, a table rule |
| `rules-by-hand` (and at a negative fractional origin, dark palette) | `span`: hairlines, empties, abutting edges, exact halves, negative width, one scaled point |
| `code-block-rigid-spaces` | spaces that are glyphs: counted, placed, not drawn |
| `span-edges-by-hand` (and `-debug`) | a missing glyph mid-span, U+0020, U+00A0, U+0000, U+000D, a face and a size change inside a span, raised and lowered glyphs, glue, an atom, a rule and a line ending spans, squeezed and natural glue |
| `runs-highlights-fills-inks` | three runs in three inks; ink ranges across runs, one fully transparent, one running past the last glyph; fills (one transparent, one hairline) and highlights |
| `dark-palette` | every colour read from the palette given |
| `debug-overlay` | line boxes, baselines, stretched glue |
| `clip-rect-cutting-lines` | a pushed clip rect cutting the paragraph on four sides: 218 of 254 glyphs dropped |
| `pixels-per-inch-120` | composed and drawn at 120 px per inch |
| `math-display-summation` | the display summation, whose big sigma is a minted codepoint, through the patched font file |
| `math-display-summation-unpatched-font` | the same through the file as shipped: `missing` is 1 |
| `math-inline` | formulas in the line: fractions, a radical, scripts, their rules |
| `no-face-bound` | every role unbound |

Composed cases go through the reference's own path: `Theme.build`,
`markdown.build_document`, `ui.compose`, `ship(box, Point(0, box.height))`.

## `tables.golden`

    snap_px <value:double> <result:double>
    snap_span <near:double> <far:double> <lo:double> <hi:double>
    baked_size_px <size_px:double> <ascent:double> <descent:double> <em:double> <result:double>
    at <origin> <x:sp> <px_per_inch:double> <result>
    rgba <red> <green> <blue> <alpha> <result>
    palette <default|dark> <member> <colour>

`snap_px` is `floor(value + 0.5)` in double: `0.49999999999999994` gives 1, and
`-0.75` gives -1 where ImGui's truncating `IM_ROUND` gives 0. `baked_size_px`
returns `size_px` unchanged when `em <= 0`. `at` is the float32 hop:
`f32(origin + px(x))`, one axis.

The `font-setup` case (kind `setup`):

    imgui <version:string> <font loader name:string> <display width> <display height>
    font <role> <file basename> <backing role> <patched:bool> <role whose ImFont it shares>
    fontconfig <role> <SizePixels> <OversampleH> <OversampleV> <PixelSnapH> <RasterizerMultiply>
               <RasterizerDensity> <GlyphOffset.x> <GlyphOffset.y> <GlyphMinAdvanceX>
               <GlyphMaxAdvanceX> <GlyphExtraAdvanceX> <FontLoaderFlags> <FontNo> <MergeMode>
               <ExtraSizeScale> <Flags>
    wchar32 <bool>
    minted <count> <present in the patched font> <present in the font as shipped>

The reference loads each role with `add_font_from_file_ttf(path, 16.0)` and a
default `ImFontConfig`; the `fontconfig` record is that default read back. The
16 is a registration size and not a drawing size: every glyph is drawn at the
key's baked size. `patched` is 1 for the math role, whose file is a temporary
copy with the minted codepoints written into its format-12 cmap subtables: the
on-disk patch the port's font loader replaces. `code-bold` shares
`prose-bold`'s `ImFont`.

## `boxes.golden`

One case per role that has a file of its own, named for the role:

    face <role> <file basename> <codepoints per size>
    baked <requested size:double> <baked size> <ascent> <descent>
    size <baked size> <ascent> <descent>
      box <codepoint> <in_font:bool> <visible:bool> <colored:bool> <x0> <y0> <x1> <y1> <advance_x>

`baked` shows the size rounding (13.4999 gives 13, 13.5 gives 14). The `box`
records after a `size` are at that size: 13, 16, 19, 29 and 37 px, for U+0020
to U+007E, U+00A0 to U+00FF and a few more, plus every minted codepoint and
every vertical-variant base for the math face.
