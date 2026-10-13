# pypi-release: result

Builder of task `pypi-release`, 2026-10-12. Debian 12 x86-64, GCC 12.2,
CPython 3.11.2 (`/usr/bin/python3`), pip 26.2.1, load average 1.4 to 6.2 while
it ran. Every process that loaded the library ran under `ulimit -v 6000000`
or a container's `--memory`. Nothing was uploaded to any index, pushed,
tagged, committed or `git add`ed; no GitHub setting was changed (two were
READ with `gh api`).

## What the owner must do

Once:

1. **PyPI: the pending trusted publisher**, if not already entered: project
   `imtypeset`, owner `WalterCReel3`, repository `imtypeset`, workflow
   `wheels.yml`, environment `pypi`. It does not hold the name; the first
   upload does (READ: docs.pypi.org, "Creating a PyPI project with a Trusted
   Publisher"). The account needs two-factor authentication (READ:
   pypi.org/help).
2. **Know what the GitHub environment `pypi` does today.** The lead's note
   said "no protection rules". `gh api repos/WalterCReel3/imtypeset/environments/pypi`
   answered otherwise during this task: **one rule, required reviewers:
   `WalterCReel3`**, no branch or tag restriction. So every release's
   `Publish to PyPI` job waits until the owner clicks "Review deployments" on
   the run's page. That is a sound second step; it is only a surprise if
   nobody expects it. Keep it or remove it there.
3. Nothing for TestPyPI: it is skipped, as the owner said. The job exists,
   is off unless a person starts the workflow by hand with `testpypi` = true,
   and nothing on the path to PyPI needs it.

For 0.4.1 (`docs/releasing.md`, section 3, has every command):

