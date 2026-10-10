[from builder-7] oracle-para done (filed and condensed by lead). tools/oracle/dump_para.py,
FORMAT-para.md, tests/golden/para/*.golden (253 cases, 11.3 MB); --verify 2,088 checks.

For `para`: INPUT per case is styled runs (`run <font> <breakable> <text>`, `hardbreak`, optional
`marker`) plus every Theme value and every metrics answer the reference consulted, so the port needs
no Theme and, for these cases, no font reader. EXPECTED: `nodes material`; core's break group
verbatim; then `assemble` (hung marker, strutted dims, vlist, vbox, cmds). FORMAT-para.md §5 lists
rules no single record shows. _text_boxes is at ui/__init__.py:1369-1422.

Reference findings (each in docs/divergences.md with its case) and LEAD RULINGS:
1. first_line_indent is applied TWICE in the reference (an empty box in the material AND in _shape);
   tex.web §1091 does only the box. RULING: `para` reproduces the reference exactly for the goldens
   and takes a parameter selecting where the indent is applied; `public-api` selects the TeX
   behaviour (box only, once) for its paragraph indent and tests it directly. Recorded as a
   deliberate departure from the reference.
2. A minted plane-15 codepoint typed as TEXT is set from the math face's variant glyph. Known; draw
   and font-loader need not prevent it.
3. White space collapses per run only: "a " + " b" as two runs gives two glues. RULING: `public-api`
   collapses white space across run boundaries before the builder, and says so in the header; `para`
   keeps the reference's per-run behaviour.
4. Hard-break fill glue is `plus 65536 fil`; par_fill_skip is `plus 1 fil`.
5. NBSP and U+202F are breakpoints.
List markers, hard breaks and mixed-size runs are in the goldens; `para` ports them.
