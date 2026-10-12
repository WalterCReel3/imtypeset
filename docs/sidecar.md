# The sidecar: imtypeset beside an unmodified imgui-bundle wheel

A Python application that runs on the `imgui-bundle` wheel from PyPI gets
imtypeset as one shared library loaded with `ctypes`. The library holds no
Dear ImGui. Its Dear ImGui symbols are left undefined and are bound to the
wheel's own extension module, so it draws into the application's context, font
atlas and draw lists. The wheel is not touched.

This document says how that binding is made on each platform (sections 1 to
7), how the library reaches an application as an installable wheel and what
`import imtypeset` checks (8 and 9), what a packaged application must carry
(10), what an update is (11), and what of all that a run has established (1
and 12). Three words are kept apart throughout:

- **RAN** — done on a machine, with who and when;
- **READ** — taken from a source or a document, with which;
- **RECALLED** — written from memory of a tool's documentation and not checked
  against the tool. Everything about macOS below is RECALLED until
  `.github/workflows/sidecar-probe.yml` has run.

## 1. Where it stands

| | Linux x86-64 | macOS arm64 | macOS x86-64 |
|---|---|---|---|
| a wheel on PyPI (READ: PyPI's file lists for 1.92.900 and 1.92.801, 2026-10-11, through a summarising fetch) | `manylinux_2_28_x86_64`, CPython 3.11 to 3.14 | `macosx_14_0_arm64`, CPython 3.11 to 3.14 | **none** |
| the wheel exports the Dear ImGui symbols the library needs | RAN: 117 needed, 117 exported, wheels 1.92.900 and 1.92.801 | not known | no wheel |
| a library builds | RAN, stand-in headers, GCC 12.2 | never built | no wheel |
| it loads and binds | RAN, bound by `link` (section 2) | never loaded | no wheel |
| import gate, `import imtypeset`, headless checks | RAN: accepted by the field probe (24 rows then; 25 since D72, run again 2026-10-12, section 12), 23 + 18 checks pass, both wheels | never run | no wheel |
| built against the wheel's own headers | **never**: the fetch needs the network and no session here has had it | never | no wheel |
| Dear ImGui's own layout check passes | never (it refuses stand-in headers, as it should: `sizeof(ImGuiStyle)` 1368 against the wheel's 1372) | never | no wheel |

"RAN" in this table is by the builder of task `mac-ci`, 2026-10-11, on one
machine (Debian 12, glibc 2.36, CPython 3.11.2), in two virtual environments:
`../imgui-adv-text/.venv` (1.92.900, Dear ImGui 1.92.9) and `../pptmstr/.venv`
(1.92.801, Dear ImGui 1.92.8), through `tools/ci/sidecar_probe.py`.

**Whether the sidecar works on macOS is not answered here, and is not to be
answered from this document.** The answer is the first run of the
`sidecar-probe` workflow; section 6 says how to read it.

## 2. How the library finds the wheel's Dear ImGui

`python/host/build_bundle_library.sh` knows two ways, `IMTYPESET_BUNDLE_BIND`:

**`link`** (ELF only; the default on Linux). The wheel's module is named as a
needed library, by file name, with a run path to the directory it is in, and
the linker is told to refuse any undefined symbol (`-Wl,--no-undefined`). When
the library is loaded in a process that has imported `imgui_bundle`, the
dynamic loader finds the needed file through the run path, sees that it is the
file already loaded, and binds to it.

- One library per CPython ABI tag, because the tag is in the module's file
  name (`_imgui_bundle.cpython-311-x86_64-linux-gnu.so`), and per wheel
  version.
- The run path of a development build is the absolute directory of the wheel
  the library was built against. The library inside a wheel
  (`IMTYPESET_BUNDLE_RPATH`, set by `tools/wheel/build_wheel.py`) has
  `$ORIGIN/../imgui_bundle`: it finds the wheel's module beside its own
  package and nowhere else, so on Linux `imtypeset` and `imgui_bundle` must be
  in one site directory, which is where `pip` puts them (section 9, kind
  `layout`). The module has no SONAME (RAN: `readelf -d`), so a module that is
  already loaded is matched by the file the run path leads to, not by name.
- RAN: a library built in one environment and imported in the other (same
  CPython tag, another wheel) is refused before it is loaded, because the
  wheel's module is not the one its attestation records (section 9). Without
  an attestation it would load the first environment's module as a second
  copy, which `_bundle._same_module` refuses by name.

**`lookup`** (the only way on macOS; available on Linux for comparison). The
Dear ImGui symbols are left undefined in the library and found when it is
loaded, among what the process already holds. Nothing ties the library to a
CPython tag or to a path. Two things follow:

1. *The linker no longer checks anything.* The permission to leave symbols
   undefined covers every symbol. So `tools/ci/sidecar_symbols.py audit` runs
   after the link and checks what the linker did not: every undefined Dear
   ImGui symbol is exported by the wheel's module; on Mach-O, every symbol left
   for lookup is one of Dear ImGui's; the library exports the C ABI and nothing
   else.
2. *The wheel's module must be loaded first, and visible.* A library with
   undefined symbols cannot be loaded into a process that has not imported
   `imgui_bundle`. The package arranges it (`_bundle.open_library`, used by
   `import imtypeset`, by the gate's child interpreter and by
   `tools/bundle_route`): `imgui_bundle` is imported before the library is
   opened; if the load still fails **and the platform is not Linux**, the
   wheel's module is opened again with `RTLD_GLOBAL` and the load tried once
   more. On Linux that second step is taken only when
   `IMTYPESET_PRELOAD_BUNDLE=1` asks for it, for the reason measured below,
   and only in a source checkout: an installed distribution does not read
   the variable (section 9).
   `host_info()["bundle"]["preload"]` says what was needed: `import` or
   `global`. RAN on Linux, a lookup-bound library: refused with only the
   import, accepted with `IMTYPESET_PRELOAD_BUNDLE=1`. The macOS half of the
   ladder has never run. `tools/ci/preload` remains as the probe's harness.

### Why Linux does not bind by lookup (an agent decision, measured)

One library for every CPython would be worth having. On Linux it costs this:
Python loads an extension module with its symbols private to it (RAN:
`sys.getdlopenflags()` is `RTLD_NOW`, and `ImGui::GetIO` is not found through
`dlopen(NULL)` after `import imgui_bundle`), so a lookup-bound library loads
only after the module is reopened with `RTLD_GLOBAL` (RAN: it fails with
`undefined symbol` otherwise, both wheels). That makes all of the module's
11,328 exported symbols global for the rest of the process — and they include
its own copy of FreeType (153 `FT_*` functions), `glad_*`, `stbi_*`, `stbtt_*`
and NanoVG. ELF binds a later library's undefined symbols to global ones
first. RAN (`tools/ci/interposition_demo.sh`, both wheels): a four-line
library linked against the system's FreeType 2.12.1 reports **2.12.1** when
loaded normally and **2.13.3**, the wheel's, when loaded after the module was
made global. An application that imports Pillow or matplotlib after imtypeset
would have their FreeType calls divided between two FreeTypes.

So Linux stays on `link`: more libraries to build (one per CPython tag), and
nothing done to the process. The lookup build is kept on Linux because it
exercises, on a machine we have, the audit and the load order that macOS
depends on.

### macOS (RECALLED; never run)

- Apple's `ld` links a dylib only against dylibs. A CPython extension module
  is expected to be a loadable bundle (`MH_BUNDLE`), which it refuses as a
  link input. The probe prints `file` and `otool -hv` for the module.
- So the link line is `-dynamiclib -Wl,-undefined,dynamic_lookup`, with
  `-Wl,-exported_symbols_list,python/host/exports_bundle.txt` in place of the
  GNU version script and `@rpath/libimtypeset_bundle.dylib` as the install
  name. Under two-level namespaces a symbol marked for dynamic lookup is
  searched for in the images already loaded when the library is.
- Whether that search sees a module Python loaded is the open point. On macOS
  `dlopen` without `RTLD_LOCAL` is global by default, which would make a plain
  `import imgui_bundle` enough; the probe prints `sys.getdlopenflags()`, tests
  visibility directly, and tries the load with and without reopening the
  module `RTLD_GLOBAL`. macOS binds other libraries' symbols to the library
  they were linked against, so the interposition measured on Linux is not
  expected there even if the reopening turns out to be needed; that too is
  RECALLED.
