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

`check()` returns a report; `python/imtypeset/__init__.py` turns a refusal into
an ImportError. The policy is `accepted()`'s:

  - the probe must pass whole, always;
  - ImGui's check must pass too, unless the library says it was built against
    STAND-IN headers ("stand-in:..." from ImTypesetBundle_Headers) and the only
    thing ImGui's check refuses is sizeof(ImGuiStyle), a struct of which the
    library reads one field that the probe covers. IMTYPESET_BUNDLE_STRICT=1
    removes that allowance.
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

    # The enumerators the library names, as it was compiled, against the wheel's
    # binding (generated from the wheel's headers). The library passes them to
    # the wheel's functions by value.
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
        record("enumerators", own and not differing, "; ".join(differing))
    except Exception as exc:
        record("enumerators", False, f"{type(exc).__name__}: {exc}")

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
                record("ImFontConfig fields", False, "no .ttf in the wheel's assets")
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
    """A TrueType file the wheel itself ships."""
    root = os.path.join(os.path.dirname(imgui_bundle.__file__), "assets", "fonts")
    for name in ("DroidSans.ttf", "Roboto/Roboto-Regular.ttf"):
        if os.path.isfile(os.path.join(root, name)):
            return os.path.join(root, name)
    found = sorted(glob.glob(os.path.join(root, "**", "*.ttf"), recursive=True))
    return found[0] if found else None


def check(lib) -> dict:
    """Everything the gate knows. Creates and destroys one Dear ImGui context."""
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
    probes = _probe(lib, facts) if imgui_ok or style_only else []
    return {
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
    refusal = (
        f"Dear ImGui's own layout check refuses the library: {report['imgui_check_message']}"
    )
    if not report["imgui_check"] and not report["imgui_check_refuses_only_style_size"]:
        return False, refusal
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
    return False, refusal


# --- The gate as the package runs it -------------------------------------------------------

_CHILD = """
import ctypes, importlib.util, json, sys
spec = importlib.util.spec_from_file_location("_bundle", sys.argv[1])
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
report = gate.check(ctypes.CDLL(sys.argv[2]))
print("IMTYPESET-GATE " + json.dumps(report))
"""


def _pair_key(library_path: str) -> str:
    """Names this library and this wheel binary: the pair a verdict is about."""
    import imgui_bundle

    parts = []
    for path in (library_path, imgui_bundle._imgui_bundle.__file__):
        status = os.stat(path)
        parts.append(f"{os.path.abspath(path)} {status.st_size} {status.st_mtime_ns}")
    return "\n".join(parts) + "\n"


def _same_module(library_path: str) -> None:
    """ImportError if loading the library brought a second Dear ImGui Bundle module
    into the process. The library names the wheel's module it was linked for by
    path; under another interpreter's wheel that is a different file, with a
    Dear ImGui of its own that the application never initialises."""
    import imgui_bundle

    mine = os.path.realpath(imgui_bundle._imgui_bundle.__file__)
    try:
        with open("/proc/self/maps", encoding="utf-8", errors="replace") as handle:
            fields = (line.split(None, 5) for line in handle)
            mapped = {f[5].strip() for f in fields if len(f) == 6}
    except OSError:
        return  # not Linux: the probe is what there is
    others = sorted(
        path for path in mapped
        if os.path.basename(path).startswith("_imgui_bundle.") and os.path.realpath(path) != mine
    )
    if others:
        raise ImportError(
            f"imtypeset: {library_path!r} was linked for the imgui_bundle module at {others[0]!r}, "
            f"and this interpreter's is {mine!r}. Build one for this interpreter: "
            "python/host/build_bundle_library.sh <bundle-checkout> <this python>."
        )


def gate(lib, library_path: str) -> dict:
    """The report for an accepted pairing; ImportError, with the reason, otherwise.

    A probe at a wrong offset can read through a wild pointer, and then the
    process dies instead of raising. So the first time a (library, wheel) pair
    is seen the whole check runs in a child interpreter, where a crash is an
    answer; a pair that passed there is remembered in `<library>.gate` (when
    that can be written) and from then on checked in this process.
    """
    import json
    import subprocess
    import sys

    _same_module(library_path)
    key = _pair_key(library_path)
    stamp = library_path + ".gate"
    try:
        with open(stamp, encoding="utf-8") as handle:
            known = handle.read() == key
    except OSError:
        known = False

    if known:
        report = check(lib)
        report["checked_in"] = "process"
    else:
        child = subprocess.run(
            [sys.executable, "-P", "-B", "-c", _CHILD, os.path.abspath(__file__), library_path],
            capture_output=True, text=True, timeout=300,
        )
        lines = [l for l in child.stdout.splitlines() if l.startswith("IMTYPESET-GATE ")]
        if child.returncode != 0 or not lines:
            raise ImportError(
                f"imtypeset: checking {library_path!r} against the Dear ImGui of the installed "
                f"imgui_bundle ended a child interpreter with status {child.returncode} "
                "(negative: a signal). The library was built for another wheel; rebuild it with "
                "python/host/build_bundle_library.sh. " + child.stderr.strip()[-400:]
            )
        report = json.loads(lines[-1][len("IMTYPESET-GATE "):])
        report["probes"] = [tuple(p) for p in report["probes"]]
        report["checked_in"] = "child"
    ok, how = accepted(report)
    if not ok:
        raise ImportError(
            f"imtypeset: {library_path!r} (headers {report['headers']}) cannot be used with the "
            f"installed imgui_bundle {report['wheel_version']} (Dear ImGui "
            f"{report['wheel_imgui_version']}): {how}. Rebuild the library for this wheel with "
            "python/host/build_bundle_library.sh."
        )
    report["accepted_by"] = how
    if not known:
        try:
            with open(stamp, "w", encoding="utf-8") as handle:
                handle.write(key)
        except OSError:
            pass
    return report
