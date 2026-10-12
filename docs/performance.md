# Performance

What was measured, on what, and what was changed because of it. Every number
here was printed by a program in `bench/` or by one of the reference's own
scripts, and the command line is given with it. Nothing is quoted from memory.

**Read the ratios, not the microseconds.** Each C++ benchmark was run five times
and the medians of those runs lie within a few percent of each other (the
`spread` column of each table: mostly 1 to 3%, worst 18% on one 6 us row).
That is the spread *inside one sitting*. Between two sittings an hour apart,
with no change to the code being timed, the font rows moved by up to 54%
(`FontLibrary::register_font` 0.73 ms, then 1.12 ms) while everything else
stayed within 4%. The machine is a laptop with other work on it: its load
average was between 0.4 and 2.5 across these runs.

The design note's own figures (`../imgui-adv-text/docs/cpp-extension.md` §3)
carry the same warning from their author: one machine, a 22% spread across four
runs. They are an order of magnitude and are treated as one here. The
comparison this document rests on is not against them but against the
reference **run on this machine, on the same text**, below.

## What was run, on what

| | |
|---|---|
| machine | AMD Ryzen 7 PRO 4750U (8 cores, 16 threads), 32 GB, Linux 6.1.0-45-amd64 |
| compiler | GCC 12.2.0 |
| build | `CMAKE_BUILD_TYPE=Release`: `-O3 -DNDEBUG`, plus what `imtypeset_build_flags` adds to every source of ours: `-ffp-contract=off -fno-exceptions -fno-rtti` and the warnings. No `-march`, no `-ffast-math`, no LTO. This is what the `release` preset gives; the directory was a private one (`build/bench`) because the preset directories are shared. |
| rasteriser | FreeType 2.12.1 (the system's), through ImGui's `misc/freetype`. **stb_truetype was not benchmarked.** |
| ImGui | 1.92.9, `IMGUI_USE_WCHAR32`, 16-bit `ImDrawIdx` |
| reference | `../imgui-adv-text` as its working tree stood (it has uncommitted changes), its own `.venv` interpreter, FreeType through imgui_bundle |
| display | none on either side: frames are built in memory and composited nowhere |

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/bench
for i in 1 2 3 4 5; do
    build/bench/bench/bench_draw                  > draw-$i.txt
    build/bench/bench/bench_reflow --skip growth  > reflow-$i.txt
    build/bench/bench/bench_font                  > font-$i.txt
done
build/bench/bench/bench_reflow --only growth      > growth-1.txt     # minutes; once
python3 bench/spread.py draw-*.txt                # the tables below
```

Each row is `n` samples of one operation; the benchmarks print the minimum, the
median and the 95th percentile. `bench/spread.py` lines five runs up: `least
min` is the smallest minimum of the five, and the next three columns are the
smallest, middle and largest of the five medians.

### The text both sides set

`bench/data/sample.txt`, written by

```sh
python3 tools/sample_text.py ../imgui-adv-text/demo/sample.md bench/data/sample.txt
```

**The port has no markdown**, so it cannot set `demo/sample.md` as the reference
does. The script keeps the prose and drops the markup: 34 paragraphs, 833
words, 4,717 characters. A heading, a block quote and each list item become a
paragraph of body text (no heading sizes, no bold, no list markers); 4 lines of
fenced code, 8 table rows and 1 rule are dropped; emphasis and code spans lose
their markers and keep their text, in the body face.

The reference's two scripts take `--source`, and that file is also valid
markdown that parses to 34 paragraph blocks and nothing else (checked: 34
`TextBlock`s of kind `PARAGRAPH`). So **both sides were given the same file**,
set in Roboto at a 16 px em, 1.35 leading, justified, at the reference's litmus
measure of 420 pt (557.91 px at 96 px per inch). At that measure both ship
**74 lines, 3,918 glyphs and 4,859 commands**. Where they still differ:

- The counts agree; the breaks were not compared line by line here. (That
  comparison is `tests/test_api.cpp`'s, on the goldens.)
- The reference stacks the blocks in one vertical box with 0.55 em below each
  (1,883 px in all). The benchmark stacks 34 separate paragraphs, each a whole
  number of pixels tall, 8.8 px apart (1,912 px). A 768 px window therefore
  holds slightly different slices: the reference's first window has 2,136
  commands in it, the port's 1,715 glyphs.
- The reference's unmodified `demo/sample.md` (4,362 glyphs, eight font keys)
  was also run once, for the record; see "The reference on its own sample".
  The port has no row for it. The nearest thing is the `mixed:` page of
  `bench_draw`, 4,377 glyphs over eight font keys tiled from
  `tests/golden/draw`, which is the reference's own command lists but not that
  document.

## Headline: the reference here against the port here

Reference figures are best-of-N as its scripts print them, as the range over
five runs; port figures are the median of five runs' medians, after the
optimisation described below. Same machine, same hour, same text.

| | reference (Python), this machine | port (C++), this machine | ratio | design note §3 (another machine) |
|---|---|---|---|---|
| full draw of the document, per frame | 12.76 to 12.87 ms | 0.100 ms (25.6 ns/glyph) | about 130x | 13.7 to 15.9 ms |
| the same, document x10 | 125.9 to 128.9 ms | 1.27 ms | about 100x | |
| the same, document x100 | 1,228 to 1,284 ms | 12.9 ms | about 97x | |
| culled draw, 768 px window, x1 / x10 / x100 | 5.68 to 5.73 / 5.66 to 5.78 / 5.57 to 5.79 ms | 0.039 to 0.043 / 0.039 to 0.042 / 0.039 ms | about 140x | 5.1 to 5.9 ms |
| ImGui's own share of a full draw (one `AddText` per glyph) | 1.13 to 1.17 ms | 0.090 ms | | 0.7 to 1.3 ms |
| reflow: the whole document at a new width, 420 pt | 101.6 to 103.3 ms (compose + ship), 103.4 to 105.1 with the selection index | 0.465 ms, 0.526 with the selection models | about 220x | (its script's docstring: 97.7 ms) |
| the heaviest paragraph (635 nodes) alone, justified | 33.7 ms | 0.089 ms | about 380x | |
| the same paragraph ragged right by total fit | 2,863 ms | 5.2 ms | about 550x | |
| streaming: one appended word, the paragraph being written re-laid-out | 0.34 to 6.21 ms layout, 0.74 to 9.22 ms with its parse and frame work (four sampled words) | median 0.011 ms, 95th percentile 0.058 ms, worst 0.110 ms | | 1.4 to 2.2 ms per frame per view |
| font setup, the math face | 790 to 808 ms for the cmap rewrite, 52 ms for its metrics | 6.1 ms in all since the strict pass of D33 (2.9 ms before it; 0.24 ms of it the cmap rewrite) | about 140x | about 1 s |
| font setup, all six faces | 1.25 to 1.30 s with the import | 9.6 ms since the strict pass (4.9 ms before it) | about 130x | |

What these do and do not say:

- **Culled draw is flat as the document grows** on both sides, when the host
  finds the visible paragraphs itself (a binary search over the tops it keeps):
  0.039 ms at x1, x10 and x100. Handing every paragraph to `DrawParagraph` and
  letting the library skip by line is not flat (0.044, 0.056, 0.44 ms), and the
  widget is not either (0.049, 0.092, 1.23 ms at 34, 340 and 3,400 calls): each
  call is cheap but there is one per paragraph. A host with thousands of
  paragraphs should clip them itself, as it would any ImGui item.
- The reference's culled figure is `draw` over a command list that was culled
  *before* the clock started; the port's includes finding the paragraphs.
- The reference's "ImGui's share" row is `add_text` called from Python, so it
  includes the binding. The port's is `ImDrawList::AddText` called from C++ on
  the same pens. The port's own draw (0.065 ms through one `Drawer`) is
  cheaper than that row, because it writes the quads itself rather than
  calling `AddText` per glyph; `tests/test_draw.cpp` holds the two byte-equal.
- **Streaming is not like for like.** The reference's `StreamView` parses
  markdown, lays the live block out ragged by first fit and indexes it. The
  port's row is `SetParagraphText` + `LayoutParagraph` on the paragraph being
  written, justified by total fit, every word. It does less (no markdown) and
  more (the optimiser on every append). The port's worst case, the 635-node
  paragraph at its last ten words, is 0.100 ms.
- The design note's column is from its author's machine. This machine's
  figures for the reference are close to it everywhere they overlap, which is
  the only sense in which the note's figures were "re-taken".

## The benchmarks

### bench_draw: what a frame costs once layout is done

`build/bench/bench/bench_draw`, five runs. One section can be run alone with
`--only x1`, `x10`, `x100`, `anatomy`, `mixed` or `selection`.

| what | least min | median: least | of medians | greatest | spread 
|---|---|---|---|---|---
| full draw, x1 | 99.10 us | 99.63 us | 100.30 us | 101.61 us | 2.0% 
| culled, host skips, x1, scroll 0.00 | 37.95 us | 38.11 us | 38.32 us | 38.89 us | 2.1% 
| culled, host skips, x1, scroll 0.25 | 42.20 us | 42.59 us | 42.85 us | 43.08 us | 1.2% 
| culled, host skips, x1, scroll 0.50 | 41.13 us | 41.34 us | 41.53 us | 41.98 us | 1.6% 
| culled, host skips, x1, scroll 0.75 | 22.53 us | 22.64 us | 22.68 us | 23.04 us | 1.8% 
| culled, all offered, x1, scroll 0.25 | 43.24 us | 43.47 us | 43.84 us | 44.16 us | 1.6% 
| widget frame, x1, scroll 0.25 | 48.73 us | 49.06 us | 49.36 us | 49.72 us | 1.3% 
| widget frame, NoTextCompare, x1, scroll 0.25 | 48.02 us | 48.21 us | 48.62 us | 49.03 us | 1.7% 
| full draw, x10 | 1243.78 us | 1265.32 us | 1269.47 us | 1276.50 us | 0.9% 
| culled, host skips, x10, scroll 0.00 | 38.08 us | 38.27 us | 38.59 us | 38.70 us | 1.1% 
| culled, host skips, x10, scroll 0.25 | 41.18 us | 41.56 us | 41.68 us | 42.07 us | 1.2% 
| culled, host skips, x10, scroll 0.50 | 38.14 us | 38.41 us | 38.48 us | 38.75 us | 0.9% 
| culled, host skips, x10, scroll 0.75 | 41.15 us | 41.34 us | 41.81 us | 42.06 us | 1.7% 
| culled, all offered, x10, scroll 0.25 | 55.15 us | 55.76 us | 56.06 us | 57.17 us | 2.5% 
| widget frame, x10, scroll 0.25 | 90.66 us | 91.39 us | 91.70 us | 91.98 us | 0.6% 
| widget frame, NoTextCompare, x10, scroll 0.25 | 81.30 us | 82.11 us | 82.72 us | 83.45 us | 1.6% 
| full draw, x100 | 12.77 ms | 12.82 ms | 12.86 ms | 12.93 ms | 0.8% 
| culled, host skips, x100, scroll 0.00 | 38.13 us | 38.42 us | 38.52 us | 38.94 us | 1.4% 
| culled, host skips, x100, scroll 0.25 | 38.16 us | 38.41 us | 38.57 us | 38.89 us | 1.3% 
| culled, host skips, x100, scroll 0.50 | 38.25 us | 38.43 us | 38.61 us | 39.02 us | 1.5% 
| culled, host skips, x100, scroll 0.75 | 38.23 us | 38.50 us | 38.57 us | 38.97 us | 1.2% 
| culled, all offered, x100, scroll 0.25 | 260.69 us | 431.64 us | 440.44 us | 447.70 us | 3.7% 
| widget frame, x100, scroll 0.25 | 1158.22 us | 1223.30 us | 1226.63 us | 1234.07 us | 0.9% 
| widget frame, NoTextCompare, x100, scroll 0.25 | 621.32 us | 851.25 us | 859.28 us | 864.43 us | 1.5% 
| sample: Drawer into a draw list | 64.11 us | 64.45 us | 64.97 us | 65.64 us | 1.8% 
| sample: the same calls, one AddText each (ImGui's share) | 88.64 us | 89.66 us | 90.03 us | 91.26 us | 1.8% 
| sample: placement alone, no vertices | 40.28 us | 40.48 us | 40.66 us | 41.45 us | 2.4% 
| sample: Drawer into buffers | 67.57 us | 68.02 us | 68.38 us | 69.54 us | 2.2% 
| sample: 768 px window, no line index (every command visited) | 57.92 us | 58.27 us | 58.54 us | 59.41 us | 2.0% 
| sample: 768 px window, by line index | 22.11 us | 22.24 us | 22.44 us | 22.67 us | 1.9% 
| mixed: Drawer into a draw list | 68.92 us | 69.29 us | 69.70 us | 70.61 us | 1.9% 
| mixed: the same calls, one AddText each (ImGui's share) | 102.12 us | 102.70 us | 103.42 us | 105.53 us | 2.8% 
| mixed: placement alone, no vertices | 43.46 us | 43.69 us | 44.00 us | 44.83 us | 2.6% 
| mixed: Drawer into buffers | 72.51 us | 73.09 us | 73.42 us | 74.57 us | 2.0% 
| mixed: 768 px window, no line index (every command visited) | 59.90 us | 60.60 us | 61.13 us | 62.58 us | 3.3% 
| mixed: 768 px window, by line index | 17.47 us | 17.58 us | 17.63 us | 17.64 us | 0.3% 
| first hit test after a layout (builds the selection model) | 6.08 us | 6.12 us | 6.19 us | 6.26 us | 2.3% 
| hit test, per call | 45.1 ns | 45.4 ns | 45.7 ns | 45.9 ns | 1.1% 
| select a range by character, per call | 89.9 ns | 90.8 ns | 91.6 ns | 93.6 ns | 3.1% 
| select a range by word, per call | 134.2 ns | 135.6 ns | 137.1 ns | 140.2 ns | 3.4% 
| select a range by line, per call | 46.9 ns | 47.1 ns | 47.5 ns | 48.0 ns | 1.8% 
| selection rectangles, everything selected | 220.0 ns | 230.0 ns | 230.0 ns | 231.0 ns | 0.4% 
| draw the paragraph | 9.16 us | 9.22 us | 9.26 us | 9.28 us | 0.7% 
| draw the paragraph, everything selected | 9.65 us | 9.72 us | 9.77 us | 9.80 us | 0.8% 
| copy the whole paragraph | 5.72 us | 5.78 us | 5.85 us | 6.83 us | 18.2% 

- `full draw` is `DrawParagraph` for each of the 34 (340, 3,400) paragraphs.
  It costs more per glyph than the `sample:` rows (25.6 against 16.6 ns),
  which draw the same commands through one `Drawer` as one list: about 1 us of
  each `DrawParagraph` is per call, not per glyph (the font is bound and every
  distinct glyph looked up again, because the atlas may have changed between
  two calls).
- `widget frame` was `Begin`, `TextJustified` per paragraph, `End`, with
  `Layouts` and `TextBuilds` checked to be zero on every timed frame. At x100
  a third of it was measuring and comparing 3,400 texts that have not changed,
  which `ImTypesetTextFlags_NoTextCompare` removed. **Both the entry and the
  flag are gone** (D36): the rows as they are now, and what they cost against
  these, are in "After the API revision" below.
- `culled, all offered, x100` is bimodal (minimum 0.26 ms, median 0.44 ms) in
  every run. Not investigated.
- Hit testing is tens of nanoseconds once the selection model exists, and the
  model for the 601-item paragraph is built in 6 us at the first query after a
  layout. `copy the whole paragraph` allocates (about 14 times, as the
  `api-math` report notes); it is not a per-frame call.

### bench_reflow: what layout costs

`build/bench/bench/bench_reflow --skip growth`, five runs after the change and
five before it (`before`, `ratio`).

| what | least min | median: least | of medians | greatest | spread | before | ratio 
|---|---|---|---|---|---|---|---
| text to lines: SetParagraphText + LayoutParagraph, 420 pt | 628.37 us | 632.59 us | 634.28 us | 634.69 us | 0.3% | 1293.67 us | 2.04x 
| reflow at a new width, 300 pt | 517.71 us | 531.30 us | 532.30 us | 533.58 us | 0.4% | 1191.17 us | 2.24x 
| reflow at a new width, 420 pt | 459.56 us | 464.71 us | 465.21 us | 465.81 us | 0.2% | 1144.76 us | 2.46x 
| reflow at a new width, 800 pt | 406.48 us | 408.49 us | 409.59 us | 410.11 us | 0.4% | 1321.89 us | 3.23x 
| reflow, 420 pt, and the selection model of every paragraph | 518.75 us | 524.28 us | 525.59 us | 526.76 us | 0.5% | 1205.27 us | 2.29x 
| LayoutParagraph with nothing changed, all paragraphs | 960.9 ns | 991.9 ns | 991.9 ns | 1.00 us | 0.9% | 1.00 us | 1.01x 
| text to lines | 106.49 us | 107.06 us | 107.70 us | 108.22 us | 1.1% | 357.92 us | 3.32x 
| reflow, justified, total fit (the default) | 87.72 us | 88.69 us | 89.13 us | 89.36 us | 0.8% | 340.33 us | 3.82x 
| reflow, justified, first fit | 36.31 us | 36.47 us | 36.58 us | 36.65 us | 0.5% | 66.87 us | 1.83x 
| reflow, ragged right, first fit (the default) | 36.75 us | 36.97 us | 37.06 us | 37.19 us | 0.6% | 66.26 us | 1.79x 
| reflow, ragged right, total fit | 5153.01 us | 5201.55 us | 5264.28 us | 5274.21 us | 1.4% | 28.76 ms | 5.46x 
| text to lines, a paragraph with three formulas | 28.52 us | 28.72 us | 28.96 us | 29.08 us | 1.3% | 39.17 us | 1.35x 
| reflow, the same paragraph | 13.98 us | 14.07 us | 14.15 us | 14.18 us | 0.8% | 24.16 us | 1.71x 
| append: SetParagraphText + LayoutParagraph, every append | 951.0 ns | 10.86 us | 10.92 us | 11.08 us | 2.0% | 18.57 us | 1.70x 
| append, then DrawParagraph of that paragraph, every append | 1.33 us | 13.58 us | 13.61 us | 13.80 us | 1.6% | 21.06 us | 1.55x 
| append, the last paragraph's appends only | 1.31 us | 41.04 us | 41.15 us | 41.60 us | 1.4% | 91.60 us | 2.23x 
| append, the last ten words of the last paragraph | 90.55 us | 99.98 us | 100.19 us | 100.96 us | 1.0% | 317.96 us | 3.17x 

`reflow` is `LayoutParagraph` at a width the paragraph was not last laid out
at: break, pack, stack, ship, index by line. `text to lines` adds
`SetParagraphText` and the text build. The streaming rows are one sample per
appended word over twenty passes through the document; `min` is a paragraph's
first word.

### bench_font: getting fonts ready

`build/bench/bench/bench_font`, five runs, forty fresh ImGui and ImTypeset
contexts per row. Files come from the operating system's cache after the first
read.

| what | least min | median: least | of medians | greatest | spread 
|---|---|---|---|---|---
| Roboto-Regular.ttf | 498.05 us | 521.18 us | 523.64 us | 525.53 us | 0.8% 
| Roboto-Bold.ttf | 394.58 us | 410.79 us | 411.87 us | 413.43 us | 0.6% 
| Roboto-RegularItalic.ttf | 385.71 us | 398.84 us | 400.61 us | 402.97 us | 1.0% 
| Roboto-BoldItalic.ttf | 401.97 us | 465.12 us | 465.89 us | 469.10 us | 0.9% 
| Inconsolata-Medium.ttf | 211.76 us | 216.43 us | 218.39 us | 218.78 us | 1.1% 
| latinmodern-math.otf | 1741.13 us | 2872.49 us | 2885.05 us | 2917.08 us | 1.6% 
| all six faces | 3729.11 us | 4913.30 us | 4918.21 us | 4946.20 us | 0.7% 
| FontLibrary::register_font: read and parse for layout | 1085.53 us | 1108.95 us | 1118.69 us | 1125.19 us | 1.5% 
| build_atlas_font: the cmap rewrite alone | 234.33 us | 238.68 us | 239.07 us | 239.68 us | 0.4% 
| add_face_to_atlas: the rewrite again, validation, AddFontFromMemoryTTF | 1221.38 us | 1242.22 us | 1249.87 us | 1262.83 us | 1.7% 
| the first formula: text to lines | 107.68 us | 112.20 us | 112.47 us | 117.19 us | 4.4% 
| the same text again | 10.17 us | 10.46 us | 10.61 us | 10.70 us | 2.3% 
| the first frame that draws the sample x1 | 398.04 us | 413.83 us | 416.64 us | 424.37 us | 2.5% 
| the second frame | 104.42 us | 105.34 us | 105.45 us | 105.90 us | 0.5% 
| the first frame that draws the formula's paragraph | 332.19 us | 441.01 us | 442.80 us | 447.27 us | 1.4% 

- No glyph is rasterised when a font is added (ImGui 1.92 bakes on first use);
  that cost is the `first frame` row: 0.42 ms for every glyph the sample uses,
  once.
- The public call for the math face (2.9 ms) is more than the sum of its
  stages as the internals time them (1.12 + 1.25 ms): `add_font` registers the
  font in a probe registry first, so that a refusal leaves nothing changed, and
  so reads and parses the file twice. Not changed: it is paid once per process.
- These rows are the ones that moved between sittings (see the top).
- **They are from before the strict pass** (D33, `OpenTypeFont::strict_check`:
  every structure a rasteriser follows is checked before the atlas sees the
  font). With it the public call for the math face is 6.0 to 6.5 ms and all
  six faces 9.3 to 10.2 ms, in the sitting of "After the API revision" below.
  The `stress` task measured the same step in its own sitting: 2.9 ms to
  6.2 ms.

The reference, five processes of
`.venv/bin/python -B bench/reference_font_setup.py`:

```
| import advtext.fontlib | 25.6 ms |
| math: metrics | 52.8 ms | imgui_font_file | 791.3 ms | math-6ef2b9965539-latinmodern-math.otf |
| all faces | metrics 434.8 ms | imgui_font_file 791.8 ms | with the import 1252.2 ms |
| import advtext.fontlib | 43.9 ms |
| math: metrics | 52.4 ms | imgui_font_file | 795.8 ms | math-6ef2b9965539-latinmodern-math.otf |
| all faces | metrics 436.2 ms | imgui_font_file 796.3 ms | with the import 1276.4 ms |
| import advtext.fontlib | 43.8 ms |
| math: metrics | 52.5 ms | imgui_font_file | 791.3 ms | math-6ef2b9965539-latinmodern-math.otf |
| all faces | metrics 433.0 ms | imgui_font_file 791.8 ms | with the import 1268.5 ms |
| import advtext.fontlib | 25.5 ms |
| math: metrics | 52.1 ms | imgui_font_file | 789.7 ms | math-6ef2b9965539-latinmodern-math.otf |
| all faces | metrics 431.1 ms | imgui_font_file 790.1 ms | with the import 1246.8 ms |
| import advtext.fontlib | 44.2 ms |
| math: metrics | 52.7 ms | imgui_font_file | 807.8 ms | math-6ef2b9965539-latinmodern-math.otf |
| all faces | metrics 448.1 ms | imgui_font_file 808.3 ms | with the import 1300.5 ms |
```

## Total fit has no work budget: the growth curve

**It has one now** (D33: 2^20 candidate lines, then first fit on the same
material). This section is the measurement the budget was chosen from and is
kept as taken; the curve as it is with the budget is at the end of "After the
API revision" below. `ImTypesetLayoutFlags_TotalFit`, named below, is now
`ImTypesetLineBreak_TotalFit`.

Review finding E (`.concerns/review-layout-core--reviewer-3--findings.md`):
the optimiser keeps every active node that is still feasible, and with a `fil`
right skip every line is feasible. Measured, not capped; the cap is the
`stress` task's. The sample's last paragraph (635 nodes in the reference, 601
shipped items) concatenated 1, 4, 16 and 64 times and laid out at 420 pt:
`build/bench/bench/bench_reflow --only growth`, one run each side of the
change. Rows of more than ten seconds are a single sample.

| what | of medians | before | ratio 
|---|---|---|---
| justified, total fit, x1 | 88.63 us | 344.29 us | 3.88x 
| justified, total fit, x4 | 862.43 us | 4365.73 us | 5.06x 
| justified, total fit, x16 | 9266.36 us | 51.74 ms | 5.58x 
| justified, total fit, x64 | 126.44 ms | 733.87 ms | 5.80x 
| justified, first fit, x1 | 36.55 us | 67.08 us | 1.84x 
| justified, first fit, x4 | 145.22 us | 270.66 us | 1.86x 
| justified, first fit, x16 | 586.14 us | 1092.35 us | 1.86x 
| justified, first fit, x64 | 2826.32 us | 4665.38 us | 1.65x 
| ragged right, first fit, x1 | 36.78 us | 66.33 us | 1.80x 
| ragged right, first fit, x4 | 147.15 us | 269.49 us | 1.83x 
| ragged right, first fit, x16 | 596.77 us | 1080.14 us | 1.81x 
| ragged right, first fit, x64 | 2935.21 us | 4749.58 us | 1.62x 
| ragged right (fil right skip), total fit, x1 | 5273.12 us | 29.46 ms | 5.59x 
| ragged right (fil right skip), total fit, x4 | 94.84 ms | 563.61 ms | 5.94x 
| ragged right (fil right skip), total fit, x16 | 1565.69 ms | 9456.21 ms | 6.04x 
| ragged right (fil right skip), total fit, x64 | 26534.47 ms | 156280.86 ms | 5.89x 

| | x1 | x4 | x16 | x64 | growth per 4x of text |
|---|---|---|---|---|---|
| lines | 8 | 30 to 32 | 120 to 128 | 480 to 512 | |
| first fit, either alignment | 0.037 ms | 0.146 ms | 0.59 ms | 2.9 ms | 4.0x, 4.0x, 4.9x: linear |
| justified, total fit | 0.089 ms | 0.86 ms | 9.3 ms | 126 ms | 9.7x, 10.7x, 13.6x |
| ragged right, total fit | 5.3 ms | 95 ms | 1.57 s | 26.5 s | 18.0x, 16.5x, 16.9x: quadratic |

- **Ragged right by total fit is quadratic in the length of the paragraph**, as
  the review estimated: sixteen times the time for four times the text, at
  every step measured. A 32-line paragraph takes 95 ms and a 128-line one
  1.6 s, in release, after the change below; before it they took 0.56 s and
  9.5 s, and the 512-line one 156 s. The public API reaches this with
  `ImTypesetAlign_Left` + `ImTypesetLayoutFlags_TotalFit`; ragged text is
  broken by first fit unless the host asks.
- **Justified total fit is superlinear too**, which the review did not say:
  about ten to fourteen times for four times the text, where a breaker with
  TeX's `easy_line` merging would be near linear. The reference keeps one
  class of active node per line number (`src/linebreak.h` says so and
  `docs/divergences.md` should, review item H), so breakpoints reachable in
  several line counts are carried once per count. It is exact and it is what
  the goldens hold, so it was not changed. In practice: an 8-line paragraph
  11 us a line, a 30-line one 29 us a line, a 120-line one 77 us a line.
- For `stress`: first fit is linear at every length measured, so it is a sound
  fallback. How many candidate lines each row evaluated was not counted here;
  a cap stated in candidates needs that count first.

The reference's own figure for the x1 ragged case, on this machine
(`scripts/bench_reflow.py --widths 420 --repeat 3 --ragged --stream-at ""
--source bench/data/sample.txt`): justified 33.67 ms, ragged by total fit
2,862.87 ms, ragged by first fit 4.98 ms.

## What was optimised

One change. The profile was taken with gprof (`perf` is not installed): a
second Release build with `-pg -fno-omit-frame-pointer -fno-inline-functions
-fno-inline-small-functions`, in `build/bench-prof`, running
`bench_reflow --only document`. Before the change the flat profile read: `hpack`
30%, `CharMeasureCache::operator()` 26% (173 million calls, 38 per `hpack`),
its hash and thunk 11%, `LineBreaker::build_line` 6%. Three quarters of a
reflow was the breaker building each candidate line in a scratch list and
walking it to measure it.

**Candidate lines are judged from running totals** (`src/linebreak.cpp`,
`LineBreaker::tabulate` and `LineBreaker::trial`; private members in
`src/linebreak.h`). When a paragraph is prepared, the natural width, stretch
and shrink that `hpack` would sum over nodes `[0, i)` are tabulated once, and
for each position the first later one a break does not discard. A candidate
line's totals are then a difference of two entries plus the skips, whatever its
length. They are handed to the real `hpack` as four glue nodes, one per order,
so the judgement (glue order, badness, overfull) is still `hpack`'s and there
is no second copy of that arithmetic. The lines that are chosen are built and
packed as before. A paragraph the totals cannot describe takes the old path
unchanged: one with a discretionary (its break adds and removes material), a
rule of running width or characters and no measure (where `hpack` reports an
error per line).

| | before | after | |
|---|---|---|---|
| reflow, the whole sample, 420 pt | 1.145 ms | 0.465 ms | 2.46x |
| reflow, the whole sample, 300 pt / 800 pt | 1.191 / 1.322 ms | 0.533 / 0.410 ms | 2.24x / 3.23x |
| text to lines, the whole sample | 1.294 ms | 0.634 ms | 2.04x |
| the last paragraph: justified total fit | 0.340 ms | 0.089 ms | 3.82x |
| the last paragraph: first fit | 0.067 ms | 0.037 ms | 1.83x |
| the last paragraph: ragged right, total fit | 28.8 ms | 5.2 ms | 5.48x |
| streaming, median append | 18.6 us | 10.9 us | 1.70x |
| streaming, the last ten words of the last paragraph | 318 us | 100 us | 3.18x |
| justified total fit, x64 (480 lines) | 734 ms | 126 ms | 5.80x |
| ragged total fit, x64 (512 lines) | 156 s | 26.5 s | 5.89x |

(Medians of five runs' medians; the last two rows one run each.) The draw rows
did not move (0.97x to 1.04x, inside the spread), as they should not have.

**Exactness.** The full suite was run, unnarrowed, in private Release, Debug
and ASan+UBSan builds before the change and after it: 21 test executables and
the two examples before, and the new test with them after. Every executable reports the same number of tests and of
checks before and after, to the digit, in all three builds (for instance
`test_paragraph` 767,445 checks, `test_linebreak` 3,550, `test_api` 6,051,
`test_api_math` 28,810); `test_opentype_wild` fails before and after, as it is
meant to until task `font-wild`; nothing else fails. No golden was touched.
The `stb` and `wchar16` configurations were not run.

The goldens hold whichever path a paragraph takes to the reference; a new
test, `tests/test_linebreak_totals.cpp`, holds the two paths to each other:
6,000 random lists (characters, glue of every order, kerns, math nodes,
penalties, boxes, rules; three paragraph shapes; first fit, total fit and the
emergency pass; `fil` and finite skips) broken both ways and compared field by
field, 335,325 checks. `LineBreaker::use_running_totals(false)` is the switch
it uses. Three hand mutations of the new code (no pruning at the start of a
line; no right skip; pruning only glue) each fail it, and each also fails the
`test_linebreak` goldens. Three more did not compile and say nothing.

After the change the same profile is flat: `tabulate` 19%, `hpack` 11%,
`trial` 9%, `try_break` 8%, the measure 7%, `map_shipped` 7%,
`find_breakpoints` 6%.

### Looked at and left alone

- **The draw loop** (`src/draw.cpp`). It already has what the task's list
  names: glyph slots cached per key, quads written in batches, lines culled by
  bisection. Its profile is `place_commands` 39%, the emission 22%, `snap_px`
  19%: the pen arithmetic itself, which is held to the reference bit for bit.
  A culled frame is 0.04 ms, a quarter of a percent of a 16.7 ms frame.
- **The per-call cost of `DrawParagraph`** (about 1 us, a third of a full draw
  of 34 short paragraphs). Keeping a `Drawer`'s font and glyph lookups across
  calls needs a way to know the atlas has not moved, and ImGui 1.92 packs on
  demand. Not attempted.
- **Reading the math font twice** in `add_font` (above): about 1 ms, once.
- **Merging active nodes across line numbers** in the breaker, which is what
  makes justified total fit superlinear. It changes which chain wins in the
  last-resort case (the reference takes the last active node in line-number
  order), so it is not an optimisation in the sense used here.

## The reference on its own sample

For the record, the reference's two scripts on its unmodified `demo/sample.md`
on this machine, one run each (`scripts/bench_draw.py --copies 1,2,4 --repeat
7`, `scripts/bench_reflow.py --widths 300,420,800 --repeat 5`): full draw
14.26 ms at one copy (4,362 glyphs), 28.22 and 56.08 ms at two and four;
culled 5.2 to 5.3 ms; per-glyph `add_text` alone 1.29 ms; cold reflow
103.70 ms at 420 pt. The design note's 13.7 to 15.9 ms, 5.1 to 5.9 ms and 0.7
to 1.3 ms are the same quantities from its author's machine.

The reference on `bench/data/sample.txt`, the third of five runs
(`scripts/bench_draw.py --copies 1,10,100 --repeat 7 --source
bench/data/sample.txt`):

| copies | commands | full draw | % budget | visible | culled draw | % budget |
|---|---|---|---|---|---|---|
| 1 | 4859 | 12.87ms | 77% | 2136 | 5.73ms | 34% |
| 10 | 48590 | 128.89ms | 773% | 2136 | 5.78ms | 35% |
| 100 | 485900 | 1283.71ms | 7702% | 2136 | 5.74ms | 34% |

| what | glyphs | best |
|---|---|---|
| 1. `draw`, as shipped | 3918 | 12.78ms |
| 2. the real `draw`, into a draw list that discards | 3918 | 12.06ms |
| 3. per-glyph `add_text` alone, arguments precomputed | 3918 | 1.16ms |
| 4. batched `add_text` over 1 runs (WRONG geometry) | 3918 | 0.01ms |

and `scripts/bench_reflow.py --widths 300,420,800 --repeat 5 --source
bench/data/sample.txt`:

| width | compose | ship | selection index | total |
|---|---|---|---|---|
| 300pt | 100.97ms | 6.20ms | 1.78ms | 108.95ms |
| 420pt | 95.75ms | 6.09ms | 1.73ms | 103.56ms |
| 800pt | 102.57ms | 5.92ms | 1.67ms | 110.16ms |

| width | words | units | shipped | parse | layout | frame | total | % budget |
|---|---|---|---|---|---|---|---|---|
| 300pt | 208/833 | 5 | 1 | 1.38ms | 3.96ms | 0.59ms | 5.93ms | 36% |
| 300pt | 416/833 | 11 | 1 | 0.74ms | 0.50ms | 0.12ms | 1.36ms | 8% |
| 300pt | 625/833 | 27 | 1 | 0.28ms | 0.33ms | 0.10ms | 0.71ms | 4% |
| 300pt | 833/833 | 34 | 1 | 2.10ms | 5.51ms | 0.86ms | 8.47ms | 51% |
| 420pt | 208/833 | 5 | 1 | 1.38ms | 4.67ms | 0.56ms | 6.61ms | 40% |
| 420pt | 416/833 | 11 | 1 | 0.72ms | 0.49ms | 0.12ms | 1.34ms | 8% |
| 420pt | 625/833 | 27 | 1 | 0.29ms | 0.82ms | 0.13ms | 1.23ms | 7% |
| 420pt | 833/833 | 34 | 1 | 2.14ms | 6.21ms | 0.87ms | 9.22ms | 55% |
| 800pt | 208/833 | 5 | 1 | 1.38ms | 6.25ms | 0.57ms | 8.20ms | 49% |
| 800pt | 416/833 | 11 | 1 | 0.78ms | 0.52ms | 0.13ms | 1.43ms | 9% |
| 800pt | 625/833 | 27 | 1 | 0.29ms | 0.39ms | 0.12ms | 0.80ms | 5% |
| 800pt | 833/833 | 34 | 1 | 2.14ms | 8.32ms | 0.87ms | 11.33ms | 68% |

All of them were run as

```sh
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text \
    ../imgui-adv-text/.venv/bin/python -B ../imgui-adv-text/scripts/bench_draw.py ...
```

with output redirected outside the reference's tree. The reference writes one
file of its own accord, the patched math font, under the temporary directory.

## After the API revision (D36)

The public header changed under these benchmarks (`api-revise`): positions
became bytes, the widget's cache is keyed on content, drawing checks its
context, its backend and its fonts first. What that did to the numbers was
measured as an A/B in one sitting, alternating, five runs each: `before` is
the tree as it stood with only this task's twelve files as they were
(`include/imtypeset.h`, `src/imtypeset.cpp`, `src/context.h`, the benchmarks),
everything else current; `after` is the tree. Same compiler, flags and text
as above. The machine was busier than for the tables above (load average 1.5
to 4.5, other builds running), so the absolute values are some 10% above
them; the ratio is what was measured. Medians of the five medians; `range` is
the five medians of the `after` side.

```sh
for i in 1 2 3 4 5; do for side in before after; do
    $side/bench/bench_draw; $side/bench/bench_reflow --skip growth; $side/bench/bench_font
done; done
```

Rows that moved by more than the spread:

| what | before | after | ratio | range, after |
|---|---|---|---|---|
| widget frame, x1 (34 calls) | 54.9 us | 56.6 us | 1.03x | 56.1 to 58.7 |
| widget frame, x10 (340 calls) | 103.0 us | 119.0 us | 1.16x | 117.6 to 123.5 |
| widget frame, x100 (3,400 calls) | 1.42 ms | 2.04 ms | 1.44x | 1.64 to 2.07 |
| widget frame by ID, x100 (`TextWithID`; before: `NoTextCompare`, which read nothing) | 0.93 ms | 1.93 ms | 2.07x | 1.62 to 2.01 |
| culled, all offered, x10 | 62.6 us | 70.0 us | 1.12x | 68.2 to 71.7 |
| culled, all offered, x100 | 479 us | 641 us | 1.34x | 338 to 683 |
| LayoutParagraph with nothing changed, 34 paragraphs | 1.14 us | 1.49 us | 1.31x | 1.46 to 1.71 |
| first hit test after a layout | 6.57 us | 7.95 us | 1.21x | 7.84 to 8.26 |
| hit test, per call | 48.9 ns | 63.7 ns | 1.30x | 62.5 to 65.8 |
| selection rectangles, everything selected | 250 ns | 291 ns | 1.16x | 291 to 301 |
| the first frame that draws the formula's paragraph | 412 us | 311 us | 0.75x | 296 to 318 |

Rows that did not: full draw at x1, x10 and x100 (1.01x, 1.00x, 1.00x),
culled with the host skipping (0.97x to 1.02x in twelve rows), every `sample:`
and `mixed:` row (0.95x to 1.01x), draw one paragraph with and without a
selection (0.97x, 1.01x), range selection by character, word and line (0.96x
to 0.99x), copy (1.01x), every reflow, text-to-lines and append row (0.98x to
1.05x), font setup (0.93x to 1.02x per face).

What the rows that moved say:

- **A widget call costs more: about 50 ns at x10 and 110 to 180 ns at x100**
  (3,400 calls, every paragraph object cold in the cache by the time it comes
  round). What was added to each call is known: a hash of the text to find
  its layout (the default entry), the checks that the ImGui context is the
  one the fonts are in, that a frame is open and that the backend updates
  textures, and the call's status. **Which of them the time goes to was not
  isolated**: there is no profiler on this machine, and two changes made on a
  guess (the cache's list is no longer reordered on every hit; a paragraph's
  footprint is recounted only when it is laid out) did not move the row. The
  by-ID entry costs the same as the default one, so it is not the hash alone.
- **`culled, all offered, x100` is the bimodal row** the list above says was
  not investigated; its five medians here run from 338 us (below every
  `before` run) to 683 us. One sitting earlier, with nothing else changed
  but the cache's list, it measured 446 us before and 338 us after. No
  conclusion is drawn from it.
- **Hit testing is 15 ns slower a call** because it now also answers the line
  and whether the point is on text (two bisections over the lines); the first
  one after a layout builds the lines as well as the selection model.
- **`LayoutParagraph` with nothing changed is 10 ns slower a call**: the font
  check, and the status it reports.
- The first frame of the formula's paragraph moved the other way, and so
  did two rows that time the registry directly (`register_font` 1.01 ms to
  0.85 ms, `add_face_to_atlas` 4.50 ms to 3.96 ms), whose code is the same
  on both sides of the A/B. So the font rows wander by that much in this
  sitting, as the top of this document says they do between sittings, and
  nothing is claimed from them. (This change does read a font file once and
  register it from memory, where it was opened and parsed from its path
  twice.)

What a width change costs was not a benchmark row and is now a test
(`a_width_change_lays_out_what_is_in_view_and_the_rest_when_it_is_seen`): 200
paragraphs in a 400 px window, one step of a resize, 200 layouts before and 2
after, 198 deferred until they are scrolled to.

The growth curve with the work budget, `bench_reflow --only growth`, one run:

| | x1 (8 lines) | x4 (30 to 32) | x16 (120 to 128) | x64 (480 to 512) |
|---|---|---|---|---|
| first fit, justified | 0.038 ms | 0.153 ms | 0.63 ms | 3.4 ms |
| first fit, ragged right | 0.040 ms | 0.159 ms | 0.68 ms | 3.7 ms |
| justified, total fit | 0.096 ms | 0.93 ms | 10.1 ms | over budget: 3.3 ms a reflow |
| ragged right, total fit | 5.8 ms | over budget: 0.153 ms a reflow | 0.63 ms | 3.2 ms |

A row marked over budget is `ImTypesetStatusFlags_FirstFitFallback`. Its
figure is the reflow that follows, which asks for first fit outright (D33);
the layout that first runs out pays the budget, about 85 ms in this build,
and that first layout is not in the row.

## After the second hardening pass (task `harden-2`)

Release, GCC 12.2, the same machine as above, with two other builders at work on it (load average
2 to 4). Every number is the minimum over three runs, the binaries compared being run in turn
(a, b, c, a, b, c, ...); rows that no change touches moved by about 1 % between those runs, which
is the noise to read the rest against.

### X3: writing a quad through ImGui's public functions

`bench_draw --only anatomy`, the row "sample: Drawer into a draw list" (3,918 glyphs, one key), us.
Taken on the loop as it stood half way through the task, the three differing in nothing else:

| how a glyph's quad is written | min of 3 | against the first row |
|---|---|---|
| own pointers into the reserved space; `_VtxWritePtr`, `_IdxWritePtr`, `_VtxCurrentIdx` set when a batch ends (kept) | 70.2 | — |
| `ImDrawList::PrimRectUV` per quad (public, not inline) | 80.9 | +15 % (2.7 ns a glyph) |
| `PrimWriteIdx` x 6 and `PrimWriteVtx` x 4 per quad (public, inline; still reads `_VtxCurrentIdx`) | 72.5 | +3 % (0.6 ns a glyph) |

The members stay.

### What this task cost the draw loop

The same section, before the task and at its end, us:

| row | before | after | |
|---|---|---|---|
| sample: placement alone, no vertices | 41.5 | 43.0 | +0.4 ns a glyph |
| sample: Drawer into a draw list | 66.6 | 71.7 | +8 %, 1.3 ns a glyph |
| sample: Drawer into buffers | 68.8 | 68.1 | -1 % |
| full draw, x1 (the public path) | 102 to 107 | 112 | one run each, hours apart: not a comparison |

Placement asked of every glyph whether it is a formula's (`DrawRules::rule_joins_span`, which
`d7-fix` has since replaced: next section but one). The
emit loop now also writes the overlay's outlines and sends rectangles through `PrimRect`; with that
in it GCC's code for the glyph path into a list is about 0.9 ns a glyph slower however the rest
was arranged (a cursor held by value, the test for a glyph first, a guard object or none: 67.9 to
74.2 over seven arrangements, none back to 66.6), while the same loop into buffers got faster.
Not chased further; it is the first thing to look at if a host needs the nanosecond back.

### X4: where a widget call's time goes

`gprof` (`-O3 -pg`) over `bench_draw --only x100` and `--only x10`; self time per call, which
`-pg` inflates and which rests on few samples at x10: read as proportions.

| function | x10: 340 calls a frame | x100: 3,400 calls a frame |
|---|---|---|
| `text_widget` itself (hash, compare, the paragraph's fields) | 44 ns | 194 ns |
| `widget_slot` (the cache's `std::unordered_map`) | 22 ns | 145 ns |
| `evict` | below one sample | **139 ns** |
| `resolve_style` | 37 ns | 32 ns |
| ImGui's `ItemSize`, `ItemAdd`, `IsRectVisible` | about 15 ns | about 23 ns |

At 3,400 calls the cache is over its limit of 2,048 paragraphs with every one of them in use, and
`evict` went over the whole list at the first call of every frame: 3,400 unlinks and links, each a
few cache misses. That is now skipped when the two frames' counts show nothing can be dropped. The
rest of the growth from x10 to x100 is the same code missing the cache: the paragraph's fields, the
map's nodes, and the text itself, which is read twice a call (hashed where the host keeps it,
compared where the cache keeps it) at about 600 bytes a paragraph — 4 MB a frame at x100.

"widget frame", us, before and after the change to `evict`, three rounds in turn:

| row | before | after | |
|---|---|---|---|
| x100, by content | 1544, 1558, 1562 | 1314, 1335, 1334 | -15 % |
| x100, `TextWithID` | 1503, 1470, 1515 | 1299, 1282, 1280 | -14 % |
| x10, by content | 112.5, 111.1, 111.8 | 112.5, 112.6, 113.5 | unchanged: under the limit |
| x10, `TextWithID` | 105.6, 104.5, 105.2 | 106.2, 105.6, 106.6 | unchanged |

(The x100 rows read 935 and 901 in the one run made before this task began, on a quieter machine
and with a bimodal spread, minimum 935 and median 1,589; they are not comparable with the table,
whose two columns were taken minutes apart.)

What a content-keyed call costs over one keyed by an ID is the hash: 6 to 7 us of 340 calls at
x10, about 20 ns a call at 600 bytes. The comparison of the text is paid by both. What is left of
the difference from a pointer-keyed call is that comparison and the cache misses above; see D39
for what was not done about them.

## After `d7-fix`: a formula placed whole (D47)

Placement asks of every glyph whether a formula placed whole is open (one test of a flag that is
never set under the reference's rules; `DrawRules::formula_placed_whole`). Release, GNU 12.2.0
`-O3`, FreeType, `bench_draw` built from the sources before and after d7-fix and run alternately,
three rounds, load average 1.6 to 1.9; medians in us for 3,918 glyphs:

| row | before (`rule_joins_span`) | after |
|---|---|---|
| sample: placement alone, no vertices | 42.96 / 43.83 / 43.56 | 43.62 / 44.16 / 44.33 |
| sample: Drawer into a draw list | 72.25 / 73.55 / 73.01 | 72.73 / 73.66 / 73.75 |
| sample: Drawer into buffers | 68.45 / 70.16 / 69.36 | 69.03 / 69.97 / 70.18 |
| mixed: placement alone, no vertices | 47.72 / 48.52 / 48.13 | 49.04 / 49.34 / 49.79 |
| mixed: Drawer into a draw list | 78.93 / 80.77 / 80.08 | 80.65 / 81.20 / 81.98 |
| full draw, x1 | 109.5 / 110.9 / 110.1 | 110.1 / 110.7 / 112.9 |

So within about 1 % on the sample and 2 % slower on the mixed page, which is at the edge of the
run-to-run spread. With the test compiled out placement alone is 42.4 to 42.6 us (two runs): the
question costs about 0.4 ns a glyph, and the rest of what harden-2 measured (71.7 against 66.6 us)
is not in it. The benchmark draws under the reference's rules and holds no formula, so the cost of
placing a formula whole is not in these rows; per glyph it is the same arithmetic as a glyph of a
word, and the look-back and the second pass over a formula's rules are gone. Command:
`( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw )`, rows as named.

Two shapes of the same rule that were measured and not kept, because they are worth knowing:
choosing between two rounded expressions per glyph (a formula's or a word's) cost 4 % on the sample
and 8 % on the mixed page; the kept shape swaps the anchor a formula's glyph is placed from and
leaves one expression.

## After `word-gaps`: words held and spaces led (D48)

Holding words and leading spaces (`DrawRules::word_width_held`, `word_spaces_led`) cost the
public draw about a tenth. Release, GNU 12.2.0 `-O3 -DNDEBUG -ffp-contract=off -fno-exceptions
-fno-rtti`, FreeType 2.12.1, AMD Ryzen 7 PRO 4750U; two builds of one snapshot of the tree that
differ only in `src/draw.cpp` and `src/draw.h`, run alternately eight times with the load average
between 1.7 and 3.5, `( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw )`; the
figure is the median of the eight medians, in us unless marked:

| row | before | after | |
|---|---|---|---|
| full draw, x1 (product rules) | 110.8 | 122.4 | +10.5 % |
| full draw, x10 | 1342.6 | 1452.5 | +8.2 % |
| full draw, x100, ms | 13.54 | 14.67 | +8.3 % |
| culled, host skips, x1, scroll 0.25 | 48.06 | 51.93 | +8.1 % |
| widget frame, x1, scroll 0.25 | 56.25 | 61.55 | +9.4 % |
| draw the paragraph (498 glyphs) | 10.51 | 11.75 | +11.8 % |
| sample: Drawer into a draw list (reference rules) | 73.2 | 74.7 | +2.0 % |
| sample: placement alone (reference rules) | 43.85 | 45.5 | +3.7 % |
| mixed: Drawer into a draw list (reference rules) | 81.0 | 83.3 | +2.9 % |
| mixed: placement alone (reference rules) | 49.2 | 51.6 | +4.9 % |

That is about 3 ns a glyph under the product's rules, of which the hold is about two thirds
(measured on "placement alone" with each rule switched on by itself: +6.5 us and +3.3 us of
47.1). The rows through `detail::Drawer` run under the reference's rules and pay only for the
state the two rules keep. Three shapes of the hold were measured before this one: finding the gap
to give by walking the word's commands again (+12 us on the same row), keeping the largest
rounding up and down as each glyph is placed (+12 us, with or without branches), and the one
kept, which stores each gap's rounding with its op (`DrawOp::x1`) and looks only at the ops of a
word that is out. A word out by more than a pixel has its gaps selected, not found one pass at a
time: a megabyte with no space in it is placed in one pass (`test_stress_text`, 2.0 s for the
whole test in debug, where a pass a pixel took 362 s).

`bench_draw` had no row that draws through `detail::Drawer` under `DrawRules::product()` when
this was measured; the per-rule figures above came from a private copy of the benchmark with the
rules set from an environment variable. Task `draw-seams` added the rows (next section).

## After `draw-seams`: one leader along a line (D50)

Two rows were added to `bench_page`: "Drawer into a draw list, DrawRules::product()" and
"placement alone, DrawRules::product()" (the other Drawer rows place under the reference's rules,
which is what a `DrawArgs` is made with).

Before and after this task: two release builds (GNU 12.2.0, `-O3 -DNDEBUG -ffp-contract=off
-fno-exceptions -fno-rtti`, FreeType 2.12.1, Ryzen 7 PRO 4750U) of one snapshot of the tree
differing only in `src/draw.cpp` and `src/draw.h`, run alternately 8 times each,
`( ulimit -v 6000000; timeout 300 build/draw-seams-rel{A,B}/bench/bench_draw )`, load average 3.6
to 4.0 (other builders were compiling; word-gaps measured at 1.7 to 3.5 and its absolute figures
are some 25 % lower for the same code). Rows this task cannot have touched moved by -4 % to
+8 % between the two builds (copy -4.0 %, a culled row +8.1 %): that is the floor. Median of the
8 medians, us:

| row | before | after | |
|---|---|---|---|
| full draw, x1 (public API: product rules) | 161.6 | 158.4 | -2.0 % |
| full draw, x10 | 1814.7 | 1815.8 | +0.1 % |
| full draw, x100 (ms) | 18.00 | 17.99 | -0.1 % |
| widget frame, x1, scroll 0.25 | 78.7 | 77.9 | -1.0 % |
| draw the paragraph (498 glyphs) | 14.46 | 14.13 | -2.3 % |
| sample: Drawer into a draw list (reference rules) | 92.9 | 90.7 | -2.4 % |
| sample: placement alone (reference rules) | 56.6 | 53.0 | -6.4 % |
| sample: Drawer into a draw list, product rules | 105.9 | 104.6 | -1.3 % |
| sample: placement alone, product rules | 68.0 | 67.3 | -1.1 % |
| mixed: Drawer into a draw list, product rules | 112.7 | 112.9 | +0.1 % |
| mixed: placement alone, product rules | 74.4 | 73.6 | -1.1 % |

**No change that the measurement can see.** An earlier series of the same 16 runs at load 8 to 9
put the same rows between -1 % and +5 % with untouched rows between -7 % and +5 %; it says the
same thing with less authority. What the product's rules cost over the reference's in one build,
same run: placement alone 53.0 -> 67.3 us on the sample (3.6 ns a glyph), 61.5 -> 73.6 on the
mixed page; a draw into a list 90.7 -> 104.6 (+15 %). The per-glyph path inside a word is the
code it was; what was added runs once per word, per formula and per formula glyph.

## A benchmark started cold reads high (found by the `docs` task, 2026-10-10)

Measured while checking the README's draw figure, and it qualifies every table in this file
that was taken from a single run.

`bench_draw` prints "full draw, x1" first. One binary (release preset, GNU 12.2.0 `-O3`,
FreeType 2.12.1, the tree of 2026-10-10 after `draw-seams`), run as
`( ulimit -v 6000000; timeout 300 build/<dir>/bench/bench_draw --only x1 )`, median of 3,000
samples each time, us:

| how it was started | "full draw, x1" |
|---|---|
| after 12 s with nothing running | 210.8 |
| after 12 s with nothing running and then 3 s of one busy loop | 211.2 |
| sixteen times back to back, no pause | 210.4, 213.6, 211.5, 188.6, then twelve between 125.5 and 128.3 |
| six times, 4 s apart | 209.4 to 211.9 |
| four at once | 210.0 to 211.9 |
| after 10 s of eight busy loops | 126.1 |
| a second, byte-identical binary from another build directory (same md5) | 127.9 and 127.4 straight after the build that made it; 210.0 to 210.8 a minute later; and the first binary 125.6 to 128.4 at the end of a loop of runs where it had read 210.6 and 211.5 cold |

The whole first part of a cold run is slow by about the same factor (culled 88 us, widget frame
106 us, full draw x10 2.47 ms, where `word-gaps` recorded 52, 62 and 1.45 warm), and the rows
printed later in the same run are not: "sample: Drawer into a draw list" read 74.0 us in a run whose full draw
read 210.6. So it is the first six to eight seconds of work after an idle spell, not the code
and not the load. `scaling_governor` is `schedutil`, and `/proc/cpuinfo` showed cores between
1,397 and 4,047 MHz just after a run: the processor's frequency coming up is the likely cause,
which is an inference; nothing here measured the frequency during a run. Not the cause: compile
flags (the two binaries are the same file) or the load average (0.4 for one of the cold runs).
Six warm runs under `setarch -R` read 125.6 to 128.7, the same as without it.

What it means for the figures above:

- The headline table was five runs in a loop and the `word-gaps` and `draw-seams` comparisons
  were eight alternations on a machine that was compiling, so they were taken warm, and their
  ratios stand. Their "before" and "after" would both have been inflated alike had they been
  taken cold.
- "full draw, x1" on today's tree, warm: 125.5 to 128.3 us (the twelve runs above), against
  122.4 us recorded after `word-gaps`. That is the figure the README quotes.
- A single run on an idle machine overstates the public-API rows of `bench_draw` by about 1.7
  times and understates nothing. `bench_reflow` and `bench_font` were not examined for the same
  effect.
- Since task `final-touches` every benchmark keeps its thread busy before its first row
  (`warm_up()` in `bench/bench_common.h`, called from `header()`; 8 s unless
  `IMTYPESET_BENCH_WARMUP_S` says otherwise) and prints a line saying so. Eight because the table
  above has three seconds of one busy loop not enough and six to eight seconds of back-to-back
  runs enough. **Not measured:** that it removes the effect. The load average was 5.3 to 5.7
  from other work when it was added, so nothing could be started cold; `bench_draw --only x1`
  read 161.4 us with the warm-up off and 148.4 us with it on, one run each, which on a machine
  that busy is not a result either way. What the rows measure is unchanged.

## `easy_line` (task `tex-fixes`, D41)

`TexRules::easy_line` gives the optimiser `tex.web`'s classes of line numbers (§835, §848, §850):
past the last specially shaped line every line number is one class, so a breakpoint keeps at most
one active node per fitness class however many lines led to it. `TexRules::product()`, which the
public API uses, has it on. The reference keeps a node per line count, which is where the growth
measured above came from.

`build/<dir>/bench/bench_reflow --only growth`, release, on a machine with a load average of 3 to
5 (another task was building), so read the ratios and not the last digit. The second and third tables raise the work
budget to `MAX_LINE_BREAK_WORK_BUDGET` so that the optimiser is not cut short; they differ only in
the rule. "work" is `LineBreaker::work_done()`, the unit the budget is counted in.

| the optimiser alone, budget at its maximum | x1 | x4 | x16 | x64 | growth per 4x |
|---|---|---|---|---|---|
| justified, a class per line count | 0.092 ms (work 792) | 0.90 ms (9,881) | 9.9 ms (115,938) | 133 ms (1,704,259) | 9.8x, 11.0x, 13.4x |
| justified, `easy_line` | 0.089 ms (792) | 0.53 ms (5,468) | 2.3 ms (24,404) | 10.0 ms (97,218) | 5.9x, 4.4x, 4.3x |
| ragged right by total fit, a class per line count | 5.8 ms (74,607) | 105 ms (1,363,730) | 1.73 s (22,380,790) | 21.5 s, then over the maximum budget (268,442,463): first fit | 18.2x, 16.5x: quadratic |
| ragged right by total fit, `easy_line` | 0.143 ms (1,613) | 0.63 ms (7,286) | 2.58 ms (29,978) | 10.9 ms (120,746) | 4.4x, 4.1x, 4.2x: linear at this measure |

As a host gets it (product rules, the default budget of 1,048,576 units), same run:

| | x1 (8 lines) | x4 (30 to 32) | x16 (120 to 128) | x64 (480 to 512) |
|---|---|---|---|---|
| justified, total fit | 0.148 ms | 0.88 ms | 3.8 ms | 16.0 ms |
| justified, first fit | 0.060 ms | 0.239 ms | 0.97 ms | 4.6 ms |
| ragged right, first fit | 0.063 ms | 0.236 ms | 0.73 ms | 3.4 ms |
| ragged right, total fit | 0.140 ms | 0.62 ms | 2.6 ms | 10.9 ms |

(Every table times `LayoutParagraph` whole. This one was the first of the run and was taken while
the machine was busiest: its justified rows do the same work as the `easy_line` rows of the table
above and took 1.6x as long. A second run, under a load average of 5.7, gave medians 1.2x to 1.45x
those of every table here and the same ratios between them; `build/tex-fixes-bench1.txt` and
`-bench2.txt` were the two outputs and are not kept. The work counts depend only on which of the
two alternating widths was laid out last.)

- No row falls back to first fit. In "After the API revision" above, justified x64 and ragged
  total fit from x4 up did.
- "Linear" is at a fixed measure. `easy_line` costs about (breakpoints) x (breakpoints that fit
  on a line), so the growth above is in the paragraph's length with the column held at the
  reference's measure. Ragged text by total fit in a column wide enough to hold much of the
  paragraph on one line is quadratic again, and the work budget is the only bound on it: past
  the default 1,048,576 units (roughly words x words-a-line) the paragraph is broken by first
  fit instead, silently, and correctly. Read, by review
  (`.concerns/review-round-3--reviewer--findings.md`, item 2); no row here measures a column
  that wide.
- At x64 the optimiser uses a tenth of the default budget. The budget is unchanged and is now a
  bound on hostile material and not on long paragraphs.
- Ragged right by total fit is 40 times cheaper at eight lines and about 2,000 times at 512.
- The breaks: over the 1,915 paragraph compositions of the core, para and blocks goldens,
  `easy_line` changes the breaks of 42, and of 57 of the 460 ragged para compositions when they
  are broken by total fit; every one is either a paragraph in which §854's free break was taken
  or one of seven built to tie (D41 has the table).

Not measured: the `document`, `heaviest` and `stream` sections of `bench_reflow` were not re-run
for this change; their justified rows are total fit at 8 to 30 lines, where the tables above put
the gain between nothing and 1.7x. `bench_draw` is not affected (layout only). stb_truetype,
clang and MSVC as everywhere else in this document.

## Tables (task `tables-core`, D57)

`bench/bench_tables.cpp`, one run, release (GNU 12.2.0, `-O3 -DNDEBUG` plus
`-ffp-contract=off -fno-exceptions -fno-rtti`), FreeType 2.12.1, AMD Ryzen 7 PRO 4750U, a
four-second warm-up. **The machine was not quiet:** load average 3.9, two helpers compiling.
Read the ratios; a quiet run was not taken. Tables of four columns, short cells, at the
reference's 420 pt measure and at four times it.

| what | n | min | median | p95 | unit |
|---|---|---|---|---|---|
| 100 rows, fitted to the measure (every row wraps): describe and lay out | 30 | 620 | 627 | 662 | us |
| 1,000 rows, the same | 30 | 9.80 | 9.94 | 10.09 | ms |
| 10,000 rows, the same | 5 | 81.3 | 87.8 | 89.4 | ms |
| 1,000 rows at four times the measure (no row wraps) | 30 | 4.36 | 4.40 | 4.44 | ms |
| 10,000 rows, the same | 5 | 66.6 | 67.1 | 69.2 | ms |
| a row appended to a held table of 10 rows, and the table laid out | 200 | 6.5 | 23.4 | 36.0 | us |
| ... of 100 rows | 200 | 18.0 | 31.5 | 46.7 | us |
| ... of 1,000 rows | 200 | 138 | 155 | 187 | us |
| ... of 10,000 rows | 200 | 2,063 | 2,140 | 2,389 | us |
| a row appended to a held table of 100 rows that widens a column | 20 | 191 | 351 | 660 | us |
| ... of 1,000 rows | 20 | 1,978 | 2,261 | 3,068 | us |
| a 768 px window in the middle of 100 rows, drawn | 500 | 20.4 | 20.6 | 20.7 | us |
| ... of 10,000 rows | 500 | 20.0 | 20.2 | 20.8 | us |

What the rows say.

- **A row that moves no column sets one row**: 1.0 rows set and 4.0 cells measured an append at
  every size (the counters, printed in the note column), where a row that widens a column sets
  every row once (111.5 and 1,011.5 an append, the tables growing as they are timed).
- **The append is not flat all the same**: about 0.2 microseconds for each row the table has
  (155 us at 1,000 rows, 2.1 ms at 10,000). Nothing is typeset for the rows that stand; what is
  left is passes over the whole table that the append still makes: the widest cell of each
  column is taken again over the cells' kept widths, the line index and the index-to-byte map
  of the block are built again from all of its commands, and the held widths are taken again
  over every cell. Before the stacking was resumed from the rows that stand, the same rows cost
  587 us and 6.4 ms (the block's commands were copied and renumbered for every row).
- A table is drawn at the cost of what the window shows, whatever its length.
- Setting a table costs about 7 us a row unwrapped and 9 to 10 us a row wrapped, 10,000 rows in
  under a tenth of a second.

*Not measured:* a table of the limit (262,144 cells) in release (in debug the test that sets one
passes inside its time limit); the cost of the first hit test or selection after an append,
which builds the block's view from all of its commands; a table with formulas; memory.

### After task `tables-fixes` (D64): an append costs a row

The same benchmark on the tree after `ce4d66d` (release, GNU 12.2.0 `-O3 -DNDEBUG` plus
`-ffp-contract=off -fno-exceptions -fno-rtti`, FreeType 2.12.1, an eight-second warm-up, one
run). **The machine was not quiet:** load average 3.0. The rows above this heading are the tree
of `be40444` and are kept for the comparison.

| what | n | min | median | p95 | unit |
|---|---|---|---|---|---|
| a row appended to a held table of 10 rows, and the table laid out | 200 | 3.6 | 6.0 | 9.8 | us |
| ... of 100 rows | 200 | 3.6 | 4.3 | 8.4 | us |
| ... of 1,000 rows | 200 | 3.5 | 4.5 | 8.7 | us |
| ... of 10,000 rows | 200 | 3.5 | 4.3 | 4.8 | us |
| a row appended to a held table of 100 rows that widens a column | 20 | 182 | 329 | 652 | us |
| ... of 1,000 rows | 20 | 1,895 | 2,118 | 2,717 | us |
| 4,096 rows appended one at a time to a held table, laid out after each | 1 | 24.3 | | | ms |
| 65,536 rows, the same | 1 | 386 | | | ms |

- **The append is flat**: 4.3 us at 10,000 rows where it was 2,140, and over 65,536 appends
  5.0 us an append for the first 4,096 and 5.0 for the last. The passes over the whole table
  are gone: the widest cell of each column, the longest words, which kept rows stand, the held
  widths, and the line index, colours and index-to-byte map beside the layout are each brought
  up from the rows closed since they were last taken (`TableCells::epoch`).
- By the counters (`tests/test_api_tables.cpp`,
  `a_row_appended_goes_over_that_row_and_not_the_table`, 3,000 appends): one row set, at most 3
  rows gone over by layout (5 in a table fitted to 500 px), one row's lines and index map made,
  an append.
- A row that widens a column still sets every row, as it must.

*Not measured:* any of this on a quiet machine; an append followed by a draw (the draw's cost is
the window's, measured above; what is kept beside the layout is brought up by the counters'
one row); the first hit test, caret or selection in the table after an append, which still
copies the block's commands into a view.

## Columns (task `columns-core`, D58)

No benchmark was written. These are single timings printed by the tests in a release build
(GNU 12.2.0, `-O3 -DNDEBUG` plus `-ffp-contract=off -fno-exceptions -fno-rtti`; private
directory `build/columns-core-rel`, one run, another build finishing on the machine), and the
work counters, which do not depend on the machine. 10,000 paragraphs of 20 to 40 words;
`tests/test_columns.cpp` (a fake face of round numbers, 200 pt column) and
`tests/test_api_columns.cpp` (the public calls, Roboto at 16 px, 300 px column).

| what | work, by the counters | time |
|---|---|---|
| filled in order, a frame that shows three columns of 600 px | 14 blocks laid out (the columns hold 14), 3 columns cut | not timed |
| the same frame again; a hit test; the size; the count | 0 blocks, 0 columns | not timed |
| a frame of columns 10 to 12 | 62 blocks laid out in all (they end at block 57), 14 columns cut in all | not timed |
| every column (2,308 of them) | 10,000 blocks laid out | not timed apart from the layout |
| cutting 10,000 laid-out blocks into 1,177 columns again (no block is laid out) | 1,177 scans over 84,000 nodes | 4.6 ms |
| text appended to the last block, every column already cut | 1 block laid out, at most 2 columns cut | not timed |
| balanced into five or six: the first question | 10,000 blocks laid out, 36 heights tried | the layout, plus the next row |
| balancing 10,000 laid-out blocks again, which is what any edit costs | 36 passes over 84,000 nodes, 5 columns cut | 29 ms |
| `split_into_columns` at the core, 200,000 lines into 20,000 columns | one pass | 9 ms |

What the rows say.

- **Filled in order, a frame costs what its columns hold, plus every column before them once.**
  Nothing stands in for the columns before: where a column starts is where they ended.
- **Balancing needs every height, and an edit to a balanced document of 10,000 paragraphs costs
  about 29 ms** with no block laid out again: the bisection makes 36 passes over the document's
  lines. It scales with the lines (about 3 ms at 1,000 paragraphs, by proportion, not measured).
  A document that streams should be filled in order.
- The estimate of the column count made after three columns was 2,653 against 2,308 (the
  estimate is the document's estimated height over the column's, and a block that has never
  been laid out is itself an estimate).

*Not measured:* any of this on a quiet machine or more than once; the draw of a column against
the draw of the same lines uncut (a block a cut falls in is drawn without its line index, so
its lines outside the clip are not skipped before their commands are visited); memory (a
balanced document keeps a node, a height and a unit number for each line and gap while its
columns stand: about 44 bytes each, 3.7 MB at 84,000); columns of tables and code.

## Marks (task `handoff-core`, D69; rows taken by task `v030-gate`, D76)

`bench/bench_marks.cpp`: one code block of 600 lines with 1,881 marks on it
(three colour spans a line, as a syntax highlighter hands them over), drawn
through a window of 768 px, warm.

**Not a result from a quiet machine.** Two runs one after the other on
2026-10-12, with the load average at 4.4 to 5.0 from other sessions' work
before, between and after them, from a private directory configured as the
`release` preset (`-O3 -DNDEBUG -ffp-contract=off -fno-exceptions -fno-rtti`,
GCC 12.2.0, FreeType 2.12.1, each after the benchmark's own eight seconds of
warm-up). Read the ratios between rows of one run:

```sh
cmake --preset release -B build/<dir> && cmake --build build/<dir> --target bench_marks
build/<dir>/bench/bench_marks
```

| a draw (2,000 samples each) | run 1: min / median / p95, us | run 2 |
|---|---|---|
| no marks | 36.5 / 37.0 / 38.6 | 35.5 / 36.2 / 48.1 |
| every mark, Ink, in text order | 69.6 / 70.8 / 74.1 | 66.9 / 68.0 / 72.3 |
| every mark, Ink, shuffled (the same picture) | 95.6 / 97.2 / 102.0 | 91.6 / 92.7 / 97.7 |
| every mark, Behind, in text order | 71.3 / 72.5 / 75.5 | 66.7 / 67.3 / 80.1 |

Before marks were looked for where the clip is, `handoff-core` measured the
same four rows at 54, 2,766, 8,914 and 710 us on a machine as busy (medians;
D69): that comparison is across two sittings and two trees and is good for
its order of magnitude only. These rows are of the tree with D76 in it, where
a draw with marks makes one more pass over them (a comparison a mark); every
mark of this benchmark is Ink or Behind, so the window is not widened in it.
**Not measured:** the same benchmark on the commit before D76, run
alternately with this one, which is what would say what that pass costs; a
draw with Under marks; a quiet machine.

## Not measured

- **stb_truetype.** Every number is the FreeType build. Layout does not depend
  on the rasteriser; the first-frame and font rows do.
- **Any other machine, compiler or flag set.** GCC 12.2 at `-O3` only. No
  clang, no MSVC, no `-march=native` row, no LTO, no 16-bit `ImWchar` build.
- **A real renderer.** No GPU, no texture upload, no display: the cost of
  ImGui's `Render` and of a backend drawing 15,672 vertices is not in any row.
- **Memory.** No allocation counts or resident sizes were taken here
  (`tests/test_api.cpp` asserts zero allocations in a steady frame; this
  document adds nothing to that).
- **A document with headings, code and tables.** The port sets paragraphs.
- **Runs of mixed faces through the public API.** Every public-API row is one
  face at one size; the eight-key page goes through `detail::Drawer`.
- **Formulas at scale.** One paragraph with three formulas (29 us to build,
  14 us to reflow) and the first formula in a context (0.11 ms). No page of
  mathematics, no display blocks.
- **The first layout in a process** for prose (the measure cache filling) has
  no row of its own; it is inside the first sample of `text to lines`, which
  is discarded as warm-up.
- **Cold file cache.** Font files were read from memory after the first sample.
- **Line-by-line agreement of the two sides' breaks** on the sample: counts
  only.
- **The reference's markdown parse, find and diff costs**, which the design
  note names and the port does not have.
- **Whether `culled, all offered, x100`'s two modes** are the machine or the
  code.
- **The profile after inlining.** gprof needed inlining of small functions
  turned off, so its percentages are of a slower program than the one timed.
- The draw loop against ImGui 1.93.
- What a one-entry memo of `resolve_style` or a flat table for the widget cache would save.
