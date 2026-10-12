# py-dist — result (builder, 2026-10-11/12)

Words: **RAN** (on this machine, by me, in this session), **READ** (from a named source),
**RECALLED** (from memory of a tool, a format or a platform; not checked).
Nothing staged, committed, tagged or pushed. Nothing written outside this repository; nothing
installed into `../imgui-adv-text/.venv` or `../pptmstr/.venv`. The tree builds and passes as left.

## What the user must do

1. **Choose a licence** (D24) before any wheel leaves the machine or a GitHub Release is public.
   The wheel's metadata says "no licence has been chosen"; it carries the third-party notices only.
2. **Commit and push.** New files are untracked (list at the end).
3. **Look at the first run of the workflow `wheels`** (Actions tab; it starts on the push). It has
   never run. It needs H2 to work: it builds against the wheel's own headers, fetched from GitHub,
   which no session has been able to do. If the header fetch or the compile against the fork's
   headers fails, every wheel job is red for that reason; to see the rest of the pipeline anyway,
   start it by hand (Run workflow) with `headers` = `stand-in`. Stand-in wheels are marked
   `+standin` and can never be attached to a release.
4. **macOS is still unanswered** (H1): send back one `macos-arm64` job's log from `wheels` and the
   summary from `sidecar-probe`. Everything this task wrote for macOS is RECALLED and never run.
5. **A person at a Mac** is needed for a signed, frozen application (H18): what to report is in
   `docs/sidecar.md` section 10.
6. **The tag**: after the above is green and `hidpi` has landed, `sh tools/release.sh v0.3.0`, then
   the two commands it prints. Conditions are under "The proposed tag".

## What ran, in one table

All RAN on Debian 12 x86-64, GCC 12.2, CPython 3.11.2, pip 26.2.1, every binary and Python under
`ulimit -v 6000000` and `timeout`, `-j4`, load average 2.3 to 4.5, in `build/py-dist*`.

| what | result |
|---|---|
| presets after the version moved to 0.3.0 (`tools/ci/cpp_preset.sh`) | `python-host` 53 of 53, `debug` 52 of 52, `release` 51 of 51, `amalgamation` 51 of 51; no warning |
| `python_binding`, `python_demo_sections`, again on the final tree | both Passed (15.5 s, 0.4 s) |
| both development sidecars, `build_bundle_library.sh`, stand-in headers, final tree | accepted, 24 probe rows, attestation written; 117 symbols needed, all exported; 106 exported, all the C ABI |
| `mutate_gate.py` (1.92.900) | 30 offsets, 87 mutants, **12 crashed, 0 survived** |
| both wheels, `tools/wheel/build_wheel.py --headers stand-in` | `imtypeset-0.3.0.192900+standin-cp311-none-manylinux_2_28_x86_64.whl`, `…192801+standin…`, 580 kB, 11 files |
| `check_wheel.py` | both ok; `--release --tag v0.3.0`: both refused; `wheel_release.sh` (dry) refuses them |
| `pip wheel --no-deps --no-index .` (pyproject backend, isolation on) | same wheel |
| throwaway environments `build/py-dist/env-900`, `env-801` (`tools/wheel/local_env.py`), `pip install --no-index --find-links <both> imtypeset imgui-bundle==X` | each got its own line; `pip check` clean. In the 801 environment pip tried `0.3.0.192900` first and backtracked |
| `check_installed.py`, no `IMTYPESET_*`/`PYTHON*` variable | **28 of 28 and 27 of 27**: import, route `installed`, accepted on the attestation by sha256, the pin, the frozen shape (no process started, nothing written, package read-only), `check_document_edits.py` 23 PASS and `check_swap_answers.py` 18 PASS **through the installed package**, 14 partial-update cases |
| `tools/ci/test_wheel.py` 11 tests, `test_fetch_headers.py`, `check_workflows.py` (3 files ok; a copy with wider permissions and no tag condition: reported), `shellcheck` | pass |
| `tools/ci/sidecar_probe.py --also-lookup --bundle-checkout ../imgui_bundle` (1.92.900) | exit 0, H1 YES by link; lookup accepted only with the module global, as before |
| `tools/release.sh v0.3.0` | on this tree: stops at "uncommitted changes" (correct). On a scratch copy committed in a temporary repository: passes and prints the two commands. `v0.2.0`: refused |
| `import imtypeset` after `import imgui_bundle`, installed | 30 to 34 ms, hashing 28 MB + 1.2 MB included |

