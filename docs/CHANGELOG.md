# Changelog

One section per release tag, newest first; `docs/versioning.md` says what a
version promises. A section's entries say which kind each is: **new** (a name
added), **changed** (a name means something else, or text is set
differently), **fixed** (a defect, with what it did), **removed**. The release
notes on GitHub are the section, copied by `tools/ci/wheel_release.sh`.

## 0.4.1

The first release published on PyPI, as the distribution `imtypeset`. A patch by
`docs/versioning.md`: **no name and no layout differs from 0.4.0, and one meaning: a font whose glyph a side bearing moves out of range is now refused (below)**, which was
never published (its tag was not pushed). A library of 0.4.0 and a Python package of 0.4.1
still refuse each other, as any two versions do: they are one release.

- **new (packaging)**: wheels on PyPI for Linux x86-64 (`manylinux_2_28_x86_64`) and macOS on
  Apple Silicon (`macosx_14_0_arm64`), CPython 3.11 to 3.14, each for imgui-bundle 1.92.900
  (`imtypeset==0.4.1.192900`) and 1.92.801 (`imtypeset==0.4.1.192801`). Every one is built
  against the Dear ImGui headers of the fork its imgui-bundle wheel is built from, and is
  accepted by Dear ImGui's own layout check as well as by the field probe. There is no source
  distribution (`docs/releasing.md`).
- **new**: the licence is MIT (`LICENSE`; `docs/decisions.md`, D82). A wheel says so in its
  metadata and carries the file beside the third-party notices.
- **changed (packaging)**: a wheel's metadata has the project's URLs, classifiers and a
  description written for the project's page (`python/README-pypi.md`).
- **fixed (the build's own checks, not the library)**: the checksums of the fork's headers are
  recorded in `python/host/bundle_pins.toml`, so a release build is allowed and a fetched or
  cached header that differs is refused; the check of an installation expected a refusal on
  macOS that only a Linux library gives (on macOS `imtypeset` and `imgui_bundle` may be in two
  directories, `docs/sidecar.md`, section 9); a test cut a library "in half" where half of the
  file was all of what a loader maps.
- **fixed**: a `glyf` font whose `hmtx` left side bearing moves a glyph's outline past 16 bits,
  or further than sixteen heights from its origin, is refused when it is added
  (`ImTypesetError_FontRefused`, "a glyph's side bearing moves its outline ..."). Before, a
  596-byte font with one side bearing of 32,767 was accepted, and under FreeType with hinting
  its glyph came back as a bitmap 1,046 px wide at 16 px, which Dear ImGui's atlas answers with
  its "Out of texture memory" assertion. None of 2,991 faces of 2,925 installed fonts is
  refused by it. This is the one difference from 0.4.0 in what the library accepts.
- **fixed (the build, not the library)**: the tests and the drop-in file compile without a
  warning under Clang 18 (unused constants and variables, two sign conversions).

### The first run on macOS

On macOS 14 arm64 with Apple Clang every golden replay passed and three tests
failed. All three were faults of the tests, reproduced on Linux; the library is
unchanged by them (GitHub issues 1 to 3). The corrected tests have run on Linux
with GCC, Clang and libc++; their first run on macOS is the release's own CI.

- **fixed (tests, not the library)**: the four things the first run on macOS reported
  (`docs/decisions.md`, D84). The copy and select-all tests pressed Ctrl, which is not the
  shortcut's key on a Mac: `SelectableText()` and `DocumentView()` answer Cmd+C and Cmd+A
  there, as Dear ImGui's own text fields do, and the tests now press each platform's key under
  both settings of `io.ConfigMacOSXBehaviors`. The decimal-comma test compared a struct's
  padding bytes; no layout depends on the locale. Two glyph boxes of 7,510 differ under
  FreeType 2.14.3 from the reference's 2.13.3 (Inconsolata's `"` at 16 and 37 px, one pixel on
  one edge, no advance), and the test says which FreeType it ran under and what it asserted. A
  font fuzz wrote past the end of a file it had cut.
- **documented**: `include/imtypeset.h` says which key the selection shortcuts are on a Mac.

## 0.4.0

Never published: the tag exists in one checkout and was not pushed. A minor version by `docs/versioning.md`: two host-filled structs grew, a
library-filled one grew, a default changed, and text is set differently. **A library and a
package of 0.3.0 and of 0.4.0 refuse each other**, by version and by struct sizes.

### The grid, and what keeps it true (task `device-finish`, D79 to D81)

