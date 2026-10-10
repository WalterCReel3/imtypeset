# Selection goldens: `tests/golden/selection/*.golden`

Written by `tools/oracle/dump_selection.py` from the Python reference
(`../imgui-adv-text/advtext/selection.py`). Regenerate, from the repository
root, with

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
        ../imgui-adv-text/.venv/bin/python -B tools/oracle/dump_selection.py

Two runs write identical bytes. `dump_selection.py --verify` re-reads every
file with a reader that uses only what this document and `FORMAT.md` say,
rebuilds the model from the commands and asks it every recorded question again.

`dump_selection.py --emit-unicode-tables` prints the block of
`src/selection.cpp` between its `GENERATED` markers: the ranges of
`str.isalnum` and `str.isspace`, which word granularity classifies by.

`dump_selection.py` imports `dump_core.py` for the shared records.

## 1. What is shared with `FORMAT.md`

Its sections 1 (lexical layer), 4 (draw-command records: `cmds <n>` …
`endcmds`, with `glyph`, `rulerect`, `setglue`, `atom`, `line`) and 5 (file
header and `case <name> <kind>` … `endcase`). The header's file id is
`selection/<file stem>`. Every case here is of kind `selection`.

One enum is added: Granularity, `character` `word` `line`.

## 2. The files

| file | cases | what |
|---|---|---|
| `shapes.golden` | 41 | command lists built by hand: the shapes the reference's own tests build, the ones its docstrings argue about, and ones ship-out never produces |
| `shipped.golden` | 16 | command lists the reference shipped: a paragraph at four alignments and two widths, a markdown document (list, quotation, code block with a blank line, a table whose cells wrap, a rule, inline and display formulas) at two widths and at an origin far from zero, four formulas shipped as blocks of their own, a hand-built page |

The hand-built lists use one cell of 10 per character, lines 20 apart, height 8
and depth 2, as `tests/test_selection.py` in the reference does. They are
commands, not shipped boxes: coordinates are small integers and nothing about
them is a scaled point.

## 3. A case

    cmds <n> ... endcmds                          INPUT: the command list

then what `SelectionModel.from_commands(cmds)` is:

    model <length> <nfragments>
    fragment <line> <start> <end> <outer> <top> <bottom> <reach_top> <reach_bottom>
    indices <n> <index>{n}
    edges <n+1> <x>{n+1}
      ... `fragment`, `indices`, `edges` for each fragment, in the model's order ...

`line` is the position in `cmds`, counting from 0, of the fragment's `line`
command, which is its identity; `outer` is the position of the outermost `line`
it is nested in, or `-1`. `start` and `end` are that line's. `top` and `bottom`
are its band, `y - height` and `y + depth`; `reach_top` and `reach_bottom` are
its outer line's band, or its own. `indices` are the reading-order indices of
the items the fragment owns and `edges` their left edges followed by the line's
right edge (`x + width`).

A `line` command that is in no `fragment` record is a container (it holds only
other lines) or holds a rule and no item.

then what it answers:

    probes <n>
    probe <x> <y> <fragment> <hit> <word_start> <word_end> <line_start> <line_end>

For the point `(x, y)`: `fragment` is the ordinal, from 0, of
`fragment_at(point)` among the fragment records above, or `-1`; `hit` is
`hit(point)`; then `word_at(point)` and `line_at(point)` as ranges.

The points are, for the first, middle and last lines of a case: every
combination of a y just above, at the top of, in the middle of, on the last row
of and at the bottom of the line's band, with an x far left, either side of and
at the line's left and right edges, far right, and one below, at and one above
each edge and each midpoint of a sample of its items. Then, for every line, its
band's edges at three x, and the middle of the gap to the next line and one
either side of it. Then points above and below everything, and sixty scattered
over and around the whole. Thinned evenly to at most 260 (160 or 420 for some
shipped cases).

    selects <n>
    select <anchor> <active> <start> <end>        select(anchor, active); either may be
                                                  negative or past the end

    drags <n>
    drag <ax> <ay> <bx> <by> <granularity> <start> <end>
                                                  select_at(Point(ax, ay), Point(bx, by), g)

    ranges <n>
    range <start> <end> <nrects>                  rects(Selection(start, end))
    rect <x> <y> <width> <height>                 nrects of them, in order

