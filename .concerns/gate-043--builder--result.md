# gate-043: 0.4.3 version bump, changelog, gate (builder), 2026-10-14

Tree: 8091c56 (0.4.2, untagged) + patch-geometry, patch-python, patch-build,
patch-docs (uncommitted) + this task's edits. Debian 12 x86-64, GCC 12.2,
FreeType 2.12.1, CPython 3.11.2. Nobody else wrote the tree. Logs:
`build/gate-043-logs/` (`summary.txt`, `<preset>.{configure,build,ctest}.log`,
one log per tool, `run_presets.sh` the runner). Private build dirs removed.

## Edits

- Version 0.4.3 (403) in `include/imtypeset.h`, `include/imtypeset_c.h`,
  `python/imtypeset/_capi.py`, `vcpkg.json`, `ports/imtypeset/vcpkg.json`,
  `python/README-pypi.md` (the five example versions).
- `README.md` status: 0.4.2 "committed and prepared for upload, not yet
  tagged; 0.4.3, a patch after it, is prepared in this tree".
- `docs/CHANGELOG.md`: `## 0.4.3`, opened with a classification paragraph
  (patch; no name added or removed; `Document.edit_error` now also on the
  class; layout unchanged; the one observable change is the Code-block drag
  stopping before the newline, D88). Bullets checked against the four result
  files and `../imgui-adv-text/HANDOFF-imtypeset.md` H49 (8.000008583 at 16 px
  is imscribe's number); no wording needed changing.
- **Defect fixed in patch-build's own change**, `tools/ci/reproducible_bundle_library.sh`:
  a relative `<python>` argument was run after `cd` into each copy, so it was
  not found. The workflow passes a relative one
  (`wheel-out/build-env/bin/python`, `.github/workflows/wheels.yml` step at
  line 133), so every wheel job would have gone red there. Reproduced here:
  `tools/ci/reproducible_bundle_library.sh ../imgui-adv-text/.venv/bin/python <scratch>`
  -> `build_bundle_library.sh: 131: ../imgui-adv-text/.venv/bin/python: not found`,
  status 2. Fix: the argument is made absolute when it holds a slash (dirname
  through `cd`/`pwd`, basename kept, so a venv's symlinked python is not
  resolved away); a bare name stays on PATH. After: same relative call, rc 0,
  identical. shellcheck clean.

## Library diff since 8091c56

`git diff --stat 8091c56 -- src include python`:

    include/imtypeset.h                 | 103   (sentences, version)
    include/imtypeset_c.h               |   8
    python/README-pypi.md               |  12
    python/host/build_bundle_library.sh |  35
    python/imtypeset/__init__.py        | 129
    python/imtypeset/_capi.py           |   2
    src/blocks.cpp                      |  16
    src/blocks.h                        |  10
    src/imtypeset.cpp                   |  13
    src/imtypeset_document.cpp          | 138
    src/imtypeset_document.h            |   4
    11 files changed, 416 insertions(+), 54 deletions(-)

Plus untracked tests/test_api_code_caret.cpp, test_api_device_advance.cpp,
test_api_header_sentences.cpp, tools/ci/reproducible_bundle_library.sh.
Non-comment header changes: version macros only (two trailing comments moved
into block comments). New Python definitions: underscored helpers and the
class attribute `Document.edit_error`. No public name added or removed.

## Presets

`cmake --preset <p> -B build/gate-043-<p>` from a nonexistent dir, `cmake
--build -j4`, `ctest -j4 --output-on-failure`, one at a time 10:50 to 11:34,
load 2 to 8. Sanitised: `ASAN_OPTIONS=hard_rss_limit_mb=3000`,
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, `timeout 3000`; others
`ulimit -v 6000000; timeout 3000`.

| preset | ctest | gate-042 | warnings |
|---|---|---|---|
| debug | 58 of 58 | 55 of 55 | 0 |
| release | 57 of 57 | 54 of 54 | 0 |
| asan | 57 of 57 | 54 of 54 | 0 |
| stb | 57 of 57 | 54 of 54 | 0 |
| wchar16 | 57 of 57 | 54 of 54 | 0 |
| amalgamation | 57 of 57 | 54 of 54 | 0 |
| fp-host | 57 of 57 | 54 of 54 | 0 |
| exceptions | 57 of 57 | 54 of 54 | the known 4 `-Wterminate`, Dear ImGui's destructors (`~ImGuiContext`, `~ImGuiWindow`, `~ImDrawListSharedData`, `~ImFontAtlas`) |
| bundle-config | 57 of 57 | 54 of 54 | the same 4 |
| imgui-next | 57 of 57 | 54 of 54 | 0 (configure warns ../imgui is newer than tested, expected) |
| python-host | 59 of 59 (python_binding, python_demo_sections pass) | 56 of 56 | 0 |

+3 everywhere = the three new test files. Nothing skipped.

## Tools

| command | result |
|---|---|
| `python3 -I -B tools/version.py --check --tag v0.4.3` | rc 0: "version 0.4.3: every copy agrees, and the tag v0.4.3" |
| `python3 -I -B tools/ci/test_wheel.py` | rc 0, 35 tests OK |
| `/usr/bin/python3 -B tools/ci/check_workflows.py` | rc 0, ok on ci.yml, sidecar-probe.yml, wheels.yml (PyYAML 6.0 is in /usr/bin/python3; `python3` on PATH is pptmstr's venv, which is why gate-042 found none) |
| `../imgui-adv-text/.venv/bin/python -I -B tools/wheel/dependent_check.py --out <mktemp>` | rc 0, 17 ok; pin `imtypeset==0.4.3.192900` with the >=3.11,<3.15 marker |
| `tools/oracle/pin_reference.sh` | rc 0, 32e3953, 179 files, matches lock |
| `manifest.py --check` (pinned reference) | rc 0, 160 files match |
| `IMTYPESET_BUNDLE_HEADERS=stand-in IMTYPESET_IMGUI_CLONE=/home/wreel/Source/imgui tools/ci/reproducible_bundle_library.sh ../imgui-adv-text/.venv/bin/python <mktemp>` (ulimit -v 6000000) | after the fix: rc 0, identical, 14835088 bytes, sha256 fb0657ea…9d9b31. **Stand-in headers** (local ../imgui), not the fork's; the default clone path is `<copy>/../imgui`, which does not exist, so the local clone has to be named or it goes to GitHub (refused here) |
| `clang-format --dry-run -Werror` (14.0.6) on the 10 C++ files changed since 8091c56 | rc 0 |
| `shellcheck` 0.9.0 on build_bundle_library.sh, reproducible_bundle_library.sh | rc 0 |
| `tools/amalgamate.py --out <mktemp>` | rc 0, 5 files |
| `PYTHON=/usr/bin/python3 tools/release.sh v0.4.3` | step 1 passes; stops at step 2 (uncommitted tree), as it must |

## Not run, and why

- Fork headers for the reproducible check, a release wheel, twine, wheel_publish:
  need GitHub/PyPI (no network).
- actionlint: needs Docker. Anything on GitHub or macOS.
- `docs/distribution.md` section 7's table not updated (still 2026-10-12 counts).