- **changed (text is set differently)**: text in a FIXED-PITCH face measured on the device
  (which Code is by default) is on a grid of cells, whatever it holds. A character borrowed
  from another face takes as many whole cells as hold it (an ideograph two) where it took its
  own advance; a control, format character or combining mark the face has no ink for takes none
  where it took the missing glyph's advance (and U+200B takes none even where the font gives it
  one); U+FFFD for a character no face has is the code face's own and one cell; a tab in a Code
  block counts cells; a wrapped code line hangs whole cells; the spaces of such text never
  stretch; a paragraph whose own face is such a face is ragged right where its style says
  Justify; a word of such text is drawn on the pixel nearest its place. Every advance on the
  device is a whole framebuffer pixel, for any character (D79).
- **fixed**: advances remembered from the atlas outlived the rasterisation they were read from
  when a host changed loader flags, the loader or a source in place; the entry asked for
  followed the density a font was last pushed at; a font with locked sizes answered another
  size's advance. They are checked once a frame, asked for at their own density, and held to
  their size (D80).
- **changed**: `DrawParagraph` REFUSES a paragraph holding device text that was laid out for
  another display density or atlas rasterisation (`InvalidArgument`: call `LayoutParagraph`
  again); it drew the old geometry (D81).
- **changed**: a document asked anything while its ImGui context is not current answers from
  the layout it has; it was laid out again by its fonts. `LayoutParagraph` there keeps what it
  measured. **new**: `ImTypesetDocumentStats::MeasuredWithoutDevice` (C
  `ImTypeset_DocumentStats`; Python `DocumentStats.MeasuredWithoutDevice`), last field (D81).
- **changed**: layout asks the atlas through `ImFontBaked::GetCharAdvance`; it no longer reads
  `ImFontGlyph::AdvanceX`. The probe keeps its 26 rows.
- **fixed**: two code cells of a table column laid out at one x could be drawn a pixel apart.

### What a character is measured by (task `device-advances`, requests H34 and H35)

- **new**: `ImTypesetAdvances_` (`Default`, `Font`, `Device`; C `ImTypeset_Advances_*`; Python
  `imtypeset.Advances`), `ImTypesetStyle::Advances` and `ImTypesetBlockStyle::Advances`, each
  the LAST field of its struct (C `ImTypeset_Style`, `ImTypeset_BlockStyle`; Python
  `Style(Advances=...)`, `BlockStyle(Advances=...)`). `Font` measures a face by its font file,
  as every face was measured before. `Device` measures it by the advance of the glyph in the
  entry of Dear ImGui's atlas it is drawn from, at the size and display density it is drawn at,
  rounded to a whole framebuffer pixel, with no kerns: such text is drawn exactly where it was
  laid out, in steps of one size, and on the pens of Dear ImGui's own text in the same ImFont
  at the same size wherever ImGui's advances are whole pixels (D77).
- **changed (text is set differently)**: with a style that says nothing (`Default`), text in
  the **Code and CodeBold faces is measured on the device**: a Code block, a Code-face run in
  prose, a paragraph whose face is Code. At a size where the font's advance is not a whole
  pixel a row of code is a different width than before (Inconsolata at a 16 px pixel height:
  8 px a character where it was 7.63), lines of code can break elsewhere, and the layout is
  for one display: it is made again when the display's density changes. Every other face is
  measured by its font as before. A host that wants the old layout of code says
  `ImTypesetAdvances_Font` in the style.
- **changed (where glyphs are drawn)**: text measured by its font has every glyph of a word
  within a pixel of its laid-out distance from the word's first glyph, so less than two pixels
  from where layout has it, however long the word (D78). Before, the middle of a long word or
  of a row of code could stand many pixels from the caret and the selection that answered for
  it (16.9 px in a row of 74 characters). Which gaps of a long word are a pixel wider or
  narrower changes; a word's width and the spaces between words do not.
- **new**: `GetDeviceGlyphAdvance(face, codepoint, style, scale)` (C
  `ImTypeset_GetDeviceGlyphAdvance`; Python `device_glyph_advance(face, char, style=None,
  scale=1.0)`): the advance layout uses for text of a style measured on the device, on the
  display as it is now. `GetGlyphAdvance` and `GetFontMetrics` answer the font's numbers, as
  before, whatever a style says.
