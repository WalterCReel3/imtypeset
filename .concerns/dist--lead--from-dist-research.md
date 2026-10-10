# Lead notes for `dist` (and `harden-2`) from the dist-research result

Read `.concerns/dist-research--builder-19--result.md` and `docs/distribution-notes.md` first; this file
is the rulings on top of them. The snapshot was of a tree in motion: re-run before trusting line numbers.

ESTABLISHED BY RUNNING (builder-19): src/ and include/ compile with 0 errors and 0 warnings against
Dear ImGui 1.93.0 WIP (19298) in six configurations; no file includes imgui_internal.h; glyph-box
equality with the reference holds on 1.93 exactly as on 1.92.9 (0 of 583 under FreeType).

RULINGS
1. `cmake/imgui.cmake` enforces exactly 19290 while the header's floor is 19200. Make the build system
   agree with the header: a floor, with a WARNING (not an error) above the newest version tested, and
   presets for the pinned 1.92.9 and for `../imgui` when present. `dist` owns cmake/imgui.cmake for
   this.
2. Tests that fail on 1.93 for reasons that are the TESTS' (`dist` owns tests/smoke.cpp and
   tests/test_soft_raster.cpp for this; tests/test_draw.cpp is `harden-2`'s): smoke.cpp:21 asserts
   the exact version — compare against the floor; test_soft_raster.cpp:103 relies on AddRectFilled
   aliasing half-pixel edges, which 1.93 anti-aliases — use PrimReserve + PrimRect; test_draw.cpp:658
   replays rules through AddRectFilled, which 1.93 drops for zero/inverted rects — PrimRect in the
   replay; with IMGUI_DISABLE_OBSOLETE_FUNCTIONS on 1.93, ImDrawListFlags* is merged into ImDrawFlags
   (7 errors in tests/test_draw.cpp and bench/bench_draw.cpp) — guard on IMGUI_VERSION_NUM.
3. `harden-2`: the debug overlay draws its outlines through ImGui's AddRect/AddLine, whose geometry
   changed in 1.93 (the two debug goldens cannot be reproduced there). Draw the outlines as the
   library's own quads (ListSink::outline) so the overlay is version-neutral, and keep the 1.92.9
   debug goldens passing if the new quads can equal them; if they cannot, the debug goldens are
   compared at pen level only and the reason is recorded in docs/divergences.md.
4. src/draw.cpp uses four ImDrawList members imgui.h marks [Internal] (_VtxWritePtr, _IdxWritePtr,
   _VtxCurrentIdx, _CmdHeader). They are in imgui.h and unchanged across both versions; ImGui's own
   PrimWriteVtx/PrimWriteIdx/PrimRectUV are the public route. `harden-2`: measure whether using only
   the Prim* inline functions costs anything (bench_draw); if it is within noise, switch and drop the
   [Internal] members; if not, keep them and say so in docs/decisions.md with the number.
