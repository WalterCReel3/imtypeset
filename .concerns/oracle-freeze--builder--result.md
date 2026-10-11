# oracle-freeze: result

Builder, 2026-10-10. Words used strictly: **RAN** (a command of mine, output
seen), **READ** (in a file or in git, cited), **NOT RUN**.

## 0. In one screen

- The oracle no longer reads `../imgui-adv-text`'s working tree. It is commit
  `32e395346551cac82c7a6dd6e9b6d8e47feeac8e` (tag `advtext-reference`),
  extracted read-only into `build/advtext-reference` by
  `tools/oracle/pin_reference.sh`, held to the tracked
  `tools/oracle/REFERENCE.lock`, and every dump script exits 2 against anything
  else. This was needed already: the sibling's working tree has its `advtext`
  core deleted and replaced (READ, `git status` there), and the reference
  interpreter's editable install points at it.
- **One premise of the task was false.** `30496e1` — the commit `PROVENANCE.md`
  named — does NOT have the tag's tree. `a3c589a` does. `30496e1` is three
  commits after the tag and differs in `docs/`, `scripts/bench_draw.py`,
  `tests/test_docs.py`, `tests/test_style.py`; nothing under `advtext/`,
  `demo/` or the one test file a dump reads. So the goldens are unaffected, and
  the re-run below shows it rather than assumes it.
- Every dump re-run from the pinned tree: **no record line differs anywhere.**
  Only `tests/golden/tables/*.golden`'s header comments changed (they name the
  commit). Font corpus: see section 2.
- New goldens, `tests/golden/doc-tables/` (2 files, 15 cases, 2.0 MB), for what
  the swap needs and no golden held: selection, hit tests and copy through
  cells and across a table's edges, find hits and links in cells, a table
  growing by rows. Format: `tools/oracle/FORMAT-doc-tables.md`. **No C++ test
  replays them; that is `tables-core`'s.**
- Absent from the reference, so requirements with no oracle: selection / copy
  across columns of a multi-column layout; find MARKS drawn in cells; balanced
  columns; a caret rectangle in a cell.

## 1. The pin

**How.** `tools/oracle/pin_reference.sh` runs
`git --no-optional-locks --git-dir=../imgui-adv-text/.git archive <commit> | tar -x`
into a staging directory under `build/`, checks it, stamps it
(`.pinned-commit`), makes the files read-only and renames it to
`build/advtext-reference`. Only `rev-parse` and `archive` touch the sibling's
repository: no worktree, no tag, no index refresh. `build/` is ignored.
`IMTYPESET_REFERENCE_GIT` names another git directory if the sibling moves.

**Which commit.** RAN `git rev-parse` there: `advtext-reference` =
`32e395346551cac82c7a6dd6e9b6d8e47feeac8e`, tree
`7a591a485818c85fd6c6c43df75271a477c9d612`, 179 files. The script pins the
COMMIT and checks the tag against it: a moved tag is refused with a message, a
missing tag is a note (the commit still resolves).

| commit | tree | same as the pin? |
|---|---|---|
| `32e3953` (tag) | `7a591a48…` | — |
| `a3c589a` | `7a591a48…` | yes (RAN `git diff --stat`: empty) |
| `30496e1` (sibling HEAD; named in the old PROVENANCE and the old tables headers) | `40183e2a…` | **no**: 7 files, +558 −90, none read by a dump (RAN `git diff --stat 32e3953 30496e1 -- advtext demo tests/test_stream_stability.py`: empty) |

**`tools/oracle/REFERENCE.lock`** (tracked): tag, commit, tree, file count, and
one SHA-256 over the listing `<sha256>  <path>` of every file. With it a pinned
tree can be verified with no access to the sibling at all, and an extraction
that does not reproduce it is refused.

**How a dump refuses the wrong tree.** `tools/oracle/pinned_reference.py`,
`require()`, called by every `dump_*.py` that imports `advtext` (12 of 13;
`dump_font_synth.py` imports none), by `manifest.py` when it writes, and by
`tools/gen_mathtext_tables.py`, before the first `advtext` import. Exit 2 with
the correct invocation printed when: the directory or its stamp is missing; the
stamp's commit is not the lock's; the tree's digest is not the lock's (a file
added, removed or edited); `advtext` resolves anywhere but
`build/advtext-reference/advtext`; the interpreter was started without `-B`.
RAN, each exit 2 and nothing written:

