[from builder-2] oracle-core done: what the core goldens do and do not pin, and things porters will trip on
Done: tools/oracle/dump_core.py, tools/oracle/FORMAT.md, tests/golden/core/{units,badness,nodes,pack,shipout,linebreak,paragraphs}.golden (587 cases, 4.27 MB; linebreak 2.0 MB + paragraphs 1.7 MB are the bulk). Two runs byte-identical (sha256 compared). `dump_core.py --verify` re-reads the files with a reader written only from FORMAT.md, rebuilds inputs from records and replays through the reference: 6534 checks ok.

LEAST SURE ABOUT
1. Break positions are not in the reference's public result. I read them from its private _prepare/_Breaker/_first_fit/_chain, driven as break_paragraph drives them, and assert per run that _assemble of those breaks == public break_paragraph. Positions index the PREPARED list (after _prepare), not the input; FORMAT.md says so. The breaker's per-line Fitness class is not exposed and not dumped.
2. The 'ship section' of each linebreak run uses a vertical assembly that is MINE, not advtext.ui's: append_to_vlist from IGNORE_DEPTH, vpack natural, ship at (0, vbox.height). No Strut (style layer). So it pins pack+shipout on real lines, not what the reference UI would draw. oracle-draw/selection should know this before reusing those command lists as 'what the app shows'.
3. Size: 4.27 MB is at the top of 'a few MB'. Easy cut if wanted: drop the ship section from hand-built runs.

SPEC STATEMENTS THAT DID NOT HOLD
- 'CharMeasure answers (width/height/depth/italic)': CharDimens has no italic; none dumped. 'kerns used': there is no kern lookup in the core; kerns are the Kern(normal) nodes in the material. For the style port I added informational `source`/`theme` records (text, spacing, align, font key) so text->hlist can be replayed against `nodes material` once a font reader exists; markdown-built cases (mixed faces, inline math, demo/sample.md blocks) cannot be replayed without markdown and say so in their source kind.
- Composition path FOUND: markdown.text_block/build_document -> _Converter._append_text (style.interword_glue, GPOS kerns), measured by style.char_measure(fontlib_metrics); breaking inputs are ui._skips/_shape/_unshrinkable over ui.PROSE_BREAKING exactly as ui._text_boxes does. Note for task `style`: the text->hlist function lives in markdown.py (_append_text), not style.py.
- Goldens name fonts by font KEY ('prose@789381'), not file basename: the core never opens a font. Inline-math case uses keys math@789381/552567/394690 with synthetic codepoints as CharNode.codepoint; opaque to the core.

THINGS A PORTER WILL TRIP ON (all pinned by cases)
- Demerits: a candidate whose total exceeds AWFUL_BAD (2^30-1) is never recorded, so a non-final pass FAILS (cases demerits-past-awful-bad, adjacent-fitness-classes/adj-past-awful-bad with adj_demerits=2^30 — sums need >31 bits). 64-bit demerits required to match.
- pascal_round is floor(v+0.5): pascal_round(0.49999999999999994) == 1. A 'correct' llround/round port gives 0.
- hpack/ship accept negative stretch/shrink totals (negative glue_set, sign stretching) and rule widths <0; cases included. vet_glue clamp at +-1e9 covered with hand-set boxes.
- Reference raises ValueError in 6 places (error slugs in FORMAT.md section 3) and for badness(t<0). core-nodes/pack/shipout/linebreak need ONE agreed no-exception convention for these; nobody owns that decision yet as far as I can see.
- Integer inputs go past 2^53 (double conversion) and to 2^62; results outside int64 are deliberately not dumped — port's overflow policy is unpinned by goldens.

NOT VERIFIED
- No C++ reads these yet; tests/golden.h compatibility checked by reading its header comment only (my writer additionally %-encodes '#', which it decodes fine).
- 161 lines of dump_core.py exceed 100 columns; no Python formatter is configured here, none run.

OUTSIDE MY TASK
- git index: D4 says nothing is staged, the builder system prompt says 'git add as soon as you create a file'. I staged dump_core.py, then removed only my index entry (git rm --cached -f) on reading D4. tests/golden/font/* and tools/oracle/dump_font.py are currently STAGED by another builder — the two rules contradict and builders are resolving it differently.
- ../imgui-adv-text has uncommitted changes (docs/cpp-extension.md, docs/guide/README.md, tests/test_docs.py, a new postmortem). Not from this session's oracle runs as far as I can tell (docs only; my script never writes there), but 'the reference' is a dirty tree, so goldens are tied to working-tree state, not a commit.
