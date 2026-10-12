# handoff-core — result (builder-35, 2026-10-11)

Words used strictly: **RAN** (a command of mine, output seen), **READ**, **NOT RUN**; a figure a
helper produced is marked **HELPER** (two helpers worked under this claim: one audited the Python
package, one rendered the demo). Nothing is `git add`ed or committed. Decisions:
`docs/decisions.md` **D66 to D70**. Departures: `docs/divergences.md`, section "handoff-core"
(H1 to H4). `docs/advtext-swap-status.md` is NOT edited: the text for it is in section 9.

Start: tree clean at `caaeab3` (`3a81315` at HEAD when read: docs only). The premises file said to
look in `.concerns/` for a resumption: there was no file of this task; it started fresh.

## 1. Per request

### H7: a role's font metrics — DONE, new public calls

- **Re-measured how.** READ what advtext asks of its old reader (`Theme.at_pixel_height`:
  ascent, descent and em of a face at a probe size, and the advances of ten digits and the
  period; `code_size(pixel_height=)`: `em_size_for_pixel_height`). `load_fonts`' use is finding
  font FILES in the wheel, which is no question about metrics and is not answered here.
- **Done.** `GetFontMetrics(face, em_size, ImTypesetFontMetrics* out, size_t out_size =
  sizeof(...))` and `GetGlyphAdvance(face, em_size, codepoint)`; struct `{EmSize, FontSize,
  Ascent, Descent, LineGap, XHeight, SpaceAdvance}`, seven floats, pixels. C mirror with
  static_asserts and two mapping rows; Python `font_metrics`, `glyph_advance`, `FontMetrics`.
  The role's own font (as `FaceHasGlyph`), no ImGui context needed.
- **How the numbers relate to layout** (said in the header at the struct): the font file's, by
  the library's reader, `value * (em / unitsPerEm)`, not rounded; ascent and descent are hhea's,
  whatever OS/2 asks (what the atlas is sized by); layout does not read the line gap; `FontSize`
  = ascent + descent is ImGui's size for that em, so **the em for an ImGui size `h` is
  `h * EmSize / FontSize`**; layout rounds each advance to a scaled point and kerns, so a text's
  width is a paragraph's line, not a sum of advances.
- **RAN:** `font_metrics_and_advances_are_the_references_as_floats` replays
  `tests/golden/font/*.answers.golden`: 42 (role, size) answers and 20,508 advances equal to the
  reference's doubles as floats, exactly; 7 (role, size) pairs passed over and counted (the
  reference was asked at 14.4, which no float holds). Line gap held to the raw goldens' `HHEA`
  by this tree's own formula (the reference has no such answer).
- **For advtext:** `probe = ts.font_metrics(face, 1024.0)`; `em_px = h * probe.em_size /
  probe.font_size`; the widest marker is `(2 * max(ts.glyph_advance(face, 1024.0, c) for c in
  "0123456789") + ts.glyph_advance(face, 1024.0, ".")) / 1024.0`. Needs the role's font added
  (`load_fonts`) and a context; raises `imtypeset.Error` otherwise.
- **Agent decisions to confirm:** the struct's size is an argument and `IMTYPESET_CHECKVERSION()`
  and `ImTypeset_BuildInfo` are untouched (D66.6); floats, not doubles.

### H8: the rule under a table's header — NOT A DEFECT; both statements are true of different things