| invocation | refusal |
|---|---|
| no `PYTHONPATH` | "`advtext` was imported from /home/wreel/Source/imgui-adv-text/advtext" — the venv has an editable install (`_editable_impl_advtext.pth`) of the sibling, so this is what a forgotten variable silently did before |
| `PYTHONPATH=../imgui-adv-text` | the same |
| no `-B` | "would write `__pycache__` into the pinned tree" |
| `manifest.py` (write) without the pin on the path | the same as the first |

NOT RUN: the refusals for an edited tree and for a wrong stamp (they are the
two digest comparisons; I did not corrupt the pinned directory to see them
fire), and `pin_reference.sh` against a moved or missing tag.
`manifest.py --check` needs no reference and still runs under any `python3`
(RAN).

**The interpreter is not pinned.** It stays
`../imgui-adv-text/.venv/bin/python -P -B`: a virtual environment is not
history, no commit holds it, and if that directory goes the oracle cannot run
whatever this task did. `PROVENANCE.md` records Python 3.11.2, imgui_bundle
1.92.900, fontTools 4.66.0, markdown-it-py 4.2.0, and the fonts the reference
loads come from that environment's imgui_bundle. The invocation is now

    tools/oracle/pin_reference.sh
    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference \
        ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/<script>

and is what `CLAUDE.md`, `README.md`, every `tools/oracle/FORMAT*.md`, every
script's docstring, `manifest.py`'s `INVOCATION` and `PROVENANCE.md` say.
`docs/performance.md` still shows the old one: it is a record of how a past
timing was run, and I left it.

**If `build/` is deleted** (BOARD rule 5 invites it): run `pin_reference.sh`
again. That needs the commit to still be in the sibling's object store. The
tag keeps it reachable; nothing in this repository does.

## 2. Each dump, re-run from the pinned tree

One at a time, `( ulimit -v 6000000; timeout 900 … )`, writing into
`tests/golden` in place, compared with `git status` / `git diff`.

| script | directory | result |
|---|---|---|
| `dump_core.py` | `core` (7) | byte-identical |
| `dump_para.py` | `para` (8) | byte-identical |
| `dump_para_map.py` | `para-map` (1) | byte-identical |
| `dump_selection.py` | `selection` (2) | byte-identical |
| `dump_blocks.py` | `blocks` (8) | byte-identical |
| `dump_tables.py` | `tables` (6) | **header comments only**: each file's lines 3–5 (`Reference: … commit 30496e1…`, `working tree: dirty in 1 paths`, `- CLAUDE.md (??)`) became two (`commit 32e3953…`, `Reference tree: tag advtext-reference, extracted … verified against REFERENCE.lock (179 files)`). 12 insertions, 18 deletions over 6 files; `git diff -U0` filtered to non-comment lines: **0** |
| `dump_doc_extras.py` | `doc` (4) | byte-identical — twice: before and after I split its `emit_blocks` into reusable pieces for the new script |
| `dump_draw.py` | `draw` (3) | byte-identical |
| `dump_font.py` | `font` (23) | byte-identical |
| `dump_font_synth.py` | `font-synth` (1) | byte-identical |
| `dump_kern_legacy.py` | `font-wild-kern` (1) | byte-identical (2,991 corpus faces, 81 with a kern table) |
| `dump_math.py` | `math` (6) | byte-identical |
| `dump_font_wild.py` | `font-wild` (87) | byte-identical. Run alone, `--jobs 4`, 2,991 faces, 905 s, exit 0; all 87 files rewritten (mtimes) and `git status` clean for the directory. MemAvailable fell by about 3 GB while it ran (11.1 → 8.0 GB at the lowest I read) |
| `gen_mathtext_tables.py --check` | `src/mathtext_tables.inc` | up to date |

`dump_tables.py` no longer runs git against the sibling: its header comes from
the lock, which `require()` has just verified against the tree in use — a
stronger statement than `rev-parse HEAD` of a working tree with uncommitted
changes was.

