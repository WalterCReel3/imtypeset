- start (resumed session, no prior progress file): claimed draw-seams, reading. Nothing changed yet.
- read: findings, word-gaps result, src/draw.{h,cpp}. DESIGN (not yet written): one leader per line under product():
  (lead_x, lead_pen) = last glyph of the word before, or the EDGE of the formula before; kept across set glue and across an ATOM
  boundary, dropped by LINE / foreign rule / unknown. Every start (word after space, text glued after a formula, formula edge after
  text or space) = lead_pen + snap(px(x - lead_x)) clamped to floor/ceil of its own place. Text after a formula is placed from the
  formula's EDGE (one rounding), not from its last glyph. Bound to assert: < 1.5 px at every space and joint, a formula taken at its
  box edge. F7: replace nth_element + _gaps by a 4-pass byte histogram selection on the float bits (linear worst case, no buffer).
- originals of my five files saved in build/draw-seams-orig/ (for the before/after bench and to diff); goldens checksum there too
- STEP 1 DONE (reproduced by running, unchanged src/draw.*; output kept in build/draw-seams-orig/baseline-run.txt until the result file is written):
  hand cases F1 ($x^2$-th a: pens 11,17,22,27,32,39 -> space 7 against 8.65, 1.65 off), F1b (bracket at 46 inside a bar ending 47; the REFERENCE does the same),
  F2 (bracket 40, box 44: 4 against 5.47; reference 5), F3 (space 2.45 drawn 0.48), F8 (hand-built only) all reproduce exactly as derived.
  Corpus (3 joint paragraphs x 3 widths x 16 sizes x 3 origins, FreeType): product text|formula box edge -1.83..+1.75 (143 of 2448 over 1 px; reference
  nearest glyph -1.25..+1.21, 28 over 1); text|space|formula -1.80..+1.77; spaces between words -1.31..+1.42 (F1 not hit by the corpus, only by hand).
  F6: the sub-case indeed never clamps; test now has a tie case (k) that does.
- tests written in tests/test_word_gaps.cpp (reader extended with formulas/joints, model `pens_under` extended, 6 new tests). NEXT: src/draw.cpp fix.
- FIX IN src/draw.cpp (leader: word end or formula's rightmost part; formula edge led+clamped; text after formula led from rightmost part + clamped; foreign rule ends formula; settle_word selection by byte histogram, _gaps gone). test_word_gaps green in debug (18 tests). NEXT: test_harden d7 expectations, test_draw goldens, draw.h comments, bench row, images, full suites.
- draw.h comments rewritten (the bound as asserted); bench rows added; snapshots build/draw-seams-snap/{A=old draw.*,B=new} + release dirs build/draw-seams-rel{A,B} configured. test_word_gaps/test_draw/test_harden green in debug on live tree. NEXT: build relA/relB, pictures (look), interleaved bench, then full suites in 5 configs.
- corpus fixed ($n$th is literal text by scan_math's rule, flagged for lead; corpus now uses $n$-th); pictures made in build/draw-seams-shots (looked at 3 crops: h(u) no longer squeezed; 13 px formula-dense line still uneven within the bound). debug test_word_gaps 19 tests green. NEXT: rebuild relA/relB, before-numbers from relA, interleaved bench, full suites.
- suites started 13:02 by build/draw-seams-orig/suite.sh (debug, release, stb, wchar16, asan; unnarrowed, consumer tests OFF); summary in build/draw-seams-orig/suite/summary.txt. Bench done once (8x interleaved, load avg 8-9, noise +-6%): build/draw-seams-orig/bench/summary*.txt. md5 of the five files at suite start in build/draw-seams-orig/final.md5.
- DONE: five configurations 41/41 each; result + doc-additions written; build/draw-seams* removed.