- The exported names are the same Itanium manglings with one more leading
  underscore, except where a signature carries a standard-library type:
  the fork's two text-input functions take a `std::function`, which libc++
  mangles differently from libstdc++.
- `MACOSX_DEPLOYMENT_TARGET` defaults to 14.0, the wheels' tag.
- The file is `libimtypeset_bundle.dylib` on macOS and
  `libimtypeset_bundle.so` on Linux. `<out>/LIBRARY` holds the path, for a
  caller that should not have to know.

Sections 9 and 10 say what is written, and never run, for macOS in the
package itself: the duplicate-module check through dyld's image list, a
content comparison that a re-signed copy passes, and what is known of code
signing. Not looked at here: a universal binary, and Retina (task `hidpi`).

## 3. Which headers

The wheel's Dear ImGui is a fork (`github.com/pthom/imgui`) and a wheel ships
no headers. `python/host/bundle_pins.toml` records, per wheel version, the
commit of the fork the bundle's release tag pins, the tag's own commit, the
sha256 of the bundle's ImGui configuration header at that commit, and
upstream's docking tag and commit for the same Dear ImGui version. The file
says how each entry is derived, and `tools/ci/test_fetch_headers.py` derives
every entry again where the two clones are present (RAN here: they agree).
That a wheel on PyPI was built from its tag is an inference; nothing in a
wheel records a commit.

`python/host/fetch_headers.py` fetches for one wheel version:

| what | from | checked by |
|---|---|---|
| every top-level `*.h` and `LICENSE.txt` of the fork | `git fetch --depth 1 https://github.com/pthom/imgui.git <commit>` | the commit id: what arrives must be that commit, and git names every object by the hash of its content |
| `imgui_bundle_cmake/imgui_bundle_config.h` | `https://raw.githubusercontent.com/pthom/imgui_bundle/<tag's commit>/...`, or the same file from a local clone's git objects | the recorded sha256, either way |

A build includes three of those files: `imgui.h`, the `imconfig.h` it
includes, and the configuration header (through `IMGUI_USER_CONFIG`; it is
what makes `IM_ASSERT` throw and `ImDrawIdx` 32 bits). The rest of the fork's
top-level headers are taken because whether its `imgui.h` includes a sibling
has not been read. The other settings of the bundle's Python build are compiler
definitions, in the build script (`docs/bindings-notes.md`, section 3).

- A wheel version the pins do not know is refused with instructions (exit 2).
- A fetch that fails, or a file that does not match, ends the build. **The
  script never substitutes stand-in headers.**
- `IMTYPESET_BUNDLE_HEADERS=stand-in` asks for upstream's docking tag by name,
  fetched and checked the same way. The library then says `stand-in:...`, which
  `imtypeset.host_info()["bundle"]["headers"]` shows; Dear ImGui's own check
  refuses it; `IMTYPESET_BUNDLE_STRICT=1` makes the package refuse it too.
- Results are kept under `build/bundle-headers` (`IMTYPESET_HEADER_CACHE`),
  and a kept copy is used again **only when its files hash to what the
  tracked pins record**: the configuration header by `config_sha256`, the
  headers by `standin_headers_sha256` (stand-in) or `imgui_h_sha256` and
  `imgui_headers_sha256` (the fork). The cache's own `SOURCE.json` says where
  the files came from and vouches for nothing. (RAN, before the change: a
  cached `imgui.h` edited together with its line in `SOURCE.json` was used.
  After: refused, and so are a file added to or taken out of the cache.)
- **The fork's two checksums are not recorded yet**, because no machine here
  has held the fork. Until they are: the fork's headers are checked by commit
  id alone, are fetched again for every build, and a release build is
  refused (`--require-recorded`, which `build_wheel.py --release` sets). The
  first fetch of the fork prints both values as the two lines to paste into
  `bundle_pins.toml` and writes them on the job's summary page; a person
  pastes them. From then on they are checked on every fetch. The stand-in
  checksums are recorded (computed here from `../imgui` at the pinned
  commits).

RAN (17 tests, `tools/ci/test_fetch_headers.py`): the fetch, the checks, the
cache held to the pins, each refusal, against a git repository and a `file://` URL made for the
test; and stand-in headers for both wheels from the local `ocornut/imgui`
clone. **Never run: any fetch from github.com**, so that GitHub serves these
two commits by id is unconfirmed (it is what `git submodule update` relies on
for the same commits, which is an inference). **Never compiled: the library
against the fork's headers.** The demo window calls two functions whose
signature the fork changes; whether that compiles as written is not known.

## 4. Building one

```sh
# the wheel's own headers (needs the network the first time)
python/host/build_bundle_library.sh "" /path/to/python

# stand-in headers, by name; here, from the local clone ../imgui
IMTYPESET_BUNDLE_HEADERS=stand-in python/host/build_bundle_library.sh ../imgui_bundle /path/to/python
```

The first argument is an optional local clone of Dear ImGui Bundle, used in
place of the network for what it holds. The script's header lists every
variable. It ends by running the import gate and prints the line to use the
library with.

**A change for whoever calls the script today**: without
`IMTYPESET_BUNDLE_HEADERS=stand-in` it no longer falls back to stand-in
headers, so on a machine with no network it stops at the fetch and says so.

Its last step runs the full probe in a child interpreter and writes
`<library>.attestation.json` (section 9). **Copy that file with the library**
if the library is moved: with it, `import imtypeset` checks content and starts
nothing; without it, a library named by `IMTYPESET_LIBRARY` is loaded and
probed in a child interpreter at every import, before the importing process
loads it. The script no longer writes
`<library>.gate`, and nothing reads one.

## 5. The checks, and where each runs

| script | what | here | on GitHub |
|---|---|---|---|
| `tools/ci/sidecar_probe.py` | the whole question for one interpreter: machine, wheel, symbols, build, audit, load, gate, checks; a report whatever happens | RAN, both environments | `sidecar-probe.yml`, every job |
| `tools/ci/sidecar_symbols.py` | what the module exports against what the library needs; the audit after linking | RAN (ELF) | same |
| `tools/ci/test_fetch_headers.py` | the header fetch against local sources | RAN, 17 tests | `ci.yml`, job `tools/ci` |
| `tools/ci/check_workflows.py` | the workflow files are YAML and keep this repository's rules (read-only permissions, every action named by a commit id with its version beside it, no `${{ }}` inside a `run`, a timeout per job, no logic outside `tools/ci`) | RAN with PyYAML 6 | same |
| `tools/ci/cpp_preset.sh` | one CMake preset: configure, build, ctest | RAN for `debug` on a copy of commit `caaeab3`, GCC | `ci.yml`: six presets by three toolchains |
| `tools/ci/sidecar_install.sh`, `install_deps.sh` | pip and the system's package manager | not runnable here (no network); syntax and shellcheck only | both workflows |
| `tools/ci/test_wheel.py` | the version line and its ordering, the content comparison (ELF; Mach-O on a file made by hand), which route a package directory is, which gate runs where, every refusal before a library is loaded | RAN, 24 tests | `ci.yml`, job `tools/ci` |
| `tools/wheel/build_wheel.py`, `check_wheel.py`, `check_installed.py` | one wheel built, its archive checked, the installation checked (section 12) | RAN, both environments, stand-in headers | `wheels.yml` through `tools/ci/wheel_build.sh` |
| `tools/ci/wheel_build.sh`, `wheel_release.sh` | the same from PyPI's wheel in new environments; the release | syntax and shellcheck only (no index, no GitHub) | `wheels.yml` |

What `check_workflows.py` cannot show is that GitHub accepts the files:
expression syntax, contexts, runner labels and the actions' commit ids are
checked by the first run there. The four commit ids were READ on 2026-10-12
from `api.github.com` (`repos/actions/<name>/git/ref/tags/<tag>` and
`repos/actions/<name>/commits/<tag>`, which agreed for each) through a
summarising fetch, not by git; an id that is wrong stops its job at once with
"unable to resolve action", which is loud.

