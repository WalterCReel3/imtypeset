# Port hazards and lead rulings

Read this before starting any task. Where it disagrees with a task's
specification on the board, **this file wins**: the board does not let a
specification be edited once declared, and several were written before the
reference had been read closely.

**If `post_concern` starts refusing or truncating** (the operator warned it
has limits), write the concern as a file instead:
`.concerns/<task-id>--<your-role-name>--<gist>.md`, one concern per file, the
same naming the reference project uses in `../imgui-adv-text/.concerns/`. A
completion report that is too long for a final message goes there too, as
`<task-id>--<role>--result.md`. Before starting a task, list `.concerns/` and
read any file whose name carries your task id or `lead`.

Provenance, so nothing here reads as more settled than it is:

- **[R]** — a reviewer finding, *read* from the reference at the cited lines and
  not run. Line numbers are in `../imgui-adv-text/`. Treat as a strong claim to
  confirm when you reach the line, not as fact.
- **[R-inferred]** — the reviewer inferred it; weaker.
- **[V]** — the lead ran or read it in this tree.
- **RULING** — a decision by the lead. Follow it, or tell the lead why it is
  wrong via `post_concern`.

## Corrections to task specifications

1. **`draw`: "snap_px must equal IM_ROUND" is wrong.** [V] `snap_px` is
   `math.floor(value + 0.5)` (`advtext/backends/imgui_draw.py:274`); the pinned
   `IM_ROUND` is `((float)(int)((_VAL) + 0.5f))`, marked "Positive values
   only!" (`imgui_internal.h:287`). They differ for every negative non-integer,
   which is any document scrolled above its window. Port `floor(v + 0.5)` in
   double. The design note's §1 makes the same over-claim.
2. **`style`: text → hlist is `markdown.py:1047-1094` (`_Builder._append_text`),
   not `style.py`.** [R] See "style" below.
3. **`draw` / `oracle-draw`: raw vertex equality across rasterisers cannot be
   the test.** See "draw" below for what replaces it.
4. **`font-reader`: "glyf for TrueType" does not mean the glyf header box.** [R]
   Ink boxes are exact outline bounds. See "font-reader".
5. **Paragraph assembly was in no task.** RULING: tasks `oracle-para` and `para`
   now cover it; `public-api` builds on `src/paragraph.*` and must not invent
   that layer. If you hold `public-api` and `para` is not complete, say so to
   the lead and release the task rather than proceeding.

## Rulings that cut across tasks

- **RULING — ImWchar width.** [V] Stock ImGui has 16-bit `ImWchar`
  (`imgui.h:274`); the reference mints 227 synthetic codepoints for the math
  face in plane 15 (`0xFFF1B..0xFFFFD`), which only works because imgui-bundle
  builds with `IMGUI_USE_WCHAR32`. Requiring that define of every C++ host is
  an adoption cost we do not accept. So: nodes, shipped commands and goldens
  carry the reference's plane-15 values unchanged; `src/fontlib` exposes one
  function mapping a layout codepoint to the codepoint the **atlas** is asked
  for — identity when `ImWchar` is 32-bit, and a remap into BMP private-use
  slots the face's own cmap does not occupy when it is 16-bit — and
  `font-loader` and `draw` use it at the ImGui boundary and nowhere else. Our
  own `imgui` target builds with `IMGUI_USE_WCHAR32` by default (option
  `IMTYPESET_IMGUI_WCHAR32`, default ON, so we match the reference); the gate
  also builds and tests with it OFF.
- **RULING — floating point.** `-ffp-contract=off` on every target in every
  preset; never `-ffast-math`, `-Ofast` or `-funsafe-math-optimizations`,
  including for benchmarks. GCC contracts `a*b+c` to an FMA once `-march`
  allows, and hex-float goldens stop matching on that machine only.
- **RULING — rounding helpers live in `src/units.h`** (task `core-nodes`) and
  everything else calls them: `py_round` (half-to-even: `std::nearbyint` under
  `FE_TONEAREST`; never `std::round`/`lround`/`(int)(x+0.5)`), `floor_div`,
  `ceil_div`, and a checked float→Scaled conversion that clamps NaN/inf/huge to
  a stated bound instead of invoking UB (Python raises there; we cannot).
  `pascal_round` (`shipout.py:183-185`: `floor(v+0.5)` for `v >= 0`, else
  `ceil(v-0.5)`) and `snap_px` are their own formulas — transcribe the
  branches.
- **RULING — exceptions in the reference become:** `IM_ASSERT` for a caller's
  programming error on an internal entry point, and a status/`bool` + reason
  for anything reachable from data (fonts, text, math source, hand-built node
  lists through the public API). After the assert the function must still not
  execute UB when asserts are compiled out. Raise sites to give an outcome:
  `badness.py:107`; `pack.py:340, 355, 433, 442`; `shipout.py:401, 491, 576`;
  `linebreak.py:245, 265, 967`; `fontlib.py:852, 915`; `mlist.py:758, 808`.
- **RULING — `Rect` lives in `src/shipout.h`** so `draw` and `selection` share
  it without depending on each other. `BlockKind`/`Palette.ink_for` are on the
  Python side of D1; `draw` takes colours, not block roles.
- **RULING — FontKey.** The reference's key is the string `face@size`
  (`style.py:491-501`), stored in every `CharNode` and `Glyph` and re-parsed by
  draw. The port interns (face id, size) into the node; `core-nodes` fixes the
  scheme and the golden reader maps strings to it. [R] No ported module orders
  by FontKey, so interning cannot change results.
- **RULING — a ninth node kind.** `markdown.py:273` `ReplacedChar(CharNode)`
  remembers the original codepoint of a character set from a stand-in face, so
  copy and find return what was written. `core-nodes` gives the char node an
  original-codepoint field (or a declared side table) now.

