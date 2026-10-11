# Math goldens: `tests/golden/math/*.golden`

Written by `tools/oracle/dump_math.py` from the Python reference
(`build/advtext-reference/advtext/{style,mathtext}.py`, `advtext/math/{atoms,mlist}.py`,
and `advtext/markdown.py` for formulas in running text). Regenerate, from the
repository root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/dump_math.py

Two runs write identical bytes. `dump_math.py --verify` re-reads every file
with a reader that uses only what this document and `FORMAT.md` say, rebuilds
the inputs from the records, replays them through the reference and compares.
For a math-list case it replays twice: with the fonts the case names, and with
a font that knows nothing but the case's own `q_*` records. Both must give what
is recorded, which is the check that those records are sufficient.

`dump_math.py` imports `dump_core.py` and writes the shared records with its
functions; a change to those functions changes these files.

## 1. What is shared with `FORMAT.md`

Everything in its sections 1 to 4, verbatim: the lexical layer (tokens,
integers, `float.hex()` floats, `%XX` strings with `%` for the empty string,
booleans `0`/`1`, enums as lower-case member names), the node records (`char`,
`rule`, `glue`, `kern`, `penalty`, `mathon`, `mathoff`, `box`…`endbox`,
`disc`…`enddisc`, `nodes <name> <n>`…`endnodes`), `measure`, `packed`, and the
draw-command records (`cmds <n>`…`endcmds`, `origin`). Also its section 5 file
header and `case <name> <kind>`…`endcase` framing, and the `linebreak` case kind
of its section 6, which `inline.golden` reuses whole.

The header's file id is `math/<file stem>`. Every integer fits a signed 64-bit
integer.

Enums added here:

| type | tokens |
|---|---|
| Style | `display` `display_cramped` `text` `text_cramped` `script` `script_cramped` `script_script` `script_script_cramped` (numeric values 0..7, in that order; the values are semantics, the transitions are arithmetic on them) |
| MathSize | `text` `script` `script_script` (numeric values 0, 16, 32) |
| AtomType | `ord` `op` `bin` `rel` `open` `close` `punct` `inner` `over` `under` `vcenter` `radical` `accent` `left` `right` `space` |
| Limits | `default` `limits` `no_limits` |
| Spacing | `nonfrench` `extra_space_only` `french` |
| RefusalKind | the reference's *values*: `empty` `syntax` `nesting` `unknown-command` `character` `macro` `environment` `matrix` `alignment` `infix` `delimiter` `radical` `accent` `style` `spacing` `text` `font` `layout` |
| Face | the reference's *values*, as a string: `prose` `prose-bold` `prose-italic` `prose-bold-italic` `code` `code-bold` `math` |

## 2. Math lists

A math list is

    mlist <n>
      ... n items ...
    endmlist

An **item** is an atom or a fraction, each followed by its fields:

    atom <kind:AtomType> <limits:Limits>
      <field: nucleus>
      <field: sup>
      <field: sub>
    fraction <has_thickness:bool> <thickness:int>
      <field: numerator>
      <field: denominator>

`fraction 0 0` is the reference's `thickness=None` (use the font's default rule
thickness); `fraction 1 0` is a thickness of exactly zero, which is `\atop`: no
rule. The two are different inputs.

A **field** is exactly one of:

    empty
    mathchar <codepoint>
    mathtext <face:string> <text:string>
    muglue <width> <stretch> <shrink>             in math units
    subbox
      box ... endbox                              one node record (FORMAT.md section 3)
    sublist <n>
      ... n items ...
    endsublist

So an item is self-delimiting and a reader recurses without looking ahead. The
reference places no restriction on which field kind appears where; the
`malformed/*` cases of `mlist.golden` are the combinations it rejects.

## 3. The files

