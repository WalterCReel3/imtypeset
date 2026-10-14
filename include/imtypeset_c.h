/* imtypeset_c.h: the C ABI of imtypeset.
 *
 * imtypeset.h is the API. This is the same API with nothing in it a C compiler
 * or a foreign-function interface cannot read: what cimgui is to Dear ImGui.
 * It is the seam for every language that is not C++ (Python through ctypes,
 * Rust, Zig, C#, Lua), and it includes no C++ header and no ImGui header. What
 * each function DOES is documented once, in imtypeset.h, at the declaration of
 * the same name; what is written here is only how it crosses.
 *
 *     ImTypeset_BuildInfo info;
 *     ImTypeset_GetBuildInfo(&info, (int)sizeof info);   // compare with the host's ImGui
 *     if (!IMTYPESET_C_CHECKVERSION(sizeof(ImWchar), sizeof(ImDrawVert), sizeof(ImDrawIdx)))
 *         ...ImTypeset_GetLastErrorMessage(buf, sizeof buf)...
 *     ImTypeset_CreateContext();
 *     ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Regular, "Roboto-Regular.ttf", NULL, 0, NULL);
 *     ...
 *     ImTypeset_TextUnformatted(text, text + length, 0, 0.0f, NULL);
 *
 * NAMES. `ImTypeset_` and then the C++ name: ImTypeset::CreateContext() is
 * ImTypeset_CreateContext(), struct ImTypesetStyle is ImTypeset_Style,
 * ImTypesetFace_Regular is ImTypeset_Face_Regular. The underscore keeps the
 * two headers apart, so one C++ file can include both.
 *
 * WHAT CROSSES, AND HOW. These rules have no exception in this header:
 *   - TEXT is (begin, end): `end` points one past the last byte, or is NULL
 *     and `begin` is then zero-terminated. A language whose strings carry a
 *     length never has to terminate one. The same for a file name, a
 *     string ID and a separator, with one difference: those are handed on to
 *     functions that take zero-terminated strings, so a NUL byte inside one
 *     ends it there ("a\0b" and "a\0c" are the same ID). Text that is laid
 *     out is not cut: a NUL in it is a character like any other.
 *   - A STRUCT crosses by pointer, never by value, in both directions. One the
 *     library fills is the caller's: `out` parameters, last. A null pointer
 *     where a struct is required is refused (ImTypeset_Error_InvalidArgument,
 *     and the function answers 0); where one is optional the declaration
 *     says so.
 *   - AN ARRAY OR A STRING THE LIBRARY PRODUCES is written into a buffer the
 *     caller gives, with its capacity, and the function returns how much there
 *     was to write, as snprintf does. Nothing is written past the capacity, so
 *     a capacity of zero (and then a null buffer) asks for the size. No memory
 *     is allocated on one side of this boundary and freed on the other.
 *       - a string: `buf_size` counts bytes, the terminator included; the
 *         result counts bytes without it; what does not fit is cut, at a
 *         character boundary, and terminated.
 *       - an array: the capacity and the result count elements; when the
 *         result is larger than the capacity the first `capacity` elements
 *         were written.
 *   - HANDLES are the C++ handles: ImTypeset_Context*, ImTypeset_Paragraph*
 *     and ImTypeset_Document* are the pointers ImTypeset::CreateContext(),
 *     CreateParagraph() and CreateDocument() return, and may be passed
 *     between the two APIs in one process.
 *   - ImDrawList*, ImFont* AND ImFontConfig* ARE PASSED THROUGH, never looked
 *     into here: `struct ImDrawList*` is what the host's ImGui (or cimgui, or
 *     a binding's pointer value) calls by that name.
 *   - bool does not cross as a parameter or a result: `int`, zero or not. It
 *     appears only inside the structs that mirror a C++ one holding it
 *     (ImTypeset_Hit, ImTypeset_Formula, ImTypeset_DocumentHit,
 *     ImTypeset_BlockGeometry, ImTypeset_TableGeometry,
 *     ImTypeset_TableRowGeometry, ImTypeset_ColumnGeometry).
 *   - A STRUCT THAT HOLDS POINTERS (ImTypeset_DocumentContent,
 *     ImTypeset_DocumentDraw) points at arrays of the caller's, which are read
 *     during the call and never kept.
 *
 * WHAT A FUNCTION RETURNS.
 *   - `int` STATUS, non-zero for success: where the C++ function returns bool
 *     it is that bool; where the C++ function is marked [E] and returns a
 *     value, it is "ImTypeset_GetLastError() is None"; where the C++ function
 *     cannot fail it is 1, and 0 only for a null struct or a C++ exception.
 *   - A COUNT or an offset: zero or more, and -1 only for a C++ exception.
 *   - A HANDLE or a pointer: null for failure.
 *   - A SERIAL (ImTypeset_GetContextSerial): zero for none, and for a C++
 *     exception.
 *
 * ERRORS are imtypeset.h's: ImTypeset_GetLastError() is the outcome of the
 * most recent function marked [E], and those are marked [E] here too. This
 * layer adds two failures of its own, a null struct and a caught exception,
 * and reports them the same way from ANY function of this header: the
 * function fails as above and ImTypeset_GetLastError() says so until the next
 * call of a function of this header other than the ImTypeset_GetLastError*,
 * ImTypeset_GetBuildInfo, ImTypeset_GetVersion and
 * ImTypeset_GetImGuiVersionNum.
 *
 * EXCEPTIONS. The library is written without them. Dear ImGui Bundle, and any
 * host that defines IM_ASSERT to throw, compiles it with them enabled, and an
 * ImGui assertion then unwinds through the library. No exception leaves a
 * function of this header: when the file is compiled with exceptions enabled
 * each entry catches, fails, and reports ImTypeset_Error_Exception with the
 * exception's message. ImTypeset_BuildInfo::CatchesExceptions says which way
 * the library was compiled. What state the context is in after one is
 * imtypeset.h's business, not this layer's.
 *
 * THREADS. As imtypeset.h: one thread at a time for the whole library. The
 * error slot above and the scratch buffers behind the array-producing
 * functions are globals of the process.
 */
#ifndef IMTYPESET_C_H
#define IMTYPESET_C_H

#include <stdarg.h>
#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

/* The same numbers as IMTYPESET_VERSION and IMTYPESET_VERSION_NUM. */
#define IMTYPESET_C_VERSION     "0.4.3"
#define IMTYPESET_C_VERSION_NUM 403
/* The revision of THIS header's own shape: raised when a function of it
 * changes its parameters or a rule above changes. Adding a function does not
 * raise it. */
#define IMTYPESET_C_ABI_VERSION 1

/* Define IMTYPESET_C_API to override. IMTYPESET_C_BUILD_DLL when compiling the
 * library as a Windows DLL, IMTYPESET_C_USE_DLL when calling one. */
#ifndef IMTYPESET_C_API
#if defined(_WIN32) && defined(IMTYPESET_C_BUILD_DLL)
#define IMTYPESET_C_API __declspec(dllexport)
#elif defined(_WIN32) && defined(IMTYPESET_C_USE_DLL)
#define IMTYPESET_C_API __declspec(dllimport)
#elif defined(__GNUC__)
#define IMTYPESET_C_API __attribute__((visibility("default")))
#else
#define IMTYPESET_C_API
#endif
#endif

