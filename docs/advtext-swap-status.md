# advtext full swap: the core's half

Kept current by imtypeset's coordinating session for the session working in
`../imgui-adv-text` (branch `experimental/imtypeset-adoption`), which reads this
tree and cannot write it. One entry per numbered item of the swap request.
States: **done**, **in progress**, **queued**, **declined** (with the reason),
**needs the user**. A decision an agent took is marked *agent decision*; the
user is the only one who moves the border.

Last updated: 2026-10-11, after commit `12d77ac` (items 2, 6, 7, 8 are in it).

## Build from

| | |
|---|---|
| commit | **`12d77ac`** on `main` ("Pin the oracle, answer advtext's API questions, add the galleries"). Before it: `5654eee`, the tree the release gate passed |
| what it is | `5654eee` (every preset from clean) plus items 2, 6 and 7. On `12d77ac` itself only the `debug` preset was run: 43 of 43. Rebuild the library: the Python package of this commit refuses one built from `5654eee` |
| how | `python/host/build_bundle_library.sh` as before; one library per interpreter |
| pushed? | no — the session cannot reach the network; the local repository has it |

A library built from a working tree with uncommitted changes is not
reproducible, and this tree will have such changes while the items below are
in progress. Build from a commit named here. Each later commit that changes
what advtext can call is listed under "API changes".

## API changes since `5654eee`

**In `12d77ac`** (tasks `swap-api-answers` and `galleries`). Nothing was renamed or
removed. A library and the `python/imtypeset` package must come from the same
commit: the package refuses a library that lacks a function it declares.
Also new in `12d77ac`, from `galleries`: `ShowDemoSection` /
`GetDemoSectionName` / `ImTypesetDemoSection_` (C: `ImTypeset_ShowDemoSection`,
`ImTypeset_GetDemoSectionName`; Python: `imtypeset.demo`), see item 6.

| what | C++ | C | Python | note |
|---|---|---|---|---|
| a role's font by its place in the atlas | — (a C++ host has the pointer) | `ImTypeset_GetFontAtlasIndex(face)`, -1 for none | `imtypeset.font_atlas_index(face)` → `int` or `None` | new. `imgui.get_io().fonts.fonts[i]` is then the wheel's own `ImFont` object for the role: answer (f) |
| no draw list means the current window's | `DrawParagraph(p, NULL, pos)`, `DrawDocument(d, NULL, pos)` | the same, `draw_list` null | `Document.draw(None, pos)`, `Paragraph.draw(None, pos)` | was refused (`InvalidArgument`); still refused outside a frame. Request C1: `typesetter.window_draw_list()` and its mangled symbol are no longer needed |
| `GetFont` with the ImGui context gone | null unless the context's ImGui context is current | the same | `get_font()` → `None` | was a dangling address: answer (c) |
| a refused `import imtypeset` | | | leaves no `imtypeset.*` in `sys.modules` | answer (e) |
| `Document.pack_marks` | | | same result, about four times faster | request C3, in part |
| one section of the demo window, as a page of the host's | `ShowDemoSection(ImTypesetDemoSection)`, `GetDemoSectionName(section)`, `enum ImTypesetDemoSection_` (11 values, `_COUNT`) | `ImTypeset_ShowDemoSection`, `ImTypeset_GetDemoSectionName`, `enum ImTypeset_DemoSection_` | new module `imtypeset.demo`: `DemoSection`, `sections()`, `section_name(s)`, `show_demo_section(s)`, and `show_demo_window` again | new (task `galleries`): item 6 |
| the demo window in the sidecar | | | `imtypeset.show_demo_window()` draws the window in a bundle-hosted library | was an empty function there (`IMTYPESET_DISABLE_DEMO`); the library is built with the demo now |

(Between the first library advtext built and `5654eee`:
`ImTypeset_GetContextSerial` / `imtypeset.get_context_serial()` were added —
the package refuses a library without it, so rebuild; run trouble in
`set_blocks` / `replace_blocks` / `append_text` is `DescriptionCut`, a soft
outcome returning `False`, where it used to be `InvalidArgument`; a paragraph
with out-of-order runs likewise; `Document.edit_error` keeps what was cut.)

Rule from here on: no public Python name is renamed or removed without a line
in this section naming the commit.

## Items

