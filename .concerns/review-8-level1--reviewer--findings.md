# review-8-level1 findings (filed by the lead, 2026-10-12) — ALL READ-DERIVED, nothing run

Full text: the notes on board task `review-8-level1` (read_board()). [R] = the reviewer's recollection
of a format, linker, dyld, pip, a freezer or GitHub Actions. Commits reviewed: 69375e6, 74f5116, 03b5cd4.

F1 MOST SEVERE. `IMTYPESET_LIBRARY` bypasses the gate in a frozen or installed application.
   `_capi.library_route` honours the variable unconditionally; the only `sys.frozen` test is in
   `_bundle.mode` and only forbids a child interpreter. With the variable set where the app starts:
   (a) no attestation beside the named file: `precheck` returns None before `mode()`; ctypes.CDLL maps
       the file, static initialisers run, ImTypeset_GetBuildInfo is called; only then can `gate()`
       raise `unchecked`. A truncated file here is the SIGBUS the redesign was to prevent.
   (b) a library without `ImTypesetBundle_LayoutFacts` (the self-hosted libimtypeset_pyhost.so):
       `__init__._load` never calls `gate()`; the import succeeds, frozen or not.
   (c) a dev build with `<lib>.attestation.json` beside it (build_bundle_library.sh writes one by
       default): `mode()` returns "attested" before its frozen test.
   Same root, lesser: `IMTYPESET_GATE=probe` in an installed non-frozen wheel loads the packaged
   library with no content check; `IMTYPESET_PRELOAD_BUNDLE=1` reaches the RTLD_GLOBAL reopen on
   Linux from the environment, frozen included. [R] Briefcase and Nuitka do not set sys.frozen.
F2. Tail truncation on Mach-O passes: `_macho_loaded` excludes __LINKEDIT (the end of the file); a
   dylib cut inside it fails sha256, matches the section hash, is loaded. [R] dyld should refuse
   cleanly. check_installed.py only cuts to half.
F3. What shares the second hash: Mach-O header, load commands (LC_LOAD_DYLIB, LC_RPATH), __LINKEDIT,
   [R] bind/fixup tables; ELF header, p_flags/p_memsz/p_offset, non-LOAD program headers. A tampered
   __TEXT cannot pass; an added LC_LOAD_DYLIB can. (The doc says the gate is not a defence against an
   attacker.)
F4. Non-ImportError escapes (hand-edited/foreign files; none can accept): `_bundle._module_file()`
   AttributeError when imgui_bundle has no `_imgui_bundle.__file__`; `same_content` AttributeError
   when the attestation's library/module is not an object; `gate()` ValueError on probe rows that are
   not triples; `precheck` imports imgui_bundle (loading its native module) before hashing it,
   contrary to its docstring.
F5. `+standin` sorts ABOVE the release of the same line [R, PEP 440]: a --find-links directory with
   both gives pip the stand-in. tools/wheel/build_wheel.py, tools/ci/wheel_build.sh.
F6. `pip install -U imtypeset` alone picks the newest line and upgrades imgui-bundle under the
   application [R]; `imtypeset==0.3.0` matches nothing, only `==0.3.*`.
F7. Linux wheels built on ubuntu-latest will likely not suit Debian 12 [R]: `platform_tag` would
   give manylinux_2_38 or plain linux_x86_64.
F8. `.github/workflows/wheels.yml`: `"${{ github.ref_name }}"` interpolated into a `run:` line of the
   one job with contents: write. Actions pinned by tag, not commit id.
F9. `fetch_headers._cached` validates the cache against its own SOURCE.json; `imgui_h_sha256` is
   optional in the pins.
F10. macOS link line [R]: flags and exported-symbols syntax match recollection; lookup binding takes
   the first global image exporting each ImGui name; `_same_module` looks only for a second
   `_imgui_bundle.*`.
F11. `License-File` under Metadata-Version 2.1 [R]: an index would reject it.
F12 (C++, low). Mark culling: an `Under` mark with a host-given Offset larger than about an em, on
   the line just above the clip, in a block with more than 16 marks, loses its bar. May predate H10.
Confirmed by reading: the Mach-O reader's layout [R, high confidence]; the installed route with no
IMTYPESET_* variable refuses absent/stale/truncated/wrong-platform/mismatched attestations before
load; attestation bound to both files by content; mark culling drops no visible mark the reviewer
could construct; GetFontMetrics out_size; wheel contents and the release refusal; workflow
permissions and triggers; fetch_headers executes nothing before verification. The four pinned action
tags are each listed as the latest release on GitHub (a summarising fetch by the reviewer).
Not reached: H11's clip rule and the C++ hidpi is writing; H7's arithmetic; tools/ci/sidecar_*.py,
test_wheel.py, tools/version.py, tools/release.sh, pyproject.toml and its backend; the shim.
Two facts about Apple's tools that decide the macOS half and cannot be settled here: dyld's behaviour
on a file cut inside __LINKEDIT, and that re-signing leaves section bytes untouched.