- **RULING — `tex.web` is the structural reference** (user instruction,
  2026-10-09). The original TeX82 WEB source is at
  `../imgui-adv-text/docs/tex.web` [V: 25,010 lines], with
  `../imgui-adv-text/docs/tex-notes.md` as its audited digest — read that
  file's "CORRECTIONS" section first, it overrides its own body. Use them when
  deciding how a module is structured, what things are called, and what the
  comments and documentation say:
  - Name things as TeX does where a thing *is* TeX's (`hpack`, `vpack`,
    `try_break`, `active`/`passive`, `glue_set`, `mlist_to_hlist`,
    `make_fraction`, `hlist_out`…), so a reader with *TeX: The Program* open
    can follow.
  - Cite the WEB section (`§851`) at each function or block that transcribes
    one, as the Python reference does. Section numbers are WEB section numbers
    counted from the top of the file (`tex-notes.md` explains and spot-checks
    the numbering). Check the citation against `tex.web` itself rather than
    copying it from the Python.
  - Where the port deliberately does something `tex.web` does not — because the
    Python reference does, or because the reference omits a part (delta nodes,
    hyphenation, `\looseness`, the penalties of §890) — the comment says so in
    the present tense and names the section departed from.
  - **Behaviour still follows the Python oracle, not `tex.web`.** Where the two
    disagree, match the oracle, cite the section, and add the disagreement to
    `docs/divergences.md`. Do not "fix" the port toward TeX; report it to the
    lead instead.
  - `tex.web`'s own notice [V, its first lines]: copyright D. E. Knuth, "all
    rights reserved"; unlimited copying and redistribution while the file is
    not modified; modifications permitted "only if the resulting file is not
    named tex.web"; "TeX is a trademark of the American Mathematical Society".
    So: do not copy `tex.web` into this tree and do not paste runs of it into
    sources or docs — cite section and line. Nothing here is named `tex.web`
    or calls itself TeX.

- **RULING — licensing hygiene** (user instruction, 2026-10-09: whatever
  affects licensing must stay compatible). No licence has been chosen for
  this project and choosing one is the user's decision; until then keep the
  tree compatible with a permissive (MIT-style) licence, which is the ImGui
  ecosystem norm:
  - **Write from the specification and the Python reference, not from other
    people's code.** Do not transliterate fontTools (MIT), FreeType (FTL/GPLv2),
    HarfBuzz, MicroTeX or any other implementation. Reading them to learn what
    a table means is fine; a function ported from one is not, unless it is
    listed in `THIRD-PARTY.md` with its notice.
  - Code adapted from Dear ImGui (MIT) — most likely in `src/font_loader.cpp`,
    which follows ImGui's stb_truetype loader — and any use of
    `imstb_truetype.h` (MIT / public domain) carries the upstream notice at
    the point of use and an entry in `THIRD-PARTY.md`.
  - No GPL/LGPL code or data enters the tree. System fonts are read where
    they are installed and never copied in; the corpus goldens hold paths,
    hashes and measured values only.
  - Tables generated from Python's `unicodedata` say so, name the Unicode
    version, and carry the Unicode licence notice in the generated file.
  - The bundled test fonts are test inputs, never compiled in or installed:
    Roboto and Droid Sans (Apache-2.0), Inconsolata (OFL 1.1), Latin Modern
    Math (GUST Font License). `tests/data/fonts/LICENSES.md` records that the
    OFL 1.1 and GUST texts are **not yet in the tree**; neither could be
    obtained verbatim from this sandbox.
  - If you bring in anything not listed here, tell the lead before it lands.
  The `docs` task writes `THIRD-PARTY.md` from what the tree then contains.

## Per task

### core-nodes, pack, shipout [R]

- Exact halves are routine: a 2048-upm advance times an integer size lands on
  .5 about one time in 2048.
- `pack.py:277, 292`: `excess / available` is int/int true division, correctly
  rounded. `(double)a / (double)b` equals it only while both fit in 53 bits —
  state `|Scaled| < 2^53` as an invariant (enforcing `MAX_DIMEN` gives it).
- `shipout.py:650-664`: `cur_glue` is a **double** accumulator; keep the
  `_vet_glue` clamp between the multiply and the add; then `pascal_round`.
- `badness.py:118-126` floor-divides safely only because `t >= 0` is enforced
  at `:107`; a negative `t` must still not reach the divisions.
- `shipout.py:458`: the `is_atom` arm precedes the plain Box arm and is
  guarded by `not self.in_atom` — an atom nested in an atom is an ordinary box.
  Glyphs inside an atom carry the atom's index and do not advance it (`:454`);
  glue inside emits nothing but still advances `cur_h` (`:512-525`);
  `vlist_out` passes `as_line = not in_atom` (`:568`).
- `pack.py:380`: `is_empty` is `not nodes` on the **input** list.
  `pack.py:363`: a `RUNNING` rule height deliberately loses `max()` against 0 —
  `RUNNING` stays `-(2^30)`, not a separate flag.
- `pack.py:519-524` `interline_glue`: the lineskip branch shares the spec
  whole; the baselineskip branch copies stretch/shrink and replaces width.

### linebreak [R]

Ordering that decides ties — port literally:

1. `:637` `sorted(self.active, key=line_number)` is **stable** over
   `survivors + created` (`:712`), which is not sorted by line number. Use
   `std::stable_sort` or maintain the equivalent order.
2. `:683` `d <= best_demerits[fit]` — the later candidate wins a tie.
3. `:699` new actives are created in Fitness order VERY_LOOSE, LOOSE, DECENT,
   TIGHT (`:648`).
4. `:740` `min(self.active, key=total_demerits)` returns the **first** minimum:
   fewest lines, then lowest fitness class.
