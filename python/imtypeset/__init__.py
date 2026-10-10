"""imtypeset from Python: a ctypes binding over the C ABI (include/imtypeset_c.h).

    import imtypeset as ts          # loads $IMTYPESET_LIBRARY and checks it; ImportError if wrong
    ts.create_context()             # inside a Dear ImGui context the same library draws through
    ts.add_font(ts.Face.Regular, "Roboto-Regular.ttf")
    ...each frame, between the host's NewFrame and Render:
    ts.text("A paragraph, justified to the window.")

What each call DOES is documented once, in include/imtypeset.h, at the function
of the same name. What is written here is how it looks from Python:

  - TEXT is `str`. POSITIONS IN TEXT are indices into that `str` (codepoints),
    in both directions, where the library's own are UTF-8 byte offsets; the
    conversion is the library's (ImTypeset_*CodepointToByteOffset and back).
    A `str` that cannot be encoded as UTF-8 (a lone surrogate) raises
    UnicodeEncodeError before anything is called.
  - ERRORS. A call that did nothing raises `imtypeset.Error`, which carries the
    library's code, message and place. A call that was done differently (a
    formula shown as its source, a run set in the paragraph's face, text cut at
    the limit: the codes in `Error.SOFT`) returns False and raises nothing: the
    text is on the screen, and `last_error()` or `Paragraph.error` says what
    happened to it.
  - POSITIONS IN A DOCUMENT are (block, index into that block's `str`). The
    conversion to the library's (block, byte offset) is done per block, here,
    and costs nothing for a block whose text is ASCII: see `Document`.
  - HANDLES. A `Paragraph` or a `Document` is closed by `close()`, by `with`,
    by the garbage collector, or by `destroy_context()`, which closes every
    one the context has; using one afterwards raises ValueError and does not
    reach the library. `ImDrawList*` and `ImFont*` are plain integers (addresses), as
    whatever shares the library's Dear ImGui hands them out.
  - Lengths are pixels, colours are ImGui's packed 32-bit ints (`col32`).
  - One thread, as for the library and for ImGui.

WHICH DEAR IMGUI. The library named by IMTYPESET_LIBRARY decides, and
`host_info()` says: "self-hosted" when ImGui is compiled into that library
(python/host: one .so with ImGui, its SDL2 backends and a small host surface),
"imgui_bundle" when the library was built against Dear ImGui Bundle's ImGui and
resolves it from the bundle's module. Importing this package checks the pairing
before anything else can be called.

This is a binding to run from a build tree. It is not a packaged, portable
distribution: no wheel, no installer, one platform (Linux) run.
"""

from __future__ import annotations

import ctypes as _C
import enum as _enum
import struct as _struct
import weakref as _weakref
from typing import NamedTuple, Optional, Sequence, Tuple, Union

from . import _capi
from ._capi import BlockStyle, Container, DocumentOptions, Style

__all__ = [
    "Align",
    "Block",
    "BlockFlags",
    "BlockGeometry",
    "BlockKind",
    "BlockStyle",
    "BuildInfo",
    "CodeOverflow",
    "Container",
    "ContainerFlags",
    "ContainerKind",
    "Document",
    "DocumentHit",
    "DocumentOptions",
    "DocumentRange",
    "DocumentStats",
    "Mark",
    "MarkPlace",
    "Position",
    "Error",
    "ErrorCode",
    "ErrorInfo",
    "Face",
    "Formula",
    "Granularity",
    "Hit",
    "Line",
    "LineBreak",
    "Paragraph",
    "Run",
    "RunFlags",
    "Spacing",
    "Stats",
    "StatusFlags",
    "Style",
    "TextFlags",
    "add_font",
    "build_info",
    "byte_to_codepoint_offset",
    "calc_text_size",
    "codepoint_to_byte_offset",
    "col32",
    "create_context",
    "current_context",
    "destroy_context",
    "get_font",
    "get_style",
    "host_info",
    "last_error",
    "library",
    "remove_font",
    "selectable_text",
    "set_current_context",
    "set_style",
    "set_text_cache_limits",
    "show_demo_window",
    "stats",
    "text",
    "text_with_id",
    "version",
]


class Face(_enum.IntEnum):
    Default = 0
    Regular = 1
    Bold = 2
    Italic = 3
    BoldItalic = 4
    Code = 5
    CodeBold = 6
    Math = 7


class Align(_enum.IntEnum):
    Justify = 0
    Left = 1
    Center = 2
    Right = 3


class Spacing(_enum.IntEnum):
    Sentence = 0
    French = 1
    TeX = 2


class LineBreak(_enum.IntEnum):
    Auto = 0
    FirstFit = 1
    TotalFit = 2


class Granularity(_enum.IntEnum):
    Character = 0
    Word = 1
    Line = 2


class ErrorCode(_enum.IntEnum):
    None_ = 0
    NoContext = 1
    VersionMismatch = 2
    InvalidArgument = 3
    FileNotFound = 4
    FontRefused = 5
    NoFont = 6
    TextTooLong = 7
    MathRefused = 8
    LayoutFailed = 9
    TooManyVertices = 10
    BackendUnsupported = 11
    DescriptionCut = 12
    Exception = 1000


class TextFlags(_enum.IntFlag):
    None_ = 0
    HardBreaks = 1 << 0
    Math = 1 << 2


class RunFlags(_enum.IntFlag):
    None_ = 0
    NoBreak = 1 << 0
    Formula = 1 << 1  # The run is one formula, set inline: its text is the source.
    DisplayFormula = 1 << 2  # The same, displayed.


class BlockKind(_enum.IntEnum):
    Paragraph = 0
    Heading = 1
    ListItem = 2
    Code = 3
    Rule = 4
    Host = 5
    Table = 6  # Reserved: set as an empty Host block, and the edit raises.


class BlockFlags(_enum.IntFlag):
    None_ = 0
    HardBreaks = 1 << 0
    Math = 1 << 2


class ContainerKind(_enum.IntEnum):
    Quote = 0
    List = 1


class ContainerFlags(_enum.IntFlag):
    None_ = 0
    Tight = 1 << 0


class CodeOverflow(_enum.IntEnum):
    Wrap = 0
    Clip = 1
    Grow = 2


class MarkPlace(_enum.IntEnum):
    Ink = 0
    Behind = 1
    Under = 2


class StatusFlags(_enum.IntFlag):
    None_ = 0
    LaidOut = 1 << 0
    FirstFitFallback = 1 << 1
    DisplayMath = 1 << 2


class ErrorInfo(NamedTuple):
    code: int  # ErrorCode, or a number a newer library added.
    message: str
    byte_pos: int  # -1 when the error has no place.
    codepoint_pos: int  # The same place as an index into the `str`.


class Error(Exception):
    """A call that did nothing. `code` is an ErrorCode; `message` is the library's."""

    # "Done, differently" (imtypeset.h, ImTypesetError_): reported by a False, never raised.
    SOFT = frozenset(
        {
            ErrorCode.NoFont,
            ErrorCode.TextTooLong,
            ErrorCode.MathRefused,
            ErrorCode.LayoutFailed,
            ErrorCode.DescriptionCut,
        }
    )

    def __init__(self, info: ErrorInfo, where: str):
        super().__init__(f"{where}: {info.message or _code_name(info.code)}")
        self.code = info.code
        self.message = info.message
        self.byte_pos = info.byte_pos
        self.codepoint_pos = info.codepoint_pos


