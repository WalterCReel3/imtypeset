# Font goldens: `tests/golden/font/*.golden`

Written by `tools/oracle/dump_font.py` from the Python reference
(`../imgui-adv-text/advtext/fontlib.py`, fontTools 4.66.0). Regenerate with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_font.py

Two runs write identical bytes. The script exits non-zero if the directory holds
a `.golden` it did not write.

The base format is the shared one `tests/golden.h` reads: one record per line,
space-separated tokens, the first a tag; integers decimal; floats as Python's
`float.hex()`; strings percent-encoded UTF-8 (`%` alone is the empty string).
This script encodes every byte outside `A-Za-z0-9._-`. Booleans are `0`/`1`.
Bare lower-case keywords (`simple`, `os2`, `ok`) are written as they are.
A record whose length varies carries a count before the repeated part.

Fonts are named by file **basename**; the same files are in
`tests/data/fonts/`, and `FONT`/`ROLE` records carry each file's size and
SHA-256 so a test can tell it is reading the bytes the golden was made from.

## Files

| file | kind | what |
|---|---|---|
| `<basename>.raw.golden` | RAW | font-unit data of one font file, six files |
| `latinmodern-math.otf.math.golden` | RAW | the `MATH` table, and the minted codepoints |
| `<role>.answers.golden` | ANSWERS | `advtext.fontlib`'s public answers for one role, seven roles |
| `<role>.charmeasure.golden` | ANSWERS | `advtext.style.char_measure`'s rounded triples for every mapped codepoint, six roles |
| `registered.DroidSans.ttf.answers.golden` | ANSWERS | the same for a file registered by a host |
| `registry.golden` | ANSWERS | roles to files, `em_size_for_pixel_height`, failures |

RAW records say what the file holds, in font units, by glyph id. ANSWERS records
say what the reference returns, by codepoint, as floats at a size. Where the two
describe the same fact the script asserts they agree before writing either.

Roles are `prose`, `prose-bold`, `prose-italic`, `prose-bold-italic`, `code`,
`code-bold`, `math`. `code-bold` has no file of its own: with nothing registered
it is backed by `prose-bold`'s, so `code-bold.answers.golden` differs from
`prose-bold.answers.golden` only in its `FACE` line, and that is the datum.

`DroidSans.ttf` is no bundled role. It is dumped, through
`register_font(Face.CODE_BOLD, ...)`, for two things no bundled face has:
`numberOfHMetrics` (896) below `numGlyphs` (898), and a `kern` feature that is
one PairPos format 1 subtable with nothing behind it.

## What a from-scratch reader would get differently

This section is the reason the goldens exist. Each item is something the
reference's answer depends on that is fontTools' behaviour, or the reference's
own, and not what the OpenType specification alone would lead to. Every claim
here was measured on the files, and the measurement is in the goldens as a
`...STAT` record.

### How the ink box is computed

`FaceMetrics.bbox` is `fontTools.pens.boundsPen.BoundsPen` over the glyph's
outline: the exact extent of the curves, in doubles. It is not the `glyf`
header box and not the control-point box.

| file | glyphs | box is not integral | box differs from glyf header |
|---|---|---|---|
| Roboto-Regular.ttf | 1294 | 22 | 23 |
| Roboto-Bold.ttf | 1294 | 33 | 33 |
| Roboto-RegularItalic.ttf | 1294 | 748 | 749 |
| Roboto-BoldItalic.ttf | 1294 | 724 | 726 |
| Inconsolata-Medium.ttf | 962 | 6 | 49 |
| DroidSans.ttf | 898 | 0 | 0 |
| latinmodern-math.otf (CFF) | 4802 | 680 | no header |

The boxes are reproducible **bit for bit** without fontTools.
`tools/oracle/check_font_bounds.py` is a standard-library implementation of the
procedure below (glyf parser, Type 2 interpreter, bounds) and matches all 11838
`GLYPH` records exactly. The procedure, in the order of operations that matters:

**The pen.** State: `bounds` (none, or xMin yMin xMax yMax) and the current
point. "Add a point" is `min`/`max` into `bounds`.