`ci.yml` has never run, and **the library has never been compiled by Clang, on
any system, nor on arm64**. Expect red on the first run, for reasons that are
findings and not noise: warnings GCC does not give (the build is `-Werror`),
the runners' FreeType being newer than the 2.12.1 the glyph goldens were dumped
under, and Apple's C library answering a transcendental function one unit in
the last place away from glibc's where a golden holds a double exactly. None of
the flags is weakened in advance.

## 6. Reading a run of `sidecar-probe`

Each job's summary page is that job's report, verdict first:

- **H1 YES** — a library was accepted by the import gate, `import imtypeset`
  succeeded and the headless checks passed. The line says which headers, which
  binding, and what had to be loaded first.
- **H1 NO LIBRARY WAS ACCEPTED** — with how far each attempt got. The sections
  under it have the compiler's, the linker's and the loader's own words, and
  the symbol sections list by name what the module does not export.
- **H1 NOT TESTED** — `imgui_bundle` could not be imported; expected only for
  the Intel job.
- **H2 YES** — the wheel's own headers were fetched and Dear ImGui's own
  `DebugCheckVersionAndDataLayout` accepts the library.

The job is green only for "H1 YES and H2 YES". When the wheel's own headers do
not get as far as an accepted library, the probe goes on to stand-in headers,
so that a problem with headers does not hide the answer about binding: a job
can be red and still say "H1 YES ... built against stand-in headers".

Which line answers which question:

| question | where |
|---|---|
| are the symbols exported on this platform | "Symbols (... compiled here)" in the verdict; `objects-*-missing.txt` in the artifact |
| what kind of file is the module, what does it link | section 2 of the report |
| is the module visible to a later library after a plain import | section 2, and the verdict |
| did the link work, what did the linker say | the `link` and `audit` entries |
| what did the loader say | the three `gate` entries of a lookup-bound library |
| does Dear ImGui's own check pass | `imgui_check` in the gate's output |

## 7. If macOS does not work

Not built, and not chosen: which applies depends on what the run shows, and
two of them move the border between the projects, which is the user's to move.

| what the run shows | what would work | what it costs |
|---|---|---|
| the symbols are exported; the load fails until the module is reopened `RTLD_GLOBAL` | the package reopens it before loading the library | a few lines in `python/imtypeset`. Whether it disturbs other libraries on macOS as it does on Linux would have to be measured there |
| the symbols are exported; lookup at load does not find them however the module is opened | the library stops relying on the loader: it asks the module's handle for each function by name once (`dlsym`), and calls through a table | every Dear ImGui call in the library goes through a generated layer, 117 entries today, and members are called through plain function pointers. Weeks, not days; no change for the application |
| some symbols are not exported (a few) | the same table for the ones that are, and the library's own code for the rest where they are small and stateless; otherwise the next row | depends on which. The report names them |
| Dear ImGui's symbols are not exported at all | **no sidecar is possible beside an unmodified wheel.** What remains: | |
| | (a) a private `imgui-bundle` wheel with imtypeset compiled in. This is "Level 2", which the user set aside | a full bundle build per platform, CPython and release; the application ships a wheel that is not PyPI's |
| | (b) a library with its own Dear ImGui. It cannot share the wheel's context, atlas or draw lists, so it can only produce vertices for the application to hand to the wheel through its Python API | a second font atlas whose texture must reach the wheel's renderer (not attempted); selection, hover and clipboard fed from Python; the demo window lost. MEASURED on Linux by task `bundle-route` for the hand-over alone: 57 to 65 microseconds a paragraph with numpy against 19 to 34 drawn directly |
| | (c) asking the bundle's author to export Dear ImGui's symbols on macOS, or to take imtypeset in (`bindings/imgui_bundle/` is a sketch of that) | not ours to schedule |
| the wheel's own headers do not compile or are refused by Dear ImGui's check, and stand-in headers work | ship on stand-in headers and the field probe, as Linux does today | the caveat `docs/distribution.md` already states, on one more platform |

There is no macOS x86-64 wheel for either pinned version. A Mac install on
this pin is Apple Silicon, macOS 14 or later, unless the application builds
the bundle itself.

## 8. The distribution: a wheel with the library inside

Everything in this section and the next was built by task `py-dist`
(2026-10-11). What ran is in section 12; here is what was decided, and why.
The decisions are an agent's.

**The shape is a prebuilt wheel, one per platform, CPython version and
imgui-bundle version.** `pip install imtypeset` then gives `import imtypeset`
with no environment variable, no compiler and no network beyond the download.
The other shape, a source distribution compiled on install, needs a C++
compiler and a fetch of the wheel's Dear ImGui headers on the user's machine;
a product cannot ask its user for either. It exists for developers only:
`pip wheel .` and `pip install git+https://github.com/WalterCReel3/imtypeset@<tag>`
work through `pyproject.toml`'s backend (`tools/wheel/imtypeset_backend.py`),
which builds the same wheel for the environment's imgui-bundle. There is no
sdist.

**What a wheel depends on, and how each is encoded:**

| the library depends on | because | encoded as |
|---|---|---|
| the platform and architecture | it is machine code | the platform tag, **measured from the library that was built and never asserted** (below). macOS: `macosx_14_0_arm64`, never built |
| one imgui-bundle wheel's binary | its Dear ImGui symbols and struct layouts are that module's | `Requires-Dist: imgui-bundle==X`, exactly; and the version line below |
| the CPython version | the attestation is of that CPython's `_imgui_bundle` module, and on Linux the library names that module's file, which carries the CPython tag | the Python tag `cp311`, and `Requires-Python: ==3.11.*` |
| **not** the CPython ABI | it is loaded with `ctypes` and calls no CPython function | the ABI tag `none` |

So a file is `imtypeset-0.3.0.192900-cp311-none-manylinux_2_28_x86_64.whl`.

**The Linux platform tag is a measurement** (`build_wheel.measured_linux_tag`,
with `objdump`). `manylinux_2_<n>_x86_64` promises that the file needs no
glibc newer than 2.`n` and no library but a short list every system has. So:
`n` is the newest `GLIBC_2.x` among the library's versioned symbols, and at
least 28 (the imgui-bundle wheel's own floor); the tag is plain
`linux_x86_64` instead when the library needs more of libstdc++ or libgcc
than manylinux_2_28 allows, or names a needed library other than the system's
and the wheel's own module (which the pin supplies). `check_wheel.py`
measures the library in the archive again and refuses a tag that claims more;
a tag given by hand is held to the same.

- MEASURED here (2026-10-12, the library built by GCC 12.2 on Debian 12,
  glibc 2.36, `objdump -T` and `objdump -p`): `GLIBC_2.14` (one symbol; the
  rest are `GLIBC_2.2.5`), `GLIBCXX_3.4.21`, `CXXABI_1.3.9`, `GCC_7.0.0`;
  needed libraries `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`,
  `libc.so.6` and the wheel's module. So `manylinux_2_28_x86_64`.
- RECALLED, not read: the limits compared against (`GLIBCXX_3.4.24`,
  `CXXABI_1.3.11`, `GCC_8`) and the list of system libraries, from the
  manylinux policy. auditwheel was not run: it would refuse the wheel's
  module as a library from outside.
- NOT KNOWN: what the measurement gives on a GitHub runner. A library
  compiled against a newer glibc can pick up newer symbol versions (2.38
  redirects the `strtol` family, for one). On `ubuntu-latest` the tag might
  come out `manylinux_2_38` or higher, which Debian 12 (2.36) cannot install.
  The `wheels` workflow therefore builds Linux wheels on `ubuntu-22.04`
  (glibc 2.35) and prints each wheel's measured tag and its basis on the
  job's summary page. If the tag there is above what the application's users
  have, the remedy is to build inside the `manylinux_2_28` container image,
  which is not written: `actions/setup-python` does not serve that image, so
  the job would take its Python from `/opt/python` and its system libraries
  from `dnf`. That the label `ubuntu-22.04` still exists is RECALLED.

**Two imgui-bundle versions coexist as two version lines of one distribution
name.** The distribution's version is the library's version with the
imgui-bundle version as a fourth number: `0.3.0.192900` for 1.92.900,
`0.3.0.192801` for 1.92.801 (`major*100000 + minor*1000 + patch`, which orders
as the versions do). Why this and not the alternatives:

- *Wheels of one version differing only in their pin* cannot work: a resolver
  picks one wheel per version by tag and reads its requirements afterwards; it
  backtracks over versions, never over files of a version.
- *A local version segment* (`0.3.0+bundle1.92.900`) resolves correctly, but
  an index like PyPI refuses local versions, so it would have to be redone the
  day an index is wanted.
- *Separate distribution names* (`imtypeset-bundle192900`) work, and would
  make an application's requirement name the imgui-bundle twice.

**What an application writes: both pins, exactly.**

```
imgui-bundle==1.92.900
imtypeset==0.3.0.192900
```

The second line is the first one again (192900 is 1.92.900), and both change
together. Why exactly, and not a range:

- `imtypeset==0.3.0` matches **nothing**: under PEP 440 it means
  `0.3.0.0`. (RAN, `packaging` 26.3.)
- `imtypeset==0.3.0.*` matches every line of 0.3.0. With `imgui-bundle==X`
  beside it a resolver settles on the line built for X: RAN (pip 26.2.1,
  `--no-index --find-links` over a directory holding both lines, in an
  environment that has 1.92.801): pip considers `0.3.0.192900` first, finds
  its pin unsatisfiable, and settles on `0.3.0.192801`. It works, and it
  depends on the other pin being there.
- `pip install -U imtypeset` alone, or `imtypeset==0.3.0.*` with imgui-bundle
  unpinned, takes the **newest line** and, because that wheel requires its own
  imgui-bundle exactly, **replaces the application's imgui-bundle with the one
  that line was built for**. The pair that results is consistent and the gate
  accepts it; what changed under the application is its Dear ImGui. This is
  pip's ordinary behaviour, RECALLED and not run here (there is no index, and
  one environment cannot hold two imgui-bundles to choose between).
