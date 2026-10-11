# Real-font corpus goldens: `tests/golden/font-wild/*.golden`

Written by `tools/oracle/dump_font_wild.py` from fontTools 4.66.0 and the Python
reference (`build/advtext-reference/advtext/fontlib.py`). Regenerate with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_font_wild.py

`--jobs N` (default: 4, or the number of cores where that is fewer), `--limit SECONDS`
per face (default 900), `--only SUBSTRING` writes the faces whose path contains it to
`$TMPDIR/font-wild-only/` and leaves the golden directory alone.

**These goldens describe one machine.** The corpus is whatever `fc-list : file`
reports where the script runs; the fonts are not in the repository (988 MB,
mixed licences). Every face carries its absolute path, byte length, SHA-256 and
CRC-32, and a test must skip — and count as skipped — a face whose file is
absent or whose bytes differ. Regenerating on another machine replaces the whole
directory; the script exits non-zero if a `.golden` it did not write is left
behind.

The base format is the shared one `tests/golden.h` reads (see
`FORMAT-font.md`): one record per line, space-separated tokens, integers
decimal, floats as `float.hex()`, strings percent-encoded with every byte
outside `A-Za-z0-9._-` escaped, booleans `0`/`1`. Hashes are 16 lower-case hex
digits, read as a string. Bare keywords (`ok`, `simple`, `os2`) are written as
they are.

## Files

| file | what |
|---|---|
| `index.golden` | the corpus, the files refused unread, the census |
| `<directory>.<NN>.golden` | face digests |

A digest file is named for the fonts' directory relative to `/usr/share/fonts`
with `/` written `.` (`truetype.noto`), then a two-digit run number: a
directory's files are sorted and cut into runs of 96. Faces within a file are in
(path, face index) order. Each face is the records from `FACE` to `END`.

## What is measured, and from where

Three sources, never mixed in one record:

- **Bytes** — read by the script with `struct`, without fontTools. What the
  file holds: `SFNT`, `TABLES`, `HEAD`, `MAXP`, `HHEA`, `OS2`, `POST`,
  `LEGACYKERN`, `GLYF`, `KSHAPE`, `CMAPBYTES`.
- **fontTools** — `TTFont(path, fontNumber=index, lazy=True)`: `FONTTOOLS`,
  `GLYPHORDER`, `CMAPSUB`.
- **The reference** — the file registered with `fontlib.register_font(role,
  path)` and read back as `fontlib._face_data(role)`, the record every public
  answer (`advance`, `bbox`, `kern`, `has`, the math methods) is computed from:
  `FONTLIB`, `PICK`, `CMAP*`, `HMTX*`, `BOX*`, `CFF*`, `KERN*`, `KSUB`, `K`,
  `MATH*`, `VV`, `SYN*`, `MP`. `role` is `code-bold`, or `math` for a file with
  a `MATH` table so that `math_metrics` answers for it; the reference reads the
  `MATH` table and mints codepoints for any role, so nothing else depends on
  the choice. The registry is restored after every face.

Values are in **font units** and glyphs are named by **glyph id**: the
reference's dictionaries are keyed by fontTools' glyph names, which a TrueType
font with a format 3 `post` table does not have, and the script maps each name
back to its id. Scaling to a size (`value * (size / unitsPerEm)`) is covered by
`tests/golden/font` and not repeated here, except for `MP`.

### Collections

The reference opens `TTFont(path, lazy=True)` with no face index, and fontTools
refuses that for a `ttcf` file. That is recorded as `FONTLIB stock ...
TTLibFileIsCollectionError`. So that the 80 faces inside this machine's 14
collections have answers to compare against, the same reference code is then
run with the face index supplied — `fontTools.ttLib.TTFont`, which `_face_data`
imports at call time, is pointed at a wrapper adding `fontNumber` for the
duration of the call — and recorded as `FONTLIB indexed`. For a file that is not
a collection nothing is patched and there is one `FONTLIB stock` record.

### Hashes

`h64` is FNV-1a with a 64-bit **word** folded per step, not a byte:

    h = 0xcbf29ce484222325
    for w in words: h = ((h ^ (w mod 2^64)) * 0x100000001b3) mod 2^64

A negative integer enters as its two's complement. A double enters as its
IEEE-754 bits with `-0.0` folded onto `+0.0` first (`value + 0.0`). What the
words are is stated per record.

## Per-face records

In the order written. A record is absent when the stage before it failed; what
failed is in `FONTTOOLS`, `FONTLIB`, `TIMEOUT` or `DUMPERR`.

