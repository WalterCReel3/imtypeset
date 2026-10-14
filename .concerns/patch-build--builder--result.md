# patch-build (H45, reproducible library bytes) — builder — result

Status: done. No API change. Not committed, not added (D4).

## Reproduced (before the fix)
Stand-in headers (`IMTYPESET_BUNDLE_HEADERS=stand-in`, from the local `../imgui`; the
sandbox has no network, so the fork's headers were not fetched). GCC 12.2, Linux x86-64,
imgui_bundle 1.92.900 in `../imgui-adv-text/.venv`. One snapshot of the sources copied to
two paths; the script as at HEAD; different output dir and header cache.

- 14798288 vs 14798352 bytes; 2,776,732 bytes differ; two build-ids.
- The absolute paths of the checkout, `$out/dist` and the header cache: in `.debug_line_str`
  / DWARF, AND in `.rodata` (`__FILE__` via IM_ASSERT: `<out>/dist/imtypeset.cpp`,
  `<cache>/.../imgui/imgui.h`). `.rodata` is loaded, so the wheel's `strip --strip-debug`
  does not remove it. The triage's inference (paths of $out, $inc, repo) is CONFIRMED; it
  missed the caller's working directory (DW_AT_comp_dir) and `.rodata`.

## Fix (python/host/build_bundle_library.sh)
- compilers run from `$out/dist`; `-ffile-prefix-map=$repo=imtypeset`,
  `$out/dist=imtypeset-dist`, `$header_dir=imgui-headers` (repo first; GCC takes the last
  match — measured: with out and cache inside the checkout the specific names win);
  header dir made absolute.
- link the four objects in a fixed order, not `obj/*.o` (glob order is the locale's;
  same order for these names under C and en_US.UTF-8 here, so a guard, not a measured cause).
- macOS: `-Wl,-S` (no debug map: it names objects by absolute path and mtime). RECALLED,
  never run.
- `tools/wheel/build_wheel.py` does not compile; it calls the script. Unchanged.

## Evidence
- Two builds, different checkout paths, out dirs, caches, cwds (defaults inside the
  checkout on one side): identical, sha256 6db22eec…, one build-id d507fe7b….
- Two wheels via build_wheel.py from two copies: the library inside identical (a2678625…);
  `check_wheel.py` ok for both; `check_installed.py` all passed (scratch venv with a copy
  of the bundle wheel). Attestation written beside build B accepted for build A at import
  (matched by sha256).
- `tools/ci/reproducible_bundle_library.sh` on the live tree: identical. Against HEAD's
  script: exit 1, names the differing paths.
- shellcheck clean; `check_workflows.py` ok (system python3's PyYAML).

## CI test
`tools/ci/reproducible_bundle_library.sh <python> <scratch>`: copies src/include/python/
tools + top-level files to two paths, builds fetch+compile+link twice (out/cache default
inside copy 1 vs elsewhere; cwd checkout vs scratch; LC_ALL=C vs C.UTF-8), cmp. Runs in
`.github/workflows/wheels.yml`, job `wheel`, every matrix entry (ubuntu-22.04 x86-64 and
macos-14 arm64), after the wheel step, with the build env's python; fork headers there.
Wheel job timeout 45 -> 75 min (two extra builds; durations not measured).

## Not verified
Fork headers; macOS at all (Apple clang -ffile-prefix-map and ld64 -S RECALLED, LC_UUID
determinism RECALLED); Clang on Linux; the workflow on GitHub. Wheel `_attestation.json`
and METADATA still differ between builds (`attested_on`, a heap address in one probe row):
a record of the probe run, not of the build — left alone.

## Status-file row
| H45 reproducible library bytes | done in 0.4.3 (patch-build) | -ffile-prefix-map for checkout, out/dist, header dir; compile from out/dist; fixed link order; macOS -Wl,-S (recalled). Two builds identical on Linux/GCC 12 stand-in; CI check tools/ci/reproducible_bundle_library.sh in wheels.yml (not yet run there) | D87 |

## CHANGELOG line (0.4.3)
- **fixed (packaging)**: two builds of the sidecar library from the same sources and headers
  now give the same bytes, and so the same sha256 and build-id, whatever the checkout's,
  output directory's and header cache's paths (imscribe's H45): the absolute paths that
  reached the debugging information and `__FILE__` strings are recorded as fixed names.
  Checked on every wheel job by `tools/ci/reproducible_bundle_library.sh`. D87.