5. `:664` `alone` = no survivors, nothing created, and last element of the
   sorted list.

Where the reference is not tex.web — do not "fix":

6. `:421-430` only **body** glue is made shrink-finite; left/right/par-fill
   skips go into each line unmodified (`:487-505`). This describes the port under
   `TexRules::reference()` only: under `TexRules::product()` the switch
   `skips_made_shrink_finite` makes the three skips shrink-finite as `tex.web` does
   (`docs/decisions.md` D41).
7. `:504` `left_skip` is inserted only if `!= GlueSpec()` (all five fields);
   `right_skip` is **always** appended. Each becomes a `SetGlue` with a
   reading-order index, so this shifts every selection index. This describes the port
   under `TexRules::reference()` only: under `TexRules::product()` the switch
   `left_skip_omitted_when_zero` leaves a zero left skip off as `tex.web` does
   (`docs/decisions.md` D41).
8. `:581-582` emergency stretch is a Glue appended to the candidate list
   (finite stretch, NORMAL order, after right_skip); `_assemble` (`:994`)
   repacks without it. A running-sum formulation must still report
   `glue_order` from the dominant **nonzero** order (`nodes.py:231-234`).
9. `:480` pruning depends on the candidate's position: line natural width is
   not `prefix[cur] - prefix[prev]` when a breakpoint lies inside a run of
   discardables.

Width and hostile input:

10. Demerits need int64; `abs()` at `:607, :631` is UB at `INT_MIN`.
11. `Disc.replace_count` is trusted at `:798`: an overrun is an out-of-bounds
    read in C++, a negative value never terminates. Clamp, and say so.

### font-reader, oracle-font [R]

1. **Synthetic codepoints are assigned by sorted glyph NAME**
   (`fontlib.py:626-629`): `reachable` = names in **vertical** variant chains
   only; `sorted(reachable - set(by_name))`; codepoint = `0xFFFFD - offset`.
   The reader therefore needs fontTools' glyph names: CFF charset + the 391
   standard strings for the math face, `post` format 2 for TrueType. Sorting by
   glyph id yields a self-consistent, wrong mapping. Bytewise compare (names
   are ASCII). `:621-622`: `by_name` takes the lowest codepoint.
2. cmap choice is fontTools `getBestCmap()` (`:752`). [R-inferred] preference
   (3,10), (0,6), (0,4), (3,1), (0,3), (0,2), (0,1), (0,0) — confirm from the
   installed fontTools source.
3. Ink boxes are `BoundsPen` (`:696-708`): exact curve extrema as floats, with
   composites resolved. The CFF face needs a Type 2 charstring interpreter and
   cubic-extrema solving. RULING on tolerance: what must be **exact** is
   `round(height)`/`round(depth)`/`round(width)` in sp at every oracle size
   (that is what layout consumes, `style.py:551-555`); the raw float box is
   compared with a stated tolerance, and you report the largest deviation and
   its cause. If `imstb_truetype.h`'s box is the control-point box rather than
   the extrema, that answers one of the design note's open questions — record
   it.
4. Kern lookup (`:441-458, :710-724`): the first subtable returning non-None
   wins across **all** subtables of all kern lookups, in
   `sorted(set(lookup indices))` order. A format-1 subtable returns None for an
   unlisted pair even when the first glyph is covered, so the search falls
   through — against the OpenType rule. Match the reference; note the
   divergence from the standard in `docs/divergences.md`. Format 2: coverage
   gate, class 0 default, `row[column] if column < len(row) else 0`. Only
   `Value1.XAdvance`. Format-1 duplicates: last wins.
5. `scale = size / units_per_em` is formed once (`:785`), then `value * scale`.
   Not `value * size / upm`.
6. A missing codepoint answers the advance of `.notdef` (glyph 0), not 0
   (`:687, :820`). `has()` includes minted entries (`:816`).
7. `x_height` fallback: `round(_X_HEIGHT_FALLBACK * upm)` (`:767`).
8. `MathParams` (`:859-899`): sup1 = sup2 = SuperscriptShiftUp, sub1 = sub2 =
   SubscriptShiftDown, delim1 = 2.4 em, delim2 = 1.0 em, big_op_spacing5 =
   0.1 em; script scales are `c / 100.0`.

**RULING — the reader is for real fonts, not for the bundled seven.** The user
said so directly on 2026-10-09: hosts and other applications will hand this
arbitrary fonts. The `font-reader` board spec says to support "whatever
lookup/subtable formats the bundled faces' kern feature really uses"; that
sentence is withdrawn. `font-reader` owns, in addition to its listed files,
`tests/test_opentype_wild.cpp` and `docs/font-support.md`, and must:

- Read what real fonts actually contain, each either supported or refused
  with a reason and listed in `docs/font-support.md`: sfnt TrueType and CFF
  (`OTTO`), TrueType collections (`ttcf`, by face index), cmap formats 0, 4, 6,
  10, 12, 13 (and 14 ignored cleanly), `hmtx` with fewer metrics than glyphs,
  composite and empty glyphs, `loca` short and long, missing `OS/2`, GPOS
  PairPos formats 1 and 2 with every ValueFormat (skipping the fields it does
  not read), Extension lookups, script/language/feature selection as the
  reference does it, and the legacy `kern` table **if and only if** the
  reference reads it (check `fontlib.py`; if it does not, say so in
  `docs/font-support.md` as a known gap rather than inventing behaviour the
  oracle cannot check). Variable fonts are read at their default instance.
  CFF2, WOFF/WOFF2 and Type 1 are refused with a reason.
