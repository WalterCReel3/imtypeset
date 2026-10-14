# gate-042: the gate for 0.4.2 (builder), 2026-10-14

Tree: v0.4.1 + the two docs commits + h52 and issue4 uncommitted + the lead's
README/CHANGELOG edits. Debian 12 x86-64, GCC 12.2, FreeType 2.12.1, CPython
3.11.2. Nobody else wrote the tree during the run. Nothing was fixed: no step
was red. Logs: `build/gate-042-logs/` (`summary.txt`, `<preset>.{configure,build,ctest}.log`,
one log per tool). The private build directories are removed.

## The library diff since v0.4.1

`git diff --stat v0.4.1 -- src include python/imtypeset`:

    include/imtypeset.h       | 4 ++--   IMTYPESET_VERSION 0.4.2, _NUM 402
    include/imtypeset_c.h     | 4 ++--   IMTYPESET_C_VERSION 0.4.2, _NUM 402
    python/imtypeset/_capi.py | 2 +-    C_VERSION = "0.4.2"
    src/opentype.cpp          | 1 +     ++_outlines_run in OpenTypeFont::bounds()
    src/opentype.h            | 5 +++++  outlines_run() accessor + the member
    5 files changed, 11 insertions(+), 5 deletions(-)

As the task says: version strings and the internal counter only.

## Presets

Each `cmake --preset <p> -B build/gate-042-<p>` from a directory that did not
exist, `cmake --build -j4`, `ctest -j4 --output-on-failure`; one preset at a
time, 09:23 to 10:02, load average 2 to 7 (no timing was taken). Sanitised:
`ASAN_OPTIONS=hard_rss_limit_mb=3000`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`,
`timeout 3000`; others `ulimit -v 6000000; timeout 3000`.

| preset | ctest | warnings in the build |
|---|---|---|
| debug | 55 of 55 | 0 |
| release | 54 of 54 | 0 |
| asan | 54 of 54 | 0 |
| stb | 54 of 54 | 0 |
| wchar16 | 54 of 54 | 0 |
| amalgamation | 54 of 54 | 0 |
| fp-host | 54 of 54 | 0 |
| exceptions | 54 of 54 | 4 `-Wterminate`, all in Dear ImGui's destructors (`~ImDrawListSharedData`, `~ImFontAtlas`, `~ImGuiContext`, `~ImGuiWindow`, `.deps/imgui/imgui*.cpp`), the known four |
| bundle-config | 54 of 54 | the same four |
| imgui-next | 54 of 54 | 0 (configure warns: ../imgui is 19298, newer than tested 19290 — expected) |
| python-host | 56 of 56 | 0 |

No warning from this tree's sources in any preset. No ctest test reported
skipped. Counts are two above `docs/distribution.md` section 7's 2026-10-12
table (53/52/54); that table is not updated here.

## Tools

| command | result |
|---|---|
| `python3 -I -B tools/version.py --check --tag v0.4.2` | rc 0: "version 0.4.2: every copy agrees, and the tag v0.4.2" |
| `python3 -I -B tools/ci/test_wheel.py` | rc 0: 35 tests OK, none skipped |
| `python3 -B tools/ci/check_workflows.py` | rc 77: SKIPPED, no PyYAML in any interpreter on this machine (system, ../imgui-adv-text/.venv, ../pptmstr/.venv); no network to install it |
| `../imgui-adv-text/.venv/bin/python -I -B tools/wheel/dependent_check.py --out <mktemp>` | rc 0: 17 ok of 17; marker `python_version >= '3.11' and python_version < '3.15' and (linux x86_64 or darwin arm64)` for `imtypeset==0.4.2.192900`; both stand-in wheels written (scratch, removed) |
| `tools/oracle/pin_reference.sh` | rc 0: 32e3953, 179 files, matches REFERENCE.lock |
| `PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=build/advtext-reference ../imgui-adv-text/.venv/bin/python -P -B tools/oracle/manifest.py --check` | rc 0: 160 files match MANIFEST.sha256 |
| `clang-format --dry-run -Werror` (14.0.6) on include/imtypeset.h, include/imtypeset_c.h, src/opentype.{cpp,h}, tests/test_stress_font.cpp (the C++ files changed since v0.4.1) | rc 0, no output |
| `python3 -I -B tools/amalgamate.py --out <mktemp>` | rc 0, 5 files |
| `tools/release.sh v0.4.2` | step 1 passes; stops at step 2 (uncommitted tree), as it must before the commit. Tag v0.4.2 does not exist. Steps 4-5 run separately above |

## Not run, and why

- `check_workflows.py`: no PyYAML (above). `actionlint`: needs Docker.
- A release wheel (`tools/ci/wheel_build.sh` / `build_wheel.py --release`):
  needs the network (pip installs imgui-bundle from PyPI; the fork's headers,
  pthom/imgui c9a0a84, are fetched from GitHub and not cached). h52 built a
  stand-in cp311 wheel offline; not repeated.
- `twine check`, `wheel_publish.py` on a set: need wheels from the above.
- `release.sh` steps 2-5 end to end: need the commit.
- Everything on GitHub (ci, sidecar-probe, wheels) and macOS: not here.
