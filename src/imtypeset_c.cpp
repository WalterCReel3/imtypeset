// The C ABI of include/imtypeset_c.h, over the C++ API of include/imtypeset.h.
//
// Nothing here typesets, measures or decides. Each function converts its
// arguments, calls the C++ function of the same name, and converts the answer
// by the rules at the top of imtypeset_c.h. Four things are this file's own,
// because the boundary needs them and the C++ API has no reason to:
//   - the proof that every mirrored struct and enumerator is what the C++
//     header says (the static_asserts below);
//   - the error slot for the two failures only this layer has, a null `out`
//     and a caught exception;
//   - the copies that turn (begin, end) into the zero-terminated strings three
//     C++ parameters take, and ImVector / ImGuiTextBuffer output into the
//     caller's buffers;
//   - ImTypeset_GetBuildInfo().
//
// EXCEPTIONS. The library is built without them, and then nothing below
// catches. Compiled with them enabled (a host whose IM_ASSERT throws: Dear
// ImGui Bundle's Python build) every entry runs inside `guarded`, which is the
// one place in this library where try and catch appear, under
// `#if defined(__cpp_exceptions)`.

#include "imtypeset_c.h"

#include "imtypeset.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__cpp_exceptions)
#include <exception>
#endif

// --- The mirror, proved ---------------------------------------------------------------

namespace
{

// Two fields are the same field if they are the same scalar type, or are both
// structs of one size (ImVec2 and ImTypeset_Vec2, each proved on its own).
template<typename A, typename B>
struct SameField {
    static const bool value =
        std::is_same<A, B>::value || (std::is_class<A>::value && std::is_class<B>::value &&
                                      sizeof(A) == sizeof(B) && alignof(A) == alignof(B));
};

} // namespace

#define IMTYPESET_C_MIRROR(CType, CppType)                                                      \
    static_assert(sizeof(CType) == sizeof(CppType), #CType " and " #CppType " differ in size"); \
    static_assert(alignof(CType) == alignof(CppType),                                           \
                  #CType " and " #CppType " differ in alignment");                              \
    static_assert(std::is_standard_layout<CType>::value &&                                      \
                      std::is_standard_layout<CppType>::value &&                                \
                      std::is_trivially_copyable<CType>::value &&                               \
                      std::is_trivially_copyable<CppType>::value,                               \
                  #CType " or " #CppType " cannot be copied byte for byte")

#define IMTYPESET_C_FIELD(CType, CppType, Field)                                        \
    static_assert(offsetof(CType, Field) == offsetof(CppType, Field),                   \
                  #CType "::" #Field " is not where " #CppType " has it");              \
    static_assert((SameField<decltype(CType::Field), decltype(CppType::Field)>::value), \
                  #CType "::" #Field " is not the type " #CppType " has")

#define IMTYPESET_C_ENUM(Group, Name)                              \
    static_assert(static_cast<int>(ImTypeset_##Group##_##Name) ==  \
                      static_cast<int>(ImTypeset##Group##_##Name), \
                  "ImTypeset_" #Group "_" #Name " is not ImTypeset" #Group "_" #Name)

IMTYPESET_C_MIRROR(ImTypeset_Vec2, ImVec2);
IMTYPESET_C_FIELD(ImTypeset_Vec2, ImVec2, x);
IMTYPESET_C_FIELD(ImTypeset_Vec2, ImVec2, y);

IMTYPESET_C_MIRROR(ImTypeset_Vec4, ImVec4);
IMTYPESET_C_FIELD(ImTypeset_Vec4, ImVec4, x);
IMTYPESET_C_FIELD(ImTypeset_Vec4, ImVec4, y);
IMTYPESET_C_FIELD(ImTypeset_Vec4, ImVec4, z);
IMTYPESET_C_FIELD(ImTypeset_Vec4, ImVec4, w);

IMTYPESET_C_MIRROR(ImTypeset_Style, ImTypesetStyle);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, Face);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, FontSize);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, EmSize);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, LineHeight);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, Align);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, LeftIndent);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, FirstLineIndent);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, Spacing);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, LineBreak);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, PixelsPerInch);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, Color);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, SelectionColor);
IMTYPESET_C_FIELD(ImTypeset_Style, ImTypesetStyle, Advances);

IMTYPESET_C_MIRROR(ImTypeset_Run, ImTypesetRun);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Start);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, End);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Face);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Scale);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Color);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Flags);
IMTYPESET_C_FIELD(ImTypeset_Run, ImTypesetRun, Tag);

IMTYPESET_C_MIRROR(ImTypeset_TextRange, ImTypesetTextRange);
IMTYPESET_C_FIELD(ImTypeset_TextRange, ImTypesetTextRange, Start);
IMTYPESET_C_FIELD(ImTypeset_TextRange, ImTypesetTextRange, End);

IMTYPESET_C_MIRROR(ImTypeset_Line, ImTypesetLine);
IMTYPESET_C_FIELD(ImTypeset_Line, ImTypesetLine, Min);
IMTYPESET_C_FIELD(ImTypeset_Line, ImTypesetLine, Max);
IMTYPESET_C_FIELD(ImTypeset_Line, ImTypesetLine, Baseline);
IMTYPESET_C_FIELD(ImTypeset_Line, ImTypesetLine, TextStart);
IMTYPESET_C_FIELD(ImTypeset_Line, ImTypesetLine, TextEnd);

IMTYPESET_C_MIRROR(ImTypeset_Hit, ImTypesetHit);
IMTYPESET_C_FIELD(ImTypeset_Hit, ImTypesetHit, TextOffset);
IMTYPESET_C_FIELD(ImTypeset_Hit, ImTypesetHit, Line);
IMTYPESET_C_FIELD(ImTypeset_Hit, ImTypesetHit, Inside);
IMTYPESET_C_FIELD(ImTypeset_Hit, ImTypesetHit, Tag);

IMTYPESET_C_MIRROR(ImTypeset_FontMetrics, ImTypesetFontMetrics);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, EmSize);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, FontSize);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, Ascent);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, Descent);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, LineGap);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, XHeight);
IMTYPESET_C_FIELD(ImTypeset_FontMetrics, ImTypesetFontMetrics, SpaceAdvance);
static_assert(sizeof(ImTypeset_FontMetrics) == 7 * sizeof(float),
              "ImTypeset_FontMetrics has a field the mirror does not name");

IMTYPESET_C_MIRROR(ImTypeset_ErrorInfo, ImTypesetErrorInfo);
IMTYPESET_C_FIELD(ImTypeset_ErrorInfo, ImTypesetErrorInfo, Code);
IMTYPESET_C_FIELD(ImTypeset_ErrorInfo, ImTypesetErrorInfo, BytePos);
IMTYPESET_C_FIELD(ImTypeset_ErrorInfo, ImTypesetErrorInfo, CodepointPos);
IMTYPESET_C_FIELD(ImTypeset_ErrorInfo, ImTypesetErrorInfo, Message);

IMTYPESET_C_MIRROR(ImTypeset_Formula, ImTypesetFormula);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, TextStart);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, TextEnd);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, Display);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, Refused);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, BytePos);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, CodepointPos);
IMTYPESET_C_FIELD(ImTypeset_Formula, ImTypesetFormula, Reason);

IMTYPESET_C_MIRROR(ImTypeset_Stats, ImTypesetStats);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, TextBuilds);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, Layouts);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, DeferredLayouts);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, DrawCalls);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, CulledCalls);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, VerticesEmitted);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, BytesHashed);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, CacheEvictions);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, CachedParagraphs);
IMTYPESET_C_FIELD(ImTypeset_Stats, ImTypesetStats, CachedBytes);

static_assert((std::is_same<ImTypeset_U32, ImU32>::value), "ImTypeset_U32 is not ImU32");
static_assert((std::is_same<ImTypeset_U64, ImU64>::value), "ImTypeset_U64 is not ImU64");
static_assert(IMTYPESET_C_VERSION_NUM == IMTYPESET_VERSION_NUM,
              "imtypeset_c.h and imtypeset.h are different versions");
static_assert(sizeof(IMTYPESET_C_VERSION) == sizeof(IMTYPESET_VERSION),
              "imtypeset_c.h and imtypeset.h are different versions");