- Pass the corpus test: task `oracle-font-wild` dumps a compact digest for
  every font on this machine [V: `fc-list` reports about 2,900 `.ttf`, 54
  `.otf` and 14 `.ttc` files, 988 MB under `/usr/share/fonts`] as fontTools
  and `fontlib.register_font` answer it. `tests/test_opentype_wild.cpp` loads
  each font by its recorded path (skipping, and counting as skipped, any file
  whose size or hash no longer matches, so the test is honest on another
  machine) and compares. Report the census: fonts read, refused by design,
  mismatched, and every mismatch class with one named example. A font
  fontTools itself cannot open is not a failure but must not crash us.
- The loader (`font-loader`) is then checked over a sample of the same
  corpus: rasterising through our loader must not crash on any of them.

### style, para [R]

- `_stand_in` (`markdown.py:315-355`): a character the face lacks is set from
  the prose face, then math, then code, else U+FFFD in prose, else `?`.
  General categories Cc, Cf, Mn, Me are never stood in for (`:312`) — the port
  needs a table for those four classes.
- `str.isspace()` decides interword glue (`:1064-1065, :1090`): U+00A0,
  U+2000-200A, U+2028/2029, U+3000, U+0085 and 0x1C-0x1F all count. A no-break
  space **is** a breakpoint in the reference. Whitespace runs collapse to one
  glue; leading whitespace of a block is dropped (`:1067-1070`); an unbreakable
  run gets `Penalty(INF_PENALTY)` before the glue (`:1071-1072`).
- `str.isupper()` drives the space factor (`style.py:589`) for any cased
  uppercase character, not just A-Z. `interword_glue`'s default spacing is
  NONFRENCH (`style.py:597`) while `Theme`'s default is the other; the native
  API picks one and says which.
- Kerning (`markdown.py:1089-1093`): only when the current character is
  covered by its own face, a following character exists in the **same run**,
  and it is not whitespace; `round(kern)` half-to-even; zero kerns are not
  emitted; none after a stood-in character.
- `interword_glue` (`style.py:612-620`): `round(space * fraction)`, then under
  NONFRENCH `round(stretch * sf / 1000)` and `round(shrink * 1000 / sf)` — true
  division.
- `char_measure` (`style.py:551-555`): height and depth are **not** clamped at
  zero.
- Paragraph assembly, `advtext/ui/__init__.py`: `RAGGED_RIGHT` (`:169`);
  `PROSE_BREAKING = LineBreakParams(tolerance=1000, emergency_stretch=20pt)`
  (`:178`) [V] — this, not plain-TeX defaults, is what the reference breaks
  with; `_skips` (`:1312-1344`); `_text_boxes` (`:1405-1418`): anything not
  JUSTIFY is broken **first-fit** after `_unshrinkable` strips shrink
  (`:1425-1445`) — total-fit on ragged text measured 2.5-9.3 s on a 635-node
  paragraph; `_shape` (`:1347-1366`); `Strut.filling` (`style.py:641-651`),
  without which a line of `xxx` has x-height and zero depth.

### draw, oracle-draw, font-loader [R]

- **What the differential test is.** The loader feeds the pen as well as the
  quad: `imgui_draw.py:1077-1079` takes `baked.size` and `baked.ascent` from
  ImGui and `:1132` computes `top = snap_px(baseline.y - ascent)`. So:
  (a) a **pen-level golden** — per glyph: font key, baked size, pen x, pen y,
  colour, codepoint; per rectangle: corners — with baked size and ascent per
  key recorded as INPUT that the C++ test injects. Rasteriser-independent and
  exact. The reference validated its own memo this way (`:996-998`).
  (b) full vertex equality only in a FreeType build, and only reported, not
  gated, unless the FreeType version matches the golden's `2.13.3`.
- `RenderText` drops quads: below the clip rect, outside it horizontally,
  `!glyph->Visible` (a rigid space CharNode — though `DrawStats.glyphs` still
  counts it, `:1146`), alpha-0 ink; `Colored` glyphs ignore ink. A direct
  `PrimReserve` path reproduces each. `oracle-draw` includes a clip rect
  cutting a line, a space CharNode and an alpha-0 ink range, and states the
  clip rect in the INPUT.
- **The float32 hop.** `at()` builds `ImVec2(origin.x + px_from_sp(x), ...)`
  (`:664-667`): a double sum stored as float32. `_glyph` reads it back widened
  and snaps in double (`:1126-1134`). `_snap_span` compares `far > near` on the
  float32-rounded values (`:773-776`). `pen()` computes
  `prev_pen + floor(px_from_sp(cmd.x - prev_x) + 0.5)` in double (`:924-925`).
  Port: double arithmetic, one `(float)` cast exactly where the reference
  constructs an `ImVec2`, widen, then `floor(+0.5)`.
- `px_from_sp` is `length * (px_per_inch / PT_PER_INCH) / UNITY`
  (`units.py:178`): the ratio is formed first.
- `Atom` is a `SetGlue` subclass, so it ends a rigid span (`:738`). A missing
  glyph neither opens nor closes a span (`:1117-1122`). The span resets per run
  (`:718`). `inks` lookup is `bisect_right - 1` with a half-open end test
  (`:727-729`).
- `fonts(face)` may answer None: draw in the current ImGui font, skipping the
  glyph-presence check (`:1055-1056, :1139-1140`). Keep the path or drop it on
  purpose and say so.
- The debug overlay uses `add_rect`/`add_line`, whose geometry depends on
  anti-aliasing flags: call ImGui's `AddRect`/`AddLine`, and keep debug cases
  out of direct-write comparisons.
- [R-inferred] `FontRasterizerDensity` could make `baked.size` differ between
  contexts; recording baked size as input makes it moot.

### math-mlist, mathtext [R]

- `mlist.py:797` `-((-2 * delta1 * numerator) // denominator)` is a
  **ceiling** division. Transcribed literally to C++ it floors, and a formula
  the reference sets is refused at exactly one size. Use `ceil_div`.
