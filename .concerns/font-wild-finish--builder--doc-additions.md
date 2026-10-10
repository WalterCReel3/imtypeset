# Text for the shared documents, from `font-wild-finish` (builder), 2026-10-10

For `fold-docs`. One section per file. `docs/font-support.md` is this task's own
file and is already edited; nothing here repeats it. Every number below is from
a run this task made (logs were under `build/font-wild-finish-logs/`, removed
with the build directories; the result file
`.concerns/font-wild-finish--builder--result.md` lists the runs).

---

## docs/decisions.md — append one entry (next free number; written here as D-FW)

### D-FW. Font reader: five rulings from finishing `font-wild` (2026-10-10)

**Decided.**

1. **A cmap format 13 group stays one range, end to end.** `CmapRange` has a
   flag, `one_glyph`: every codepoint of the range maps to `glyph`, where
   without it glyph ids run on. The reader keeps a format 13 group as one such
   range; `FaceData` inverts it as one; `build_atlas_font` writes the atlas's
   cmap as format 13 when the face's ranges are of that kind and as format 12
   otherwise (both rasterisers read both). The alternative was a cap on the
   codepoints a format 13 table may map, which would refuse or truncate the one
   real font of this shape, the Unicode Last Resort font.
2. **A format 12 group's glyph ids do not wrap.** A group whose ids would pass
   2^32 - 1 maps the codepoints whose ids fit and no others, with a warning.
   `OpenTypeFont::lookup` therefore never answers glyph 0 and never an id the
   file did not name.
3. **A legacy `kern` sum is held to ±MAX_DIMEN where it becomes a length**, in
   `FontMetrics::kern` (`src/fontlib.cpp`), not in the reader: the reader keeps
   what the table states (`pair_kern` may answer up to 2^31 units), and a value
   inside the bound is returned exactly as computed, so no golden moves. The
   bound is applied there, and not left to each caller, because the math list
   rounds a kern with `round_to_scaled` alone (`src/math_mlist.cpp`), which
   bounds at `SCALED_LIMIT` and not at `MAX_DIMEN`.
4. **A Type 2 charstring whose arithmetic leaves a coordinate infinite or NaN
   draws no glyph.** The glyph's ink box is empty, with the warning every
   malformed glyph gets, and the strict pass refuses the font. fontTools
   computes in unbounded integers and would have a box; no font was found that
   does this, and the reference has no golden for one.
5. **A glyf header box that ends at or below the baseline over an outline that
   rises above it is repaired in the atlas's copy, not refused.**
   `OpenTypeFont::atlas_box_repairs()` lists such glyphs (the strict pass
   finds them) and `build_atlas_font` raises each header's yMax over the
   glyph's points. Refused only when no 16-bit yMax can state the bound, which
   takes a composite placed past 32,767 units.

**Measured** (for 5; the task's specification asked for a refusal and its
pre-written test asserted one):

- stb_truetype called directly (a scratch program over `imstb_truetype.h` with
  `STBTT_assert` counting): a Roboto glyph with its header rewritten to
  yMin -100, yMax 0 fails `z->ey >= scan_y_top` four times at every size from
  8 to 96 px; with yMax -50 at 8 to 24 px and not at 48 or 96; with yMax -200
  at 8 px only. That is the model the rule is written from: the assertion needs
  the bitmap to start at row 0 and an edge wholly above it.
- A refusal rule ("header yMax <= 0 and a point above the baseline") refused
  **5 of the 2,990** installed faces the atlas had taken until then:
  NanumBarunGothic.ttf and NanumBarunGothicBold.ttf (glyph 17929: header yMax
  -4, outline to 11; stb_truetype really does fail the assertion on it, 6
  times at 200 px and at 240 px, at no other size tried), Ponnala.ttf (glyphs
  697 and 698: header yMax 0, one control point at 2; no failure at any size
  tried), kalimati.ttf and samanata.ttf (glyph 174: a one-point contour 505
  units up, which has no edge; no failure).