IMTYPESET_C_ENUM(Face, Default);
IMTYPESET_C_ENUM(Face, Regular);
IMTYPESET_C_ENUM(Face, Bold);
IMTYPESET_C_ENUM(Face, Italic);
IMTYPESET_C_ENUM(Face, BoldItalic);
IMTYPESET_C_ENUM(Face, Code);
IMTYPESET_C_ENUM(Face, CodeBold);
IMTYPESET_C_ENUM(Face, Math);
IMTYPESET_C_ENUM(Face, COUNT);
IMTYPESET_C_ENUM(Align, Justify);
IMTYPESET_C_ENUM(Align, Left);
IMTYPESET_C_ENUM(Align, Center);
IMTYPESET_C_ENUM(Align, Right);
IMTYPESET_C_ENUM(Spacing, Sentence);
IMTYPESET_C_ENUM(Spacing, French);
IMTYPESET_C_ENUM(Spacing, TeX);
IMTYPESET_C_ENUM(LineBreak, Auto);
IMTYPESET_C_ENUM(LineBreak, FirstFit);
IMTYPESET_C_ENUM(LineBreak, TotalFit);
IMTYPESET_C_ENUM(Advances, Default);
IMTYPESET_C_ENUM(Advances, Font);
IMTYPESET_C_ENUM(Advances, Device);
IMTYPESET_C_ENUM(Granularity, Character);
IMTYPESET_C_ENUM(Granularity, Word);
IMTYPESET_C_ENUM(Granularity, Line);
IMTYPESET_C_ENUM(Error, None);
IMTYPESET_C_ENUM(Error, NoContext);
IMTYPESET_C_ENUM(Error, VersionMismatch);
IMTYPESET_C_ENUM(Error, InvalidArgument);
IMTYPESET_C_ENUM(Error, FileNotFound);
IMTYPESET_C_ENUM(Error, FontRefused);
IMTYPESET_C_ENUM(Error, NoFont);
IMTYPESET_C_ENUM(Error, TextTooLong);
IMTYPESET_C_ENUM(Error, MathRefused);
IMTYPESET_C_ENUM(Error, LayoutFailed);
IMTYPESET_C_ENUM(Error, DescriptionCut);
IMTYPESET_C_ENUM(Error, TooManyVertices);
IMTYPESET_C_ENUM(Error, BackendUnsupported);
IMTYPESET_C_ENUM(TextFlags, None);
IMTYPESET_C_ENUM(TextFlags, HardBreaks);
IMTYPESET_C_ENUM(TextFlags, Math);
IMTYPESET_C_ENUM(RunFlags, None);
IMTYPESET_C_ENUM(RunFlags, NoBreak);
IMTYPESET_C_ENUM(StatusFlags, None);
IMTYPESET_C_ENUM(StatusFlags, LaidOut);
IMTYPESET_C_ENUM(StatusFlags, FirstFitFallback);
IMTYPESET_C_ENUM(StatusFlags, DisplayMath);

// A pointer to an array of mirrored structs: the same place, and a pointer.
#define IMTYPESET_C_POINTER(CType, CppType, Field)                         \
    static_assert(offsetof(CType, Field) == offsetof(CppType, Field),      \
                  #CType "::" #Field " is not where " #CppType " has it"); \
    static_assert(std::is_pointer<decltype(CType::Field)>::value &&        \
                      std::is_pointer<decltype(CppType::Field)>::value,    \
                  #CType "::" #Field " is not a pointer on both sides")

IMTYPESET_C_MIRROR(ImTypeset_BlockStyle, ImTypesetBlockStyle);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, Face);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, Scale);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, LineHeight);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, Align);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, FirstLineIndent);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, SpaceAbove);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, SpaceBelow);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, Color);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, MarkerFace);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, MarkerColor);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, MarkerGap);
IMTYPESET_C_FIELD(ImTypeset_BlockStyle, ImTypesetBlockStyle, Advances);
IMTYPESET_C_MIRROR(ImTypeset_Container, ImTypesetContainer);
IMTYPESET_C_FIELD(ImTypeset_Container, ImTypesetContainer, Kind);
IMTYPESET_C_FIELD(ImTypeset_Container, ImTypesetContainer, Parent);
IMTYPESET_C_FIELD(ImTypeset_Container, ImTypesetContainer, Indent);
IMTYPESET_C_FIELD(ImTypeset_Container, ImTypesetContainer, Flags);
IMTYPESET_C_FIELD(ImTypeset_Container, ImTypesetContainer, Color);
IMTYPESET_C_MIRROR(ImTypeset_Block, ImTypesetBlock);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Kind);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Flags);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Level);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Style);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, TextStart);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, TextEnd);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, RunStart);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, RunCount);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, MarkerStart);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, MarkerEnd);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, LeftIndent);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, QuoteDepth);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Container);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Item);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, Extent);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, CellStart);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, CellCount);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, ColumnStart);
IMTYPESET_C_FIELD(ImTypeset_Block, ImTypesetBlock, ColumnCount);
IMTYPESET_C_MIRROR(ImTypeset_Cell, ImTypesetCell);
IMTYPESET_C_FIELD(ImTypeset_Cell, ImTypesetCell, TextStart);
IMTYPESET_C_FIELD(ImTypeset_Cell, ImTypesetCell, TextEnd);
IMTYPESET_C_FIELD(ImTypeset_Cell, ImTypesetCell, Flags);
IMTYPESET_C_MIRROR(ImTypeset_TableColumn, ImTypesetTableColumn);
IMTYPESET_C_FIELD(ImTypeset_TableColumn, ImTypesetTableColumn, Align);
IMTYPESET_C_FIELD(ImTypeset_TableColumn, ImTypesetTableColumn, Width);
IMTYPESET_C_FIELD(ImTypeset_TableColumn, ImTypesetTableColumn, Share);
IMTYPESET_C_MIRROR(ImTypeset_DocumentContent, ImTypesetDocumentContent);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, Text);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, TextSize);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Blocks);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, BlockCount);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Runs);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, RunCount);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Styles);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, StyleCount);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Containers);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, ContainerCount);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Cells);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, CellCount);
IMTYPESET_C_POINTER(ImTypeset_DocumentContent, ImTypesetDocumentContent, Columns);
IMTYPESET_C_FIELD(ImTypeset_DocumentContent, ImTypesetDocumentContent, ColumnCount);
IMTYPESET_C_MIRROR(ImTypeset_DocumentOptions, ImTypesetDocumentOptions);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, QuoteIndent);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, QuoteBarThickness);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, QuoteBarColor);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, RuleColor);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, CodeOverflow);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, TableCellPadding);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, TableRuleThickness);
IMTYPESET_C_FIELD(ImTypeset_DocumentOptions, ImTypesetDocumentOptions, TableRuleColor);
IMTYPESET_C_MIRROR(ImTypeset_Position, ImTypesetPosition);
IMTYPESET_C_FIELD(ImTypeset_Position, ImTypesetPosition, Block);
IMTYPESET_C_FIELD(ImTypeset_Position, ImTypesetPosition, Offset);
IMTYPESET_C_MIRROR(ImTypeset_DocumentRange, ImTypesetDocumentRange);
IMTYPESET_C_FIELD(ImTypeset_DocumentRange, ImTypesetDocumentRange, Start);
IMTYPESET_C_FIELD(ImTypeset_DocumentRange, ImTypesetDocumentRange, End);
IMTYPESET_C_MIRROR(ImTypeset_Mark, ImTypesetMark);
IMTYPESET_C_FIELD(ImTypeset_Mark, ImTypesetMark, Range);
IMTYPESET_C_FIELD(ImTypeset_Mark, ImTypesetMark, Color);
IMTYPESET_C_FIELD(ImTypeset_Mark, ImTypesetMark, Place);
IMTYPESET_C_FIELD(ImTypeset_Mark, ImTypesetMark, Thickness);
IMTYPESET_C_FIELD(ImTypeset_Mark, ImTypesetMark, Offset);
IMTYPESET_C_MIRROR(ImTypeset_DocumentDraw, ImTypesetDocumentDraw);
IMTYPESET_C_FIELD(ImTypeset_DocumentDraw, ImTypesetDocumentDraw, Selection);
IMTYPESET_C_POINTER(ImTypeset_DocumentDraw, ImTypesetDocumentDraw, Marks);
IMTYPESET_C_FIELD(ImTypeset_DocumentDraw, ImTypesetDocumentDraw, MarkCount);
IMTYPESET_C_FIELD(ImTypeset_DocumentDraw, ImTypesetDocumentDraw, Flags);
IMTYPESET_C_MIRROR(ImTypeset_DocumentHit, ImTypesetDocumentHit);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Position);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Line);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Tag);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Inside);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, InBlock);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Row);
IMTYPESET_C_FIELD(ImTypeset_DocumentHit, ImTypesetDocumentHit, Column);
IMTYPESET_C_MIRROR(ImTypeset_BlockGeometry, ImTypesetBlockGeometry);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, Min);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, Max);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, FullMin);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, FullMax);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, LineCount);
IMTYPESET_C_FIELD(ImTypeset_BlockGeometry, ImTypesetBlockGeometry, LaidOut);
IMTYPESET_C_MIRROR(ImTypeset_TableGeometry, ImTypesetTableGeometry);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, RowCount);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, ColumnCount);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, CellCount);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, Min);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, Max);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, Stated);
IMTYPESET_C_FIELD(ImTypeset_TableGeometry, ImTypesetTableGeometry, LaidOut);
IMTYPESET_C_MIRROR(ImTypeset_TableColumnGeometry, ImTypesetTableColumnGeometry);
IMTYPESET_C_FIELD(ImTypeset_TableColumnGeometry, ImTypesetTableColumnGeometry, Min);
IMTYPESET_C_FIELD(ImTypeset_TableColumnGeometry, ImTypesetTableColumnGeometry, Max);
IMTYPESET_C_FIELD(ImTypeset_TableColumnGeometry, ImTypesetTableColumnGeometry, Natural);
IMTYPESET_C_FIELD(ImTypeset_TableColumnGeometry, ImTypesetTableColumnGeometry, Floor);
IMTYPESET_C_MIRROR(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry);
IMTYPESET_C_FIELD(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry, Min);
IMTYPESET_C_FIELD(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry, Max);
IMTYPESET_C_FIELD(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry, CellStart);
IMTYPESET_C_FIELD(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry, CellCount);
IMTYPESET_C_FIELD(ImTypeset_TableRowGeometry, ImTypesetTableRowGeometry, Wrapped);
IMTYPESET_C_MIRROR(ImTypeset_CellPosition, ImTypesetCellPosition);
IMTYPESET_C_FIELD(ImTypeset_CellPosition, ImTypesetCellPosition, Row);
IMTYPESET_C_FIELD(ImTypeset_CellPosition, ImTypesetCellPosition, Column);
IMTYPESET_C_FIELD(ImTypeset_CellPosition, ImTypesetCellPosition, Cell);
IMTYPESET_C_FIELD(ImTypeset_CellPosition, ImTypesetCellPosition, Offset);
IMTYPESET_C_MIRROR(ImTypeset_DocumentStats, ImTypesetDocumentStats);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, MaterialBuilds);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, BlocksLaidOut);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, UnitsStacked);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, StructureVisits);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, BlocksDrawn);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, Blocks);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, BlocksDeferred);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, TableCellsMeasured);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, TableRowsSet);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, ColumnsCut);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, BalanceHeightsTried);
IMTYPESET_C_FIELD(ImTypeset_DocumentStats, ImTypesetDocumentStats, MeasuredWithoutDevice);
IMTYPESET_C_MIRROR(ImTypeset_Columns, ImTypesetColumns);
IMTYPESET_C_FIELD(ImTypeset_Columns, ImTypesetColumns, Count);
IMTYPESET_C_FIELD(ImTypeset_Columns, ImTypesetColumns, Height);
IMTYPESET_C_FIELD(ImTypeset_Columns, ImTypesetColumns, Gutter);
IMTYPESET_C_FIELD(ImTypeset_Columns, ImTypesetColumns, Flags);
IMTYPESET_C_MIRROR(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, Min);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, Max);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, ContentMin);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, ContentMax);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, Start);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, End);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, UncutTop);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, Overfull);
IMTYPESET_C_FIELD(ImTypeset_ColumnGeometry, ImTypesetColumnGeometry, Exists);
IMTYPESET_C_ENUM(RunFlags, Formula);
IMTYPESET_C_ENUM(RunFlags, DisplayFormula);
IMTYPESET_C_ENUM(BlockKind, Paragraph);
IMTYPESET_C_ENUM(BlockKind, Heading);
IMTYPESET_C_ENUM(BlockKind, ListItem);
IMTYPESET_C_ENUM(BlockKind, Code);
IMTYPESET_C_ENUM(BlockKind, Rule);
IMTYPESET_C_ENUM(BlockKind, Host);
IMTYPESET_C_ENUM(BlockKind, Table);
IMTYPESET_C_ENUM(BlockFlags, None);
IMTYPESET_C_ENUM(BlockFlags, HardBreaks);
IMTYPESET_C_ENUM(BlockFlags, Math);
IMTYPESET_C_ENUM(BlockFlags, FixedLineHeight);
IMTYPESET_C_ENUM(BlockFlags, TableHeader);
IMTYPESET_C_ENUM(BlockFlags, TableHeld);
IMTYPESET_C_ENUM(CellFlags, None);
IMTYPESET_C_ENUM(CellFlags, RowStart);
IMTYPESET_C_ENUM(ColumnFlags, None);
IMTYPESET_C_ENUM(ColumnFlags, Balance);
IMTYPESET_C_ENUM(ContainerKind, Quote);
IMTYPESET_C_ENUM(ContainerKind, List);
IMTYPESET_C_ENUM(ContainerFlags, None);
IMTYPESET_C_ENUM(ContainerFlags, Tight);
IMTYPESET_C_ENUM(CodeOverflow, Wrap);
IMTYPESET_C_ENUM(CodeOverflow, Clip);
IMTYPESET_C_ENUM(CodeOverflow, Grow);
IMTYPESET_C_ENUM(MarkPlace, Ink);
IMTYPESET_C_ENUM(MarkPlace, Behind);
IMTYPESET_C_ENUM(MarkPlace, Under);
IMTYPESET_C_ENUM(DocumentDrawFlags, None);
IMTYPESET_C_ENUM(DocumentDrawFlags, Debug);

