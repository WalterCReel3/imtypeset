# What the Python package raises on input a host controls

For whoever calls `python/imtypeset` from a program of their own and wants to
validate at their own boundary. It answers H12 of advtext's hand-off list.

**How this was produced.** Run, not read. Every public call was made with each
of its arguments, and each value inside a tuple or a list it takes, replaced in
turn by 42 hostile values (`None`, `True`, ints from `-2**70` to `10**400`,
`1.5`, `nan`, `inf`, `1e300`, `""`, `"\x00"`, a lone surrogate, a 2 MB `str`,
`bytes`, `[]`, `()`, tuples of the wrong length and of the wrong things, `{}`,
an `object()`, a generator, a list of 200 000, a `Style`): about 47 000 calls,
on CPython 3.11.2 against `libimtypeset_pyhost.so` (imtypeset 0.2.0, C ABI 1,
self-hosted Dear ImGui 1.92.9 with FreeType, a Debug build: ImGui's assertions
abort, and the library does not catch exceptions), Linux x86-64, headless.
What this file says is held by `HostileInput` in
`tests/python/test_binding.py`: a table of rows, each provoked and compared
with the exact class, and the test fails if a public name is missing from its
table or from this file. `font_metrics`, `glyph_advance` and
`Document.append_table_cells` were added after the sweep: their rows are in the
test's table and were written by hand from the same list of values, not by the
sweep.

## The rule

**`imtypeset.Error`** is the library speaking: the call reached it and it did
nothing, and `code`, `message` and the position say why (no context, a font
refused, a file not there, an index that names no table, an ID that is empty).
What the library did *differently* is not raised at all: the call returns
`False` and `last_error()`, `Paragraph.error` or `Document.edit_error` say what.

**Anything else is the package refusing a Python value before the library is
called**, and it is one of three built-in classes and no other:

| class | means | examples |
|---|---|---|
| `TypeError` | the value is of the wrong kind | `None` or `bytes` where a `str` goes, `None` for a number, a number where a tuple goes, a tuple with a value too many, a `str` for an address |
| `ValueError` | the kind is right, the value cannot be meant | a `str` with a lone surrogate (`UnicodeEncodeError`, which is a `ValueError`), `"x"` or `nan` where an int goes, a pair of the wrong length, a closed handle, rows on a block that is no table |
| `OverflowError` | a number no C number is near | `inf` where an int goes, an int past every float (`10**400`) where a length goes |

No `struct.error`, `ctypes.ArgumentError`, `AttributeError`, `KeyError`,
`IndexError` or `MemoryError` leaves a public call on any input tried; before
this audit all six did. `except (TypeError, ValueError, OverflowError)` around
a call is the whole of it, and a host that checks the kinds in the next table
at its boundary meets none of them.

## By kind of argument

"Where" is the step that raises: *conversion* (`int()`, a float, unpacking),
*encoding* (`str` to UTF-8), *shape* (a tuple or a sequence of the wrong
length or kind), *state* (the handle, the document as it stands).

