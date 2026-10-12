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
  (`docs/decisions.md` D32.3), except where an entry says **switch**: B3, the
  class per line count, is deliberate in the prototype and a switch all the
  same (D41.2).
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
| B3a | §854 l.16824-16829 | the last resort is taken from the one active node left | the same test, but with B3 several nodes for one breakpoint can be left and the last in order — the highest line number — gets it, whatever its demerits | deliberate for ragged text only (`advtext/linebreak.py:74-77`), undeclared for justified; **switch** `easy_line`. Hand-worked case: `easy_line_the_last_resort_is_forced_from_the_one_active_node_left` (reference 2, 2, 2, 1 at 1,452 demerits; `tex.web` 3, 3, 1 at 200) |
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
  quads as the draw list, the debug overlay's line boxes and baselines
  included since they became rectangles of the port's own (the bullet after
  the next).
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
- **Under `DrawRules::product()`, a formula is placed whole.** The reference places a formula's
  glyphs as a word's (each from the glyph shipped before it, gap rounded once) and snaps each rule
  alone. The product places a formula whole (`DrawRules::formula_placed_whole`, `docs/decisions.md`
  D47). Goldens replay under `reference()`. (The first product rule here, `rule_joins_span`, joined
  each end of a rule to the glyph nearest along the line; it is gone.)
- **Under `DrawRules::product()`, a word is held to its laid-out width and a word after a space
  is placed from the word before it** (`word_width_held`, `word_spaces_led`, `src/draw.h`). The
  reference rounds every gap of a span and anchors every span on the pixel nearest its own
  place, so what a word gathers comes out of the space after it: measured, spaces drawn at less
  than no width. The product gives back whole pixels through the gaps that were nearest to
  rounding the other way, and anchors a word a rounded space after the last, within a pixel of
  its place. `reference()` is unchanged and is what every golden replays under;
  `tests/test_word_gaps.cpp` measures both. Since task `draw-seams`, `word_spaces_led` also places
  the left edge of a formula's box from the text before it and the text glued to a formula's end
  from the formula's rightmost part (`docs/decisions.md` D50); one joint, text straight after a
  formula, is drawn less evenly than the reference draws it, and D50 says by how much.
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
| 16 | The space between two blocks is a kern with nothing after it that a column could end at (tex.web §973 breaks at a kern only before glue): a column never ends between two blocks. Read only when a document is cut into columns. | In the list columns are cut from the space is glue, a legal break, between any two blocks; after a block that says `keep_with_next` (a heading) it stays the kern. | A run of one-line blocks could not be cut at all and ran off the foot of its column; added by `columns-core` (D58), after the fifteen. | `break_between_blocks` · `tests/test_columns.cpp` `under_the_products_rules_a_column_can_end_between_two_blocks_but_not_after_a_heading` |

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
reference's rules (`ImTypesetContext::tex_rules` and `ImTypesetDocument::reference_rules`, internal
state a test sets; no public call selects either) the layout is the reference's, with every length
handed over as float pixels through the public structs and held to the golden's scaled points by
`==` (`tests/document_reference.h`; `docs/decisions.md` D45): `tests/test_api_document.cpp` replays 67 of the 69 block goldens through
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

## d7-fix: layout findings seen while fixing draw, none of them changed

Recorded by `d7-fix`'s builder for whoever takes up the layout rules next. Where a sentence says
`rule_joins_span`, that rule has since been replaced (D47).

Three things the demo's builder saw by eye at 19 px, measured (`ImTypeset::DrawParagraph`, FreeType,
origin (3, 40); the numbers are the same under both draw rule sets unless said):

- `\sqrt{\frac{a}{b}}`, "the rule touches the a". Layout. The radical's rule ends at y 44.41 and
  the `a` (script size) has its baseline at 51.75; drawn, the rule is the row 44 and the `a`'s
  quad starts at row 46: one clear row. The clearance is what §737 gives in text style with this
  face; nothing in draw moves it. Whether a product layout rule should give a radical over a
  fraction more room is a `tex-fixes`-kind question.
