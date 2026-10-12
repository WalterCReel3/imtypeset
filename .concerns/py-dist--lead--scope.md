# Lead scope for `py-dist` (adds to the board detail; read both)

advtext's handoff file gained H15-H20 after the task was declared (re-read
`../imgui-adv-text/HANDOFF-imtypeset.md` whole). Yours in addition to H3, H4, H6:

- **H15 — the import gate must work in a packaged application.** No child interpreter at run time
  (`sys.executable` is the application when frozen), a verdict keyed on CONTENT (hashes of the
  library and of the wheel's module, or a build-time attestation carried inside the wheel), nothing
  written beside the library (a signed bundle is read-only). The child exists because the probe's
  reads can crash on a wrong layout (mac-ci: 8 of 87 mutants die by crash). Decide how safety is kept
  without it: e.g. the risky field reads are done ONCE at wheel-build time in CI against the exact
  imgui-bundle wheel the distribution pins, and what ships is a library whose build recorded the
  wheel's module hash; at import the gate compares hashes (cheap, cannot crash) and runs only the
  reads that cannot fault. Whatever you choose: state what is trusted and why, keep the development
  path (unknown pair -> full probe in a child, as today) behind an explicit switch, and test the
  frozen shape by running with a `sys.executable` that cannot spawn Python.
- **H18** — `_same_module()` reads /proc/self/maps: give it a route that exists on macOS (the list
  of loaded images is available without /proc: decide how from Python/ctypes; written from memory
  of Apple's APIs = never run: say so) and make the check NOT silently skipped: where it cannot be
  done, the gate says so in host_info(). Code-signing: write what is known (a .dylib inside a
  hardened-runtime app must be signed by the app's identity or ad hoc; library validation) as READ,
  and what needs a person at a Mac.
- **H20** — the fonts a packaged application must carry: which files the library and its gate need
  on disk and where they look (the gate's probe looks in imgui_bundle/assets/fonts for a .ttf and
  records a failed probe when there is none — a freezer that drops assets/ would be refused): make
  the gate independent of the wheel's assets (carry a tiny font inside the distribution, or probe
  with the default font), and document what advtext/an application must ship.
- **From mac-ci (its result file, 'For other tasks')**: a lookup-bound library — the only kind macOS
  can have — cannot pass the import gate as python/imtypeset stands, because the gate loads the
  library before imgui_bundle is imported; CI works around it with tools/ci/preload/sitecustomize.py.
  The package must do it itself: import imgui_bundle first, and on platforms that need it make the
  wheel's module's symbols visible before the library loads. mac-ci measured a cost of reopening the
  module RTLD_GLOBAL on Linux (FreeType interposition): read it and keep Linux linked by name
  unless you can show the interposition gone.
- T-requests of advtext you need go in your result for the lead (T1, T2 exist: stand-in opt-in;
  read `<out>/LIBRARY`).
- `hidpi` (H19) is a separate task after handoff-core; not yours.
