"""include/imtypeset_c.h, as ctypes sees it: the structs, the prototypes, and
the loading of the library. Nothing here is for a program to call; the package's
`__init__` is.

The structs mirror the header field for field. Their sizes are handed to the
library when the package is imported (ImTypeset_DebugCheckVersionAndDataLayout,
ImTypeset_DebugCheckDocumentDataLayout, ImTypeset_DebugCheckTableDataLayout and
ImTypeset_DebugCheckColumnDataLayout),
so a header that moved under this file is an ImportError and not a wrong answer.
"""

from __future__ import annotations

import ctypes as C
import os

C_ABI_VERSION = 1  # IMTYPESET_C_ABI_VERSION this file was written against.
C_VERSION = "0.2.0"  # IMTYPESET_C_VERSION


class Vec2(C.Structure):
    _fields_ = [("x", C.c_float), ("y", C.c_float)]


class Vec4(C.Structure):
    _fields_ = [("x", C.c_float), ("y", C.c_float), ("z", C.c_float), ("w", C.c_float)]


class Style(C.Structure):
    """ImTypesetStyle. All zero is the default; fields are the header's."""

    _fields_ = [
        ("Face", C.c_int),
        ("FontSize", C.c_float),
        ("EmSize", C.c_float),
        ("LineHeight", C.c_float),
        ("Align", C.c_int),
        ("LeftIndent", C.c_float),
        ("FirstLineIndent", C.c_float),
        ("Spacing", C.c_int),
        ("LineBreak", C.c_int),
        ("PixelsPerInch", C.c_float),
        ("Color", C.c_uint),
        ("SelectionColor", C.c_uint),
    ]


class Run(C.Structure):
    _fields_ = [
        ("Start", C.c_int),
        ("End", C.c_int),
        ("Face", C.c_int),
        ("Scale", C.c_float),
        ("Color", C.c_uint),
        ("Flags", C.c_int),
        ("Tag", C.c_int),
    ]


class TextRange(C.Structure):
    _fields_ = [("Start", C.c_int), ("End", C.c_int)]


class Line(C.Structure):
    _fields_ = [
        ("Min", Vec2),
        ("Max", Vec2),
        ("Baseline", C.c_float),
        ("TextStart", C.c_int),
        ("TextEnd", C.c_int),
    ]


class Hit(C.Structure):
    _fields_ = [("TextOffset", C.c_int), ("Line", C.c_int), ("Inside", C.c_bool), ("Tag", C.c_int)]


class ErrorInfo(C.Structure):
    _fields_ = [
        ("Code", C.c_int),
        ("BytePos", C.c_int),
        ("CodepointPos", C.c_int),
        ("Message", C.c_char_p),
    ]


class Formula(C.Structure):
    _fields_ = [
        ("TextStart", C.c_int),
        ("TextEnd", C.c_int),
        ("Display", C.c_bool),
        ("Refused", C.c_bool),
        ("BytePos", C.c_int),
        ("CodepointPos", C.c_int),
        ("Reason", C.c_char_p),
    ]


STATS_FIELDS = (
    "TextBuilds",
    "Layouts",
    "DeferredLayouts",
    "DrawCalls",
    "CulledCalls",
    "VerticesEmitted",
    "BytesHashed",
    "CacheEvictions",
    "CachedParagraphs",
    "CachedBytes",
)


class Stats(C.Structure):
    _fields_ = [(name, C.c_ulonglong) for name in STATS_FIELDS]


# --- Documents: include/imtypeset_c.h's "Documents" structs ------------------------------


class BlockStyle(C.Structure):
    """ImTypesetBlockStyle. All zero is the document's style, justified, no space."""

    _fields_ = [
        ("Face", C.c_int),
        ("Scale", C.c_float),
        ("LineHeight", C.c_float),
        ("Align", C.c_int),
        ("FirstLineIndent", C.c_float),
        ("SpaceAbove", C.c_float),
        ("SpaceBelow", C.c_float),
        ("Color", C.c_uint),
        ("MarkerFace", C.c_int),
        ("MarkerColor", C.c_uint),
        ("MarkerGap", C.c_float),
    ]


class Container(C.Structure):
    _fields_ = [
        ("Kind", C.c_int),
        ("Parent", C.c_int),
        ("Indent", C.c_float),
        ("Flags", C.c_int),
        ("Color", C.c_uint),
    ]