class Run(NamedTuple):
    """A stretch of a paragraph's text set differently. `start` and `end` index the `str`."""

    start: int
    end: int
    face: int = Face.Default
    scale: float = 0.0
    color: int = 0
    flags: int = 0
    tag: int = 0  # What Document.hit_test answers for a point on the run. 0: nothing.


class Line(NamedTuple):
    min: Tuple[float, float]
    max: Tuple[float, float]
    baseline: float
    text_start: int
    text_end: int


class Hit(NamedTuple):
    text_offset: int
    line: int
    inside: bool


class Formula(NamedTuple):
    text_start: int
    text_end: int
    display: bool
    refused: bool
    pos: int  # Where the reason applies, as an index into the `str`; -1 when it was set.
    reason: str


Stats = NamedTuple("Stats", [(_name, int) for _name in _capi.STATS_FIELDS])  # type: ignore[misc]


class Block(NamedTuple):
    """One block of a document: ImTypesetBlock, with its text and runs in it.

    `runs` index `text`, as a Paragraph's do. `style` is an index into the
    styles handed over with the blocks (or already in the document). `container`
    is 1 + the index of the innermost container the block is in, 0 for none, as
    the header has it. `marker` is a list item's ("1.", a bullet).
    """

    kind: int = BlockKind.Paragraph
    text: str = ""
    runs: Sequence = ()
    marker: str = ""
    style: int = 0
    left_indent: float = 0.0
    quote_depth: int = 0
    container: int = 0
    item: int = 0
    extent: float = 0.0
    flags: int = 0
    level: int = 0


class Position(NamedTuple):
    block: int
    offset: int  # An index into the block's `str`.


class DocumentRange(NamedTuple):
    start: Tuple[int, int]  # Position, or any (block, offset).
    end: Tuple[int, int]


class Mark(NamedTuple):
    """Something drawn over a range that is no part of the layout: ImTypesetMark."""

    range: Tuple[Tuple[int, int], Tuple[int, int]]
    color: int
    place: int = MarkPlace.Ink
    thickness: float = 0.0
    offset: float = 0.0


class DocumentHit(NamedTuple):
    position: Position
    line: int
    tag: int
    inside: bool
    in_block: bool


class BlockGeometry(NamedTuple):
    min: Tuple[float, float]
    max: Tuple[float, float]
    full_min: Tuple[float, float]
    full_max: Tuple[float, float]
    line_count: int
    laid_out: bool


DocumentStats = NamedTuple(  # type: ignore[misc]
    "DocumentStats", [(_name, int) for _name in _capi.DOCUMENT_STATS_FIELDS]
)


class BuildInfo(NamedTuple):
    version: str
    version_num: int
    abi_version: int
    imgui_version: str
    imgui_version_num: int
    sizeof_imwchar: int
    sizeof_imdrawvert: int
    sizeof_imdrawidx: int
    offsetof_imdrawvert_pos: int
    offsetof_imdrawvert_uv: int
    offsetof_imdrawvert_col: int
    freetype: bool
    catches_exceptions: bool
    face_count: int


def col32(r: int, g: int, b: int, a: int = 255) -> int:
    """IM_COL32 with ImGui's default byte order."""
    return (a & 255) << 24 | (b & 255) << 16 | (g & 255) << 8 | (r & 255)


def _code_name(code: int) -> str:
    try:
        return ErrorCode(code).name
    except ValueError:
        return f"error {code}"


# --- Loading, and the check that comes before everything else ---------------------------


class _HostFacts(_C.Structure):
    """The head of ImTypesetHost_Info (python/host/imtypeset_host.h)."""

    _fields_ = [
        ("StructSize", _C.c_int),
        ("ImGuiVersionNum", _C.c_int),
        ("SizeofImWchar", _C.c_int),
        ("SizeofImDrawVert", _C.c_int),
        ("SizeofImDrawIdx", _C.c_int),
    ]


def _load():
    path = _capi.library_path()
    lib = _capi.load(path)
    raw = _capi.BuildInfo()
    whole = lib.ImTypeset_GetBuildInfo(_C.byref(raw), _C.sizeof(raw))
    if whole < _C.sizeof(raw) or raw.StructSize != whole:
        raise ImportError(
            f"imtypeset: {path!r} describes itself in {whole} bytes and this package reads "
            f"{_C.sizeof(raw)}: the library is older than the package"
        )
    if raw.AbiVersion != _capi.C_ABI_VERSION:
        raise ImportError(
            f"imtypeset: {path!r} has C ABI revision {raw.AbiVersion}; this package was written "
            f"for revision {_capi.C_ABI_VERSION}"
        )
    info = BuildInfo(
        version=raw.Version.decode("ascii", "replace"),
        version_num=raw.VersionNum,
        abi_version=raw.AbiVersion,
        imgui_version=raw.ImGuiVersion.decode("ascii", "replace"),
        imgui_version_num=raw.ImGuiVersionNum,
        sizeof_imwchar=raw.SizeofImWchar,
        sizeof_imdrawvert=raw.SizeofImDrawVert,
        sizeof_imdrawidx=raw.SizeofImDrawIdx,
        offsetof_imdrawvert_pos=raw.OffsetofImDrawVertPos,
        offsetof_imdrawvert_uv=raw.OffsetofImDrawVertUV,
        offsetof_imdrawvert_col=raw.OffsetofImDrawVertCol,
        freetype=bool(raw.ImGuiEnableFreeType),
        catches_exceptions=bool(raw.CatchesExceptions),
        face_count=raw.FaceCount,
    )

    # Which ImGui the library draws through, and whether it is the ImGui the
    # library was compiled against. The three sizes are the HOST's.
    sizes = (raw.SizeofImWchar, raw.SizeofImDrawVert, raw.SizeofImDrawIdx)
    bundle_report = None
    if hasattr(lib, "ImTypesetHost_GetInfo"):
        host = "self-hosted"
        facts = _HostFacts()
        lib.ImTypesetHost_GetInfo.restype = _C.c_int
        lib.ImTypesetHost_GetInfo.argtypes = [_C.c_void_p, _C.c_int]
        lib.ImTypesetHost_GetInfo(_C.byref(facts), _C.sizeof(facts))
        if facts.ImGuiVersionNum != raw.ImGuiVersionNum:
            raise ImportError(
                f"imtypeset: {path!r} holds Dear ImGui {facts.ImGuiVersionNum} and an imtypeset "
                f"compiled against {raw.ImGuiVersionNum}"
            )
        sizes = (facts.SizeofImWchar, facts.SizeofImDrawVert, facts.SizeofImDrawIdx)
        layout_checked = True
    elif hasattr(lib, "ImTypesetBundle_LayoutFacts"):
        # No Dear ImGui in the library: it resolved the one in Dear ImGui
        # Bundle's module. _bundle.gate() raises ImportError unless that ImGui
        # is laid out the way the library was compiled to expect.
        from . import _bundle

        host = "imgui_bundle"
        bundle_report = _bundle.gate(lib, path)
        layout_checked = True
    else:
        host = "unknown"
        layout_checked = False

    ok = lib.ImTypeset_DebugCheckVersionAndDataLayout(
        _capi.C_VERSION.encode("ascii"),
        None,
        *sizes,
        _C.sizeof(_capi.Style),
        _C.sizeof(_capi.Run),
        _C.sizeof(_capi.TextRange),
        _C.sizeof(_capi.Line),
        _C.sizeof(_capi.Hit),
        _C.sizeof(_capi.Formula),
        _C.sizeof(_capi.Stats),
        _C.sizeof(_capi.ErrorInfo),
    )
    if not ok:
        message = _C.create_string_buffer(1024)
        lib.ImTypeset_GetLastErrorMessage(message, len(message))
        raise ImportError(
            f"imtypeset: {path!r} refuses this package (written for {_capi.C_VERSION}): "
            f"{message.value.decode('utf-8', 'replace')}"
        )
    ok = lib.ImTypeset_DebugCheckDocumentDataLayout(
        *(_C.sizeof(struct) for _, struct in _capi.DOCUMENT_STRUCTS)
    )
    if not ok:
        message = _C.create_string_buffer(1024)
        lib.ImTypeset_GetLastErrorMessage(message, len(message))
        raise ImportError(
            f"imtypeset: {path!r} refuses this package's document structs (written for "
            f"{_capi.C_VERSION}): {message.value.decode('utf-8', 'replace')}"
        )
    return lib, path, info, host, layout_checked, bundle_report


