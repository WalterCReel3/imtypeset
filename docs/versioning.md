# Versions, tags and what they promise

Written by task `py-dist` (2026-10-11) in answer to advtext's request H4. The
rule is an agent's decision; the user can change it. Nothing here has been
exercised by a release: there is no tag yet.

## One number, in one place

`IMTYPESET_VERSION` in `include/imtypeset.h` is the version. Everything else
reads it or is checked against it:

| who | how |
|---|---|
| CMake (`CMakeLists.txt`), the drop-in files (`tools/amalgamate.py`) | read the header |
| the wheel (`tools/wheel/build_wheel.py`) | reads the header through `tools/version.py`, and refuses to build when the library it just built reports another |
| `IMTYPESET_VERSION_NUM`, `IMTYPESET_C_VERSION` and `_NUM` (`include/imtypeset_c.h`) | copies; `static_assert` in `src/imtypeset_c.cpp` and a test |
| `C_VERSION` in `python/imtypeset/_capi.py` | a copy; the library refuses the package at import when it differs (`package-mismatch`) |
| `vcpkg.json`, `ports/imtypeset/vcpkg.json` | copies |

`python3 -I -B tools/version.py --check` reads every copy and fails naming the
one that differs; `tools/release.sh` and the wheel build both run it.

At run time:

| call | answers |
|---|---|
| `imtypeset.version()` | the library's version, `"0.4.0"`: the string compiled into the library that is loaded |
| `imtypeset.build_info().version_num` | the same as an integer, `400` (`XYYZZ`) |
| `imtypeset.host_info()["distribution_version"]` | the wheel's version, `"0.4.0.192900"`: the library's version and the imgui-bundle line (`docs/sidecar.md`, section 8); `None` for a development build; `.dev0+standin` at the end for a build against stand-in headers, a development release that orders below the release of its line |
| `imtypeset.build_info().abi_version` | the revision of the C header's shape, which the Python package checks itself and a host need not |

**What changes when a name's meaning does: the second number.** Until 1.0 the
version is `0.MINOR.PATCH`, and a host compares `MINOR`.

## The rule

It covers the Python names a host calls: everything in `imtypeset.__all__`,
the public methods and attributes of `Document` and `Paragraph`, the fields of
the structs and tuples, the members of the enums, `imtypeset.demo`, and the
keys of `host_info()` named in `docs/sidecar.md`. The names advtext uses
today are all among them: `Document` and its table, column, mark, hit-test and
geometry calls; `Paragraph`; `Block`, `Run`, `Cell`, `TableColumn`, `Columns`,
`Mark`, `BlockStyle`, `Style`, `DocumentOptions`; the enums; `create_context`,
`destroy_context`, `add_font`, `font_atlas_index`, `face_has_glyph`;
`imtypeset.demo`. It covers the C++ and C headers in the same terms.

| bump | what may change | what a host must do |
|---|---|---|
| **patch** (`0.4.0` to `0.4.1`) | Nothing a correct host can observe through those names except a defect going away: no name removed or renamed, no argument added without a default, no field removed, no enum value renumbered, no error raised where none was, and **no change to where text is set** (line breaks, positions, sizes) other than a fix the changelog names. New names may NOT appear either: a patch is installable over its predecessor in both directions | nothing as to names; a library's range `imtypeset==0.4.*` takes it, and an application moves its exact pin (below) |
| **minor**, while the major is 0 (`0.4.x` to `0.5.0`) | Anything, **and the changelog says what**: names added, a call's meaning changed, a default changed, layout changed, a name removed. Each entry says which of those it is | read `docs/CHANGELOG.md` for that version before moving; pin `==0.4.*` until then |
| **major** (`1.0.0`) | The promise changes: from 1.0 on, a minor adds and never changes or removes, and only a major may | — |

Outside the rule, and free to change in any release: names beginning with an
underscore (`_capi`, `_bundle`, `_Marks`), the text of an error message or of a
refusal's sentence (its `kind` is inside the rule), the probe's rows, what
`host_info()["bundle"]` holds beyond `headers`, `stand_in`, `wheel_version`,
`built_for`, `accepted_by`, `checked_in`, and anything a document here calls
never run.

Two consequences worth saying:

- **The library and the Python package are one release, always.** They ship
  in one wheel; a package that meets a library of another version refuses it
  at import. There is no compatibility between a package of one tag and a
  library of another, in either direction, and none is promised.
- **The imgui-bundle line is not part of the rule.** `0.4.0.192900` and
  `0.4.0.192801` are the same imtypeset, with the same names meaning the same
  things, built for two imgui-bundle wheels.

## What an application pins

An imtypeset wheel is built for one imgui-bundle and requires it exactly
(`docs/sidecar.md`, section 8). So an application pins **both, exactly, and
moves them together**:

```
imgui-bundle==1.92.900
imtypeset==0.4.0.192900
```

- `imtypeset==0.4.0` matches no wheel: the version has four numbers, and
  under PEP 440 `==0.4.0` means `0.4.0.0`. A range over the library version
  is `==0.4.0.*` (this patch, any imgui-bundle line) or `==0.4.*`.
- A range is safe only beside an exact `imgui-bundle==X`: the resolver then
  settles on the line built for X (measured, pip 26.2.1).
- **`pip install -U imtypeset` alone takes the newest line and replaces the
  application's imgui-bundle with the one that line was built for.** Nothing
  breaks at import, because the pair is consistent; what moved is the Dear
  ImGui under the application. Recalled from pip's behaviour, not run here.
- A build against stand-in headers is `0.4.0.192900.dev0+standin`: a
  development release. It is below the release of its line, an exact pin of
  the release does not match it, and a range does not take it while a
  release fits (measured, `packaging` 26.3 and pip 26.2.1). A stand-in of a
  newer line is still above an older release: the exact pins keep it out.

A library that others install into their own environment (advtext) states the
range it works with, `imtypeset==0.4.*`, and says in its own notes that the
application pins the pair.

## Tags and releases

- A release is an annotated tag `v<version>` on `main`: `v0.4.0`. That tag is
  the ref another project builds from (`make typeset-lib IMTYPESET_REF=v0.4.0`)
  and the ref CI builds the wheels of.
- `tools/release.sh v0.4.0` checks what must hold before the tag is made (the
  version and its copies, a changelog section, a clean tree, the drop-in files
  generating, the workflow rules, the distribution's tests) and prints the two
  commands. It creates nothing. A person creates and pushes the tag.
- On the tag, the workflow `wheels` builds a wheel for every platform, CPython
  and imgui-bundle of its matrix against the wheel's own headers, checks and
  installs each, and attaches them to the GitHub Release of the tag only if
  every one passed and none is a stand-in build. Never run.
- `docs/CHANGELOG.md` has one section per tag, newest first, and the release's
  notes are that section.
- A tag is never moved. A mistake is a new patch.

## The first tag

Proposed: **`v0.3.0`**. Not `v0.2.0`: six untagged commits called themselves
0.2.0 while names were added and meanings changed, and advtext built against
several of them; a number that has meant several things should not become a
tag. What must be true before it is created is in the result file of the task
that wrote this (`.concerns/py-dist--builder--result.md`) and, once decided,
belongs in `docs/advtext-swap-status.md`.

## After the first tag

`v0.3.0` was created. `0.4.0` is the next: task `device-finish` moved the number (two
host-filled structs grew, one the library fills grew, a default changed, text is set
differently; `docs/CHANGELOG.md`). Not tagged by that task: a person creates the tag.
