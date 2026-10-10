# docs-hygiene — result (builder-20), 2026-10-09

## Exists now

- `THIRD-PARTY.md` (new, repo root). Nine entries: Dear ImGui, stb, fontTools,
  FreeType, Unicode data, `tex.web`, test fonts, SDL2, the Python reference.
  The ImGui MIT text, the fontTools MIT text, `tex.web`'s first ten lines and
  the Unicode notice block are embedded by a script from the files named
  beside them and re-compared byte for byte afterwards (4 of 4 equal).
- `tests/data/fonts/LICENSE-OFL-1.1.txt` (new): lines 36-121 of fontTools
  4.66.0's `dist-info/licenses/LICENSE.external`, `cmp`-identical to that run.
- `tests/data/fonts/LICENSES.md`: provenance table for the two texts present;
  "Not done" now names only GUST and says where it was looked for.
- `.concerns/docs-hygiene--builder-20--notice-for-opentype.md`: the comment
  block for `src/opentype.cpp`, its location by symbol, two one-line pointers,
  and what to delete from `THIRD-PARTY.md` once applied. For `font-wild`.
- `tools/oracle/FORMAT-math.md`, `tools/oracle/FORMAT-font.md`,
  `cmake/imgui.cmake` (comment only): corrected, below.

## Still missing, and why

1. **GUST Font License text.** Not on this machine: no lmodern / tex-gyre / TeX
   package under `/usr/share/doc`, no `copyright` file there names it,
   `/usr/share/texmf*`, `/usr/share/texlive`, `/usr/share/licenses` do not
   exist, imgui-bundle's `assets/fonts/latex` has the font and no licence.
   Not written from a fetch. `LICENSE-GUST.txt` does not exist.
2. **The Unicode notice as published with 14.0.0.** Not on this machine
   (Python's copyright file does not carry it). `THIRD-PARTY.md` holds instead
   lines 664-710 of `/usr/share/doc/libgtk-4-bin/copyright` unaltered — the
   same licence family (Unicode-DFS-2016) in Debian's layout with that
   package's year range (1991-2022) — and says so, with the instruction to
   replace it from unicode.org/license.txt before release.
3. **The fontTools notice at the point of use.** `src/` is `stress`'s;
   handed over as a file. `THIRD-PARTY.md` says "Open" until it lands.
4. **A standalone OFL 1.1 file** was not found either (only 1.0, and DEP-5
   renderings). I decided an unaltered run of lines from a file that embeds
   the licence is a verbatim copy and took it; reason and check recorded in
   `LICENSES.md`. It has not been compared with SIL's published copy.

## Corrected, and how each was checked

- `FORMAT-math.md` §3: 3,765 -> 3,776 sources; 2,576 -> 2,589 lists; added the
  per-file and total counts (8,555) and the `boundary/` cases; §5: added
  `braced` (11). Checked by running `dump_math.py --verify` (8555 cases, 10728
  checks ok) and by counting `case` records per kind and per name prefix. The
  brief's numbers were right. 2,037 for `formulas.golden` was already right.
- `FORMAT-font.md` "Which cmap subtable": the sentence saying the reference
  would answer yes for a codepoint mapped to glyph 0 was wrong. Checked on
  `/usr/share/fonts/truetype/Gargi/Gargi.ttf` by reading the (3,1) format 4
  subtable from the bytes (10 codepoints map to glyph 0) and asking fontTools'
  `getBestCmap()`, which `fontlib.py` builds its map from: 0 of the 10 present.
  Agrees with `docs/font-support.md` (261 entries vs 251).
- `cmake/imgui.cmake`: the comment justified the exact pin by `ImFontLoader`.
  Checked: no `#include` of `imgui_internal.h` under `src`, `include`, `tests`,
  `examples`, `bench`; `include/imtypeset.h` has `IMTYPESET_IMGUI_VERSION_NUM_MIN
  19200`. Comment rewritten; no command changed. Not re-configured (comment
  lines only, and `src/` is mid-rewrite).
- fontTools derivation (review L1): all five routine groups re-compared with
  the installed fontTools source; all hold. Copyright line taken from the
  dist-info `LICENSE` ("Copyright (c) 2017 Just van Rossum").
- stb: no `#include` of `imstb_*`/`stb_*` in our sources; FreeType: no
  `ft2build`/`FT_` use in our sources (one comment).
- `src/selection.cpp` no longer embeds Unicode tables (includes
  `unicode_tables.h`).
- SDL2: no file in the tree uses it yet; the entry says that and that
  `shot-rig`/`demo` must correct it.

## Noticed, not mine

- `docs/decisions.md` D24 "State when this was written" is now out of date
  (THIRD-PARTY.md exists; OFL text is in). Not in my touches.
- `tools/oracle/check_font_bounds.py` holds the same fontTools arithmetic with
  no notice. Its header says it was written from FORMAT-font.md's description,
  which transcribes fontTools. Listed in THIRD-PARTY.md; needs the notice.
- `tools/oracle/dump_selection.py` still has `emit_unicode_tables()` for a
  block of `src/selection.cpp` that no longer exists.
- `CFF_STANDARD_STRINGS` in `src/opentype.cpp`: the comment says CFF spec
  Appendix A; `check_font_bounds.py` borrows fontTools' copy of the same table
  to check names. How the C++ list was produced is not recorded. A list of
  391 names from a specification, so low risk, but stated as unknown.
- No licence for the project or the reference: stated plainly in
  THIRD-PARTY.md as the owner's decision.