class Block(C.Structure):
    _fields_ = [
        ("Kind", C.c_int),
        ("Flags", C.c_int),
        ("Level", C.c_int),
        ("Style", C.c_int),
        ("TextStart", C.c_int),
        ("TextEnd", C.c_int),
        ("RunStart", C.c_int),
        ("RunCount", C.c_int),
        ("MarkerStart", C.c_int),
        ("MarkerEnd", C.c_int),
        ("LeftIndent", C.c_float),
        ("QuoteDepth", C.c_int),
        ("Container", C.c_int),
        ("Item", C.c_int),
        ("Extent", C.c_float),
        ("CellStart", C.c_int),
        ("CellCount", C.c_int),
        ("ColumnStart", C.c_int),
        ("ColumnCount", C.c_int),
    ]


class Cell(C.Structure):
    """ImTypesetCell: [TextStart, TextEnd) of its block's text."""

    _fields_ = [("TextStart", C.c_int), ("TextEnd", C.c_int), ("Flags", C.c_int)]


class TableColumn(C.Structure):
    _fields_ = [("Align", C.c_int), ("Width", C.c_float), ("Share", C.c_float)]


class DocumentContent(C.Structure):
    _fields_ = [
        ("Text", C.c_void_p),
        ("TextSize", C.c_int),
        ("Blocks", C.c_void_p),
        ("BlockCount", C.c_int),
        ("Runs", C.c_void_p),
        ("RunCount", C.c_int),
        ("Styles", C.c_void_p),
        ("StyleCount", C.c_int),
        ("Containers", C.c_void_p),
        ("ContainerCount", C.c_int),
        ("Cells", C.c_void_p),
        ("CellCount", C.c_int),
        ("Columns", C.c_void_p),
        ("ColumnCount", C.c_int),
    ]


class DocumentOptions(C.Structure):
    """ImTypesetDocumentOptions. All zero is the default."""

    _fields_ = [
        ("QuoteIndent", C.c_float),
        ("QuoteBarThickness", C.c_float),
        ("QuoteBarColor", C.c_uint),
        ("RuleColor", C.c_uint),
        ("CodeOverflow", C.c_int),
        ("TableCellPadding", C.c_float),
        ("TableRuleThickness", C.c_float),
        ("TableRuleColor", C.c_uint),
    ]


class Position(C.Structure):
    _fields_ = [("Block", C.c_int), ("Offset", C.c_int)]


class DocumentRange(C.Structure):
    _fields_ = [("Start", Position), ("End", Position)]


class Mark(C.Structure):
    _fields_ = [
        ("Range", DocumentRange),
        ("Color", C.c_uint),
        ("Place", C.c_int),
        ("Thickness", C.c_float),
        ("Offset", C.c_float),
    ]


class DocumentDraw(C.Structure):
    _fields_ = [
        ("Selection", DocumentRange),
        ("Marks", C.c_void_p),
        ("MarkCount", C.c_int),
        ("Flags", C.c_int),
    ]


class DocumentHit(C.Structure):
    _fields_ = [
        ("Position", Position),
        ("Line", C.c_int),
        ("Tag", C.c_int),
        ("Inside", C.c_bool),
        ("InBlock", C.c_bool),
        ("Row", C.c_int),
        ("Column", C.c_int),
    ]


class BlockGeometry(C.Structure):
    _fields_ = [
        ("Min", Vec2),
        ("Max", Vec2),
        ("FullMin", Vec2),
        ("FullMax", Vec2),
        ("LineCount", C.c_int),
        ("LaidOut", C.c_bool),
    ]


class TableGeometry(C.Structure):
    _fields_ = [
        ("RowCount", C.c_int),
        ("ColumnCount", C.c_int),
        ("CellCount", C.c_int),
        ("Min", Vec2),
        ("Max", Vec2),
        ("Stated", C.c_bool),
        ("LaidOut", C.c_bool),
    ]


class TableColumnGeometry(C.Structure):
    _fields_ = [
        ("Min", C.c_float),
        ("Max", C.c_float),
        ("Natural", C.c_float),
        ("Floor", C.c_float),
    ]