- An application that updates itself therefore ships a lock: the two exact
  pins above, moved together, in one step (section 11).

**A wheel built against stand-in headers says so four times**, and is not a
release: its version is `0.3.0.192900.dev0+standin`, its metadata's summary
and description say it, its attestation has `"stand_in": true`, and
`host_info()["bundle"]["stand_in"]` is `True` with `"headers": "stand-in:..."`.
`build_wheel.py --release`, `check_wheel.py --release` and
`tools/ci/wheel_release.sh` each refuse one. Both wheels built here are
stand-in wheels: this machine cannot fetch the fork's headers.

`.dev0` is what keeps a stand-in wheel **below** the release of its line.
RAN (`packaging` 26.3, in `tools/ci/test_wheel.py`): `0.3.0.192900+standin`,
which these wheels were called until 2026-10-12, orders *above*
`0.3.0.192900`, so a directory holding both would have given pip the stand-in;
`0.3.0.192900.dev0+standin` orders below it, is not matched by
`==0.3.0.192900`, and is left out by `==0.3.0.*` while any release matches.
RAN (pip 26.2.1, `--no-index --find-links`, `--dry-run` for the pairs): the
old `+standin` wheel beside a wheel named as the release of its line (made
for the test; it holds no library): pip chose `+standin`. The `.dev0+standin`
wheel beside the same: pip chose the release, with `--pre` too. With only
stand-in wheels in the directory pip 26.2.1 installs one unasked. An older
resolver need not: `packaging` 21.3 (inside Debian 12's pip 23.0.1) does not
fall back to a development release for a range like `==0.3.0.*` (RAN on the
specifier alone, not through that pip), so installing a stand-in wheel by
name is `pip install --pre`, which is what `tools/ci/wheel_build.sh` does. Still true, and worth knowing: a stand-in
of a *newer* line or library version orders above an older release; the exact
pins above are what keep it out, and `--pre` is what would let it in.

`imtypeset.version()` is the library's version (`0.3.0`).
`imtypeset.host_info()["distribution_version"]` is the distribution's
(`0.3.0.192900`), `None` for a development build.

**What is in a wheel**, and nothing else (`check_wheel.py` holds every wheel
to this list): `imtypeset/__init__.py`, `_bundle.py`, `_capi.py`, `demo.py`;
`imtypeset/libimtypeset_bundle.so` (`.dylib` on macOS), 1.2 MB with its
debugging sections taken off; `imtypeset/_attestation.json`; and in
`imtypeset-<version>.dist-info`: `METADATA`, `WHEEL`, `RECORD`,
`licenses/THIRD-PARTY.md`, `licenses/NOTICE.txt`. No font, no Dear ImGui, no
FreeType. `NOTICE.txt` says what is compiled into the library (imtypeset; the
routines that follow fontTools; the Unicode range tables; the inline functions
of the Dear ImGui headers it was compiled against) and what is not. **No
licence has been chosen for imtypeset** (`docs/decisions.md`, D24), and the
metadata says so; a wheel should not be given to anyone outside until that is
decided.

**How the package finds its library** (`_capi.library_route`). The route is
decided by what is in the package's own directory, and by nothing else:

1. **Installed**: the directory holds `_attestation.json` or a library, as a
   wheel puts them. The library is `libimtypeset_bundle.so` (`.dylib`) in
   that directory. No metadata, no entry point, no search path, and **no
   environment variable**: `IMTYPESET_LIBRARY` is not read (section 9).
2. **Development**: the directory holds neither, which is a source checkout.
   `IMTYPESET_LIBRARY` names the library. `IMTYPESET_PYTHON` is advtext's
   variable for where the package directory is; the package does not read it.

`host_info()["route"]` is `"installed"`, `"IMTYPESET_LIBRARY"`, or
`"IMTYPESET_LIBRARY (build-info check only)"` for a library that is not a
sidecar (section 9). An application that wants to know whether an installed
imtypeset is present without importing it can look for `_attestation.json`
beside `importlib.util.find_spec("imtypeset").origin`; that survives a freezer
that drops `dist-info` directories, which the metadata does not.

**The metadata** is version 2.4, the version that has `License-File`; the two
files it names are the notices, in `dist-info/licenses/`. It names **no
licence** (no `License-Expression`, no `License`, no classifier), because none
has been chosen, and its description says so in words. RECALLED: that an index
refuses `License-File` under an older metadata version (PEP 639). RAN: pip
26.2.1 installs the wheel.

Building one, as a developer:

```sh
python3 -P -B tools/wheel/build_wheel.py --python /path/to/python --out wheels
python3 -I -B tools/wheel/check_wheel.py wheels/*.whl
/path/to/an/environment/python -P -B tools/wheel/check_installed.py   # after pip install
```

## 9. The import gate

`import imtypeset` either succeeds with a library it has reason to believe
fits the Dear ImGui in the process, or raises `ImportError`. An installed
distribution starts no child interpreter, writes nothing, keys nothing on a
path or a date, **and reads no environment variable**. The development route
at the end of this section is the only place a variable or a child exists.

**An environment variable never chooses what a shipped application loads**
(an agent decision of 2026-10-12, after review-8 F1; `docs/decisions.md`).
Which route a package is on is decided by its own directory (section 8): if
it holds `_attestation.json` or a library, it is installed, whatever
`sys.frozen` says and whatever the environment holds.

| variable | installed distribution | source checkout (development) |
|---|---|---|
| `IMTYPESET_LIBRARY` | not read | names the library |
| `IMTYPESET_GATE` | not read: the attestation, or a refusal | `attested` forbids the probe; `probe` asks for it over an attestation |
| `IMTYPESET_PRELOAD_BUNDLE` | not read: on Linux the module is never reopened `RTLD_GLOBAL` | `1` reopens it (section 2) |
| `IMTYPESET_PYTHON` | not read (it is advtext's) | not read |
| `IMTYPESET_BUNDLE_STRICT` | read: `1` refuses a stand-in build. It can only refuse | the same |
| `IMTYPESET_GATE_FONT` | not read: the installed gate reads no font | names the probe's font |

`host_info()["ignored_environment"]` lists the first four that were set and
not read; it is empty in a checkout. RAN (review-8 F1, reproduced on the
installed wheel as it was: `IMTYPESET_LIBRARY` naming a sidecar with no
attestation was loaded and probed; naming a truncated one **killed the
process with SIGBUS**, frozen shape included; naming the self-hosted library
or a development build with its attestation was accepted with `sys.frozen`
set; `IMTYPESET_GATE=probe` started a child from an installed package and,
with `sys.executable` an application and no `sys.frozen`, **tried to start
the application again**; `IMTYPESET_PRELOAD_BUNDLE=1` made the module global).
After the change, each of those in each of three shapes (plain, `sys.frozen`,
and an application that sets no flag): the import is the plain one, the named
file is never mapped, nothing is started, and `host_info()` names the
variable (`check_installed.py`, section 12).

**Why there was a child, and where it went.** The field probe reads the
wheel's live objects at the offsets the library was compiled with. At a wrong
offset that is a read through a wild pointer, and the process dies (MEASURED
by task `mac-ci`: 12 of 87 offset mutants end in a crash). So the probe must
run where a crash is an answer. It still does, but **when the wheel is built**:
`build_wheel.py` installs nothing, stages the package as it will be installed,
and runs the whole probe in a child of the interpreter whose imgui-bundle the
wheel is for. If, and only if, the probe accepts, it writes
`imtypeset/_attestation.json`:

- the sha256 of the library and of the wheel's `_imgui_bundle` module, and for
  each a second sha256 of only what the loader maps of the file (below);
- the imgui-bundle and Dear ImGui versions, the headers, how the pair was
  accepted, and every probe row;
- the platform, the CPython version, the distribution's version, and where and
  when the probe ran.

**What `import imtypeset` then does** (`_bundle.precheck`, then `_bundle.gate`):

1. *Before anything is loaded:* the attestation is read and held to its shape
   (anything else is `damaged`); the platform is compared; the library file is
   hashed and compared with it. Then the wheel's module file is found
   **without importing `imgui_bundle`** (from the module if the application
   has imported it already, else from where the import system says the
   package is), hashed and compared. Any difference is a refusal: a library
   that is not the attested one is never mapped, and an imgui-bundle that is
   not the attested one is not imported by this package. (RAN: a library cut
   to half its length is refused here. With that comparison left out it is
   loaded and the process dies with SIGBUS; that is what was found when an
   earlier draft of this gate accepted a file on its linker build-id, which a
   truncated file keeps.) Where an importer does not say where the module
   file is, `imgui_bundle` is imported first and
   `host_info()["bundle"]["attestation"]["module_found"]` says "by importing
   imgui_bundle"; never seen here.
2. `imgui_bundle` is imported, the module it brought is held to the file that
   was hashed, and the library is loaded (section 2).
3. *In the process, reading nothing through an offset:* that no second Dear
   ImGui is in reach (below); the library's own account of its headers; Dear
   ImGui's `DebugCheckVersionAndDataLayout` (a comparison of a version string
   and six sizes inside the wheel; its assertion is an exception the library
   catches); the nine enumerators the library passes by value against the
   wheel's binding. Each must answer what the attestation records.
4. The package's own checks of the library against itself (struct sizes, the
   C ABI revision, every function it declares).