- `moveTo(p)`: add `p`. A contour that is a lone `moveTo` counts.
- `lineTo(p)`: add `p`.
- quadratic `(c, p)` from current point `s`: add `p`. Then, only if `c` is
  outside `bounds` (`xMin <= x <= xMax and yMin <= y <= yMax` is inside), with
  `bx = (c.x - s.x) * 2.0`, `ax = p.x - s.x - bx`, and the same in y: the roots
  are `-bx / (ax * 2.0)` when `ax * 2.0 != 0`, then `-by / (ay * 2.0)` likewise;
  for each root `t` with `0 <= t < 1` add the point
  `(ax*t*t + bx*t + s.x, ay*t*t + by*t + s.y)`. Both coordinates of each such
  point are added, so an x-extremum also contributes its y.
- cubic `(c1, c2, p)` from `s`: add `p`. Then, only if `c1` or `c2` is outside
  `bounds`: `cx = (c1.x - s.x) * 3.0`, `bx = (c2.x - c1.x) * 3.0 - cx`,
  `ax = p.x - s.x - cx - bx`, same in y. Solve `(ax*3.0) t^2 + (bx*2.0) t + cx`
  and then the y equation with: if `|a| < 1e-10`, no root when `|b| < 1e-10`,
  else `-c / b`; otherwise `DD = b*b - 4.0*a*c`, and when `DD >= 0.0` the two
  roots `(-b + sqrt(DD)) / 2.0 / a` and `(-b - sqrt(DD)) / 2.0 / a`. For each
  root `0 <= t < 1` add `(ax*t*t*t + bx*t*t + cx*t + s.x, ...)`.

The "only if outside" tests are evaluated against `bounds` as it stands at that
segment, after the end point has been added. Expressions are evaluated left to
right as written, in double, with no fused multiply-add.

**TrueType (`glyf`).** Per contour, rotate the points so the contour ends on its
first on-curve point; `moveTo` that point; then each on-curve point is a
`lineTo` if no off-curve points precede it, else the off-curve run `o1..on`
followed by the on-curve `p` is `n` quadratics, with the implied on-curve point
between consecutive off-curve points computed as `(0.5 * (x + nx), 0.5 * (y + ny))`.
A contour with no on-curve point starts at `0.5 * (last + first)` and returns to
it. The closing segment back to the start is never drawn as a line, which
changes no box.

- **A simple glyph drawn at top level is shifted in x by `lsb - xMin`**: the
  `hmtx` left side bearing minus the `glyf` header's `xMin`. This is fontTools
  (`_TTGlyphGlyf._getGlyphAndOffset`), not OpenType, and it moves the box of
  one glyph in each Roboto face: `chi` (U+03C7, glyph 935), by -49, -4, +5 and
  +39 font units in Regular, Bold, Italic and BoldItalic. A rasteriser draws
  that glyph where the outline says, so its ink box in x is not where its ink
  is. Heights and depths are unaffected.
- **A composite glyph is not shifted**, whatever its `lsb`, and neither are its
  components (`endash`, `emdash`, U+2015 have `lsb != xMin` and are left alone).
  `GLYPHSTAT simple_glyphs_shifted_by_lsb_minus_xmin` and
  `composite_glyphs_with_lsb_not_xmin_unshifted` count both.
