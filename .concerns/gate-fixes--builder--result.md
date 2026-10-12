# gate-fixes — result (builder-40, 2026-10-12)

Words: **RAN** (here, by me, this session), **READ** (from a named source), **RECALLED** (from
memory of a tool, a format or a platform; not checked). Nothing staged, committed or pushed. Nothing
written outside this repository; nothing installed into `../imgui-adv-text/.venv` or
`../pptmstr/.venv` (their interpreters ran scripts with `-B`). No C++ touched. `docs/decisions.md`,
`docs/advtext-swap-status.md` and `docs/CHANGELOG.md` not edited: text for them is below.
The tree passes as left; nothing of mine is running.

One thing to know about this machine: `python3` on PATH is `../pptmstr/.venv/bin/python3`, not the
system's. I ran some tools with it before noticing (nothing was installed; `-B`). The final runs
name `/usr/bin/python3` or a throwaway environment's interpreter.

## Per finding

### F1 — the environment walks around the gate. REPRODUCED, all of it; fixed
**Reproduced** on the installed wheel of 2026-10-11 (`build/py-dist/env-900`), three shapes (plain;
`sys.frozen`; no `sys.frozen` and `sys.executable` an application). Log:
`build/gate-fixes/repro/f1-before.log`.
- (a) `IMTYPESET_LIBRARY` = a sidecar with no attestation: plain, loaded and probed
  (`checked_in: child`); frozen, refused `unchecked` but only AFTER the file was mapped. The same
  with a **truncated** file: **the process died, status -7 (SIGBUS), in all three shapes.**
- (b) = the self-hosted `libimtypeset_pyhost.so`: imported, `host: self-hosted`, in all three.
- (c) = a development build with its attestation: imported, in all three.
- `IMTYPESET_GATE=probe`: plain, the installed library probed in a child with no content check.
  In the unflagged shape it **called `subprocess.run([sys.executable, "-P", "-B", "-c", …])`, i.e.
  would have started the application again** (not in the finding; found by the reproduction).
- `IMTYPESET_PRELOAD_BUNDLE=1`: `preload: global` in all three.

**Changed** (`python/imtypeset/_capi.py` `library_route`, `_bundle.py`, `__init__.py` `_load`):
- The route is decided by the package's own directory. It is **installed** if the directory holds
  `_attestation.json` **or a packaged library** (wider than the ruling's "attestation": otherwise a
  package that lost its attestation in a partial update would become a "development" package and
  obey the variable; with this it stays installed and is refused `unchecked`). An installed package
  reads none of `IMTYPESET_LIBRARY`, `IMTYPESET_PYTHON`, `IMTYPESET_GATE`,
  `IMTYPESET_PRELOAD_BUNDLE`; `host_info()["ignored_environment"]` lists those that were set.
  `IMTYPESET_BUNDLE_STRICT` is still read: it can only refuse.
- Development route (neither file in the directory): nothing is mapped before a content check or a
  child's verdict. `precheck()` now returns `(attestation, child_report)`; with no attestation it
  runs `probe_in_child()` BEFORE `_capi.load`. The child prints a line once the library is loaded,
  so a file that kills it on loading is `not-a-library` and a failed probe is `refused`.
- `child_interpreter()` decides whether a child may be started, running nothing: `sys.frozen`,
  Nuitka's `__compiled__`, and `sys.executable` being a file named `python*`/`pypy*`. Otherwise
  `unchecked`. `IMTYPESET_GATE=probe` where no child can start is `unchecked` too.
- A library without `ImTypesetBundle_LayoutFacts`: development route only, loaded in a child first,
  then accepted by its build information (`ImTypeset_GetBuildInfo` size and C ABI revision;
  self-hosted: `ImTypesetHost_GetInfo`'s Dear ImGui version equals the one imtypeset was compiled
  against; `ImTypeset_DebugCheck*DataLayout`). `host_info()["route"]` is then
  `"IMTYPESET_LIBRARY (build-info check only)"`, and `host_info()["checked_by"]` (new key) says it
  in words. Under an attestation such a library is refused.

