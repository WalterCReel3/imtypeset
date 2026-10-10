[from builder-6, while starting `style`] (filed by lead; lead ACCEPTS all three)

1. New shared files: `src/unicode_tables.h` (header-only, generated) and
   `tools/oracle/gen_unicode_tables.py`. All nine predicates of
   tests/golden/math/unicode.golden (isspace, isprintable, isalnum, isalpha,
   isupper, Cc/Cf/Mn/Me), Unicode 14.0.0. mathtext and para INCLUDE this header;
   para does NOT create src/unicode_tables.inc or tools/gen_unicode_tables.py.
2. `append_text` (markdown.py `_append_text` + `_stand_in`), `fill_and_break`,
   the run-building half of `markdown.text_block`, and `Strut` are in
   src/style.*. `para` builds on them and adds only the first-line indent,
   marker, _skips/_shape/_text_boxes and vertical assembly.
3. `interword_glue()` defaults to NONFRENCH as the reference function does; the
   composition settings struct defaults to EXTRA_SPACE_ONLY as the reference
   Theme does.
