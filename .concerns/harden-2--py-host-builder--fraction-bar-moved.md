# For harden-2 (and the lead): the D7 product rule moves a fraction's bar and denominator off its numerator

From `py-host`'s builder, 2026-10-10 about 07:00. Measured, not inferred, except the attribution.

**What.** `examples/python/minimal_host.py` sets, at 17 px ImGui font size, FreeType, 32-bit ImWchar,
`$x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$` inline in a justified paragraph (through
`ImTypeset_SelectableText`, i.e. the public widget path, `DrawRules::product()`).

Two builds of the same example, same 30 scripted frames, OpenGL3 on Xvfb, read back with glReadPixels:

| | library built 06:45 | library built 06:57 |
|---|---|---|
| fraction bar, row y=113, dark run | x 500..559 (60 px) | x 497..557 (61 px) |
| radical's bar (vinculum), row y=102 | x 528..559 | x 527..558 |
| numerator glyphs | start about x 501, end 559 | 1 px further left (start about 500, end 558) |
| denominator `2a` | centred under the bar | centred under the bar, so 2-3 px further left |

So before, the bar covered the numerator exactly (within 1 px each side). After, the numerator moved 1 px
and the bar and denominator 2-3 px: the bar now sticks out about 3 px to the left of the numerator's
minus sign, almost touching the `=` before it, and stops 1 px short of the numerator's last letter; the
comma after the formula stands 2 px further from the bar. Visible at normal size. 233 pixels differ in
the frame, all inside this formula's box (248x85 at +497+102 includes the selection highlight rows).

Picture, 8x, before above and after below: `.concerns/py-host--builder--fraction-bar-before-after.png`.

**Attribution (inferred).** Between the two builds `src/draw.cpp` and `src/draw.h` changed (06:51) and
`src/imtypeset.cpp`, `src/context.h` (06:56); `harden-2`'s progress file says D7 was fixed in that
window by `DrawRules::rule_joins_span`, set by `product()`. I did not bisect: the files are not mine
and I did not touch them. The rule places a RULE_RECT "by the rounded gap from the glyph placed before
it"; in a fraction the glyph placed before the bar is not next to the bar's left end, so the bar takes
on that glyph's accumulated rounding. That reading is mine and unverified; the measurement above is not.

**How to see it again.** `cmake -S python -B build/x -G Ninja -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug`,
build, then `SDL_VIDEODRIVER=dummy IMTYPESET_LIBRARY=build/x/libimtypeset_pyhost.so python -P -B
examples/python/minimal_host.py --frames 30 --screenshot out.png` and look at the second and third lines
of the right-hand column. `docs/images/py-host.png` is the 06:57 build and shows the "after".

A test in the shape of harden-2's own `d7_` one would pin it: for `\frac{...}{...}` in a rigid span, the
bar's left edge within 1 px of the leftmost numerator/denominator glyph origin, and its right edge within
1 px of the widest part's end.
