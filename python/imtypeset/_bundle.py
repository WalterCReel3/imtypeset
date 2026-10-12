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
starts no interpreter and writes nothing. docs/sidecar.md, section 8, says what
that trusts. Only a library named by IMTYPESET_LIBRARY with no attestation
beside it, or IMTYPESET_GATE=probe, has the probe run at import, in a child.

This file imports nothing of its package, so that a child interpreter and the
tools can load it by path.
"""

from __future__ import annotations

import ctypes as C
import glob
import os
import struct

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

    wheel = C.CDLL(imgui_bundle._imgui_bundle.__file__)

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

      no-library       IMTYPESET_LIBRARY is not set and the package carries none
      wrong-platform   the library is for another system or machine
      damaged          a file of the distribution is not the one it was built with
      not-a-library    the file cannot be loaded, or is not an imtypeset library
      package-mismatch the library and the Python package are of different versions
      bundle-missing   imgui_bundle cannot be imported
      bundle-mismatch  the library was built for another imgui-bundle
      layout           imtypeset and imgui_bundle are not installed side by side
      unchecked        nothing vouches for this pair and no check can be run here
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


def _elf_loaded(handle, head: bytes, digest) -> bool:
    # Every PT_LOAD segment's bytes in the file, with its address and length,
    # and the program headers themselves. The first 64 bytes of the file (the
    # ELF header, which says where the SECTION headers are, and `strip` moves
    # them) are left out.
    if len(head) < 64 or head[4] != 2 or head[5] != 1:
        return False
    (phoff,) = struct.unpack_from("<Q", head, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", head, 0x36)
    if phentsize < 56 or not 0 < phnum <= 512:
        return False
    handle.seek(phoff)
    table = handle.read(phentsize * phnum)
    if len(table) != phentsize * phnum:
        return False
    loads = 0
    for index in range(phnum):
        p_type, _flags, p_offset, p_vaddr, _paddr, p_filesz = struct.unpack_from(
            "<IIQQQQ", table, index * phentsize
        )
        if p_type != 1:  # PT_LOAD
            continue
        loads += 1
        digest.update(struct.pack("<QQ", p_vaddr, p_filesz))
        skip = max(0, 64 - p_offset) if p_offset < 64 else 0
        if p_filesz > skip and not _take(handle, digest, p_offset + skip, p_filesz - skip):
            return False
    return loads > 0


def _macho_loaded(handle, head: bytes, digest) -> bool:
    # Every section's bytes in the file, with its names and length, of every
    # LC_SEGMENT_64. Left out: the header and the load commands (a code
    # signature and install_name_tool rewrite them) and __LINKEDIT, which has
    # no sections and ends in the signature. Written from the format as
    # remembered; no Mach-O file made by Apple's linker has been read by this.
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
        command, size = struct.unpack_from("<II", commands, at)
        if size < 8 or at + size > len(commands):
            return False
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                return False
            (nsects,) = struct.unpack_from("<I", commands, at + 64)
            if 72 + nsects * 80 > size:
                return False
            for index in range(nsects):
                section = at + 72 + index * 80
                names = commands[section:section + 32]
                _addr, length, offset = struct.unpack_from("<QQI", commands, section + 32)
                (flags,) = struct.unpack_from("<I", commands, section + 64)
                if flags & 0xFF in (0x1, 0xC, 0x12) or offset == 0:  # zero-fill: no bytes in the file
                    continue
                sections += 1
                digest.update(names + struct.pack("<Q", length))
                if not _take(handle, digest, offset, length):
                    return False
        at += size
    return sections > 0


def image_sha256(path: str):
    """A sha256 of what the loader MAPS of this file: "elf-load:<hex>" (the
    PT_LOAD segments) or "macho-sections:<hex>" (the sections), or None for a
    file that is neither, is cut short, or does not read as one. `strip`,
    install_name_tool and code signing leave those bytes alone and change the
    file's own sha256, so this is what holds a copy that an application
    freezer rewrote to the file the attestation is about. A file that is
    truncated or overwritten inside what is mapped does not keep it.
    Reads the file as bytes, within its own length; never raises."""
    import hashlib

    try:
        with open(path, "rb") as handle:
            head = handle.read(64)
            digest = hashlib.sha256()
            if head[:4] == b"\x7fELF":
                return "elf-load:" + digest.hexdigest() if _elf_loaded(handle, head, digest) else None
            if head[:4] == b"\xcf\xfa\xed\xfe":
                done = _macho_loaded(handle, head, digest)
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


def same_content(recorded: dict, path: str):
    """"sha256" when the file is byte for byte the recorded one; "image" when
    it is not, and what the loader maps of it is (image_sha256); None
    otherwise. Never raises."""
    try:
        if sha256_file(path) == recorded.get("sha256"):
            return "sha256"
    except OSError:
        return None
    mapped = image_sha256(path)
    if mapped is not None and mapped == recorded.get("image_sha256"):
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


# --- The wheel's module in the process, before the library ----------------------------------


def _module_file() -> str:
    import imgui_bundle

    return imgui_bundle._imgui_bundle.__file__


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


def open_library(path: str, bundle_first: bool = False):
    """(the library, what had to be in the process first: "none", "import" or "global").

    A library bound by lookup (the only kind macOS has) finds Dear ImGui among
    what is already loaded, so the wheel's module must be loaded before it:
    "import". Where that is not enough the module is opened again RTLD_GLOBAL:
    "global". On Linux that is never done unasked, because it puts every symbol
    of the module, its FreeType included, in front of libraries loaded later
    (measured: docs/sidecar.md, section 2); IMTYPESET_PRELOAD_BUNDLE=1 asks.
    """
    import sys

    def make_global():
        C.CDLL(_module_file(), mode=C.RTLD_GLOBAL)

    preload = "none"
    asked_global = os.environ.get("IMTYPESET_PRELOAD_BUNDLE", "") == "1"
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
        + (" (on Linux a library bound by lookup loads only with IMTYPESET_PRELOAD_BUNDLE=1)"
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


def _same_module(library_path: str) -> str:
    """Refuses if loading the library brought a second Dear ImGui Bundle module
    into the process: a library linked by name finds the module through its run
    path, and where that leads to another copy than the one the application
    imported, the library draws through a Dear ImGui the application never
    initialises. Returns how the check was made, or "not done: <why>" where the
    loaded images cannot be listed: host_info() shows it either way."""
    mine = os.path.realpath(_module_file())
    mapped, how = _loaded_images()
    if mapped is None:
        return "not done: " + how
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
    return "done: " + how


# --- The attestation: the full probe's verdict, carried as content --------------------------

ATTESTATION_SCHEMA = 1
PACKAGED_ATTESTATION = "_attestation.json"


def attestation_file(library_path: str):
    """Where the attestation for this library would be: `_attestation.json` in
    its directory (an installed distribution), else `<library>.attestation.json`
    (a build directory). None when there is neither."""
    packaged = os.path.join(os.path.dirname(library_path), PACKAGED_ATTESTATION)
    if os.path.isfile(packaged):
        return packaged
    beside = library_path + ".attestation.json"
    return beside if os.path.isfile(beside) else None


def mode(route: str, attested: bool) -> str:
    """"attested" or "probe": which gate runs. IMTYPESET_GATE decides when set.
    Unset: the attestation where there is one; the full probe, in a child
    interpreter, only for a library named by IMTYPESET_LIBRARY (the development
    override) and only where a child interpreter can exist."""
    import sys

    asked = os.environ.get("IMTYPESET_GATE", "")
    frozen = bool(getattr(sys, "frozen", False))
    if asked not in ("", "attested", "probe"):
        raise refusal(
            "configuration",
            f"IMTYPESET_GATE is 'attested' or 'probe', not {asked!r}.",
        )
    if asked == "probe":
        if frozen:
            raise refusal(
                "configuration",
                "IMTYPESET_GATE=probe starts a Python interpreter, and a packaged application "
                "has none. Unset it.",
            )
        return "probe"
    if attested:
        return "attested"
    if asked == "" and route == "IMTYPESET_LIBRARY" and not frozen:
        return "probe"
    raise refusal(
        "unchecked",
        "This copy of imtypeset carries no record that it was checked against the imgui-bundle "
        "it is running beside, and it is not checked here. Install a released imtypeset wheel.",
        "No _attestation.json beside the library. A development build gets one from "
        "python/host/build_bundle_library.sh (its gate step) or tools/bundle_route/attest.py; "
        "IMTYPESET_GATE=probe runs the full probe in a child interpreter instead.",
    )


def precheck(library_path: str, route: str):
    """Before the library is loaded: the attestation for it, or None when the
    full probe is to decide. Refuses a library, or a wheel's module, that is not
    the one the attestation is about. Hashes two files and imports imgui_bundle;
    loads nothing else and reads nothing through an offset."""
    import json
    import platform

    where = attestation_file(library_path)
    if where is None and route != "installed":
        return None  # gate() decides, once it is known what kind of library this is
    if mode(route, where is not None) == "probe":
        return None
    try:
        with open(where, encoding="utf-8") as handle:
            attestation = json.load(handle)
        if attestation["schema"] != ATTESTATION_SCHEMA:
            raise ValueError(f"schema {attestation['schema']!r}")
        recorded_library, recorded_module = attestation["library"], attestation["module"]
        built_wheel = str(attestation["wheel_version"])
        version = str(attestation["imtypeset_version"])
        system, machine = attestation["platform"]["system"], attestation["platform"]["machine"]
        attestation["accepted_by"], attestation["probes"], attestation["headers"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise refusal(
            "damaged",
            "This copy of imtypeset is damaged: its record of what it was built for cannot be "
            "read. Install imtypeset again.",
            f"{where!r}: {type(exc).__name__}: {exc}",
        ) from exc

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
    bundle = _import_bundle(built_wheel)
    running = str(getattr(bundle, "__version__", "unknown"))
    module_match = same_content(recorded_module, _module_file())
    if module_match is None:
        if running != built_wheel:
            sentence = (
                f"imtypeset {version} was built for imgui-bundle {built_wheel}, and this "
                f"application has imgui-bundle {running}. The two are updated together: install "
                f"imgui-bundle {built_wheel}, or the imtypeset built for imgui-bundle {running}."
            )
        else:
            sentence = (
                f"imtypeset {version} was built for another build of imgui-bundle {built_wheel} "
                "than the one in this application (another Python version, or a wheel that is "
                "not the released one). Install the imtypeset built for this one."
            )
        raise refusal(
            "bundle-mismatch",
            sentence,
            f"{_module_file()!r} is not sha256 {recorded_module.get('sha256')} nor "
            f"{recorded_module.get('image_sha256')} ({where!r})",
        )
    if attestation.get("run_path") == "$ORIGIN/../imgui_bundle":
        # A library linked by name finds the wheel's module beside its own
        # package, and nowhere else.
        beside = os.path.join(
            os.path.dirname(os.path.dirname(library_path)), "imgui_bundle",
            os.path.basename(_module_file()),
        )
        if not os.path.exists(beside) or not os.path.samefile(beside, _module_file()):
            raise refusal(
                "layout",
                "imtypeset and imgui-bundle are installed in two different places, and imtypeset "
                "finds imgui-bundle only beside itself. Install both into the same environment.",
                f"the library looks for {beside!r}; imgui_bundle was imported from "
                f"{_module_file()!r}",
            )
    attestation["_file"] = where
    attestation["_matched_by"] = {"library": library_match, "module": module_match}
    return attestation


_CHILD = """
import importlib.util, json, sys
spec = importlib.util.spec_from_file_location("_bundle", sys.argv[1])
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
library, preload = gate.open_library(sys.argv[2], bundle_first=True)
report = gate.check(library)
report["preload"] = preload
print("IMTYPESET-GATE " + json.dumps(report))
"""


def probe_in_child(library_path: str) -> dict:
    """check()'s whole report, from a child interpreter. A probe at a wrong
    offset can read through a wild pointer, and then the process dies instead
    of raising; in a child, that is an answer. Needs `sys.executable` to be a
    Python interpreter: development and wheel builds, never an application."""
    import json
    import subprocess
    import sys

    try:
        child = subprocess.run(
            [sys.executable, "-P", "-B", "-c", _CHILD, os.path.abspath(__file__), library_path],
            capture_output=True, text=True, timeout=300,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise refusal(
            "unchecked",
            "imtypeset could not start the check of its library against imgui-bundle. Install a "
            "released imtypeset wheel, which is checked when it is built.",
            f"the full probe needs a child interpreter; {sys.executable!r}: "
            f"{type(exc).__name__}: {exc}",
        ) from exc
    lines = [l for l in child.stdout.splitlines() if l.startswith("IMTYPESET-GATE ")]
    if child.returncode != 0 or not lines:
        raise refusal(
            "refused",
            "The imtypeset library does not fit the imgui-bundle in this application: it was "
            "built for another one. Install the imtypeset built for this imgui-bundle.",
            f"checking {library_path!r} against the Dear ImGui of the installed imgui_bundle ended "
            f"a child interpreter with status {child.returncode} (negative: a signal). Rebuild "
            "with python/host/build_bundle_library.sh. " + child.stderr.strip()[-400:],
        )
    report = json.loads(lines[-1][len("IMTYPESET-GATE "):])
    report["probes"] = [tuple(p) for p in report["probes"]]
    report["enumerators"] = tuple(report["enumerators"])
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
         preload: str = "none") -> dict:
    """The report for an accepted pairing; ImportError, with the reason, otherwise.

    With an attestation (precheck() has already held the library and the
    wheel's module to it, by content): the checks that read nothing through an
    offset are run here, in this process, and must answer what the attestation
    records; the field probe's rows are the attestation's, from the build. No
    child interpreter, no Dear ImGui context, no font, and nothing is written.

    Without one (precheck() returned None): the whole of check() in a child
    interpreter, every time. Nothing is remembered: what remembers a verdict is
    an attestation, and tools/bundle_route/attest.py writes one.
    """
    second = _same_module(library_path)
    if attestation is None:
        mode(route, False)  # raises where no child interpreter may be started
        report = probe_in_child(library_path)
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
    ok, how = accepted(report)
    if not ok:
        _refuse_report(library_path, report, how)
    attestation = {
        "schema": ATTESTATION_SCHEMA,
        "library": identity(library_path),
        "module": identity(_module_file()),
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
    out_path = out_path or library_path + ".attestation.json"
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(attestation, handle, indent=1, sort_keys=True)
        handle.write("\n")
    return attestation
