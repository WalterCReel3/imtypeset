# api-math — result

Files: `include/imtypeset.h`, `src/imtypeset.cpp`, `src/context.h`,
`tests/test_api_math.cpp` (new, 20 tests). Appended: `docs/divergences.md`
(`## api-math`), `docs/decisions.md` (D31). Nothing else touched;
`tests/test_api.cpp` and `tests/test_header_pod.cpp` are unchanged.

## Added to the public surface (nothing existing changed)

- `bool ImTypeset::EnableMath(bool enable = true)` — the context's switch. False only with no context.
- `ImTypesetTextFlags_Math` (value unchanged, 1 << 2) — now live: the text's dollars are formulas, once `EnableMath()` was called.
- `struct ImTypesetFormula { int TextStart, TextEnd; bool Display, Refused; int BytePos, CodepointPos; const char* Reason; }` — plain data; `Reason` never null, valid until the paragraph's text is set or it is laid out again.
- `int ImTypeset::GetParagraphFormulaCount(const ImTypesetParagraph*)` — formulas found at the last layout; 0 for null.
- `bool ImTypeset::GetParagraphFormula(const ImTypesetParagraph*, int index, ImTypesetFormula* out)` — false, `out` untouched, for a bad index or null.

No declaration, struct size or enum value that `public-api` shipped moved.
`ImTypesetError_MathRefused` already existed.