Whatever else goes wrong in those steps, an exception of any kind, leaves
`import imtypeset` as an `ImportError` with kind `unchecked` and the
exception's name on its second line.

No Dear ImGui context is created, no font is read, no process is started and
nothing is written. MEASURED (CPython 3.11.2, files in the page cache, load
average 3.4): `import imtypeset` after `import imgui_bundle` takes 30 to 34
ms, the hashing of 28 MB and 1.2 MB included. The probe in a child, where it
still runs, adds about 200 ms to an import.

**What this trusts, and why that is sound.**

- *That the probe's verdict about two files holds wherever those two files
  are.* The probe compares offsets compiled into the library with what the
  module's code does to its own objects. Both are fixed by the bytes that are
  mapped; neither depends on the machine, the path or the date. The font the
  probe adds and the context it creates are inputs chosen by the probe, not
  properties of the machine.
- *sha256*, to say that two files are the same bytes.
- *The second hash, for a file a freezer rewrote.* `strip`, `install_name_tool`
  and code signing change a file's sha256 and leave alone what the loader
  maps. So each file also has a sha256 over exactly that. A file that matches
  neither hash is refused. What goes into it, and what does not:

  | | hashed | not hashed |
  |---|---|---|
  | ELF | of the ELF header: type, machine, entry point, flags, and where and how many the program headers are; the whole program header table; every `PT_LOAD` segment's bytes with its address and length (those bytes include `.dynamic`, the dynamic symbols and strings, so the needed libraries and the run path) | the rest of the 64-byte ELF header, which says where the section headers are (`strip` moves them); the section headers; the sections no segment maps (debugging information, the static symbol table) |
  | Mach-O | every section's bytes with its segment and section name, address, length and flags; each segment's name, address, size in memory and protection, `__LINKEDIT` excepted | the header (its count of load commands changes when a signature is added); **every load command that is not a segment**: `LC_ID_DYLIB`, `LC_LOAD_DYLIB`, `LC_RPATH`, `LC_CODE_SIGNATURE`, the symbol-table and fixup commands, `LC_UUID`, `LC_BUILD_VERSION`; and all of `__LINKEDIT` (symbols, the exports trie, the binding and fixup tables, the signature) |

  **And the file must be as long as its own headers say** (`_elf_loaded`,
  `_macho_loaded`): on ELF every program header's bytes, the section header
  table and every section with bytes in the file; on Mach-O every segment's
  bytes, `__LINKEDIT`'s included, and the tables `LC_SYMTAB` and the
  linkedit-data commands place. A file cut anywhere, its tail included, has
  no second hash and is `damaged`. RAN before this was added (review-8 F2):
  a hand-made Mach-O cut 1, 150 or 300 bytes into `__LINKEDIT` kept its
  second hash, and so did this build's ELF library cut 1 to 7,432 bytes from
  its tail (an ELF file cut there still loads; it is refused because it is
  not the file that was installed). RAN after: all refused, and an installed
  library cut 1, 64 or 4,096 bytes short is `damaged`.

  *Why the Mach-O load commands are not hashed* (review-8 F3 asked). As
  understood, RECALLED: re-signing adds or rewrites `LC_CODE_SIGNATURE` and
  changes `__LINKEDIT`'s size and the header's command count; and an
  application freezer runs `install_name_tool` on the libraries it collects,
  which rewrites `LC_ID_DYLIB`, `LC_LOAD_DYLIB` and `LC_RPATH`. Hashing those
  commands would refuse exactly the re-signed, frozen application this second
  hash exists for. So a Mach-O file with a tampered `__TEXT` cannot pass, and
  one with an **added `LC_LOAD_DYLIB` or a changed run path can**: that is
  the price, and it is a tampering case, which this gate does not claim to
  stop (next item). On ELF the same information is inside a hashed segment.
  What was added on 2026-10-12 is the part of the load commands that
  describes the mapping of the hashed bytes (addresses, sizes, protections,
  section flags), which neither tool is known to touch.

  RAN on ELF: copies rewritten by `strip --strip-debug` and `strip
  --strip-all` are accepted by the second hash (a library compiled for the
  test, and the installed sidecar); one bit changed in the entry point, the
  flags, or a program header is refused; a file with bytes added after its
  end is accepted. **The Mach-O reader is written from the format as
  remembered and has read only files made by hand for its tests.** Two facts
  about Apple's tools decide whether a signed application is accepted and
  cannot be settled here: that re-signing and `install_name_tool` leave
  section bytes, section and segment addresses, protections and flags
  untouched; and that they keep every table inside the length `__LINKEDIT`
  states. If either is wrong, the failure is a refusal (`damaged`) in a
  re-signed application, not an acceptance; the refusal's second line prints
  both hashes for the report section 10 asks for.
- *That the attestation was written by a real probe.* Nothing signs it. Anyone
  who can write in the package can write an attestation, and can as well
  replace the library; the gate is a guard against a mismatched or damaged
  install, not against an attacker. `check_wheel.py` verifies, for a wheel
  file, that its library is the attested one.
- *That the file hashed is the file mapped.* True unless a file is replaced
  between the hash and the load, or was replaced after the process mapped the
  wheel's module (section 11, "under a running process").

