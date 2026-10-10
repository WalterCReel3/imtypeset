[from builder-5] font-wild-census done (filed and condensed by lead). tests/test_opentype_wild.cpp,
docs/font-support.md. The reader was not changed.

Whole corpus (IMTYPESET_WILD_ALL=1; release, debug, ASan+UBSan — release and asan output identical):
  in the corpus 2,991 | skipped 0 | read with every compared value equal 2,882 | refused with a
  reason 85 | mismatched 24.
Compared on loaded faces: 3,009,179 ink boxes bit for bit, all advances, the whole cmap, 423,272
sampled kern pairs, MATH values. No crash, no sanitiser report.
Refused: 80 collection faces (all Noto CJK .ttc, which are ALSO CID-keyed CFF — collections alone
unlock none of them); 4 CID-keyed CFF (unifont); 1 no outlines (NotoColorEmoji; reference refuses too).
Mismatch A, 19 faces: codepoints mapped to glyph 0 are kept (Gargi.ttf: 261 entries vs 251).
Mismatch B, 5 faces: TrueType MATH fonts minted in glyph-id order, not name order
(DejaVuMathTeXGyre.ttf glyph 3473: U+FFFD8 vs U+FFFBC).

test_opentype_wild FAILS in ctest BY DESIGN until task `font-wild` fixes A and B (8 of the 24 fall in
the default sample: every 8th face + MATH faces, 379 faces; 1.6 s release, 30 s asan). Whole corpus:
6.4 s release, 2 min asan.

Not verified: kern subtable contents and the MATH italic table are sampled only; the skip paths
(file absent/changed) were exercised by hand once, not in ctest.
Open: docs/divergences.md has no kerning entry; the measurable divergence from OpenType is
first-value-wins across kern lookups (590 faces have more than one) — how many pairs differ is not
measured. `font-wild` records it.