- Components are drawn through their transform as
  `(xx*x + yx*y + dx, xy*x + yy*y + dy)`, the 2.14 scales as `raw / 16384.0`,
  nested components applying the innermost transform first. The transform is
  applied to each point **before** implied on-curve points are formed.
  `ROUND_XY_TO_GRID` is ignored, so a scaled component yields non-integer
  points: Roboto-Regular's `endash` has header `xMin` 163 and ink box 162.60.
  `USE_MY_METRICS` is ignored (advances are `hmtx`'s). `SCALED_COMPONENT_OFFSET`
  is ignored and no bundled file sets it. Components placed by point matching
  (`ARGS_ARE_XY_VALUES` clear) do not occur; fontTools raises on them.
- `GLYFSTAT` records give the census a reader must handle: nested composites to
  depth 3 (Inconsolata), `WE_HAVE_AN_X_AND_Y_SCALE` (Roboto), contours starting
  off-curve (DroidSans), repeat flags everywhere. No contour lacks an on-curve
  point and none is a single point in these files; those paths are untested.

**CFF.** A Type 2 charstring interpreter over the single non-CID font:
`rmoveto hmoveto vmoveto rlineto hlineto vlineto rrcurveto hvcurveto vhcurveto
callsubr return endchar hstem vstem hstemhm hintmask cntrmask` are every
operator the file uses (`CFFOP` records), with 1039 local subroutines (bias
107), no global ones, and no non-integer operand. `vstemhm`, `rcurveline`,
`rlinecurve`, `hhcurveto`, `vvcurveto`, the flex operators, `callgsubr` and the
16.16 operand are therefore untested by any golden. Advances come from `hmtx`,
never from the charstring; they agree for all 4802 glyphs.

**Empty glyphs** answer `(0.0, 0.0, 0.0, 0.0)`, which is indistinguishable from
a glyph whose ink is the single point at the origin.

### Glyph names decide the minted codepoints

The reference mints a codepoint for each unencoded glyph a **vertical** variant
chain reaches: sort those glyphs' **names** (Python string order, which for
these ASCII names is byte order), and assign `0xFFFFD`, `0xFFFFC`, ... downward.
Sorting by glyph id gives a different, self-consistent mapping. The names are
the CFF charset's (format 0 here). 223 of the font's 4802 names are CFF standard
strings (SID below 391); none of the 227 minted glyphs' names is. A reader that
has no table of the 391 standard strings therefore still mints correctly for
this file, and would not for a font whose unencoded variants had standard names.
`SYNSTAT name_order_is_glyph_id_order` is 0, and under glyph-id order all 227
would get a different codepoint. `SYN` lists the result; `VARIANTCP` gives, for all 325 reachable glyphs, the
codepoint a `GlyphVariant` carries: the lowest codepoint mapped to the glyph,
else the minted one.

For the TrueType files glyph names are **not** reproducible and must not be
compared: the four Roboto files have a format 3 `post` table (no names) and
fontTools invents names from the cmap and its glyph list (`at`, `endash`,
`uni0259`). Only glyph ids are meaningful there. Nothing in layout reads a
TrueType glyph's name.

### Which cmap subtable

`getBestCmap()`: the first present of (platform, encoding) = (3,10), (0,6),
(0,4), (3,1), (0,3), (0,2), (0,1), (0,0). That is (3,1) format 4 for every
TrueType file and (3,10) format 12 for the math face. `CMAPSUB` marks the one
picked. `has()` is membership in that mapping, and **a codepoint the subtable
maps to glyph 0 is not in it**: fontTools drops such entries when it decodes
the subtable, so the reference's `has()` is false for them and they are
measured as unmapped codepoints are (below). No bundled file has one, so these
goldens do not test it; installed fonts do. Checked on 2026-10-09 against
`/usr/share/fonts/truetype/Gargi/Gargi.ttf`, whose (3,1) format 4 subtable maps
10 codepoints (U+200E, U+200F, U+2011, ...) to glyph 0 and whose
`getBestCmap()` holds none of them; the corpus digest
(`tests/golden/font-wild`, `FORMAT-font-wild.md`) covers the 19 installed faces
that do this. An earlier version of this paragraph said the reference would
answer yes. It was a guess and it was wrong.

### Kerning

Read from the `kern` feature's lookups (`KERNLOOKUPS`): every feature record
tagged `kern` regardless of script or language, lookup indices de-duplicated and
ascending, each lookup's PairPos subtables in order (Extension lookups
unwrapped; none occur). The answer for a pair is the first subtable, across all
of them, that has a value:

- format 1 has a value only for a listed pair. A pair whose first glyph heads a
  PairSet that does not list the second **falls through** to the next subtable.
  In Roboto-Regular 639918 glyph pairs fall through and 59725 of them are then
  kerned by the format 2 subtable (`KERNSTAT`), so stopping at "first glyph
  covered" loses most class kerning for the commonest letters. `KERNFALL`
  names 14 such pairs per Roboto face. This is also what an OpenType shaper
  does: a format 1 subtable that does not list the pair has not applied, and
  the lookup goes on to its next subtable.
