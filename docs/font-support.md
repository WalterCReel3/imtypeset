# Which fonts work

imtypeset reads fonts with its own OpenType reader (`src/opentype`, behind
`src/fontlib`), not with FreeType or HarfBuzz: layout needs exact font-unit
metrics, pair kerning and the `MATH` table, and Dear ImGui exposes none of them.
This page says what that reader has been shown to read, what it refuses, and
where it goes beyond or departs from the Python reference.

It is a measurement, not a promise. "Verified" below means: the reader's answer
was compared with the Python reference's (which reads fonts through fontTools)
and was equal — integers equal, ink-box doubles equal to the last bit. "Tested"
means a test with a hand-made font holds the behaviour, and no installed font
has the feature.

## The corpus

Every font `fc-list` reported on one Debian 12 machine on 2026-10-09: 3053
files. 128 are Type 1 (`.pfa`, `.pfb`, `.t1`) and are out of scope. The other
2925 files (2857 `.ttf`, 54 `.otf`, 14 `.ttc`) hold **2991 faces**. Noto is 80%
of it; the rest is DejaVu, Liberation, FreeFont, URW base 35, and Arabic,
Hebrew, Indic, Thai, Georgian, CJK and Uyghur families.

`tools/oracle/dump_font_wild.py` dumped a digest of each face from the
reference into `tests/golden/font-wild/`; `tests/test_opentype_wild.cpp` loads
each font from its recorded path and compares. The fonts are not in the
repository, so on another machine the test skips whatever is absent or
different and compares what is left.

## Census

Whole corpus (`IMTYPESET_WILD_ALL=1`), the same in release, debug (with
stb_truetype, and with a 16-bit `ImWchar`) and ASan+UBSan builds; no crash and
no sanitiser report in any of them.

| outcome | faces |
|---|---|
| in the corpus | 2991 |
| skipped (file absent or changed) | 0 |
| **read, every compared value equal** | **2990** |
| refused with a reason | 1 |
| **mismatched** (loaded, and at least one answer is wrong) | **0** |

Compared on the faces that loaded: 8,389,075 glyph ink boxes (every glyph of
every face, bit for bit), every advance, the whole cmap, 441,304 sampled kern
pairs, and for the six faces with a `MATH` table its constants, variant lists
and the codepoints minted for them. The reader warned about nothing in any of
the 2990.

Before task `font-wild` the same test read 2882, refused 85 and mismatched 24:

| was | faces | now |
|---|---|---|
| refused: a face of a collection (`.ttc`) | 80 | read, by face index; all 80 are also CID-keyed CFF |
| refused: CID-keyed CFF | 4 | read |
| mismatched: codepoints the file maps to glyph 0 were kept | 19 | dropped, as fontTools drops them |
| mismatched: a TrueType `MATH` font minted its codepoints in glyph-id order | 5 | minted in glyph-name order, from `post` |
| refused: no outlines (colour bitmaps) | 1 | still refused, by design |

### Refused (1)

| reason the reader gives | faces | example | the reference |
|---|---|---|---|
| the font has only bitmap glyphs (CBDT, sbix or EBDT) and no outlines, and bitmap fonts are not supported | 1 | `truetype/noto/NotoColorEmoji.ttf` | refuses it too |

### Refused by design, whatever the corpus holds

Each of these fails `load()` with a reason that names the format, so a host can
tell its user what the file is. None but the first occurs in the corpus; the
reasons are tested with hand-made files (`tests/test_opentype_formats.cpp`).

| what | reason begins |
|---|---|
| a font with bitmaps and no `glyf` or `CFF ` table | "the font has only bitmap glyphs (CBDT, sbix or EBDT) ..." |
| a font with `SVG ` or `COLR` and no outlines | "the font has only colour glyphs ..." |
| `CFF2` outlines (variable CFF) | "CFF2 outlines (a variable CFF font) are not supported" |
| WOFF | "WOFF fonts are not supported: this is a compressed web font ..." |
| WOFF2 | "WOFF2 fonts are not supported: ..." |
| Type 1 (`.pfa`, `.pfb`, an sfnt wrapper tagged `typ1`) | "Type 1 (PostScript) fonts are not supported" |
| a `cmap` with no Unicode, Symbol or Macintosh Roman subtable in a format that is read | "the font has no Unicode, Symbol or Macintosh Roman cmap subtable", or "the font's cmap subtables are in formats that are not read ..." |
| a face index the file does not have | "face index N was asked of a font collection that holds M faces" |

