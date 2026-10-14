# patch-docs (builder): H25, H37, H38, H39, H41, H42 for 0.4.3

No behaviour change: comments, docs, docstrings and one new test file.

## Test written first: `tests/test_api_header_sentences.cpp` (new; not git-added, D4)

Five tests, worked by hand (no oracle):
- `each_newline_under_hard_breaks_is_a_line_and_a_copy_gives_one` (H41): "a\n\nb" = 3 lines, as
  tall as "a\nb\nc"; "a\n\n\nb" = 4; a whitespace-only line, "\r\n\r\n", "\r\r" blank too;
  newlines at the ends dropped; U+2028 and \v not newlines; copy of "a\n\nb" and "a\n\n\nb" =
  "a\nb"; same through a document block with ImTypesetBlockFlags_HardBreaks (LineCount 3, copy
  "a\nb"). The old header's KNOWN GAP and hardening.md were wrong for layout: CONFIRMED by run.
- `a_table_with_no_cells_has_no_rows_and_no_height` (H38): with and without TableHeader:
  SetDocumentBlocks/LayoutDocument true, error None; RowCount/ColumnCount/CellCount 0; height 0;
  LineCount 0; the next block's y and the document height equal those without the table;
  DrawDocumentToBuffers gives the same vertex count as without it.
- `a_scale_that_is_not_finite_is_one_and_the_rest_is_clamped_to_a_tenth_and_ten` (H37): run
  Scale 0, -3, NaN, +inf, -inf -> width at 1; 1e-20, 0.05 -> 0.1; 11, 3e38 -> 10; block style
  Scale the same. Code: `src/context.h` MIN_RUN_SCALE 0.1 / MAX_RUN_SCALE 10; the readers
  (`src/imtypeset.cpp` run validation and GetDeviceGlyphAdvance, `src/imtypeset_document.cpp`
  block font) take `Scale > 0` then `clamped(..., 1.0)`, so NEGATIVE is 1, not 0.1 (the triage
  sentence said only "non-finite is 1").
- `white_space_is_pythons_isspace_and_a_no_break_space_breaks` (H42): `py_isspace` over
  0..0x10FFFF is true of exactly 29, the listed ones; each of the 29 between words gives the
  natural width of "word word" and copies as one space, also in a run with a space; U+200B,
  U+2060, U+FEFF are not; U+00A0, U+202F, U+2007 break at a narrow width; a NoBreak run keeps
  "word word" on one line.
- `a_roles_font_removed_while_it_is_the_default_is_replaced` (H25), both routes, between
  frames, with a host font as Fonts[1]: io.FontDefault = the Regular role's font, a frame,
  then removal. Host's `ImFontAtlas::RemoveFont`: FontDefault -> host font, ImGui::GetFont() ->
  host font. `ImTypeset::RemoveFont`: FontDefault -> NULL (our code at
  `src/imtypeset.cpp` RemoveFont clears it before calling ImGui; null = Fonts[0] to ImGui),
  GetFont() -> host font. A following frame draws vertices in both.

## H25 reading of .deps/imgui (v1.92.9, IMGUI_VERSION_NUM 19290)

- `imgui_draw.cpp:3340-3363` `ImFontAtlas::RemoveFont`: asserts `!Locked`, destroys output and
  sources, `Fonts.find_erase`, then `new_current_font = Fonts.empty() ? NULL : Fonts[0]` and
  `ImFontAtlasBuildNotifySetFont(this, font, new_current_font)` (:3361-3362).
- `imgui_draw.cpp:3308-3338` `ImFontAtlasBuildNotifySetFont`: for every ImDrawListSharedData of
  the atlas (every context using it): shared Font, `ctx->IO.FontDefault` (:3320-3321),
  `ctx->Font` via SetCurrentFont with the context bound (:3322-3330), and every
  `ctx->FontStack` entry (:3332-3334) are replaced by the new font.
