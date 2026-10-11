# oracle-fixes: result

Builder, 2026-10-11. Words used strictly: **RAN** (a command of mine, output
seen), **READ** (in a file, cited), **NOT RUN**. No C++ was built or run.
Nothing was `git add`ed or committed; the three new files are untracked.

## 0. In one screen

- All four findings **held** when checked. None was already repaired.
- **No existing golden record changed.** `stream.golden` is byte-identical.
  `selection.golden` grew by three appended cases; its first 1,545,594 bytes
  are the old file. No correction to the script would have altered an old
  record, so nothing was withheld.
- The pin now refuses a reference module from outside the pinned tree **at
  the moment it is imported**, in every script, and the two digest walks
  treat symbolic links identically. Twenty-four refusals RAN.
- New: `tests/golden/specimen/specimen.golden`, the reference's specimen in
  integer scaled points, with its dump script and format document.
- Manifest: 160 files, `--check` passes.

## 1. Per finding

### F5 — the pin check (held; fixed)

Held, READ: `require_all_modules()` had one caller, `dump_doc_tables.py`,
after `write_text`. `tree_digest` used `rglob` + `is_file()`, which follows a
link to a file; `pin_reference.sh` used `find -type f`, which does not count
one.

Changed, `tools/oracle/pinned_reference.py`:

- `require()` puts a finder first on `sys.meta_path` (`_Guard`). For every
  `advtext` / `advtext.*` import it asks the finders after it where the
  module would come from and exits 2 if that is not under
  `build/advtext-reference/advtext`. So no line of an outside module ever
  runs, in any script that calls `require()` — all twelve dump scripts,
  `manifest.py`, `gen_mathtext_tables.py`.
- `require_all_modules()` stays, asks the same of `sys.modules` (a module put
  there by hand, or imported with the guard moved aside), and re-seats the
  guard. `require()` calls it once; `dump_doc_tables.py` and
  `dump_specimen.py` call it after everything is dumped in memory and
  **before** the first write.
- `tree_digest` walks with `os.walk(followlinks=False)`: a regular file is
  hashed as its bytes; a symbolic link (to a file, a directory, or nothing)
  is one entry hashed as `symlink\0<target>` and never followed.

Changed, `tools/oracle/pin_reference.sh`: `listing()` does the same, entry
for entry (`find \( -type f -o -type l \)`).

**A decision I took, for the lead to confirm or overturn.** The spec says
"call the all-modules check in EVERY dump script". The other eleven dump
scripts are not in this task's write set, and a call at the end of `main()`
is the defect, not the fix. The import guard gives every script the property
without editing them, and gives it earlier than a call before the write
would. I did not add the explicit call to the scripts I do not own.

The lock is unchanged: the pinned tree has no links, so both walks still
give 179 entries and `cae5c698…` (RAN, both; and a fresh extraction from a
scratch clone reproduced it).

RAN — `build/oracle-fixes/refusals.sh`, output in `refusals.out`, against a
scratch copy of the pin under `build/oracle-fixes/fake` (the pin itself was
never touched; the scratch copy is removed afterwards):

| | Python `require()` | `pin_reference.sh` |
|---|---|---|
| untouched copy | accepted | accepted |
| a file edited / added / removed | exit 2 ×3 | exit 1 ×3 |
| stamp names another commit | exit 2 | exit 1 |
| no stamp | exit 2 | exit 1 |
| a link to a file / to a directory / dangling, added | exit 2 ×3 | exit 1 ×3 |
| a file replaced by a link to the same bytes | exit 2 | exit 1 |
| started without `-B` | exit 2 | — |
| no `PYTHONPATH`; `PYTHONPATH=` the sibling | exit 2 ×2 | — |
| a submodule found outside the pin, at import | exit 2, before the module ran | — |
| a module put in `sys.modules` by hand | exit 2 (`require_all_modules`) | — |
| guard removed, outside module imported, then `require_all_modules` | exit 2 | — |
| the 24 reference modules a dump loads | accepted | — |