**The development route** (a source checkout: `python/imtypeset` with no
library in it, and `IMTYPESET_LIBRARY`). The full probe remains here:
`tools/bundle_route/run_gate.py` prints its report, `mutate_gate.py` moves
each offset to see that it notices, `attest.py` writes an attestation, and the
build script's last step runs all that. **Nothing is mapped into the importing
process before a content check or a child's verdict:**

- A named library with an attestation beside it
  (`<library>.attestation.json`) is held to it by content before it is
  loaded, exactly as an installed one is. `checked_in` is `"attestation"`.
- A named library with none is **loaded and probed in a child interpreter
  first** (`_bundle.probe_in_child`), every time; only if the child accepts
  it does the importing process load it. `checked_in` is `"child"`. A
  truncated file ends the child (RAN: the importing process gets an
  `ImportError`, kind `not-a-library`, and lives; before, it died).
- **Where no child can be started, that import is refused, kind
  `unchecked`.** Nothing is run to find out, because starting a
  `sys.executable` that is an application starts the application again
  (RAN, above). `_bundle.child_interpreter` decides from what can be read:
  `sys.frozen`; Nuitka's `__compiled__` on the module; that `sys.executable`
  is a file **named like a Python interpreter** (`python`, `python3.11`,
  `pythonw.exe`, `pypy3`), which is what is left for a packager that sets no
  flag. RECALLED: Briefcase and Nuitka do not set `sys.frozen`; Briefcase
  starts the application from a binary named for it. An interpreter under
  another name is refused too; its way out is an attestation, not a
  variable.
- **A library that is not a sidecar** (it does not export
  `ImTypesetBundle_LayoutFacts`): the self-hosted `libimtypeset_pyhost.so`,
  which has Dear ImGui inside it, or a library with none at all. It is
  accepted **only on this route**, after a child interpreter has loaded it,
  and by its **build-info check** alone: `ImTypeset_GetBuildInfo` answers a
  struct of the size and C ABI revision this package was written for; for a
  self-hosted library `ImTypesetHost_GetInfo` says the Dear ImGui inside it
  is the version imtypeset was compiled against; and the library accepts the
  package's struct sizes (`ImTypeset_DebugCheck*DataLayout`). There is no
  second Dear ImGui to compare with, so there is nothing to probe.
  `host_info()["route"]` is then `"IMTYPESET_LIBRARY (build-info check
  only)"` and `host_info()["checked_by"]` says the same in words. Under an
  attestation such a library is refused (`not-a-library`).

**Refusals.** Every refusal is a plain `ImportError` (not a subclass: a
refused import leaves no module of the package behind to hold a class). Its
first line is a sentence an application can show: it names versions and what
to do, and holds no path. The lines after it are for a log. `e.kind`,
`e.sentence` and `e.detail` are set.

| `kind` | when | the sentence (0.3.0, examples from the runs of section 12) |
|---|---|---|
| `bundle-mismatch` | the wheel's module is not the attested one | "imtypeset 0.3.0 was built for imgui-bundle 1.92.900, and this application has imgui-bundle 1.92.801. The two are updated together: install imgui-bundle 1.92.900, or the imtypeset built for imgui-bundle 1.92.801." (same version, other binary: "…built for another build of imgui-bundle 1.92.900 than the one in this application…") |
| `bundle-missing` | `import imgui_bundle` fails | "imtypeset needs imgui-bundle 1.92.900, and it cannot be imported here. Install it." |
| `damaged` | the library is not the attested file, is cut short anywhere, or is missing; or the attestation cannot be read or is not of the shape the gate reads | "The imtypeset library is not the one imtypeset 0.3.0 was installed with: it is damaged, or an update replaced only part of it. Install imtypeset again." |
| `package-mismatch` | the Python modules and the library are of different releases | "The imtypeset library does not export 2 of the 95 functions this imtypeset package calls: … The library and the package are installed together, from one release." / "The imtypeset library refuses this package (written for …): …" |
| `layout` | Linux: `imgui_bundle` was imported from another directory than the package's neighbour | "imtypeset and imgui-bundle are installed in two different places, and imtypeset finds imgui-bundle only beside itself. Install both into the same environment." |
| `wrong-platform` | the package was built for another system | "This copy of imtypeset 0.3.0 was built for Darwin arm64, and this computer is Linux x86_64. Install the imtypeset built for this computer." |
| `unchecked` | no attestation, and no probe may run (an installed package, always; a checkout where no child interpreter can be started, or with `IMTYPESET_GATE=attested`); or the check itself ended in an error | "This copy of imtypeset carries no record that it was checked against the imgui-bundle it is running beside, and it is not checked here. Install a released imtypeset wheel." / "The imtypeset library named by IMTYPESET_LIBRARY has no record that it was checked against this imgui-bundle, and it cannot be checked here: this is not a Python interpreter that can start another. …" |
| `no-library` | a source checkout (no library and no attestation in the package), and `IMTYPESET_LIBRARY` unset | "IMTYPESET_LIBRARY is not set, and this copy of the imtypeset package carries no library: it is a source directory and not an installed wheel." |
| `not-a-library` | the file cannot be loaded or is not imtypeset's | "The imtypeset library cannot be loaded: it is damaged, or it was built for another system or another imgui-bundle. Install imtypeset again." |
| `refused` | a check ran and refused: a second copy of the module, a second Dear ImGui in the global symbol scope, an answer that differs from the attestation, a failed probe | "The imtypeset library does not fit imgui-bundle … in this application. Install the imtypeset built for this imgui-bundle." |
| `configuration` | `IMTYPESET_BUNDLE_STRICT=1` over a stand-in build; in a checkout, `IMTYPESET_GATE` with a value that is not one | "This imtypeset was built against stand-in Dear ImGui headers, and IMTYPESET_BUNDLE_STRICT=1 refuses such a build. …" |

"There is no library for this platform" (advtext's H26) has no refusal of its
own, because it cannot arise from an install: where no wheel matches the
platform, pip installs nothing, and `import imtypeset` is Python's own
`ModuleNotFoundError`. A package copied from another system is
`wrong-platform`.

**Is another Dear ImGui in reach?** (`_bundle._same_module`, after the load.)
Two checks, and what each cannot see:

1. *A second copy of the wheel's module.* The files mapped in the process
   are listed and a second `_imgui_bundle.*` is refused. On Linux from
   `/proc/self/maps`. On macOS from dyld (`_dyld_image_count`,
   `_dyld_get_image_name`, through `ctypes.CDLL(None)`): written from Apple's
   header as remembered, **never run**. On macOS a second copy is also less
   likely by construction: the library names no module file, so loading it
   cannot bring one in.
2. *Another image answering for Dear ImGui's names in the global symbol
   scope* (review-8 F10). A library's undefined symbols are bound there
   first: on ELF before its own needed libraries are looked at, and on macOS
   it is where a symbol left for lookup is found at all, the first image in
   load order that exports the name. So an executable with a Dear ImGui of
   its own linked in, or another extension opened `RTLD_GLOBAL` that carries
   one, would be what the library draws through. The check asks `dlsym` on
   the global scope for four of the names and `dladdr` for the image of each
   answer, and refuses any image but the wheel's module. RAN on Linux: a
   library exporting those names, opened `RTLD_GLOBAL` before the import, is
   refused ("a second Dear ImGui in this application"); the plain import
   finds none. The macOS half has never run.
   **Not seen:** an image that exports the names and was opened
   `RTLD_LOCAL`. On ELF the library does not bind to such an image. Whether
   macOS's lookup can is RECALLED as no (a lookup searches global images)
   and not known; and four names stand for 117.

Neither is silently skipped: `host_info()["bundle"]["second_module_check"]`
is, in full, `"done: /proc/self/maps; global Dear ImGui names: none but the
wheel's (dlsym and dladdr on the global scope)"`, with `"not done: <why>"` in
place of either half that could not be made.

## 10. What a packaged application carries

**Files the library and its gate need on disk at run time:**

| file | where it is looked for |
|---|---|
| `imtypeset/*.py` (or their compiled form), `imtypeset/libimtypeset_bundle.so` or `.dylib`, `imtypeset/_attestation.json` | one directory, the package's. The library and the attestation are data files to a freezer: it must be told to collect them (PyInstaller: `--collect-binaries imtypeset --collect-data imtypeset`; READ from its documentation's option names as remembered, not run) |
| `imgui_bundle`, as the pinned wheel installed it | wherever Python imports it from; on Linux, in the directory `imtypeset/` is in |
| **no font, for the gate** | the installed gate reads none. The full probe (wheel build, development) takes the first of `IMTYPESET_GATE_FONT`, `DroidSans.ttf` or Roboto in the wheel's `assets/fonts`, any `.ttf` there |
| the fonts the application gives `imtypeset.add_font` | wherever the application says: `add_font(face, path)` opens the path it is given, and `add_font(face, bytes)` opens nothing. The library itself looks nowhere |

