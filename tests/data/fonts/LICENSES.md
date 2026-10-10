# Fonts under tests/data/fonts

Test inputs only. Nothing here is compiled into the library or installed with it.

Each file is a byte-for-byte copy from imgui-bundle 1.92.900
(`imgui_bundle/assets/fonts/`), which is where the Python reference
(`advtext.fontlib.font_file`) finds its faces. The goldens under
`tests/golden/font` were dumped from the same bytes and record the same hashes,
so a font replaced here has to be replaced there.

The licence column is what each font's own `name` table says (name IDs 0, 13
and 14), read with fontTools on 2026-10-09, not a recollection.

| file | family, version | role in the reference | licence | sha256 |
|---|---|---|---|---|
| `Roboto-Regular.ttf` | Roboto Regular, 2.137 (2017) | `Face.PROSE` | Apache-2.0 | `4e147ab64b9fdf6d89d01f6b8c3ca0b3cddc59d608a8e2218f9a2504b5c98e14` |
| `Roboto-Bold.ttf` | Roboto Bold, 2.137 (2017) | `Face.PROSE_BOLD`, `Face.CODE_BOLD` | Apache-2.0 | `ec685a46105296fe46c8744da4a11cf8118ba6c11271941766f7a546df6aa7c7` |
| `Roboto-RegularItalic.ttf` | Roboto Italic, 2.137 (2017) | `Face.PROSE_ITALIC` | Apache-2.0 | `99e4a85061136e99e052929ed0d85e36384fba5c34b773139a8f64339c609943` |
| `Roboto-BoldItalic.ttf` | Roboto Bold Italic, 2.137 (2017) | `Face.PROSE_BOLD_ITALIC` | Apache-2.0 | `61df597f73c91f238cbe88fe3c536702d120d048b79b25fa0f4f7818fd123a6f` |
| `Inconsolata-Medium.ttf` | Inconsolata Medium, 3.001 | `Face.CODE` | SIL Open Font License 1.1 | `82bc2d670af8c23ac9efaef8c0346f1b444c79252a37e3be395a8c9e45080a82` |
| `latinmodern-math.otf` | Latin Modern Math, 1.959 | `Face.MATH` | GUST Font License | `6075562b771f8b82f0c179e363389684f2dd09de30038269e2628e504bd7be0f` |
| `DroidSans.ttf` | Droid Sans, 1.00 | none: a face the reference does not bundle, for the register-a-font path | Apache-2.0 | `4e2371bc0e4cf6983342e150412f140da79d674c9be0b56458401f581072ecd3` |

`Face.CODE_BOLD` resolving to Roboto Bold is what the reference does, not a
mistake in this table: imgui-bundle ships no bold Inconsolata.

## Notices, as the fonts state them

**Roboto** — "Copyright 2011 Google Inc. All Rights Reserved." "Roboto is a
trademark of Google." "Licensed under the Apache License, Version 2.0",
<http://www.apache.org/licenses/LICENSE-2.0>. The licence text imgui-bundle
ships beside the Roboto files is copied here as `LICENSE-Apache-2.0.txt`.

**Droid Sans** — "Digitized data copyright © 2007, Google Corporation." "Droid
is a trademark of Google and may be registered in certain jurisdictions."
"Licensed under the Apache License, Version 2.0",
<http://www.apache.org/licenses/LICENSE-2.0>. Same text, `LICENSE-Apache-2.0.txt`.

**Inconsolata** — "Copyright 2006 The Inconsolata Project Authors
(https://github.com/cyrealtype/Inconsolata)". "This Font Software is licensed
under the SIL Open Font License, Version 1.1. This license is available with a
FAQ at: http://scripts.sil.org/OFL".

**Latin Modern Math** — "Copyright 2012--2014 for Latin Modern Math OTF by B.
Jackowski, P. Strzelczyk and P. Pianowski (on behalf of TeX users groups). This
work is released under the GUST Font License -- see
http://tug.org/fonts/licenses/GUST-FONT-LICENSE.txt for details."

## Licence texts in this directory

| file | what | copied from |
|---|---|---|
| `LICENSE-Apache-2.0.txt` | Apache License 2.0 | imgui-bundle 1.92.900, `imgui_bundle/assets/fonts/Roboto/LICENSE.txt`, the whole file (compared byte for byte on 2026-10-09) |
| `LICENSE-OFL-1.1.txt` | SIL Open Font License, Version 1.1 - 26 February 2007 | lines 36 to 121 of fontTools 4.66.0's `fonttools-4.66.0.dist-info/licenses/LICENSE.external` in the reference's `.venv`, unaltered. sha256 of the copy: `58a449e34b1aa50202fdca51cd73651fe0c5f78e5731e089c5e2c670b76eb299` |

About `LICENSE-OFL-1.1.txt`: no standalone OFL 1.1 file was found on the
machine (the one under `/usr/share/doc/fonts-dzongkha` is version 1.0).
fontTools ships the licence inside a longer file, between its list of bundled
test fonts and its next section, and this is that run of lines: from the dashed
rule above the title to the end of the disclaimer. It therefore has no
copyright line of its own; Inconsolata's is quoted above under "Notices, as the
fonts state them", and the two belong together. As a check that the run is the
licence and nothing else, its text from `PREAMBLE` to the end equals, with
runs of white space and hyphens collapsed, the OFL 1.1 text in four Debian
copyright files on the same machine (`fonts-noto`, `fonts-hosny-amiri`,
`fonts-yrsa-rasa`, `libharfbuzz0b`). It was not compared with the copy SIL
publishes.

## Not done

**The text of the GUST Font License is not in this directory.** It is named and
linked above. No copy was found on the machine this was filled from, searched
on 2026-10-09: `/usr/share/doc` (no `lmodern`, `fonts-lmodern`, `tex-gyre` or
TeX package is installed, and no `copyright` file there mentions the licence),
`/usr/share/fonts`, the reference's `.venv` including imgui-bundle's
`assets/fonts/latex`, which holds the font and no licence; `/usr/share/texmf`,
`/usr/share/texlive` and `/usr/share/licenses` do not exist there. The sandbox
has no network access to tug.org, and a page fetched through a summarising tool
is not a verbatim copy, so none was written. Add `LICENSE-GUST.txt` from
<http://tug.org/fonts/licenses/GUST-FONT-LICENSE.txt> before this directory is
published.