- format 2 has a value for every pair whose first glyph is in its Coverage:
  the class matrix entry, class 0 for a glyph in neither ClassDef, including a
  zero entry, which ends the search.
- Only `Value1.XAdvance` is read. No file has a `ValueFormat2`, a PairSet naming
  a second glyph twice (the reference would keep the last), a second `kern`
  lookup (the reference would not accumulate), or a legacy `kern` table (the
  reference does not read one): all four are recorded as zero and untested.
- Lookup flags are ignored (DroidSans' lookup has `IgnoreMarks`).

Inconsolata and the math face have no `kern` feature and answer 0.0 for
everything.

### Scaling

`scale = size / unitsPerEm` once, then `value * scale`. Not `value * size /
unitsPerEm`. Ink boxes are the double box times `scale`, per coordinate.
`MathParams.script_scale` is `constant / 100.0`; `delim1` is `2.4 * size`,
`big_op_spacing5` is `0.1 * size`, `delim2` and `math_quad` are `size`.
`em_size_for_pixel_height` is `pixel_height * unitsPerEm / (ascent + descent)`,
left to right.

### Vertical metrics

`ascent` is `hhea.ascender` and `descent` is `-hhea.descender`. OS/2 `sTypo*`
and `usWin*` are never read; `OS2` records them so a test can see they differ
(Roboto: hhea 1900/-500, sTypo 1536/-512, usWin 1946/512). `x_height` is
`OS/2.sxHeight`, or `round(0.45 * unitsPerEm)` when the table, the field (OS/2
version below 2) or the value is absent or zero. Every file here has it, so
`registry.golden` reaches the fallback another way: `XHEIGHTFALLBACK` gives the
answer for a copy of the file with the two bytes of `sxHeight` zeroed, and the
offset of those bytes, so a test patches the font in memory.

### Unmapped codepoints

A codepoint the font does not map is measured as glyph 0: `has` is false, and
`advance`, `bbox` and `kern` are `.notdef`'s, not zero. That includes integers
that are not Unicode (`0x110000`, `0x7FFFFFFF`) and the plane-15 range on a face
that minted nothing. `italic_correction` is 0.0 and `vertical_variants` is empty
for a glyph without an entry.

### MATH

Read: the constants flagged in `MATHCONST`, `MathItalicsCorrectionInfo`, and the
`MathGlyphVariantRecord` lists of `VertGlyphConstruction`. **Not read**, and
dumped only so the file's contents are on record: top accent attachments,
extended shape coverage, every `GlyphAssembly` (the reference's out-of-scope
"ext_tag extensible assembly"; 43 vertical and 71 horizontal exist), all
horizontal constructions, `MinConnectorOverlap`. The font has no device tables
and no `MathKernInfo`. Every vertical chain's first entry is the base glyph
itself, and every base is encoded.

## RAW records: `<basename>.raw.golden`

| record | fields |
|---|---|
| `FONT` | basename, byte length, sha256, `TrueType` or `OTTO` |
| `TABLE` | tag (string; `OS%2F2`, `cvt%20`), offset, length, checksum; directory order |
| `HEAD` | unitsPerEm, indexToLocFormat, xMin, yMin, xMax, yMax |
| `HHEA` | ascender, descender, lineGap, numberOfHMetrics |
| `MAXP` | numGlyphs |
| `OS2` | version, sTypoAscender, sTypoDescender, sTypoLineGap, usWinAscent, usWinDescent, fsSelection, has_sxHeight, sxHeight, sCapHeight |
| `PICK` | what the reference uses: units_per_em, ascent, descent (positive down), x_height, `os2` or `fallback` |
| `CMAPSUB` | platform, encoding, format, entry count, picked |
| `CMAPCOUNT` | n |
| `CMAP` | codepoint, glyph id; ascending; the font's own mapping, no minted entries |
| `GLYPH` | glyph id, name, advance, lsb, kind, numberOfContours, header xMin yMin xMax yMax (ints; zeros unless glyf with contours), ink box x_min y_min x_max y_max (floats, font units) |
| `GLYPHSTAT` | key, count; absent keys are zero |
| `GLYFSTAT` | key, count; from the bytes; TrueType only |
| `CFFSTAT` | key, value; CFF only |
| `CFFOP` | operator name, occurrences over all charstrings and subroutines |
| `LEGACYKERN` | whether a `kern` table exists |
| `GPOS` | whether a GPOS table exists |
| `GPOSSCRIPT` | script tag, LangSys count, DefaultLangSys feature indices... |
| `GPOSFEATURE` | index, tag, lookup indices... |
| `GPOSSUB` | lookup, subtable, lookup type, lookup flag, is_extension, effective type, pos format, valueFormat1, valueFormat2, coverage format, classDef1 format, classDef2 format, count1, count2; `-1` where absent; from the bytes |
| `KERNLOOKUPS` | n, lookup indices the reference reads |
| `KERNSTAT` | key, count |
| `KERNSUBTABLES` | n; the subtables the reference holds, in search order |
| `KSUBSRC` | index in that order, GPOS lookup index, subtable index in the lookup, reached through an Extension lookup |
| `KSUB` | index, format, then for format 1: pair count, 0; for format 2: class1 count, class2 count |
| `KPAIR` | subtable, left glyph id, right glyph id, value; every format 1 pair |
| `KCOV` | subtable, n, glyph ids of the format 2 Coverage |
| `KCLASS1`, `KCLASS2` | subtable, class, n, glyph ids; non-zero classes only |
| `KROW` | subtable, class1, one value per class2; rows of all zeros omitted |
| `KERNAXIS` | n, codepoints: U+0020-007E then U+00A0-00FF |
| `KERNAXISGID` | n, the glyph id each axis codepoint resolves to |
| `KERNROW` | left codepoint, n values: the reference's kern with each axis codepoint as the right |
| `KERNDENSESTAT` | cells, non-zero cells |
| `KERNGID` | left glyph id, right glyph id, the reference's kern; a seeded sample |
| `KERNGIDSTAT` | pairs, non-zero pairs |
| `KERNFALL` | left codepoint, right codepoint, left glyph id, right glyph id, kern; pairs answered by falling through subtable 0 |

`GLYPH` kind is `simple`, `composite`, `empty` (no outline) or `cff`.
`KPAIR`/`KCOV`/`KCLASS*`/`KROW` are the whole kern data and `KERNROW`/`KERNGID`
are answers computed from it by the reference's search; a reader should be
tested against the answers, and can use the structure to find where it differs.

## RAW records: `latinmodern-math.otf.math.golden`

| record | fields |
|---|---|
| `FONT` | as above |
| `MATHVERSION` | 65536 |
| `MATHCONST` | position in the table, name, value, is a MathValueRecord (else a plain int16/uint16), read by the reference |
| `ITALICCOUNT`, `ITALIC` | n; glyph id, value; coverage order |
| `TOPACCENTCOUNT`, `TOPACCENT` | n; glyph id, value. Not read by the reference |
| `EXTENDEDSHAPE` | n, glyph ids. Not read |
| `MATHKERNINFO` | present |
| `MINCONNECTOROVERLAP` | value. Not read |
| `VCOUNT`, `HCOUNT` | covered glyphs, whether the reference reads the variant lists |
| `VVARIANTS`, `HVARIANTS` | base glyph id, n, then n of (variant glyph id, AdvanceMeasurement) |
| `VASSEMBLY`, `HASSEMBLY` | base glyph id, italics correction, n, then n of (glyph id, StartConnectorLength, EndConnectorLength, FullAdvance, PartFlags). Not read |
| `DEVICETABLES` | how many values carry one (0) |
| `SYNRANGE` | first (0xFFFFD), last (0xF0000); minting runs downward from first |
| `SYNCOUNT` | minted, reachable |
| `SYNSTAT` | key, value |
| `SYN` | codepoint, glyph id, glyph name; descending codepoint, which is ascending name |
| `VARIANTCP` | glyph id, the codepoint a GlyphVariant carries for it, whether it is minted |

## ANSWERS records: `<role>.answers.golden`

| record | fields |
|---|---|
| `FACE` | role, basename, backing role, has MATH |
| `SAMPLE` | codepoints sampled, kern pairs sampled |
| `GN` | codepoint, `has`, glyph id, `glyph_name`; size-independent. Compare the name for the math face only |
| `SIZE` | size; every record up to the next `SIZE` is at this size |
| `FM` | ascent, descent, x_height, em, space_width, size |
| `CH` | codepoint, has, advance, bbox x_min, y_min, x_max, y_max |
| `K` | left codepoint, right codepoint, kern |
| `MP` | MathParams field name, value; math only, all 29 fields |
| `IC` | codepoint, italic_correction; only where non-zero |
| `VV` | codepoint, n, then n of (variant codepoint, variant glyph id, extent) |
| `NOVV` | codepoint whose vertical_variants is empty |
| `IC0` | codepoint, its italic_correction, which is 0.0 |

`GlyphBBox.height` is `y_max`, `depth` is `-y_min`, `width` is `x_max - x_min`;
the script asserts all three and does not write them.

Sizes: 10.0, 12.0, 13.0, 16.0, 14.4, 655360.0, 894785.0. The last two are in
scaled points, which is what the reference's layout really passes
(`metrics(face, float(spec.size))`); 14.4 and 894785 make `size / unitsPerEm`
inexact.

The codepoint sample is U+0020-007E, U+00A0-00FF, every fifth mapped codepoint,
a handful named in the script, and the unmapped set (`AWKWARD` in the script).
For the math face it adds every minted codepoint, every vertical-variant base,
every third codepoint with an italic correction, Greek and U+2200-22FF. `IC`
and `VV` are written for sampled codepoints only: a codepoint in the sample
with no `IC` record has italic correction 0.0, and with no `VV` an empty chain.

## ANSWERS records: `<role>.charmeasure.golden`

What layout consumes: `advtext.style.char_measure(fontlib_metrics)` on a
`CharNode`, which is `round(advance)`, `round(bbox.height)`, `round(bbox.depth)`
at an em size in scaled points, with Python's `round` (half to even). These are
the integers the port must reproduce exactly.

| record | fields |
|---|---|
| `FACE` | role, basename, backing role, has MATH |
| `CMCOUNT` | codepoints per size, sizes |
| `CMSIZE` | em size in scaled points, the font key (`prose@655360`); the `CM` records up to the next `CMSIZE` are at this size |
| `CM` | codepoint, width, height, depth (integers, scaled points) |
| `CMSTAT` | `values_exactly_on_a_half`, count |

Every codepoint the face maps, minted ones included, then two it does not map
(U+E000 and 0x110000, measured as `.notdef`). Sizes 655360, 786432, 851968,
1048576 (10, 12, 13, 16 pt), 943718 and 894785. `code-bold` has no file: with
nothing registered its answers are `prose-bold`'s.

`CMSTAT` counts the float values that were exactly `n + 0.5` before rounding
(61 to 872 per role): outline coordinates are dyadic rationals and a power-of-two
em makes the scale one too, so ties are common and round-half-away-from-zero
gives different integers.

## ANSWERS records: `registry.golden`

| record | fields |
|---|---|
| `SYNRANGE` | first, last |
| `STATE` | label; the `ROLE` and `EMSIZE` records that follow are in this registry state |
| `ROLE` | role, basename of `font_file`, `backing_face`, byte length, sha256, count of `synthetic_codepoints` |
| `EMSIZE` | role, pixel height, `em_size_for_pixel_height` |
| `ERR` | case, `ok` or the exception type, message (`%` when it would hold a path, or there is none) |
| `XHEIGHTFALLBACK` | basename, file offset of `OS/2.sxHeight`, its value in the file, unitsPerEm, x_height in font units once those two bytes are zero, `x_height` at size 16.0 |

States: `default`; `registered code-bold DroidSans.ttf`; `default again` after
`unregister_font`. Registering a role changes that role alone and `backing_face`
of it becomes itself. The reference signals failure by raising
`MissingFontError`; the port has no exceptions, so an `ERR` record names the
condition that must fail and, where the text is machine-independent, the
message.
