# bundle-research — builder-21 — result

Deliverables: `docs/bindings-notes.md` (READ / RAN / NOT DETERMINED kept apart; every statement about
the bundle has a file reference), `bindings/imgui_bundle/` (drafts, UNRUN), `tools/experiments/`
(what was run), evidence under `build/bundle-research/`.

Wrote only: `docs/bindings-notes.md`, `bindings/imgui_bundle/*`, `tools/experiments/*`,
`build/bundle-research/*`, this file. No library source, no build-system file, nothing in a sibling
tree. Nothing was committed. Sibling trees were read with `git --git-dir=... show|archive|ls-tree`
and plain file reads only. No network access was attempted.

## The three answers

1. **Python usage** would look like implot's in the bundle: `from imgui_bundle import imtypeset`,
   `imtypeset.create_context()`, `imtypeset.add_font_from_file_ttf(imtypeset.Face_.regular, path)`,
   `imtypeset.text_unformatted(text, flags=imtypeset.TextFlags_.math)` each frame; a paragraph handle
   for runs, selection and copy. A sketch against the post-`api-revise` API is in the notes, section 1.
   Fonts need not be brought: the bundle ships all seven files of `tests/data/fonts/`, byte-identical
   (SHA-256 compared).
2. **Plays well when compiled INTO the bundle** (the only way any library is in it): 37 of 49
   declarations bind as written, 5 with one option, 7 by hand with a precedent each; all 40 ImGui
   symbols we use are exported by the wheel; sources compile clean against 1.92.9 docking with the
   bundle's defines. Changes on our side: exceptions cannot be off (the bundle's `IM_ASSERT` throws:
   14 of 15 sources fail under `-fno-exceptions` with its config header, 0 fail with exceptions on);
   sources are not in the flat layout the bundle's CMake helper globs; `src/context.h` must be
   reachable for the two opaque handles. **Does NOT work as a separate binary next to the installed
   wheel**: the bundle's ImGui is a fork (extra `ImGuiStyle::LayoutAlign`, 21 `ImGuiStyleVar_` values
   shifted) and the wheel ships no headers.
3. **To settle the rest** (network needed): `git submodule update --init --recursive` in the clone;
   `pip install -r requirements-dev.txt` (line 3 is `litgen @ git+https://github.com/pthom/litgen`,
   line 5 `nanobind`); apply `bindings/imgui_bundle/README.md`; `just libs_bindings imtypeset`; build
   with the `python_bindings` preset. For experiment B against the installed 1.92.900 wheel the fork
   must be at `c9a0a84f8a4ddae794a9e1acf6a93afce73d5057` (what tag `v1.92.900` pins), not the clone's
   HEAD pin (`7002a580`, ImGui 1.93 WIP); then `tools/experiments/build_expb.sh fork` and
   `exp_b_gate.py fork`, `exp_b_run.py fork`.

## Experiments

