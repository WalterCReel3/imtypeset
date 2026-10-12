# For whoever takes `py-dist`: what `handoff-core` left in `python/imtypeset` (builder-35, 2026-10-11)

`post_concern` had no agent named `py-dist` when this was written, so it is a file.
`python/imtypeset` is yours from here; I have stopped writing in it.

- **New public names** in `__all__`: `font_metrics`, `glyph_advance`, `FontMetrics`; new method
  `Document.append_table_cells`. `_capi.py` declares `ImTypeset_GetFontMetrics` and
  `ImTypeset_GetGlyphAdvance`, so the package refuses a library built before this tree (it lacks
  two functions the package declares). A compatibility rule should go on saying that a package
  and a library come from one commit or tag.
- **Unchanged:** every struct the version check compares, `ImTypeset_BuildInfo`, `_bundle.py`,
  the gate's 24 probe rows.
- **Behaviour a version note should carry:** an int past a C int is clamped where it used to
  wrap; only `Error`, `TypeError`, `ValueError` and `OverflowError` leave a public call
  (`docs/python-errors.md`).
- **A test that will stop you:** `HostileInput` in `tests/python/test_binding.py` FAILS if a
  public name is added without a row in its `TABLE` and a mention in `docs/python-errors.md`
  (and, for a `Document` or `Paragraph` method, an entry in `DOCUMENT_CALLS` /
  `PARAGRAPH_CALLS`). Add those when you add a name, a version attribute included.
- `python/imtypeset/demo.py`: two lines of argument conversion changed.
- RAN on the tree as left: `python_binding` 101 tests, `python_demo_sections` 4, in a debug
  build with the Python host; both sidecars rebuilt with stand-in headers and accepted.

The whole list is `.concerns/handoff-core--builder--result.md`, section 2.