| file | case kinds | what |
|---|---|---|
| `style.golden` | `table` | font keys, the style algebra, the spacing table, constants, space factors, interword glue, struts, face metrics, the three math sizes |
| `unicode.golden` | `ranges` | the Python `str` predicates the reference branches on, for every codepoint |
| `mathtext.golden` | `table` `parse` | `advtext.mathtext.parse`: 3,776 sources, each with its math list or its refusal |
| `mlist.golden` | `table` `mlist` | `mlist_to_hlist` over 2,589 hand-built lists, on fake fonts and the real one |
| `formulas.golden` | `table` `mlist` | 2,037 parsed sources set on the real fonts, with the `typeset` refusal where the engine refuses |
| `inline.golden` | `inline` `linebreak` | formulas in running text: where they are found, and the paragraph that results |

The six files hold 8,555 cases between them (`dump_math.py --verify` prints the
count per file: 2,038, 142, 3,777, 2,590, 7 and 1 in the order `formulas`,
`inline`, `mathtext`, `mlist`, `style`, `unicode`; the `table` case that opens
`formulas`, `mathtext` and `mlist` is counted). Counted on 2026-10-09; the
script's output is the authority, not this paragraph.

The last 13 cases of `mlist.golden` are named `boundary/…`
(`largest-variant-cover`, `delimiter-tie`, `op-one-step`, `atop-clearance`).
Each stands on one comparison or one constant that no other case lands on, and
`boundary_cases` in the script asserts that it does.

## 4. Case kind `mlist`

One call of `mlist_to_hlist(items, style, fonts)`, then `hpack` at natural
width and `ship`.

    source <text:string> <display:bool>           only in formulas.golden
    fonts <math:keyword> <text:keyword> <base_size>
    delimiter_shortfall <sp>
    style <style:Style>
    mlist ... endmlist
    sizes <text_size> <text_key:string> <script_size> <script_key:string>
          <scriptscript_size> <scriptscript_key:string>          (one line)

then either a formula that set:

    measure*                                      FORMAT.md; absent if the result holds no char
    q_* records                                   section 4.2
    nodes hlist <n> ... endnodes                  what mlist_to_hlist returned, unpacked
    packed ... box ... endbox                     hpack(nodes, measure) at natural width
    origin 0 <box.height>
    cmds ... endcmds                              ship(box, origin, measure)

or one it refused:

    q_* records
    refused <slug:keyword> <detail...>
    message <text:string>                         str() of what the reference raised
    typeset_refusal <kind:RefusalKind> <position> <byte_position> <reason:string>
                                                  only with `source`: what typeset() returns

**Inputs** are `fonts`, `delimiter_shortfall`, `style`, the math list, and —
for a port driven without a font reader — the `q_*` records. Everything else
is expected output. `sizes` is `math_fonts(base_size, ...)`: the script sizes
are `round(base_size * script_scale)` and `round(base_size *
script_script_scale)` with the scales read from the *text-size* face's
parameters, and the keys are `math@<size>`.

`source` is where the list came from: `parse(text, display=display).items`,
with `style` then `display` or `text`. It is informational for the engine and
is the input for an end-to-end test. A case without it was built by hand.

`delimiter_shortfall` is the value of `advtext.math.mlist.DELIMITER_SHORTFALL`
during the call. It is 327680 (5pt) everywhere except the cases whose `fonts`
record names the `chained` face with base size 18000 and which say 9000: the
reference's own tests patch the module constant for that fake, because 5pt is
eighteen of its quads and would never decide. A port has to be able to take
this number as a parameter to replay those; they are the cases that state each
boundary of the delimiter rules in round numbers.

### 4.1 `refused` slugs

| slug | detail fields | what the reference raised |
|---|---|---|
| `delimiter-too-small` | `<codepoint> <needed> <largest>` | `DelimiterTooSmallError`: a `MathLayoutError`, a data condition |
| `missing-glyph` | `<codepoint> <face:string>` | `MissingGlyphError`: likewise |
| `text-face-unavailable` | none | `TextFaceUnavailableError`: likewise; `fonts` text is `none` or `broken` |
| `not-implemented` | none | `NotImplementedError`: an `accent` atom. **Not** a `MathLayoutError` |
| `malformed-field` | none | `TypeError`: a `muglue` where no space is, a `space` without one, a boundary whose nucleus is not a character or empty. **Not** a `MathLayoutError` |

