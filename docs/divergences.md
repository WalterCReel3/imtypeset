# Divergences

Where the port, the Python reference (`../imgui-adv-text`) and `tex.web` do not
agree. Append only, under a heading for the task that found it. Each entry
says which of the three differ, what the port does, and what pins it.

## Register of departures from `tex.web`, classified (tex-web-pass)

Every place where the port does something `tex.web` does not, one entry each,
found by reading the sections named against the sources (`tex.web` version
3.1415926, 25,010 lines; section numbers counted from the top of the file and
calibrated on §108 `badness`, §749 `make_op`, §859 the demerits). Each entry
gives the section and the line in `tex.web`; what TeX does; what the port does;
and a classification:

- **deliberate in the prototype** — the Python prototype's own documents say
  so, at the file and line given (`../imgui-adv-text`). These stay
  (`docs/decisions.md` D32.3).
- **undeclared in the prototype** — the prototype does it and no document of
  its says so, or says the opposite. Task `tex-fixes` made nine of them
  switches (`src/tex_rules.h`, `docs/decisions.md` D41): the port does what
  the prototype does under `TexRules::reference()`, which every golden replay
  runs under, and what `tex.web` does under `TexRules::product()`, which the
  public API uses. Such an entry says **switch** and names it. An entry marked
  *no visible effect* changes no position or dimension anybody can observe and
  is listed for completeness.
- **the port's own** — not in the prototype either: hardening, budgets,
  clamps, representation. The decision number is given.

The sections after this register are the ones each task wrote as it found
things, unchanged; a register entry that has a fuller account there says
where. The port's behaviour is the prototype's in every entry not classed as
the port's own; the goldens hold it there. Where the comment in the source
states the departure, the file is named.

