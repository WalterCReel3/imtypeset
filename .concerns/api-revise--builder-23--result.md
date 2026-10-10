# api-revise — result (builder-23)

Files: `include/imtypeset.h`, `src/imtypeset.cpp`, `src/context.h`, `tests/test_api.cpp` (39 tests),
`tests/test_api_math.cpp` (19), `tests/test_header_pod.cpp`, `tests/test_stress_text.cpp` (10),
`examples/minimal.cpp`, `examples/specimen.cpp`, `bench/bench_draw.cpp`, `bench/bench_reflow.cpp`,
`bench/bench_font.cpp`, `docs/performance.md`. Appended: `docs/decisions.md` (D36),
`docs/divergences.md` (`## api-revise`). Nothing else touched. Version is now 0.2.0.

Work was done in a private copy (`build/api-revise-work`, links to everybody else's files) and
copied into the tree when the four test files passed, so the shared header was the old one until
then.

## The surface, before -> after

| before | after | why |
|---|---|---|
| `CreateContext(version, sizeof_imwchar, sizeof_imdrawvert, sizeof_imdrawidx, sizeof_style)`; always made the new context current | `CreateContext()`; current only if none is. `IMTYPESET_CHECKVERSION()` -> `bool DebugCheckVersionAndDataLayout(version, 11 sizes)`; a failed check makes `CreateContext` refuse | R4, C3, C4. The check covers ImWchar, ImDrawVert, ImDrawIdx and all seven structs, `ImTypesetRun` among them |
| `const ImTypesetStats* GetStats()`, seven `int` | `ImTypesetStats GetStats()` by value, ten `ImU64` (`BytesCompared` -> `BytesHashed`; new `DeferredLayouts`, `CacheEvictions`, `CachedBytes`) | R2 (overflow reproduced under UBSan); by value because the pointer dangled after `DestroyContext` (example #6) |
| `GetLastError()`: the current context's, sticky | one process-wide slot, set by a failing `[E]` call and cleared by a succeeding one | R5, B1, B2 |
| — | `void SetTextCacheLimits(int max_paragraphs, int max_bytes)` | R1: the cap, on the context |
| `AddFontFromFileTTF/MemoryTTF`: non-zero `face_index` refused here; a replaced font stayed in the atlas; `fopen` | index passed to the registry; the replaced `ImFont` is removed from the atlas; the file is read here (`_wfopen` on Windows) and registered from memory; refused on a legacy renderer | R13, R6, D5, B7 |
| — | `bool RemoveFont(ImTypesetFace)` | R6 |
| `bool EnableMath(bool)` | removed; `ImTypesetTextFlags_Math` alone | R12 |
| `void Text(text, text_end, flags)` keyed on the pointer | `bool TextUnformatted(text, text_end, flags, wrap_width, style)` keyed on the ID stack + content | R1, R8, R9 |
| `void TextJustified(...)` | removed | R9: it was the default |
| — | `bool Text(fmt, ...)`, `bool TextV(fmt, va_list)` | R9: the name formats |
| — | `bool TextWithID(str_id, text, text_end, flags, wrap_width, style)` | R1: text that changes every frame |
| — | `bool SelectableText(str_id, text, text_end, flags, wrap_width, style, ImTypesetTextRange* selection)` | R10 |
| — | `ImVec2 CalcTextSize(text, text_end, wrap_width, flags, style)` | R10 |
| `void DestroyParagraph(p)`: current context, linear | the paragraph's own context, constant time | R9, B11 (30 us each at 20,000, 124 us at 80,000 before) |
| `ImVec2 LayoutParagraph(p, width, style)` | `bool LayoutParagraph(p, width, style)`; size from `GetParagraphSize` | R5 |
| — | `ImTypesetStatusFlags GetParagraphStatus(p)`: `LaidOut`, `FirstFitFallback`, `DisplayMath` | the stress note: the fallback is a status, not `Code` None with a `Message` |
| `bool GetParagraphFormula(p, i, ImTypesetFormula* out)` | `ImTypesetFormula GetParagraphFormula(p, i)` | bindings-notes §5.2 |
| — | `ImTypesetLine GetParagraphLine(p, line)` (box, baseline, text bytes) | R10, example #1 and #2 |
| `void DrawParagraph(...)`, `bool DrawParagraphToBuffers(...)` (true for a paragraph with no font), `const ImTypesetSelection*` | both `bool`, both refuse without their ImGui context, on a legacy backend, with no font, or laid out before the fonts changed; `const ImTypesetTextRange*` | R5, R6, B4, B5, B10 |
| `int GetParagraphLength(p)`, `int GetParagraphTextOffset(p, index)`, `int GetParagraphIndexFromTextOffset(p, byte)` | removed | R3: indices are internal |
| `int HitTestParagraph(p, pos)` -> index | `ImTypesetHit HitTestParagraph(p, pos)` -> `{TextOffset, Line, Inside}` | R3, C10 |
| `ImTypesetSelection SelectParagraphRange(...)` -> indices | `ImTypesetTextRange SelectParagraphRange(...)` -> bytes | R3 |
| `GetParagraphSelectionRects(p, sel, out)` | `GetParagraphRangeRects(p, range, out)` | R3 |
| — | `ImVec4 GetParagraphCaretRect(p, byte_offset)` | R10 |
| `void AppendParagraphSelectionText(p, sel, out)` | `int AppendParagraphRangeText(p, range, out)` (bytes appended) | R3 |
| — | `TextCodepointToByteOffset`, `TextByteToCodepointOffset`, `ParagraphCodepointToByteOffset`, `ParagraphByteToCodepointOffset` | R3, C2, bindings-notes §5.5 |
| `ImTypesetFace_Default = -1`, `Regular = 0` .. `COUNT = 7`; other enums implicit | `Default = 0`, `Regular = 1` .. `Math = 7`, `COUNT = 8`; every value written out | R4 (zero is the default), C10 |
| `ImTypesetLayoutFlags_FirstFit`, `_TotalFit`; `ImTypesetStyle::LayoutFlags` | `ImTypesetLineBreak_Auto/FirstFit/TotalFit`; `ImTypesetStyle::LineBreak` | R9, C7 |
| `ImTypesetTextFlags_NoTextCompare` | removed | R1 |
| — | `ImTypesetError_BackendUnsupported = 11` | R6 |
| `ImTypesetStyle`, `ImTypesetRun`: constructors with non-zero defaults (1.35, 96, Scale 1, Face -1) | all zero is the default; the constructors zero | R4 |
| `ImTypesetSelection {Start, End}` indices | `ImTypesetTextRange {Start, End}` bytes | R3 |
| — | `ImTypesetLine`, `ImTypesetHit` | R10 |
| explicit `Color` / run colour drawn raw | multiplied by `style.Alpha` | R8, B8 |
| item always the available width | ragged right: the longest line; otherwise the wrap width | R8, D4 |
| a width change re-broke every submitted paragraph | only those in view | R7, D2 (200 of 200 before, 2 of 200 after) |

Unchanged: `DestroyContext`, `Get/SetCurrentContext`, `GetVersion`, `GetImGuiVersionNum`, `GetStyle`,
`GetFont`, `CreateParagraph`, `SetParagraphText`, `GetParagraphError`, `GetParagraphFormulaCount`,
`GetParagraphSize`, `GetParagraphLineCount`, `ImTypesetErrorInfo`, `ImTypesetFormula`, the align,
spacing, granularity and run-flag enums (values now written out).

## What a binding generator needs, per declaration (a reading; no generator was run)

| declaration | ownership / nullability / buffers |
|---|---|
| `IMTYPESET_CHECKVERSION()` | a macro: call it once in the module's init; do not bind `DebugCheckVersionAndDataLayout` |
| `CreateContext()` | returns a handle the caller destroys with `DestroyContext`; null on failure. The handle type is opaque: its definition is in `src/context.h` |
| `DestroyContext(ctx = NULL)` | null means the current one |
| `GetCurrentContext`, `SetCurrentContext(ctx)` | not owning; null allowed |
| `GetVersion()` | static string |
| `GetLastError()` | never null; points at library storage that the next `[E]` call overwrites, and `Message` with it: copy `Code` and `Message` at once |
| `GetStats()` | by value |
| `GetStyle()` | a reference into the context: writes through; do not keep it past `DestroyContext` |
| `SetTextCacheLimits(int, int)` | plain |
| `AddFontFromFileTTF(face, filename, face_index, font_cfg)` | `filename` UTF-8; `font_cfg` nullable, copied; returns an `ImFont*` the atlas owns, null on failure |
| `AddFontFromMemoryTTF(face, const void* data, int size, ...)` | BY HAND, taking bytes; the data is copied before the call returns |
| `RemoveFont(face)`, `GetFont(face)` | `GetFont` returns a non-owning pointer or null, invalid after the role's font is removed or replaced |
| `TextUnformatted(text, text_end, flags, wrap_width, style)` | drop `text_end` by name (a binding's second string is an unrelated buffer); `style` nullable, read during the call only |
| `Text(fmt, ...)`, `TextV` | bind as one string through `"%s"`, or not at all (`TextUnformatted` is the same call) |
| `TextWithID(str_id, text, text_end, ...)` | drop `text_end`; `str_id` non-null |
| `SelectableText(str_id, text, text_end, flags, wrap_width, style, ImTypesetTextRange* selection)` | drop `text_end`; `str_id` not empty; `selection` nullable, IN-OUT (read at entry, written before return): bind as value in, value out |
| `CalcTextSize(text, text_end, wrap_width, flags, style)` | drop `text_end`; returns `ImVec2` |
| `CreateParagraph()` | handle the caller destroys with `DestroyParagraph`, or that dies with its context; using it after either is a use after free. Do not tie it to a Python object's lifetime without keeping the context alive |
| `DestroyParagraph(p)` | null ignored |
| `SetParagraphText(p, text, text_end, const ImTypesetRun* runs, int runs_count, flags)` | BY HAND: drop `text_end`; `runs` is a pointer + count of structs, nullable with count 0; text and runs are copied. Offsets are bytes: a binding taking codepoints converts with `TextCodepointToByteOffset` |
| `LayoutParagraph(p, width, style)` | `style` nullable, read during the call |
| `GetParagraphError(p)` | never null (null `p` answers a static error); points into the paragraph, valid until its next `SetParagraphText`/`LayoutParagraph` |
| `GetParagraphStatus`, `GetParagraphFormulaCount`, `GetParagraphSize`, `GetParagraphLineCount` | plain values; null `p` allowed |
| `GetParagraphFormula(p, i)` | by value; `Reason` is a `const char*` into the paragraph, never null, valid until its text is set or it is laid out again: copy it |
| `GetParagraphLine(p, line)`, `HitTestParagraph(p, pos)`, `GetParagraphCaretRect(p, byte)`, `SelectParagraphRange(...)` | by value, nothing to own; a binding in codepoints converts offsets with the `Paragraph...Offset` pair |
| `DrawParagraph(p, ImDrawList*, pos, const ImTypesetTextRange* selection)` | draw list non-null, not owned; `selection` nullable |
| `DrawParagraphToBuffers(p, pos, clip_rect, ImVector<ImDrawVert>*, ImVector<ImDrawIdx>*, selection)` | both vectors non-null, APPENDED to, owned by the caller; needs `ImVector_ImDrawVert` and the `ImDrawIdx` instantiation the bundle already has |
| `GetParagraphRangeRects(p, range, ImVector<ImVec4>* out)` | appended to; nullable (then 0); returns the count appended. By hand if a list is wanted |
| `AppendParagraphRangeText(p, range, ImGuiTextBuffer* out)` | appended to; nullable; returns bytes appended. By hand if a `str` is wanted |
| `Text/ParagraphCodepointToByteOffset`, `...ByteToCodepointOffset` | drop `text_end` on the two `Text` ones; clamped; no context needed for those two |
| `ImTypesetStyle`, `ImTypesetRun` | plain, copyable, all-zero default; constructors take nothing |
| `ImTypesetTextRange`, `ImTypesetLine`, `ImTypesetHit`, `ImTypesetStats` | plain values |
| `ImTypesetErrorInfo`, `ImTypesetFormula` | read-only; the `const char*` member aliases library storage |
| enums | all `int` typedef + enum, values explicit; `ImTypesetFace_COUNT` is a count, not a face |

By hand, as before: the two handles, `AddFontFromMemoryTTF`, `SetParagraphText`. No longer by hand:
`GetParagraphFormula`. `docs/bindings-notes.md` §5 was written against the old header and is now
out of date row by row; I did not edit it (not mine).

Three things in the table above are subtler than a row says:

- **The widget cache is a clock, not least-recently-used.** Entries stay in order of arrival; a
  hit moves nothing; the oldest arrival is dropped if unused for two frames and the cache is over
  a limit (or unused for 600 frames), and sent round if still in use. Over a limit the whole list
  is gone over once a frame. What this frame and the last submitted is never dropped.
- **`bool` from a widget means "set as asked", not "pressed".** That is R5's model and reads
  oddly beside ImGui's widgets; the header says so.
- **A position at a line break is one number for two places** (end of one line, start of the
  next), as in the reference. `ImTypesetHit::Line` says which; `GetParagraphCaretRect(byte)`
  answers the later line.

## The review, item by item

Reproduced against the old header in a private snapshot (`build/api-revise-snap`, ASan+UBSan), one
failing test each, before anything changed, then fixed and held by a test in `tests/test_api.cpp`:

| item | reproduction (old header) | now |
|---|---|---|
| A1 temporaries | one text at two addresses: 2 layouts, 2 cached | 1 layout in 40 frames of fresh buffers |
| A1 leak | 300 frames, fresh address each: 300 layouts, 300 cached | content-keyed; limits; evictions counted |
| A1 inserted row | 20 rows in one buffer, one inserted on top: 21 layouts in that frame | 1 |
| A1 `NoTextCompare` stale text | three texts in one buffer: 1 text build, the first text shown three times | flag removed; 3 builds |
| A3 | UBSan: signed overflow in `BytesCompared` and `VerticesEmitted` | `ImU64`; test runs the counters past 2^31 |
| B1 | failed `CreateContext` with a context current: `GetLastError()->Code` 0 | reported |
| B2 | error stays after a success | cleared |
| B3 | widget's NoFont reported on the layout frame only | every frame |
| B4 | ASan SEGV in `ImGui::GetColorU32` with no ImGui context | refused, `NoContext` |
| B5 | another ImGui context current: 1,124 vertices emitted, returned true | refused |
| B6 | NOT as claimed: ImGui's own legacy atlas build asserts (`font_size > 0`) on a font added with no size, before any call of ours | refused in `AddFont*` when a legacy renderer is known, and at the first frame otherwise; the assert itself is reachable still (D36) |
| B8 | explicit colour at alpha 255 under `style.Alpha` 0.5 | 127 |
| B10 | `DrawParagraphToBuffers` true for a paragraph with no font | false, `NoFont` |
| B11 | paragraph of a non-current context not destroyed; 30 us each at 20,000, 124 us each at 80,000 | own context; slot swap |
| B12 | no-op `LayoutParagraph` 383 ns with one line height, 915 ns alternating two (ASan build) | four values remembered; test counts the conversions (2 in 2,000 calls) |
| C4 | second `CreateContext` took the current context | only if none |
| C7 | both bits expressible | one enum |
| D2 | 200 of 200 paragraphs laid out on a width change | 2, 198 deferred |
| D4 | three left-aligned words: item 584 px in a 600 px window | as wide as the words |
| D5 | ASan heap-use-after-free in `ImFont::IsGlyphInFont` after the host's `RemoveFont` | role found empty; `RemoveFont(face)` |

Not reproduced: **B7** (Windows paths; fixed from a reading, never compiled). **B9** (stale
`LayoutFailed` across widths): I found no input whose layout fails at one width and not at
another; the error is now recomputed per layout, and the test plants a stale one to show it goes.
**C5** (`Text("%d items", 0)` compiles): true by reading; `Text` now formats.
By reading only: C1, C2, C3, C8, C10, D3 (documented with `AlignTextToFramePadding()`; not
fixable through `imgui.h`), D6 (documented), D7 (`FontRasterizerDensity` still unverified), D8.
Not done: C9 (fallback faces; R10 says not now), D1 (the budget is `stress`'s, done).

## The examples' ten awkward places

1 line geometry: `GetParagraphLine` (the specimen's line printout now uses it; its 108 line-break
listings are identical to the old build's). 2 baseline: `ImTypesetLine::Baseline`. 3 per-call
alignment: a style pointer per call. 4 two sizes: kept, explained at `ImTypesetStyle`. 5 runs as
byte offsets the host assembles: kept (a builder is the Document layer's business); the silent
overfull line of a `NoBreak` run that swallows a space is NOT addressed. 6 `GetStats()` pointer:
by value; `GetLastError()` storage is the process's. 7 `SelectionColor` at layout: kept; the
header says to fill `GetParagraphRangeRects` for another colour. 8 two math switches: one.
9 `Text(text, NULL, flags)`: still `TextUnformatted(text, NULL, flags)`; `text_end` is ImGui's
idiom and was kept. 10 item width: the text's, for ragged right.
The seven images the specimen writes are byte-identical to `docs/images/`.

## From the stress note

Fallback status: `ImTypesetStatusFlags_FirstFitFallback` (the `Code` None + `Message` is gone;
`tests/test_stress_text.cpp` asserts the flag). `docs/performance.md`: growth section marked and
re-taken with the budget; font rows marked with the strict-pass cost (2.9 -> 6.2 ms theirs;
6.0 to 6.5 ms in my sitting). ImGui's "Out of texture memory" at very large type: not fixed, now
said in the header under FONTS. `tests/test_stress_text.cpp` updated, and it now also fuzzes
`TextWithID`, `SelectableText`, `CalcTextSize`, the per-call style and width, lines, carets and
the offset helpers.

## What the Document API inherits (R14)

- A block is an `ImTypesetParagraph`: every query here takes `const ImTypesetParagraph*` and does
  not ask who owns it. `DestroyParagraph` already ignores a paragraph that is not in its
  context's own list (a widget's, the measuring one), which is what a document-owned block is.
- A position is a byte offset; a document position adds a block number beside it. Nothing here
  is named for "the" paragraph in a way that blocks that: `ImTypesetTextRange` stays the range
  inside one block, and a document range is a new struct of two (block, offset) pairs.
- `AppendParagraphRangeText` already joins blocks with a blank line when called per block.
- The widget cache keys on content + ID stack and holds whole paragraphs; a `Document` widget
  can use the same clock with a document as the entry.
- Not inherited: selection across blocks in `SelectableText` (one paragraph only); a status for
  widgets beyond their error.
- `ImTypesetParagraph` and `ImTypesetContext` are defined in `src/context.h`; a nanobind binding
  must see them (bindings-notes §5.6). For `dist`.

## Outside my files, for whoever owns them

- `examples/demo_sdl2/main.cpp` (shot-rig's, not built by default, not in my touches) no longer
  compiles: `ImTypeset::EnableMath()` is gone and `ImTypeset::Text(text, nullptr, 0)` is now a
  format call. Fix: delete the `EnableMath()` line; `ImTypesetStyle s = ImTypeset::GetStyle();
  s.Align = align; ImTypeset::TextUnformatted(text, nullptr, 0, 0.0f, &s);` in `column()`.
- `docs/bindings-notes.md` §5 and `bindings/imgui_bundle/*` describe the old header.
- README (task `docs`): R10's known gaps to list — no fallback faces for CJK/emoji/icons
  (`MergeMode` refused), display math only as a whole-text `$$` block, no background-thread
  layout.
- `FontLibrary::set_legacy_kern` (font-wild) has no public switch; I did not add one.
- `tests/smoke.cpp` is why `GetImGuiVersionNum()` stayed.

## What I ran

Private directories `build/api-revise-{debug,release,asan,stb,wchar16}`, configured from the tree
with nothing narrowed, `-Werror`, on the final sources. 30 ctest tests each, the two examples
among them:

| configuration | ctest | my four test files | warnings | sanitiser reports |
|---|---|---|---|---|
| debug, FreeType 2.12.1, 32-bit ImWchar | 28 of 30 | pass | 0 | |
| release | 28 of 30 | pass | 0 | |
| ASan+UBSan | 28 of 30 | pass | 0 | 0 |
| stb_truetype | 28 of 30 | pass | 0 | |
| 16-bit ImWchar | 28 of 30 | pass | 0 | |

The same two fail in all five, and neither is mine or goes through the public entry in the
failing checks: `test_opentype_formats` (cmap format 12 glyph ids past 2^32; Type 2 arithmetic
overflow) and `test_stress_font` (`n3_a_kern_table_of_65535_subtables_is_read_in_one_pass`, a
timing bound; `a_glyph_drawn_above_a_header_box_...`; `fuzz_small_synthetic_fonts_densely_all_the_way`,
"12 fonts had a cmap group whose glyph ids pass 2^32"). Both files were being edited through the
evening (last write 00:36) and read like tests written ahead of a reader fix. Run three times
over about ninety minutes (the set of failing checks changed between runs; it never included a
check of mine). `test_opentype_wild` PASSES. I did not touch either file.

`tests/test_api.cpp` 39 tests, 71,556 checks; `tests/test_api_math.cpp` 19 tests, 28,841;
`tests/test_stress_text.cpp` 10 tests, 207,471; `tests/test_header_pod.cpp` exit 0.
`example_minimal`, `example_specimen` pass under ctest in all five; the specimen run with an
output directory (debug, FreeType) writes seven PNGs byte-identical to `docs/images/`.
Benchmarks: built in all five; run in release, old against new, five alternating rounds, plus
the growth table once: `docs/performance.md`, "After the API revision (D36)".
clang-format clean on the twelve files.

## Not verified

- Windows (`_wfopen` branch never compiled), clang, MSVC, any ImGui but 1.92.9.
- No binding generator was run on the new header.
- Where the extra 50 to 180 ns of a widget call goes (no profiler here; two guesses did not move
  it). It is a regression against the old header at thousands of calls a frame: 1.44x at 3,400.
- `SelectableText` with a real mouse, a real clipboard, or two of them competing for Ctrl+C in
  one window; nav/keyboard focus (it tracks "last clicked", not ImGui focus).
- The font check runs once a frame (and when the atlas's font count changes): a font removed
  AND another added between two calls inside one frame is not noticed until the next frame.
- A legacy backend that adds a font with no size before its renderer is initialised still dies in
  ImGui's own atlas build (D36).
- Two ImGui contexts sharing an atlas; `FontRasterizerDensity` != 1; tables and `SameLine`
  layouts beyond the one `SameLine` in the item-width test; a widget text over 16 MiB.
- The preset directories (`build/debug` ...) were not built.
- The collection path of `face_index` through the public entry: my tests only hold that index 1
  of a single-face file is refused; font-wild's tests hold the rest.
- No mutation run on the new code.
