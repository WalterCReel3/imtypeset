# review-5-swap findings (filed by the lead, 2026-10-11) — ALL READ-DERIVED, nothing run

The reviewer's two concerns in full are on the board under task `review-5-swap` (read_board()).
Summary with the routing the lead chose:

F1 (high) -> tables-core. In tests/golden/doc-tables, a caret's `<pos>` (probe/sep/atomtext/original)
   and `<span>` are the dump script's own forward resolution (`TMap.pos`, `TMap.span` in
   tools/oracle/dump_doc_tables.py), not the reference's answer: selection.golden lines 528 (caret
   after "Name" = byte -1), 534, 596, 608. Also script arithmetic: `tgeom left`, `trow
   top/height/wrapped`, `linkarea row/column`, byte ranges of match/link/formula, stream `moved`,
   `npieces`, `nwrapped`, `mode`, `totals`. THE REFERENCE'S OWN (hold the core to these): `hit`,
   `word_*`, `line_*`, `block_at`, drag `start/end`, `copy`, `rects`, `tset`, `tnatural`, `built`,
   `sep` text, `blocktext`, match material range, linkarea rects. Reference behaviours that look like
   bugs and are the reference's: word selection crosses a cell boundary (528); a copy drops a tab with
   no selected text on one side (837, 841: `empty first\tlast` for a three-column row); an unwrapped
   row highlights as one rectangle (621); a probe at a row's middle in a column with no line there
   lands in another cell (547-552).
F2 (high) -> tables-core stage 0. Null draw_list after NewFrame(); EndFrame(); without Render():
   "inside a frame" is `GetFrameCount() > 0 && GetDrawData() == nullptr`, still true; then
   GetWindowDrawList() reads a null current window (.deps/imgui/imgui.cpp:8384, 8826). DrawParagraph,
   the same test near src/imtypeset.cpp:1637, DrawDocument.
F3 (medium) -> swap-fixes. Same-address ImGui-context collision (D53 item 5), concrete: G1 with 7
   role fonts (ids 1..7), a validate before any frame; destroy the ImGui context only; G2 at the same
   address, host adds exactly 7 fonts, AddFont(role): validate_fonts skips, add_font RemoveFont()s a
   stale pointer = possibly the host's font. Id match: the 4th of 7 (most-recently-freed-first,
   recalled) — a repro needs 4+ host fonts. For advtext: its tests mostly create ImGui contexts
   without destroying them, so add_font is refused NoContext and `load_fonts` calls disable():
   imtypeset silently OFF for the rest of the process — the likelier bite.
F4 (medium) -> oracle-fixes + swap-fixes. tests/test_demo.cpp Specimen: eleven CHECK_NEAR(0.0001)
   against typed-in 4-decimal numbers; one sp is ~2e-5 px. Dump integer sp from the pin; compare exactly.
F5 (medium) -> oracle-fixes. pinned_reference.py: require_all_modules() only in dump_doc_tables.py and
   AFTER the goldens are written; file-count walk follows symlinks where pin_reference.sh does not.
F6 (low) -> oracle-fixes. FORMAT-doc-tables.md: §1 asserts less than it says; §3.2 "an empty cell keeps
   its tab" over-claims; `lead` order; `stable` has no per-row cell count; STRASSE/straße needles cannot
   tell "searched as it stands" from lowercasing (a `STRA` needle would); no header-less table.
F7 (low) -> swap-fixes. python/imtypeset/__init__.py refused-import cleanup iterates live sys.modules;
   failed reload can leave a second _capi.
F8 (low) -> swap-fixes. Demo state cap 8 per set; nine live contexts round-robin evict and leak handles.
F9 (low) -> swap-fixes. bundle shim forwarders: add static_assert(std::is_pointer<ImGuiInputTextCallback>).
Confirmed by reading: copy separators `\t`/`\n` are the reference's; cell attribution; ~20 record kinds
decode from the format document; stream widths; GetFont refusal.
