# api-document — result (builder, 2026-10-10)

The Document object exists in C++, in C and in Python: styled blocks in; layout, drawing and
selection out. One call per edit, a constant number of calls per frame. `docs/decisions.md` D43
says what was decided and why; `docs/divergences.md`, `## api-document`, lists the nine places the
public behaviour is the product's and not the prototype's.

## For the user's need: pptmstr and advtext, inside their own wheels

`imtypeset.Document` (ctypes over the C ABI, `python/imtypeset`) runs inside BOTH stock wheels
through the sidecar library bundle-route established, one library per venv (28 s each):

```sh
IMTYPESET_BUNDLE_OUT=build/api-document-bundle-pptmstr sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
IMTYPESET_BUNDLE_OUT=build/api-document-bundle-advtext sh python/host/build_bundle_library.sh
IMTYPESET_LIBRARY=$PWD/build/api-document-bundle-pptmstr/libimtypeset_bundle.so sh tools/shot.sh --screen 1600x1000x24 -- \
    ../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_document_pane.py --repeat 225 [--stream] [--tables] --frames 240
IMTYPESET_LIBRARY=$PWD/build/api-document-bundle-advtext/libimtypeset_bundle.so sh tools/shot.sh --screen 1600x1000x24 -- \
    ../imgui-adv-text/.venv/bin/python -P -B python/spike_bundle/advtext_document_view.py --repeat 700 [--stream] --frames 240
```

`python/spike_bundle/pptmstr_document_pane.py` draws pptmstr's REAL blocks (`ui/blocks.py`,
`ui/inline.py`, `ui/rich_pane.py`, imported read-only) three ways side by side: pptmstr's own
`rich_pane.draw()`, the per-paragraph spike, and ONE `Document`. Per frame, microseconds, medians of
3 runs of 240 frames, Xvfb + llvmpipe, load 2 to 7; (calls into imtypeset / calls into the
imgui_bundle binding) for one steady frame. Measured by the Python helper under my claim; the
2,250 + streaming row I re-ran myself after the last library change and got 6,419 / 11,947 / 261
with the same call counts.

| pptmstr, wheel 1.92.801 | today, `rich_pane.draw` | per-paragraph spike | Document |
|---|---|---|---|
| 40 blocks | 1,304 (0 / 1,199) | 568 (120 / 242) | **243 (4 / 7)** |
| 40 + streaming tail | 1,299 (0 / 1,241) | 628 (124 / 245) | **311 (5 / 7)** |
| 2,250 blocks | 8,066 (0 / 9,936) | 14,346 (6,750 / 13,502) | **208 (4 / 7)** |
| 2,250 + streaming | 8,315 (0 / 9,978) | 14,631 (6,754 / 13,505) | **298 (5 / 7)** |
| 2,700 with 450 tables | 7,985 (0 / 7,786) | 35,351 (6,750 / 21,827) | **369 (8 / 58)** |

"Today" at 2,250 blocks windows itself to the last 400 source lines and the Document draws
everything with culling, so those two rows do not show the same content.

| advtext, wheel 1.92.900 (imtypeset calls) | today, `StreamView` | per-paragraph | Document |
|---|---|---|---|
| 12 paragraphs | 8,078 | 221 (36) | **107 (3)** |
| 12 + streaming | 23,666 | 325 (43) | **199 (4)** |
| 2,100 paragraphs | 7,264 | 9,848 (6,300) | **98 (3)** |
| 2,100 + streaming | 29,738 | 9,518 (6,307) | **200 (4)** |

Python positions are (block, index into that block's `str`); the conversion is per block (an ASCII
block converts for free) and does not grow with the document: at the far end of a non-ASCII
document of 115 bytes a block, `hit_test` 2.9 us and `range_text` 5.1 us at 1,000 blocks, 2.9 and
5.0 at 10,000; `from_blocks` (the one call) 5.8 ms and 54.6 ms. Self-hosted, release.

Photographs, looked at: `docs/images/py-bundle-pptmstr-document.png` (I looked: the Document pane
shows the accent heading, set formulas, bullets and "1." "2." markers in their gutter, the quote's
bar, the fence with its line breaks and "py" label, underlined links, and pptmstr's own table in a
Host block; the per-paragraph pane beside it runs the fence onto one line and numbers both items
"1."), `py-bundle-advtext-document.png` (the helper looked: same line breaks in all three panes for
settled paragraphs; advtext's live tail is ragged and the Document's is justified),
`py-host.png` (the helper looked; a tight word gap "item,with").