#if defined(__GNUC__)
#define IMTYPESET_C_FMTARGS(fmt_index) __attribute__((format(printf, fmt_index, fmt_index + 1)))
#define IMTYPESET_C_FMTLIST(fmt_index) __attribute__((format(printf, fmt_index, 0)))
#else
#define IMTYPESET_C_FMTARGS(fmt_index)
#define IMTYPESET_C_FMTLIST(fmt_index)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Types -------------------------------------------------------------------------- */

/* Dear ImGui's, by name only. */
struct ImDrawList;
struct ImFont;
struct ImFontConfig;

/* The two handles. Opaque. */
struct ImTypesetContext;
struct ImTypesetParagraph;
typedef struct ImTypesetContext ImTypeset_Context;
typedef struct ImTypesetParagraph ImTypeset_Paragraph;
typedef struct ImTypesetDocument ImTypeset_Document;

typedef unsigned int ImTypeset_U32;       /* ImU32: a colour, as IM_COL32() packs one. */
typedef unsigned long long ImTypeset_U64; /* ImU64 */

typedef int ImTypeset_Face;              /* ImTypeset_Face_ */
typedef int ImTypeset_Align;             /* ImTypeset_Align_ */
typedef int ImTypeset_Spacing;           /* ImTypeset_Spacing_ */
typedef int ImTypeset_LineBreak;         /* ImTypeset_LineBreak_ */
typedef int ImTypeset_Granularity;       /* ImTypeset_Granularity_ */
typedef int ImTypeset_Error;             /* ImTypeset_Error_ */
typedef int ImTypeset_TextFlags;         /* ImTypeset_TextFlags_ */
typedef int ImTypeset_RunFlags;          /* ImTypeset_RunFlags_ */
typedef int ImTypeset_StatusFlags;       /* ImTypeset_StatusFlags_ */
typedef int ImTypeset_BlockKind;         /* ImTypeset_BlockKind_ */
typedef int ImTypeset_BlockFlags;        /* ImTypeset_BlockFlags_ */
typedef int ImTypeset_ContainerKind;     /* ImTypeset_ContainerKind_ */
typedef int ImTypeset_ContainerFlags;    /* ImTypeset_ContainerFlags_ */
typedef int ImTypeset_CodeOverflow;      /* ImTypeset_CodeOverflow_ */
typedef int ImTypeset_MarkPlace;         /* ImTypeset_MarkPlace_ */
typedef int ImTypeset_DocumentDrawFlags; /* ImTypeset_DocumentDrawFlags_ */
typedef int ImTypeset_DemoSection;       /* ImTypeset_DemoSection_ */
typedef int ImTypeset_CellFlags;         /* ImTypeset_CellFlags_ */
typedef int ImTypeset_ColumnFlags;       /* ImTypeset_ColumnFlags_ */
typedef int ImTypeset_Advances;          /* ImTypeset_Advances_ */

/* Every value below is the value of the C++ enumerator of the same name. */

enum ImTypeset_Face_ {
    ImTypeset_Face_Default = 0,
    ImTypeset_Face_Regular = 1,
    ImTypeset_Face_Bold = 2,
    ImTypeset_Face_Italic = 3,
    ImTypeset_Face_BoldItalic = 4,
    ImTypeset_Face_Code = 5,
    ImTypeset_Face_CodeBold = 6,
    ImTypeset_Face_Math = 7,
    ImTypeset_Face_COUNT = 8
};

enum ImTypeset_Align_ {
    ImTypeset_Align_Justify = 0,
    ImTypeset_Align_Left = 1,
    ImTypeset_Align_Center = 2,
    ImTypeset_Align_Right = 3
};

enum ImTypeset_Spacing_ {
    ImTypeset_Spacing_Sentence = 0,
    ImTypeset_Spacing_French = 1,
    ImTypeset_Spacing_TeX = 2
};

enum ImTypeset_LineBreak_ {
    ImTypeset_LineBreak_Auto = 0,
    ImTypeset_LineBreak_FirstFit = 1,
    ImTypeset_LineBreak_TotalFit = 2
};

enum ImTypeset_Advances_ {
    ImTypeset_Advances_Default = 0,
    ImTypeset_Advances_Font = 1,
    ImTypeset_Advances_Device = 2
};

enum ImTypeset_Granularity_ {
    ImTypeset_Granularity_Character = 0,
    ImTypeset_Granularity_Word = 1,
    ImTypeset_Granularity_Line = 2
};

enum ImTypeset_Error_ {
    ImTypeset_Error_None = 0,
    ImTypeset_Error_NoContext = 1,
    ImTypeset_Error_VersionMismatch = 2,
    ImTypeset_Error_InvalidArgument = 3,
    ImTypeset_Error_FileNotFound = 4,
    ImTypeset_Error_FontRefused = 5,
    ImTypeset_Error_NoFont = 6,
    ImTypeset_Error_TextTooLong = 7,
    ImTypeset_Error_MathRefused = 8,
    ImTypeset_Error_LayoutFailed = 9,
    ImTypeset_Error_TooManyVertices = 10,
    ImTypeset_Error_BackendUnsupported = 11,
    ImTypeset_Error_DescriptionCut = 12,
    /* This header's own, with no C++ enumerator: a C++ exception was caught at
     * the boundary. The message is the exception's. */
    ImTypeset_Error_Exception = 1000
};

enum ImTypeset_TextFlags_ {
    ImTypeset_TextFlags_None = 0,
    ImTypeset_TextFlags_HardBreaks = 1 << 0,
    ImTypeset_TextFlags_Math = 1 << 2
};

enum ImTypeset_RunFlags_ {
    ImTypeset_RunFlags_None = 0,
    ImTypeset_RunFlags_NoBreak = 1 << 0,
    ImTypeset_RunFlags_Formula = 1 << 1,
    ImTypeset_RunFlags_DisplayFormula = 1 << 2
};

enum ImTypeset_StatusFlags_ {
    ImTypeset_StatusFlags_None = 0,
    ImTypeset_StatusFlags_LaidOut = 1 << 0,
    ImTypeset_StatusFlags_FirstFitFallback = 1 << 1,
    ImTypeset_StatusFlags_DisplayMath = 1 << 2
};

enum ImTypeset_BlockKind_ {
    ImTypeset_BlockKind_Paragraph = 0,
    ImTypeset_BlockKind_Heading = 1,
    ImTypeset_BlockKind_ListItem = 2,
    ImTypeset_BlockKind_Code = 3,
    ImTypeset_BlockKind_Rule = 4,
    ImTypeset_BlockKind_Host = 5,
    ImTypeset_BlockKind_Table = 6
};

enum ImTypeset_BlockFlags_ {
    ImTypeset_BlockFlags_None = 0,
    ImTypeset_BlockFlags_HardBreaks = 1 << 0,
    ImTypeset_BlockFlags_Math = 1 << 2,
    ImTypeset_BlockFlags_FixedLineHeight = 1 << 3,
    ImTypeset_BlockFlags_TableHeader = 1 << 4,
    ImTypeset_BlockFlags_TableHeld = 1 << 5
};

enum ImTypeset_CellFlags_ { ImTypeset_CellFlags_None = 0, ImTypeset_CellFlags_RowStart = 1 << 0 };