_lib, _path, _build, _host, _layout_checked, _bundle_report = _load()


def library() -> _C.CDLL:
    """The loaded library: for code that calls the same library's other exports."""
    return _lib


def build_info() -> BuildInfo:
    return _build


def version() -> str:
    return _lib.ImTypeset_GetVersion().decode("ascii", "replace")


def host_info() -> dict:
    """Which Dear ImGui this binding resolved, and how it knows."""
    return {
        "host": _host,  # "self-hosted", "imgui_bundle" or "unknown"
        "library": _path,
        "imgui_version": _build.imgui_version,
        "imgui_version_num": _build.imgui_version_num,
        "imgui_layout_checked": _layout_checked,
        "imwchar_bits": _build.sizeof_imwchar * 8,
        "imdrawidx_bits": _build.sizeof_imdrawidx * 8,
        "rasteriser": "FreeType" if _build.freetype else "stb_truetype",
        "imtypeset_version": _build.version,
        "c_abi_version": _build.abi_version,
        "catches_exceptions": _build.catches_exceptions,
        # Inside Dear ImGui Bundle only: _bundle.check()'s report, with
        # "headers" ("fork:<commit>" or "stand-in:..."), "accepted_by"
        # ("imgui-check+probe" or "probe") and every probe's outcome.
        "bundle": _bundle_report,
    }


# --- Text in, offsets both ways -----------------------------------------------------------


def _text_args(data: bytes):
    """(begin, end) for bytes: zero-terminated unless they hold a zero themselves."""
    if b"\0" not in data:
        return data, None
    begin = _C.cast(_C.c_char_p(data), _C.c_void_p).value or 0
    return data, begin + len(data)


def _to_byte(data: bytes, chars: int, offset: int) -> int:
    if len(data) == chars:  # ASCII: the two kinds of offset are one.
        return min(max(int(offset), 0), chars)
    return _lib.ImTypeset_TextCodepointToByteOffset(*_text_args(data), int(offset))


def _to_codepoint(data: bytes, chars: int, offset: int) -> int:
    if len(data) == chars:
        return min(max(int(offset), 0), chars)
    return _lib.ImTypeset_TextByteToCodepointOffset(*_text_args(data), int(offset))


def codepoint_to_byte_offset(text: str, offset: int) -> int:
    """Where character `offset` of `text` starts in its UTF-8, as the library counts."""
    return _lib.ImTypeset_TextCodepointToByteOffset(*_text_args(text.encode("utf-8")), int(offset))


def byte_to_codepoint_offset(text: str, offset: int) -> int:
    return _lib.ImTypeset_TextByteToCodepointOffset(*_text_args(text.encode("utf-8")), int(offset))


# --- Errors -------------------------------------------------------------------------------


def _error_info(raw: _capi.ErrorInfo) -> ErrorInfo:
    message = raw.Message.decode("utf-8", "replace") if raw.Message else ""
    return ErrorInfo(raw.Code, message, raw.BytePos, raw.CodepointPos)


def last_error() -> ErrorInfo:
    """The outcome of the most recent call that reports one; `code` 0 if it succeeded."""
    raw = _capi.ErrorInfo()
    _lib.ImTypeset_GetLastError(_C.byref(raw))
    return _error_info(raw)


def _outcome(ok, where: str) -> bool:
    """True; or False for something done differently; or raises for something not done."""
    if ok:
        return True
    info = last_error()
    if info.code == ErrorCode.None_ or info.code in Error.SOFT:
        return False
    raise Error(info, where)


def _must(ok, where: str) -> None:
    """For a call that cannot fail short of an exception caught at the C boundary."""
    if not ok:
        raise Error(last_error(), where)


def _count(value: int, where: str) -> int:
    """A count or an offset from the library: -1 is an exception caught at the boundary."""
    if value < 0:
        raise Error(last_error(), where)
    return value


def _style_arg(style: Optional[Style]):
    if style is None:
        return None
    if not isinstance(style, Style):
        raise TypeError("style is an imtypeset.Style or None")
    return _C.byref(style)


# --- Context ------------------------------------------------------------------------------

_paragraphs: dict = {}  # context address -> WeakSet of its open Paragraphs and Documents


def create_context() -> int:
    """Makes a context in the current Dear ImGui context. Returns its handle."""
    ctx = _lib.ImTypeset_CreateContext()
    if not ctx:
        raise Error(last_error(), "create_context")
    _paragraphs.setdefault(ctx, _weakref.WeakSet())
    return ctx


def destroy_context(ctx: Optional[int] = None) -> None:
    """Destroys a context (None: the current one) and closes its paragraphs and documents."""
    target = ctx if ctx is not None else _lib.ImTypeset_GetCurrentContext()
    if not target:
        return
    for paragraph in list(_paragraphs.pop(target, ())):
        paragraph._handle = None
    _lib.ImTypeset_DestroyContext(target)


def current_context() -> Optional[int]:
    return _lib.ImTypeset_GetCurrentContext() or None


def set_current_context(ctx: Optional[int]) -> None:
    _lib.ImTypeset_SetCurrentContext(ctx)


def stats() -> Stats:
    raw = _capi.Stats()
    _lib.ImTypeset_GetStats(_C.byref(raw))
    return Stats(*(getattr(raw, name) for name in _capi.STATS_FIELDS))


def get_style() -> Style:
    """A copy of the context's style. Change it and hand it to `set_style`."""
    style = Style()
    _lib.ImTypeset_GetStyle(_C.byref(style))
    return style


def set_style(style: Style) -> None:
    _lib.ImTypeset_SetStyle(_style_arg(style))


def set_text_cache_limits(max_paragraphs: int = 0, max_bytes: int = 0) -> None:
    _lib.ImTypeset_SetTextCacheLimits(int(max_paragraphs), int(max_bytes))


# --- Fonts --------------------------------------------------------------------------------


def add_font(face: int, source: Union[str, bytes], face_index: int = 0) -> int:
    """Gives a role its font, from a file name (`str`) or from the file's bytes.

    Returns the ImFont's address. Raises Error with the library's reason if the
    font is refused or the file is not there.
    """
    if isinstance(source, str):
        name = source.encode("utf-8")
        font = _lib.ImTypeset_AddFontFromFileTTF(int(face), *_text_args(name), int(face_index), None)
    else:
        data = bytes(source)
        font = _lib.ImTypeset_AddFontFromMemoryTTF(int(face), data, len(data), int(face_index), None)
    if not font:
        raise Error(last_error(), "add_font")
    return font


def remove_font(face: int) -> bool:
    """False if the role had no font."""
    return bool(_lib.ImTypeset_RemoveFont(int(face)))