| | outcome | evidence |
|---|---|---|
| A1 bytes through `ImFontConfig.font_data` | WORKS, only as a `PyCapsule` named `nb_handle` (made with ctypes) over a buffer the caller keeps alive. `bytes`, `bytearray`, `memoryview`, numpy, ctypes array, `c_void_p`, `int`, `None`, unnamed capsule: all `TypeError`. `add_font` then gives a font; `U+0041` has 49 inked atlas texels. Atlas-owned variant (block from `ImGui::MemAlloc`) also works | `tools/experiments/exp_a_font_bytes.py`; `build/bundle-research/exp_a_roboto.jsonl` |
| A2 our rebuilt Latin Modern Math in the bundle's atlas | WORKS: 227 of 227 minted codepoints (`U+FFF1B..U+FFFFD`) found, visible, inked (fewest 22 texels); original file as control: 0 of 227. FreeType 2.13.3 in the wheel. D27's open question is answered: §2's "structurally unreachable from Python" does not hold for the font path | `expa_write_atlas_font.cpp`; `build/bundle-research/expa/` |
| B load | plain `ctypes.CDLL` fails (`undefined symbol: _ZN15ImGuiTextBuffer11EmptyStringE`), before or after `import imgui_bundle`; works after re-opening the bundle's `.so` `RTLD_GLOBAL`, or with it as `DT_NEEDED` + rpath | `exp_b_gate.py`; `exp_b_gate_{deps,docking}.jsonl` |
| B layout gate, headers `.deps/imgui` (1.92.9 master) | **MISMATCH**, `sz_io` (ours 3016, wheel 3080): the wheel is docking. STOPPED | same |
| B layout gate, headers upstream `v1.92.9-docking` | version and `ImGuiIO` pass; **MISMATCH**, `sz_style` (ours 1368, wheel 1372). STOPPED. Nothing further was called across the boundary in either build; no header was edited to pass | same; `exp_b_run_{deps,docking}.json` |
| B diagnosis (bundle's own binding and stub only) | fork adds `float LayoutAlign` to `ImGuiStyle` and `ImGuiStyleVar_LayoutAlign = 25`, shifting 21 enum values; `ImGuiCol_`, `ImGuiBackendFlags_` identical | `exp_b_fork_diff.py`; `exp_b_fork_diff.json` |
| B control, no bundle (`selfhost`: our ImGui compiled in, all calls through ctypes) | WORKS end to end: 6 faces, headless frames, `TextJustified` with two formulas, paragraph path, copy equals source, screenshot. First call 1.36 ms; steady state 10.0 µs per call (0 layouts in 300 frames); frame 15.4 µs with / 5.2 µs without; 1,132 / 104 vertices. `-O2`, asserts on, stb_truetype, one machine | `exp_b_run.py selfhost`; `exp_b_run_selfhost.json`; `build/bundle-research/selfhost/frame.png` |
| review A1, measured in the control | equal text in a NEW object each frame, old ones alive: 100 layouts in 100 frames, 101 cached paragraphs, 101 µs per call; same object: 0 layouts, 10 µs | same json, `equal_text_new_object_each_frame` |
| `-fno-exceptions` under the bundle's config header | 14 of 15 sources fail; 0 with `-fexceptions` | command below |

The syntax check (from the repository root; `$R` = `build/bundle-research`, `$CFG` = absolute path of
`../imgui_bundle/imgui_bundle_cmake/imgui_bundle_config.h`), once per flag set, over `$R/snap/src/*.cpp`:

    g++ -std=c++17 -fsyntax-only <-fno-exceptions -fno-rtti | -fexceptions -fno-rtti | -fexceptions> \
        -ffp-contract=off -Wall -Wextra -Wno-terminate -DIMGUI_USER_CONFIG="\"$CFG\"" \
        -DIMGUI_BUNDLE_BUILD_PYTHON -DIMGUI_BUNDLE_PYTHON_API -DIMGUI_USE_WCHAR32 -DImTextureID=int \
        -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -I$R/imgui-1.92.9-docking -I$R/snap/include -I$R/snap/src <file>

## Decisions taken unattended

- **Stopped experiment B at the layout check and did not go round it.** The spec named `.deps/imgui`
  headers; that mismatch was master-versus-docking, known before the call from the wheel's exported
  `ImGui::DockSpace`, so a second build against upstream `v1.92.9-docking` was run as a second
  experiment under the same gate. It failed too, on a struct only the fork has, and that is where it
  ends. Patching a header with the one field the stub reveals would make six numbers agree with every
  other struct of the fork unseen; not done.
- Built a `selfhost` control (no bundle in the process) so that the Python driver and the shim are
  known to work and a per-frame cost from Python exists. It says nothing about the bundle's ImGui.
- Docking headers were extracted from `../imgui`'s git objects (`git --git-dir=../imgui/.git archive
  v1.92.9-docking ...`) into `build/bundle-research/imgui-1.92.9-docking/`; read-only on the sibling.
- Section 8 of the notes was cut to a short paragraph on the lead's direction change (D32); the full
  advtext map is task `advtext-map`'s.
- `git add` was run on the new files under `docs/`, `bindings/`, `tools/experiments/` (the session
  rule: add on creation). Nothing committed.

## Not verified

litgen, nanobind, the bundle build (nothing in `bindings/imgui_bundle/` has run or compiled); the
fork's source and whatever else it changes; imtypeset inside the bundle's ImGui in any form;
`exp_b_run.py`'s `BundleHost` half and `build_expb.sh fork`; the size probes for `ImFontConfig`,
`ImDrawList`, `ImFont`, `ImFontBaked` against the wheel (they come after the failed check); the
bundle's GL/offscreen path; nanobind call cost and whether a nanobind `str` keeps its pointer between
frames; FreeType 2.13.3 vs 2.12.1 glyph boxes; other platforms' wheels; hello_imgui's fonts beside
ours; exception safety when an ImGui assertion unwinds through the library. The snapshot is of a tree
others were editing, taken before `api-revise` landed.
