# Undeclared departures from `tex.web` — reading list for `tex-fixes`

From task `tex-web-pass` (builder-25). Each is something the Python prototype
does, the port reproduces (the goldens hold it), `tex.web` does otherwise, and
no document of the prototype's records as a choice. Numbers are the register's
in `docs/divergences.md`. Section numbers were counted in
`../imgui-adv-text/docs/tex.web` (version 3.1415926; calibrated on §108, §749,
§859); line numbers are that file's.

"RUN" means the prototype was run for the input (read-only, from `$TMPDIR`);
"READ" means worked from the two sources only. Lengths are scaled points at a
10 pt math face (Latin Modern Math) unless said otherwise.

Ordered by how much a reader would see.

---

## M9. §755 — italic correction with a subscript (review T1)

- **Section, line.** §755, l.14862-14873; the test is l.14868
  `if (math_type(subscr(q))=empty)and(delta<>0)`. Consumed by §756 (l.14884)
  and §759 l.14958 `shift_amount(x):=delta`.
- **What TeX prescribes.** For a math character with italic correction δ: if
  the atom has **no subscript**, append a kern of δ after the character and set
  delta to 0. If it **has** a subscript, append nothing and pass δ to
  `make_scripts`, which sets a lone subscript directly after the character,
  and, when there is a superscript as well, shifts the superscript δ to the
  right of the subscript.
- **What the prototype does.** `advtext/math/mlist.py`, `_translate_nucleus`
  (l.586-600): appends the kern whenever δ ≠ 0 **and** returns δ.
  Port: `Converter::translate_nucleus`, `src/math_mlist.cpp`.
- **Minimal inputs** (RUN, `mathtext.typeset(src, 10pt)`; δ(f) = 58982 sp =
  0.9 pt = 0.09 em):

  | source | prototype | TeX, by hand from §755/§756/§759 |
  |---|---|---|
  | `f^2` | `f`, kern 58982, sup box — width 642252 | the same (no subscript: kern, delta := 0) |
  | `f_i` | `f`, **kern 58982**, sub box — width 571145 | `f`, sub box — width 512163. The subscript is 58982 sp further left |
  | `f_i^2` | `f`, **kern 58982**, vbox(sup shifted right 58982; sub) — width 701234 | `f`, vbox(sup shifted right 58982; sub) — width 642252. Subscript at 0 after the `f`, superscript at δ; the prototype has them at δ and 2δ |
  | `x_i` | `x`, sub box | the same: δ(x) = 0 in this font |

