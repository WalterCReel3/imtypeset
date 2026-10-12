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
| import gate, `import imtypeset`, headless checks | RAN: accepted by the field probe (24 rows), 23 + 18 checks pass, both wheels | never run | no wheel |
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
   `IMTYPESET_PRELOAD_BUNDLE=1` asks for it, for the reason measured below.
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
- Results are kept under `build/bundle-headers` (`IMTYPESET_HEADER_CACHE`) and
  used again only while every file still has the sha256 recorded with it.

RAN (14 tests, `tools/ci/test_fetch_headers.py`): the fetch, both checks, the
cache, each refusal, against a git repository and a `file://` URL made for the
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
nothing; without it, a library named by `IMTYPESET_LIBRARY` is probed in a
child interpreter at every import. The script no longer writes
`<library>.gate`, and nothing reads one.

## 5. The checks, and where each runs

| script | what | here | on GitHub |
|---|---|---|---|
| `tools/ci/sidecar_probe.py` | the whole question for one interpreter: machine, wheel, symbols, build, audit, load, gate, checks; a report whatever happens | RAN, both environments | `sidecar-probe.yml`, every job |
| `tools/ci/sidecar_symbols.py` | what the module exports against what the library needs; the audit after linking | RAN (ELF) | same |
| `tools/ci/test_fetch_headers.py` | the header fetch against local sources | RAN, 14 tests | `ci.yml`, job `tools/ci` |
| `tools/ci/check_workflows.py` | the workflow files are YAML and keep this repository's rules (read-only permissions, exact action versions, a timeout per job, no logic outside `tools/ci`) | RAN with PyYAML 6 | same |
| `tools/ci/cpp_preset.sh` | one CMake preset: configure, build, ctest | RAN for `debug` on a copy of commit `caaeab3`, GCC | `ci.yml`: six presets by three toolchains |
| `tools/ci/sidecar_install.sh`, `install_deps.sh` | pip and the system's package manager | not runnable here (no network); syntax and shellcheck only | both workflows |
| `tools/ci/test_wheel.py` | the version line, the content comparison (ELF; Mach-O on a file made by hand), which gate runs where, every refusal before a library is loaded | RAN, 11 tests | `ci.yml`, job `tools/ci` |
| `tools/wheel/build_wheel.py`, `check_wheel.py`, `check_installed.py` | one wheel built, its archive checked, the installation checked (section 12) | RAN, both environments, stand-in headers | `wheels.yml` through `tools/ci/wheel_build.sh` |
| `tools/ci/wheel_build.sh`, `wheel_release.sh` | the same from PyPI's wheel in new environments; the release | syntax and shellcheck only (no index, no GitHub) | `wheels.yml` |