On a tree with three links both walks print the same digest (`82beafa3…`,
182 entries). `pin_reference.sh` against a scratch bare clone of the sibling
(RAN; clone deleted): fresh extraction reproduces the lock and leaves no
writable file; tag missing is a note and pins by commit; tag moved is
refused; an extraction that does not reproduce the lock is refused and
leaves nothing in `build/`.

Every other script still runs through the guard: RAN `--verify` of
`dump_core`, `dump_para`, `dump_selection`, `dump_blocks`, `dump_tables`,
`dump_doc_extras`, `dump_math` and `gen_mathtext_tables.py --check`, all exit
0, `git status` of `tests/golden` and `src` unchanged by them. **NOT RUN**:
`dump_draw`, `dump_font`, `dump_para_map`, `dump_kern_legacy`,
`dump_font_synth`, `dump_font_wild` — they have no read-only mode and would
rewrite goldens in place under `tables-core`.

### F6 and F1's documentation half — `FORMAT-doc-tables.md` (held; corrected)

Each claim checked against the golden and the reference before the text
changed:

| claim | check | result |
|---|---|---|
| §3.2 "an empty cell keeps its tab" over-claims | `range 148 171` copies `vpack\tstacks $x^2$ lines`; `range 171 190` copies `empty first\tlast`; `blocktext 1` has `…lines\t\n\tempty first\tlast`. READ `advtext/selection.py` `text`: "a separator is written only between two pieces of selected text" | held. Kept inside a selection, dropped at its edge. §3.2 now says both, with these examples |
| `lead` order | tag order of a case, RAN: `size unit bspan lead cmds imap/tmap unmapped tables … seps` | held. §3 was right, §3.2's "`lead`, then" was not; removed |
| §1 asserts less than it says | READ the script: nothing asserted a cell box owns an index, nor that an empty cell's indices have no byte | held. Both are now asserted (they hold on every old and new case), and §1 says what is and is not asserted |
| `stable` has no per-row cell count | RAN a probe over all nine stream cases and a ragged markdown source: markdown-it gives every row the header's count (`[3, 3, 3, 3]` for rows of 1, 4 and 2 cells) | held, and harmless: documented, with the rule a reader needs, and asserted per step. **No new record tag** — it would have put new lines inside existing cases |
| `STRASSE`/`straße` cannot tell "as it stands" from lowercasing | READ `FindIndex.find` | held. New case with needle `STRA` |
| no header-less table | — | held. New case |
| a caret's `<pos>` and every `<span>` are the script's | READ `TMap.pos`, `TMap.span` | held. New §0 |

What the document has now: **§0 "Whose answer each field is"** — two tables,
every record's fields sorted into the reference's own return values (with
the call) and the script's arithmetic (with how), and a paragraph on a
caret: `<pos>` is the item *after* the boundary, a forward resolution, and
these files are not an oracle for a caret convention. §1 (what is asserted);
§2 (`dropheader`); §3.2; §3.3 (the four reference behaviours that read like
defects, each with its record); §4 (the `ignore_case` rule and a table of
what `find-as-it-stands` shows); §5 (`stable`: which `cell` group is
current; no ragged rows); §6; §8; a new §9 listing the cases in file order.

One correction of mine to the findings file: of the six probes it cites at
`y = 5782215` (lines 547–552) only the first four have x in column 0; column
0 ends at 2891569, so the last two are on and past the edge and hitting
column 1 there is unremarkable. The document cites four.

### F6 — the added cases, `tests/golden/doc-tables/selection.golden`

| case | layouts | what |
|---|---|---|
| `no-header` | 240, 700 px | new INPUT row `dropheader`: every table's header removed in memory after the markdown is built (`dataclasses.replace(block, header=())`). `table 1 3 3 0`; row 0 is a body row; needle `Name` finds 0. At 240 px row 1 wraps with an empty last cell |
| `wrapped-empty-cell` | 260, 420 px | rows 1–4 wrap with an empty cell first, middle, last, and two at once; row 5 unwrapped with an empty middle cell |
| `find-as-it-stands` | 520 px | needles `STRA`, `stra`, `STRASSE`, `straße`, `ß`, `SS` with `ignore_case`, and `STRA` without |