namespace
{

// --- Conversions ----------------------------------------------------------------------

// One mirrored struct as the other. A copy of the bytes, which the asserts
// above make the same struct; a cast of the pointer would read one type
// through another.
template<typename To, typename From>
To mirror(const From& from)
{
    static_assert(sizeof(To) == sizeof(From), "not a mirrored pair");
    To to;
    std::memcpy(static_cast<void*>(&to), static_cast<const void*>(&from), sizeof(To));
    return to;
}

// An optional struct parameter: the C++ value, and a pointer to it or null.
template<typename Cpp, typename C>
struct Optional {
    Cpp value;
    Cpp* ptr;

    explicit Optional(const C* given)
        : value()
        , ptr(nullptr)
    {
        if (given != nullptr) {
            value = mirror<Cpp>(*given);
            ptr = &value;
        }
    }
};

using OptionalStyle = Optional<ImTypesetStyle, ImTypeset_Style>;
using OptionalRange = Optional<ImTypesetTextRange, ImTypeset_TextRange>;

// --- The boundary's own failures ------------------------------------------------------

struct BoundaryError {
    bool set;
    ImTypeset_Error code;
    char message[512];
};

BoundaryError g_error = {false, ImTypeset_Error_None, {0}};

void c_fail(ImTypeset_Error code, const char* message)
{
    g_error.set = true;
    g_error.code = code;
    std::snprintf(g_error.message, sizeof(g_error.message), "%s",
                  message != nullptr ? message : "");
}

// True, with the failure recorded, when a pointer the function needs is null.
bool missing(const void* pointer, const char* what)
{
    if (pointer != nullptr)
        return false;
    c_fail(ImTypeset_Error_InvalidArgument, what);
    return true;
}

// Runs `fn`. With exceptions enabled, one that escapes it is recorded and
// `on_exception` is the answer.
template<typename Result, typename Fn>
Result caught(Result on_exception, Fn&& fn)
{
#if defined(__cpp_exceptions)
    try {
        return fn();
    } catch (const std::exception& e) {
        c_fail(ImTypeset_Error_Exception, e.what());
    } catch (...) {
        c_fail(ImTypeset_Error_Exception, "a C++ exception that is not a std::exception");
    }
    return on_exception;
#else
    static_cast<void>(on_exception);
    return fn();
#endif
}

// What every entry but the error getters runs in: the boundary's failure of
// the call before is forgotten, then `caught`.
template<typename Result, typename Fn>
Result guarded(Result on_exception, Fn&& fn)
{
    g_error.set = false;
    return caught(on_exception, static_cast<Fn&&>(fn));
}

// --- Strings in -----------------------------------------------------------------------

// (begin, end) as the zero-terminated string a C++ parameter takes: `begin`
// itself when `end` is null, and otherwise a copy in `copy`, which is kept
// between calls so that a string ID passed every frame allocates once.
const char* terminated(std::string& copy, const char* begin, const char* end)
{
    if (begin == nullptr || end == nullptr)
        return begin;
    copy.assign(begin, end > begin ? static_cast<size_t>(end - begin) : 0u);
    return copy.c_str();
}

std::string& name_scratch()
{
    static std::string scratch;
    return scratch;
}

// --- Strings and arrays out -----------------------------------------------------------

// snprintf's convention: as much of `text` as fits in `buf_size` bytes with
// its terminator, and the length of all of it. Text is cut before a UTF-8
// continuation byte, so what was written is still UTF-8 if `text` was.
int copy_string(const char* text, int length, char* buf, int buf_size)
{
    if (buf != nullptr && buf_size > 0) {
        int n = length < buf_size - 1 ? length : buf_size - 1;
        while (n > 0 && n < length && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
            --n;
        if (n > 0)
            std::memcpy(buf, text, static_cast<size_t>(n));
        buf[n] = '\0';
    }
    return length;
}

int copy_string(const char* text, char* buf, int buf_size)
{
    if (text == nullptr)
        text = "";
    return copy_string(text, static_cast<int>(std::strlen(text)), buf, buf_size);
}

// The first `capacity` of `count` elements, as bytes.
void copy_elements(void* out, int capacity, const void* data, int count, size_t element_size)
{
    const int n = count < capacity ? count : capacity;
    if (out != nullptr && data != nullptr && n > 0)
        std::memcpy(out, data, static_cast<size_t>(n) * element_size);
}

// What the C++ API appends to, between a call and the copy into the caller's
// buffers. Kept between calls, so that a host drawing to buffers every frame
// does not allocate every frame, and released by ImTypeset_DestroyContext():
// the memory is ImGui's allocator's, and must not outlive what the host does
// with that. Never destroyed at exit, for the same reason.
struct Scratch {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImVector<ImVec4> rects;
};

Scratch* g_scratch = nullptr;

Scratch& scratch()
{
    if (g_scratch == nullptr)
        g_scratch = new Scratch();
    return *g_scratch;
}

void release_scratch()
{
    if (g_scratch == nullptr)
        return;
    g_scratch->vertices.clear();
    g_scratch->indices.clear();
    g_scratch->rects.clear();
}

int status(bool ok)
{
    return ok ? 1 : 0;
}

// For an [E] function whose C++ form returns a value and not a bool.
int last_call_succeeded()
{
    return status(ImTypeset::GetLastError()->Code == ImTypesetError_None);
}

} // namespace

extern "C" {

// --- The build ------------------------------------------------------------------------

int ImTypeset_GetBuildInfo(ImTypeset_BuildInfo* out, int out_size)
{
    ImTypeset_BuildInfo info;
    std::memset(&info, 0, sizeof(info));
    info.StructSize = static_cast<int>(sizeof(ImTypeset_BuildInfo));
    info.AbiVersion = IMTYPESET_C_ABI_VERSION;
    info.VersionNum = IMTYPESET_VERSION_NUM;
    info.ImGuiVersionNum = IMGUI_VERSION_NUM;
    info.SizeofImWchar = static_cast<int>(sizeof(ImWchar));
    info.SizeofImDrawVert = static_cast<int>(sizeof(ImDrawVert));
    info.SizeofImDrawIdx = static_cast<int>(sizeof(ImDrawIdx));
    info.OffsetofImDrawVertPos = static_cast<int>(offsetof(ImDrawVert, pos));
    info.OffsetofImDrawVertUV = static_cast<int>(offsetof(ImDrawVert, uv));
    info.OffsetofImDrawVertCol = static_cast<int>(offsetof(ImDrawVert, col));
    info.SizeofPointer = static_cast<int>(sizeof(void*));
#ifdef IMGUI_ENABLE_FREETYPE
    info.ImGuiEnableFreeType = 1;
#endif
#if defined(__cpp_exceptions)
    info.CatchesExceptions = 1;
#endif
    info.FaceCount = ImTypesetFace_COUNT;
    info.SizeofStyle = static_cast<int>(sizeof(ImTypesetStyle));
    info.SizeofRun = static_cast<int>(sizeof(ImTypesetRun));
    info.SizeofTextRange = static_cast<int>(sizeof(ImTypesetTextRange));
    info.SizeofLine = static_cast<int>(sizeof(ImTypesetLine));
    info.SizeofHit = static_cast<int>(sizeof(ImTypesetHit));
    info.SizeofFormula = static_cast<int>(sizeof(ImTypesetFormula));
    info.SizeofStats = static_cast<int>(sizeof(ImTypesetStats));
    info.SizeofErrorInfo = static_cast<int>(sizeof(ImTypesetErrorInfo));
    std::snprintf(info.Version, sizeof(info.Version), "%s", IMTYPESET_VERSION);
    std::snprintf(info.ImGuiVersion, sizeof(info.ImGuiVersion), "%s", IMGUI_VERSION);
    info.SizeofBlock = static_cast<int>(sizeof(ImTypesetBlock));
    info.SizeofBlockStyle = static_cast<int>(sizeof(ImTypesetBlockStyle));
    info.SizeofContainer = static_cast<int>(sizeof(ImTypesetContainer));
    info.SizeofDocumentContent = static_cast<int>(sizeof(ImTypesetDocumentContent));
    info.SizeofDocumentOptions = static_cast<int>(sizeof(ImTypesetDocumentOptions));
    info.SizeofPosition = static_cast<int>(sizeof(ImTypesetPosition));
    info.SizeofDocumentRange = static_cast<int>(sizeof(ImTypesetDocumentRange));
    info.SizeofMark = static_cast<int>(sizeof(ImTypesetMark));
    info.SizeofDocumentDraw = static_cast<int>(sizeof(ImTypesetDocumentDraw));
    info.SizeofDocumentHit = static_cast<int>(sizeof(ImTypesetDocumentHit));
    info.SizeofBlockGeometry = static_cast<int>(sizeof(ImTypesetBlockGeometry));
    info.SizeofDocumentStats = static_cast<int>(sizeof(ImTypesetDocumentStats));
    info.SizeofCell = static_cast<int>(sizeof(ImTypesetCell));
    info.SizeofTableColumn = static_cast<int>(sizeof(ImTypesetTableColumn));
    info.SizeofTableGeometry = static_cast<int>(sizeof(ImTypesetTableGeometry));
    info.SizeofTableColumnGeometry = static_cast<int>(sizeof(ImTypesetTableColumnGeometry));
    info.SizeofTableRowGeometry = static_cast<int>(sizeof(ImTypesetTableRowGeometry));
    info.SizeofCellPosition = static_cast<int>(sizeof(ImTypesetCellPosition));
    info.SizeofColumns = static_cast<int>(sizeof(ImTypesetColumns));
    info.SizeofColumnGeometry = static_cast<int>(sizeof(ImTypesetColumnGeometry));
    if (out != nullptr && out_size > 0) {
        const size_t n = static_cast<size_t>(out_size) < sizeof(info)
                             ? static_cast<size_t>(out_size)
                             : sizeof(info);
        std::memcpy(out, &info, n);
    }
    return info.StructSize;
}

const char* ImTypeset_GetVersion(void)
{
    return IMTYPESET_VERSION;
}

int ImTypeset_GetImGuiVersionNum(void)
{
    return IMGUI_VERSION_NUM;
}

int ImTypeset_DebugCheckVersionAndDataLayout(const char* version, const char* version_end,
                                             size_t sz_imwchar, size_t sz_imdrawvert,
                                             size_t sz_imdrawidx, size_t sz_style, size_t sz_run,
                                             size_t sz_range, size_t sz_line, size_t sz_hit,
                                             size_t sz_formula, size_t sz_stats, size_t sz_error)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::DebugCheckVersionAndDataLayout(
            terminated(name_scratch(), version, version_end), sz_imwchar, sz_imdrawvert,
            sz_imdrawidx, sz_style, sz_run, sz_range, sz_line, sz_hit, sz_formula, sz_stats,
            sz_error));
    });
}