| record | fields |
|---|---|
| `FACE` | path (absolute), face index, faces in the file, byte length, SHA-256 (hex), CRC-32 (zlib's, decimal) |
| `SFNT` | sfnt version (`0x00010000`, `OTTO`, `true`, `typ1`), outline flavour (`TrueType` has `glyf`; `CFF`; `CFF2`; `none`), has `fvar` |
| `TABLES` | n, tags in directory order |
| `HEAD` | unitsPerEm, indexToLocFormat (`-1 -1` if no usable `head`) |
| `MAXP` | numGlyphs |
| `HHEA` | present, ascender, descender, lineGap, numberOfHMetrics |
| `OS2` | present, version (-1 if absent), has sxHeight (version >= 2 and long enough), sxHeight |
| `POST` | `post1`, `post2`, `post2.5`, `post3`, `post4`, `none` — where fontTools takes TrueType glyph names from. For `CFF` they come from the CFF charset instead: see `CFF` |
| `LEGACYKERN` | present, table version (0 Microsoft, 1 Apple, -1 unreadable), subtable count, pairs in format 0 subtables, n, then n subtable formats. **The reference never reads this table**: a face with it and no GPOS `kern` answers 0 for every pair |
| `GLYF` | n, then n of (key, count): the composite census, TrueType only. Keys below |
| `GLYFERR` | exception type, message: the script's own `glyf` scan failed |
| `FONTTOOLS` | `ok` or exception type, message (`%` when none) |
| `GLYPHORDER` | length of fontTools' glyph order |
| `CMAPSUB` | platform, encoding, format, language, entries (variation selectors for format 14), picked by `getBestCmap()` |
| `CMAPERR` | exception type, message: fontTools could not list the cmap |
| `FONTLIB` | `stock` or `indexed`, role, `ok` or exception type, message. **The expected outcome**: a type other than `ok` is the reference refusing the font |
| `PICK` | units_per_em, ascent (`hhea.ascender`), descent (`-hhea.descender`), x_height, `os2` or `fallback` (`round(0.45 * unitsPerEm)`, half to even, when `OS/2.sxHeight` is absent or zero) |
| `CMAP` | entries, h64 over (codepoint, glyph id) pairs ascending by codepoint, entries naming a glyph id not below numGlyphs, minted codepoints. The font's own mapping: minted codepoints are counted, not hashed |
| `CMAPS` | n, then n of (codepoint, glyph id): first, last and evenly spaced |
| `CMAPBYTES` | format, entries the picked subtable has when read as the specification describes, how many of those map to glyph 0, how many of those are not U+FFFF, codepoints only that reading has (glyph 0 excluded), codepoints only the reference has, codepoints both have with different glyphs. Format `-1`: not format 4 or 12, not scanned |
| `CMAPDIFF` | `only_spec`, `only_reference` or `differ`; codepoint; glyph by the specification (-1 none); glyph in the reference (-1 none). At most four per kind |
| `HMTX` | glyphs, h64 over the advance of each glyph id in order |
| `HMTXS` | n, then n of (glyph id, advance): evenly spaced, plus the ids either side of numberOfHMetrics |
| `BOXES` | glyphs, h64 exact, h64 rounded, boxes that are all zero, boxes with a non-integral coordinate, boxes with a coordinate exactly on a half, glyphs whose drawing raised |
| `BOXCHUNKS` | 16, then 16 h64 exact over glyph ids `[k*n//16, (k+1)*n//16)` |
| `BOXERR` | glyph id, exception type, message; the first eight |
| `BOXI` | glyph id, kind, x_min, y_min, x_max, y_max as **integers**: every coordinate of the double box is exactly integral |
| `BOXF` | glyph id, kind, x_min, y_min, x_max, y_max as hex floats |
| `BOXX` | glyph id, kind: drawing this glyph raised |
| `CFF` | major, CID-keyed (names are `cidNNNNN`), FDArray length, CharstringType, FontMatrix is `[0.001 0 0 0.001 0 0]`, global subroutines, local subroutines (all FDs), subroutines some glyph called, non-integer operands |
| `CFFOPS` | n, operator names occurring in any glyph's charstring or any called subroutine |
| `CFFERR` | exception type, message |
| `KERNLOOKUPS` | FeatureRecords tagged `kern`, distinct lookup lists among them, n, then the n lookup indices the reference reads (their union, ascending) |
| `KSHAPE` | lookup, subtable, lookup type, lookup flag, is Extension, effective type, pos format, ValueFormat1, ValueFormat2, Coverage format, ClassDef1 format, ClassDef2 format, count1, count2. From the bytes, for every subtable of every `KERNLOOKUPS` lookup; -1 where absent; fields past pos format only for effective type 2. The reference reads effective type 2 and skips the rest |
| `KSHAPEERR` | exception type, message |
| `KERNSUBTABLES` | how many subtables the reference holds, in search order |
| `KSUB` | index, format, count1, count2, h64. Format 1: pairs, 0, hash over (left, right, value) ascending. Format 2: Class1Count, widest row, hash described below |
| `KERNS` | pairs sampled, how many are non-zero |
| `K` | n (at most 16), then n of (left glyph id, right glyph id, kern, index of the `KSUB` that answered or -1) |
| `KERR` | left, right, exception type: `pair_kern` raised |
| `MATHCONSTS` | n, then n of (name, value), by name |
| `MATHITALIC` | entries, h64 over (glyph id, value) ascending |
| `MATHITALICS` | n, then n of (glyph id, value) |
| `MATHVARIANTS` | bases with a vertical variant list, h64 over, per base ascending: base, n, then n of (variant glyph id, AdvanceMeasurement, codepoint) |
| `VV` | base glyph id, n, then n of (variant glyph id, AdvanceMeasurement, the codepoint a `GlyphVariant` carries: lowest mapped, else minted); 24 bases |
| `SYN` | minted codepoints, h64 over (codepoint, glyph id) by descending codepoint, whether that order is also ascending glyph id |
| `SYNS` | codepoint, glyph id, glyph name; 16 samples |
| `MATHPARAMS` | `ok` or exception type, message, size (10.0) |
| `MP` | `MathParams` field, value at that size (hex float); all 29 fields, by name |
| `TIMEOUT` | the stage that was running at the time limit |
| `DUMPERR` | stage, exception type, message: the script failed. None occur; one would be a bug here |
| `END` | face index |

### Ink boxes

`BOXES`' exact hash is over, per glyph id in order: the word 0 then the four
doubles' bits (x_min, y_min, x_max, y_max) as `FaceMetrics.bbox` would scale
them — `_FaceData.glyph_bounds`, `BoundsPen`'s box, `(0, 0, 0, 0)` for a glyph
with no ink — or five words of 1 for a glyph whose drawing raised. The rounded
hash is the same with each coordinate replaced by Python's `round()` (half to
even) of it. `FORMAT-font.md` gives the procedure that reproduces the doubles
bit for bit, including fontTools' shift of a top-level simple glyph by
`lsb - xMin`. If a reader matches the rounded hash and not the exact one, it
differs below the integer; if neither, `BOXCHUNKS` bisects to a sixteenth of
the glyph ids and the `BOX*` samples may name the glyph.