| kind | input that provokes | raises | where | a host avoids it by |
|---|---|---|---|---|
| **text**: a `str` (text, `str_id`, a marker, a cell, a separator) | `None`, `bytes`, a number | `TypeError` | conversion | passing `str`; decoding bytes itself |
| | a lone surrogate (`"\ud800"`) | `UnicodeEncodeError` | encoding | replacing them first: `text.encode("utf-8", "replace").decode("utf-8")` |
| | `""`, a NUL inside, 2 MB | nothing (text holds its NULs; an ID or a file name ends at the first) | | |
| **index**: a block, line, row, column, offset, count, face, enum member | `None`, a list | `TypeError` | conversion | passing an `int` |
| | `"x"`, `nan` | `ValueError` | conversion | |
| | `inf` | `OverflowError` | conversion | |
| | negative, past the end, past a C int | nothing: clamped (below) | | |
| **flags**, a run's tag | as an index | as an index | conversion | |
| | past 32 bits | nothing: the low 32 bits | | keeping tags within a C int |
| **length**: a width, a height, an extent, a scale | `None`, `"200"` | `TypeError` | conversion | passing a `float` |
| | an int past every float | `OverflowError` | conversion | |
| | `nan`, `inf`, negative, `1e300` | nothing: the library's default or its clamp | | |
| **point**, rectangle: `(x, y)`, `(x0, y0, x1, y1)` | `None`, a number, a value too many, `("a", "b")` | `TypeError` | shape | a tuple of floats of the right length |
| | `(10**400, 0)` | `OverflowError` | conversion | |
| | too FEW values | nothing: the missing ones are 0 | | checking the length |
| **selection** of a paragraph: `(start, end)` | a number, `(None, 1)` | `TypeError` | shape, conversion | a 2-tuple of ints |
| | `()`, `(1,)`, `{}`, `("x", 1)`, `(nan, 1)` | `ValueError` | shape, conversion | |
| | `(inf, 1)` | `OverflowError` | conversion | |
| **position** in a document: `(block, offset)`; a range is two | a number, `(None, 0)`, `(0, 1.5)`, `(0, 1)` for a range | `TypeError` | shape, conversion | 2-tuples of ints (a float is refused, not rounded) |
| | one value, three values, a `str` | `ValueError` | shape | |
| **address**: `draw_list`, `ctx` | a `str`, `bytes`, a float, anything not an int | `TypeError` | conversion | passing what the host's ImGui gave, or `None` |
| | an int that is not such an address | **the process ends** (below) | | never making one up |
| **struct**: `style`, `options`, a list of styles or containers | anything but the class asked for | `TypeError` | conversion | constructing `Style`, `BlockStyle`, `Container`, `DocumentOptions` |
| **sequence** of runs, blocks, rows, cells, columns, marks | `None`, a number, a generator | `TypeError` | shape | passing a `list` or a `tuple` |
| | an element `None`, a tuple too short or too long | `TypeError` | shape | building them with `Run`, `Block`, `Cell`, `TableColumn`, `Mark` |
| | an element that is a `str` | `ValueError` or `TypeError` | shape | |
| | a field of an element | as its kind above; in a document `Run.start`, `Run.end`, a colour and a mark's positions must be `int` (`TypeError` for a float) | conversion | |
| **handle** (`Paragraph`, `Document`) | closed, or its context destroyed | `ValueError` | state | `closed`; never keeping one across `destroy_context` |

## By call

Every public name. "—" is a call that takes nothing from the host and raised
nothing. Methods of `Paragraph` and `Document` all raise `ValueError` on a
closed handle, except `Paragraph.close`, `Paragraph.closed`, `Document.close`,
`Document.closed`, `Document.from_blocks` and `Document.pack_marks` (and
`len(document)` and `Document.edit_error`, which answer what was last known).