- `mlist.py:791` `(delta1 // 500) * DELIMITER_FACTOR`: use `floor_div`
  ([R-inferred] that `delta1` can be negative). Use `floor_div` for every `//`.
- `atoms.py:147-187`: Style's integer values are semantics; keep TeX's 0..7.
- `mlist.py:386` `round(base_size * script_scale)` with
  `script_scale = c / 100.0` — that order.
- Refusal is an exception out of `mlist` (`:758` `DelimiterTooSmallError`),
  and `MathRefused` lives in `markdown.py`. The port threads a status return
  through every recursive `make_*` from the start and defines the refusal type
  natively. `oracle-math`'s refused corpus includes an mlist-originated
  refusal (`\left(` around content taller than the largest variant).
- `_delimiter_box` search order (`:728-759`): sizes from the style's size by
  16 toward TEXT; candidates = variant chain, or `[codepoint]` if empty;
  `extent < needed and extent <= largest: continue`; first `extent >= needed`
  wins; fallback keeps the last strictly-larger one. Width includes
  `round(italic_correction)`.
- Text inside math (`mlist.py:673-685`) is a second composition path with
  `interword_glue` at NONFRENCH and neutral factor.

### selection, public-api, api-math [R]

- `selection.py:429` is a **stable** sort and ties are normal (a blank line
  shares its start with the next). `std::stable_sort`.
- `:377-426` keys tables by `id(line)`. Key by position in the command buffer,
  never by value.
- `Glyph() | SetGlue()` includes `Atom`; glyphs whose index equals the current
  atom's are skipped (`:399`); `_is_fill` excludes `Atom` (`:949`); a line with
  no items that is ruled or has a child is not a fragment (`:418`).
- `:614` midpoint test is `2 * x < left + right` — no division. `:502` is a
  lexicographic compare (vertical distance, then horizontal).
- `text()` (`:754-894`) takes separators, hard_breaks, atoms and originals as
  **data** the unported layers build. RULING on ownership: `public-api`
  supplies `\n\n` separators at paragraph starts; `api-math` supplies the atom
  index → `$source$` map; `style`/`para` supply originals for replaced and
  malformed characters. End-to-end test in `api-math`: select-all over two
  paragraphs with one inline formula and assert the copied string.
- Indices are positions in the **shipped** sequence (including one `SetGlue`
  per line for right_skip, and for left_skip/par_fill_skip when present), not
  source offsets. The public header says so wherever it exposes an index, and
  `public-api` provides the source-offset mapping a host actually wants.

### bench, stress, final-gate

An optimisation pass on `linebreak.cpp` or `draw.cpp` is where a tie-break or
a float reassociation gets reintroduced. The full golden suite runs, unnarrowed,
before and after every optimisation and again in the gate.

## What completed tasks established (added by the lead as results came in)

Each builder's completion report is a file under `.concerns/`
(`<task>--<role>--result.md`). **Read the reports of the tasks yours depends
on before you start** — they hold the API facts and the decisions you inherit.
The points below are the ones that change what a later task must do; they are
the builders' statements of what they ran, not re-run by the lead.

- **Rasteriser.** The system FreeType is 2.12.1; against the reference's
  2.13.3, 0 of 7,540 glyph boxes differ (6 files x 5 sizes, ASCII + Latin-1 +
  all minted codepoints). Under stb_truetype 6,657 of 7,540 differ. So full
  vertex equality **is gated** in the FreeType build, and skipped with a
  printed reason in the `stb` preset. This supersedes "reported, not gated".
- **stb and the pixel grid.** With the default `ImFontConfig`, stb resolves
  `OversampleH` to 2 below 36 px and most glyph boxes get a fractional `X0`,
  which breaks the reference's one-texel-to-one-pixel rule. `font-loader` /
  `public-api` set `OversampleH = 1` (or `PixelSnapH`) on every face they add.
  Judge crispness in images from the FreeType build only.
- **Ink boxes are exact.** The font reader reproduces all 11,838 glyph boxes
  bit for bit; the tolerance in the font-reader ruling above is withdrawn.
- **Kern fall-through is not a divergence from OpenType** — the oracle-font
  builder disputes the reviewer's reading, with evidence (Roboto-Regular has
  59,725 pairs kerned only by falling through format 1 into format 2). The
  reference's real departures are: no accumulation across kern lookups,
  script/language ignored, lookup flags ignored, Value2 ignored.
- **fontTools quirks the reader replicates** to match goldens: a top-level
  simple glyph's outline is shifted in x by `lsb - xMin`; composite
  `ROUND_XY_TO_GRID` and `USE_MY_METRICS` are ignored. See
  `.concerns/oracle-font--builder-3--result.md`.
- **FontKey is a packed 64-bit value**, not an interned id (15-bit face id +
  48-bit size in sp); face ids are the reference's `Face` enum order. Accepted
  by the lead in place of the interning ruling above.
- **Errors.** `IMTYPESET_CONTRACT(cond, "what")` in `src/units.h` for
  programmer error (testable after-violation path); `PackResult::error` as a
  status for data error. `hpack`/`vpack` are measure-only over a `NodeView`
  and allocate nothing; the caller makes the box. Callers must not drop
  `error`.
- **Nodes.** 32-byte trivially copyable tagged value; boxes and discs live in
  a `NodeArena` by 32-bit index; nesting is refused beyond 1,024, so mathtext's
  own bound must stay under that. `Disc.replace_count` is stored as given and
  `linebreak` must bound it.
- **Draw.** A glyph with codepoint 0, U+000A or U+000D on a face that maps it
  is counted and advances the span but emits no quad. There is no partial
  clipping: whole lines drop by y, whole quads by x. Size rounding is
  `IM_ROUND` in float32 on the requested size; the drawn size is the baked
  size. The reference sets no `ImFontConfig` field.
