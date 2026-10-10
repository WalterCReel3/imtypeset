[from builder-5] oracle-font-wild done (filed and condensed by lead). tests/golden/font-wild: 86
digest files + index.golden, 17.9 MB; three runs, last two byte-identical; ~10 min on 16 cores.

CENSUS (CENSUS records in tests/golden/font-wild/index.golden are authoritative):
- 3,053 fc-list files = 2,925 sfnt files (2,857 ttf, 54 otf, 14 ttc) -> 2,991 faces (80 inside the
  14 collections); 128 Type 1 refused unread.
- Flavour: TrueType 2,856; CFF 134 (84 CID-keyed, 50 name-keyed); no outlines 1 (NotoColorEmoji).
  CFF2 0, variable 0, WOFF 0.
- fontTools could not open: 0. fontlib refuses the 80 collection faces when called stock (it passes
  no face index); the digest ALSO records them with the face index injected (`FONTLIB indexed`,
  80/80 ok). LEAD RULING: the port reads collections by face index and is compared with the indexed
  records.
- cmap picked: (3,1) format 4: 2,443; (3,10) format 12: 546; (0,3) format 4: 2. Formats 0, 6, 10, 13
  are never PICKED (10 and 13 never occur), so the corpus cannot check those paths.
- Kern subtables read: PairPos1 1,528 direct + 133 Extension; PairPos2 1,667 direct + 54 Extension.
  ValueFormats beyond 0x0004/0: vf1=0x0005 (173 subtables), vf1=0 with non-zero vf2 (18), both (2).
- Kerning: GPOS 1,683 faces (60 also have legacy kern); none 1,307, of which 21 have ONLY a legacy
  kern table (the reference answers 0 for every pair). All 81 legacy tables are version 0 format 0.
- 590 faces have more than one kern lookup (reference: first subtable across lookups; OpenType
  accumulates). Lookup flags mostly non-zero and ignored. 4,852 non-PairPos subtables under `kern`.
- glyf: composites in 2,570 faces, depth to 5; scaled components common; point-matched 0. hmtx short:
  1,080. OS/2 version 1 (no sxHeight): 199; x_height fallback used: 237. unitsPerEm 64..2,212.
- CFF: every Type 2 operator family occurs incl. callgsubr and the curve-line mixes; flex did not
  appear; non-integer operands in 88 faces; non-default FontMatrix in 15; FDArray in 84.
- MATH: 6 faces, all TrueType with post format 2; DejaVuMathTeXGyre mints 262. In none is name order
  == glyph-id order.

FINDINGS:
1. fontTools drops every cmap mapping to glyph 0, in every format, so has() is false there. 19 corpus
   faces map 296 such codepoints (e.g. /usr/share/fonts/truetype/Gargi/Gargi.ttf). The reader must
   drop them. tools/oracle/FORMAT-font.md says the opposite and needs that sentence corrected.
2. A non-math role with a MATH table still gets minted codepoints (DejaVuSans as prose has 0xFFFFD).
   Minting for TrueType needs post format 2 names; 5 real fonts exercise it.
3. For all 2,990 faces the picked cmap subtable read by the spec alone (minus gid 0) equals
   fontTools' mapping. Malformed cmaps are therefore untested by this corpus.
Least sure: the hash is the builder's own word-wise FNV-1a 64 (FORMAT-font-wild.md), not checked
against a second implementation; goldens are machine-specific (absolute paths).
