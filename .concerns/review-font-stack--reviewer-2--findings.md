[from reviewer-2] Review of the font stack (filed and condensed by lead). All READ, nothing run, unless
marked INFERRED. No out-of-bounds read was found in src/opentype.cpp itself. D27 was attacked and holds.
Owners: `stress` takes S1-S9; `font-wild` takes W1-W14; `public-api` takes P1-P3; `docs` takes L1.

== stress (hostile input; reproduce first, fix, keep the repro as a test) ==
S1. GPOS amplification (read_gpos). One kern lookup with 1,024 subtable offsets all pointing at the
    SAME PairPos subtable is parsed 1,024 times; MAX_KERN_SUBTABLES is the only bound. Format 2 keeps
    a matrix of ~GPOS size per copy; format 1 up to one pair per GPOS byte per copy (65,535 PairSet
    offsets may share one PairSet). Estimated 4-6 GB from a 1-4 MB GPOS; allocation failure aborts
    under -fno-exceptions. Fix: one global GPOS budget (bytes kept, pairs read) and/or dedupe offsets.
S2. stb hand-off. The reader accepts a font with a broken glyph ("empty box", load succeeds) and
    add_face_to_atlas gives it to stb_truetype, which does no range checking. Shape: loca[g] =
    0x7FFFFFF0, loca[g+1] = 0x7FFFFFF8, cmap maps 'A' to g -> wild read in the host on first draw.
    LEAD RULING: add_face_to_atlas refuses a face unless a STRICT pass validates everything a
    rasteriser will follow (every loca pair against glyf, every composite, every CFF index/charstring
    offset); "a defect confined to one glyph does not fail the load" stays true for METRICS-only use
    (a face registered but never given to the atlas) and stops being true for atlas use. No test
    currently puts a damaged font through ImGui: add mutants run through add_face_to_atlas + a baked
    glyph request under ASan in BOTH the stb and FreeType presets.
S3. build_atlas_font amplification. A directory of ~11,500 records each covering the whole file turns
    185 KB into ~2 GiB, passes the INT_MAX test, and is allocated twice. Cap the rebuilt size at file
    size + new cmap + padding, or refuse overlapping tables. Duplicate tags are kept and std::sort by
    tag is unstable, so atlas and layout can read different copies: refuse duplicate tags.
S4. (INFERRED, 32-bit) read_gpos format 2: `cells * record` wraps on wasm32 -> 2 GiB resize -> abort.
    Compute in uint64_t.
S5. (INFERRED) CFF budget counts operators, not curves: ~16M curve evaluations per hostile glyph.
S6. MAX_KERN_LOOKUPS is applied before sort/unique and without a warning.
S7. Stand-in exhaustion is silent and serves minted codepoints LAST: assign minted first, record a
    warning with the count unserved. The collision/capacity paths of build_stand_ins have never run
    under a test (Latin Modern maps nothing in the PUA): test with a face that fills the PUA.
S8. Tests that do not establish what is claimed: test_font_loader
    glyph_boxes_against_the_reference_rasteriser_are_reported PRINTS `differing`, it does not assert —
    gate `differing == 0` when the loader is FreeType; the 16-bit build disables the box-count floor.
    Robustness tests can only fail under ASan and their mutants stop at OpenTypeFont (never reach
    FaceData::init, build_atlas_font or ImGui). every_bundled_font_matches_its_raw_golden requires
    names.size() >= 6 with seven fonts. The add_face_to_atlas failure path is unexecuted.
S9. FontLibrary::metrics caches one object per exact size and never evicts; a host animating zoom
    grows it per frame. Bound it (LRU or quantise) and say so.

== public-api ==
P1. add_face_to_atlas passes the host's ImFontConfig through. LEAD RULING: force FontNo = 0 (the
    rebuilt file is single-face); REFUSE MergeMode with a reason; document that nothing may be merged
    into a font this library returns in a 16-bit ImWchar build (stand-ins start at U+E000, which is
    where Material Icons / Font Awesome / Codicons live — a merged icon font and our stand-ins shadow
    each other silently). Record next to D15/D27.
P2. ATLAS_WCHAR_BITS is fixed by OUR compilation's imgui.h. A host linking against an ImGui built with
    the other IMGUI_USE_WCHAR32 setting gets a silent mismatch. Add a check in the public init in the
    style of IMGUI_CHECKVERSION (host passes sizeof(ImWchar) through a macro) and refuse on mismatch.
P3. One add per face, not per (face, size): each add copies the font's bytes into the atlas.

== font-wild (real fonts; each is where it happens in src/opentype.cpp) ==
W1.  TrueType collections refused in read_directory. When added, build_atlas_font must take the
     directory from OpenTypeFont (it re-parses from file offset 0 and would read the TTC version as
     the table count) and emit a single-face sfnt.
W2.  CID-keyed CFF refused in read_cff (every Source Han / Noto CJK .otf).
W3.  read_cmap fails outright when the PREFERRED record's format is not 4/6/12 instead of trying the
     next; (3,0)-symbol-only and (1,0)-only fonts refused.
W4.  loca with no final sentinel refused; numberOfHMetrics == 0 refused. FreeType loads both.
W5.  WOFF/WOFF2/Type 1 get a generic reason; name them.
W6.  TrueType math fonts minted in glyph-id order (needs post format 2 names). Most math fonts a host
     would bring other than Latin Modern are TrueType.
W7.  read_classdef format 2 lets a later class-0 range overwrite; fontTools skips class-0 ranges.
W8.  A ClassDef/coverage/construction offset of 0 is read as a table at the parent's header.
W9.  cmap format 12 overlapping groups: port keeps the first; fontTools lets the later win.
W10. Type 2 `div` (12 12) is implemented by fontTools and refused here -> empty ink box on a valid font.
W11. A point-matched component is skipped and the rest drawn; fontTools raises. Say so in
     docs/font-support.md.
W12. (INFERRED) glyph 0 not named .notdef: reference answers advance 0, port glyph 0's metrics.
W13. CFF predefined Expert charsets: names not read.
W14. Evidence that would settle D27 beyond reading: the corpus loader run under BOTH presets asserting
     IsGlyphInFont agrees with FaceData::has for a codepoint sample.

== docs ==
L1. Licensing. BoundsPen::solve_quadratic, curve_to and quad_to are fontTools misc/bezierTools.py
    (solveQuadratic, calcCubicParameters + calcCubicBounds, calcQuadraticBounds) expression for
    expression; hflex1/flex1 follow psCharStrings.py; the cmap format-4 decode follows _c_m_a_p.py.
    Compared against the installed text. The reason is legitimate (bit-equal doubles need the same
    expression order). fontTools is MIT. LEAD RULING: keep it; put the fontTools MIT notice at the
    point of use and an entry in THIRD-PARTY.md naming these routines. Nothing mirrors stb or
    FreeType; font_loader.cpp contains no code adapted from ImGui.