The first three are what `typeset` turns into a `Refusal` (`layout` for the
first, `font` for the other two). The last two are a caller's error in the
reference and reach `typeset`'s catch-all only as kind `font`; a port decides
how to report them (docs/decisions.md D17) and must not crash on them.
`mathtext` never produces either, so no case with a `source` has one.

`message` for the first two contains numbers and a codepoint and is checked by
the reference's tests; it is here so a port that reproduces it can be compared,
not because a host shows it. `typeset_refusal`'s `reason` is what a host shows.

### 4.2 Font questions: `q_*`

Every question the reference asked of a font during the call — while
converting, and while the `measure` records were produced — with its answer.
Sorted by tag, then key, then the remaining fields; each distinct question
once.

    q_params <key:string>                         the 27 values are in the file's first case
    q_space <key:string> <space_width:float>
    q_has <key:string> <codepoint> <bool>
    q_advance <key:string> <codepoint> <advance:float>
    q_bbox <key:string> <codepoint> <x_min:float> <y_min:float> <x_max:float> <y_max:float>
    q_italic <key:string> <codepoint> <italic_correction:float>
    q_kern <key:string> <codepoint> <following> <kern:float>
    q_variants <key:string> <codepoint> <n> (<name:string> <codepoint> <extent:float>){n}

`key` is the font key of the face asked (`math@1105133`, `prose-bold@1578762`).
All lengths are floats in scaled points at that key's size, unrounded; rounding
is the engine's and is part of what is being tested. `height` is `y_max` and
`depth` is `-y_min`. `q_variants` with `n` 0 is a character with no chain; the
first variant of a chain is the base glyph. A variant's codepoint may be a
synthetic one in plane 15 (`0xFFF1B..0xFFFFD` for Latin Modern Math), and those
appear in `char`, `measure` and `glyph` records unchanged.

`mlist.golden` and `formulas.golden` each open with

    case font-params table
      params <key:string> <27 floats>
    endcase

giving `MathParams` for every key a `q_params` in the file names, in the field
order `style.golden` lists in its `param_fields` record: `math_x_height`
`math_quad` `num1` `num2` `num3` `denom1` `denom2` `sup1` `sup2` `sup3` `sub1`
`sub2` `sup_drop` `sub_drop` `delim1` `delim2` `axis_height`
`default_rule_thickness` `big_op_spacing1`…`big_op_spacing5`
`radical_rule_thickness` `radical_vertical_gap`
`radical_display_style_vertical_gap` `script_scale` `script_script_scale`
`display_operator_min_height`. The two scales are dimensionless.

A port may answer from these records and nothing else (a question they do not
answer is one the reference did not ask, though a port is free to ask in a
different order or to ask more), or from its own font reader, in which case the
records are a cross-check of the reader on exactly the glyphs the case touches.

### 4.3 The fonts a case names

`fonts <math> <text> <base_size>`:

| `math` | the math face at a size `s` (a float; the size in the key) |
|---|---|
| `real` | `advtext.fontlib.math_metrics(s)`: Latin Modern Math |
| `fake` | the reference tests' `FakeMathMetrics`: `has` always true; `advance` `s/2`; `bbox` `(0, 0, 0.45 s, 0.4 s)`; `kern` 0; `italic_correction` 0; no variants; `space_width` `s/3`; parameters below |
| `chained` | `fake`, plus: variant chains for `(` (`0x28, 0xE001, 0xE002, 0xE003`), `)` (`0x29, 0xE011, 0xE012, 0xE013`) and U+221A (`0x221A, 0xE021, 0xE022, 0xE023`), named `v0`..`v3`, with (height, depth) in ems of (0.75, 0.25), (1.25, 0.75), (1.75, 1.25), (2.775, 2.275), `extent` their sum times `s`, and `bbox` `(0, -depth s, 0.45 s, height s)` for each link; `[` has no chain and `bbox` `(0, 0, 0.45 s, 1.0 s)`; `italic_correction` `0.1 s` for `f` and `0.15 s` for `V` |