- **Re-measured how.** RAN the pinned reference (`build/advtext-reference`, its own interpreter)
  on `demo/tables.md` at 684 px (`.concerns/handoff-core--builder--h8-ref_tables.py.txt`). In scaled points:

  | table | stacked by the reference (`ui._stacked`; its views' units are the same) | sum of `ui._table_boxes`' box heights |
  |---|---|---|
  | 0 | 6,393,984 = 129.6 px | 5,375,683 = 108.96 px |
  | 1 | 13,853,632 = 280.8 px | 12,835,331 = 260.16 px |
  | 2 | 7,459,648 = 151.2 px | 6,441,347 = 130.56 px |
  | 3 | 20,247,616 = 410.4 px | 19,229,315 = 389.76 px |
  | 4 | 8,525,312 = 172.8 px | 7,507,011 = 152.16 px |

  The left column is advtext's "library" figure for each table; the right is its "reference"
  figure. READ in `../imgui-adv-text/.concerns/T4-TABLES-scripts/compare_tables.py`:
  `height_old = sum(b.height + b.depth for b in boxes)` over `_table_boxes`. The reference never
  draws that sum: it stacks those boxes with `append_to_vlist`, and the rule (47,363 sp) gets a
  `\baselineskip` like a line. Difference: 1,065,664 - 47,363 = 1,018,301 sp = 20.64 px.
- **Which is true:** this repository's builders' (T20). The product is NOT taller than the
  reference; advtext compared against a sum the reference never set. Its other finding is true
  as geometry: the rule takes one leading whatever `TableRuleThickness` is (it is placed by its
  baseline), and `-1` removes rule and leading together.
- **RAN at this tree, public calls:** the golden replay of `tests/golden/doc-tables` (demo/tables.md
  at 360 and 660 px among its cases; block and row tops and heights against the reference's own,
  reference rules) passes in the full suite; new
  `a_table_with_a_header_is_a_leading_taller_than_its_rows_under_either_set_of_rules` holds table
  0's shape at 684 px to 6,393,984 sp under reference AND product rules;
  `the_rule_under_a_header_takes_the_same_height_whatever_its_thickness`. (My table 0 has the
  demo's cells as plain text, its code spans unstyled: no row wraps either way.)
- **Changed:** nothing in layout. One sentence in the header at `ImTypesetBlockFlags_TableHeader`.
  The typographic question (should it be a whole leading) stays the border question already in
  the status file.

### H9: a row that arrives in pieces — ADVICE, plus one Python method; no new C/C++ call

- **Re-measured how.** RAN a C++ test that streams a 200-row, three-column held table 7 bytes of
  source at a time, with a stand-in for a host's parser, three ways, by the library's counters:

  | the unfinished row is described | appends | replaces | cells measured | rows set |
  |---|---|---|---|---|
  | padded with empty cells (advtext today) | 370 | 944 | 288,914 | 96,418 |
  | with the cells it has | 1,512 | 0 | 1,513 | 1,410 |
  | (table given whole) | | | 603 | 201 |

  The three end as one table (rows, columns, cells, column edges, sampled row tops, copied text).
  advtext's own 468 / 146,304 were for its text; the shape is reproduced, not the figures.
- **Answer.** Describe the unfinished row WITHOUT padding. A row may have fewer cells than the
  table has columns (set as if the rest were empty). The cell being written is then the last:
  `AppendDocumentText` grows it; `AppendDocumentTableCells` with a first cell that is no row's
  start adds the next cell. Both existed in C and C++. Python lacked the second:
  **`Document.append_table_cells(cells)`** is new (over the existing C function).
- **Declined:** appending text to a named (row, column) cell. D64's invariant (a table's cells
  change only at their end between clears) is **kept, knowingly** (D68): a write into the middle
  moves every later byte, cell, run and kept row. What that leaves a host: markup that closes late
  in the row being written is still a replace of the table.
- **For advtext's `typeset_stream_view`:** strip trailing empty cells from the unfinished (last)
  row before describing; when the old last row is a prefix of the new one cell for cell and its
  last cell's text a prefix of the new cell's: `append_text(delta)` then
  `append_table_cells(new cells)` then `append_table_rows(new rows)`; a finished row's missing
  cells are appended as empty cells.

### H10: marks culled to the clip inside DrawDocument — DONE

- **Re-measured how.** RAN `bench/bench_marks.cpp` (new) in release against an export of HEAD
  (a scratch directory, since removed) and against this tree: 600-line code block, 768 px window, 1,881
  marks, 2,000 warm frames, medians, load average 4 to 5 (NOT a quiet machine):

  | a draw | before | after |
  |---|---|---|
  | no marks | 54 us | 45 us |
  | every mark, Ink, in text order | 2,766 us | 83 us |
  | every mark, Ink, shuffled | 8,914 us | 119 us |
  | every mark, Behind, in text order | 710 us | 75 us |

  advtext's 48 -> 2,515 us reproduced (54 -> 2,766 here).
- **Cause.** Every mark of the draw was worked out against every block drawn, and each Ink mark
  copied the block's whole colour list.
- **Done** (`src/imtypeset_document.{cpp,h}`): marks are kept in start order (used as given when
  sorted; sorted ONCE otherwise, and again only when a range handed over differs from last
  draw's, which costs one comparison a mark a draw); a block's marks by bisection; in a block
  with more than 16, only the marks on bytes of the lines within reach of the clip are worked
  out. **Precondition: none.** Sorted input skips the sort. Drawing order is still the order
  given.
- **RAN:** `marks_culled_to_the_clip_draw_what_every_mark_worked_out_draws` (quads inside the
  window equal to a draw with culling off: 3,000 and 40 marks, sorted and not; 15,302 marks looked
  at against 96,095) and `the_order_of_a_mark_set_is_made_once_...`.
- **For advtext:** hand every mark over each frame; its own culling (two hit tests and building
  `_Marks` by hand) can go. It measured 57 us with its own culling; this is 83 us on my text and
  machine: not the same measurement, do not read a difference into it.

### H11: one glyph quad at a clip edge — REPRODUCED, EXPLAINED, FIXED

- **Reproduced** in C++ at HEAD (`a_glyph_quad_is_emitted_exactly_when_part_of_it_is_inside_the_clip_rectangle`,
  64 failed checks before): U+2502's quad, (42, 59)-(46, 81) with its row's top at 60, was not
  emitted under clips whose bottom was 59.5 to 60.5; several quads at the top edge likewise.
- **Explained.** Not `CULL_MARGIN_PX` and not the overhang bound (the line was looked at). The
  emit loop kept `ImFont::RenderText`'s test on the font's CELL in front of the product's test
  on the quad, so `quads_off_clip_dropped` could drop but never keep. U+2502's bitmap starts a
  pixel above its cell. (advtext's U+4E2D passing in the same frame: INFERRED, it comes from a
  face with a higher cell; here no bundled face has it.)
- **The rule now, stated in the header at `DrawDocument`:** a glyph's quad is emitted exactly
  when some of it is inside the clip rectangle from top to bottom, for every glyph of a line the
  draw looks at (every line whose box is within 2 px of the clip; further for
  `FixedLineHeight`, as already stated). Reference rules untouched.
- **LOOKED AT:** `.concerns/handoff-core--builder--shot-h11-before-clip60.png` and
  `...-after-clip60.png` (6x): clip ending on the row's top; before, nothing of the row; after,
  the top pixel row of the bar and the tip of the integral sign (7 pixels lit in that row).

### H12: which package calls raise — DONE: `docs/python-errors.md`, a test that walks it, three crashes fixed in C++

- HELPER (RAN, CPython 3.11.2, the debug self-hosted library, about 47,000 calls with 42 hostile
  values in every argument): 133 public names audited; after its changes to the wrappers the
  only non-`Error` classes are `TypeError`, `ValueError` (with `UnicodeEncodeError`) and
  `OverflowError`; before, `struct.error`, `ctypes.ArgumentError`, `AttributeError`, `KeyError`,
  `IndexError` and `MemoryError` also escaped. `HostileInput` in `tests/python/test_binding.py`
  provokes every row, fails if a public name is missing from its table or from the document.
- **Clamped instead of raising (decided per call, listed in the document):** indices, counts,
  faces and enums past a C int (ctypes kept the low 32 bits: block `2**32 + 1` meant block 1);
  lengths too large for a float become infinity; a run's `tag`, flags and colours keep their low
  32 bits; `add_font(face, <int>)` is `TypeError` (it allocated that many zero bytes). Still
  loud on purpose: `None` or a `str` for a number, a float for an offset, wrong tuple lengths,
  `replace_blocks` out of range.
- **Process-ending inputs, fixed by me in C++** (`tests/test_api_handoff.cpp`, two tests; the
  crashes were reproduced by the helper in child processes, my tests were written after the fix
  and NOT seen to fail): `DocumentView` with an empty `str_id` (now the view's own ID, as null
  was); `ShowDemoWindow` and `ShowDemoSection` outside a frame (draw nothing).
- **Process-ending inputs left** (in the document): an int that is no `ImDrawList*`; a context
  address that is no context, or destroyed twice; a handle of the other kind; anything after
  `end_frame()` with no `render()`.

### H13: the size each demo section is designed for — DONE: numbers in the header; two fixes

- HELPER (RAN and LOOKED): 11 sections x 7 sizes (10, 13, 16, 19, 24, 32, 40 px) at 1100x720,
  about 70 pictures looked at, plus scrolled and wider renders. A size pushed in one frame and
  changed in the next is followed (byte-identical final frames).
- **Found:** Math's subset table cut formulas from 16 px and ran inline formulas under the
  display column from 24 px (columns were a share of the window; a formula does not break);
  lines of counters were cut at the right edge from 24 to 32 px; fixed pixel measures (250, 420,
  460 px), Specimen samples and gallery plates do not follow the size; Stress and Document hold
  children 24 and 18 lines tall.
- **Fixed** in `src/imtypeset_demo.cpp`: counters wrap; Math's two formula columns are as wide
  as their widest formula. LOOKED AT by me after the fix: Math at 13, 19, 24 and 32 px
  (`.concerns/handoff-core--builder--shot-h13-math-{19,24}px.png`), Stress and Document at 32.
  At 19 px in 1100 px the table is whole; at 24 its two columns of words break inside words; at
  32 they are a few characters wide (formulas still whole and apart).
- **Not changed:** the fixed measures and plate sizes (a slider's meaning), the tall children, a
  refused formula's source being cut by the next column. Nothing new exposed.
- The paragraph is in the header at `ShowDemoSection`.

## 2. API changes (for the status file's "API changes")

| what | C++ | C | Python | note |
|---|---|---|---|---|
| a role's font at a size | `GetFontMetrics(face, em_size, ImTypesetFontMetrics* out, size_t out_size = sizeof(ImTypesetFontMetrics))`; `ImTypesetFontMetrics {EmSize, FontSize, Ascent, Descent, LineGap, XHeight, SpaceAdvance}` | `ImTypeset_GetFontMetrics(face, em_size, out, sizeof *out)`, `ImTypeset_FontMetrics` | `imtypeset.font_metrics(face, em_size)` -> `FontMetrics(em_size, font_size, ascent, descent, line_gap, x_height, space_advance)`; raises `Error` | new. Pixels, floats, the role's own font, hhea. The em for an ImGui size `h`: `h * em_size / font_size` |
| one character's advance | `GetGlyphAdvance(face, em_size, codepoint)` | `ImTypeset_GetGlyphAdvance` | `imtypeset.glyph_advance(face, em_size, char_or_codepoint)` | new. Kerning apart; a character the font lacks answers the missing glyph's (ask `face_has_glyph`) |
| cells into the row a table ends in | `AppendDocumentTableCells` (unchanged) | unchanged | **`Document.append_table_cells(cells)`** | new in Python only |
| which glyphs a clip rectangle gets | `DrawDocument`, `DrawParagraph`, the widgets, the `ToBuffers` forms | the same | `draw`, `draw_to_buffers`, `view` | **changed**: a quad is emitted exactly when part of it is inside the clip from top to bottom. A glyph taller than its font's cell is now drawn by the rows inside (it could be dropped with one or two rows inside). Vertex counts under a clip that cuts such a glyph go up |
| marks | `ImTypesetDocumentDraw::Marks` | the same | `Document.draw(marks=...)`, `pack_marks` | **changed, same picture**: any order, all of them every frame; cost is the marks in view plus one comparison a mark. Marks on lines not shown no longer emit their rectangles |
| `DocumentView` with an empty `str_id` | the view's own ID | the same | `Document.view("")` draws | was an ImGui assertion (process ends) |
| the demo outside a frame | `ShowDemoWindow`, `ShowDemoSection` draw nothing | the same | `show_demo_window`, `demo.show_demo_section` | was an assertion or a segmentation fault |
| what Python raises | | | every public call: `Error`, or `TypeError` / `ValueError` / `OverflowError` | **changed**: `struct.error`, `ctypes.ArgumentError`, `AttributeError`, `KeyError`, `IndexError`, `MemoryError` no longer leave a call; ints past a C int are clamped (they wrapped); `docs/python-errors.md` |
| the demo's Math table and counters | `ShowDemoSection` / `ShowDemoWindow` | | `imtypeset.demo` | changed look: formula columns as wide as their widest formula; long counter lines wrap |

Nothing was renamed or removed. No struct that `IMTYPESET_CHECKVERSION()` compares changed size;
`ImTypeset_BuildInfo` is unchanged; the sidecar's probe rows are unchanged. A package from this
tree refuses a library from before it (it declares two functions the old library lacks).

## 3. What was run on the final tree

All RAN by me, each in a private directory with the preset's flags, `-j4`, every binary under
`ulimit -v 6000000` and a timeout (ASan under `ASAN_OPTIONS=hard_rss_limit_mb=3000`, one
sanitised build, alone). The four suites below ran one after another AFTER the last edit to a
compiled or tested file (`.concerns/handoff-core--builder--gate-final.log.txt`); only this file
and the scratch copies under `.concerns/` were written after.

| configuration | result |
|---|---|
| debug, FreeType 2.12.1, GCC 12.2 | 51 of 51 (the `amalgamation` ctest among them; `test_api_handoff` is the new one) |
| ASan + UBSan | 51 of 51, run twice (the second after a comment and a rename in one file); no report |
| release | 51 of 51 |
| debug with the Python host | 53 of 53: `python_binding` (101 tests) and `python_demo_sections` (4) among them |
| sidecar for advtext's venv (`build_bundle_library.sh`, `IMTYPESET_BUNDLE_HEADERS=stand-in`; wheel 1.92.900) | exit 0, 0 warning lines, accepted by probe, 24 of 24 rows; `run_gate` accepted; `mutate_gate` 30 offsets, 87 mutants, 12 crashed, 0 survived; `check_document_edits` 23 PASS 0 FAIL; `check_swap_answers` 18 PASS 0 FAIL |
| sidecar for pptmstr's venv (wheel 1.92.801) | the same figures |
| the new calls inside each wheel (`.concerns/handoff-core--builder--bundle-new_calls.py.txt`: `font_metrics`, `glyph_advance`, `append_table_cells`, `view("")`, 40 unsorted marks, the demo outside a frame) | 8 of 8 in each |
| `clang-format` 14.0.6 `--dry-run -Werror` over the 13 C and C++ files changed or added | clean |

`test_api_handoff`: 11 tests, 43,392 checks. `test_c_api`: 746 checks. `test_c_api_map`: 90 C++
functions each with a C counterpart, 29 structs, 191 fields.

The sidecars were built with **stand-in** headers because the script's new default (`fork`)
fetches from GitHub, which this session cannot reach; `mac-ci` was writing that script while I
ran it, and it ran clean both times.

The private build directories, the "before" export and the sidecar outputs are removed.

## 4. Not run, not done

- The `stb`, `wchar16`, `amalgamation` (as a preset), `fp-host`, `exceptions`, `bundle-config`
  and `imgui-next` configurations. The mark order's unwinding (a `bad_alloc` in the sort) is
  READ to leave nothing kept as made; no throwing-allocator sweep covers a draw with marks.
- Timings on a quiet machine: load average 4 to 5 for every figure in D69.
- The C++ tests for the empty `str_id` and the demo outside a frame were not seen to fail (the
  crashes were reproduced from Python by the helper, against the library before the change).
- H13: I looked at Math (13, 19, 24, 32 px), Stress and Document (32 px) after the fix; the
  other eight sections' code did not change beyond wrapped lines and I relied on the helper's
  pictures of them, taken BEFORE the wrap. No section was looked at with a proportional UI font,
  or interacted with.
- `docs/images/demo-math*.png`, `demo-section-math.png`, `demo-stress.png`, `demo-document*.png`
  now differ from what the demo draws (the Math table's columns, wrapped counters). Not
  regenerated: `docs/images` is not this task's. `example_demo_headless <dir>` writes them.
- `docs/performance.md` has no row for `bench_marks`; `bindings/imgui_bundle/*` (the unrun litgen
  draft), `README.md` and `docs/spec-status.md` do not know the two new functions. Not this
  task's files.
- The Python files were not run through a formatter: none is configured in the tree and the
  files at HEAD do not pass `ruff format` at any line length tried.
- Nothing was run on anything but Linux x86-64, GCC 12.2.

## 5. Files changed

Changed (17): `include/imtypeset.h`, `include/imtypeset_c.h`, `src/draw.cpp`, `src/draw.h`,
`src/imtypeset.cpp`, `src/imtypeset_c.cpp`, `src/imtypeset_demo.cpp`,
`src/imtypeset_document.cpp`, `src/imtypeset_document.h`, `tests/test_c_api.c`,
`tests/test_c_api_map.cpp`, `tests/python/test_binding.py`, `python/imtypeset/__init__.py`,
`python/imtypeset/_capi.py`, `python/imtypeset/demo.py` (the helper: two lines of argument
conversion), `docs/decisions.md` (D66 to D70 appended), `docs/divergences.md` (section
"handoff-core" appended).

New, untracked (3 in the tree proper): `tests/test_api_handoff.cpp`, `docs/python-errors.md`,
**`bench/bench_marks.cpp`** (outside the declared write-set: benchmarks are found by glob and the
measurement H10 asks for needed one).

Declared and NOT touched: `src/blocks.*`, `src/align.*` (H8 and H9 needed no layout change).

Under `.concerns/`: this file, `handoff-core--builder--progress.md`, four pictures
(`...shot-h11-{before,after}-clip60.png`, `...shot-h13-math-{19,24}px.png`), four scratch
sources as `.txt`, the final gate log.

Not mine, present in `git status`: `python/host/build_bundle_library.sh`,
`python/host/{bundle_pins.toml,exports_bundle.txt,fetch_headers.py}`, `tools/ci/`,
`docs/sidecar.md`, `.github/` (`mac-ci`); `docs/vector-output.md` if `vector-output` has written
it.

## 6. Decisions I took, for the lead to confirm or overturn

1. D66.6: `GetFontMetrics` carries the struct's size; `IMTYPESET_CHECKVERSION()` and
   `ImTypeset_BuildInfo` unchanged. The tables-and-columns way (a `DebugCheck...` call and a
   `Sizeof` field) is the alternative.
2. D68: H9 is advice plus `Document.append_table_cells`; "append to a named cell" declined and
   D64's invariant kept. A "replace the last row" call would be the next step if advtext's
   late-closing markup turns out to matter.
3. D69: under product rules EVERY glyph placed is loaded into the atlas (it was only those whose
   cell met the clip). With lines culled, as both public draws do, that is the lines within reach
   of the clip.
4. D70: an empty `DocumentView` id is the view's own ID (not a refusal, where `SelectableText`
   refuses one); the demo outside a frame draws nothing and says nothing.
5. The helper's: a document run's `tag` past 32 bits keeps its low 32 bits silently (it was
   `struct.error`); a host using `id(obj)` as a tag gets another number back from `hit_test`.
6. H13: I changed the demo (Math's columns, wrapped counters) where the request only asked for
   numbers, because the section the user named was the one that broke.

## 7. Least sure of

1. **The mark culling's edges.** The equivalence test's margins are generous (two mutations of
   the culling window, by two bytes and by an em, PASSED it; only a gross one failed). The
   argument that no visible mark is dropped rests on reading: the index-to-byte map being
   non-decreasing (checked per layout at run time, culling off when it is not) and on how
   `index_of` picks an item for a byte. A mark that should colour a visible glyph and does not
   would be the failure to look for, most likely at the first or last visible line of a text
   block with formulas or collapsed spaces.
2. **Loading every placed glyph** under product rules: not measured for atlas growth on a page of
   CJK text; reasoned to be the same glyphs a scroll of a line or two would load.
3. **H8's product-rules statement** stands on one unwrapped table by hand at 684 px plus the
   golden replays under REFERENCE rules; demo/tables.md whole under product rules was not laid
   out by me (wrapped rows under product rules were not compared with the reference's heights).
4. **Math's table through an ID trick**: its formula columns take their width when the table is
   first made, so the widths are part of its ID; a host that saved table settings (ini) would
   keep one entry a size. The demo's own ini handling was not looked at.
5. `GetGlyphAdvance` in Python tells "zero advance" from "failed" by reading the last error after
   a zero.

## 8. Seen, and nobody asked

- advtext's `compare_tables.py` baseline (a sum of `_table_boxes`) is also what its test
  `test_the_five_tables_have_advtext_s_columns` "and the next test" may hold heights to (READ of
  its result file only, not of the tests): if a test there expects 108.96, it expects a height
  the reference never drew.
- `examples/demo_headless.cpp --section N` pushes 19 px and takes no size: a `--font-size`
  option would let the gate render sections at other sizes; the helper's scratch driver
  (`.concerns/handoff-core--builder--h13-shot.cpp.txt`) is that.
- The refusal message for a tall parenthesis gives a ratio that changes with size (1.27 at 13 px,
  1.28 at 19, 1.31 at 24: HELPER, seen in the pictures; I saw 1.27 and 1.28). Hinting-free
  metrics should scale; rounding to scaled points at each size is the likely cause. Not looked
  into.
- `Document.view` in a window with no horizontal scrollbar cannot reach content cut on the
  right; the demo's Math table at large sizes is the case.
- A golden size of 14.4 is not a float: any public call that takes an em as a float cannot be
  asked for exactly what the reference was asked at that size.

## 9. Text for `docs/advtext-swap-status.md` (the lead holds the file)

Rows for "Answers to advtext's requests":

| # | request | state |
|---|---|---|
| H7 | a role's font metrics | **done** (not committed when written; task `handoff-core`, D66). `font_metrics(face, em_size)` -> `FontMetrics(em_size, font_size, ascent, descent, line_gap, x_height, space_advance)` and `glyph_advance(face, em_size, char)`, in pixels, from the role's own font by the library's reader, hhea ascent and descent. The em for an ImGui size `h` is `h * em_size / font_size` from the metrics at any size, which is `em_size_for_pixel_height`. *Measured here:* 42 (role, size) answers and 20,508 advances equal to `advtext.fontlib`'s at the pinned commit, as floats, exactly. Finding font FILES in `load_fonts` is not a metrics question and is not answered |
| H8 | the rule under a table's header takes a whole line | **not a defect; both figures are right and are of different things.** *Measured here* on `demo/tables.md` at 684 px through the pinned reference: its own stacking (`ui._stacked`, and its views' units) gives 129.6 / 280.8 / 151.2 / 410.4 / 172.8 px, which are the heights you measured from the library; 108.96 / 260.16 / ... is the sum of the heights of `_table_boxes`' boxes (`compare_tables.py`: `height_old`), which the reference never draws: it gives the rule a `\baselineskip` like a line. 20.64 px is one leading less the rule. The library's height is the reference's to the scaled point under both sets of rules at this tree. Whether it SHOULD be a whole leading is the border question below, unchanged. If a test of yours expects 108.96, it expects a height the old view did not have |
| H9 | appending to a table row that arrives in pieces | **answered: describe the unfinished row without padding**, and `Document.append_table_cells(cells)` is new for the second half (task `handoff-core`, D68). A row may have fewer cells than the table has columns. The cell being written is then the last: `append_text` grows it, `append_table_cells` adds the next cell of the row, `append_table_rows` the next row. *Measured here* by the library's counters, 200 rows, 7 bytes a piece: padded 944 replaces and 288,914 cells measured; unpadded 0 replaces and 1,513; the tables end equal. **Declined:** appending to a named (row, column) cell: a table's cells change only at their end between clears, which is what makes an append cost a row. Markup that closes late in the row being written is still a replace |
| H10 | cull marks to the clip inside `DrawDocument` | **done** (task `handoff-core`, D69). Hand every mark over every frame, in any order: the draw works out the ones on the lines in view. No precondition; marks in document order skip a sort, which is otherwise made once per set of ranges. *Measured here* (release, a loaded machine, 600 lines, 1,881 Ink marks): 2,766 us a frame before, 83 after, 45 with no marks. Your own culling (the two hit tests, the hand-built `_Marks`) can go |
| H11 | one glyph quad at a clip edge | **reproduced, explained, fixed** (task `handoff-core`, D69). The draw tested a glyph's font CELL against the clip before its quad, as `ImFont::RenderText` does; U+2502's bitmap starts a pixel above its cell, so with the clip ending on its row's top the quad was dropped with one row inside. The rule now: a glyph's quad is emitted exactly when part of it is inside the clip rectangle from top to bottom. *Looked at here:* that pixel row is now drawn |
| H12 | which package calls can raise on host input | **done**: `docs/python-errors.md` (task `handoff-core`, D70). A public call raises `imtypeset.Error`, or one of `TypeError`, `ValueError` (with `UnicodeEncodeError`), `OverflowError`, and nothing else; the document says which input gives which, and what the package now clamps instead (an index past a C int used to WRAP). Three inputs that ended the process no longer do (`Document.view("")`, the demo outside a frame); the ones that still do are listed there |
| H13 | the text size a demo section is designed for | **done**: the numbers are in `imtypeset.h` at `ShowDemoSection` (task `handoff-core`). Every section runs from 10 to 40 px and follows a size that changes between frames. In a page 1100 px wide all are whole to 24 px except Math's table of the subset, whose two columns of words want a page some 58 times the size wide (whole at 19 px); from 32 px a row of controls or a caption is cut at the right edge in four sections. Two things were fixed on the way: Math's formulas no longer run under each other at 24 px and above, and long lines of counters wrap. For "Math in text" larger: push up to 19 px in an 1100 px page, or give the page more width |

Line for "Build from", when there is a commit: a library and the package must come from one
commit; the package declares `ImTypeset_GetFontMetrics` and `ImTypeset_GetGlyphAdvance` and
refuses a library without them. No struct of the version check changed and the sidecar's probe
rows are what they were (24).

"API changes": section 2 above, as it stands.

For "Things the advtext side should know":

- **A draw under a clip that cuts a tall glyph emits more quads than before** (the glyph is now
  in both slices, as the header always said of a glyph a cut goes through). A test that counts
  vertices under such a clip changes; the probe `probe_overhang.py` is expected to report 2 of 2 at
  "exactly at the row's top" (NOT RUN here: it needs your tree's views).
- **Marks need not be culled or sorted by the host.** Rectangles of a mark on lines out of view
  are no longer emitted at all.
- **`Document.view("")` draws; an index past a C int no longer wraps to a small one.**