- `\int f(x) dx`, "12 px between the sign and the integrand". Layout. The `f` starts 22.1 px after
  the integral's origin: its advance (665/1000 em = 12.6 px), plus its italic correction (332/1000
  em = 6.3 px, read from the face's MATH table), plus a thin space (3.2 px). The sign's ink ends
  609/1000 em along. §749 adds the italic correction to an operator with no subscript, which is
  right for a TFM, where the width stops short of the slant; an OpenType advance already covers
  the glyph. The reference does the same, so the port is faithful and the result is wide. The same
  correction pushes the upper limit of `\int_0^1` far to the right. To check against a current
  engine before changing: I believe OpenType-aware engines do not widen the operator, but I did
  not verify it.
- "About 2 px of word space after an inline formula ending in a fraction, 5 px before." Partly this
  rule, partly layout. Not reproduced at 2 px on the sentences I tried (unjustified, 16 and 19 px:
  3 to 6 px after, 4 to 7 before). What is there: (i) under the reference's rules and under
  `rule_joins_span` the formula's glyphs gather rounding, and the ink of
  `\sum_{i=1}^{n} i = \frac{n(n+1)}{2}` at 16 px ends at 138 where its box ends at 136.66, taking a
  pixel and a half out of the space after it; placed whole it ends at 137. (ii) A fraction's bar
  runs to the very edge of its box (the reference sets no null delimiter space around a fraction,
  §748), so the space after it has no side bearing in it, while the space before a formula that
  starts with a letter or a sum sign has that glyph's bearing added: 7 against 5 px at 19 px from
  that alone. (iii) In a justified line the glue may also be shrunk. (ii) and (iii) are layout.

## tables-core

`src/align.*`: the reference's table layout (`_natural_widths`,
`_longest_word`, `_column_widths`, `_apportioned`, `_squeezed`, `_shared`,
`_table_boxes`, `_table_row`, `_wrapped_row`, `_cell_lines`, `_cell_params`,
`_aligned`, `_WrappedRow`, `prev_depth_after` and `_stacked`'s arm for it, of
`advtext/ui/__init__.py`). The port is the reference: `tests/test_align.cpp`
replays the 36 `table` cases of `tests/golden/tables` (`widths`, `shapes`,
`demo`; 97 layouts, 441 rows) record by record, exactly, twice over (the steps
one at a time, and `Aligner::fin_align`).