def get_font(face: int) -> Optional[int]:
    """The address of the ImFont drawing a role, or None."""
    return _lib.ImTypeset_GetFont(int(face)) or None


# --- Widgets ------------------------------------------------------------------------------


def text(
    text: str, *, flags: int = 0, wrap_width: float = 0.0, style: Optional[Style] = None
) -> bool:
    """ImTypeset::TextUnformatted: one paragraph at the cursor. False: see ERRORS."""
    data = text.encode("utf-8")
    ok = _lib.ImTypeset_TextUnformatted(*_text_args(data), int(flags), wrap_width, _style_arg(style))
    return _outcome(ok, "text")


def text_with_id(
    str_id: str,
    text: str,
    *,
    flags: int = 0,
    wrap_width: float = 0.0,
    style: Optional[Style] = None,
) -> bool:
    """The same, found again by `str_id`: for text that changes every frame."""
    ok = _lib.ImTypeset_TextWithID(
        *_text_args(str_id.encode("utf-8")),
        *_text_args(text.encode("utf-8")),
        int(flags),
        wrap_width,
        _style_arg(style),
    )
    return _outcome(ok, "text_with_id")


def selectable_text(
    str_id: str,
    text: str,
    *,
    flags: int = 0,
    wrap_width: float = 0.0,
    style: Optional[Style] = None,
    selection: Optional[Tuple[int, int]] = None,
) -> Tuple[bool, Optional[Tuple[int, int]]]:
    """Text the user can select and copy. Returns (ok, selection).

    `selection` is (start, end) in characters of `text` and IS the selection,
    as ImGui's `bool*` is a checkbox's state: pass what the last call returned
    (start with (0, 0)). None keeps the selection inside the library and
    returns None for it.
    """
    data = text.encode("utf-8")
    chars = len(text)
    if selection is None:
        pointer = None
    else:
        held = _capi.TextRange(
            _to_byte(data, chars, selection[0]), _to_byte(data, chars, selection[1])
        )
        pointer = _C.byref(held)
    ok = _lib.ImTypeset_SelectableText(
        *_text_args(str_id.encode("utf-8")),
        *_text_args(data),
        int(flags),
        wrap_width,
        _style_arg(style),
        pointer,
    )
    result = _outcome(ok, "selectable_text")
    if selection is None:
        return result, None
    return result, (_to_codepoint(data, chars, held.Start), _to_codepoint(data, chars, held.End))


def calc_text_size(
    text: str, *, wrap_width: float = 0.0, flags: int = 0, style: Optional[Style] = None
) -> Tuple[float, float]:
    """The size the text would have. (0, 0) when it cannot be set at all."""
    size = _capi.Vec2()
    ok = _lib.ImTypeset_CalcTextSize(
        *_text_args(text.encode("utf-8")), wrap_width, int(flags), _style_arg(style), _C.byref(size)
    )
    _outcome(ok, "calc_text_size")
    return size.x, size.y


# --- Paragraphs ---------------------------------------------------------------------------


