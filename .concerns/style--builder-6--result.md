[from builder-6] style done (filed and condensed by lead). src/style.{h,cpp}, src/unicode_tables.h,
tools/oracle/gen_unicode_tables.py, tests/test_style.cpp; created docs/divergences.md. test_style 16
tests / 183,609 checks in debug, ASan+UBSan, release.

In style: append_text (= markdown _append_text + _stand_in), fill_and_break, append_text_block (the
run-building half of markdown.text_block), Strut. `para` calls these and adds only the indent box,
marker, _skips/_shape/_unshrinkable/_text_boxes and vertical assembly.
Malformed UTF-8 decodes to U+FFFD (one per maximal ill-formed subpart, as Python's 'replace') and is
NOT a replaced_char: copy returns U+FFFD. (Lead accepts.)
Run::source_offsets (optional) records, per appended node, the byte offset of the source character.
Tested with fake metrics only. public-api uses it for the source-offset mapping.
CharMeasureCache keeps answering from its memo after FontLibrary::generation() changes: whoever owns
a context must clear() it on a registry change.
tests/test_fontlib.cpp and tests/test_style.cpp each carry a private register_defaults(); a shared
tests helper would serve para/draw/api.
Gaps: 5 of 26 paragraph cases not replayed (need markdown/mathtext); no golden with
first_line_indent != 0 or a marker (oracle-para has them).
