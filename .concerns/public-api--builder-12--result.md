# public-api — result

Files: `include/imtypeset.h` (409 lines, 28 functions), `src/imtypeset.cpp`,
`src/context.h` (new, internal), `tests/test_api.cpp` (19 tests),
`tests/test_header_pod.cpp`. Appended: `docs/divergences.md` (`## public-api`),
`docs/decisions.md` (D30). Nothing else touched.

## The public surface, one line per declaration

Macros: `IMTYPESET_VERSION "0.1.0"`, `IMTYPESET_VERSION_NUM 100`,
`IMTYPESET_IMGUI_VERSION_NUM 19290` (built and tested against),
`IMTYPESET_IMGUI_VERSION_NUM_MIN 19200` (`#error` below it, unless
`IMTYPESET_DISABLE_IMGUI_VERSION_CHECK`), `IMTYPESET_API`.

Opaque: `ImTypesetContext`, `ImTypesetParagraph`.

Enums (each an `int` typedef plus `Xxx_`): `ImTypesetFace_` (Default=-1,
Regular, Bold, Italic, BoldItalic, Code, CodeBold, Math, COUNT — values are
`detail::Face`'s), `ImTypesetAlign_` (Justify, Left, Center, Right),
`ImTypesetSpacing_` (Sentence = the reference theme's EXTRA_SPACE_ONLY,
French, TeX = NONFRENCH), `ImTypesetGranularity_` (Character, Word, Line),
`ImTypesetError_` (None, NoContext, VersionMismatch, InvalidArgument,
FileNotFound, FontRefused, NoFont, TextTooLong, MathRefused, LayoutFailed,
TooManyVertices), `ImTypesetTextFlags_` (HardBreaks, NoTextCompare, Math —
reserved), `ImTypesetRunFlags_` (NoBreak), `ImTypesetLayoutFlags_` (FirstFit,
TotalFit).

Structs (plain data; constructors only on the first two):
- `ImTypesetStyle` — Face, FontSize (ImGui pixel height; 0 = `GetFontSize()`),
  EmSize (>0 overrides), LineHeight (ems, 1.35), Align, LeftIndent,
  FirstLineIndent, Spacing, LayoutFlags, PixelsPerInch (96), Color (0 =
  `ImGuiCol_Text`), SelectionColor (0 = `ImGuiCol_TextSelectedBg`).
- `ImTypesetRun` — Start, End (bytes), Face, Scale, Color, Flags.
- `ImTypesetSelection` — Start, End: indices into ONE paragraph's shipped
  sequence. The header says at length that an index is not a text offset.
- `ImTypesetErrorInfo` — Code, BytePos, CodepointPos, Message.
- `ImTypesetStats` — TextBuilds, Layouts, DrawCalls, CulledCalls,
  VerticesEmitted, BytesCompared, CachedParagraphs.

Functions, `namespace ImTypeset`:
- `CreateContext(version, sizeof_imwchar, sizeof_imdrawvert, sizeof_imdrawidx, sizeof_style)` — all defaulted, evaluated in the caller's TU; null + reason on any mismatch or no ImGui context.
- `DestroyContext(ctx = NULL)`, `GetCurrentContext()`, `SetCurrentContext(ctx)`.
- `GetVersion()`, `GetImGuiVersionNum()`.
- `GetLastError()` → `const ImTypesetErrorInfo*`; `GetStats()` → `const ImTypesetStats*`; `GetStyle()` → `ImTypesetStyle&`.
- `AddFontFromFileTTF(face, filename, face_index = 0, font_cfg = NULL)` → `ImFont*`.
- `AddFontFromMemoryTTF(face, data, size, face_index = 0, font_cfg = NULL)` → `ImFont*` (bytes copied).
- `GetFont(face)`.
- `Text(text, text_end = NULL, flags = 0)`; `TextJustified(text, text_end = NULL, flags = 0)`.
- `CreateParagraph()`, `DestroyParagraph(p)`.
- `SetParagraphText(p, text, text_end, runs, runs_count, flags)` → bool.
- `GetParagraphError(p)`.
- `LayoutParagraph(p, width, style = NULL)` → `ImVec2`; cached.
- `GetParagraphSize(p)`, `GetParagraphLineCount(p)`.
- `DrawParagraph(p, draw_list, pos, selection = NULL)`.
- `DrawParagraphToBuffers(p, pos, clip_rect, ImVector<ImDrawVert>*, ImVector<ImDrawIdx>*, selection = NULL)` → bool.
- `GetParagraphLength(p)`, `HitTestParagraph(p, local_pos)`, `SelectParagraphRange(p, anchor, active, granularity)`.
- `GetParagraphSelectionRects(p, selection, ImVector<ImVec4>*)` → count.
- `AppendParagraphSelectionText(p, selection, ImGuiTextBuffer*)` — puts `"\n\n"` between paragraphs.
- `GetParagraphTextOffset(p, index)`, `GetParagraphIndexFromTextOffset(p, byte_offset)`.

## What was run

Private dirs, all modules, tests `test_api;test_header_pod;smoke`, zero
warnings under `-Werror` in each:

| dir | config | result |
|---|---|---|
| `build/public-api` | debug, FreeType, 32-bit ImWchar | 3/3; test_api 19 tests, 6,051 checks |
| `build/public-api-release` | Release | 3/3 |
| `build/public-api-asan` | ASan+UBSan | 3/3; 0 "runtime error" lines in the output |
| `build/public-api-stb` | `-DIMTYPESET_USE_FREETYPE=OFF` | 3/3 |
| `build/public-api-wchar16` | `-DIMTYPESET_IMGUI_WCHAR32=OFF` | 3/3 |

What the tests hold:
- **Against the reference**: 225 of 243 `para` goldens rebuilt through
  `SetParagraphText`/`LayoutParagraph`: 667 compositions, 85,962 glyphs,
  shipped commands equal field for field. Then the reference's commands (read
  from the golden) drawn by `detail::Drawer` vs `DrawParagraphToBuffers`:
  343,536 vertices byte-equal, indices equal. The 18 not replayed, by reason:
  6 first-line indent (deliberate divergence), 7 list marker (no API), 2
  non-default stand-in faces, 3 white space the API collapses on purpose.
  There is no golden for text → vertices in one step; this is the chain.