4. **Commit this tree** (the lead; with the Clang agent's work). The commit
   is also what makes the licence true: `LICENSE` was written on the owner's
   words as the coordinator relayed them ("MIT, yes"), which I did not hear
   myself. Read `LICENSE` (it is imscribe's file, byte for byte) and D82.
5. **Push the branch / `main` and get `wheels` green BEFORE any tag.** Three
   things in it have never run on GitHub: the macOS jobs with the corrected
   check, the CPython 3.14 rows (macOS 3.14 has run nowhere), and the file as
   rewritten. A tag whose wheels fail is a spent version number: tags are
   never moved, and PyPI never takes a file name twice.
6. `PYTHON=/usr/bin/python3 tools/release.sh v0.4.1`, then the two commands
   it prints (`git tag -a v0.4.1 …`, `git push origin v0.4.1`).
7. Approve the deployment on the run's page; then verify:
   `pip install imtypeset==0.4.1.192900` in a new venv, and the one-line
   import in `docs/releasing.md` (expected output is written there; it is
   what the same command printed here from a local wheel directory).
8. Tell imscribe (text below).

Decisions of mine to confirm or overturn (all in D82): no sdist; a release
is all sixteen wheels or none; a plain `linux_x86_64` tag is not a release;
macOS accepts `imtypeset` and `imgui_bundle` in two directories.

## 1. Header checksums, and the first release wheels

- **Fetched here** from github.com (`python/host/fetch_headers.py`, both
  versions, into a private cache) and each value also computed by hand with
  `sha256sum imgui.h` and `sha256sum $(ls | LC_ALL=C sort) | sha256sum`: the
  four values on the board, exactly. Third source: I counted them in the
  saved CI logs and probe artifacts (`build/ci-artifacts`): identical in
  every job, Linux and macOS. **Recorded** in `python/host/bundle_pins.toml`;
  its header comment now says how, and what a new entry starts without.
  After recording: `--require-recorded` passes for both and uses the cache;
  `tools/ci/test_fetch_headers.py` 17 tests OK.
- **Release wheels, built here against the fork's headers**, through
  `tools/ci/wheel_build.sh` with `IMTYPESET_RELEASE_TAG=v0.4.1` (that script
  had never run anywhere but GitHub: new venv, `pip install imgui-bundle==X`
  from PyPI, `build_wheel.py --release`, `check_wheel.py --release --tag`,
  second new venv, install, `pip check`, `check_installed.py` with no
  variable set):

  | wheel | where | result |
  |---|---|---|
  | `imtypeset-0.4.1.192900-cp311-none-manylinux_2_28_x86_64.whl` | this machine | accepted by `imgui-check+probe`, 26 probe rows, 59 PASS 0 FAIL |
  | `imtypeset-0.4.1.192801-cp311-…` | this machine | the same |
  | `imtypeset-0.4.1.192900-cp314-…`, `…192801-cp314-…` | container `python:3.14-bookworm` (CPython 3.14.8, GCC 12.2), as a non-root user | the same, 59 PASS each |

  Tag measured: GLIBC_2.14, GLIBCXX_3.4.21. 608 kB, 12 files. The four are
  in `build/pypi-release/wheelhouse` (kept; the environments are removed).
  The cp311 pair was built twice more after later edits, same results.
- **Not run:** `check_installed.py` with `--dev-library`, `--other-python`,
  `--self-hosted-library` on these wheels (4 SKIP lines each); the local
  venvs of advtext and pptmstr were not used at all: imgui-bundle came from
  PyPI into new environments, which is the stronger form of the same check.
  CPython 3.12/3.13 not run here (green on GitHub at 0.3.0).

## 2. The macOS layout check

**Rule, by binding** (`docs/sidecar.md`, section 9, new table): Linux links
the wheel's module by name with run path `$ORIGIN/../imgui_bundle`, so apart
is refused, kind `layout`; macOS binds by lookup, names no file, and apart is
**accepted**, the module being held to the attestation by content before the
load. macOS is not made to refuse: a refusal would protect nothing, and a
freezer may separate the two packages.

- The package already did this (`_bundle.precheck` keys on the attestation's
  `run_path`); **only the test was wrong**. `tools/wheel/check_installed.py`
  now checks that the binding is the platform's (`bind`/`run_path` from the
  attestation) and expects the refusal or the import accordingly.
- RAN: the Linux branch, in every run above. Checked against real data: a
  macOS wheel from the CI artifacts has `bind: lookup`, `run_path: ""`, which
  is what the new branch tests for.
- **NOT RUN: the macOS branch itself.** It asserts more than "imported"
  (checked in `attestation`, module matched by content, second-module check
  done). Those hold for the baseline case on macOS in the CI log, so I expect
  green, and it is the first thing to look at in the next macOS run.

## 3. The `tools/ci` failure

Cause found and reproduced, not environment noise in the sense guessed
(nothing to do with imgui_bundle being absent). The test cut the
interpreter's `_ctypes` to half its LENGTH and expected it not to load.
GitHub's hosted CPython 3.11.17 has a `_ctypes` of 606,824 bytes whose last
`PT_LOAD` segment ends at 133,664 (debugging sections follow), so half the
file is everything a loader maps, and it loads. Here `_ctypes` is 135,648
bytes, so the cut was real. Reproduced by downloading that hosted Python's
tarball (`actions/python-versions`, 3.11.17 for ubuntu-22.04) and pointing
the test at its `_ctypes`: "AssertionError: ImportError not raised", the CI's
words. The test now cuts at half of what is mapped; passes with both files.
The product was right: a file whole to the loader is not a truncated file.
`build/pypi-release/repro_truncated.py` and the hosted `_ctypes` are kept.

## 4. Metadata, licence, twine

- **Licence: MIT** (the coordinator's relay of the owner). `LICENSE` at the
  root, copied from `../imgui-adv-text/LICENSE` ("Copyright (c) 2026 Walter
  Reel"). In a wheel: `License-Expression: MIT`, three `License-File`s
  (`LICENSE`, `THIRD-PARTY.md`, `NOTICE.txt`) in `dist-info/licenses/`.
  Metadata 2.4, no `License` field, no licence classifier (READ:
  packaging.python.org, "Core metadata specifications": the two are mutually
  exclusive and PyPI rejects both together; classifiers deprecated). Also
  changed: `THIRD-PARTY.md`, `README.md`, `vcpkg.json`, `ports/imtypeset/*`,
  `docs/distribution.md`, `pyproject.toml` (a comment: it has no `[project]`
  table by design), and **D82 appended to `docs/decisions.md`**. CLAUDE.md
  not edited by me. No source file got a header.
- **Metadata**: Summary, Author, Keywords, 11 classifiers, four Project-URLs
  (Repository, Changelog, Documentation = `docs/sidecar.md`, Issues),
  `Requires-Python: ==3.<n>.*`, `Requires-Dist: imgui-bundle==X`,
  `text/markdown` description.
- **The PyPI README** is `python/README-pypi.md` (new, 97 lines). Its usage
  example is run by `check_installed.py` **from the installed wheel's own
  metadata**, so what the page shows is what ran (59 PASS includes it; it
  prints `0.4.1 (240.0, 31.0) Text set`). `tools/version.py --check` now
  also holds the versions in its examples to the header. The three-line
  "in a frame" snippet under it is NOT run; it mirrors
  `tools/bundle_route/check_swap_answers.py`, which is.
- RAN: `twine check --strict` (twine 7.0.0) PASSED on all four wheels and on
  a stand-in wheel; `packaging` 26.3 parses each file name (release
  `(0, 4, 1, 192900)`, no local, not dev) and each METADATA with validation;
  all classifiers in `trove-classifiers` 2026.9.21.13.
- **Stand-in wheels still cannot be published**: `.dev0+standin` is refused
  by `build_wheel.py --release`, `check_wheel.py --release` (now also by the
  version's shape alone) and `wheel_publish.py`. RAN on a stand-in wheel
  built today.
- **No sdist**: said in `docs/releasing.md` section 1 with the reason. That
  PyPI needs none: nothing in the trusted-publishing pages or the action's
  README asks for one (READ); that a wheels-only project is accepted is
  otherwise my recollection.
- From recollection, not a page: PyPI refuses plain `linux_x86_64` and
  unknown classifiers; where the Yank button is; the Windows values of
  `platform_machine`.

## 5. The publishing workflow

In `.github/workflows/wheels.yml`, the names as fixed by the coordinator
(I had chosen no others):

| job | condition | does |
|---|---|---|
| `publish-pypi` | `github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v')`, needs `wheel` | environment `pypi`, `id-token: write`; `tools/ci/wheel_publish.py`; then `pypa/gh-action-pypi-publish@dc37677b2e1c63e2034f94d8a5b11f265b73ba33 # v1.14.2`, `skip-existing: true` |
| `publish-testpypi` | `workflow_dispatch` with input `testpypi` true (boolean, default false), needs `wheel` | the same to `https://test.pypi.org/legacy/`, environment `testpypi`; the wheels of such a run are built as a release of the tree's version (`IMTYPESET_REHEARSAL`) |
| `release` | the tag-push condition (was: any tag ref) | unchanged otherwise; now behind the same `wheel_publish.py` check |

- **A branch push publishes nothing**: no sending job can run for one, and
  `tools/ci/check_workflows.py` now holds each job's `if`, permissions,
  environment, index URL, the absence of a password, and that the upload
  step follows `wheel_publish.py`. RAN: three files ok; nine mutated copies
  of `wheels.yml` each reported. `actionlint` (container) clean on it.
- **`tools/ci/wheel_publish.py`** (new): tag = version, every wheel passes
  `check_wheel.py --release`, and the set is exactly the matrix (16). RAN:
  refuses my 4 wheels naming the 12 missing cells; accepts with the matrix
  narrowed in a scratch script; refuses tag `v0.4.2`; refuses the twelve
  0.3.0 wheels downloaded from the CI run (which also showed the artifact
  layout it walks: `wheel-<id>/wheels/*.whl`).
- **Action pins verified with `gh api`**: v1.14.2 is an annotated tag →
  commit `dc37677…`, `commits/v1.14.2` agrees, and `release/v1` pointed
  there today. The four `actions/*` pins already in the files: verified the
  same way, all correct.
- **CPython 3.14 added** to both matrices (wheels: 16 jobs; probe: 11).
  imgui-bundle has cp314 wheels for both versions on both platforms, and
  `setup-python` serves 3.14.8 for both runners (READ: PyPI JSON,
  `actions/python-versions` manifest). Cells and what ran:
  `docs/releasing.md` section 1. Not offered, with what each would take
  (aarch64, musllinux, Windows, Intel macOS, cp310, Pyodide): same section.
- `tools/ci/wheel_build.sh`: a non-tag build now also says on the summary
  page whether a release would accept the wheel. RAN, all three paths.
- **NEVER RUN: the publishing jobs, and the file on GitHub.** In
  particular unverified by any run: the boolean input arriving as the
  string `'true'` in `github.event.inputs`; `environment:` with a `url`;
  the action's attestation step; PyPI accepting the publisher.

## 6. Version

0.4.1 in all six copies (`tools/version.py --check --tag v0.4.1` passes),
CHANGELOG section `## 0.4.1` (and 0.4.0's first line now says it was never
published), `docs/versioning.md` examples moved, rule unchanged. A narrowed
debug build (`smoke`, `test_c_api`, `test_c_api_map`, examples) passed 6 of
6 at 0.4.1; no preset was run. **The CHANGELOG has no entry for the Clang
work**: that agent's to add, in the same section.

## 7. `docs/releasing.md`

Written: what a release is, the one-time setup, the per-release steps with
commands, what goes wrong and what to do (partial upload, yank, never
delete), what the workflow sends and when, what imscribe declares with the
marker and its tests, a table of each PyPI rule with the page it is from
and how it was checked, and what never ran.

## The text for imscribe

For `docs/advtext-swap-status.md`, new requests (the highest in use is T20):

> **T21. Declare imtypeset as a dependency, once `0.4.1` is on PyPI.** Not
> before: the name resolves only after the first upload. Beside the pin you
> have:
>
> ```toml
> "imgui-bundle==1.92.900",
> "imtypeset==0.4.1.192900; python_version >= '3.11' and python_version < '3.15' and ((sys_platform == 'linux' and platform_machine == 'x86_64') or (sys_platform == 'darwin' and platform_machine == 'arm64'))",
> ```
>
> Both exact, moved together (`192900` is `1.92.900`). The marker is where
> imtypeset has wheels: Linux x86-64 (glibc 2.28+) and macOS 14+ arm64,
> CPython 3.11 to 3.14. Elsewhere pip leaves it out and `import imtypeset`
> is `ModuleNotFoundError`: treat that as your "no library" state. Tested
> with pip 26.2.1 against a local wheel directory (`docs/releasing.md`,
> section 5). **One hole no marker closes: Alpine/musl**, where the marker
> is true and there is no wheel, so the install fails there. If that
> matters, make it an extra instead.
>
> **T22. With the wheel installed, set nothing.** An installed imtypeset
> reads no `IMTYPESET_LIBRARY` and no `IMTYPESET_PYTHON`
> (`host_info()["ignored_environment"]` names them if set). `make
> typeset-lib` and those variables remain the development route only.
>
> **T23. The licence is MIT**, the same copyright line as yours; the wheel
> carries `LICENSE` and the third-party notices.
>
> **T24. `pip install -U imtypeset` alone moves your imgui-bundle**
> (measured, dry run: over 1.92.801 it "would install imgui-bundle-1.92.900
> imtypeset-0.4.1.192900"). Your exact pins prevent it; say so to
> applications that pin neither.

The ref: **`v0.4.1`, not created and not pushed.** Until it is, `v0.3.0`
(`2fd705e`) is the only tag on GitHub (`git ls-remote`); `v0.4.0` exists in
this checkout only. 0.4.1 has 0.4.0's API exactly; from imscribe's pin
(`v0.3.0`) the changes are the 0.4.0 section of the changelog. H3 and H5
can be answered "done at `v0.4.1`" once the tag's run has published.

## What I found that nobody asked about

1. **The `pypi` environment has a required reviewer** (above). The board
   and the coordinator both said it had no rules.
2. **`pip install -U imtypeset` replacing imgui-bundle is now measured**
   (a dry run; `docs/versioning.md` said "recalled, not run" and now cites
   it; `docs/sidecar.md` section 8 still has the old sentence, under the
   note that section 14 and `docs/releasing.md` are current).
3. **`>> "${GITHUB_STEP_SUMMARY:-/dev/stdout}"` loses lines** when a
   script's stdout is a file opened by `>`: the append opens a second
   description and the first overwrites it. `wheel_build.sh` did this for
   the platform-tag line; replaced by a pipe. `fetch_headers.py` writes the
   summary from Python and is not affected.
4. **Every C++ preset is red under both Clangs on GitHub** (12 of 12 jobs
   at `5077b22`), which the Clang agent is on; `README.md`'s status
   paragraph now says so and says the wheels are ready and not uploaded.
   The rest of that README still describes 2026-10-12 morning in many
   places; I changed five sentences, not the document.
5. **`docs/sidecar.md` sections 1 to 12 are left as history** with a note
   at the top and a new section 14 that overrides them. Rewriting 950 lines
   of dated RAN/RECALLED statements in place seemed worse than adding the
   account of today; a reader must know section 14 wins.
6. The 0.3.0 wheels GitHub built on `main` were already accepted by
   `imgui-check+probe` against fork headers on macOS and Linux: the only
   thing between them and a release was the unrecorded checksums.
7. The page on PyPI shows the description of one file of a release
   (recalled: the first uploaded). The per-file section at the end of the
   description says so; the facts above it are the same in every file.
8. Left on the machine: four Docker images pulled for the tests
   (`python:3.14-bookworm`, `python:3.14-alpine`, `python:3.15-slim`,
   `rhysd/actionlint`), about 1.3 GB; and `build/pypi-release` (3 MB: the
   four wheels, the logs, two scratch scripts, the two stand-in dependents).

## What I am least sure of

- **The first tag run end to end.** Everything on PyPI's side is unrun:
  the publisher matching, the action at that commit, attestations. A
  mismatch fails loudly and publishes nothing, but it fails AFTER the tag
  exists. The rehearsal that would catch it is TestPyPI, which is skipped.
- **macOS + CPython 3.14**, never run anywhere, now two of sixteen cells
  that all must pass. If it is red on the branch, dropping 3.14 is one
  entry in `tools/ci/sidecar_matrix.py` (and the README table and marker).
- **The corrected macOS check**, unrun on macOS.
