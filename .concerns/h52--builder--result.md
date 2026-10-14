# h52 — builder — result (2026-10-14)

H52: every wheel of a release declares one Requires-Python. Release 0.4.2 is
prepared, with metadata only. Nothing was committed, tagged or uploaded.

## What changed

- `tools/ci/wheel_matrix.py`: two new functions.
  - `requires_python()` derives `>=3.11,<3.15` from `sidecar_matrix.PYTHONS`
    and refuses a matrix with a gap in it.
  - `admits(spec, "3.Y")` says whether every patch of 3.Y satisfies a
    specifier. It answers None for a clause it cannot read, and callers take
    that as a no.
- `tools/wheel/build_wheel.py`:
  - writes `Requires-Python: <the range>` in every wheel;
  - writes the classifiers the same in every file: both OS classifiers and
    all four CPythons, because PyPI keeps classifiers per release too (that
    PyPI does is RECALLED, not read);
  - refuses to build for a CPython outside the matrix.
- `tools/wheel/check_wheel.py`: a wheel's Requires-Python must be the
  release's range and must admit the wheel's own cp tag
  (`requires_python_problems`).
- `tools/ci/wheel_publish.py`: before upload it reads Requires-Python from
  every file and refuses the set if the files disagree, or if the one value
  leaves out any file's CPython (`requires_python_problems`,
  `requires_python_of`).
- `tools/wheel/dependent_check.py`: `MARKER` is now written from the same
  range (`python_marker()`), and it checks that the marker and the range agree
  from 3.10 to 3.15.
- `tools/ci/test_wheel.py`: the old `==3.11.*` assertion is now the range, and
  a new class `RequiresPython` has seven tests. They cover 0.4.1 as it was
  built (the files disagree) and as PyPI kept it (12 files refused), a range
  that is too narrow, an unreadable specifier, `admits` cross-checked against
  `packaging.SpecifierSet`, the marker in `docs/releasing.md`, and the
  cp-tag selection.
- Version 0.4.2 is set in `include/imtypeset.h`, `include/imtypeset_c.h`,
  `python/imtypeset/_capi.py`, `vcpkg.json`, `ports/imtypeset/vcpkg.json`
  and `python/README-pypi.md`.
- `docs/CHANGELOG.md` has a `## 0.4.2` section.
- `docs/releasing.md`: section 5's marker line now pins 0.4.2, with two new
  bullets; section 6 has a new row; there is a new section 8.
- `docs/sidecar.md`: two statements that the wheel declares `==3.11.*` are
  corrected. This file was not named in the task; it had to change because it
  was now wrong.
- `docs/decisions.md`: D85.

## What ran (this machine, Debian 12, CPython 3.11.2)

- RAN: the cp-tag check, with `packaging` 26.3 and with pip 26.2.1's vendored
  copy, for 3.11 to 3.14 on `manylinux_2_28_x86_64` and `macosx_14_0_arm64`.
  Each CPython takes exactly one `cp3?-none-<platform>`, its own, so CPython
  3.12 does not select a `cp311-none-manylinux_2_28_x86_64` wheel.
- READ: `../imgui-adv-text/.venv/lib/python3.11/site-packages/packaging/tags.py`,
  `cpython_tags` at line 398 (the "none" yield is at line 463; only abi3 is
  yielded for older minors, lines 465-476), and `compatible_tags` at line
  605 (`py*` and `cpXY-none-any` only). The pip copy is
  `pip/_vendor/packaging/tags.py`, line 325, with the "none" yield at line 388.
- READ: pip 26.2.1, `pip/_internal/index/package_finder.py`,
  `_check_link_requires_python` (line 61) and `evaluate_link` (line 272). A
  file whose requires_python does not admit the target is dropped as
  `requires_python_mismatch`.
- READ, through a summarising WebFetch of Warehouse `main`:
  - `forklift/legacy.py` sets `requires_python` on the Release only when an
    upload creates it, and a later file finds the release and leaves the value
    alone;
  - `packaging/utils.py` gives each file `file.release.requires_python` in the
    simple API.

  This was read through a summary, not line by line.
- RAN: `tools/ci/test_wheel.py`, 35 tests, OK under three interpreters:
  - `/usr/bin/python3`, without `packaging`, with 1 test skipped;
  - the pptmstr venv python;
  - the imgui-adv-text venv python.
- RAN: `tools/wheel/dependent_check.py`, 17 ok out of 17.
- RAN: `tools/version.py --check --tag v0.4.2` passes, and so does
  `tools/ci/check_workflows.py`.
- RAN: built one cp311 Linux wheel locally with `build_wheel.py`.
  - **It uses STAND-IN headers.** The fork headers (pthom/imgui
    c9a0a84f8a4ddae794a9e1acf6a93afce73d5057) are not cached in
    `build/bundle-headers`, the commit is not in `../imgui_bundle`'s
    submodule, and github.com is refused.
  - The `--release` build was attempted first and stopped at the fetch.
  - The wheel is `imtypeset-0.4.2.192900.dev0+standin-cp311-none-manylinux_2_28_x86_64.whl`,
    608,638 bytes. Its metadata has `Requires-Python: >=3.11,<3.15` and 15
    classifiers.
  - `check_wheel.py` gives ok.
  - `check_wheel.py --release --tag v0.4.2` refuses it for the three
    stand-in reasons only, and Requires-Python passes.
  - `twine check --strict` (7.0.0) PASSED.
  - `packaging` validates the metadata.
  - All 15 classifiers are in `trove_classifiers`.