// --- Context --------------------------------------------------------------------------

ImTypeset_Context* ImTypeset_CreateContext(void)
{
    return guarded<ImTypeset_Context*>(nullptr, []() { return ImTypeset::CreateContext(); });
}

int ImTypeset_DestroyContext(ImTypeset_Context* ctx)
{
    return guarded(0, [&]() -> int {
        ImTypeset::DestroyContext(ctx);
        release_scratch();
        return 1;
    });
}

ImTypeset_Context* ImTypeset_GetCurrentContext(void)
{
    return guarded<ImTypeset_Context*>(nullptr, []() { return ImTypeset::GetCurrentContext(); });
}

int ImTypeset_SetCurrentContext(ImTypeset_Context* ctx)
{
    return guarded(0, [&]() -> int {
        ImTypeset::SetCurrentContext(ctx);
        return 1;
    });
}

ImTypeset_U64 ImTypeset_GetContextSerial(void)
{
    return guarded<ImTypeset_U64>(0,
                                  []() -> ImTypeset_U64 { return ImTypeset::GetContextSerial(); });
}

int ImTypeset_GetLastError(ImTypeset_ErrorInfo* out)
{
    if (out == nullptr)
        return 0;
    if (!g_error.set) {
        const int ok = caught(0, [&]() -> int {
            *out = mirror<ImTypeset_ErrorInfo>(*ImTypeset::GetLastError());
            return 1;
        });
        if (ok != 0)
            return 1;
    }
    out->Code = g_error.code;
    out->BytePos = -1;
    out->CodepointPos = -1;
    out->Message = g_error.message;
    return 1;
}

int ImTypeset_GetLastErrorMessage(char* buf, int buf_size)
{
    ImTypeset_ErrorInfo info;
    ImTypeset_GetLastError(&info);
    return copy_string(info.Message, buf, buf_size);
}

int ImTypeset_GetStats(ImTypeset_Stats* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetStats: out is null"))
            return 0;
        *out = mirror<ImTypeset_Stats>(ImTypeset::GetStats());
        return 1;
    });
}

int ImTypeset_GetStyle(ImTypeset_Style* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetStyle: out is null"))
            return 0;
        *out = mirror<ImTypeset_Style>(ImTypeset::GetStyle());
        return 1;
    });
}

int ImTypeset_SetStyle(const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        if (missing(style, "ImTypeset_SetStyle: style is null"))
            return 0;
        ImTypeset::GetStyle() = mirror<ImTypesetStyle>(*style);
        return 1;
    });
}

int ImTypeset_SetTextCacheLimits(int max_paragraphs, int max_bytes)
{
    return guarded(0, [&]() -> int {
        ImTypeset::SetTextCacheLimits(max_paragraphs, max_bytes);
        return 1;
    });
}

// --- Fonts ----------------------------------------------------------------------------

struct ImFont* ImTypeset_AddFontFromFileTTF(ImTypeset_Face face, const char* filename,
                                            const char* filename_end, int face_index,
                                            const struct ImFontConfig* font_cfg)
{
    return guarded<ImFont*>(nullptr, [&]() {
        return ImTypeset::AddFontFromFileTTF(
            face, terminated(name_scratch(), filename, filename_end), face_index, font_cfg);
    });
}

