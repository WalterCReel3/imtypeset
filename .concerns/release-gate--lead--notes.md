# Lead notes for `release-gate` (read with the spec in .concerns/BOARD.md `### release-gate`)

- Wait for: doc-fixes (done), py-fixes (done), draw-seams, demo-guard, docs. Check the board AND
  `.concerns/*--result.md` for each before starting; the board's dependency list predates them.
- CLEAN build directories for every preset in CMakePresets.json (dist ran them incrementally):
  debug, release, asan, stb, wchar16, amalgamation, fp-host, exceptions, bundle-config, imgui-next,
  python-host. One at a time, -j4, every binary guarded (rules at the top of BOARD.md). The
  `amalgamation` preset must stay in: it is the only guard against a helper capturing a call in the
  one-unit build. `exceptions` and `bundle-config` print four -Wterminate warnings from ImGui's own
  destructors (dist's report): not ours, but confirm by reading the warning text.
- Python, in BOTH venvs through freshly rebuilt sidecars (public API and C ABI changed:
  GetContextSerial, DescriptionCut semantics): `python_binding`,
  `tools/bundle_route/font_lifetime.py same-count-cycle` (must exit 0),
  `tools/bundle_route/check_document_edits.py`, `python/spike_bundle/pptmstr_patch_faults.py`,
  `git apply --check` of python/spike_bundle/pptmstr_overlay/pptmstr.patch in ../pptmstr (read-only),
  and the plan's "Start here" proof run.
- UNCHECKED INTERACTION (doc-fixes' builder flagged it, nobody ran it): a block with a standing soft
  error (DescriptionCut, e.g. overlapping runs or a Table block) now makes DrawDocument/DocumentView
  answer false every frame while in view. py-fixes' patch switches imtypeset off after three failing
  frames in a pane. Establish by running in pptmstr's venv whether a soft False from draw counts as a
  "failing frame" there (it must not: pptmstr documents contain tables in Host blocks). If it does,
  fix the Python side (python/imtypeset, the overlay) so soft outcomes are not failures, regenerate
  the patch, and say so first in your result.
- Not re-run by doc-fixes after its late additions: the exceptions build; stb; wchar16.
- No Claude attribution anywhere (CLAUDE.md). Do not commit; the lead commits.
- Demo: build examples/demo_sdl2 from clean by the README's own commands and run it headless; the
  user is inspecting it at a real display.
- From demo-guard: all nine docs/images/demo-*.png are stale against the current library; re-render
  them LAST (after draw-seams), `example_demo_headless docs/images`, recompress as
  `.concerns/demo--builder--result.md` describes, and LOOK at all nine. demo-guard ran only
  test_demo and a syntax compile of the amalgamated demo file: the `amalgamation` preset's full
  suite has not seen its ~45 statics-to-structs change.
- From docs (`.concerns/docs--builder--result.md`; its observations, check each):
  * `bench_draw` reads ~1.7x high when started cold (208-213 us vs 125-128 us for "full draw, x1"
    after ten seconds of load): warm the machine before taking a timing; do not report a regression
    from one cold run. Not checked for bench_reflow / bench_font — check.
  * try/catch also appear in the IMTYPESET_NO_UNWIND macro of src/context.h, not only in
    src/imtypeset_c.cpp: confirm the docs now say what the tree does.
  * The quad bound works out to 1,023 (BATCH_QUADS - 1): the comments in src/draw.h and src/draw.cpp
    still say 1,024 — verify the derivation in the code and fix the comments.
  * demo-guard's result file has unfilled placeholders (RESULT_ASAN and the like): its sanitised
    runs of the final demo are recorded nowhere — your asan preset run is the record.
  * docs/advtext-rework-plan.md "Start here" has numbers that do not reproduce (672 lines added vs
    750 by `git apply --numstat`): re-derive every number in that section and correct it.
  * docs/distribution.md says 77 ImTypeset_* exports; a fresh sidecar has 78 (GetContextSerial).
  * README's preset table has "not re-run" cells: fill them from your clean runs.
  * docs/spec-status.md's "refuted"/"overtaken" verdicts are that builder's reading: spot-check three.
- ADDED WHILE THE GATE WAS RUNNING (user request; also posted as a concern on the release-gate task):
  SDL2 is vendored in `.deps/sdl2` (ignored; see its PROVENANCE.txt). Make it the default where
  find_package(SDL2) finds nothing, remove `../example-project` from README.md, CMakePresets.json,
  CMake comments, docs/distribution.md and scripts, add `tools/vendor_sdl2.sh`, fix THIRD-PARTY.md,
  then run the README demo commands and the python-host preset. Full text: read_board(), the note
  on `release-gate` from lead.
