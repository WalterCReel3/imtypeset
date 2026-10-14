# Releasing: the wheels on PyPI

Written by task `pypi-release`, 2026-10-12, for the person who makes a
release. What a wheel is and why is `docs/sidecar.md`, section 8; what a
version promises is `docs/versioning.md`. This file is the steps, what a
package that depends on imtypeset declares, and what has and has not run.

As everywhere here: **RAN** is done on a machine, **READ** is taken from a
page that is named, **RECALLED** is from memory and unchecked.

**Nothing has been uploaded to any index.** `imtypeset` was free on PyPI and
on TestPyPI on 2026-10-12 (RAN: `https://pypi.org/pypi/imtypeset/json` and
the same on test.pypi.org both answered 404).

## 1. What a release is

A pushed tag `v<version>` on `main`. For it the workflow
`.github/workflows/wheels.yml` builds **sixteen wheels**, and publishes all of
them or none:

| platform | CPython | imgui-bundle | file |
|---|---|---|---|
| Linux x86-64 (`ubuntu-22.04`) | 3.11, 3.12, 3.13, 3.14 | 1.92.900, 1.92.801 | `imtypeset-0.4.1.192900-cp311-none-manylinux_2_28_x86_64.whl`, … |
| macOS arm64 (`macos-14`) | 3.11, 3.12, 3.13, 3.14 | 1.92.900, 1.92.801 | `imtypeset-0.4.1.192900-cp311-none-macosx_14_0_arm64.whl`, … |

Which cells have run, as a wheel built, installed and checked:

| | Linux x86-64 | macOS arm64 |
|---|---|---|
| CPython 3.11 | RAN on GitHub (0.3.0) and here (0.4.1, release build), both imgui-bundles | RAN on GitHub (0.3.0), both; red on one check that is now corrected and has not run there since |
| CPython 3.12, 3.13 | RAN on GitHub (0.3.0), both | the same as 3.11 |
| CPython 3.14 | RAN here in the container `python:3.14-bookworm` (0.4.1, release build), both | **never run** |

`imgui-bundle` has `cp314` wheels for both versions on both platforms, and
`actions/setup-python` serves 3.14 for both runners (READ 2026-10-12:
`pypi.org/pypi/imgui-bundle/<version>/json`, and `versions-manifest.json` of
`actions/python-versions`, 3.14.8).

**No source distribution is published** (an agent's decision, `docs/decisions.md`
D82). A source build needs the exact imgui-bundle wheel already installed, a
C++17 compiler, and the fork's Dear ImGui headers fetched from GitHub by
commit id: not what `pip install` should start on a user's machine because no
wheel matched. Where no wheel matches, pip installs nothing and says so. PyPI
does not ask for one: neither its trusted-publishing pages nor the README of
its publishing action mentions a required sdist (READ 2026-10-12:
`docs.pypi.org/trusted-publishers/using-a-publisher/`,
`github.com/pypa/gh-action-pypi-publish` README), and that PyPI accepts a
project with wheels only is otherwise RECALLED.

**Not offered, and what each would take.** imgui-bundle has wheels for all of
these but the last two (READ: the same JSON).

| platform | what it would take |
|---|---|
| Linux aarch64 (`manylinux_2_28_aarch64`) | An arm64 Linux runner and a row in `tools/ci/wheel_matrix.py`. Nothing else is known to be missing; nothing of this project has been compiled on arm64 Linux, so the first run is the finding. The marker in section 5 then gains `platform_machine == 'aarch64'` |
| Linux with musl (`musllinux_1_2`, x86-64 and aarch64) | A build inside a musl container against the musllinux wheel, and a platform tag for it: `build_wheel.measured_linux_tag` measures glibc's versioned symbols and musl has none, so that function needs a second branch. Not written. No marker can tell musl from glibc (section 5) |
| Windows (`win_amd64`, `win_arm64`) | Unknown whether it is possible. A wheel ships no import library, so the link would need one made from the module's export table, and a Windows module exports only what was marked for export: whether Dear ImGui's symbols are among them has not been looked at. MSVC has never compiled the library |
| macOS on Intel | Nothing to pair with: imgui-bundle has no wheel for it |
| CPython 3.10 | imgui-bundle says `>=3.10` and ships no `cp310` wheel for either version |
| Pyodide (`pyemscripten_2026_0_wasm32`) | Not looked at |

