# bench — result (builder-16)

Files: `bench/bench_draw.cpp` (rewritten; the draw task's anatomy rows kept),
`bench/bench_reflow.cpp`, `bench/bench_font.cpp`, `bench/bench_common.h`,
`bench/spread.py`, `bench/reference_font_setup.py`, `bench/data/sample.txt`,
`tools/sample_text.py`, `docs/performance.md`, `src/linebreak.cpp`.
**Outside the touches I was given, decided and recorded here:**
`src/linebreak.h` (private members and one public switch, see below) and a new
test, `tests/test_linebreak_totals.cpp`.

## Headline (this machine, same text both sides; details in docs/performance.md)

| | reference here | port here | design note |
|---|---|---|---|
| full draw / frame | 12.8 ms | 0.100 ms | 13.7-15.9 ms |
| culled draw, x1 / x10 / x100 | 5.6-5.8 ms flat | 0.039-0.043 ms flat | 5.1-5.9 ms |
| reflow, whole document | 102 ms | 0.465 ms (1.145 before) | |
| streaming append | 0.34-6.2 ms layout | median 0.011, worst 0.110 ms | 1.4-2.2 ms |
| font setup, math face | 0.79 s + 0.05 s | 2.9 ms | ~1 s |

The reference's scripts DID run here headless with `--source
bench/data/sample.txt`; both sides ship 74 lines, 3,918 glyphs, 4,859 commands.

## Review item E, measured

Ragged right (fil right skip) by total fit is quadratic: 5.3 ms, 95 ms, 1.57 s,
26.5 s at x1, x4, x16, x64 of the 635-node paragraph (before the optimisation:
29 ms, 0.56 s, 9.5 s, 156 s). Justified total fit is superlinear too (0.089,
0.86, 9.3, 126 ms), because active nodes are kept per line number. First fit is
linear. No cap added; that is `stress`.

## The one optimisation

Candidate lines in the breaker are judged from running totals over the prepared
list instead of being built and walked (`LineBreaker::tabulate`, `trial`); the
totals go to the real `hpack` as four glue nodes, so there is no second copy of
the glue arithmetic. Paragraphs with a discretionary, a running-width rule or
no measure take the old path. Reflow 2.2-3.2x, the heavy paragraph 3.8x, total
fit growth rows 5-6x. Draw rows unchanged.

Full suite, unnarrowed, private Release + Debug + ASan/UBSan, before and after:
identical test and check counts per executable in all three; only
`test_opentype_wild` fails (expected). No golden touched.

## Decisions taken alone

1. **`src/linebreak.h` edited** though it was not in my touches: the scratch
   vectors have to be members (the alternative was `thread_local` state in the
   .cpp). Private section plus `use_running_totals(bool)`; the header comment
   that said every candidate is re-walked was rewritten to what is true now.
   Nobody else was writing: every other src task waits on this one.
2. **New test file `tests/test_linebreak_totals.cpp`** (6,000 random lists
   broken both ways, 335,325 checks). The goldens cover whichever path a case
   takes; nothing else held the two paths to each other.
3. Draw was profiled and left alone (0.04 ms a culled frame; the rest is pen
   arithmetic held bit-exact).
4. Python-side font timing has no reference script, so
   `bench/reference_font_setup.py` is the traceable source of those rows.

## Not verified

- `stb` and `wchar16` configurations: neither the suite nor the benches.
- The preset directories (`build/release` etc.) were not built; numbers are
  from `build/bench` configured as the release preset is.
- Line-by-line equality of the two sides' breaks on the sample (counts only).
- Font rows moved up to 54% between two sittings with no code change; the
  spread within a sitting is 1-5%.
- gprof only (no perf), with small-function inlining off.
- Three of six hand mutations of the new code did not compile and prove nothing.

## For later tasks

- **stress**: the growth table is in docs/performance.md. If the cap counts
  candidate lines, `LineBreaker::trial` is the one place every candidate goes
  through. `tabulate` indexes glue orders with `& 3`, `hpack`'s `GlueTotal::add`
  does not: a corrupt order is defined in the first and not the second.
- **tex-web-pass**: `tabulate`/`trial` have no TeX names; they stand in for
  what sections 823-827 do with `active_width`/`break_width` deltas.
- **api-revise** owns the bench files next: every public call the benches make
  is in `bench_common.h`, `document_of`/`draw_all`/`draw_window`
  (bench_draw.cpp) and the four section functions of bench_reflow.cpp.
- `add_font` reads and parses each font twice (probe registry): ~1 ms for the
  math face, once per process. Left.
- `culled, all offered, x100` is bimodal (0.26 / 0.44 ms) in every run; cause
  not looked for.