enum ImTypeset_ColumnFlags_ {
    ImTypeset_ColumnFlags_None = 0,
    ImTypeset_ColumnFlags_Balance = 1 << 0
};

enum ImTypeset_ContainerKind_ {
    ImTypeset_ContainerKind_Quote = 0,
    ImTypeset_ContainerKind_List = 1
};

enum ImTypeset_ContainerFlags_ {
    ImTypeset_ContainerFlags_None = 0,
    ImTypeset_ContainerFlags_Tight = 1 << 0
};

enum ImTypeset_CodeOverflow_ {
    ImTypeset_CodeOverflow_Wrap = 0,
    ImTypeset_CodeOverflow_Clip = 1,
    ImTypeset_CodeOverflow_Grow = 2
};

enum ImTypeset_MarkPlace_ {
    ImTypeset_MarkPlace_Ink = 0,
    ImTypeset_MarkPlace_Behind = 1,
    ImTypeset_MarkPlace_Under = 2
};

enum ImTypeset_DocumentDrawFlags_ {
    ImTypeset_DocumentDrawFlags_None = 0,
    ImTypeset_DocumentDrawFlags_Debug = 1 << 0
};

/* ImVec2 and ImVec4. A rectangle in an ImTypeset_Vec4 is (min x, min y, max x,
 * max y). */
typedef struct ImTypeset_Vec2 {
    float x;
    float y;
} ImTypeset_Vec2;

typedef struct ImTypeset_Vec4 {
    float x;
    float y;
    float z;
    float w;
} ImTypeset_Vec4;

/* The structs below mirror the C++ ones field for field: the same names, the
 * same order, the same sizes and offsets, which the library proves when it is
 * compiled. All zero is the default of one the caller fills. */

typedef struct ImTypeset_Style { /* ImTypesetStyle */
    ImTypeset_Face Face;
    float FontSize;
    float EmSize;
    float LineHeight;
    ImTypeset_Align Align;
    float LeftIndent;
    float FirstLineIndent;
    ImTypeset_Spacing Spacing;
    ImTypeset_LineBreak LineBreak;
    float PixelsPerInch;
    ImTypeset_U32 Color;
    ImTypeset_U32 SelectionColor;
    ImTypeset_Advances Advances;
} ImTypeset_Style;

typedef struct ImTypeset_Run { /* ImTypesetRun */
    int Start;
    int End;
    ImTypeset_Face Face;
    float Scale;
    ImTypeset_U32 Color;
    ImTypeset_RunFlags Flags;
    int Tag;
} ImTypeset_Run;

typedef struct ImTypeset_TextRange { /* ImTypesetTextRange */
    int Start;
    int End;
} ImTypeset_TextRange;

typedef struct ImTypeset_Line { /* ImTypesetLine */
    ImTypeset_Vec2 Min;
    ImTypeset_Vec2 Max;
    float Baseline;
    int TextStart;
    int TextEnd;
} ImTypeset_Line;

typedef struct ImTypeset_Hit { /* ImTypesetHit */
    int TextOffset;
    int Line;
    bool Inside;
    int Tag;
} ImTypeset_Hit;

typedef struct ImTypeset_FontMetrics { /* ImTypesetFontMetrics */
    float EmSize;
    float FontSize;
    float Ascent;
    float Descent;
    float LineGap;
    float XHeight;
    float SpaceAdvance;
} ImTypeset_FontMetrics;

typedef struct ImTypeset_ErrorInfo { /* ImTypesetErrorInfo */
    ImTypeset_Error Code;
    int BytePos;
    int CodepointPos;
    /* Zero-terminated, never null. Library storage: valid until the next call
     * into the library. ImTypeset_GetLastErrorMessage() and
     * ImTypeset_GetParagraphErrorMessage() copy it out instead. */
    const char* Message;
} ImTypeset_ErrorInfo;

typedef struct ImTypeset_Formula { /* ImTypesetFormula */
    int TextStart;
    int TextEnd;
    bool Display;
    bool Refused;
    int BytePos;
    int CodepointPos;
    /* Zero-terminated, never null. The paragraph's storage: valid until its
     * text is set or it is laid out again. ImTypeset_GetParagraphFormulaReason()
     * copies it out instead. */
    const char* Reason;
} ImTypeset_Formula;

typedef struct ImTypeset_Stats { /* ImTypesetStats */
    ImTypeset_U64 TextBuilds;
    ImTypeset_U64 Layouts;
    ImTypeset_U64 DeferredLayouts;
    ImTypeset_U64 DrawCalls;
    ImTypeset_U64 CulledCalls;
    ImTypeset_U64 VerticesEmitted;
    ImTypeset_U64 BytesHashed;
    ImTypeset_U64 CacheEvictions;
    ImTypeset_U64 CachedParagraphs;
    ImTypeset_U64 CachedBytes;
} ImTypeset_Stats;

/* --- Documents: the structs of include/imtypeset.h's "Documents", field for field ---- */

typedef struct ImTypeset_BlockStyle { /* ImTypesetBlockStyle */
    ImTypeset_Face Face;
    float Scale;
    float LineHeight;
    ImTypeset_Align Align;
    float FirstLineIndent;
    float SpaceAbove;
    float SpaceBelow;
    ImTypeset_U32 Color;
    ImTypeset_Face MarkerFace;
    ImTypeset_U32 MarkerColor;
    float MarkerGap;
    ImTypeset_Advances Advances;
} ImTypeset_BlockStyle;

typedef struct ImTypeset_Container { /* ImTypesetContainer */
    ImTypeset_ContainerKind Kind;
    int Parent;
    float Indent;
    ImTypeset_ContainerFlags Flags;
    ImTypeset_U32 Color;
} ImTypeset_Container;

typedef struct ImTypeset_Block { /* ImTypesetBlock */
    ImTypeset_BlockKind Kind;
    ImTypeset_BlockFlags Flags;
    int Level;
    int Style;
    int TextStart;
    int TextEnd;
    int RunStart;
    int RunCount;
    int MarkerStart;
    int MarkerEnd;
    float LeftIndent;
    int QuoteDepth;
    int Container;
    int Item;
    float Extent;
    int CellStart;
    int CellCount;
    int ColumnStart;
    int ColumnCount;
} ImTypeset_Block;

typedef struct ImTypeset_Cell { /* ImTypesetCell */
    int TextStart;
    int TextEnd;
    ImTypeset_CellFlags Flags;
} ImTypeset_Cell;

typedef struct ImTypeset_TableColumn { /* ImTypesetTableColumn */
    ImTypeset_Align Align;
    float Width;
    float Share;
} ImTypeset_TableColumn;

/* Everything one edit hands over, as views: all of it is copied. */
typedef struct ImTypeset_DocumentContent { /* ImTypesetDocumentContent */
    const char* Text;
    int TextSize;
    const ImTypeset_Block* Blocks;
    int BlockCount;
    const ImTypeset_Run* Runs;
    int RunCount;
    const ImTypeset_BlockStyle* Styles;
    int StyleCount;
    const ImTypeset_Container* Containers;
    int ContainerCount;
    const ImTypeset_Cell* Cells;
    int CellCount;
    const ImTypeset_TableColumn* Columns;
    int ColumnCount;
} ImTypeset_DocumentContent;

