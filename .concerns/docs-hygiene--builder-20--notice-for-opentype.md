# For task `font-wild` (or whoever next owns `src/opentype.cpp`): the fontTools notice to insert

From `docs-hygiene` (builder-20), 2026-10-09. `src/opentype.cpp` was being
rewritten by task `stress` when this was written, so the edit was not made.
Ruling it implements: `docs/port-hazards.md`, "RULING — fontTools-derived
arithmetic stays, attributed"; review item L1.

## Confirm first

The defect is the absence of a copyright and permission notice. Check it is
still absent: `grep -n "Just van Rossum" src/opentype.cpp` printed nothing on
2026-10-09. If it prints something, somebody did this already.

## Where

Immediately above `struct OpenTypeFont::BoundsPen`, replacing the four-line
comment that begins `// fontTools' BoundsPen, operation for operation:` (under
the `// --- The pen ---` rule). The block below keeps what that comment said.

## The block

The licence lines are the 21 lines of
`../imgui-adv-text/.venv/lib/python3.11/site-packages/fonttools-4.66.0.dist-info/licenses/LICENSE`
with `// ` put before each and nothing else changed (this file was generated
from that one, not typed). Do not re-wrap them; if clang-format wants to, fence
the block with `// clang-format off` / `// clang-format on`.

```cpp
// fontTools' BoundsPen, operation for operation: the doubles it produces are
// the reference's, so the order of every expression here is fontTools' and is
// not to be rearranged. tools/oracle/FORMAT-font.md states the procedure and
// tools/oracle/check_font_bounds.py is the same thing in Python.
//
// UPSTREAM NOTICE. These routines in this file follow fontTools 4.66.0
// expression for expression, because bit-equal results need the same
// operations in the same order:
//   - BoundsPen::solve_quadratic: solveQuadratic, misc/bezierTools.py;
//   - BoundsPen::curve_to: calcCubicParameters and calcCubicBounds,
//     misc/bezierTools.py;
//   - BoundsPen::quad_to: calcQuadraticBounds, misc/bezierTools.py;
//   - the hflex1 and flex1 operators of the Type 2 charstring interpreter:
//     op_hflex1 and op_flex1, misc/psCharStrings.py;
//   - the cmap format 4 decode: cmap_format_4.decompile,
//     ttLib/tables/_c_m_a_p.py.
// The rest of this reader is written from the OpenType specification.
// fontTools is distributed under the following licence (THIRD-PARTY.md):
//
// MIT License
//
// Copyright (c) 2017 Just van Rossum
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
```

## At the two other sites

One line each, so a reader who lands there finds the notice:

- above the `hflex1` arm of the escape operators (`op == 36`):
  `// hflex1 and flex1 follow fontTools; see the UPSTREAM NOTICE above BoundsPen.`
- at the existing comment `// fontTools' index: charCode + rangeOffset/2 - start + i - segCount.`
  in the cmap format 4 decode, append: ` See the UPSTREAM NOTICE above BoundsPen.`

## Afterwards

In `THIRD-PARTY.md`, section "fontTools", delete the paragraph that begins
`**Open:** the notice is not yet at the point of use.` It is true only until
this lands.

If `font-wild` adds more fontTools-shaped code (the task mentions `post`
format 2 names and dropping cmap entries for glyph 0, which fontTools does in
`_make_map`), list it in both places or write it from the specification.

`tools/oracle/check_font_bounds.py` holds the same arithmetic in Python and has
no notice either. It is not in my `touches`; whoever owns it next should give
it the same block as a `#` comment.