class TableRowGeometry(C.Structure):
    _fields_ = [
        ("Min", C.c_float),
        ("Max", C.c_float),
        ("CellStart", C.c_int),
        ("CellCount", C.c_int),
        ("Wrapped", C.c_bool),
    ]


class CellPosition(C.Structure):
    _fields_ = [("Row", C.c_int), ("Column", C.c_int), ("Cell", C.c_int), ("Offset", C.c_int)]


class Columns(C.Structure):
    """ImTypesetColumns. All zero is no columns."""

    _fields_ = [
        ("Count", C.c_int),
        ("Height", C.c_float),
        ("Gutter", C.c_float),
        ("Flags", C.c_int),
    ]


class ColumnGeometry(C.Structure):
    _fields_ = [
        ("Min", Vec2),
        ("Max", Vec2),
        ("ContentMin", Vec2),
        ("ContentMax", Vec2),
        ("Start", Position),
        ("End", Position),
        ("UncutTop", C.c_float),
        ("Overfull", C.c_bool),
        ("Exists", C.c_bool),
    ]


DOCUMENT_STATS_FIELDS = (
    "MaterialBuilds",
    "BlocksLaidOut",
    "UnitsStacked",
    "StructureVisits",
    "BlocksDrawn",
    "Blocks",
    "BlocksDeferred",
    "TableCellsMeasured",
    "TableRowsSet",
    "ColumnsCut",
    "BalanceHeightsTried",
)


class DocumentStats(C.Structure):
    _fields_ = (
        [(name, C.c_ulonglong) for name in DOCUMENT_STATS_FIELDS[:5]]
        + [("Blocks", C.c_int), ("BlocksDeferred", C.c_int)]
        + [(name, C.c_ulonglong) for name in DOCUMENT_STATS_FIELDS[7:]]
    )


# The structs ImTypeset_DebugCheckDocumentDataLayout() is told the sizes of, in
# its parameters' order; each has a `Sizeof<name>` in ImTypeset_BuildInfo.
DOCUMENT_STRUCTS = (
    ("Block", Block),
    ("BlockStyle", BlockStyle),
    ("Container", Container),
    ("DocumentContent", DocumentContent),
    ("DocumentOptions", DocumentOptions),
    ("Position", Position),
    ("DocumentRange", DocumentRange),
    ("Mark", Mark),
    ("DocumentDraw", DocumentDraw),
    ("DocumentHit", DocumentHit),
    ("BlockGeometry", BlockGeometry),
    ("DocumentStats", DocumentStats),
)

# The same for ImTypeset_DebugCheckTableDataLayout().
TABLE_STRUCTS = (
    ("Cell", Cell),
    ("TableColumn", TableColumn),
    ("TableGeometry", TableGeometry),
    ("TableColumnGeometry", TableColumnGeometry),
    ("TableRowGeometry", TableRowGeometry),
    ("CellPosition", CellPosition),
)

# The same for ImTypeset_DebugCheckColumnDataLayout().
COLUMN_STRUCTS = (
    ("Columns", Columns),
    ("ColumnGeometry", ColumnGeometry),
)


class BuildInfo(C.Structure):
    _fields_ = [
        ("StructSize", C.c_int),
        ("AbiVersion", C.c_int),
        ("VersionNum", C.c_int),
        ("ImGuiVersionNum", C.c_int),
        ("SizeofImWchar", C.c_int),
        ("SizeofImDrawVert", C.c_int),
        ("SizeofImDrawIdx", C.c_int),
        ("OffsetofImDrawVertPos", C.c_int),
        ("OffsetofImDrawVertUV", C.c_int),
        ("OffsetofImDrawVertCol", C.c_int),
        ("SizeofPointer", C.c_int),
        ("ImGuiEnableFreeType", C.c_int),
        ("CatchesExceptions", C.c_int),
        ("FaceCount", C.c_int),
        ("SizeofStyle", C.c_int),
        ("SizeofRun", C.c_int),
        ("SizeofTextRange", C.c_int),
        ("SizeofLine", C.c_int),
        ("SizeofHit", C.c_int),
        ("SizeofFormula", C.c_int),
        ("SizeofStats", C.c_int),
        ("SizeofErrorInfo", C.c_int),
        ("Version", C.c_char * 16),
        ("ImGuiVersion", C.c_char * 32),
    ] + [
        ("Sizeof" + name, C.c_int)
        for name, _ in DOCUMENT_STRUCTS + TABLE_STRUCTS + COLUMN_STRUCTS
    ]