**Never ran:** anything on macOS; any fetch from GitHub or PyPI; a wheel against the wheel's own
headers (so `--release` has never accepted anything); `wheels.yml`, `wheel_build.sh`,
`wheel_release.sh` beyond syntax, shellcheck and the dry refusal; an application freezer; CPython
3.12/3.13; pip other than 26.2.1; `tests/python` against the sidecar (they import
`imtypeset_host` and need the self-hosted library, as `mac-ci` found; they ran against that).
The throwaway environments hold imgui-bundle by symbolic links to the existing environments'
site-packages, not by a pip install from an index.

## Per request

### H3 — an installable distribution
**Decided** (D71; `docs/sidecar.md` §8): prebuilt wheels, one per platform × CPython × imgui-bundle;
`Requires-Dist: imgui-bundle==X` exactly; tag `cp3XY-none-<platform>`; two bundle versions are two
version lines of one name (`0.3.0.192900`, `0.3.0.192801`), because a resolver backtracks over
versions only, and an index refuses local versions. No sdist; `pip install git+…@tag` works for a
developer through an in-tree backend that needs nothing downloaded. No third-party backend at all:
the wheel is written by `tools/wheel/build_wheel.py` (the two venvs hold setuptools and hatchling,
which were not needed). The library is at `imtypeset/libimtypeset_bundle.so`, found beside
`_capi.py`; `IMTYPESET_LIBRARY` still wins. Linux run path `$ORIGIN/../imgui_bundle`.
**H26** answered in §9: no refusal of its own; `wrong-platform` for a copied package.
**H21** done on the way: `ImTypesetBundle_BuiltFor`, `host_info()["bundle"]["built_for"]`.
**Ran:** above. **Never:** a real-headers wheel, macOS, an index.

### H4 — releases by tag, a compatibility rule
**Decided** (`docs/versioning.md`): the header is the one place; `tools/version.py --check` holds
five copies to it. Version bumped **0.2.0 → 0.3.0** (header, `imtypeset_c.h`, `_capi.py`, two
`vcpkg.json`, README). `imtypeset.version()` = the library's `0.3.0`;
`host_info()["distribution_version"]` = the wheel's. Until 1.0 the MINOR is what changes when a
name's meaning does; a patch changes nothing observable and adds no name. Tags `v<version>`,
annotated, never moved. `docs/CHANGELOG.md` seeded for 0.3.0 from the status file's API-change
sections since `5654eee`. `tools/release.sh` checks and creates nothing.

### H6 — what an update is
`docs/sidecar.md` §11: imtypeset + imgui-bundle are one unit; the table of which partial update
gives which refusal, each RAN except "same version, other binary"; under a running process
(RECALLED, Linux and macOS). Every refusal is an `ImportError` whose first line is a sentence with
no path, with `kind`/`sentence`/`detail` (§9 table; held by `check_installed.py`).

### H15 — the gate in a packaged application
**Decided** (§9, D71): the full probe runs in a child **at wheel build**, against the exact
imgui-bundle module the wheel pins, and writes `imtypeset/_attestation.json` (hashes of both
files, every row). Import: hashes compared **before the library is loaded**, then only checks that
read nothing through an offset. No child, no stamp, no context, no font. The probe at import
survives only for `IMTYPESET_LIBRARY` without an attestation (never when `sys.frozen`), or
`IMTYPESET_GATE=probe`; `IMTYPESET_GATE=attested` forbids it. What is trusted is spelled out in §9.
**A finding on the way, worth knowing:** my first draft accepted a rewritten file on the linker's
build-id (to survive re-signing). `check_installed.py` then showed a library truncated to half
being *accepted and killing the process with SIGBUS*. Replaced by a sha256 over what the loader
maps (ELF `PT_LOAD`, Mach-O sections). RAN on ELF (strip accepted; truncation and a flipped byte
refused). The Mach-O reader has read only a file made by hand.

### H18 — `_same_module` without `/proc`; signing
macOS route through dyld's image list (`_dyld_image_count`, `_dyld_get_image_name`): RECALLED,
never run. Never silently skipped: `host_info()["bundle"]["second_module_check"]` says `done: …`
or `not done: <why>`. Signing: §10, as READ/RECALLED, with what a person at a Mac must report.

### H20 — fonts
The installed gate reads no font at all, so a freezer dropping `imgui_bundle/assets` no longer
breaks the import. The full probe takes `IMTYPESET_GATE_FONT`, else the wheel's assets. The library
looks for fonts nowhere; `add_font` opens what it is given. §10 lists the files an application
carries. No font was added to the distribution (not needed once the probe moved to build time).