- **changed**: the sidecar's field probe has a 26th row, `ImFontGlyph.AdvanceX`, which layout
  now reads. A package from this tree refuses a library from before it, and the other way
  about (the two structs' sizes differ as well).
- **new**: `tools/bundle_route/check_device_advances.py`, `bench/bench_code.cpp`, and a ruler
  set three ways in the demo's Known limits.

## 0.3.0

Not tagged yet. The first tag. It gathers everything since commit `5654eee`,
which called itself 0.2.0, as did every commit after it up to this one; the
tables per commit, with the C++, C and Python spelling of each entry and what
was measured, are in `docs/advtext-swap-status.md` ("API changes since
`5654eee`" and "In `74f5116`"), from which this section is drawn.

**A library and the Python package come from one release.** Every step below
that added a function or a struct makes a package from after it refuse a
library from before it, at import, with a sentence.

### At the gate before the tag (task `v030-gate`)

No declaration changed.

- **fixed**: the bar of an `Under` mark was not drawn where it hung into the
  clip rectangle from a line above it: in a block of more than sixteen marks
  when its `Offset` was more than about an em, and at any number of marks when
  its line was in a block, or a table row, wholly above the clip (a thick bar
  at the library's own offset was enough). A bar is now drawn wherever it is
  inside the clip; a draw looks above the clip as far as the lowest bar among
  its marks can end, so an `Offset` of many lines costs every draw that many
  lines (D76).
- **changed (text)**: two sentences of the demo's Specimen and Formula gallery
  that said the library answers nothing about a face name `GetFontMetrics`
  and `FaceHasGlyph` instead.

### Dense displays (task `hidpi`, request H19)

- **changed**: on a display whose framebuffer is denser than ImGui's
  coordinates (`io.DisplayFramebufferScale` above 1: macOS Retina) every draw
  places glyphs, rules and rectangles on the framebuffer's pixels and
  rasterises the glyphs at the framebuffer's size. Before, they were bitmaps
  of density 1 stretched, on whole units. No call changed and none was added;
  layout, sizes and hit tests are the same; at a scale of 1 the vertices are
  the same floats (D72). A vertex can now be at a fraction of a unit.
- **changed**: the sidecar's field probe has a 25th row,
  `ImGuiIO.DisplayFramebufferScale`. A package from this tree refuses a
  library from before it, and the other way about.
- **new**: `examples/python/hidpi_check.py`, the check a person at a dense
  display runs.

### The Python distribution (task `py-dist`)

- **new**: imtypeset installs as a wheel with its library inside:
  `import imtypeset` with no environment variable. One wheel per platform,
  CPython version and imgui-bundle version, pinned to that imgui-bundle
  exactly; the wheel's version is `0.3.0.<imgui-bundle line>`
  (`docs/sidecar.md`, section 8).
- **changed**: the import gate. It compares the library and the wheel's module
  by content with a record written when the wheel was built, and no longer
  starts a child interpreter or writes `<library>.gate`; it works in a packaged
  application (`docs/sidecar.md`, section 9). The full probe in a child
  remains, in a source checkout only, for a library named by
  `IMTYPESET_LIBRARY` that has no `<library>.attestation.json` beside it, and
  for `IMTYPESET_GATE=probe`.
- **changed** (task `gate-fixes`, D73): an installed package reads no
  `IMTYPESET_LIBRARY`, `IMTYPESET_GATE` or `IMTYPESET_PRELOAD_BUNDLE`;
  `host_info()` gains `"ignored_environment"` and `"checked_by"`.
  `host_info()["route"]` has a third value,
  `"IMTYPESET_LIBRARY (build-info check only)"`.
- **changed** (D73): in a checkout, a library with no attestation is loaded
  and probed in a child before the import loads it, and refused (`unchecked`)
  where no child can be started.
- **changed** (D75): a stand-in wheel's version ends `.dev0+standin` (it was
  `+standin`, which ordered above the release of its line).
- **changed** (D74): a file cut short anywhere is `damaged`; an attestation of
  another shape is `damaged`; any other error while checking is `ImportError`
  kind `unchecked`.
- **changed**: a refused import is still an `ImportError`, and its text is
  different: the first line is a sentence without a path, the detail follows
  on later lines, and the error has `kind`, `sentence` and `detail`.
- **new**: `host_info()` has `route` and `distribution_version`, and its
  `bundle` report has `built_for`, `stand_in`, `second_module_check`,
  `preload` and `attestation`. `checked_in` is now `"attestation"` or
  `"child"` (it was `"process"` or `"child"`).
- **new**: the library says which imgui-bundle it was built for
  (`ImTypesetBundle_BuiltFor`; `host_info()["bundle"]["built_for"]`).
- **changed**: `python/host/build_bundle_library.sh` writes
  `<library>.attestation.json`; copy it with the library.
  `IMTYPESET_BUNDLE_RPATH` is new. The package imports `imgui_bundle` before it
  loads the library.
- **changed**: the version is 0.3.0.

### From `74f5116` (task `handoff-core`)

- **new**: `font_metrics(face, em_size)` and `FontMetrics`,
  `glyph_advance(face, em_size, char)`; `Document.append_table_cells(cells)`.
- **changed**: which glyphs a clip rectangle gets. A quad is emitted exactly
  when part of it is inside the clip from top to bottom; a glyph taller than
  its font's cell is now drawn by the rows inside. Vertex counts under a clip
  that cuts such a glyph go up.
- **changed, same picture**: marks may be handed over in any order, all of
  them every frame; marks on lines not shown emit nothing.
- **changed**: what Python raises. A public call raises `imtypeset.Error`,
  `TypeError`, `ValueError` or `OverflowError` and nothing else; an index past
  a C int is clamped where it used to wrap (`docs/python-errors.md`).
- **fixed**: `Document.view("")`, and the demo outside a frame, ended the
  process; they draw, and draw nothing.
- **changed (look)**: the demo's Math table and counters.

### From `c9f8902`

- **fixed**: after an allocation failure partway through `set_blocks`, the next
  `set_blocks` wrote past the end of an array.

### From `7a7bc36` (task `tables-fixes`)

No declaration changed.

- **changed**: a point in a table cell's rectangle hit-tests to that cell
  (its blank and padding included); a range's rectangles in a table are one a
  cell and line, inside the cell's text; in columns, the page is as tall as its
  tallest overfull column, `Overfull` is never set under `Balance`, and a
  column no longer ends between a table's header and its first row, after a
  heading, or inside a wrapped row.
- **fixed**: the caret in a table (one line tall, at the cell's own end);
  `FixedLineHeight` on a table (per line); ink overhanging a fixed line under
  a clip; held widths after the type changes; a table's estimated height
  before layout; the cost of appending a row (it was that of the table);
  caret and range rectangles in columns depending on what was asked before;
  the height a view lent to `set_columns`; cuts inherited across an edit.

### From `92847e6` (task `swap-fixes`)

No declaration changed.

- **fixed**: a role's font is the library's only while it carries the
  library's mark (it could name a font of a destroyed ImGui context).
- **changed**: the name ImGui shows for a font added here is the first 31
  bytes of the file's name (was 39).
- **fixed**: a refused `importlib.reload(imtypeset)` leaves the package as it
  was.
- **changed**: the demo keeps its state for 32 contexts (was 8).

### From `0b21807` (task `columns-core`)

- **new**: columns. `Document.set_columns(Columns(...))`, `ColumnFlags`,
  `column_count()`, `column(k)` and `ColumnGeometry`, `column_pos(point)`;
  draw, hit test, selection, rectangles, caret, size and the view work in
  columns; two counters in `DocumentStats`.

### From `be40444` (task `tables-core`)

- **new**: `BlockFlags.FixedLineHeight`; `face_has_glyph`; `Hit.tag`.
- **new**: tables. `BlockKind.Table` blocks with `Cell`, `TableColumn`,
  `BlockFlags.TableHeader` and `TableHeld`, three options in
  `DocumentOptions`; `append_table_rows`, `set_table_columns`;
  `table_geometry`, `table_column`, `table_row`; `table_cell`,
  `table_position`; `DocumentHit.row` and `.column`; two counters.
- **changed**: `Hit` is one field longer and `DocumentHit` two: unpack by name.
- **changed**: a `BlockKind.Table` block is a table, and copies as its cells
  (tab between cells, newline between rows).
- **changed**: a glyph whose quad is wholly at or below the clip's bottom
  edge, or wholly above its top, is no longer emitted.
- **fixed**: `DocumentHit.tag` answered the run of the character before the
  caret.

### From `12d77ac` (tasks `swap-api-answers`, `galleries`)

- **new**: `font_atlas_index(face)`; `imtypeset.demo` (`show_demo_section`,
  the section names); the demo window in the sidecar.
- **changed**: `draw(None, pos)` draws into the current window's draw list
  (it was refused); `get_font()` is `None` when the ImGui context is gone (it
  was a dangling address); a refused `import imtypeset` leaves no
  `imtypeset.*` in `sys.modules`.