**What those panes draw that a block cannot say, kept in Python:** a table (a Host block reserves
its place, pptmstr's `_draw_table` draws into it, `set_block_extent` when its height changes); the
per-block context menu (one invisible button over the document and the frame's hit test); a
fence's chrome rows (said as dim one-line paragraphs, which a selection then copies, unlike
pptmstr's). pptmstr's "N earlier blocks not shown" row is not needed. advtext: `LinkSpan` URLs live
in a host-side table keyed by `ImTypesetRun::Tag`; find marks are the host's search plus
`ImTypesetMark`s; `carry_selection`, `BlockSource`, tables and columns are not expressed.

**Not shown:** either application itself running on it (these are spikes shaped like the panes,
not patches to ../pptmstr or ../imgui-adv-text, which are read-only here); a real GPU or display; a
wheel upgrade; the `fork:` header route (still needs a person, as bundle-route said).

## What a host calls

```cpp
ImTypesetDocument* doc = ImTypeset::CreateDocument();
ImTypesetDocumentContent content;            // Text, Blocks, Runs, Styles, Containers: views
ImTypeset::SetDocumentBlocks(doc, &content); // ONE call; nothing is laid out
// an edit:  ReplaceDocumentBlocks(doc, first, count, &content)   AppendDocumentText(doc, " more")
// a frame:
ImTypeset::LayoutDocument(doc, width);                  // free when nothing changed
ImTypeset::DrawDocument(doc, draw_list, pos, &draw);    // lays out what the clip rect shows
ImTypesetDocumentHit hit = ImTypeset::HitTestDocument(doc, mouse - pos);  // block, byte, Tag
// or all of it:  ImTypeset::DocumentView("id", doc, size, &selection, &draw);
```

Header: `include/imtypeset.h`, "Documents" (types above `namespace ImTypeset`, 27 functions).
C: the same under `ImTypeset_` in `include/imtypeset_c.h`. Internals: `src/imtypeset_document.{h,cpp}`
over `src/blocks.*`.

## R1 to R11 (`docs/advtext-migration.md` section 3)

| | state | what shows it |
|---|---|---|
| R1 blocks the host describes | **met**, tables excepted | `the_block_goldens_described_through_the_public_header_compose_to_the_references_commands`: 67 of 69 cases, 211 compositions, 34,647 commands equal the reference; the 2 others have a negative space above, which the header clamps (divergences 9). Kinds: paragraph, heading, list item, code, rule, host rectangle; quote depth on any kind; markers; containers (quote with bar position and colour, list with gutter and tight); runs with face, scale, colour, no-break, an `int Tag`; hard breaks; formulas by dollar scanning or by `ImTypesetRunFlags_Formula` / `_DisplayFormula` runs. `every_kind_of_block_is_described_laid_out_and_found_again`, `quotes_have_their_bars_and_a_tight_list_its_marker_gutter`, `a_formula_copies_as_its_source_whether_found_or_marked_and_a_refusal_is_reported`. |
| R2 retained and incremental | **met for append and extent; a structural edit costs one arithmetic pass over all blocks, counted** | `appending_to_the_last_block_costs_one_block_whatever_the_documents_length`: 25 appends = 25 text builds, 25 block layouts, 0 structure visits, at 200 blocks and at 10,000, and a selection in the block survives. `replacing_blocks_keeps_the_layout_of_every_block_it_does_not_name`: 2 blocks to 3 in view builds 3 texts; a replacement out of view builds none; the pass is between N and 2N + 8 visits (`ImTypesetDocumentStats::StructureVisits`). Stream golden: 1,583 steps as edits, every earlier block keeps its commands and top (`test_api_document_golden`). "Replace in place keeps identity": positions are (block number, byte), so a replaced block keeps its number; nothing more is promised. |
| R3 positions (block, byte) | **met** | selection golden through the public calls: 387 of 387 in-block probes, 483 of 483 drags with both ends in blocks (character, word, line), 703 rectangle groups / 1,387 rectangles exact in float pixels, 108 of 108 block tops and bottoms. Caret, lines with baselines per block (`GetDocumentBlockLine`), block rectangle (`GetDocumentBlockGeometry`: ink box and full-measure box). Not provided: each line's glyph x positions (`set_lines`). |
| R4 one draw call, culled | **met** | `ten_thousand_blocks_are_laid_out_only_where_a_window_shows_them` (34 layouts / 25 text builds over four windows at 500 blocks and at 10,000; second identical frame: 0 work, 0 allocations), `a_steady_frame_with_a_selection_and_marks_allocates_nothing`, `marks_are_drawn_in_the_order_given_and_an_underline_has_its_own_thickness` (ink ranges, fills behind, bars under, per-run and per-block ink, marker ink, quote bar ink; debug overlay is `ImTypesetDocumentDrawFlags_Debug`, drawn, not asserted on). `DrawDocumentToBuffers` is the same into buffers. Culling is by unit (O(log units)) then by line. |
| R5 copy across blocks | **met** | 428 golden range copies rebuilt from the public call's per-block text plus the golden's separators, 250 byte-for-byte as they stand; separator stated and replaceable (`block_separator`); formula as its source; code as its bytes; host block's text whole. |
| R6 tables | **not built; fields reserved** | `ImTypesetBlockKind_Table`, `TableRows`, `TableColumns`; a table block is set as an empty host block and the call says InvalidArgument (`what_is_wrong_with_a_description_is_cut_to_what_is_right_and_said`). |
| R7 a block that reserves a rectangle | **met** | `a_host_block_changes_height_without_anything_else_being_laid_out` (1 layout, 0 text builds, 0 structure visits); hosts golden 8 of 8 layouts, 4 of 4 resizes. |
| R8 formulas of their own | **not part of this**; attaches at `src/mathtext.h` `typeset` / `RefusalKind`. A block's refusal is `GetDocumentBlockError` (code, byte, sentence), not the kind. |
| R9 font numbers | **not part of this**; attaches at `src/fontlib.*`. |
| R10 a report of what a layout did | **met** | `ImTypesetDocumentStats`: MaterialBuilds, BlocksLaidOut, UnitsStacked, StructureVisits, BlocksDrawn, Blocks, BlocksDeferred. Every counter test above reads it. |
| R11 columns | **not part of this**; `GetDocumentBlockLine` gives each line's top and bottom, which is the "enough to do it outside" half; drawing a line range at an offset is not offered. |

## What ran

GCC 12.2.0, FreeType 2.12.1, ImGui 1.92.9, every binary under `( ulimit -v 6000000; timeout … )`
or `ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`, `-j4`.

| configuration (private dir) | what | result |
|---|---|---|
| debug, whole tree (`build/api-document`) | every test, bench and example built with `-Werror`; ctest | 39 of 40 entries run (`-E test_stress_text`, see below): 38 pass, `test_api` fails one test, see below |
| debug | `test_api_document` 14 tests / 11,013 checks; `test_api_document_golden` 5 / 25,549; `test_c_api` 453 checks; `test_c_api_map` 7 / 694; `test_blocks` 23 / 128,674; `test_demo` 8 / 90 | pass |
| ASan + UBSan (`build/api-document-asan`) | `test_api_document` (14 / 11,012, incl. the hostile-description test), `test_api_document_golden`, `test_c_api`, `test_blocks`, `test_demo` | pass, no sanitizer report in any |
| release, stb_truetype, 16-bit `ImWchar`, exceptions on | `test_api_document` (13 tests at that point: before the views-bound test and `DescriptionCut`), `test_c_api` (471 checks with exceptions), `test_c_api_map`, `test_blocks` | pass in each |
| self-hosted Python host, release (`build/api-document-py`) | ctest `python_binding` (47 tests) | pass |
| inside wheel 1.92.801 (pptmstr's venv) and 1.92.900 (advtext's) | the two spikes, 240 frames each case | ran; numbers at the top |
| not run | the `asan`/`release`/… PRESETS (their directories belong to the gate); `test_api_document_golden` outside debug and ASan; MSVC, clang | |

Two entries of the suite fail on the live tree for a cause that is not this task's, both since
`src/draw.cpp` changed at 09:42 (word-gaps, in flight); both passed in the same directory with the
Document in before that, and the lead is told: `test_api`'s
`text_to_commands_to_vertices_equals_the_reference_chain` (543 checks: the public draw under product
rules against the reference draw), and `test_stress_text`, which times out in
`a_megabyte_with_no_space_in_it_is_one_overfull_line` inside `Drawer::settle_word` (gdb backtrace
in my concern).

One clock reading in C++, printed by `replacing_blocks_keeps_the_layout_of_every_block_it_does_not_name`
and not asserted on (release, load about 4, a sanitised build running beside it): appending one
BLOCK to a 10,000-block document and drawing the tail costs 610 us, of which the pass over all
blocks is nearly all (10,055 blocks visited, 1 laid out). That is the cost the spec asked to be
"measured and justified": a commit of a block pays it, an append of text to the last block
(`AppendDocumentText`) pays none of it, and it is 0.06 us a block. Not optimised further.

## Claims relayed in the notes, checked

| claim | held? | done |
|---|---|---|
| `TextCodepointToByteOffset` / `TextByteToCodepointOffset` have a non-defaulted `text_end` in the middle | yes | reordered to `(text, offset, text_end = NULL)` in C++; C ABI left as `(begin, end, offset)` (D40's convention; the lead is told I did not follow that half) |
| the header's "missing glyph" sentence | yes: `stand_in` tries Regular, Math, Code, then U+FFFD | header says what happens |
| the font's debug name is the first 39 bytes of its PATH | yes | the file name is used |
| `remove_font` names the internal face `prose-italic` | yes, and three other messages did too | all four name `ImTypesetFace_…`; `tests/test_api.cpp` expected "prose" in one and now expects the public name |
| Reason/Message spell NUL as `\x00`; a formula source over 524,288 bytes is refused; two newlines under HardBreaks give ONE break | all three hold (`c_string_reason`, `MAX_SOURCE_BYTES`, `src/paragraph.cpp` de-duplication) | each is in the header; the third as a KNOWN GAP, behaviour unchanged |
| review-round-3 item 4, X1 narrow | holds on reading; no trigger found by me either | `IMTYPESET_NO_UNWIND` round the two destructor calls; AddFont brings the context up to date before removing the old font |
| review-3a item 6: `terminated` cuts a name at the first NUL | yes | stated in `include/imtypeset_c.h` |
| review-3a item 5: `Paragraph.range_rects` / `range_text` turn -1 into `[]` / `""` | yes, and so did `line_count`, `formulas` and the offset converters | all raise now (`tests/python/test_binding.py`, ParagraphFailures) |
| review-round-3 item 3: `_bundle.py` probe census gaps | yes, all three parts | 22 probe rows on both wheels: enumerators against the binding, `ImFontGlyph` bit-fields by flipping each bit, `ImDrawVert` pos/uv/col; mutation test 29 offsets, 84 mutants, 0 survive (the helper's run). `src/imtypeset_document.cpp` reads no new ImGui struct field (`_CmdHeader.ClipRect`, `VtxBuffer.Size` were probed already) |
| hello_imgui: fonts in `load_additional_fonts`; the demo TU cannot link in the wheel | the second: yes | the bundle library compiles `src/imtypeset_demo.cpp` with `-DIMTYPESET_DISABLE_DEMO`, links with `--no-undefined`; `show_demo_window` ran 6 frames in wheel 1.92.900 and draws nothing there |
| a tab in a code block is selected by its last space (found by the golden replay) | yes | fixed: `index_of` / `span_of` in `src/imtypeset_document.cpp` |

## Cut, or not done

- **A new error code**, taken late: `ImTypesetError_DescriptionCut = 12`, "done, differently", for a
  description that was wrong in part and cut (a table, an index that names nothing). Before it such
  an edit said InvalidArgument, which the Python policy raises on, so a pane would have raised on
  every edit that held a table. A range that is not the document's is still InvalidArgument and
  nothing is done.
- **Tables** (R6): reserved only, as ruled.
- **Find** with case folding and a map back to source bytes (a product rule the lead asked to take):
  not built. Marks are drawn from ranges the host gives.
- **Scroll anchoring** when a block above the window takes its true height: the host's.
- **Demo API gaps from the demo's builder**: the debug overlay is public for a document
  (`ImTypesetDocumentDrawFlags_Debug`), not for a paragraph; no handle-alive query; widgets report no
  line count; `SelectableText` has no granularity or copy hook. Not taken.
- **A clipped code line** (`ImTypesetCodeOverflow_Clip`) is clipped in a draw list, not in buffers.
- **`examples/demo_sdl2`** was not rebuilt or re-photographed (Xvfb rig); the headless demo was.
- `bindings/imgui_bundle/*` and `docs/bindings-notes.md` do not know the document; `docs/advtext-migration.md`
  section 3 still reads as requirements, not as what exists.
- Nothing of the Python list was cut. `tests/python/test_binding.py`: 47 tests (28 before);
  `ctest --test-dir build/api-document-py -R python_binding` passes (I re-ran it after adding
  `ImTypesetError_DescriptionCut`).

## Demo

`ImTypeset::ShowDemoWindow` is declared in the header (C: `ImTypeset_ShowDemoWindow`), and
`examples/imtypeset_demo.h` is gone. A **Document** section is the window's last header: a block-count
slider (10 to 10,000), a Stream toggle (`AppendDocumentText` every frame), Marks, the document in
`DocumentView`, and two lines of counters. `docs/images/demo-*.png` were re-rendered by
`example_demo_headless` (9 pages). Looked at: document, paragraph, limits, selection, stress. Not
looked at: faces, math, inspector, fonts. "Known limits" no longer says "one paragraph at a time".

## Files

New: `src/imtypeset_document.h`, `src/imtypeset_document.cpp`, `tests/test_api_document.cpp`,
`tests/test_api_document_golden.cpp`, `docs/images/demo-document.png`.
Changed, mine by the task: `include/imtypeset.h`, `include/imtypeset_c.h`, `src/imtypeset.cpp`,
`src/imtypeset_c.cpp`, `src/context.h`, `src/imtypeset_demo.cpp`, `tests/test_c_api_map.cpp`.
Changed, allowed by the task: `src/blocks.h`, `src/blocks.cpp` (additions only: `splice`,
`edit_text`, `material`, `layout_unit`, `top_in_unit`, `prepare_if_needed`, `derived`,
`Block::length_hint` read by `estimate` and `sets_box`, `DocumentComposer::materialize` and
`blocks_prepared`, `BlockLayout::serial`; `tests/test_blocks.cpp` untouched and passing).
Changed, not mine, because the header moved under them: `tests/test_api.cpp`, `tests/test_header_pod.cpp`,
`tests/test_stress_text.cpp`, `tests/test_c_api.c` (+ `test_document`), `tests/test_demo.cpp`,
`examples/demo_headless.cpp`, `examples/demo_sdl2/main.cpp`, `examples/demo_sdl2/CMakeLists.txt`,
`bindings/imgui_bundle/generate_imtypeset.py` (two reordered calls). Deleted: `examples/imtypeset_demo.h`.
Shared documents: `docs/decisions.md` (D42, D43 appended), `docs/divergences.md` (`## api-document`
appended), `THIRD-PARTY.md` (SDL2 rows as the demo builder wrote them),
`docs/images/demo-*.png`.
Python (the helper's, under my claim): `python/imtypeset/_capi.py`, `python/imtypeset/__init__.py`,
`python/imtypeset/_bundle.py`, `python/host/imtypeset_bundle_shim.cpp`,
`python/host/build_bundle_library.sh` (compiles the demo TU disabled; a relative
`IMTYPESET_BUNDLE_OUT` works now; run it with `sh`, it is not executable),
`tools/bundle_route/compile_objects.sh`, `tests/python/test_binding.py`,
`examples/python/minimal_host.py`, new `python/spike_bundle/pptmstr_document_pane.py` and
`advtext_document_view.py`, `docs/images/py-bundle-pptmstr-document.png`,
`py-bundle-advtext-document.png`, `py-host.png` (overwritten).

Git: I ran `git add` on `.concerns/api-document--builder--progress.md` and `git rm --cached` on
`examples/imtypeset_demo.h` before re-reading "do not git add"; nothing else, nothing committed.