A `range` whose `nrects` is `-1` is one for which the reference raised
`ValueError`: a line whose items' indices do not ascend, which
`Fragment.rect_for` cannot look up again. No `rect` follows it. Only the cases
`indices-out-of-order` and `lines-in-descending-index-order` have any. A port
must not fail there; what it answers is its own.

Ranges are `Selection(start, end)` as given, not passed through `select`: some
run backwards (`3 1`, which is empty) and some past the end.

    textdata <n>
    hard_breaks <k> <boundary>{k}
    separators <k> (<boundary> <text:string>){k}
    atoms <k> (<index> <text:string>){k}
    originals <k> (<index> <text:string>){k}
      ... those four records, n times: data set 0, 1, ... ...

    texts <n>
    text <start> <end> <data> <text:string>       text(Selection(start, end), **data set)

Each data set is what `text()` is given by the layers above it: `hard_breaks`
(a collection of boundaries), and three mappings, each written with its keys
ascending. Data set 0 is always empty: every break is a wrap. The others are
synthetic, chosen to reach every branch: every other line a hard break; every
line one; `"\n\n"`, `"\n"`, `"\t"` and `" "` as separators with every atom
named `$<index>$`; and one with separators that are empty, blank or `"--"`,
every other atom named, and a few glyphs written as something else, including
the empty string, a space and a newline. They are not what any document layer
would pass; `text` treats them as data either way.

Every range of the case is asked with every data set.

## 4. What a port has to reproduce that no record shows

- **Fragments are in order of `start`, and the sort is stable**: a blank line
  shares its start with the line after it and stays before it.
- **`fragment_at` is three rules, in order, with ties to the earlier
  fragment** (`advtext.selection.fragment_nearest`): among fragments whose
  reach contains y and whose `[x, x + width]` contains the point's x (both
  ends included), the least distance in y from its own band; else among those
  whose band contains y, the least distance in x; else the least
  `(max(top - y, y - bottom), distance in x)`, compared as a pair. A band is
  `top <= y < bottom`.
- **The midpoint test is `2 * x < left + right`**, in integers: the midpoint
  itself goes right.
- **An item's cell runs to the next item's left edge**, so a kern belongs to
  the item before it, and `x` is looked up by walking the edges in order: the
  first item whose right edge is beyond `x`. Edges need not ascend
  (`edges-going-backwards`).
- **Indices are looked up with Python's `bisect_left` and `bisect_right`**, and
  on a list that does not ascend the answer is whatever those two loops give
  (`indices-out-of-order`, `indices-repeating`). A port using a different
  binary search agrees on ascending lists only.
- **A `glyph` whose index equals the most recent `atom`'s is that atom's ink
  and is not an item.** The comparison is with the last atom seen anywhere
  earlier in the list, not only on the same line.
- **Fill is a `setglue` whose `natural` is 0. An `atom` is never fill.**
- **Word classes** are Python's: a glyph is a word character if
  `chr(c).isalnum()` or it is `_`, a space if `chr(c).isspace()`, and
  otherwise other; a `setglue` of either kind is a space; an `atom` is a class
  of its own. `tests/golden/math/unicode.golden` has both predicates as
  ranges.
- **`text` edits the tail of what it has written.** A wrap adds a space only
  if the last piece does not end in a space, newline or tab; any other
  separator first removes trailing pieces that are empty or all spaces, then
  trailing spaces of the last piece, and is dropped if nothing is left; and
  at the end everything written after the last character is dropped — except
  that the count is of *pieces*, so a separator that replaced removed pieces
  can survive at the end (`text 0 10 4` of several cases ends in one).

## 5. What is not in here

- `advtext.ui.units.UnitModel`, the reference's model over independent layout
  units, and the separators it derives from a document's blocks.
- A codepoint that is not a Unicode scalar value: Python cannot `chr()` one
  past U+10FFFF and a golden cannot hold a lone surrogate.
- Boundaries or indices beyond 32 bits, and negative ranges.
