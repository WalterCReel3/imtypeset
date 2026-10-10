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

Placement now asks of every glyph whether it is a formula's (`DrawRules::rule_joins_span`). The
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
| ragged right by total fit, `easy_line` | 0.143 ms (1,613) | 0.63 ms (7,286) | 2.58 ms (29,978) | 10.9 ms (120,746) | 4.4x, 4.1x, 4.2x: linear |

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