### From mac-ci — lookup-bound libraries
`_bundle.open_library`: imports `imgui_bundle` first; off Linux, reopens the module `RTLD_GLOBAL`
only if the load then fails; on Linux only with `IMTYPESET_PRELOAD_BUNDLE=1` (interposition kept
out). Used by the package, the gate's child, `run_gate.py`, `mutate_gate.py`. RAN on Linux both
ways. `sidecar_probe.py`'s wording for its first mode changed to match.

## Row text for `docs/advtext-swap-status.md`

> | H3 | an installable Python distribution | **in progress: built and installed here for Linux x86-64 against stand-in headers; CI written, never run; nothing published** (task `py-dist`, not pushed). Prebuilt wheels, one per platform, CPython version and imgui-bundle version: `imtypeset-0.3.0.192900-cp311-none-manylinux_2_28_x86_64.whl` holds the package, `libimtypeset_bundle.so` inside it, and `Requires-Dist: imgui-bundle==1.92.900`. The version is the library's plus the imgui-bundle as a fourth number, so two bundle versions are two version lines: require `imgui-bundle==X` and `imtypeset==0.3.*` and the resolver takes the matching line (*measured here*, pip 26.2.1, a directory of both wheels, both environments). `import imtypeset` needs no variable; `IMTYPESET_LIBRARY` still wins. **How the distribution finds its library, for your loader:** the file is beside the package's modules; `host_info()["route"]` is `"installed"` or `"IMTYPESET_LIBRARY"`; without importing, `_attestation.json` beside `find_spec("imtypeset").origin` marks an installed one and survives a freezer that drops `dist-info`. H26: there is no "no library for this platform" state after an install (pip installs nothing; `ModuleNotFoundError`); a package copied from another system raises with `kind == "wrong-platform"`. H21: `host_info()["bundle"]["built_for"]["wheel_version"]`. Both wheels built here are **stand-in builds, marked `+standin`**; no wheel against the wheel's own headers exists yet. On Linux `imtypeset` and `imgui_bundle` must be in one site directory. No licence chosen: do not redistribute. `docs/sidecar.md` §8, §12 |
> | H4 | releases by tag, a compatibility rule | **done in the tree, no tag yet** (task `py-dist`, not pushed). The version is **0.3.0** (was 0.2.0 through six commits). `imtypeset.version()` is the library's version; until 1.0 the middle number changes when a name's meaning does, a patch changes nothing a host can observe and adds no name: pin `imtypeset==0.3.*`. Library and package are always one release. First tag proposed: `v0.3.0`, created by the user after the conditions in `docs/versioning.md`; then name `v0.3.0` in `imtypeset.ref`. `docs/CHANGELOG.md` has the section; `docs/versioning.md` the rule |
> | H6 | what an update is | **done** (task `py-dist`): `docs/sidecar.md` §11. imtypeset and imgui-bundle are one unit and are replaced together, whole; a partial update is an `ImportError` whose first line is a sentence without a path and whose `kind` says which case (`bundle-mismatch`, `damaged`, `package-mismatch`, `layout`, `unchecked`, `wrong-platform`, …). *Run here* through an installed wheel: the wheel for 1.92.900 beside 1.92.801 and the reverse, a truncated, emptied and altered library, modules of another release, a library from `c9f8902` under this package, a lost attestation. Replacing files under a running process: from reading, not run |
> | H15 | an import gate that works in a packaged application | **done on Linux; the macOS half never run** (task `py-dist`). No child interpreter, nothing written, keyed on content: the field probe runs when the wheel is built and its verdict ships as `_attestation.json`; `import imtypeset` compares sha256 of the library and of the wheel's module before loading anything. *Run here:* `sys.frozen` set, `sys.executable` not an interpreter, process creation disabled, package read-only: imported, nothing started, nothing written; 30 to 34 ms. A copy rewritten by `strip` is accepted by a hash of what the loader maps; the same for a re-signed Mach-O is written and has met no real file. `checked_in` is now `"attestation"` or `"child"`. A library named by `IMTYPESET_LIBRARY` without `<library>.attestation.json` is still probed in a child at each import (never when frozen) |
> | H18 | `_same_module` without `/proc`; code-signing | **written, never run** (task `py-dist`): dyld's image list on macOS; `host_info()["bundle"]["second_module_check"]` says `done: …` or `not done: <why>`, never silence. Signing: `docs/sidecar.md` §10, from reading, with what a person at a Mac must report |
> | H20 | the fonts a packaged application must carry | **done** (task `py-dist`): the installed gate reads no font, so dropping `imgui_bundle/assets` no longer refuses the import. The library looks for fonts nowhere: it opens what `add_font` is given. advtext's own six files in `imgui_bundle/assets/fonts` are advtext's to carry. `docs/sidecar.md` §10 |

API-changes text for the status file: the "The Python distribution" block of `docs/CHANGELOG.md`.