struct ImFont* ImTypeset_AddFontFromMemoryTTF(ImTypeset_Face face, const void* font_data,
                                              int font_data_size, int face_index,
                                              const struct ImFontConfig* font_cfg)
{
    return guarded<ImFont*>(nullptr, [&]() {
        return ImTypeset::AddFontFromMemoryTTF(face, font_data, font_data_size, face_index,
                                               font_cfg);
    });
}

int ImTypeset_RemoveFont(ImTypeset_Face face)
{
    return guarded(0, [&]() -> int { return status(ImTypeset::RemoveFont(face)); });
}

struct ImFont* ImTypeset_GetFont(ImTypeset_Face face)
{
    return guarded<ImFont*>(nullptr, [&]() { return ImTypeset::GetFont(face); });
}

int ImTypeset_FaceHasGlyph(ImTypeset_Face face, unsigned int codepoint)
{
    return guarded(0, [&]() -> int { return status(ImTypeset::FaceHasGlyph(face, codepoint)); });
}

int ImTypeset_GetFontMetrics(ImTypeset_Face face, float em_size, ImTypeset_FontMetrics* out,
                             size_t out_size)
{
    return guarded(0, [&]() -> int {
        // The size is what the caller compiled; a caller of another size is
        // refused before anything is written to it.
        ImTypesetFontMetrics metrics;
        const bool ok =
            ImTypeset::GetFontMetrics(face, em_size, out != nullptr ? &metrics : nullptr, out_size);
        if (out != nullptr && out_size == sizeof(ImTypeset_FontMetrics))
            *out = mirror<ImTypeset_FontMetrics>(metrics);
        return status(ok);
    });
}

float ImTypeset_GetGlyphAdvance(ImTypeset_Face face, float em_size, unsigned int codepoint)
{
    return guarded(0.0f, [&]() { return ImTypeset::GetGlyphAdvance(face, em_size, codepoint); });
}

float ImTypeset_GetDeviceGlyphAdvance(ImTypeset_Face face, unsigned int codepoint,
                                      const ImTypeset_Style* style, float scale)
{
    return guarded(0.0f, [&]() {
        if (style == nullptr)
            return ImTypeset::GetDeviceGlyphAdvance(face, codepoint, nullptr, scale);
        const ImTypesetStyle given = mirror<ImTypesetStyle>(*style);
        return ImTypeset::GetDeviceGlyphAdvance(face, codepoint, &given, scale);
    });
}

int ImTypeset_GetFontAtlasIndex(ImTypeset_Face face)
{
    return guarded(-1, [&]() -> int {
        // Not null only with the context's ImGui context current, so the
        // atlas looked through is the one the font is in.
        const ImFont* const font = ImTypeset::GetFont(face);
        if (font == nullptr)
            return -1;
        const ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
        if (atlas == nullptr)
            return -1;
        for (int i = 0; i < atlas->Fonts.Size; ++i) {
            if (atlas->Fonts[i] == font)
                return i;
        }
        return -1;
    });
}

// --- Widgets --------------------------------------------------------------------------

int ImTypeset_TextUnformatted(const char* text, const char* text_end, ImTypeset_TextFlags flags,
                              float wrap_width, const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        return status(ImTypeset::TextUnformatted(text, text_end, flags, wrap_width, s.ptr));
    });
}

int ImTypeset_TextV(const char* fmt, va_list args)
{
    return guarded(0, [&]() -> int { return status(ImTypeset::TextV(fmt, args)); });
}

int ImTypeset_Text(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int result = ImTypeset_TextV(fmt, args);
    va_end(args);
    return result;
}

int ImTypeset_TextWithID(const char* str_id, const char* str_id_end, const char* text,
                         const char* text_end, ImTypeset_TextFlags flags, float wrap_width,
                         const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        return status(ImTypeset::TextWithID(terminated(name_scratch(), str_id, str_id_end), text,
                                            text_end, flags, wrap_width, s.ptr));
    });
}

int ImTypeset_SelectableText(const char* str_id, const char* str_id_end, const char* text,
                             const char* text_end, ImTypeset_TextFlags flags, float wrap_width,
                             const ImTypeset_Style* style, ImTypeset_TextRange* selection)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        OptionalRange range(selection);
        const bool ok =
            ImTypeset::SelectableText(terminated(name_scratch(), str_id, str_id_end), text,
                                      text_end, flags, wrap_width, s.ptr, range.ptr);
        if (selection != nullptr)
            *selection = mirror<ImTypeset_TextRange>(range.value);
        return status(ok);
    });
}

int ImTypeset_CalcTextSize(const char* text, const char* text_end, float wrap_width,
                           ImTypeset_TextFlags flags, const ImTypeset_Style* style,
                           ImTypeset_Vec2* out_size)
{
    return guarded(0, [&]() -> int {
        if (missing(out_size, "ImTypeset_CalcTextSize: out_size is null"))
            return 0;
        const OptionalStyle s(style);
        *out_size = mirror<ImTypeset_Vec2>(
            ImTypeset::CalcTextSize(text, text_end, wrap_width, flags, s.ptr));
        return last_call_succeeded();
    });
}

// --- Paragraphs -----------------------------------------------------------------------

ImTypeset_Paragraph* ImTypeset_CreateParagraph(void)
{
    return guarded<ImTypeset_Paragraph*>(nullptr, []() { return ImTypeset::CreateParagraph(); });
}

int ImTypeset_DestroyParagraph(ImTypeset_Paragraph* paragraph)
{
    return guarded(0, [&]() -> int {
        ImTypeset::DestroyParagraph(paragraph);
        return 1;
    });
}

int ImTypeset_SetParagraphText(ImTypeset_Paragraph* paragraph, const char* text,
                               const char* text_end, const ImTypeset_Run* runs, int runs_count,
                               ImTypeset_TextFlags flags)
{
    return guarded(0, [&]() -> int {
        // The runs as the C++ struct, in a buffer kept between calls. The C++
        // function copies them again; nothing points here once it returns.
        static std::vector<ImTypesetRun> converted;
        const int count = runs != nullptr && runs_count > 0 ? runs_count : 0;
        if (count > 0) {
            converted.resize(static_cast<size_t>(count));
            std::memcpy(static_cast<void*>(converted.data()), runs,
                        static_cast<size_t>(count) * sizeof(ImTypesetRun));
        }
        return status(ImTypeset::SetParagraphText(
            paragraph, text, text_end, count > 0 ? converted.data() : nullptr, count, flags));
    });
}

int ImTypeset_LayoutParagraph(ImTypeset_Paragraph* paragraph, float width,
                              const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        return status(ImTypeset::LayoutParagraph(paragraph, width, s.ptr));
    });
}

int ImTypeset_GetParagraphError(const ImTypeset_Paragraph* paragraph, ImTypeset_ErrorInfo* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetParagraphError: out is null"))
            return 0;
        *out = mirror<ImTypeset_ErrorInfo>(*ImTypeset::GetParagraphError(paragraph));
        return 1;
    });
}

int ImTypeset_GetParagraphErrorMessage(const ImTypeset_Paragraph* paragraph, char* buf,
                                       int buf_size)
{
    return guarded(-1, [&]() -> int {
        return copy_string(ImTypeset::GetParagraphError(paragraph)->Message, buf, buf_size);
    });
}

ImTypeset_StatusFlags ImTypeset_GetParagraphStatus(const ImTypeset_Paragraph* paragraph)
{
    return guarded(0, [&]() -> int { return ImTypeset::GetParagraphStatus(paragraph); });
}

int ImTypeset_GetParagraphFormulaCount(const ImTypeset_Paragraph* paragraph)
{
    return guarded(-1, [&]() -> int { return ImTypeset::GetParagraphFormulaCount(paragraph); });
}

int ImTypeset_GetParagraphFormula(const ImTypeset_Paragraph* paragraph, int index,
                                  ImTypeset_Formula* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetParagraphFormula: out is null"))
            return 0;
        // Field by field into zeroed memory, as for ImTypeset_Hit.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(paragraph, index);
        out->TextStart = formula.TextStart;
        out->TextEnd = formula.TextEnd;
        out->Display = formula.Display;
        out->Refused = formula.Refused;
        out->BytePos = formula.BytePos;
        out->CodepointPos = formula.CodepointPos;
        out->Reason = formula.Reason;
        return 1;
    });
}

int ImTypeset_GetParagraphFormulaReason(const ImTypeset_Paragraph* paragraph, int index, char* buf,
                                        int buf_size)
{
    return guarded(-1, [&]() -> int {
        return copy_string(ImTypeset::GetParagraphFormula(paragraph, index).Reason, buf, buf_size);
    });
}

int ImTypeset_GetParagraphSize(const ImTypeset_Paragraph* paragraph, ImTypeset_Vec2* out_size)
{
    return guarded(0, [&]() -> int {
        if (missing(out_size, "ImTypeset_GetParagraphSize: out_size is null"))
            return 0;
        *out_size = mirror<ImTypeset_Vec2>(ImTypeset::GetParagraphSize(paragraph));
        return 1;
    });
}

int ImTypeset_GetParagraphLineCount(const ImTypeset_Paragraph* paragraph)
{
    return guarded(-1, [&]() -> int { return ImTypeset::GetParagraphLineCount(paragraph); });
}

