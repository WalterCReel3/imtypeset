# Sidecar probe: Linux x86_64, CPython 3.11.2

**Where:** Linux x86_64, CPython 3.11.2; imgui-bundle 1.92.900.

**H1 (does the sidecar work here): YES.** A library bound by `link`, built against stand-in headers, was accepted by the import gate with nothing loaded first, `import imtypeset` succeeded, and the headless checks passed.
- Symbols (stand-in headers, compiled here): 117 Dear ImGui symbols needed, **0 not exported by the wheel's module**.
- Symbols (the Linux list, nothing compiled here): 117 needed, 0 not exported.
- Module visible to the process after a plain import: **False**; after reopening it global: **True**.

**H2 (the wheel's own headers): NO.** Reached `nothing`, failed at `fetch`.

For comparison, the same objects bound by `lookup` on this platform: accepted only after the wheel's module is reopened with RTLD_GLOBAL.

Written by `tools/ci/sidecar_probe.py` in 113 s. Everything below is what ran on this machine; a number or a message here was printed by a tool, not recalled.

## 1. The machine

- **system**: Linux 6.1.0-45-amd64 x86_64
- **platform**: Linux-6.1.0-45-amd64-x86_64-with-glibc2.36
- **python**: 3.11.2 at /home/wreel/Source/imtypeset/../imgui-adv-text/.venv/bin/python
- **extension suffix**: .cpython-311-x86_64-linux-gnu.so
- **how this Python links an extension module (LDSHARED)**: x86_64-linux-gnu-gcc -shared -Wl,-O1 -Wl,-Bsymbolic-functions   -g -fwrapv -O2   
- **how this Python loads one (sys.getdlopenflags())**: 0x2 (RTLD_NOW); RTLD_GLOBAL is 0x100, RTLD_LOCAL is 0x0
- **compiler**: `g++`: g++ (Debian 12.2.0-14+deb12u1) 12.2.0
- **git**: git version 2.39.5
- **linker**: GNU ld (GNU Binutils for Debian) 2.40

## 2. The wheel

- **imgui-bundle**: 1.92.900, Dear ImGui 1.92.9
- **wheel tags**: cp311-cp311-manylinux_2_28_x86_64
- **module**: `/home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so`
- **its own symbol table has `ImGui::GetIO`** (dlsym on the module's handle): True
- **visible to the whole process after a plain `import imgui_bundle`** (dlsym on dlopen(NULL)): **False**
- **visible after opening the module again with RTLD_GLOBAL**: **True**

`file <module>` (status 0):

```
/home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so: ELF 64-bit LSB shared object, x86-64, version 1 (SYSV), dynamically linked, BuildID[sha1]=96cd95038ade1704c972505ace042b52c96c736a, stripped
```

`readelf -d <module>` (status 0):

```
 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN/./:$ORIGIN/../imgui_bundle.libs]
 0x0000000000000001 (NEEDED)             Shared library: [libglfw.so.3]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libdl.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libX11.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libGLX-c14481bc.so.0.0.0]
 0x0000000000000001 (NEEDED)             Shared library: [libOpenGL-4c5f8d52.so.0.0.0]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libpthread.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
```

## 3. What the module exports, against the Linux list

- the module exports **11328** symbols, 6046 of them in Dear ImGui's namespace or a class or namespace whose name begins with `Im`
- needed: **117** Dear ImGui symbols, from expected_imgui_symbols.txt (Linux, stand-in headers)
- of those, the module does not export: **0**

The needed list here is NOT from this machine: it is what a Linux x86-64 build against stand-in headers leaves undefined. Two of its names carry a `std::function` as libstdc++ mangles it; where the wheel is built with libc++ those two are expected in the missing list and mean nothing by themselves. The sections that follow have the list from objects compiled here.

## 4. A library with fork headers, bound by link

**fetch** (headers=fork, bind=link): status **1**, 0 s, log `build-fork-link-fetch.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=fork, bind=link, steps=fetch
fetch_headers.py: fetching Dear ImGui c9a0a84f8a4ddae794a9e1acf6a93afce73d5057 from https://github.com/pthom/imgui.git
fetch_headers.py: the fork headers for imgui-bundle 1.92.900 (Dear ImGui commit c9a0a84f8a4ddae794a9e1acf6a93afce73d5057) could not be fetched: git -c fetch.fsckObjects=true fetch -q --depth 1 --no-tags https://github.com/pthom/imgui.git c9a0a84f8a4ddae794a9e1acf6a93afce73d5057 ended with status 128: fatal: unable to access 'https://github.com/pthom/imgui.git/': CONNECT tunnel failed, response 403
build_bundle_library.sh: the fork headers for imgui_bundle 1.92.900 are not available
  (fetch_headers.py ended with status 1; its reason is above). Nothing was built.
  This script does not substitute other headers. If a library built against
  upstream Dear ImGui's headers as a stand-in is acceptable, ask for it by name:
      IMTYPESET_BUNDLE_HEADERS=stand-in /home/wreel/Source/imtypeset/python/host/build_bundle_library.sh ...
```

## Symbols the objects compiled here need (stand-in headers; belongs to section 5. below)

- the module exports **11328** symbols, 6046 of them in Dear ImGui's namespace or a class or namespace whose name begins with `Im`
- needed: **117** Dear ImGui symbols, from /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-link/obj
- of those, the module does not export: **0**

## 5. A library with stand-in headers, bound by link

**fetch** (headers=stand-in, bind=link): status **0**, 1 s, log `build-stand-in-link-fetch.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=link, steps=fetch
build_bundle_library.sh: NOTE: building against STAND-IN headers, as asked
  (IMTYPESET_BUNDLE_HEADERS=stand-in). They are upstream Dear ImGui's, not the
  wheel's: Dear ImGui's own layout check will refuse this library, and
  python/imtypeset accepts it on its field probe alone.
fetch_headers.py: fetching Dear ImGui 9b4eb24cee2071e61dc1f9ef3e5228097cdde720 from /home/wreel/Source/imtypeset/../imgui
fetch_headers.py:   173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3  imgui/LICENSE.txt
fetch_headers.py:   5755e1b8d6ab0d7811a9d7cac0f509878b51fc5ee77e09bd0f235bcb414971e7  imgui/imconfig.h
fetch_headers.py:   9d0e253b99b6a06fbfc98bae7565fc1c5e8a8825626c49d7c5c23143fb30e851  imgui/imgui.h
fetch_headers.py:   5930ceee326d78e58c3f3a0baa500e36db24affc8e73d9f8a14b57700536310e  imgui/imgui_internal.h
fetch_headers.py:   889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f  imgui/imstb_rectpack.h
fetch_headers.py:   a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741  imgui/imstb_textedit.h
fetch_headers.py:   c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528  imgui/imstb_truetype.h
fetch_headers.py:   ccab306d04457df1130dc6b702e9499adfc0607fce63bc573284bda4122c78ea  imgui_bundle_config.h
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
```

**compile** (headers=stand-in, bind=link): status **0**, 38 s, log `build-stand-in-link-compile.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=link, steps=compile
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
amalgamate.py: wrote 5 files to /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-link/dist (imtypeset.cpp: 35496 lines)
compiled 4 objects into /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-link/obj
```

**link** (headers=stand-in, bind=link): status **0**, 0 s, log `build-stand-in-link-link.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=link, steps=link
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
built /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-link/libimtypeset_bundle.so (stand-in:ocornut-v1.92.9-docking, bind=link)
```

**audit** (headers=stand-in, bind=link): status **0**, 0 s, log `build-stand-in-link-audit.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=link, steps=audit
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
sidecar_symbols.py: libimtypeset_bundle.so needs 117 Dear ImGui symbols, all exported by _imgui_bundle.cpython-311-x86_64-linux-gnu.so; it exports 103 symbols, all the C ABI
```

**gate, nothing loaded first**: status **0**, log `load-stand-in-link-gate-preload-none.log`

```
{
 "headers": "stand-in:ocornut-v1.92.9-docking",
 "wheel_version": "1.92.900",
 "wheel_imgui_version": "1.92.9",
 "imgui_check": false,
 "imgui_check_message": "IM_ASSERT( sz_style == sizeof(ImGuiStyle) && \"Mismatched struct layout!\" )   ---   imgui.cpp:11748",
 "imgui_check_refuses_only_style_size": true,
 "wheel_sizeof_imguistyle": 1372,
 "library_sizeof_imguistyle": 1368,
 "probes_passed": 24,
 "probes_failed": [],
 "strict": false
}
accepted: (True, 'probe') (85.6 ms)
```

**import imtypeset (the package's own gate)**: status **0**, 0 PASS lines, log `load-stand-in-link-check-0.log`

```
{"host": "imgui_bundle", "library": "/home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-link/libimtypeset_bundle.so"}
{"headers": "stand-in:ocornut-v1.92.9-docking", "accepted_by": "probe", "checked_in": "child", "imgui_check": false, "imgui_check_message": "IM_ASSERT( sz_style == sizeof(ImGuiStyle) && \"Mismatched struct layout!\" )   ---   imgui.cpp:11748", "probes_passed": 24, "probes_failed": []}
```

**tools/bundle_route/check_document_edits.py**: status **0**, 23 PASS lines, log `load-stand-in-link-check-1.log`

```
(no output)
```

**tools/bundle_route/check_swap_answers.py**: status **0**, 18 PASS lines, log `load-stand-in-link-check-2.log`

```
all passed
```

**tools/bundle_route/mutate_gate.py (does the probe notice a moved offset)**: status **0**, 0 PASS lines, log `load-stand-in-link-check-3.log`

```
[... 73 earlier lines in the log ...]
ImFontConfig.MergeMode                   +8: caught by ImFontConfig fields
ImFontConfig.MergeMode                   -4: caught by ImFontConfig fields
ImFontConfig.OversampleH                 +4: caught by ImFontConfig fields
ImFontConfig.OversampleH                 +8: caught by ImFontConfig fields
ImFontConfig.OversampleH                 -4: caught by ImFontConfig fields
ImFontConfig.FontNo                      +4: caught by ImFontConfig fields
ImFontConfig.FontNo                      +8: caught by ImFontConfig defaults, ImFontConfig fields
ImFontConfig.FontNo                      -4: caught by ImFontConfig fields
ImFontConfig.SizePixels                  +4: caught by ImFontConfig fields
ImFontConfig.SizePixels                  +8: caught by ImFontConfig fields
ImFontConfig.SizePixels                  -4: caught by ImFontConfig fields
ImFontConfig.RasterizerDensity           +4: caught by ImFontConfig fields
ImFontConfig.RasterizerDensity           +8: caught by ImFontConfig defaults, ImFontConfig fields
ImFontConfig.RasterizerDensity           -4: caught by ImFontConfig fields
30 offsets, 87 mutants, 12 crashed, 0 survived: []
```

## 6. A library with stand-in headers, bound by lookup

**link** (headers=stand-in, bind=lookup): status **0**, 0 s, log `build-stand-in-lookup-link.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=lookup, steps=link
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
built /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-lookup/libimtypeset_bundle.so (stand-in:ocornut-v1.92.9-docking, bind=lookup)
```

**audit** (headers=stand-in, bind=lookup): status **0**, 1 s, log `build-stand-in-lookup-audit.log`

```
wheel: imgui_bundle 1.92.900, Dear ImGui 1.92.9, /home/wreel/Source/imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle/_imgui_bundle.cpython-311-x86_64-linux-gnu.so
build: Linux x86_64, g++, headers=stand-in, bind=lookup, steps=audit
headers: stand-in:ocornut-v1.92.9-docking, in /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/headers/1.92.900/stand-in
sidecar_symbols.py: libimtypeset_bundle.so needs 117 Dear ImGui symbols, all exported by _imgui_bundle.cpython-311-x86_64-linux-gnu.so; it exports 103 symbols, all the C ABI
```

**gate, nothing loaded first**: status **1**, log `load-stand-in-lookup-gate-preload-none.log`

```
Traceback (most recent call last):
  File "/home/wreel/Source/imtypeset/tools/bundle_route/run_gate.py", line 23, in <module>
    library = ctypes.CDLL(os.path.abspath(sys.argv[1]))
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/usr/lib/python3.11/ctypes/__init__.py", line 376, in __init__
    self._handle = _dlopen(self._name, mode)
                   ^^^^^^^^^^^^^^^^^^^^^^^^^
OSError: /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-lookup/libimtypeset_bundle.so: undefined symbol: _ZN15ImGuiTextBuffer11EmptyStringE
```

**gate, `import imgui_bundle` first**: status **1**, log `load-stand-in-lookup-gate-preload-import.log`

```
Traceback (most recent call last):
  File "/home/wreel/Source/imtypeset/tools/bundle_route/run_gate.py", line 23, in <module>
    library = ctypes.CDLL(os.path.abspath(sys.argv[1]))
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/usr/lib/python3.11/ctypes/__init__.py", line 376, in __init__
    self._handle = _dlopen(self._name, mode)
                   ^^^^^^^^^^^^^^^^^^^^^^^^^
OSError: /home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-lookup/libimtypeset_bundle.so: undefined symbol: _ZN15ImGuiTextBuffer11EmptyStringE
```

**gate, `import imgui_bundle` and its module reopened RTLD_GLOBAL first**: status **0**, log `load-stand-in-lookup-gate-preload-1.log`

```
{
 "headers": "stand-in:ocornut-v1.92.9-docking",
 "wheel_version": "1.92.900",
 "wheel_imgui_version": "1.92.9",
 "imgui_check": false,
 "imgui_check_message": "IM_ASSERT( sz_style == sizeof(ImGuiStyle) && \"Mismatched struct layout!\" )   ---   imgui.cpp:11748",
 "imgui_check_refuses_only_style_size": true,
 "wheel_sizeof_imguistyle": 1372,
 "library_sizeof_imguistyle": 1368,
 "probes_passed": 24,
 "probes_failed": [],
 "strict": false
}
accepted: (True, 'probe') (2.4 ms)
```

**import imtypeset (the package's own gate)**: status **0**, 0 PASS lines, log `load-stand-in-lookup-check-0.log`

```
{"host": "imgui_bundle", "library": "/home/wreel/Source/imtypeset/build/mac-ci-probe-advtext/lib-stand-in-lookup/libimtypeset_bundle.so"}
{"headers": "stand-in:ocornut-v1.92.9-docking", "accepted_by": "probe", "checked_in": "child", "imgui_check": false, "imgui_check_message": "IM_ASSERT( sz_style == sizeof(ImGuiStyle) && \"Mismatched struct layout!\" )   ---   imgui.cpp:11748", "probes_passed": 24, "probes_failed": []}
```

**tools/bundle_route/check_document_edits.py**: status **0**, 23 PASS lines, log `load-stand-in-lookup-check-1.log`

```
(no output)
```

**tools/bundle_route/check_swap_answers.py**: status **0**, 18 PASS lines, log `load-stand-in-lookup-check-2.log`

```
all passed
```

**tools/bundle_route/mutate_gate.py (does the probe notice a moved offset)**: status **0**, 0 PASS lines, log `load-stand-in-lookup-check-3.log`

```
[... 73 earlier lines in the log ...]
ImFontConfig.MergeMode                   +8: caught by ImFontConfig fields
ImFontConfig.MergeMode                   -4: caught by ImFontConfig fields
ImFontConfig.OversampleH                 +4: caught by ImFontConfig fields
ImFontConfig.OversampleH                 +8: caught by ImFontConfig fields
ImFontConfig.OversampleH                 -4: caught by ImFontConfig fields
ImFontConfig.FontNo                      +4: caught by ImFontConfig fields
ImFontConfig.FontNo                      +8: caught by ImFontConfig defaults, ImFontConfig fields
ImFontConfig.FontNo                      -4: caught by ImFontConfig fields
ImFontConfig.SizePixels                  +4: caught by ImFontConfig fields
ImFontConfig.SizePixels                  +8: caught by ImFontConfig fields
ImFontConfig.SizePixels                  -4: caught by ImFontConfig fields
ImFontConfig.RasterizerDensity           +4: caught by ImFontConfig fields
ImFontConfig.RasterizerDensity           +8: caught by ImFontConfig defaults, ImFontConfig fields
ImFontConfig.RasterizerDensity           -4: caught by ImFontConfig fields
30 offsets, 87 mutants, 12 crashed, 0 survived: []
```