typedef struct ImTypeset_DocumentOptions { /* ImTypesetDocumentOptions */
    float QuoteIndent;
    float QuoteBarThickness;
    ImTypeset_U32 QuoteBarColor;
    ImTypeset_U32 RuleColor;
    ImTypeset_CodeOverflow CodeOverflow;
    float TableCellPadding;
    float TableRuleThickness;
    ImTypeset_U32 TableRuleColor;
} ImTypeset_DocumentOptions;

typedef struct ImTypeset_Position { /* ImTypesetPosition */
    int Block;
    int Offset;
} ImTypeset_Position;

typedef struct ImTypeset_DocumentRange { /* ImTypesetDocumentRange */
    ImTypeset_Position Start;
    ImTypeset_Position End;
} ImTypeset_DocumentRange;

typedef struct ImTypeset_Mark { /* ImTypesetMark */
    ImTypeset_DocumentRange Range;
    ImTypeset_U32 Color;
    ImTypeset_MarkPlace Place;
    float Thickness;
    float Offset;
} ImTypeset_Mark;

typedef struct ImTypeset_DocumentDraw { /* ImTypesetDocumentDraw */
    ImTypeset_DocumentRange Selection;
    const ImTypeset_Mark* Marks;
    int MarkCount;
    ImTypeset_DocumentDrawFlags Flags;
} ImTypeset_DocumentDraw;

typedef struct ImTypeset_DocumentHit { /* ImTypesetDocumentHit */
    ImTypeset_Position Position;
    int Line;
    int Tag;
    bool Inside;
    bool InBlock;
    int Row;
    int Column;
} ImTypeset_DocumentHit;

typedef struct ImTypeset_BlockGeometry { /* ImTypesetBlockGeometry */
    ImTypeset_Vec2 Min;
    ImTypeset_Vec2 Max;
    ImTypeset_Vec2 FullMin;
    ImTypeset_Vec2 FullMax;
    int LineCount;
    bool LaidOut;
} ImTypeset_BlockGeometry;

typedef struct ImTypeset_TableGeometry { /* ImTypesetTableGeometry */
    int RowCount;
    int ColumnCount;
    int CellCount;
    ImTypeset_Vec2 Min;
    ImTypeset_Vec2 Max;
    bool Stated;
    bool LaidOut;
} ImTypeset_TableGeometry;

typedef struct ImTypeset_TableColumnGeometry { /* ImTypesetTableColumnGeometry */
    float Min;
    float Max;
    float Natural;
    float Floor;
} ImTypeset_TableColumnGeometry;

typedef struct ImTypeset_TableRowGeometry { /* ImTypesetTableRowGeometry */
    float Min;
    float Max;
    int CellStart;
    int CellCount;
    bool Wrapped;
} ImTypeset_TableRowGeometry;

typedef struct ImTypeset_CellPosition { /* ImTypesetCellPosition */
    int Row;
    int Column;
    int Cell;
    int Offset;
} ImTypeset_CellPosition;

/* How a document is shown in columns: see "Columns" in imtypeset.h. */
typedef struct ImTypeset_Columns { /* ImTypesetColumns */
    int Count;
    float Height;
    float Gutter;
    ImTypeset_ColumnFlags Flags;
} ImTypeset_Columns;

typedef struct ImTypeset_ColumnGeometry { /* ImTypesetColumnGeometry */
    ImTypeset_Vec2 Min;
    ImTypeset_Vec2 Max;
    ImTypeset_Vec2 ContentMin;
    ImTypeset_Vec2 ContentMax;
    ImTypeset_Position Start;
    ImTypeset_Position End;
    float UncutTop;
    bool Overfull;
    bool Exists;
} ImTypeset_ColumnGeometry;

typedef struct ImTypeset_DocumentStats { /* ImTypesetDocumentStats */
    ImTypeset_U64 MaterialBuilds;
    ImTypeset_U64 BlocksLaidOut;
    ImTypeset_U64 UnitsStacked;
    ImTypeset_U64 StructureVisits;
    ImTypeset_U64 BlocksDrawn;
    int Blocks;
    int BlocksDeferred;
    ImTypeset_U64 TableCellsMeasured;
    ImTypeset_U64 TableRowsSet;
    ImTypeset_U64 ColumnsCut;
    ImTypeset_U64 BalanceHeightsTried;
    bool MeasuredWithoutDevice;
} ImTypeset_DocumentStats;

/* What the library was compiled as. Only `int` and `char`, so that it reads
 * the same whatever else disagrees. It has no C++ counterpart. */
typedef struct ImTypeset_BuildInfo {
    int StructSize;      /* sizeof(ImTypeset_BuildInfo) in the library. */
    int AbiVersion;      /* IMTYPESET_C_ABI_VERSION */
    int VersionNum;      /* IMTYPESET_VERSION_NUM */
    int ImGuiVersionNum; /* IMGUI_VERSION_NUM of the imgui.h it was compiled against. */
    /* The three ImGui types whose size a build option changes. A host whose
     * ImGui disagrees with any of them must not call anything else. */
    int SizeofImWchar;    /* 2, or 4 with IMGUI_USE_WCHAR32. */
    int SizeofImDrawVert; /* 20 unless IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT. */
    int SizeofImDrawIdx;  /* 2, or 4 with `#define ImDrawIdx unsigned int`. */
    /* Where an ImDrawVert keeps its position (two floats), its texture
     * coordinates (two floats) and its colour (ImU32): what a reader of
     * ImTypeset_DrawParagraphToBuffers()'s vertices needs. */
    int OffsetofImDrawVertPos;
    int OffsetofImDrawVertUV;
    int OffsetofImDrawVertCol;
    int SizeofPointer;
    /* Non-zero if the imgui.h it was compiled against had IMGUI_ENABLE_FREETYPE:
     * glyphs are then rasterised by FreeType, and by stb_truetype otherwise,
     * unless the host installs another font loader at run time. */
    int ImGuiEnableFreeType;
    /* Non-zero if this layer was compiled with C++ exceptions enabled and
     * turns one into ImTypeset_Error_Exception. Zero: the library was built
     * without exceptions and nothing in it throws. */
    int CatchesExceptions;
    int FaceCount; /* ImTypesetFace_COUNT */
    /* The sizes IMTYPESET_C_CHECKVERSION() compares. */
    int SizeofStyle;
    int SizeofRun;
    int SizeofTextRange;
    int SizeofLine;
    int SizeofHit;
    int SizeofFormula;
    int SizeofStats;
    int SizeofErrorInfo;
    char Version[16];      /* IMTYPESET_VERSION, zero-terminated. */
    char ImGuiVersion[32]; /* IMGUI_VERSION of that imgui.h, zero-terminated. */
    /* The sizes of the structs of a document, which IMTYPESET_C_CHECKVERSION()
     * compares too. */
    int SizeofBlock;
    int SizeofBlockStyle;
    int SizeofContainer;
    int SizeofDocumentContent;
    int SizeofDocumentOptions;
    int SizeofPosition;
    int SizeofDocumentRange;
    int SizeofMark;
    int SizeofDocumentDraw;
    int SizeofDocumentHit;
    int SizeofBlockGeometry;
    int SizeofDocumentStats;
    /* The sizes of the structs of a table, which IMTYPESET_C_CHECKVERSION()
     * compares too. */
    int SizeofCell;
    int SizeofTableColumn;
    int SizeofTableGeometry;
    int SizeofTableColumnGeometry;
    int SizeofTableRowGeometry;
    int SizeofCellPosition;
    /* Appended with columns. */
    int SizeofColumns;
    int SizeofColumnGeometry;
} ImTypeset_BuildInfo;

