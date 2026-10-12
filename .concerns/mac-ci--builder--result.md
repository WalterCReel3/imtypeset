# mac-ci — result (builder, 2026-10-11)

Words used below: **RAN** (on this machine, by me, today), **READ** (from a named source),
**RECALLED** (from memory of a tool's documentation; not checked against the tool).
Nothing staged, committed or pushed. Nothing written outside this repository.

## What the user must do

**Whether the sidecar works on macOS is not answered. The answer is the first run of the workflow
below; nothing in this file or in `docs/sidecar.md` is that answer.**

1. **Commit and push `main`** to https://github.com/WalterCReel3/imtypeset. The new files are
   untracked and must be added (list at the end); `git add .github tools/ci python/host docs/sidecar.md`.
2. The push itself starts two workflows (Actions tab): **`sidecar-probe`** and **`ci`**. If Actions
   is disabled for the repository, enable it; no secret and no setting is needed (both workflows
   ask for `contents: read` only).
3. **`sidecar-probe` is the one that answers H1.** Open its run. Each job's **Summary** is a report
   that begins with the verdict for that platform:
   - `H1 (does the sidecar work here): YES ...` / `NO LIBRARY WAS ACCEPTED ...` / `NOT TESTED ...`
   - `H2 (the wheel's own headers): YES ...` / `NO ...`
   The jobs to look at are the four named **`macos-arm64 / ...`**. A red job still has its whole
   report: a job is green only for "H1 YES and H2 YES", and the probe goes on to stand-in headers
   when the wheel's own do not get there, so red can still say "H1 YES ... built against stand-in
   headers".
4. **Send back** (nobody here can read GitHub Actions): for one `macos-arm64` job and one
   `linux-x86_64` job, the Summary text (copy it), or the artifact `sidecar-probe-<job>` (a zip:
   `report.md`, `verdict.json` and a log per stage). If a job is red, the artifact is the better
   thing to send. For `ci`: which jobs are red, and the log of one red job per toolchain.
5. **Three things that can stop a run before it says anything**, and the fix for each:
   - *A job stays "Queued" for many minutes*: its runner label has been retired. Actions →
     `sidecar-probe` → **Run workflow**, and type another label in the form (the form has one field
     per platform; `none` leaves the Intel job out). `ci.yml` names `macos-14` once, in its matrix.
   - *"Unable to resolve action ...@vX.Y.Z"*: a version tag I pinned does not exist. The three
     pins are `actions/checkout@v7.0.1`, `actions/setup-python@v7.0.0`,
     `actions/upload-artifact@v7.0.2`; change the number in both files under `.github/workflows`.
   - *The job "Which jobs" fails*: then no probe job starts; its log is short.

Expect **`ci` to be red on its first run**; that is information, not breakage (see "Never run").
Expect the **Intel job to say NOT TESTED**: PyPI has no macOS x86-64 wheel for either pinned
version, and the job is there to show that by a run.

## What ran here, and the results

All RAN on Debian 12 x86-64, GCC 12.2.0, glibc 2.36, CPython 3.11.2, in the two read-only
environments `../imgui-adv-text/.venv` (imgui-bundle 1.92.900, Dear ImGui 1.92.9) and
`../pptmstr/.venv` (1.92.801, 1.92.8); interpreters with `-P -B`; every process under
`( ulimit -v 6000000; timeout ... )`; nothing installed anywhere. The working tree was in motion
(task `handoff-core` was writing `src/`, `include/`, `python/imtypeset`) while the sidecar runs
were made; the preset run was on a copy of the committed tree.

| what | result |
|---|---|
| `tools/ci/sidecar_probe.py --also-lookup --mutate`, each environment (the command each CI job runs; final run after the last edit) | exit 0 both. **H1 works**: stand-in headers, bound by `link`, gate accepted on the field probe (24 rows), `import imtypeset` accepted, `check_document_edits` 23 PASS, `check_swap_answers` 18 PASS, `mutate_gate` 30 offsets / 87 mutants / 12 crashed / **0 survived**. **H2 fails at `fetch`** (no network), which is what the report says in so many words. 81 to 100 s. A whole report as written: `.concerns/mac-ci--builder--sample-report-linux.md` |
| symbols | 117 Dear ImGui symbols needed (from the objects compiled here, and from the linked library: same list), 117 exported by the module, both wheels. The module exports 11,328 (1.92.900) and 11,288 (1.92.801) symbols |
| `python/host/build_bundle_library.sh` with no option (the new default: the wheel's own headers) | **stops at the fetch**, exit 1, names the commit and the URL, says it does not substitute, and how to ask for stand-in. `git` answered `CONNECT tunnel failed, response 403` |
| the same with `IMTYPESET_BUNDLE_HEADERS=stand-in`, both environments | exit 0, 47 and 50 s; audit: 117 needed all exported, 103 exported all the C ABI; gate accepted. Needed entry is now the module's file NAME with a run path (was its absolute path) |
| a library built in one environment, imported in the other | refused by `_same_module`, naming both modules (unchanged behaviour, re-checked because the needed entry changed) |
| the same objects bound by `lookup` (`IMTYPESET_BUNDLE_BIND=lookup`), both environments | links; audit passes; **loads only after the module is reopened `RTLD_GLOBAL`**: with nothing loaded first and with `import imgui_bundle` first, `undefined symbol: _ZN15ImGuiTextBuffer11EmptyStringE`; with the reopening, gate accepted, both checks and the mutation run pass |
| `tools/ci/interposition_demo.sh`, both environments | a library linked against the system's FreeType reports **2.12.1** loaded normally and **2.13.3** (the wheel's) loaded after the module was made global |
| `tools/ci/test_fetch_headers.py` | 14 tests OK: pinned (not tip) commit fetched by id from a local git repository; cache reused, and refetched after a cached file was edited or the pins changed; unknown commit, wrong sha256, missing file, wrong recorded `imgui.h` sha256 each refused with no cache entry left; second source tried; unknown wheel version exit 2; **every entry of `bundle_pins.toml` derived again from `../imgui_bundle` and `../imgui` and equal**; stand-in headers fetched for both wheels from the local clone |
| `tools/ci/cpp_preset.sh debug` on a `git archive` copy of `caaeab3`, private build directory, `-DIMTYPESET_IMGUI_DIR=.deps/imgui` | exit 0, 0 warnings, **51 of 51** (112 s), `consumer_projects` and `amalgamation` Passed |
| `tools/ci/check_workflows.py` (PyYAML 6.0, `/usr/bin/python3`) | both files `ok`; and on a copy with an action pinned to `@v7`, `contents: write`, a timeout removed, and a malformed file: all four reported, exit 1 |
| `sidecar_summary.sh`, `sidecar_verdict.py` | on a finished report, on a missing directory ("DID NOT START", exit 1 from the verdict) |
| the no-wheel path | `sidecar_probe.py --expect-no-wheel` under an interpreter without imgui_bundle: "NOT TESTED ... expected", `ok=True` |
| `shellcheck` and `sh -n`, `dash -n` | clean on the build script and the six scripts under `tools/ci` |

## What has never run

- **Anything on macOS.** Every Darwin branch: the link line (`-dynamiclib -Wl,-undefined,dynamic_lookup
  -Wl,-exported_symbols_list,... -Wl,-install_name,...`), the wildcards and `#` comments in
  `exports_bundle.txt`, `nm -gU` / `nm -u` / `nm -U` / `nm -m -u` and my parse of `nm -m`, `otool`,
  `sw_vers`, `brew`. All RECALLED. The probe saves each tool's raw output beside what it parsed.
- **Any fetch from the network**: `git fetch --depth 1 https://github.com/pthom/imgui.git <commit>`
  (that GitHub serves these commits by id is inferred from `git submodule update` needing the
  same), the raw URL for `imgui_bundle_config.h`, `pip install`, `apt-get`, CMake's FetchContent
  of Dear ImGui.
- **The library compiled against the fork's headers**, on any platform. The demo window calls two
  functions whose signatures the fork changes. If that does not compile, H2 is red and the probe
  falls through to stand-in headers.
- **Dear ImGui's own layout check passing**, anywhere.
- **Both workflows.** GitHub's expression syntax, the dynamic matrix (`fromJSON`), `github.event.inputs`
  being empty on a push, the `!` exclusions in the artifact path: RECALLED.
- **Clang, any version, any system; arm64.** `ci.yml` builds with `-Werror` as the tree does. Red
  is likely from: Clang warnings GCC does not give; FreeType newer than the 2.12.1 the glyph
  goldens were dumped under; Apple's libm against glibc's where a golden holds a double. Nothing
  was weakened in advance.
- **CPython 3.12 and 3.13** with the sidecar (only 3.11.2 is here).
- `tools/ci/sidecar_install.sh` and `install_deps.sh` beyond syntax (and one run of the first with
  `/bin/false` as the interpreter).
- The presets other than `debug` through `cpp_preset.sh`; `python-host` through it.

READ on 2026-10-11 through WebFetch, which returns a summary and not the page: runner labels
(`macos-14`, `macos-15`, `macos-26` arm64; `macos-15-intel`, `macos-26-intel`; no `macos-13`);
that the three action tags exist; PyPI's file lists (macOS wheels are `macosx_14_0_arm64` only,
CPython 3.11 to 3.14, for both versions).

