# For the `docs` task, from the lead after fold-docs (2026-10-10)

fold-docs (`.concerns/fold-docs--builder--result.md`) left two disagreeing pairs unapplied. Rulings:

1. try/catch in src/. The tree is the evidence (lead and fold-docs both grepped): `try`/`catch` appear
   only in `src/imtypeset_c.cpp`. Restore c-api's sentence in D40 ("No exception crosses…") and write
   harden-2's as "no try/catch in src/ outside src/imtypeset_c.cpp" in D39 item 11 and hardening row X1.
2. 1,023 or 1,024 quads. `src/draw.cpp:34` has `BATCH_QUADS = 1024`; `src/draw.h:50` says "up to
   1,024"; docs/divergences.md:381 says 1,023; D39 item 9 and hardening row D4 say 1,024. NOT settled
   by the lead: work out from the batching code whether a command can end 1,024 or at most 1,023 quads
   early, then make the header comment and the three documents say the same number.
3. D39 uses review item ids ("D3", "D4", "D6", "D7") inside docs/decisions.md, where Dn means a
   decision: qualify them ("review item D3").
4. Stale text fold-docs lists at the end of its result (e.g. the hardening `## Counts` row saying
   D3/D4/D7 "not done"): bring to what is true, checking each against the tree.
5. bundle-route refutes docs/bindings-notes.md §6B and `.concerns/py-host--lead--rulings.md` ("cannot
   be driven inside the wheel") for the probe-gated case, and D6's exact ImGui pin must read as a floor
   (1.92.8 builds): `.concerns/bundle-route--builder--result.md`.
6. The README's Python story leads with advtext and pptmstr on their stock wheels (the user's stated
   need), not with imgui_bundle adoption.
7. b6-map-shipped renamed a test to `easy_line_the_last_resort_is_forced_from_the_one_active_node_left`;
   docs/divergences.md (B3a row) still cites the old name. review-round-3 item 2: say easy_line is
   linear "at a fixed measure" and state the first-fit fallback for a long ragged total-fit paragraph
   in a very wide column (`.concerns/review-round-3--reviewer--findings.md`).
8. d7-fix removed `rule_joins_span`, `RULE_JOIN_LOOKBACK`, `Drawer::settle_span`; D39 item 10, hardening
   row D7, the divergences `## draw` bullet and performance.md still name them. Apply
   `.concerns/d7-fix--builder--doc-additions.md` and, when they exist, word-gaps' and api-document's.
9. USER REQUEST (their words, about 16:00): "If there are demos, examples, etc that can be executed
   after a build then instructions in the README.md on how to run them would be useful." README.md
   gets a section "Running the demos and examples", near the top, listing EVERY runnable thing with
   the exact build and run commands, each of which you RAN from a clean build directory before
   writing it down (say which could only be run headless here): the SDL2 demo window
   (examples/demo_sdl2; `.concerns/demo--builder--how-to-run.md`), example_minimal, example_specimen,
   example_demo_headless (writes one PNG per section to a directory), the benchmarks (release only),
   the self-hosted Python host (examples/python/minimal_host.py), the bundle-hosted example
   (examples/python/bundle_host.py via python/host/build_bundle_library.sh, per venv), the pptmstr
   patch ("Start here" of docs/advtext-rework-plan.md), and the test suite. State prerequisites
   (SDL2 from ../example-project or a system SDL2; FreeType optional; no network) and what each shows.