- **Math.** `math-mlist` can be tested with no font reader (every font query
  is recorded per case). Refusal reason strings embed Python `repr()` of a
  character and a `:.2f` — the port reimplements those forms
  (`tools/oracle/FORMAT-math.md` §5). Refusal positions are in codepoints in
  the reference; the goldens carry bytes too, and the native API reports
  **both**. `DELIMITER_SHORTFALL` is a parameter. `accent` and malformed
  fields raise non-refusal exceptions in the reference and need a defined,
  non-crashing outcome. Where a `$` opens a formula is a markdown-it rule that
  is not ported; `api-math` states what the native scanner does with `\$`.
- **Unicode tables** are in `tests/golden/math/unicode.golden` (isspace,
  isprintable, isalnum, isupper, Cc/Cf/Mn/Me), Unicode 14.0.0. `para` and
  `mathtext` generate from there; `oracle-para` need not dump them again.
- **The core goldens' "ship" section** uses a vertical assembly the
  oracle-core builder wrote (no Strut); it pins pack + shipout on real lines,
  not what the reference UI draws. `oracle-para` is what pins that.
- **Examples** are also ctest tests run with no arguments in a scratch
  directory: `examples/specimen.cpp` takes its output directory as an
  argument. The PNG writer does not compress (about 4 bytes per pixel), so
  specimen canvases stay small.
- **Git.** Some agents' launch rules say to `git add` files on creation and
  some builders did. Staging is harmless and need not be undone; **nobody
  commits**.
- **The reference is a dirty working tree** (`docs/cpp-extension.md` and three
  other files are modified or untracked there), so goldens are tied to its
  working-tree state, not to a commit.
- **Golden size** is about 47 MB including the system-font corpus digest. That
  is a publishing decision for the user, not something to trim now.

## Order at the end of the board

`font-wild` (the real-font extension of the reader) was declared after
`final-gate` and the board does not let a dependency be added to an existing
task. So:

- Whoever holds **`final-gate`** while `font-wild` is unfinished does **not**
  edit or reformat `src/opentype.*`, `src/fontlib.*`,
  `tests/test_opentype_wild.cpp`, `tests/test_font_loader_wild.cpp` or
  `docs/font-support.md`, and treats a failing `test_opentype_wild` as
  expected (it fails by design until `font-wild` lands) — report it, do not
  fix or disable it.
- Whoever holds **`docs`** writes the font section of the README from
  `docs/font-support.md` as it then stands and says it is provisional if
  `font-wild` is unfinished.
- **`release-gate`** depends on all three and is the gate whose result is
  reported.

## Second review round and later results (lead, in order received)

Full text in `.concerns/`: `review-layout-core--reviewer-3--findings.md`,
`oracle-font-wild--builder-5--census.md`, `style--builder-6--result.md`,
`oracle-para--builder-7--result.md`, `font-loader--builder-3--result.md`.

- **RULING — core hardening belongs to `stress`**, which for this purpose also
  owns `src/nodes.h`, `src/nodes.cpp` and `tests/test_nodes.cpp`: review
  findings A-F (a box can contain itself; ship's default item bound; unbounded
  `import`/`deep_equal`; signed overflow when `MAX_DIMEN` is not honoured; no
  work budget in total fit; a bad box index asserting instead of returning a
  status). They are a reviewer's reading, not run: reproduce each first, fix,
  and keep the repro as a test. For total fit, the cap falls back to first-fit
  and the result says so; `bench` measures the growth first (the 635-node
  sample x16 and x64, fil right skip, total fit).
- **RULING — one validating entry.** `public-api` clamps every dimension it
  accepts from a host (widths, sizes, indents) and the measure adapter clamps
  what fonts answer, so the `MAX_DIMEN` invariant in `src/units.h` holds for
  everything built through the public API.
- **RULING — quoting `tex.web`.** A TeX identifier or a single short statement
  quoted to identify what a line transcribes is fine. Verbatim prose sentences
  and worked examples are paraphrased. `tex-web-pass` rewrites the ones the
  review listed (item J) and takes items G-I.
- **RULING — paragraph indent and white space** at the public API: the indent
  is applied once, as TeX does (§1091), not twice as the reference does; white
  space collapses across run boundaries. Both are deliberate departures,
  recorded in `docs/divergences.md` by whoever implements them. `para` itself
  reproduces the reference for the goldens.
- **RULING — collections.** The reader takes a face index; corpus comparison
  uses the digest's `FONTLIB indexed` records.
- **For `font-wild`, from the census** (2,991 faces): the present reader
  refuses the 80 collection faces and the 84 CID-keyed CFF faces; it must drop
  cmap mappings to glyph 0 (19 faces); TrueType MATH fonts need `post` format 2
  names to mint in the reference's order (6 faces); 21 faces have only a legacy
  `kern` table, all version 0 format 0 — enough to justify the extension, which
  stays opt-out-able and is recorded as going beyond the reference. cmap
  formats 0, 6, 10 and 13 are never the picked subtable in this corpus, so they
  remain "supported, unexercised" unless a test font is synthesised.
- **Font-stack review** (`.concerns/review-font-stack--reviewer-2--findings.md`):
  no out-of-bounds read found in the reader; D27 holds. Three resource
  amplification paths and the hand-off of a leniently accepted font to
  stb_truetype are `stress`'s (S1-S9); the real-font refusals and five
  divergences from fontTools are `font-wild`'s (W1-W14); `public-api` forces
  `FontNo = 0`, refuses `MergeMode`, checks `sizeof(ImWchar)` at init and adds
  each face to the atlas once (P1-P3).