Internal (`src/context.h`): `scan_math(...)` (declared so a test can hold it
against the golden), `FoundFormula`, `FormulaRecord`,
`AtomSource::in_display_text`, `ImTypesetParagraph::{formulas, display_block,
display_text, display_map}`, `ImTypesetContext::{math_enabled, math_fonts, …}`.
`InlineMath` / `set_inline_math` are kept: an installed layer replaces the
built-in pair (test_api's fake does).

## How it works

`build_material`: text decoded once; if the whole text is a `$$` block →
`display_block` (centred, no first-line indent, display style); else
`scan_math` lists the formulas and the backslashes that escape a dollar; each
formula → `mathtext::parse` → `MathFonts` at the size of the run it starts in
(kept per size in the context, dropped on a registry generation change) →
`set_formula` → `atom_box` → one node of the hlist. Broken, shipped and drawn
by the paragraph's own path; glyph keys are `math@size`, drawn through the
atlas font `AddFontFromFileTTF(ImTypesetFace_Math, …)` made (D27), stand-ins
included (D15). Copy: `AtomSource` → `TextData::atoms`, ascending.

## Decisions (D31; departures in docs/divergences.md `## api-math`)

1. Two switches: `EnableMath()` on the context and the flag on the text.
   Forced by test_api (flag + math face + nothing installed ⇒ no atoms) and
   defensible on its own; collapsing to the flag alone is one line.
2. Scanner = the reference's `_math_inline` + markdown-it's escape rule,
   nothing else of markdown. `\$` is a dollar and its backslash is not set
   or copied; no other backslash is touched (`\\` stays two); backquotes,
   asterisks and `ImTypesetRun`s hide nothing.
3. Display math by the same flag: a text that is wholly a `$$` block.
   reviewer-4 asked for a separate flag; not done, reason in D31 and in a
   concern back to reviewer-4.
4. Refused formula: source with dollars in the code face (paragraph's face if
   no Code font), unbreakable inline, breakable as a display block — the
   reference's `_formula`. Reason strings are mathtext's (the reference's).
   First refusal → `GetParagraphError()` with byte and codepoint position in
   the TEXT; all → `GetParagraphFormula()`. `Text()` → `GetLastError()` on
   the frame of the layout.
5. Unclosed display block: reported as refused with the reference's sentence.
6. No math face: every formula refused with "the math face could not set it:
   MissingFontError: math has no bundled file and none is registered".
7. Space factor after a formula is neutral (changed `public-api`'s seam).
8. No `ImTypesetStyle` field added; no memo of built formulas.

## What was run

Private dirs, all modules, tests `test_api;test_api_math;test_header_pod;smoke`,
`-Werror`, zero warnings:

| dir | config | ctest | test_api_math | test_api |
|---|---|---|---|---|
| `build/api-math` | debug, FreeType 2.12.1, 32-bit ImWchar | 4/4 | 20 tests, 28,810 checks, 0 failed | 19 tests, 6,051 checks, 0 failed |
| `build/api-math-release` | Release | 4/4 | same | same |
| `build/api-math-asan` | ASan+UBSan | 4/4 | same; no "runtime error" line | same |
| `build/api-math-stb` | `-DIMTYPESET_USE_FREETYPE=OFF` | 4/4 | same | same |
| `build/api-math-wchar16` | `-DIMTYPESET_IMGUI_WCHAR32=OFF` | 4/4 | same | same |

What the tests hold:
- **Against the reference** (`tests/golden/math/inline.golden`): 58 paragraphs
  segmented by `scan_math` as the reference segments them (43 formulas); all
  71 cases through `SetParagraphText`/`LayoutParagraph`: material node for
  node, each formula's outcome/reason/position, and at 102 widths the breaks,
  the breaker's line boxes and the shipped commands (2,749 glyphs, 78 atoms;
  48 formulas set, 8 refused). Vertical placement is compared relative to the
  line's baseline and heights as "not less": the golden has no strut.
- 19 sources with backslashes run through the reference's tokenizer by hand
  (`_tokens(source, math=True)`): the formulas found are the port's; only the
  backslashes shown differ, as recorded.
- Copy: select-all by drag over two wrapped paragraphs, one with a formula →
  exactly `"The area of a circle is $\pi r^2$, and no more.\n\nA second paragraph follows it."`
- Refusals: parser, engine (`\sqrt` ×6, the reference's "1.04 times" sentence),
  no math face, unclosed and refused display blocks; byte and codepoint
  positions with multi-byte characters before the place.
- Performance: steady-state frame of a justified paragraph with formulas plus
  a display block: 0 text builds, 0 layouts, 0 allocations; reflow = 0 text
  builds; kept paragraph (layout, draw to buffers with selection, hit test,
  rects): 0 allocations.
- Pixels (`soft_raster`): `$$\sum$$` at a 28 px em ships one minted glyph
  U+FFFA4 (asked of the atlas as U+E43A in the 16-bit build), 430 inked
  pixels of 1,656 in the atom's box, 0 outside (420 under stb); two minted
  tall parentheses (U+FFFBB, U+FFFB4) each ink ~160 of 839 pixels of their
  own boxes.
- Nesting: 60 depths × 3 shapes: 72 set (deepest 24), 108 refused, no
  `LayoutFailed`.

## Not verified

- No mutation run on the new code.
- The golden has no strutted line; how a tall formula's line sits vertically
  is the `para` module's composition, not compared with the reference here.
- That `inline.golden` #57 fails without the space-factor reset: read, not run.
- Preset builds (`build/debug` …) and the rest of the suite: not run.
- clang, MSVC, any ImGui but 1.92.9, litgen: not run.
- `Text()` with formulas in tables / columns; timings: none.
- The `MissingFontError` sentence is the reference's by reading
  `fontlib.font_file`, not by running the reference without its bundle.

## Findings

- The brief says 59 paragraphs + 12 blocks; the golden has **58 + 13**.
- `AppendParagraphSelectionText` allocates (~14 per call on the test
  paragraph, inside `SelectionModel::text`), with or without math. Not a
  frame cost; left.
- A code face that lacks a character of a refused source stands it in from
  the prose face (Greek in Inconsolata), as for any text.
- An ATOM command's height and depth are its line's, not the formula's own
  (as SET_GLUE's are); the formula's extent is in its glyphs.

## For later tasks

**api-revise** — R3 (byte offsets everywhere): `ImTypesetFormula` already
speaks text bytes; a formula is ONE index covering `[TextStart, TextEnd)`, so
a byte offset inside a formula has no caret of its own — decide whether it
snaps to the formula's start. An escaped dollar's backslash has no index
(its offset answers the dollar's). R5: the widget's refusal is copied to the
context error only on the frame of the layout (`text_widget`). R1: content
hashing must include the flags (Math changes the material).

**example** — add the Math font, `ImTypeset::EnableMath()`, pass
`ImTypesetTextFlags_Math`. A display formula is its own `Text()` call with a
text of `$$ … $$`. Literal dollars in math-flagged text: `\$` (in C source
`"\\$"`). `GetParagraphFormula` lists refusals for a "what could not be set"
panel.

**bench** — a text build with formulas costs parse + mlist per formula; the
first formula at a new size also builds three `MathFonts` faces. Steady state
is unchanged. `GetStats()->TextBuilds` counts formula work (one per text).

**stress** — fuzz `SetParagraphText(…, ImTypesetTextFlags_Math)` after
`EnableMath()`: `scan_math` and `display_block` index codepoint arrays by
hand (the backslash-count loop walks backwards and stops at the opener);
`display_map` maps collapsed bytes back to text bytes. A formula's size is the
run's (clamped to `MAX_EM`); `MathFonts` are kept for 64 sizes, so runs at
many scales rebuild them.
