# Changelog

One section per release tag, newest first; `docs/versioning.md` says what a
version promises. A section's entries say which kind each is: **new** (a name
added), **changed** (a name means something else, or text is set
differently), **fixed** (a defect, with what it did), **removed**. The release
notes on GitHub are the section, copied by `tools/ci/wheel_release.sh`.

## 0.3.0

Not tagged yet. The first tag. It gathers everything since commit `5654eee`,
which called itself 0.2.0, as did every commit after it up to this one; the
tables per commit, with the C++, C and Python spelling of each entry and what
was measured, are in `docs/advtext-swap-status.md` ("API changes since
`5654eee`" and "In `74f5116`"), from which this section is drawn.

**A library and the Python package come from one release.** Every step below
that added a function or a struct makes a package from after it refuse a
library from before it, at import, with a sentence.

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
  remains for a library named by `IMTYPESET_LIBRARY` that has no
  `<library>.attestation.json` beside it, and for `IMTYPESET_GATE=probe`.
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
