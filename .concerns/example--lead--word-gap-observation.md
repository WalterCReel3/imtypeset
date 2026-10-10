[lead] Word gaps in docs/images/paragraph.png — CORRECTED after measuring (2026-10-09, later).

My first note said one justified line had gaps of about 9, 4 and 9 px. That was read by eye off a 4x
enlargement and is WRONG. Measured from the PNG itself (ink-to-ink gaps of 3 px or more, per line):

  ragged     line 4: 5 5 5 6        line 5: 6 7 8 5       (all lines: 5..8)
  justified  line 4: 6 4 7 6 7      line 5: 9 6 10 6      line 1: 10 10 10 10 10 11

So on the justified line "for everyday output, with no macro" the gap after "everyday" is 4 px beside
6-7 px: a 2-3 px spread, and on the next line 9 px after "processor" beside 6. Ink gaps include the
glyphs' side bearings, so part of that is the letters, not the pen.

The reviewer of draw (see review-draw-para-style--reviewer--findings.md) confirmed the mechanism is the
reference's by design (advance-rule.md §1, §3, §4; _RigidSpan.pen), matched line for line by the port,
and bounded it: a gap's error is at most 1 + 0.5 x (glyphs in the preceding word - 1) px, nothing
compounds past the word; the reference's own measured worst word drift is 2.09 px. A 2-3 px spread is
inside that. The ragged column shows the same 5..8 spread at natural spacing, which is what a
span-local drift predicts. No evidence of a layout-side defect.

- `docs`: describe it as it is — word gaps on a line can differ by a couple of pixels because letter
  advances inside a word are rounded to whole pixels (the reference's measured rule) — with these
  numbers, not my earlier ones.
- `api-revise`: the optional alternative pen rule is LOW priority; do it only if it is nearly free.