_P = C.c_void_p  # A handle, an ImDrawList*, an ImFont*: an address and nothing more.
_TEXT = C.c_char_p  # (begin, end): bytes and an address, or bytes and None.
_END = C.c_void_p
_INT = C.c_int
_F = C.c_float


def _ptr(struct):
    return C.POINTER(struct)


# name: (result, arguments). ImTypeset_Text and ImTypeset_TextV are left out:
# they are for a host written in C, and Python formats its own strings.
PROTOTYPES = {
    "ImTypeset_GetBuildInfo": (_INT, [_ptr(BuildInfo), _INT]),
    "ImTypeset_GetVersion": (C.c_char_p, []),
    "ImTypeset_GetImGuiVersionNum": (_INT, []),
    "ImTypeset_DebugCheckVersionAndDataLayout": (_INT, [_TEXT, _END] + [C.c_size_t] * 11),
    "ImTypeset_CreateContext": (_P, []),
    "ImTypeset_DestroyContext": (_INT, [_P]),
    "ImTypeset_GetCurrentContext": (_P, []),
    "ImTypeset_SetCurrentContext": (_INT, [_P]),
    "ImTypeset_GetContextSerial": (C.c_ulonglong, []),
    "ImTypeset_GetLastError": (_INT, [_ptr(ErrorInfo)]),
    "ImTypeset_GetLastErrorMessage": (_INT, [C.c_char_p, _INT]),
    "ImTypeset_GetStats": (_INT, [_ptr(Stats)]),
    "ImTypeset_GetStyle": (_INT, [_ptr(Style)]),
    "ImTypeset_SetStyle": (_INT, [_ptr(Style)]),
    "ImTypeset_SetTextCacheLimits": (_INT, [_INT, _INT]),
    "ImTypeset_AddFontFromFileTTF": (_P, [_INT, _TEXT, _END, _INT, _P]),
    "ImTypeset_AddFontFromMemoryTTF": (_P, [_INT, C.c_char_p, _INT, _INT, _P]),
    "ImTypeset_RemoveFont": (_INT, [_INT]),
    "ImTypeset_GetFont": (_P, [_INT]),
    "ImTypeset_GetFontAtlasIndex": (_INT, [_INT]),
    "ImTypeset_FaceHasGlyph": (_INT, [_INT, C.c_uint]),
    "ImTypeset_TextUnformatted": (_INT, [_TEXT, _END, _INT, _F, _ptr(Style)]),
    "ImTypeset_TextWithID": (_INT, [_TEXT, _END, _TEXT, _END, _INT, _F, _ptr(Style)]),
    "ImTypeset_SelectableText": (
        _INT,
        [_TEXT, _END, _TEXT, _END, _INT, _F, _ptr(Style), _ptr(TextRange)],
    ),
    "ImTypeset_CalcTextSize": (_INT, [_TEXT, _END, _F, _INT, _ptr(Style), _ptr(Vec2)]),
    "ImTypeset_CreateParagraph": (_P, []),
    "ImTypeset_DestroyParagraph": (_INT, [_P]),
    "ImTypeset_SetParagraphText": (_INT, [_P, _TEXT, _END, _ptr(Run), _INT, _INT]),
    "ImTypeset_LayoutParagraph": (_INT, [_P, _F, _ptr(Style)]),
    "ImTypeset_GetParagraphError": (_INT, [_P, _ptr(ErrorInfo)]),
    "ImTypeset_GetParagraphErrorMessage": (_INT, [_P, C.c_char_p, _INT]),
    "ImTypeset_GetParagraphStatus": (_INT, [_P]),
    "ImTypeset_GetParagraphFormulaCount": (_INT, [_P]),
    "ImTypeset_GetParagraphFormula": (_INT, [_P, _INT, _ptr(Formula)]),
    "ImTypeset_GetParagraphFormulaReason": (_INT, [_P, _INT, C.c_char_p, _INT]),
    "ImTypeset_GetParagraphSize": (_INT, [_P, _ptr(Vec2)]),
    "ImTypeset_GetParagraphLineCount": (_INT, [_P]),
    "ImTypeset_GetParagraphLine": (_INT, [_P, _INT, _ptr(Line)]),
    "ImTypeset_DrawParagraph": (_INT, [_P, _P, _ptr(Vec2), _ptr(TextRange)]),
    "ImTypeset_DrawParagraphToBuffers": (
        _INT,
        [_P, _ptr(Vec2), _ptr(Vec4), _P, _INT, _P, _INT, _ptr(TextRange), _ptr(_INT), _ptr(_INT)],
    ),
    "ImTypeset_HitTestParagraph": (_INT, [_P, _ptr(Vec2), _ptr(Hit)]),
    "ImTypeset_SelectParagraphRange": (
        _INT,
        [_P, _ptr(Vec2), _ptr(Vec2), _INT, _ptr(TextRange)],
    ),
    "ImTypeset_GetParagraphRangeRects": (_INT, [_P, _ptr(TextRange), _ptr(Vec4), _INT]),
    "ImTypeset_GetParagraphCaretRect": (_INT, [_P, _INT, _ptr(Vec4)]),
    "ImTypeset_AppendParagraphRangeText": (_INT, [_P, _ptr(TextRange), _INT, C.c_char_p, _INT]),
    "ImTypeset_DebugCheckDocumentDataLayout": (_INT, [C.c_size_t] * 12),
    "ImTypeset_DebugCheckTableDataLayout": (_INT, [C.c_size_t] * 6),
    "ImTypeset_DebugCheckColumnDataLayout": (_INT, [C.c_size_t] * 2),
    "ImTypeset_CreateDocument": (_P, []),
    "ImTypeset_DestroyDocument": (_INT, [_P]),
    "ImTypeset_SetDocumentBlocks": (_INT, [_P, _ptr(DocumentContent)]),
    "ImTypeset_ReplaceDocumentBlocks": (_INT, [_P, _INT, _INT, _ptr(DocumentContent)]),
    "ImTypeset_AppendDocumentText": (_INT, [_P, _TEXT, _END, _P, _INT]),
    "ImTypeset_AppendDocumentTableCells": (_INT, [_P, _TEXT, _END, _P, _INT, _P, _INT]),
    "ImTypeset_SetDocumentTableColumns": (_INT, [_P, _INT, _P, _INT]),
    "ImTypeset_SetDocumentBlockExtent": (_INT, [_P, _INT, _F]),
    "ImTypeset_SetDocumentOptions": (_INT, [_P, _ptr(DocumentOptions)]),
    "ImTypeset_GetDocumentBlockCount": (_INT, [_P]),
    "ImTypeset_GetDocumentStyleCount": (_INT, [_P]),
    "ImTypeset_GetDocumentContainerCount": (_INT, [_P]),
    "ImTypeset_GetDocumentBlockTextSize": (_INT, [_P, _INT]),
    "ImTypeset_LayoutDocument": (_INT, [_P, _F, _ptr(Style)]),
    "ImTypeset_LayoutDocumentWindow": (_INT, [_P, _F, _F]),
    "ImTypeset_GetDocumentSize": (_INT, [_P, _ptr(Vec2)]),
    "ImTypeset_GetDocumentBlockError": (_INT, [_P, _INT, _ptr(ErrorInfo)]),
    "ImTypeset_GetDocumentBlockErrorMessage": (_INT, [_P, _INT, C.c_char_p, _INT]),
    "ImTypeset_GetDocumentStats": (_INT, [_P, _ptr(DocumentStats)]),
    "ImTypeset_DrawDocument": (_INT, [_P, _P, _ptr(Vec2), _ptr(DocumentDraw)]),
    "ImTypeset_DrawDocumentToBuffers": (
        _INT,
        [_P, _ptr(Vec2), _ptr(Vec4), _P, _INT, _P, _INT, _ptr(DocumentDraw), _ptr(_INT), _ptr(_INT)],
    ),
    "ImTypeset_HitTestDocument": (_INT, [_P, _ptr(Vec2), _ptr(DocumentHit)]),
    "ImTypeset_SelectDocumentRange": (
        _INT,
        [_P, _ptr(Vec2), _ptr(Vec2), _INT, _ptr(DocumentRange)],
    ),
    "ImTypeset_GetDocumentRangeRects": (_INT, [_P, _ptr(DocumentRange), _ptr(Vec4), _INT]),
    "ImTypeset_GetDocumentCaretRect": (_INT, [_P, _ptr(Position), _ptr(Vec4)]),
    "ImTypeset_AppendDocumentRangeText": (
        _INT,
        [_P, _ptr(DocumentRange), _TEXT, _END, C.c_char_p, _INT],
    ),
    "ImTypeset_GetDocumentBlockGeometry": (_INT, [_P, _INT, _ptr(BlockGeometry)]),
    "ImTypeset_GetDocumentBlockLine": (_INT, [_P, _INT, _INT, _ptr(Line)]),
    "ImTypeset_GetDocumentTableGeometry": (_INT, [_P, _INT, _ptr(TableGeometry)]),
    "ImTypeset_GetDocumentTableColumn": (_INT, [_P, _INT, _INT, _ptr(TableColumnGeometry)]),
    "ImTypeset_GetDocumentTableRow": (_INT, [_P, _INT, _INT, _ptr(TableRowGeometry)]),
    "ImTypeset_SetDocumentColumns": (_INT, [_P, _ptr(Columns)]),
    "ImTypeset_GetDocumentColumnCount": (_INT, [_P]),
    "ImTypeset_GetDocumentColumn": (_INT, [_P, _INT, _ptr(ColumnGeometry)]),
    "ImTypeset_GetDocumentColumnPos": (_INT, [_P, _ptr(Vec2), _ptr(Vec2)]),
    "ImTypeset_GetDocumentTableCell": (_INT, [_P, _ptr(Position), _ptr(CellPosition)]),
    "ImTypeset_GetDocumentTablePosition": (_INT, [_P, _INT, _INT, _INT, _INT, _ptr(Position)]),
    "ImTypeset_DocumentView": (
        _INT,
        [_TEXT, _END, _P, _ptr(Vec2), _ptr(DocumentRange), _ptr(DocumentDraw), _ptr(Style)],
    ),
    "ImTypeset_ShowDemoWindow": (_INT, [_ptr(_INT)]),
    "ImTypeset_ShowDemoSection": (_INT, [_INT]),
    "ImTypeset_GetDemoSectionName": (C.c_char_p, [_INT]),
    "ImTypeset_TextCodepointToByteOffset": (_INT, [_TEXT, _END, _INT]),
    "ImTypeset_TextByteToCodepointOffset": (_INT, [_TEXT, _END, _INT]),
    "ImTypeset_ParagraphCodepointToByteOffset": (_INT, [_P, _INT]),
    "ImTypeset_ParagraphByteToCodepointOffset": (_INT, [_P, _INT]),
}