/* --- The build -------------------------------------------------------------------- */

/* Copies the first `out_size` bytes of the library's ImTypeset_BuildInfo to
 * `out` and returns the size of the whole of it. Later versions append fields;
 * a caller reads those its own `out_size` and the result both cover. Calls
 * nothing, needs nothing, cannot fail: what a binding calls first. */
IMTYPESET_C_API int ImTypeset_GetBuildInfo(ImTypeset_BuildInfo* out, int out_size);

/* IMTYPESET_VERSION as compiled into the library. Static, zero-terminated. */
IMTYPESET_C_API const char* ImTypeset_GetVersion(void);
IMTYPESET_C_API int ImTypeset_GetImGuiVersionNum(void);

/* What IMTYPESET_C_CHECKVERSION() calls. `version` is (begin, end). [E] */
IMTYPESET_C_API int ImTypeset_DebugCheckVersionAndDataLayout(
    const char* version, const char* version_end, size_t sz_imwchar, size_t sz_imdrawvert,
    size_t sz_imdrawidx, size_t sz_style, size_t sz_run, size_t sz_range, size_t sz_line,
    size_t sz_hit, size_t sz_formula, size_t sz_stats, size_t sz_error);

/* IMTYPESET_CHECKVERSION() for a caller of this header: this header's version
 * and struct sizes, and the three ImGui sizes as the CALLER's ImGui has them,
 * which this header cannot know. A binding with no ImGui header at hand takes
 * them from whatever it shares ImGui with, and compares ImTypeset_BuildInfo
 * itself. After a failed check ImTypeset_CreateContext() refuses. */
#define IMTYPESET_C_CHECKVERSION(sz_imwchar, sz_imdrawvert, sz_imdrawidx)                        \
    (ImTypeset_DebugCheckVersionAndDataLayout(                                                   \
         IMTYPESET_C_VERSION, NULL, (sz_imwchar), (sz_imdrawvert), (sz_imdrawidx),               \
         sizeof(ImTypeset_Style), sizeof(ImTypeset_Run), sizeof(ImTypeset_TextRange),            \
         sizeof(ImTypeset_Line), sizeof(ImTypeset_Hit), sizeof(ImTypeset_Formula),               \
         sizeof(ImTypeset_Stats), sizeof(ImTypeset_ErrorInfo)) &&                                \
     ImTypeset_DebugCheckDocumentDataLayout(                                                     \
         sizeof(ImTypeset_Block), sizeof(ImTypeset_BlockStyle), sizeof(ImTypeset_Container),     \
         sizeof(ImTypeset_DocumentContent), sizeof(ImTypeset_DocumentOptions),                   \
         sizeof(ImTypeset_Position), sizeof(ImTypeset_DocumentRange), sizeof(ImTypeset_Mark),    \
         sizeof(ImTypeset_DocumentDraw), sizeof(ImTypeset_DocumentHit),                          \
         sizeof(ImTypeset_BlockGeometry), sizeof(ImTypeset_DocumentStats)) &&                    \
     ImTypeset_DebugCheckTableDataLayout(                                                        \
         sizeof(ImTypeset_Cell), sizeof(ImTypeset_TableColumn), sizeof(ImTypeset_TableGeometry), \
         sizeof(ImTypeset_TableColumnGeometry), sizeof(ImTypeset_TableRowGeometry),              \
         sizeof(ImTypeset_CellPosition)) &&                                                      \
     ImTypeset_DebugCheckColumnDataLayout(sizeof(ImTypeset_Columns),                             \
                                          sizeof(ImTypeset_ColumnGeometry)))

/* --- Context ---------------------------------------------------------------------- */

IMTYPESET_C_API ImTypeset_Context* ImTypeset_CreateContext(void); /* [E] */
/* Null destroys the current one. Also releases the memory this layer keeps
 * between calls for ImTypeset_DrawParagraphToBuffers() and
 * ImTypeset_GetParagraphRangeRects(), which is ImGui's allocator's: destroy
 * the ImTypeset context before the ImGui one. */
IMTYPESET_C_API int ImTypeset_DestroyContext(ImTypeset_Context* ctx);
IMTYPESET_C_API ImTypeset_Context* ImTypeset_GetCurrentContext(void);
IMTYPESET_C_API int ImTypeset_SetCurrentContext(ImTypeset_Context* ctx);
/* The current context's serial: never zero for a context, and zero with no
 * current context (or for a C++ exception). The one function here that
 * returns a 64-bit number. */
IMTYPESET_C_API ImTypeset_U64 ImTypeset_GetContextSerial(void);

/* ImTypeset::GetLastError(), copied; or this layer's own failure (see ERRORS
 * at the top). `out->Message` is library storage. */
IMTYPESET_C_API int ImTypeset_GetLastError(ImTypeset_ErrorInfo* out);
/* The same message as a string in the caller's buffer. Returns its length. */
IMTYPESET_C_API int ImTypeset_GetLastErrorMessage(char* buf, int buf_size);

IMTYPESET_C_API int ImTypeset_GetStats(ImTypeset_Stats* out);

/* ImTypeset::GetStyle() is a reference into the context. Here it is a copy out
 * and a copy in: nothing a binding holds dangles when the context goes. With
 * no context both go to the scratch style that nothing reads. */
IMTYPESET_C_API int ImTypeset_GetStyle(ImTypeset_Style* out);
IMTYPESET_C_API int ImTypeset_SetStyle(const ImTypeset_Style* style);

IMTYPESET_C_API int ImTypeset_SetTextCacheLimits(int max_paragraphs, int max_bytes);

/* --- Fonts ------------------------------------------------------------------------ */

/* `filename` is (begin, end), UTF-8. `font_cfg` may be null. [E] */
IMTYPESET_C_API struct ImFont*
ImTypeset_AddFontFromFileTTF(ImTypeset_Face face, const char* filename, const char* filename_end,
                             int face_index, const struct ImFontConfig* font_cfg);
/* The bytes are copied before it returns. [E] */
IMTYPESET_C_API struct ImFont* ImTypeset_AddFontFromMemoryTTF(ImTypeset_Face face,
                                                              const void* font_data,
                                                              int font_data_size, int face_index,
                                                              const struct ImFontConfig* font_cfg);
IMTYPESET_C_API int ImTypeset_RemoveFont(ImTypeset_Face face); /* [E] */
IMTYPESET_C_API struct ImFont* ImTypeset_GetFont(ImTypeset_Face face);
/* Where ImTypeset_GetFont(face) stands in the atlas's list of fonts
 * (ImGui::GetIO().Fonts->Fonts), or -1 when it answers null. For a binding in
 * which an ImFont is an object of its own and cannot be made from an address:
 * the object at that place of the same list IS the role's font, and a host
 * draws its own text with it instead of adding the file a second time. Good
 * until a font is added to or removed from the atlas. -1 for a role that was
 * given a font is also how a binding learns that the ImGui context is gone or
 * not current: see ImTypeset::GetFont() in imtypeset.h. */