- **How visible (the task's item 3).** `tests/golden/font/math.answers.golden`
  (`IC` records, 331 of them; italic letters read at the first size and
  divided by the em). In thousandths of an em:
  - lower-case math italic with a correction: **f 90**, q 34, c 25, k 15,
    b 14, w 3. The other twenty — a d e g h i j l m n o p r s t u v x y z —
    are **zero**.
  - upper-case: **F 134, J 106, N 106, U 105, I 85, C 73, S 60, X 51**, R 24,
    D 4. Zero: A B E G H K L M O P Q T V W Y Z.
  - Greek italic: **Γ 134, Ν 106, Ι 85, Π 77, ς 74, Ζ 68, Σ 54, Χ 51, Ω 42**,
    β 36, δ 36, η 27, Β 25, θ 14, ρ 13, ο 12, υ 12, Θ 5, Φ 4; ∂ (U+1D715) 63.
  - So: **every `f_i` is affected** (subscript 0.09 em too far right — 1.4 px
    at a 16 px em), and so is every subscripted F, N, U, J, I, C, S, X, Γ, Π,
    Σ: `N_k`, `U_i`, `C_n`, `S_n`, `F_n`, `X_i`, `I_n`, `\Gamma_i`. The most
    common variables of all — x, y, n, i, a, t — are not, because this font
    gives them no correction. With both scripts (`f_i^2`, `N_k^2`) the
    superscript is also a whole correction too far right.
  - Big operators are **not** affected: `make_op` (§749) removes the
    correction when there is a subscript, and the prototype does that
    (`\int_a^b` is right; ∫ has 332).
- **Fix.** In `translate_nucleus`: for `MATH_CHAR`, append the kern only when
  the atom's subscript is empty, and return 0 in that case, δ otherwise. The
  function does not currently see the atom; `translate` does. `clean_box` and
  `make_op` call it with no scripts in play and must keep the kern.

## M10. §735 — `make_under` omits the clearance below the rule (review T2)

- **Section, line.** §735, l.14441-14453; l.14449
  `delta:=height(y)+depth(y)+default_rule_thickness`.
- **TeX.** y := vpack(nucleus, kern 3θ, rule θ); delta := height(y) + depth(y)
  + θ; height(y) := height(x); depth(y) := delta − height(y). So the box
  extends one rule thickness below the rule.
- **Prototype.** `_make_under` (`mlist.py:1177-1192`): `total = stacked.height
  + stacked.depth` with no θ. Port: `Converter::make_under`.
- **Minimal input** (RUN): `\underline{x}` at 10 pt; θ = 26214.
  Prototype: box height 289669, **depth 112065** (= 7209 + 3·26214 + 26214).
  TeX: depth 112065 + 26214 = **138279**. Nothing moves inside the box; what
  is below an underlined term (a line, a denominator, a subscript's clearance)
  sits 0.4 pt closer than TeX sets it.
- **Fix.** Add `thickness` to `total`.

## B7. §877/§884 — discardables pruned after a discretionary that has post-break text

- **Section, line.** §877 l.17279-17280 `if cur_p<>null then if not
  post_disc_break then <Prune ...>`; `post_disc_break` set at §884 l.17387;
  the trial agrees, §840 l.16571 `if post_break(cur_p)=null then s:=link(v)`.
- **TeX.** After a break at a discretionary whose post-break list is not
  empty, the next line starts with that list and nothing after it is pruned.
  Pruning happens only if the post-break list was empty.
- **Prototype.** `_line_nodes` (`linebreak.py:472-481`) prunes after every
  break. Port: `LineBreaker::build_line`, and `trial` through `_resume`.
- **Minimal input** (RUN, `_line_nodes` directly): body = box a (10 pt),
  `Disc(pre=[box 3pt], post=[box x 4pt], replace_count=0)`, glue 5 pt, box b
  (10 pt); the line after a break at the Disc.
  Prototype: `[x, b, penalty, parfill, rightskip]` — natural width 14 pt.
  TeX: `[x, glue 5pt, b, ...]` — 19 pt.
- **Reach.** Only a list with a hand-built discretionary followed directly by
  glue (the library builds none itself: no hyphenation). The public API cannot
  produce one today.
- **Fix.** In `build_line`, skip the pruning loop when the previous break was
  a DISC with a non-empty `post_break`; `tabulate` already declines lists with
  a DISC, so `trial` is unaffected.

## B6. §887 — when the left skip is left off a line

- **Section, line.** §887 l.17408 `if left_skip<>zero_glue`; a glue parameter
  is `zero_glue` whenever its width, stretch and shrink are all zero (§1229
  l.23002 `trap_zero_glue`), whatever the orders.
- **Prototype.** `params.left_skip != GlueSpec()` (`linebreak.py:504`): all
  five fields. Port: `build_line`, `trial`.
- **Minimal input** (RUN): `left_skip = GlueSpec(0, stretch 0, FIL)`.
  Prototype: every line begins with a zero glue (a `SET_GLUE` command and one
  reading index a line). TeX: no glue.
- **Reach.** A caller that writes a zero skip with an infinite order. The
  port's own `skips()` writes `GlueSpec()` for "none", so nothing the library
  composes shows it.
- **Fix.** Compare width, stretch and shrink only. NOTE this changes selection
  indices for such a paragraph; `docs/port-hazards.md` linebreak item 7 (which
  itself mis-states §887: it *does* skip a zero left skip).

## B5. §827/§868 — the skips keep infinite shrink

- **Section, line.** §827 l.16309 `check_shrinkage(left_skip);
  check_shrinkage(right_skip)`; §868 l.17119 for each glue of the list, which
  includes the `\parfillskip` appended in §816; §826 the error.
- **TeX.** No glue that enters a line of a paragraph has infinite shrink.
- **Prototype.** `_prepare` (`linebreak.py:421-430`) makes only the list's
  glue finite; `left_skip`, `right_skip` and `par_fill_skip` go into lines as
  given, and the docstring (l.54-57) says "exactly as §825-§826".
- **Minimal input** (READ): any paragraph, `right_skip = 0pt minus 1fil`,
  width narrower than a word. Prototype/port: `hpack` answers `INFINITE_GLUE`,
  `classify` scores badness 0 and decent, so **no line is ever overfull or
  tight and every break is feasible** — the first pass accepts one line per
  breakpoint-or-fewer at zero badness; lines are set with the right skip
  shrunk negative. TeX: the skip's shrink is made finite (the same amount, 1 pt, at
  normal order) and the lines are judged normally.
- **Reach.** A caller passing such a skip. `skips()` in `src/paragraph.cpp`
  writes stretch only.
- **Fix.** Reset `shrink_order` on the three skips in `break_paragraph` before
  use (and optionally report it, as §826 does).

## B3a. §854 with no `easy_line` — which chain gets the last resort

- **Section, line.** §854 l.16824-16829: `final_pass and
  (minimum_demerits=awful_bad) and (link(r)=last_active) and (prev_r=active)`;
  `easy_line` §848 l.16717, §850 l.16741.
- **TeX.** With a uniform shape every line number is one class, so each
  breakpoint has at most one active node per fitness class: the one with the
  fewest demerits. When a line cannot be made feasible on the final pass, the
  break is forced from the last node standing — a best chain to that point.
- **Prototype.** One class per line number (`linebreak.py:41-45`), so a
  breakpoint reached in 3 lines and in 4 lines keeps both nodes. `alone`
  (`linebreak.py:664`) is true only of the last node in the order, which is
  the one with the **highest line number**. `linebreak.py:74-77` records this
  for ragged text ("the chain that put every earlier word on its own line")
  and gives it as a reason for first fit. It is not weighed for justified
  text, where it also holds.
- **Scenario** (READ; not constructed numerically): a justified paragraph in
  which some breakpoint p is feasible both as the end of line n and of line
  n+1 within `tolerance` (common at tolerance 1000, the port's
  `prose_breaking()`), followed by an unbreakable word wider than the measure
  (a URL). At the breakpoint after that word both nodes are infeasible; the
  n-line node is deactivated (not alone), the (n+1)-line node is alone and
  gets the artificial break. The paragraph before the long word is set in
  n+1 looser lines although the n-line chain had fewer demerits. TeX keeps
  one node for p — the cheaper — and forces from that.
- **Fix options.** (i) restore `easy_line` merging for the uniform tail of the
  shape — the faithful one, and it also removes the quadratic growth that the
  work budget exists for; (ii) in the last-resort case, choose among the
  nodes of the same breakpoint by demerits. Either changes the goldens only
  for paragraphs that reach §854 with more than one line count alive. D32.3
  lists "the class-per-line-count formulation" as deliberate, so this is the
  lead's call; it is here because the consequence for justified text is not
  written down anywhere in the prototype.

## B4. §845/§874 — which of two equal breakings wins

- **Section, line.** §845 l.16658 (`link(q):=r; link(prev_r):=q`: a new active
  node goes in front of the first node of the next class), §836 l.16485
  (fitness order), §855 l.16849 (`<=`), §874 l.17209 (first of the fewest).
- **TeX.** Shaped lines (`l <= easy_line`): within a line-number class the
  newest active nodes come first, so of two candidates r with equal totals
  the one from the **earlier** breakpoint is met last and wins the `<=`.
  Unshaped lines: one class in order of creation, across line counts.
- **Prototype.** Sorted by line number, then survivors before created
  (`linebreak.py:637, 712`): within a line number the oldest is met first and
  the **later** breakpoint wins the `<=`; across line counts both survive and
  the final choice takes the fewest lines (`min` at `:740`).
- **Input** (READ): needs two chains with exactly equal total demerits — e.g.
  material in which two consecutive breakpoints give lines of equal badness
  and class on both sides (identical words and spaces, a measure that makes
  the two splits mirror images). Not constructed; the review (item H) reached
  the same conclusion by reading.
- **Fix.** Follows from (i) under B3a; on its own it would mean reordering the
  merge in `try_break` (created before survivors within a line number) for
  shaped lines.

## Y1. §1044 — sentence-space stretch and shrink are rounded, not truncated

- **Section, line.** §1044 l.20374-20375, `xn_over_d` (§107 l.2299), which
  truncates.
- **Prototype.** `round(stretch * sf / 1000)`, `round(shrink * 1000 / sf)`
  (`style.py:619-620`), half to even. Port: `interword_glue`.
- **Input** (READ): space factor 3000, shrink 7 sp → TeX 2 (7000/3000 = 2.33
  truncated), prototype 2; shrink 5 sp → TeX 1 (1.67), prototype 2. One scaled
  point, wherever the fraction is a half or more.
- **Reach.** Invisible (2^-16 pt); it can move a badness across a fitness
  threshold in principle.
- **Fix.** `floor_div` on non-negative operands.

## M11. §761 — a fraction's spacing class. READ THIS BEFORE "FIXING"

- **Section, line.** §761 l.14996 `fraction_noad: s:=fraction_noad_size;` —
  `t` keeps the default set at l.14988, `ord_noad`.
- **This `tex.web`.** A fraction noad spaces as Ord.
- **Prototype.** `_spacing_type` (`mlist.py:463`) answers INNER, and
  `atoms.py:20-23` attributes that to §761. Port: `mlist_to_hlist`,
  `AtomType kind = AtomType::INNER`.
- **Why not to change it without checking.** *The TeXbook*, Appendix G rule
  15e, makes the result of a generalised fraction an Inner atom, and my
  recollection — NOT verified here, no network source was read — is that
  later `tex.web` (3.14159265, 2014) changed this very line to set
  `t:=inner_noad`. The copy in `../imgui-adv-text/docs` is 3.1415926 (2008).
  If that is right the prototype matches current TeX and the file in hand is
  the outlier. Recommendation: leave the behaviour, correct the citation (done
  in the port's comments), and have someone check a current `tex.web`.
- **Input** (READ): `a \over b` followed by `c`, as a bare fraction item then
  an Ord. Prototype: thin space (Inner–Ord, table entry `1`, non-script
  styles). This `tex.web`: no space (Ord–Ord).

## M14. §703 — `cur_mu` rounded half to even where TeX truncates (review T3)

- **Section, line.** §703 l.13867 `cur_mu:=x_over_n(math_quad(cur_size),18)`.
- **Prototype.** `round(math_quad / 18)` on the unrounded float
  (`mlist.py:365`). Port: `MathFonts::mu`.
- **Input** (READ): quad 655360 sp → 36408.89: TeX 36408, prototype 36409. A
  thick space (5 mu) is 5 sp wider. Invisible.
- **Fix.** Floor the quotient of the rounded quad. Every inter-atom glue in
  the math goldens moves by a few sp.

## M13. §754/§720 — a sublist nucleus is not packed; `clean_box` re-wraps (review T3)

- **Section, line.** §754 l.14849 `p:=hpack(link(temp_head),natural)`; §720
  l.14194-14196 ("it's already clean").
- **Prototype.** `_translate_nucleus` returns the sublist's nodes
  (`mlist.py:600-610`); `_clean_box` always packs.
- **Effect.** Dimensions are equal. The node structure differs (TeX has one
  more box around a group, one fewer around an already-clean box), and glue
  inside a group can take part in the outer list's stretching if that list is
  ever packed to other than its natural width — which nothing in the library
  does to a formula (it is an atom box). No visible effect found.
- **Fix.** Optional; it changes every `mlist.golden` node list that has a
  group.

## Smaller, listed so nobody rediscovers them

- **B14** §851 l.16763 / §853 l.16811: an exact fit with *negative* total
  shrink is `inf_bad+1` in TeX and decent here.
- **M12** §752-§753: no `make_ord`. An OpenType math face has no TFM lig/kern
  program; nothing to apply.
- **M15** §736: `make_vcenter` accepts any nucleus (TeX: a vbox only).
- **M16** §716: math glue is whole mu and finite.
- **M17** §762 l.15026: `div 500` floors (differs only for negative `delta1`).
- **Y5** §1043: no `space_skip`/`xspace_skip`.
- **Y8** §847: a line is never under one point (`ui/__init__.py:1357`).
- **Y4** §1034: "upper case" is `str.isupper()` over Unicode.
- **B5a, B8, P3, S3**: no visible effect (register).
- **Y6** §1091 (indent twice): already fixed at the public API.
- **Y10** (not `tex.web`): U+00A0 and U+202F are breakpoints
  (`markdown.py:1064`); a host cannot ask for a tie.

## The port's own bugs found while reading

None. Every departure found is the prototype's, reproduced; the token streams
of the files changed in this task are the snapshot's apart from the renames
listed in the result file.

## Citations the prototype has wrong (for whoever edits it)

`mlist.py` "§727" for the Bin tests → §728/§729; "§740" accents → §738 (§740
is the larger-accent step inside it); "the reason §726 gives" → §725;
"§766's `bin_op_penalty`" → set in §761, used in §767; `pack.py` §659 for the
zero-stretch case → §658; §666 for `last_badness := 1000000` → §664;
`atoms.py:20-23` §761 "a fraction spaces as Inner" → not in this `tex.web`
(M11); `linebreak.py:54` "exactly as §825-§826" → not for the skips (B5);
`tex-notes.md` spot-check "§851 = demerits" → §851 opens that part of
`try_break`, the demerits themselves are §859.