Fake parameters at integer size `n = int(s)`, `//` flooring: `math_x_height`
`n//2`, `math_quad` `n`, `num1` `7n//10`, `num2` `4n//10`, `num3` `45n//100`,
`denom1` `7n//10`, `denom2` `35n//100`, `sup1` `4n//10`, `sup2` `36n//100`,
`sup3` `29n//100`, `sub1` `15n//100`, `sub2` `25n//100`, `sup_drop`
`25n//100`, `sub_drop` `20n//100`, `delim1` `24n//10`, `delim2` `n`,
`axis_height` `n//4`, `default_rule_thickness` `n//20`, `big_op_spacing1..5`
`n//5`, `n//6`, `n//9`, `6n//10`, `n//10`, `radical_rule_thickness` `n//20`,
`radical_vertical_gap` `n//20 + (n//20)//4`,
`radical_display_style_vertical_gap` `n//20 + (n//2)//4`, `script_scale` 0.7,
`script_script_scale` 0.5, `display_operator_min_height` `13n//10`. At base
size 180 one math unit is exactly 10; at 18000, exactly 1000.

The `chained` italic corrections are this script's addition to the reference's
fake, which has none: without one no fake case reaches the kern after a slanted
character.

| `text` | how a key that is not a math key resolves |
|---|---|
| `real` | `advtext.style.fontlib_metrics(key)` |
| `fake` | the reference tests' `FakeProse` in its `Latin` variant: `space_width` `s/4`; `advance` `s/2`; `bbox` `(0, -0.2 s, 0.45 s, 0.7 s)`; `kern` `-s/10` for the pair `A`,`V` and 0 otherwise; `has(c)` is `c < 0x250` |
| `none` | there is no resolver: `math_fonts(..., text=None)` |
| `broken` | the resolver raises for every key |

## 5. Case kind `parse` (`mathtext.golden`)

    source <text:string>
    formula
    mlist ... endmlist
      or
    refusal <kind:RefusalKind> <position> <byte_position> <reason:string>

`parse(text)` and `parse(text, display=True)` give the same list or the same
refusal (asserted for every case), so `display` is not recorded; the resulting
`Formula.style` is `text` and `display` respectively.

`position` is the reference's: an offset into the source **in codepoints**.
`byte_position` is the same place as an offset into the source's UTF-8
encoding. A port reports one or both; they differ in every case with a
non-ASCII character before the offending one.

The corpus, by case-name prefix: `page-reference/`, `page-refused/` (the
reference's demo page), `gallery/`, `demo` (every formula it finds in its
`demo/math.md`), `accepted`, `refused` (the reference's test table and the
branches it does not name), `command/supported/<name>` and
`command/refused/<name>` (one per command the parser knows),
`character/…` (every ASCII character alone and as a control symbol, every
classified character, every delimiter after `\left` and `\right`, the three
alphabets), and deterministic fuzz: `soup` (1,500 token strings), `grown` (300
well-formed nestings), `characters` (400 strings of arbitrary codepoints below
U+3000) and `words` (200 of the same inside `\text{}`). After the fuzz, and last
because every case name carries a running number: `braced` (11 sources where a
braced group stands where an atom is expected, which is where `tex.web` §1186's
"one bare ordinary atom" decides the list).

The first case, `tables`, lists what the parser knows:

    supported <name:string>
    refused_command <name:string> <kind:RefusalKind> <reason-tail:string>
    sized_delimiter <written:string> <codepoint>           -1 is the null delimiter
    refusal_kind <kind>
    prose_command <name:string> <face:string>
    const max_depth int 24

The reason for a refused command is `\<name> <reason-tail>`.

### What a port has to reproduce that no record shows