int ImTypeset_GetParagraphLine(const ImTypeset_Paragraph* paragraph, int line, ImTypeset_Line* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetParagraphLine: out is null"))
            return 0;
        *out = mirror<ImTypeset_Line>(ImTypeset::GetParagraphLine(paragraph, line));
        return 1;
    });
}

int ImTypeset_DrawParagraph(const ImTypeset_Paragraph* paragraph, struct ImDrawList* draw_list,
                            const ImTypeset_Vec2* pos, const ImTypeset_TextRange* selection)
{
    return guarded(0, [&]() -> int {
        if (missing(pos, "ImTypeset_DrawParagraph: pos is null"))
            return 0;
        const OptionalRange range(selection);
        return status(
            ImTypeset::DrawParagraph(paragraph, draw_list, mirror<ImVec2>(*pos), range.ptr));
    });
}

int ImTypeset_DrawParagraphToBuffers(const ImTypeset_Paragraph* paragraph,
                                     const ImTypeset_Vec2* pos, const ImTypeset_Vec4* clip_rect,
                                     void* vertices, int vertices_capacity, void* indices,
                                     int indices_capacity, const ImTypeset_TextRange* selection,
                                     int* out_vertices_count, int* out_indices_count)
{
    return guarded(0, [&]() -> int {
        if (out_vertices_count != nullptr)
            *out_vertices_count = 0;
        if (out_indices_count != nullptr)
            *out_indices_count = 0;
        if (missing(pos, "ImTypeset_DrawParagraphToBuffers: pos is null") ||
            missing(clip_rect, "ImTypeset_DrawParagraphToBuffers: clip_rect is null") ||
            missing(out_vertices_count,
                    "ImTypeset_DrawParagraphToBuffers: out_vertices_count is null") ||
            missing(out_indices_count,
                    "ImTypeset_DrawParagraphToBuffers: out_indices_count is null"))
            return 0;
        Scratch& s = scratch();
        s.vertices.resize(0);
        s.indices.resize(0);
        const OptionalRange range(selection);
        if (!ImTypeset::DrawParagraphToBuffers(paragraph, mirror<ImVec2>(*pos),
                                               mirror<ImVec4>(*clip_rect), &s.vertices, &s.indices,
                                               range.ptr))
            return 0;
        copy_elements(vertices, vertices_capacity, s.vertices.Data, s.vertices.Size,
                      sizeof(ImDrawVert));
        copy_elements(indices, indices_capacity, s.indices.Data, s.indices.Size, sizeof(ImDrawIdx));
        *out_vertices_count = s.vertices.Size;
        *out_indices_count = s.indices.Size;
        return 1;
    });
}

// --- Hit testing, selection, copy -----------------------------------------------------

int ImTypeset_HitTestParagraph(const ImTypeset_Paragraph* paragraph,
                               const ImTypeset_Vec2* local_pos, ImTypeset_Hit* out)
{
    return guarded(0, [&]() -> int {
        if (missing(local_pos, "ImTypeset_HitTestParagraph: local_pos is null") ||
            missing(out, "ImTypeset_HitTestParagraph: out is null"))
            return 0;
        // Field by field into zeroed memory: the struct has padding, and a
        // caller may hash or compare what it was given as bytes.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetHit hit = ImTypeset::HitTestParagraph(paragraph, mirror<ImVec2>(*local_pos));
        out->TextOffset = hit.TextOffset;
        out->Line = hit.Line;
        out->Inside = hit.Inside;
        out->Tag = hit.Tag;
        return 1;
    });
}

int ImTypeset_SelectParagraphRange(const ImTypeset_Paragraph* paragraph,
                                   const ImTypeset_Vec2* anchor, const ImTypeset_Vec2* active,
                                   ImTypeset_Granularity granularity, ImTypeset_TextRange* out)
{
    return guarded(0, [&]() -> int {
        if (missing(anchor, "ImTypeset_SelectParagraphRange: anchor is null") ||
            missing(active, "ImTypeset_SelectParagraphRange: active is null") ||
            missing(out, "ImTypeset_SelectParagraphRange: out is null"))
            return 0;
        *out = mirror<ImTypeset_TextRange>(ImTypeset::SelectParagraphRange(
            paragraph, mirror<ImVec2>(*anchor), mirror<ImVec2>(*active), granularity));
        return 1;
    });
}

int ImTypeset_GetParagraphRangeRects(const ImTypeset_Paragraph* paragraph,
                                     const ImTypeset_TextRange* range, ImTypeset_Vec4* out_rects,
                                     int rects_capacity)
{
    return guarded(-1, [&]() -> int {
        if (missing(range, "ImTypeset_GetParagraphRangeRects: range is null"))
            return 0;
        ImVector<ImVec4>& rects = scratch().rects;
        rects.resize(0);
        ImTypeset::GetParagraphRangeRects(paragraph, mirror<ImTypesetTextRange>(*range), &rects);
        copy_elements(out_rects, rects_capacity, rects.Data, rects.Size, sizeof(ImVec4));
        return rects.Size;
    });
}

int ImTypeset_GetParagraphCaretRect(const ImTypeset_Paragraph* paragraph, int byte_offset,
                                    ImTypeset_Vec4* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetParagraphCaretRect: out is null"))
            return 0;
        *out = mirror<ImTypeset_Vec4>(ImTypeset::GetParagraphCaretRect(paragraph, byte_offset));
        return 1;
    });
}

int ImTypeset_AppendParagraphRangeText(const ImTypeset_Paragraph* paragraph,
                                       const ImTypeset_TextRange* range, int after_text, char* buf,
                                       int buf_size)
{
    return guarded(-1, [&]() -> int {
        if (buf != nullptr && buf_size > 0)
            buf[0] = '\0';
        if (missing(range, "ImTypeset_AppendParagraphRangeText: range is null"))
            return 0;
        // The C++ function decides the separator by whether its buffer holds
        // text. One byte stands for the text the caller holds, and is not
        // part of the answer.
        ImGuiTextBuffer text;
        if (after_text != 0)
            text.append("-");
        const int before = text.size();
        ImTypeset::AppendParagraphRangeText(paragraph, mirror<ImTypesetTextRange>(*range), &text);
        return copy_string(text.begin() + before, text.size() - before, buf, buf_size);
    });
}

// --- Offsets for bindings -------------------------------------------------------------

int ImTypeset_TextCodepointToByteOffset(const char* text, const char* text_end,
                                        int codepoint_offset)
{
    return guarded(-1, [&]() -> int {
        return ImTypeset::TextCodepointToByteOffset(text, codepoint_offset, text_end);
    });
}

int ImTypeset_TextByteToCodepointOffset(const char* text, const char* text_end, int byte_offset)
{
    return guarded(-1, [&]() -> int {
        return ImTypeset::TextByteToCodepointOffset(text, byte_offset, text_end);
    });
}

int ImTypeset_ParagraphCodepointToByteOffset(const ImTypeset_Paragraph* paragraph,
                                             int codepoint_offset)
{
    return guarded(-1, [&]() -> int {
        return ImTypeset::ParagraphCodepointToByteOffset(paragraph, codepoint_offset);
    });
}

int ImTypeset_ParagraphByteToCodepointOffset(const ImTypeset_Paragraph* paragraph, int byte_offset)
{
    return guarded(-1, [&]() -> int {
        return ImTypeset::ParagraphByteToCodepointOffset(paragraph, byte_offset);
    });
}

} // extern "C"

// --- Documents ------------------------------------------------------------------------

namespace
{

// A struct that holds pointers to arrays of mirrored structs is itself a
// mirror: the same bytes mean the same thing on both sides.
template<typename Cpp, typename C>
const Cpp* same(const C* given)
{
    static_assert(sizeof(Cpp) == sizeof(C), "not a mirrored pair");
    return reinterpret_cast<const Cpp*>(given);
}

} // namespace

extern "C" {

int ImTypeset_DebugCheckDocumentDataLayout(size_t sz_block, size_t sz_block_style,
                                           size_t sz_container, size_t sz_content,
                                           size_t sz_options, size_t sz_position, size_t sz_range,
                                           size_t sz_mark, size_t sz_draw, size_t sz_hit,
                                           size_t sz_geometry, size_t sz_stats)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::DebugCheckDocumentDataLayout(
            sz_block, sz_block_style, sz_container, sz_content, sz_options, sz_position, sz_range,
            sz_mark, sz_draw, sz_hit, sz_geometry, sz_stats));
    });
}

int ImTypeset_DebugCheckTableDataLayout(size_t sz_cell, size_t sz_column, size_t sz_table,
                                        size_t sz_column_geometry, size_t sz_row_geometry,
                                        size_t sz_cell_position)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::DebugCheckTableDataLayout(
            sz_cell, sz_column, sz_table, sz_column_geometry, sz_row_geometry, sz_cell_position));
    });
}

int ImTypeset_DebugCheckColumnDataLayout(size_t sz_columns, size_t sz_column_geometry)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::DebugCheckColumnDataLayout(sz_columns, sz_column_geometry));
    });
}

ImTypeset_Document* ImTypeset_CreateDocument(void)
{
    return guarded<ImTypeset_Document*>(nullptr, []() { return ImTypeset::CreateDocument(); });
}

int ImTypeset_DestroyDocument(ImTypeset_Document* document)
{
    return guarded(0, [&]() -> int {
        ImTypeset::DestroyDocument(document);
        return 1;
    });
}