- RAN: an offline install into `/usr/bin/python3.11 -m venv` (pip 23.0.1)
  under `mktemp -d`.
  - The command was `pip install --no-index --find-links <dir> --pre imtypeset`.
  - imgui-bundle came from a wheel **repacked from the installed copy in
    `../imgui-adv-text/.venv`**, using its RECORD (read-only). No imgui-bundle
    wheel file exists on this machine. The repacked file is not PyPI's file,
    but the `.so` bytes are the same.
  - `pip check` is clean.
  - The import printed `0.4.2 0.4.2.192900.dev0+standin installed probe 1.92.900`,
    and the installed metadata has `Requires-Python` `>=3.11,<3.15`.
  - `tools/wheel/check_installed.py` gave 59 PASS, 0 FAIL and 4 SKIP (the
    skips need `--dev-library`).
- `git log v0.4.1..HEAD` shows two commits, both documents only. Nothing
  committed since v0.4.1 changes behaviour.
- The scratch directories and `build/h52*` were removed.

## What I did not verify

- A release (fork-header) build; anything on macOS, 3.12, 3.13 or 3.14; any
  upload; PyPI's answer for 0.4.2; the C++ presets and ctest after the version
  bump. The library did compile with the 0.4.2 headers inside the wheel
  build, and `build_wheel` checks that the library's version equals the
  header's. The bundle library compiles the generated `dist/imtypeset_c.cpp`
  (READ: `python/host/build_bundle_library.sh`), so the version
  static_assert was compiled.
- That PyPI keeps classifiers per release, which is RECALLED. The change is
  harmless either way.

## Row for docs/advtext-swap-status.md (lead to place)

| H52 | Every wheel of a release declares one Requires-Python | **done in the tree, not released**: every wheel declares `>=3.11,<3.15` (derived from `tools/ci/wheel_matrix.py`); `check_wheel.py` and `wheel_publish.py` refuse a file or a set that does not; the dependents' marker is derived from the same range; version 0.4.2, packaging only (D85, `docs/releasing.md` section 8). Cp-tag selection checked against packaging 26.3 and pip 26.2.1 (RAN); Warehouse's per-release `requires_python` READ via a summarising fetch. One cp311 wheel built (stand-in headers: no network for the fork's), checked, installed offline and imported. Waiting on: the owner's commit, push, tag `v0.4.2`, approval, and the post-upload `pip download` check | open (release pending) |

## Commands the user must run (network, GitHub, PyPI)

Before tagging, replace the placeholder bullet in the `## 0.4.2` section of
`docs/CHANGELOG.md` ("TO BE NAMED BEFORE THE TAG") with the test changes that
actually landed. Then:

```sh
git diff v0.4.1 -- src include python/imtypeset   # expect: the version lines, and only test-support changes
python3 -I -B tools/version.py --check
python3 -I -B tools/ci/test_wheel.py
# commit, push main, then wait for ci, sidecar-probe and wheels to be green:
gh run list --commit "$(git rev-parse HEAD)"
tools/release.sh v0.4.2
git tag -a v0.4.2 -m "imtypeset 0.4.2" <the commit release.sh named>
git push origin v0.4.2
gh run watch "$(gh run list --workflow wheels --branch v0.4.2 --limit 1 --json databaseId --jq '.[0].databaseId')"
# approve "Publish to PyPI" on the run's page (Review deployments)
```

After the upload (also in `docs/releasing.md`, section 8):

```sh
d=$(mktemp -d)
for L in 192900 192801; do
  for V in 3.11 3.12 3.13 3.14; do
    for P in manylinux_2_28_x86_64 macosx_14_0_arm64; do
      python3 -m pip download --no-deps --only-binary=:all: --dest "$d/$L-$V-$P" \
          --platform "$P" --implementation cp --python-version "$V" \
          "imtypeset==0.4.2.$L" >/dev/null 2>&1 && echo "ok   $L $V $P" || echo "FAIL $L $V $P"
    done
  done
done
curl -s https://pypi.org/pypi/imtypeset/0.4.2.192900/json | python3 -c \
    "import json,sys; d=json.load(sys.stdin); print(d['info']['requires_python'], sorted({u['requires_python'] for u in d['urls']}))"
```

The expected result is 16 `ok` lines, then `>=3.11,<3.15 ['>=3.11,<3.15']`.
After that, on the Mac where the owner found the fault, with a Python newer
than 3.11:

```sh
python3 -m venv /tmp/imtypeset-check && /tmp/imtypeset-check/bin/pip install imtypeset==0.4.2.192900 \
  && /tmp/imtypeset-check/bin/python -c "import imtypeset; print(imtypeset.version(), imtypeset.host_info()['route'])"
```