- **`reason` strings are compared whole, and some contain Python's `repr()` of
  a character.** `the character '€' (U+20AC) is not in the subset` is
  `f"{character!r}"`: the character between single quotes if it is
  `str.isprintable()`, and otherwise `'\xNN'`, `'\uNNNN'` or `'\UNNNNNNNN'`
  in lower-case hex, by magnitude (`'\x00'`, `'\x9f'`, `'\xad'`,
  `'\u200b'`, `'\U0010ffff'` all occur). `repr` would also write `\t` `\n` `\r` by name,
  double a backslash and switch to double quotes around `'`, but none of those
  characters can reach this message: the first three are blanks, and the other
  two mean something to the parser. The `message` of `missing-glyph` is built
  the same way.
- **`typeset_refusal` for `layout` contains `{needed / largest:.2f}`**: a
  double division, formatted to two places with correct rounding, as C's
  `%.2f` does.
- **Blanks are `str.isspace()`**, not ASCII white space: U+00A0, U+1680,
  U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000, U+0085 and
  0x1C–0x1F are skipped between atoms and collapse inside `\text{}`.
  `unicode.golden` has the set.
- **`\text{}` refuses what is not `str.isprintable()`**, and `\mathrm{}`
  takes only what is `' ' <= c <= '~'` or a blank.
- A command name is ASCII letters only; a letter is `isascii() and isalpha()`.
- A Python string can hold a lone surrogate and a `char*` cannot. No case has
  one; what a port does with malformed UTF-8 is its own decision.

## 6. Case kinds `inline` and `linebreak` (`inline.golden`)

Each source is one paragraph of text and dollar signs, converted by
`advtext.markdown.build_document(source, Theme.build(), fontlib_metrics,
math=markdown_math())`. It gives two cases.

`case <name> inline`:

    source <text:string>
    segments <n>                                  the paragraph, left to right
      text <text:string>
      formula <display:bool> <source:string>      `$$…$$` in a line is display
      or, for a source that is a `$$` block:
    block <closed:bool> <source:string>
    spans <n>
      span <source:string> <display:bool> <unclosed:bool> <start> <end>
           <refused:bool> <position> <byte_position> <reason:string>     (one line)

`segments` is what the reference's inline rule made of the paragraph: text
that is not a formula (dollars that open nothing are in it, as text) and the
formulas between dollars. A source with no formula is one `text`; adjacent
`text` records do not occur. `spans` is the block's `FormulaSpan`s: `start` and
`end` are a half-open range of the paragraph's material — one node, an atom
box, for a formula that set; the characters of its source with its dollars, in
the code face, for one that was refused, with `reason` and `position` the
refusal's. An `unclosed` block was never offered to the math builder and its
`reason` says only that.

`case <name>/paragraph linebreak` is `FORMAT.md`'s `linebreak` case, written by
the same function as `core/paragraphs.golden`, with `source markdown-math
<text>`: the material, and that material broken at the widths named in its
runs. A set formula is in the material as a `box hlist` whose contents are
`mathon 0`, the formula's box, `mathoff 0` (`advtext.shipout.atom_box`), and
ships as one `atom` command followed by its glyphs.

**Which dollars open a formula** is `advtext.markdown._math_inline`, and its
docstring is the specification. In brief: the opener is not followed by white
space and not preceded by a letter or digit (`str.isalnum()`); the closer is
the **first** later marker that is not backslash-escaped, and it closes only
if it is not preceded by white space and not followed by a letter, a digit,
`{` or `(`; if that first candidate fails, the opener opens nothing — the
search does not go on. `$$` is tried before `$`.

