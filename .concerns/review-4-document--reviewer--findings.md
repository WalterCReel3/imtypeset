# review-4-document findings (filed by the lead from the reviewer's report, 2026-10-10)

ALL READ-DERIVED; nothing run. Symbols first; line numbers drift under dist's renames.

1. `AppendDocumentText` (src/imtypeset_document.cpp) ends `return report(b.text_error)`;
   `validate_runs` (src/imtypeset.cpp) stores ImTypesetError_InvalidArgument into that same sticky
   `text_error`. Input: SetDocumentBlocks, one Paragraph "abcdef", runs [{3,5},{0,2}] (or any
   overlapping pair, e.g. bold inside a link) -> returns true. Then AppendDocumentText(doc, " more")
   appends AND returns false/InvalidArgument, every time. Python `Document.append_text` raises; the
   pptmstr patch then disables imtypeset for the run.
2. pptmstr patch (python/spike_bundle/pptmstr_overlay/pptmstr.patch): `_draw_tables` has begin_child
   … `_draw_table` … end_child with no try/finally; a raise there makes `_draw_classic` draw inside
   the open child and leaves EndChild missing. Same shape: begin_popup_context_item … doc.range_text
   … end_popup. A raise after doc.draw() has emitted gives one frame with both renderers.
3. `DocumentView`'s retained selection (selection=NULL) is cleared only by SetDocumentBlocks:
   after ReplaceDocumentBlocks(doc, 0, 1, NULL) a selection in block 5 covers what was block 6.
4. `map_code` does not walk UTF-8 as the strict `decode_utf8` (src/style.cpp) does. A Code block with
   "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80" or "\xF8\x88\x80\x80" makes the counts disagree and
   `materialize` assigns all-zero source_offsets: hit tests answer 0, selection/copy break, no error.
   Reachable from C/C++ hosts (not from Python str).
5. View cache (`view_of`, limit max(32, 2*shown_blocks + 8)) is not bounded at 64 and never shrinks:
   DrawDocumentToBuffers with a clip covering 10,000 blocks and a whole-document selection keeps
   ~10,000 DocView copies until DestroyDocument, scanned linearly afterwards.
6. DescriptionCut is coded consistently ("applied in part") but include/imtypeset.h at
   ImTypesetBlockKind_Table still says InvalidArgument; the Python Document docstring says such an
   edit raises (it returns False; from_blocks discards it); a bad ImTypesetContainer::Parent, an
   unknown container Kind and out-of-order runs are never noted.
7. SetDocumentBlocks clears the document before `Document::splice` can refuse (BlockCount =
   4,194,305 -> InvalidArgument with the document already emptied). Python replace_blocks/set_blocks
   assign self._texts before `_outcome` raises: mirror desync on any hard failure.
8. tests/test_api_document_golden.cpp `take` fails only above 2 sp then SUBSTITUTES the golden value
   (a tolerance, which CLAUDE.md forbids); `golden_block` assigns spec.interline and spec.strut
   wholesale after checking two widths (Strut::filling, interline stretch/shrink never compared);
   first_line forced to BOX_AND_SHAPE; marker_gap replaced unconditionally; a block number past
   `inputs` skipped unchecked. `set_document_hook` is unreachable from public headers/C/Python but is
   compiled into the shipped library.
9. pptmstr patch `_sync`/`_marks`: find marks index lower-cased text (lower() can change length);
   _FIND_MARK_CAP tested per block not in the inner loop; every streaming frame re-searches all text
   while a find is active; a streaming table's Host extent resets to a guess on every change.
10. `layout_window` lays out every zero-extent unit above the window in one frame (a million
    Extent-0 Host blocks = one O(N) frame); GetDocumentStats is O(blocks).
11. `describe()`/`stretch` does not clamp the start: TextStart > TextSize forms a pointer past the
    buffer (length 0, not dereferenced; still UB to form). A 256-byte marker cut can split a UTF-8
    character.
12. Exceptions-enabled builds: PushClipRect/PopClipRect in `draw_block` and BeginChild/EndChild in
    DocumentView are not unwound on a throw between them.
NOT REACHED, flagged as a possible use-after-free: whether `validate_fonts`/`add_font` survive
hello_imgui emptying the atlas before typeset.load_fonts() runs a second time (DPI change) — the
context would hold ImFont* the runner freed. Also: code under ImTypesetCodeOverflow_Wrap and
index_of's first-at-byte rule; an unmeasurable code character shifting map_code_indices by one.
Confirmed by reading: host counts/offsets bounded; serials; splice; view invalidation; the tab-in-code
fix (index_of first-at-byte for code, last for text); C ABI document entries.