IMTYPESET_C_API int ImTypeset_GetFontAtlasIndex(ImTypeset_Face face);
/* Non-zero if the role's own font has a glyph for the codepoint. */
IMTYPESET_C_API int ImTypeset_FaceHasGlyph(ImTypeset_Face face, unsigned int codepoint);
/* A role's own font at an em size in pixels. `out_size` is
 * sizeof(ImTypeset_FontMetrics) where the call is compiled: a caller compiled
 * against another header is refused (ImTypeset_Error_VersionMismatch) and not
 * written past. 0 on failure, with `*out` zeroed when it is the library's
 * size. */
IMTYPESET_C_API int ImTypeset_GetFontMetrics(ImTypeset_Face face, float em_size,
                                             ImTypeset_FontMetrics* out, size_t out_size);
/* The advance of one character of the role's own font at an em size in
 * pixels; 0 wherever ImTypeset_GetFontMetrics() fails. */
IMTYPESET_C_API float ImTypeset_GetGlyphAdvance(ImTypeset_Face face, float em_size,
                                                unsigned int codepoint);
/* The advance of one character of the role's font as text that `style` sets
 * and measures on the device has it, in pixels; `style` may be null (the
 * context's) and a `scale` of 0 is 1. 0 on failure, with the reason. */
IMTYPESET_C_API float ImTypeset_GetDeviceGlyphAdvance(ImTypeset_Face face, unsigned int codepoint,
                                                      const ImTypeset_Style* style, float scale);

/* --- Widgets ---------------------------------------------------------------------- */

/* All [E]. `style` may be null: the context's. */
IMTYPESET_C_API int ImTypeset_TextUnformatted(const char* text, const char* text_end,
                                              ImTypeset_TextFlags flags, float wrap_width,
                                              const ImTypeset_Style* style);
/* For a host written in C. A binding formats in its own language and calls
 * ImTypeset_TextUnformatted(), which is what these two do with the result. */
IMTYPESET_C_API int ImTypeset_Text(const char* fmt, ...) IMTYPESET_C_FMTARGS(1);
IMTYPESET_C_API int ImTypeset_TextV(const char* fmt, va_list args) IMTYPESET_C_FMTLIST(1);
/* `str_id` is (begin, end). */
IMTYPESET_C_API int ImTypeset_TextWithID(const char* str_id, const char* str_id_end,
                                         const char* text, const char* text_end,
                                         ImTypeset_TextFlags flags, float wrap_width,
                                         const ImTypeset_Style* style);
/* `selection` may be null; otherwise it is read when the call starts and
 * written before it returns. */
IMTYPESET_C_API int ImTypeset_SelectableText(const char* str_id, const char* str_id_end,
                                             const char* text, const char* text_end,
                                             ImTypeset_TextFlags flags, float wrap_width,
                                             const ImTypeset_Style* style,
                                             ImTypeset_TextRange* selection);
/* The status is "the text could be set as asked"; `out_size` is written
 * either way. */
IMTYPESET_C_API int ImTypeset_CalcTextSize(const char* text, const char* text_end, float wrap_width,
                                           ImTypeset_TextFlags flags, const ImTypeset_Style* style,
                                           ImTypeset_Vec2* out_size);

/* --- Paragraphs ------------------------------------------------------------------- */

IMTYPESET_C_API ImTypeset_Paragraph* ImTypeset_CreateParagraph(void); /* [E] */
IMTYPESET_C_API int ImTypeset_DestroyParagraph(ImTypeset_Paragraph* paragraph);

/* `runs` points at `runs_count` ImTypeset_Run, or is null. Text and runs are
 * copied. [E] */
IMTYPESET_C_API int ImTypeset_SetParagraphText(ImTypeset_Paragraph* paragraph, const char* text,
                                               const char* text_end, const ImTypeset_Run* runs,
                                               int runs_count, ImTypeset_TextFlags flags);
IMTYPESET_C_API int ImTypeset_LayoutParagraph(ImTypeset_Paragraph* paragraph, float width,
                                              const ImTypeset_Style* style); /* [E] */

/* `out->Message` is the paragraph's storage. */
IMTYPESET_C_API int ImTypeset_GetParagraphError(const ImTypeset_Paragraph* paragraph,
                                                ImTypeset_ErrorInfo* out);
IMTYPESET_C_API int ImTypeset_GetParagraphErrorMessage(const ImTypeset_Paragraph* paragraph,
                                                       char* buf, int buf_size);
/* ImTypeset_StatusFlags_; None (zero) for a C++ exception as well. */
IMTYPESET_C_API ImTypeset_StatusFlags
ImTypeset_GetParagraphStatus(const ImTypeset_Paragraph* paragraph);
IMTYPESET_C_API int ImTypeset_GetParagraphFormulaCount(const ImTypeset_Paragraph* paragraph);
/* `out->Reason` is the paragraph's storage. */
IMTYPESET_C_API int ImTypeset_GetParagraphFormula(const ImTypeset_Paragraph* paragraph, int index,
                                                  ImTypeset_Formula* out);
/* ImTypesetFormula::Reason of formula `index` as a string in the caller's
 * buffer. Returns its length. */
IMTYPESET_C_API int ImTypeset_GetParagraphFormulaReason(const ImTypeset_Paragraph* paragraph,
                                                        int index, char* buf, int buf_size);

IMTYPESET_C_API int ImTypeset_GetParagraphSize(const ImTypeset_Paragraph* paragraph,
                                               ImTypeset_Vec2* out_size);
IMTYPESET_C_API int ImTypeset_GetParagraphLineCount(const ImTypeset_Paragraph* paragraph);
IMTYPESET_C_API int ImTypeset_GetParagraphLine(const ImTypeset_Paragraph* paragraph, int line,
                                               ImTypeset_Line* out);

/* `selection` may be null. `draw_list` null is the current window's. [E] */
IMTYPESET_C_API int ImTypeset_DrawParagraph(const ImTypeset_Paragraph* paragraph,
                                            struct ImDrawList* draw_list, const ImTypeset_Vec2* pos,
                                            const ImTypeset_TextRange* selection);

/* ImTypeset::DrawParagraphToBuffers() into two arrays of the caller's.
 * `vertices` holds `vertices_capacity` ImDrawVert and `indices`
 * `indices_capacity` ImDrawIdx, in the layout and of the sizes
 * ImTypeset_BuildInfo gives. `out_vertices_count` and `out_indices_count`
 * receive how many of each the paragraph makes, whatever the capacities; when
 * either is larger than its capacity both arrays hold a prefix and the call
 * is repeated with room for all of it. An index is a position in `vertices`:
 * the first vertex written is 0. A host that puts several paragraphs into one
 * buffer adds its own base to each index, or draws each with a base vertex.
 * The status is false, with both counts zero, for what the C++ function
 * refuses; a capacity that is too small is not a failure. [E] */
IMTYPESET_C_API int
ImTypeset_DrawParagraphToBuffers(const ImTypeset_Paragraph* paragraph, const ImTypeset_Vec2* pos,
                                 const ImTypeset_Vec4* clip_rect, void* vertices,
                                 int vertices_capacity, void* indices, int indices_capacity,
                                 const ImTypeset_TextRange* selection, int* out_vertices_count,
                                 int* out_indices_count);

/* --- Hit testing, selection, copy ------------------------------------------------- */

