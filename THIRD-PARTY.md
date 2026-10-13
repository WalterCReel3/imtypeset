# Third-party material in this tree

What this project took from other people, what it only links or reads, and the
notice each asks for. Written on 2026-10-09 by opening each file named here; a
statement that was not checked says so.

**This project's own licence is the MIT License**: the file `LICENSE` at the
root of this repository, "Copyright (c) 2026 Walter Reel", the owner's choice
of 2026-10-12 (`docs/decisions.md`, D82, which supersedes D24's "no licence
has been chosen"). It is the licence of Dear ImGui and of Dear ImGui Bundle,
and of the Python package this began as a port of (`../imgui-adv-text`, now
imscribe, whose `LICENSE` has the same copyright line). The entries below are
what is in this tree, or in what is built from it, under somebody else's
terms; each was checked for compatibility with an MIT-style licence when it
was added, and D24's rules for what may enter the tree stand.

## Summary

| upstream | licence | how it is used here | in the repository? |
|---|---|---|---|
| Dear ImGui | MIT | linked; two files reproduce its output conventions | no (fetched or `.deps/`, untracked) |
| stb_truetype, stb_rect_pack, stb_textedit | MIT or public domain | compiled inside Dear ImGui only | no |
| fontTools | MIT | three groups of routines in `src/opentype.cpp` follow it expression for expression | derived code, yes |
| FreeType | FreeType License (FTL) | optional rasteriser, linked through Dear ImGui's `misc/freetype` | no |
| Unicode Character Database 14.0.0 | Unicode licence | generated range tables | derived data, yes |
| `tex.web` (D. E. Knuth) | its own notice | cited by section; not copied | no |
| Test fonts | Apache-2.0, OFL 1.1, GUST Font License | test inputs | yes, `tests/data/fonts` |
| SDL2 | zlib | linked by the optional demo host `examples/demo_sdl2` and the self-hosted Python library only; found on the machine or in `.deps/sdl2` | no (`.deps/`, untracked) |
| `advtext`, the Python reference | none stated | the oracle; data derived from it | derived data, yes |

## What a Python wheel of this project carries

A wheel built by `tools/wheel/build_wheel.py` (`docs/sidecar.md`, section 8)
holds one shared library, the Python package, this project's `LICENSE` and
this file, with a `NOTICE.txt` written by the build (the three are in
`<dist-info>/licenses/`, and the metadata names them as `License-File`). Compiled into that library: this project's
own code; the routines that follow fontTools (below); the Unicode range tables
(below); the code that reproduces Dear ImGui's output conventions (below); and
the inline functions of the Dear ImGui headers it was compiled against, which
are the fork's that the imgui-bundle wheel is built from (`pthom/imgui`, MIT,
Dear ImGui's licence) or, in a development build, upstream's. Not in it: Dear
ImGui itself, FreeType, SDL2, any font. Its Dear ImGui symbols are bound at
load to the installed imgui-bundle wheel, which is a requirement of the wheel
and not a part of it; FreeType is inside that wheel, not this one, so the
FreeType disclaimer below falls to whoever ships imgui-bundle. The C++ runtime
is the system's, linked dynamically. Checked for the two wheels built on
2026-10-11 by listing the archive (11 files) and the library's needed
libraries (`_imgui_bundle.*.so`, `libstdc++`, `libm`, `libgcc_s`, `libc`).

## Dear ImGui

MIT. The library is an extension of Dear ImGui and links it. No Dear ImGui
source file is in this repository: `cmake/imgui.cmake` takes v1.92.9 from a
directory the caller names, from `.deps/imgui` (untracked, ignored by
`.gitignore`), or by `FetchContent` from `https://github.com/ocornut/imgui`.

Two files here reproduce what Dear ImGui does closely enough to carry its
notice at the point of use:

| our file | what it follows | upstream file |
|---|---|---|
| `src/draw.cpp`, the two sinks and `Drawer::emit` (the comment headed `UPSTREAM NOTICE`) | the vertex and index layout of `ImDrawList::PrimRect` and `PrimRectUV`; the reserve, write and give-back pattern of `ImFont::RenderText`; the order and strictness of its clip tests | `imgui_draw.cpp` |
| `tests/support/soft_raster.h`, `update_texture()` and the scissor projection in `render()` | the texture state machine a backend implements, and the projection of an `ImDrawCmd` clip rectangle | `docs/BACKENDS.md`, `backends/imgui_impl_opengl3.cpp` |

`tests/support/soft_raster.h` is test support and is not part of the library.
Its rasteriser, sampler and image writers are stated in the file not to be
derived from Dear ImGui. `src/font_loader.cpp` was reviewed for adapted code
and none was found (`.concerns/review-font-stack--reviewer-2--findings.md`,
item L1); it calls Dear ImGui's public API.

The licence, copied from `.deps/imgui/LICENSE.txt` of v1.92.9 (sha256
`173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3`):

```text
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## stb_truetype, stb_rect_pack, stb_textedit

Sean Barrett's single-header libraries, as Dear ImGui vendors them
(`imstb_truetype.h` v1.26, `imstb_rectpack.h` v1.01, `imstb_textedit.h` v1.14).
Each file's own closing section offers two licences, "choose whichever you
prefer": the MIT License ("Copyright (c) 2017 Sean Barrett") or the public
domain.

**No source file of this project includes or adapts them.** They are compiled
only as part of Dear ImGui (`imgui_draw.cpp` includes the first two,
`imgui_widgets.cpp` the third). Checked by searching `src`, `include`, `tests`,
`examples` and `bench` for an `#include` of `imstb_*` or `stb_*`: none.
`src/opentype.h` records why `imstb_truetype.h` is not used for metrics (its
glyph box is the header's or the control points', not the curve extrema the
reference computes). When `IMTYPESET_USE_FREETYPE` is off, stb_truetype is the
rasteriser the atlas uses, inside Dear ImGui.

## fontTools

MIT. fontTools 4.66.0 is what the Python reference reads fonts with, and the
goldens under `tests/golden/font` and `tests/golden/font-wild` are its answers.
The reader in `src/opentype.cpp` was written from the OpenType specification,
with these exceptions, which follow fontTools expression for expression because
bit-equal `double` results need the same operations in the same order
(`docs/port-hazards.md`, "RULING — fontTools-derived arithmetic stays,
attributed"):

| in `src/opentype.cpp` | follows, in fontTools |
|---|---|
| `OpenTypeFont::BoundsPen::solve_quadratic` | `solveQuadratic` in `misc/bezierTools.py` |
| `OpenTypeFont::BoundsPen::curve_to` | `calcCubicParameters` and `calcCubicBounds` in `misc/bezierTools.py` |
| `OpenTypeFont::BoundsPen::quad_to` | `calcQuadraticBounds` in `misc/bezierTools.py` |
| the `hflex1` and `flex1` operators of the Type 2 charstring interpreter | `op_hflex1` and `op_flex1` in `misc/psCharStrings.py` |
| the cmap format 4 decode (the `rangeOffset` index) | `cmap_format_4.decompile` in `ttLib/tables/_c_m_a_p.py` |
| which groups of a cmap format 12 or 13 subtable are taken, in `OpenTypeFont::read_cmap_subtable` (its rules, written afresh) | `cmap_format_12_or_13.decompile` and `_make_map` in `ttLib/tables/_c_m_a_p.py` |
| the names of unnamed and repeated glyphs of a format 2 `post` table, in `OpenTypeFont::glyph_order_names` (its rules, written afresh) | `decode_format_2_0`, `unpackPStrings` and `build_psNameMapping` in `ttLib/tables/_p_o_s_t.py` |

Found by the font-stack review (item L1 of the findings named above) and
compared again with the installed fontTools 4.66.0 text when this file was
written: all five hold. The last two rows were added by task `font-wild`, which
wrote them from fontTools' behaviour as the corpus goldens show it and lists
them here because the behaviour is fontTools' own and not the specification's.

The notice is at the point of use: above `OpenTypeFont::BoundsPen` in
`src/opentype.cpp`, with a pointer to it at the two other sites.

`tools/oracle/check_font_bounds.py` holds the same arithmetic in Python. Its
header says it was written from the description in `tools/oracle/FORMAT-font.md`
without fontTools; that description is itself a transcription of what fontTools
does, so it carries the same notice.

The scripts under `tools/oracle` import fontTools when they are run. They are
development tools; fontTools is not copied into this repository and the library
does not depend on it.

The licence, copied from
`../imgui-adv-text/.venv/lib/python3.11/site-packages/fonttools-4.66.0.dist-info/licenses/LICENSE`
(sha256 `6787208f83f659ccbc2223b2fde952ffa6f7e8aca62f1a8a2bf5bc51bb1b2383`):

```text
MIT License

Copyright (c) 2017 Just van Rossum

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## FreeType

Optional. Nothing in `src`, `include`, `tests`, `examples` or `bench` includes
a FreeType header or calls a FreeType function (searched for `ft2build` and
`FT_` followed by a capital; the one match, in `src/font_loader.h`, is a
comment). With `IMTYPESET_USE_FREETYPE` on, `cmake/imgui.cmake` adds Dear
ImGui's `misc/freetype/imgui_freetype.cpp` to the `imgui` target it builds and
links `Freetype::Freetype`. The option defaults to on where CMake finds
FreeType.

FreeType is offered under the FreeType License (FTL) or the GNU GPL version 2.
The FTL was read in `/usr/share/doc/libfreetype6/copyright` (FreeType 2.12.1,
Debian's rendering of it); the choice between the two is as
`docs/port-hazards.md` states it, since the file that says so upstream,
`docs/LICENSE.TXT`, is not installed on the machine this was written on. Under
the FTL, a host that ships a binary linked with FreeType:

- "must provide a disclaimer that states that the software is based in part of
  the work of the FreeType Team, in the distribution documentation" (section 2
  of the licence; the original's justified spacing is not kept in this quote);
- must acknowledge in its documentation that it uses FreeType code. The licence
  suggests this wording:

```text
    Portions of this software are copyright © <year> The FreeType
    Project (www.freetype.org).  All rights reserved.
```

A host that redistributes FreeType's source keeps `FTL.TXT` with it unaltered.
This repository redistributes no FreeType source or binary, so these fall to
whoever ships a build with the option on.

## Unicode Character Database

Range tables generated from Python's `unicodedata`, which reported Unicode
14.0.0 (CPython 3.11.2) when they were made:

- `src/unicode_tables.h`, written by `tools/oracle/gen_unicode_tables.py`: the
  only one compiled into the library;
- `tests/golden/math/unicode.golden` and `tests/golden/para/unicode.golden`,
  written by `tools/oracle/dump_math.py` and `tools/oracle/dump_para.py`: test
  data.

Each of the three names its source, the Unicode version and the licence in its
header and points here for the notice. `src/selection.cpp` no longer embeds a
table of its own; it includes `src/unicode_tables.h`.
`src/mathtext_tables.inc` holds codepoints the reference's parser assigns to
command names, taken from the reference and not from the database.

**The notice as Unicode published it with version 14.0.0 is not on the machine
this was written on, and it was not fetched** (a fetched page comes back
summarised, which is not a verbatim copy). It is at
<https://www.unicode.org/license.txt>. Before a release, replace the block
below with that file's text for the version in use.

What is on the machine is the same licence family (SPDX `Unicode-DFS-2016`) as
Debian packages record it. The block below is lines 664 to 710 of
`/usr/share/doc/libgtk-4-bin/copyright`, unaltered: it is in Debian's
copyright-file layout (one leading space, a lone ` .` for an empty line, line
breaks of the packager's choosing) and its year range is that package's,
1991-2022. Python's own licence file on this machine
(`/usr/share/doc/python3.11/copyright`) does not contain the Unicode notice.

```text
License: Unicode-DFS-2016
 UNICODE, INC. LICENSE AGREEMENT - DATA FILES AND SOFTWARE
 .
 See Terms of Use <https://www.unicode.org/copyright.html>
 for definitions of Unicode Inc.’s Data Files and Software.
 .
 NOTICE TO USER: Carefully read the following legal agreement.
 BY DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING UNICODE INC.'S
 DATA FILES ("DATA FILES"), AND/OR SOFTWARE ("SOFTWARE"),
 YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
 TERMS AND CONDITIONS OF THIS AGREEMENT.
 IF YOU DO NOT AGREE, DO NOT DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE
 THE DATA FILES OR SOFTWARE.
 .
 COPYRIGHT AND PERMISSION NOTICE
 .
 Copyright © 1991-2022 Unicode, Inc. All rights reserved.
 Distributed under the Terms of Use in https://www.unicode.org/copyright.html.
 .
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of the Unicode data files and any associated documentation
 (the "Data Files") or Unicode software and any associated documentation
 (the "Software") to deal in the Data Files or Software
 without restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, and/or sell copies of
 the Data Files or Software, and to permit persons to whom the Data Files
 or Software are furnished to do so, provided that either
 (a) this copyright and permission notice appear with all copies
 of the Data Files or Software, or
 (b) this copyright and permission notice appear in associated
 Documentation.
 .
 THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
 ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
 NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
 DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 PERFORMANCE OF THE DATA FILES OR SOFTWARE.
 .
 Except as contained in this notice, the name of a copyright holder
 shall not be used in advertising or otherwise to promote the sale,
 use or other dealings in these Data Files or Software without prior
 written authorization of the copyright holder.
```

## `tex.web`

Cited, not copied. D. E. Knuth's TeX82 source is the structural reference
(`docs/decisions.md`, D23): functions are named as TeX names them and comments
cite its section numbers. It is read at `../imgui-adv-text/docs/tex.web`,
outside this repository. No file here is a copy of it or of a run of it, none
is named `tex.web`, and the library does not call itself TeX. A TeX identifier
or one short statement is quoted where it identifies what a line transcribes;
prose and worked examples are paraphrased (`docs/port-hazards.md`, "RULING —
quoting `tex.web`"). Behaviour follows the Python reference where the two
differ, and `docs/divergences.md` lists where.

One datum is verbatim: `MATH_SPACING` in `src/math_mlist.h` is the
64-character inter-atom spacing string of section 764 (`math_spacing`,
`tex.web` line 15064), character for character. It is a table that is only
correct as exactly that string, and it is cited where it is defined. The
integer constants of section 108 (`badness`) and the numeric values of the
type and style codes are likewise TeX's.

Its notice, the first ten lines of that file:

```text
% This program is copyright (C) 1982 by D. E. Knuth; all rights are reserved.
% Unlimited copying and redistribution of this file are permitted as long
% as this file is not modified. Modifications are permitted, but only if
% the resulting file is not named tex.web. (The WEB system provides
% for alterations via an auxiliary file; the master file should stay intact.)
% See Appendix H of the WEB manual for hints on how to install this program.
% And see Appendix A of the TRIP manual for details about how to validate it.

% TeX is a trademark of the American Mathematical Society.
% METAFONT is a trademark of Addison-Wesley Publishing Company.
```

The CFF standard string table in `src/opentype.cpp` (`CFF_STANDARD_STRINGS`,
391 names) is the list in Appendix A of Adobe's Compact Font Format
specification, as the comment above it says. How the list was typed in was not
established when this file was written.

## Test fonts

Seven files under `tests/data/fonts`, byte-for-byte copies from imgui-bundle
1.92.900: Roboto (four faces) and Droid Sans under Apache-2.0, Inconsolata
under the SIL Open Font License 1.1, Latin Modern Math under the GUST Font
License. They are test inputs: nothing compiles them into the library or
installs them. `tests/data/fonts/LICENSES.md` has each file's hash, the notice
each font states in its own `name` table, and where each licence text came
from.

- `tests/data/fonts/LICENSE-Apache-2.0.txt`: present.
- `tests/data/fonts/LICENSE-OFL-1.1.txt`: present.
- **The GUST Font License text is missing.** No copy was found on the machine
  this was written on; `LICENSES.md` says where it was looked for.

The images under `docs/images` are pictures of text set in these fonts by
`examples/specimen.cpp`. No font file is embedded in them.

Fonts installed on a developer's machine are read where they are by the corpus
test and are never copied in. The goldens under `tests/golden/font-wild` hold
their paths, hashes and measured values.

## SDL2

zlib licence (`/usr/share/doc/libsdl2-2.0-0/copyright`, SDL 2 as Debian
packages it: "zlib/libpng" for the library's own files). Used by two things,
`examples/demo_sdl2`, the windowed host of the demo window, and the self-hosted
Python library of `python/`: a build-time dependency of those only. The library
does not depend on it. Both are built only when asked for.

**SDL2 is not in this repository.** It is found on the machine (its CMake
package, then pkg-config), or in `.deps/sdl2`, or where `IMTYPESET_SDL2_DIR`
says (`cmake/sdl2.cmake`). `.deps/` is untracked and ignored, as `.deps/imgui`
is: no SDL2 source, header or binary is committed. `tools/vendor_sdl2.sh` fills
`.deps/sdl2` from an SDL2 somebody has already built (its libraries, its
headers), and copies SDL2's licence text to `.deps/sdl2/LICENSE.txt` beside
them, with a `PROVENANCE.txt` saying where each came from. If that directory is
ever passed on with the tree, the licence text goes with it.

`examples/demo_sdl2/main.cpp` is derived from Dear ImGui's
`examples/example_sdl2_sdlrenderer2/main.cpp` and
`examples/example_sdl2_opengl3/main.cpp` (v1.92.9, MIT) and carries Dear
ImGui's notice at its head. The host also compiles Dear ImGui's
`backends/imgui_impl_sdl2.cpp`, `imgui_impl_sdlrenderer2.cpp`,
`imgui_impl_opengl3.cpp` and `imgui_demo.cpp` from the Dear ImGui checkout,
under Dear ImGui's licence above; none of them is copied in.

## The Python reference, `advtext`

`../imgui-adv-text`, by the same owner as this project. The C++ here is a port
of it, and these files are produced by running it, through the scripts named in their
headers: every file under `tests/golden`, `src/mathtext_tables.inc`
(`tools/gen_mathtext_tables.py`) and `bench/data/sample.txt`
(`tools/sample_text.py`, from the reference's `demo/sample.md`).

No licence file exists in the reference and none exists here. That is the
owner's decision to make, for both.
