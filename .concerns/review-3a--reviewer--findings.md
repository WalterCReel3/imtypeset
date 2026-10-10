# review-3a findings (filed by the lead from the reviewer's concern, 2026-10-10 about 08:00)

EVERYTHING HERE IS READ-DERIVED. The reviewer ran nothing; placements are worked by hand from
`src/draw.cpp` in the units of harden-2's hand test (1 pt = 1 px, FixedBinder). Reproduce before fixing.

## 1. D7: "nearest by origin" joins a rule end to a glyph on another row

`Drawer::settle_span`, the `joined` lambda (`src/draw.cpp` about 665-680), left end at 684, right end at
685. Nearness is measured along the line; the error that matters is the rounding gathered between the
chosen glyph and the ink the rule should meet (up to 0.5 px per gap in placement order).
`src/math_mlist.cpp`: `make_fraction` pushes numerator, rule, denominator; `make_radical` pushes surd,
rule, radicand.

a. Radical in a denominator under a wider numerator (left end).
   `\frac{<21 glyphs 10.4 apart from 50.0>}{\sqrt{x}}`, surd 10 wide, x 8 wide. Numerator pens
   50,60,...,250 (true last 258.0). Surd true 150.2, rule 160.2..168.2, x 160.2. Surd pen =
   250 + snap(150.2-258.0) = 142, right edge 152; x pen 152. Rule left: surd origin is 10.0 away, the
   numerator glyph at 164.4 (pen 160) is 4.2 away and wins: 160 + snap(-4.2) = 156. Right: same glyph
   (3.8 away against x's 8.0): 164. Bar drawn 156..164; sign ends at 152, radicand 152..160: a 4 px
   hole and the bar half off its radicand. Under `DrawRules::reference()` the same input is exact, so
   for this shape product() is a regression. The surd is always a surd-width from its bar, so this is
   the normal case whenever a numerator overhangs a radical.
b. Radical in a numerator over a wider denominator (right end): `\frac{\sqrt{x}}{abcdefghijkl}` — a
   denominator glyph nearer the bar's end than x's origin wins and carries the denominator's rounding.
c. Nested fraction inside a wider part: `\frac{<same 21 glyphs>}{\frac{x}{y}}`, inner bar true
   150.2..158.2: left from x (142), right from the numerator glyph at 154.0 (pen 150) = 154. Bar
   142..154, 12 px over an 8 px x.
d. `d7_a_fraction_bar_reaches_over_its_parts_as_they_were_drawn` (tests/test_harden.cpp about 1491)
   cannot see this: radical only in a numerator over a narrower denominator, and only the WIDER rule
   is measured. The 0.42 / 1.00 px figures cover those four formulas only.

The reviewer's diagnosis: the selection criterion is the defect. A rule needs the glyphs of its own
box (the surd for a radical's left, the radicand's last for its right, the wider part's first and last
for a fraction); "nearest origin within 64 either way" is a proxy that fails when rows overlap.
Formulas that would settle it, measuring EVERY rule against the glyphs of its own box:
`\frac{a+b+c+d+e+f+g+h+i+j}{\sqrt{x}}`, `\frac{\sqrt{x}}{abcdefghijkl}`, `\frac{abcdefghijkl}{\frac{x}{y}}`.

## 2. D7: the narrower part sits off-centre by half the wider part's gathered rounding

Visible in harden-2's own expected values (tests/test_harden.cpp about 1464-1480): bar 48..102, denominator
at 68, where exact is 48..104 and 70. Bound 0.25 px per glyph of the wider part. Conditional example:
`\frac{1}{11111111111}` about 2.5 px off if digits advance half an em at an odd pixel em (not checked).

## 3. D7: past RULE_JOIN_LOOKBACK (64) the left end reverts to drift

`src/draw.cpp` about 660-661. `\frac{<200 glyphs>}{2}`: left end up to 68 px (worst case) from the
numerator's start.

## 4. D7: under product() a rule no longer re-anchors the span (by design, unstated). Low.

## 5. python/imtypeset/__init__.py about 862-865 and 879-884

A -1 (exception caught at the C boundary) from ImTypeset_GetParagraphRangeRects /
ImTypeset_AppendParagraphRangeText becomes [] / "" with no raise. Low. -> api-document.

## 6. src/imtypeset_c.cpp `terminated` (about 299-305)

A (begin, end) ID or file name stops at the first NUL: "a\0b" and "a\0c" are one ID. State it in the
header or refuse. Low. -> api-document.

## Found sound by reading

D7 vertical placement and bookkeeping; D4 atlas repair incl. composites (code only, not its test);
X1 (no try/catch in src/ outside imtypeset_c.cpp; the one PrimReserve followed by infallible work);
the test_draw skip of v/tri/drawcmd for the two debug cases (slightly more than the minimum — those
cases' glyph and filled-rect vertices are no longer compared either — judged acceptable); the
test_math_mlist change; the C API buffer contract; ctypes signatures against the header.

## Not reached

H1/H5 code in src/mathtext.cpp, src/selection.cpp; X1 RAII guards in src/imtypeset.cpp; whether a
format 13 one-glyph range is expanded anywhere other than `cmap_groups`; the rewritten D4 test and
fuzz changes; tests/test_c_api.c, test_c_api_map.cpp, python/host/*, tests/python/test_binding.py.