- **Performance contract** (counters + replaced `operator new` + ImGui's
  allocator): steady-state `TextJustified`: 0 text builds, 0 layouts, 0
  allocations; width change = 1 layout, 0 text builds; clipped child:
  10 of 12 paragraphs emit 0 vertices, 0 draw calls, 0 bytes read
  (`NoTextCompare`), 0 allocations; a 100 px window on a >100-line paragraph
  emits and *places* under 9/lines of the glyphs (line index).
- **Pixels** (`soft_raster`): no ink outside the item rectangle except the one
  pixel column after its right edge (10 of 19,143 inked pixels under FreeType,
  12 of 20,054 under stb); every line starts within 3 px of the left margin
  and every line but the last ends within 3 px of the right.
- Selection/copy/offsets, hostile lengths and runs (NaN, inf, ±FLT_MAX…),
  fonts (once per face, MergeMode, face index, junk, truncated, non-math as
  math, registry unchanged on refusal), the math seam with a stand-in formula
  layer, fallback without fonts, cache eviction, 16-bit index overflow.
- 23 single-line mutations of `src/imtypeset.cpp`: 18 fail a test; 4 did not
  compile (so say nothing); 1 survived and its code was removed (a second
  clamp on character measures that `src/style` already applies). One of the
  18 survived at first because its test was wrong (`A.` is not a sentence
  end: an upper-case letter before the period keeps the space ordinary).

## Not verified

- Any ImGui other than 1.92.9. The 19200 floor is from the changelog, not a build.
- clang, MSVC: only GCC here. `test_header_pod.cpp` uses `__is_pod` etc.
- litgen has not been run on the header.
- The preset builds (`build/debug` …) and the rest of the suite were not run.
- Tables, columns, `SameLine` baseline alignment: `Text()` was run in a
  window and a child window only. "Works in tables" in the header is by
  construction (Dummy + IsRectVisible), not by test.
- `FontRasterizerDensity`/`style.FontScaleDpi` ≠ 1; multi-viewport; a context
  used with an ImGui context other than the one it was created in (fonts
  refuse; drawing does not check).
- Text over 16 MiB (the truncation path) was not executed.
- `Text()` called outside a frame crashes inside ImGui (`GetID` with no
  window), as any ImGui widget does; there is no public way to detect it.
- Timing: none taken.

## Decisions (also D30)

1. Exact ImGui pin → floor 1.92.0. No `src/` file includes `imgui_internal.h`.
2. Errors: null/false + `GetLastError()`; per paragraph `GetParagraphError()`.
3. Fonts atomic: a probe registry takes every refusal first.
4. A replaced role's old `ImFont` stays in the atlas.
5. `DrawParagraphToBuffers` uses `ImDrawIdx`, refuses on overflow.
6. `Text()` keys by pointer + ID stack, then compares bytes; n-th use of an ID
   per frame gets its own slot; eviction after 600 unused frames.