So a freezer that drops `imgui_bundle/assets` no longer breaks the import.
advtext's own font paths resolve inside `imgui_bundle/assets/fonts` (the four
Roboto faces, Inconsolata Medium, Latin Modern Math), so an application built
on advtext must carry those six files or hand over others; that is advtext's
list to state, and the library does not check it.

`dist-info` directories are not needed: the package reads no metadata.

**Code signing on macOS.** Nothing here was run; there is no Mac.

- READ (general knowledge of Apple's platform, not checked against a current
  document): on Apple Silicon every executable page must be signed, so the
  linker signs what it links ad hoc; a hardened-runtime application loads only
  libraries signed by its own team or by Apple (library validation) unless it
  carries the `com.apple.security.cs.disable-library-validation` entitlement;
  notarisation requires every Mach-O in the bundle to be signed with the
  Developer ID and a secure timestamp. So the `.dylib` inside a wheel, signed
  ad hoc by the linker in CI, must be signed again with the application's
  identity when the application is packaged, exactly as `_imgui_bundle.*.so`
  and every other binary in it must. Freezers do this for the files they
  collect.
- What follows for the gate: re-signing changes both files' sha256. The second
  hash of section 9 exists for that, and is the part most in need of a run.
- Needs a person at a Mac: build or download the arm64 wheel, freeze an
  application with it, sign with a Developer ID under the hardened runtime,
  and report `imtypeset.host_info()["bundle"]` — in particular
  `attestation.matched_by` (expected `image` for both files),
  `attestation.module_found`, `preload`, and `second_module_check` — or the
  refusal's two lines, whose second names both hashes. With them, the output
  of `otool -l` on the library before and after signing settles the two
  facts section 9 could not: that signing and `install_name_tool` leave
  section bytes, addresses, protections and flags alone, and that a file cut
  inside `__LINKEDIT` is one whose load commands point past its end.

## 11. What an update is

Four things are versioned: the application, advtext, the imtypeset wheel and
the imgui-bundle wheel. Two rules cover them.

**imtypeset and imgui-bundle are one unit.** An imtypeset wheel is built for
one imgui-bundle and its metadata pins it exactly, so an installer that
honours metadata cannot leave them apart. An updater that copies files must
replace the directories `imtypeset/` and `imgui_bundle/` (and
`imgui_bundle.libs/`) in one step, and all of each: the library, the Python
modules and the attestation come from one wheel.