Every glyph of every face is drawn (8.39 million; this is nearly all of the
run time). The sample is glyph 0, the highest id, the first six and last two
composites and eight spread through them, the first four empty glyphs, a few
with non-integral boxes, then evenly spaced ids and a seeded fill, to 64 or a
few more. `kind` is `simple`, `composite` or `empty` from the `glyf` header
(numberOfContours positive, negative, zero or no data), `cff` for CFF.

### Kerning

The sample is drawn from what the reference holds, so it is of pairs the font
kerns and pairs it does not: (0, 0), (last, last), (0, last); up to 80 pairs
listed in format 1 subtables, evenly spaced; up to 96 non-zero and 16 zero pairs
from the format 2 subtables' own coverage and class-2 glyphs; up to 32 pairs
whose left glyph heads a format 1 PairSet and whose right is drawn from the
format 2 class glyphs — the pairs that fall through an unlisting format 1
subtable; then seeded random pairs to 256. A face with no kern subtable gets 8.

`K`'s last field says which subtable answered, replaying the reference's
search (`_subtable_kern`): the first subtable **across all kern lookups** with a
value. That is not OpenType's rule — every lookup of the feature applies and
adjustments accumulate — and 590 faces here have more than one kern lookup, so
on those faces the reference and a shaper disagree. The port matches the
reference.

`KSUB`'s format 2 hash is over: coverage count, coverage glyph ids ascending;
ClassDef1 entry count, (glyph id, class) ascending by glyph id; ClassDef2 the
same; then per Class1Record its Class2Record count followed by each
`Value1.XAdvance`. fontTools omits class 0 glyphs from a ClassDef and so do
these.

### `GLYF` keys

`simple`, `simple_zero_contours`, `composite`, `no_data` (loca gives the glyph
fewer than 10 bytes), `data_shorter_than_header`, `components`,
`components_placed_by_point_matching`, `components_naming_a_glyph_out_of_range`,
`composite_contours_below_minus_one`, `simple_with_lsb_not_xmin` (the glyphs
fontTools draws shifted in x), `max_component_depth`,
`component_cycle_or_depth_over_64`, and `glyphs_with_<FLAG>`: composite glyphs
with a component setting that flag. Absent keys are zero. Simple-glyph contours
are not scanned (contours starting off-curve, with no on-curve point, of one
point): `tests/golden/font` has that census for the bundled files only.

## `index.golden`