## Text for the status file (H1, H2, H5)

> | H1 | does the sidecar work on macOS | **needs the user (a run)**. Not answered, and not answerable here. What exists (task `mac-ci`, not pushed): `.github/workflows/sidecar-probe.yml`, whose jobs on `macos-14` (arm64), CPython 3.11 to 3.13, install the PyPI wheel, list what the wheel's module exports against the Dear ImGui symbols the library needs, build a `.dylib` with undefined dynamic lookup, and run the import gate, `import imtypeset` and the headless checks, with the report on each job's summary page whether it passes or fails. Every macOS line of it is written from memory of Apple's tools and has never run. On Linux the same script RAN in both local environments (imgui-bundle 1.92.900 and 1.92.801, CPython 3.11.2): 117 symbols needed, 117 exported, accepted. READ from PyPI (summarising fetch, 2026-10-11): there is **no macOS x86-64 wheel** for either version, so Intel is out on this pin (H17); an Intel job is in the matrix to show that by a run. H16: the file is `libimtypeset_bundle.dylib` on macOS and stays `.so` on Linux; `<out>/LIBRARY` holds the path. **Known before any run:** a library bound by lookup cannot be loaded before `imgui_bundle` is imported, and the import gate's child interpreter loads it first; the workflow gets round that with a harness (`tools/ci/preload`), and the package needs the change itself (task `py-dist`). `docs/sidecar.md` section 7 says what would work for each way the run can fail |
> | H2 | the wheel's own headers, fetched; stand-in an explicit opt-in | **in progress: written, the fetch tested against local sources, never run against GitHub** (not pushed). `python/host/build_bundle_library.sh` now fetches the fork at the commit `python/host/bundle_pins.toml` records for the wheel's version (checked by commit id) and the bundle's `imgui_bundle_config.h` (checked by sha256), refuses an unknown wheel version, and **never falls back**: stand-in headers need `IMTYPESET_BUNDLE_HEADERS=stand-in`. **This changes your `make typeset-lib`**: on a machine without network, or until the fork has been fetched once into `build/bundle-headers`, it stops at the fetch; add `IMTYPESET_BUNDLE_HEADERS=stand-in` to get what you have today. Never done anywhere: compiling the library against the fork's headers, and Dear ImGui's own layout check passing. The needed entry in the Linux library is now the module's file name with a run path, no longer its absolute path |
> | H5 | CI on GitHub | **in progress: written, never run** (not pushed). `sidecar-probe.yml` (above; Linux x86-64 and macOS arm64, CPython 3.11 to 3.13, both pinned wheels) and `ci.yml` (presets `debug`, `release`, `asan`, `stb`, `wchar16`, `amalgamation` by GCC and Clang on Linux and Apple's Clang on macOS; `python-host` with `tests/python` on Linux; the scripts' own tests). The library has never been compiled by Clang; the first run is expected to be red and to say why. Publishing artefacts for a tag is not in it (task `py-dist`, H3) |