**advtext and the application follow imtypeset's version** by the rule of
`docs/versioning.md`. What they write is a lock: `imgui-bundle==X` and the
imtypeset line built for it, `imtypeset==0.3.0.<line of X>`, both exact and
moved together (section 8 says what `pip install -U imtypeset` alone would do
instead: move the application's imgui-bundle). A library like advtext, which
does not own the application's environment, states the range it works with
(`imtypeset==0.3.*`) and leaves the two exact pins to the application.

| what was updated | what happens at `import imtypeset` |
|---|---|
| everything, to a consistent set | accepted; nothing is probed, started or written |
| imgui-bundle only (a new wheel under an old imtypeset) | `bundle-mismatch`, naming both versions. RAN, both directions, 1.92.900 and 1.92.801, frozen shape included |
| imtypeset only, to a wheel built for another imgui-bundle | the same sentence. pip refuses to get there: the pin conflicts |
| imgui-bundle rebuilt at the same version (another binary) | `bundle-mismatch`, "another build of". Not run: there is one binary of each version here |
| the library replaced, the Python modules not (or the reverse) | `damaged` when the attestation still describes the old library (RAN: any library but the attested one); `package-mismatch` when all three files of the new wheel arrived except the modules (RAN: modules declaring a function the library lacks; modules of another version string; and, for real, a library built from `c9f8902` under this tree's package: "does not export 2 of the 95 functions") |
| an update interrupted partway through a file | `damaged`, before the file is loaded (RAN: truncated to half, cut 1, 64 and 4,096 bytes short at its tail, emptied, one byte changed, missing) |
| the attestation lost | `unchecked` (RAN), and still `unchecked` with `IMTYPESET_LIBRARY` naming a good library: a package with a library in it is an installed one |
| a variable left in the environment by a developer's shell or a launcher (`IMTYPESET_LIBRARY`, `IMTYPESET_GATE`, `IMTYPESET_PRELOAD_BUNDLE`) | nothing: an installed package does not read them, and `host_info()["ignored_environment"]` names them (RAN) |
| advtext or the application newer than imtypeset | not the gate's to see. `imtypeset.version()` and `host_info()["distribution_version"]` are what they compare; a call that does not exist is Python's `AttributeError` |

In every row that refuses, the application gets an `ImportError` whose first
line it can show, and the process goes on. advtext turns that into its
"refused" state and draws without the library.

**Under a running process.** Not testable in a unit test; RECALLED, from how
the two systems treat mapped files, and not run:

- *Linux.* An installer that writes a new file and renames it over the old
  one (pip does: it writes to a temporary name and moves) leaves the running
  process with the old file, which stays mapped until the process exits; the
  process is unaffected and sees the update when it restarts. A tool that
  overwrites the file in place changes pages the process has mapped, and the
  process can die with SIGBUS or run torn code. If `imtypeset` is not imported
  until after such an update of a running process, its files on disk are the
  new ones while the process may hold the old `_imgui_bundle` mapped: the gate
  hashes the file on disk, so it compares the new module's file, not the one
  mapped. That one case the content comparison does not cover; the
  duplicate-module check does not either. Restart after updating.
- *macOS.* The same holds for rename against overwrite, with one more
  consequence: the kernel checks signed pages as they are paged in, so a
  signed file overwritten in place kills the process ("code signature
  invalid") instead of running torn code.
- So an updater replaces by rename, never in place, and the update takes
  effect at the next start. A packaged application's updater (Sparkle and its
  like) replaces the whole bundle and relaunches, which is that.

## 12. What ran for the distribution, and what never did

**The runs below are of 2026-10-11, before the gate's routes changed.** What
was run again on 2026-10-12 on the changed tree is the table after this one;
where the two differ (wheel names, counts), the later one is current.

RAN, by the builder of task `py-dist`, 2026-10-11, on the machine of section 1
(Debian 12, GCC 12.2, CPython 3.11.2, pip 26.2.1), every process under
`ulimit -v 6000000`, load average 3 to 4.5:

| what | result |
|---|---|
| `tools/wheel/build_wheel.py --headers stand-in`, each environment | two wheels, `imtypeset-0.3.0.192900+standin-cp311-none-manylinux_2_28_x86_64.whl` and `…192801+standin…`, 580 kB each, 11 files; the probe accepted each in a child (24 rows); about a minute each |
| `tools/wheel/check_wheel.py` on both | ok; with `--release --tag v0.3.0`: both refused, "NOT A RELEASE: built against stand-in headers" |
| `pip wheel --no-deps --no-index .` (the backend of `pyproject.toml`, build isolation on) | the same wheel |
| two throwaway environments under `build/py-dist` (`tools/wheel/local_env.py`: a new venv whose site-packages links to the entries of the existing environment's, so that imgui-bundle is present without an index and nothing is installed into the existing environment); `pip install --no-index --find-links <both wheels> imtypeset imgui-bundle==X` | each environment got its own line; `pip check` clean |
| `tools/wheel/check_installed.py` in each, no `IMTYPESET_*` or `PYTHON*` variable set | 28 and 27 checks, all pass: the import; the library inside the package; accepted on the attestation by sha256; the pin; **the frozen shape** (`sys.frozen`, `sys.executable` a file that does not exist, every way of starting a process replaced by a failure, the package read-only: imported, nothing started, nothing written); `check_document_edits.py` 23 PASS and `check_swap_answers.py` 18 PASS through the installed package; and the partial-update cases of section 11 |
| `tools/ci/test_wheel.py` | 11 tests |
| a library bound by lookup on Linux through the package | refused after the import alone (`undefined symbol`), accepted with `IMTYPESET_PRELOAD_BUNDLE=1` (`preload: global`) |

RAN again, by the builder of task `gate-fixes`, 2026-10-12, same machine, on
the tree with that task's changes and task `hidpi`'s (25 probe rows), every
process under `ulimit -v 6000000`:

| what | result |
|---|---|
| review-8's findings, on the tree and the installed wheel as they were | F1: every sequence reproduced (section 9), two of them a process killed by SIGBUS and one an application started again. F2: reproduced on a hand-made Mach-O and on this build's ELF library. F3: the Mach-O half as stated; the ELF half only in part (the program headers were already inside a hashed segment; the ELF header's entry point and flags were not hashed). F4: three of the four raised `AttributeError`, the fourth `ValueError`. F5: reproduced with `packaging` 26.3 and with pip. F9: reproduced |
| both wheels, `build_wheel.py --headers stand-in` | `imtypeset-0.3.0.192900.dev0+standin-cp311-none-manylinux_2_28_x86_64.whl` and `…192801.dev0+standin…`, 588 kB, 11 files, metadata 2.4, tag measured |
| `check_wheel.py` on both | ok, the tag measured again from the archive; with `--release --tag v0.3.0`: both refused |
| two new throwaway environments (`local_env.py`), `pip install --no-index --find-links <both> imtypeset imgui-bundle==X` | each got its own line (in the 1.92.801 one pip tried `…192900.dev0` first); `pip check` clean |
| `check_installed.py` in each, with `--other-python`, `--dev-library` (a development sidecar built for that environment) and `--self-hosted-library` | **92 of 92 in each**: what section 12's first table lists; the unflagged shape; the library cut 1, 64 and 4,096 bytes short, missing, and lengthened; five attestations of another shape; 27 imports with a variable set that an installed package must not read (nine settings by three shapes); a second Dear ImGui in the global scope; and 18 development-route cases from this repository's package |
| `tools/ci/test_wheel.py` | 24 tests, under `packaging` 26.3 and under pip 23.0.1's `packaging` 21.3 |
| `tools/ci/test_fetch_headers.py`, `check_workflows.py` (PyYAML 6; and a copy with a tag in place of a commit id and an expression in a `run`: reported), `shellcheck` on `tools/ci/*.sh` and the build script | 17 tests; three files ok; clean |
| `tools/bundle_route/mutate_gate.py` (1.92.900) | 31 offsets, 90 mutants, 12 crashed, 0 survived |
| `tests/python/test_binding.py`, `test_demo_sections.py` against a self-hosted library built for this run | 101 and 4 tests pass; each import now starts one child interpreter |
| `tools/ci/sidecar_probe.py --also-lookup --only-stand-in` (1.92.900) | H1 YES by `link`; the lookup-bound library accepted only with the module global, as before |

Not run again on 2026-10-12: the C++ presets (this task changed no C++);
`pip wheel .` through `pyproject.toml`'s backend; `tools/release.sh`.

RAN a third time, by the builder of task `v030-gate`, later on 2026-10-12,
same machine, after every preset of `CMakePresets.json` had passed from clean
on the same unchanged tree (533 files hashed before and after), every process
under `ulimit -v 6000000`:

| what | result |
|---|---|
| both development sidecars from clean, `IMTYPESET_BUNDLE_HEADERS=stand-in python/host/build_bundle_library.sh` | exit 0, 37 s each, no warning line, accepted by probe, 25 rows, attestation written; 106 symbols exported (97 `ImTypeset_*`, 9 `ImTypesetBundle_*`), 117 Dear ImGui symbols left to the wheel |
| in both environments, against those: `run_gate.py`, `mutate_gate.py`, `check_document_edits.py`, `check_swap_answers.py` | accepted; 31 offsets, 90 mutants, 12 crashed, 0 survived; 23 PASS 0 FAIL; 18 PASS 0 FAIL |
| in both, on Xvfb through `tools/shot.sh`: `examples/python/bundle_host.py --frames 30`; `bundle_demo.py` eight ways; `hidpi_check.py` at the display's density (1) and with `--assume-density` 1.5, 2, 3; `tools/bundle_route/font_lifetime.py`, seven scenarios | `selection: (74, 284) layouts: 2`; `last error: 0` sixteen times; `PASS` (1.000 texels per framebuffer pixel, 0 off the grid) eight times; `application_fonts_removed` 0 fourteen times. **With `--screenshot`, `bundle_host.py` and `hidpi_check.py` exit 1 in the 1.92.801 environment after printing their line: hello_imgui's screenshot needs numpy, which that environment does not have.** Without it, exit 0 |
| both wheels, `build_wheel.py --headers stand-in` | 38 and 40 s; `…192900.dev0+standin…` and `…192801.dev0+standin…`, 589 kB, 11 files, 25 probe rows, tag `manylinux_2_28_x86_64` measured |
| `check_wheel.py`; with `--release --tag v0.3.0` | both ok; both refused |
| two new throwaway environments (`local_env.py`), `pip install --no-cache-dir --no-index --find-links <both> --pre imtypeset imgui-bundle==X`, `pip check` | each got its own line; clean; `import imtypeset` with no variable: route `installed`, checked in `attestation` |
| `check_installed.py` in each, with `--other-python`, `--dev-library`, `--self-hosted-library` | **92 PASS, 0 FAIL, 0 SKIP in each** |
| `tools/ci/test_wheel.py` (under `/usr/bin/python3` and under a throwaway environment's, `packaging` 26.3), `test_fetch_headers.py`, `check_workflows.py`, `tools/version.py --check`, `shellcheck` 0.9.0 on `tools/ci/*.sh`, `python/host/*.sh`, `tools/release.sh` | 24 tests OK twice; 17 tests OK; three files ok; agree; clean |
| `tools/ci/sidecar_probe.py --also-lookup --only-stand-in` (1.92.900) | exit 0: H1 YES by `link`; H2 not attempted |
| `python/spike_bundle/pptmstr_patch_faults.py`, twelve scenarios (1.92.801, Xvfb); `git -C ../pptmstr apply --check` of the patch | all exit 0; exit 0 at `4974657`, 750 added and 19 removed |

Not run that time either: `pip wheel .` through `pyproject.toml`'s backend;
`check_installed.py --old-library`. And everything under NEVER RUN below is
still never run.

NEVER RUN:

- **The Mach-O length check, the widened Mach-O hash, the global-scope check
  on macOS, and `child_interpreter()` inside a real Briefcase, Nuitka,
  PyInstaller or py2app application.** Each is written from the format or the
  tool as remembered.
- **The four actions' commit ids, the label `ubuntu-22.04`, and a wheel's
  measured tag on a runner**: the first run of the workflows.
- **A fetch of the fork**, so its two checksums are unrecorded and
  `--release` cannot pass until they are pasted in (section 3).
- **Anything on macOS**: the wheel build, the platform tag, the load ladder's
  second step, dyld's image list, the Mach-O content hash on a real file, a
  signed or frozen application.
- **A wheel built against the wheel's own headers**, hence `--release`
  accepting anything, and `accepted_by` being `imgui-check+probe` in an
  attestation.
- **`wheels.yml`, `tools/ci/wheel_build.sh`, `tools/ci/wheel_release.sh`**:
  no index and no GitHub. `pip install imgui-bundle==X` into a new environment
  followed by the wheel is what CI does; here imgui-bundle was present by
  links.
- **An application freezer** (PyInstaller, py2app), on any system. The frozen
  shape above is the import with a frozen interpreter's properties imitated,
  not a frozen application.
- **CPython 3.12 and 3.13**, and pip older than 26.2.1.
- **An upload to an index.** It would take: a licence; a name on the index
  (`imtypeset` is not registered by this project; nobody checked whether it is
  free); wheels that pass `check_wheel.py --release`; a publishing job with
  the index's trusted-publisher configuration, which is a setting on the
  index's side and a new permission (`id-token: write`) in one job.
