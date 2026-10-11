# imtypeset in pptmstr and in advtext: a measured plan

For the two applications that need it, on the stock `imgui_bundle` wheel each already runs on:
`../pptmstr` (its own `.venv`, wheel 1.92.801) first, `../imgui-adv-text` (wheel 1.92.900) second.
Dear ImGui Bundle adopting the library upstream is one line of backlog (section 9) and nothing here
waits for it.

Everything was measured on 2026-10-10 in this tree; section 10 says how, with the commands. Numbers
are microseconds of the pane's own draw call unless they say otherwise, medians, on Xvfb + llvmpipe
with the machine at a load average of 4 to 12 (other builders were compiling). Read them as ratios.
Nothing was written into either application: both are imported read-only.

## Start here

The smallest change that puts imtypeset on screen in pptmstr is three files, and it has run in the
real application (`pptmstr.app.main --fake`) from copies.

```sh
cd ~/Source/imtypeset
# 1. the library, for the wheel in pptmstr's venv (about 30 s; stand-in headers, see 3.2)
IMTYPESET_BUNDLE_OUT=build/pptmstr-bundle sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
# 2. the change
cd ../pptmstr && git apply ../imtypeset/python/spike_bundle/pptmstr_overlay/pptmstr.patch
# 3. run it
IMTYPESET_LIBRARY=$HOME/Source/imtypeset/build/pptmstr-bundle/libimtypeset_bundle.so \
PYTHONPATH=$HOME/Source/imtypeset/python  make run        # CONTEXT -> "rich", or DETAIL on a question
```

The patch (750 lines added, 19 removed by `git apply --numstat`, of which 672 are not blank, the
figure `make_patch.sh` prints; `git apply --check` passes against `../pptmstr` at 4974657;
`sh python/spike_bundle/pptmstr_overlay/make_patch.sh` writes it again from the overlay files and
ends with that check):

| file | change |
|---|---|
| `pptmstr/ui/typeset.py` | NEW, 131 lines. `load_fonts()` imports imtypeset only if `IMTYPESET_LIBRARY` is set, makes its context, adds Inconsolata Medium (prose and code), the vendored Bold and the wheel's Latin Modern Math. `module()` is the imtypeset module or `None`; `reason()` says why not. Any failure, the import gate's included, means `None`. `trouble()` is the last failure a pane got over; both are logged once as a warning (`pptmstr.typeset`). |
| `pptmstr/theme.py` | `load_fonts()`: two lines at its end, `from .ui import typeset; typeset.load_fonts()`. After pptmstr's faces, so `Fonts[0]` is still Inconsolata. |
| `pptmstr/ui/rich_pane.py` | `draw()` asks `typeset.module()`. `None`: `_draw_classic()`, which is today's body, renamed, untouched. Otherwise `_draw_typeset()`: the blocks are described into ONE `imtypeset.Document` kept on `RichState` when they change (`_sync`: one library call per change), and a frame is `layout` + `draw` + a hit test while the mouse is over it. Exactly one renderer draws a frame: `_draw_typeset` never raises, ends in a `finally` whatever it began in ImGui (the table's child, the menu's popup; `_draw_table`'s own table and popup too), and answers whether the frame is its own. It is not only when it failed BEFORE drawing (describing, layout, marks), and then the classic renderer draws that frame; a failure after drawing began leaves the frame as far as it got. Either way the failure is in `typeset.trouble()` and the document is described again next frame; three failing frames in a row in one pane switch the library off for the run, with the reason in `typeset.reason()`. A copy that fails is noted and nothing else. `RichState` gains `find`, `typeset` and `typeset_failures`. |

Without the environment variable nothing is imported and pptmstr is what it is today. With it, the
same call `rich_pane.draw(rich, blocks, live)` serves its three callers unchanged:
`transcript_pane._draw_rich`, `detail._question`, `detail._deliverable`.