## Feature by feature

"Verified on N" counts faces, among the 2990 that read with everything equal,
whose file contains the feature. "Tested" and "unexercised" are as defined at
the top.

### Containers and outlines

| feature | status |
|---|---|
| TrueType outlines (`glyf`), sfnt version `0x00010000` or `true` | verified on 2856 |
| CFF outlines (`OTTO`), name-keyed | verified on 50 |
| CFF, CID-keyed: `FDArray`, `FDSelect` formats 0 and 3, a Private DICT and local subroutines per Font DICT | verified on 84 (4 single files, and all 80 collection faces) |
| TrueType/OpenType collections (`ttcf`), by face index | verified on 80 faces of 14 files (2 to 10 faces a file) |
| CFF2 | refused by design; none in the corpus |
| variable fonts (`fvar`) | read at the default instance by design (`fvar`, `gvar`, `HVAR` are not looked at); none in the corpus, so unexercised |
| WOFF, WOFF2 | refused by design; none in the corpus |
| Type 1 (`.pfa`, `.pfb`, `.t1`) | refused by design; the 128 files are not opened by the corpus test |
| bitmap-only and colour-bitmap fonts (`CBDT`, `EBDT`, `sbix`) | refused when there is no outline table (1); fonts with outlines *and* `EBDT` strikes read from the outlines (5) |
| a `loca` table with no entry after the last glyph's | read, with `glyf`'s end standing in, and a warning; refused for drawing; tested |

### Metrics

| feature | status |
|---|---|
| `head.unitsPerEm` from 64 to 2212 (27 distinct values) | verified |
| `hhea` ascender / descender | verified on 2990 |
| x-height from `OS/2.sxHeight` | verified on 2753 |
| x-height fallback, `round(0.45 em)`, when `OS/2` is version 0 or 1 or says 0 | verified on 237 |
| missing `OS/2` table | supported; no font in the corpus lacks one, unexercised |
| `hmtx` with fewer metrics than glyphs | verified on 1079 |
| `hhea.numberOfHMetrics` of zero | read with every advance zero and a warning (fontTools refuses such a font); refused for drawing; tested |

### cmap