What `check_workflows.py` cannot show is that GitHub accepts the files:
expression syntax, contexts, runner labels and action versions are checked by
the first run there.

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
| the platform and architecture | it is machine code | the platform tag. Linux: `manylinux_2_<n>_x86_64`, `n` the larger of 28 (the imgui-bundle wheel's own tag) and what the library's versioned glibc symbols need, and only while its libstdc++ symbols fit what manylinux_2_28 allows; else `linux_x86_64`. RAN here: the library needs `GLIBC_2.14`, `GLIBCXX_3.4.21`, `CXXABI_1.3.9`, `GCC_7.0.0`, so `manylinux_2_28_x86_64`. The limits compared against are RECALLED from the manylinux policy, and auditwheel was not run (it does not know that the one unusual needed library, the wheel's module, is supplied by the pin). macOS: `macosx_14_0_arm64`, never built |
| one imgui-bundle wheel's binary | its Dear ImGui symbols and struct layouts are that module's | `Requires-Dist: imgui-bundle==X`, exactly; and the version line below |
| the CPython version | the attestation is of that CPython's `_imgui_bundle` module, and on Linux the library names that module's file, which carries the CPython tag | the Python tag `cp311`, and `Requires-Python: ==3.11.*` |
| **not** the CPython ABI | it is loaded with `ctypes` and calls no CPython function | the ABI tag `none` |

So a file is `imtypeset-0.3.0.192900-cp311-none-manylinux_2_28_x86_64.whl`.

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

With version lines, an application states both requirements and the resolver
does the rest: `imgui-bundle==1.92.801` and `imtypeset==0.3.0.*`. RAN (pip
26.2.1, `--no-index --find-links` over a directory holding both lines, in an
environment that has 1.92.801): pip considers `0.3.0.192900` first, finds its
pin unsatisfiable, and settles on `0.3.0.192801`; in the 1.92.900 environment
it takes `0.3.0.192900`. **Name both requirements.** `pip install imtypeset`
alone, with an index that also offers a newer imgui-bundle, would take the
newest line and move imgui-bundle with it; that is pip's ordinary behaviour
and was not run here (no index).

`imtypeset.version()` is the library's version (`0.3.0`).
`imtypeset.host_info()["distribution_version"]` is the distribution's
(`0.3.0.192900`), `None` for a development build.

**A wheel built against stand-in headers says so four times**, and is not a
release: its version ends `+standin` (which an index refuses), its metadata's
summary and description say it, its attestation has `"stand_in": true`, and
`host_info()["bundle"]["stand_in"]` is `True` with `"headers": "stand-in:..."`.
`build_wheel.py --release`, `check_wheel.py --release` and
`tools/ci/wheel_release.sh` each refuse one. Both wheels built here are
stand-in wheels: this machine cannot fetch the fork's headers.

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

**How the package finds its library**, in this order (`_capi.library_route`):

1. `IMTYPESET_LIBRARY`, when set: the development override. It wins, inside an
   installed distribution too. `IMTYPESET_PYTHON` is advtext's variable for
   where the package directory is; the package does not read it.
2. Otherwise the file `libimtypeset_bundle.so` (`.dylib`) **in the package's
   own directory**. No metadata, no entry point, no search path.

`host_info()["route"]` is `"IMTYPESET_LIBRARY"` or `"installed"`. An
application that wants to know whether an installed imtypeset is present
without importing it can look for `_attestation.json` beside
`importlib.util.find_spec("imtypeset").origin`; that survives a freezer that
drops `dist-info` directories, which the metadata does not.

Building one, as a developer:

```sh
python3 -P -B tools/wheel/build_wheel.py --python /path/to/python --out wheels
python3 -I -B tools/wheel/check_wheel.py wheels/*.whl
/path/to/an/environment/python -P -B tools/wheel/check_installed.py   # after pip install
```

## 9. The import gate

`import imtypeset` either succeeds with a library it has reason to believe
fits the Dear ImGui in the process, or raises `ImportError`. It no longer
starts a child interpreter, writes a stamp, or keys anything on a path or a
date, except in the development case at the end of this section.

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

1. *Before the library is loaded:* the attestation is read; the platform is
   compared; the library file and the installed wheel's module file are hashed
   and compared with it. Any difference is a refusal, and a library that is
   not the attested one is never mapped. (RAN: a library cut to half its
   length is refused here. With that comparison left out it is loaded and the
   process dies with SIGBUS; that is what was found when an earlier draft of
   this gate accepted a file on its linker build-id, which a truncated file
   keeps.)
2. `imgui_bundle` is imported, then the library is loaded (section 2).
3. *In the process, reading nothing through an offset:* that a second copy of
   the wheel's module was not loaded; the library's own account of its headers;
   Dear ImGui's `DebugCheckVersionAndDataLayout` (a comparison of a version
   string and six sizes inside the wheel; its assertion is an exception the
   library catches); the nine enumerators the library passes by value against
   the wheel's binding. Each must answer what the attestation records.
4. The package's own checks of the library against itself (struct sizes, the
   C ABI revision, every function it declares).

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
- *The second hash, for a file a freezer rewrote.* `strip` and code signing
  change a file's sha256 and leave alone what the loader maps. So each file
  also has a sha256 over exactly that: on ELF every `PT_LOAD` segment's bytes
  with its address and length, without the first 64 bytes (the ELF header,
  which `strip` edits); on Mach-O every section's bytes with its names and
  length, leaving out the header, the load commands and `__LINKEDIT`, which
  hold the signature and the install names. A file that matches neither hash
  is refused. RAN on ELF: a `strip --strip-all` copy is accepted by the second
  hash; a byte changed or the file cut inside a segment is refused. **The
  Mach-O reader is written from the format as remembered and has read only a
  file made by hand for its test.** If it is wrong, the failure is a refusal
  ("damaged") in a re-signed application, not an acceptance: a file it cannot
  read has no second hash.
- *That the attestation was written by a real probe.* Nothing signs it. Anyone
  who can write in the package can write an attestation, and can as well
  replace the library; the gate is a guard against a mismatched or damaged
  install, not against an attacker. `check_wheel.py` verifies, for a wheel
  file, that its library is the attested one.
- *That the file hashed is the file mapped.* True unless a file is replaced
  between the hash and the load, or was replaced after the process mapped the
  wheel's module (section 11, "under a running process").

The full probe remains, as the development path: `tools/bundle_route/run_gate.py`
prints its report, `mutate_gate.py` moves each offset to see that it notices,
`attest.py` writes an attestation, and the build script's last step runs all
that. **The probe runs at import only** for a library named by
`IMTYPESET_LIBRARY` that has no attestation beside it, in a child interpreter,
every time, and never when `sys.frozen` is set; or when `IMTYPESET_GATE=probe`
asks. `IMTYPESET_GATE=attested` forbids it everywhere. That one case also has
no protection against a truncated library, which is loaded before the child
is started.

**Refusals.** Every refusal is a plain `ImportError` (not a subclass: a
refused import leaves no module of the package behind to hold a class). Its
first line is a sentence an application can show: it names versions and what
to do, and holds no path. The lines after it are for a log. `e.kind`,
`e.sentence` and `e.detail` are set.

