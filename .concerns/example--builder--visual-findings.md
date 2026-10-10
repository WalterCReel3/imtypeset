# example — what the specimen images show that someone should decide about

Images: `docs/images/*.png`, written by `build/example/examples/example_specimen docs/images`
(FreeType 2.12.1, 32-bit ImWchar). None of these is a crash or a wrong break; 1 is visible to any
user, 2-4 are judgement calls. Nothing in `src/` was edited.

1. **Selection highlight has a white seam under a line that holds a formula.**
   `docs/images/selection.png`, pixel rows 54-55 (between the 2nd and 3rd text lines): the highlight
   of line 2 ends at row 53 and that of line 3 starts at row 56; lines 1 and 2 abut (rows 31/32).
   Line 2 holds `$\sum_{i=1}^{n} i = \frac{n(n+1)}{2}$`, which deepens the line, and the rectangle of
   the next line starts after the extra interline glue. The reference does the same: for the same
   text at 400 px its `SelectionModel.rects` are y 0..21.6, 21.6..44.16, 45.76..67.36 px, a 1.6 px
   gap. So it is inherited, not a port error. Whether the public selection rectangles should be
   made to abut (a departure to record) is the lead's / `api-revise`'s call (R10 adds a
   selectable-text helper, where this will be seen at once).

2. **A refused inline formula's source is unbreakable but its spaces still stretch.**
   `docs/images/math-refused.png`, line 3: `$\begin{matrix} a & b \end{matrix}$` in the code face
   is set with visibly wide gaps around `a`, `&`, `b` because the line is justified. Readable, but
   the "source as typed" is not monospaced any more. Not compared with the reference.

3. **NoBreak run + justification: an overfull line with no report.** While writing the faces page
   I gave a `ImTypesetRunFlags_NoBreak` run a trailing space (`"Hxg code(), "`). The only break
   left was after the next word and line 1 overhung the right margin by about 25 px (TeX's
   behaviour on the final pass). I did not check whether `GetParagraphError()` said anything; the
   example was corrected instead. A first user will do this.

4. **The math face is much lighter than the text face at 16 px.** Latin Modern Math's hairlines
   at a 16 px em render grey beside Roboto (`docs/images/math-inline.png`); a nested fraction
   (`\frac{\frac{a}{b}}{\frac{c}{d}}`) is about 5 px type and not legible. Font and size, not
   layout; the reference SVG shows the same proportions. Worth one sentence in the README.