**What I ran, exactly.** I cannot write into `../pptmstr`, so the same three files sit in
`python/spike_bundle/pptmstr_overlay/` with a `sitecustomize.py` that makes the real application
import them (`pptmstr.theme`, `pptmstr.ui.rich_pane`, `pptmstr.ui.typeset`; every other module is
pptmstr's own):

```sh
PYTHONPATH=$PWD/python/spike_bundle/pptmstr_overlay:$PWD/python \
IMTYPESET_LIBRARY=$PWD/build/advtext-rework-plan-pptmstr/libimtypeset_bundle.so \
XDG_CONFIG_HOME=$PWD/build/advtext-rework-plan-run/cfg SPIKE_EXTRA_SITE=<a site-packages with numpy> \
sh tools/shot.sh --screen 1600x1000x24 -- ../pptmstr/.venv/bin/python -P -B \
    python/spike_bundle/pptmstr_app_shot.py --out app.png --frames 240 --rich --focus CONTEXT -- --fake
```

It reported `pptmstr.app` from `../pptmstr`, `rich_pane` from the overlay, `imtypeset: in use`, wheel
1.92.801, 3.02 library calls per `rich_pane.draw`; the photograph is
`python/spike_bundle/shots/pptmstr-app-imtypeset.png` beside `pptmstr-app-today.png` (looked at: the
same pane, the same face and colours, the text about 5 % narrower, see 5.1). A copy of pptmstr with
the patch applied passes its own gate: `mypy --strict` as configured, `ruff`, `black --check`, and
`pytest tests`: 2,569 passed, 3 xfailed, none failed (pptmstr at 4974657; at the commit this was
first written against, 2,567 passed and the same 2 failed as on the unpatched tree).

**What it does when something goes wrong, run:** `python/spike_bundle/pptmstr_patch_faults.py`
puts a fault into the pane at frame 30 in pptmstr's wheel and watches ImGui's current window
before and after `rich_pane.draw`, which renderers drew, and what the patch says:

```sh
IMTYPESET_LIBRARY=$PWD/build/pptmstr-bundle/libimtypeset_bundle.so \
sh tools/shot.sh -- ../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_patch_faults.py \
    --scenario table-raises    # or table-raises-once, table-cell-raises, menu-copy-raises,
                               #    key-copy-raises, describe-raises-once, draw-raises-once,
                               #    soft-standing, find, stream-find, stream-table
```

Eight fault scenarios (`none` among them), one for an outcome that is not a fault, and three for
find marks and streaming tables; each exits 0 when its verdict holds. `soft-standing` describes a
paragraph with its runs out of order, which the library applies and reports as `DescriptionCut`:
`Document.draw` then answers False in every frame the block is in view (70 of 70), and none of
them counts as a failing frame: the library stays in use and the classic renderer draws nothing. With the patch as first written (before these guards) a raising table left the
window `##table_2` open, drew the frame twice and ended the application on ImGui's "Must call
EndChild() and not End()!".

**Fonts are loaded once.** hello_imgui at the commits both wheels pin calls
`load_additional_fonts` once, at start-up, and never again by itself (there is no reload on a
DPI change in 1.92: fonts are scaled at draw time). An application that assigns the callback a
second time gets a second call; `tools/bundle_route/font_lifetime.py` runs that, with and without
the atlas emptied first, in both wheels.

**The check that it is on:** drag across two blocks in the pane and they highlight; a right-click
then offers "copy selection". If it is not, `pptmstr.ui.typeset.reason()` holds the sentence.

---

## 1. What the applications really show

### 1.1 pptmstr

`python/spike_bundle/pptmstr_census.py` replays Claude Code session logs through the rules of
`pptmstr/driver.py` (thinking is REASONING, text is OUTPUT, a tool use is `Name(compact args)`, a
result its first 300 characters) into pptmstr's own `Transcript`, `NodeTranscript` and `BlockCursor`.
The corpus is the 46 logs of this project written before 06:15 that day (the sessions pptmstr itself
drove here, sub-agents included), read in place, nothing copied.

| per transcript | median | p90 | largest |
|---|---|---|---|
| bytes | 78,968 | 190,139 | 526,229 |
| lines | 850 | 2,010 | 5,639 |
| blocks | 211 | 431 | 1,441 |
| blocks inside RICH mode's 400-line window | 124 | 147 | 294 |
| the turn's prose (DETAIL) | 6.1 KB | 8.5 KB | 40.5 KB |

Of 47,701 lines, 5,066 are OUTPUT; the rest are tool results (16,904), reasoning (14,526), tool
calls (7,322), system (3,577), errors (306). So 70 % of the 11,361 blocks are LITERAL, which pptmstr
draws as unwrapped lines. A second corpus, the 52 agent reports under `.concerns/` strung into one
transcript (5,701 lines, 1,713 blocks, 90 % OUTPUT), stands for the prose-heavy case: a sub-agent's
deliverable, which is what DETAIL renders.

"The sizes the application sees" are therefore: the median session (809 lines by rank), the largest
(5,639), each times ten, and a three-report deliverable (504 lines).

### 1.2 advtext

`demo/sample.md` (5,513 bytes, 37 blocks) once; five times on its Find page; twenty times in
`BareView`'s own benchmark note. Measured at 1, 5, 10, 20 and 100 copies.

## 2. What did not fit a block description

Counted by walking each application's block and inline kinds: `pptmstr_census.py` over both
pptmstr corpora (13,074 blocks), `python/spike_advtext/census.py` over `demo/*.md` (157 blocks).

### 2.1 pptmstr: nine block kinds, ten inline token types

| pptmstr | imtypeset | fits? | count (sessions / reports) |
|---|---|---|---|
| PARAGRAPH, HEADING, BULLET_ITEM, ORDERED_ITEM, BLOCKQUOTE | Paragraph, Heading, ListItem with a marker, `quote_depth` | yes | 3,364 / 1,440 |
| LITERAL (every non-OUTPUT kind) | Code block, coloured by segment kind, `CodeOverflow_Clip` | yes | 7,939 / 208 |
| THEMATIC_BREAK | Rule | yes | 13 / 0 |
| FENCE body | Code block | yes | 4 / 12 |
| FENCE chrome: the language label, "(fence not closed)" | an extra dim paragraph each | **as text a selection copies**, unlike today | 3 / 8 labels |
| FENCE "... N more lines" at 200 | not needed: lines are culled, the cap goes | n/a | 0 / 0 |
| **TABLE** | **nothing. A Host block reserves the rectangle and `_draw_table` (ImGui's table) draws into it** | **no** | **41 (344 rows) / 54 (455 rows)** |
| inline `text`, `code_inline`, `strong`, `em`, `softbreak`, `hardbreak` | text and runs (face, colour), `HardBreaks` | yes | 15,605 / 12,563 tokens |
| inline `link_open` | an underline mark; the URL stays in Python (`Run.tag` is there for it, unused) | yes | 0 / 0 |
| inline `html_inline`, `image` | their literal content, as today | yes | 0 / 7 |
| "N earlier blocks not shown" | not needed | n/a | |
| per-block context menu, hover latch | Python: one invisible button and one hit test | stays in Python | |

So **tables are the whole list**: 95 of 13,074 blocks (0.7 %), but one in four of the reports has
several. Three further things are not misfits and matter:

- **`$` in prose.** 12 of 1,440 prose blocks in the reports hold a dollar (3 hold two or more); none
  of the 3,364 in the session logs. With `BlockFlags.Math` set those 3 become formulas or refusals.
  The patch leaves Math OFF (`PPTMSTR_TYPESET_MATH=1` turns it on).
- **Tabs.** 320 LITERAL blocks in the session logs hold a tab. The tab-in-code-block range defect
  (section 7) is about exactly these.
- **Characters Inconsolata has no glyph for:** 16 distinct in the session logs (Greek capitals,
  Bengali, Arabic digits, U+2261). imtypeset stands in from the Math face and then U+FFFD; ImGui
  draws its fallback glyph. Not compared by eye.

### 2.2 advtext: four block types, the node kinds in their material

advtext's blocks hold nodes, so `python/spike_advtext/convert.py` walks material back into text:
18,821 `CharNode`s to characters with a run where the face or size changes, 4,193 glue to spaces,
1,185 kerns dropped (imtypeset kerns for itself), 3 penalties, 32 list markers, 4 links to
`Run.tag` plus a host table, and **26 formula boxes to explicit `RunFlags.Formula` runs holding
their source** (no dollars rescanned; 0 blocks reported an error). `TextBlock` (all 13 kinds),
`CodeBlock` and `RuleBlock` fit, with each block's whole `BlockStyle` carried as a
`ImTypesetBlockStyle` (4 to 9 distinct per document).

| did not fit | count over `demo/*.md` |
|---|---|
| **`TableBlock`** (a Host block the view must draw; nothing draws it in the spike) | **9 tables, 129 cells** |
| a code block's language string | 5 (advtext does not draw it either; kept by the host) |
| `ReplacedChar`, boxes that are not formulas, `Disc`, `MathOn/Off` in material | 0 met |

Not walked because `build_document` does not emit them (they are in `docs/advtext-migration.md`
2.1 and stand): a formula box built by a host's own `MathBuilder`, `HostBlock`, `DiffView`'s
positioned glyphs, columns.

## 3. The sidecar in each application

### 3.1 Built, located, gated

One library per interpreter, because the two venvs hold different wheels:

```sh
IMTYPESET_BUNDLE_OUT=build/pptmstr-bundle sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
IMTYPESET_BUNDLE_OUT=build/advtext-bundle sh python/host/build_bundle_library.sh        # ../imgui-adv-text/.venv
```

- **Located** by `IMTYPESET_LIBRARY` (the `.so`) and `PYTHONPATH=<imtypeset>/python` (the ctypes
  package). Nothing is installed into either venv. For pptmstr the natural homes are two lines in
  its `Makefile` `run` target; for advtext, its `Makefile` env block.
- **Gated** at `import imtypeset` (`python/imtypeset/_bundle.py`): the library must be linked for
  this interpreter's wheel, pass Dear ImGui's own version and layout check, and pass a probe of
  every ImGui field it reads. A refusal is an `ImportError` with the sentence. In pptmstr the patch
  catches it in `typeset.load_fonts()` and the application draws as before; `typeset.reason()`
  keeps the sentence (showing it once in the status bar is step P6).
- **Gated again at run time**: an exception in the imtypeset path of `rich_pane.draw` disables it
  for the run. This fired for real during the measurements (a streaming table, 6.2) and the pane
  kept drawing through the classic renderer.

### 3.2 The stand-in-headers caveat

The wheel ships no headers and its ImGui is a fork. The libraries above are compiled against
UPSTREAM Dear ImGui's docking tag of the same version and accepted by the field probe, not by Dear
ImGui's own check, which refuses one thing: `sizeof(ImGuiStyle)` (the fork adds a field; the
library reads one field of that struct, probed). `host_info()["bundle"]` says `stand-in:`.
`IMTYPESET_BUNDLE_STRICT=1` refuses stand-in libraries. The two commands that remove the caveat,
per wheel, in `../imgui_bundle` (network; never run here):

```sh
git submodule update --init external/imgui/imgui
git -C external/imgui/imgui checkout eb8df947f7e319de14ad383ad5c13c802d8163f6   # wheel 1.92.801, pptmstr
#                               or  c9a0a84f8a4ddae794a9e1acf6a93afce73d5057   # wheel 1.92.900, advtext
```

then the same build script, which labels the library `fork:<commit>`. That branch of the script
has never run (`.concerns/bundle-route--builder--result.md`).

### 3.3 On a wheel upgrade

`pip install -U imgui-bundle` in a venv makes the next `import imtypeset` fail at the gate (new
ImGui version string, or the same version and a different binary, re-probed in a child process).
pptmstr then runs as today's pptmstr and says why. The remedy is the 30 s rebuild, which needs
upstream's `v<version>-docking` tag in `../imgui` (`git fetch` there) or the fork at the new tag's
commit. Not run: an actual upgrade.

## 4. What the measurements say

### 4.1 pptmstr, CONTEXT in RICH mode: `transcript_pane.draw`, per frame

"Today" windows the history to its last 400 lines; the Document shows all of it.

| transcript | today | Document | |
|---|---|---|---|
| median session, 809 lines / 117 blocks | 2,514 | **516** | 4.9x |
| p90 session, 2,010 / 416 | 3,042 | **544** | 5.6x |
| largest session, 5,639 / 1,441 | 2,208 | **660** | 3.3x |
| median x10, 8,090 / 1,179 | 2,492 | **602** | 4.1x |
| largest x10, 56,390 / 14,419 (one run) | 2,572 | **1,268** | 2.0x |
| the reports (prose), 5,701 / 1,713 | 10,219 (p95 19,079) | **876** (p95 1,568) | 11.7x |
| three reports, 504 / 141: a deliverable | 12,347 (p95 21,884) | **586** (p95 752) | 21x |

The gain is where the prose is: today lays every visible paragraph out in Python every frame
(`span_layout.layout_inline`), 8,246 binding calls a frame on the reports.

**Where a frame goes** (median session; `--profile`, and the unprofiled median for the total):

| | today | Document |
|---|---|---|
| calls into the `imgui_bundle` binding | 2,856 | 33 (the controls row; 83 with a table in view) |
| time inside them | 1,155 | 67 |
| calls into imtypeset | 0 | 3 (`layout`, `draw`, `size`; +1 hit test under the mouse, +2 and one per table with tables in view) |
| time inside them (native work plus the ctypes boundary) | 0 | 243 |
| Python (the rest; derived by subtraction) | about 1,360 | about 210 |
| an empty call: binding / library | 31 to 42 ns | 230 to 270 ns |

What is left in Python per frame, by its own clock: building the "copy" button's text 84
(`windowed` + `block_copy_text`, clicked or not), `BlockCursor.live_block` 35 to 40, `_sync`'s
no-change check 2.3, `NodeTranscript.sync` 1.7 when nothing arrived.

**The other operations** (largest session unless said; today / Document):

| | today | Document |
|---|---|---|
| first frame, median session | 7,841 | 9,617 (parse 2,502 + describe and set 1,255) |
| first frame, largest | 51,727 | 61,435 (parse 33,933 + describe and set 10,369) |
| first frame, the reports | 134,586 | 184,607 (parse about 115,000 + describe and set about 55,000) |
| streaming, median session | 1,356 | 963 |
| streaming, largest | 2,233 | 1,228 |
| streaming prose (the reports' last three) | 10,105, p95 20,561, worst 53,110 | 1,040, p95 1,593, worst 2,207 |
| width changing every frame, median / largest / reports | 2,281 / 2,095 / 8,598 | 790 / 1,122 / 1,245 |
| scrolling by wheel | 2,026 over its 4,991 px window | 922 over all 98,000 px |
| scrolling, the reports | 10,641 | 915 |
| find: each keystroke | 2,088 to 4,348, and RICH falls back to filtered lines | 12,219 9,713 12,626 13,848 3,038 2,787 1,809 1,691 2,229 1,614 (2,000 marks at first), RICH kept |
| find: every frame after | 2,068 (the filter re-runs every frame) | 672 |
| drag-selecting across blocks | not possible | 761 (steady 735) |
| Ctrl+C of 1,577 selected characters | not possible | 897 for that frame |
| copy everything, 523,223 characters | 98 for the 400-line window | 15,243 once laid out; 45,000 to 119,000 if not (core request C4) |
| lay the whole document out once (`layout_window`) | n/a | 7,679 median session; 65,804 largest; 96,795 median x10 |

The first frame is SLOWER through the Document, by the description step (7 us a LITERAL block, about
30 us a prose block, all Python). It is paid once per node shown, not per frame.

### 4.2 pptmstr, CONTEXT in WRAP mode (the default mode)

Today: `text_wrapped` per line over the last 400 lines. The Document side is a prototype that
exists only in the harness (`lines_document` in `pptmstr_real_panes.py`): one Code block per run of
same-kind lines, `CodeOverflow_Wrap`, nothing windowed.

| | today | Document prototype | both, and the copy text built on click |
|---|---|---|---|
| median session | 1,566 | 601 | today 1,542 / **356** |
| largest session | 2,468 | 1,603 | today 1,927 / **368** |
| streaming, largest | 2,598 | 1,803 | not run |

At the largest session 1,000 to 1,280 us of every frame, in either column, is
`_visible()` + `copy_text()` building the "copy" button's string. That is the largest single
per-frame cost found in pptmstr and it has nothing to do with typesetting.

### 4.3 advtext: `BareView` / `StreamView` against one Document, `demo/sample.md`

| | today | Document |
|---|---|---|
| parse, first time (reads the faces) | 285,555 | the same call |
| parse again, x1 / x10 | 12,948 / 169,888 | the same call |
| of which markdown-it alone (`parse_cost.py`) | 3,688 / 34,287 | |
| convert material to descriptions, x1 / x10 | n/a | 2,924 / 25,693 |
| first frame, x1 / x10 | 142,390 / 152,828 | **4,840 / 28,152** (conversion included) |
| steady frame, x1 / x5 / x10 / x20 / x100 | 7,026 / 10,527 / 7,031 / 6,081 / not run | **167 / 165 / 171 / 165 / 192** |
| calls: binding / imtypeset | 3,747 / 0 (one `add_text` a glyph) | 6 / 3 |
| scrolling, x10 | 9,420, p95 17,403, worst 93,994 | **215**, p95 435, worst 812 |
| streaming, x1: draw + parse per frame | 11,058 + 1,147 | **413** + 881 + convert 192 |
| streaming, x10 | 14,875 + 1,214 | **517** + 823 + convert 204 |
| width changing every frame, x1 / x10 | 165,283 / 145,053 | **441 / 543** |
| find, x10: index, then each keystroke | 22,098 once; 9,412 to 74,618 | none; 18,094 8,346 3,612 3,380 1,061 1,023 707 1,015 |
| drag-selecting, x10 | 7,282 | **234** |
| select all and copy, x10 (52,768 characters) | 152,984 | **6,785** |
| `demo/math.md` steady | 6,480 | **189** |

Same line breaks in both panes on the photographed page
(`python/spike_advtext/shots/sample-today.png`, `sample-document.png`, looked at). The Document
pane's word gaps are visibly more even than advtext's own: that is the in-flight `word-gaps` change
in the library the sidecar was built from (section 7), not something to rely on yet.

### 4.4 What the typesetter does not remove, ranked by measured share

For advtext after the change (x10, per event): the parse 169,888 (of which 135,600 is building
node material that is then walked back to text, and 34,287 is markdown-it); conversion 25,693;
stream parse 823 to 1,214 per append; find 707 to 18,094 per keystroke (Python string search and
packing marks). For pptmstr: the copy text every frame (84 to 1,280); `BlockCursor.feed`'s inline
parse (33,933 once for the largest session); description (10,369 once); `NodeTranscript.sync`
while streaming (41 at the median session, 112 to 143 at the largest: it walks every segment).
Not measured here and unchanged by any of this: advtext's `difflib` and `DiffView`
(`docs/advtext-migration.md` 4 has the reference's own figures).

## 5. Behaviour changes a user will see

### 5.1 pptmstr

1. **Text is about 5 % narrower.** Measured: ImGui advances Inconsolata at size 16 by 8.01 px a
   character; imtypeset by 7.626 px. ImGui's "16" is ascent plus descent (1.049 em for this face),
   so the em is 15.25 px and the true advance 7.63; that ImGui rounds it to the pixel grid is
   inferred from the two numbers, not read in its source. More characters fit a line, and a column of code in the pane no longer lines up with
   ImGui-drawn monospace beside it. `Style(EmSize=16.0)` would give an 8.0 px advance and 5 %
   larger glyphs instead. Core request C6.
2. **Line breaks differ.** Ragged-right as today (`Align.Left`), but chosen by the library's
   breaker; a word is never split mid-row with a stray hyphen-like break; white space collapses
   across styled runs. Rows keep today's pitch (`LineHeight` 1.05 em is the 16 px row advance;
   that constant is Inconsolata's and is hard-coded, C6).
3. **Nothing is windowed.** RICH mode shows the whole history, not the last 400 lines; the
   "N earlier blocks not shown" row and the 200-line fence cap go.
4. **The scroll bar is an estimate until blocks have been seen** (section 7).
5. **Selection exists.** Drag selects across blocks (double click by word, triple by line), Ctrl+C
   and "copy selection" copy what is DISPLAYED (markers excluded, a blank line between blocks, a
   table's source whole if touched); "copy paragraph" still copies the verbatim markdown source.
6. **A fence's language label and "(fence not closed)" are selectable text.**
7. **Ordered markers sit right-aligned in a gutter** wide enough for them; today "12." overlaps
   its text. The bullet is drawn smaller (seen in the photograph; the marker is the font's U+2022).
8. **A table is the same ImGui table**, in a child the height of its block; for one to three
   frames after it first appears it is drawn at a guessed height (6.2).
9. **Math: none, unless switched on.** With `PPTMSTR_TYPESET_MATH=1` dollars are scanned; refused
   formulas show as their source. Not photographed in pptmstr.
10. **Justified text: off.** `PPTMSTR_TYPESET_JUSTIFY=1` exists in the patch; justified monospace
    was not looked at and is not recommended. Collapsed word gaps (section 7) therefore do not
    arise in pptmstr as shipped by this plan: ragged-right sets every space at its natural width.
11. **Fonts.** imtypeset adds its own copies of the faces to the atlas (Inconsolata twice over,
    Latin Modern Math); in `load_additional_fonts`, after pptmstr's, never in `post_init`
    (hello_imgui empties the atlas between the two). A DPI change mid-run does not call the
    callback again: hello_imgui at the commits these wheels pin calls it once (read, and run for
    60 frames; `tools/bundle_route/font_lifetime.py`).

### 5.2 advtext

As `docs/advtext-migration.md` section 4 lists from `docs/divergences.md` (first-line indent
applied once; white space collapsing across runs; the total-fit work budget; fixes toward
`tex.web` in formulas). Added by this spike: the nine public differences of the Document
(`docs/divergences.md`, "api-document": a marker is never selected or copied; one stated separator
between blocks of a copy; a tab copies as a tab); formula placement is the product's (d7-fix: a
formula is placed whole from its box edge); word gaps are the product's once `word-gaps` lands;
tables are not drawn until step A5.

## 6. The plan

Each step leaves the application working. "Gain" is from section 4; "check" is the run that
confirms it after the step is in the application.

### 6.1 pptmstr

| step | what changes | gain expected | check |
|---|---|---|---|
| **P0** Start here | the patch: `ui/typeset.py` (new), `theme.load_fonts` (+2 lines), `rich_pane.draw` / `_draw_classic` / `_draw_typeset` / `_sync` / `_describe` / `_prose` / `_marks` / `_draw_tables`, `RichState.find`, `RichState.typeset` | RICH and DETAIL: 2,514 to 516 at the median session; 12,347 to 586 on a deliverable; whole history; selection and Ctrl+C | `pptmstr_app_shot.py --rich` prints `imtypeset: in use` and about 3 library calls per `rich_pane.draw`; `make check` |
| **P1** copy on click | `transcript_pane.draw`: stop computing `copy_value` before `_draw_controls`; `_draw_controls(state, node_id, copy_value)` takes a callable and calls it inside `if imgui.small_button("copy")`. In RICH with imtypeset the callable is `doc.range_text` over everything. Independent of imtypeset. | WRAP at the largest session 2,468 to 1,927 alone; RICH Document 660 to 636, 516 to 445 | `pptmstr_real_panes.py --lazy-copy`, both panes |
| **P2** find in RICH | `transcript_pane.draw`: when `typeset.module()` is not `None`, do not demote RICH on `state.search`; set `state.rich.find = state.search`. `_draw_controls`: drop "(rich: search falls back to raw lines)" in that case. | per frame with a needle 2,068 to 672; structure stays on screen while searching. A keystroke with thousands of matches costs 10 to 14 ms until C3 | `--scenario find` |
| **P3** WRAP and RAW on a Document | `transcript_pane._draw_lines`: with imtypeset, one Code block per run (`run_starts`), colour by `_KIND_COLOUR`, `CodeOverflow_Wrap` for WRAP and `_Clip` for RAW; append to the last block as the tail grows; `context_run` from one hit test (block index is the run index). The filter (`_visible`) becomes find marks, or rebuilds the document when `show_reasoning` flips. | with P1: 2,468 to 368 at the largest session, 1,566 to 356 at the median; all scrollback in WRAP | `--mode wrap --lazy-copy`. The prototype has no context menu, no filter and no `show_reasoning`: those are to write, and the gain is the prototype's |
| **P4** DETAIL's narration through blocks | `detail._narration`: a retained `BlockCursor` per `DetailState` and `rich_pane.draw(pane.rich, blocks, live)` in place of `text_colored(prose)`. Its docstring's objection (a mid-turn parse every frame at 1.6 ms per KB) is what `live_block` + `_LIVE_INLINE_LIMIT` already bound. | not measured as a pane; streaming prose through the same path is 1,040 against 10,105 | a streaming run in DETAIL; `tests/test_detail.py` |
| **P5** `detail._diff`, `inbox` diff rows | a Code block with a colour run per line. NOT measured; listed so it is not forgotten, kept out of every total | none claimed | measure first |
| **P6** say why it is off | `app._status_bar`: show `typeset.reason()` once when `IMTYPESET_LIBRARY` is set and `module()` is `None` | a wheel upgrade stops being silent | upgrade the wheel in a scratch venv |
| **P7** opt-ins | math on; a proportional prose face and justification, if wanted at all | none in speed | photographs, by eye |

Per frame after P0 to P3, what stays in Python: the controls row (33 binding calls, 67 us),
`live_block` (35 to 40 us), `_sync`'s check (2 us), and, only while they are in view, tables
(`_draw_table`, about 50 binding calls each) and the mouse (one hit test). While streaming:
`NodeTranscript.sync`, the inline parse of the live block (under 1,200 characters) and one
description of it (136 to 227 us together with the library edit).

### 6.2 What the pptmstr spike found on the way, now in the patch

- **A table measures up to 35,066 px on the first frame ImGui draws it** (its columns have not
  found their widths) and settles in one to three frames. Trusting the first measurement made the
  document's height swing by 413,203 px summed over one scroll through the reports. In the patch a
  table is drawn in a child the size of its Host block, a height is believed when it repeats, and
  the scroll is moved by the difference when the table began above the pane's top: 19,607 px
  summed, the largest 3,552 (a real table taller than its guess).
- **A table can be the live block**, and a Host block cannot be appended to. Both raised; the
  run-time gate caught both and the pane fell back, which is how they were found in the numbers
  (a "Document" run with today's timings).

### 6.3 advtext

The map's recommendation stands (`docs/advtext-migration.md` 7): a new thin layer beside the
present views, not a retrofit of the 94-name facade, of which 48 keep signature and meaning, 19
keep their name while something about them stops being true, and 27 cannot keep their signature
(the map lists each). Nothing in `../pptmstr` imports advtext, so nothing downstream breaks.

| step | what changes in `../imgui-adv-text` | gain expected | check |
|---|---|---|---|
| **A0** freeze the oracle | `git -C ../imgui-adv-text tag advtext-reference a3c589a`; the oracle scripts under `tools/oracle/` refuse a tree that is not at that tag (their change is in THIS repository). Before any file of advtext is edited: imtypeset's goldens are dumped from it. | none; it protects every directory under `tests/golden` | `git -C ../imgui-adv-text describe --tags` |
| **A1** the description | new `advtext/typeset.py`: `convert.py`'s `Converter` (material to text, runs, styles; formulas as explicit runs; links as tags) and the library's loading, gated as in 3.1 | first frame 142 ms to 4.8 ms; nothing on screen yet | `census.py` reports only tables as misfits |
| **A2** `TypesetView` | new `advtext/ui/typeset_view.py`, the shape of `BareView.draw` (a call inside the host's child window, the item left as the last item): `layout`, `draw`, selection by `select_range`, `copy`, `hovered_link` from `hit.tag`, `scroll_to` from `block_geometry`. Demo pages opt in one at a time. | steady 7,026 to 167; resize 165,283 to 441; select-all-and-copy 152,984 to 6,785 | `views.py --scenario steady/resize/select` |
| **A3** streaming | `TypesetStreamView`: `StreamDocument` unchanged; after `sync()`, the first block that is not the same object starts one `replace_blocks`, or `append_text` when only the last text grew (in the spike 109 appends to 271 to 290 replaces) | per frame with new text 12,205 to 1,486, of which 881 is `StreamDocument`'s parse | `views.py --scenario stream` |
| **A4** find | `FindIndex` over the described texts (plain `str.find`), marks packed once per keystroke | 9,412 to 74,618 per keystroke to 707 to 18,094; no 22 ms index | `views.py --scenario find` |
| **A5** tables | a Host block per `TableBlock` and advtext's own table composition drawn at `block_geometry`, as pptmstr's patch does with ImGui's; or wait for tables in the core (backlog) | none in speed; without it a document with a table has a hole | photograph `demo/tables.md` |
| **A6** skip the material | `markdown._Converter` emits text and runs directly instead of nodes that A1 walks back | of a 12.9 ms warm parse of `sample.md`, markdown-it is 3.7 ms; at x10, 34 of 170 ms. Upper bound of the saving: 9.3 ms and 135.6 ms, plus the 2.9 / 25.7 ms conversion | `parse_cost.py` |
| not planned | `DiffView` (positioned glyphs), columns, `MathBuilder` boxes, `specimen`, `formulas`: no counterpart in the core | | |

Per frame after A2, in Python: the view's own six binding calls and three library calls; marks
are rebuilt only when the needle or the document changes.

Budget of Python-to-native calls per frame, both applications: **3 per view** (`layout`, `draw`,
`size`), +1 hit test while the mouse is over it, +2 hit tests and one `block_geometry` per visible
table. One call per edit.

## 7. Open items, stated rather than planned around

- **Collapsed word gaps in justified text** are visible at 16 to 19 px. Task `word-gaps` is in
  progress in `src/draw.*`; it had no result file when this was written (10:58). The sidecars
  measured here were built from its in-flight source (`src/draw.cpp` sha256 `f9612b0b…`, 10:23;
  `python/spike_bundle/results/advtext-rework-plan-source-state.txt`), and that state changes glyph
  placement and made one stress test hang. advtext's justified prose shows the gaps; pptmstr's
  ragged-right text does not have them by construction.
- **The Document's scroll estimates have never been scrolled with a real mouse.** What the
  synthetic wheel measured: Code blocks are estimated exactly (0 of 622 wrong); 30 to 60 % of
  paragraphs and list items are wrong by 16 to 97 px before they are laid out (net 3,988 px of
  102,796 at the largest session; 11,387 of 94,337 on the reports). Scrolling the largest session
  changed the content height 81 times, 512 px at most; advtext x10, 45 times, 44 px at most.
  Laying everything out once removes it for 7.7 to 97 ms (4.1); whether that or anchoring is
  right is for a hand on a wheel to say. The patch anchors tables only.
- **A tab-in-code-block range defect has a failing test** (the board's `doc-fixes`). 320 LITERAL
  blocks in the session corpus hold a tab: a selection or a find mark that starts at one may be
  drawn from the wrong edge. Not reproduced here.
- **Tables are not in the core.** A table sits in a Host block: ImGui's table in pptmstr, nothing
  yet in advtext.
- **Seen and not explained:** in two photographs of the harness window one row of text near
  y = 450 has a dark band through it (`python/spike_bundle/shots/harness-reports-table-and-dark-band.png`,
  item 7 of the list; the other, of the WRAP prototype, was not kept because it shows a session's
  text). Same screen row, different content. Not seen in the application's own
  photograph. Told to the lead.

## 8. Risks, each with the evidence that would retire it

| risk | evidence that retires it |
|---|---|
| A real GPU, a real display, HiDPI: every number here is llvmpipe on Xvfb | `make run` with the patch on the user's desktop; `scripts/bench_idle.py` before and after |
| The load during measurement (4 to 12) | `sh python/spike_bundle/measure_pptmstr.sh` on a quiet machine; ratios should hold |
| The stand-in headers (3.2) | the two git commands, a rebuild, `host_info()["bundle"]` saying `fork:` |
| A wheel upgrade | upgrade in a scratch venv: the import refuses, pptmstr runs classic, the rebuild restores |
| The in-flight `src/draw.*` the sidecars were built from | rebuild after `word-gaps` completes; rerun `--scenario steady` and re-photograph |
| The mouse paths (drag, the table child, the context menu over a table, wheel over a table) ran only as synthetic events | ten minutes of a person using the pane; `scripts/verify_rich_render.py` stage 3 and 4 against the patched tree (they warp the real pointer, so not run here) |
| The session corpus is one project's 46 logs | `pptmstr_census.py --sessions <another project's directory>` |
| `typeset.py` reaches the draw list through a mangled symbol name | core request C1 |
| An `IM_ASSERT` thrown by the wheel inside the library | not provoked by anything here; `catches_exceptions` is True |
| Memory: a Document per `RichState`, texts kept twice for find | not measured; `tracemalloc` on the largest session |
| pptmstr's `prune()` drops a node's cache but `RichState` keeps one Document for the pane | by construction one document per pane; confirm by switching nodes for a minute |

## 9. Core requests and backlog

The requests the spike turned up, each with the measurement behind it, are in
`.concerns/advtext-rework-plan--builder--core-requests.md` (C1 to C8).

Backlog, one line: Dear ImGui Bundle shipping imtypeset as a bound module (litgen,
`bindings/imgui_bundle/`) would replace the sidecar, its gate and the ctypes boundary; neither
application needs it.

## 10. How it was measured

| what | command (from the repository root; every process under `ulimit -v 6000000` and a timeout) |
|---|---|
| sidecars, once, from the source state recorded beside the results | `IMTYPESET_BUNDLE_OUT=build/advtext-rework-plan-pptmstr sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python`; `IMTYPESET_BUNDLE_OUT=build/advtext-rework-plan-advtext sh python/host/build_bundle_library.sh` |
| pptmstr census | `../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_census.py` (this repository's reports, the default) and `… --sessions DIR` (session logs: read only when their directory is named; the plan's numbers used this project's) |
| pptmstr panes | `sh python/spike_bundle/measure_pptmstr.sh [out.jsonl] [sessions-dir]` (the `--pick` rows run only with a sessions directory); one run is `sh tools/shot.sh -- ../pptmstr/.venv/bin/python -P -B python/spike_bundle/pptmstr_real_panes.py --pane today|document [--mode wrap] [--sessions DIR --pick median|p90|max] --scenario steady|scroll|stream|resize|find|select [--profile] [--lazy-copy] [--settle]` |
| the real application | `python/spike_bundle/pptmstr_app_shot.py` as in "Start here" |
| advtext census, parse | `../imgui-adv-text/.venv/bin/python -P -B python/spike_advtext/census.py`; `…/parse_cost.py` |
| advtext views | `sh python/spike_advtext/measure.sh`; one run is `…/views.py --pane today|document --repeat N --scenario …` |
| medians | `python3 python/spike_bundle/summarise.py python/spike_bundle/results/results-*.jsonl` |

The result files are in `python/spike_bundle/results/` with the load average of every run. The
runs were made from scripts of the same content under `build/advtext-rework-plan-run/`
(`matrix.sh`, `matrix2.sh`, `matrix-advtext.sh`, and by hand for `results-pptmstr-3.jsonl`; that
directory and the two sidecars were removed when the task finished), so
`measure_pptmstr.sh` as checked in is the union and has not itself been run end to end.

Which numbers come from which overlay: tables of 4.1 use `results-pptmstr.jsonl` and
`-2.jsonl` (taken before the two table fixes of 6.2) except the largest-session and reports
"Document" steady rows, the streaming rows and the scroll rows, which are from
`results-pptmstr-3.jsonl` (the patch as checked in). The session-corpus rows hold at most 4
tables, none in view at the tail, so the fixes do not bear on them; that was not re-run to show it.

A time is one `transcript_pane.draw` (pptmstr) or one view draw (advtext) by `perf_counter`.
`--profile` wraps the `imgui` module, draw lists, fonts and the ctypes library in a timing proxy
(1.1 to 2.2 us a call of its own), which is why the profiled totals are not quoted and "Python"
is the unprofiled median minus the time inside the two. advtext's binding-call count is what
went through `imgui_bundle.imgui` looked up at call time, and may miss calls made through a
reference a module kept.