| `kind` | when | the sentence (0.3.0, examples from the runs of section 12) |
|---|---|---|
| `bundle-mismatch` | the wheel's module is not the attested one | "imtypeset 0.3.0 was built for imgui-bundle 1.92.900, and this application has imgui-bundle 1.92.801. The two are updated together: install imgui-bundle 1.92.900, or the imtypeset built for imgui-bundle 1.92.801." (same version, other binary: "…built for another build of imgui-bundle 1.92.900 than the one in this application…") |
| `bundle-missing` | `import imgui_bundle` fails | "imtypeset needs imgui-bundle 1.92.900, and it cannot be imported here. Install it." |
| `damaged` | the library is not the attested file, or the attestation cannot be read | "The imtypeset library is not the one imtypeset 0.3.0 was installed with: it is damaged, or an update replaced only part of it. Install imtypeset again." |
| `package-mismatch` | the Python modules and the library are of different releases | "The imtypeset library does not export 2 of the 95 functions this imtypeset package calls: … The library and the package are installed together, from one release." / "The imtypeset library refuses this package (written for …): …" |
| `layout` | Linux: `imgui_bundle` was imported from another directory than the package's neighbour | "imtypeset and imgui-bundle are installed in two different places, and imtypeset finds imgui-bundle only beside itself. Install both into the same environment." |
| `wrong-platform` | the package was built for another system | "This copy of imtypeset 0.3.0 was built for Darwin arm64, and this computer is Linux x86_64. Install the imtypeset built for this computer." |
| `unchecked` | no attestation, and no probe may run (installed, frozen, or `IMTYPESET_GATE=attested`) | "This copy of imtypeset carries no record that it was checked against the imgui-bundle it is running beside, and it is not checked here. Install a released imtypeset wheel." |
| `no-library` | `IMTYPESET_LIBRARY` unset and no library in the package: a source directory | "IMTYPESET_LIBRARY is not set, and this copy of the imtypeset package carries no library: it is a source directory and not an installed wheel." |
| `not-a-library` | the file cannot be loaded or is not imtypeset's | "The imtypeset library cannot be loaded: it is damaged, or it was built for another system or another imgui-bundle. Install imtypeset again." |
| `refused` | a check ran and refused: a second copy of the module, an answer that differs from the attestation, a failed probe | "The imtypeset library does not fit imgui-bundle … in this application. Install the imtypeset built for this imgui-bundle." |
| `configuration` | `IMTYPESET_GATE` or `IMTYPESET_BUNDLE_STRICT` asks for what cannot be given | "IMTYPESET_GATE=probe starts a Python interpreter, and a packaged application has none. Unset it." |

"There is no library for this platform" (advtext's H26) has no refusal of its
own, because it cannot arise from an install: where no wheel matches the
platform, pip installs nothing, and `import imtypeset` is Python's own
`ModuleNotFoundError`. A package copied from another system is
`wrong-platform`.

**The duplicate-module check** (`_bundle._same_module`) lists the files mapped
in the process and refuses a second `_imgui_bundle.*`. On Linux it reads
`/proc/self/maps`. On macOS it asks dyld (`_dyld_image_count`,
`_dyld_get_image_name`, through `ctypes.CDLL(None)`): written from Apple's
header as remembered, **never run**. It is never silently skipped:
`host_info()["bundle"]["second_module_check"]` is `"done: /proc/self/maps"`,
`"done: dyld's image list"`, or `"not done: <why>"`. On macOS a second copy is
also less likely by construction: the library names no module file, so
loading it cannot bring one in.

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
  `attestation.matched_by` (expected `image` for both files), `preload`, and
  `second_module_check` — or the refusal's two lines.

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
`docs/versioning.md`: until 1.0, a requirement of the form `imtypeset==0.3.*`.

| what was updated | what happens at `import imtypeset` |
|---|---|
| everything, to a consistent set | accepted; nothing is probed, started or written |
| imgui-bundle only (a new wheel under an old imtypeset) | `bundle-mismatch`, naming both versions. RAN, both directions, 1.92.900 and 1.92.801, frozen shape included |
| imtypeset only, to a wheel built for another imgui-bundle | the same sentence. pip refuses to get there: the pin conflicts |
| imgui-bundle rebuilt at the same version (another binary) | `bundle-mismatch`, "another build of". Not run: there is one binary of each version here |
| the library replaced, the Python modules not (or the reverse) | `damaged` when the attestation still describes the old library (RAN: any library but the attested one); `package-mismatch` when all three files of the new wheel arrived except the modules (RAN: modules declaring a function the library lacks; modules of another version string; and, for real, a library built from `c9f8902` under this tree's package: "does not export 2 of the 95 functions") |
| an update interrupted partway through a file | `damaged`, before the file is loaded (RAN: truncated to half, emptied, one byte changed) |
| the attestation lost | `unchecked` (RAN) |
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

NEVER RUN:

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