- Lock: `imgui.cpp:9092-9094` locks atlases in NewFrame ONLY when the backend lacks
  RendererHasTextures; `imgui.cpp:6174-6175` unlocks at EndFrame. So with RendererHasTextures
  1.92.9 does not refuse a removal inside a frame; "outside a frame" is our advice (our
  RemoveFont refuses only when Locked).
- Not in the triage: if the removed font is the LAST in the atlas, the replacement is NULL; the
  header says so.
- Context hooks (H39): `ImGuiContextHook` / `AddContextHook` are declared only in
  `imgui_internal.h:2232` and `:3409`; nothing in imgui.h.

## Sentences added

`include/imtypeset.h`:
- `ImTypesetTextFlags_HardBreaks` (H41): what a newline is ("\n", "\r\n", lone "\r"; U+2028 and
  other white space not); EACH NEWLINE ENDS A LINE, blank lines set, a whitespace-only line blank,
  newlines at the ends dropped; KNOWN GAP now: a copy gives ONE newline for a run.
- `AppendParagraphRangeText`: "a newline (one for a run of them: see the flag)".
- `ImTypesetRun::Scale`, `ImTypesetBlockStyle::Scale` (H37): "0 is 1, and so is a value below
  zero or not finite; the rest is clamped to 0.1..10." `GetDeviceGlyphAdvance`'s `scale`: "read
  as those are: 0 is 1".