- **RULING — strict validation before the atlas.** A font may be registered
  for metrics with a glyph-local defect, but `add_face_to_atlas` refuses any
  face that has not passed a strict structural pass over everything a
  rasteriser will follow, because stb_truetype does no range checking.
- **RULING — fontTools-derived arithmetic stays, attributed.** The ink-box
  routines follow fontTools expression for expression because bit-equal
  results need the same order. That is a ported function under the licensing
  ruling: MIT notice at the point of use and a `THIRD-PARTY.md` entry. This
  amends "do not transliterate fontTools" for those routines only.
- **Selection** (`.concerns/selection--builder--result.md`): one `SelectionModel` per shipped paragraph; the public API never exposes a bare index that collides across paragraphs. `stress` replaces the Unicode tables embedded in `src/selection.cpp` with `src/unicode_tables.h`.
- **Paragraph assembly** (`.concerns/para--builder--result.md`): all 243 cases
  at 710 widths match at every stage, from recorded metrics and again through
  the real font reader. `BlockStyle::first_line` defaults to the reference's
  double indent; `public-api` sets `BOX_ONLY`. **Hard breaks reach
  `SelectionModel::text` as `"\n"` separators** (as the reference's
  `ui/units.py` does), not as `TextData::hard_breaks`, or a hard break in a
  right-aligned or centred block copies as a space — read from the sources by
  the `para` builder, not run end to end; `public-api` tests the copy.
  Multi-block stacking (space above and below) is not ported.
- **Draw** (`.concerns/draw--builder-6--result.md`): `Drawer` writes into an
  `ImDrawList` or into `DrawBuffers`; `index_lines`/`LineIndex` give opt-in
  culling by line. Under FreeType all 22 golden cases equal the reference's
  buffers outright.
- **For `stress`:** `tests/test_linebreak.cpp` replaces only one form of
  `operator new`; it will abort under ASan (alloc/dealloc mismatch) once that
  executable loads a font, because `std::stable_sort` uses the nothrow form.
  `tests/test_paragraph.cpp` shows the full replacement.
- **Math list and mathtext** are complete: all 2,576 + 2,037 mlist cases match
  node for node under three font sources; all 3,765 mathtext sources give the
  reference's list or its refusal byte for byte. Bounds: `MAX_MATH_NESTING` 64
  (the deepest list `parse` emits is 49), `MAX_MATH_ITEMS` 2^20. A refusal
  carries a codepoint and a byte position. `typeset` takes the fonts.
  `markdown_math` and `Formula.source` are not ported — `api-math` keeps the
  source text itself for copy. `src/mathtext_tables.inc` is generated and
  deliberately not clang-formatted: **gates format `*.cpp` and `*.h` only**.
- **Math oracle gaps closed** (`.concerns/oracle-math-gaps--builder-13--result.md`):
  the math goldens now hold 8,555 cases (3,776 mathtext sources, 2,589 mlist
  lists); every count quoted earlier in this file and in
  `docs/divergences.md` predates that. `docs` corrects the stale counts in
  `tools/oracle/FORMAT-math.md` §3 and §5 (it lacks `braced` and `boundary/`)
  and in `tools/oracle/FORMAT-font.md` (a codepoint mapped to glyph 0 has
  `has()` false, not true). One pin remains native-only: which box
  `_delimiter_box` keeps on a `<=` tie needs a math-face keyword the golden
  format does not have.
- **Licensing paperwork** (`.concerns/docs-hygiene--builder-20--result.md`):
  `THIRD-PARTY.md` exists. **`font-wild` applies the fontTools notice** to
  `src/opentype.cpp` exactly as
  `.concerns/docs-hygiene--builder-20--notice-for-opentype.md` gives it, adds
  the same notice to `tools/oracle/check_font_bounds.py`, and then removes the
  "pending" wording from `THIRD-PARTY.md` as that file says. Still missing
  before publication, and not obtainable on this machine: the GUST Font
  License text and the Unicode 14.0.0 notice (a Debian rendering of the same
  licence family stands in and is marked for replacement). The OFL 1.1 text is
  an unaltered run of lines from fontTools' bundled licences, not SIL's own
  file. `shot-rig`/`demo` correct the SDL2 entry once something links it.
- **Review of draw / paragraph / style**
  (`.concerns/review-draw-para-style--reviewer--findings.md`): style and
  paragraph clean on read; two defects in `src/draw.cpp` (an all-ones font key
  reads out of bounds; an unbound face drops glyphs depending on which glyph
  is met first), plus tests there that skip or cannot fail. **Correction to
  linebreak item 7 above:** tex.web §887 does skip a zero left skip; the
  reference's departure is omitting on equality with the default spec, not on
  zero.
- **Word gaps** (`.concerns/example--lead--word-gap-observation.md`, corrected):
  measured spread on a justified line is 2-3 px, inside the reference rule's
  own bound; the lead's earlier "9, 4, 9 px" was a misreading.
- **ImGui 1.93** (`.concerns/dist-research--builder-19--result.md`, rulings in `.concerns/dist--lead--from-dist-research.md`): the library compiles clean against 1.93.0 WIP and glyph-box equality holds; three tests and the debug overlay do not carry over, and `cmake/imgui.cmake` still enforces the exact version the header no longer requires.

## Direction change (user, 2026-10-09): imtypeset is the product — see docs/decisions.md D32

advtext is treated as a prototype and stays the differential oracle; it is no
longer the authority on correctness where it departs from `tex.web` without
saying so, and nothing is designed for the sake of running advtext's Python
views on top of imtypeset. `tex-web-pass` still records every departure;
task `tex-fixes` then fixes the ones D32.3 covers. Tasks `oracle-blocks` and
`blocks` add the native document layer.
- **Decision numbering:** the hardening decisions are **D33** (they were written as a second D32 while the direction change took that number); D32 is the direction change.