## 2. Once: what the owner sets up

1. **A PyPI account with two-factor authentication.** READ (`pypi.org/help`):
   "Two-factor authentication **is required** on your PyPI account."
2. **A pending trusted publisher on PyPI** (Account → Publishing → add a
   pending publisher, the GitHub tab; READ:
   `docs.pypi.org/trusted-publishers/creating-a-project-through-oidc/`). The
   values, which the workflow is written to match exactly:

   | field | value |
   |---|---|
   | PyPI project name | `imtypeset` |
   | Owner | `WalterCReel3` |
   | Repository name | `imtypeset` |
   | Workflow name | `wheels.yml` (the file's name, not its path) |
   | Environment name | `pypi` |

   From that page: a pending publisher "does **not** create a project or
   reserve a project's name **until** it is actually used to publish", and if
   somebody else registers the name first it "will be **invalidated**". So the
   name is held by the first release and by nothing before it. No API token
   is created and none is stored anywhere.
3. **The GitHub environment `pypi`** (repository → Settings → Environments).
   It exists. RAN 2026-10-12 (`gh api repos/WalterCReel3/imtypeset/environments/pypi`):
   it has one protection rule, **required reviewers: `WalterCReel3`**, and no
   restriction on which branches or tags may deploy. So the publishing job
   waits, for every release, until the owner approves it on the run's page
   ("Review deployments"). That is a second, deliberate step between a pushed
   tag and PyPI; keep it or remove it there. Worth adding in the same place
   (RECALLED, the setting's name may differ): "Deployment branches and tags",
   selected tags only, `v*`.

**Optional, and not done: TestPyPI.** The owner's word of 2026-10-12 is that
TestPyPI is skipped for now. The job `publish-testpypi` is written and cannot
run unless a person starts the workflow by hand with the input `testpypi` set
to true; it then fails at its upload until these exist: an account on
`test.pypi.org` (a separate site and a separate account), a pending publisher
there with the five values above and environment name `testpypi`, and a
GitHub environment named `testpypi`. Nothing on the path to PyPI depends on it.

## 3. For each release

`python3` below is any CPython 3.11 or later; the scripts need nothing
installed, except `tools/ci/check_workflows.py`, which wants PyYAML and is
skipped with a note without it.

1. **The version.** `IMTYPESET_VERSION` in `include/imtypeset.h` and its
   copies (`docs/versioning.md` lists them; the examples in
   `python/README-pypi.md` are among them), then

   ```sh
   python3 -I -B tools/version.py --check
   ```

2. **The changelog.** A section `## <version>` in `docs/CHANGELOG.md`: it
   becomes the notes of the GitHub Release.
3. **A new imgui-bundle version, if there is one**: an entry in
   `python/host/bundle_pins.toml` (the file says how each value is derived;
   the two header checksums are pasted from the first run's summary pages
   after two jobs agree), and the version in `tools/ci/sidecar_matrix.py`. A
   version leaves the matrix when its applications have moved.
4. **Commit, push `main`, and wait for green.** The workflows `ci`,
   `sidecar-probe` and `wheels` run for the push. In `wheels`, each job's
   summary page ends with "a release would accept this wheel" or says why
   not. **Do not tag before all of `wheels` is green:** a tag is never moved
   and PyPI never takes a file name twice, so a tag whose wheels fail is a
   version number that is spent, and the fix is the next patch.

   ```sh
   gh run list --commit "$(git rev-parse HEAD)"
   ```

5. **The pre-flight.**

   ```sh
   tools/release.sh v0.4.1          # PYTHON=/usr/bin/python3 where python3 on PATH is not a plain one
   ```

   It checks the version and its copies, the changelog, a clean tree, that
   the tag does not exist, the drop-in files, the workflow rules and the
   distribution's tests, creates nothing, and prints the two commands of the
   next step.
6. **The tag.**

   ```sh
   git tag -a v0.4.1 -m "imtypeset 0.4.1" <the commit release.sh named>
   git push origin v0.4.1
   ```

7. **Watch the run, and approve it.**

   ```sh
   gh run watch "$(gh run list --workflow wheels --branch v0.4.1 --limit 1 --json databaseId --jq '.[0].databaseId')"
   ```

   Sixteen wheel jobs; then `Publish to PyPI` waits for the approval of
   section 2 (the run's page, "Review deployments"); `Attach the wheels to
   the release` does not wait. Both first run `tools/ci/wheel_publish.py`,
   which refuses the set unless every wheel is a release of this tag and the
   set is the whole matrix, and prints each file's sha256 on the job's
   summary page.
8. **Verify, from PyPI, in a new environment.**

   ```sh
   python3 -m venv /tmp/imtypeset-check
   /tmp/imtypeset-check/bin/pip install imtypeset==0.4.1.192900
   /tmp/imtypeset-check/bin/python -c "import imtypeset, imgui_bundle; i = imtypeset.host_info(); print(imtypeset.version(), i['distribution_version'], i['route'], i['bundle']['accepted_by'], imgui_bundle.__version__)"
   ```

   pip installs `imgui-bundle==1.92.900` with it. The line to expect is
   `0.4.1 0.4.1.192900 installed imgui-check+probe 1.92.900`. RAN here with
   `--find-links <the wheels built here>` in place of the index, CPython
   3.11: that line. And look at `https://pypi.org/project/imtypeset/`: the
   description is `python/README-pypi.md`.

**If it goes wrong.**

- *A wheel job fails on the tag.* Nothing is published: both publishing jobs
  need every wheel job. The version is spent only if the tag stays; the tag
  is not moved (`docs/versioning.md`), so fix and release the next patch.
- *The upload stops partway* (PyPI takes files one at a time). Run the failed
  job again from the run's page: the publishing step has `skip-existing`, so
  it sends what is missing. Do not rebuild: a rebuilt wheel is another file
  under the same name (its attestation carries the time it was made), and
  READ (`pypi.org/help`): "PyPI does not allow for a filename to be reused,
  even once a project has been deleted and recreated."
- *PyPI refuses the publisher* ("invalid-publisher" or similar, RECALLED):
  one of the five values of section 2 does not match the run. The workflow's
  file name and the environment's name are the usual ones.
- *A release that should not be installed.* **Yank it** on PyPI (the
  project's page → Manage → the release → Options → Yank; RECALLED where the
  button is), all files of the version line, with a reason. READ
  (`pypi.org/help`): "A yanked release is a release that is always ignored by
  an installer, unless it is the only release that matches a version
  specifier." So an application with the exact pin still installs it and one
  with a range moves on. Do not delete: the file names would be gone for
  good and exact pins would break. Then release the next patch.

## 4. What the workflow sends, and when

`.github/workflows/wheels.yml`, whose header says the same; held to it by
`tools/ci/check_workflows.py`.

| job | runs for | sends |
|---|---|---|
| `wheel` (the matrix) | every run: a push to `main` that touches the listed paths, a pushed tag `v*`, a run started by hand | artifacts of the run, nothing else |
| `publish-pypi` | **a pushed tag `v<version>` only** (`github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v')`), after every wheel job passed | the wheels to PyPI, environment `pypi`, by Trusted Publishing (`permissions: id-token: write`) |
| `release` | the same condition | the wheels to the GitHub Release of the tag (`contents: write`) |
| `publish-testpypi` | **a run started by hand with `testpypi` = true only** | the wheels, built as a release of the tree's own version from whatever ref the run is on, to TestPyPI, environment `testpypi` |

A push to a branch, `main` or any other, publishes nothing and creates no
release: no job that sends anything can run for it. A run started by hand on
a tag does not publish to PyPI either.

The publishing action is `pypa/gh-action-pypi-publish` v1.14.2, named by its
commit `dc37677b2e1c63e2034f94d8a5b11f265b73ba33`. RAN 2026-10-12 with `gh
api`: `git/ref/tags/v1.14.2` is an annotated tag whose object names that
commit, `commits/v1.14.2` answers the same commit, and it is where the
branch `release/v1` pointed that day. The four `actions/*` ids in the
workflows were read the same way and agree with what the files had. READ in
that action's README and `action.yml` at that commit: it runs on Linux
runners only; it cannot be used from a reusable workflow with trusted
publishing; it runs `twine check` before uploading (`verify-metadata`); and
it makes and uploads PEP 740 attestations by default, for which it needs
nothing but the same `id-token: write`.

## 5. What a package that depends on imtypeset declares

For imscribe (`../imgui-adv-text`), which pins `imgui-bundle==1.92.900`:

```toml
dependencies = [
    "imgui-bundle==1.92.900",
    "imtypeset==0.4.2.192900; python_version >= '3.11' and python_version < '3.15' and ((sys_platform == 'linux' and platform_machine == 'x86_64') or (sys_platform == 'darwin' and platform_machine == 'arm64'))",
]
```

- **Both exact, moved together**: `192900` is `1.92.900` again
  (`docs/versioning.md`). A package that is a library to others may write
  `imtypeset==0.4.*` with the same marker; beside the exact imgui-bundle pin
  the resolver settles on the right line.
- **The marker is where imtypeset has wheels.** Everywhere else it is false,
  pip leaves imtypeset out, and `pip install imscribe` succeeds without it;
  `import imtypeset` is then `ModuleNotFoundError`, which the package must
  treat as "no library here". The upper bound on the Python version is a
  wheel that does not exist yet.
- **The marker's Python range is the wheels' `Requires-Python`** (section 8):
  both are derived from the CPythons of `tools/ci/wheel_matrix.py`
  (`tools/wheel/dependent_check.py` writes the marker from them and checks
  the two agree from one CPython below the matrix to one above it;
  `tools/ci/test_wheel.py` holds this section's line to it). A CPython added to
  the matrix moves both, and this line is edited to match.
- **0.4.1 installs on CPython 3.11 only** (section 8). Under the marker above,
  `imtypeset==0.4.1.192900` on 3.12 to 3.14 fails at imtypeset; pin 0.4.2.
- **What no marker can say:**
  - *musl.* On Alpine the marker is true (it is Linux on x86-64), there is no
    musllinux wheel, and the install fails at imtypeset. There is no marker
    for the C library.
  - *An old glibc or an old macOS* (before 2.28, before 14): the marker is
    true and no wheel fits; but imgui-bundle has no wheel there either.
- **The alternative**, if imscribe would sooner never fail on a platform: an
  extra (`imscribe[typeset]`) holding the imtypeset line, and the plain
  install without it. The owner's to choose.

RAN, 2026-10-12, with `tools/wheel/dependent_check.py` (which prints the two
lines, evaluates the marker with `packaging` 26.3 for eleven environments,
and writes a stand-in dependent: a wheel that is nothing but those two
`Requires-Dist` lines) and pip 26.2.1 against a directory holding the four
wheels built here (`--find-links`; imgui-bundle from PyPI):

| what | result |
|---|---|
| the marker, evaluated: Linux x86-64 and macOS arm64, CPython 3.11 to 3.14 | true |
| the marker, evaluated: CPython 3.10 and 3.15, Linux aarch64, macOS x86-64, Windows x86-64 and arm64, Emscripten | false. The values of `platform_machine` on Windows (`AMD64`, `ARM64`) are RECALLED; the marker is false there by `sys_platform` whatever they are |
| `pip install --dry-run dependent_stand_in`, CPython 3.11, Linux x86-64 | "Would install dependent_stand_in-0.0.0 imgui-bundle-1.92.900 imtypeset-0.4.1.192900" |
| the same in `python:3.15-slim`, the dependent holding the imtypeset line alone | "Would install dependent_only_imtypeset-0.0.0": the marker is false and imtypeset is left out. (With both lines it fails there at `imgui-bundle==1.92.900`, which has no `cp315` wheel: imgui-bundle's limit, not the marker's) |
| the same in `python:3.14-alpine` (musl) | fails: "No matching distribution found for imtypeset==0.4.1.192900; …". The gap named above, seen |
| `pip install imtypeset==0.4.1.192900` in a new environment | installs it and `imgui-bundle==1.92.900`; `pip check` clean; the import with no variable: route `installed`, accepted by `imgui-check+probe` |
| `pip install --dry-run "imtypeset==0.4.1.*" "imgui-bundle==1.92.801"` | pip takes `0.4.1.192900` first, finds its pin unsatisfiable, settles on `0.4.1.192801` |
| `pip install --dry-run "imtypeset==0.4.1"` | "Could not find a version that satisfies the requirement imtypeset==0.4.1 (from versions: 0.4.1.192801, 0.4.1.192900)" |
| `pip install --dry-run -U imtypeset` in an environment holding imgui-bundle 1.92.801 | "Would install imgui-bundle-1.92.900 imtypeset-0.4.1.192900": **it replaces the application's imgui-bundle**, as `docs/versioning.md` warns |

Not run: any of it on macOS, on Windows or on arm64, where the marker was
evaluated and no resolver was started; any of it against PyPI itself.

## 6. What PyPI takes, and what was checked against it

| rule | where it is from | checked how |
|---|---|---|
| a version is `N(.N)*` with any number of parts; `0.4.1.192900` is one | READ: packaging.python.org, "Version specifiers" ("any number of additional components"; no limit on a number's size is stated) | RAN: `packaging` 26.3 parses the four file names; release `(0, 4, 1, 192900)`, no local part, not a development release |
| no local version label on a public index | READ, the same page: PyPI "MUST NOT allow the use of local version identifiers" | a stand-in wheel (`.dev0+standin`) is refused by `build_wheel.py --release`, `check_wheel.py --release` and `wheel_publish.py`; RAN |
| `==0.4.1` does not match `0.4.1.192900`; `==0.4.1.*` does | READ, the same page (zero padding; prefix matching) | RAN, section 5 |
| the file name is `{distribution}-{version}-{python tag}-{abi tag}-{platform tag}.whl`; `.dist-info` holds `METADATA`, `WHEEL`, `RECORD`; with metadata 2.4 and a `License-File`, the files are in `.dist-info/licenses/` | READ: packaging.python.org, "Binary distribution format" | `check_wheel.py` holds every wheel to it; RAN |
| `License-Expression` and `License-File` need metadata 2.4; `License` beside `License-Expression` is refused by PyPI; licence classifiers are deprecated | READ: packaging.python.org, "Core metadata specifications" | `check_wheel.py`, and a test in `tools/ci/test_wheel.py`; RAN |
| the description must render | READ (`pypi.org/help`): "PyPI will reject uploads if the package description fails to render" | RAN: `twine check --strict` (twine 7.0.0) on the four wheels: PASSED |
| classifiers are from the list | RECALLED that PyPI refuses an unknown one | RAN: all eleven are in `trove-classifiers` 2026.9.21.13 |
| a platform tag must be one PyPI knows: `manylinux_*`, `macosx_*`, not plain `linux_x86_64` | RECALLED, not read today | `check_wheel.py --release` refuses plain `linux_<arch>`. That PyPI takes `manylinux_2_28_x86_64` and `macosx_14_0_arm64` is shown by imgui-bundle's own files there |
| one `Requires-Python` per release, kept from the first file uploaded, and given to the installer as every file's | READ (section 8) | every wheel declares the same range; `check_wheel.py` and `wheel_publish.py` refuse anything else; RAN |
| a file is at most 100 MiB, a project 10 GiB | READ (`pypi.org/help`) | a wheel is 608 kB |
| a file name is never reused; a yanked release is ignored unless it is the only match | READ (`pypi.org/help`) | section 3 |

## 7. What ran, and what did not

RAN by the builder of the task, 2026-10-12 (Debian 12, x86-64, GCC 12.2,
CPython 3.11.2, pip 26.2.1; processes that load the library under `ulimit -v
6000000` or a container's memory limit):

- Four release wheels built, checked, installed from scratch and checked
  installed, through `tools/ci/wheel_build.sh` with `IMTYPESET_RELEASE_TAG=v0.4.1`:
  CPython 3.11 on the machine and CPython 3.14 in a container, each for
  imgui-bundle 1.92.900 and 1.92.801 (`docs/sidecar.md`, section 14).
- `tools/ci/wheel_build.sh` without a tag, as a push to `main` runs it, with
  the wheel's own headers and with stand-in headers: section 14 there.
- `tools/ci/wheel_publish.py v0.4.1` on those four: every wheel accepted as a
  release, **the set refused** as 4 of the matrix's 16, naming the 12 missing
  cells, nothing written. With the matrix narrowed in a scratch script to the
  four cells one Linux machine can build: accepted, four files gathered with
  their sha256, `twine check --strict` PASSED on the gathered files. With the
  tag `v0.4.2`: refused by `tools/version.py`.
- `tools/ci/wheel_release.sh` with `IMTYPESET_RELEASE_DRY=1`: stops at the
  same refusal, as it must with four wheels. Its last lines (the notes, the
  dry-run stop) did not run today.
- `tools/release.sh v0.4.1`: step 1 passes; it stops at step 2, the tree
  being uncommitted, as it must.
- `tools/ci/check_workflows.py`, `actionlint` (container `rhysd/actionlint`)
  on the three files, and nine edited copies of `wheels.yml`, each reported.
- Section 5's table.

NEVER RUN:

- **Any upload**, so: PyPI's acceptance of the pending publisher and of
  these files, the action at this commit, the attestations it makes, the
  approval step of the environment.
- **`wheels.yml` as it is now, on GitHub**: the two publishing jobs, the
  `environment` and `id-token` keys, the boolean input, the 3.14 rows of
  both matrices, and macOS with the corrected check.
- **`wheel_publish.py` on a whole release** (sixteen wheels from two
  platforms' artifacts, as `actions/download-artifact` lays them out).
- **A release wheel on macOS.** The macOS wheels GitHub built were 0.3.0 and
  not release builds (no recorded checksums then).

## 8. Requires-Python: one per release (H52, 2026-10-14)

**What a release must check.** Every wheel of a release declares the same
`Requires-Python`, and that range admits the CPython of every wheel's tag.
Today it is `>=3.11,<3.15`. It is written by `tools/wheel/build_wheel.py`
from `tools/ci/wheel_matrix.requires_python()`, which derives it from the
matrix's CPythons (`tools/ci/sidecar_matrix.PYTHONS`) and refuses a matrix with
a gap in it. Nothing else holds a copy. Which CPython a file is for is its
tag, `cp3XY-none-<platform>`. The classifiers are the release's too, the same
in every file: both systems and the four CPythons.

- `check_wheel.py` refuses a wheel whose `Requires-Python` is not the
  release's range, or does not admit the wheel's own CPython.
- `wheel_publish.py`, run before any upload, reads `Requires-Python` from
  every file it is about to send, without looking at the tree. It refuses a set
  whose files disagree, or whose one value leaves out a file's CPython. It also
  refuses a value it cannot read rather than taking it as a yes.
- `build_wheel.py` refuses to build for a CPython outside the matrix, because
  the wheel would declare a range that excludes itself.
- `tools/ci/test_wheel.py`, class `RequiresPython`, covers each of these,
  0.4.1's files as they were written and as PyPI kept them included.

**Why.** 0.4.1 wrote `==3.X.*` into each wheel, one per CPython. On PyPI only
CPython 3.11 could install it.

- READ (`../imgui-adv-text/HANDOFF-imtypeset.md`, H52, which records imscribe's
  run of 2026-10-14): `pip download --no-deps --only-binary=:all: --platform
  macosx_14_0_arm64 --implementation cp --python-version V
  imtypeset==0.4.1.192900` succeeds for 3.11 and fails for 3.12 and 3.13.
  PyPI's JSON gives `requires_python` `==3.11.*` on all eight files.
- READ, through a summarising fetch of Warehouse's source (`pypi/warehouse`,
  branch `main`, 2026-10-14), so the code was not read line by line:
  - `warehouse/forklift/legacy.py` sets `requires_python` on the **Release**,
    and only when the upload creates that release. A later file of the same
    version finds the existing release and does not change the value.
  - `warehouse/packaging/utils.py` gives each file of the simple index
    `"requires-python": file.release.requires_python`.

  So the first file uploaded decides `Requires-Python` for every file of the
  version.
- READ (pip 26.2.1 as installed in `../imgui-adv-text/.venv`,
  `pip/_internal/index/package_finder.py`, `_check_link_requires_python` and
  `LinkEvaluator.evaluate_link`): a link whose `requires_python` does not
  admit the target Python is dropped as `requires_python_mismatch` before
  its tag is looked at.

**Why the wide range is safe.** The per-CPython limit is the wheel's tag.

- READ (`packaging` 26.3, `packaging/tags.py`, `cpython_tags` and
  `compatible_tags`; pip vendors the same function at
  `pip/_vendor/packaging/tags.py`): CPython 3.Y takes `cp3Y-<abi>-<platform>`,
  `cp3Y-abi3-…` and older `cp3X-abi3-…`, `cp3Y-none-<platform>`, `py*-none-…`,
  and `cp3Y-none-any`. It takes no `cp3X-none-<platform>` for an X other than
  Y.
- RAN, with both copies of that module, for 3.11 to 3.14 on
  `manylinux_2_28_x86_64` and `macosx_14_0_arm64`: each CPython takes exactly
  one `cp3?-none-<platform>` tag, its own. `test_a_cpython_takes_only_its_own_cp_tag`
  keeps this as a test (skipped where `packaging` is not installed).

**What 0.4.1 is now.** A file on PyPI is never replaced, so 0.4.1 stays as
it is and installs on CPython 3.11 only. 0.4.2 is the fix, a patch with no
change to the library (`docs/CHANGELOG.md`). Yanking 0.4.1 is the owner's
call and is not needed: a dependent pinning 0.4.1 exactly keeps working on
3.11, and the one change for everyone else is to move the pin.

**NOT RUN:** an upload; `pip download` against PyPI for 0.4.2. Both need the
network. The commands are below.

After the upload, from any machine with network, for each `V` in `3.11 3.12
3.13 3.14` and each platform:

```sh
d=$(mktemp -d)
for V in 3.11 3.12 3.13 3.14; do
  for P in manylinux_2_28_x86_64 macosx_14_0_arm64; do
    python3 -m pip download --no-deps --only-binary=:all: --dest "$d/$V-$P" \
        --platform "$P" --implementation cp --python-version "$V" \
        "imtypeset==0.4.2.192900" >/dev/null 2>&1 && echo "ok   $V $P" || echo "FAIL $V $P"
  done
done
curl -s https://pypi.org/pypi/imtypeset/0.4.2.192900/json | python3 -c \
    "import json,sys; d=json.load(sys.stdin); print(d['info']['requires_python'], sorted({u['requires_python'] for u in d['urls']}))"
```

Eight `ok` lines are expected, followed by `>=3.11,<3.15 ['>=3.11,<3.15']`.
Repeat the loop for `0.4.2.192801`.