int ImTypeset_SetDocumentBlocks(ImTypeset_Document* document,
                                const ImTypeset_DocumentContent* content)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::SetDocumentBlocks(
            document, same<ImTypesetDocumentContent, ImTypeset_DocumentContent>(content)));
    });
}

int ImTypeset_ReplaceDocumentBlocks(ImTypeset_Document* document, int first, int count,
                                    const ImTypeset_DocumentContent* content)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::ReplaceDocumentBlocks(
            document, first, count,
            same<ImTypesetDocumentContent, ImTypeset_DocumentContent>(content)));
    });
}

int ImTypeset_AppendDocumentText(ImTypeset_Document* document, const char* text,
                                 const char* text_end, const ImTypeset_Run* runs, int runs_count)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::AppendDocumentText(
            document, text, text_end, same<ImTypesetRun, ImTypeset_Run>(runs), runs_count));
    });
}

int ImTypeset_AppendDocumentTableCells(ImTypeset_Document* document, const char* text,
                                       const char* text_end, const ImTypeset_Cell* cells,
                                       int cells_count, const ImTypeset_Run* runs, int runs_count)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::AppendDocumentTableCells(
            document, text, text_end, same<ImTypesetCell, ImTypeset_Cell>(cells), cells_count,
            same<ImTypesetRun, ImTypeset_Run>(runs), runs_count));
    });
}

int ImTypeset_SetDocumentTableColumns(ImTypeset_Document* document, int block,
                                      const ImTypeset_TableColumn* columns, int columns_count)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::SetDocumentTableColumns(
            document, block, same<ImTypesetTableColumn, ImTypeset_TableColumn>(columns),
            columns_count));
    });
}

int ImTypeset_SetDocumentBlockExtent(ImTypeset_Document* document, int block, float extent)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::SetDocumentBlockExtent(document, block, extent));
    });
}

int ImTypeset_SetDocumentOptions(ImTypeset_Document* document,
                                 const ImTypeset_DocumentOptions* options)
{
    return guarded(0, [&]() -> int {
        return status(ImTypeset::SetDocumentOptions(
            document, same<ImTypesetDocumentOptions, ImTypeset_DocumentOptions>(options)));
    });
}

int ImTypeset_GetDocumentBlockCount(const ImTypeset_Document* document)
{
    return guarded(0, [&]() -> int { return ImTypeset::GetDocumentBlockCount(document); });
}

int ImTypeset_GetDocumentStyleCount(const ImTypeset_Document* document)
{
    return guarded(0, [&]() -> int { return ImTypeset::GetDocumentStyleCount(document); });
}

int ImTypeset_GetDocumentContainerCount(const ImTypeset_Document* document)
{
    return guarded(0, [&]() -> int { return ImTypeset::GetDocumentContainerCount(document); });
}

int ImTypeset_GetDocumentBlockTextSize(const ImTypeset_Document* document, int block)
{
    return guarded(0,
                   [&]() -> int { return ImTypeset::GetDocumentBlockTextSize(document, block); });
}

int ImTypeset_LayoutDocument(ImTypeset_Document* document, float width,
                             const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        return status(ImTypeset::LayoutDocument(document, width, s.ptr));
    });
}

int ImTypeset_LayoutDocumentWindow(ImTypeset_Document* document, float top, float bottom)
{
    return guarded(
        0, [&]() -> int { return status(ImTypeset::LayoutDocumentWindow(document, top, bottom)); });
}

int ImTypeset_GetDocumentSize(ImTypeset_Document* document, ImTypeset_Vec2* out_size)
{
    return guarded(0, [&]() -> int {
        if (missing(out_size, "ImTypeset_GetDocumentSize: out_size is null"))
            return 0;
        *out_size = mirror<ImTypeset_Vec2>(ImTypeset::GetDocumentSize(document));
        return 1;
    });
}

int ImTypeset_GetDocumentBlockError(const ImTypeset_Document* document, int block,
                                    ImTypeset_ErrorInfo* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentBlockError: out is null"))
            return 0;
        *out = mirror<ImTypeset_ErrorInfo>(*ImTypeset::GetDocumentBlockError(document, block));
        return 1;
    });
}

int ImTypeset_GetDocumentBlockErrorMessage(const ImTypeset_Document* document, int block, char* buf,
                                           int buf_size)
{
    return guarded(-1, [&]() -> int {
        return copy_string(ImTypeset::GetDocumentBlockError(document, block)->Message, buf,
                           buf_size);
    });
}

int ImTypeset_GetDocumentStats(const ImTypeset_Document* document, ImTypeset_DocumentStats* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentStats: out is null"))
            return 0;
        *out = mirror<ImTypeset_DocumentStats>(ImTypeset::GetDocumentStats(document));
        return 1;
    });
}

int ImTypeset_DrawDocument(ImTypeset_Document* document, struct ImDrawList* draw_list,
                           const ImTypeset_Vec2* pos, const ImTypeset_DocumentDraw* draw)
{
    return guarded(0, [&]() -> int {
        if (missing(pos, "ImTypeset_DrawDocument: pos is null"))
            return 0;
        return status(
            ImTypeset::DrawDocument(document, draw_list, mirror<ImVec2>(*pos),
                                    same<ImTypesetDocumentDraw, ImTypeset_DocumentDraw>(draw)));
    });
}

int ImTypeset_DrawDocumentToBuffers(ImTypeset_Document* document, const ImTypeset_Vec2* pos,
                                    const ImTypeset_Vec4* clip_rect, void* vertices,
                                    int vertices_capacity, void* indices, int indices_capacity,
                                    const ImTypeset_DocumentDraw* draw, int* out_vertices_count,
                                    int* out_indices_count)
{
    return guarded(0, [&]() -> int {
        if (out_vertices_count != nullptr)
            *out_vertices_count = 0;
        if (out_indices_count != nullptr)
            *out_indices_count = 0;
        if (missing(pos, "ImTypeset_DrawDocumentToBuffers: pos is null") ||
            missing(clip_rect, "ImTypeset_DrawDocumentToBuffers: clip_rect is null") ||
            missing(out_vertices_count,
                    "ImTypeset_DrawDocumentToBuffers: out_vertices_count is null") ||
            missing(out_indices_count,
                    "ImTypeset_DrawDocumentToBuffers: out_indices_count is null"))
            return 0;
        Scratch& s = scratch();
        s.vertices.resize(0);
        s.indices.resize(0);
        // What was drawn is handed over even when a block in view has
        // something wrong with it: the status says so, as the C++ call does.
        const bool ok = ImTypeset::DrawDocumentToBuffers(
            document, mirror<ImVec2>(*pos), mirror<ImVec4>(*clip_rect), &s.vertices, &s.indices,
            same<ImTypesetDocumentDraw, ImTypeset_DocumentDraw>(draw));
        copy_elements(vertices, vertices_capacity, s.vertices.Data, s.vertices.Size,
                      sizeof(ImDrawVert));
        copy_elements(indices, indices_capacity, s.indices.Data, s.indices.Size, sizeof(ImDrawIdx));
        *out_vertices_count = s.vertices.Size;
        *out_indices_count = s.indices.Size;
        return status(ok);
    });
}

int ImTypeset_HitTestDocument(ImTypeset_Document* document, const ImTypeset_Vec2* local_pos,
                              ImTypeset_DocumentHit* out)
{
    return guarded(0, [&]() -> int {
        if (missing(local_pos, "ImTypeset_HitTestDocument: local_pos is null") ||
            missing(out, "ImTypeset_HitTestDocument: out is null"))
            return 0;
        // Field by field into zeroed memory: the struct has padding.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetDocumentHit hit =
            ImTypeset::HitTestDocument(document, mirror<ImVec2>(*local_pos));
        out->Position.Block = hit.Position.Block;
        out->Position.Offset = hit.Position.Offset;
        out->Line = hit.Line;
        out->Tag = hit.Tag;
        out->Inside = hit.Inside;
        out->InBlock = hit.InBlock;
        out->Row = hit.Row;
        out->Column = hit.Column;
        return 1;
    });
}

int ImTypeset_SelectDocumentRange(ImTypeset_Document* document, const ImTypeset_Vec2* anchor,
                                  const ImTypeset_Vec2* active, ImTypeset_Granularity granularity,
                                  ImTypeset_DocumentRange* out)
{
    return guarded(0, [&]() -> int {
        if (missing(anchor, "ImTypeset_SelectDocumentRange: anchor is null") ||
            missing(active, "ImTypeset_SelectDocumentRange: active is null") ||
            missing(out, "ImTypeset_SelectDocumentRange: out is null"))
            return 0;
        *out = mirror<ImTypeset_DocumentRange>(ImTypeset::SelectDocumentRange(
            document, mirror<ImVec2>(*anchor), mirror<ImVec2>(*active), granularity));
        return 1;
    });
}

int ImTypeset_GetDocumentRangeRects(ImTypeset_Document* document,
                                    const ImTypeset_DocumentRange* range, ImTypeset_Vec4* out_rects,
                                    int rects_capacity)
{
    return guarded(-1, [&]() -> int {
        if (missing(range, "ImTypeset_GetDocumentRangeRects: range is null"))
            return 0;
        ImVector<ImVec4>& rects = scratch().rects;
        rects.resize(0);
        ImTypeset::GetDocumentRangeRects(document, mirror<ImTypesetDocumentRange>(*range), &rects);
        copy_elements(out_rects, rects_capacity, rects.Data, rects.Size, sizeof(ImVec4));
        return rects.Size;
    });
}

