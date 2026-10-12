"""The import gate for a library hosted inside Dear ImGui Bundle's wheel.

An installed wheel ships no headers, and its Dear ImGui is a fork. So the
question "was this library compiled against the ImGui that is in the process?"
is answered twice:

  1. Dear ImGui's own IMGUI_CHECKVERSION(): the library's version string and six
     struct sizes, judged by the wheel's ImGui. It passes when the library was
     compiled against the fork's headers at the wheel's commit.
  2. A FIELD PROBE: for every Dear ImGui field the library reads or writes
     directly, the offset the library was compiled with is compared with what
     the wheel does to a live object. Each value is set or read through the
     wheel's own binding (compiled from the wheel's headers) or produced by one
     of the wheel's exported functions, and then read from memory at the
     library's offset. This runs in a context of its own that is destroyed
     before the function returns.

`check()` returns a report and `accepted()` is the policy:

  - the probe must pass whole, always;
  - ImGui's check must pass too, unless the library says it was built against
    STAND-IN headers ("stand-in:..." from ImTypesetBundle_Headers) and the only
    thing ImGui's check refuses is sizeof(ImGuiStyle), a struct of which the
    library reads one field that the probe covers. IMTYPESET_BUNDLE_STRICT=1
    removes that allowance.

WHERE THE PROBE RUNS. A probe at a wrong offset can read through a wild
pointer, and then the process dies instead of raising. So the probe is never
run in the application's process. It runs in a child interpreter, when a
library is built (python/host/build_bundle_library.sh, tools/wheel), and its
verdict is written down as an ATTESTATION: the sha256 of the library and of the
wheel's module it was run against, a sha256 of what the loader maps of each,
and every row. `import imtypeset` then compares content (`precheck()`), which
cannot fault, runs the checks that read nothing through an offset (`gate()`),
starts no interpreter and writes nothing. docs/sidecar.md, section 9, says what
that trusts.

TWO ROUTES, told apart by what is in the package's own directory and by
nothing else (`_capi.library_route`):

  installed    the directory holds `_attestation.json` or a library, as a
               wheel puts them. The library is the one in the directory, the
               gate is the attestation, and NO ENVIRONMENT VARIABLE CHANGES
               EITHER: IMTYPESET_LIBRARY, IMTYPESET_PYTHON, IMTYPESET_GATE and
               IMTYPESET_PRELOAD_BUNDLE are not read (`ignored_environment()`
               names the ones that were set, for host_info()). This is every
               shipped application, frozen or not.
  development  the directory holds neither: a source checkout.
               IMTYPESET_LIBRARY names the library, and the other variables
               are honoured. Nothing is mapped into this process before a
               content check or a child's verdict: with an attestation beside
               it the library is held to it by content first; without one it
               is loaded and probed in a CHILD interpreter first
               (`probe_in_child`), and where a child cannot be started
               (`child_interpreter`) the import is refused, kind `unchecked`.

This file imports nothing of its package, so that a child interpreter and the
tools can load it by path.
"""

from __future__ import annotations

import ctypes as C
import glob
import os
import re
import struct

# The variables that choose a library or a gate. They are a developer's: an
# installed distribution reads none of them.
OVERRIDES = (
    "IMTYPESET_LIBRARY", "IMTYPESET_PYTHON", "IMTYPESET_GATE", "IMTYPESET_PRELOAD_BUNDLE",
)


def ignored_environment(route: str) -> list:
    """The override variables that are set and were NOT read, because the
    package is an installed distribution."""
    if route != "installed":
        return []
    return [name for name in OVERRIDES if os.environ.get(name, "") != ""]

# Mangled names (Itanium ABI) of the wheel's functions the probe calls itself.
_GET_IO = "_ZN5ImGui5GetIOEv"
_GET_STYLE = "_ZN5ImGui8GetStyleEv"
_GET_WINDOW_DRAW_LIST = "_ZN5ImGui17GetWindowDrawListEv"
_GET_FONT_BAKED = "_ZN6ImFont12GetFontBakedEff"
_FIND_GLYPH = "_ZN11ImFontBaked9FindGlyphEj"
_PRIM_RESERVE = "_ZN10ImDrawList11PrimReserveEii"
_PRIM_UNRESERVE = "_ZN10ImDrawList13PrimUnreserveEii"
_FONT_CONFIG_CTOR = "_ZN12ImFontConfigC1Ev"
_ADD_FONT_FROM_MEMORY = "_ZN11ImFontAtlas20AddFontFromMemoryTTFEPvifPK12ImFontConfigPKj"
_MEM_ALLOC = "_ZN5ImGui8MemAllocEm"


def _facts(lib) -> dict:
    lib.ImTypesetBundle_LayoutFacts.restype = C.c_int
    lib.ImTypesetBundle_LayoutFacts.argtypes = [C.c_char_p, C.c_int]
    need = lib.ImTypesetBundle_LayoutFacts(None, 0)
    text = C.create_string_buffer(need + 1)
    lib.ImTypesetBundle_LayoutFacts(text, need + 1)
    facts = {}
    for line in text.value.decode("ascii").splitlines():
        name, offset, size = line.split()
        facts[name] = (int(offset), int(size))
    return facts


def _constants(lib) -> dict:
    lib.ImTypesetBundle_Constants.restype = C.c_int
    lib.ImTypesetBundle_Constants.argtypes = [C.c_char_p, C.c_int]
    need = lib.ImTypesetBundle_Constants(None, 0)
    text = C.create_string_buffer(need + 1)
    lib.ImTypesetBundle_Constants(text, need + 1)
    return {name: int(value) for name, value in (l.split() for l in text.value.decode("ascii").splitlines())}


def _imgui_check(lib, sz_io: int = -1, sz_style: int = -1):
    lib.ImTypesetBundle_CheckSizes.restype = C.c_int
    lib.ImTypesetBundle_CheckSizes.argtypes = [C.c_long, C.c_long, C.c_char_p, C.c_int]
    message = C.create_string_buffer(1024)
    ok = lib.ImTypesetBundle_CheckSizes(sz_io, sz_style, message, len(message)) == 1
    return ok, message.value.decode("utf-8", "replace")


class _Memory:
    """Reads of the process's own memory at an address plus a compiled offset."""

    def __init__(self, facts: dict):
        self.facts = facts

    def at(self, base: int, name: str) -> int:
        return base + self.facts[name][0]

    def read(self, base: int, name: str, fmt: str, extra: int = 0):
        return struct.unpack(fmt, C.string_at(self.at(base, name) + extra, struct.calcsize(fmt)))[0]


def _enumerators(lib, imgui):
    """("enumerators", passed, detail): the enumerators the library names, as it
    was compiled, against the wheel's binding (generated from the wheel's
    headers). The library passes them to the wheel's functions by value. Reads
    no memory through an offset, so it cannot fault. Never raises."""
    try:
        own = _constants(lib)
        theirs = {
            "ImGuiCol_Text": imgui.Col_.text,
            "ImGuiCol_TextSelectedBg": imgui.Col_.text_selected_bg,
            "ImGuiCol_Separator": imgui.Col_.separator,
            "ImGuiBackendFlags_RendererHasTextures": imgui.BackendFlags_.renderer_has_textures,
            "ImGuiKey_A": imgui.Key.a,
            "ImGuiKey_C": imgui.Key.c,
            "ImGuiMod_Ctrl": imgui.Key.mod_ctrl,
            "ImGuiMouseCursor_TextInput": imgui.MouseCursor_.text_input,
            "ImGuiMouseButton_Left": imgui.MouseButton_.left,
        }
        differing = [
            f"{name}: library {own[name]}, wheel {int(theirs.get(name, -1))}"
            for name in own
            if name not in theirs or own[name] != int(theirs[name])
        ]
        return "enumerators", bool(own) and not differing, "; ".join(differing)
    except Exception as exc:
        return "enumerators", False, f"{type(exc).__name__}: {exc}"


