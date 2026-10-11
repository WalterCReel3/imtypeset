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

- Date written: 2026-10-11
- SHA-256 of `MANIFEST.sha256`: `691f847b1d55099a2c769b9b5c3fabb02c82457a8182a62d753682badaca96dd`
- Files listed: 159

## The reference

The Python prototype (D32), frozen: one commit of `../imgui-adv-text`, extracted
read-only into `build/advtext-reference` by `tools/oracle/pin_reference.sh` (`git archive`
from that repository's object store; its working tree is not read and nothing
is written there). `tools/oracle/REFERENCE.lock` holds what is below, and
`tools/oracle/pinned_reference.py` makes every dump script and this one exit 2
unless the extracted tree matches it and `advtext` is imported from it.

- Tag: `advtext-reference`
- Commit: `32e395346551cac82c7a6dd6e9b6d8e47feeac8e`
- Tree: `7a591a485818c85fd6c6c43df75271a477c9d612`
- Files: 179
- SHA-256 over the listing of their SHA-256s: `cae5c698082ef77bcb96cbe3fa691ef0d6ef60809d511ebfd8db0f55625f7e56`

This is the reference this file was written against. Nothing records, per
directory, which reference a golden was dumped from; `--verify` of a script,
or a re-run that leaves its directory byte-identical, is what shows a
directory agrees with the pinned tree.

The interpreter and its libraries are not pinned by that commit: they are
those of `../imgui-adv-text/.venv`, a virtual environment no commit holds.

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

    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference ../imgui-adv-text/.venv/bin/python -P -B

`REFERENCE_PATCHES` is what a script changed in the reference **in memory**
before dumping (D32.3), as the script itself declares it.

| directory | files | regenerate with | REFERENCE_PATCHES |
|---|---|---|---|
| `blocks` | 8 | `... tools/oracle/dump_blocks.py` | none (the script declares no REFERENCE_PATCHES) |
| `core` | 7 | `... tools/oracle/dump_core.py` | none (the script declares no REFERENCE_PATCHES) |
| `doc` | 4 | `... tools/oracle/dump_doc_extras.py` | none |
| `doc-tables` | 2 | `... tools/oracle/dump_doc_tables.py` | none |
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
