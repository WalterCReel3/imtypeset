# py-fixes — result (builder, 2026-10-10)

Everything below was run on this machine, every process under `ulimit -v 6000000` and a timeout,
windows on Xvfb + llvmpipe through `tools/shot.sh`. Two interpreters: pptmstr's
(`../pptmstr/.venv`, imgui_bundle 1.92.801, Dear ImGui 1.92.8) and advtext's
(`../imgui-adv-text/.venv`, 1.92.900, 1.92.9), each with its own sidecar from
`python/host/build_bundle_library.sh` (stand-in headers, gate accepted). Nothing was written outside
this repository; no session log was read; nothing installed; no `git add`.

**"Before" always means the tree as it was**: for the patch, a copy of the three overlay files
taken before I edited them; for the package, `python/imtypeset/__init__.py` from git's index.

## The font-lifetime answer

**Established by running, in both wheels, and by reading hello_imgui at the commits the wheels pin.**

1. **hello_imgui does not reload fonts on a DPI change in these versions.** Read:
   `src/hello_imgui/internal/backend_impls/abstract_runner.cpp` at `4c187c66` (the commit tag
   `v1.92.801` of `../imgui_bundle` pins) and `82f162b8` (`v1.92.900`), from the git objects of
   `~/Source/hello_imgui` (`../imgui_bundle`'s own submodule directory holds only the bindings).
   `LoadAdditionalFonts` is called in `Setup()` right after `ImGui::GetIO().Fonts->Clear()` and set to
   null; the only other call is in the frame loop, `if (params.callbacks.LoadAdditionalFonts != nullptr)`,
   which is true only when the APPLICATION assigned the callback again, and the atlas is not cleared
   for that call. 1.92 scales fonts at draw time (`style.FontScaleDpi`). Run: 60 frames, the callback
   fired once; assigned again at frame 20 it fired a second time with 5 fonts in the atlas on entry.
   So "and again on a DPI change" (bundle-route's result, `examples/python/bundle_support.py`, the
   patch's `typeset.py`, the plan's 5.1 item 11) was false; the last three are corrected.
   That a PyPI wheel was built from its tag is still an inference; the run agrees with the read.
2. **A second font load does not crash and does not dangle, in the shapes an application would
   write** (`tools/bundle_route/font_lifetime.py`, new; one process a scenario, both wheels, all exit 0):
   `reassign` (no clear: each role's font is replaced and the old one leaves the atlas), `clear`
   (the callback empties the atlas, reloads the application's font, then imtypeset's: all four
   roles get new fonts, document and widget draw on), `clear-lost` (atlas emptied, imtypeset's
   fonts not added again: every draw answers False with "no font has been added for
   ImTypesetFace_Regular", `get_font` is None, no crash). pptmstr assigns the callback once
   (`pptmstr/app.py`), so today it is the `once` case.
3. **One real defect, narrow, in `src/imtypeset.cpp` (not my file): REPRODUCED, fix verified on a
   scratch copy, posted to lead.** `validate_fonts` does not look when the frame count and
   `atlas->Fonts.Size` are both unchanged. If the application empties the atlas and adds back
   exactly as many fonts as it held, in one frame, the next `add_font` still holds the freed
   `ImFont*` as `old`; `atlas->Fonts.contains(old)` compares addresses, the allocator hands that
   address to one of the application's NEW fonts, and `atlas->RemoveFont(old)` removes the
   application's font. Scenario `same-count-cycle`: an application font left the atlas in 3 of 12
   cycles (1.92.801) and 4 of 12 (1.92.900) on the first run, and in between 1 and 6 of 12 on
   the four later runs of each: it depends on what the allocator reuses. The application's binding object for that font is
   then dangling: `push_font` on it is the crash. `GetFont` returns the freed pointer in the same
   window (seen); `RemoveFont` would hand it to `atlas->RemoveFont` (read, not run).
   Fix: `ctx->fonts_checked_frame = -1;` before `validate_fonts(*ctx)` in `add_font`, `RemoveFont`
   and `GetFont`. With that one line in `add_font`, on a copy of the generated `imtypeset.cpp` in
   my build directory: 0 of 24 cycles. **Not in the tree unless doc-fixes or the lead put it
   there**; `font_lifetime.py same-count-cycle` exits 1 while it is not.
   pptmstr's own shape cannot hit it: after a clear its faces are back and imtypeset's are not,
   so the count differs.

## Per item

| item | reproduced? how | fixed how | ran, after |
|---|---|---|---|
| **2** patch: `_draw_table` raises between BeginChild and EndChild | **Yes**, pptmstr wheel, `python/spike_bundle/pptmstr_patch_faults.py --scenario table-raises --overlay <copy of the patch as it was>`: after `rich_pane.draw` the current window is `faults/pane_…/##table_2_…` (left open), both renderers drew the frame, imtypeset switched off, and the application ended on `IM_ASSERT((g.WithinEndChildID == window->ID) && "Must call EndChild() and not End()!")` (exit 2) | `_draw_tables`: `pop_style_var` and `end_child` each in a `finally`. `_draw_table` (pptmstr's own, shared with the classic renderer): `end_table` and `end_popup` in a `finally`. `_draw_typeset` split into `_prepare` (describe, layout, marks: draws nothing, begins nothing) and `_emit`; it never raises and returns whether the frame is its own. `draw()` calls `_draw_classic` only when it is not | `table-raises`: lived, no window left open, no frame drawn twice, switched off after 3 failing frames with `reason()` = "Injected: _draw_table (3 frames in a row)". `table-raises-once`: still in use, failure in `trouble()` |
| **2** a cell raises inside `_draw_table`, between BeginTable and EndTable | **Yes**, `table-cell-raises`: window left open, frame drawn twice, "Missing EndTable()" then the application ended | the `finally` in `_draw_table` above | lived, balanced, off after 3 frames with the reason |
| **2** `doc.range_text` raises inside BeginPopup/EndPopup | **Yes**, `menu-copy-raises` (synthetic drag, right click, the menu item answers True once): window left open is `##Popup_36c0230f`, drawn twice, application ended on "Must call EndPopup() and not End()!" | `end_popup` in a `finally`; copying is `_copy()`, which records a failed copy and changes nothing else | lived, balanced, still in use, `trouble()` = "copy: Injected: Document.range_text" |
| **2** a raise after `doc.draw()` gives one frame with both renderers | **Yes**, `key-copy-raises` (Ctrl+C with a selection): frame 31 drawn by both, imtypeset switched off for the run by a copy that failed | as above; and no path reaches `_draw_classic` once `_emit` has begun | 0 frames drawn twice in every scenario |
| **2** (the brief) "never disabling itself for a recoverable condition without saying why" | **Yes**: `describe-raises-once` and `draw-raises-once` each switched the library off for the run after ONE failure, the reason only in `typeset.reason()` | one failure drops the document (described again next frame) and is recorded: `typeset.trouble()`, and a warning logged once per distinct message under `pptmstr.typeset`; `_FAILURES_BEFORE_OFF = 3` consecutive failing frames in one pane switch it off, `reason()` says why and is logged. A failed import or font load is logged too. `_sync` notes a description the library cut (`edit_error`) | `describe-raises-once`: 1 classic frame (nothing had been drawn), 69 document frames, in use. `draw-raises-once`: 1 blank frame (drawing had begun, so the classic renderer may not draw it), in use |
| **6** Python docstring says a cut edit raises; `from_blocks` discards the False | **Yes**, headless in the wheel: `Document.from_blocks([Block(BlockKind.Table)])` returns a document and no sign; `set_blocks` of the same answers False | docstring rewritten (three outcomes); `Document.edit_error` (an `ErrorInfo`, code 0 after an edit applied whole) set by `set_blocks`, `replace_blocks`, `append_text`; `append_text` now returns the bool too | `tools/bundle_route/check_document_edits.py` (new) 17/17 in both wheels; before: 5 FAIL then AttributeError |
| **7** Python half: mirror written before `_outcome` raises | **Yes**: `replace_blocks(1, 1, 4,194,305 blocks)` raises InvalidArgument and leaves `len(doc)` 4,194,306 against the library's 2, and `range_text(((0,1),(1,3)))` answers `'éllo|wö'` for `'éllo|wör'`; `set_blocks` of the same leaves 4,194,305 against 0; a refused `append_text` leaves `[[0, '', b'']]` for the block | `_texts`/`_last_size` are assigned only after `_edited()` did not raise. `append_text` builds the new entry beside the old one and commits it after the call (and cuts it to what the library kept on TextTooLong). `_refused()`: after a refusal, if the library's block count is not the mirror's (the library changed the document while refusing: today's `SetDocumentBlocks`, doc-fixes item 7), the mirror takes the library's count | same script; and two tests added to `tests/python/test_binding.py` (`test_from_blocks_keeps_what_the_library_cut`, `test_a_refused_edit_leaves_what_python_keeps_of_the_document`) plus one assertion in `test_append_text_costs_one_block`: `python_binding` Passed on a private self-hosted build |
| **9** find marks index lower-cased text | **Yes**, `--scenario find`: a block starting "İİİ İstanbul" (U+0130 lowers to two characters): 2 marks not on the needle (`'LE in '`, `'le aft'`) | `_fold()`: when lowering changes the length, an index map back to the text; marks use it | 0 marks off the needle |
| **9** `_FIND_MARK_CAP` tested per block | **Yes**: a block with 3,000 needles gave 3,003 marks, cap 2,000 | tested in the inner loop (`_find`); the cap counts find marks, not link underlines | 2,000 |
| **9** every streaming frame re-searches all text | **Yes**: 1,611 blocks / 400,052 characters (this repository's reports), a needle, 6 characters a frame: `_marks` median 882 us a frame, p95 1,294 | `_find` keeps the marks of blocks that did not change (`found`, `found_upto`, `found_needle`) and searches from the first changed block | median 114 us, p95 181 (what is left is packing links + marks); the marks at the end equal a search from nothing by the patch's own function and by the harness's |
| **9** a streaming table's Host extent resets to a guess on every change | **Yes**, but only visible when cells wrap (with one-line rows the guess IS the height, so a first attempt showed nothing): a 6-row table with wrapping cells arriving 6 characters a frame was cut off by its Host block in 206 of 216 frames (heights 20, 40 … 140, then 620 when the stream ended) | `_sync` carries a re-described table's height and last measurement (`carried`); `_describe` starts from `max(guess, carried)` | cut off in 35 of 220 frames (the one or two after each growth, until the measurement repeats); height never falls |
| **privacy** `pptmstr_corpus.py` defaulted to the session logs | read: `DEFAULT_SESSIONS` and `--source` default `sessions` in `pptmstr_census.py` and `pptmstr_real_panes.py`; `measure_pptmstr.sh` used them | no default directory exists any more: `--source` defaults to `repo`; sessions are read only with `--sessions DIR` (`corpus.sessions_directory` exits with the sentence otherwise); `measure_pptmstr.sh [out] [sessions-dir]` skips its `--pick` rows without the directory | `pptmstr_census.py` with no arguments: `"source": "repo"`, 55 files; `--source sessions` alone exits 1 with the sentence; an EMPTY directory I made, named: 0 files. No session log was opened. **Nothing was re-measured from them**: the plan's session-corpus numbers stand as its author took them |

## The patch, and what the user applies

- `python/spike_bundle/pptmstr_overlay/pptmstr.patch`: lines added/removed are printed by the last
  run below. Written again by `python/spike_bundle/pptmstr_overlay/make_patch.sh` (new), which keeps
  the hand-made `theme.py` part and diffs the other two files against `../pptmstr`.
- **`git apply --check` passes in `../pptmstr`** at `4974657`. pptmstr moved on during this
  session (two commits at 12:33; it was `851978e` when the plan was written); `theme.py` and
  `ui/rich_pane.py` are untouched by them.
- A scratch copy of pptmstr at that commit with the patch applied (`git archive` into my build
  directory): `ruff check pptmstr scripts tests`, `black --check`, `mypy pptmstr` (its strict
  configuration, 47 files) clean; `pytest tests`: 2,569 passed, 3 xfailed, 0 failed.
- The plan's proof run (`pptmstr_app_shot.py --rich --focus CONTEXT -- --fake`, the real
  `pptmstr.app.main` with the three overlay modules): `imtypeset: in use`, wheel 1.92.801, 3.02 to
  3.03 library calls per `rich_pane.draw`; the photograph was looked at (RICH pane, right side,
  text and colours as before).
- Without `IMTYPESET_LIBRARY`: 70 of 70 frames by the classic renderer, nothing imported.
- "Start here" in `docs/advtext-rework-plan.md` is updated: the patch's size and commit, what the
  three files now do on failure, the fault harness command, "fonts are loaded once"; section 10's
  census and measurement commands (the default is the repository's reports).

## The last run, against the tree as it stood then

A script in my build directory (removed with it): both sidecars rebuilt from `src/` through
`python/host/build_bundle_library.sh`, then every check above, then the self-hosted library
(`cmake -S python`, Release, `-j3`) and `python_binding`. Started 12:59, ended 13:04; the four
source files hashed before and after are the same, so this is one state of the tree. doc-fixes'
progress file was read first: its temporary edit in `src/imtypeset_document.cpp` was gone
(`grep -c TEMPORARY-REPRO` = 0) and its run-trouble change (InvalidArgument to DescriptionCut)
was in. doc-fixes was still working when I finished: a later `src/` is not covered.
`same-count-cycle` exits 1 because the font fix is not in `src/`; everything else is 0.

```
12:59
d3b1c65db358f626  src/imtypeset.cpp
03ad751c49b0bbab  src/imtypeset_document.cpp
a465bff42bd67400  src/draw.cpp
3021083aeba1a976  src/imtypeset_c.cpp
sidecar pptmstr exit=0
sidecar advtext exit=0
edits pptmstr exit=0 pass=17 fail=0
fonts pptmstr once exit=0 done=1 host-font-removed=0
fonts pptmstr reassign exit=0 done=1 host-font-removed=0
fonts pptmstr clear exit=0 done=1 host-font-removed=0
fonts pptmstr clear-lost exit=0 done=1 host-font-removed=0
fonts pptmstr same-count exit=0 done=1 host-font-removed=0
fonts pptmstr same-count-add exit=0 done=1 host-font-removed=0
fonts pptmstr same-count-cycle exit=1 done=1 host-font-removed=1
edits advtext exit=0 pass=17 fail=0
fonts advtext once exit=0 done=1 host-font-removed=0
fonts advtext reassign exit=0 done=1 host-font-removed=0
fonts advtext clear exit=0 done=1 host-font-removed=0
fonts advtext clear-lost exit=0 done=1 host-font-removed=0
fonts advtext same-count exit=0 done=1 host-font-removed=0
fonts advtext same-count-add exit=0 done=1 host-font-removed=0
fonts advtext same-count-cycle exit=1 done=1 host-font-removed=1
faults none exit=0
faults table-raises exit=0
faults table-raises-once exit=0
faults table-cell-raises exit=0
faults menu-copy-raises exit=0
faults key-copy-raises exit=0
faults describe-raises-once exit=0
faults draw-raises-once exit=0
faults find exit=0
faults stream-find exit=0
faults stream-table exit=0
proof run exit=0  "imtypeset": "in use", "library calls per rich_pane.draw": 3.03
bundle_host advtext exit=0 selection: (74, 284) layouts: 2 frames: 30 
bundle_host pptmstr exit=0 selection: (74, 284) layouts: 2 frames: 30 
672 lines added, 19 removed
git apply --check passes in /home/wreel/Source/pptmstr at 4974657
applied in a scratch copy
ruff exit=0
black exit=0
mypy exit=0 Success: no issues found in 47 source files
pytest exit=0 2569 passed, 3 xfailed in 25.27s
self-hosted library build exit=0
python_binding exit=0 1/1 Test #1: python_binding ...................   Passed   16.43 sec 100% tests passed, 0 tests failed out of 1 
d3b1c65db358f626  /home/wreel/Source/imtypeset/src/imtypeset.cpp
03ad751c49b0bbab  /home/wreel/Source/imtypeset/src/imtypeset_document.cpp
a465bff42bd67400  /home/wreel/Source/imtypeset/src/draw.cpp
3021083aeba1a976  /home/wreel/Source/imtypeset/src/imtypeset_c.cpp
13:04
```

## What I changed

`python/imtypeset/__init__.py` (Document edits, `edit_error`, docstring);
`python/spike_bundle/pptmstr_overlay/{rich_pane.py, typeset.py, pptmstr.patch}`, `make_patch.sh` (new);
`python/spike_bundle/pptmstr_patch_faults.py` (new, the harness);
`python/spike_bundle/{pptmstr_corpus.py, pptmstr_census.py, pptmstr_real_panes.py, measure_pptmstr.sh}`;
`tests/python/test_binding.py`; `docs/advtext-rework-plan.md`;
`tools/bundle_route/{font_lifetime.py, check_document_edits.py}` (new);
and, outside the files the board lists for me, one comment in `examples/python/bundle_support.py`
(the false DPI sentence; its tasks are complete and nobody else is in it).
No file under `src/`, `include/` or a CMake file.

## Not run, not verified

- A real display, GPU, mouse or an actual DPI change: every mouse path is synthetic events, and
  the DPI answer is the read of hello_imgui plus a run that shows the callback is not called again
  by itself in 60 frames. I did not change a monitor's scale.
- The wheels' hello_imgui is taken to be the tagged commit; nothing in a wheel records it.
- The `RemoveFont` and `GetFont` halves of the font fix: read only. The `add_font` half was run on
  a scratch copy, in the pptmstr wheel only.
- `scripts/verify_rich_render.py` of pptmstr against the patch (it warps the real pointer).
- macOS, Windows, the `fork:` header route, a wheel upgrade.
- `pptmstr_real_panes.py` and `measure_pptmstr.sh` after the privacy change: argument parsing was
  read and `pptmstr_census.py` run; the two were not run end to end (`measure_pptmstr.sh` passes
  `sh -n`).

## Least sure of

1. **Three failing frames before switching off** is a number I chose. A pane whose imtypeset path
   fails every OTHER frame never reaches it and would alternate renderers; I did not build a case
   that does that and nothing I injected behaves so.
2. A frame that fails after drawing began is left as it is: text drawn, tables possibly missing,
   or (when `Document.draw` itself raised) blank for that one frame. I chose that over ever
   drawing a frame twice, as the brief asks; it is one frame, and it is logged.
3. `Document._refused` falls back to "positions are bytes" when the library holds another number
   of blocks after refusing. It is exact for the only case that exists (the document was
   emptied) and a guess for any other; doc-fixes' "Set checks count first" removes the case.