5. Drop-in set, as builder-19 recommends: imtypeset.h + a generated imtypeset.cpp + imtypeset_demo.cpp.
   A unity build of src/*.cpp fails today on anonymous-namespace collisions (append_utf8 x3,
   formatted x2, EMPTY_DISC, an ambiguous `set` in style.cpp): `dist` runs last and alone and may
   rename those symbols in src/ (mechanical, no behaviour change, full suite before and after). The
   gate gets a unity/amalgamation configuration.
6. CMake target contract: resolve IMTYPESET_IMGUI_TARGET as imgui::imgui, then imgui; when included by
   a host that has neither, FAIL with a message rather than silently build a second ImGui from .deps
   (top-level builds of this repo still build their own). Install only when ImGui came from an imported
   target; generated config calls find_dependency(imgui CONFIG).
7. Floating point in a HOST's build is the least-sure point: a host compiling the drop-in file with
   -ffast-math or default GCC contraction changes results at the last bit, and layout decisions turn on
   exact halves. `dist`: put `#pragma STDC FP_CONTRACT OFF` (and the GCC/Clang/MSVC equivalents that
   actually take effect — check each: GCC ignores the STDC pragma; `#pragma GCC optimize` or
   function attributes may be needed; MSVC has `#pragma fp_contract(off)` and `#pragma float_control`)
   at the top of the amalgamated file, add a compile-time `#error` when `__FAST_MATH__` is defined
   unless IMTYPESET_ALLOW_FAST_MATH is, and TEST it: build the amalgamation at -O3 -march=native with
   contraction left at the compiler default and run the golden suite against it. If goldens still pass,
   say so; if not, say which and whether the pragma fixed it. Document the outcome in
   docs/distribution.md.
8. hello_imgui defines IMGUI_USE_WCHAR32 only when it builds ImGui with FreeType (off by default on
   Android and MinGW), so BOTH ImWchar widths occur among its users: the 16-bit preset stays in the
   gate.

## Added 2026-10-10 after c-api (lead; both are builders' reports, check before applying)

- `CMakePresets.json`: the `asan` preset sets only CMAKE_CXX_FLAGS, so tests/test_c_api.c is
  uninstrumented there. Add `"CMAKE_C_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer"`.
- The C ABI has only ever been linked statically: export visibility and -fPIC are untested until
  py-host builds its shared library. Read py-host's result for what it needed.
- py-host may hand you a line for the root CMakeLists.txt (its option + add_subdirectory(python)).
- From py-host (their reading, check it): the throwing IM_ASSERT of imgui_bundle is in
  `../imgui_bundle/imgui_bundle_cmake/imgui_bundle_config.h` under IMGUI_BUNDLE_BUILD_PYTHON, not in
  `litgen_imconfig.h`; the exceptions preset should use the former. Root-CMake lines and ctest wiring
  for python/ are in `.concerns/py-host--builder--result.md`; with the option ON the imtypeset target
  becomes -fPIC (never time such a tree) and under asan the python test needs LD_PRELOAD of libasan
  and detect_leaks=0, or exclusion.
- From harden-2: `test_exception_safety` only does anything with -DIMTYPESET_ENABLE_EXCEPTIONS=ON and
  tests/imconfig_throwing.h; no preset has that. Add the exceptions preset (with ASan) so the gate
  runs it. harden-2 confirms by building that the bundle's throwing config is
  imgui_bundle_cmake/imgui_bundle_config.h with IMGUI_BUNDLE_BUILD_PYTHON.

## Added about 15:25 — state of the tree when dist starts (lead)

- You are the ONLY writer in src/ now. Last known good: word-gaps' builder ran the full unnarrowed
  suite on the live tree, 40/40 in debug, release, ASan+UBSan, stb, 16-bit (private dirs, not presets).
  The board's note that test_api_document_golden has a failing tab test may be stale: run it and say.
- New since the dist research: src/imtypeset_document.{h,cpp}, src/imtypeset_demo.cpp, src/tex_rules.h,
  src/imtypeset_c.cpp, tests/test_c_api.c (C compiler; `enable_language(C)` sits in tests/CMakeLists.txt),
  python/CMakeLists.txt (option + ctest wiring lines in `.concerns/py-host--builder--result.md`),
  examples/CMakeLists.txt option IMTYPESET_BUILD_DEMO_SDL2, IMTYPESET_DISABLE_DEMO,
  IMTYPESET_ENABLE_EXCEPTIONS + tests/imconfig_throwing.h (no preset has it: add one, with ASan).
- src/imtypeset.cpp's helpers moved from an anonymous namespace to `ImTypeset::detail::core` so the
  document TU shares them — relevant to the unity/amalgamation collisions.
- The ImGui pin must become a FLOOR: the bundle sidecar builds the library against ImGui 1.92.8
  (pptmstr's wheel) and 1.92.9 (`.concerns/bundle-route--builder--result.md`). Decide the floor by
  building, record it, make header and build system agree.
- The user's need is the Python sidecar inside stock imgui_bundle wheels (python/host/
  build_bundle_library.sh), not upstream adoption: distribution.md must cover how that library is
  built per interpreter, and the amalgamation must be usable by that script if that simplifies it.
  `chmod +x python/host/build_bundle_library.sh` if it is still not executable.
- Memory guard on every binary; presets one at a time, -j4.