## `T` requests of advtext

| # | request | why |
|---|---|---|
| T3 | In `make typeset-lib`, copy `<library>.attestation.json` beside the staged library (same name, `.attestation.json` appended), and stop removing/expecting `<library>.gate` | Without it the staged library is probed in a child at every import (it works, about 200 ms). Nothing writes or reads `.gate` now |
| T4 | Any test that expects `host_info()["bundle"]["checked_in"] == "process"`, or matches a refusal's old text, changes: `"attestation"`/`"child"`; first line is a sentence, paths are on later lines; prefer `e.kind` | The gate and its messages changed |
| T5 | Once a wheel exists: require `imgui-bundle==1.92.900` and `imtypeset==0.3.*`; recognise an installed imtypeset by `_attestation.json` beside the package or by `host_info()["route"]`, not by metadata alone | Version lines; freezers drop `dist-info` |
| T6 | State the font files advtext needs from `imgui_bundle/assets/fonts` in its own packaging notes | The library does not look there |

## The proposed tag

**`v0.3.0`**, annotated, on `main`. Before it is created:
1. `hidpi` (and anything else touching `include/` or `python/imtypeset`) has landed; if it adds a
   name, it is in the 0.3.0 section of the changelog.
2. `sh tools/release.sh v0.3.0` passes on the commit (clean tree).
3. `wheels` has run green on that commit **against the wheel's own headers** (H2) for at least
   Linux; otherwise the tag's release job attaches nothing, by design, and the tag exists without
   wheels. If macOS jobs cannot pass yet, either take them out of the matrix for the tag
   (`tools/ci/wheel_matrix.py`) or accept a tag with no release: the release job needs every job.
4. A licence is chosen, or the release is kept private.
If 3 cannot be met soon and advtext needs a tag only as a ref to build from, tagging without
wheels is harmless: `make typeset-lib IMTYPESET_REF=v0.3.0` needs only the source.

## Decisions I took (agent decisions; overrule freely)

D71 in `docs/decisions.md` has them with reasons. Beyond it: `docs/distribution.md` §1 and
`README.md` got a pointer paragraph each (lead's files; minimal); `check_workflows.py` now allows
exactly one writing job (`wheels.yml`/`release`, `contents: write`, tag-only, needing all others);
`actions/download-artifact@v8.0.2` was READ through a summarising fetch whose dates were garbled;
`<library>.gate` is gone; the wheel is stripped of debug sections on Linux (14 MB → 1.2 MB).

## Files

New: `pyproject.toml`, `docs/versioning.md`, `docs/CHANGELOG.md`, `tools/version.py`,
`tools/release.sh`, `tools/wheel/{build_wheel.py,check_wheel.py,check_installed.py,local_env.py,imtypeset_backend.py}`,
`tools/bundle_route/attest.py`, `tools/ci/{test_wheel.py,wheel_matrix.py,wheel_build.sh,wheel_release.sh}`,
`.github/workflows/wheels.yml`.
Changed: `python/imtypeset/{__init__.py,_bundle.py,_capi.py}`, `python/host/build_bundle_library.sh`,
`python/host/imtypeset_bundle_shim.cpp`, `tools/bundle_route/{run_gate.py,mutate_gate.py}`,
`tools/ci/{check_workflows.py,sidecar_probe.py}`, `.github/workflows/ci.yml`,
`include/imtypeset.h`, `include/imtypeset_c.h`, `vcpkg.json`, `ports/imtypeset/vcpkg.json` (version only),
`docs/{sidecar.md,decisions.md,python-errors.md,distribution.md}`, `THIRD-PARTY.md`, `README.md`.
In `.concerns/`: this file and `py-dist--builder--progress.md`.
Left under `build/` (ignored): `py-dist/` (wheels, both environments, logs), `py-dist-dev-900`,
`py-dist-dev-801`, `py-dist-lookup-900`. `build/advtext-reference` and `.deps/` untouched.
For `hidpi`: `src/` is untouched; the headers changed only in the two version lines.

## Least sure of

1. **The macOS content hash and a re-signed bundle.** The whole frozen-Mac story rests on
   `image_sha256` reading a real Mach-O and on signing leaving section bytes alone. If either is
   wrong the result is a refusal (`damaged`) in the signed application, not a crash, but it is a
   refusal in exactly the product the goal names.
2. **The manylinux tag** rests on a symbol-version audit of mine against limits RECALLED from the
   policy, not on auditwheel.
3. **Whether a freezer keeps `imtypeset/` and `imgui_bundle/` as siblings on Linux** (the run path
   needs it). Not run.
4. The frozen-shape test imitates a frozen interpreter; it is not one.