- With the repair: 2,990 of 2,990 baked under FreeType (release, and
  ASan+UBSan), under stb_truetype (debug, ImGui's assertions live) and under
  FreeType with a 16-bit ImWchar; 3 faces and 4 glyphs have a header raised
  (the two Nanum faces and Ponnala). The Nanum glyph with yMax raised to 11:
  no assertion failure at any of 9 sizes, 8 to 1000 px, at oversample 1 and 2.

**Inferred, not read.** That FreeType does not use the glyf header's box for
what ImGui asks of it: the corpus baked the same under FreeType before and
after (277,868 inked glyphs, all visible with coverage). FreeType's source was
not read for it.

**Not measured.** The Nanum glyph baked through ImGui at 200 px (the corpus
test bakes at 16 px; the mechanism is tested at 16 and 200 px on hand-made
fonts). Any font whose composite bound is far larger than its outline: the
raised box is an outer bound there, and the bitmap is larger than it need be.

---

## docs/hardening.md

### 1. Replace the heading `### Fonts: \`tests/test_stress_font.cpp\` (16 tests)` with

    ### Fonts: `tests/test_stress_font.cpp` (22 tests)

### 2. Replace the table that begins `| build | random mutants per bundled font` and the paragraph after it (to `multiplies the random counts.`) with

| build | random mutants per bundled font (math face: half) | of which go to the atlas | mutants per synthetic seed (53 seeds, all to the atlas) |
|---|---|---|---|
| ASan + UBSan | 120 | 1 in 5 | 80 |
| debug | 450 | 1 in 5 | 600 |
| release | 3,000 | 1 in 5 | 3,000 |

The 53 seeds are 21 glyf, CFF and MATH fonts, 7 collections, 4 CID-keyed
fonts, 3 Type 2 charstring fonts, 4 with a `kern` table, 3 with `post` names
and 11 cmap shapes; a mutant of a collection goes through once per face.
Plus, per bundled font, the file itself and a cut at both ends of every table;
146 structured cases; and 188 mutants through
`ImTypeset::AddFontFromMemoryTTF`. `IMTYPESET_STRESS_ITERS=n` multiplies the
random counts.

### 3. Replace the census table (`| stage | bundled fonts (7) | synthetic seeds (21) |` and its rows) and the two paragraphs after it (`Wall time ...` and `**10x** ...`) with

Default run, census, ASan + UBSan with FreeType, 2026-10-10:

| stage | structured (146) | bundled fonts (7) | synthetic seeds (53) |
|---|---|---|---|
| mutants | 146 | 1,033 | 4,883 |
| refused by the reader | 34 | 335 | 1,840 |
| refused by the strict pass | 46 | 170 | 708 |
| refused by build_atlas_font | 0 | 0 | 0 |
| refused by ImGui | 0 | 0 | 3 |
| accepted | 66 | 528 (353 stopped before the atlas) | 2,332 |
| glyphs baked / visible | 7,630 / 852 | 11,349 / 10,606 | 110,007 / 11,796 |

The same executable in the other builds, default run, 0 failed checks in each:
release with FreeType 19,753 bundled-font mutants and 182,976 seed mutants;
debug with stb_truetype (ImGui's assertions live) 3,178 and 36,596, none
refused by ImGui; debug with FreeType and a 16-bit ImWchar 3,178 and 36,608.

**10x** (`IMTYPESET_STRESS_ITERS=10`), ASan + UBSan with FreeType: 8,053
bundled-font mutants and 48,800 seed mutants, 22 tests, 0 failed checks, 0
sanitiser reports; the two fuzz tests took 118.6 s and 139.8 s.
ASan + UBSan with stb_truetype: the same 8,053 and 48,800, 22 tests, 0 failed
checks, 0 sanitiser reports, none refused by ImGui; 134.9 s and 145.3 s.

### 4. Append to the "Font stack" table (after row S9)

| id | input, and what it broke (observed) | fix | test |
|---|---|---|---|
| N1 | A 628-byte font whose cmap is one format 13 group over U+0000..U+10FFFF: 1,114,112 ranges of 12 bytes, a **13,369,960-byte** font for the atlas, +70 MB peak RSS (release). | `CmapRange::one_glyph`: the group is one range in the reader, in `FaceData` and in the atlas's cmap, which is written as format 13. Now 1 range, a 628-byte font for the atlas, no measurable time or memory. | `n1_a_format_13_cmap_over_the_whole_code_space_stays_one_range` |
| W-D1 | cmap format 12 group `{first 0x41, last 0x44, glyph 0xFFFFFFFE}`: `lookup` added in 32 bits and answered glyphs 0xFFFFFFFE, 0xFFFFFFFF, **0, 1**; the atlas's cmap left the group out, so the atlas and layout disagreed about what the font has. 27 fonts of the dense fuzz had such a group (release run). | `read_cmap_subtable`: the group is cut at the last codepoint whose id fits, with a warning. | `cmap_format_12_glyph_ids_that_run_past_32_bits_never_answer_glyph_zero` (`tests/test_opentype_formats.cpp`); the fuzz asserts no range's ids wrap and no longer sets such fonts aside |
| W-D2 | A `kern` table of 65,535 one-pair subtables: load **0.663 s** against 0.033 s for 16,384 (19.8 times for 4 times the input; release). The cause was a `reserve()` to the exact size at each subtable, which reallocates and moves the whole vector every time, not the sort. | `read_legacy_kern`: no exact `reserve()`. Now 0.001 s release, 0.052 s ASan, 4.1 to 4.2 times for 4 times the input. | `n3_a_kern_table_of_65535_subtables_is_read_in_one_pass` |
| W-D3 | Type 2 charstring `32767 dup mul` seven times over, then `rmoveto`: ink box x **inf**; with `dup sub` after it, **NaN**, and the NaN passed `strict_check` because `std::max` drops a NaN operand and the reach test never saw it. | `CffState::finite`, cleared when the pen is sent to a point that is not finite: the glyph has no box, and `strict_cff` refuses the font ("a CFF charstring's arithmetic puts a point where no number is"). | `type2_arithmetic_that_overflows_gives_no_box_that_is_not_finite` (`tests/test_opentype_formats.cpp`) |
| W-D4 | A simple glyph whose glyf header says yMin -100, yMax 0 and whose outline is a square from 100 to 400: passed the strict pass; stb_truetype sizes the bitmap by the header and fails `STBTT_assert(z->ey >= scan_y_top)` in `stbtt__rasterize_sorted_edges` (4 failures at every size from 8 to 96 px, counted by calling stb_truetype directly). Installed fonts have the shape: NanumBarunGothic's glyph 17929 fails the assertion at 200 and 240 px. | Repaired, not refused (decision D-FW.5): `strict_glyf_glyph` notes the glyph in `OpenTypeFont::atlas_box_repairs()` and `build_atlas_font` raises the header's yMax over the outline in the atlas's copy. Refused only when no 16-bit yMax holds a composite's bound. | `a_glyph_drawn_above_a_header_box_at_the_baseline_does_not_reach_a_rasteriser`: six shapes baked at 16 and 200 px under whichever rasteriser the build has, and a seventh refused; the fuzz no longer keeps such glyphs from stb_truetype and checks each raised header in the rebuilt font |
| W-K | 16,384 `kern` subtables kerning one pair by 2,000 each: `pair_kern` answers 32,768,000 units, which at 12 pt in a 1000-unit em is a length 24 times `MAX_DIMEN`. The text path clamped it (`rounded_dimen`, `src/style.cpp`); the math path rounds with `round_to_scaled` alone. | `FontMetrics::kern` (`src/fontlib.cpp`) holds the length to ±`MAX_DIMEN`; a value inside the bound is unchanged. | in `n3_...`: both signs, and a pair with no kern still exactly 0 |

### 5. In the paragraph `**Installed fonts** (optional test ...`, append

Since `font-wild` the reader takes collections and CID-keyed CFF, and the
corpus loader test covers every installed face: `tests/test_font_loader_wild.cpp`
with `IMTYPESET_WILD_ALL=1`, 2,991 faces, 2,990 baked and none refused by the
atlas, `IsGlyphInFont` agreeing with `FaceData::has` on all 939,193 codepoints
asked, under FreeType (release, and ASan + UBSan), under stb_truetype (debug)
and under FreeType with a 16-bit ImWchar (921,962 asked).
The whole corpus was also baked under ASan + UBSan with stb_truetype: the same
counts, 0 failed checks, 0 sanitiser reports.

---

## docs/divergences.md — in `## font-wild`, append to the list "Beyond the reference, on purpose"

N. **A cmap format 12 group whose glyph ids pass 2^32 - 1 is cut** at the last
   codepoint whose id fits. fontTools computes the ids as unbounded integers
   and then fails to name the glyphs. No installed font has such a group.
N+1. **A Type 2 charstring that computes a coordinate which is infinite or not
   a number has no ink box.** fontTools works in unbounded integers where the
   operands are integers and has a box. No installed font uses the arithmetic
   operators at all.
N+2. **A legacy `kern` value is held to ±`MAX_DIMEN` as a length**
   (`FontMetrics::kern`). The reference never reads the table.

And, under the heading for what the atlas is given (or as a new item if there
is none): **the font the atlas is given is not the file.** Beyond the cmap
(D27) and the OS/2 `USE_TYPO_METRICS` bit, a glyf header whose box ends at or
below the baseline over an outline that rises above it has its yMax raised
(3 installed faces, 4 glyphs). Nothing layout measures reads that field.

---

## THIRD-PARTY.md

Nothing to add. Checked on 2026-10-10: the fontTools notice is in
`src/opentype.cpp` above `struct OpenTypeFont::BoundsPen` and in
`tools/oracle/check_font_bounds.py`; the paragraph beginning `**Open:** the
notice is not yet at the point of use.` is no longer in `THIRD-PARTY.md`. The
code this task added (the `one_glyph` range, the format 12 cut, the finite
check, the header repair, the kern bound) is written from the OpenType
specification and from `imstb_truetype.h`'s behaviour as observed by calling
it; none of it follows fontTools or copies stb_truetype.