7. Item size is whole pixels (`ceil(px - 1e-4)`), width = width given.
8. `LineHeight` and `Scale` floats are read as the decimal the host wrote
   (`%.7g`), so `1.35f` gives the reference's `round(size * 1.35)`.
9. One em per paragraph, from the Regular face's pixel height.
10. `Text()` with no usable font falls back to wrapped `ImGui::TextUnformatted`.
11. A refused formula is set as its literal source and reported.
12. A run naming an unbacked face is set in the paragraph's face and reported.

## Findings

- **Ink overhangs the right margin by one pixel column** on some justified
  lines. Cause (read, not bisected): draw snaps each rigid span's first glyph
  to a whole pixel and rounds each gap, after the line was justified in
  scaled points. It is the reference's device rule, so not changed; a host
  that clips exactly at the item's edge loses that column.
- `src/style` already clamps character measures and space widths to
  `MAX_DIMEN` (`rounded_dimen`), contrary to the para report's "nothing else
  here clamps". But a sentence space (`interword_glue`, extra space and
  stretch × space factor) exceeds it again: with a 32-em space at the largest
  em a glue of 1.33 × MAX_DIMEN reaches the material. Clamped in
  `build_material`; test `what_a_font_answers_is_clamped…`.
- An index often *equals* the byte offset in justified ASCII text (each line
  drops one space and gains one right-skip item), so a host that confuses the
  two will pass casual testing. The header is loud about it for that reason.
- `cmake/imgui.cmake`'s comment still justifies the pin by `ImFontLoader`.
- `tests/smoke.cpp` checks `GetImGuiVersionNum() == IMTYPESET_IMGUI_VERSION_NUM`,
  so that macro was kept with its old meaning-ish ("tested against").

## For later tasks

**api-math** — `src/context.h`, `detail::InlineMath {scan, build, user}` and
`set_inline_math(ctx, math)`. With `ImTypesetTextFlags_Math` set and a layer
installed, `build_material` (src/imtypeset.cpp) asks `scan` for the next
`[begin, end)` (delimiters included), and at it calls `build`, which must
return an atom box in the paragraph's arena (`atom_box`). The atom is one
material node; `ImTypesetParagraph::atoms` records node → source bytes and
`ensure_model` hands `SelectionModel::text` the source substring (with its
dollars) as `TextData::atoms`. A refusal sets `ImTypesetError_MathRefused`
with `BytePos = formula begin + byte_pos` and `CodepointPos` likewise, and
the source is set as text. `build` receives the font of the run the formula
starts in — you probably want Face::MATH at that size instead; decide there.
The space factor is not touched around an atom. Public change needed: none to
signatures; update the `ImTypesetTextFlags_Math` comment, and decide whether
installing the layer is automatic in `CreateContext`. The test
`the_math_seam_…` uses a fake layer and shows every step.

**example** — the snippet at the top of the header is the minimal host. Use a
backend with `RendererHasTextures` (`tests/support/soft_raster.h`). Fonts:
`tests/data/fonts`. `ImTypesetStyle::EmSize = 16` reproduces the reference's
theme. For selection, keep `(paragraph, ImTypesetSelection)` pairs and call
`AppendParagraphSelectionText` per paragraph on one buffer.

**bench** — `GetStats()` separates text builds, layouts and draws.
`detail::shipped_commands(p)` gives the commands. Per-call cost of a cached
`Text()`: one `GetID`, one hash lookup, a `memcmp` of the text unless
`NoTextCompare`, `resolve_style` (one `snprintf` only when `LineHeight`
changes), `Dummy`, `IsRectVisible`, then draw with the line index.

**stress** — entry points to fuzz: `SetParagraphText` (text, runs),
`LayoutParagraph` (width, style), `AddFontFromMemoryTTF`. The limits are the
constants at the top of `src/context.h`. Review item E (total fit with no
work budget) is reachable from the public API through
`ImTypesetLayoutFlags_TotalFit` on ragged text and, for justified text, by
sheer length: nothing here caps it. Paragraph height is a sum and can pass
`MAX_DIMEN` (about 1,000 lines of 16 px type); it is a `Box` height all the
same — the vbox is never nested, so nothing sums it again.

**font-wild** — `face_index` is in both Add signatures and refused in
`add_font` (one `if`), because `FontLibrary::register_font` has no index
parameter. Give it one and delete the `if`; the probe-registry sequence then
needs the index passed twice.