### Line breaking — `src/linebreak.*` (tex.web parts 38, 39)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| B1 | §863 l.16999, parts 40–43 (§891–§966) | the second pass tries to hyphenate every word | no hyphenation; the second pass is the first at `tolerance` | deliberate: `advtext/linebreak.py:27-34`, `docs/decisions.md:515-519` |
| B2 | §822–§824 l.16193, §832, §837–§844, §860 | keeps the width from each active node to the current node as `active_width` and delta nodes | every candidate line is measured by `hpack` when tried (laid out, or from running totals, B17) | deliberate: `advtext/linebreak.py:18-23, 35-40`, `docs/decisions.md:527-530` |
| B2a | §837 l.16519-16534, §879 l.17297 | `break_width` discounts every discardable after a break; a line that ends inside that run is judged at a width it will not have (the "anomalous case") | the candidate judged is the line that will be built | deliberate, as a consequence of B2 (`linebreak.py:18-23`: the line scored and the line emitted cannot disagree) |
| B3 | §848 l.16717, §850 l.16741, §835 l.16464 | `easy_line`: all line numbers past `last_special_line` are one class, so one active node per fitness class per breakpoint | every line number is its own class | deliberate in the prototype (`advtext/linebreak.py:41-45`); **switch** `easy_line`, ON in `product()` (D41.2) |
| B3a | §854 l.16824-16829 | the last resort is taken from the one active node left | the same test, but with B3 several nodes for one breakpoint can be left and the last in order — the highest line number — gets it, whatever its demerits | deliberate for ragged text only (`advtext/linebreak.py:74-77`), undeclared for justified; **switch** `easy_line`. Hand-worked case: `easy_line_the_last_resort_is_forced_from_the_cheapest_chain` (reference 2, 2, 2, 1 at 1,452 demerits; `tex.web` 3, 3, 1 at 200) |
| B3b | §851 l.16769, §854 l.16825 | the free break is taken for any forced break, not only an overfull line, when `minimum_demerits` is still `awful_bad`; with one class of line numbers that means no feasible break to this point was found at all | `minimum_demerits` starts afresh for each line count, so at a forced break (a hard line break, the paragraph's end) the last line count's lone node gets the free break although a chain with fewer lines was feasible | undeclared, found by `tex-fixes`; a consequence of B3 and gone under **switch** `easy_line`. `hard-breaks-after-space-justify` at 45 pt: 7 lines at 152,542 demerits, 5 at 10,661 under the switch, no overfull line in either |
| B4 | §845 l.16658, §836 l.16485, §855 l.16849, §874 l.17209 | new active nodes are linked in ahead of the next class; with `easy_line` unshaped lines are one class in order of creation; first of the fewest wins | active nodes are met in order of line number, then of becoming active; first of the fewest wins: fewest lines, then lowest fitness | undeclared; **switch** `easy_line`. Hand-worked case: `easy_line_of_two_breakings_with_equal_demerits_the_later_node_wins` |
| B5 | §827 l.16309, §868 l.17119, §826 l.16293 | `check_shrinkage` on `left_skip`, `right_skip` and every glue of the list (the par-fill glue is one), with an error once | list glue only, silently; `left_skip`, `right_skip`, `par_fill_skip` go into lines as given, and a line holding infinite shrink always fits | undeclared; **switch** `skips_made_shrink_finite`. §826's error is still not reported |
| B5a | §825 l.16280 | leaves a zero shrink of infinite order alone | resets the order of every glue's shrink | undeclared, *no visible effect* |
| B6 | §887 l.17408, §1229 l.23002 | `left_skip` is left off a line when it is `zero_glue`: width, stretch and shrink all zero, whatever the orders | left off only when equal to the default spec in all five fields | undeclared; **switch** `left_skip_omitted_when_zero` |
| B7 | §877 l.17279, §884 l.17387, §840 l.16571 | after a break at a discretionary with post-break material, discardables at the start of the next line are kept | they are dropped | undeclared; **switch** `post_break_text_stops_pruning` |
| B8 | §882 l.17368 | the emptied `disc_node` stays in the line | it is not carried into the line | undeclared, *no visible effect* |
| B9 | §875 l.17216 | `\looseness` | none | deliberate: `advtext/linebreak.py:46-48` |
| B10 | §890 l.17438, §888 l.17413 | club, widow, broken and interline penalties; adjust material; lines appended to the vertical list | lines are returned; none of it | deliberate: `advtext/linebreak.py:49-53` |
| B11 | §849 l.16721 | negative `hang_after` indents the first lines | `ParShape::hanging` takes a non-negative one only | deliberate: `advtext/linebreak.py:262-263` |
| B12 | §863 l.17017 | `emergency_stretch` is added to the background | a finite-stretch glue appended to each candidate, absent from the chosen lines | deliberate: `advtext/linebreak.py:574-578`; **same result** |
| B13 | §852 l.16795 | quick test for a hopeless stretch before calling `badness` | not made | deliberate: `advtext/linebreak.py:528-532`; **same result** |
| B14 | §851 l.16763, §853 l.16811 | a shortfall of zero goes to the shrink case, which is `inf_bad+1` if total shrink is negative | an exact fit is decent, badness 0 | undeclared; reachable only with glue of negative shrink |
| B15 | — | no first-fit breaker; ragged text is total fit under a `fil` right skip | `first_fit`, used for everything not justified | deliberate: `advtext/linebreak.py:59-82`, `docs/decisions.md:663` |
| B16 | §859, §855 (32-bit integers) | demerits in 32 bits, unchecked | parameters clamped to 2^40, demerits in 64 bits and saturating; `replace_count` bounded; a node naming nothing replaced by an empty rule | the port's own: D33, `docs/port-hazards.md` linebreak items 10, 11 |
| B17 | §822–§824 | (see B2) | `tabulate`/`trial`: candidates judged from running totals over the prepared list where the paragraph allows; same answers as laying each out (`tests/test_linebreak_totals.cpp`) | the port's own: `docs/performance.md`; no decision number |
| B18 | §863 | three passes run to the end | a work budget of 2^20 candidate lines, then first fit, reported | the port's own: D33; `## stress` item 1 below |

Stated in `src/linebreak.h` ("WHERE THIS IS NOT tex.web") and at each function
in `src/linebreak.cpp`.

A correction to `docs/port-hazards.md`, linebreak item 7, which says the
reference's omission of a zero `left_skip` is "not tex.web": §887 does omit a
zero left skip. The departure is B6, the test used.

### Packaging — `src/pack.*` (part 33)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| P1 | §660 l.13067, §666 l.13131, §667, §674–§678 | reports a box only past `hbadness`/`hfuzz`; appends `overfull_rule` | `PackResult` always answers badness and fitness; no rule appended | deliberate: `advtext/pack.py:56-59` |
| P2 | §655 l.13006, §656 l.13022 | moves inserts, marks, adjusts out; leaders set height and depth | no such nodes | deliberate: `advtext/pack.py:60-62` |
| P3 | §664 l.13109-13114 | an overfull box with no shrink at all has `glue_sign` normal and a ratio of one | `SHRINKING` and a ratio of one | undeclared, *no visible effect* (nothing to multiply) |
| P4 | §669 l.13188, §653 l.12980 | `confusion("vpack")` for a character in a vlist; a `null_flag` width is added unexamined | the reference raises `ValueError` (`pack.py:357`, `:434`); the port names the error and leaves the node out | the port's own: D17 |
| P5 | §651–§656, §669–§671 | 32-bit sums, unchecked | `Guarded`/`Checked`: sums proven or checked | the port's own: D33 |
| P6 | §649, §668 | `hpack`/`vpackage` build the box node | they measure; the caller stores | the port's own: D28 (`src/pack.h`) |

`append_to_vlist` taking a rule is §1056 (l.20520) folded in, not a departure.

### Ship-out — `src/shipout.*` (part 32)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| S1 | §626–§628, §635–§637, §640 l.12737 | leaders; the DVI machinery; page offsets | none | deliberate: `advtext/shipout.py:102-107` |
| S2 | — | nothing is emitted for glue; no reading index, lines or atoms | `SET_GLUE`, `ATOM`, `LINE` and the index | deliberate (the reference's design; `advtext/shipout.py` module docstring); the command format is D26 |
| S3 | §623 l.12412, §632 l.12587 | an empty box is stepped over | walked like any other | undeclared, *no visible effect* |
| S4 | §630 l.12560 | `confusion("vlistout")` | an error status | the port's own: D17 |
| S5 | §619, §629, §641 | unchecked positions; a page past `max_dimen` refused | checked sums, `max_items`, a depth bound | the port's own: D33, D26 |

The glue rounding of §625 is reproduced, Pascal's `round` included.

### Units, badness, nodes — `src/units.h`, `src/badness.*`, `src/nodes.*` (parts 7, 10)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| U1 | §101, §104, §421 l.8495 | 32-bit `scaled`, `arith_error` | 64-bit `Scaled` with `MAX_DIMEN` as an invariant, checked where a list is foreign | the port's own: D33 (`src/units.h`) |
| U2 | §108 l.2338 | `t>=0` is a precondition | a negative `t` answers `INF_BAD` (the reference raises) | the port's own: D17 |
| U3 | §99, §106, §107 | `div` truncates, `x_over_n`/`xn_over_d` truncate | `floor_div`, `py_round`: the reference's `//` and `round()`. Each place it changes an answer is its own entry (Y1, M14, M17) | undeclared as a general rule |
| N1 | §140–§146, §159 | ligature, insert, mark, adjust, whatsit, unset nodes | none | deliberate: `advtext/nodes.py:27-31` |
| N2 | §150 l.3147 | a glue node points at a shared spec; `zero_glue` is an identity | glue holds its spec; equality only (hence B6) | the port's own representation: D21, D28 |

### Math — `src/math_atoms.h`, `src/math_mlist.*` (parts 34–36)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| M1 | §706 l.13895, §708 l.13957, §710–§714 | builds extensible delimiters; returns the largest variant when none is large enough | walks the variant chain only; a `\left`/`\right` uses the largest while it covers ¾, else refuses; a radical always refuses | deliberate: `advtext/math/mlist.py:42-57`, `docs/decisions.md:520-526`. `## math-mlist` item 1 |
| M2 | §737 l.14478-14488 | a TFM surd whose height is the rule thickness | the font's `RadicalRuleThickness` and gaps; the glyph hung from the top of its ink | deliberate: `advtext/math/mlist.py:59-67`. Item 2 |
| M3 | §748 l.14656 | a fraction is set between two delimiters | not done | deliberate: `advtext/math/mlist.py:81-84`. Item 3 |
| M4 | §738, §731, §732, §688, §767 l.15099 | accents, `\mathchoice`, `\nonscript`, style nodes, penalties after Bin and Rel | none | deliberate: `advtext/math/mlist.py:85-92`, `docs/decisions.md:531`. Item 3 |
| M5 | §749 l.14688 | one step along the character list for a display operator | the same, over the OpenType variant chain | not a departure from `tex.web` (it is one from OpenType's rule): `mlist.py:11-23`. Item 4 |
| M6 | §728, §760, §761 | `left_noad`/`right_noad` kept apart until the second pass | Open/Close from the start, sized after the first pass | no difference in result: `mlist.py:455-459`. Item 5 |
| M7 | — (plain.tex) | `\quad` is an em of the text font | eighteen math units | deliberate: `advtext/math/atoms.py:633-636`. Item 6 |
| M8 | — | text in a formula is an hbox made in horizontal mode | `MATH_TEXT`, set at the style's size | deliberate (the reference's design, `mlist.py` `_text_box`) |
| **M9** | **§755 l.14868** | the italic-correction kern follows a math character only when there is **no subscript**; with one, `delta` goes to `make_scripts` and the superscript is set that far right of the subscript (§759 l.14958) | the kern is always appended and `delta` is still returned: a subscript sits one correction too far right, a superscript over a subscript two where TeX has one | undeclared; **switch** `italic_kern_only_without_subscript`. Review item T1 |
| **M10** | **§735 l.14449** | `make_under` adds one `default_rule_thickness` below the rule | not added: the box is that much shallower | undeclared; **switch** `underline_clearance`. Review item T2 |
| M11 | §761 l.14996 | a `fraction_noad`'s spacing type is left at `ord_noad` | a fraction spaces as Inner | **not changed, and not established as a departure**: agrees with *The TeXbook* (Appendix G, rule 15e) and, by recollection only, with current `tex.web`; differs from the 2008 copy in this tree (§761 l.14996). Left for the user (D41.3) |
| M12 | §752–§753 l.14759, §733 l.14419 | `make_ord`: kerns and ligatures between adjacent math characters from the TFM | none | undeclared; an OpenType math face has no such table, so there is nothing to apply |
| M13 | §754 l.14849, §720 l.14194-14196 | a sublist nucleus is `hpack`ed into one box; `clean_box` reuses a result that is one unshifted box | the sublist is spliced unpacked; `clean_box` always packs | undeclared; **switch** `sublist_nucleus_packed`; dimensions equal. Review item T3 |
| M13a | §721 l.14202-14209 | "Simplify a trivial box" is applied to whatever `clean_box` returns, a box used as it stands included | a `SUB_BOX` field that is already clean is returned untouched | undeclared, found by `tex-fixes`; under **switch** `sublist_nucleus_packed` the section is applied. No dimension changes |
| M14 | §703 l.13867 | `cur_mu` is `x_over_n(math_quad,18)`: truncated | `round(math_quad/18)`, half to even | undeclared; **switch** `mu_truncates`. Review item T3 |
| M15 | §736 l.14458 | `make_vcenter` accepts a vbox only | any nucleus | undeclared; a superset |
| M16 | §716 l.14096 | `math_glue` scales fractional mu, truncating, and passes infinite orders through | whole math units, finite orders | undeclared; a restriction of the vocabulary, exact where it applies |
| M17 | §762 l.15026 | `(delta1 div 500)` truncates | floors | undeclared; differs only for a negative `delta1` |
| M18 | — | — | statuses for exceptions, `MAX_MATH_NESTING`, `MAX_MATH_ITEMS`, lengths clamped | the port's own: D17, D28, D33. `## math-mlist` items 7–14 |

`MATH_SPACING` is §764's string (l.15064) verbatim, and `THIRD-PARTY.md` says
so. Citations the reference has wrong, corrected in the port: its "§727" for
the Bin conversions is §728 and §729; "§740" for the accent is §738; "the
reason §726 gives" for the two passes is §725; "§766's" penalties are §767's;
`check_dimensions` is a label in §727.

### Text composition and paragraphs — `src/style.*`, `src/paragraph.*`

Each has its account under `## style`, `## oracle-para`, `## para` or
`## public-api` below; this adds the classification.

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| Y1 | §1044 l.20374-20375 | stretch and shrink scaled with `xn_over_d`, truncating | `round()`, half to even | undeclared; **switch** `sentence_space_truncates`. One scaled point |
| Y2 | §1042 l.20342 | space, stretch, shrink, extra space from `\fontdimen` | fractions of the advance of U+0020 | deliberate: `advtext/style.py:168-172, 249` |
| Y3 | §1044 | — | `Spacing::EXTRA_SPACE_ONLY` | deliberate: `advtext/style.py:187-190, 312` |
| Y4 | §1034 l.20123 | an assignable `\sfcode` per character | four fixed classes | deliberate: `advtext/style.py:200-202`. "Upper case" as `str.isupper()` over all of Unicode: undeclared |
| Y5 | §1043 l.20359 | `space_skip`, `xspace_skip` | not present | undeclared; parameters nobody sets |
| Y6 | §1091 l.21125 | the paragraph indent is one empty box | the reference also narrows and shifts the first line: applied twice | **undeclared**; fixed at the public API (D32.3; `## public-api` item 1) |
| Y7 | §1105 l.21268 | `delete_last` removes one glue node | `unskip` removes every trailing glue and a penalty before each | deliberate: `advtext/markdown.py:1530-1544` |
| Y8 | §847 | line widths as given | never under one point | undeclared (`advtext/ui/__init__.py:1357`); a clamp |
| Y9 | §813–§890 | (B15) | not-justified text is first fit on unshrinkable material | deliberate: `docs/decisions.md:663` |
| Y10 | — (TeX's tokens; `~` is a plain.tex macro) | a no-break space is not a breakpoint | every `str.isspace()` character is interword glue, U+00A0 included | **undeclared**; not a `tex.web` matter, listed because a host cannot ask for a tie. `## oracle-para` item 2 |

The strut, the hung marker, alignment as skips and the paragraph terminator
are plain TeX macros restated as functions, not departures from `tex.web`.
`## mathtext`'s list (a `\frac` is an Inner atom holding the fraction, `\ `
is six math units, `\binom` fits its parentheses) is deliberate:
`advtext/mathtext.py:60, 89, 107, 920`.

### Counts

67 entries: 27 deliberate in the prototype (B12 and B13 with the same result
as TeX; B3a deliberate for ragged text and unweighed for justified), 25
undeclared in the prototype, 12 the port's own, 2 that are not departures
(M5, M6), and M11, which is not established as one.

Of the undeclared ones, eleven are behind nine switches of `TexRules` and are
`tex.web`'s under `TexRules::product()`: B3a, B3b, B4, B5, B6, B7, Y1, M9,
M10, M13 with M13a, M14 (and B3, deliberate, goes with them). Y6 was already
fixed at the public API. Still the prototype's under both presets: B5a, B8,
B14, P3, S3, U3, M12, M15, M16, M17, Y5, Y8 (no visible effect, or reachable
only by input nobody writes), Y4's `str.isupper()`, and Y10.

How many golden paragraphs and formulas each switch moves is in
`docs/decisions.md` D41 and is asserted by `tests/test_tex_rules.cpp`.

## style

**Reference versus `tex.web`** — the port does what the reference does; the
goldens in `tests/golden/math/style.golden` and
`tests/golden/core/paragraphs.golden` hold it there.

- **§1044, the sentence-space arithmetic.** `tex.web` scales the stretch and
  the shrink with `xn_over_d`, which truncates. The reference's
  `interword_glue` takes `round()` of the true quotient, half to even
  (`style.py`, `interword_glue`), and so does `interword_glue` in
  `src/style.cpp`. They differ by one scaled point wherever the quotient's
  fraction is a half or more.
- **§1042, where the three space parameters come from.** `tex.web` reads the
  space, its stretch, its shrink and `extra_space` from the font's
  `\fontdimen`s. An OpenType face states only the advance of U+0020, so the
  reference synthesises the others as fractions of it — 1/2, 1/3, 1/3, cmr10's
  proportions — and rounds each once. The port carries the fractions
  (`SPACE_STRETCH_FRACTION` and its neighbours in `src/style.h`).
- **§1044, `Spacing::EXTRA_SPACE_ONLY`.** Widening a sentence space without
  scaling its stretch and shrink has no counterpart in `tex.web`. It is the
  reference theme's default and `Composition`'s.
- **§1034, the `\sfcode` table.** `tex.web` has a table a format file sets;
  the reference has four fixed classes, and "upper case" is Python's
  `str.isupper()` over all of Unicode (`Ⅰ` and `Ⓐ` are upper case, `ǅ` is
  not), not `A`–`Z`. The branches of §1034 themselves are transcribed.
- **§816, the paragraph terminator.** `tex.web` removes a glue node at the end
  of the list before appending `\parfillskip`. The reference never has one to
  remove — `text_block` strips the text first — and `fill_and_break` appends
  the three nodes without looking. In a block set ragged left or centred the
  reference leaves the `\parfillskip` out of a hard break altogether, which is
  plain TeX's `\raggedleft` setting it to zero and not anything in `tex.web`.
- **White space.** TeX's interword glue comes from a space token, and `~` is a
  macro with a penalty in it. In the reference every character for which
  `str.isspace()` is true makes interword glue, U+00A0 included: a no-break
  space is a breakpoint. The port matches (`py_isspace`).

**Port versus reference** — the port's own decisions, where the reference's
behaviour cannot or should not be carried over.

- **`parse_font_key` and digits outside ASCII.** The reference reads the size
  with Python's `int()`, which accepts any Unicode decimal digit
  (`prose@٣٤` is size 34) and non-ASCII white space around the number.
  `parse_font_key_text` accepts the ASCII forms of everything `int()` does —
  sign, underscores between digits, ASCII white space around — and refuses the
  rest. One record of `style.golden` is affected and
  `tests/test_style.cpp` names it. A font key in the port is the packed
  `FontKey`; the text form is for messages and for reading goldens.
- **Sizes a `FontKey` cannot hold.** The reference's key is a string and takes
  any integer, negative ones included (`style.golden` has `prose@-655360`).
  `FontKey` holds a size in `[0, 2^48)`; `font_key_text` and
  `parse_font_key_text` still write and read every 64-bit size.
- **Lengths past `MAX_DIMEN`.** `CharMeasureCache`, and the space width and
  kerns `append_text` takes from a face, are held to ±`MAX_DIMEN` (2^30 − 1 sp,
  about 16,384 pt) after rounding, which is the invariant `src/units.h` gives
  the layout core. The reference's integers have no bound. No golden reaches
  it: the largest dimension in the `char_measure` goldens is 3,732,931 sp.
- **A resolver that does not know a face.** The reference lets whatever its
  resolver raised propagate out of `_append_text` and out of `char_measure`.
  `append_text` returns false with a reason and appends nothing;
  `CharMeasureCache` measures the character as `{0, 0, 0}`.
- **A space factor outside 1..32767** handed to `interword_glue` is a contract
  violation answered as 1000. The reference divides by it (and raises
  `ZeroDivisionError` at 0).
- **Malformed UTF-8.** The reference is handed `str` and never sees bytes. The
  port decodes, and a malformed sequence is U+FFFD, one per maximal ill-formed
  subpart — what `bytes.decode("utf-8", "replace")` gives. From then on it is a
  U+FFFD the text contained: an ordinary character where the face has that
  glyph, and not a `replaced_char`, because there is no written codepoint to
  remember.

## oracle-para

Found while dumping `tests/golden/para/` (`tools/oracle/dump_para.py`). Each
was run against the reference, not only read; the golden case that holds it is
named.

1. **A first-line indent is applied twice (reference vs `tex.web` §1091).**
   TeX's paragraph indent is one empty box at the head of the horizontal list
   and nothing else. The reference puts that box there
   (`markdown._Converter._text_block`) *and* gives the first line its own
   entry in the paragraph shape, narrower by the indent and shifted right by
   it (`advtext.ui._shape`). The first line's text starts
   `left_indent + 2 × first_line_indent` from the column edge and the line is
   `first_line_indent` shorter than TeX would set it. No theme the reference
   builds has a non-zero `first_line_indent`, so nothing it renders shows
   this. The port follows the reference. Cases: `runs.golden`,
   `first-line-indent-*`; `tables.golden`, `shape`.

2. **A no-break space is a breakpoint (reference vs Unicode, and vs TeX's
   `~`).** `_append_text` tests `str.isspace()`, which is true of U+00A0 and
   U+202F, so each becomes ordinary interword glue the breaker may cut at.
   The port follows the reference; a host that needs a tie has no way to ask
   for one through text. Cases: `whitespace.golden`, `no-break-space-*`.

3. **The hard-break fill is not the paragraph's fill.** `_fill_and_break`
   appends `\parfillskip`-like glue of `0pt plus 1fil` with a stretch of
   65536 sp-units of `fil` (`markdown.PARFILLSKIP`), where
   `LineBreakParams.par_fill_skip`, which ends the paragraph, has a stretch of
   1. The two never meet on one line, so no ratio between them is visible;
   the `glue_set` of the line differs by that factor and the goldens hold it.
   Cases: `tables.golden`, `constants` and `fill-and-break`.

4. **Plane-15 codepoints written as text reach the math face's variant
   glyphs.** The math face answers `has()` for the synthetic codepoints the
   reference mints for glyphs with no cmap entry (`0xFFF1B..0xFFFFD`), and
   `_stand_in` asks the math face for any character the prose face lacks. So
   U+FFFFD typed into a paragraph is set as whichever variant glyph was minted
   there, where every other private-use codepoint is replaced by U+FFFD. The
   port follows the reference. Case: `standin.golden`,
   `unassigned-and-private`.

5. **White space is per run.** `_append_text` collapses a run of white space
   only within one call. A space ending one styled run and a space opening the
   next are two glues, where one source space between two emphasis spans is
   one. Markdown never hands the builder such a pair (it splits at the
   emphasis marker, and the space belongs to one side), so the reference's
   documents do not show it; a host feeding styled runs directly will. Case:
   `runs.golden`, `space-at-run-boundaries`.

## para

Found while porting paragraph assembly (`src/paragraph.*`). The goldens named
are in `tests/golden/para/` unless another directory is given.

**Reference versus `tex.web`** — the port does what the reference does.

- **§886, §887: alignment.** `tex.web` puts `\leftskip` and `\rightskip` on
  every line and knows nothing of alignment; ragged setting is a plain TeX
  macro that sets them. The reference's `_skips` is that macro as a function,
  and for ragged-left and centred text it also sets `\parfillskip` to zero.
  `skips` in `src/paragraph.cpp`; `tables.golden`, `skips`.
- **§813–§890: text that is not justified is not broken by the optimiser.**
  The reference breaks it first-fit, on material whose interword shrink has
  been removed (`_text_boxes`, `_unshrinkable`), where TeX would run total fit
  under a `fil` right skip. `BlockStyle::ragged_total_fit` restores the
  optimiser, as the reference's `ComposeParams(ragged_total_fit=True)` does;
  no golden exercises that switch.
- **§847: a line is never narrower than one point.** `_shape` floors the
  column less its indents at 65536 sp; `tex.web` takes whatever `\hsize` and
  `\parshape` say. `tables.golden`, `shape`.
- **§1105: `\unskip` repeats and takes a penalty with it.** `tex.web` removes
  one glue node. The reference's `_unskip`, before a hard break, removes every
  glue the run ends with and, with each, an `INF_PENALTY` immediately before
  it. `unskip` in `src/paragraph.cpp`; `runs.golden`,
  `hard-breaks-after-space-*`.
- **The strut is two maxima, not a box.** TeX's `\strut` is a zero-width rule
  in the list. The reference floors a packed line's height and depth at the
  strut's (`Strut.applied_to`) and leaves the list alone, so a strut is not an
  item and takes no part in breaking.
- **The marker.** A list marker hung in the margin is `\llap`-like macro work
  in TeX and no part of `tex.web`; the reference's `_hang_marker` packs it
  beside the first line after breaking. `runs.golden`, `bullet-item-*`,
  `ordered-item-*`.

**Port versus reference.**

- **§1091: the first-line indent can be applied once.** The reference applies
  it twice (see `oracle-para` above, item 1). `BlockStyle::first_line` selects:
  `BOX_AND_SHAPE`, the default, is the reference and is what the goldens hold;
  `BOX_ONLY` is `tex.web`'s single empty box. Pinned by
  `the_first_line_indent_is_applied_twice_or_once` in
  `tests/test_paragraph.cpp`, not by a golden.
- **The map from material to shipped indices is derived, not recovered.** The
  reference matches a block's material against the commands shipped for it
  (`advtext.spans.map_text_block`) and raises `SpanMapError` when they do not
  line up. The port computes the same map from the breaker's breaks
  (`ParagraphLayout::shipped`), with no commands needed. The two agree on
  every node of 36 blocks at 3 widths (`tests/golden/para-map/map.golden`,
  written by `tools/oracle/dump_para_map.py`). Where the port cannot map — a
  break taken at a discretionary, which the reference's material never
  contains — it says so (`mapped` false) and maps nothing.
- **A box that is not an atom is named by its first index.** The reference's
  `SpanMap` also records how many indices each node covers (`extent`); the
  port records only the first. `map.golden` holds no box; the port's own test
  `an_inline_box_is_one_item_when_it_is_an_atom` is what pins this.
- **`hard_breaks` holds each boundary once.** The reference's
  `_after_hard_breaks` answers once per forced break, and two breaks with
  nothing shipped between them answer the same index twice; `src/selection`'s
  `TextData` wants each index once.
- **Lengths handed to `shape` are held to ±`MAX_DIMEN`** before they are
  subtracted. The reference's integers have no bound. No golden reaches it.
- **Malformed UTF-8 keeps `style`'s rule** (see `style` above): it is set as
  U+FFFD and is not a replaced character, so `originals` does not name it. The
  bytes that were written are still recoverable, through `Run::source_offsets`
  and `ParagraphLayout::shipped` together;
  `malformed_utf8_draws_replacement_characters_and_the_bytes_are_recoverable`
  shows how.

## draw

**Port versus reference** — `src/draw` against
`advtext/backends/imgui_draw.py`. The calls the reference makes on the draw
list (`tests/golden/draw`, the `ops` of each case) and the vertices that come
of them are reproduced exactly; what follows is where the port does something
the reference does not, each on purpose.

- **Quads are written directly, not through `AddText`.** The reference makes
  one `ImDrawList::AddText` call per glyph. The port places every glyph first,
  loads the glyphs it will draw, and writes the quads into space reserved in
  runs of up to 1,024. `tests/test_draw.cpp` replays the port's calls through
  ImGui one `AddText` at a time and requires the two draw lists to be equal
  byte for byte, for every golden case and with the clip rectangle's edges
  swept across pens and glyph boxes. One consequence is visible only past 64k
  vertices with 16-bit indices: ImGui starts a new draw command when a
  *reservation* would cross that line, so the port's commands split up to
  1,023 quads earlier than the reference's would.
- **`ImDrawListFlags_TextNoPixelSnap` is not set.** The reference sets it for
  the length of the call so that ImGui does not truncate a pen it has placed.
  The port's quads do not pass through the code that would, so the draw list's
  flags are left alone.
- **A font key that names no face** makes the reference raise `ValueError` out
  of `draw`. In the port its glyphs are counted in `DrawStats::missing`.
- **A size ImGui cannot bake.** A key whose baked size would be under half a
  pixel, over 2,048 px, or not a number is not drawn and its glyphs are counted
  missing. The reference hands the size to ImGui, which asserts on one that
  rounds to zero and would try to rasterise one that is enormous.
- **A resolver that does not know a key.** The reference raises whatever the
  resolver raised. The port asks ImGui for the key's nominal size, as the
  reference does for an em of zero.
- **Codepoint zero** is asked of the atlas as itself, which is the
  reference's behaviour and an exception to decision D27's rule that a zero
  from `atlas_codepoint` means "cannot be asked for": the reference's `ops`
  count a NUL the face maps as a glyph (placed, drawn as nothing), not as
  missing, and case `span-edges-by-hand` pins it.
- **With a 16-bit `ImWchar`** a glyph of an unbound face whose codepoint is
  above U+FFFF is counted missing; the reference only runs with 32 bits.
- **Culling by line** (`DrawArgs::cull_lines` with a `LineIndex`) has no
  counterpart in the reference's draw; its views cull. With it on, lines whose
  boxes miss the clip rectangle by more than 2 px are not visited: the stats
  count only what was visited, and quads ImGui would have appended wholly
  outside the clip rectangle are not appended. Off, which is the default, the
  port's output is the reference's.
- **The buffer output** (`DrawBuffers`) has no counterpart. It holds the same
  quads as the draw list; the debug overlay's line boxes and baselines, which
  are ImGui's own anti-aliased outlines, are left out of it.
- **Colours per block role** (`Palette.roles`, `ink_for`, `with_roles`) and
  the diff palette are not ported (decision D19): a run arrives with its ink.
- **The debug overlay's outlines are rectangles, not `AddRect` and `AddLine`.** The reference
  draws a line's box with `ImDrawList::AddRect` and its baseline with `AddLine`; ImGui makes
  anti-aliased polylines of them, whose vertices depend on the list's flags and are not the same
  in ImGui 1.93 as in 1.92.9. The port writes the frame of the box as up to four rectangles one
  pixel thick, inside it, and the baseline as the row of pixels below it — the same pixels'
  worth of outline, hard-edged, under every version and into buffers as well as lists. The calls
  (`ops`) are still the reference's and are compared; of the two cases of
  `tests/golden/draw/draw.golden` that have `debug 1`, `span-edges-by-hand-debug` and
  `debug-overlay`, the `v`, `tri` and `drawcmd` records (120 + 1,268 vertices, 68 + 662 triangles,
  one draw command each) are no longer compared, because they hold what `AddRect` and `AddLine`
  made (the port's lists have 136 and 1,324 vertices). Their `stats`, `keys`, `boxes` and `ops`
  records (`text`, `rectfilled`, `rect`, `linesegment`) are compared as before, and so is every
  record of the other twenty cases.
- **Under `DrawRules::product()`, a rule inside a rigid span belongs to the span.** The reference
  snaps every rule by its own corners and lets it end the span, so the glyphs of a formula —
  placed by adding rounded gaps from the span's first — drift from its fraction bars and radical
  bars: a radical sign 3 px from its bar after sixty terms at 13 px to the em, a numerator 5 px
  off its fraction bar after forty at 17. With the rule on (the public API), a rule in a formula
  is placed from the formula's glyphs — its left end from the nearest one placed before it, its
  right end from the nearest one — by the gap in scaled points rounded once, and from the span's
  baseline, and the span goes on through it; so the glyphs after a rule in a formula are also not
  where the reference puts them (it starts a new span there). With it off (`reference()`, the
  default of `DrawArgs`, every golden replay) the port does what the reference does. Worked by
  hand in `tests/test_harden.cpp`.

  The rule of this item is under revision in task `d7-fix`: a later review, made by reading
  with nothing run, found its placement suspect (`.concerns/review-3a--reviewer--findings.md`).
- **Rectangles take the white texel of the list they are written to** (`PrimRect`), as the
  reference's `AddRectFilled` does; glyph quads are written directly.

**Reference versus what it says of itself.**

- The reference's `snap_px` docstring says ImGui's `IM_ROUND` is
  `floor(x + 0.5)`. In the pinned ImGui it is `(float)(int)(x + 0.5f)`, which
  truncates: the two differ for every negative non-integer. The port follows
  the reference's *code*, `math.floor(value + 0.5)`, in double.

## math-mlist

`src/math_atoms.h`, `src/math_mlist.*`. The goldens
(`tests/golden/math/mlist.golden`, `formulas.golden`: 4,613 conversions) hold
the port to the reference node for node; what follows is where one of the
three differs from another.

**Reference versus `tex.web`** — the port does what the reference does, and
`src/math_mlist.h` says so at the top. Each is the reference's own decision,
restated here with the section departed from:

1. *Delimiters are chosen, never assembled* (§706 to §714). Only the walk
   along the variant chain is implemented. Where `tex.web` returns the largest
   variant when none is large enough (§706), a `\left`/`\right` delimiter is
   used only while it covers three quarters of twice the content's reach from
   the axis, rounded **up**, and is refused below that; a radical is refused
   whenever no surd covers it. The three-quarters figure is the reference's.
2. *A surd hangs from the top of its ink* (§737). `tex.web` assumes a TFM surd
   whose height is the rule thickness. The rule thickness is the font's
   `RadicalRuleThickness`, read from the font the surd was found in, and the
   clearances are the font's two radical gaps where §737 computes them.
3. *No delimiters around a fraction* (§748), no accents (§738), no
   `\mathchoice` (§731), no `\nonscript`, no style nodes inside a list, and no
   penalties after Bin and Rel atoms (§767).
4. *`make_op` reads the variant chain, not `DisplayOperatorMinHeight`.* §749's
   one step along the chain, which is not OpenType's rule. The reference
   says the two agree for Latin Modern Math; that was not checked here.
5. *The first pass sees a boundary as Open or Close.* §728 lists `left_noad`
   and `right_noad` beside `open_noad` and `close_noad` in both of its tests,
   so the outcome is the same; the boundaries are sized at the end of the
   first pass, not in the second (§760, §761), which is the same moment.
6. *`\quad` is eighteen math units*, of the math face at the size of the style
   it lands in. Plain TeX's is an em of the font outside the formula.

Citations: the reference's docstrings give §727 for the Bin conversions, which
are §728 (the tests) and §729 (the final Bin), and §740 for the accent, whose
procedure is §738. The port cites the sections as counted in `tex.web`.

**Port versus reference** — where the reference raises, cannot be handed the
input, or would not return:

7. *Errors are a status.* `DELIMITER_TOO_SMALL`, `MISSING_GLYPH` and
   `TEXT_FACE_UNAVAILABLE` are the reference's `MathLayoutError`s, with the
   same numbers; the first two reproduce the reference's message to the
   character (compared by the tests). `NOT_IMPLEMENTED` is its
   `NotImplementedError` and `MALFORMED` its `TypeError`; their messages, and
   `TEXT_FACE_UNAVAILABLE`'s, hold the Python spelling of an object and are
   not reproduced. On any error nothing is appended and the arena is rewound.
8. *`MALFORMED` also covers what Python cannot express*: a sublist or prose
   span outside the list, a box index the arena does not hold, a tag that is
   not a member of its enum, a style above 7, and fonts never initialised.
9. *Nesting is bounded at 64 sublists* (`MAX_MATH_NESTING`, `TOO_DEEP`). The
   reference's bound is its interpreter's recursion limit: at the default of
   1000 a tower of nested superscripts sets up to 197 levels (measured). Its
   formula parser stops at 24, and the deepest list in the goldens is 24. A
   hand-built list between 65 levels and that limit sets there and is refused
   here.
10. *Work is bounded at 2^20 items* (`MAX_MATH_ITEMS`, `TOO_LARGE`), counting
    an item once per path to it. A hand-built list can share one sublist among
    many fields; the reference would not return from forty levels of that.
11. *Lengths from a host are clamped to `MAX_DIMEN`*: a fraction's stated
    thickness, and each product of `math_glue` (§716). The reference's
    integers are unbounded. Nothing in the goldens is near the bound.
12. *`MathFonts::init` refuses* a base size outside (0, `MAX_DIMEN`], a
    resolver with no face at one of the three sizes, and script percentages
    that do not give a size in that range. The reference carries on with
    whatever `round()` gave it, or raises from the resolver.
13. *Prose is UTF-8.* A malformed sequence decodes to U+FFFD
    (`decode_utf8`'s policy) and is then a character the face has or lacks;
    with the fake prose face it is `MISSING_GLYPH` for U+FFFD. A Python string
    has no malformed sequences.
14. *A character of a font nothing resolves is noticed when it is measured.*
    The reference's measure raises `TextFaceUnavailableError` from inside
    `hpack` or `ship`. Here `MathFonts::measure` answers nothing and sets a
    flag (`unmeasured`), which `mlist_to_hlist` turns into
    `TEXT_FACE_UNAVAILABLE`; a caller that packs or ships the result with the
    same measure asks the flag itself.

## mathtext

`src/mathtext.*`, `src/mathtext_tables.inc` (generated from the reference's
tables by `tools/gen_mathtext_tables.py`). The goldens hold `parse` to the
reference on 3,765 sources (`tests/golden/math/mathtext.golden`), list for
list and refusal for refusal, reason strings included, and `typeset` on the
1,971 sourced cases of `formulas.golden`.

**Reference versus TeX** — the port does what the reference does. The
reference's own docstring lists these; they are restated in `src/mathtext.h`:
a `\frac` is an Inner atom holding the fraction where LaTeX's is an Ord; `\ `
and `~` are six math units of glue where §1041 to §1043 append the text font's
interword glue, and they shrink in a script where TeX's do not; `\quad` is
eighteen math units; `\binom` fits its parentheses to the content
(`make_left_right`, §762) where `\atopwithdelims` takes them at a size the
font states (§748); `\dots` is `\ldots`. The one rule transcribed from the
program is §1186's: a group holding a single bare Ord atom is that atom.

**Port versus reference:**

1. *The source is bytes.* It is decoded once as UTF-8; a malformed sequence
   is U+FFFD, which is a printable character outside the subset (`the
   character '�' (U+FFFD) is not in the subset`), a word in `\text{}`, or an
   unknown control symbol after a backslash. A Python string can hold a lone
   surrogate and cannot hold a malformed sequence; neither has a counterpart.
2. *A refusal has two positions*: the reference's, in codepoints, and the same
   place in bytes. The goldens carry both.
3. *`typeset` takes the fonts.* The reference builds a `MathFonts` when given
   none and turns a failure to build one into a `FONT` refusal; here
   `MathFonts::init` is the caller's, with its own reason.
4. *When a text resolver exists and does not know the face*, the reference's
   reason is `there is no prose face to set its words in: <face> did not
   resolve: <what its resolver raised>`. A resolver here answers null and has
   no words, so the reason stops at `did not resolve`. With no resolver at
   all the reason is the reference's to the character, Python's `repr` of the
   words included.
5. *The catch-all, and what is too large.* The reference's last `except` gives `the math face could
   not set it: <ExceptionType>: <message>`, kind `FONT`. Here the same prefix is followed by
   `math_error_message`, with no type name, for `NOT_IMPLEMENTED` and `MALFORMED`, which the parser
   emits no list for. `TOO_DEEP` and `TOO_LARGE` are this port's own bounds and are not the font's
   doing: kind `LAYOUT`, "the formula is too large to set: ...". The parser cannot reach `TOO_DEEP`
   — the deepest list it lets through (24 `\frac`s, each the argument of the one before, with an
   operator name innermost) nests 50 levels against a bound of 64, and a test sets it — but a
   source it reads can reach `TOO_LARGE`: a row of `\qquad` wider than `MAX_DIMEN` is a box no
   node may hold, and is refused where the reference sets it.
6. *Not ported*: `markdown_math` (the memo of built formulas, and the callable
   markdown is handed) and `Formula.source`; whoever owns the document keeps
   both. `supported_commands()`, `refused_commands()` and `sized_delimiters()`
   are count-and-index accessors over the generated tables.
7. *A source has a longest length.* `parse` refuses a source of more than 524,288 bytes
   (`MAX_SOURCE_BYTES`) before decoding it: kind `LAYOUT`, position 0. The reference reads any
   length; its own hostile-source table (up to 450,000 bytes) is inside the bound and is answered
   as it answers.
8. *The proportion in a layout refusal does not follow the C locale.* Python's `:.2f` never does;
   `printf` does. The digits are the same as the reference's for every quotient tried (about 314,000,
   every exact half of a hundredth up to 20 among them).
9. *A NUL in a reason.* `Refusal::reason` carries it, as the reference's string does. The public
   structs spell it `\x00`.

## public-api

Where `include/imtypeset.h` sets text differently from the Python reference
**on purpose**. Both are rulings recorded in `docs/port-hazards.md`; `para`
and `style` underneath still reproduce the reference, and the goldens hold
them to it. `tests/test_api.cpp` replays every `para` golden the public API
can express (225 of 243 cases, 667 compositions) and counts the rest by
reason; the two below are 9 of the 18 it cannot.

1. **The first-line indent is applied once** (tex.web §1091: one empty box as
   wide as the indent at the head of the paragraph). The reference puts that
   box in the material *and* narrows and shifts the first line by the same
   amount in its `_shape`, so its first line starts `left_indent + 2 *
   first_line_indent` from the column's edge. The public API composes with
   `FirstLineIndent::BOX_ONLY`: `ImTypesetStyle::FirstLineIndent = 30` moves
   the first line 30 px. Test: `the_first_line_indent_is_applied_once`. The
   six goldens with a first-line indent are therefore not replayed through
   the public API.
2. **White space collapses across run boundaries.** The reference's
   `_append_text` makes one glue per run of white space *within one call*, so
   two styled runs that meet inside a run of spaces set two spaces, a run
   that begins with a space after a hard break keeps it, and one that ends
   the paragraph with a space leaves a trailing glue. The public API takes
   one string with style ranges over it and collapses first: a piece that
   follows white space, or starts a line, loses its own leading white space
   before `append_text` sees it; white space at either end of the text (and
   of each line under `ImTypesetTextFlags_HardBreaks`) is dropped. The space
   that survives is set in the face of the run it *began* in. Test:
   `white_space_collapses_across_run_boundaries`. Three goldens whose raw
   runs depend on the reference's behaviour are not replayed.

Also differing, and not a ruling:

3. **A leading is `round(em * ratio)` with the ratio read as the decimal the
   host wrote.** `ImTypesetStyle::LineHeight` is a `float`; `1.35f` widened
   is 1.35000002384 and `round(size * that)` differs from the reference's
   `round(size * 1.35)` for about one size in fifty million. The float is
   converted through its shortest decimal (`%.7g`), which gives the double
   the reference used. Same for `ImTypesetRun::Scale`.
4. **Sizes are pixels.** The reference's `Theme.build` takes an em in pixels;
   `ImTypesetStyle::FontSize` is ImGui's pixel height (ascent + descent of the
   Regular face) because that is what an ImGui host holds, converted as the
   reference's `Theme.at_pixel_height` does. `EmSize` is the reference's.
5. **Not expressible, so not compared**: list markers (7 goldens) and a theme
   whose stand-in faces are not prose/math/code (2 goldens). `para` holds
   both against the reference directly.

## api-math

Formulas through the public API (`ImTypeset::EnableMath`,
`ImTypesetTextFlags_Math`; `build_material`, `scan_math` and `display_block`
in `src/imtypeset.cpp`). Where a `$` opens a formula is, in the reference, a
markdown-it inline rule among markdown-it's other rules
(`advtext/markdown.py`, `_math_inline`, `_math_block`); D1 does not port
markdown, so the rule is restated natively over plain text. On every source
of `tests/golden/math/inline.golden` (58 paragraphs, 13 `$$` blocks) the
segmentation, the material, the breaks, the line boxes and the shipped
commands are the reference's. What follows is what the golden does not hold.

1. **`\$` is an escaped dollar, and only that escape exists.** In the
   reference markdown-it's escape rule runs before the math rule: a backslash
   and the character after it are one piece of text, and before ASCII
   punctuation the backslash is removed. The port keeps the first half for
   every character (so `\\$x$` is a backslash pair and then a formula, and
   `\$x$` opens nothing) and the second half for the dollar alone: the
   backslash before an escaped dollar is not set and not copied, and every
   other backslash is set as written. So `\\` shows two backslashes here and
   one in the reference, `\*` shows both characters here and an asterisk
   there. Reason: the host needs a way to write a dollar that would otherwise
   open a formula, and nothing else about markdown's escapes is this
   library's to decide. The piece of text around a dropped backslash is still
   set as one piece, so the dollar kerns with the character before it, as in
   the reference. Checked against the reference by hand for 19 sources (the
   formulas found are the same; only the backslashes shown differ); test
   `the_scanner_decides_what_markdown_decided_for_the_reference`,
   `an_escaped_dollar_is_a_dollar_and_its_backslash_is_not_set`.
2. **Code spans, emphasis and links do not exist.** In the reference a dollar
   inside a code span that opens before it is never looked at, and emphasis
   markers around a formula are consumed. Here backquotes and asterisks are
   characters: `` `$x$` `` sets two backquotes around a formula. An
   `ImTypesetRun` does not hide a dollar either, whatever its face or flags;
   a formula takes the size and colour of the run it starts in and a run
   boundary inside it is ignored. A host that parsed markup itself escapes
   the dollars it means literally or leaves the flag off for that text.
3. **A display block is a whole text.** The reference's block rule cuts a
   document into blocks; the API is handed one block. A text is a display
   block when it is nothing but one: a line `$$`, the source, a line `$$`; or
   one line `$$ ... $$`. Followed by anything else it is a paragraph and the
   inline rule reads it (in the reference: a block, then a paragraph). An
   unclosed block runs to the end of the text. Styled runs over a display
   block are ignored.
4. **An unclosed display block is reported.** The reference records it on the
   block's `FormulaSpan` (`unclosed`, with the reason "the display block has
   not been closed yet") and has no error channel. The API reports
   `ImTypesetError_MathRefused` with that sentence at the opening `$$`, and
   `ImTypesetFormula::Refused` is true for it.
5. **`more_may_follow` is not ported.** It is a stream's question (is the
   closing dollar the last character of a text still arriving); the API has
   no stream, and the end of the text is the end.
6. **A refusal's position is a place in the text**, in bytes and in
   codepoints, where the reference's is an offset in codepoints into the
   source between the dollars. For a display block the typesetter is handed
   the source with white space collapsed (as in the reference) and the
   position is mapped back to the text as given.
7. **With no font for the code face** a refused formula's source is set in
   the face of the text around it; the reference always has a code face.
   **With no font for the math face** every formula is refused with "the
   math face could not set it: MissingFontError: math has no bundled file
   and none is registered", the reference's sentence for a role with no
   file; the reference's bundle always has one, so that path is the port's.
8. **The reference's memo of built formulas (`markdown_math`'s `remember`)
   is not ported.** A paragraph keeps its material, so a formula is set once
   per text, size and font registry; the three math faces are kept per size
   (64 sizes, then all forgotten at once).
9. **Two switches where the reference has one.** The reference turns math on
   per document (`math=builder`). Here the context is switched on once
   (`EnableMath`) and each text says whether its dollars are math
   (`ImTypesetTextFlags_Math`): a host's labels and its prose go through the
   same context.

## stress

Where the hardening task made the port answer differently from the reference
on purpose. None of these changes an answer for input the goldens hold: each
is about input the reference never finishes on, or raises for.

1. **Total fit has a work budget; the reference has none.** The optimiser
   counts the candidate lines it judges, all passes together, and at
   `DEFAULT_LINE_BREAK_WORK_BUDGET` (2^20; `src/linebreak.h`) it stops and
   the paragraph is broken by first fit: `Paragraph::budget_exhausted` is
   set, `pass_used` is `FIRST_FIT`, `total_demerits` is 0. The reference
   (and tex.web, which has `easy_line` and delta nodes instead) carries on:
   29.7 s for 64 copies of the sample's last paragraph set ragged right,
   here. Below the budget the breaking is unchanged, which
   `tests/test_stress_layout.cpp` holds by breaking each paragraph with the
   default budget and with the largest. The public API reports the fallback
   as `GetParagraphError()->Message` with `Code` None, and does not try the
   optimiser again on the same text at another width.
2. **A box cannot contain itself, and a walk does not follow one that
   does.** The reference's nodes are immutable tuples and cannot form a
   cycle. An arena's can be made to, around its interface (a `Mark` made by
   hand); `ship` answers `TOO_DEEP` past `NodeArena::MAX_NESTING` levels,
   `import` copies such a level as an empty box, `deep_equal` answers false.
3. **`ship` gives up in proportion to the tree it was handed.** The
   reference walks every path of a tree whose boxes share contents, however
   many there are. The default `max_items` is 16 visits per node of the
   arena and the list plus 65,536; a caller whose tree shares more than that
   says so. Not one visit per node, as the review suggested: two existing
   tests set the same box three times in a list, legitimately.
4. **`import` keeps sharing; the reference has nothing to import.** A box
   named twice in the source is copied once and named twice in the copy.
5. **A node naming nothing in the arena is a status.** The reference has no
   indices. `hpack`/`vpack` report `NODE_NOT_IN_ARENA` and count the node as
   nothing; `ship` fails with the same; the line breaker puts an empty rule
   where the node was, so that positions keep their meaning, and reports the
   paragraph as malformed.
6. **Sums that leave 64 bits stop there and are reported.** The reference's
   integers do not overflow. `hpack`, `vpack` and `ship` report
   `DIMENSION_OVERFLOW`; `interline_glue`, `ParShape::hanging`, `atom_box`
   and the breaker's demerit totals saturate without a report, having
   nowhere to make one. Within `MAX_DIMEN` no sum can reach the bound.
7. **A sentence space is held to `MAX_DIMEN`.** `interword_glue` clamps the
   space width it is given and the three components it returns; the
   reference returns a width a third, and a stretch up to thirty-two times,
   larger than the space. The values differ only for a space wider than
   about 12,288 pt.
8. **A font the reference would load can be refused for drawing.** The
   reference hands any font fontTools reads to FreeType. Here
   `add_face_to_atlas` first runs a strict structural pass
   (`OpenTypeFont::strict_check`) and refuses a face that fails it, with the
   reason; the face still registers and measures. A table directory that
   names a tag twice, which fontTools reads by taking one of the two, is
   refused outright.
9. **Stand-ins go to minted codepoints first.** With a 16-bit `ImWchar`, the
   reference has no stand-ins at all (its `ImWchar` is 32 bits). The port's
   were assigned in ascending codepoint order; they are now given to the
   minted codepoints first and then to the font's own supplementary ones,
   and a shortfall is counted (`FaceData::atlas_stand_ins_unserved`).
10. **Kern lookups are read once each, within one budget.** The reference
   reads every subtable a lookup lists, however many list the same one. A
   font whose GPOS repeats or overlaps subtables past the budget loses the
   pairs past it, with a warning; no bundled or installed font reaches it.
11. **The atlas's copy of OS/2 has USE_TYPO_METRICS cleared**, so that
   FreeType sizes the font as layout does (by hhea), where the reference lets
   FreeType size it by the typo metrics and lays it out by hhea all the same.

## font-wild

Fonts. What `src/opentype` and `src/fontlib` do that the reference
(`advtext/fontlib.py` over fontTools 4.66.0) does not, since task `font-wild`
made the reader read the fonts hosts bring (D25, D35). `docs/font-support.md`
has the census; the corpus test compares everything else with the reference
exactly.

Beyond the reference, on purpose:

1. **The legacy `kern` table is read.** The reference reads GPOS only, so a
   font with only a `kern` table (21 of 2,991 installed faces) has no kerning
   there. Here a version 0 table's horizontal format 0 subtables are the
   kerning of a face whose GPOS has no PairPos subtable under a `kern`
   feature; a face with any GPOS pair kerning is kerned by GPOS alone, as
   before. Subtables add up, an override subtable replaces, a pair listed
   twice in a subtable keeps its last value. It is an option that is on
   unless turned off (`FontLibrary::set_legacy_kern`); off gives the
   reference's answers, and the differential corpus test runs with it off.
   Checked against fontTools' reading of the same tables
   (`tools/oracle/dump_kern_legacy.py`, `tests/golden/font-wild-kern`): 81
   tables equal pair for pair.
2. **A missing codepoint is glyph 0, whatever glyph 0 is called.** The
   reference keys everything by glyph name and takes the missing glyph to be
   the one *named* `.notdef`; when glyph 0 has another name its answer for a
   codepoint the font lacks is zero width and no ink. Two installed faces
   name glyph 0 `nonmarkingreturn` (teluguvijayam's PottiSreeramulu.ttf is
   one). Here the missing glyph is glyph 0, which is what OpenType says it is
   and what a rasteriser draws. This is the review's item W12, confirmed on
   those two faces by reading the names with fontTools; no golden compares a
   missing codepoint's advance on a corpus face.
3. **More of `cmap` is read.** Format 10 (fontTools has no reader for it and
   answers an empty map); the next subtable in preference order when the
   preferred one is in format 2, 8 or 14 (the reference takes the preferred
   one whatever it is); a font with only a Windows Symbol subtable, codes as
   they stand, or only a Macintosh Roman one, codes taken to Unicode (the
   reference refuses a font with no Unicode subtable). Each is warned about.
4. **Fonts fontTools raises on can load for metrics**: `numberOfHMetrics` of
   zero (every advance zero), a `loca` table without its last entry, a format
   4 `cmap` segment that points outside its glyph array (the segment's codes
   are unmapped), a truncated format 12 subtable whose stated groups fit. Each
   is warned about, and the first two are refused by the strict pass before a
   rasteriser sees them.
5. **Collections are read by face index.** The reference passes fontTools no
   face index and so cannot open a `.ttc` at all; the goldens for the 80
   collection faces were made by supplying the index to the same reference
   code (`tools/oracle/FORMAT-font-wild.md`, "Collections").
6. **A glyph with a component placed by point matching** has the box of its
   other components (the reference has none: fontTools raises); unchanged by
   this task, listed because the review asked (W11).

The reference's own departures from OpenType, **kept** for now and listed for
a decision under D32.3 rather than changed silently:

7. **The first value across `kern` lookups, not their sum.** OpenType applies
   every lookup of a feature and the adjustments add up. Measured on the
   corpus by `OpenTypeFont::pair_kern_accumulated` (`KERN-ACCUMULATION` in
   `tests/test_opentype_wild.cpp`): 590 faces have more than one `kern`
   lookup; of 149,304 pairs sampled on them 1,426, on 84 faces, get another
   value when the lookups are added, the largest by 369/1000 em.
8. **Script and language are ignored** (every `kern` feature record counts),
   **lookup flags are ignored** (1,405 faces set one, `IgnoreMarks` mostly),
   and **a pair's second value record is ignored** (12 faces kern only
   through it and answer 0).
9. **TrueType math fonts without glyph names.** With a format 3 `post` table
   fontTools makes names up from the cmap and the reference sorts by those;
   the port mints in glyph-id order, which is the same order unless a glyph a
   variant chain reaches is mapped by a secondary cmap subtable and not by
   the picked one. No installed font is such a font.

Beyond the reference, on purpose, added by task `font-wild-finish` (numbered on
from the list above):

10. **A cmap format 12 group whose glyph ids pass 2^32 - 1 is cut** at the last
   codepoint whose id fits. fontTools computes the ids as unbounded integers
   and then fails to name the glyphs. No installed font has such a group.
11. **A Type 2 charstring that computes a coordinate which is infinite or not
   a number has no ink box.** fontTools works in unbounded integers where the
   operands are integers and has a box. No installed font uses the arithmetic
   operators at all.
12. **A legacy `kern` value is held to ±`MAX_DIMEN` as a length**
   (`FontMetrics::kern`). The reference never reads the table.
13. **The font the atlas is given is not the file.** Beyond the cmap
   (D27) and the OS/2 `USE_TYPO_METRICS` bit, a glyf header whose box ends at or
   below the baseline over an outline that rises above it has its yMax raised
   (3 installed faces, 4 glyphs). Nothing layout measures reads that field.

## api-revise

Where the revised `include/imtypeset.h` (rulings R1 to R14,
`.concerns/api-revise--lead--rulings.md`; decisions in D36) answers
differently from the Python reference **on purpose**. None of it changes a
shipped command: `tests/test_api.cpp` still replays 225 of the 243 `para`
goldens and `tests/test_api_math.cpp` all 71 formula cases, exactly.

1. **A position is a byte of the source text, not an index into what was
   shipped.** `advtext/selection.py` answers boundaries, which are positions
   in one layout's reading order, and the reference's views keep a selection
   in them. The public API converts at its edge (`selection_of`,
   `text_range_of` in `src/imtypeset.cpp`): a range covers the items whose
   source byte is in it, so it is the same words after a reflow. What that
   costs in fidelity: a space a line was broken at has no item, so a position
   after the last word of a wrapped line is the position before the first
   word of the next (the reference has the same boundary, numbered twice);
   and a formula is covered by a range only if its opening dollar is.
   `ImTypesetHit::Line` says which side of a break a point was on.
2. **There are lines and carets.** `selection.py` has "no caret rectangle";
   `GetParagraphLine` and `GetParagraphCaretRect` are computed from the same
   commands the reference ships (a LINE command's box and baseline, the x of
   the item at a boundary). A line's content ends at the left edge of the
   glue that closes it, which is the reference's right skip (and its
   par-fill skip on the last line).
3. **Ragged-right text is as wide as its longest line.** The reference's
   blocks are as wide as their column whatever the alignment. The shipped
   commands are the same; only the size the paragraph reports (and so the
   ImGui item) is narrower. Centred, flush-right and justified text keep the
   column's width, because their lines are placed across it.
4. **One switch for formulas, on the text.** The reference turns math on for
   a document (`markdown_math()` handed to `build_document`); `api-math` had
   two switches. `ImTypesetTextFlags_Math` alone decides, per text, and
   `EnableMath()` is gone.
5. **A widget out of view is not laid out again when only the width
   changes.** The reference composes every block at every width. Here a
   clipped `TextUnformatted` keeps its lines and its height until it is in
   view (`ImTypesetStats::DeferredLayouts`); the scroll extent is an estimate
   until then. Paragraph handles (`LayoutParagraph`) are never deferred.
6. **Colours a host gives are multiplied by ImGui's `style.Alpha`**, as the
   default ink always was. The reference's palette is drawn as given.
7. **Zero is the default of every field a host fills.** `LineHeight` 0 is the
   reference's 1.35, `PixelsPerInch` 0 its 96, `ImTypesetRun::Scale` 0 is 1,
   and face 0 is "the default" (Regular in a style, the paragraph's in a
   run), so the registry's roles are public faces 1 to 7.
8. **Text() finds its layout by a hash of the text**, compares the bytes, and
   evicts under a limit. The reference has no immediate-mode entry; nothing
   to compare.
9. **A formula's refusal reason and an error's message never hold a NUL**: each is written `\x00`
   (the reference's reason holds the character itself).
10. **The widget cache does not go over its whole list in a frame in which nothing can be dropped**
   (no counterpart in the reference, which has no such cache).

Not changed, and asked about: the pen rule that rounds letter advances
inside a word to whole pixels (`.concerns/example--lead--word-gap-observation.md`).
It is the reference's measured device rule and lives in `src/draw.cpp`, which
this task does not own; no alternative was added.

## blocks

`src/blocks.*`: the reference's block stacking (`_block_boxes`, `_rule_box`,
`_runs`, `_stacked`, `_barred`, `compose_blocks` of `advtext/ui/__init__.py`;
`_code_lines` and `_newlines` of `advtext/markdown.py`). With a default
`DocumentRules` the port is the reference: `tests/test_blocks.cpp` replays the
69 cases of `tests/golden/blocks` (260 blocks, 215 widths) stage by stage,
exactly, three ways (the pieces one by one, `compose_blocks`, and `Document`).

### What the prototype does and the product does not (D32.3)

The reference's block layer is a prototype's, and the oracle task listed
fifteen shortcuts in it (`.concerns/oracle-blocks--builder-22--result.md`).
Each is a switch of `DocumentRules` whose default is the reference;
`DocumentRules::product()` turns them all on. No golden exists for the product
side: each has a test of its own in `tests/test_blocks.cpp`, named below, and
`a_document_laid_out_block_by_block_is_the_document_composed_at_once` holds the
two ways of composing together under random combinations of the switches.

| # | the prototype | the product | why | switch · test |
|---|---|---|---|---|
| 1 | `compose_blocks` adds a gap only when the list holds something; `layout_units` adds it always. The two disagree when the first block sets no box. | Follows `compose_blocks`, which is what the goldens hold: nothing above the first box. With item 2 on, an empty block is not in the list at all, so the case in which the two differed is gone. | One layout path cannot disagree with itself; `Document` and `compose_blocks` are held together by test. | none · `an_empty_block_contributes_no_gap` |
| 2 | A block that sets no box (an empty code block) still contributes its space above and its space below once the list holds something: two kerns back to back around nothing. | Such a block takes no part in the gaps: its neighbours are spaced as if it were absent. | Space around nothing is space the author did not ask for. | `skip_empty_blocks` · `an_empty_block_contributes_no_gap` |
| 3 | Only a text block has a quote depth; a code block or a rule inside a quote reads as unquoted and cuts the bar in two. | Any block can be in a quote and the bar runs past it. | The bar says "this is quoted", and the code is. | `quote_any_block` · `code_and_rules_inside_a_quote_keep_its_bar_unbroken` |
| 4 | A bar is one quote indent left of the least indent in its run, whatever its nesting: text quoted twice with nothing quoted once draws both bars at one x. | A bar stands where its quote opens: `Quote::bar_x` when the block names its quote, otherwise the least indent less one quote indent per level between the bar and the block. | Two bars at one x are one bar. | `bar_by_depth` · `a_bar_stands_where_its_own_level_of_quoting_is` |
| 5 | Consecutive quoted blocks are one run with one bar, even from two separate quotes. | Blocks that name different quotes are different runs: two bars, and the blocks' gap between them. | Two quotes are two quotes. | `separate_quotes` · `two_quotes_in_a_row_have_a_bar_each` |
| 6 | A rule's width is the column less its indent, unfloored: negative in a column narrower than the indent, wider than the column for a negative indent. | The rule is at least zero wide and its box is exactly the column. | A negative rule draws nothing by accident, not by rule. | `floor_rule_width` · `a_rule_is_never_negative_and_its_box_is_the_column` |
| 7 | A code line is set at its natural width and the document box grows to the longest line; nothing reports the overflow. | Per document: grow (the reference), clip (the box is the measure, `BlockLayout::clipped` tells a draw to clip), or wrap at the measure, after the last space that fits where there is one past the indentation, with continuation lines hung by `code_continuation_indent` and flagged in `BlockLayout::continues`. Clipped or wrapped, the document box is never wider than a column of at least a point. | A document wider than its column cannot be laid out in a window. | `code_overflow`, `code_continuation_indent` · `a_code_line_longer_than_the_measure_grows_clips_or_wraps` |
| 8 | A line is `max(width - indent, 1 pt)` wide: deep nesting in a narrow column sets every word on its own overfull one-point line. | `min_measure` is the least a line is given: an indent is cut back to the column less that, and to nothing in a column narrower than that. (A bar that names its place is cut back the same way. A marker wider than what is left of its indent overfulls its box, as in the reference.) | The floor is a number a host chose, not the accident of a `max`. | `min_measure` · `deep_nesting_in_a_narrow_column_keeps_a_stated_measure` |
| 9 | A paragraph of a list item inside a quote is styled as a quote, so a quoted list's items are spaced as quoted paragraphs. | A block says which list and which quote it is in, both, and its spaces are its own; the list's spacing (item 10) holds inside a quote as outside. | The role lookup lost the fact that the block was a list item. | `list_spacing_in_quotes` (with 10) · `a_list_is_spaced_by_what_it_says_in_a_quote_or_out_of_one` |
| 10 | Every gap is the sum of two block spaces, so tight and loose lists are identical and a second paragraph of an item is spaced like a new item. | `List::tight`: nothing between the items of a tight list (they follow as lines do, the strut making that exact), nor between an item and a list nested in it; the blocks' own spaces between the items of a loose one, between two blocks of one item (`Block::item`), and at a list's edges. The innermost list both blocks are in decides. | Tight and loose are the two things a list can say about its spacing. | `structured_list_spacing` · same test |
| 11 | Only a text block carries a marker: an item holding only code is unmarked code, and an empty item vanishes. | A code block's marker is hung on its first line, and a code block with a marker and no lines is one empty line on its strut. An empty text block with a marker is already a line in the reference. A rule's marker is not set under either. | The marker is the item's, not its first paragraph's. | `marker_on_any_block` · `an_item_holding_only_code_or_nothing_keeps_its_marker` |
| 12 | The gutter is the list's fixed indent; a wider marker overfulls its box and overprints the text. | A list whose widest marker plus gap exceeds `List::indent` moves its blocks, and whatever is nested in it, right by the excess. The excess is content's, not the width's: `Document::prepare` computes it without laying anything out. | "100." must not overprint. | `widen_marker_gutter` · `the_marker_gutter_widens_to_the_widest_marker_of_its_list` |
| 13 | A rule is handed the enclosing indent and nothing else: no bar (item 3), and nothing that happens to its container's indent happens to it. | A rule is a block like the others: it moves with its list's widened gutter, gives way to the least measure, and (item 3) stands inside its quote's bar. | A rule inside an item belongs under the item's text. | `rule_in_container` · `a_rule_in_a_list_is_indented_with_its_list` |
| 14 | Which block of a quote an item belongs to is found by bisecting baselines against block bottoms (`ui/units.py` `_spans`), wrong for a first line of no height. | Carried as data, always: each block is shipped alone with indices of its own, and `Document::flatten` reports each block's command and index range. | Nothing has to be found that was known. | none (no reference mode: on the goldens the two agree, all 799 `span` records) · `which_block_an_item_belongs_to_is_where_it_is_stored` |
| 15 | Every bullet is "•" and every ordered marker "N." (the markdown front end). | A marker is whatever text and font the block says (`set_marker`); this layer makes none. | Markers are the host's vocabulary. | none (data) · `a_marker_is_whatever_text_the_block_says` |

### Where the port differs from the reference whatever the rules

1. **One block, one index space.** The reference ships a unit (a depth-0 run)
   and numbers a quote's blocks in one sequence. `Document` ships each block
   alone, from its own top-left corner with indices from zero, and places it by
   translation; `flatten` renumbers into the reference's sequence, with each
   quote's `LINE` and bar `RULE_RECT` put back where ship-out has them, and
   the result equals the reference's `cmds` on every golden.
2. **Laziness.** The reference's `LayoutCache` memoises a block's boxes but
   composes every block of a document at every width. `Document` lays out the
   units a window shows; heights elsewhere are the last known or an estimate
   (three eighths of the leading per character of text), and
   `Document::settled()` says when the height is the document's.
3. **What a block says is clamped, not trusted.** Lengths to `MAX_DIMEN`, a
   quote's depth to 64 (`MAX_QUOTE_DEPTH`), a list's to 255, a container id
   that names nothing to no container, a unit's height to 2^38 sp. The
   reference raises or recurses.
4. **`_code_lines`' memo of chosen nodes is not kept**: each character asks
   `has` again. Same nodes; the recorded-metrics replay asks nothing the
   reference did not.

### Departures from `tex.web`

- **The space between blocks is not `\parskip`.** tex.web section 1091
  (`new_graf`) appends `par_skip` glue (section 224) before a paragraph, and
  the paragraph's first line still gets interline glue against the box above
  (section 679), so two paragraphs are `\parskip` plus the baselineskip's
  remainder apart, and the glue can stretch. The reference, and so the port,
  puts one **kern** of `space_below + space_above` between blocks (section
  1061 is the kern; nothing in tex.web sums two blocks' spaces) and resets
  `prev_depth` to `ignore_depth` (section 212) after it, as section 1056 does
  after a rule, so there is no interline glue between blocks at all. This is
  the reference's rule, recorded as deliberate in its `_stacked` and
  `_rule_box` docstrings; it stays.
- **A thematic rule is a box, not a rule node** in the vertical list: an hbox
  of a kern and a rule. Section 1056's suppression of interline glue either
  side of a bare rule is thereby avoided; the kern has done it.
- **No page builder** (part 45, sections 980 to 1028): no page breaks,
  `\topskip`, `\vsize`, insertions or marks; the document box is `vpack` at
  natural height (section 668).
- **A quote's bar** is a rule beside a vbox in an hbox with a negative kern
  (section 649 packs it); tex.web has no such construction of its own.
- **A wrapped code line's continuation indent** is hanging indentation in the
  sense of section 849 (`\hangafter 1`), applied by this layer to boxes it
  cuts itself: a code line has no glue and is not given to `line_break`.

## tex-fixes

What the product's rules were checked against, and what they were not.

1. **No TeX engine.** None is installed here. Each switch has a case worked by
   hand from the cited section in `tests/test_tex_rules.cpp`; the comment above
   the test is the working. Agreement with TeX is claimed for those cases.
2. **§854 is taken for a forced break** (§851's `(b > inf_bad) or (pi =
   eject_penalty)`), so on the final pass the last line of a paragraph costs
   nothing when it is forced from the only active node left and nothing
   feasible was recorded at that break. The port has always done this under
   both presets; it is `tex.web`'s. It surprised the first hand-working of B7
   (2,600 demerits, not 7,700).
3. **Under `sublist_nucleus_packed` a `\frac` is a box in a box**: the parser
   makes it an Inner atom whose nucleus is a list holding the fraction, and
   §754 packs that list. `{a \over b}` in TeX is the same shape.
4. **`easy_line` with `line_penalty 0` and a `fil` right skip** sets one word
   to a line: every breaking ties at zero demerits and §855's `<=` lets the
   latest active node win. The reference keeps line counts apart and takes the
   fewest lines. Not reachable through the public API.
5. **The reference's rule for which chain gets the last resort** (the one with
   the most lines) is in the goldens at narrow measures: 17 para and 14 blocks
   compositions, all at columns of 40 px and 60 px. The public-API replays of those
   goldens run under `TexRules::reference()` for that reason.

## api-document

`include/imtypeset.h`, "Documents", over `src/blocks.*` (`src/imtypeset_document.*`). Under the
test-only `DocumentHook` (reference `DocumentRules`, reference `TexRules`, exact scaled points) the
layout is the reference's: `tests/test_api_document.cpp` replays 67 of the 69 block goldens through
`SetDocumentBlocks` (211 compositions, 34,647 commands), and `tests/test_api_document_golden.cpp`
the 22 layouts of `tests/golden/doc` (selection 10, hosts 8, decorations 4), all equal. What follows
is where the PUBLIC behaviour is the product's and not the prototype's. None has a reference
switch: these are answers to questions the prototype answers in material indices, and the product
answers in (block, byte). Counts are from `tests/golden/doc`.

| # | The prototype | The product | Why | Where it shows |
|---|---|---|---|---|
| 1 | A list marker has indices, so it can be selected, has rectangles and is copied (with an indent per list level). | A marker is not part of its block's text: never selected, no rectangle, never copied. | A position is a byte of the text the host handed in, and the marker is not in it. A host that wants `1. ` in a copy has the marker and writes it. | 170 copies, 319 rectangle groups, 42 `blocktext` rows |
| 2 | A point between two blocks is answered from the unit's lines by a rule this task did not characterise (the replay only counts where the answers part). | It belongs to the block below, and `InBlock` is false. | One rule a host can state, found without laying out the block above. | 91 probes, 684 drags with an end between blocks (9 agree anyway) |
| 3 | Blocks of a copy are joined by rules read off the styles: blank line, newline between code lines, marker and indent in a list. | One separator between every two touched blocks, a blank line unless the caller names another. | R5 of `docs/advtext-migration.md`: stated and replaceable. | 3 joins in a list |
| 4 | A code block's text is tab-expanded before anything keeps it; a tab copies as spaces. | Positions and copies are bytes of the raw source: a tab is one byte and copies as a tab; the spaces it is set as are selected together. | The lead's ruling on coordinates. | 5 copies, 1 `blocktext` |
| 5 | A block's rectangle is the full measure. | `Min`/`Max` start at the block's indent and end at its longest line; `FullMin`/`FullMax` are the measure from the indent. | Both are wanted: a background and a hit area. | 57 `bspan` left edges |
| 6 | A hosted widget is a code block with no lines and ships no command; at a document's edge it is copied only under an `everything` flag the view keeps. | A host block is a rule that is laid out and not drawn (so the block layer's flattened list holds one RULE_RECT for it), and its text is copied whenever a range covers any of it. | A reserved rectangle needs a height and nothing else; a copy should not depend on view state. | 29 commands in 8 layouts; 8 select-alls |
| 7 | Find is in the library and silently case-sensitive when folding changes a length. | No find: the host has the text and gives ranges to draw. | Not built. Recorded as a gap in D43, not as a decision against it. | `decorations.golden` searches not replayed |
| 8 | A link's underline borrows the table rule's thickness. | `ImTypesetMark::Thickness` and `Offset`, with defaults from the block's em. | The prototype's own shortcut list. | 10 underlines compared on left and right only |
| 9 | Spaces above and below a block can be negative (the `edges` goldens have two). | Lengths a host gives are clamped at zero. | The one validating entry (D30): no host length goes in negative. | 2 block cases not expressible |

The stream goldens are replayed as a property of the product, not differentially: the golden has no
`blocks` group for a stream, so the reference's layouts and digests cannot be rebuilt. 1,583 steps
as edits (1,172 `AppendDocumentText`, 293 `ReplaceDocumentBlocks`): every block before the edit
keeps its commands and its top, and no step lays out more blocks than its edit names.