class Paragraph:
    """Text and runs in; laid out for a width; drawn, hit-tested and copied from.

        p = imtypeset.Paragraph("Some text with $x^2$.", flags=imtypeset.TextFlags.Math)
        p.layout(320.0)
        p.draw(draw_list, (x, y))

    Every position is an index into the `str` given to `set_text`.
    """

    def __init__(self, text: Optional[str] = None, runs: Sequence[Run] = (), flags: int = 0):
        ctx = _lib.ImTypeset_GetCurrentContext()
        handle = _lib.ImTypeset_CreateParagraph()
        if not handle:
            raise Error(last_error(), "Paragraph")
        self._handle: Optional[int] = handle
        self._same = True  # Byte offsets and character indices coincide (ASCII).
        self._vertices = None
        self._indices = None
        _paragraphs.setdefault(ctx, _weakref.WeakSet()).add(self)
        if text is not None:
            self.set_text(text, runs, flags)

    # A paragraph dies with its context, so the handle is dropped there too
    # (destroy_context) and nothing here can reach freed memory.
    def _h(self) -> int:
        if self._handle is None:
            raise ValueError("this Paragraph is closed, or its context was destroyed")
        return self._handle

    def close(self) -> None:
        if self._handle is not None:
            _lib.ImTypeset_DestroyParagraph(self._handle)
            self._handle = None

    def __enter__(self) -> "Paragraph":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:  # The interpreter is shutting down and the library may be gone.
            pass

    @property
    def closed(self) -> bool:
        return self._handle is None

    def _byte(self, offset: int) -> int:
        if self._same:
            return max(int(offset), 0)
        return _count(
            _lib.ImTypeset_ParagraphCodepointToByteOffset(self._h(), int(offset)), "Paragraph"
        )

    def _char(self, offset: int) -> int:
        if self._same or offset < 0:
            return offset
        return _count(_lib.ImTypeset_ParagraphByteToCodepointOffset(self._h(), offset), "Paragraph")

    def _range(self, selection: Tuple[int, int]) -> _capi.TextRange:
        return _capi.TextRange(self._byte(selection[0]), self._byte(selection[1]))

    def set_text(self, text: str, runs: Sequence[Run] = (), flags: int = 0) -> bool:
        """Replaces the text. False if something in it was done differently: `error`."""
        handle = self._h()
        data = text.encode("utf-8")
        chars = len(text)
        array = None
        if runs:
            array = (_capi.Run * len(runs))()
            for slot, run in zip(array, runs):
                run = Run(*run)
                slot.Start = _to_byte(data, chars, run.start)
                slot.End = _to_byte(data, chars, run.end)
                slot.Face = int(run.face)
                slot.Scale = run.scale
                slot.Color = run.color
                slot.Flags = int(run.flags)
                slot.Tag = int(run.tag)
        ok = _lib.ImTypeset_SetParagraphText(
            handle, *_text_args(data), array, len(runs), int(flags)
        )
        self._same = len(data) == chars
        return _outcome(ok, "Paragraph.set_text")

    def layout(self, width: float, style: Optional[Style] = None) -> bool:
        """Breaks the paragraph into lines. Free when nothing changed."""
        ok = _lib.ImTypeset_LayoutParagraph(self._h(), width, _style_arg(style))
        return _outcome(ok, "Paragraph.layout")

    @property
    def error(self) -> ErrorInfo:
        """What the last set_text and layout found wrong; `code` 0 if nothing."""
        raw = _capi.ErrorInfo()
        _lib.ImTypeset_GetParagraphError(self._h(), _C.byref(raw))
        return _error_info(raw)

    @property
    def status(self) -> StatusFlags:
        return StatusFlags(_lib.ImTypeset_GetParagraphStatus(self._h()))

    @property
    def size(self) -> Tuple[float, float]:
        out = _capi.Vec2()
        _must(_lib.ImTypeset_GetParagraphSize(self._h(), _C.byref(out)), "Paragraph.size")
        return out.x, out.y

    @property
    def line_count(self) -> int:
        return _count(_lib.ImTypeset_GetParagraphLineCount(self._h()), "Paragraph.line_count")

    def line(self, index: int) -> Line:
        raw = _capi.Line()
        _must(
            _lib.ImTypeset_GetParagraphLine(self._h(), int(index), _C.byref(raw)), "Paragraph.line"
        )
        return Line(
            (raw.Min.x, raw.Min.y),
            (raw.Max.x, raw.Max.y),
            raw.Baseline,
            self._char(raw.TextStart),
            self._char(raw.TextEnd),
        )

    def lines(self) -> list:
        return [self.line(i) for i in range(self.line_count)]

    def formulas(self) -> list:
        handle = self._h()
        out = []
        count = _count(_lib.ImTypeset_GetParagraphFormulaCount(handle), "Paragraph.formulas")
        for index in range(count):
            raw = _capi.Formula()
            _lib.ImTypeset_GetParagraphFormula(handle, index, _C.byref(raw))
            reason = raw.Reason.decode("utf-8", "replace") if raw.Reason else ""
            out.append(
                Formula(
                    self._char(raw.TextStart),
                    self._char(raw.TextEnd),
                    bool(raw.Display),
                    bool(raw.Refused),
                    raw.CodepointPos,
                    reason,
                )
            )
        return out

    def draw(
        self,
        draw_list: int,
        pos: Tuple[float, float],
        selection: Optional[Tuple[int, int]] = None,
    ) -> None:
        """Draws into an ImDrawList (its address) with the top-left corner at `pos`."""
        handle = self._h()
        held = self._range(selection) if selection is not None else None
        ok = _lib.ImTypeset_DrawParagraph(
            handle,
            draw_list,
            _C.byref(_capi.Vec2(*pos)),
            _C.byref(held) if held is not None else None,
        )
        if not ok:
            raise Error(last_error(), "Paragraph.draw")

    def draw_to_buffers(
        self,
        pos: Tuple[float, float],
        clip_rect: Tuple[float, float, float, float],
        selection: Optional[Tuple[int, int]] = None,
    ) -> Tuple[bytes, bytes]:
        """The paragraph as (vertices, indices): ImDrawVert and ImDrawIdx as bytes.

        Their sizes and the vertex layout are in `build_info()`. Indices start
        at zero for this paragraph.
        """
        handle = self._h()
        held = self._range(selection) if selection is not None else None
        selection_arg = _C.byref(held) if held is not None else None
        vertex_size = _build.sizeof_imdrawvert
        index_size = _build.sizeof_imdrawidx
        n_vertices = _C.c_int(0)
        n_indices = _C.c_int(0)
        while True:
            vertex_room = len(self._vertices) // vertex_size if self._vertices else 0
            index_room = len(self._indices) // index_size if self._indices else 0
            ok = _lib.ImTypeset_DrawParagraphToBuffers(
                handle,
                _C.byref(_capi.Vec2(*pos)),
                _C.byref(_capi.Vec4(*clip_rect)),
                self._vertices,
                vertex_room,
                self._indices,
                index_room,
                selection_arg,
                _C.byref(n_vertices),
                _C.byref(n_indices),
            )
            if not ok:
                raise Error(last_error(), "Paragraph.draw_to_buffers")
            if n_vertices.value <= vertex_room and n_indices.value <= index_room:
                break
            self._vertices = _C.create_string_buffer(max(n_vertices.value, 1) * vertex_size)
            self._indices = _C.create_string_buffer(max(n_indices.value, 1) * index_size)
        vertices = self._vertices.raw[: n_vertices.value * vertex_size] if self._vertices else b""
        indices = self._indices.raw[: n_indices.value * index_size] if self._indices else b""
        return vertices, indices

    def hit_test(self, local_pos: Tuple[float, float]) -> Hit:
        raw = _capi.Hit()
        _must(
            _lib.ImTypeset_HitTestParagraph(
                self._h(), _C.byref(_capi.Vec2(*local_pos)), _C.byref(raw)
            ),
            "Paragraph.hit_test",
        )
        return Hit(self._char(raw.TextOffset), raw.Line, bool(raw.Inside))

    def select_range(
        self,
        anchor: Tuple[float, float],
        active: Tuple[float, float],
        granularity: int = Granularity.Character,
    ) -> Tuple[int, int]:
        """What a drag from `anchor` to `active` selects, as (start, end)."""
        raw = _capi.TextRange()
        _must(
            _lib.ImTypeset_SelectParagraphRange(
                self._h(),
                _C.byref(_capi.Vec2(*anchor)),
                _C.byref(_capi.Vec2(*active)),
                int(granularity),
                _C.byref(raw),
            ),
            "Paragraph.select_range",
        )
        return self._char(raw.Start), self._char(raw.End)

    def range_rects(self, selection: Tuple[int, int]) -> list:
        """One (min x, min y, max x, max y) per line the range touches."""
        handle = self._h()
        held = self._range(selection)
        room = 16
        while True:
            rects = (_capi.Vec4 * room)()
            count = _count(
                _lib.ImTypeset_GetParagraphRangeRects(handle, _C.byref(held), rects, room),
                "Paragraph.range_rects",
            )
            if count <= room:
                return [(r.x, r.y, r.z, r.w) for r in rects[:count]]
            room = count

    def caret_rect(self, offset: int) -> Tuple[float, float, float, float]:
        raw = _capi.Vec4()
        _must(
            _lib.ImTypeset_GetParagraphCaretRect(self._h(), self._byte(offset), _C.byref(raw)),
            "Paragraph.caret_rect",
        )
        return raw.x, raw.y, raw.z, raw.w

    def range_text(self, selection: Tuple[int, int], after_text: bool = False) -> str:
        """The text of the range as it was written: what copying it gives."""
        handle = self._h()
        held = self._range(selection)
        room = 256
        while True:
            buffer = _C.create_string_buffer(room)
            length = _count(
                _lib.ImTypeset_AppendParagraphRangeText(
                    handle, _C.byref(held), 1 if after_text else 0, buffer, room
                ),
                "Paragraph.range_text",
            )
            if length < room:
                return buffer.raw[:length].decode("utf-8", "replace")
            room = length + 1


# --- Documents ----------------------------------------------------------------------------

# ImTypeset_Block and ImTypeset_Run as struct formats: an edit packs its blocks
# straight into the bytes the library reads, one pack a block, rather than
# setting seventeen ctypes fields a block.
_BLOCK = _struct.Struct("=10if3if2i")
_RUN = _struct.Struct("=3ifI2i")
assert _BLOCK.size == _C.sizeof(_capi.Block) and _RUN.size == _C.sizeof(_capi.Run)


class _Marks:
    """Marks as the library reads them: what `Document.pack_marks` returns."""

    __slots__ = ("array", "count")

    def __init__(self, array, count):
        self.array = array
        self.count = count


def _byte_offsets(text: str, wanted) -> dict:
    """{index into `text`: byte offset} for each index wanted, in one pass over the text."""
    out = {}
    previous = 0
    total = 0
    for offset in sorted(set(wanted)):
        index = min(max(offset, 0), len(text))
        total += len(text[previous:index].encode("utf-8"))
        previous = index
        out[offset] = total
    return out