### F4 — the specimen (held; golden added)

Held, computed: one scaled point is 2.03e-5 px at 96 px per inch, five times
finer than the test's 1e-4. The eleven typed-in numbers in
`tests/test_demo.cpp` all agree with the reference to their four decimals
(RAN, compared with the dump), so the test was blind, not wrong.
`tests/test_demo.cpp` was not edited.

## 2. The evidence that old records are untouched

RAN, after the write, against a copy taken before any change
(`build/oracle-fixes/before/doc-tables/`):

    cmp before/stream.golden tests/golden/doc-tables/stream.golden          identical
    cmp -n 1545594 before/selection.golden tests/golden/…/selection.golden  identical
    head -c 1545594 new | sha256sum    6226acbdb3149bc1067add9e09e66fd6484ee99e5f12ddfb74ea34877f94c7b1
    sha256sum before/selection.golden  6226acbdb3149bc1067add9e09e66fd6484ee99e5f12ddfb74ea34877f94c7b1
    diff before new                    one hunk, 31657a31658,37991: 0 lines removed, 6,334 added

Before any script change, `dump_doc_tables.py --check` said both files
identical (RAN), so the baseline was the script's own output. After, `--check`
says identical again. The header comment lines are the same.

| file | before | after |
|---|---|---|
| `doc-tables/selection.golden` | 6 cases, 31,657 lines, sha `6226acbd…` | 9 cases, 37,991 lines, sha `10b9462c…` |
| `doc-tables/stream.golden` | 9 cases, 11,876 lines, sha `258d186f…` | the same |
| `specimen/specimen.golden` | — | 1 case, 585 lines, sha `65b7d78e…` |
| `MANIFEST.sha256` | 159 files | 160: one hash changed, one line added |

The lead was told by `post_concern` when `selection.golden` changed on disk.

## 3. The specimen golden, and how a C++ test should compare it

`tests/golden/specimen/specimen.golden`, format `tools/oracle/FORMAT-specimen.md`,
written by `tools/oracle/dump_specimen.py` (`--check`, and a `--verify` that
re-reads the file with a reader written from the format document: RAN, every
stated sum holds). One case, `reference`:

- INPUT: the default theme (96 px per inch), the three sizes (24, 44, 12 px)
  and the six ladder sizes each as pixels and as scaled points, the two
  texts, the six faces with the font file each loaded, the seven pairs.
- EXPECT, what `build_specimen` returned: 19 `row`s — `set_width` for each
  of 6 faces and 6 ladder rungs, and 7 `pair`s (`kern`, `kerned`,
  `unkerned`) — plus the boxes of the reference's own arrangement.
- 40 `set` groups: the reference's `_set` called again for each sample and,
  per pair, for the pair kerned, unkerned and each letter alone, with every
  character's width, every kern and every space's glue, so a width that
  disagrees can be traced to the glyph.

The numbers (scaled points): Regular 23357671, Bold 23709197, Italic
22904968, Bold Italic 23247245, Mono 25457548, Math 23653022; ladder
3892143, 4865179, 6081471, 7601839, 9502298, 11877877; `AV` kern −92217,
kerned 2705018, unkerned 2797235; `rt` kern +52998, kerned 1496663,
unkerned 1443665.

**How to compare (for `swap-fixes`, `tests/test_specimen.cpp`):**

1. Compare `Scaled` with `Scaled`, inside the library, with `CHECK_EQ`. Set
   the text in the face at the row's `<size>` integer and read the natural
   width of the line as scaled points.
2. **Do not go through `ImTypesetLine` or `CalcTextSize`.** From 256 px a
   `float` steps by 3.05e-5 px or more; a scaled point is 2.03e-5 px. Every
   face row is 464–516 px wide, so the public float cannot represent the
   difference the test is for, whatever the tolerance. This is the real
   content of F4: the exact check needs an internal entry point, not a
   tighter `CHECK_NEAR`.