IMTYPESET_C_API int ImTypeset_HitTestParagraph(const ImTypeset_Paragraph* paragraph,
                                               const ImTypeset_Vec2* local_pos, ImTypeset_Hit* out);
IMTYPESET_C_API int ImTypeset_SelectParagraphRange(const ImTypeset_Paragraph* paragraph,
                                                   const ImTypeset_Vec2* anchor,
                                                   const ImTypeset_Vec2* active,
                                                   ImTypeset_Granularity granularity,
                                                   ImTypeset_TextRange* out);
/* One rectangle per line the range touches, into `out_rects`, which holds
 * `rects_capacity` of them. Returns how many there are. */
IMTYPESET_C_API int ImTypeset_GetParagraphRangeRects(const ImTypeset_Paragraph* paragraph,
                                                     const ImTypeset_TextRange* range,
                                                     ImTypeset_Vec4* out_rects, int rects_capacity);
IMTYPESET_C_API int ImTypeset_GetParagraphCaretRect(const ImTypeset_Paragraph* paragraph,
                                                    int byte_offset, ImTypeset_Vec4* out);
/* The text of the range, as a string in the caller's buffer. Returns its
 * length. The C++ function appends to a buffer and puts a blank line between
 * what was there and what it adds; here the caller owns the buffer, so it
 * says whether text precedes: with `after_text` non-zero the result begins
 * with that blank line ("\n\n") whenever the paragraph adds anything. Call it
 * for each paragraph of a document in turn, `after_text` being "anything was
 * written so far", and the pieces end to end are the document's selection. */
IMTYPESET_C_API int ImTypeset_AppendParagraphRangeText(const ImTypeset_Paragraph* paragraph,
                                                       const ImTypeset_TextRange* range,
                                                       int after_text, char* buf, int buf_size);

/* --- Documents -------------------------------------------------------------------- */
/*
 * ImTypeset::CreateDocument() and the rest of "Documents" in imtypeset.h,
 * which says what each does. A struct the library fills is the caller's `out`.
 * ImTypeset_DocumentContent and ImTypeset_DocumentDraw hold pointers to
 * arrays of the caller's, read during the call and not kept.
 */

/* The second part of IMTYPESET_C_CHECKVERSION(). [E] */
IMTYPESET_C_API int ImTypeset_DebugCheckDocumentDataLayout(size_t sz_block, size_t sz_block_style,
                                                           size_t sz_container, size_t sz_content,
                                                           size_t sz_options, size_t sz_position,
                                                           size_t sz_range, size_t sz_mark,
                                                           size_t sz_draw, size_t sz_hit,
                                                           size_t sz_geometry, size_t sz_stats);
/* The third part of IMTYPESET_C_CHECKVERSION(): the structs of a table. [E] */
IMTYPESET_C_API int ImTypeset_DebugCheckTableDataLayout(size_t sz_cell, size_t sz_column,
                                                        size_t sz_table, size_t sz_column_geometry,
                                                        size_t sz_row_geometry,
                                                        size_t sz_cell_position);
/* The fourth part of IMTYPESET_C_CHECKVERSION(): the structs of columns. [E] */
IMTYPESET_C_API int ImTypeset_DebugCheckColumnDataLayout(size_t sz_columns,
                                                         size_t sz_column_geometry);

IMTYPESET_C_API ImTypeset_Document* ImTypeset_CreateDocument(void); /* [E] */
IMTYPESET_C_API int ImTypeset_DestroyDocument(ImTypeset_Document* document);

/* `content` may be null: no blocks. [E] */
IMTYPESET_C_API int ImTypeset_SetDocumentBlocks(ImTypeset_Document* document,
                                                const ImTypeset_DocumentContent* content);
IMTYPESET_C_API int ImTypeset_ReplaceDocumentBlocks(ImTypeset_Document* document, int first,
                                                    int count,
                                                    const ImTypeset_DocumentContent* content);
/* `text` is (begin, end). [E] */
IMTYPESET_C_API int ImTypeset_AppendDocumentText(ImTypeset_Document* document, const char* text,
                                                 const char* text_end, const ImTypeset_Run* runs,
                                                 int runs_count);
/* `text` is (begin, end). `cells` points at `cells_count` ImTypeset_Cell and
 * `runs` at `runs_count` ImTypeset_Run; either may be null. All are copied. [E] */
IMTYPESET_C_API int ImTypeset_AppendDocumentTableCells(ImTypeset_Document* document,
                                                       const char* text, const char* text_end,
                                                       const ImTypeset_Cell* cells, int cells_count,
                                                       const ImTypeset_Run* runs, int runs_count);
/* `columns` points at `columns_count` ImTypeset_TableColumn, or is null: no
 * column is described. They are copied. [E] */
IMTYPESET_C_API int ImTypeset_SetDocumentTableColumns(ImTypeset_Document* document, int block,
                                                      const ImTypeset_TableColumn* columns,
                                                      int columns_count);
IMTYPESET_C_API int ImTypeset_SetDocumentBlockExtent(ImTypeset_Document* document, int block,
                                                     float extent);
/* `options` may be null: the defaults. [E] */
IMTYPESET_C_API int ImTypeset_SetDocumentOptions(ImTypeset_Document* document,
                                                 const ImTypeset_DocumentOptions* options);
IMTYPESET_C_API int ImTypeset_GetDocumentBlockCount(const ImTypeset_Document* document);
IMTYPESET_C_API int ImTypeset_GetDocumentStyleCount(const ImTypeset_Document* document);
IMTYPESET_C_API int ImTypeset_GetDocumentContainerCount(const ImTypeset_Document* document);
IMTYPESET_C_API int ImTypeset_GetDocumentBlockTextSize(const ImTypeset_Document* document,
                                                       int block);

/* `style` may be null: the context's. [E] */
IMTYPESET_C_API int ImTypeset_LayoutDocument(ImTypeset_Document* document, float width,
                                             const ImTypeset_Style* style);
IMTYPESET_C_API int ImTypeset_LayoutDocumentWindow(ImTypeset_Document* document, float top,
                                                   float bottom); /* [E] */
IMTYPESET_C_API int ImTypeset_GetDocumentSize(ImTypeset_Document* document,
                                              ImTypeset_Vec2* out_size);
/* `out->Message` is the document's storage: valid until the block is laid out
 * or described again. ImTypeset_GetDocumentBlockErrorMessage() copies it out. */
IMTYPESET_C_API int ImTypeset_GetDocumentBlockError(const ImTypeset_Document* document, int block,
                                                    ImTypeset_ErrorInfo* out);
IMTYPESET_C_API int ImTypeset_GetDocumentBlockErrorMessage(const ImTypeset_Document* document,
                                                           int block, char* buf, int buf_size);
IMTYPESET_C_API int ImTypeset_GetDocumentStats(const ImTypeset_Document* document,
                                               ImTypeset_DocumentStats* out);

/* `draw` may be null. `draw_list` null is the current window's. [E] */
IMTYPESET_C_API int ImTypeset_DrawDocument(ImTypeset_Document* document,
                                           struct ImDrawList* draw_list, const ImTypeset_Vec2* pos,
                                           const ImTypeset_DocumentDraw* draw);
/* As ImTypeset_DrawParagraphToBuffers(): two arrays of the caller's with
 * their capacities, the counts whatever the capacities, indices from 0. [E] */