def _probe(lib, facts: dict) -> list:
    """[(name, passed, detail)] for every field the library touches. Never raises."""
    import imgui_bundle
    from imgui_bundle import imgui

    wheel = C.CDLL(module_file())

    def function(name, restype, *argtypes):
        f = getattr(wheel, name)
        f.restype = restype
        f.argtypes = list(argtypes)
        return f

    mem = _Memory(facts)
    results = []

    def record(name, passed, detail=""):
        results.append((name, bool(passed), detail))

    def near(a, b):
        return abs(a - b) <= 1e-6 * max(1.0, abs(a), abs(b))

    record(*_enumerators(lib, imgui))

    previous = imgui.get_current_context()
    context = imgui.create_context()
    imgui.set_current_context(context)
    try:
        io = imgui.get_io()
        io_p = function(_GET_IO, C.c_void_p)()

        # ImGuiIO::BackendFlags: two values through the binding, read at the offset.
        seen = []
        for value in (
            imgui.BackendFlags_.renderer_has_vtx_offset | imgui.BackendFlags_.has_gamepad,
            imgui.BackendFlags_.renderer_has_textures,
        ):
            io.backend_flags = value
            seen.append(mem.read(io_p, "ImGuiIO.BackendFlags", "<i") == int(value))
        record("ImGuiIO.BackendFlags", all(seen))

        # ImGuiIO::DisplayFramebufferScale, which the draw step reads for the
        # display's density: two values through the binding, x and y apart.
        seen = []
        for value in (2.5, 1.25):
            io.display_framebuffer_scale = imgui.ImVec2(value, value * 3.0)
            seen.append(
                near(mem.read(io_p, "ImGuiIO.DisplayFramebufferScale", "<f"), value)
                and near(mem.read(io_p, "ImGuiIO.DisplayFramebufferScale", "<f", 4), value * 3.0)
            )
        io.display_framebuffer_scale = imgui.ImVec2(1.0, 1.0)
        record("ImGuiIO.DisplayFramebufferScale", all(seen))

        # ImGuiIO::BackendRendererName, which the binding can read and not write:
        # null in a context no backend has touched; then a string of the probe's
        # own is stored at the library's offset (inside an ImGuiIO whose size
        # ImGui's check has confirmed, in a context about to be destroyed) and
        # the binding must read it back.
        label = C.create_string_buffer(b"imtypeset-probe")
        name_at = mem.at(io_p, "ImGuiIO.BackendRendererName")
        was_null = mem.read(io_p, "ImGuiIO.BackendRendererName", "<Q") == 0
        seen = io.backend_renderer_name is None
        if was_null and seen:
            C.memmove(name_at, struct.pack("<Q", C.addressof(label)), 8)
            seen = io.backend_renderer_name == "imtypeset-probe"
            C.memmove(name_at, struct.pack("<Q", 0), 8)
        record("ImGuiIO.BackendRendererName", was_null and seen)

        # ImGuiStyle::Alpha.
        style = imgui.get_style()
        style_p = function(_GET_STYLE, C.c_void_p)()
        seen = []
        for value in (0.4321, 0.8765):
            style.alpha = value
            seen.append(near(mem.read(style_p, "ImGuiStyle.Alpha", "<f"), value))
        style.alpha = 1.0
        record("ImGuiStyle.Alpha", all(seen))

        # ImGuiIO::Fonts, ImFontAtlas::Fonts and Locked, ImFont::FontId,
        # ImGuiIO::FontDefault: the pointer at Fonts' offset is taken to be the
        # atlas, and what is found through it must follow the binding.
        atlas = io.fonts
        atlas_p = mem.read(io_p, "ImGuiIO.Fonts", "<Q")
        vector = "ImFontAtlas.Fonts"

        def fonts_size():
            return mem.read(atlas_p, vector, "<i")

        def font_at(i):
            data = mem.read(atlas_p, vector, "<Q", 8)
            return struct.unpack("<Q", C.string_at(data + 8 * i, 8))[0]

        counts = [fonts_size() == len(atlas.fonts)]
        first = atlas.add_font_default()
        counts.append(fonts_size() == len(atlas.fonts) == 1)
        second = atlas.add_font_default()
        counts.append(fonts_size() == len(atlas.fonts) == 2)
        record("ImGuiIO.Fonts", atlas_p != 0 and all(counts))
        record("ImFontAtlas.Fonts", all(counts))

        ids = [
            mem.read(font_at(i), "ImFont.FontId", "<I") == font.font_id
            for i, font in enumerate((first, second))
        ]
        record("ImFont.FontId", all(ids) and first.font_id != second.font_id)

        seen = []
        for i, font in ((1, second), (0, first)):
            io.font_default = font
            seen.append(mem.read(io_p, "ImGuiIO.FontDefault", "<Q") == font_at(i))
        record("ImGuiIO.FontDefault", all(seen))

        seen = []
        for value in (True, False):
            atlas.locked = value
            seen.append(mem.read(atlas_p, "ImFontAtlas.Locked", "<B") == int(value))
        record("ImFontAtlas.Locked", all(seen))

        # ImFontConfig, which the library copies by value, fills in, and hands to
        # ImFontAtlas::AddFontFromMemoryTTF. Its size, the constructor's
        # defaults at the library's offsets, and a font added the library's way.
        lib.ImTypesetBundle_ProbeFontConfigSize.restype = C.c_int
        wheel_size = lib.ImTypesetBundle_ProbeFontConfigSize()
        own_size = facts["ImFontConfig"][1]
        record("sizeof(ImFontConfig)", wheel_size == own_size, f"wheel {wheel_size}, library {own_size}")
        if wheel_size == own_size:
            room = C.create_string_buffer(b"\xa5" * (own_size + 64), own_size + 64)
            cfg_p = C.addressof(room)
            function(_FONT_CONFIG_CTOR, None, C.c_void_p)(cfg_p)
            reference = imgui.ImFontConfig()
            defaults = [
                mem.read(cfg_p, "ImFontConfig.FontDataOwnedByAtlas", "<B") == 1,
                mem.read(cfg_p, "ImFontConfig.MergeMode", "<B") == int(reference.merge_mode),
                mem.read(cfg_p, "ImFontConfig.OversampleH", "<b") == reference.oversample_h,
                mem.read(cfg_p, "ImFontConfig.FontNo", "<I") == reference.font_no,
                near(mem.read(cfg_p, "ImFontConfig.SizePixels", "<f"), reference.size_pixels),
                near(
                    mem.read(cfg_p, "ImFontConfig.RasterizerDensity", "<f"),
                    reference.rasterizer_density,
                ),
                mem.read(cfg_p, "ImFontConfig.RasterizerDensity", "<f") == 1.0,
                room.raw[own_size:] == b"\xa5" * 64,
            ]
            record("ImFontConfig defaults", all(defaults), repr(defaults))

            ttf = _a_font_file(imgui_bundle)
            if ttf is None:
                record(
                    "ImFontConfig fields", False,
                    "no .ttf in the wheel's assets; name one with IMTYPESET_GATE_FONT",
                )
            else:
                with open(ttf, "rb") as handle:
                    data = handle.read()
                owned = function(_MEM_ALLOC, C.c_void_p, C.c_size_t)(len(data))
                C.memmove(owned, data, len(data))
                label = b"imtypeset-probe"
                C.memmove(mem.at(cfg_p, "ImFontConfig.Name"), label + b"\0", len(label) + 1)
                # The library marks a font it adds in the last eight bytes of
                # Name, past the name's terminator, and knows its own fonts by
                # reading them back through ImFont::Sources[0].
                name_size = facts["ImFontConfig.Name"][1]
                mark = b"\x21\x43\x65\x87\xa9\xcb\xed\xff"
                C.memmove(mem.at(cfg_p, "ImFontConfig.Name") + name_size - len(mark), mark, len(mark))
                added = function(
                    _ADD_FONT_FROM_MEMORY,
                    C.c_void_p, C.c_void_p, C.c_void_p, C.c_int, C.c_float, C.c_void_p, C.c_void_p,
                )(atlas_p, owned, len(data), 17.5, cfg_p, None)
                third = atlas.fonts[2] if len(atlas.fonts) == 3 else None
                record(
                    "ImFontConfig through AddFontFromMemoryTTF",
                    added is not None
                    and added == font_at(2)
                    and third is not None
                    and label.decode() in third.get_debug_name()
                    and len(third.sources) == 1,
                )
                # The atlas keeps its own copy of the config. Each field the
                # library sets is set there through the binding, read at the
                # library's offset, and put back.
                if third is not None and atlas.sources.size() == 3:
                    stored_p = atlas.sources.data_address() + 2 * own_size
                    stored = atlas.sources[2]

                    # ImFont::Sources, a vector of pointers into the atlas's
                    # configurations: one source for each of the three fonts,
                    # and the third font's is the configuration the binding
                    # shows as the atlas's third.
                    def sources_of(font_p):
                        size = mem.read(font_p, "ImFont.Sources", "<i")
                        data = mem.read(font_p, "ImFont.Sources", "<Q", 8)
                        return size, data

                    # A library built before the mark has no such offset to
                    # report, and cannot be told from one that reports it wrong.
                    known = "ImFont.Sources" in facts
                    sizes = [
                        known and sources_of(font_at(i))[0] == len(font.sources) == 1
                        for i, font in enumerate((first, second, third))
                    ]
                    source_p = 0
                    if all(sizes):
                        source_p = struct.unpack("<Q", C.string_at(sources_of(font_at(2))[1], 8))[0]
                    record(
                        "ImFont.Sources",
                        all(sizes) and source_p == stored_p,
                        f"{sizes} {source_p:#x} {stored_p:#x}"
                        if known
                        else "the library does not report this offset: it was built before the "
                        "package",
                    )
                    # The mark: forty bytes of Name as the library counts them,
                    # kept whole by the atlas's copy, and the name the wheel
                    # shows ending where its terminator was put.
                    kept_mark = b""
                    if source_p == stored_p:
                        kept_mark = C.string_at(
                            mem.at(source_p, "ImFontConfig.Name") + name_size - len(mark), len(mark)
                        )
                    record(
                        "ImFontConfig.Name mark",
                        name_size == 40
                        and kept_mark == mark
                        and third.get_debug_name() == label.decode(),
                        f"Name is {name_size} bytes; kept {kept_mark.hex()}",
                    )
                    fields = [
                        mem.read(stored_p, "ImFontConfig.FontData", "<Q") == owned,
                        mem.read(stored_p, "ImFontConfig.FontDataSize", "<i")
                        == stored.font_data_size
                        == len(data),
                    ]
                    for attribute, name, fmt, value in (
                        ("font_data_size", "ImFontConfig.FontDataSize", "<i", 0x01020304),
                        ("font_data_owned_by_atlas", "ImFontConfig.FontDataOwnedByAtlas", "<B", 0),
                        ("merge_mode", "ImFontConfig.MergeMode", "<B", 1),
                        ("oversample_h", "ImFontConfig.OversampleH", "<b", 3),
                        ("font_no", "ImFontConfig.FontNo", "<I", 0x1234),
                        ("size_pixels", "ImFontConfig.SizePixels", "<f", 31.25),
                        ("rasterizer_density", "ImFontConfig.RasterizerDensity", "<f", 2.5),
                    ):
                        kept = getattr(stored, attribute)
                        setattr(stored, attribute, type(kept)(value))
                        fields.append(mem.read(stored_p, name, fmt) == value)
                        setattr(stored, attribute, kept)
                        fields.append(mem.read(stored_p, name, fmt) == kept)
                    record("ImFontConfig fields", all(fields), repr(fields))
                else:
                    record("ImFontConfig fields", False, "the atlas did not keep the config")

        # A frame, for a baked font, a glyph and a draw list.
        io.display_size = imgui.ImVec2(640.0, 480.0)
        io.delta_time = 1.0 / 60.0
        io.set_ini_filename(None)  # the probe context must leave no imgui.ini behind
        io.backend_flags = imgui.BackendFlags_.renderer_has_textures
        imgui.new_frame()
        try:
            font_p = font_at(0)
            baked = first.get_font_baked(13.0)
            baked_p = function(_GET_FONT_BAKED, C.c_void_p, C.c_void_p, C.c_float, C.c_float)(
                font_p, 13.0, -1.0
            )
            record(
                "ImFontBaked.Size",
                near(mem.read(baked_p, "ImFontBaked.Size", "<f"), baked.size) and baked.size > 0.0,
                f"{baked.size}",
            )
            record(
                "ImFontBaked.Ascent",
                near(mem.read(baked_p, "ImFontBaked.Ascent", "<f"), baked.ascent)
                and baked.ascent != 0.0,
                f"{baked.ascent}",
            )

            lib.ImTypesetBundle_ReadGlyph.restype = None
            lib.ImTypesetBundle_ReadGlyph.argtypes = [C.c_void_p] + [C.c_void_p] * 4
            find = function(_FIND_GLYPH, C.c_void_p, C.c_void_p, C.c_uint)
            glyphs = []
            for codepoint in (ord("A"), ord("g"), ord(" ")):
                witness = baked.find_glyph(codepoint)
                box = (C.c_float * 8)()
                visible, colored, got = C.c_uint(9), C.c_uint(9), C.c_uint(0)
                lib.ImTypesetBundle_ReadGlyph(
                    find(baked_p, codepoint), box, C.byref(visible), C.byref(colored), C.byref(got)
                )
                expected = (
                    witness.x0, witness.y0, witness.x1, witness.y1,
                    witness.u0, witness.v0, witness.u1, witness.v1,
                )
                glyphs.append(
                    all(near(a, b) for a, b in zip(box, expected))
                    and visible.value == int(witness.is_visible())
                    and colored.value == int(witness.is_colored())
                    and got.value == witness.get_codepoint() == codepoint
                )
            inked = baked.find_glyph(ord("A"))
            record(
                "ImFontGlyph",
                all(glyphs) and inked.is_visible() and inked.u1 > inked.u0 and inked.x1 > inked.x0,
                repr(glyphs),
            )

            # The three bit-fields, which no font here sets every way: each bit
            # of the word that holds them is flipped in the wheel's own glyph (in
            # this throwaway context), the wheel's accessors and the library's
            # read must agree on what that did, and one of the flips must be
            # the one both call Colored.
            glyph_p = find(baked_p, ord("A"))
            word = C.string_at(glyph_p, 4)
            agreed, colored_bits, visible_bits = [], [], []
            for bit in range(32):
                flipped = bytearray(word)
                flipped[bit // 8] ^= 1 << (bit % 8)
                C.memmove(glyph_p, bytes(flipped), 4)
                witness = baked.find_glyph(ord("A"))
                visible, colored, got = C.c_uint(9), C.c_uint(9), C.c_uint(0)
                box = (C.c_float * 8)()
                lib.ImTypesetBundle_ReadGlyph(
                    glyph_p, box, C.byref(visible), C.byref(colored), C.byref(got)
                )
                agreed.append(
                    visible.value == int(witness.is_visible())
                    and colored.value == int(witness.is_colored())
                    and got.value == witness.get_codepoint()
                )
                if colored.value == 1 and witness.is_colored():
                    colored_bits.append(bit)
                if visible.value == 0 and not witness.is_visible():
                    visible_bits.append(bit)
            C.memmove(glyph_p, word, 4)
            record(
                "ImFontGlyph bit-fields",
                all(agreed) and len(colored_bits) == 1 and len(visible_bits) == 1,
                f"Colored at bits {colored_bits}, Visible at bits {visible_bits}, "
                f"{sum(agreed)} of 32 flips agree",
            )

            imgui.begin("imtypeset-probe")
            try:
                dl = imgui.get_window_draw_list()
                dl_p = function(_GET_WINDOW_DRAW_LIST, C.c_void_p)()
                vertex = facts["ImDrawVert"][1]
                index = facts["ImDrawIdx"][1]

                def vtx_size():
                    return mem.read(dl_p, "ImDrawList.VtxBuffer", "<i")

                def vtx_data():
                    return mem.read(dl_p, "ImDrawList.VtxBuffer", "<Q", 8)

                def idx_size():
                    return mem.read(dl_p, "ImDrawList.IdxBuffer", "<i")

                def idx_data():
                    return mem.read(dl_p, "ImDrawList.IdxBuffer", "<Q", 8)

                dl.add_rect_filled((1.0, 2.0), (3.0, 4.0), 0xFF112233)
                record(
                    "ImDrawList.VtxBuffer",
                    vtx_size() == dl.vtx_buffer.size() > 0
                    and vtx_data() == dl.vtx_buffer.data_address()
                    and idx_size() == dl.idx_buffer.size() > 0
                    and idx_data() == dl.idx_buffer.data_address(),
                )

                before_v, before_i = vtx_size(), idx_size()
                current = mem.read(dl_p, "ImDrawList._VtxCurrentIdx", "<I")
                function(_PRIM_RESERVE, None, C.c_void_p, C.c_int, C.c_int)(dl_p, 6, 4)
                write_v = mem.read(dl_p, "ImDrawList._VtxWritePtr", "<Q")
                write_i = mem.read(dl_p, "ImDrawList._IdxWritePtr", "<Q")
                reserved = (
                    vtx_size() == before_v + 4
                    and idx_size() == before_i + 6
                    and write_v == vtx_data() + before_v * vertex
                    and write_i == idx_data() + before_i * index
                    and current == dl._vtx_current_idx
                )
                dl.prim_rect_uv((5.0, 6.0), (7.0, 8.0), (0.125, 0.25), (0.5, 0.75), 0xFF445566)
                written = (
                    mem.read(dl_p, "ImDrawList._VtxWritePtr", "<Q") == write_v + 4 * vertex
                    and mem.read(dl_p, "ImDrawList._IdxWritePtr", "<Q") == write_i + 6 * index
                    and mem.read(dl_p, "ImDrawList._VtxCurrentIdx", "<I")
                    == current + 4
                    == dl._vtx_current_idx
                )
                record("ImDrawList write position", reserved and written, f"{reserved} {written}")

                # ImDrawVert, which the library writes member by member: the
                # first and third corners of the rectangle the wheel just
                # wrote, read at the library's three offsets.
                def corner(i):
                    at = write_v + i * vertex
                    return (
                        struct.unpack("<ff", C.string_at(mem.at(at, "ImDrawVert.pos"), 8)),
                        struct.unpack("<ff", C.string_at(mem.at(at, "ImDrawVert.uv"), 8)),
                        struct.unpack("<I", C.string_at(mem.at(at, "ImDrawVert.col"), 4))[0],
                    )

                corners = (corner(0), corner(2))
                record(
                    "ImDrawVert members",
                    corners
                    == (
                        ((5.0, 6.0), (0.125, 0.25), 0xFF445566),
                        ((7.0, 8.0), (0.5, 0.75), 0xFF445566),
                    ),
                    repr(corners),
                )

                dl.push_clip_rect((11.0, 22.0), (333.0, 444.0), False)
                clip = struct.unpack(
                    "<4f",
                    C.string_at(
                        mem.at(dl_p, "ImDrawList._CmdHeader") + facts["ImDrawCmdHeader.ClipRect"][0], 16
                    ),
                )
                dl.pop_clip_rect()
                record("ImDrawList._CmdHeader.ClipRect", clip == (11.0, 22.0, 333.0, 444.0), repr(clip))
            finally:
                imgui.end()
        finally:
            imgui.end_frame()

        lib.ImTypesetBundle_ProbeTextBuffer.restype = C.c_int
        record("ImGuiTextBuffer", lib.ImTypesetBundle_ProbeTextBuffer() == 1)
    except Exception as exc:  # a probe that cannot run is a probe that failed
        record("probe", False, f"{type(exc).__name__}: {exc}")
    finally:
        imgui.destroy_context(context)
        if previous is not None:
            imgui.set_current_context(previous)
    return results


def _a_font_file(imgui_bundle):
    """A TrueType file for the full probe: IMTYPESET_GATE_FONT if it names one,
    else one the wheel itself ships. Only the full probe needs it; the gate of
    an installed distribution reads no font (docs/sidecar.md, section 10)."""
    asked = os.environ.get("IMTYPESET_GATE_FONT", "")
    if asked and os.path.isfile(asked):
        return asked
    root = os.path.join(os.path.dirname(imgui_bundle.__file__), "assets", "fonts")
    for name in ("DroidSans.ttf", "Roboto/Roboto-Regular.ttf"):
        if os.path.isfile(os.path.join(root, name)):
            return os.path.join(root, name)
    found = sorted(glob.glob(os.path.join(root, "**", "*.ttf"), recursive=True))
    return found[0] if found else None


def check(lib, probe: bool = True) -> dict:
    """Everything the gate knows. With `probe`, creates and destroys one Dear
    ImGui context and reads the wheel's objects at the library's offsets: a
    wrong offset can fault. Without it, nothing is read through an offset: the
    report has no probe rows, and "enumerators" holds that one comparison."""
    import imgui_bundle
    from imgui_bundle import imgui

    lib.ImTypesetBundle_Headers.restype = C.c_char_p
    headers = lib.ImTypesetBundle_Headers().decode("ascii", "replace")
    facts = _facts(lib)
    imgui_ok, imgui_message = _imgui_check(lib)

    # When ImGui's check refuses, is sizeof(ImGuiStyle) the ONLY thing it refuses?
    style_only, wheel_style = False, None
    if not imgui_ok and "sz_style" in imgui_message:
        own = facts["ImGuiStyle"][1]
        for candidate in range(own - 64, own + 68, 4):
            if _imgui_check(lib, -1, candidate)[0]:
                style_only, wheel_style = True, candidate
                break

    # A refusal of anything but that one size (the version string first of all,
    # after a wheel upgrade) is final, and nothing is probed through offsets
    # that are already known to belong to another Dear ImGui.
    probes = _probe(lib, facts) if probe and (imgui_ok or style_only) else []
    return {
        "imtypeset_version": _version(lib),
        "enumerators": _enumerators(lib, imgui)[1:],
        "built_for": built_for(lib),
        "headers": headers,
        "wheel_version": getattr(imgui_bundle, "__version__", "unknown"),
        "wheel_imgui_version": imgui.get_version(),
        "imgui_check": imgui_ok,
        "imgui_check_message": imgui_message,
        "imgui_check_refuses_only_style_size": style_only,
        "wheel_sizeof_imguistyle": wheel_style,
        "library_sizeof_imguistyle": facts["ImGuiStyle"][1],
        "probes": probes,
        "probes_passed": sum(1 for _, passed, _ in probes if passed),
        "probes_failed": [name for name, passed, _ in probes if not passed],
        "strict": os.environ.get("IMTYPESET_BUNDLE_STRICT", "") not in ("", "0"),
    }


def accepted(report: dict):
    """(True, how) or (False, why): the policy in this module's docstring."""
    imgui_refuses = (
        f"Dear ImGui's own layout check refuses the library: {report['imgui_check_message']}"
    )
    if not report["imgui_check"] and not report["imgui_check_refuses_only_style_size"]:
        return False, imgui_refuses
    if report["probes_failed"] or not report["probes"]:
        details = "; ".join(
            f"{name} ({detail})" if detail else name
            for name, passed, detail in report["probes"]
            if not passed
        )
        return False, (
            "the Dear ImGui in this process does not lay out these the way the library was "
            f"compiled to expect: {details or 'no probe ran'}"
        )
    if report["imgui_check"]:
        return True, "imgui-check+probe"
    if report["headers"].startswith("stand-in:") and not report["strict"]:
        return True, "probe"
    return False, imgui_refuses


# --- What a refusal is ----------------------------------------------------------------------


def refusal(kind: str, sentence: str, detail: str = "") -> ImportError:
    """The ImportError for a refused `import imtypeset`.

    The FIRST LINE of str() is a sentence an application can show to its user:
    it names versions and what to do, and no file path. The lines after it are
    for a log. Three attributes are set: `sentence`, `detail`, and `kind`, one
    word for a program (docs/sidecar.md, section 9):

      no-library       a source checkout, and IMTYPESET_LIBRARY is not set
      wrong-platform   the library is for another system or machine
      damaged          a file of the distribution is missing, cut short, or not
                       the one it was built with
      not-a-library    the file cannot be loaded, or is not an imtypeset library
      package-mismatch the library and the Python package are of different versions
      bundle-missing   imgui_bundle cannot be imported
      bundle-mismatch  the library was built for another imgui-bundle
      layout           imtypeset and imgui_bundle are not installed side by side
      unchecked        nothing vouches for this pair and no check can be run
                       here, or the check itself could not be finished
      refused          the pair was checked, and the check refused it
      configuration    an environment variable asks for something that is not offered

    A plain ImportError and not a subclass: a refused import takes this module
    out of sys.modules, and a class would go with it.
    """
    error = ImportError("imtypeset: " + sentence + ("\n" + detail if detail else ""), name="imtypeset")
    error.kind = kind
    error.sentence = sentence
    error.detail = detail
    return error


# --- Content: what a file is, whatever its path and date ------------------------------------


def sha256_file(path: str) -> str:
    import hashlib

    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _elf_build_id(handle, head: bytes):
    # ELF64, little-endian: the GNU build-id note, found through the program
    # headers (they survive `strip`; the section headers need not).
    if len(head) < 64 or head[4] != 2 or head[5] != 1:
        return None
    (phoff,) = struct.unpack_from("<Q", head, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", head, 0x36)
    if phentsize < 56 or phnum > 512:
        return None
    for index in range(phnum):
        handle.seek(phoff + index * phentsize)
        header = handle.read(56)
        if len(header) < 56:
            return None
        p_type, _flags, p_offset, _vaddr, _paddr, p_filesz = struct.unpack_from("<IIQQQQ", header)
        if p_type != 4 or p_filesz > (1 << 20):  # PT_NOTE
            continue
        handle.seek(p_offset)
        notes = handle.read(p_filesz)
        at = 0
        while at + 12 <= len(notes):
            namesz, descsz, note_type = struct.unpack_from("<III", notes, at)
            name_at = at + 12
            desc_at = name_at + ((namesz + 3) & ~3)
            end = desc_at + ((descsz + 3) & ~3)
            if end > len(notes):
                break
            if note_type == 3 and notes[name_at:name_at + namesz] == b"GNU\0" and descsz > 0:
                return "elf-build-id:" + notes[desc_at:desc_at + descsz].hex()
            at = end
    return None


def _macho_uuid(handle, head: bytes):
    # Mach-O, 64-bit, little-endian, one architecture: the LC_UUID load command.
    # Written from the format as remembered; no Mach-O file has been read by
    # this code (tools/ci/test_wheel.py feeds it one made by hand).
    if len(head) < 32:
        return None
    ncmds, sizeofcmds = struct.unpack_from("<II", head, 16)
    if ncmds > 4096 or sizeofcmds > (1 << 20):
        return None
    handle.seek(32)
    commands = handle.read(sizeofcmds)
    at = 0
    for _ in range(ncmds):
        if at + 8 > len(commands):
            return None
        command, size = struct.unpack_from("<II", commands, at)
        if size < 8 or at + size > len(commands):
            return None
        if command == 0x1B and size >= 24:  # LC_UUID
            return "macho-uuid:" + commands[at + 8:at + 24].hex()
        at += size
    return None


def image_id(path: str):
    """What the linker called this image: "elf-build-id:<hex>" (the GNU build-id
    note), "macho-uuid:<hex>" (LC_UUID), or None. Recorded in an attestation
    for a person to compare with `readelf -n` or `dwarfdump --uuid`; the gate
    does NOT accept a file on it, because a truncated or overwritten file still
    carries it. Reads the file's headers as bytes; never raises."""
    try:
        with open(path, "rb") as handle:
            head = handle.read(64)
            if head[:4] == b"\x7fELF":
                return _elf_build_id(handle, head)
            if head[:4] == b"\xcf\xfa\xed\xfe":
                return _macho_uuid(handle, head)
    except (OSError, struct.error, ValueError):
        return None
    return None


def _take(handle, digest, offset: int, size: int) -> bool:
    """Feeds `size` bytes at `offset` to the digest; False if the file is short."""
    handle.seek(offset)
    left = size
    while left > 0:
        block = handle.read(min(left, 1 << 20))
        if not block:
            return False
        digest.update(block)
        left -= len(block)
    return True


def _elf_loaded(handle, head: bytes, digest, size: int) -> bool:
    # What the loader is told and what it maps: the fields of the ELF header
    # it reads (type, machine, entry, flags, where the program headers are),
    # the program headers, and every PT_LOAD segment's bytes in the file with
    # its address and length. Left out: the rest of the 64-byte ELF header,
    # which says where the SECTION headers are (`strip` moves them), and the
    # sections that no segment maps.
    #
    # And the file must be as long as its own headers say: every program
    # header's bytes, the section header table, and every section that has
    # bytes in the file lie inside it. The loader reads no section header, so
    # a file cut in its tail would still load; it is refused here because it
    # is not the file that was installed.
    if len(head) < 64 or head[4] != 2 or head[5] != 1:
        return False
    e_type, e_machine = struct.unpack_from("<HH", head, 0x10)
    e_entry, phoff, shoff = struct.unpack_from("<QQQ", head, 0x18)
    (e_flags,) = struct.unpack_from("<I", head, 0x30)
    phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", head, 0x36)
    if phentsize < 56 or not 0 < phnum <= 512:
        return False
    handle.seek(phoff)
    table = handle.read(phentsize * phnum)
    if len(table) != phentsize * phnum:
        return False
    if shoff != 0:
        if shentsize < 64 or shoff + shentsize * max(shnum, 1) > size:
            return False
        handle.seek(shoff)
        sections = handle.read(shentsize * shnum)
        if len(sections) != shentsize * shnum:
            return False
        for index in range(shnum):
            (sh_type,) = struct.unpack_from("<I", sections, index * shentsize + 4)
            sh_offset, sh_size = struct.unpack_from("<QQ", sections, index * shentsize + 0x18)
            if sh_type not in (0, 8) and sh_offset + sh_size > size:  # SHT_NULL, SHT_NOBITS
                return False
    digest.update(struct.pack("<HHQQIHH", e_type, e_machine, e_entry, phoff, e_flags, phentsize, phnum))
    digest.update(table)
    loads = 0
    for index in range(phnum):
        p_type, _flags, p_offset, p_vaddr, _paddr, p_filesz = struct.unpack_from(
            "<IIQQQQ", table, index * phentsize
        )
        if p_offset + p_filesz > size:
            return False
        if p_type != 1:  # PT_LOAD
            continue
        loads += 1
        digest.update(struct.pack("<QQ", p_vaddr, p_filesz))
        skip = max(0, 64 - p_offset) if p_offset < 64 else 0
        if p_filesz > skip and not _take(handle, digest, p_offset + skip, p_filesz - skip):
            return False
    return loads > 0


# Load commands that are a (file offset, size) pair into __LINKEDIT
# (linkedit_data_command): the code signature, segment split info, function
# starts, data in code, code-signing requirements, linker hints, the exports
# trie and the chained fixups.
_MACHO_LINKEDIT_DATA = (0x1D, 0x1E, 0x26, 0x29, 0x2B, 0x2E, 0x80000033, 0x80000034)


def _macho_loaded(handle, head: bytes, digest, size: int) -> bool:
    # Every section's bytes in the file, with its names, address, length and
    # flags, of every LC_SEGMENT_64; and each such segment's name, address,
    # size in memory and protection. __LINKEDIT is left out, name and all: it
    # has no sections, ends in the code signature, and signing changes its
    # length. Left out too: the header and every other load command. Signing
    # adds or rewrites LC_CODE_SIGNATURE (and with it the header's count),
    # and install_name_tool, which application freezers run on the libraries
    # they collect, rewrites LC_ID_DYLIB, LC_LOAD_DYLIB and LC_RPATH.
    #
    # And the file must be as long as its load commands say: every segment's
    # bytes (__LINKEDIT's too, which is the END of the file), the symbol
    # table, and every table a linkedit_data_command names lie inside it.
    #
    # Written from the format as remembered; no Mach-O file made by Apple's
    # linker has been read by this.
    if len(head) < 32:
        return False
    ncmds, sizeofcmds = struct.unpack_from("<II", head, 16)
    if ncmds > 4096 or sizeofcmds > (1 << 20):
        return False
    handle.seek(32)
    commands = handle.read(sizeofcmds)
    if len(commands) != sizeofcmds:
        return False
    at, sections = 0, 0
    for _ in range(ncmds):
        if at + 8 > len(commands):
            return False
        command, length_of = struct.unpack_from("<II", commands, at)
        if length_of < 8 or at + length_of > len(commands):
            return False
        if command == 0x19:  # LC_SEGMENT_64
            if length_of < 72:
                return False
            segname = commands[at + 8:at + 24]
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", commands, at + 24)
            _maxprot, initprot, nsects = struct.unpack_from("<iiI", commands, at + 56)
            if fileoff + filesize > size or 72 + nsects * 80 > length_of:
                return False
            if segname.rstrip(b"\0") != b"__LINKEDIT":
                digest.update(segname + struct.pack("<QQi", vmaddr, vmsize, initprot))
            for index in range(nsects):
                section = at + 72 + index * 80
                names = commands[section:section + 32]
                addr, length, offset = struct.unpack_from("<QQI", commands, section + 32)
                (flags,) = struct.unpack_from("<I", commands, section + 64)
                if flags & 0xFF in (0x1, 0xC, 0x12) or offset == 0:  # zero-fill: no bytes in the file
                    continue
                sections += 1
                digest.update(names + struct.pack("<QQI", addr, length, flags))
                if offset + length > size or not _take(handle, digest, offset, length):
                    return False
        elif command in _MACHO_LINKEDIT_DATA:
            if length_of < 16:
                return False
            dataoff, datasize = struct.unpack_from("<II", commands, at + 8)
            if dataoff + datasize > size:
                return False
        elif command == 0x2:  # LC_SYMTAB
            if length_of < 24:
                return False
            symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", commands, at + 8)
            if symoff + nsyms * 16 > size or stroff + strsize > size:
                return False
        at += length_of
    return sections > 0


def image_sha256(path: str):
    """A sha256 of what the loader MAPS of this file and of what it is told
    about mapping it: "elf-load:<hex>" or "macho-sections:<hex>"
    (`_elf_loaded` and `_macho_loaded` say exactly what goes in). None for a
    file that is neither, does not read as one, or is SHORTER THAN ITS OWN
    HEADERS SAY (cut anywhere, its tail included). `strip`, install_name_tool
    and code signing leave what is hashed alone and change the file's own
    sha256, so this is what holds a copy that an application freezer rewrote
    to the file the attestation is about. A file that is truncated, or
    overwritten inside what is mapped, does not keep it.
    Reads the file as bytes, within its own length; never raises."""
    import hashlib

    try:
        size = os.path.getsize(path)
        with open(path, "rb") as handle:
            head = handle.read(64)
            digest = hashlib.sha256()
            if head[:4] == b"\x7fELF":
                done = _elf_loaded(handle, head, digest, size)
                return "elf-load:" + digest.hexdigest() if done else None
            if head[:4] == b"\xcf\xfa\xed\xfe":
                done = _macho_loaded(handle, head, digest, size)
                return "macho-sections:" + digest.hexdigest() if done else None
    except (OSError, struct.error, ValueError):
        return None
    return None


def identity(path: str) -> dict:
    """What an attestation records of a file."""
    return {
        "file": os.path.basename(path),
        "size": os.path.getsize(path),
        "sha256": sha256_file(path),
        "image_sha256": image_sha256(path),
        "image_id": image_id(path),
    }


def same_content(recorded, path: str):
    """"sha256" when the file is byte for byte the recorded one; "image" when
    it is not, and what the loader maps of it is (image_sha256); None
    otherwise, and for a record that is not one. Never raises."""
    if not isinstance(recorded, dict) or not isinstance(path, str):
        return None
    wanted, wanted_image = recorded.get("sha256"), recorded.get("image_sha256")
    try:
        if isinstance(wanted, str) and len(wanted) == 64 and sha256_file(path) == wanted:
            return "sha256"
    except OSError:
        return None
    mapped = image_sha256(path)
    if mapped is not None and isinstance(wanted_image, str) and mapped == wanted_image:
        return "image"
    return None


def _version(lib) -> str:
    if not hasattr(lib, "ImTypeset_GetVersion"):
        return ""
    lib.ImTypeset_GetVersion.restype = C.c_char_p
    lib.ImTypeset_GetVersion.argtypes = []
    return (lib.ImTypeset_GetVersion() or b"").decode("ascii", "replace")


def built_for(lib) -> dict:
    """What the library says it was built for (the build script compiles it in):
    wheel_version, module (a file name), module_sha256. Empty for a library
    built before it could say."""
    if not hasattr(lib, "ImTypesetBundle_BuiltFor"):
        return {}
    lib.ImTypesetBundle_BuiltFor.restype = C.c_char_p
    lib.ImTypesetBundle_BuiltFor.argtypes = []
    answer = {}
    for line in (lib.ImTypesetBundle_BuiltFor() or b"").decode("utf-8", "replace").splitlines():
        key, equals, value = line.partition("=")
        if equals and value:
            answer[key] = value
    return answer


# --- The wheel's module: which file it is, and getting it into the process ------------------


def module_file():
    """The file of the wheel's extension module (imgui_bundle._imgui_bundle),
    found WITHOUT importing anything: from the module when the application has
    already imported it, else from where the import system says the package
    is. None when neither finds it. Never raises."""
    import importlib.machinery
    import importlib.util
    import sys

    native = sys.modules.get("imgui_bundle._imgui_bundle")
    path = getattr(native, "__file__", None)
    if isinstance(path, str) and os.path.isfile(path):
        return path
    try:
        spec = importlib.util.find_spec("imgui_bundle")
    except Exception:  # whatever a broken install or a foreign finder raises
        spec = None
    try:
        directories = list(getattr(spec, "submodule_search_locations", None) or [])
    except Exception:
        directories = []
    for directory in directories:
        for suffix in importlib.machinery.EXTENSION_SUFFIXES:
            candidate = os.path.join(str(directory), "_imgui_bundle" + suffix)
            if os.path.isfile(candidate):
                return candidate
    return None


def _running_bundle_version():
    """The version of the imgui-bundle that is here, without importing it:
    from the package if it is imported, else from its metadata. None when
    neither says (a freezer may drop the metadata)."""
    import sys

    version = getattr(sys.modules.get("imgui_bundle"), "__version__", None)
    if isinstance(version, str) and version:
        return version
    try:
        import importlib.metadata

        return str(importlib.metadata.version("imgui-bundle"))
    except Exception:
        return None


def _import_bundle(needed: str = ""):
    try:
        import imgui_bundle

        return imgui_bundle
    except Exception as exc:  # an ImportError, or whatever a broken install raises
        wanted = f"imgui-bundle {needed}" if needed else "imgui-bundle"
        raise refusal(
            "bundle-missing",
            f"imtypeset needs {wanted}, and it cannot be imported here. Install it.",
            f"import imgui_bundle: {type(exc).__name__}: {exc}",
        ) from exc


def _bundle_missing(needed: str = "") -> ImportError:
    wanted = f"imgui-bundle {needed}" if needed else "imgui-bundle"
    return refusal(
        "bundle-missing",
        f"imtypeset needs {wanted}, and it is not found here. Install it.",
        "imgui_bundle has no extension module (_imgui_bundle) where the import system looks "
        "for the package",
    )


def open_library(path: str, bundle_first: bool = False, environment: bool = True):
    """(the library, what had to be in the process first: "none", "import" or "global").

    A library bound by lookup (the only kind macOS has) finds Dear ImGui among
    what is already loaded, so the wheel's module must be loaded before it:
    "import". Where that is not enough the module is opened again RTLD_GLOBAL:
    "global". On Linux that is never done unasked, because it puts every symbol
    of the module, its FreeType included, in front of libraries loaded later
    (measured: docs/sidecar.md, section 2); IMTYPESET_PRELOAD_BUNDLE=1 asks,
    and is read only with `environment`, which an installed distribution
    passes as False.
    """
    import sys

    def make_global():
        module = module_file()
        if module is None:
            raise _bundle_missing()
        C.CDLL(module, mode=C.RTLD_GLOBAL)

    preload = "none"
    asked_global = environment and os.environ.get("IMTYPESET_PRELOAD_BUNDLE", "") == "1"
    if bundle_first or asked_global:
        _import_bundle()
        preload = "import"
        if asked_global:
            make_global()
            preload = "global"
    try:
        return C.CDLL(path), preload
    except OSError as exc:
        first = exc
    ladder = []
    if preload == "none":
        ladder.append("import")
    if preload != "global" and not sys.platform.startswith("linux"):
        ladder.append("global")
    for step in ladder:
        try:
            if step == "import":
                import imgui_bundle  # noqa: F401
            else:
                make_global()
        except Exception:
            break
        try:
            return C.CDLL(path), step
        except OSError:
            continue
    raise refusal(
        "not-a-library",
        "The imtypeset library cannot be loaded: it is damaged, or it was built for another "
        "system or another imgui-bundle. Install imtypeset again.",
        f"cannot load {path!r}: {first}"
        + (" (on Linux a library bound by lookup loads only with IMTYPESET_PRELOAD_BUNDLE=1, "
           "which only a development checkout reads)"
           if "undefined symbol" in str(first) and sys.platform.startswith("linux") else ""),
    ) from first


def _loaded_images():
    """(the paths of the files mapped in this process, how they were listed), or
    (None, why not)."""
    import sys

    if sys.platform.startswith("linux"):
        try:
            with open("/proc/self/maps", encoding="utf-8", errors="replace") as handle:
                fields = (line.split(None, 5) for line in handle)
                return {f[5].strip() for f in fields if len(f) == 6}, "/proc/self/maps"
        except OSError as exc:
            return None, f"/proc/self/maps cannot be read ({exc})"
    if sys.platform == "darwin":
        # dyld's own list of images. Written from Apple's documentation of
        # <mach-o/dyld.h> as remembered; never run.
        try:
            dyld = C.CDLL(None)
            count = dyld._dyld_image_count
            count.restype = C.c_uint32
            count.argtypes = []
            name = dyld._dyld_get_image_name
            name.restype = C.c_char_p
            name.argtypes = [C.c_uint32]
            paths = set()
            for index in range(count()):
                raw = name(index)  # null when the list shrank meanwhile
                if raw:
                    paths.add(os.fsdecode(raw))
            return paths, "dyld's image list"
        except (OSError, AttributeError, ValueError) as exc:
            return None, f"dyld's image list cannot be read ({type(exc).__name__}: {exc})"
    return None, f"this package knows no way to list loaded images on {sys.platform}"


def _global_imgui(module: str):
    """(the images, other than the wheel's module, that the process's GLOBAL
    symbol scope answers with for Dear ImGui's names; how that was found out),
    or (None, why not).

    A library's undefined symbols are bound in that scope first: on ELF before
    the library's own needed libraries are looked at, and on macOS it is where
    a symbol left for lookup is found at all. So an executable that has a Dear
    ImGui of its own linked in, or another extension opened RTLD_GLOBAL that
    carries one, would be what the library draws through, with the wheel's
    module loaded and unused. Asks dlsym for four names and dladdr for the
    image of each answer; maps nothing and calls none of them.

    NOT SEEN: an image that exports the names and was opened RTLD_LOCAL. On
    ELF the library does not bind to such an image. Whether macOS's lookup can
    is not known here; the macOS half of this function has never run.
    """

    class DlInfo(C.Structure):
        _fields_ = [("dli_fname", C.c_char_p), ("dli_fbase", C.c_void_p),
                    ("dli_sname", C.c_char_p), ("dli_saddr", C.c_void_p)]

    try:
        scope = C.CDLL(None)
        dladdr = scope.dladdr
        dladdr.restype = C.c_int
        dladdr.argtypes = [C.c_void_p, C.POINTER(DlInfo)]
    except (OSError, AttributeError, ValueError) as exc:
        return None, f"the global scope cannot be asked ({type(exc).__name__}: {exc})"
    others = []
    for name in (_GET_IO, _GET_STYLE, _GET_WINDOW_DRAW_LIST, _MEM_ALLOC):
        try:
            address = C.cast(getattr(scope, name), C.c_void_p).value
        except (AttributeError, ValueError):
            continue  # not in the global scope: the ordinary case on Linux
        info = DlInfo()
        image = ""
        if address and dladdr(address, C.byref(info)) and info.dli_fname:
            image = os.fsdecode(info.dli_fname)
        if not image or os.path.realpath(image) != module:
            if (image or "an image dladdr cannot name") not in others:
                others.append(image or "an image dladdr cannot name")
    return others, "dlsym and dladdr on the global scope"


def _same_module(library_path: str) -> str:
    """Refuses if the library can be drawing through another Dear ImGui than
    the one the application imported:

      - a second Dear ImGui Bundle module in the process. A library linked by
        name finds the module through its run path, and where that leads to
        another copy than the one the application imported, the library draws
        through a Dear ImGui the application never initialises;
      - another image answering for Dear ImGui's names in the global symbol
        scope (`_global_imgui`).

    Returns how the checks were made, with "not done: <why>" for one that
    could not be: host_info() shows it either way."""
    module = module_file()
    if module is None:
        raise _bundle_missing()
    mine = os.path.realpath(module)
    mapped, how = _loaded_images()
    if mapped is None:
        first = "not done: " + how
    else:
        others = sorted(
            path for path in mapped
            if os.path.basename(path).startswith("_imgui_bundle.") and os.path.realpath(path) != mine
        )
        if others:
            raise refusal(
                "refused",
                "imtypeset found two copies of imgui-bundle in this application and cannot tell "
                "which one draws. Install imtypeset and imgui-bundle into the same place.",
                f"{library_path!r} loaded the imgui_bundle module at {others[0]!r}, and this "
                f"interpreter's is {mine!r}. A library built by python/host/build_bundle_library.sh "
                "is for the interpreter it was built with.",
            )
        first = "done: " + how
    foreign, how = _global_imgui(mine)
    if foreign is None:
        return f"{first}; global Dear ImGui names not done: {how}"
    if foreign:
        raise refusal(
            "refused",
            "imtypeset found a second Dear ImGui in this application, beside imgui-bundle's, and "
            "would draw through the wrong one. imtypeset works only where imgui-bundle's is the "
            "one Dear ImGui that is visible to it.",
            f"the global symbol scope answers for Dear ImGui's names from {foreign[0]!r}, and "
            f"the imgui_bundle module is {mine!r}",
        )
    return f"{first}; global Dear ImGui names: none but the wheel's ({how})"


# --- The attestation: the full probe's verdict, carried as content --------------------------

ATTESTATION_SCHEMA = 1
PACKAGED_ATTESTATION = "_attestation.json"


def attestation_file(library_path: str, route: str = "IMTYPESET_LIBRARY"):
    """Where the attestation for this library is, or None. For an installed
    distribution: `_attestation.json` in the package's directory and nothing
    else. For a library a developer named: `<library>.attestation.json` (a
    build directory), else `_attestation.json` in its directory (a library
    named inside an unpacked wheel)."""
    packaged = os.path.join(os.path.dirname(library_path), PACKAGED_ATTESTATION)
    if route == "installed":
        return packaged if os.path.isfile(packaged) else None
    beside = library_path + ".attestation.json"
    if os.path.isfile(beside):
        return beside
    return packaged if os.path.isfile(packaged) else None


def child_interpreter():
    """(sys.executable, "") when it can be taken for a Python interpreter that
    may be started to run the probe; (None, why not) otherwise. Nothing is run
    to find out: starting a `sys.executable` that is an application starts the
    application again. So the answer is from what can be read: `sys.frozen`
    (PyInstaller, py2app, cx_Freeze set it), Nuitka's mark on a compiled
    module, this file being a file a child can load by path, and the
    executable's NAME, which is what is left for a packager that sets no flag
    (Briefcase starts the application from a binary named for it). An
    interpreter under another name is refused too; the way past that is an
    attestation (tools/bundle_route/attest.py), not a variable."""
    import sys

    if getattr(sys, "frozen", False):
        return None, "sys.frozen is set: this is a packaged application"
    if "__compiled__" in globals():
        return None, "this module was compiled by Nuitka: this is a packaged application"
    executable = sys.executable or ""
    if not executable or not os.path.isfile(executable):
        return None, f"sys.executable ({executable!r}) names no file"
    name = os.path.basename(executable).lower()
    if not re.fullmatch(r"(python|pypy)[0-9.]*[a-z]*(\.exe)?", name):
        return None, (f"sys.executable is {name!r}, which is not the name of a Python "
                      "interpreter: taken for a packaged application")
    if not os.path.isfile(os.path.abspath(__file__)):
        return None, "this module is not a file a child interpreter could load"
    return executable, ""


def mode(route: str, attested: bool) -> str:
    """"attested" or "probe": which gate decides, before the library is loaded.

    An installed distribution: its attestation, or a refusal. No variable is
    read. A development checkout: IMTYPESET_GATE when set ("attested" forbids
    the probe, "probe" asks for it over an attestation); unset, the
    attestation where there is one and else the full probe, in a child
    interpreter, where one can be started (`child_interpreter`)."""
    if route == "installed":
        if attested:
            return "attested"
        raise refusal(
            "unchecked",
            "This copy of imtypeset carries no record that it was checked against the "
            "imgui-bundle it is running beside, and it is not checked here. Install a released "
            "imtypeset wheel.",
            "No _attestation.json in the package's directory, which holds a library: an "
            "installed distribution is accepted on its attestation and on nothing else.",
        )
    asked = os.environ.get("IMTYPESET_GATE", "")
    if asked not in ("", "attested", "probe"):
        raise refusal(
            "configuration",
            f"IMTYPESET_GATE is 'attested' or 'probe', not {asked!r}.",
        )
    if attested and asked != "probe":
        return "attested"
    how_to = (
        "A development build gets an attestation from python/host/build_bundle_library.sh "
        "(its gate step) or tools/bundle_route/attest.py."
    )
    if asked == "attested":
        raise refusal(
            "unchecked",
            "The imtypeset library named by IMTYPESET_LIBRARY has no record that it was checked "
            "against this imgui-bundle, and IMTYPESET_GATE=attested forbids checking it here.",
            "No <library>.attestation.json beside the library. " + how_to,
        )
    python, why_not = child_interpreter()
    if python is None:
        raise refusal(
            "unchecked",
            "The imtypeset library named by IMTYPESET_LIBRARY has no record that it was checked "
            "against this imgui-bundle, and it cannot be checked here: this is not a Python "
            "interpreter that can start another. Install a released imtypeset wheel.",
            f"The library is not loaded unchecked. No child interpreter: {why_not}. " + how_to,
        )
    return "probe"


def _read_attestation(where: str) -> dict:
    """The attestation in this file, with everything the gate reads of it held
    to its shape; ImportError (kind `damaged`) for anything else."""
    import json

    try:
        with open(where, encoding="utf-8") as handle:
            attestation = json.load(handle)
        if not isinstance(attestation, dict):
            raise TypeError("not an object")
        if attestation["schema"] != ATTESTATION_SCHEMA:
            raise ValueError(f"schema {attestation['schema']!r}")
        for key in ("library", "module", "platform"):
            if not isinstance(attestation[key], dict):
                raise TypeError(f"{key!r} is not an object")
        for key in ("wheel_version", "imtypeset_version", "accepted_by", "headers"):
            if not isinstance(attestation[key], str):
                raise TypeError(f"{key!r} is not a string")
        for key in ("system", "machine"):
            if not isinstance(attestation["platform"][key], str):
                raise TypeError(f"platform.{key} is not a string")
        rows = attestation["probes"]
        if not isinstance(rows, list) or not rows or not all(
            isinstance(row, list) and len(row) == 3 and isinstance(row[0], str)
            and isinstance(row[1], bool) and isinstance(row[2], str)
            for row in rows
        ):
            raise TypeError("'probes' is not a list of [name, passed, detail]")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise refusal(
            "damaged",
            "This copy of imtypeset is damaged: its record of what it was built for cannot be "
            "read. Install imtypeset again.",
            f"{where!r}: {type(exc).__name__}: {exc}",
        ) from exc
    return attestation


def precheck(library_path: str, route: str):
    """Everything that is decided BEFORE the library is mapped into this
    process. Returns (attestation, None) or (None, the child's report);
    ImportError otherwise.

    With an attestation: the library file, then the wheel's module file, are
    hashed and held to it. The module's file is found without importing
    imgui_bundle (`module_file`); only when it is the attested one is
    imgui_bundle imported, and the module that import brought is then held to
    the file that was hashed. Where the file cannot be found without the
    import (an importer that keeps extension modules somewhere it does not
    say), the import comes first, and `_module_found` in what is returned is
    "by importing". Nothing is read through an offset.

    Without one (a development checkout only): the library is loaded and
    probed in a child interpreter, here, before anything else (`mode` says
    when that is allowed).
    """
    import platform

    where = attestation_file(library_path, route)
    if mode(route, where is not None) == "probe":
        return None, probe_in_child(library_path)
    attestation = _read_attestation(where)
    recorded_library, recorded_module = attestation["library"], attestation["module"]
    built_wheel, version = attestation["wheel_version"], attestation["imtypeset_version"]
    system, machine = attestation["platform"]["system"], attestation["platform"]["machine"]

    here = (platform.system(), platform.machine())
    if (system, machine) != here:
        raise refusal(
            "wrong-platform",
            f"This copy of imtypeset {version} was built for {system} {machine}, and this "
            f"computer is {here[0]} {here[1]}. Install the imtypeset built for this computer.",
            f"{where!r}",
        )
    library_match = same_content(recorded_library, library_path)
    if library_match is None:
        raise refusal(
            "damaged",
            f"The imtypeset library is not the one imtypeset {version} was installed with: it is "
            "damaged, or an update replaced only part of it. Install imtypeset again.",
            f"{library_path!r} is not sha256 {recorded_library.get('sha256')} nor "
            f"{recorded_library.get('image_sha256')} ({where!r})",
        )

    def held(module: str):
        match = same_content(recorded_module, module)
        if match is not None:
            return match
        running = _running_bundle_version()
        if running is not None and running != built_wheel:
            sentence = (
                f"imtypeset {version} was built for imgui-bundle {built_wheel}, and this "
                f"application has imgui-bundle {running}. The two are updated together: install "
                f"imgui-bundle {built_wheel}, or the imtypeset built for imgui-bundle {running}."
            )
        elif running is None:
            sentence = (
                f"imtypeset {version} was built for imgui-bundle {built_wheel}, and this "
                "application has another imgui-bundle, or a damaged one. The two are updated "
                f"together: install imgui-bundle {built_wheel} again, or the imtypeset built for "
                "the imgui-bundle that is here."
            )
        else:
            sentence = (
                f"imtypeset {version} was built for another build of imgui-bundle {built_wheel} "
                "than the one in this application (another Python version, a wheel that is "
                "not the released one, or a damaged file). Install the imtypeset built for this "
                "one."
            )
        raise refusal(
            "bundle-mismatch",
            sentence,
            f"{module!r} is not sha256 {recorded_module.get('sha256')} nor "
            f"{recorded_module.get('image_sha256')} ({where!r})",
        )

    module = module_file()
    found = "before importing imgui_bundle"
    if module is None:
        _import_bundle(built_wheel)
        module = module_file()
        found = "by importing imgui_bundle"
        if module is None:
            raise _bundle_missing(built_wheel)
    module_match = held(module)
    _import_bundle(built_wheel)
    imported = module_file()
    if imported is None:
        raise _bundle_missing(built_wheel)
    if os.path.realpath(imported) != os.path.realpath(module):
        # The import found another file than the import system said it would.
        module, module_match = imported, held(imported)
    if attestation.get("run_path") == "$ORIGIN/../imgui_bundle":
        # A library linked by name finds the wheel's module beside its own
        # package, and nowhere else.
        beside = os.path.join(
            os.path.dirname(os.path.dirname(library_path)), "imgui_bundle",
            os.path.basename(module),
        )
        if not os.path.exists(beside) or not os.path.samefile(beside, module):
            raise refusal(
                "layout",
                "imtypeset and imgui-bundle are installed in two different places, and imtypeset "
                "finds imgui-bundle only beside itself. Install both into the same environment.",
                f"the library looks for {beside!r}; imgui_bundle was imported from {module!r}",
            )
    attestation["_file"] = where
    attestation["_matched_by"] = {"library": library_match, "module": module_match}
    attestation["_module_found"] = found
    return attestation, None


_CHILD = """
import importlib.util, json, sys
spec = importlib.util.spec_from_file_location("_bundle", sys.argv[1])
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
try:
    have = gate.module_file() is not None
    if have:
        try:
            import imgui_bundle
        except Exception:
            have = False  # a library with its own Dear ImGui needs none
    library, preload = gate.open_library(sys.argv[2], bundle_first=have)
    print("IMTYPESET-GATE-LOADED", flush=True)
    if hasattr(library, "ImTypesetBundle_LayoutFacts"):
        gate._import_bundle()
        report = gate.check(library)
        report["kind"] = "imgui_bundle"
    else:
        hosted = hasattr(library, "ImTypesetHost_GetInfo")
        report = {"kind": "self-hosted" if hosted else "unknown"}
    report["preload"] = preload
except ImportError as refused:
    if not hasattr(refused, "kind"):
        raise
    print("IMTYPESET-GATE-REFUSED " + json.dumps(
        {"kind": refused.kind, "sentence": refused.sentence, "detail": refused.detail}))
else:
    print("IMTYPESET-GATE " + json.dumps(report))
"""


def probe_in_child(library_path: str) -> dict:
    """What a child interpreter found on LOADING this library and, when it is
    a sidecar for Dear ImGui Bundle, check()'s whole report ("kind":
    "imgui_bundle"). A library with a Dear ImGui of its own, or with neither,
    is only loaded there: {"kind": "self-hosted" or "unknown"}.

    A truncated file can kill the process that maps it, and a probe at a wrong
    offset can read through a wild pointer; in a child, either is an answer.
    Needs `child_interpreter()`: development and wheel builds, never an
    application."""
    import json
    import subprocess

    python, why_not = child_interpreter()
    if python is None:
        raise refusal(
            "unchecked",
            "imtypeset could not start the check of its library against imgui-bundle: this is "
            "not a Python interpreter that can start another. Install a released imtypeset "
            "wheel, which is checked when it is built.",
            f"The library is not loaded unchecked. No child interpreter: {why_not}.",
        )
    try:
        child = subprocess.run(
            [python, "-P", "-B", "-c", _CHILD, os.path.abspath(__file__), library_path],
            capture_output=True, text=True, timeout=300,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise refusal(
            "unchecked",
            "imtypeset could not start the check of its library against imgui-bundle. Install a "
            "released imtypeset wheel, which is checked when it is built.",
            f"the full probe needs a child interpreter; {python!r}: "
            f"{type(exc).__name__}: {exc}",
        ) from exc
    lines = child.stdout.splitlines()
    for line in lines:
        if line.startswith("IMTYPESET-GATE-REFUSED "):
            try:
                said = json.loads(line[len("IMTYPESET-GATE-REFUSED "):])
                raise refusal(str(said["kind"]), str(said["sentence"]), str(said["detail"]))
            except (ValueError, KeyError, TypeError):
                break
    reports = [l for l in lines if l.startswith("IMTYPESET-GATE ")]
    status = (f"a child interpreter that ended with status {child.returncode} (negative: a "
              "signal). " + child.stderr.strip()[-400:])
    if "IMTYPESET-GATE-LOADED" not in lines:
        raise refusal(
            "not-a-library",
            "The imtypeset library cannot be loaded: it is damaged, or it was built for another "
            "system or another imgui-bundle. Build it again.",
            f"loading {library_path!r} was tried in {status}",
        )
    report = None
    if child.returncode == 0 and reports:
        try:
            report = json.loads(reports[-1][len("IMTYPESET-GATE "):])
            if report.get("kind") == "imgui_bundle":
                report["probes"] = [tuple(p) for p in report["probes"]]
                report["enumerators"] = tuple(report["enumerators"])
        except (ValueError, KeyError, TypeError, AttributeError):
            report = None
    if report is None:
        raise refusal(
            "refused",
            "The imtypeset library does not fit the imgui-bundle in this application: it was "
            "built for another one. Install the imtypeset built for this imgui-bundle.",
            f"checking {library_path!r} against the Dear ImGui of the installed imgui_bundle "
            f"was done in {status} Rebuild with python/host/build_bundle_library.sh.",
        )
    return report


def _refuse_report(library_path: str, report: dict, how: str):
    built = report.get("built_for", {}).get("wheel_version")
    running = report["wheel_version"]
    probed = report.get("probes") and not report.get("probes_failed")
    if report.get("strict") and report["headers"].startswith("stand-in:") and probed:
        sentence = (
            "This imtypeset was built against stand-in Dear ImGui headers, and "
            "IMTYPESET_BUNDLE_STRICT=1 refuses such a build. Install a released imtypeset wheel, "
            "or unset it."
        )
        kind = "configuration"
    elif built and built != running:
        sentence = (
            f"The imtypeset library was built for imgui-bundle {built}, and this application has "
            f"imgui-bundle {running}. The two are updated together: install imgui-bundle {built}, "
            f"or the imtypeset built for imgui-bundle {running}."
        )
        kind = "bundle-mismatch"
    else:
        sentence = (
            f"The imtypeset library does not fit imgui-bundle {running} (Dear ImGui "
            f"{report['wheel_imgui_version']}) in this application. Install the imtypeset built "
            "for this imgui-bundle."
        )
        kind = "refused"
    raise refusal(
        kind,
        sentence,
        f"{library_path!r} (headers {report['headers']}): {how}. Rebuild the library for this "
        "wheel with python/host/build_bundle_library.sh.",
    )


def gate(lib, library_path: str, route: str = "IMTYPESET_LIBRARY", attestation=None,
         preload: str = "none", child_report=None) -> dict:
    """The report for an accepted pairing; ImportError, with the reason, otherwise.
    `attestation` or `child_report` is what precheck() returned, before the
    library was loaded.

    With an attestation (precheck() has already held the library and the
    wheel's module to it, by content): the checks that read nothing through an
    offset are run here, in this process, and must answer what the attestation
    records; the field probe's rows are the attestation's, from the build. No
    child interpreter, no Dear ImGui context, no font, and nothing is written.

    With a child's report: that report is the verdict, made in the child
    before this process loaded the library. Nothing is remembered: what
    remembers a verdict is an attestation, and tools/bundle_route/attest.py
    writes one.
    """
    second = _same_module(library_path)
    if attestation is None:
        if not isinstance(child_report, dict) or child_report.get("kind") != "imgui_bundle":
            raise refusal(
                "unchecked",
                "This copy of imtypeset carries no record that it was checked against the "
                "imgui-bundle it is running beside, and it is not checked here. Install a "
                "released imtypeset wheel.",
                f"{library_path!r} was loaded with neither an attestation nor a child "
                "interpreter's report of the full probe.",
            )
        report = dict(child_report)
        report["checked_in"] = "child"
        report["attestation"] = None
    else:
        report = check(lib, probe=False)
        differing = [
            key for key in ("headers", "imgui_check", "imgui_check_refuses_only_style_size")
            if report[key] != attestation.get(key)
        ]
        name, passed, detail = ("enumerators",) + tuple(report["enumerators"])
        if not passed:
            differing.append(f"enumerators ({detail})")
        if differing:
            _refuse_report(
                library_path, report,
                "the library answers otherwise than the one the attestation was written for: "
                + ", ".join(differing),
            )
        report["probes"] = [tuple(p) for p in attestation["probes"]]
        report["probes_passed"] = sum(1 for _, ok, _ in report["probes"] if ok)
        report["probes_failed"] = [n for n, ok, _ in report["probes"] if not ok]
        report["checked_in"] = "attestation"
        report["attestation"] = {
            "file": attestation["_file"],
            "matched_by": attestation["_matched_by"],
            "module_found": attestation.get("_module_found"),
            "attested_by": attestation.get("attested_by"),
            "attested_on": attestation.get("attested_on"),
            "attested_where": attestation.get("attested_where"),
            "distribution_version": attestation.get("distribution_version"),
        }
        if not report["built_for"]:
            report["built_for"] = {
                "wheel_version": attestation["wheel_version"],
                "module": attestation["module"].get("file"),
                "module_sha256": attestation["module"].get("sha256"),
            }
    ok, how = accepted(report)
    if not ok:
        _refuse_report(library_path, report, how)
    report["accepted_by"] = how
    report["stand_in"] = report["headers"].startswith("stand-in:")
    report["second_module_check"] = second
    report["preload"] = preload
    return report


def attest(library_path: str, out_path: str = "", extra=None) -> dict:
    """Runs the full probe in a child interpreter and, if the pair is accepted,
    writes the attestation for it: `<library>.attestation.json` unless
    `out_path` names another file. Returns what it wrote; ImportError otherwise.
    This is the only writer of an attestation, and importing the package never
    calls it."""
    import datetime
    import json
    import platform
    import sys

    library_path = os.path.abspath(library_path)
    report = probe_in_child(library_path)
    if report.get("kind") != "imgui_bundle":
        raise refusal(
            "not-a-library",
            "This library is not a sidecar for Dear ImGui Bundle, and only such a library is "
            "attested.",
            f"{library_path!r} does not export ImTypesetBundle_LayoutFacts ({report.get('kind')})",
        )
    ok, how = accepted(report)
    if not ok:
        _refuse_report(library_path, report, how)
    module = module_file()
    if module is None:
        raise _bundle_missing()
    attestation = {
        "schema": ATTESTATION_SCHEMA,
        "library": identity(library_path),
        "module": identity(module),
        "wheel_version": report["wheel_version"],
        "wheel_imgui_version": report["wheel_imgui_version"],
        "headers": report["headers"],
        "stand_in": report["headers"].startswith("stand-in:"),
        "imgui_check": report["imgui_check"],
        "imgui_check_message": report["imgui_check_message"],
        "imgui_check_refuses_only_style_size": report["imgui_check_refuses_only_style_size"],
        "accepted_by": how,
        "probes": [list(p) for p in report["probes"]],
        "preload": report.get("preload", "none"),
        "platform": {"system": platform.system(), "machine": platform.machine()},
        "python": "%d.%d" % sys.version_info[:2],
        "attested_by": "the full field probe, in a child interpreter (python/imtypeset/_bundle.py)",
        "attested_on": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "attested_where": "",
        "imtypeset_version": report.get("imtypeset_version", ""),
        "distribution_version": "",
    }
    attestation.update(extra or {})
    for key in ("library", "module"):
        if attestation[key]["image_sha256"] is None:
            raise refusal(
                "not-a-library",
                "A file of the pair does not read as an ELF or Mach-O image that is as long as "
                "its headers say, and is not attested.",
                f"{attestation[key]['file']!r}",
            )
    out_path = out_path or library_path + ".attestation.json"
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(attestation, handle, indent=1, sort_keys=True)
        handle.write("\n")
    return attestation
