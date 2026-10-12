# The sidecar: imtypeset beside an unmodified imgui-bundle wheel

A Python application that runs on the `imgui-bundle` wheel from PyPI gets
imtypeset as one shared library loaded with `ctypes`. The library holds no
Dear ImGui. Its Dear ImGui symbols are left undefined and are bound to the
wheel's own extension module, so it draws into the application's context, font
atlas and draw lists. The wheel is not touched.

This document says how that binding is made on each platform, what a run has
established, and what waits for one. Three words are kept apart throughout:

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
- The run path is the absolute directory of the wheel the library was built
  against. A library that is to be installed somewhere else needs a run path
  relative to itself (`$ORIGIN/...`); that belongs to the packaging work and is
  not done.
- RAN: a library built in one environment and imported in the other (same
  CPython tag, another wheel) loads the first environment's module as a second
  copy, and the import gate refuses it by name (`_bundle._same_module`).

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
   `imgui_bundle`. **`python/imtypeset` does not arrange this today**: the
   import gate's child interpreter (`_bundle._CHILD`) loads the library before
   it imports the wheel, and `tools/bundle_route/run_gate.py` does the same.
   Until the package does (task `py-dist` owns those files), a lookup-bound
   library is run with `tools/ci/preload` on `PYTHONPATH` and
   `IMTYPESET_PRELOAD_BUNDLE` set: `import` imports the wheel first, `1` also
   reopens its module with `RTLD_GLOBAL`. The probe tries a lookup-bound
   library all three ways and reports which is the least that works.

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

Not looked at for macOS, and each needs a decision or a person at a Mac: code
signing and the hardened runtime, a universal binary, Retina, the gate's
`/proc/self/maps` check (which does nothing off Linux), an application freezer.

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

## 5. The checks, and where each runs

| script | what | here | on GitHub |
|---|---|---|---|
| `tools/ci/sidecar_probe.py` | the whole question for one interpreter: machine, wheel, symbols, build, audit, load, gate, checks; a report whatever happens | RAN, both environments | `sidecar-probe.yml`, every job |
| `tools/ci/sidecar_symbols.py` | what the module exports against what the library needs; the audit after linking | RAN (ELF) | same |
| `tools/ci/test_fetch_headers.py` | the header fetch against local sources | RAN, 14 tests | `ci.yml`, job `tools/ci` |
| `tools/ci/check_workflows.py` | the workflow files are YAML and keep this repository's rules (read-only permissions, exact action versions, a timeout per job, no logic outside `tools/ci`) | RAN with PyYAML 6 | same |
| `tools/ci/cpp_preset.sh` | one CMake preset: configure, build, ctest | RAN for `debug` on a copy of commit `caaeab3`, GCC | `ci.yml`: six presets by three toolchains |
| `tools/ci/sidecar_install.sh`, `install_deps.sh` | pip and the system's package manager | not runnable here (no network); syntax and shellcheck only | both workflows |

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
