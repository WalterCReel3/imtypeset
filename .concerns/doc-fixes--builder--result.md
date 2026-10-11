# doc-fixes — result (builder, 2026-10-10)

Scope: the board entry `doc-fixes`, as superseded by `.concerns/doc-fixes--lead--scope.md`, over
`.concerns/review-4-document--reviewer--findings.md` (read-derived; each was made to fail first).
Decisions and reasons: `docs/decisions.md` **D45**. Text for other shared documents:
`.concerns/doc-fixes--builder--doc-additions.md`.

## Per item

"Reproduced" means a test was written first and FAILED against the tree as I found it.

| item | reproduced? | what was done | test |
|---|---|---|---|
| review 1: an applied append answers "nothing done", for good | **yes** (`AppendDocumentText` false / InvalidArgument three times running after a set with runs `{3,5},{0,2}` or `{0,6},{2,4}`; the set itself answered true) | Run trouble is `ImTypesetError_DescriptionCut` ("done, differently"), set in the one place runs are validated (`validate_runs`, `src/imtypeset.cpp`), so for `SetParagraphText` too. `describe()` reports a block's text trouble at set time (`block N: …`). `AppendDocumentText` reports what THAT call cut or left out, and nothing else; the block's standing trouble stays in `GetDocumentBlockError`. | `an_append_that_was_applied_does_not_answer_for_what_an_earlier_call_found`, `a_paragraph_whose_runs_are_out_of_order_is_set_and_says_done_differently` (`tests/test_api_document.cpp`); the same through C in `tests/test_c_api.c` `test_document` |
| review 8: tolerance and overwritten fields in the golden replays; hook compiled into the library | by reading (`resolved = exact` after a `> 2` test, in BOTH replay files; `spec.interline = in.interline`, `spec.strut`, `first_line`, marker gap) | Every substitution removed and every comparison `==`: `tests/document_reference.h` (`hold_block`, `hold_theme`, `use_reference`). It all holds exactly: 8,967 lengths over the block goldens, 2,146 over the doc goldens, interline glue whole, strut, lineskip limit, marker face and size, the quote theme through `ImTypesetDocumentOptions`. Same coverage as before: 67 of 69 block cases (211 compositions, 34,647 commands), 5 doc tests. A block number past the inputs now fails. **The hook is gone from the library**: `DocumentHook`, `set_document_hook`, `ImTypesetDocument::hook`/`run_fonts`, `ImTypesetParagraph::run_fonts` deleted. What remains is ONE internal `bool`, `ImTypesetDocument::reference_rules` (reference `DocumentRules` + first line narrowed by the indent), of the standing of `ImTypesetContext::tex_rules`; it cannot leave the library without building the sources twice, and the header of `src/imtypeset_document.h` says so. Nothing "cannot be expressed": no value needed an exception. | both replay files |
| review 4: two UTF-8 walkers | **yes** (`"a\xC0\x80b"`, `"a\xE0\x80\x80b"`, `"a\xED\xA0\x80b"`, `"a\xF8\x88\x80\x80b"`: no rectangle for any character after the first, hit tests answer 0) | One walk, `walk_code` in `src/blocks.cpp`, over `decode_utf8`'s codepoints: `set_code(..., node_bytes)` hands back the byte of each node it makes and `code_extent()` counts lines and the end of what is set. `map_code` and its "the two walks disagree" branch are deleted. | `a_code_block_of_ill_formed_utf8_has_positions_at_the_bytes_its_characters_start_at` (also a wrapped line with a tab: the reviewer's "not reached" `index_of` under `CodeOverflow_Wrap` holds) |
| review 3: the retained selection is not moved by `ReplaceDocumentBlocks` | **yes** | Moved with its blocks (`moved_by_edit`); one-for-one replacement keeps it; where the count changes, the part in the replaced blocks is given up. Header says so at `DocumentView`. | `the_selection_a_view_keeps_follows_its_blocks_through_an_edit` |
| review 5: view cache unbounded, never shrinks | **yes, on the second large frame** (the first is limited by the previous frame's count): 2 x blocks shown | `MAX_VIEWS` = 256, and `trim_views` after each draw: 256 after a window over 2,000 selected blocks, 32 after a screenful. | `a_window_over_thousands_of_selected_blocks_keeps_a_bounded_number_of_views` |
| review 7 (C++ half): `SetDocumentBlocks` empties the document before the splice can refuse | **yes** | The count is checked before anything is given up. | `a_set_that_is_refused_leaves_the_document_as_it_was` |
| review 6: `DescriptionCut` in header / unnoted cuts | **yes** for the unnoted ones (a bad `Parent`, an unknown container `Kind`: the set answered true) | Both noted; header at `ImTypesetBlockKind_Table` says `DescriptionCut`; the enum comment lists what the code now covers. Python docstring: `py-fixes`. | `a_container_or_a_marker_that_was_cut_is_said_too` |
| review 10: zero-extent units above a window laid out in one frame; `GetDocumentStats` O(blocks) | **yes, exactly as stated, and once**: 5,000 host blocks of Extent 0 with no space between: 5,012 layouts in the frame that starts at their height, 0 in the next, 12 blocks drawn a frame; a window far down touches none (first attempt did not reproduce: with `SpaceBelow` the blocks are 8 px apart and not at one height) | **Not changed, by decision** (D45): it is linear in what the host handed over, once; documented in the header (BLOCKS OF NO HEIGHT). `GetDocumentStats`: the header already said one step a block; unchanged. | `a_run_of_blocks_of_no_height_is_laid_out_once_and_only_by_a_window_at_its_height` pins the counts |
| review 11: pointer past the buffer; marker cut inside a character | pointer: **not reproducible as a test** (formed, never dereferenced; no sanitizer flags it); marker: **yes** (255 bytes + `é` ended in U+FFFD) | `stretch` clamps the start to the text; the marker is cut before the character that crosses 256 bytes, and the cut is noted. | marker: `a_container_or_a_marker_that_was_cut_is_said_too` |
| review 12: unwinding leaves the child open / the clip rectangle pushed | `EndChild`: **yes** (ASan + exceptions build, guards disabled: 1,686 failed checks). Clip rectangle: **did not reproduce** with `bad_alloc` (no allocation between push and pop on the input swept); guard added by reading | RAII guards with `IMTYPESET_NO_UNWIND` (moved to `src/context.h`). **The sweep found four more defects, all fixed**: a code block flagged built before it was (drew nothing for good); `sync()` flagged a block seen before its line index was built (same symptom); a `DocView` leaked when listing it threw; the document's arena left with `_boxes`/`_box_nesting` out of step after an unwound build, so the next formula tripped "box contents name a box that does not exist yet" (fixed by rewinding: `Rewound` in `src/blocks.cpp`, `Lent` in `materialize`). Also `has_spec` now set last (by reading). | `a_throw_from_anywhere_in_a_document_view_leaves_the_hosts_window_current`, `a_throw_from_anywhere_in_drawing_a_clipped_code_block_leaves_the_clip_stack_as_it_was` (`tests/test_exception_safety.cpp`): 842 and 65 allocations unwound from |
| board 1: tab in code | already fixed, as the scope says: `a_tab_in_a_code_block_is_selected_whole_by_its_one_byte` passes. Plain-paragraph API checked for the same shape: **no defect** (a tab is one glue item) | — | `a_tab_in_a_paragraph_is_one_space_selected_whole_by_its_one_byte` (paragraph and document paths) |
| board 2: "source bytes as they were handed in" vs the dropped closing line end | by reading | **The header moved.** The reference drops trailing blank lines (`advtext/markdown.py` `_code_block`; read, not run). | `tests/test_c_api.c` already expects it |
| board 3: `DocumentView` (0,0) in an auto-resizing window | measured: **1 block drawn in 6 frames**, not 0 (ImGui gives the child a few pixels); 16 with a size | Header reworded ("next to nothing"). | `a_view_of_no_size_in_a_window_that_fits_its_contents_shows_next_to_nothing` |
| board 4: `DescriptionCut` audit | read every site and binding | Three sites set it (`describe`, `declare_containers`, `validate_runs`); all apply the edit. C enum 12, `IMTYPESET_C_ENUM`, Python `Error.SOFT`: consistent. The one inconsistency was the header's Table sentence. | — |
| board 5: C keeps `(text, text_end, offset)` | — | Comment added at the declarations in `include/imtypeset_c.h`; D43 already recorded it, D45 repeats it. | — |
| board 6: script mode | already `-rwxr-xr-x` (dist did it) | nothing | — |

## Public and C changes (for `py-fixes`)

**No declaration, struct, enumerator value or C signature changed.** `tests/test_c_api_map.cpp`
and the static_asserts are untouched and pass. Comments changed in both headers. What a caller
can observe:

1. A run out of order: `SetParagraphText`, `LayoutParagraph`, `GetParagraphError` say
   **`DescriptionCut` (12)** where they said `InvalidArgument` (3). Python:
   `Paragraph.set_text`/`layout` return False where they raised.
2. `SetDocumentBlocks` / `ReplaceDocumentBlocks` now answer **false + `DescriptionCut`** (they
   answered true) for: runs out of order, a container `Parent` or `Kind` that is not one, a
   marker over 256 bytes; and false + `TextTooLong` for a block cut at 16 MiB. Messages start
   `block N: ` or `container N: `. Python `from_blocks`/`set_blocks`/`replace_blocks` get False
   more often.
3. `AppendDocumentText` answers true whenever THIS call was applied whole, whatever the block
   already had wrong; false + `TextTooLong`/`DescriptionCut` only for this call's own cut.
4. A block with dropped runs has `GetDocumentBlockError` = `DescriptionCut` (was
   `InvalidArgument`), so `DrawDocument`/`DocumentView`/`LayoutDocumentWindow` over it answer
   false with a SOFT code where Python used to raise. **They answer false every frame while the
   block is in view** — a host must not read a False from `draw` as failure.
5. `SetDocumentBlocks` with more than 4,194,304 blocks leaves the document untouched.
6. `DocumentView(selection=NULL)`: `ReplaceDocumentBlocks` moves the kept selection.
7. Markers are cut at a character boundary; code positions over ill-formed UTF-8 are right
   (C/C++ hosts only).

## What ran

GCC 12.2.0, FreeType 2.12.1, `-j3`, every binary under `( ulimit -v 6000000; timeout … )` or
`ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout …`. Private directories, removed afterwards.

| configuration | what | result |
|---|---|---|
| debug, un-narrowed configure with `IMTYPESET_BUILD_PYTHON_HOST=ON`, only the named targets built | ctest: `test_api` (40 tests / 71,574 checks), `test_api_document` (24 / 26,800), `test_api_document_golden` (5 / 28,253), `test_c_api` (464 checks), `test_c_api_map` (7 / 694), `test_blocks` (23 / 128,674), `test_demo` (13 / 126, demo-guard's file as it stood), `test_exception_safety` (skips), `python_binding`, `amalgamation` | 10 of 10 pass, twice |
| ASan + UBSan (as the `asan` preset's flags, plus the Python host) | ctest: the same seven test binaries and `amalgamation` | 8 of 8 pass, no report |
| ASan + UBSan, Python | `tests/python/test_binding.py` against the sanitised `libimtypeset_pyhost.so`, by hand with `LD_PRELOAD=libasan.so` and `detect_leaks=0` (the interpreter leaks) — NOT through ctest | 49 tests OK, no ASan or UBSan line |
| ASan + UBSan, exceptions on, `tests/imconfig_throwing.h` | `test_exception_safety` | 9 tests / 63,379 checks, no leak |
| release, tests narrowed | `test_api_document`, `test_api_document_golden`, `test_c_api`, `test_c_api_map`, `test_blocks`, three examples | 8 of 8 pass |
| **not run** | any PRESET directory (`debug`, `asan`, `release`, `amalgamation`, `stb`, `wchar16`, `exceptions`, `fp-host`, `bundle-config`, `imgui-next`); the whole suite anywhere; stb or 16-bit `ImWchar`; the sidecar inside either wheel; the two spikes; clang, MSVC; any timing | |

## Files

Library: `src/imtypeset_document.{h,cpp}`, `src/blocks.{h,cpp}` (`walk_code`, `code_extent`,
`set_code(node_bytes)`, `CodeLines::offsets`, `Rewound`), `src/imtypeset.cpp` (`validate_runs`;
`run_fonts` gone; macro moved out), `src/context.h` (`IMTYPESET_NO_UNWIND` moved in; `run_fonts`
gone), `include/imtypeset.h` and `include/imtypeset_c.h` (comments only).
Tests: `tests/test_api_document.cpp` (+10 tests), `tests/test_api_document_golden.cpp`,
`tests/document_reference.h` (new), `tests/test_c_api.c`.
**Outside the files the scope names, and why:** `tests/test_api.cpp` (four expectations, the
paragraph's run error code: the header files `InvalidArgument` under "nothing done" and the test
pinned it for an applied set); `tests/test_exception_safety.cpp` (two tests: the only place
unwinding can be tested). Nobody else's task lists either.
Docs: `docs/decisions.md` (D45; two pointers in D43).
New file-local names, checked unique across `src/` and by the `amalgamation` ctest:
`measure_code`, `view_limit`, `cut_at_character`, `trim_views`, `too_many_blocks`,
`moved_by_edit`, `walk_code`, `Rewound`.

Git: nothing committed. I staged three new files (`tests/document_reference.h`, the progress
file, the doc-additions file) following the session's own rule to `git add` on creation, which
contradicts "do not git add"; nothing else was staged.

## Not done, and what I would not vouch for

- `NodeArena::add_box` / `push_disc` (`src/nodes.cpp`, not mine) still push to two vectors in
  turn; I rewind at the document's three call sites. A paragraph is safe because it clears its
  arena each build. Any OTHER long-lived arena has the same exposure.
- An unmeasurable code character shifting `map_code_indices` by one (reviewer's note): not
  reached; I found no input that makes `stand_in` answer something unmeasurable.
- The font-lifetime question under hello_imgui (`validate_fonts` after the atlas is emptied):
  `py-fixes` has it; nothing was relayed to me and `src/imtypeset.cpp` was not changed for it.
- The unwinding sweep covers `DocumentView` and `DrawDocument` on one small document. The edit
  calls, the hit tests and the range questions were not swept.
- A partially overlapping run is still cut silently (the header has always said so).

---

# Addendum: `doc-fixes-now` (two additions the lead relayed after `doc-fixes` closed)

Decision text: `docs/decisions.md` **D46**.

## B. New public declaration: `GetContextSerial` (lead's ruling)

| layer | what |
|---|---|
| `include/imtypeset.h` | `IMTYPESET_API ImU64 GetContextSerial();` after `SetCurrentContext`, with the lead's comment |
| `src/context.h`, `src/imtypeset.cpp` | `ImTypesetContext::serial`; `g_contexts_made`, incremented in `CreateContext`; the getter answers for the CURRENT context, 0 with none |
| `include/imtypeset_c.h`, `src/imtypeset_c.cpp` | `ImTypeset_U64 ImTypeset_GetContextSerial(void)` through `guarded` (0 for none or an exception); the header's "what a function returns" list has the case |
| `tests/test_c_api_map.cpp` | row `BOTH(GetContextSerial)` |
| `python/imtypeset/_capi.py`, `__init__.py` | prototype `(c_ulonglong, [])`; `get_context_serial()`, in `__all__` |
| tests | `tests/test_api.cpp` `a_contexts_serial_is_its_own_and_is_never_used_again` (64 contexts made and destroyed in turn all came back at ONE address; 64 distinct serials); `tests/test_c_api.c` (none: 0; two contexts differ; destroy + create does not repeat); `tests/python/test_binding.py` `test_a_contexts_serial_is_its_own_and_is_never_used_again` |

**This IS a public and C ABI addition** (one function each; no struct or enumerator changed).
Consequence: `python/imtypeset` refuses a library that lacks any prototype, so every sidecar or
pyhost `.so` built before this must be rebuilt.

## A. Font lifetime (reproduced by `py-fixes` in both wheels; reproduced by me in C++)

Reproduced: `tests/test_api.cpp`
`an_atlas_refilled_with_as_many_fonts_between_two_frames_has_none_of_the_roles` ended, against
the tree as found, in ImGui's `ImFontAtlas::RemoveFont: Assertion 'removed' failed` (our
`RemoveFont` handing ImGui a freed `ImFont*`).

Fixed at the cause, NOT as proposed: `validate_fonts` skipped while (frame count, number of
fonts) were unchanged. Its key is now (frame, count, `FontId` of the atlas's last font, the
atlas). ImGui assigns `FontId` from a counter only the atlas's constructor sets and appends
new fonts, so no font can leave the atlas without the key changing (read in
`.deps/imgui/imgui_draw.cpp` and in v1.92.0 from `../imgui`'s objects). The proposed three
resets of `fonts_checked_frame` would have fixed `add_font`, `RemoveFont` and `GetFont` and
left every draw call to the same stale skip. No new ImGui field is read, so the sidecar's probe
census is unchanged. The comment over `validate_fonts` and the header at `RemoveFont` say what
is now true.

After: 24 refills, 3 of the host's new fonts at the address a role's font had, 0 stale
answers, 0 host fonts lost. In the wheels, sidecar rebuilt from this tree
(`python/host/build_bundle_library.sh`, both interpreters, gate accepted):
`tools/bundle_route/font_lifetime.py same-count-cycle` **exits 0, `application_fonts_removed`
0, in wheel 1.92.900 (advtext) and 1.92.801 (pptmstr)**, where it exited 1; `same-count-add`
0 in both; `same-count` and `clear` 0 in advtext's (not run in pptmstr's).
`tools/bundle_route/check_document_edits.py` passes in pptmstr's wheel against this library.

## What ran for the addendum

See the last progress lines for the final counts; the ASan + UBSan and release runs were
repeated after both changes.

Final runs after both additions (private directories, guarded, `-j3`, removed afterwards):
debug ctest 10 of 10 (`test_api` now 42 tests / 71,955 checks, `test_c_api` 480 checks, the
document tests as above, `test_demo`, `python_binding` 50 tests, `amalgamation`); ASan + UBSan
ctest 8 of 8, and `tests/python/test_binding.py` 50 tests OK against the sanitised library by
hand under `LD_PRELOAD=libasan.so` with `detect_leaks=0`; release 9 of 9 (`test_api`, the
document tests, the C tests, `test_blocks`, three examples). NOT re-run after the additions:
the exceptions build (`test_exception_safety`), and everything listed as not run above.