Not covered, because the port has no markdown and these are markdown's: `\$`
as an escaped dollar, a dollar inside a code span, emphasis or a link around a
formula, a formula across a line break in the source, and `more_may_follow`
(a stream's unfinished tail). No source here contains a backslash outside a
formula, a backtick, `*`, `[`, `<` or `&`.

## 7. `style.golden` (`table` cases)

Independent records; inputs first, answer last.

    font_key <face:string> <size> <key:string>
    parse_font_key <key:string> ok <face:string> <size>
    parse_font_key <key:string> error

`parse_font_key` splits at the **last** `@` and reads the size with Python's
`int()`, which accepts a sign, surrounding white space, `_` between digits and
any Unicode decimal digits (`prose@٣٤` is size 34); whether a port accepts
those is its decision, and these records say what the reference does.

    style <style> <value> <cramped> <sup> <sub> <num> <denom> <size:MathSize>
          <is_display:bool> <is_cramped:bool>
    mathsize <size> <value>
    atomtype <kind> <spacing_class>               0..7; -1 for `space`, which has none
    limits <limits>
    const math_spacing string <64 characters>
    spacing <left:AtomType> <right:AtomType> <code:string>    code is one of 0 1 2 3 4 *
    muglue_const <name> <width> <stretch> <shrink>
    const <name> int|float <value>
    half <value> <result>                         TeX's half(): (v+1)//2 if odd else v//2, flooring
    from_mu <width> <stretch> <shrink> <mu> <width> <stretch> <shrink>

    space_factor_after <codepoint> <current> <result>
    interword_glue <space_width> <space_factor> <spacing:Spacing> <width> <stretch> <shrink>
    strut_filling <leading> <height> <depth>

`space_factor_after` treats as upper case whatever `str.isupper()` does
(`unicode.golden`), not A–Z. `interword_glue` is `round(w * 0.5)` and `round(w
* (1/3))`, then for `nonfrench` with a factor other than 1000
`round(stretch * sf / 1000)` and `round(shrink * 1000 / sf)` in true division,
and `round(w * (1/3))` more width at a factor of 2000 or above; every `round`
is half-to-even.

    face_metrics <key:string> <size:float> <ascent:float> <descent:float>
                 <x_height:float> <em:float> <space_width:float>
    has <key:string> <codepoint> <bool>
    measure <key:string> <codepoint> <width> <height> <depth>
    kern <key:string> <codepoint> <following> <kern:float>

`fontlib_metrics(key)` for each of the seven faces at 13, 16, 17.5 and 34 px
(96 px per inch); `has`, `measure` and `kern` at 16 px only, for printable
ASCII and a few others, U+E000 and U+FFFD among them. The bulk of
`char_measure` is in `tests/golden/font/<role>.charmeasure.golden`.

    param_fields 27 <name>...
    math_fonts <base_size> text <size> <key> script <size> <key> script_script <size> <key>
    params <key:string> <27 floats>
    mu <base_size> <style> <mu>                   round(math_quad / 18) at that style's size
    fake_math_fonts ... / fake_mu ...             the same on the `fake` face

## 8. `unicode.golden` (`ranges` case)

    unicode_version <string>
    predicate <name> <ranges> <codepoints>
      range <first> <last>                        inclusive; `ranges` of them follow

For `isspace`, `isprintable`, `isalnum`, `isalpha`, `isupper` (the `str`
methods on a one-character string) and the general categories `category_cc`,
`category_cf`, `category_mn`, `category_me`, every codepoint 0..0x10FFFF for
which the predicate is true. Unicode 14.0.0, which is what the reference's
interpreter (Python 3.11) carries; a table generated from another Unicode
version differs, and the difference is then the version.

The ranges are derived from the Unicode Character Database (copyright Unicode,
Inc., Unicode License, <https://www.unicode.org/license.txt>), and the file's
header says so. A table a port generates from the same source needs the same
notice.

## 9. What is not in here

- Sources `parse` refuses are not in `formulas.golden`; sources of 50,000
  repeats (the reference's hostile-size tests) are not anywhere — the longest
  source here is 2,800 characters. Their answers are a kind and a position.
- `markdown_math`'s memo, `typeset` with a resolver that raises, and a missing
  math face: host-side behaviour of the Python API.
- The draw backend. `cmds` here are `ship`'s, in scaled points.
- A font key whose size does not fit 64 bits.
