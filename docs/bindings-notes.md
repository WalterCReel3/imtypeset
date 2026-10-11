# imtypeset and Dear ImGui Bundle: what Python use would look like

**Read with what came after it (note added 2026-10-10).** Two of this file's conclusions have been overtaken. Section 6B ends by saying a separately built library could not be driven inside the installed wheel; task `bundle-route` did it, for the probe-gated case, and the paragraph there now says how (`docs/distribution.md` section 1). And section 5 lists the header as it was before the Document, the demo window and `GetContextSerial` were added (D42, D43, D46): the list of declarations to trust is `tests/test_c_api_map.cpp`. Binding the library the bundle's own way (sections 2 to 4, `bindings/imgui_bundle/`) is backlog; nothing of it has been built.

Task `bundle-research`, 2026-10-09. Three kinds of statement are kept apart:

- **READ**: taken from a file that was opened; the file and line are given. Bundle paths are relative to `../imgui_bundle` (the clone, `v1.92.900-723-gf639fda4`, commit `f639fda4`), wheel paths to `../imgui-adv-text/.venv/lib/python3.11/site-packages/imgui_bundle` (the installed `imgui-bundle 1.92.900`, `cp311-manylinux_2_28_x86_64`).
- **RAN**: executed in this session; the script and the output file under `build/bundle-research/` are given.
- **NOT DETERMINED**: said so, with what would settle it.

Two facts frame everything below.

- The clone and the wheel are not the same source. The clone's `pyproject.toml:13` says `1.93.0` ("Based on ImGui 1.93.0 WIP"); the wheel is `1.92.900` and its ImGui answers `1.92.9`. Where it matters the tag `v1.92.900` of the clone was read as well (`git show v1.92.900:<path>`), and says so.
- No submodule of the clone is checked out, litgen and nanobind are installed nowhere on this machine, and PyPI and GitHub are not reachable. So the bundle was not built, the generator was not run, and the bundle's ImGui **fork** (`external/imgui/imgui`, `https://github.com/pthom/imgui.git` branch `imgui_bundle`, `.gitmodules:1-4`) was never seen.

---

## 1. The three questions

### What would using imtypeset from Python look like?

Like implot does in the bundle: a submodule `imgui_bundle.imtypeset`, snake_case functions, the `ImTypeset` prefix dropped from types, enums as `IntFlag` classes with a trailing underscore. The sketch below is written against `include/imtypeset.h` 0.2.0 (the header after `api-revise`, D36) and the hand-written entries of `bindings/imgui_bundle/generate_imtypeset.py`. It is a sketch: no generated binding exists and none of it has run.

```python
from imgui_bundle import imgui, imtypeset, immapp, hello_imgui

def font(name: str) -> str:                # the bundle's own assets: see "Beyond the bindings"
    return hello_imgui.asset_file_full_path("fonts/" + name)

def setup() -> None:                       # once, after imgui.create_context()
    imtypeset.create_context()
    imtypeset.add_font_from_file_ttf(imtypeset.Face_.regular, font("Roboto/Roboto-Regular.ttf"))
    imtypeset.add_font_from_file_ttf(imtypeset.Face_.italic, font("Roboto/Roboto-RegularItalic.ttf"))
    imtypeset.add_font_from_file_ttf(imtypeset.Face_.math, font("latex/latinmodern-math.otf"))
    # or, from bytes that never touch the disk:
    # imtypeset.add_font_from_memory_ttf(imtypeset.Face_.code, code_font_bytes)

def gui() -> None:                         # every frame
    # One call per paragraph, like imgui.text_wrapped(); layout is cached.
    imtypeset.text_unformatted(
        r"The roots are $x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$, set in the line.",
        flags=imtypeset.TextFlags_.math,
    )

# A paragraph the host keeps: styled runs, selection, copy.
text = "Total fit, with a bold word."
para = imtypeset.create_paragraph()
run = imtypeset.Run()
run.start, run.end = text.encode().index(b"bold"), text.encode().index(b"bold") + 4   # BYTE offsets
run.face = imtypeset.Face_.bold
imtypeset.set_paragraph_text(para, text, runs=[run])

def gui_paragraph() -> None:
    imtypeset.layout_paragraph(para, imgui.get_content_region_avail().x)   # False: get_paragraph_error(para)
    pos = imgui.get_cursor_screen_pos()
    imtypeset.draw_paragraph(para, imgui.get_window_draw_list(), pos, selection)   # selection: a TextRange
    imgui.dummy(imtypeset.get_paragraph_size(para))
    if imgui.is_item_clicked():
        m = imgui.get_mouse_pos()
        hit = imtypeset.hit_test_paragraph(para, (m.x - pos.x, m.y - pos.y))
        byte_offset = hit.text_offset                                      # a BYTE offset into the text
    if copy_requested:
        imgui.set_clipboard_text(imtypeset.append_paragraph_range_text(para, selection))

params = hello_imgui.RunnerParams()
params.callbacks.post_init = setup         # the ImGui context exists, the first frame has not run
params.callbacks.show_gui = gui
immapp.run(params)
```

What a Python user will notice, none of it visible in the sketch:

- **Offsets are UTF-8 byte offsets**, and Python strings are indexed by codepoint. `run.start = text.index("bold")` is wrong for any text with a non-ASCII character before the word. The header has the conversions (`text_codepoint_to_byte_offset`, `paragraph_byte_to_codepoint_offset` and their inverses); until a binding hides them inside its wrappers every caller converts, or writes `.encode()`.
- **Handles are freed by hand.** `create_paragraph()` returns an object whose C++ side lives until `destroy_paragraph()` or until the context dies; Python's garbage collector does not free it, and a Python reference kept past `destroy_context()` is dangling. The bundle binds returned pointers as references (`external/imgui/bindings/litgen_options_imgui.py:383`), so nothing owns them on the Python side.
- **An ImGui assertion is a Python exception.** In a Python build the bundle's `IM_ASSERT` throws `std::runtime_error` (`imgui_bundle_cmake/imgui_bundle_config.h:38-47`), which arrives as `RuntimeError` (RAN: the message of section 6 is one). The library's own errors stay data (`get_last_error()`), so there are two error channels.
- **The widget's cache** is keyed on the text's content and the ID stack since D36, so an equal string in a new object each frame is one layout. Section 7 measured the header before that, where it was not; the measurement has not been repeated.

### Does it play well with the bundle?

**As a library compiled into the bundle, the way implot is: yes on the evidence available, with a short list of changes. As a separate binary loaded next to the installed wheel: no.**

What speaks for the first:

- The public header is the shape the bundle binds: free functions in one namespace behind one API macro, PODs, `imgui.h` types, no templates of its own, no callbacks. Of 49 declarations that produce a binding (8 enums, 6 structs, 33 functions, 2 handles), 37 need nothing, 5 need one option, and 7 need a hand-written binding of a few lines, each with a precedent in the bundle (section 5).
- It needs nothing from ImGui that the bundle's build does not have: the wheel's ImGui is 1.92.9 with a 32-bit `ImWchar` and FreeType (RAN, section 6), which is this repository's default configuration, and all 40 ImGui symbols the library references are exported by the wheel (RAN: `build/bundle-research/docking/undefined-imgui.txt`, `missing-from-bundle.txt` is empty).
- The sources compile without a warning against the 1.92.9 docking header with every define the bundle's Python build sets, `ImDrawIdx=unsigned int` and `ImTextureID=int` included (RAN: `build/bundle-research/docking/build.log`, `-Wall -Wextra`). They also compile against stock 1.93 (`.concerns/dist-research--builder-19--result.md`), which is where the clone's ImGui now is.
- The font path, the part the design note called structurally unreachable from Python, is reachable today on the unmodified wheel: the font this library rebuilds in memory, handed to the bundle's atlas as bytes, gives all 227 minted plane-15 codepoints as inked glyphs (RAN, section 6).