## Decisions I took, and why (agent decisions; overrule freely)

1. **Linux stays bound by `link`; lookup is not adopted there.** Measured, not argued: lookup on
   Linux needs the module reopened `RTLD_GLOBAL`, which makes its 11,328 symbols global, FreeType's
   among them, and a library loaded afterwards then gets the wheel's FreeType in place of its own
   (2.12.1 became 2.13.3). Cost of the choice: one Linux library per CPython tag. The lookup build
   is kept as an option because it runs the audit and the load order macOS depends on.
2. **The fork's headers are fetched with git, by commit id; the config header by URL with a
   sha256.** The task asked for "a recorded checksum". No copy of the fork has ever been on this
   machine, so no sha256 of its `imgui.h` can be recorded honestly; the commit id can (it is derived
   from `git ls-tree`), and git checks content against it. The pins file accepts an
   `imgui_h_sha256` once a run has printed one.
3. **Every top-level `*.h` of the fork is fetched**, not three files, because whether the fork's
   `imgui.h` includes a sibling header has not been read.
4. **The default is the wheel's own headers and there is no fallback**, so the script fails today
   on this machine without the opt-in. That is H2 as asked; it breaks existing callers until they
   opt in (`docs/distribution.md` section 1, `README.md`, the spikes, advtext's Makefile).
5. **The probe is one Python driver, not many workflow steps**, so that the thing CI runs is the
   thing that ran here, and so that a failure at any stage still ends in a written report.
6. **Job colour**: green only for H1 and H2 both yes. A stand-in-only success is red, with the
   report saying H1 yes.
7. **`tests/python` are not in `sidecar-probe`** (the spec put them there): they import
   `imtypeset_host` and need the self-hosted library, not the sidecar. They run in `ci.yml`'s
   `python-host` job, Linux only (that library's link is written for ELF).
8. **Actions pinned by exact version tag, not by commit hash**: a hash from a summarised page could
   not be checked here and a wrong one stops every job before it starts.
9. **x86-64 macOS is `macos-15-intel`, not `macos-13`** (the spec's label): READ that `macos-13` is
   no longer listed. Labels are form inputs so a wrong one needs no edit.
10. **`tools/ci/preload/sitecustomize.py`**, a harness, instead of editing `python/imtypeset`
    (not my files): see the first item below.

## For other tasks

- **`py-dist` (owns `python/imtypeset/_bundle.py`, `__init__.py`)**: a lookup-bound library, which
  is the only kind macOS can have, cannot pass the import gate as the package stands. `_CHILD`
  evaluates `ctypes.CDLL(sys.argv[2])` before anything imports `imgui_bundle`;
  `tools/bundle_route/run_gate.py` and `mutate_gate.py` do the same; `_capi.load` is called before
  `imgui_bundle` is imported when the application has not imported it. Needed: import
  `imgui_bundle` first in all of them, and (only if the macOS run shows it is needed, and never on
  Linux for the reason in decision 1) reopen the module `RTLD_GLOBAL`. `_same_module` does nothing
  off Linux, and "was linked for the module at" is not true of a lookup-bound library.
- **The lead**: `docs/distribution.md` section 1 and `README.md` describe the old script (fallback
  to stand-in, absolute needed path, "Linux only"). `docs/decisions.md` has no entry for decisions
  1, 2 and 4; I did not add one (not in my files).
- The run path of a Linux library is still the absolute directory of the wheel it was built
  against. An installed distribution needs `$ORIGIN`-relative; not done.
- The role instructions say to `git add` a new file at once; `CLAUDE.md` and the task say not to.
  I did not. Nineteen new files are untracked and unprotected until somebody adds them.

## Files

New: `.github/workflows/sidecar-probe.yml`, `.github/workflows/ci.yml`,
`python/host/bundle_pins.toml`, `python/host/fetch_headers.py`, `python/host/exports_bundle.txt`,
`docs/sidecar.md`, and under `tools/ci/`: `sidecar_probe.py`, `sidecar_symbols.py`,
`expected_imgui_symbols.txt`, `sidecar_matrix.py`, `sidecar_install.sh`, `sidecar_summary.sh`,
`sidecar_verdict.py`, `preload/sitecustomize.py`, `interposition_demo.sh`, `test_fetch_headers.py`,
`check_workflows.py`, `cpp_preset.sh`, `install_deps.sh`.
Changed: `python/host/build_bundle_library.sh` (rewritten).
Not changed: `python/host/exports_bundle.map`.
In `.concerns/`: this file and `mac-ci--builder--sample-report-linux.md`.
`build/mac-ci*` removed. `build/advtext-reference`, `.deps/` and the sibling trees untouched.