## Two runnable demos are a requirement (user, 2026-10-09)

1. **A fully native host**: task `demo` — `ImTypeset::ShowDemoWindow()` in an
   SDL2 host built against `../example-project`'s SDL2, run and photographed
   on Xvfb (`tools/shot.sh`) and through SDL's offscreen driver.
2. **A minimal Python host with direct hooks into imtypeset**: tasks `c-api`
   (a C ABI, `include/imtypeset_c.h`) and `py-host` (a ctypes binding under
   `python/imtypeset/`, `examples/python/minimal_host.py` inside an
   imgui_bundle app, run and photographed). No binding generator is installed
   here, so ctypes over the C ABI is the route; the litgen route is drafted
   under `bindings/imgui_bundle/` and unrun.

Whoever holds the LAST gate runs both demos unattended (`--frames N
--screenshot`), looks at both screenshots, and reports them with the gate
results. A gate that has not run both is not the final gate.
- **Product shape:** docs/decisions.md **D34** (core + C ABI + bindings here; advtext reworked as the Python layer; two demos; a measured rework plan).
- **imgui_bundle research** (`.concerns/bundle-research--builder-21--result.md`,
  `docs/bindings-notes.md`, rulings in `.concerns/py-host--lead--rulings.md`):
  driving imtypeset inside the INSTALLED bundle wheel is blocked — the bundle's
  ImGui is a fork whose `ImGuiStyle` differs, and its headers are not on this
  machine. Compiled INTO the bundle it should bind well (37 of 49 declarations
  as written). Font bytes can reach the bundle's atlas from Python through a
  capsule, and the library's rebuilt math font works there. The bundle builds
  with a throwing `IM_ASSERT`, so the library must also build and be
  exception-safe with exceptions enabled.
- **tex.web pass done** (`.concerns/tex-web-pass--builder-25--result.md`): 516 citing lines, six wrong citations corrected, ten private renames, token streams otherwise unchanged; `docs/divergences.md` is now a classified register (27 deliberate in the prototype, 24 undeclared, 12 the port's own). `tex-fixes` follows `.concerns/tex-fixes--lead--rulings.md`, which replaces its board spec's mechanism with reference()/product() rule presets.
- **Scratch Python goes in a `mktemp -d` directory, never directly in `$TMPDIR`**, and the reference interpreter is run with `-P -B`: the shared temp directory holds stray `.py` files from other sessions, one named `bisect.py`, which a builder's scratch script imported and ran by accident. See CLAUDE.md.
- **Oracles for the document layer are complete**: `tests/golden/blocks`, `tests/golden/tables`, `tests/golden/doc`, with `tests/golden/MANIFEST.sha256` and `PROVENANCE.md` (`tools/oracle/manifest.py --check`; the last gate regenerates them). Positions are (block, byte offset into the text the host handed in) — see `.concerns/api-document--lead--notes.md`.
- **API revision done** (D36, `.concerns/api-revise--builder-23--result.md`): source-byte offsets everywhere, content-keyed widget cache, `IMTYPESET_CHECKVERSION()`, one error model, `TextUnformatted`/`TextWithID`/`SelectableText`/`CalcTextSize`, line and caret geometry. A steady widget call is 1.03x-1.44x slower than before (cause not isolated): `harden-2` item X4. `docs/bindings-notes.md` §5 and `bindings/imgui_bundle/*` describe the OLD header: `c-api`/`dist` update them.

## Two ways to measure a character (task `device-advances`, D77 and D78)

For whoever next touches layout, the draw loop or a binding.

- **A font key can carry a device.** `FontSpec::device`, face id `0x4000 + 8 * device + face`.
  `parse_font_key` returns it; code that makes a key for ANOTHER face at the same size
  (`stand_in`) must pass the device on, and code that makes keys for a formula must not: math
  is measured by its font. A key with a device and a key without are different keys of one
  face at one size, with different advances.
- **Make a `FontSpec` through `ResolvedStyle::font(face, size)`**, never by filling face and
  size: the style knows which faces it measures on the device, and a spec made by hand is
  measured by the font whatever the style says. `resolve_spec` and `build_material_now` are
  the two places that do it; a block with its own `Advances` takes `measured_style`.
- **The device is part of the style as resolved, and the display's density is read when a
  style is resolved.** Anything that keeps a layout must compare `ResolvedStyle::same_device`
  as well as what it compared before, or it will keep a layout made for another display. A
  document asks again in `ready`; nothing else does it for a paragraph, whose layout is
  explicit.
- **`face_bake` is the one place that says which atlas entry a face is drawn from.** The draw
  loop and `src/device.cpp` both call it. A change to how a size is asked of ImGui made in one
  of them and not there breaks the property the feature is: that the advance layout used is
  the advance of the entry that is drawn.
- **Nothing of ImGui's is read at layout time without `core::bound`, `backend_has_textures`
  and `role_font_in_atlas`.** Layout can be asked for with the ImGui context gone. A
  measurement that cannot be taken from the atlas is taken from the font file, and the memo
  then holds a font advance under a device key until the fonts change; the styles that would
  use it resolve to no device while the context is away, so it is not reached.
- **`validate_fonts` must not be called from inside a measurement**: it can clear the memo
  that is being written to. `resolve_device` calls it, before any key is made.
- **The reference has none of this.** Under `TexRules::reference()` the default measures
  every face by its font and the goldens replay as they did; `DrawRules::reference()` does
  not hold glyphs. `Device` asked for outright works under either, and no golden asks.
- **A test that compares pens with layout** compares to `EXACT_PX` (1/256 px), not to zero: a
  pen is a whole pixel and a laid-out position is a sum of advances each rounded to a scaled
  point. That is the unit's rounding and not a tolerance on a golden; no golden comparison
  has one.