| call | kinds of its arguments | and |
|---|---|---|
| `text`, `calc_text_size` | text, flags, length (`wrap_width`), struct | |
| `text_with_id` | text ×2, flags, length, struct | an empty `str_id` is taken |
| `selectable_text` | text ×2, flags, length, struct, selection | an empty `str_id`: `Error` (InvalidArgument) |
| `codepoint_to_byte_offset`, `byte_to_codepoint_offset` | text, index | |
| `add_font` | index (`face`, `face_index`), text or `bytes` | an int for `source`: `TypeError`; a role or a file that is not there, a font refused: `Error` |
| `remove_font`, `get_font`, `font_atlas_index` | index | a role that is not there: `False` / `None` |
| `face_has_glyph` | index, one character or an int | `""`, `"ab"`: `TypeError`; an int that is no codepoint: `False` |
| `font_metrics` (answers a `FontMetrics`) | index, length (`em_size`) | a role that is not there or has no font, a size that is not a finite number above zero and at most 1,000,000 (`nan`, `inf`, `0`, `1e300`): `Error` |
| `glyph_advance` | index, length (`em_size`), one character or an int | as `font_metrics`; `""`, `"ab"`: `TypeError`; an int that is no codepoint: the missing glyph's advance |
| `set_text_cache_limits` | index ×2 | |
| `set_style` | struct | |
| `set_current_context`, `destroy_context` | address | |
| `col32` | ints | `None`, a float: `TypeError`; an int past a byte keeps its low byte |
| `create_context`, `current_context`, `get_context_serial`, `get_style`, `stats`, `last_error`, `library`, `build_info`, `version`, `host_info`, `show_demo_window` | — | `show_demo_window(open)` takes any value for its truth |
| `demo.section_name`, `demo.show_demo_section` | index | a value that is no section: `""`, nothing drawn |
| `demo.sections`, `demo.show_demo_window` | — | |
| `Paragraph`, `Paragraph.set_text` | text, sequence of `Run`, flags | |
| `Paragraph.layout` | length, struct | |
| `Paragraph.line` | index | a line that is not there: all zero |
| `Paragraph.draw` | address, point, selection | |
| `Paragraph.draw_to_buffers` | point, rectangle, selection | |
| `Paragraph.hit_test` | point | |
| `Paragraph.select_range` | point ×2, index (`granularity`) | |
| `Paragraph.range_rects`, `Paragraph.range_text` | selection | |
| `Paragraph.caret_rect` | index | |
| `Paragraph.close`, `Paragraph.closed`, `Paragraph.error`, `Paragraph.status`, `Paragraph.size`, `Paragraph.line_count`, `Paragraph.lines`, `Paragraph.formulas` | — | |
| `Document` | — | |
| `Document.from_blocks`, `Document.set_blocks` | sequence of `Block`, of `BlockStyle`, of `Container` | rows or columns on a block that is no table, a table's `text` that is not its rows': `ValueError` |
| `Document.replace_blocks` | index ×2, then as `Document.set_blocks` | blocks that are not all in the document: `ValueError` (state) |
| `Document.append_text` | text, sequence of `Run` | a document with no block: `ValueError` (state) |
| `Document.append_table_rows` | sequence of rows of cells | no block, or the last is no table: `ValueError` (state); a row that is a `str`: `TypeError` |
| `Document.append_table_cells` | sequence of cells | no block, or the last is no table: `ValueError` (state); a `str` or one `Cell` where the sequence is wanted: `TypeError`; no cells: nothing is done |
| `Document.set_table_columns` | index, sequence of `TableColumn` | a block that is no table: `Error` |
| `Document.set_block_extent` | index, length | a block that is not there: `Error` |
| `Document.set_options` | struct or `None` | |
| `Document.layout` | length, struct | |
| `Document.layout_window` | length ×2 | |
| `Document.block_error`, `Document.block_geometry`, `Document.table_geometry`, `Document.column` | index | what is not there: all zero |
| `Document.block_line`, `Document.table_column`, `Document.table_row` | index ×2 | the same |
| `Document.table_position` | index ×4 | |
| `Document.pack_marks` | sequence of `Mark` | |
| `Document.draw` | address, point, range, marks | |
| `Document.draw_to_buffers` | point, rectangle, range, marks | |
| `Document.view` | text (`str_id`), point (`size`), range, marks, struct | an empty `str_id` is taken: the view's own ID |
| `Document.hit_test`, `Document.column_pos` | point | |
| `Document.select_range` | point ×2, index | |
| `Document.range_rects` | range | |
| `Document.caret_rect`, `Document.table_cell` | position | |
| `Document.range_text` | range, text or `None` (`separator`) | |
| `Document.set_columns` | a `Columns`, its four values, or `None` | a number: `TypeError`; fields as index, length, length, flags |
| `Document.close`, `Document.closed`, `Document.block_count`, `Document.style_count`, `Document.container_count`, `Document.size`, `Document.stats`, `Document.column_count` | — | |

**The types.** Constructing one validates almost nothing; what is in it is
checked when it is handed to a call above.

