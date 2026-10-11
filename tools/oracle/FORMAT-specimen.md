# Specimen golden: `tests/golden/specimen/specimen.golden`

Written by `tools/oracle/dump_specimen.py` from the pinned Python reference
(`build/advtext-reference`, commit `32e3953`, `advtext/ui/specimen.py`): the
font specimen `build_specimen` returns — one line in each of six faces at one
size, one face up a ladder of six sizes, and seven pairs of letters set with
and without their kern — with **every length an integer number of scaled
points**, as the reference computed it.

It exists because the product's demo shows the same chart
(`src/imtypeset_demo.cpp`, "Specimen") and a check of that chart against
pixel figures printed to four decimals cannot see a scaled point: one is
2.03e-5 px at 96 px per inch.

Regenerate, from the repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_specimen.py

Two runs write identical bytes: `--check` dumps again in
memory and compares with the file. `--verify` re-reads the file with a reader
written from this document alone and checks every sum §4 states.

The lexical layer and the `case … endcase` frame are [FORMAT.md](FORMAT.md)'s.
The header's file id is `specimen/specimen`. There is one case, `reference`,
of kind `specimen`. Lengths are scaled points (65536 to the TeX point) unless
marked `:float`, which are pixels; `px = sp × (px_per_inch / 72.27) / 65536`.

Enums: **Face** is `prose` `prose_bold` `prose_italic` `prose_bold_italic`
`code` `math`; the glue orders are FORMAT.md's (`normal` throughout).

## 1. INPUT

    case reference specimen
    theme <px_per_inch:float> <body_px:float> <scale_ratio:float>
    measure <px:float> <width>
    specimen_px <px:float> <size>
    kern_px <px:float> <size>
    label_px <px:float> <size>
    ladder <n> <px:float>{n}
    texts <pangram:string> <ladder_text:string>
    faces <n>
    face <Face> <key:string> <label:string> <file:string>        n times
    pairs <n> <pair:string>{n}
    kern_face <Face>
    expect

- `theme`: `Theme.build()`, the reference's default: 96 px per inch, a 16 px
  body, the type scale's ratio of 1.25.
- `measure`: the width `build_specimen` was given, 760 px, which is what the
  reference's own tests use. Only the three notes are broken to it; **no row
  depends on it**.
- `specimen_px`, `kern_px`, `label_px`: `SPECIMEN_PX`, `KERN_PX`, `LABEL_PX` of
  the reference's module, each with `sp_from_px` of it — the size the six
  faces are set at (24 px), the pairs (44 px) and the gutter labels (12 px).
- `ladder`: `ladder_sizes_px(16.0)`, ascending: `16 × 1.25^(k − 2)` for
  `k = 0 … 5`, as Python doubles. The first two, 10.24 and 12.8, are not
  exactly representable; the `row` of each rung carries the size in scaled
  points, and that integer is what the text was set at.
- `face`: in `FACE_LABELS`' order, which is the order of the rows. `key` is
  the face's name in a font key (`prose-bold@1184072`), `label` what the
  reference writes in the gutter, `file` the base name of the font file the
  reference loaded for it. `tests/data/fonts` holds a file of each name with
  the same SHA-256 (`tests/golden/PROVENANCE.md`).
- `pairs`: `KERN_PAIRS`, each as its two letters. `kern_face`: `KERN_FACE`.

## 2. EXPECT: what `build_specimen` returned

    specimen <gutter> <nsections> <nabsent> <width> <height> <depth>
    section <title:string> <nrows> <has_header:bool>              nsections times
      sectionbox <width> <height> <depth>
      notebox <width> <height> <depth>
      headerbox <width> <height> <depth>                          if has_header
      row <label:string> <Face> <size> <set_width> <label_width> <has_pair:bool>
      rowbox <width> <height> <depth>
      set sample …  endset                                        if not has_pair
      pair <left:string> <right:string> <kern> <kerned> <unkerned>     if has_pair
      set kerned … endset / set unkerned … / set left … / set right …  if has_pair
    endsection

The sections are, in order, the six faces (rows in `face` order, all at
`specimen_px`), the size ladder (`prose`, rows in `ladder` order) and the
kerning (`kern_face` at `kern_px`, rows in `pairs` order). `nabsent` is 0: the
script asserts every face resolved.

- `row`: `SpecimenRow.label`, `.face`, `.size`, `.set_width`, `.label_width`.
  **`set_width` is the number the chart is about**: the natural width of the
  sample alone, gutter excluded. In the kerning section it is the kerned
  setting's.