IMTYPESET_C_API int ImTypeset_DrawDocumentToBuffers(
    ImTypeset_Document* document, const ImTypeset_Vec2* pos, const ImTypeset_Vec4* clip_rect,
    void* vertices, int vertices_capacity, void* indices, int indices_capacity,
    const ImTypeset_DocumentDraw* draw, int* out_vertices_count, int* out_indices_count);

IMTYPESET_C_API int ImTypeset_HitTestDocument(ImTypeset_Document* document,
                                              const ImTypeset_Vec2* local_pos,
                                              ImTypeset_DocumentHit* out);
IMTYPESET_C_API int ImTypeset_SelectDocumentRange(ImTypeset_Document* document,
                                                  const ImTypeset_Vec2* anchor,
                                                  const ImTypeset_Vec2* active,
                                                  ImTypeset_Granularity granularity,
                                                  ImTypeset_DocumentRange* out);
/* Into `out_rects`, which holds `rects_capacity`. Returns how many there are. */
IMTYPESET_C_API int ImTypeset_GetDocumentRangeRects(ImTypeset_Document* document,
                                                    const ImTypeset_DocumentRange* range,
                                                    ImTypeset_Vec4* out_rects, int rects_capacity);
IMTYPESET_C_API int ImTypeset_GetDocumentCaretRect(ImTypeset_Document* document,
                                                   const ImTypeset_Position* position,
                                                   ImTypeset_Vec4* out);
/* The text of the range as a string in the caller's buffer; returns its
 * length. `separator` is (begin, end) and goes between two blocks; both null
 * is a blank line. The C++ function appends to a buffer; this answers the
 * range's text alone. */
IMTYPESET_C_API int ImTypeset_AppendDocumentRangeText(ImTypeset_Document* document,
                                                      const ImTypeset_DocumentRange* range,
                                                      const char* separator,
                                                      const char* separator_end, char* buf,
                                                      int buf_size);
IMTYPESET_C_API int ImTypeset_GetDocumentBlockGeometry(ImTypeset_Document* document, int block,
                                                       ImTypeset_BlockGeometry* out);
IMTYPESET_C_API int ImTypeset_GetDocumentBlockLine(ImTypeset_Document* document, int block,
                                                   int line, ImTypeset_Line* out);
/* A table, one of its columns and one of its rows. The status is 1 whatever
 * `block`, `column` and `row` name: what is not there is all zero in `out`. */
IMTYPESET_C_API int ImTypeset_GetDocumentTableGeometry(ImTypeset_Document* document, int block,
                                                       ImTypeset_TableGeometry* out);
IMTYPESET_C_API int ImTypeset_GetDocumentTableColumn(ImTypeset_Document* document, int block,
                                                     int column,
                                                     ImTypeset_TableColumnGeometry* out);
IMTYPESET_C_API int ImTypeset_GetDocumentTableRow(ImTypeset_Document* document, int block, int row,
                                                  ImTypeset_TableRowGeometry* out);
/* Columns: see "Columns" in imtypeset.h. `columns` may be null: no columns. [E] */
IMTYPESET_C_API int ImTypeset_SetDocumentColumns(ImTypeset_Document* document,
                                                 const ImTypeset_Columns* columns);
/* A count: 1 for a document shown uncut and for a null document. */
IMTYPESET_C_API int ImTypeset_GetDocumentColumnCount(ImTypeset_Document* document);
/* 1 unless `out` is null; a column that is not there has Exists false and the
 * rest zero. */
IMTYPESET_C_API int ImTypeset_GetDocumentColumn(ImTypeset_Document* document, int column,
                                                ImTypeset_ColumnGeometry* out);
/* `uncut_pos` and `out` may be the same struct. */
IMTYPESET_C_API int ImTypeset_GetDocumentColumnPos(ImTypeset_Document* document,
                                                   const ImTypeset_Vec2* uncut_pos,
                                                   ImTypeset_Vec2* out);
IMTYPESET_C_API int ImTypeset_GetDocumentTableCell(const ImTypeset_Document* document,
                                                   const ImTypeset_Position* position,
                                                   ImTypeset_CellPosition* out);
IMTYPESET_C_API int ImTypeset_GetDocumentTablePosition(const ImTypeset_Document* document,
                                                       int block, int row, int column, int offset,
                                                       ImTypeset_Position* out);
/* `str_id` is (begin, end). `size` null is (0, 0); `selection`, `draw` and
 * `style` may be null. [E] */
IMTYPESET_C_API int ImTypeset_DocumentView(const char* str_id, const char* str_id_end,
                                           ImTypeset_Document* document, const ImTypeset_Vec2* size,
                                           ImTypeset_DocumentRange* selection,
                                           const ImTypeset_DocumentDraw* draw,
                                           const ImTypeset_Style* style);

/* --- Offsets for bindings --------------------------------------------------------- */

/* `text` is (begin, end), together and first, as text is everywhere in this
 * header: so the offset is the THIRD argument here, where the C++ functions
 * take (text, offset, text_end = NULL). */
IMTYPESET_C_API int ImTypeset_TextCodepointToByteOffset(const char* text, const char* text_end,
                                                        int codepoint_offset);
IMTYPESET_C_API int ImTypeset_TextByteToCodepointOffset(const char* text, const char* text_end,
                                                        int byte_offset);
IMTYPESET_C_API int ImTypeset_ParagraphCodepointToByteOffset(const ImTypeset_Paragraph* paragraph,
                                                             int codepoint_offset);
IMTYPESET_C_API int ImTypeset_ParagraphByteToCodepointOffset(const ImTypeset_Paragraph* paragraph,
                                                             int byte_offset);

/* --- Demo ------------------------------------------------------------------------- */

/* ImTypeset::ShowDemoWindow(). `p_open` may be null; otherwise it points at an
 * int read as a bool and written 0 when the window's close button is pressed.
 * Returns 1, or 0 with the error set if a C++ exception was caught. */
IMTYPESET_C_API int ImTypeset_ShowDemoWindow(int* p_open);

/* The sections of the demo window. The values are stable: a new section is
 * added before COUNT and none is renumbered. */
enum ImTypeset_DemoSection_ {
    ImTypeset_DemoSection_Paragraph = 0,
    ImTypeset_DemoSection_Faces = 1,
    ImTypeset_DemoSection_Specimen = 2,
    ImTypeset_DemoSection_Math = 3,
    ImTypeset_DemoSection_Formulas = 4,
    ImTypeset_DemoSection_Selection = 5,
    ImTypeset_DemoSection_Inspector = 6,
    ImTypeset_DemoSection_Stress = 7,
    ImTypeset_DemoSection_Fonts = 8,
    ImTypeset_DemoSection_KnownLimits = 9,
    ImTypeset_DemoSection_Document = 10,
    ImTypeset_DemoSection_COUNT = 11
};

/* ImTypeset::ShowDemoSection(): the contents of one section, into the current
 * window. Returns 1, or 0 with the error set if a C++ exception was caught. */
IMTYPESET_C_API int ImTypeset_ShowDemoSection(ImTypeset_DemoSection section);
/* ImTypeset::GetDemoSectionName(). Never null; "" for a value that is not a
 * section. */
IMTYPESET_C_API const char* ImTypeset_GetDemoSectionName(ImTypeset_DemoSection section);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IMTYPESET_C_H */
