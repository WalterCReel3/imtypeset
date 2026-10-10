# Lead rulings for `py-host` after the bundle research

Read `.concerns/bundle-research--builder-21--result.md` and `docs/bindings-notes.md` first.

ESTABLISHED (builder-21, by running): an imtypeset shared library CANNOT be driven inside the
INSTALLED imgui_bundle wheel from this machine today. The bundle's ImGui is a fork on the docking
branch; ImGui's own DebugCheckVersionAndDataLayout fails across the boundary (sizeof(ImGuiIO) with our
upstream headers; sizeof(ImGuiStyle) 1368 vs 1372 even with upstream v1.92.9-docking headers — the
fork adds `float LayoutAlign` and shifts 21 ImGuiStyleVar_ values). The wheel ships no headers and
`../imgui_bundle`'s submodules are not initialised. The fork commit the 1.92.900 wheel pins is
c9a0a84f8a4ddae794a9e1acf6a93afce73d5057. The experiment stopped at the failed check, correctly.
ALSO ESTABLISHED: the control works — our own ImGui compiled into the library and EVERY call made from
Python through ctypes: six faces, headless frames, a justified paragraph with two formulas, copy equal
to source, a screenshot; 10.0 us per steady-state call, 15.4 us per whole frame.

So `py-host` delivers TWO Python hosts, and the board spec's items are re-read as follows:

A. SELF-HOSTED (runnable here, REQUIRED): Python owns the loop and calls imtypeset through the ctypes
   package over `include/imtypeset_c.h`; the window, the backend and Dear ImGui itself come from a
   small native host library built in this repo (`python/host/`: ImGui v1.92.9 from .deps/imgui +
   SDL2 from `../example-project` as `examples/demo_sdl2` links it + imtypeset, one shared library),
   which exposes a SMALL C surface for the host side only: create/destroy window, begin/end frame,
   poll events / should-close, screenshot, and the dozen ImGui calls the minimal demo needs (begin/end
   window, text wrapped, slider float, same line, columns or a child, get content region, get window
   draw list as an opaque pointer, mouse state). Not a general ImGui binding and it must say so.
   `examples/python/minimal_host.py` runs on this, through tools/shot.sh on Xvfb, with
   `--frames N --screenshot`; SDL's offscreen driver as the fallback. This is the "minimal python host
   with direct hooks to imtypeset as a python binding" the user asked for, runnable today.
B. BUNDLE-HOSTED (the real target, GATED): the same `python/imtypeset` package inside an imgui_bundle
   app. Built only when the fork's headers are present at `../imgui_bundle/external/imgui/imgui`
   (imgui.h exists there) AND they are the commit the installed wheel pins; the package's import runs
   the build-facts check and ImGui's layout check first and raises a readable ImportError on any
   mismatch. If the headers are absent when you run, SKIP with the exact message a person needs:
   in ../imgui_bundle `git submodule update --init external/imgui/imgui` then
   `git -C external/imgui/imgui checkout c9a0a84f8a4ddae794a9e1acf6a93afce73d5057`. If they are
   present, finish Experiment B (`tools/experiments/build_expb.sh fork`, `exp_b_gate.py fork`,
   `exp_b_run.py fork`) and make `examples/python/bundle_host.py` (the immapp/hello_imgui version of
   the same demo) run and be photographed. Never write into ../imgui_bundle; check with read-only
   commands.
C. Both hosts share ONE `python/imtypeset` package: which ImGui it resolves is decided at load, and
   the package reports which (`imtypeset.host_info()`).

For `dist` and `harden-2`, from the same research:
- The bundle defines IM_ASSERT to THROW in Python builds, and with its config header 14 of our 15
  sources fail to compile under -fno-exceptions (none fail with exceptions on). The library must
  build and pass BOTH ways: add a configuration to the gate that compiles with exceptions enabled and
  the bundle's `litgen_imconfig.h` as IMGUI_USER_CONFIG (read-only use of ../imgui_bundle; skip when
  absent), and `harden-2` tests exception safety — an IM_ASSERT that throws from inside a layout or a
  draw must unwind through the library leaving the context usable (no leak under ASan, no half-written
  draw list reserve, caches consistent).
- The bundle's CMake helper globs a flat source layout: the amalgamated drop-in files are what it
  takes. `src/context.h` must stay reachable for a binding to see the opaque handle types complete.
- The bundle already ships all seven test fonts byte-identical; a bundle integration brings no fonts.
- 37 of 49 public declarations bind as written by the bundle's conventions, 5 need one litgen option,
  7 need a few hand-written lines (table in docs/bindings-notes.md) — `api-revise` should read that
  table before it finishes.