| record | fields |
|---|---|
| `CORPUS` | files `fc-list` reported, sfnt files digested, faces, files refused unread |
| `SHARD` | golden file name, faces in it |
| `REFUSED` | path, reason: `Type1` for `.pfa`/`.pfb`/`.t1`, which are out of scope and not opened |
| `CENSUS` | faces counted, one example (`path#index`), n, then the n parts of the key |

The census counts **faces**, except `cmap_subtable`, `kern_subtable`,
`kern_value_formats`, `kern_coverage_format`, `kern_classdef_formats` and
`kern_feature_subtable_not_read`, which count subtables. `kerning` is `gpos`
when the reference holds at least one PairPos subtable and `none` otherwise,
with `+legacy_kern_table` when the file has a `kern` table.

## Size and run time, measured 2026-10-09

3053 files from `fc-list`: 2925 sfnt files (2857 `.ttf`, 54 `.otf`, 14 `.ttc`)
holding 2991 faces, and 128 Type 1 files refused unread. 17.9 MB in 87 files;
the task's target was 15 MB. `BOXI` (6 MB) and `K` (5.7 MB) are two thirds of
it, and the sample sizes the task named (64 boxes, 256 pairs) were kept in
preference to the target. About 11 minutes on 16 cores (140 CPU-minutes) with
other work running on the machine; no face came near the 900 s limit. Three
runs: the second and third were compared and are byte-identical.

## What a from-scratch reader would get differently

Measured on this corpus; the counts are in `index.golden`.

- **fontTools drops every codepoint mapped to glyph 0** (`_c_m_a_p._make_map`),
  in every cmap format, so the reference's `has()` is false for it and `CMAP`'s
  count excludes it. `FORMAT-font.md` says the reference "would say yes"; no
  bundled file tests it and that sentence is wrong. `CMAPBYTES` counts them.
  19 faces here map 296 codepoints other than U+FFFF to glyph 0 (Gargi.ttf
  is one).
- **Format 4** as fontTools reads it: the last segment is skipped whatever it
  holds; a later segment overrides an earlier one; an `idRangeOffset` index
  past the glyph index array is an `AssertionError`, which is the reference
  refusing the font. **Format 12**: a group is clamped to U+10FFFF and a group
  that is inverted or starts before the previous one ends is skipped. None of
  that shows on this corpus: for all 2990 faces the reference read, the picked
  subtable read by the specification alone, with glyph 0 discarded, **is** the
  reference's mapping (`CMAPBYTES`' last three fields are zero everywhere). A
  malformed subtable is therefore untested by these goldens.
- **A prose font with a `MATH` table gets minted codepoints.** `_face_data`
  calls `_encode_variants` for every role, so `has(0xFFFFD)` is true for
  DejaVu Sans registered as any role (36 minted). Six faces have a `MATH`
  table; five mint, and in none of the five is name order glyph-id order.
  All six are TrueType with a format 2 `post` table, so the names that decide
  the minting come from `post` here and from the CFF charset in the bundled
  math face.
- The `kern` feature's lookups are the union over every script and language
  (`KERNLOOKUPS`' second field counts the distinct lists), lookup flags are
  ignored (`IgnoreMarks` is the commonest flag in the corpus), only
  `Value1.XAdvance` is read whatever the ValueFormats say, and contextual
  lookups under `kern` (`KSHAPE` effective types 7 and 8) are skipped.
- Not present in this corpus, so untested by it: CFF2, variable fonts (`fvar`),
  WOFF, a font fontTools cannot open, a font with no Unicode cmap, a picked
  cmap format other than 4 and 12, components placed by point matching,
  `SCALED_COMPONENT_OFFSET`, a glyph whose drawing raises, a `pair_kern` that
  raises, an Apple-format `kern` table. The one face the reference refuses is
  NotoColorEmoji.ttf (`CBDT` bitmaps, no outlines: `TTLibError`).

## Companion goldens (task `font-wild`)

Two smaller sets sit beside this one. Each script's docstring documents its
records; both use the base format and the `h64` above.

- `tests/golden/font-wild-kern/kern.golden`, written by
  `tools/oracle/dump_kern_legacy.py`: the legacy `kern` table of each of the
  81 corpus faces that has one, as **fontTools** reads it (the reference does
  not read the table, so there is no reference answer), combined by the rule
  the C++ reader states. It takes its list of faces from the digests here, so
  regenerate it after them.
- `tests/golden/font-synth/synth.golden`, written by
  `tools/oracle/dump_font_synth.py`: hand-made `cmap` and `post` tables put
  into a bundled font, and fontTools' reading of each — the cmap formats and
  malformations and the glyph-naming rules that "What a from-scratch reader
  would get differently" above says this corpus cannot test. It does not
  depend on the machine's fonts.

Since those were written the C++ reader drops codepoints mapped to glyph 0,
reads collections by face index and mints TrueType math fonts by `post` name,
so the first, second and third points of that section describe what the port
now does as well as what the reference does.