The manifest: regenerated after all of the above (RAN `manifest.py`, then `manifest.py --check` under the system `python3`: "159 files match"). 157 → 159 files: the two new ones, and the six `tables` hashes changed with their headers. `PROVENANCE.md` now names the tag, commit, tree and digest from the lock in place of `git rev-parse HEAD` and the sibling's dirty-file list.

## 3. Coverage: what the table and column work will be held to

| need | golden | status |
|---|---|---|
| table widths: natural, floors, apportioned / squeezed / floors / rigid, stated widths and shares | `tables/demo.golden`, `shapes.golden`, `widths.golden`: `limits`, `solve`, `layout` | existed; re-dumped identical |
| a row that fits, a row that wraps, the rule, resumed depth | the same, `layout` | existed |
| columns: `vert_break`, `prune_top`, `vsplit`, `split_into_columns` | `tables/vertbreak.golden`, `documents.golden` (`vsplit`), `columns.golden` (`columns`) | existed |
| a table among other blocks: units, extents, commands | `doc-tables/selection.golden`: `unit`, `bspan`, `cmds` | **new** |
| every index of a table as (block, row, column, byte) | `tmap` | **new** (the position is the script's definition; the reference has none) |
| column edges, row tops, cell rectangles | `tgeom`, `trow`, `tcell` + `rect` | **new** (read off the reference's row boxes and `line` commands; `tgeom` asserted equal to `table_columns`) |
| selection rectangles through cells | `range` … `rects`, `tcell` rects | **new** |
| hit tests in cells, on column edges, in the gaps between rows, beside the table | `probe` | **new** |
| drags cell to cell, down a column, corner to corner, at character / word / line | `drag` | **new** |
| COPY through cells: `\t` between cells, `\n` between rows, an empty cell keeps its tab, a wrapped cell joins with spaces, row-major whatever was dragged | `sep`, `range` + `copy`, `blocktext` | **new** |
| copy across blocks where one is a table (paragraph → table → paragraph; a table in a list item; two tables in a row; a table first and last in the document) | `range` + `copy` in cases `small`, `edges`, `in-a-list`, `demo-sample`, `demo-tables`; `lead` | **new** |
| a formula and a replaced character in a cell, in a copy | `atomtext`, `original` | **new** |
| find through cells: each cell searched alone, row-major, with rectangles | `found`, `match`, `rect` | **new** |
| find MARKS drawn through cells | — | **absent from the reference**: a colour span on a table is kept and not applied (READ `bare_view._decorate_unit`; RAN, asserted by the script). No oracle |
| links in cells: ink ranges, underlines, link areas, a link wrapping inside a cell | `decoinks`, `fills`, `linkarea`, `lrun` | **new** |
| a table that grows by rows — which widths move | `doc-tables/stream.golden`: `ttable` (`moved`, pieces, wrapped rows), `tnatural`, `tstated`, `tset`, `built`, `unit` | **new**. `doc/stream.golden` had one such case (`sample-with-table/by-line`) with unit digests only |
| text arriving inside the last cell; a table committing in the sync it arrived in; RESET against KEEP_STREAMED | cases `growing/9-chars`, `table-and-after-in-one`, `first-row-then-rest-with-after`, `by-row-reset` | **new** |
| selection and copy in reading order ACROSS COLUMNS | — | **absent from the reference**: `advtext.columns` returns boxes, the Columns page draws them, no selection model is ever built over them (READ `ui/pages/columns.py`, `ui/app.py`). `columns.golden` has each column's commands and `tests/golden/selection` the model over a command list; nothing in the reference composes them. No oracle |
| selection in a table with stated widths; the demo's column dragging | — | not dumped. Same code path as above with other widths; the pin makes it dumpable later |
| a long table in pieces under a static view; selection in a LIVE table; `carry_selection` | — | not dumped (`BareView` does not cut; only `StreamView` does) |
| balanced columns; a caret rectangle; `\span`, `\valign` | — | the reference has none |

Measured sizes: `selection.golden` 6 cases / 10 layouts / 31,657 lines;
`stream.golden` 9 cases / 11,876 lines. Two runs write identical bytes (RAN
`--check`).

What the new script cross-checks while dumping, so the records are not just a
transcript: every table index lies in exactly one cell box; `map_table` puts
each cell's material inside that cell's range; a table is set to
`table_columns`; each node's byte is read against the cell's text and asserted.
All held on every case.

### Against `../imgui-adv-text/.concerns/FS-S2--builder--result.md` (READ in full)

| its point | covered by |
|---|---|
| RQ-T1 cells, alignment, header | `tables/*` INPUT; `doc-tables` `table`/`cell` rows (text, runs, links, formulas) |
| RQ-T2 default widths | `tables/widths.golden`, `demo.golden` — as it says |
| RQ-T3 stated widths / shares | `tables/widths.golden` (`policy widths`, `policy fractions`). "Changeable cheaply" is cost, not an oracle matter |
| RQ-T4 limits askable | `tables/*` `limits` (`widest`, `floor`) |
| RQ-T5 wrapping, wrapped throughout, resumed depth | `tables/*` `layout`; in a document, `trow`'s `wrapped` and the commands |
| RQ-T6 rule and padding | `tables/*`; `decor`, `tabledecor` |
| RQ-T7 growth, holding widths | `doc-tables/stream.golden`. The rule a port must reproduce is in FORMAT-doc-tables.md §5: held at each column's widest natural width so far, used while it fits, else the default apportioning |
| RQ-T8 positions: hit test, select, range rects, range text | `tmap`, `probe`, `drag`, `range`/`rects`/`copy`. Separators are fixed in the reference (`\t`, `\n`), not host-stated. `caret_rect`: the reference has none. `Mark`: absent (above). `Tag` in a cell: `linkarea` rectangles |
| RQ-T9 lazy and windowed | no golden; culling is excluded from every doc golden |
| RQ-T10 geometry out | `tgeom`, `trow` — the script's reading of the boxes; the reference has no such call |
| RQ-C1 cut at a height | `tables/vertbreak.golden`, `documents.golden` |
| RQ-C2 a slice drawn, hit-tested, selected across slices | per-column commands in `columns.golden`; selection across slices **absent** |
| RQ-C3 balanced | absent; advtext never had it |
| §4 notes 1–4 (API) | not oracle matters; `swap-api-answers`' |

## 4. Text for item 2 of `docs/advtext-swap-status.md`

That file is held by another builder; this is the row and a paragraph to put
under it.

> | 2 | freeze the oracle at `advtext-reference` (32e3953); goldens for tables, columns, cross-block copy | **done** (task `oracle-freeze`). See "The oracle" below |
>
> ### The oracle (item 2)
>
> The core's goldens no longer depend on advtext's working tree or on which
> branch it has checked out. The reference is commit `32e3953` (your tag
> `advtext-reference`), taken with `git archive` from your repository's object
> store into `build/advtext-reference` here, and every dump script refuses to
> run against anything else — your working tree included, which is what the
> reference interpreter's editable install would otherwise import. Nothing was
> written into your repository. **Two things there the oracle still needs: the
> commit `32e3953` must stay reachable (keep the tag, or any branch containing
> it), and `.venv` must stay as it is** (Python 3.11.2, imgui_bundle 1.92.900,
> fontTools 4.66.0, markdown-it-py 4.2.0) — the interpreter is not something a
> commit holds. Deleting `advtext/`'s layout core from the working tree, on any
> branch, no longer affects the core.
>
> Every existing golden was dumped again from the pinned tree and no record
> changed. One correction to what was passed on: `a3c589a` has the tag's tree;
> `30496e1` does not (it adds the postmortem and revises `cpp-extension.md`;
> nothing under `advtext/`).
>
> New goldens for the swap, `tests/golden/doc-tables` (format:
> `tools/oracle/FORMAT-doc-tables.md`): a table among other blocks — every
> position as (block, row, column, byte in the cell's text), row and cell
> rectangles, hit tests, drags, selection rectangles and the exact copied
> string through cells and across the table's edges (`\t` between cells, `\n`
> between rows, row-major), find hits and links in cells, and a table arriving
> row by row with the widths held and the widths set at each step. Table
> layout and column cutting were already held by `tests/golden/tables`.
>
> What the reference does not do, and the core will therefore do without an
> oracle: selection and copy across the columns of a multi-column layout
> (advtext draws columns and never selects in them); find marks drawn inside
> cells (a colour span on a table is ignored; only a match's rectangles
> exist); balanced columns; a caret rectangle in a cell. FS-S2's RQ-T1 to T8,
> T10 and RQ-C1 have goldens; RQ-T9 (lazy), RQ-C2's selection across slices and
> RQ-C3 do not.

## 5. Files

New: `tools/oracle/pin_reference.sh`, `tools/oracle/pinned_reference.py`,
`tools/oracle/REFERENCE.lock`, `tools/oracle/dump_doc_tables.py`,
`tools/oracle/FORMAT-doc-tables.md`, `tests/golden/doc-tables/selection.golden`,
`tests/golden/doc-tables/stream.golden`.

Changed: every `tools/oracle/dump_*.py` (the `require()` call, the invocation in
the docstring, `REFERENCE` now the pinned tree); `dump_tables.py` (header from
the lock, no git); `dump_doc_extras.py` (`emit_blocks` split into `emit_theme`,
`emit_pieces`, `emit_spans`, `emit_block`; output unchanged); `manifest.py`
(the pin instead of the sibling's git; `doc-tables` in `WRITERS`);
`tools/gen_mathtext_tables.py`, `bench/reference_font_setup.py` (docstring),
`tools/oracle/gen_unicode_tables.py` (docstring); every
`tools/oracle/FORMAT*.md` (invocation, module paths; two "what is not in here"
entries now point at doc-tables); `CLAUDE.md`, `README.md`,
`docs/decisions.md` (D54); `tests/golden/tables/*.golden` (headers),
`tests/golden/MANIFEST.sha256`, `tests/golden/PROVENANCE.md`.

Kept: `build/advtext-reference` (the pin; do not remove with the other build
directories). Removed: `build/oracle-freeze` (my scratch).

Nothing was `git add`ed or committed. I ran `git add -N` on one new golden by
mistake and undid it with `git reset -q -- <file>`; the index is as it was.
No C++ was built or run.

## 6. Least sure of

1. **The new goldens have no independent reader.** Every other oracle script
   has a `--verify` that re-reads its file with a reader written from the
   FORMAT document. `dump_doc_tables.py --check` only shows two runs agree.
   The cross-checks it asserts while dumping are real, but FORMAT-doc-tables.md
   has been read against the output by me alone, by eye, on three of the six
   `tdoc` cases and two of the nine `tstream` ones. The first replay test
   (`tables-core`) is the first real test of that document; expect it to find
   a field I described loosely.
2. **(block, row, column, byte) and a cell's text are my definitions**, not
   the reference's and not yet the API's. If `tables-core` or
   `swap-api-answers` settles another shape (cells as sub-ranges of one block
   text, say), `tmap` has to be re-derived — cheap now that the pin exists,
   but the goldens as written assume this one. Recorded as D54.
3. **The queries are chosen by the script and thinned evenly**, so what they
   reach is what I thought of: no ragged table in a document, no table in a
   quote, no hard break in a cell, no selection in a live table, no stated
   widths. FORMAT-doc-tables.md §8 lists them.
4. `trow`'s top and height come from the `line` command the row box ships,
   and `tgeom`'s left from that line's x plus the table's indent. That is my
   reading of the row box (a kern of the indent, then a box per column),
   asserted on every row dumped, but it is the script's geometry, not an
   answer the reference gives a host.
5. **What still ties the oracle to the sibling**: its `.venv`, and the commit
   staying reachable there. I could not make either independent from inside
   this sandbox, and a 5 MB tarball of the reference in this tree is a
   licensing and scope question (D24) I did not take.
6. Not looked at beyond noticing: `../imgui-adv-text/docs/tex.web`, which
   CLAUDE.md's D23 rule leans on, is in NO commit of that repository (RAN
   `git ls-files --error-unmatch`, `git log --all -- docs/tex.web`: nothing),
   so the tag does not protect it and it is not in the pinned tree. The
   pinned `docs/` has `tex-notes.md`, `advance-rule.md`, `subpixel.md` and an
   OLDER `cpp-extension.md` than the one CLAUDE.md points at (`30496e1`
   revised it after the tag).
