# Lead rulings for `tex-fixes` (after tex-web-pass)

Read `.concerns/tex-web-pass--builder-25--result.md`, its `--undeclared-departures.md`, and the
register in `docs/divergences.md` (65 entries: 27 deliberate in the prototype, 24 undeclared, 12 the
port's own).

1. MECHANISM — this REPLACES the board spec's "patch the reference in memory and regenerate goldens".
   Use the pattern already used for the paragraph indent and the block layer: a rules struct
   (`TexRules`, or per-module `BreakRules` / `MathRules` — your call, one place to read them) with two
   presets: `reference()` reproduces the prototype exactly and is what every golden replay runs under,
   unchanged; `product()` is what the public API uses. No golden file changes; no oracle script
   changes. Each product rule has its own direct tests with the TeX answer worked by hand from the
   cited section (show the working in the test's comment). No TeX engine is installed on this machine
   (lead checked `which tex pdftex luatex etex`: none) — say so; do not claim agreement with TeX
   beyond the hand-worked cases.
2. FIX under product(): M9 (§755 italic-correction kern only when there is no subscript — tex-web-pass
   established it affects every `f_i`, and F J N U I C S X R, q c k b, and several Greek capitals in
   Latin Modern Math); M10 (§735 underline depth); M13 (§754/§720 sublist nucleus packing / reuse);
   M14 (§703 mu conversion truncates); Y1 (§1044 sentence-space stretch/shrink truncate); B5 (§827 the
   three skips are made shrink-finite too); B6 (§887 left skip omitted on zero amounts); B7 (§877
   discardables after a discretionary with post-break text). For B5, B6, B7 and Y1 measure, over every
   paragraph case in tests/golden/core, para and blocks, how many break sets and how many line boxes
   differ between reference() and product(), and report the counts — the user should know how much
   ordinary text moves.
3. DO NOT FIX M11 (a fraction spacing as Inner). The TeXbook's rule 15e makes a fraction Inner; the
   tex.web here is a 2008 copy and the tex-web-pass builder recalls (unverified) that tex.web itself
   was corrected on this point in 2014. Reclassify it in docs/divergences.md as "agrees with The
   TeXbook and, by recollection only, with current tex.web; differs from the 2008 copy in this tree"
   and leave it for the user.
4. B3a/B4 and `easy_line` (§848, §850). The prototype's class-per-line-count formulation is recorded
   there as deliberate, and D32.3 listed it as staying. But tex-web-pass found its consequence reaches
   justified text too (the last-resort break goes to the chain with the most lines), and it is also
   what makes total fit superlinear (docs/performance.md: 126 ms justified, 26.5 s ragged at x64) and
   forced the work budget. So: IMPLEMENT `easy_line` merging for the uniform tail of the shape as a
   product() rule, and then DECIDE WITH EVIDENCE whether product() turns it on: (a) golden replays
   under reference() are untouched by construction; (b) under product(), count how many golden
   paragraphs change breaks at all — the expectation, to be checked not assumed, is that only the
   constructed tie cases and last-resort cases change; (c) re-run bench_reflow's total-fit growth rows
   (x1, x4, x16, x64, justified and ragged) with it on. If (b) is confined to ties and last-resort
   cases and (c) shows the growth gone or greatly reduced, turn it ON in product(), keep the work
   budget as a backstop, and record the decision with the numbers in docs/decisions.md; otherwise
   leave it OFF and report what you found. Either way the user gets the numbers.
5. The remaining twelve undeclared entries with no visible effect stay as they are, listed.
6. `test_stress_layout`'s check count is not constant from run to run (150,963 / 150,966 / 150,977 on
   one binary): find out why (it should be deterministic: a time-boxed loop?) and make it so, or tell
   the gate not to compare its count. You own tests/test_stress_layout.cpp for this.
Files beyond the board's `touches` that this ruling gives you: src/linebreak.{h,cpp}, src/pack.{h,cpp},
src/style.{h,cpp}, src/paragraph.{h,cpp}, src/blocks.{h,cpp} (to thread the rules through),
tests/test_linebreak.cpp, tests/test_style.cpp, tests/test_tex_rules.cpp (new), tests/test_stress_layout.cpp,
bench/bench_reflow.cpp, docs/performance.md, docs/decisions.md. The public API selects product()
— one line in src/imtypeset.cpp, which is yours for that line only.

## Added 2026-10-10 after harden-2 (lead)

- harden-2 changed src/math_mlist.*, src/paragraph.*, src/mathtext.*, src/draw.* and one expectation
  in tests/test_math_mlist.cpp (`math_glue_stays_inside_the_dimension_invariant`: a fraction with a
  rule 2^60 thick is now refused TOO_LARGE by H2 instead of converting). Read
  `.concerns/harden-2--builder--result.md` before editing those files; keep its refusals.
- harden-2 added `DrawRules::reference()/product()` in src/draw.h (D7). Use the same shape for your
  rule presets rather than a second mechanism, and check how src/imtypeset.cpp selects it before
  adding your one line there.
- Not excluded by harden-2, no repro exists: a MATH table engineered so two huge kerns cancel inside
  a box whose stated dimensions stay small (the `push(kern(...))` sites in src/math_mlist.cpp). If
  you can construct a cheap failing input, bound it; if not, leave it and say so.
- Your before/after images of `f_i`, `f_i^2`, `\underline{x}` are drawn under product() draw rules,
  which now include D7's rule placement: an underline is a rule. Look for it.