- `pair`: `KernPair.kern`, `.kerned`, `.unkerned`. `kern` is the font's kern
  for the pair at the row's size, rounded once (`round`, half to even) on the
  way in. The label of a kerning row is that kern in pixels, signed, to two
  decimals.
- `specimen`, `sectionbox`, `notebox`, `headerbox`, `rowbox`: `Specimen.box`
  and `.gutter`, `SpecimenSection.box`, `.note`, `.header`, `SpecimenRow.box`.
  **Nothing in the product reproduces these.** They are the reference's
  arrangement of its own chart — its notes composed as markdown at `measure`,
  its gutter measured from its own labels in the code face, its rows on a
  strut of 1.5 times the size — and the demo arranges its chart with ImGui.
  They are recorded so that the file is the whole of what the call returned,
  and a port is not held to them.

## 3. A `set` group: one call of the reference's `_set`

    set <role> <Face> <size> <kerned:bool> <text:string> <width> <height> <depth> <nnodes>
    char <codepoint> <width> <height> <depth>
    kern <amount>
    glue <width> <stretch> <stretch_order> <shrink> <shrink_order>
    endset

`advtext.ui.specimen._set(text, face, size, metrics, measure, kerned=…)` with
`metrics = fontlib_metrics(font_key(face, size))` and
`measure = char_measure(fontlib_metrics)`: the horizontal box it returned
(`width`, `height`, `depth`) and, in order, what the box holds — a character
with what `measure` answers for it, a kern, the glue for a space. `_set` is
the function `build_specimen` sets every sample with; the script calls it
again so that a width that disagrees can be traced to the glyph or the kern
that differs.

| role | text | whose arguments |
|---|---|---|
| `sample` | the section's line | `build_specimen`'s own; `width` asserted equal to the row's `set_width` |
| `kerned`, `unkerned` | the pair | `build_specimen`'s own; widths asserted equal to `pair`'s |
| `left`, `right` | one letter of the pair | **the script's**: `build_specimen` never sets a letter alone. They are here because the demo measures the unkerned setting as the two letters set separately |

What `_set` does, since a port must do the same to get the same integer: a
character node per character that is not a space; between two adjacent
characters neither of which is a space, a kern of `round(metrics.kern(a, b))`
when that is not zero (and none at all when `kerned` is 0); for a space, the
interword glue of `round(metrics.space_width)` at the neutral space factor.
The space factor is not tracked. The box is packed at its natural width, so
no glue is stretched.

## 4. What holds, and is asserted while dumping and by `--verify`

- A `set`'s `width` is the sum of its `char` widths, `kern` amounts and `glue`
  widths.
- A row without a pair: its `sample`'s `width` is the row's `set_width`, its
  text the section's, its size the row's.
- A row with a pair: `kerned = unkerned + kern`, exactly; `set_width = kerned`;
  the `kerned` and `unkerned` groups have those widths; and
  `left.width + right.width = unkerned`.
- Six rows in the first section, in `face` order at `specimen_px`; `ladder`'s
  count in the second; `pairs`' count in the third, in order, at `kern_px`.

## 5. Comparing the product with it

Compare **scaled points with scaled points, inside the library**, and compare
with `CHECK_EQ`. A test that sets `text` in `Face` at `size` and reads the
natural width of the one line as a `Scaled` holds it to `set_width`, to
`kerned`, and — the two letters set separately and added — to `unkerned`;
their difference to `kern`, sign included (`rt` is positive).

Do not make the comparison through `ImTypesetLine` or any other `float`
pixel: from 256 px up a `float` steps by 3.05e-5 px or more and a scaled
point is 2.03e-5 px, so no face row (464 to 516 px) can be told from a width
one scaled point away, and a tolerance wide enough to pass is the thing this
file exists to remove. Enter the size as the `<size>` integer of
the row where the test can; where it must enter pixels, the `:float` beside
each size converts to that integer with the reference's `sp_from_px`
(`round(px × 72.27 / px_per_inch × 65536)`, half to even), also when the
pixels are first narrowed to a `float` — true of all nine sizes in this file.

If a width disagrees, the `set` group says where: compare character widths,
then kerns, then the glue.

## 6. What is not in here

- Any face the reference's specimen does not show: `code-bold`, which the
  reference backs with the bold prose file.
- Pixels as drawn: positions after snapping, the atlas, what `CalcTextSize`
  answers. This is layout arithmetic only.
- Other sizes than the reference's defaults. The demo's sliders reach them;
  the pinned tree makes them dumpable.
- The reference's notes and headings as text or as lines: only their boxes'
  extents (§2).