| feature | status |
|---|---|
| subtable choice (fontTools' order: (3,10), (0,6), (0,4), (3,1), (0,3), (0,2), (0,1), (0,0); the first record of a pair stands for it) | verified: (3,1) on 2443, (3,10) on 545, (0,3) on 2; the order itself against fontTools on hand-made tables |
| format 4 | verified on 2445 |
| format 12, including codepoints beyond the BMP | verified on 545 |
| a codepoint mapped to glyph 0 is not a mapping (every format) | verified on 19 faces that map 296 such codepoints |
| format 0, format 6 | never the picked subtable in the corpus (361 format 6 subtables exist); verified against fontTools on hand-made tables |
| format 13 (one glyph for a whole group) | not in the corpus; verified against fontTools on a hand-made table. A group stays one range from the reader to the font the atlas is given (which is then written with a format 13 `cmap` too), so a "last resort" font that maps all of Unicode in a few groups costs what its groups cost: one group over U+0000..U+10FFFF is one range and a 628-byte font for the atlas, where a range a codepoint would be 1,114,112 ranges and 13 MB; tested |
| a format 12 group whose glyph ids would run past 2^32 - 1 | the codepoints whose ids fit are mapped and the rest are not, with a warning; no codepoint answers glyph 0 or an id the file never named; tested |
| format 12 and 13 groups out of order, touching, inverted, past U+10FFFF, or to glyph 0 | taken as fontTools 4.66 takes them; verified against fontTools on hand-made tables |
| format 10 | read; fontTools does not read it (it answers an empty map), so this goes beyond the reference; tested |
| the preferred subtable is in format 2, 8 or 14 | the next in order is read, with a warning. fontTools answers an empty map for 8 and reads 2 through a codec; beyond the reference; tested |
| format 14 (variation selectors) | ignored, as the reference ignores it; 83 faces carry one |
| a font with only a Windows Symbol subtable (3,0) | read with its codes as they stand (U+F020..U+F0FF typically), with a warning; the reference refuses such a font; tested |
| a font with only a Macintosh Roman subtable (1,0), format 0 or 6 | read with its codes taken to Unicode, with a warning; the reference refuses such a font; tested |
| malformed format 4 (overlapping segments, out-of-range `idRangeOffset`) | unexercised by the corpus: for every face here the subtable read by the specification alone equals fontTools' reading. A segment that points outside its glyph array is skipped with a warning where fontTools raises |

### Glyph ink boxes

The exact extent of the curves, as fontTools' `BoundsPen` computes it.
1426 of the verified faces have non-integral boxes; they are equal bit for bit.

| feature | status |
|---|---|
| simple glyphs; `loca` short (1692) and long (1164) | verified |
| empty glyphs (no data, or zero contours) | verified |
| composite glyphs | verified on 2570; nesting to depth 5 (3 faces), depth 4 (106), depth 3 (279) |
| component scale, x-and-y scale, 2x2 transform | verified on 709, 788, 323 |
| `UNSCALED_COMPONENT_OFFSET`, `ROUND_XY_TO_GRID`, `USE_MY_METRICS`, `OVERLAP_COMPOUND` (all ignored, as fontTools ignores them) | verified on 277, 2550, 2290, 1 |
| `SCALED_COMPONENT_OFFSET` | ignored; none in the corpus, unexercised |
| components placed by point matching | the component is skipped, the rest of the glyph is measured, and the font is refused for drawing. fontTools raises on such a glyph, so the reference has no box for it at all. None in the corpus, unexercised |
| fontTools' shift of a simple glyph by `lsb - xMin` | verified on 100 faces that have such glyphs |
| contours starting off-curve, with no on-curve point, or of one point | not counted across this corpus; all 8.4 million boxes are equal, so whatever occurs is handled |
| CFF Type 2 path operators: `rmoveto hmoveto vmoveto rlineto hlineto vlineto rrcurveto hvcurveto vhcurveto hhcurveto vvcurveto rcurveline rlinecurve` | verified on 122 to 134 |
| CFF hints and masks: `hstem vstem hstemhm vstemhm hintmask cntrmask` | verified on 75 to 121 |
| CFF local and global subroutines | verified on 128 and 120 |
| CFF non-integer (16.16) operands | verified on 88 |
| CFF `FontMatrix` other than 0.001 (ignored, as fontTools' glyph set ignores it) | verified on 15 |
| CFF charset formats 0, 1 and 2; the predefined ISOAdobe, Expert and Expert Subset charsets | the names decide only minting order; verified on the bundled math face, tested for the predefined ones |
| CFF `flex`, `hflex`, `hflex1`, `flex1` | no corpus font uses them; tested |
| CFF arithmetic, storage and conditional operators (`and or not abs add sub div neg eq drop put get ifelse mul sqrt dup exch index roll`) | no corpus font uses them; tested against hand-computed boxes, not against fontTools |
| CFF arithmetic that leaves a coordinate infinite or not a number (`mul` repeated past the largest double, then `sub`) | the glyph has no ink box, with a warning, and the font is refused for drawing; fontTools computes in unbounded integers and has a box. No ink box is ever infinite or NaN; tested |
| CFF `random`, and `endchar` with four operands (an accented character built from two others) | the glyph has no ink box (`random`), or the outline ends without the two components (`endchar`); unexercised |

### Kerning

The reference's rule, which the reader follows: the lookups of every feature
tagged `kern` in any script or language, in ascending lookup order; the first
PairPos subtable *across all of them* with a value answers; only the first
glyph's horizontal advance adjustment is read. This is not what a shaper does —
OpenType applies each lookup in turn and adds the adjustments up.

**How far the two rules differ was measured** (`KERN-ACCUMULATION` in the
corpus test's output; `OpenTypeFont::pair_kern_accumulated` is the other
rule): 590 faces have more than one `kern` lookup. Of the 149,304 pairs sampled
on those faces, 74,848 are kerned by either rule, and **1,426 pairs on 84 faces
get a different value** when the lookups are added up (1.0% of the sampled
pairs, 1.9% of the kerned ones); the largest difference is 369 units of a
1000-unit em (Noto Serif Kannada Bold). The measurement still ignores script,
language and lookup flags, as the reader does, so it is the difference between
the two rules and not the difference from a shaper.

| feature | status |
|---|---|
| GPOS PairPos format 1 | verified on 1385, and on 51 through Extension lookups |
| GPOS PairPos format 2 | verified on 1359, and on 53 through Extension lookups |
| Coverage formats 1 and 2 | verified on 1379 and 850 |
| ClassDef formats 1 and 2, every combination | verified on 145 to 988 per combination |
| ValueFormats other than plain XAdvance (`0x0005` on 144; value in the *second* record on 12; both records on 2) | verified: the fields not read are skipped correctly, and a pair kerned only through its second record answers 0 as in the reference |
| more than one `kern` lookup | verified on 590 (first value wins, as the reference; see the measurement above) |
| lookup flags (`IgnoreMarks` and others) | ignored, as the reference; verified on 1405 |
| contextual lookups under `kern` (GPOS types 7, 8), mark attachment (type 4) | skipped, as the reference; 503 faces have them |
| an offset of zero where a Coverage, ClassDef, PairSet, subtable or lookup is expected | "no table": the subtable or set is skipped, a missing ClassDef puts every glyph in class 0; tested |
| a ClassDef format 2 range of class 0 over a glyph an earlier range classed | leaves the earlier class, as fontTools does; tested |
| the legacy `kern` table, version 0, format 0 | **read, beyond the reference**, for a face whose GPOS has no pair kerning: see below |
| `kern` version 1 (Apple), formats 1 to 3, vertical, cross-stream and minimum-value subtables; `kerx`, `morx` | not read |
| device tables, variation of kerning in variable fonts | not read |

GPOS kerning is verified on a sample of up to 256 pairs per face, drawn from
the pairs each font kerns and pairs it does not, not on every pair.

#### The legacy `kern` table

The reference never reads it, so a font with only that table has no kerning
there. 81 corpus faces carry one, all version 0 with format 0 subtables (one
subtable on 75, two to five on 6); 21 of them have no GPOS pair kerning (for
example `truetype/farsiweb/homa.ttf`). The reader reads the table as kerning for
those 21, and leaves the other 60 to GPOS.

- The rule: a pair's value is the sum over the horizontal format 0 subtables
  that hold kerning values, in order; a subtable with the override bit
  replaces the sum so far; a pair listed twice in one subtable keeps its last
  value. A subtable's extent is taken from its pair count, since its 16-bit
  length cannot hold more than 10,920 pairs.
- There is no reference to compare with. It is compared with fontTools' own
  reading of the table (`tools/oracle/dump_kern_legacy.py`,
  `tests/golden/font-wild-kern/`): all 81 tables are equal pair for pair
  (count and hash), and 6,044 sampled pairs answer as that rule says — the
  table's value where GPOS gives none, GPOS's value where it gives any.
- It is an option, on by default: `FontLibrary::set_legacy_kern(false)`
  (`FontLoadOptions::legacy_kern`) gives the reference's answers, and the
  corpus comparison above is made that way.
- A table may hold 65,535 subtables, and their sum for one pair can reach 2^31
  font units where a GPOS value is 16 bits. The reader keeps the sum the table
  states; `FontMetrics::kern` holds the length it becomes to ±`MAX_DIMEN`, the
  layout core's bound, and returns a value inside the bound exactly as
  computed. Reading 65,535 one-pair subtables takes 1 ms in a release build;
  tested.

### MATH

| feature | status |
|---|---|
| `MATH` constants, italic corrections, vertical variant lists, `MathParams` | verified on all 6 corpus faces that have the table |
| synthetic codepoints for unencoded variants, CFF font (names from the charset) | verified on the bundled Latin Modern Math |
| synthetic codepoints for unencoded variants, TrueType font (names from `post` format 2) | verified on the 5 corpus faces that mint (DejaVu Math TeX Gyre mints 262); the naming of unnamed and repeated glyphs against fontTools on hand-made tables |
| the same, TrueType font with `post` format 3 or none | minted in glyph-id order, which is the order of the names fontTools makes up for glyphs the cmap does not reach; a glyph that a *secondary* cmap subtable reaches and the picked one does not would sort differently in the reference. No such font in the corpus; tested for the plain case |
| the same, `post` format 1 | named from the standard Macintosh order; tested |
| a non-math role backed by a font that has a `MATH` table | gets minted codepoints, as in the reference (DejaVu Sans) |
| glyph assemblies (extensible delimiters built from parts) | not read, by the reference either |
| `MATH` device tables, `MathKernInfo` | ignored; unexercised |

## The font loader over the same corpus

`tests/test_font_loader_wild.cpp` puts a deterministic sample of the corpus
through `add_face_to_atlas` into a headless ImGui atlas and bakes it at 16 px:
every 9th face, every 5th face that lives in a collection, and every face with
a `MATH` table — 353 faces (23 from collections, 29 CFF of which 23 CID-keyed,
5 with minted codepoints). Run under **both** rasterisers, FreeType 2.12.1 and
stb_truetype, with the same result:

| | |
|---|---|
| faces the atlas refused (the strict pass, or ImGui's loader) | 0 |
| codepoints asked (ASCII, a spread of each cmap, the codepoint after each, minted ones, and codepoints no font has) | 113,013: the face has 85,247 (90 of them minted), has not 27,766 |
| codepoints where `ImFont::IsGlyphInFont` disagrees with `FaceData::has` | **0** |
| glyphs whose ink box is a pixel or more each way, baked | 32,875 |
| of those, not visible in the atlas, or visible with no coverage in the texture | 0 |

That is the evidence for decision D27 beyond reading the code: the rasteriser
reads the cmap written from layout's own view of the font, so the atlas and
layout cannot disagree about which codepoints a font has, for a collection face
and a CID-keyed font as for any other. A glyph thinner than a pixel may bake to
nothing and be right (the underscore of Sahadeva is 0.47 px at 16 px and its
own hinting closes it under FreeType), which is why the coverage check starts
at a pixel. U+0020 is left out of that check: ImGui marks the space glyph
hidden whatever it holds, and four corpus fonts (three UKIJ faces and a BPG
Georgian one) have ink in theirs, which layout measures and ImGui does not
draw.

Whole corpus (`IMTYPESET_WILD_ALL=1`; 25 to 30 s in release), under FreeType
(release, and ASan+UBSan), under stb_truetype (debug with ImGui's assertions
live, and ASan+UBSan), and under FreeType with a 16-bit `ImWchar` (debug):
2990 faces baked, none refused by the atlas, 277,868 inked glyphs all visible
with coverage, and no disagreement on 939,193 codepoints asked (921,962 in the
16-bit build, where a supplementary codepoint with no private-use stand-in
cannot be asked of the atlas at all and is not counted). No sanitiser report.

### Glyph boxes the atlas's copy has raised

stb_truetype sizes a glyph's bitmap by the box in the glyph's `glyf` header and
then rasterises the outline into it. When the header says the box ends at or
below the baseline, the bitmap can start at row 0, and an edge of the outline
that lies wholly above that row fails an assertion in its rasteriser
(`z->ey >= scan_y_top`, `stbtt__rasterize_sorted_edges`), which is the host's
`IM_ASSERT`. Installed fonts have such glyphs:

| face | glyph | header yMin..yMax | the outline's top | stb_truetype, called directly |
|---|---|---|---|---|
| `nanum/NanumBarunGothic.ttf`, and `...Bold.ttf` | 17929 (`cid9327`) | -180..-4 | 11 | the assertion fails at 200 and 240 px; not at the other sizes tried (8 to 150, 400, 1000 px) |
| `teluguvijayam/Ponnala.ttf` | 697, 698 | -345..0, -424..0 | 2 (a control point; the curve rises 0.12 units) | no failure at 8 to 1000 px |

So the strict pass notes every glyph whose header box ends at or below the
baseline and which has a point of an edge above it
(`OpenTypeFont::atlas_box_repairs`), and `build_atlas_font` raises that
header's yMax over the outline in the copy the atlas is given: 4 glyphs on
those 3 faces of the 2,990. Layout never reads the header's box, so nothing
measured changes. With yMax raised to 11 the Nanum glyph rasterises at every
size tried with no assertion failure. A contour of one on-curve point has no
edge and is not counted (`fonts-deva-extra/kalimati.ttf` and `samanata.ttf`
mark an attachment point that way, 505 units above a glyph that hangs below
the baseline). For a composite the box raised to is an outer bound of its
components' points. The one case refused is a composite whose bound no 16-bit
yMax can state; no installed font is one. Tested on hand-made fonts, baked at
16 and 200 px under each rasteriser
(`a_glyph_drawn_above_a_header_box_at_the_baseline_does_not_reach_a_rasteriser`).

## Where the product departs from the reference on fonts

Recorded in `docs/divergences.md`, `## font-wild`:

- the legacy `kern` table is read (above);
- a codepoint no cmap entry maps is measured as glyph 0, the font's missing
  glyph, whatever that glyph is named. The reference looks the missing glyph up
  by the *name* `.notdef` and answers zero width and no ink when glyph 0 is
  named otherwise: 2 corpus faces (`teluguvijayam/PottiSreeramulu.ttf` is one)
  name it `nonmarkingreturn`;
- cmap format 10, the fall-back past an unreadable preferred subtable, and
  Symbol-only and Macintosh-only fonts are read where the reference has an
  empty map or refuses the font;
- a font with `numberOfHMetrics` of zero, a `loca` one entry short, or a
  glyph placed by point matching loads for metrics where fontTools raises.

Kept as the reference has them, although OpenType says otherwise, pending a
decision (the measurement above is for that decision): the first value across
`kern` lookups instead of their sum; script and language ignored; lookup flags
ignored; a pair's second value record ignored.

## What was not measured

- Any font not on that machine. In particular: no variable font, no CFF2, no
  WOFF, no font with a Mac-only or symbol cmap, no Apple `kern` format, no CFF
  font using `flex` or the arithmetic operators was available, so those rows
  above rest on hand-made test fonts or on reading the code, as each says.
- GPOS kern subtables as a whole (a sample of pairs is compared), and the
  `MATH` italic-correction table as a whole (a sample).
- stb_truetype with a 16-bit `ImWchar` was not run.
- The corpus loader test bakes at 16 px only. The glyphs whose header box is
  raised for the atlas were not baked through ImGui at the sizes where
  stb_truetype fails its assertion on the unrepaired file (200 px for
  NanumBarunGothic); that was checked by calling stb_truetype directly on the
  file with the header rewritten, and through ImGui on hand-made fonts.
- What a shaper would do. Nothing here applies GSUB; a script that needs
  shaping (Arabic, the Indic scripts) is measured and drawn glyph for
  codepoint, as in the reference.

## Reproducing

```sh
cmake -S . -B build/wild -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DIMTYPESET_MODULES="units;opentype;fontlib;font_loader" \
    -DIMTYPESET_TESTS="test_opentype_wild;test_font_loader_wild"
cmake --build build/wild
IMTYPESET_WILD_ALL=1 IMTYPESET_WILD_FEATURES=1 build/wild/tests/test_opentype_wild
build/wild/tests/test_font_loader_wild        # add -DIMTYPESET_USE_FREETYPE=OFF for stb_truetype
```

Without `IMTYPESET_WILD_ALL` the reader test visits every 8th face, plus every
face with a `MATH` table. Whole corpus: 17 s in release (it was 6 s before the
CJK collections were read: they are 5.4 of the 8.4 million glyphs).
`IMTYPESET_WILD_TRACE=1` prints each face before loading it. To measure a
different machine's fonts, regenerate the goldens there
(`tools/oracle/FORMAT-font-wild.md`, then `tools/oracle/dump_kern_legacy.py`).
