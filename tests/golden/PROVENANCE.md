# Where the goldens came from

Written by `tools/oracle/manifest.py`. Do not edit: run it again.

`MANIFEST.sha256`, beside this file, holds the SHA-256 of every file under
`tests/golden`. `python3 -B tools/oracle/manifest.py --check` compares the
tree with it and names every file that differs, is missing or is not listed.

**Goldens are regenerated late in a session, by more than one task. The last
gate of a session regenerates this file and the manifest**; a copy written
before that describes the tree as it was then. A regenerated golden that
differs is looked at before the manifest is rewritten (CLAUDE.md, "The golden
format").

- Date written: 2026-10-10
- SHA-256 of `MANIFEST.sha256`: `357e5ba3b27d3282bf6791e568a1d510a41f2504d3e341106e0ea020758f5198`
- Files listed: 157

## The reference

`../imgui-adv-text`, the Python prototype (D32), read and never written.

- Commit: `30496e15fa68911f564a3ae5fec5dd0d0a53ec57`
- Uncommitted files at the time (1), as `git status --porcelain` prints them:
  - `?? CLAUDE.md`

The commit and the list are those of the moment this file was written, not
of the moment each directory was dumped: nothing records that per directory.
A directory dumped earlier in the session was dumped from the same commit
only if the reference did not move in between; `--verify` of its script is
what shows it still agrees.

## The interpreter and libraries

- Python: 3.11.2 (`/home/wreel/Source/imtypeset/../imgui-adv-text/.venv/bin/python`)
- imgui_bundle: 1.92.900
- fontTools: 4.66.0
- markdown-it-py: 4.2.0

## Font files

| file | SHA-256 |
|---|---|
| `tests/data/fonts/DroidSans.ttf` | `4e2371bc0e4cf6983342e150412f140da79d674c9be0b56458401f581072ecd3` |
| `tests/data/fonts/Inconsolata-Medium.ttf` | `82bc2d670af8c23ac9efaef8c0346f1b444c79252a37e3be395a8c9e45080a82` |
| `tests/data/fonts/Roboto-Bold.ttf` | `ec685a46105296fe46c8744da4a11cf8118ba6c11271941766f7a546df6aa7c7` |
| `tests/data/fonts/Roboto-BoldItalic.ttf` | `61df597f73c91f238cbe88fe3c536702d120d048b79b25fa0f4f7818fd123a6f` |
| `tests/data/fonts/Roboto-Regular.ttf` | `4e147ab64b9fdf6d89d01f6b8c3ca0b3cddc59d608a8e2218f9a2504b5c98e14` |
| `tests/data/fonts/Roboto-RegularItalic.ttf` | `99e4a85061136e99e052929ed0d85e36384fba5c34b773139a8f64339c609943` |
| `tests/data/fonts/latinmodern-math.otf` | `6075562b771f8b82f0c179e363389684f2dd09de30038269e2628e504bd7be0f` |
| reference `prose`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Roboto/Roboto-Regular.ttf` | `4e147ab64b9fdf6d89d01f6b8c3ca0b3cddc59d608a8e2218f9a2504b5c98e14` |
| reference `prose-bold`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Roboto/Roboto-Bold.ttf` | `ec685a46105296fe46c8744da4a11cf8118ba6c11271941766f7a546df6aa7c7` |
| reference `prose-italic`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Roboto/Roboto-RegularItalic.ttf` | `99e4a85061136e99e052929ed0d85e36384fba5c34b773139a8f64339c609943` |
| reference `prose-bold-italic`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Roboto/Roboto-BoldItalic.ttf` | `61df597f73c91f238cbe88fe3c536702d120d048b79b25fa0f4f7818fd123a6f` |
| reference `code`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Inconsolata-Medium.ttf` | `82bc2d670af8c23ac9efaef8c0346f1b444c79252a37e3be395a8c9e45080a82` |
| reference `code-bold`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/Roboto/Roboto-Bold.ttf` | `ec685a46105296fe46c8744da4a11cf8118ba6c11271941766f7a546df6aa7c7` |
| reference `math`: `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/assets/fonts/latex/latinmodern-math.otf` | `6075562b771f8b82f0c179e363389684f2dd09de30038269e2628e504bd7be0f` |

## Directories

Each command is run from the repository root; the prefix is

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=../imgui-adv-text ../imgui-adv-text/.venv/bin/python -B

`REFERENCE_PATCHES` is what a script changed in the reference **in memory**
before dumping (D32.3), as the script itself declares it.

| directory | files | regenerate with | REFERENCE_PATCHES |
|---|---|---|---|
| `blocks` | 8 | `... tools/oracle/dump_blocks.py` | none (the script declares no REFERENCE_PATCHES) |
| `core` | 7 | `... tools/oracle/dump_core.py` | none (the script declares no REFERENCE_PATCHES) |
| `doc` | 4 | `... tools/oracle/dump_doc_extras.py` | none |
| `draw` | 3 | `... tools/oracle/dump_draw.py` | none (the script declares no REFERENCE_PATCHES) |
| `font` | 23 | `... tools/oracle/dump_font.py` | none (the script declares no REFERENCE_PATCHES) |
| `font-synth` | 1 | `... tools/oracle/dump_font_synth.py` | none (the script declares no REFERENCE_PATCHES) |
| `font-wild` | 87 | `... tools/oracle/dump_font_wild.py` | none (the script declares no REFERENCE_PATCHES) |
| `font-wild-kern` | 1 | `... tools/oracle/dump_kern_legacy.py` | none (the script declares no REFERENCE_PATCHES) |
| `math` | 6 | `... tools/oracle/dump_math.py` | none (the script declares no REFERENCE_PATCHES) |
| `para` | 8 | `... tools/oracle/dump_para.py` | none (the script declares no REFERENCE_PATCHES) |
| `para-map` | 1 | `... tools/oracle/dump_para_map.py` | none (the script declares no REFERENCE_PATCHES) |
| `selection` | 2 | `... tools/oracle/dump_selection.py` | none (the script declares no REFERENCE_PATCHES) |
| `tables` | 6 | `... tools/oracle/dump_tables.py` | none (the script declares no REFERENCE_PATCHES) |

Scripts that take arguments, read files outside the repository (the fonts
installed on the machine, for `font-wild`) or need more than one run say so
in their own docstrings and in `tools/oracle/FORMAT*.md`; the command above
is the plain invocation.