class Document:
    """Styled blocks in; layout, drawing, hit testing and copying out.

        doc = imtypeset.Document.from_blocks(
            [imtypeset.Block(imtypeset.BlockKind.Heading, "Title", style=1),
             imtypeset.Block(text="A paragraph with a link.", runs=[imtypeset.Run(17, 21, tag=7)])],
            styles=[imtypeset.BlockStyle(SpaceBelow=8.0),
                    imtypeset.BlockStyle(Face=imtypeset.Face.Bold, Scale=1.5)])
        ...each frame:
        doc.layout(width)
        doc.draw(draw_list, (x, y))

    What each call does is in include/imtypeset.h under "Documents". From here:

    ONE CALL INTO THE LIBRARY PER EDIT. `set_blocks` and `replace_blocks` pack
    every block's text into one buffer and make one call; `append_text` makes
    one. A frame is `layout` and `draw` (and `hit_test` when the mouse
    matters): one call each, whatever the number of blocks.

    POSITIONS are (block, index into that block's `str`), in and out, where
    the library's are (block, byte offset). The conversion is per block and is
    done here: a block whose text is ASCII converts for nothing; for another
    one this object keeps the block's text and its UTF-8, and a conversion
    encodes or decodes the part of that ONE block before the position. Nothing
    is ever linear in the document.

    STYLES AND CONTAINERS are `BlockStyle` and `Container` (the header's
    structs; fields by keyword). A block names a style by index and a
    container by 1 + its index. `replace_blocks` APPENDS the styles and
    containers it is given to the document's: `style_count` and
    `container_count` say where the new ones start.

    An edit that the library applied only in part (an index that names no
    style, a Table block) raises `Error` AFTER the blocks are set as far as
    they could be; the document is consistent and may be used.
    """

    def __init__(self):
        ctx = _lib.ImTypeset_GetCurrentContext()
        handle = _lib.ImTypeset_CreateDocument()
        if not handle:
            raise Error(last_error(), "Document")
        self._handle: Optional[int] = handle
        # One entry a block: None when its text is ASCII, else
        # [n, text, data]: after `n` ASCII bytes come `text` and its UTF-8.
        self._texts: list = []
        self._last_size = 0  # Bytes of text the last block holds.
        self._vertices = None
        self._indices = None
        self._copy = _C.create_string_buffer(1024)
        _paragraphs.setdefault(ctx, _weakref.WeakSet()).add(self)

    @classmethod
    def from_blocks(
        cls, blocks: Sequence, styles: Sequence = (), containers: Sequence = ()
    ) -> "Document":
        document = cls()
        document.set_blocks(blocks, styles, containers)
        return document

    def _h(self) -> int:
        if self._handle is None:
            raise ValueError("this Document is closed, or its context was destroyed")
        return self._handle

    def close(self) -> None:
        if self._handle is not None:
            _lib.ImTypeset_DestroyDocument(self._handle)
            self._handle = None

    def __enter__(self) -> "Document":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:  # The interpreter is shutting down and the library may be gone.
            pass

    @property
    def closed(self) -> bool:
        return self._handle is None

    def __len__(self) -> int:
        return len(self._texts)

    # --- Positions ---

    def _byte(self, block: int, offset: int) -> int:
        if offset <= 0:
            return 0
        entry = self._texts[block] if 0 <= block < len(self._texts) else None
        if entry is None:
            return offset
        ascii_bytes, text, data = entry
        if offset <= ascii_bytes:
            return offset
        if offset >= ascii_bytes + len(text):
            return ascii_bytes + len(data)
        return ascii_bytes + len(text[: offset - ascii_bytes].encode("utf-8"))

    def _char(self, block: int, offset: int) -> int:
        entry = self._texts[block] if 0 <= block < len(self._texts) else None
        if entry is None or offset <= entry[0]:
            return offset
        ascii_bytes, _, data = entry
        return ascii_bytes + len(data[: offset - ascii_bytes].decode("utf-8", "ignore"))

    def _position(self, position) -> _capi.Position:
        block, offset = position
        return _capi.Position(block, self._byte(block, offset))

    def _range(self, given) -> _capi.DocumentRange:
        start, end = given
        return _capi.DocumentRange(self._position(start), self._position(end))

    def _range_out(self, raw: _capi.DocumentRange) -> DocumentRange:
        return DocumentRange(
            Position(raw.Start.Block, self._char(raw.Start.Block, raw.Start.Offset)),
            Position(raw.End.Block, self._char(raw.End.Block, raw.End.Offset)),
        )

    # --- Edits ---

    @staticmethod
    def _pack(blocks: Sequence, styles: Sequence, containers: Sequence):
        """(content, what it points into, the blocks' entries, the last block's bytes)."""
        chunks = []
        runs = []
        entries = []
        packed = bytearray(_BLOCK.size * len(blocks))
        at = 0
        last = 0
        for index, block in enumerate(blocks):
            if type(block) is not Block:
                block = Block(*block)
            text = block.text
            data = text.encode("utf-8")
            start = at
            at += len(data)
            chunks.append(data)
            same = len(data) == len(text)
            entries.append(None if same else [0, text, data])
            last = len(data)
            marker_start = marker_end = 0
            if block.marker:
                marker = block.marker.encode("utf-8")
                chunks.append(marker)
                marker_start = at
                at += len(marker)
                marker_end = at
            run_start = len(runs)
            if block.runs:
                given = [run if type(run) is Run else Run(*run) for run in block.runs]
                if same:
                    size = len(data)
                    for run in given:
                        runs.append(
                            _RUN.pack(
                                min(max(run.start, 0), size),
                                min(max(run.end, 0), size),
                                int(run.face),
                                run.scale,
                                run.color,
                                int(run.flags),
                                int(run.tag),
                            )
                        )
                else:
                    where = _byte_offsets(text, [r.start for r in given] + [r.end for r in given])
                    for run in given:
                        runs.append(
                            _RUN.pack(
                                where[run.start],
                                where[run.end],
                                int(run.face),
                                run.scale,
                                run.color,
                                int(run.flags),
                                int(run.tag),
                            )
                        )
            _BLOCK.pack_into(
                packed,
                index * _BLOCK.size,
                int(block.kind),
                int(block.flags),
                int(block.level),
                int(block.style),
                start,
                start + len(data),
                run_start,
                len(runs) - run_start,
                marker_start,
                marker_end,
                block.left_indent,
                int(block.quote_depth),
                int(block.container),
                int(block.item),
                block.extent,
                0,
                0,
            )
        text_bytes = b"".join(chunks)
        run_bytes = b"".join(runs)
        for name, given, kind in (("styles", styles, BlockStyle), ("containers", containers, Container)):
            for one in given:
                if not isinstance(one, kind):
                    raise TypeError(f"{name} holds imtypeset.{kind.__name__} and nothing else")
        style_array = (BlockStyle * len(styles))(*styles) if styles else None
        container_array = (Container * len(containers))(*containers) if containers else None
        block_array = (_C.c_char * len(packed)).from_buffer(packed) if packed else None
        content = _capi.DocumentContent()
        content.Text = _C.cast(_C.c_char_p(text_bytes), _C.c_void_p)
        content.TextSize = len(text_bytes)
        content.Blocks = _C.addressof(block_array) if packed else None
        content.BlockCount = len(blocks)
        content.Runs = _C.cast(_C.c_char_p(run_bytes), _C.c_void_p) if runs else None
        content.RunCount = len(runs)
        content.Styles = _C.addressof(style_array) if styles else None
        content.StyleCount = len(styles)
        content.Containers = _C.addressof(container_array) if containers else None
        content.ContainerCount = len(containers)
        held = (text_bytes, run_bytes, block_array, style_array, container_array)
        return content, held, entries, last

    def set_blocks(self, blocks: Sequence, styles: Sequence = (), containers: Sequence = ()) -> bool:
        """Replaces everything the document holds. One call into the library.
        False, with `last_error()` saying ErrorCode.DescriptionCut, when
        something in the description was wrong and was cut to what is right
        (a table, an index that names nothing): the blocks are set."""
        handle = self._h()
        content, held, entries, last = self._pack(blocks, styles, containers)
        ok = _lib.ImTypeset_SetDocumentBlocks(handle, _C.byref(content))
        del held
        self._texts = entries
        self._last_size = last
        return _outcome(ok, "Document.set_blocks")

    def replace_blocks(
        self,
        first: int,
        count: int,
        blocks: Sequence = (),
        styles: Sequence = (),
        containers: Sequence = (),
    ) -> bool:
        """Replaces blocks [first, first + count) by `blocks`: an insertion when
        `count` is 0, a removal when `blocks` is empty. One call into the library.
        False as `set_blocks` is."""
        handle = self._h()
        first, count = int(first), int(count)
        if first < 0 or count < 0 or first + count > len(self._texts):
            raise ValueError(
                f"blocks [{first}, {first + count}) are not all in a document of {len(self._texts)}"
            )
        content, held, entries, last = self._pack(blocks, styles, containers)
        touches_end = first + count == len(self._texts)
        ok = _lib.ImTypeset_ReplaceDocumentBlocks(handle, first, count, _C.byref(content))
        del held
        self._texts[first : first + count] = entries
        if touches_end:
            if entries:
                self._last_size = last
            else:
                self._last_size = (
                    _lib.ImTypeset_GetDocumentBlockTextSize(handle, len(self._texts) - 1)
                    if self._texts
                    else 0
                )
        return _outcome(ok, "Document.replace_blocks")

    def append_text(self, text: str, runs: Sequence = ()) -> None:
        """Appends to the text of the LAST block: text that arrives a piece at a
        time. `runs` are further runs of that block, indexing its whole text.
        Only that block is laid out again. One call into the library."""
        handle = self._h()
        if not self._texts:
            raise ValueError("append_text: the document has no block")
        data = text.encode("utf-8")
        entry = self._texts[-1]
        if entry is None:
            if len(data) != len(text):
                entry = self._texts[-1] = [self._last_size, text, data]
        else:
            entry[1] += text
            entry[2] += data
        self._last_size += len(data)
        array = None
        if runs:
            given = [run if type(run) is Run else Run(*run) for run in runs]
            block = len(self._texts) - 1
            array = _C.create_string_buffer(
                b"".join(
                    _RUN.pack(
                        self._byte(block, run.start),
                        self._byte(block, run.end),
                        int(run.face),
                        run.scale,
                        run.color,
                        int(run.flags),
                        int(run.tag),
                    )
                    for run in given
                )
            )
        ok = _lib.ImTypeset_AppendDocumentText(handle, *_text_args(data), array, len(runs))
        if not ok:
            # Nothing was appended unless the library says it did it differently.
            info = last_error()
            if info.code not in Error.SOFT:
                size = _lib.ImTypeset_GetDocumentBlockTextSize(handle, len(self._texts) - 1)
                if size != self._last_size:
                    self._last_size -= len(data)
                    if entry is not None:
                        entry[1] = entry[1][: len(entry[1]) - len(text)]
                        entry[2] = entry[2][: len(entry[2]) - len(data)]
                raise Error(info, "Document.append_text")

    def set_block_extent(self, block: int, extent: float) -> None:
        """A Rule's thickness or a Host block's height, in pixels."""
        _outcome(
            _lib.ImTypeset_SetDocumentBlockExtent(self._h(), int(block), extent),
            "Document.set_block_extent",
        )

    def set_options(self, options: Optional[DocumentOptions] = None) -> None:
        if options is not None and not isinstance(options, DocumentOptions):
            raise TypeError("options is an imtypeset.DocumentOptions or None")
        _outcome(
            _lib.ImTypeset_SetDocumentOptions(
                self._h(), _C.byref(options) if options is not None else None
            ),
            "Document.set_options",
        )

    @property
    def block_count(self) -> int:
        return _count(_lib.ImTypeset_GetDocumentBlockCount(self._h()), "Document.block_count")

    @property
    def style_count(self) -> int:
        return _count(_lib.ImTypeset_GetDocumentStyleCount(self._h()), "Document.style_count")

    @property
    def container_count(self) -> int:
        return _count(
            _lib.ImTypeset_GetDocumentContainerCount(self._h()), "Document.container_count"
        )

    # --- Layout ---

    def layout(self, width: float, style: Optional[Style] = None) -> bool:
        """Says how wide the column is. Lays nothing out: a block is broken into
        lines when a draw, a hit test or a question first reaches it."""
        ok = _lib.ImTypeset_LayoutDocument(self._h(), width, _style_arg(style))
        return _outcome(ok, "Document.layout")

    def layout_window(self, top: float, bottom: float) -> bool:
        """Lays out the blocks between two heights. False if one of them has
        something wrong with it: `last_error()`, `block_error(i)`."""
        ok = _lib.ImTypeset_LayoutDocumentWindow(self._h(), top, bottom)
        return _outcome(ok, "Document.layout_window")

    def size(self) -> Tuple[float, float]:
        """The column's width and the document's height; the height is an
        estimate until everything has been laid out at this width."""
        out = _capi.Vec2()
        _must(_lib.ImTypeset_GetDocumentSize(self._h(), _C.byref(out)), "Document.size")
        return out.x, out.y

    def stats(self) -> DocumentStats:
        raw = _capi.DocumentStats()
        _must(_lib.ImTypeset_GetDocumentStats(self._h(), _C.byref(raw)), "Document.stats")
        return DocumentStats(*(getattr(raw, name) for name in _capi.DOCUMENT_STATS_FIELDS))

    def block_error(self, block: int) -> ErrorInfo:
        """What was last found wrong with one block; `code` 0 if nothing, or if
        it has not been laid out. `codepoint_pos` indexes the block's `str`."""
        raw = _capi.ErrorInfo()
        _must(
            _lib.ImTypeset_GetDocumentBlockError(self._h(), int(block), _C.byref(raw)),
            "Document.block_error",
        )
        return _error_info(raw)

    # --- Drawing ---

    def pack_marks(self, marks: Sequence) -> _Marks:
        """Marks converted once, for `draw` and `view` to take every frame at
        no cost. Good until a block one of them names changes its text."""
        array = (_capi.Mark * max(len(marks), 1))()
        for slot, mark in zip(array, marks):
            mark = mark if type(mark) is Mark else Mark(*mark)
            slot.Range = self._range(mark.range)
            slot.Color = mark.color
            slot.Place = int(mark.place)
            slot.Thickness = mark.thickness
            slot.Offset = mark.offset
        return _Marks(array, len(marks))

    def _draw_arg(self, selection, marks, debug: bool):
        """(pointer or None, what it points into)."""
        if selection is None and not marks and not debug:
            return None, None
        draw = _capi.DocumentDraw()
        if selection is not None:
            draw.Selection = self._range(selection)
        if marks:
            packed = marks if isinstance(marks, _Marks) else self.pack_marks(marks)
            if packed.count:
                draw.Marks = _C.addressof(packed.array)
                draw.MarkCount = packed.count
        else:
            packed = None
        if debug:
            draw.Flags = 1
        return _C.byref(draw), (draw, packed)

    def draw(
        self,
        draw_list: int,
        pos: Tuple[float, float],
        selection=None,
        marks=(),
        debug: bool = False,
    ) -> bool:
        """Draws into an ImDrawList (its address) with the top-left corner at
        `pos`, laying out what its clip rectangle shows. `selection` is a
        ((block, offset), (block, offset)); `marks` a sequence of `Mark` or
        what `pack_marks` returned. False: something in view was set differently."""
        pointer, held = self._draw_arg(selection, marks, debug)
        ok = _lib.ImTypeset_DrawDocument(
            self._h(), draw_list, _C.byref(_capi.Vec2(*pos)), pointer
        )
        del held
        return _outcome(ok, "Document.draw")

    def draw_to_buffers(
        self,
        pos: Tuple[float, float],
        clip_rect: Tuple[float, float, float, float],
        selection=None,
        marks=(),
        debug: bool = False,
    ) -> Tuple[bytes, bytes]:
        """What `draw` would emit, as (vertices, indices): see Paragraph.draw_to_buffers."""
        handle = self._h()
        pointer, held = self._draw_arg(selection, marks, debug)
        vertex_size = _build.sizeof_imdrawvert
        index_size = _build.sizeof_imdrawidx
        n_vertices = _C.c_int(0)
        n_indices = _C.c_int(0)
        while True:
            vertex_room = len(self._vertices) // vertex_size if self._vertices else 0
            index_room = len(self._indices) // index_size if self._indices else 0
            ok = _lib.ImTypeset_DrawDocumentToBuffers(
                handle,
                _C.byref(_capi.Vec2(*pos)),
                _C.byref(_capi.Vec4(*clip_rect)),
                self._vertices,
                vertex_room,
                self._indices,
                index_room,
                pointer,
                _C.byref(n_vertices),
                _C.byref(n_indices),
            )
            if not ok:
                info = last_error()
                if info.code not in Error.SOFT:
                    raise Error(info, "Document.draw_to_buffers")
            if n_vertices.value <= vertex_room and n_indices.value <= index_room:
                break
            self._vertices = _C.create_string_buffer(max(n_vertices.value, 1) * vertex_size)
            self._indices = _C.create_string_buffer(max(n_indices.value, 1) * index_size)
        del held
        vertices = self._vertices.raw[: n_vertices.value * vertex_size] if self._vertices else b""
        indices = self._indices.raw[: n_indices.value * index_size] if self._indices else b""
        return vertices, indices

    def view(
        self,
        str_id: str,
        size: Tuple[float, float] = (0.0, 0.0),
        selection=None,
        marks=(),
        style: Optional[Style] = None,
        debug: bool = False,
    ) -> Tuple[bool, Optional[DocumentRange]]:
        """ImTypeset::DocumentView: the document in a child window that scrolls
        it, selects with the mouse and copies with Ctrl+C. Returns (ok, selection).

        `selection` IS the selection, as for `selectable_text`: pass what the
        last call returned (start with ((0, 0), (0, 0))). None keeps it inside
        the library and returns None for it.
        """
        handle = self._h()
        pointer, held = self._draw_arg(None, marks, debug)
        raw = self._range(selection) if selection is not None else None
        ok = _lib.ImTypeset_DocumentView(
            *_text_args(str_id.encode("utf-8")),
            handle,
            _C.byref(_capi.Vec2(*size)),
            _C.byref(raw) if raw is not None else None,
            pointer,
            _style_arg(style),
        )
        del held
        result = _outcome(ok, "Document.view")
        return result, (self._range_out(raw) if raw is not None else None)

    # --- Hit testing, selection, copy ---

    def hit_test(self, local_pos: Tuple[float, float]) -> DocumentHit:
        """Where a point falls; the point is relative to the document's top-left corner."""
        raw = _capi.DocumentHit()
        _must(
            _lib.ImTypeset_HitTestDocument(
                self._h(), _C.byref(_capi.Vec2(*local_pos)), _C.byref(raw)
            ),
            "Document.hit_test",
        )
        block = raw.Position.Block
        return DocumentHit(
            Position(block, self._char(block, raw.Position.Offset)),
            raw.Line,
            raw.Tag,
            bool(raw.Inside),
            bool(raw.InBlock),
        )

    def select_range(
        self,
        anchor: Tuple[float, float],
        active: Tuple[float, float],
        granularity: int = Granularity.Character,
    ) -> DocumentRange:
        """What a drag from `anchor` to `active` selects, across blocks."""
        raw = _capi.DocumentRange()
        _must(
            _lib.ImTypeset_SelectDocumentRange(
                self._h(),
                _C.byref(_capi.Vec2(*anchor)),
                _C.byref(_capi.Vec2(*active)),
                int(granularity),
                _C.byref(raw),
            ),
            "Document.select_range",
        )
        return self._range_out(raw)

    def range_rects(self, given) -> list:
        """One (min x, min y, max x, max y) per line the range touches."""
        handle = self._h()
        held = self._range(given)
        room = 16
        while True:
            rects = (_capi.Vec4 * room)()
            count = _count(
                _lib.ImTypeset_GetDocumentRangeRects(handle, _C.byref(held), rects, room),
                "Document.range_rects",
            )
            if count <= room:
                return [(r.x, r.y, r.z, r.w) for r in rects[:count]]
            room = count

    def caret_rect(self, position) -> Tuple[float, float, float, float]:
        raw = _capi.Vec4()
        _must(
            _lib.ImTypeset_GetDocumentCaretRect(
                self._h(), _C.byref(self._position(position)), _C.byref(raw)
            ),
            "Document.caret_rect",
        )
        return raw.x, raw.y, raw.z, raw.w

    def range_text(self, given, separator: Optional[str] = None) -> str:
        """The text of the range: what copying it gives. `separator` goes
        between two blocks; None is a blank line."""
        handle = self._h()
        held = self._range(given)
        if separator is None:
            between = (None, None)
        else:
            # Always (begin, end): an empty separator is not "none given".
            encoded = separator.encode("utf-8")
            begin = _C.cast(_C.c_char_p(encoded), _C.c_void_p).value or 0
            between = (encoded, begin + len(encoded))
        while True:
            room = len(self._copy)
            length = _count(
                _lib.ImTypeset_AppendDocumentRangeText(
                    handle, _C.byref(held), *between, self._copy, room
                ),
                "Document.range_text",
            )
            if length < room:
                return self._copy.raw[:length].decode("utf-8", "replace")
            self._copy = _C.create_string_buffer(length + 1)

    def block_geometry(self, block: int) -> BlockGeometry:
        """Where a block is, relative to the document's top-left corner. Lays it out."""
        raw = _capi.BlockGeometry()
        _must(
            _lib.ImTypeset_GetDocumentBlockGeometry(self._h(), int(block), _C.byref(raw)),
            "Document.block_geometry",
        )
        return BlockGeometry(
            (raw.Min.x, raw.Min.y),
            (raw.Max.x, raw.Max.y),
            (raw.FullMin.x, raw.FullMin.y),
            (raw.FullMax.x, raw.FullMax.y),
            raw.LineCount,
            bool(raw.LaidOut),
        )

    def block_line(self, block: int, line: int) -> Line:
        """Line `line` of a block, relative to the block's `full_min`."""
        raw = _capi.Line()
        block = int(block)
        _must(
            _lib.ImTypeset_GetDocumentBlockLine(self._h(), block, int(line), _C.byref(raw)),
            "Document.block_line",
        )
        return Line(
            (raw.Min.x, raw.Min.y),
            (raw.Max.x, raw.Max.y),
            raw.Baseline,
            self._char(block, raw.TextStart),
            self._char(block, raw.TextEnd),
        )


# --- Demo ---------------------------------------------------------------------------------


def show_demo_window(open: Optional[bool] = None) -> Optional[bool]:
    """ImTypeset::ShowDemoWindow. With `open` given, returns whether the window
    is still open (its close button answers False); with None there is no close
    button and None comes back. A library built with IMTYPESET_DISABLE_DEMO
    (the one hosted in Dear ImGui Bundle's wheel is) draws nothing."""
    if open is None:
        _must(_lib.ImTypeset_ShowDemoWindow(None), "show_demo_window")
        return None
    state = _C.c_int(1 if open else 0)
    _must(_lib.ImTypeset_ShowDemoWindow(_C.byref(state)), "show_demo_window")
    return bool(state.value)