| # | item | state |
|---|---|---|
| 1 | a committed, reproducible state | **done**: `5654eee` |
| 2 | freeze the oracle at `advtext-reference` (32e3953); goldens for tables, columns, cross-block copy | **done** (task `oracle-freeze`). See "The oracle" below |
| 3 | tables in the core | **queued** behind 2 and 7 (task `tables-core`); requirements from `FS-S2--builder--result.md`, not yet written when this was last updated |
| 4 | columns in the core | **queued** behind 3 (task `columns-core`) |
| 5 | what DiffView needs | **in progress**: FS-S1 asks for ONE thing, accepted (*agent decision, the coordinating session's; it is leading, so typesetting*): **R1**, a way to make a Code block's line height exact whatever glyphs a line holds, and its fallback **R1b**, "does face F have a glyph for codepoint C" (the face alone, not the stand-in chain). Both are stage 0 of task `tables-core`, with FS-S2 section 4's four API notes; they land in their own commit, named here, before the table work. **Declined**, as the request itself asks: anything exposing nodes, packing or draw commands — FS-S1 asks for none |
| 6 | specimen and formula galleries | **done** in `12d77ac` (task `galleries`): see "Galleries (item 6)" |
| 7 | open API questions, and C1–C8 | **done** for (a)–(g), in `12d77ac`: see "Answers (item 7)". Of C1–C8: C1 done, C3 in part, C8 answered in the header; C5 is item 3; C2, C4, C6, C7 queued, each with its reason |
| 8 | output that is not ImGui | **needs the user**: assessed, nothing built; the recommendation and what each choice costs are under "Border questions" |

## Border questions for the user

- **The mathtext page.** The request lists `pages/mathtext.py` with specimen
  and formula as moving to the core; the user named specimen and formula. The
  core's demo already has a Math section with an editable source and the subset
  table; `galleries` treats the mathtext page's content as in scope and will
  say what it could not carry. *Agent decision, the other session's extension;
  unconfirmed.*
- **Item 8: output that is not ImGui.** Not decided; nothing was built. What
  follows is an assessment by an agent (`swap-api-answers`), from reading
  `scripts/render_litmus.py`, `advtext/backends/svg.py`, `tests/test_litmus.py`
  and the `svg` target (read by a helper agent and checked against FS-S3's
  section 5, not run).

  *What the litmus chain takes from the old core.* The flat ship-out list:
  for a glyph its font key (face and size), codepoint, x and baseline y; for a
  rule a rectangle; for the debug page each line's box and baseline and each
  glue's set width beside its natural width; for the selection page the
  selection model's rectangles; the page size from the packed box. Positions
  are unsnapped integer scaled points. Glyph shapes are outlines read with
  fontTools **by glyph name**, because the display sigma is a variant with no
  codepoint of its own. `make svg` writes five files from three layouts (a
  whole markdown document, the same with the debug overlay, a selection, the
  display sum, the sum beside its text-style form). `tests/test_litmus.py`
  (23 tests) asserts in scaled points on the command list, and several reach
  inside the algorithm: a line's fitness class, a box's glue order and glue
  set, a private rounding hook.

  *Choice A: a window-free positioned-glyph query in this library.* The
  smallest form: per laid-out paragraph or document block, a count and an
  array of `{face, em size, glyph, x, baseline y}`, and the rules as
  rectangles. `DrawDocumentToBuffers` already gives output without a window,
  but as textured quads of the ImGui atlas on whole pixels, and it needs an
  ImGui context: good for a raster picture, useless for outlines. What A
  would promise to every binding, for good: (1) which positions — layout's
  own, or the ones drawn after the pixel rules; they differ, the litmus wants
  the first and a non-ImGui renderer wants the second; (2) a glyph identity
  that is not a codepoint (a glyph index into the host's file, or a name), so
  the reader's view of a font's glyphs becomes public; (3) a second unit, or
  doubles: the header's lengths are `float` pixels, which cannot carry the
  exact scaled points the litmus tests compare; (4) nothing for the debug
  page or for the tests that look at fitness and glue set, which stay
  declined under item 5's rule (no nodes, packing or draw commands as API).
  Size: two or three structs, four to six functions, their C mirrors, Python,
  mapping rows and tests; about the size of the whole of answer (f) and C1
  together, several times over. It would serve one script and one test file.

  *Choice B: the litmus artifacts become this repository's tests and leave
  advtext.* The two litmus claims are claims about the layout core, and the
  core is here. Their geometry is already held exactly, against the reference
  itself, by the golden replays (`tests/golden/doc`, `blocks`, `math`,
  `para`); the checks that reach inside the algorithm can be written here as
  C++ tests, where tests may use internals; the pictures can be rendered with
  no GPU and no display by `tests/support/soft_raster.h`, as the images under
  `docs/images` are. Cost: advtext deletes `backends/svg.py`,
  `scripts/render_litmus.py`, the `svg` target, `test_litmus.py`,
  `test_svg.py` and the few `glyph_outline` tests (FS-S3 counts 46 tests in
  the two files), and its Formula page stops saying "use `make svg`"; the
  artifacts become PNG where they were vector; someone here writes the two
  litmus pictures and the handful of internal checks as tests (not sized; a
  small task). What is lost for good is an SVG a reviewer can zoom.

  **Recommendation (an agent's): B.** A public glyph-position API is a
  permanent promise in three languages made for one diagnostic script, and it
  could not serve that script's debug page or a third of its tests anyway. If
  vector output is wanted later, the cheaper route is a program in this tree
  that uses the internals directly, not a public query.

### The oracle (item 2)

The core's goldens no longer depend on advtext's working tree or on which
branch it has checked out. The reference is commit `32e3953` (your tag
`advtext-reference`), taken with `git archive` from your repository's object
store into `build/advtext-reference` here, and every dump script refuses to
run against anything else — your working tree included, which is what the
reference interpreter's editable install would otherwise import. Nothing was
written into your repository. **Two things there the oracle still needs: the
commit `32e3953` must stay reachable (keep the tag, or any branch containing
it), and `.venv` must stay as it is** (Python 3.11.2, imgui_bundle 1.92.900,
fontTools 4.66.0, markdown-it-py 4.2.0) — the interpreter is not something a
commit holds. Deleting `advtext/`'s layout core from the working tree, on any
branch, no longer affects the core.

Every existing golden was dumped again from the pinned tree and no record
changed. One correction to what was passed on: `a3c589a` has the tag's tree;
`30496e1` does not (it adds the postmortem and revises `cpp-extension.md`;
nothing under `advtext/`).

New goldens for the swap, `tests/golden/doc-tables` (format:
`tools/oracle/FORMAT-doc-tables.md`): a table among other blocks — every
position as (block, row, column, byte in the cell's text), row and cell
rectangles, hit tests, drags, selection rectangles and the exact copied
string through cells and across the table's edges (`\t` between cells, `\n`
between rows, row-major), find hits and links in cells, and a table arriving
row by row with the widths held and the widths set at each step. Table
layout and column cutting were already held by `tests/golden/tables`.

What the reference does not do, and the core will therefore do without an
oracle: selection and copy across the columns of a multi-column layout
(advtext draws columns and never selects in them); find marks drawn inside
cells (a colour span on a table is ignored; only a match's rectangles
exist); balanced columns; a caret rectangle in a cell. FS-S2's RQ-T1 to T8,
T10 and RQ-C1 have goldens; RQ-T9 (lazy), RQ-C2's selection across slices and
RQ-C3 do not.

## Answers (item 7)

Each was first reproduced here by a test. Tests: `tests/test_api_answers.cpp`
(C++, 8 tests), `tests/python/test_binding.py` (8 new), and in both wheels
`tools/bundle_route/check_swap_answers.py` (18 checks, passing in
imgui_bundle 1.92.900 and 1.92.801). The header says the same things at the
places named. State: in the working tree, not committed.

**(a) `range_text` around a block that gives nothing.** Reproduced: nothing
and no separator. **Kept, stated and pinned.** The separator stands between
two blocks that each gave text, once, however many blocks between them gave
none: a rule, a Host block with no text, an empty block, a Code block of
blank lines, a block the range only touches the edge of. Text, rule, text is
the two texts with one separator, which is what the reference copies
(`UnitModel.copy_text`: one `BLOCK_BREAK` before the later block; read, not
run). A range that starts or ends in such a block has no separator at that
end. A Host block WITH text gives it whole, behind a separator. Not the
reference's: between two blocks of one list the reference copies a single
newline, and the library always writes the one separator it is given (list
markers are not copied either; both were already known on your side).
Header: "WHAT A COPY GIVES". Python: `Document.range_text`.

**(b) A Mark over part of a Formula run.** A formula is one thing: a mark
takes it whole when the mark's range holds the formula's FIRST character
(the first character of a `RunFlags.Formula` run; the opening dollar of a
dollar formula) and takes none of it otherwise. So: a mark strictly inside
the source draws nothing; one over its first character alone marks all of
it; one from the text before into its middle marks that text and the whole
formula; one from its middle into the text after marks only the text after.
The same in all three places (Ink, Behind, Under), and the same rule for a
selection, its rectangles and a copy. For find: a match inside a formula's
source can be shown only by marking from the formula's first character.
Header: `ImTypesetTextRange`, `ImTypesetMark`.

**(c) A context whose ImGui context was destroyed first.** Run under
ASan + UBSan, no report in any case below.
- No ImGui context current, or another one: every draw, `add_font`,
  `remove_font` and widget is refused with `ErrorCode.NoContext` (Python:
  `Error` raised); layout, hit tests and `range_text` still answer.
  **One defect found and fixed:** `get_font` returned the dangling `ImFont`
  address; it now returns `None`.
- `destroy_context()` is safe then, and with no ImGui context at all. It is
  the one thing left to do with such a context.
- **A new ImGui context at the address of the destroyed one is taken for the
  old one.** An address is all `imgui.h` gives to know a context by, and the
  allocator reused it in every unsanitised run here. What then happens, in
  every case tested: each role is found without a font (its `ImFont` is not
  in the new atlas), text reports `NoFont` and draws nothing, the host's own
  fonts are untouched, and after `add_font` again the kept documents lay out
  and draw. That is observed, not promised: two collisions were reasoned
  about and not reproduced (a host font landing at an old font's address
  with the same `FontId`; the first call into the library in the new context
  falling on the same frame number with the same number of fonts). Closing
  them needs the library to read two more ImGui fields and the wheel probe to
  learn them: **queued**, not built.
- **The rule for `typesetter.load_fonts()`:** do not reuse a context you find
  (`current_context()` says one exists, never that its ImGui context does).
  Call `destroy_context()` whenever the ImGui context is destroyed, or, if
  that moment is not yours to see, at the top of `load_fonts()` before
  `create_context()`. Both are safe at any time.
Header: `CreateContext` ("LIFETIME"), `DestroyContext`, `GetFont`. Python:
`destroy_context`, `get_font`.

**(d) A Code block whose text ends in a blank line.** Positions and copies
end at the block's last character: the line end after it and blank lines at
the end are not part of the block. `range_text(((b, 0), (b, len(text))))` is
`text.rstrip("\r\n")`; no position the library answers is past that, and
one given past it is that end. A line of spaces is not blank; blank lines
before the last character are kept. **Found and kept, now stated:** the
bytes are held all the same — `GetDocumentBlockTextSize` (C/C++) counts them
— and when `append_text` puts text after them they are inside the block
again. So a Code block may be handed over with its closing newline, or
streamed line by line, and nothing needs stripping on your side.
Header: "POSITIONS", `ImTypesetBlockKind_Code`, `GetDocumentBlockTextSize`.

**(e) A refused import.** Reproduced: `imtypeset._capi` stayed in
`sys.modules`, and another copy of the package tried afterwards got the
first copy's `_capi`. **Fixed:** a refused import removes every
`imtypeset.*` module; a second attempt in the same process, with the
variable set right or another copy of the package in front, starts clean
(tested for three kinds of refusal). **Not undone, and cannot be portably:**
the shared object stays mapped once loaded, so its static initialisers have
run; loading it again later is harmless. The `.gate` stamp file beside a
bundle library is deliberate and stays.

**(f) Seven more fonts in the atlas.** The library cannot adopt a font the
host added: what it adds is a copy of the file with its character map
rewritten (every codepoint layout can name, the unencoded math variants
among them, gets a glyph), one OS/2 bit cleared and, for some fonts, glyph
boxes repaired. Adopting a host font soundly would mean proving, per font,
that the host's rasteriser maps every codepoint as the reader does, that it
sizes the font as layout measures it, that the host's `ImFontConfig` moves
no glyph, and it could never cover the math face; it would also read several
more ImGui fields through the wheel probe. **Declined as an adoption API;
the sharing goes the other way and works today:** the host draws its own
text with the library's font and does not add the file a second time.

    index = imtypeset.font_atlas_index(imtypeset.Face.Code)       # new
    font = imgui.get_io().fonts.fonts[index]                      # the wheel's ImFont object
    imgui.push_font(font, 0.0)

Run in both wheels. Measured (`tests/test_api_answers.cpp`, FreeType, the
seven test faces, 1.68 MB of files): the role's font and a host's copy of the
same file set the same words to the same width and height at 13, 16 and
24 px for the six text faces; a second copy of every face costs 1.85 MB of
ImGui allocations before a glyph is drawn and 1.88 MB more once the same
words are drawn in both, the atlas textures going from 1.84 MB to 3.15 MB.
Differences a host can see in the library's font: `OversampleH` is 1 (it
matters under stb_truetype only; not run under stb), and under FreeType a
font that asks for OS/2 typographic sizing is sized by `hhea` (the six text
faces here measure the same either way). For advtext after the swap: stop adding the seven faces
yourself; keep the one UI font, and take any role you want for ImGui text
from the library.

**(g) C1–C8.**

| | request | outcome |
|---|---|---|
| C1 | draw without finding the draw list | **done**: `draw(None, pos)`; see API changes |
| C2 | say how much the height above the window changed, or anchor; better estimates for prose | **queued**. It is a change to how unlaid blocks are estimated and to `DocumentStats`, in the sources `tables-core` rewrites next; it goes after tables, with your 244-of-815 figure as its test |
| C3 | find in the library, or marks from packed bytes | **in part**: `pack_marks` packs with `struct` (2.0 → 0.5 µs a mark for ASCII blocks, 2.5 → 1.0 otherwise, 2,000 marks, one run here). A `FindInDocument` is **declined for now**: find is advtext's by the border, and case folding is the host's policy |
| C4 | copying a range should not lay out the blocks inside it | **queued**. The copy is produced from the laid-out lines' model; a second way to produce it without lines must agree with the first to the byte (hard breaks, stand-ins, escaped dollars, formulas), which is a task with its own goldens, not a small change |
| C5 | tables | item 3 |
| C6 | line height and advances a host can match to ImGui's | **queued, not reproduced here**: the 8.01 px against 7.626 px was not re-measured, and which ImGui size was live in that pane is not known here. `Style.FontSize` (ImGui's meaning of size) against `EmSize` is the first thing to check on your side; not checked here |
| C7 | replacing the last block without the pass over all blocks | **queued** behind tables (same sources); your own measurement puts it below notice under 10,000 blocks |
| C8 | a tag separate from style, or overlapping runs allowed | **answered, no change**: runs do not overlap; a link over plain and bold text is two runs, each with the tag and its own face. The header says so at `ImTypesetRun::Tag`. Your converter already does this |

## Galleries (item 6)

The library's demo has the three pages advtext retires, as sections of
`ImTypeset::ShowDemoWindow()` written against `imgui.h` and `imtypeset.h` only:
**Specimen** (every face at one size with its set width, one face up the
modular scale, seven pairs kerned and letter by letter with the kern), **Formula
gallery** (the eleven plates of `advtext/ui/formulas.py`, same sources, panels
on one baseline, labels on the panels' columns) and **Math** (what
`pages/mathtext.py` showed: formulas in a paragraph, in a heading, a list and
a quotation, a stream fed three bytes at a time, the subset table and the list
of refusals with the reasons the library gives when the list is made).

**How advtext's demo reaches it**, inside the stock wheel, in either venv:

```python
import imtypeset                      # the sidecar, as before: rebuild it from this tree
from imtypeset import demo

demo.show_demo_window()               # the whole window, floating; (True) adds a close button
                                      # and returns whether it is still open

for section in demo.sections():       # or one section as a page of advtext's own demo
    name = demo.section_name(section) # "Specimen", "Formula gallery", "Math", ...
...
imgui.push_font(None, 19.0)           # a section sets its text at ImGui's current size
demo.show_demo_section(demo.DemoSection.Specimen)   # into the current window or child
imgui.pop_font()
```

`examples/python/bundle_demo.py` is both, runnable (`--show window|pages`),
and is what the pictures `docs/images/py-bundle-demo-*.png` were taken from in
advtext's venv (wheel 1.92.900) and pptmstr's (1.92.801). A section shown alone
keeps its state per imtypeset context and apart from the window's; it adds no
font and makes no context. The pages advtext deletes map to
`DemoSection.Specimen`, `DemoSection.Formulas` and `DemoSection.Math`; the other
eight sections can be listed or left out.

**What the old pages showed that is not there**, each because the public API
cannot say it (the full list with reasons is in
`.concerns/galleries--builder--result.md`): the debug overlay on the specimen
and the gallery (a document can be drawn with one, a paragraph and the widgets
cannot); a face's x-height, ascent and descent; "N codepoints have no glyph in
the math face" and "N glyphs have no raster"; a formula in a table cell (item
3); markdown in the source box (the box edits one formula; markup is
advtext's); captions set from markdown with bold and code spans (they are
plain text); the count of atoms and glyphs per pane. What a gallery cannot be
without advtext: nothing. The demo's stream is a paragraph re-given as bytes
arrive, not `advtext.stream`'s commit policies, which stay advtext's.