- Tables section, after "A row may have fewer cells..." (H38): A TABLE WITH NO CELLS paragraph.
- `DestroyContext` (H25): removing the fonts is the host's, outside a frame: RemoveFont() per
  role before, or ImFontAtlas::RemoveFont() after on each role's ImFont asked of GetFont()
  before, each once (CodeBold answers Bold's); no call removes them all.
- `RemoveFont` (H25): A FONT IN USE paragraph (the reading above, null if last; our call also
  nulls io.FontDefault).
- `GetFont` (H39): THIS IS THE TEST that the ImGui context is gone or not current; also null when
  the font was removed or a new context sits at the old address; no hook, imgui_internal.h.
- `SetParagraphText` (H42): WHITE SPACE paragraph: the 29 by range, a run is one interword space
  and a breakpoint, no-break spaces (U+00A0, U+202F, U+2007) break and collapse (Y10), a NoBreak
  run keeps words together, U+200B/U+2060/U+FEFF are not white space.
`include/imtypeset_c.h`: `ImTypeset_GetFontAtlasIndex`: -1 for a role given a font is the test
the ImGui context is gone (H39).
`python/imtypeset/__init__.py`: docstrings only, `get_font` (H39) and `remove_font` (H25).
patch-python's changes untouched; patch-geometry's header sentences untouched.

H51 (optional): NOT added. THE GRID already says a tab goes to the next multiple of four cells
and a mark that has no ink takes no cell. But `walk_code` counts a column per character where
the face is not on the device grid (ImTypesetAdvances_Font), so "a mark takes no column" is not
true under every Advances; I did not test that and wrote nothing.

## docs/hardening.md ("Not verified" list), corrected passage

> - Two newlines in a row under `ImTypesetTextFlags_HardBreaks` are two line breaks in layout and
>   one newline in a copy. Each newline is a forced break (`hard_break` in `src/paragraph.cpp`), so
>   the blank line between them is SET and drawn: `"a\n\nb"` is three lines, as tall as
>   `"a\nb\nc"`. The copy is what drops it: the separators a copy inserts are read off
>   `ParagraphLayout::hard_breaks`, which keeps one entry per shipped item after a forced break, as
>   the reference's `_after_hard_breaks` does, so the two breaks share one newline. (This item said
>   until 0.4.3 that the blank line was neither drawn nor copied, and the header's KNOWN GAP said
>   the same; both were read, not run. Measured in 0.4.3 by
>   `tests/test_api_header_sentences.cpp`, `each_newline_under_hard_breaks_is_a_line_and_a_copy_gives_one`,
>   and the header corrected; imscribe's H41.) Whether the copy keeps every newline or the layout
>   drops the blank lines is the owner's to decide.

## CHANGELOG lines added (under `## 0.4.3 (in progress)`)

- **fixed (documentation)**: HardBreaks header sentence was wrong; each newline ends a line; the
  copy's one newline is the named known gap; hardening.md corrected (H41).
- documentation, each held by a test or a reading of ImGui 1.92.9: white space = str.isspace, 29,
  no-break spaces break and collapse (Y10), NoBreak run ties (H42); Scale below zero / not finite
  is 1, rest clamped 0.1..10 (H37); a table with no cells (H38); fonts stay in the atlas, removal
  is the host's outside a frame, a default/current font is safe to remove (H25); GetFont null /
  font_atlas_index None is the context-gone test, no hook (H39).

## Runs

- debug, private dir, all modules, all tests: 57/57 pass.
- ASan+UBSan, private dir: 57/57 pass; the new test run directly prints no UBSan report.
- clang-format --dry-run clean on the new test and both headers.
- python/imtypeset/__init__.py: parsed (ast) only; the Python suite NOT run (docstring change).
- release, stb, wchar16, presets, python-host: NOT run. Build dirs removed.

## Status-file rows (docs/advtext-swap-status.md; lead to paste)

| H25 | `destroy_context` and the atlas | **answered in 0.4.3's header** (read, and tested): the fonts a context added stay in Dear ImGui's atlas; removing them is the host's, outside a frame (`RemoveFont` per role before `DestroyContext`, or `ImFontAtlas::RemoveFont` after on each role's ImFont, each once). Removing a font that is `io.FontDefault`, current or pushed is safe in 1.92.9: `ImFontAtlas::RemoveFont` puts the atlas's first remaining font in its place in every context of the atlas (`imgui_draw.cpp:3340-3363`, `ImFontAtlasBuildNotifySetFont` :3308-3338), null if it was the last; `ImTypeset::RemoveFont` also nulls `io.FontDefault` (ImGui then uses the first font). `tests/test_api_header_sentences.cpp` shows both routes for the default and current font between frames. A remove-all call is declined | answered (0.4.3) |
| H37 | values taken silently | **answered in 0.4.3's header** (tested): a run's or a block style's `Scale` of 0, below zero, or not finite is 1; the rest is clamped to 0.1..10 (`MIN_RUN_SCALE`/`MAX_RUN_SCALE`, `src/context.h`). A colour is its low 32 bits (D70) | answered (0.4.3) |
| H38 | ragged tables | **answered in 0.4.3's header** (tested): a table has as many columns as its longest row, short rows padded empty; a table with no cells has no rows, no columns and no height, draws nothing, is not an error, and moves nothing around it (with no space asked above or below) | answered (0.4.3) |
| H39 | knowing the ImGui context is gone | **hook declined; test named in 0.4.3's header**: `GetFont(role)` null (Python `get_font` / `font_atlas_index` None) for a role that was given a font means the ImGui context is gone or not current (or the font was removed, or a new context sits at the old address). Context hooks are in `imgui_internal.h` (`ImGuiContextHook`, `AddContextHook`), which the library does not include. Held by existing tests (`test_api_answers.cpp`, `tests/python/test_binding.py`) | declined (hook); answered (test) |
| H41 | blank lines under HardBreaks | **confirmed by run; header and `docs/hardening.md` corrected in 0.4.3**: each newline ("\n", "\r\n", lone "\r") ends a line, so `"a\n\nb"` sets three lines; a copy gives one newline for a run of them, which the header now names as the known gap. Whether the copy keeps every newline (proposed) or the layout drops them is the owner's | needs the user (copy or layout) |
| H42 | which characters are white space | **answered in 0.4.3's header** (tested): Python's `str.isspace`, 29 characters (U+0009..000D, 001C..0020, 0085, 00A0, 1680, 2000..200A, 2028, 2029, 202F, 205F, 3000); a run is one interword glue and a breakpoint; a no-break space (U+00A0, U+202F, U+2007) breaks and collapses as in the reference (divergence Y10); a `NoBreak` run keeps words together. Proposed to the owner: a product rule making them true ties | needs the user (the tie) |