What has to change, on our side:

1. **Build with exceptions on inside the bundle.** Every target linking the bundle's `imgui` gets its config header, whose `IM_ASSERT` is a `throw`. With `-fno-exceptions` 14 of 15 `src/*.cpp` do not compile; with exceptions on all 15 pass a syntax check (RAN, section 3). The library's rule (D2) is about what it *uses*; nothing has to be rewritten, but the flag cannot be imposed and an ImGui assertion can now unwind through our frames. Whether every such frame leaves the context consistent has not been examined.
2. **A flat source layout, or custom CMake.** The bundle's one-line helper globs `external/<lib>/<lib>/*.cpp` (`imgui_bundle_cmake/internal/add_simple_library.cmake:21-35`); we keep `src/` and `include/`. This is the same conclusion task `dist-research` reached from ImGui's side: ship a generated `imtypeset.cpp` next to `imtypeset.h`. `-ffp-contract=off` (D16) has to be passed by whoever builds it.
3. **Five functions bound by hand**, and `text_end` removed from the Python signatures (section 5). After `api-revise`, fewer.
4. **The two opaque handles need their definitions reachable by the binding.** nanobind registers a class per type and wants it complete; the bundle's answer for `RichMd::Context` is to include the library's internal header in the binding file (`external/imgui_rich_md/bindings/pybind_rich_md.cpp:18`, `generate_rich_md.py:50-59`). Ours are defined in `src/context.h`. A distribution that ships one header and one `.cpp` (task `dist`) must keep that reachable.

What would have to change, or be decided, on the bundle's side:

- Add the library the usual eight ways (section 2; `bindings/imgui_bundle/README.md` has the edits written out).
- hello_imgui loads fonts through its own callbacks and its own `ImFontConfig`s. Our faces go into the same atlas beside them; that the two do not disturb each other was NOT run (it needs the bundle's GL runner).
- Whether its author wants a second text stack next to his own. See "Next to imgui_rich_md and imgui_microtex" below.

#### Beyond the bindings: what a first-class integration includes

Judged from how the bundle treats implot and its markdown library. READ unless marked.

- **Stubs**, generated, with a hand-written preamble: the module docstring opens the library's API page, and its first paragraph is shown in the C++ view too (`docs/book/devel_docs/bindings_intro.md`, "The API pages"). Drafted: `bindings/imgui_bundle/imtypeset.pyi`.
- **An API page**, generated from the stubs by `ci_scripts/api_pages.py`; a library is one `Library(...)` entry in the table at its top (implot's is at `:86-90`). The page's structure comes from the header's standalone comments: `--- Title ---` lines are sections, which is close to how `include/imtypeset.h` already titles its parts (`// --- Fonts -----`); whether the trailing rule is accepted was not checked. Its long comments (above the widgets, inside `ImTypesetTextFlags_`) would arrive as docstrings unshortened.
- **A book page** under `docs/book/addons/` (implot's API entry points at `addons/plotting`).
- **A demo in the demo app**, in both languages, under `bindings/imgui_bundle/demos_python/` and `demos_cpp/` (`bindings_newlib.md`, step 2-c), written to the rules in `AGENTS.md` ("Demos": a module-level `gui()`, a flat `main()`). Task `demo`'s `ImTypeset::ShowDemoWindow` is the natural body of the C++ one.
- **An add-on switch.** `immapp.AddOnsParams` has `with_implot`, `with_markdown`, `with_node_editor` and so on (`bindings/imgui_bundle/immapp/immapp_cpp.pyi:54-82`); `with_markdown` also loads that library's fonts (`bindings/imgui_bundle/rich_md.pyi:13-19`). A `with_imtypeset` that creates the context and adds the faces would make the user's code the three lines of `gui()` above. It is the bundle author's code to write.
- **Fonts: none need to be brought.** The bundle ships, under `bindings/imgui_bundle/assets/fonts/`, `Roboto/Roboto-{Regular,Bold,RegularItalic,BoldItalic}.ttf`, `Inconsolata-Medium.ttf`, `DroidSans.ttf` and `latex/latinmodern-math.otf`. RAN: all seven have the same SHA-256 as the files of the same names in `tests/data/fonts/`, and the installed wheel has them too. So every role of `ImTypesetFace_` can be filled from the bundle's assets (`CodeBold` reads from Bold until it has its own, as the header says), with the faces this library's goldens were made with, through `hello_imgui.asset_file_full_path(...)` (`bindings/imgui_bundle/hello_imgui.pyi:375-378`) and `add_font_from_file_ttf`. The bundle's own font helpers (`hello_imgui.load_font`, `load_font_ttf`, `hello_imgui.pyi:900-916`) are NOT a substitute: they add a font to the atlas at a size and know nothing of our metrics or of the rewritten `cmap` the math face needs; the library has to do the adding. `hello_imgui.load_asset_file_data` (`:345`) returns the bytes as a `void*` (`:336-337`), which Python cannot hand on except as a capsule, so the path is the practical route. Note that the math face would then sit in the atlas next to MicroTeX's use of the same file, which reads it itself and not through the atlas (`imgui_microtex.pyi:72`, `init(clm_file, font_file)`).
- **Tests.** The bundle has no per-library tests (section 2). Ours stay in this repository.

#### Next to imgui_rich_md and imgui_microtex (my judgement, from their stubs and headers)

- **imgui_microtex** turns a LaTeX string into RGBA pixels and then a GPU texture, cached by (source, font size, colour) and evicted after 60 unused frames (`external/imgui_microtex/imgui_microtex/imgui_microtex.h:41-42`, `:52-79`). Its coverage is MicroTeX's, far wider than our subset, which refuses accents, matrices, alignment and macros (`include/imtypeset.h`, the comment at `ImTypesetTextFlags_Math`). Its formulas are images: one texture per formula per size per colour, outside the font atlas, re-rendered when the size changes. Ours are glyphs from the atlas in the paragraph's own draw call, broken with the line and selectable as their source. **Complement**: imtypeset for formulas in running text, MicroTeX for the LaTeX it cannot set. Neither replaces the other.
- **imgui_rich_md** is a complete markdown renderer: headings, lists, tables, images, links, code blocks with an editor, LaTeX, diagrams, folding, selectable text (`bindings/imgui_bundle/rich_md.pyi:13-19`, `:218-232`). imtypeset has no markdown at all and D32 puts it fourth in a backlog. **Not a replacement, and it should not be offered as one.**
- **As its text engine**: not possible today. Its extension points are link, image, HTML div and span, wiki-link and heading callbacks and a renderer per fenced-block language (`rich_md.pyi:487-556`, `:675-682`); none replaces how a paragraph is laid out. Justified paragraphs and in-line formulas inside rich_md would need a new hook there (text, style runs and a width in; a size out, and a draw). Its author is the bundle's author, so it is a conversation and not a fork. What works with no change on his side is the modest version: `register_fenced_block_renderer("typeset", ...)` drawing a block with imtypeset, which would make a good demo and nothing more.
- The honest pitch, then: a paragraph engine the bundle does not have (total-fit justification, real selection geometry, formulas in the line), next to a markdown renderer and a LaTeX renderer it does have.

Why the second route (a separate `.so` beside the wheel) fails, measured: **the bundle's ImGui is a fork whose structs and enum values differ from every upstream tag, and the wheel does not ship its headers.** A library compiled against upstream `v1.92.9-docking` is refused by ImGui's own layout check on `sizeof(ImGuiStyle)` (1368 against the wheel's 1372): the fork adds `float LayoutAlign` in the middle of `ImGuiStyle` and `ImGuiStyleVar_LayoutAlign` in the middle of the enum, which moves 21 `ImGuiStyleVar_` values by one (RAN, section 6). An extension compiled against stock headers that calls `PushStyleVar(ImGuiStyleVar_TabRounding, ...)` sets the wrong variable in the bundle, silently. imtypeset reads nothing from `ImGuiStyle` and pushes no style variable, so it might well run; it was not run, because a failed layout check is not something to call past.

This is not specific to imtypeset: it is the reason every library in the bundle is compiled *in* the bundle against the fork.

### What could not be determined, and what settles it

| open | settled by |
|---|---|
| Does litgen accept the header and produce what section 5 predicts? | run the generator |
| Do the seven hand-written bindings compile, the two opaque handles among them? | compile the generated file |
| Does imtypeset run inside the bundle's own ImGui (fork headers), and at what cost per frame from Python? | experiment B's `fork` variant, or the real build |
| Do our fonts coexist with hello_imgui's in one atlas under its GL backend? | a demo in the real build |
| What else the fork changes besides `ImGuiStyle` and `ImGuiStyleVar_` | reading the fork's `imgui.h` |

With network access:

```sh
# 1. The bundle's sources, fork included.
cd imgui_bundle
git submodule update --init --recursive

# 2. The generator. The real requirement line is requirements-dev.txt:3,
#    "litgen @ git+https://github.com/pthom/litgen"; nanobind is on line 5 and is
#    also a build requirement (pyproject.toml:2, "nanobind>=2.4.0").
#    docs/book/devel_docs/bindings_intro.md ("Installing the generator") sends the
#    reader to litgen's own install page; whether srcML has to be installed
#    separately was not checked.
pip install -r requirements-dev.txt

# 3. Put imtypeset in (bindings/imgui_bundle/README.md in THIS repository lists
#    the edits), then generate, build and test.
just libs_bindings imtypeset
cmake -S . -B builds/imtypeset --preset python_bindings -DPython_EXECUTABLE=$(which python)
cmake --build builds/imtypeset
just mypy && just test_pytest
```

To finish experiment B against the **installed 1.92.900 wheel** the headers must be the fork's at the commit that wheel was built from, which is not the clone's `HEAD`. READ: `git ls-tree v1.92.900 external/imgui/imgui` gives `c9a0a84f8a4ddae794a9e1acf6a93afce73d5057`; `HEAD` pins `7002a580a67259f1622200ac6257eb315d46e7c1`, an ImGui 1.93 WIP.

```sh
cd imgui_bundle && git submodule update --init external/imgui/imgui
git -C external/imgui/imgui checkout c9a0a84f8a4ddae794a9e1acf6a93afce73d5057
cd ../imtypeset
tools/experiments/build_expb.sh fork
PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B tools/experiments/exp_b_gate.py fork
PYTHONDONTWRITEBYTECODE=1 ../imgui-adv-text/.venv/bin/python -B tools/experiments/exp_b_run.py fork
```

`exp_b_run.py fork` has never run. Its driver has, end to end, in the `selfhost` variant (section 6); the half that drives frames through `imgui_bundle.imgui` (`BundleHost`) has not.

---

## 2. How the bundle adds a library (READ)

Two documents describe it. `external/_doc/Howto_Bindings.md` (137 lines) is the older one: it still names pybind11 and a `module.cpp` that no longer exists. `docs/book/devel_docs/bindings_newlib.md` is current and is what `AGENTS.md` points to. implot is the worked example below.

| step | where | implot |
|---|---|---|
| Sources vendored as a git submodule, usually of the author's fork on a branch `imgui_bundle` | `external/<lib>/<lib>/` | `external/implot/implot` (empty here); fork and branch in `external/bindings_generation/bundle_libs_tooling/all_external_libraries.py:171-178` |
| Registry entry, in `ALL_LIBS`, after `lib_imgui()` which "must be first as it declare bindings used by the next ones" | `all_external_libraries.py:236-257` | `lib_implot()` |
| litgen options | `external/<lib>/bindings/` | `litgen_options_implot.py` (96 lines): starts from imgui's options (`:16`), sets the namespace and API macro (`:17-18`), exclusions and renames |
| Generator script, the folder's one `generate_*.py`, with a `main()` | the same folder; found by name, `external_library.py:77-89` | `generate_implot.py`: `options.use_nanobind()` (`:111`), `litgen.write_generated_code_for_file(...)` (`:117-123`) |
| Generated C++ bindings, inside a hand-written frame | `external/<lib>/bindings/pybind_<lib>.cpp` | `pybind_implot.cpp` (4,801 lines), `pybind_implot_internal.cpp` |
| Generated stubs, inside a hand-written preamble | `bindings/imgui_bundle/<module>.pyi` or `<module>/__init__.pyi` | `bindings/imgui_bundle/implot/__init__.pyi`, reached by the symlink `external/implot/bindings/implot_stubs` |
| CMake target | `external/CMakeLists.txt` | `:114-124`: `add_simple_external_library_with_sources(implot implot)`, the config header force-included (`:118`), the destructor-throw warning disabled (`:119`), `IMGUI_BUNDLE_WITH_IMPLOT` defined; the option itself is `CMakeLists.txt:179` |
| List of binding sources | `external/bindings_generation/cpp/all_pybind_files.cmake`, rewritten by `autogenerate_all.py:31-55` | two lines |
| Module registration | `external/bindings_generation/cpp/pybind_imgui_bundle.cpp` | `:15-16` declarations, `:109-120` `m.def_submodule("implot")` under `#ifdef IMGUI_BUNDLE_WITH_IMPLOT` |
| Python package surface | `bindings/imgui_bundle/__init__.py`, `__init__.pyi` | `__init__.py:167-181`; `__init__.pyi:4` |
| Regenerate | `justfile:57-65` | `just libs_bindings implot` |
| Tests | `pytest.ini` (`testpaths = tests ...`), `docs/book/devel_docs/testing.md` | no per-library test: `tests/lg_imgui_bundle_test.py`, `just mypy` over the stubs, GUI tests under Xvfb in CI |
| Demo | `bindings/imgui_bundle/demos_python/`, `demos_cpp/` | `demos_implot/`; `addons.with_implot = True` in `demo_imgui_bundle.py:297` |

**The live backend is nanobind.** `generate_implot.py:111` and `litgen_options_imgui.py:1409` call `options.use_nanobind()`; `pybind_implot.cpp` and `pybind_imgui.cpp:6-18` include `<nanobind/...>`; `pyproject.toml:2` requires `nanobind>=2.4.0`; `requirements-dev.txt:7` says of pybind11 "only required for exe pybind_native_debug (not used by bindings)". The `pybind_` file names and the pybind11 includes in `external/bindings_generation/bindings_generator_template/pybind_LIBNAME.cpp` are history. All libraries are compiled into one extension module, `_imgui_bundle` (`pybind_imgui_bundle.cpp:56`), which is why a type registered by the imgui module is the same type in every other.

The bundle author's own rule for C++ APIs that will be bound (`AGENTS.md`, "Code Conventions"): "a function with several results ... returns a small struct ... No output parameters: they bind badly to Python." And for adapting a library: a litgen option first, then a wrapper header in the bundle, then a custom binding, a fork last (`bindings_newlib.md`, step 3).

## 3. How the bundle configures ImGui

`litgen_imconfig.h` at the root defines **nothing that matters**: every option in it is commented out except `IMGUI_API __declspec(dllexport)` under `_WIN32` (`:27-29`). It is ImGui's `imconfig.h` template. The configuration is in CMake and in a second header.

| setting | where (READ) | on the wheel (RAN) |
|---|---|---|
| `IMGUI_USER_CONFIG = imgui_bundle_config.h`, PUBLIC on target `imgui` | `imgui_bundle_cmake/internal/add_imgui.cmake:48-49` | |
| `IM_ASSERT` throws `std::runtime_error` when `IMGUI_BUNDLE_BUILD_PYTHON` | `imgui_bundle_cmake/imgui_bundle_config.h:38-47`; the define is PUBLIC on `imgui`, `external/CMakeLists.txt:35` | the layout check failed as a `RuntimeError` carrying `IM_ASSERT( ... )   ---   imgui.cpp:11748` |
| `ImDrawIdx` is `unsigned int` | `imgui_bundle_config.h:82` | the wheel's own check passes with `sz_drawidx = 4` |
| `ImTextureID=int` in a Python build | `add_imgui.cmake:55-57` | not observable from Python |
| `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` in a Python build | `CMakeLists.txt:147`, `add_imgui.cmake:72-74` | `sizeof(ImGuiIO)` 3080 matches upstream docking only with it defined |
| `IMGUI_BUNDLE_BUILD_PYTHON`, `IMGUI_BUNDLE_PYTHON_API` | `CMakeLists.txt:378-379` | the stub shows the fork's `#ifdef IMGUI_BUNDLE_PYTHON_API` regions |
| `IMGUI_USE_WCHAR32` | NOT in the bundle: `imgui_bundle_config.h:79` says "already defined by the use of Freetype in HelloImGui". In the separate clone `../hello_imgui` (commit `3989373`, the commit the bundle's `HEAD` pins): `hello_imgui_cmake/hello_imgui_build_lib.cmake:491`, only with FreeType | the wheel exports `ImFontAtlas::AddFontFromMemoryTTF(void*, int, float, ImFontConfig const*, unsigned int const*)`: `ImWchar` is 32 bits |
| `IMGUI_ENABLE_FREETYPE` | hello_imgui, `hello_imgui_build_lib.cmake:478`; option `HELLOIMGUI_USE_FREETYPE`, `CMakeLists.txt:254`; statically linked for wheels, `imgui_bundle_cmake/imgui_bundle_build_lib.cmake:197-200` | `io.fonts.font_loader_name` is `FreeType (2.13.3; 2.13.3)` |
| docking branch | `litgen_options_implot.py:16` (`docking_branch=True`) | the wheel exports `ImGui::DockSpace` |
| the fork's own changes | NOT READ: no source | `ImGuiStyle` is 4 bytes larger than upstream's; section 6 |

The same lines are in the tag `v1.92.900` (`imgui_bundle_config.h:44, :82`; `add_imgui.cmake:46, :53, :69-70`; `CMakeLists.txt:147, :368-369`), so the table describes the wheel and not only the clone.

Three consequences for this library.

- **The 16-bit `ImWchar` path (D15) is not what the bundle exercises.** Its Python build is 32-bit, our default.
- **FreeType 2.13.3 in the wheel, 2.12.1 on this machine's system.** D7 found no glyph-box difference across one minor version for the cases it probed; the bundle's version is one further on and was not probed.
- **`-fno-exceptions` cannot survive** (RAN). Syntax check of the 15 snapshot sources with `-DIMGUI_USER_CONFIG=<the bundle's imgui_bundle_config.h> -DIMGUI_BUNDLE_BUILD_PYTHON` and the other defines above, against the 1.92.9 docking header: with `-fno-exceptions -fno-rtti`, 14 fail (`imgui_bundle_config.h:45: error: exception handling disabled`); with `-fexceptions -fno-rtti`, 0 fail; with `-fexceptions`, 0 fail. The command is in `.concerns/bundle-research--builder-21--result.md`.

## 4. How values cross the boundary

READ from the generated `external/imgui/bindings/pybind_imgui.cpp` and `external/implot/bindings/pybind_implot.cpp` of the clone and from the options files; lines marked RAN were tried on the wheel.

| C++ | Python | evidence |
|---|---|---|
| `const char*` parameter | `str`, UTF-8 | nanobind's caster; `pybind_imgui.cpp:906` |
| `const char* text, const char* text_end = NULL` | **two unrelated strings**: `text_end` is `Optional[str]`, converted on its own, and its `c_str()` is passed as the end pointer | `pybind_imgui.cpp:906-921`; the same for `calc_text_size` (`:2803-2817`) and `ImDrawList::AddText` (`:7154-7175`). The review's finding C5 was inferred; this is the source |
| `const char* fmt, ...` | one `str`, passed through a `"%s"` | `pybind_imgui.cpp:923` on |
| `bool*`, `int*`, `float*` in-out | value in, tuple out: `checkbox(label, v) -> Tuple[bool, bool]` | `litgen_options_imgui.py:401`; `pybind_imgui.cpp:1043-1055` |
| `ImVec2`, `ImVec4` by value or const reference | a bound class on return; `ImVec2Like` (class, tuple or list) as a parameter | `litgen_options_imgui.py:176`; `generate_implot.py:112` |
| `ImU32` | `int` | `litgen_options_imgui.py:326-330` |
| `typedef int ImGuiXxxFlags` + `enum ImGuiXxxFlags_` | an `int` alias written by hand in the stub preamble, and an `enum.IntFlag` class; a member is accepted where the `int` is expected | `docs/book/devel_docs/bindings_intro.md` ("The typedefs are hand-written aliases"); `litgen_options_imgui.py:171-174`; RAN: `imgui.begin("x", None, imgui.WindowFlags_.no_saved_settings)` |
| returned pointer or reference | a Python object that does not own: `rv_policy::reference` everywhere | `litgen_options_imgui.py:383-384`; `pybind_implot.cpp:802-805`, `:4159-4160` |
| `ImDrawList*` | the bound class; `imgui.get_window_draw_list()` gives one | `pybind_imgui.cpp:525-528` |
| `ImGuiTextBuffer*` | the bound class `imgui.TextBuffer`, with `append` and `c_str` | `pybind_imgui.cpp:6574-6624`; RAN: `b = imgui.TextBuffer(); b.append("abc"); b.c_str() == "abc"` |
| `ImVector<T>` | one class per instantiation listed in the options: `ImVector_ImDrawVert`, `ImVector_ImVec4`, `ImVector_uint` (which is `ImVector<ImDrawIdx>`, by the synonym `ImDrawIdx=uint`), each iterable, indexable, with `push_back` and `data_address()` | `litgen_options_imgui.py:412-473`, `:263-272`, `:994-998`; RAN: `imgui.ImVector_ImVec4()` constructs and `push_back` works |
| pointer + count of numbers | a numpy array | `fn_params_buffer_types`, `litgen_options_implot.py:43-68` |
| pointer + count of structs | not handled: excluded and rebound by hand taking a `std::vector` | `litgen_options_imgui.py:244-247`, `:1263-1285` |
| `void*` | a `PyCapsule` named `nb_handle`, and nothing else | `pybind_imgui.cpp:7516` binds `ImFontConfig::FontData` with a plain `def_rw`; RAN, section 6 |
| C function pointer | not bound; `std::function` under `IMGUI_BUNDLE_PYTHON_API` in a fork | `bindings_newlib.md`, step 3-b; `litgen_options_imgui.py:259` |
| a struct the public header only declares | an empty class registered by hand, with the library's internal header included in the binding file so the type is complete | `RichMd::Context`: `external/imgui_rich_md/bindings/generate_rich_md.py:50-59`, `pybind_rich_md.cpp:18`, `:62`; stub `bindings/imgui_bundle/rich_md.pyi:114-118`. Also `external/imgui-node-editor/bindings/pybind_imgui_node_editor.cpp:30-31, :79` |
| `IM_ASSERT` failing | `RuntimeError` | `imgui_bundle_config.h:38-47`; RAN |

## 5. Every declaration of `include/imtypeset.h` 0.2.0, and its C form

Rewritten by task `c-api` (2026-10-10) for the header as `api-revise` left it (`docs/decisions.md` D36); the table it replaces described the header before that task. Two routes to Python now exist and this section covers both:

- **the generated route**, a litgen/nanobind binding compiled into the bundle. "as written" means imgui's and implot's options already produce a usable binding; "option" names the option; "by hand" means excluded from generation and bound in a few lines of C++, drafted in `bindings/imgui_bundle/generate_imtypeset.py`; "not bound" means excluded and not replaced. All of it is a READING: no generator ran.
- **the C ABI**, `include/imtypeset_c.h` over `src/imtypeset_c.cpp`, which is what ctypes, cffi and every non-C++ language load. The last column names the C function and how it differs. This route is RAN: `tests/test_c_api.c` is compiled as C99 and drives every function in the table (section 5.5).

### 5.1 Macros, handles, typedefs

| declaration | generated route | C ABI |
|---|---|---|
| `IMTYPESET_VERSION`, `IMTYPESET_VERSION_NUM` | by hand: `m.attr("version") = IMTYPESET_VERSION`, as `pybind_implot.cpp` does for `IMPLOT_VERSION` | `IMTYPESET_C_VERSION`, `IMTYPESET_C_VERSION_NUM`; equal by `static_assert`; also `ImTypeset_BuildInfo::Version`, `::VersionNum` at run time |
| `IMTYPESET_IMGUI_VERSION_NUM`, `_MIN`, the `#error` block, `IMTYPESET_API` | not bound. Whether litgen reads into these `#if` blocks: NOT DETERMINED, and harmless either way | none; `ImTypeset_BuildInfo::ImGuiVersionNum` says what the library was compiled against |
| `IMTYPESET_CHECKVERSION()` | by hand: called once in `py_init_module_imtypeset`, which throws (an `ImportError` for the user) with `GetLastError()->Message` if it fails | `IMTYPESET_C_CHECKVERSION(sz_imwchar, sz_imdrawvert, sz_imdrawidx)`: the three ImGui sizes are arguments because the C header includes no ImGui. A ctypes binding has no macro: it reads `ImTypeset_GetBuildInfo()` and calls `ImTypeset_DebugCheckVersionAndDataLayout` with its own `ctypes.sizeof` of each mirrored struct |
| `struct ImTypesetContext;` `struct ImTypesetParagraph;` | **by hand**: an empty class each in a custom binding, and `#include "context.h"` (where they are defined, in `src/`) in the binding file, exactly as the bundle binds `RichMd::Context` (section 4). Not compiled | `ImTypeset_Context*`, `ImTypeset_Paragraph*`: typedefs of the same two struct tags, so the pointers are the C++ handles; `c_void_p` in ctypes. Nothing has to be complete |
| the nine `typedef int ImTypesetXxx;` | by hand, one alias line each in the stub preamble (`bindings/imgui_bundle/imtypeset.pyi`) | `typedef int ImTypeset_Xxx;`, all nine |

### 5.2 Enums

All nine (`Face_`, `Align_`, `Spacing_`, `LineBreak_`, `Granularity_`, `Error_`, `TextFlags_`, `RunFlags_`, `StatusFlags_`; 43 enumerators) are **as written** on the generated route: `ImTypesetFace_` becomes `imtypeset.Face_` with `Face_.regular`, by `type_replacements` (`litgen_options_implot.py:38`, pattern changed to `ImTypeset`) and `enum_flag_remove_values_prefix` (`litgen_options_imgui.py:171`). imgui's options make every enum an `IntFlag` (`:173-174`), which for `Face_`, `Align_`, `Error_` only means they are ints. Every value is written out in the header since D36 and `ImTypesetFace_Default` is 0, so the `-1` precedent the earlier table needed is no longer needed.

C ABI: the same nine with the same enumerators, `ImTypeset_Face_Regular` for `ImTypesetFace_Regular`, each value held equal by a `static_assert` in `src/imtypeset_c.cpp` and again by `tests/test_c_api_map.cpp`, which reads both headers. One enumerator is the C ABI's own: `ImTypeset_Error_Exception = 1000`.

### 5.3 Structs

| declaration | generated route | C ABI |
|---|---|---|
| `ImTypesetStyle` (12 fields, zeroing constructor) | as written; in `class_copy__regex` (`litgen_options_implot.py:40`) so `copy.copy(style)` works | `ImTypeset_Style`, same fields; all zero is the default, so `ImTypeset_Style()` in ctypes is a default style |
| `ImTypesetRun` (6 fields, zeroing constructor) | as written, same option. `Start`/`End` are byte offsets | `ImTypeset_Run` |
| `ImTypesetTextRange` (2 ints) | as written; litgen gives a POD a keyword constructor | `ImTypeset_TextRange` |
| `ImTypesetLine` (`ImVec2 Min, Max`, `float Baseline`, 2 ints) | as written | `ImTypeset_Line`, with `ImTypeset_Vec2` for `ImVec2` |
| `ImTypesetHit` (2 ints, `bool Inside`) | as written | `ImTypeset_Hit`; `bool` is C99 `_Bool`, `c_bool` in ctypes |
| `ImTypesetStats` (10 `ImU64`) | as written | `ImTypeset_Stats`, `unsigned long long` |
| `ImTypesetErrorInfo` (`const char* Message`) | option: `member_readonly_by_type__regex` extended with `^const char\s*\*` (the bundle does it for `char*`, `litgen_options_imgui.py:309-311`). Reading copies into a `str`; the object itself aliases library storage that the next `[E]` call overwrites | `ImTypeset_ErrorInfo`, same aliasing; `ImTypeset_GetLastErrorMessage(buf, size)` and `ImTypeset_GetParagraphErrorMessage(p, buf, size)` copy the message out instead |
| `ImTypesetFormula` (`const char* Reason`, 2 `bool`) | the same option | `ImTypeset_Formula`; `ImTypeset_GetParagraphFormulaReason(p, index, buf, size)` copies the reason out |

C ABI only: `ImTypeset_Vec2`, `ImTypeset_Vec4` (proved to be `ImVec2`, `ImVec4`), `ImTypeset_BuildInfo`. Size, alignment, each field's offset and each field's type are `static_assert`ed against the C++ struct in `src/imtypeset_c.cpp`; a struct crosses the C boundary by pointer only, in both directions.

### 5.4 Functions

`(b, e)` below is a text as begin and end-or-NULL. Every C function is `ImTypeset_` + the C++ name; "status" is an `int`, non-zero for success.

| declaration | generated route | C ABI: how it differs |
|---|---|---|
| `DebugCheckVersionAndDataLayout(version, 11 sizes)` | **not bound**: what the macro calls | same parameters, `version` as `(b, e)`; status |
| `CreateContext()` | as written, given the handle type. Returns a reference the caller destroys | same; null on failure |
| `DestroyContext(ctx = NULL)` | as written (`nb::arg("ctx") = nb::none()`, as `pybind_implot.cpp:807-810`) | status; also releases this layer's scratch buffers |
| `GetCurrentContext()`, `SetCurrentContext(ctx)` | as written | same; the setter returns a status |
| `GetVersion()`, `GetImGuiVersionNum()` | as written | same |
| `GetLastError()` returning `const ImTypesetErrorInfo*` | as written: a reference to library storage, which the next `[E]` call overwrites. Copy `code` and `message` at once | `GetLastError(ImTypeset_ErrorInfo* out)` copies the struct; it also reports this layer's two own failures (a null struct, a caught exception) |
| `GetStats()` by value | as written | `GetStats(ImTypeset_Stats* out)`; status |
| `GetStyle()` returning `ImTypesetStyle&` | as written, by reference (`litgen_options_imgui.py:384`): `imtypeset.get_style().em_size = 16` writes the context's style. The reference dangles after `destroy_context()` | `GetStyle(ImTypeset_Style* out)` and **`SetStyle(const ImTypeset_Style*)`**: a copy out and a copy in, nothing to dangle |
| `SetTextCacheLimits(int, int)` | as written | status |
| `AddFontFromFileTTF(face, filename, face_index = 0, font_cfg = NULL)` | as written. `font_cfg` is `Optional[ImFontConfig]`; the `ImFont*` comes back as a reference the atlas owns | `filename` as `(b, e)`; `struct ImFont*`, `const struct ImFontConfig*` passed through and never looked into |
| `AddFontFromMemoryTTF(face, const void* font_data, int font_data_size, ...)` | **by hand**, taking `bytes`. As written it would take a capsule (section 4). The library copies the bytes | same parameters: pointer + length is already the C form |
| `RemoveFont(face)`, `GetFont(face)` | as written | same; status for the first |
| `TextUnformatted(text, text_end = NULL, flags, wrap_width, style = NULL)` | **option**: `text_end` dropped by name. As written the binding would offer a second string and the library would hash and copy `text_end - text` bytes between two unrelated buffers | `(b, e)` kept: it is how a language with counted strings avoids terminating one. `style` nullable |
| `Text(fmt, ...)` | as written: one `str` passed through `"%s"` (`pybind_imgui.cpp:923` on) | same, for a host written in C; a binding calls `TextUnformatted` |
| `TextV(fmt, va_list)` | **not bound**: a `va_list` | same, for a host written in C |
| `TextWithID(str_id, text, text_end = NULL, ...)` | option: `text_end` dropped | `str_id` and `text` both `(b, e)` |
| `SelectableText(str_id, text, text_end, flags, wrap_width, style, ImTypesetTextRange* selection = NULL)` | **by hand**: `selection` is a struct read at entry and written before return. Rebound taking `Optional[TextRange]` and returning `(bool, TextRange)`, the bundle's value-in, tuple-out rule for `bool*` (`litgen_options_imgui.py:401`) | both strings `(b, e)`; `selection` stays an in-out pointer, nullable |
| `CalcTextSize(text, text_end = NULL, wrap_width, flags, style)` returning `ImVec2` | option: `text_end` dropped | `(…, ImTypeset_Vec2* out_size)`; the status is the `[E]` outcome, which the C++ caller reads from `GetLastError()` |
| `CreateParagraph()`, `DestroyParagraph(p)` | as written, given the handle type. Not tied to Python object lifetime | same; status for the second |
| `SetParagraphText(p, text, text_end, const ImTypesetRun* runs, int runs_count, flags)` | **by hand**, taking `str` and `List[Run]`: `text_end`, and a pointer + count of structs | same parameters: already the C form |
| `LayoutParagraph(p, width, style = NULL)` returning `bool` | as written | status |
| `GetParagraphError(p)` | as written; the object aliases the paragraph's storage | `(p, ImTypeset_ErrorInfo* out)`, and `GetParagraphErrorMessage` |
| `GetParagraphStatus(p)`, `GetParagraphFormulaCount(p)`, `GetParagraphLineCount(p)` | as written | same |
| `GetParagraphFormula(p, index)` by value | as written (it filled a pointer before D36, and was by hand) | `(p, index, ImTypeset_Formula* out)`, and `GetParagraphFormulaReason` |
| `GetParagraphSize(p)`, `GetParagraphLine(p, line)`, `GetParagraphCaretRect(p, byte_offset)` by value | as written | each through an `out` pointer; status |
| `DrawParagraph(p, ImDrawList*, const ImVec2& pos, const ImTypesetTextRange* selection = NULL)` | as written: `pos` is `ImVec2Like`, `selection` is `Optional[TextRange]` | `struct ImDrawList*` passed through; `pos` by pointer; status |
| `DrawParagraphToBuffers(p, pos, clip_rect, ImVector<ImDrawVert>*, ImVector<ImDrawIdx>*, selection)` | as written: the caller passes `imgui.ImVector_ImDrawVert()` and `imgui.ImVector_uint()` and reads them back through `data_address()` | `(p, pos, clip, void* vertices, int capacity, void* indices, int capacity, selection, int* out_vertices, int* out_indices)`: two caller arrays; both counts are always the full counts, so a call with capacity 0 asks the size. Element sizes and `ImDrawVert`'s field offsets are in `ImTypeset_BuildInfo`. NOT appending: indices start at 0 |
| `HitTestParagraph(p, local_pos)` returning `ImTypesetHit`, `SelectParagraphRange(p, anchor, active, granularity)` returning `ImTypesetTextRange` | as written, by value | points by pointer, answer through `out`; status |
| `GetParagraphRangeRects(p, range, ImVector<ImVec4>* out_rects)` | **by hand**, returning `List[ImVec4]`. As written it works with an `imgui.ImVector_ImVec4()` | `(p, range, ImTypeset_Vec4* out_rects, int capacity)`, returning the count there is |
| `AppendParagraphRangeText(p, range, ImGuiTextBuffer* out)` | **by hand**, taking the text so far and returning a `str`. As written it works with an `imgui.TextBuffer()` | `(p, range, int after_text, char* buf, int buf_size)`, returning the length there is. The C++ function decides its `"\n\n"` separator by whether the buffer already holds text; here the caller says so with `after_text` |
| `TextCodepointToByteOffset(text, text_end, n)`, `TextByteToCodepointOffset(text, text_end, n)` | **by hand**, two lines each: `text_end` has no default value and is not the last parameter, so it cannot be dropped by name (`fn_params_exclude_names__regex` is for defaulted parameters) | same |
| `ParagraphCodepointToByteOffset(p, n)`, `ParagraphByteToCodepointOffset(p, n)` | as written | same |

Count on the generated route, of the 62 entries that are a binding or a decision not to make one (9 enums, 8 structs, 2 handles, 43 functions): **46 as written** (9 enums, 6 structs, 31 functions), **5 by one option** (`ErrorInfo`, `Formula`, `TextUnformatted`, `TextWithID`, `CalcTextSize`), **9 by hand** (the two handles; `AddFontFromMemoryTTF`, `SetParagraphText`, `SelectableText`, `GetParagraphRangeRects`, `AppendParagraphRangeText`, and the two `Text…Offset` functions), **2 not bound** (`DebugCheckVersionAndDataLayout`, `TextV`). Most of the functions counted "as written" take or return a handle, so they stand on the handle binding compiling, which nobody has seen (section 9).

Against the table this replaces (37 / 5 / 7 of 49): `GetParagraphFormula` left the by-hand list as predicted; `SelectableText` and the two `Text…Offset` helpers joined it. The last two are the one place the revised header is harder on a generator than it had to be: giving `TextCodepointToByteOffset(text, codepoint_offset, text_end = NULL)` the order `TextUnformatted` has would let the existing option drop `text_end`. That is a change to a public declaration and is not this task's to make; it is in `.concerns/c-api--builder--result.md` as an open question.

### 5.5 The C ABI as a binding's seam (RAN)

`include/imtypeset_c.h` states its own rules at the top; the ones a binding author needs first:

- **Call `ImTypeset_GetBuildInfo()` before anything else.** It takes a pointer and a size, copies at most that much, needs no context and cannot fail. It answers the library's version, the ImGui it was compiled against, `sizeof(ImWchar)`, `sizeof(ImDrawVert)`, `sizeof(ImDrawIdx)`, the offsets of `ImDrawVert`'s three fields, whether ImGui was compiled with FreeType, whether this layer catches C++ exceptions, and the sizes of the eight mirrored structs. A binding compares these with what it shares ImGui with and refuses to go on if they differ. It does NOT cover `ImGuiIO`, `ImGuiStyle` or anything else of ImGui's own layout: that is `ImGui::DebugCheckVersionAndDataLayout`'s job, and section 6 is what happens when it is skipped.
- **Text is `(begin, end)`** with `end` null for a zero-terminated string: file names and string IDs too. ctypes passes `bytes` and `None`.
- **Nothing is allocated on one side and freed on the other.** Strings and arrays come back in the caller's buffer with a capacity; the return value is how much there was, as `snprintf`'s is, so capacity 0 asks the size. A string cut to fit is cut at a UTF-8 boundary.
- **No exception leaves it.** Compiled with exceptions enabled (the bundle's way: its `IM_ASSERT` throws), every entry catches, fails as its return type fails, and `ImTypeset_GetLastError()` answers `ImTypeset_Error_Exception` with the exception's message. Compiled without, nothing in the library throws and `ImTypeset_BuildInfo::CatchesExceptions` is 0.
- **Handles are the C++ handles**, so a process may use both headers on one context.

What was run, and in which builds, is in `.concerns/c-api--builder--result.md`. What was NOT: no Python has loaded this header's library yet (task `py-host`); the `selfhost` control of section 6 used an earlier, private shim (`tools/experiments/`), not this ABI.

**For whoever revises the header next**, in the order a binding cares:

1. Keep `(text, text_end)`, with `text_end` defaulted and after the text, everywhere: the generator drops it by name and the C ABI keeps it. The two `Text…Offset` functions are the exception today.
2. An in-out struct pointer (`SelectableText`'s `selection`) is by hand on every route. One is fine; do not add a second casually.
3. `const void* + int` and `const ImTypesetRun* + int` are right for C and C++ and always by hand in a generated binding.
4. A function that answers a struct should return it by value, as D36 made them all: the generated route takes it as written and the C ABI turns it into an `out` pointer mechanically.
5. The two handle types can stay opaque. A nanobind binding must see their definitions, so `src/context.h` has to ship with whatever form the library is distributed in (task `dist`); the C ABI needs nothing.

---

## 6. The experiments

Everything here was RAN in this session. Scripts are in `tools/experiments/`, outputs in `build/bundle-research/`. Native code was loaded in subprocesses; none crashed. The library sources are a snapshot taken once (`build/bundle-research/snap/`, timestamp in `SNAPSHOT_TAKEN`) while `src/` was being edited by others.

### A. Font bytes through `ImFontConfig.font_data`

`tools/experiments/exp_a_font_bytes.py`; output `exp_a_roboto.jsonl`, `expa/run_*.json`.

**It works, through exactly one kind of object.** `ImFontConfig.font_data` is a `void*` member bound with a plain `def_rw`. Assigning to it:

| object | result |
|---|---|
| `bytes`, `bytearray`, `memoryview`, numpy `uint8` array | `TypeError`: "supported: (self, arg: typing_extensions.CapsuleType, /)" |
| ctypes array, `ctypes.c_void_p`, an `int` address | the same `TypeError` |
| `None` | the same `TypeError` |
| a `PyCapsule` with no name | the same `TypeError` |
| a `PyCapsule` named `nb_handle` holding the address of a buffer the caller keeps alive, `font_data_owned_by_atlas = False` | accepted. `io.fonts.add_font(cfg)` returns a font; after three headless frames `U+0041` is found, flagged visible, and has 49 inked texels in the atlas texture |
| the same capsule over a block from `ImGui::MemAlloc` (taken from the module with ctypes), `font_data_owned_by_atlas = True` | the same result; `destroy_context()` frees it without complaint |

So the binding does nothing with the object but unwrap a pointer: it takes no reference, copies nothing, and does not know the size. The capsule is made with `ctypes.pythonapi.PyCapsule_New(address, b"nb_handle", None)`, and the caller must keep both the buffer and the name bytes alive for the life of the atlas. It is a real route and not a comfortable one. The name is nanobind's own convention for `void*`; it was found by trying, it is not in the stub (which says `font_data: Any`, wheel `imgui/__init__.pyi:11038`).

**The font this library rebuilds works in the bundle's atlas.** `tools/experiments/expa_write_atlas_font.cpp` (standalone, linked with this repository's ImGui, nothing of the bundle) wrote what `build_atlas_font` produces for `tests/data/fonts/latinmodern-math.otf` at 32-bit `ImWchar`: 732,548 bytes, 227 minted codepoints `U+FFF1B..U+FFFFD`. Handed to the wheel through the capsule and asked for at 16 px:

| font | minted codepoints found | flagged visible | with inked texels |
|---|---|---|---|
| rebuilt in memory by `src/font_loader` | 227 of 227 | 227 | 227 (fewest: 22 texels) |
| the original file, as a control | 0 of 227 | 0 | 0 |

`U+0041` and `U+222B` are found in both. The loader was the bundle's FreeType 2.13.3.

**What this refutes.** `docs/decisions.md` D27 left open whether the design note's §2 ("structurally unreachable from Python") holds for the font path, pending §11's open item. It does not: the capability a custom `ImFontLoader` was to provide reaches the unmodified wheel as bytes. What stays out of Python's reach is *producing* those bytes without native code or fontTools, and everything after the font (layout, drawing).

### B. A call from Python against the installed wheel

`tools/experiments/build_expb.sh`, `expb_shim.cpp`, `exp_b_gate.py`, `exp_b_run.py`, `exp_b_fork_diff.py`.

The library was built as `libimtypeset_expb.so` from the snapshot with all ImGui symbols undefined: 40 of them, every one exported by the wheel. A small C shim over the public API catches the bundle's throwing `IM_ASSERT`.

**Loading** (`exp_b_gate_*.jsonl`):

| how | result |
|---|---|
| `ctypes.CDLL(lib)`, bundle not imported | fails: `undefined symbol: _ZN15ImGuiTextBuffer11EmptyStringE` |
| `import imgui_bundle`, then `ctypes.CDLL(lib)` | fails the same way: Python loads extension modules `RTLD_LOCAL` |
| `import imgui_bundle`, `ctypes.CDLL(<_imgui_bundle...so>, RTLD_GLOBAL)`, then `ctypes.CDLL(lib)` | loads |
| library linked with the wheel's `.so` as `DT_NEEDED` plus an rpath | loads, with or without the import |

**The layout check, which is where the experiment stopped.** The first call across the boundary was ImGui's own `DebugCheckVersionAndDataLayout`, executed by the wheel's ImGui on the library's compile-time sizes.

| headers the library was compiled against | result |
|---|---|
| `.deps/imgui`, v1.92.9 master, with the bundle's defines | **mismatch**: `IM_ASSERT( sz_io == sizeof(ImGuiIO) && "Mismatched struct layout!" )   ---   imgui.cpp:11747`. Ours 3016. The wheel is the docking branch |
| upstream tag `v1.92.9-docking` (commit `9b4eb24c`, extracted from `../imgui`'s git objects into `build/bundle-research/imgui-1.92.9-docking/`), same defines | version string and `ImGuiIO` (3080) pass; **mismatch**: `IM_ASSERT( sz_style == sizeof(ImGuiStyle) && ... )   ---   imgui.cpp:11748`. Ours 1368 |

The second set was not in the task's specification, which named `.deps/imgui` only. It was tried because the first failure was a known difference (master against docking) and not yet an answer about the bundle. The second failure is the answer. **Nothing else was called through the boundary in either build**: no context, no font, no frame. `exp_b_run.py docking` and `deps` print the gate result and `"stopped"`.

No header was edited to make the check pass. One could be: the diagnosis below says where the four bytes are. That would be fitting the header to the six numbers the check looks at, with every other struct of the fork unseen, and the check exists to prevent exactly that.

**The diagnosis**, without crossing the boundary (`exp_b_fork_diff.py`, output `exp_b_fork_diff.json`):

- Asked through its own binding with candidate sizes, the wheel's ImGui has `sizeof(ImGuiIO) = 3080` and `sizeof(ImGuiStyle) = 1372`.
- The stub lists `ImGuiStyle`'s members with their C++ declarations. Against upstream `v1.92.9-docking` it has one more: `float LayoutAlign;`, between `GrabRounding` and `LogSliderDeadzone` (wheel `imgui/__init__.pyi:8620`).
- `ImGuiStyleVar_` has 47 values against upstream's 46: `ImGuiStyleVar_LayoutAlign = 25` is inserted and the 21 values after it are one higher than upstream's (`TabRounding` 26 against 25, and so on). `ImGuiCol_` (64 values) and `ImGuiBackendFlags_` (10) are identical.

**The control: the same driver with no bundle** (`exp_b_run.py selfhost`, output `exp_b_run_selfhost.json`, image `build/bundle-research/selfhost/frame.png`). The library with this repository's own ImGui compiled in (`.deps/imgui`, stb_truetype, `-O2`, assertions on), every call from Python through ctypes: contexts created, six faces added through `AddFontFromFileTTF`, headless frames, `TextJustified` of a 400-character paragraph with two formulas between `Begin` and `End`, the paragraph path (`CreateParagraph` ... `AppendParagraphSelectionText`), a soft-raster screenshot. All of it worked; the copy of a full selection equals the source, formulas included.

| measured from Python, one machine, median of 300 frames | |
|---|---|
| first `TextJustified` call (text build, total-fit layout, glyphs baked) | 1.36 ms |
| the call in steady state (0 layouts, 0 text builds over the 300 frames) | 10.0 µs, minimum 9.9 µs |
| whole frame (`NewFrame`, `Begin`, the call, `End`, `Render`), with / without the call | 15.4 µs / 5.2 µs |
| vertices in the frame, with / without | 1,132 / 104 |

The 10 µs includes the ctypes call itself, which was not separated out. It is a statement about this library called from Python, not about the bundle: a nanobind call has a different overhead and the bundle's ImGui is a different build.

**Superseded for the probe-gated case by task `bundle-route` (2026-10-10).** What follows in this paragraph is what this experiment concluded, and it stopped at ImGui's failed check. `bundle-route` went on from there without editing a header: it enumerated every ImGui field the library touches, probes each against the live wheel at import (`python/imtypeset/_bundle.py`, 22 probe rows; a mutation test of the probe leaves no shifted offset undetected), and accepts a library built against upstream's docking tag of the same version when the probe passes and `sizeof(ImGuiStyle)` is the only size ImGui's own check refuses. On that basis the sidecar library runs inside both stock wheels on this machine (imgui_bundle 1.92.801 and 1.92.900), linked with the wheel's module as `DT_NEEDED`, with no `RTLD_GLOBAL`. It is still one library per interpreter, Linux x86-64 only, and a library that passes ImGui's own check still needs the fork's headers: `docs/distribution.md` section 1, `.concerns/bundle-route--builder--result.md`.

**This is an experiment on one Linux machine against one wheel. It is not a distribution route**, and its result is that it could not be one: a separately built extension would have to be compiled against the fork's headers at the exact commit of each wheel, with the wheel's defines, for each platform, and loaded with `RTLD_GLOBAL` tricks, and the wheel ships no headers.

## 7. The widget's cache from Python (review finding A1, measured)

RAN in the `selfhost` control, on the snapshot, where `Text()` still finds its layout by the text pointer. **The header no longer works this way** (D36: content-keyed, `tests/test_api.cpp` holds one layout for forty frames of fresh buffers); this table is the measurement that made the case and has not been taken again from Python.

| what Python passes each frame | layouts in 100 frames | paragraphs cached after | call, median |
|---|---|---|---|
| the same `bytes` object | 0 | | 10 µs |
| an equal `bytes` in a new object, earlier ones still alive | 100 | 101 | 101 µs |
| an equal `bytes` in a new object, the earlier one freed | 0 | | 10 µs |

The third row is the allocator handing back the same address, which is luck. The reviewer inferred this for bindings from memory; here it is observed for ctypes. For a nanobind binding the string a `str` is converted from was NOT observed; the mechanism is the same wherever the pointer differs between frames, and any text formatted per frame is such a case. R1 (key on content) removes all three rows' difference.

## 8. What a Python application that wants documents would be missing

Short, because `docs/decisions.md` D32 makes imtypeset the product and no API is added to keep the prototype's views alive; task `advtext-map` writes the full map (`docs/advtext-migration.md`). READ, by definitions and signatures and not line by line: `../imgui-adv-text/advtext/ui/bare_view.py` (2,529 lines) and `advtext/backends/imgui_draw.py` (1,225 lines).

Everything the prototype's view does **inside one paragraph** has a counterpart in the header: `draw_runs(draw_list, runs, origin, ...)` (`imgui_draw.py:587-600`) is `DrawParagraph`; per-run ink is `ImTypesetRun::Color`; `pointed()` and `granularity_for()` (`bare_view.py:1046`, `:2510`) are `HitTestParagraph` and `SelectParagraphRange`; `span_rects()` (`:1235`) is `GetParagraphSelectionRects`; `selected_text()` (`:855`) is `AppendParagraphSelectionText`.

What a document needs and the header does not have: paragraphs stacked into one object with one layout cache per width (`BareView.layout_for`, `:617`, over `advtext/ui/units.py`); one selection across paragraphs and its survival of a reflow (`:794`, `:2290-2315`); links, as a property of a run and as "which link is under this point" (`:1113-1123`, `:2039-2098`); a fill colour behind a run (`draw_runs`' `fills`); recolouring a run without rebuilding the paragraph (`set_colours`, `:1088`); line rectangles and baselines for carets and scroll-to (`set_lines`, `:1432`; ruling R10 adds these); a widget hosted inside the text (`hosted`, `:697-784`); and keeping the reading position while the width changes (`_held_in_place`, `:1568`). The first of these is what tasks `oracle-blocks` and `blocks` have started; without it every Python view re-implements stacking, culling by paragraph and selection joining per frame, which is the cost the native library exists to remove.

## 9. Not verified

- litgen, nanobind and the bundle's build: none ran. Everything in `bindings/imgui_bundle/` is unrun and uncompiled; section 5 is a reading. In particular nobody has seen nanobind accept `ImTypesetContext` with `src/context.h` included; the claim rests on the bundle doing the same for `RichMd::Context`.
- The bundle's `api_pages.py`, its book and its demo app: read for where a library is listed, not run.
- The bundle's ImGui fork source. What it changes beyond `ImGuiStyle` and `ImGuiStyleVar_` is unknown; the size probes written for `ImFontConfig`, `ImDrawList`, `ImFont`, `ImFontBaked` and `ImGuiIO` (`expb_probe_sizes`) did not run against the wheel, because they come after the check that failed.
- imtypeset inside the bundle's ImGui: not run in any form.
- `exp_b_run.py`'s `BundleHost` (frames through `imgui_bundle.imgui`) and `build_expb.sh fork`: never executed.
- The bundle's own offscreen or GL path. The one image is from this repository's soft rasteriser in the control run.
- The cost of a nanobind call; the ctypes overhead inside the 10 µs.
- Whether a nanobind `str` argument keeps its pointer between frames.
- FreeType 2.13.3 against 2.12.1 for glyph boxes; Windows and macOS wheels; Pyodide.
- hello_imgui's font loading next to ours; its renderer backends announcing `ImGuiBackendFlags_RendererHasTextures`.
- Exception safety of the library when an ImGui assertion unwinds through it: task `harden-2`. What the C ABI does with an exception that reaches it is tested (section 5.5); what state the context is in afterwards is not this document's.
- Sections 1 to 4 and 6 to 8 are of a snapshot taken before `api-revise`; section 5 and the sketch in section 1 were brought to the 0.2.0 header by task `c-api`, by reading, and the drafts in `bindings/imgui_bundle/` with them. None of that was generated or compiled; the seven hand-written lambda bodies pass `g++ -fsyntax-only` against the header with `std::string` standing in for `nb::bytes`.
- No Python has loaded a library exporting `include/imtypeset_c.h` (task `py-host`).
