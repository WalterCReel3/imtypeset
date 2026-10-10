[from builder-2] linebreak done (filed by lead; summary of the report)

Literal transcription: each candidate line is built and judged by hpack; no
prefix sums, no delta nodes. Active list kept as two sorted runs (survivors,
created), merged survivors-first on ties. All 488 runs (137 cases, 2,836 lines)
match as text; debug, ASan+UBSan, release. 20 mutations: 18 fail, 1 equivalent,
1 exposed a hole — no golden lands on the fitness thresholds 12/13 and 99/100;
covered by 12 hard-coded reference answers in tests/test_linebreak.cpp (task
`oracle-core-thresholds` moves them into the goldens).

API: LineBreaker is the reusable scratch (zero allocations in steady state,
tested by counting operator new); Paragraph is caller-owned output with lines
{PackResult, BOX node, shift_amount}, breaks (position into the PREPARED list +
kind), total_demerits, pass_used, error, pack_error. ParShape never allocates;
an empty shape is LineBreakError::EMPTY_PARSHAPE. Numeric params clamped to
+-2^40. Disc.replace_count: negative -> 0, overrun bounded by the list.

Timing, indicative only (throwaway program, one run, -O2, min of 300): the
reference's 635-node paragraph breaks in 0.12-0.68 ms total-fit and
0.08-0.10 ms first-fit; the reference's own docstrings say ~30 ms and 5-9 ms.
The reference's timing was not re-run here.

Not done: tex.web / tex-notes.md not read (task `tex-web-pass`); the
cost-counting and flip-detector tests of test_firstfit.py not ported.