- The NamedTuples `Run`, `Line`, `Hit`, `Formula`, `Stats`, `TableColumn`,
  `Cell`, `Block`, `Position`, `DocumentRange`, `Mark`, `DocumentHit`,
  `BlockGeometry`, `TableGeometry`, `TableColumnGeometry`, `TableRowGeometry`,
  `CellPosition`, `Columns`, `ColumnGeometry`, `DocumentStats`, `BuildInfo`,
  `ErrorInfo`: `TypeError` for a value too many, a required one missing or a
  keyword that is no field. Any value is taken for any field.
- The enums `Face`, `Align`, `Spacing`, `LineBreak`, `Granularity`,
  `ErrorCode`, `BlockKind`, `ContainerKind`, `CodeOverflow`, `MarkPlace`,
  `demo.DemoSection`: `ValueError` for a value with no member (`Face(99)`,
  `Face("x")`). The flags `TextFlags`, `RunFlags`, `BlockFlags`,
  `ContainerFlags`, `ColumnFlags`, `StatusFlags`: `ValueError` for what is not
  an int; an int with bits no flag has is kept. No call needs the enum: the
  plain int does, and an int with no member is the library's to answer.
- The structs `Style`, `BlockStyle`, `Container`, `DocumentOptions`:
  `TypeError` for a field given the wrong kind (`None`, a `str`, a float for an
  int field), at construction and at assignment; `OverflowError` for an int
  past every float in a float field. **A keyword that is no field is taken
  silently and does nothing** (`Style(Fontsize=14.0)`): ctypes' behaviour, and
  the one thing here a host can get wrong without being told. An int past a C
  int keeps its low 32 bits.
- `Error` is raised by the package and not constructed by a host; given
  anything but an `ErrorInfo` it raises `AttributeError`.

## What the package clamps instead of raising

Each was `OverflowError`, `struct.error` or, worse, a silent wrong answer
(ctypes keeps the low 32 bits of an int, so block `2**32 + 1` was block 1).

| what | now | why |
|---|---|---|
| an index, count, face or enum member past a C int, in every call | the nearest C int | it then names nothing, and the library answers as for any block, line or role that is not there; wrapped, it named another one |
| `Block.kind`, `style`, `level`, `quote_depth`, `container`, `item`; `Run.face`; `TableColumn.align`; `Mark.place` and a mark's positions, past a C int | the nearest C int | the same: the library does with it what it does with any value that names nothing (a kind, a container, a style where there are styles: set without it, and `DescriptionCut`) |
| `Block.flags`, `Run.flags`, `Run.tag` past 32 bits in a document | the low 32 bits | what `Paragraph.set_text` always did with them; a tag is the host's own number and is not moved to another one's |
| a colour (`Run.color`, `Mark.color`) outside 32 bits in a document | the low 32 bits | as `col32` does a channel, and as a paragraph's run did |
| `Block.left_indent`, `extent`, `Run.scale`, a column's `width` and `share`, a mark's `thickness` and `offset` too large for a C float (`1e300`) | infinity | what ctypes makes of the same number in every other call; the library has its own answer for a length that is not finite |
| `Paragraph` offsets on ASCII text past a C int | the end of the text | they were clamped at zero only |

Not clamped, on purpose: `None` or a `str` for a number, `bytes` for a `str`, a
float for a document offset, a tuple of the wrong length. Those are a host's
mistake and stay loud. `Document.replace_blocks` still raises `ValueError` for
blocks that are not all there: an edit applied to other blocks than the host
meant is worse than one refused.

One thing is refused by the package that the process would not have survived:
an **int as `add_font`'s source** (`TypeError`; `bytes(n)` is `n` zero bytes,
2 GB of them for `2**31`).