**RAN after:** `check_installed.py` sections 5 and 7: nine settings by three shapes on the installed
wheel (27 imports: the plain import, named file never mapped, nothing started, variable named as
ignored), and 18 development-route cases. 92 of 92 in each environment.
**Still RECALLED:** that Briefcase and Nuitka do not set `sys.frozen`, and that Briefcase's
`sys.executable` is a binary named for the application. `child_interpreter()` has met no real
freezer. A frozen application whose executable is named `python…` and sets no flag would be taken
for an interpreter — on the development route only, which a shipped package is not on.

### F2 — a Mach-O cut at its tail. REPRODUCED (hand-made file); fixed
**Reproduced** (`repro/f2-before.log`): a hand-made Mach-O cut 1, 150, 300 bytes into `__LINKEDIT`
kept `same_content == "image"`. Also on ELF, with this build's library: cut 1 to 7,432 bytes from
its tail, still `"image"` (an ELF file cut there loads; it was accepted as the installed file).
**Changed:** `_macho_loaded` and `_elf_loaded` take the file's size and return no hash unless the
file is as long as its headers say (Mach-O: every `LC_SEGMENT_64`'s `fileoff+filesize`, `LC_SYMTAB`,
eight linkedit-data commands; ELF: every program header's bytes, the section header table, every
non-NOBITS section). `attest()` refuses to write an attestation for a file with no second hash.
**RAN:** `test_wheel.py` (hand-made Mach-O, said so in the file; ELF), and an installed library cut
1, 64, 4,096 bytes short is `damaged`; `strip` copies still accepted.
**Still RECALLED:** the Mach-O layout; what dyld does with such a file; that signing keeps every
table inside the length `__LINKEDIT` states. If the last is wrong a signed application is refused
`damaged`.

### F3 — what the second hash covers. Mach-O half as stated; ELF half only partly
**Checked:** on ELF a flipped `p_flags` was ALREADY refused (the program header table lies inside
the first `PT_LOAD`, which was hashed); the ELF header's entry point was not hashed (reproduced).
**Changed:** ELF now hashes the header fields the loader reads (type, machine, entry, flags,
program header offset/size/count) and the program header table explicitly. Mach-O now also hashes
each non-`__LINKEDIT` segment's name, address, memory size and protection, and each section's
address and flags. **The other Mach-O load commands are NOT hashed, and the document says why**:
as I understand it [RECALLED], re-signing rewrites `LC_CODE_SIGNATURE` and the header's count, and
freezers run `install_name_tool`, which rewrites `LC_ID_DYLIB`/`LC_LOAD_DYLIB`/`LC_RPATH`; hashing
them would refuse the re-signed frozen application the second hash exists for. So an added
`LC_LOAD_DYLIB` still passes; `docs/sidecar.md` §9 has the table of hashed / not hashed and states
that price. Existing attestations' `image_sha256` values no longer match (the plain sha256 still
does); no released attestation exists.

### F4 — escapes that were not ImportError. REPRODUCED (3 of 4 by running); fixed
**Reproduced** on HEAD's `_bundle.py` (`repro/f4-before.log`): `library` not an object →
`AttributeError`; `imgui_bundle` without `_imgui_bundle` → `AttributeError`; probe rows not triples
→ `ValueError` (the unpacking statement run alone). "Imports before hashing": by reading.
**Changed:** `_read_attestation()` holds everything the gate reads to its shape (`damaged`);
`same_content` never raises; `module_file()` finds the wheel's module WITHOUT importing
(`sys.modules`, else `find_spec("imgui_bundle")` + extension suffixes), and `precheck` hashes it
before `import imgui_bundle`, then holds the module the import brought to that file. Where the
file cannot be found without importing, the import comes first and
`host_info()["bundle"]["attestation"]["module_found"]` says so. `_load()` turns any other
exception into `ImportError` kind `unchecked`.
**Cost:** when the bundle does not match and is not yet imported, its version comes from
`importlib.metadata`; where a freezer dropped the metadata the sentence says "another imgui-bundle"
without the number. (In an application that imported imgui_bundle first, the number is there.)

