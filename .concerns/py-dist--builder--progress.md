# py-dist — progress (builder; overwritten as work proceeds)

Claimed 2026-10-11. Nothing staged or committed. If this session dies, resume from here.

## Design taken (agent decisions; details will be in the result file)
- Shape: prebuilt wheels, one per (platform, CPython tag, imgui-bundle version). Distribution
  version = `<library version>.<bundle line>` (1.92.900 -> 192900); stand-in headers add `+standin`.
- Library inside the package: `imtypeset/libimtypeset_bundle.so`, run path `$ORIGIN/../imgui_bundle`.
- Gate: `imtypeset/_attestation.json` written at wheel-build time from the full probe (child);
  at import a content comparison (sha256, else ELF build-id / Mach-O UUID) plus non-faulting checks.
  No child, no write. Full probe in a child only for the development override without an
  attestation, or `IMTYPESET_GATE=probe`.
- Version source stays `include/imtypeset.h`.

## Done so far
(nothing yet)

## State at the gate rewrite (written mid-task)
- DONE and RAN once (dev override, 1.92.900 env): python/imtypeset/_bundle.py (refusal(), image_id,
  attestation, precheck, gate, attest, open_library, _same_module w/ dyld route), _capi.py
  (library_route, load via open_library), __init__._load (+ host_info route/distribution_version),
  python/host/imtypeset_bundle_shim.cpp (ImTypesetBundle_BuiltFor), build_bundle_library.sh
  (MODULE_SHA256, IMTYPESET_BUNDLE_RPATH, attestation in gate step), tools/bundle_route/attest.py,
  run_gate.py + mutate_gate.py (open_library).
- TODO: tools/version.py, tools/wheel/build_wheel.py, pyproject.toml + backend, tools/ci/test_wheel.py,
  wheels workflow + check_workflows, sidecar_probe.py preload wording, docs/sidecar.md 8-12,
  docs/versioning.md, docs/CHANGELOG.md, tools/release.sh, version bump 0.2.0 -> 0.3.0
  (header, imtypeset_c.h, _capi.C_VERSION, vcpkg.json x2, README), python-errors.md, THIRD-PARTY.md,
  final runs, result file.
- Scratch build dirs: build/py-dist-*.
- DONE (after the above): tools/version.py; tools/wheel/build_wheel.py, local_env.py; version bumped
  to 0.3.0 in include/imtypeset.h, imtypeset_c.h, _capi.py, vcpkg.json x2, README (C++ presets must
  be re-run: debug, release, amalgamation). RAN: wheel for 1.92.900 built (stand-in), installed with
  pip 26.2.1 into build/py-dist/env-900 (links to advtext's site-packages), import with no variable ok.
- NEXT: tools/wheel/check_installed.py (frozen shape, partial updates), 801 wheel, pyproject+backend,
  tools/ci/test_wheel.py, workflow, docs, release.sh, result.
- presets RAN at 0.3.0: python-host 53/53, debug 52/52, release 51/51, amalgamation 51/51 (logs build/py-dist/preset-*.log); python tests to be re-run at the end. Written: check_installed.py, check_wheel.py, imtypeset_backend.py, pyproject.toml, tools/ci/{wheel_matrix.py,wheel_build.sh,wheel_release.sh,test_wheel.py}, wheels.yml, check_workflows rule, tools/release.sh. image-id matching replaced by image_sha256 (a truncated library was accepted by build-id and died with SIGBUS). TODO: rebuild both wheels at 0.3.0 + check_installed in both envs, pip resolver demo, docs, result.