### Alignment — `src/align.*` (tex.web part 37, §768–§812)

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| T1 | §801 l.15767, §804 l.15836 | the final width `w_j` of a column is the widest entry in it (spans aside) and nothing changes it; `\halign to` and `spread` are met by packaging the prototype row, which stretches or shrinks the tabskip glue between columns | a table whose widest cells sum to no more than the measure has every column scaled **up** in proportion, `widest[c] * available // total`, the last column taking the remainder, so the table always fills the measure (`apportioned`) | deliberate: `ui/__init__.py:2211-2226`, `2015-2017` |
| T2 | §801 l.15767, §810 l.15937 | a table wider than its `to` is an overfull prototype row: tabskip shrink is used up and the columns stay at `w_j` | each column is given its longest unbreakable piece plus padding and the measure left over is shared in proportion to `widest - narrowest`, the last column taking the remainder (`squeezed`); when the floors alone exceed the measure they are the widths and the table is wider than it | deliberate: `ui/__init__.py:2018-2031`, `2108-2120` |
| T3 | §801 l.15767 | no counterpart: a width is never given, only a template | a caller's absolute widths are used as they are, whatever the measure; a caller's fractions divide the measure, each share rounded half to even and floored at 1 sp, the last column taking the remainder and borrowing from the first of the widest what would leave it under 1 sp (`shared`) | deliberate: `ui/__init__.py:1580-1611`, `2058-2071` |
| T4 | §801 l.15767 (`null_flag`, "which we regard as −∞"), §802 l.15804 | a column's width starts at `null_flag` and becomes 0 only if no entry ended in it | starts at 0, so a column whose entries are all narrower than nothing is 0 wide | undeclared (`ui/__init__.py:2041`, `[0] * columns`); reachable only with a cell of negative natural width and no padding to cover it |
| T5 | §796 l.15665 | an entry is packaged as it ends, unset, and its natural width recorded; its stretch and shrink totals are kept in the unset node for §810 | a cell is a list the caller keeps; its natural width is one `hpack` asked for when wanted, with the padding added (`natural_width`, `fin_col`), and nothing is unset | deliberate, as representation: `ui/__init__.py:2144-2147` (two passes over the cells) |
| T6 | §810 l.15937 | an entry wider than its column has its glue set as far as it goes and is left overfull; an entry is restricted horizontal material and is never broken | a row with a cell whose natural width exceeds its column has **every** cell of the row, the ones that fit included, broken first fit at `max(column - 2 * padding, 1 pt)` from the material with its shrink removed, ragged on the side its alignment leaves free and without `\parfillskip` unless left-aligned | deliberate: `ui/__init__.py:2149-2168`, `2270-2291`, `2374-2410` |
| T7 | §1087 l.21078 (`\vtop`), §807 l.15881 | a `\vtop` is a user's box: height the first item's, the rest depth | each cell of a wrapped row is that reboxing of its lines, done here: `vpack`, then height := the first line's strutted height and depth := the rest; its width is the column's whatever its lines are | deliberate: `ui/__init__.py:2400-2404` |
| T8 | §679 l.13312, §799 l.15714 | `fin_row` appends the row by `append_to_vlist`, and the next row's interline glue is computed from the row's own depth | after a wrapped row the next append is handed the depth of the row's **lowest line** (`strutted.depth - lowest`), not the row's depth, which is every line below the first; the row's box is unchanged (`SetRow::prev_depth`, `TableBox::prev_depth_carried`) | deliberate: `ui/__init__.py:2316-2350`, `1162-1164` (what a document that assigned `\prevdepth` after the row would get) |
| T9 | §778 l.15379, §795 l.15661, §809 l.15920 | columns are separated by tabskip glue, which is in the row's list between the entries' boxes and takes part in the prototype row's glue setting | no glue between columns: the padding is a kern on each side **inside** each cell's box, column widths include both, and a row is its cells' boxes side by side at natural width | deliberate: `ui/__init__.py:2246-2257` (`_table_row`), `1600-1603` |
| T10 | §807 l.15881 (`shift_amount(q):=o`) | a row is shifted by the display indent, or not at all | the table's indent is a kern at the head of every row's list (and of the rule's box), and the row's `shift_amount` is 0 | deliberate in effect: `ui/__init__.py:2246`, `2292`, `2197` (no comment says why) |
| T11 | §797 l.15683, §798 l.15699, §803 l.15812, §808 l.15898; §788 l.15553, §790 l.15586; §785 l.15499; §811 l.15958 | `\span`, `\omit`, `\noalign`, `\valign` | none: one cell, one column, rows only. A row with fewer cells than the table has columns is given an empty cell for each it lacks (packed to the column's width, or one empty line when the row wraps); §807 leaves such a row's list short | deliberate: the reference's `TableBlock` has no such thing; `ui/__init__.py:2248`, `2295` for the missing cell |
| T12 | §777–§784 l.15367-15498, §789 l.15567, §793 l.15640, §794 l.15649 | the preamble's `u_j` and `v_j` templates are inserted around each entry | no templates: a column's alignment is `\hss` (`0 plus 1fil minus 1fil`) after the material, before it, or both, inside the cell's box (`aligned`) | deliberate: `ui/__init__.py:2456-2462` |
| T13 | §806 l.15871 | a rule between rows has running dimensions, extended to the alignment's edges when it is set | the rule under a header is an hbox of the indent kern and a rule of written width `sum(columns)` and the theme's thickness, not strutted; there is no other rule | deliberate: `ui/__init__.py:2193-2206`, `2222-2224` |
| T14 | §799 l.15714, §805 l.15859 | a row's height and depth are its entries' | each row is floored at the table's strut, once, on the row; a line of a wrapped cell is floored at it too | deliberate: `ui/__init__.py:2188-2191`, `2400-2401` |
| T15 | §800 l.15736, §796, §799 | rows are packaged unset as they are read and all set at the end by `fin_align` | a row is built once, at widths already decided, and appended afterwards; `fin_row` here is only §799's `append_to_vlist` | the port's own (representation); no decision number |
| T16 | — | no counterpart | the longest unbreakable piece of a cell is asked of the line breaker: the cell's material (shrink left in) broken first fit at a measure of 1 pt with no left or right skip, and the widest line's natural width taken (`Aligner::longest_word`); measured only when the table does not fit | deliberate: `ui/__init__.py:2086-2092`, `1888-1890` |
| T17 | — | no counterpart | a policy the reference refuses when the table is constructed (not one entry per column; a width under 1; a fraction that is not finite and positive, or nothing as a share of the sum) is refused here at the solve: the default policy is taken and `ColumnSolve::refused` says why. Also refused, the port's alone: a share whose width `available * share / total` is not finite, for which the reference's `round` raises | the port's own: D17 (no exceptions; a status for anything reachable from data) |
| T18 | — | no counterpart | sums through `add_checked`; `a * b // total` through a 128-bit product where the compiler has one (otherwise exact while the product fits 64 bits, and a double's quotient with `ColumnSolve::overflowed` past that); the style's lengths clamped to `MAX_DIMEN`; at most 2^20 columns; a BOX or DISC node the arena does not hold, or one nested within 8 levels of `NodeArena::MAX_NESTING`, set as an empty rule and reported as `NODE_NOT_IN_ARENA` | the port's own: D17, D33 |
| T19 | §810 l.15938 (`height(r):=height(q); depth(r):=depth(q)`) | every entry of a row is given the row's height and depth when it is made an hlist | a cell's box keeps the height and depth its own `hpack` found; the row's box, packed from the cells, has the largest of each | undeclared in the reference (`_table_row` packs each cell `exactly` to its width and nothing more); every golden's commands are the reference's with it (`tests/test_align.cpp`). Found by review-6, recorded by `tables-fixes` |
| T20 | §1056 l.20520 (`prev_depth:=ignore_depth` after a rule in vertical mode), §806 l.15871 | a rule between rows (`\noalign{\hrule}`) is a rule node of the vertical list: no interline glue is put above it or below it, and the rows either side are as far apart as their struts make them plus the rule | the rule under a header is an hbox (T13) appended by `append_to_vlist` like a row: §679's glue puts its foot one `\baselineskip` below the header's baseline and the next row's baseline one `\baselineskip` below that. The first body row is two leadings under the header, with the rule nearer the body | the reference's own (`_table_boxes`, `_stacked`), held by every golden with a header; seen by eye as unbalanced (`swap-fixes`). Not changed: a question for the user (docs/advtext-swap-status.md, "Border questions") |
| T21 | §679 l.13312 | no counterpart: TeX has no line that is exactly a strut | with `Strut::exact` a row set on one line is the strut's height and depth; a wrapped row is the strut's height and as deep as its cells' lines, each of which is the strut | the port's own: D62. The reference has no exact strut |

Stated in `src/align.h` ("WHERE THIS IS NOT tex.web") and at each function in
`src/align.cpp`.

### Where the reference's two paths differ, and which the port follows

1. **A negative natural width in a column only later rows have.**
   `_column_widths` (`ui/__init__.py:2041-2044`) starts every column at 0;
   `TableMeasure` (`_column_maxima`, `ui/__init__.py:1962-1971`) lets a column
   that only one of two rows has keep that row's width as it is, negative
   included. The oracle script asserts the two equal on every golden, and they
   are, since no golden cell is negative. The port follows `_column_widths`
   (T4). *Read, not run.*
2. **More fraction columns than the measure has scaled points.** `_shared`
   floors every share at 1 and takes the excess from the first of the widest
   columns, which can leave that column negative when the columns outnumber
   `available` (the docstring's "the widest column always has it to give"
   holds for any table a person would write). The port does the same and does
   not clamp. *Read, not run; no golden reaches it.*

### What the goldens do not hold

- `rigid` (FORMAT-tables.md §3.3 says so): reached only by numbers, in
  `a_table_with_nothing_to_give_is_rigid`.
- A tie in `_shared`'s rounding. Replacing half-to-even by half-up fails no
  golden record; `shares_round_half_to_even_and_the_last_column_borrows_from_the_widest`
  holds it (65537 sp in two equal shares is 32768.5 each and rounds to 32768).
  The expectation there is Python's `round`, stated from its definition; the
  reference was not run for it.

### Tables in a document — `src/blocks.*`, `src/imtypeset_document.cpp`

What the Document does with a table that the reference's views (`BareView`, `StreamView`,
`advtext.ui.units`) do otherwise. "Under reference rules" means with
`ImTypesetDocument::reference_rules` set, as the replays of `tests/golden/doc-tables` set it.

| # | the reference | the port | class |
|---|---|---|---|
| D1 | a position is a reading-order index; the oracle script (`dump_doc_tables.py`, `TMap.pos`) names an index that came from no character by the NEXT sourced item, so a caret after a cell's last character is "byte -1" or the next cell | a position is (block, byte of the block's text); an index that came from no character is its cell's first character before it and the END of the cell's text after it (`map_table_indices`) | the port's own: the script's rule is the script's, not the reference's (coordinator's ruling, D57) |
| D2 | a copy writes a separator only between two texts: an empty first cell of a selection loses its tab (`first\tlast` for `["", "first", "last"]`), and tabs after the last text are not written (`advtext.selection.text`) | under the product's rules a copy of a table is cell by cell: every cell from the range's first to its last, an empty one included, with its tab or newline. Under reference rules the reference's | deliberate (D57): a row pasted into a spreadsheet must keep its columns |
| D3 | a row with fewer cells than the table has columns copies with a tab for each cell it lacks, when text follows (`_separators_within` walks a cell box per COLUMN) | under the product's rules a row gives the cells it has. Under reference rules the reference's | deliberate (D57): the block's text, written as advised, is then what a copy gives |
| D4 | a colour span on a table is kept and never applied; a find match in a cell is outlined by its host from `span_rects` | a mark over a range of a table marks what it covers of each cell, in all three places | the port's own: no oracle (FORMAT-doc-tables.md section 4) |
| D5 | `StreamView` lays a table of more than four rows out in pieces of four, each a unit, and a row that moves no width builds the last piece only | a table is one block and one unit; each row is kept as it was shipped and one that moves no width is the only one set (`TableLayout`, `ImTypesetDocumentStats::TableRowsSet`) | the port's own: the pieces are the view's, and `unit`, `built` and `npieces` of `stream.golden` are not replayed |
| D6 | the widths a live table is held at are the view's (`StreamView._held`, `_kept`, by block number) | they are the block's, under `ImTypesetBlockFlags_TableHeld`, and survive a replace of one held table by another with as many columns | the port's own shape for the reference's rule |
| D7 | a table in a quote is not barred (only a text block has a quote depth) | under the product's `DocumentRules::quote_any_block` the quote's bar runs past it, as past a code block | deliberate: blocks item 3 |
| D8 | a hard break in a cell is a forced line end (`_inline`) | a newline in a cell is white space | the port's own: no golden has one (FORMAT-tables.md section 8) |
| D9 | `UnitModel.rects`: one rectangle a line from the first selected text item to the last, each running to the next item's left edge. A row set on one line is one line: a range through several of its cells is one bar over the blanks and the padding between them | under the product's rules one rectangle a cell and line, kept inside the cell's text area (`range_rects`). Under reference rules the reference's (1,286 golden rectangles through tables, exactly) | deliberate (D61) |
| D10 | `UnitModel.hit` answers an index; every item is split at its middle, the `\hss` that fills a cell included, so the far half of a cell's blank is the next cell's first index (the next row's, in the last column). The reference has no "which cell" | `HitTestDocument`'s Row and Column are the cell whose rectangle holds the point, under either set of rules; under the product's rules the position is a byte of that cell. `SelectionModel::hit` is still the reference's index (1,848 golden probes, exactly) | deliberate (D60) |
| D11 | no caret rectangle in a table | `GetDocumentCaretRect`: in the cell, on the line the position's item is on and as tall as it, at the cell's own end for the byte that ends it | the port's own: no oracle (D60) |
| D12 | a table not yet laid out has no height (the reference lays everything out) | estimated at its rows' leadings plus one for the rule under a header (T20 is why it is a whole one) | the port's own: lazy layout (D37) |

## columns-core

`src/vsplit.*`: the reference's column cutting (`vert_break`, `_break_badness`,
`_precedes_break`, `prune_top`, `vsplit`, `split_into_columns` of
`advtext/columns.py`). The port is the reference: `tests/test_vsplit.cpp`
replays the 28 `vsplit` and `columns` cases of `tests/golden/tables`
(`vertbreak`, `documents`, `columns`; 85 splits, 247 columns, 1,850 legal
breakpoints) record by record, exactly.

### Splitting a vertical list — `src/vsplit.*` (tex.web part 44, §967–§979)

Sections checked by counting modules from the top of `tex.web` and reading
each: 148, 833, 968 to 979, 1001, 1082. Line numbers are `tex.web`'s.

| # | tex.web | TeX | the port | class |
|---|---|---|---|---|
| V1 | §977 l.19047, §978 l.19068 | `vsplit(n, h)` takes a box register, answers void for a void box and an error for an hbox, and leaves what remains in the register, packed again with `vpack(q, natural)` | `vsplit` takes a list and answers the part split off (a `PackResult`: the box and how well it fitted) and where the pruned remainder starts; the remainder is not packed | deliberate: `columns.py:380-403` |
| V2 | §977 l.19054, §979 l.19084 | `split_first_mark` and `split_bot_mark` are set from the marks before the cut | nothing: there is no mark node | deliberate: `columns.py:109-111` |
| V3 | §968 l.18877, §973 l.18970, l.18978 | whatsits, marks and insertions are kept at the head of a pruned list and passed over by the scan | no such nodes; a character, a discretionary or a math bracket, where TeX stops with `confusion("pruning")` or `confusion("vertbreak")` and the reference raises `ValueError`, is deleted (pruning) or left out of the heights and never a breakpoint (the scan), and `VsplitError::NOT_VERTICAL` is answered | the port's own (D17): a list reaches the scan from data |
| V4 | §138, §973 l.18966 | a rule in a vertical list has a height and a depth; a running one would be added as `null_flag` unexamined | a rule whose height or depth is `RUNNING` adds nothing and `VsplitError::RUNNING_RULE` is answered; at the head of a pruned list its glue is the skip whole. The reference raises | the port's own (D17) |
| V5 | §976 l.19018 | glue with infinite shrink in the list is an error message, after which its shrink is counted as finite | its shrink is counted as finite and nothing is said | deliberate: `columns.py:325-328` ("coerced silently and the citation is the record") |
| V6 | §974 l.18995, §971 | `best_height_plus_depth` is a global the page builder reads | kept on the scan (`VertBreak::best_height_plus_depth`), where the reference does not keep it at all: the Document reads it for how much of a column its material takes | the port's own; no golden holds it, `the_size_of_the_part_split_off_is_its_boxes_and_glue_with_the_last_depth` does |
| V7 | §970 l.18926 | `prev_p := p` makes glue at the head of the list not a breakpoint | the same statement as an index: glue at index 0 is not one | none (same behaviour; said because the port, like the reference, has no `prev_p`) |
| V8 | §1082 l.20994; §1001 l.19509 | `\vsplit` is a primitive and TeX has no columns; the head of a PAGE gets `\topskip` glue by the same computation as §969 | `split_into_columns`: the list pruned once before the first column with `\splittopskip` (one parameter where TeX has two), then `vsplit` `count` times, what is left answered as overflow, columns past the end of the material empty and as tall as the rest. No balancing | deliberate: `columns.py:435-499`, and `:47-60` for balancing |
| V9 | §969 l.18889 | the head glue is a new glue node linked in front of the box | written into the node before the column's first box, which the cut before it deleted (`ColumnList`, a spare node before the material for the first column): a column's contents are a span of the caller's list and nothing is copied. The reference builds a new list for every column | the port's own (representation); the records are the same |
| V10 | §970 l.18914 | the scan is one procedure over a whole list | `VertBreak` keeps §970's variables between calls and can be fed a list that is still being made; a kern at the end of an unfinished list waits for the node after it. `vert_break` is the one call over a whole list | the port's own: a document's list is made as far as a column needs (`src/blocks.h`); `a_scan_fed_a_list_in_pieces_ends_where_a_scan_of_the_whole_list_does` holds the two equal |

No disagreement between `tex.web` and the reference was found in what the
reference implements: §973's breakpoint rule (any kern before glue; glue after
a node that `precedes_break`), §974's cost and its `<=`, §975, §976's sums and
§972's depth limit are transcribed there as written, and the goldens hold the
port to them.

### What the goldens do not hold

- A list with a node that cannot be in it, a running rule, a box of another
  arena: the reference raises; `a_node_that_cannot_be_in_a_vertical_list_is_left_out_and_named`.
- `split_into_columns` on no columns or a height not above zero (the reference
  raises; the port answers false).
- `best_height_plus_depth` (V6).
- A document composed by the reference with vertical stretch or a penalty in
  it: it makes none (FORMAT-tables.md section 6), so in `documents.golden` and
  `columns.golden` every short column costs `deplorable` and the cut is the
  last breakpoint that fits. The other arms of §974 and §975 are held by
  `vertbreak.golden`'s hand-built lists alone.

### Columns of a document — `src/columns.*`, `src/blocks.*`, `src/imtypeset_document.cpp`

What the Document does with columns that the reference (`compose_into_columns`, and the demo's
`ui/app.py` `ColumnPage` and `column_layout`) does otherwise. "Under reference rules" means with
`ImTypesetDocument::reference_rules` set, as the replays of `tests/golden/tables/columns.golden`
set it. Decisions and their reasons are D58.

| # | the reference | the port | class |
|---|---|---|---|
| C1 | `compose_into_columns` composes the whole document, cuts its list into `count` columns of one height and returns what did not fit as `overflow` | columns filled in order are as many as the document needs: the reference's `count` columns are the first `count`, and its overflow is the columns after them. A column is cut when first reached, from a list made a unit at a time (`Document::unit_vlist`), and kept until a unit its scan read changes | the port's own: a document is laid out lazily (D37). `tests/test_columns.cpp` holds 2,113 columns of random documents to `split_into_columns` of the same blocks composed at once |
| C2 | every column is a box packed `exactly` to the height and shipped | no column box is made: a column is the stretch of the uncut document it holds, moved (`DocumentColumns::flatten`, and the draw). The same commands, since a document's list has nothing to stretch or shrink; a list with stretch in it would be spread by the reference and cannot be described here | the port's own (D58); `columns.golden` holds the commands, its two overfull columns included |
| C3 | a box in the list is a box | a box is a RULE node of its width, height and depth: §969 and §973 read a box and a rule alike, and a rule needs no arena | the port's own (representation) |
| C4 | no balancing ("Balance: not done, and here is the price") | `ImTypesetColumnFlags_Balance`: the least height at which `Count` columns hold the document, by bisection over §977's cut. Every block is laid out for it | the port's own: no oracle. D58 |
| C5 | the space between two blocks is a kern with no glue after it, so no column ends between two blocks, and a run of one-line blocks cannot be cut (`columns.py:75-80`, which names the fix) | under the product's rules it is glue in the list a column is cut from, there between any two blocks, and a kern after a heading (`DocumentRules::break_between_blocks`, the block layer's item 16; `Block::keep_with_next`). Under reference rules the reference's | deliberate (D58); no golden holds the product's list |
| C6 | `\splittopskip` is 10 pt (`SPLIT_TOP_SKIP`) | under the product's rules the height of a line of the document's style above its baseline. Under reference rules 10 pt | deliberate (D58) |
| C7 | columns are drawn; nothing is hit-tested, selected or copied in them | points, rectangles and carets are taken between the page and the uncut column; a drag across columns selects in reading order and a copy is the uncut document's | the port's own: no oracle (D58) |
| C8 | pages of `count` columns, a gutter of one leading, a hairline in the gutter (`ui/app.py`) | columns side by side at `k * (width + Gutter)`, a default gutter of one line of the style; no pages and no rule between columns: a page is a clip rectangle and a rule is the host's to draw from `GetDocumentColumn` | the port's own: the reference's are its demo's |
| C9 | `\splitmaxdepth` is `\maxdimen` (`split_into_columns`' default, plain TeX's): a column's last box may be as deep as it likes below the foot. A table row whose cells wrapped is a box one line high and all its other lines deep | under the product's rules the depth of a line of the document's style (`apply_column_rules`): §971 counts what is deeper as height, and a column holds the whole of its last row or does not take it. Under reference rules `\maxdimen` | deliberate (D63): a wrapped row hung below the foot by all its lines but the first, unflagged |
| C10 | the interline glue above the rule under a table's header, and above the row after it, is glue like any other between two boxes: a column may end between a header and its rule | under the product's rules (`DocumentRules::keep_table_header`, rule 17) those two are kerns in the list the columns are cut from: header, rule and the row under it stay in one column. Under reference rules glue | deliberate (D63): seen by eye (`swap-fixes`, `c2-end`) |
| C11 | an overfull column is one whose least cost is `awful_bad`; the page's height is the columns' | `ImTypesetColumnGeometry::Overfull` under IN ORDER is that; under BALANCED (C4) it is never set, the page being as tall as its tallest column; and `GetDocumentSize` is as tall as the tallest overfull column cut so far holds | the port's own: no oracle (D63) |
| C12 | a heading is not kept with anything (C5: no cut between blocks at all) | under the product's rules a heading is kept with the next block that has something in it: a paragraph with no text between them, which sets a blank line, does not part them | the port's own (D63) |

