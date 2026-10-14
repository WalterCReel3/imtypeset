# patch-python (builder): H28, H36, H46 for 0.4.3

## Changed

- `python/imtypeset/__init__.py`
  - H28: `Document` has a class attribute `edit_error: ErrorInfo = _NO_ERROR`; the
    assignment in `__init__` is kept (a document's own value shadows it).
  - H36: `Document._pack` wraps both the fast path and the checked path; a `TypeError`
    or `UnicodeEncodeError` leaving either goes to `_name_the_block`, which walks the
    description block by block (`_block_faults`, `_run_faults`, `_fault`) for the first
    value raising the same class and raises that class again with the place:
    `blocks[2].text`, `blocks[1].runs[3]`, `blocks[0].rows[1][0].text`,
    `blocks[4].columns[0]`. Index = into the sequence given (so for `replace_blocks` it
    is the given list's index, not the document's). TypeError: `"<place>: <old message>"`;
    UnicodeEncodeError: same encoding/object/start/end, reason + `" (<place>)"`;
    `raise ... from` the original. Nothing found (styles holding a dict, a generator):
    raised as before. A lone surrogate still raises. The fast path costs nothing new.
- `tests/python/test_binding.py`: `Documents.test_edit_error_is_on_the_class`,
  `Documents.test_a_description_that_cannot_be_packed_names_the_block` (11 cases, fast
  and checked paths, replace_blocks, non-block TypeError unchanged, generator, the
  surrogate recipe), class `Environment` (two tests, below); a `TABLE` row for
  `Document.edit_error` and `edit_error` added to `CLOSED_ANSWERS` (HostileInput
  requires both for any public name on the class).
- `docs/python-errors.md`: the surrogate recipe corrected; "Where, in a description of
  blocks"; a sentence that `edit_error` is on the class.
- `docs/sidecar.md` section 9: "When they are read" paragraph (H46).

## Ran

- Surrogate recipes, CPython 3.11.2 (`../imgui-adv-text/.venv`), on
  `"a"+chr(0xD800)+"b"+chr(0xD83D)+chr(0xDE00)+"\U0001F600"`:
  `encode("utf-8","replace").decode()` -> `a?b??\U0001f600` (the reviewer was right: `?`);
  `encode("utf-16","surrogatepass").decode("utf-16","replace")` -> joins the adjacent
  pair into U+1F600 (not one for one); `re.sub("[\ud800-\udfff]", "�", t)` ->
  `a�b��\U0001f600`, same length. The doc gives the last.
- `python-host` preset configured into `build/patch-python`, `imtypeset_pyhost` built
  (Debug, self-hosted ImGui 1.92.9, FreeType); `tests/python/test_binding.py` 107 tests
  OK; `test_demo_sections.py` 4 OK. With HEAD's `__init__.py` swapped in, the new tests
  fail 15 subtests (all H28/H36 ones), so they hold the change; restored after.
- H46: `Environment.test_no_variable_is_read_after_import` replaces `os.environ`
  after import with a recording mapping holding wrong values for all five variables;
  host_info(), a Document (prose + table) laid out and drawn, `text`, a Paragraph,
  three frames: same host_info, zero IMTYPESET_* names asked. Passes on HEAD too (it
  confirms, it fixes nothing). `test_every_read_of_the_environment_is_one_made_at_import`:
  AST scan of `python/imtypeset/*.py`, the set of functions touching `environ`/`getenv`
  must equal six listed ones.
- _bundle.py reads confirmed import-time by reading the call graph:
  `mode` (IMTYPESET_GATE, ~1382) <- `precheck` <- `_load_checked`; `_a_font_file`
  (IMTYPESET_GATE_FONT, ~621) <- `_probe` <- `check` in the child started by
  `probe_in_child` during import, or `attest` (a build tool). Also `check` reads
  IMTYPESET_BUNDLE_STRICT, `open_library` IMTYPESET_PRELOAD_BUNDLE, `_capi.library_route`
  IMTYPESET_LIBRARY: all under `_load()`.

## Not verified

- No run of the bundle route (imgui_bundle wheel) with the environment replaced after
  import; that route's reads are import-time by reading only.
- No release/ASan build of the host library for these tests; only Debug.
- `append_text`, `append_table_rows`, `append_table_cells` messages are unchanged (they do
  not go through `_pack`, and have no block index to name; out of the spec).

## Status-file rows (docs/advtext-swap-status.md)

| H28 | `edit_error` as a class attribute | **done in 0.4.3**: `Document.edit_error` is `ErrorInfo(0, "", -1, -1)` on the class; a document's own value after each edit as before. `hasattr(imtypeset.Document, "edit_error")` needs no document (`tests/python/test_binding.py`, `test_edit_error_is_on_the_class`) | done (0.4.3) |
| H36 | `ValueError` naming block and field | **done in 0.4.3** (with D70 in `74f5116`): a `TypeError` or `UnicodeEncodeError` from `Document.from_blocks` / `set_blocks` / `replace_blocks` names the place, `blocks[i].field` with the run, cell or column (`blocks[1].rows[1][0].text`), index into the sequence given; same class as before. A lone surrogate is still refused, never replaced. `docs/python-errors.md` gives `re.sub("[\ud800-\udfff]", "�", text)` for U+FFFD one for one — RUN: the old recipe gave `?`, and the UTF-16 surrogatepass one joins adjacent halves | done (0.4.3) |
| H46 | when the environment is read | **answered and tested in 0.4.3:** during `import imtypeset` and never after. Every read is under `_load()` (`docs/sidecar.md` section 9 names the six functions); `tests/python/test_binding.py` class `Environment` runs host_info, a document, a paragraph and frames with `os.environ` replaced after import and no variable is asked for, and fails on any new read outside the six. A host may remove the variables once the import returns. Self-hosted library run; the bundle route by reading | answered |

## CHANGELOG lines (for `## 0.4.3 (in progress)`; the heading did not exist when I finished)

- **fixed** `Document.edit_error` is on the class as well as on each document
  (`ErrorInfo(0, "", -1, -1)`), so a host can check for the name without making a
  document (H28).
- **fixed** A `TypeError` or `UnicodeEncodeError` from `Document.from_blocks`,
  `set_blocks` or `replace_blocks` says where: `blocks[2].text`, `blocks[1].runs[0]`,
  `blocks[0].rows[1][0].text`, indexing the sequence given. The class is unchanged; a lone
  surrogate is still refused (H36).
- **fixed** (documentation) `docs/python-errors.md`'s recipe for lone surrogates gave `?`;
  it now gives U+FFFD one for one, and says it was run (H36).
- Documentation: the package reads its environment variables during import and never
  after; `docs/sidecar.md` section 9, held by a test (H46).