### F5 — `+standin` outranks the release. REPRODUCED; fixed
**Reproduced:** `packaging` 26.3: `0.3.0.192900+standin > 0.3.0.192900`. pip 26.2.1 `--dry-run`
over a directory holding the old stand-in wheel and a wheel named as the release (made for the
test, no library in it): "Would install imtypeset-0.3.0.192900+standin" (`repro/f5-pip.log`).
**Changed:** a stand-in build is `<version>.<line>.dev0+standin`. RAN: orders below the release;
pip takes the release, `--pre` too; `==0.3.0.192900` does not match it. `check_wheel`, the release
refusals, `check_installed` and `wheel_build.sh` (`--pre`) follow.
**Found on the way:** `packaging` 21.3 (Debian 12's pip 23.0.1) does not fall back to a development
release for `==0.3.0.*`; so a stand-in wheel alone needs `--pre` there. And a stand-in of a NEWER
line still orders above an older release; only exact pins keep it out.

### F6 — pins. Documented; nothing to reproduce without an index
`docs/versioning.md` ("What an application pins") and `docs/sidecar.md` §8, §11: pin
`imgui-bundle==X` and `imtypeset==0.3.0.<line>` exactly; `==0.3.0` matches nothing (RAN);
`pip install -U imtypeset` alone moves imgui-bundle (RECALLED, not run). T7 below.

### F7 — the Linux platform tag. Measured; CI default changed; container NOT written
**Measured here** (`objdump -T`/`-p`, GCC 12.2, glibc 2.36): `GLIBC_2.14` (one symbol; rest
2.2.5), `GLIBCXX_3.4.21`, `CXXABI_1.3.9`, `GCC_7.0.0`; needed: libstdc++, libm, libgcc_s, libc and
the wheel's module. Tag `manylinux_2_28_x86_64`.
**Changed:** `build_wheel.measured_linux_tag` (also refuses a needed library outside the system's
list and the module → plain `linux_x86_64`); a hand-given tag may claim less, never more;
`check_wheel.py` measures the archive's library again (and `--release` requires that it could);
`wheels` builds Linux on `ubuntu-22.04` and prints each wheel's tag and basis in the job summary.
**Not done:** a manylinux_2_28 container job. `setup-python` does not serve that image and I would
have been writing three unrun mechanisms at once; §8 says what it would take.
**Still RECALLED:** the manylinux limits and library list; that `ubuntu-22.04` exists as a label;
what the measurement gives on a runner (unknown until a run).

### F8 — workflow hygiene. Fixed; pinned by commit id
No `run:` holds `${{ }}` any more (all five passed through `env:`), and `check_workflows.py`
enforces it. Actions are named by commit id with the tag as a comment. **Source of the ids, which
I can cite:** `api.github.com/repos/actions/<name>/git/ref/tags/<tag>` and
`…/commits/<tag>`, fetched 2026-10-12 with the session's WebFetch (a summarising fetch, not git);
the two endpoints agreed for each: checkout v7.0.1 `3d3c42e5…90b1`, setup-python v7.0.0
`5fda3b95…4b97`, upload-artifact v7.0.2 `cf430e03…6cd9`, download-artifact v8.0.2 `9000827c…8333`.
A wrong id fails its job at once. If you would rather not rest on a summarising fetch, the tags
are one `sed` away and `check_workflows.py`'s `PINNED_LINE` is the rule to relax.

### F9 — the header cache vouched for itself. REPRODUCED; fixed
**Reproduced:** `imgui.h` in the cache edited together with its line in `SOURCE.json` →
`from_cache: True`. **Changed:** `_cached()` hashes the files and compares with the TRACKED pins
(`config_sha256`; `standin_headers_sha256`; `imgui_h_sha256` + `imgui_headers_sha256` for the
fork); `SOURCE.json` vouches for nothing. Unrecorded → never cached. `standin_headers_sha256`
recorded for both versions (computed here from `../imgui` at the pinned commits; the digest is
reproducible with `sha256sum $(ls | LC_ALL=C sort) | sha256sum`). **The fork's two values are not
recorded** (no fork here): the first fetch prints the two lines to paste and writes them to
`$GITHUB_STEP_SUMMARY`; `--require-recorded` (set by `build_wheel.py --release`) refuses until
then. So **no release wheel can be built until someone pastes two lines per version from the first
CI run** — intended, and a step the user must know about.

### F10 — `_same_module`. Extended; RAN on Linux; limit stated
Added `_global_imgui()`: `dlsym` on the global scope for four Dear ImGui names, `dladdr` for the
image of each answer; any image but the wheel's module is `refused`. RAN: a library exporting the
names, opened `RTLD_GLOBAL` before the import, is refused; the lookup-bound sidecar with the
module made global is still accepted. **Limits (in §9):** an image opened `RTLD_LOCAL` is not seen
(on ELF the library does not bind to it; on macOS RECALLED as the same, not known); four names
stand for 117; the macOS half never ran.

### F11 — metadata. Fixed
`Metadata-Version: 2.4`; the two `License-File` entries stay (they are in `dist-info/licenses/`);
no `License`, `License-Expression` or classifier; the description says no licence has been chosen
and that the two files are notices. `check_wheel.py` holds wheels to that. RAN: pip 26.2.1
installs it. RECALLED: that an index rejects `License-File` under 2.1 (PEP 639).

### F12 — not mine (C++).

## What ran at the end (all on Debian 12 x86-64, CPython 3.11.2, every loader under `ulimit -v 6000000` and `timeout`, `-j4`)

| what | result |
|---|---|
| both wheels, `build_wheel.py --headers stand-in` | `imtypeset-0.3.0.192900.dev0+standin-…manylinux_2_28_x86_64.whl`, `…192801.dev0+standin…`; 25 probe rows; tag measured |
| `check_wheel.py`; with `--release --tag v0.3.0` | both ok; both refused |
| `local_env.py` ×2, `pip install --no-index --find-links <both> imtypeset imgui-bundle==X`, `pip check` | each its own line; clean |
| `check_installed.py` (`--other-python`, `--dev-library`, `--self-hosted-library`) | **92 PASS, 0 FAIL, 0 SKIP in each** (`build/gate-fixes/check-installed-{900,801}.log`) |
| `tools/ci/test_wheel.py` | 24 tests OK under `/usr/bin/python3` (packaging 21.3) and env-900's (26.3) |
| `tools/ci/test_fetch_headers.py` | 17 tests OK |
| `tools/ci/check_workflows.py` (`/usr/bin/python3`, PyYAML 6) | 3 files ok; a broken copy reported |
| `mutate_gate.py`, dev sidecar 1.92.900 | 31 offsets, 90 mutants, 12 crashed, 0 survived |
| `tests/python/test_binding.py`, `test_demo_sections.py`, self-hosted library built in `build/gate-fixes-pyhost` | 101 OK, 4 OK |
| `sidecar_probe.py --also-lookup --only-stand-in` | H1 YES by link; lookup only with the module global |
| `tools/version.py --check`; `shellcheck tools/ci/*.sh build_bundle_library.sh` | agree; clean |

**Never ran:** anything on macOS; any workflow; a fetch of the fork; a `--release` build that
passes; a real freezer; `pip wheel .` (backend unchanged, not re-run); the C++ presets (no C++
changed by me; hidpi's tree is what my sidecars compiled); `tools/release.sh`;
`check_installed.py`'s `--old-library` branch (rewritten, no old library at hand).

## Decision text for `docs/decisions.md` (append; agent decisions)

> ## Dnn — An environment variable never chooses what a shipped application loads (2026-10-12)
>
> **Decided** (the lead's ruling on review-8 F1, built by task `gate-fixes`). Which route a
> package is on is read from its own directory: holding `_attestation.json` or a packaged library
> it is *installed*, and reads none of `IMTYPESET_LIBRARY`, `IMTYPESET_PYTHON`, `IMTYPESET_GATE`,
> `IMTYPESET_PRELOAD_BUNDLE`, frozen or not; `host_info()["ignored_environment"]` names the ones
> set. Holding neither it is a source checkout, and the variables are a developer's. This
> supersedes D71's "`IMTYPESET_LIBRARY` still wins". *Why the library counts as well as the
> attestation* (the builder's extension): a package that lost its attestation would otherwise
> turn into a development package and obey the variable.
> **Decided:** on the development route nothing is mapped before a content check or a child's
> verdict; an unattested library is loaded and probed in a child first, and where no child can be
> started the import is refused (`unchecked`). Whether one can is decided without running
> anything (`sys.frozen`, Nuitka's `__compiled__`, the executable's name), because starting a
> `sys.executable` that is an application starts the application.
> **Decided:** a library that is not a sidecar is accepted only on the development route, by its
> build information, and `host_info()["route"]` says "(build-info check only)".
> **Measured** (2026-10-12, before the change, installed wheel): the variable naming a truncated
> library killed the process with SIGBUS in a frozen shape; `IMTYPESET_GATE=probe` with an
> application for `sys.executable` tried to start it again. After: 27 imports with a variable set
> are the plain import; 92 of 92 checks in two environments.
> **Not measured:** any real freezer; macOS.
>
> ## Dnn — The second hash: a file as long as its headers say, and what is left out of it (2026-10-12)
>
> **Decided:** a file has no second hash, and is `damaged`, unless it is as long as its own
> headers state (Mach-O segments, `__LINKEDIT` included; ELF section header table and sections).
> **Decided:** the Mach-O load commands other than segments stay out of the hash, because signing
> and `install_name_tool` rewrite them and the hash exists for a re-signed, frozen application; an
> added `LC_LOAD_DYLIB` therefore passes. The gate guards against mismatch and damage, not
> tampering (D71). Segment and section addresses, protections and flags were added.
> **Measured:** on ELF and on hand-made Mach-O files only. **Read/recalled:** everything about what
> Apple's tools rewrite.
>
> ## Dnn — A stand-in wheel is a development release (2026-10-12)
>
> **Decided:** `X.Y.Z.<line>.dev0+standin`, superseding D71's `+standin`, which *measured* orders
> above the release of its line (`packaging` 26.3; pip 26.2.1 chose it). An application pins
> `imgui-bundle==X` and `imtypeset==X.Y.Z.<line>` exactly.
> **Decided:** the Linux platform tag is measured from the built library and re-measured by
> `check_wheel.py`; Linux wheels are built on `ubuntu-22.04`. A manylinux container is not built.
> **Decided:** a header cache is valid only against the tracked pins; the fork's checksums are
> recorded from the first CI run's job summary, and a release build is refused until they are.
> **Decided:** actions are named by commit id (ids read from api.github.com through a summarising
> fetch, two endpoints agreeing); metadata is 2.4 and names no licence.

## For `docs/CHANGELOG.md` (0.3.0, "The Python distribution"; hidpi has that file open)

> - An installed package reads no `IMTYPESET_LIBRARY`, `IMTYPESET_GATE` or
>   `IMTYPESET_PRELOAD_BUNDLE`; `host_info()` gains `"ignored_environment"` and `"checked_by"`.
>   `host_info()["route"]` has a third value, `"IMTYPESET_LIBRARY (build-info check only)"`.
> - In a checkout, a library with no attestation is loaded and probed in a child before the
>   import loads it, and refused (`unchecked`) where no child can be started.
> - A stand-in wheel's version ends `.dev0+standin` (was `+standin`).
> - A file cut short anywhere is `damaged`; an attestation of another shape is `damaged`; any
>   other error while checking is `ImportError` kind `unchecked`.
> The line there that says a library named by `IMTYPESET_LIBRARY` is probed "and for
> `IMTYPESET_GATE=probe`" needs "in a source checkout" added.

## Row updates for `docs/advtext-swap-status.md`

> **H3** — add: "`IMTYPESET_LIBRARY` no longer wins inside an installed distribution: a package
> directory that holds a library or `_attestation.json` reads no `IMTYPESET_*` variable but
> `IMTYPESET_BUNDLE_STRICT` (`host_info()["ignored_environment"]` lists what was set). The
> development override works only with a package directory that carries no library, which is what
> `IMTYPESET_PYTHON=<imtypeset>/python` gives. `host_info()["route"]` is `"installed"`,
> `"IMTYPESET_LIBRARY"` or `"IMTYPESET_LIBRARY (build-info check only)"`. Stand-in wheels are now
> `0.3.0.192900.dev0+standin` (a development release, below the release; `pip install --pre` to
> take one by name). Pin `imgui-bundle==1.92.900` and `imtypeset==0.3.0.192900`, both exactly;
> `imtypeset==0.3.0` matches nothing. Linux tag measured from the library: `manylinux_2_28` here."
> **H6** — add: "*Run here* as well: the library cut 1, 64 and 4,096 bytes short at its tail, the
> library missing, an attestation of another shape (all `damaged`), and a variable left in the
> environment (ignored, and named)."
> **H15** — replace the last sentence with: "A library named by `IMTYPESET_LIBRARY` is honoured
> only from a source checkout's package. With `<library>.attestation.json` it is held to it by
> content before loading; without, it is loaded and probed in a child interpreter BEFORE the
> importing process loads it, and where no child can be started (`sys.frozen`, Nuitka, or a
> `sys.executable` not named like a Python interpreter) the import is refused, `kind ==
> "unchecked"`. *Run here* in three shapes, the third an application that sets no flag."
> **H18** — add: "The check now also refuses a second Dear ImGui answering in the global symbol
> scope (`dlsym`/`dladdr`; *run on Linux*, written and never run for macOS); an image opened
> `RTLD_LOCAL` is not seen. A file is refused unless it is as long as its load commands say; the
> Mach-O side has met only hand-made files. What a person at a Mac reports is in `docs/sidecar.md`
> §10, now including `otool -l` before and after signing."

## `T` requests of advtext

| # | request | why |
|---|---|---|
| T7 | In an application's requirements (and advtext's packaging notes), pin BOTH `imgui-bundle==X` and `imtypeset==0.3.0.<line of X>` exactly, and move them together; advtext's own metadata may state `imtypeset==0.3.*`. Never `pip install -U imtypeset` alone | It takes the newest line and replaces the application's imgui-bundle; `imtypeset==0.3.0` matches nothing |
| T8 | `typesetter.py` route 1 (`IMTYPESET_LIBRARY` + `IMTYPESET_PYTHON`): expect it to work only when `IMTYPESET_PYTHON` names a package directory with no library in it (a checkout's `python/`). If an installed imtypeset is what gets imported, the variable is ignored and `host_info()["ignored_environment"]` says so: surface that in `pairing()` instead of reporting route 1 | The gate's routes changed |
| T9 | Tests that expect `host_info()["route"] == "IMTYPESET_LIBRARY"` for the self-hosted library now get `"IMTYPESET_LIBRARY (build-info check only)"`; and a staged library without its attestation is probed in a child BEFORE load (T3 still stands), refused where `sys.executable` is not named `python*` | Same |
| T10 | Stand-in wheels are `…dev0+standin`: any code or doc that matches `+standin` at the end of a version should match `standin` as the local label; install one by name with `pip install --pre` | F5 |

## Files changed (all pre-existing; none created but this file)

`python/imtypeset/__init__.py`, `_bundle.py`, `_capi.py`;
`python/host/fetch_headers.py`, `bundle_pins.toml`, `build_bundle_library.sh` (header comment only);
`tools/wheel/build_wheel.py`, `check_wheel.py`, `check_installed.py`;
`tools/ci/test_wheel.py`, `test_fetch_headers.py`, `check_workflows.py`, `wheel_build.sh`, `wheel_matrix.py`;
`.github/workflows/ci.yml`, `sidecar-probe.yml`, `wheels.yml`;
`docs/sidecar.md`, `docs/versioning.md`, `docs/python-errors.md`.
Not changed though in the write set: `pyproject.toml`, `tools/bundle_route/*`, `tests/python/*`.
In `_bundle.py`, the `ImGuiIO.DisplayFramebufferScale` probe row is `hidpi`'s (builder-39), agreed
between us; I changed one line inside `_probe()` (`module_file()`).
Under `build/` (ignored): `gate-fixes/` (wheels, both environments, logs, `repro/`),
`gate-fixes-dev-900`, `gate-fixes-dev-801`, `gate-fixes-pyhost`. `build/advtext-reference` and
`.deps/` untouched. `python/imtypeset/__pycache__` may hold a `.pyc` from one `py_compile` of mine.

## Least sure of

1. **The Mach-O side, more than before.** I added a length check and widened the hash on a format
   I cannot run. Both err toward refusal, and refusal in a signed Mac application is the product
   drawing nothing. The one run that settles it is a person at a Mac (§10).
2. **`child_interpreter()` by executable name.** A heuristic, chosen because nothing may be run. It
   only matters on the development route.
3. **Commit ids of the actions** rest on a summarising fetch.
4. **`ubuntu-22.04`**: the label and what the tag measures there.
5. The "installed if a library is present" extension of the ruling: confirm or overrule.