## handoff-core

What changed in task `handoff-core` that is not the reference's. Decisions: D66 to D70.

| # | the reference | the port | class |
|---|---|---|---|
| H1 | a glyph is drawn through `ImFont::RenderText`, which skips it by its font's cell: the pen below the clip rectangle's bottom edge, or the whole cell above its top edge | under the product's draw rules (`DrawRules::quads_off_clip_dropped`) the test is the quad's own: emitted exactly when its top is above the bottom edge and its bottom below the top edge. A glyph whose bitmap is taller than the cell is drawn by the rows of it that are inside; and every glyph placed is loaded into the atlas, where the reference loads only those its cell test keeps. Under reference rules the cell test, as before | deliberate (D56, D69). It replaces what D56 recorded for this rule, which kept the cell test in front of the quad's |
| H2 | a colour span or a match is applied to every line it covers; the views cull | the marks of a draw are worked out for the blocks drawn and, in a block with more than 16 of them, for the lines within reach of the clip rectangle. What is emitted inside the clip rectangle is the same; a mark's rectangles on lines the draw does not show are not emitted | the port's own (D69) |
| H3 | no query for a face's line gap; `fontlib.metrics` answers ascent, descent, x-height, em and advances | `GetFontMetrics` answers those to the bit, as floats, and the hhea line gap beside them, which no layout reads | the port's own (D66) |
| H4 | T20 stands as it was written: the rule under a header is stacked like a row. Measured again on `demo/tables.md` at 684 px, to the scaled point, under both sets of rules (D67) | the same | no departure: recorded because the opposite was reported |
