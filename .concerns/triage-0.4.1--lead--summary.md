# Triage of imscribe's open requests against v0.4.1 (lead's digest of the reviewer's seven posts, 2026-10-14)

Everything READ from the code by a reviewer; nothing run. Release plan (lead): 0.4.2 = H52 + test-only
(issue #4); 0.4.3 = the patch-sized fixes below; 0.5.0 = the API additions and whatever the owner rules.

PATCH (0.4.3)
- H27 caret/hit at a code line's end. Cause: a code line's newline has no layout item, so a line's end
  and the next line's start share one index (blocks.cpp walk_code 333-376 / code_boxes 641-749;
  imtypeset_document.cpp map_code_indices 1182-1212, index_of 1522-1534, GetDocumentCaretRect
  3619-3628, hit test 2723-2740; imtypeset.cpp line_of_index 1230-1238). WIDER (inferred): a blank code
  line can hold no caret and a click on it answers the next line. Fix: record each source line's
  terminator byte in DocBlock; caret at a terminator answers x1 of that source line's last visual line;
  a hit past a line's last character answers its terminator byte. Do NOT build "use the line before".
  Under Clip, x1 is the room, not the text's end (exact packing, blocks.cpp:673-677): use the last glyph's
  x + advance. Tests: "abcdef\nxy", "ab\n\ty" -> cells 0,1,2,0,4,5, "a\n\nb", CRLF, hits, range_rects
  of [0,7), copy unchanged.
- H49 device_glyph_advance residue: DeviceMetrics::scaled rounds px->sp UP (device.cpp:195, deliberate
  in layout); GetDeviceGlyphAdvance (imtypeset.cpp:2780-2784) converts back without undoing it (8 px ->
  394691 sp -> 8.000008583). Fix: round the answer to whole framebuffer pixels (units.h helpers) / density;
  equal to ImFontBaked::GetCharAdvance; reword imtypeset.h:1716-1720; caret/range keep the < 1 sp residue
  and the header says so. Test: heights 12-24, densities 1, 1.5, 2.
- H28 `edit_error` as a class attribute (python/imtypeset/__init__.py:1887; _NO_ERROR at :973).
- H36 error messages name the BLOCK (and field) for TypeError / UnicodeEncodeError in Document._pack's
  checked path; correct docs/python-errors.md:55 (its recipe yields '?', not U+FFFD — inferred).
- H45 reproducible library bytes: -ffile-prefix-map for $out, $inc, $repo in
  python/host/build_bundle_library.sh:240-251 (cause inferred); CI-only rebuild-and-cmp test.
- DOC sentences (one task, include/imtypeset.h + docs): H25 (fonts stay in the atlas; removing a default
  or pushed font is safe in 1.92.9 — imgui_draw.cpp:3308-3363; call outside a frame); H37 (Scale: non-
  finite is 1, clamped to 0.1..10); H38 (a table with no cells has no rows and no height, + a test);
  H39 (decline the hook — needs imgui_internal.h; GetFont null / font_atlas_index None is the test);
  H41a (header imtypeset.h:421-424 is false for layout: an empty line per extra newline is SET, the copy
  gives one newline; correct docs/hardening.md:442-446 too); H42a (list the white-space set; a no-break
  space breaks and collapses: divergence Y10); H46 (environment read at import only — confirm
  _bundle.py:621 and :1382 are import-time — docs/sidecar.md + a test); H51 optional sentence (a column
  is a cell; a mark takes none).
- ALREADY ANSWERED, reply rows only: H26 (status file H3 row, T21), H47 (LayoutDocumentWindow recipe:
  lay out a band around the scroll position before drawing, move the scroll by the anchor block's top
  delta; a new query declined).

MINOR (0.5.0) — new names (C++, C, _capi.py, __init__.py; one builder for the headers at a time)
- H22 counts of borrowed / replaced characters: GetDocumentBlockFallbacks(doc, block(-1 = all),
  int* borrowed, int* replaced); counted at material build (style.cpp:128-138, blocks.cpp:411-419, cells).
- H43 + H44 font metrics without an ImGui context and exact: file queries over the context-free
  FontLibrary (GetFontFileMetrics / GetFontFileGlyphAdvance; measure the per-call parse cost first) and
  raw units (ImTypesetFontUnits {UnitsPerEm, Ascender, Descender, LineGap, XHeight(0 = none),
  SpaceAdvance}; GetFontUnits; GetGlyphAdvanceUnits).
- H24 row rules (flag TableRowRules) — needs the owner's answer on vertical space.
- H40 KeepSpaces (pre-wrap) — needs the owner's semantics.

OWNER QUESTIONS (put to the user 2026-10-14): H50, H23, H24, H40, H41b/c, H42 tie, H22's two counting
details.
