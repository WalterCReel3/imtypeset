# swap-api-answers — result (builder, 2026-10-10/11)

The answers as the other session reads them: `docs/advtext-swap-status.md`
("API changes since `5654eee`", "Answers (item 7)", and item 8 under "Border
questions"). Decisions and measurements: `docs/decisions.md` **D53**. Nothing
is committed or staged.

## Per question

"Reproduced" means a test was written here first and its outcome seen.

| | reproduced? | outcome | test |
|---|---|---|---|
| (a) `range_text` around a block that gives nothing | **yes**: nothing and no separator | **kept, stated, pinned**. It is what the reference copies for text, rule, text (`advtext/ui/units.py`, `UnitModel.copy_text`; read, not run). Header "WHAT A COPY GIVES" reworded; Python `range_text` docstring | `a_copy_has_one_separator_between_two_texts_and_none_beside_a_block_that_gives_nothing`; Python `test_a_block_that_gives_no_text_leaves_no_trace_in_a_copy` |
| (b) a Mark over part of a Formula run | **established** (nobody had said what it draws): whole when the range holds the formula's first byte, nothing otherwise; same for Ink, Behind, Under, selection rectangles and copy; run formulas and dollar formulas alike | **stated, pinned**; no code change. Header at `ImTypesetTextRange` and `ImTypesetMark` | `a_mark_takes_a_formula_whole_when_it_holds_its_first_byte_and_not_at_all_otherwise`; Python `test_a_mark_takes_a_formula_whole_or_not_at_all` |
| (c) ImGui context destroyed without `DestroyContext` | **yes**, under ASan + UBSan. Refusals were already clean (NoContext). **One defect:** `GetFont` returned the dangling `ImFont*` | **fixed** (`GetFont` null unless bound, `src/imtypeset.cpp`); lifetime rule in the header at `CreateContext`/`DestroyContext` and in Python `destroy_context`. A new ImGui context at the SAME ADDRESS (seen naturally in every debug run; forced in the test by recycling the block) is taken for the old one: roles found empty, NoFont, re-adding fonts works, host font untouched. **Two collisions reasoned and NOT reproduced, NOT fixed** (D53 item 5 says what would close them) | `a_context_whose_imgui_context_is_gone_refuses_to_draw_and_can_still_be_destroyed`, `a_new_imgui_context_at_the_address_of_the_one_destroyed_finds_every_role_empty`; Python `test_a_context_left_behind_by_its_imgui_context_refuses_and_is_destroyed` |
| (d) Code block ending in a blank line | **yes**, and a mismatch found: positions and copies end at the last character, `GetDocumentBlockTextSize` answers the bytes handed in | **both kept, now stated**: an append goes on from the bytes held, and trailing blank lines become interior when text arrives. Header "POSITIONS", `ImTypesetBlockKind_Code`, `GetDocumentBlockTextSize` | `a_code_blocks_text_ends_at_its_last_character_and_a_copy_and_its_positions_say_so`; Python `test_a_code_block_ends_at_its_last_character` |
| (e) a refused import leaves `imtypeset._capi` behind | **yes**: the new test fails against the package as committed (run against `git archive HEAD python/imtypeset`) | **fixed**: a refused import deletes every `imtypeset.*` from `sys.modules`. The mapped shared object cannot be undone portably: said in the code and the status file | Python `test_a_refused_import_leaves_nothing_a_second_attempt_meets` (three refusals, another copy of the package, then a good import, one process) |
| (f) sharing the host's fonts | measured: a second copy of 7 faces costs 1.85 MB of ImGui allocations unbaked, 1.88 MB more drawn; textures 1.84 → 3.15 MB | **adoption declined** (the atlas font is a rewritten copy, D27; what soundness would need is in the status file). **Sharing the other way built**: C-only `ImTypeset_GetFontAtlasIndex`, Python `font_atlas_index`; the host draws its own text with the library's font. Role font and host copy give equal `CalcTextSize` for 6 text faces × 3 sizes (FreeType) | `a_host_draws_its_own_text_in_a_roles_font_and_it_is_the_files_text`, `what_a_second_copy_of_every_face_costs_the_atlas` (prints); C `test_c_api.c`; Python `test_a_roles_font_is_found_in_the_atlas_by_its_place`; wheels `check_swap_answers.py` |
| (g) C1 | — | **done**: null `draw_list` is the current window's, refused outside a frame | `no_draw_list_is_the_current_windows_and_is_refused_outside_a_frame`; C; Python `test_no_draw_list_is_the_current_windows` |
| C3 | 2.0 µs a mark before (one run, 2,000 marks) | **in part**: `pack_marks` by `struct`, 0.5 µs. No find in the library (declined for now: advtext's by the border) | Python `test_marks_packed_from_tuples_are_the_marks` |
| C8 | — | header comment at `ImTypesetRun::Tag` | — |
| C2, C4, C6, C7 | not reproduced | **queued**, each with its reason in the status file (C2, C7: the sources `tables-core` rewrites; C4: a second copy path that must agree to the byte; C6: the measurement was not repeated here) | — |
| C5 | — | item 3 | — |
| item 8 | read by a helper agent (`render_litmus.py`, `backends/svg.py`, `test_litmus.py`, the `svg` target), checked against FS-S3 section 5; nothing run | **assessment only**, in the status file, recommendation marked as an agent's: no public glyph-position API; the litmus artifacts become this repository's tests | — |

## Public changes

- `include/imtypeset.h`: **no declaration changed.** Comments only, plus two behaviours a caller can observe: `DrawParagraph`/`DrawDocument` with a null `draw_list` draw into the current window (they answered false, `InvalidArgument`); `GetFont` answers null when the context's ImGui context is not current.
- `include/imtypeset_c.h` + `src/imtypeset_c.cpp`: **one new function**, `int ImTypeset_GetFontAtlasIndex(ImTypeset_Face)`, C-only (row `C_ONLY(GetFontAtlasIndex, ...)` in `tests/test_c_api_map.cpp`). `IMTYPESET_C_ABI_VERSION` not bumped, as for `GetContextSerial`.
- `python/imtypeset`: new `font_atlas_index`; `draw(None, ...)`; refused import cleans up; `pack_marks` rewritten (same result); docstrings. `_capi.PROTOTYPES` has the new function, so **the package refuses any library built before this tree** (both sidecars and every pyhost must be rebuilt).
- Wheel probe census: unchanged (no new ImGui field read; `ImFontAtlas::Fonts` and `GetWindowDrawList` were already used).

## For `tables-core` (Document sources I touched)

- `src/imtypeset_document.cpp`: `DrawDocument` only — the null-`draw_list` branch after `can_draw`. Nothing else.
- `src/imtypeset.cpp`: `GetFont`, `DrawParagraph`.
- `src/imtypeset_c.cpp`: one function added after `ImTypeset_GetFont`.
- `tests/test_api.cpp`: one expectation (`DrawParagraph(p, nullptr)` inside a frame now succeeds).
- New: `tests/test_api_answers.cpp`, `tools/bundle_route/check_swap_answers.py`. Extended: `tests/test_c_api.c`, `tests/test_c_api_map.cpp`, `tests/python/test_binding.py`.
- Docs: `docs/advtext-swap-status.md`, `docs/decisions.md` (D53 appended; if another task took the number, one is renumbered).

## What ran (GCC 12.2, FreeType 2.12.1, `-j3`, every process guarded; private dirs, removed)

| configuration | what | result |
|---|---|---|
| debug, un-narrowed, `-Werror`, Python host | ctest: `test_api` (42 tests / 71,954 checks), `test_api_answers` (8 / 2,239), `test_api_document` (24), `test_api_document_golden` (5), `test_api_math` (19), `test_c_api` (486 checks), `test_c_api_map` (7), `test_blocks` (23), `python_binding` (58), `amalgamation` | 10 of 10 |
| ASan + UBSan, un-narrowed, `-Werror` | the same nine without `python_binding` | 9 of 9, no report |
| ASan + UBSan, Python | `tests/python/test_binding.py` by hand, `LD_PRELOAD=libasan.so`, `detect_leaks=0` | 58 OK, no sanitizer line |
| both sidecars rebuilt from this tree (`python/host/build_bundle_library.sh`, wheels 1.92.900 and 1.92.801) | gate accepted (probe; 22 probes each); `check_swap_answers.py` 18/18; `check_document_edits.py` 17/17; `font_lifetime.py same-count-cycle` through `tools/shot.sh` exit 0, `application_fonts_removed` 0 | pass in both |
| clang-format 14 `--dry-run -Werror` | the nine C/C++ files touched | clean |
| **red, not mine** | `test_demo` in the debug dir **aborts** (ImGui "Too many vertices in ImDrawList using 16-bit indices") on `galleries`' files as saved at 23:53; told the lead. Not run under ASan | |
| **not run** | any preset directory; the whole suite; release; stb_truetype; 16-bit `ImWchar`; the exceptions build; `mutate_gate.py`; the applications themselves in either wheel; clang, MSVC | |

## Least sure of

1. **(c), the same-address case.** The header now says what happens "in every case tested, not a promise". The two collisions in D53 item 5 are real by reading; I could not make the host's font land on the old font's address to show the first, and did not build the fix because it reads two more ImGui fields that the wheel probe would have to learn. If a test suite on the advtext side recreates ImGui contexts and does not call `destroy_context()` each time, this is where it would bite.
2. **(f) is an answer by inversion.** The request was to share the HOST's fonts; what I give is the host using the LIBRARY's. It saves the same memory only if advtext really stops adding its seven faces. Equal text metrics were shown for six bundled faces under FreeType only.
3. `pack_marks` now reads a mark by index (`mark[0]`, `mark[1]`...) instead of by field name: a mark object that is not a tuple in `Mark`'s field order would have worked before by accident of `Mark(*mark)` and still needs to be a sequence.
4. The item 8 survey was done by a helper agent reading the other tree; I checked it against FS-S3 and not against the files.

## Noticed, outside the task

- A soft-failure path in Python `Document.append_text` compares the last block's size with `GetDocumentBlockTextSize`; that is right only because the function answers the bytes handed in (D53 item 3). Anyone who "fixes" that function to the trimmed size breaks it.
- `ImTypeset_DestroyContext`'s comment in `imtypeset_c.h` says to destroy the ImTypeset context before the ImGui one because the C layer's buffers are ImGui's allocator's; destroying afterwards was run under ASan here (Python lifetime test) and is clean, since the allocator functions outlive the context.
- I ran `git add -N tests/test_api_answers.cpp` once by reflex and undid it with `git reset`; the index is as it was.