int ImTypeset_GetDocumentCaretRect(ImTypeset_Document* document, const ImTypeset_Position* position,
                                   ImTypeset_Vec4* out)
{
    return guarded(0, [&]() -> int {
        if (missing(position, "ImTypeset_GetDocumentCaretRect: position is null") ||
            missing(out, "ImTypeset_GetDocumentCaretRect: out is null"))
            return 0;
        *out = mirror<ImTypeset_Vec4>(
            ImTypeset::GetDocumentCaretRect(document, mirror<ImTypesetPosition>(*position)));
        return 1;
    });
}

int ImTypeset_AppendDocumentRangeText(ImTypeset_Document* document,
                                      const ImTypeset_DocumentRange* range, const char* separator,
                                      const char* separator_end, char* buf, int buf_size)
{
    return guarded(-1, [&]() -> int {
        if (buf != nullptr && buf_size > 0)
            buf[0] = '\0';
        if (missing(range, "ImTypeset_AppendDocumentRangeText: range is null"))
            return 0;
        ImGuiTextBuffer text;
        ImTypeset::AppendDocumentRangeText(
            document, mirror<ImTypesetDocumentRange>(*range), &text,
            separator != nullptr ? terminated(name_scratch(), separator, separator_end) : nullptr);
        return copy_string(text.begin(), text.size(), buf, buf_size);
    });
}

int ImTypeset_GetDocumentBlockGeometry(ImTypeset_Document* document, int block,
                                       ImTypeset_BlockGeometry* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentBlockGeometry: out is null"))
            return 0;
        std::memset(out, 0, sizeof(*out));
        const ImTypesetBlockGeometry g = ImTypeset::GetDocumentBlockGeometry(document, block);
        out->Min = mirror<ImTypeset_Vec2>(g.Min);
        out->Max = mirror<ImTypeset_Vec2>(g.Max);
        out->FullMin = mirror<ImTypeset_Vec2>(g.FullMin);
        out->FullMax = mirror<ImTypeset_Vec2>(g.FullMax);
        out->LineCount = g.LineCount;
        out->LaidOut = g.LaidOut;
        return 1;
    });
}

int ImTypeset_GetDocumentBlockLine(ImTypeset_Document* document, int block, int line,
                                   ImTypeset_Line* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentBlockLine: out is null"))
            return 0;
        *out = mirror<ImTypeset_Line>(ImTypeset::GetDocumentBlockLine(document, block, line));
        return 1;
    });
}

int ImTypeset_GetDocumentTableGeometry(ImTypeset_Document* document, int block,
                                       ImTypeset_TableGeometry* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentTableGeometry: out is null"))
            return 0;
        // Field by field into zeroed memory: the struct has padding.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetTableGeometry g = ImTypeset::GetDocumentTableGeometry(document, block);
        out->RowCount = g.RowCount;
        out->ColumnCount = g.ColumnCount;
        out->CellCount = g.CellCount;
        out->Min = mirror<ImTypeset_Vec2>(g.Min);
        out->Max = mirror<ImTypeset_Vec2>(g.Max);
        out->Stated = g.Stated;
        out->LaidOut = g.LaidOut;
        return 1;
    });
}

int ImTypeset_GetDocumentTableColumn(ImTypeset_Document* document, int block, int column,
                                     ImTypeset_TableColumnGeometry* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentTableColumn: out is null"))
            return 0;
        *out = mirror<ImTypeset_TableColumnGeometry>(
            ImTypeset::GetDocumentTableColumn(document, block, column));
        return 1;
    });
}

int ImTypeset_GetDocumentTableRow(ImTypeset_Document* document, int block, int row,
                                  ImTypeset_TableRowGeometry* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentTableRow: out is null"))
            return 0;
        // Field by field into zeroed memory: the struct has padding.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetTableRowGeometry g = ImTypeset::GetDocumentTableRow(document, block, row);
        out->Min = g.Min;
        out->Max = g.Max;
        out->CellStart = g.CellStart;
        out->CellCount = g.CellCount;
        out->Wrapped = g.Wrapped;
        return 1;
    });
}

int ImTypeset_SetDocumentColumns(ImTypeset_Document* document, const ImTypeset_Columns* columns)
{
    return guarded(0, [&]() -> int {
        if (columns == nullptr)
            return status(ImTypeset::SetDocumentColumns(document, nullptr));
        const ImTypesetColumns given = mirror<ImTypesetColumns>(*columns);
        return status(ImTypeset::SetDocumentColumns(document, &given));
    });
}

int ImTypeset_GetDocumentColumnCount(ImTypeset_Document* document)
{
    return guarded(-1, [&]() -> int { return ImTypeset::GetDocumentColumnCount(document); });
}

int ImTypeset_GetDocumentColumn(ImTypeset_Document* document, int column,
                                ImTypeset_ColumnGeometry* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentColumn: out is null"))
            return 0;
        // Field by field into zeroed memory: the struct has padding.
        std::memset(out, 0, sizeof(*out));
        const ImTypesetColumnGeometry g = ImTypeset::GetDocumentColumn(document, column);
        out->Min = mirror<ImTypeset_Vec2>(g.Min);
        out->Max = mirror<ImTypeset_Vec2>(g.Max);
        out->ContentMin = mirror<ImTypeset_Vec2>(g.ContentMin);
        out->ContentMax = mirror<ImTypeset_Vec2>(g.ContentMax);
        out->Start = mirror<ImTypeset_Position>(g.Start);
        out->End = mirror<ImTypeset_Position>(g.End);
        out->UncutTop = g.UncutTop;
        out->Overfull = g.Overfull;
        out->Exists = g.Exists;
        return 1;
    });
}

int ImTypeset_GetDocumentColumnPos(ImTypeset_Document* document, const ImTypeset_Vec2* uncut_pos,
                                   ImTypeset_Vec2* out)
{
    return guarded(0, [&]() -> int {
        if (missing(uncut_pos, "ImTypeset_GetDocumentColumnPos: uncut_pos is null") ||
            missing(out, "ImTypeset_GetDocumentColumnPos: out is null"))
            return 0;
        *out = mirror<ImTypeset_Vec2>(
            ImTypeset::GetDocumentColumnPos(document, mirror<ImVec2>(*uncut_pos)));
        return 1;
    });
}

int ImTypeset_GetDocumentTableCell(const ImTypeset_Document* document,
                                   const ImTypeset_Position* position, ImTypeset_CellPosition* out)
{
    return guarded(0, [&]() -> int {
        if (missing(position, "ImTypeset_GetDocumentTableCell: position is null") ||
            missing(out, "ImTypeset_GetDocumentTableCell: out is null"))
            return 0;
        *out = mirror<ImTypeset_CellPosition>(
            ImTypeset::GetDocumentTableCell(document, mirror<ImTypesetPosition>(*position)));
        return 1;
    });
}

int ImTypeset_GetDocumentTablePosition(const ImTypeset_Document* document, int block, int row,
                                       int column, int offset, ImTypeset_Position* out)
{
    return guarded(0, [&]() -> int {
        if (missing(out, "ImTypeset_GetDocumentTablePosition: out is null"))
            return 0;
        *out = mirror<ImTypeset_Position>(
            ImTypeset::GetDocumentTablePosition(document, block, row, column, offset));
        return 1;
    });
}

int ImTypeset_DocumentView(const char* str_id, const char* str_id_end, ImTypeset_Document* document,
                           const ImTypeset_Vec2* size, ImTypeset_DocumentRange* selection,
                           const ImTypeset_DocumentDraw* draw, const ImTypeset_Style* style)
{
    return guarded(0, [&]() -> int {
        const OptionalStyle s(style);
        Optional<ImTypesetDocumentRange, ImTypeset_DocumentRange> range(selection);
        const bool ok = ImTypeset::DocumentView(
            terminated(name_scratch(), str_id, str_id_end), document,
            size != nullptr ? mirror<ImVec2>(*size) : ImVec2(0.0f, 0.0f), range.ptr,
            same<ImTypesetDocumentDraw, ImTypeset_DocumentDraw>(draw), s.ptr);
        if (selection != nullptr)
            *selection = mirror<ImTypeset_DocumentRange>(range.value);
        return status(ok);
    });
}

int ImTypeset_ShowDemoWindow(int* p_open)
{
    return guarded(0, [&]() -> int {
        bool open = p_open == nullptr || *p_open != 0;
        ImTypeset::ShowDemoWindow(p_open != nullptr ? &open : nullptr);
        if (p_open != nullptr)
            *p_open = open ? 1 : 0;
        return 1;
    });
}

int ImTypeset_ShowDemoSection(ImTypeset_DemoSection section)
{
    return guarded(0, [&]() -> int {
        ImTypeset::ShowDemoSection(section);
        return 1;
    });
}

const char* ImTypeset_GetDemoSectionName(ImTypeset_DemoSection section)
{
    return ImTypeset::GetDemoSectionName(section);
}

} // extern "C"