3. Hold: each row's `set_width`; each pair's `kerned`; the two letters set
   apart and summed against `unkerned`; `kerned − unkerned` against `kern`
   with its sign.
4. Do not hold the port to `specimen`, `sectionbox`, `notebox`,
   `headerbox`, `rowbox`: nothing in the product reproduces the reference's
   arrangement of its chart.
5. Sizes: all nine convert to the same scaled-point integer whether the
   pixels are a double or first narrowed to a `float` (RAN). NOT checked:
   the demo computes its ladder by repeated `float` division from 16
   (`ladder[i + 1] / 1.25f`), which need not give the `float` nearest 10.24.

## 4. What the reference does that surprised me

1. **`ignore_case` is all-or-nothing per cell, and the fallback is
   case-SENSITIVE.** A cell with a sharp s is not folded at all, so with
   `ignore_case` on, `stra` does not find `Straße` and `straße` does not
   find `Straße`. A whole paragraph is one such unit: one ß anywhere in it
   makes every search in that paragraph case-sensitive. A user would call
   that a bug; the goldens hold the port to it.
2. `ß` with `ignore_case` matches the `ss` of `strasse` (the needle folds),
   and the match is two characters long for a one-character needle.
3. A row of three columns copied alone comes out as two fields when its
   first or last cell is empty; the same row inside a larger selection has
   all three. Paste into a spreadsheet shifts columns only in the first case.
4. An empty cell owns 1 or 2 indices in a row that did not wrap and 2 or 3
   in one that did. The count is not constant, so "an empty cell is one
   caret stop" is not something the reference supports.
5. A find match's material range can be longer than its text: `stra` in
   `strasse` is material 0–5, bytes 0–4. NOT investigated; my guess is a
   kern node inside the range, and I did not confirm it.
6. Markdown never produces a ragged table, in a document or a stream. The
   ragged layouts in `tests/golden/tables/shapes.golden` are hand-built;
   nothing in a document golden can reach that path.

## 5. Files

New: `tools/oracle/dump_specimen.py`, `tools/oracle/FORMAT-specimen.md`,
`tests/golden/specimen/specimen.golden`.

Changed: `tools/oracle/pinned_reference.py`, `tools/oracle/pin_reference.sh`,
`tools/oracle/dump_doc_tables.py`, `tools/oracle/FORMAT-doc-tables.md`,
`tools/oracle/manifest.py` (`specimen` in `WRITERS`),
`tests/golden/doc-tables/selection.golden`, `tests/golden/MANIFEST.sha256`,
`tests/golden/PROVENANCE.md`.

Kept: `build/advtext-reference` (untouched: `pin_reference.sh` says 179
files, matches the lock; no `__pycache__` in it). Scratch, all under
`build/oracle-fixes/`: `before/` (the goldens as they were), `refusals.sh`
and `refusals.out`, two trial drivers.

## 6. Least sure of

1. **The import guard instead of a call in every script** (section 1). It
   is the stronger check, but it is not what the spec's words ask for, and a
   reader looking for `require_all_modules()` in `dump_core.py` will not
   find it.
2. **`dropheader` builds a table the reference's converter never builds.**
   Layout, selection, copy and find all ran on it without an assertion
   failing, and `_table_rows` and `FindIndex.of` both branch on an empty
   header, so the reference means to support it. But the reference's own
   tests may never have exercised a header-less table through `BareView`,
   and I did not read them to find out.
3. `FORMAT-doc-tables.md` §0 sorts about forty fields into two tables. I
   checked each against the script, not against a replay. `tables-core` is
   still the first independent reader.
4. Whether the product's paragraph path sets `The quick brown fox…` to the
   same integer as the reference's `_set` is NOT known: `_set` tracks no
   space factor and adds nothing at the ends of the line. The pangram has no
   punctuation, so I expect agreement, and I ran nothing that shows it.