Three inputs that ended the process when this table was first made are now
answered by the library itself (task `handoff-core`; `tests/test_api_handoff.cpp`
and the rows of the test's table): an **empty `str_id`** for `Document.view`
(Dear ImGui asserts on a child window with no ID; the view now takes its own),
and **`show_demo_window`, `demo.show_demo_window` and `demo.show_demo_section`
called outside a frame** (an assertion in `Begin`, and a read of a window that
is not there; they now draw nothing before the first frame, after `render()`
and with no Dear ImGui context). Between `end_frame()` and the next
`new_frame()` with no `render()` the library cannot tell where it is: that
state still ends the process, for these and for every widget and draw.

## What ends the process

Observed, each in a child process. None raises; none can be caught.

| call | input | what happens | avoidable in the package? |
|---|---|---|---|
| `Paragraph.draw`, `Document.draw` | `draw_list` an int that is not an `ImDrawList*` | segmentation fault | no: an address is an address. A `str` or `bytes` there did the same and is now `TypeError` |
| `set_current_context`, `destroy_context` | an int that is not a context | segmentation fault | no, short of the library keeping a list of its contexts |
| `destroy_context(ctx)` | the same address twice | abort (double free) | the same. `destroy_context()` with no argument is safe twice |
| any widget, any draw, `show_demo_window`, `demo.show_demo_section` | called after `imgui.end_frame()` with no `imgui.render()` | abort or segmentation fault inside Dear ImGui (read from `docs/decisions.md`, D56; not run again for this table) | no: `imgui.h` gives no sign of that state |
| `Paragraph.layout(document, …)`, any method called unbound on the other class | a handle of the other kind | segmentation fault | not without a check in every method; no host does this by accident |

## Not covered

- Any library but the one above: no release build (where ImGui's assertions
  are compiled out and the abort cases become something else), no library
  that catches exceptions, no Dear ImGui Bundle pairing, no platform but Linux.
- Text of 2 GiB or more in one call or one document: sizes are C ints.
- Two arguments hostile at once, beyond what the nested values gave.
- Threads. The package and the library are for one.
- `imtypeset._capi` and `imtypeset._bundle` are not for a host. Importing the
  package raises `ImportError` and nothing else when it refuses: no library
  (a source checkout with `IMTYPESET_LIBRARY` unset), not a file, not an
  imtypeset library, not the one the package was written for, not built
  for the imgui-bundle that is running, or not checked and not checkable
  here. The error's first line is a sentence an application can show, and it
  carries `kind`, `sentence` and `detail`; `docs/sidecar.md`, section 9, lists
  every kind and was run for the installed package (section 12). **Any other
  exception raised while the package checks its library is caught and raised
  as an `ImportError` with kind `unchecked`**, the exception's name on its
  second line. Run (2026-10-12): an attestation whose `library`, `module` or
  `platform` is not an object, whose `probes` are not `[name, passed,
  detail]` rows, or that is not an object at all, is `damaged` (26 shapes in
  `tools/ci/test_wheel.py`, five through an installed package); an
  `imgui_bundle` with no extension module is `bundle-missing`. Before that
  date those raised `AttributeError` and `ValueError`.
  An installed package reads no `IMTYPESET_*` variable but
  `IMTYPESET_BUNDLE_STRICT`. Called by hand, `_capi.load` raises
  `ImportError`, `TypeError` or `ValueError`; `_bundle.check`,
  `_bundle.accepted` and `_bundle.gate` were called only with a library that
  has no bundle shim, where they raise `AttributeError`, `KeyError`,
  `TypeError` and `FileNotFoundError`.

## Adding a call

A new public function or method fails `HostileInput` until it has a row in
`TABLE` in `tests/python/test_binding.py` — one line, `(name, the input, the
class or OK, lambda x: the call)` — and its name in backticks in this file. A
new method of `Paragraph` or `Document` also needs one valid call in
`PARAGRAPH_CALLS` or `DOCUMENT_CALLS`, which is what proves it raises
`ValueError` on a closed handle. Convert its arguments with the package's
`_utf8`, `_index`, `_bits`, `_real`, `_pair` and `_address`, and it raises
what this file says.