ENVIRONMENT_VARIABLE = "IMTYPESET_LIBRARY"


def library_path() -> str:
    """Where the library is, by IMTYPESET_LIBRARY. ImportError says how to make one."""
    path = os.environ.get(ENVIRONMENT_VARIABLE, "")
    if not path:
        raise ImportError(
            f"imtypeset: {ENVIRONMENT_VARIABLE} is not set. It names the shared library that holds "
            "imtypeset's C ABI and the Dear ImGui it draws through, e.g. "
            "build/py-host/libimtypeset_pyhost.so after "
            "`cmake -S python -B build/py-host -G Ninja && cmake --build build/py-host`."
        )
    if not os.path.isfile(path):
        raise ImportError(f"imtypeset: {ENVIRONMENT_VARIABLE}={path!r} is not a file")
    return os.path.abspath(path)


def load(path: str) -> C.CDLL:
    """The library with every prototype declared. ImportError if it is not one of ours."""
    try:
        lib = C.CDLL(path)
    except OSError as exc:
        raise ImportError(f"imtypeset: cannot load {path!r}: {exc}") from exc
    missing = [name for name in PROTOTYPES if not hasattr(lib, name)]
    if missing:
        raise ImportError(
            f"imtypeset: {path!r} does not export {len(missing)} of the {len(PROTOTYPES)} functions "
            f"of imtypeset_c.h this package calls (first: {missing[0]}): it is not an imtypeset "
            "library, or it is an older one"
        )
    for name, (result, arguments) in PROTOTYPES.items():
        function = getattr(lib, name)
        function.restype = result
        function.argtypes = arguments
    return lib
