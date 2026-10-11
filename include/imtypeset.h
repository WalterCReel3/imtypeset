// imtypeset: typeset text for Dear ImGui.
//
// ImGui draws text; this typesets it. The model is TeX's: boxes, glue, kerns
// and penalties, broken into lines by total fit and shipped out as glyph
// positions, drawn through ImGui's own font atlas on whole pixels.
//
//     ImGui::CreateContext();
//     IMTYPESET_CHECKVERSION();
//     ImTypeset::CreateContext();
//     ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, "Roboto-Regular.ttf");
//     ...
//     ImGui::Begin("Window");
//     ImTypeset::TextUnformatted("A paragraph, justified to the window.");
//     ImGui::End();
//     ...
//     ImTypeset::DestroyContext();
//     ImGui::DestroyContext();
//
// This header is the whole public surface. It includes imgui.h and nothing
// else, declares no template and no std:: type, and stays narrow: language
// bindings are generated from it, so every declaration is a promise to each.
//
// FOUR WAYS IN, from least to most control:
//   - TextUnformatted() / Text(): one call per paragraph per frame, like
//     ImGui::TextWrapped(). Layout is cached in the context.
//   - An ImTypesetParagraph: text and style runs in, laid out for a width once,
//     drawn into any ImDrawList each frame, hit-tested and copied from.
//   - DrawParagraphToBuffers(): the same paragraph as vertices and indices, for
//     a host that has no ImDrawList to draw into.
//   - An ImTypesetDocument: many blocks the host describes once (paragraphs,
//     headings, list items, code, quotes, rules, rectangles for its own
//     widgets), laid out only where they are looked at, drawn, hit-tested and
//     copied from across blocks, edited a block at a time. What a pane that
//     shows a transcript or a rendered markdown file holds. See "Documents".
//
// UNITS. Every length here is in pixels, as ImGui's are. Layout itself is done
// in TeX's scaled points; ImTypesetStyle::PixelsPerInch is the bridge.
//
// POSITIONS IN TEXT are byte offsets into the UTF-8 text the host gave,
// everywhere: style runs, hit tests, ranges, lines, formulas, error places. A
// range is [Start, End). An offset keeps its meaning when the paragraph is
// laid out again at another width, so a selection survives a reflow. A binding
// whose strings are indexed by codepoint converts with the four functions at
// the end of this header.
//
// ERRORS. No argument of any function here is trusted: a NaN or absurd length
// is clamped, malformed UTF-8 is set as U+FFFD, a font that cannot be used is
// refused. Nothing reachable from data asserts. ONE MODEL:
//   - Every function that can fail returns false (or null). What it returns on
//     success is said where it is declared.
//   - GetLastError() then says why, and goes on saying so until the next call
//     of a function that reports there, which sets it again or, succeeding,
//     clears it. Those functions are marked [E] below. So GetLastError() read
//     straight after a call is that call's outcome, as ImGui::IsItemHovered()
//     is the last item's.
//   - What is wrong with a paragraph's text or layout also stays with the
//     paragraph: GetParagraphError(). It is what its last SetParagraphText and
//     LayoutParagraph found, and is clear when they found nothing.
//   - "False" does not always mean "nothing happened". Text cut at the length
//     limit, a formula shown as its source, a run set in the paragraph's face
//     because its own has no font, are all still set and drawn. The code says
//     which: see ImTypesetError_.
//
// FONTS. A face is added once, whatever sizes it is then set at: ImGui 1.92's
// atlas bakes sizes on demand. That needs a renderer backend that announces
// ImGuiBackendFlags_RendererHasTextures; without it every drawing call is
// refused with ImTypesetError_BackendUnsupported and the widgets fall back to
// ImGui's own wrapped text. Type is baked at the size it is set at: an em of
// several hundred pixels can ask more of the atlas than ImGui's largest
// texture holds, which ImGui reports by asserting ("Out of texture memory").
//
// MATH. Off unless asked for, text by text: a dollar is a dollar. A host that
// wants formulas adds a font for ImTypesetFace_Math and passes
// ImTypesetTextFlags_Math with the text whose dollars are meant as math:
//
//     ImTypeset::AddFontFromFileTTF(ImTypesetFace_Math, "latinmodern-math.otf");
//     ImTypeset::TextUnformatted("The roots are $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$.",
//                                NULL, ImTypesetTextFlags_Math);
//     ImTypeset::TextUnformatted("$$ \\sum_{k=0}^{n} k = \\frac{n(n+1)}{2} $$", NULL,
//                                ImTypesetTextFlags_Math);
//
// A formula is set IN the paragraph: it is a box on the baseline among the
// words, in the math face, at the size of the text around it; the line breaker
// moves it whole; it is drawn from the same font atlas in the same draw call;
// it is one thing to select and copies as its source, dollars included. See
// ImTypesetTextFlags_Math for which dollars open a formula and what happens to
// one that cannot be set.
//
// THREADS. One thread at a time for the whole library, as for ImGui with its
// default context pointer: the current context and GetLastError() are plain
// globals of the process. Layout reads ImGui's state and the context's
// scratch buffers, so a paragraph cannot be laid out on a worker thread while
// the UI thread uses the same context.
//
// COMPATIBILITY, for a host and for a binding generator:
//   - Enum values are written out and only ever appended to.
//   - A struct the host fills (ImTypesetStyle, ImTypesetRun, ImTypesetTextRange,
//     and for a document ImTypesetBlock, ImTypesetBlockStyle,
//     ImTypesetContainer, ImTypesetDocumentContent, ImTypesetDocumentOptions,
//     ImTypesetMark, ImTypesetDocumentDraw)
//     is plain data whose all-zero state is the default: zeroed memory from C
//     or a binding, a default-constructed one in C++. New fields are appended
//     at the end, in a minor version, and zero keeps meaning "as before".
//   - A struct the library fills (ImTypesetLine, ImTypesetHit,
//     ImTypesetFormula, ImTypesetStats, ImTypesetErrorInfo) grows the same
//     way. Those a call answers with are returned by value, never through an
//     output pointer: a generated binding returns them as they are.
//   - IMTYPESET_CHECKVERSION() compares all of their sizes, and sizeof(ImWchar),
//     ImDrawVert and ImDrawIdx, between the caller and the library.
//   - Handles are raw pointers. One used after DestroyParagraph() or
//     DestroyDocument(), or after its context was destroyed, is a use after
//     free, as with any pointer.
//   - DestroyContext() comes BEFORE ImGui::DestroyContext(): see CreateContext().

#pragma once

#include "imgui.h"

#define IMTYPESET_VERSION     "0.2.0"
#define IMTYPESET_VERSION_NUM 200 // XYYZZ: 0.02.00

// The ImGui release this is built and tested against, and the oldest it is
// written for: 1.92.0 introduced the dynamic font atlas (ImFontBaked, sizes
// baked on demand, textures updated by the backend) that fonts and drawing
// here rely on. Only imgui.h is used; nothing sits on imgui_internal.h.
#define IMTYPESET_IMGUI_VERSION_NUM     19290
#define IMTYPESET_IMGUI_VERSION_NUM_MIN 19200

#if !defined(IMTYPESET_DISABLE_IMGUI_VERSION_CHECK) && \
    IMGUI_VERSION_NUM < IMTYPESET_IMGUI_VERSION_NUM_MIN
#error "imtypeset needs Dear ImGui 1.92.0 or later (the dynamic font atlas)."
#endif

#ifndef IMTYPESET_API
#define IMTYPESET_API IMGUI_API
#endif

// Call once, anywhere before CreateContext(), from the code that includes this
// header, as IMGUI_CHECKVERSION() is called. False, with GetLastError() saying
// which, if the header and the library are different versions or were
// compiled against ImGui builds that disagree (IMGUI_USE_WCHAR32, ImDrawIdx,
// ImDrawVert). After a failed check CreateContext() refuses, so a host that
// does not look at the result still does not run on a mismatched layout.
#define IMTYPESET_CHECKVERSION()                                                              \
    (ImTypeset::DebugCheckVersionAndDataLayout(                                               \
         IMTYPESET_VERSION, sizeof(ImWchar), sizeof(ImDrawVert), sizeof(ImDrawIdx),           \
         sizeof(ImTypesetStyle), sizeof(ImTypesetRun), sizeof(ImTypesetTextRange),            \
         sizeof(ImTypesetLine), sizeof(ImTypesetHit), sizeof(ImTypesetFormula),               \
         sizeof(ImTypesetStats), sizeof(ImTypesetErrorInfo)) &&                               \
     ImTypeset::DebugCheckDocumentDataLayout(                                                 \
         sizeof(ImTypesetBlock), sizeof(ImTypesetBlockStyle), sizeof(ImTypesetContainer),     \
         sizeof(ImTypesetDocumentContent), sizeof(ImTypesetDocumentOptions),                  \
         sizeof(ImTypesetPosition), sizeof(ImTypesetDocumentRange), sizeof(ImTypesetMark),    \
         sizeof(ImTypesetDocumentDraw), sizeof(ImTypesetDocumentHit),                         \
         sizeof(ImTypesetBlockGeometry), sizeof(ImTypesetDocumentStats)) &&                   \
     ImTypeset::DebugCheckTableDataLayout(                                                    \
         sizeof(ImTypesetCell), sizeof(ImTypesetTableColumn), sizeof(ImTypesetTableGeometry), \
         sizeof(ImTypesetTableColumnGeometry), sizeof(ImTypesetTableRowGeometry),             \
         sizeof(ImTypesetCellPosition)) &&                                                    \
     ImTypeset::DebugCheckColumnDataLayout(sizeof(ImTypesetColumns),                          \
                                           sizeof(ImTypesetColumnGeometry)))

struct ImTypesetContext;   // Everything the library holds. Opaque.
struct ImTypesetParagraph; // One paragraph: text, its layout for a width, what was shipped. Opaque.
struct ImTypesetDocument; // Many blocks stacked: what a host describes, laid out where it is looked
                          // at. Opaque.

typedef int ImTypesetFace;              // -> enum ImTypesetFace_
typedef int ImTypesetAlign;             // -> enum ImTypesetAlign_
typedef int ImTypesetSpacing;           // -> enum ImTypesetSpacing_
typedef int ImTypesetLineBreak;         // -> enum ImTypesetLineBreak_
typedef int ImTypesetGranularity;       // -> enum ImTypesetGranularity_
typedef int ImTypesetError;             // -> enum ImTypesetError_
typedef int ImTypesetTextFlags;         // -> enum ImTypesetTextFlags_
typedef int ImTypesetRunFlags;          // -> enum ImTypesetRunFlags_
typedef int ImTypesetStatusFlags;       // -> enum ImTypesetStatusFlags_
typedef int ImTypesetBlockKind;         // -> enum ImTypesetBlockKind_
typedef int ImTypesetBlockFlags;        // -> enum ImTypesetBlockFlags_
typedef int ImTypesetContainerKind;     // -> enum ImTypesetContainerKind_
typedef int ImTypesetContainerFlags;    // -> enum ImTypesetContainerFlags_
typedef int ImTypesetCodeOverflow;      // -> enum ImTypesetCodeOverflow_
typedef int ImTypesetMarkPlace;         // -> enum ImTypesetMarkPlace_
typedef int ImTypesetDocumentDrawFlags; // -> enum ImTypesetDocumentDrawFlags_
typedef int ImTypesetDemoSection;       // -> enum ImTypesetDemoSection_
typedef int ImTypesetCellFlags;         // -> enum ImTypesetCellFlags_
typedef int ImTypesetColumnFlags;       // -> enum ImTypesetColumnFlags_

// A typographic role. A host says which file plays each; text names roles.
enum ImTypesetFace_ {
    // In an ImTypesetStyle: Regular. In an ImTypesetRun: the paragraph's own.
    ImTypesetFace_Default = 0,
    ImTypesetFace_Regular = 1,
    ImTypesetFace_Bold = 2,
    ImTypesetFace_Italic = 3,
    ImTypesetFace_BoldItalic = 4,
    ImTypesetFace_Code = 5,
    ImTypesetFace_CodeBold = 6, // Read from the Bold font until one is added for it.
    ImTypesetFace_Math = 7,     // Must carry an OpenType MATH table.
    ImTypesetFace_COUNT
};

enum ImTypesetAlign_ {
    ImTypesetAlign_Justify = 0, // Both margins flush.
    ImTypesetAlign_Left = 1,    // Ragged right.
    ImTypesetAlign_Center = 2,
    ImTypesetAlign_Right = 3 // Ragged left.
};

// What happens to the space after the end of a sentence.
enum ImTypesetSpacing_ {
    ImTypesetSpacing_Sentence = 0, // A little wider than a word space.
    ImTypesetSpacing_French = 1,   // Every space alike.
    ImTypesetSpacing_TeX = 2       // TeX's \nonfrenchspacing: wider, and stretching more.
};

// How lines are chosen.
enum ImTypesetLineBreak_ {
    // Total fit for justified text, first fit for every other alignment.
    ImTypesetLineBreak_Auto = 0,
    // Greedily, line by line, whatever the alignment.
    ImTypesetLineBreak_FirstFit = 1,
    // The whole paragraph at once, whatever the alignment. For ragged and
    // centred text every line that fits costs the optimiser the same, so it
    // keeps them all and its work grows with the square of the length; past a
    // work budget (some tens of milliseconds) the paragraph is broken by first
    // fit instead and says ImTypesetStatusFlags_FirstFitFallback.
    ImTypesetLineBreak_TotalFit = 2
};

enum ImTypesetGranularity_ {
    ImTypesetGranularity_Character = 0, // A click and drag.
    ImTypesetGranularity_Word = 1,      // A double click.
    ImTypesetGranularity_Line = 2       // A triple click: the visual line.
};

// "Nothing done" codes leave the call without effect. "Done, differently"
// codes are reported the same way (false, and here) but the text is still set
// and can be drawn.
enum ImTypesetError_ {
    ImTypesetError_None = 0,
    // Nothing done:
    ImTypesetError_NoContext = 1,       // No current ImTypeset context, or the ImGui context it was
                                        // created in is not the current one (or is gone).
    ImTypesetError_VersionMismatch = 2, // The header and the library, or the two ImGui builds,
                                        // disagree.
    ImTypesetError_InvalidArgument = 3,
    ImTypesetError_FileNotFound = 4,
    ImTypesetError_FontRefused = 5,      // The font cannot be used; the message says what is wrong
                                         // with it.
    ImTypesetError_TooManyVertices = 10, // DrawParagraphToBuffers: more vertices than ImDrawIdx
                                         // can index.
    ImTypesetError_BackendUnsupported = 11, // The renderer backend does not announce
                                            // ImGuiBackendFlags_RendererHasTextures.
    // Done, differently:
    ImTypesetError_NoFont = 6,      // Text names a face nothing has been added for. A run is set in
                                    // the paragraph's face; a paragraph whose own face has no font
                                    // is not set at all (the widgets draw it with ImGui's text).
    ImTypesetError_TextTooLong = 7, // Text was cut at the limit (16 MiB per paragraph).
    ImTypesetError_MathRefused = 8, // A formula is outside the supported subset and is shown as
                                    // its source. Positions say where.
    ImTypesetError_LayoutFailed = 9, // Lines were produced; something in them measures as
                                     // nothing.
    // Something in what was described was wrong and was cut to what is right:
    // a run out of order (a paragraph's or a block's); in a document, an
    // offset outside the text, a style, a container, runs, cells or columns
    // that are not there, a kind that is not one, a marker past its limit, a
    // cell out of order, a table past its limits. The text and the blocks
    // are set.
    ImTypesetError_DescriptionCut = 12
};

enum ImTypesetTextFlags_ {
    ImTypesetTextFlags_None = 0,
    // A newline ends the line. Without it a newline is white space like any
    // other and the paragraph is one stream of words. KNOWN GAP: several
    // newlines in a row end the line ONCE; a blank line a host wants kept is
    // its own paragraph or block.
    ImTypesetTextFlags_HardBreaks = 1 << 0,
    // Dollar signs in this text delimit formulas. This flag is the whole
    // switch: with it and no font added for ImTypesetFace_Math, every formula
    // is refused, with that reason.
    //
    // WHICH DOLLARS OPEN A FORMULA. `$...$` is a formula in text style and
    // `$$...$$` one in display style, when all of these hold, and otherwise
    // the dollars are text:
    //   - the opening marker is not followed by white space and not preceded
    //     by a letter or a digit ("US$5", "a$b$");
    //   - the closing marker is the FIRST later one not preceded by an odd
    //     number of backslashes. If that one does not qualify the opener opens
    //     nothing: the search does not go on to a later dollar ("$5 and $10"
    //     is text);
    //   - the closing marker is not preceded by white space and not followed
    //     by a letter, a digit, `{` or `(` ("$PATH=$HOME", "${A}${B}", "$n$th").
    // `$$` is tried before `$`. A formula may run across a newline.
    //
    // A LITERAL DOLLAR is written `\$`: outside a formula a backslash takes
    // the character after it out of consideration, and one before a dollar is
    // not set and is not copied. No other backslash is touched ("\\$x$" is two
    // backslashes and a formula). Inside a formula `\$` is the formula's own.
    //
    // NOTHING ELSE IS LOOKED AT. There is no markdown here: a formula between
    // backquotes or asterisks is a formula, and ImTypesetRun does not hide a
    // dollar. A host that has parsed markup itself escapes the dollars it
    // means literally, or leaves the flag off for that text. A formula is set
    // at the size, and in the colour, of the run it starts in.
    //
    // A DISPLAY FORMULA ON ITS OWN needs no second flag: it is a text that is
    // nothing but a `$$` block, either one line `$$ ... $$`, or a line that is
    // `$$`, the source, and a line that is `$$`. It is centred and takes no
    // first-line indent, whatever the style's Align says, and overhangs
    // rather than breaks if it is wider than the width. It copies as
    // `$$source$$` with each run of white space as one space. A block whose
    // closing line is missing is shown as its source and reported as refused
    // ("the display block has not been closed yet").
    //
    // A FORMULA THAT CANNOT BE SET is never guessed at. Its source is set
    // where it stands, dollars included, in ImTypesetFace_Code (the
    // paragraph's face if nothing was added for Code), unbroken; the call
    // that laid the text out answers false with ImTypesetError_MathRefused,
    // the reason and the place of the first such formula, and
    // GetParagraphFormula() answers for each. What is supported: scripts and
    // primes, \frac and \binom, big operators and their limits, \sqrt,
    // \left ... \right, \overline and \underline, \text and the \mathrm,
    // \mathbf, \mathbb, \mathcal letters, spaces, Greek and the common
    // symbols. Accents, matrices, alignment and macros are refused, and so
    // is a formula whose source is longer than 524,288 bytes.
    ImTypesetTextFlags_Math = 1 << 2
};

enum ImTypesetRunFlags_ {
    ImTypesetRunFlags_None = 0,
    ImTypesetRunFlags_NoBreak = 1 << 0, // No line break inside the run: an inline code span.
    // The run's text is the source of ONE formula, without delimiters, set in
    // text style (Formula) or display style (DisplayFormula) where it stands:
    // what a host that parsed the markup itself says instead of leaving dollars
    // to be found. It needs no ImTypesetTextFlags_Math, is refused and reported
    // exactly as a dollar formula is, and copies as the run's text.
    ImTypesetRunFlags_Formula = 1 << 1,
    ImTypesetRunFlags_DisplayFormula = 1 << 2
};

// What a paragraph's last layout did that is worth knowing and is not an error.
enum ImTypesetStatusFlags_ {
    ImTypesetStatusFlags_None = 0,
    // LayoutParagraph has run since the text was last set: sizes, lines and
    // hit tests mean something.
    ImTypesetStatusFlags_LaidOut = 1 << 0,
    // Total fit was asked for (ImTypesetLineBreak_) and would have cost more
    // than its work budget: the lines were chosen by first fit, exactly as
    // ImTypesetLineBreak_FirstFit chooses them.
    ImTypesetStatusFlags_FirstFitFallback = 1 << 1,
    // The text is one display formula: centred, not indented.
    ImTypesetStatusFlags_DisplayMath = 1 << 2
};

// How a paragraph is set. GetStyle() is the context's, used wherever a
// function is not handed another. Zero everywhere is the default style.
//
// TWO SIZES, because there are two things called a font size. FontSize is
// ImGui's: the height of a line, so that text set here matches the UI around
// it, and zero follows ImGui::GetFontSize() through PushFont() and DPI
// scaling. EmSize is a typographer's and a design specification's: "16 px
// type". Set one; EmSize wins.
struct ImTypesetStyle {
    ImTypesetFace Face; // The face of text no run restyles. Default: Regular.
    // The font size as ImGui means one: the height of a line of the Regular
    // face, ascent plus descent. 0 uses ImGui::GetFontSize(), so text matches
    // the UI around it.
    float FontSize;
    // If above zero, the em size instead, which is what a typographer means by
    // "16 px type", and FontSize is not read.
    float EmSize;
    float LineHeight;      // Baseline to baseline, in ems. 0 is 1.35.
    ImTypesetAlign Align;  // Justify.
    float LeftIndent;      // Every line, from the left edge.
    float FirstLineIndent; // The first line only, in addition. Applied once, as a box, as TeX's
                           // \parindent is.
    ImTypesetSpacing Spacing;
    ImTypesetLineBreak LineBreak;
    // What turns pixels into points (72.27 to the inch). 0 is 96, one CSS
    // pixel per pixel; a host drawing at twice that scale says 192, and the
    // line-breaking tolerances, which are in points, keep their meaning.
    float PixelsPerInch;
    // Ink. 0 uses ImGuiCol_Text. Either way it is multiplied by ImGui's
    // style.Alpha when drawn, so ImGui::BeginDisabled() dims it.
    ImU32 Color;
    ImU32 SelectionColor; // Behind selected text. 0 uses ImGuiCol_TextSelectedBg.

    ImTypesetStyle()
        : Face(0)
        , FontSize(0.0f)
        , EmSize(0.0f)
        , LineHeight(0.0f)
        , Align(0)
        , LeftIndent(0.0f)
        , FirstLineIndent(0.0f)
        , Spacing(0)
        , LineBreak(0)
        , PixelsPerInch(0.0f)
        , Color(0)
        , SelectionColor(0)
    {
    }
};

// A stretch of a paragraph's text set differently from the rest. Start and End
// are byte offsets into the text given with it, End exclusive. Runs are given
// in ascending order and do not overlap; one that does is cut where the one
// before it ends, and one of which that leaves nothing (it starts before that
// end and does not reach past it) is left out and said:
// ImTypesetError_DescriptionCut. A character belongs to the run its first
// byte is in. Zero everywhere but Start and End changes nothing.
struct ImTypesetRun {
    int Start;
    int End;
    ImTypesetFace Face; // Default keeps the paragraph's.
    float Scale;        // Size, as a multiple of the paragraph's. 0 is 1.
    ImU32 Color;        // 0 keeps the paragraph's ink.
    ImTypesetRunFlags Flags;
    // Whatever the host wants back when asked what is at a place: a link's
    // number, say. Not read by layout. 0 is no tag. Answered by
    // HitTestParagraph() and HitTestDocument().
    // A tag is a field of a run like its face, and runs do not overlap: a link
    // that runs over plain and bold text is two runs, each with the link's tag
    // and its own face. The host cuts them; nothing here merges two runs.
    int Tag;

    ImTypesetRun()
        : Start(0)
        , End(0)
        , Face(0)
        , Scale(0.0f)
        , Color(0)
        , Flags(0)
        , Tag(0)
    {
    }
};

// A stretch of a paragraph's text, [Start, End), in bytes. Empty when
// Start >= End. What it covers: a character whose first byte is in it, and a
// formula whose FIRST BYTE is in it (its opening dollar; for a run flagged
// ImTypesetRunFlags_Formula, the first byte of the run). Nothing is ever half
// selected: a formula is one thing, taken whole or not at all, so a range
// that starts inside a formula does not have it and one that only reaches
// into it has all of it. The same holds for every range here: a selection,
// its rectangles, a copy, an ImTypesetMark.
struct ImTypesetTextRange {
    int Start;
    int End;
};

// One line of a paragraph as it was last laid out. Coordinates are relative
// to the paragraph's top-left corner and are layout's own: drawing then snaps
// each baseline and each word to whole pixels, so ink sits within a pixel of
// them.
struct ImTypesetLine {
    // The line's box: horizontally from where its content starts to where
    // it ends (not the full width of a ragged line), vertically its own
    // height above the baseline and depth below it. A line holding a tall
    // formula is taller than its neighbours.
    ImVec2 Min;
    ImVec2 Max;
    float Baseline; // y of the baseline.
    // The text on the line, in bytes: from its first character to the first
    // character of the next line, so that the lines of a paragraph tile its
    // text. A space the line was broken at belongs to the line it ended.
    int TextStart;
    int TextEnd;
};

// Where a point falls in a paragraph.
struct ImTypesetHit {
    // The caret position nearest the point: the byte offset of the character
    // it would sit before, or the text's length after the last.
    int TextOffset;
    // The line the point belongs to, for GetParagraphLine(); -1 in a
    // paragraph with no lines. Where a line was broken the same TextOffset
    // is the end of one line and the start of the next: this says which.
    int Line;
    // Whether the point is on a line's content, as against a margin, the
    // ragged end of a line, or the space above and below: what tells a click
    // on a word from a click beside it.
    bool Inside;
    // ImTypesetRun::Tag of the run the character under the point is in; 0
    // if the point is on no run, or not Inside.
    int Tag;
};

struct ImTypesetErrorInfo {
    ImTypesetError Code;
    // Where in the text, for an error that has a place: a byte offset and the
    // same place counted in codepoints. -1 when it has none.
    int BytePos;
    int CodepointPos;
    const char* Message; // Never null; "" when Code is None. Valid until the next call into the
                         // library.
};

// One formula of a paragraph laid out with ImTypesetTextFlags_Math.
struct ImTypesetFormula {
    // Where it is in the text given to SetParagraphText, in bytes, End
    // exclusive, delimiters included.
    int TextStart;
    int TextEnd;
    bool Display; // Between `$$`: set in display style.
    // It could not be set, and its source is set in its place. The rest says
    // why and where.
    bool Refused;
    // Where in the text the reason applies: a byte offset, and the same place
    // counted in codepoints. -1 when the formula was set.
    int BytePos;
    int CodepointPos;
    // One sentence for a person; "" when the formula was set. Never null.
    // Valid until the paragraph's text is set or it is laid out again. Where
    // it quotes the source, a NUL byte is spelled `\x00`, here and in
    // ImTypesetErrorInfo::Message.
    const char* Reason;
};

// A DEBUG FACILITY: counters for a host to watch and a test to assert on, not
// an interface to build behaviour on. The names say what the implementation
// does today and may change with it. All but the last two count since the
// context was made. A frame in which nothing changed adds to DrawCalls,
// CulledCalls, VerticesEmitted and BytesHashed only.
struct ImTypesetStats {
    ImU64 TextBuilds;      // Times text was turned into typesetting material.
    ImU64 Layouts;         // Times a paragraph was broken into lines.
    ImU64 DeferredLayouts; // Times a widget out of view kept its old lines at a new width.
    ImU64 DrawCalls;       // Paragraphs drawn.
    ImU64 CulledCalls;     // Widget calls that drew nothing: their rectangle was clipped.
    ImU64 VerticesEmitted;
    ImU64 BytesHashed;      // Text bytes the widgets read to find or to check their layout.
    ImU64 CacheEvictions;   // Widget layouts dropped to keep the cache within its limits.
    ImU64 CachedParagraphs; // Widget layouts held now.
    ImU64 CachedBytes;      // An estimate of the memory they hold now.
};

// --- Documents: what a host hands over ------------------------------------------------
//
// A document is an ordered list of blocks the HOST describes: the library never
// parses markup. See "Documents" below for the calls.

// What a block is. It decides which fields of ImTypesetBlock are read and
// nothing else: how a heading looks is in its ImTypesetBlockStyle.
enum ImTypesetBlockKind_ {
    ImTypesetBlockKind_Paragraph = 0, // Text, broken into lines.
    ImTypesetBlockKind_Heading = 1,   // Text; Level says which.
    ImTypesetBlockKind_ListItem = 2,  // Text, usually with a marker.
    // Text set verbatim in ImTypesetFace_Code: one line per source line, white
    // space kept, tabs to the next multiple of four columns, no runs, no
    // formulas. Blank lines at the end are dropped (a line of spaces is not
    // blank): see POSITIONS under "Documents". See ImTypesetCodeOverflow_.
    ImTypesetBlockKind_Code = 3,
    ImTypesetBlockKind_Rule = 4, // A horizontal rule across the column, Extent thick.
    // A rectangle Extent tall that the library leaves empty, for a widget of
    // the host's: GetDocumentBlockGeometry() says where it is. Its text is
    // what a copy that covers it gives.
    ImTypesetBlockKind_Host = 5,
    // Rows of cells in columns: see "Tables" below ImTypesetBlock.
    ImTypesetBlockKind_Table = 6
};

enum ImTypesetBlockFlags_ {
    ImTypesetBlockFlags_None = 0,
    ImTypesetBlockFlags_HardBreaks = 1 << 0, // As ImTypesetTextFlags_HardBreaks.
    ImTypesetBlockFlags_Math = 1 << 2,       // As ImTypesetTextFlags_Math: dollars are looked for.
    // Every line of the block is exactly its line height (the style's
    // LineHeight, in ems of its size), whatever it holds: line n's top is n
    // line heights below the block's top, to the rounding of one length. For
    // rows found by arithmetic, and for two documents set side by side that
    // must stay in step: a diff, a gutter of line numbers. Without it a line
    // is AT LEAST its line height, and one holding something taller (a
    // character its face lacks, set from another face; a formula) is as tall
    // as that and moves every line below it. With it such a line keeps its
    // place and its ink reaches into its neighbours': what TeX sets with a
    // strut and \lineskiplimit at its lowest. The line's box, which is what
    // a selection fills and ImTypesetLine reports, is the line height. A
    // draw looks for such a block's ink four ems past the block's box, and
    // for a line's as far past the line's box as two ems of its largest
    // type and the tallest formula in the block reach: ink further out than
    // that is not drawn while its block or its line is outside the clip
    // rectangle. For text, code and table blocks. In a table it is per LINE:
    // a row set on one line is one line height, and a row whose cells
    // wrapped is as many as its tallest cell has lines, so the rows tile.
    // Not read for the others.
    ImTypesetBlockFlags_FixedLineHeight = 1 << 3,
    // Table: its first row is a header, and a rule is drawn under it
    // (ImTypesetDocumentOptions::TableRuleThickness). How a header's cells
    // look is in their runs, like any text's.
    ImTypesetBlockFlags_TableHeader = 1 << 4,
    // Table: its columns are HELD while it grows. See "A TABLE THAT GROWS".
    ImTypesetBlockFlags_TableHeld = 1 << 5
};

enum ImTypesetCellFlags_ {
    ImTypesetCellFlags_None = 0,
    // The cell is the first of a new row. The first cell of a table starts
    // its first row whether it says so or not.
    ImTypesetCellFlags_RowStart = 1 << 0
};

enum ImTypesetContainerKind_ {
    ImTypesetContainerKind_Quote = 0, // A bar down the left of the blocks in it.
    ImTypesetContainerKind_List = 1
};

enum ImTypesetContainerFlags_ {
    ImTypesetContainerFlags_None = 0,
    // List: its items follow one another as lines do, with no space between.
    ImTypesetContainerFlags_Tight = 1 << 0
};

// What becomes of a code line longer than the column.
enum ImTypesetCodeOverflow_ {
    ImTypesetCodeOverflow_Wrap = 0, // It continues on further lines, indented by two ems.
    ImTypesetCodeOverflow_Clip =
        1, // It is cut at the column's edge when drawn; a copy has all of it.
    ImTypesetCodeOverflow_Grow = 2 // It runs on, and the document is as wide as its longest.
};

// Where a mark is drawn. See ImTypesetMark.
enum ImTypesetMarkPlace_ {
    ImTypesetMarkPlace_Ink = 0,    // The characters themselves, in the mark's colour.
    ImTypesetMarkPlace_Behind = 1, // A filled rectangle behind each line of the range.
    ImTypesetMarkPlace_Under = 2   // A bar under the baseline of each line of the range.
};

enum ImTypesetDocumentDrawFlags_ {
    ImTypesetDocumentDrawFlags_None = 0,
    // Line boxes, baselines, and glue coloured by whether it was stretched or
    // squeezed, over the ink.
    ImTypesetDocumentDrawFlags_Debug = 1 << 0
};

// How blocks of one sort are set. A host gives a table of these with its
// blocks and each block names one. Everything a block's look depends on is
// here or on the block: nothing is implied by its kind. Zero everywhere is
// the document's style (the ImTypesetStyle given to LayoutDocument) with no
// space above or below, justified.
struct ImTypesetBlockStyle {
    ImTypesetFace Face;    // Default: the document style's. Code blocks: Default is Code.
    float Scale;           // The size, as a multiple of the document style's. 0 is 1.
    float LineHeight;      // Baseline to baseline, in ems of this size. 0 is the document style's.
    ImTypesetAlign Align;  // Justify.
    float FirstLineIndent; // Pixels.
    float SpaceAbove;      // Pixels this block asks for above itself, and below. The gap between
    float SpaceBelow;      // two blocks is the upper one's below plus the lower one's above,
                           // except in a tight list. Neither applies at the document's edge.
    ImU32 Color;           // Ink. 0 is the document style's.
    ImTypesetFace MarkerFace; // Default: Face.
    ImU32 MarkerColor;        // 0 is Color.
    float MarkerGap;          // Pixels between a marker and the text. 0 is half an em.

    ImTypesetBlockStyle()
        : Face(0)
        , Scale(0.0f)
        , LineHeight(0.0f)
        , Align(0)
        , FirstLineIndent(0.0f)
        , SpaceAbove(0.0f)
        , SpaceBelow(0.0f)
        , Color(0)
        , MarkerFace(0)
        , MarkerColor(0)
        , MarkerGap(0.0f)
    {
    }
};

// Something blocks are in: a quote or a list. Optional. A block that names
// none can still say how deeply it is quoted (ImTypesetBlock::QuoteDepth), and
// then two quotes in a row are one bar, a bar stands one QuoteIndent left of
// its blocks, a marker wider than its gutter runs into the text and every
// list is loose. With containers each of those is exact.
struct ImTypesetContainer {
    ImTypesetContainerKind Kind;
    // The container this one is inside: 1 + its index, which must be lower
    // than this one's. 0 is none; anything else that is not one is none, and
    // said (ImTypesetError_DescriptionCut). So is a Kind that is not a kind:
    // the container is then a quote.
    int Parent;
    // Quote: where its bar stands, in pixels from the document's left edge
    // (the indent in force where the quote opens). List: the gutter it gives
    // its markers, which is how much it adds to the indent of what is in it.
    float Indent;
    ImTypesetContainerFlags Flags;
    ImU32 Color; // Quote: its bar. 0 is ImTypesetDocumentOptions::QuoteBarColor.

    ImTypesetContainer()
        : Kind(0)
        , Parent(0)
        , Indent(0.0f)
        , Flags(0)
        , Color(0)
    {
    }
};

// One block. Offsets are bytes of the ONE text buffer handed over with the
// blocks (ImTypesetDocumentContent::Text); a block's own text is
// [TextStart, TextEnd) of it, and positions reported for the block are byte
// offsets into that stretch, from 0.
struct ImTypesetBlock {
    ImTypesetBlockKind Kind;
    ImTypesetBlockFlags Flags;
    int Level; // Heading: 1 to 6. Not read by layout; kept for the host.
    int Style; // Index into ImTypesetDocumentContent::Styles. Ignored when there are none.
    int TextStart;
    int TextEnd;
    // This block's runs: RunCount entries of ImTypesetDocumentContent::Runs
    // from RunStart. Their Start and End are bytes of the BLOCK's text.
    int RunStart;
    int RunCount;
    // The list marker, as bytes of the same buffer ("1.", a bullet). Set
    // right-aligned in the gutter left of the first line, MarkerGap from the
    // text. Empty is none. Not part of the block's text: never selected. At
    // most 256 bytes of it are set; a longer one is cut before the character
    // that crosses that, and said (ImTypesetError_DescriptionCut).
    int MarkerStart;
    int MarkerEnd;
    // Pixels from the document's left edge to the block's left edge: what
    // encloses the block plus its own. Absolute, so that nothing about
    // nesting has to be guessed.
    float LeftIndent;
    int QuoteDepth; // How many quotes enclose the block, for one that names no container.
    int Container;  // The innermost container the block is in: 1 + its index. 0 is none.
    int Item;       // Which item of its list the block belongs to: blocks of one list with the
                    // same number are one item (an item of two paragraphs).
    float Extent;   // Rule: its thickness in pixels, 0 is one. Host: its height in pixels.
    // Table: its cells, CellCount entries of ImTypesetDocumentContent::Cells
    // from CellStart, row after row; and its columns, ColumnCount entries of
    // ImTypesetDocumentContent::Columns from ColumnStart. Zero for any other
    // kind.
    int CellStart;
    int CellCount;
    int ColumnStart;
    int ColumnCount;

    ImTypesetBlock()
        : Kind(0)
        , Flags(0)
        , Level(0)
        , Style(0)
        , TextStart(0)
        , TextEnd(0)
        , RunStart(0)
        , RunCount(0)
        , MarkerStart(0)
        , MarkerEnd(0)
        , LeftIndent(0.0f)
        , QuoteDepth(0)
        , Container(0)
        , Item(0)
        , Extent(0.0f)
        , CellStart(0)
        , CellCount(0)
        , ColumnStart(0)
        , ColumnCount(0)
    {
    }
};

// --- Tables ---------------------------------------------------------------------------
//
// A table is ONE BLOCK (ImTypesetBlockKind_Table). Its text is the block's
// text, like any block's, and its cells are stretches of that text, in
// reading order: the cells of the first row left to right, then the second
// row's. A cell is text and runs exactly as a paragraph is: faces, sizes,
// colours, tags for links, formulas (the block's ImTypesetBlockFlags_Math, or
// runs flagged ImTypesetRunFlags_Formula). The block's runs lie each inside
// one cell; one that crosses a cell's edge is cut there. White space inside a
// cell is one space and white space at its ends is dropped, as in a
// paragraph. A newline in a cell is white space: a cell has no hard breaks.
//
// WRITE THE TEXT AS A COPY OF THE TABLE READS: the cells of a row with a tab
// between them and the rows with a newline between them ("Name\tUnit\nmass\tkg").
// Nothing requires it — the bytes between two cells belong to no cell and are
// never set — but then the block's text is what a copy of the whole table
// gives, a position at the end of one cell is not also the start of the
// next, and a search over the text cannot match across two cells.
//
// POSITIONS in a table are what they are everywhere: (block, byte offset into
// the block's text). The cell a position is in, and the position of a byte
// of a cell, are answered by GetDocumentTableCell() and
// GetDocumentTablePosition(); a hit test says the row and the column
// (ImTypesetDocumentHit). A position between two cells is the start of the
// later one. Selections, marks, carets and copies take these positions; a
// range that runs from one cell into another covers everything between them
// in reading order, whatever the mouse crossed.
//
// A POINT IN A CELL'S RECTANGLE IS THAT CELL'S: its column's edges, its
// row's top and bottom. The blank a cell's alignment leaves, the padding and
// the room under a short cell in a wrapped row all answer the cell they are
// in, at the nearest caret position of its text: its end for the blank after
// left-aligned text, its start for the blank before right-aligned text. A
// point in a column the row has no cell for is the row's last cell's, and a
// point between two rows (where the rule under a header is) the nearer row's.
// The caret at a position (GetDocumentCaretRect) is inside that cell, one
// line tall, and a point on it answers the same position again. The
// rectangles of a range (GetDocumentRangeRects, a drawn selection, a mark)
// are one a cell and line, inside the cell's text: the blank between two
// cells is not highlighted.
// Text written WITHOUT anything between two cells is the one case with an
// exception: the byte the two share is the later cell's start, so the
// earlier cell's last caret position is before its last character, and an
// empty cell that shares its byte with the next has no position of its own
// (a point in it says its Row and Column, and its Position names the next).
//
// WHAT A COPY GIVES for a table: each cell as a paragraph gives its text, a
// tab ("\t") between the cells of a row and a newline ("\n") between rows,
// for every cell from the one the range starts in to the one it ends in (a
// range that ends where a cell's text starts has none of that cell, unless
// the cell is empty). An empty cell keeps its tab wherever it stands, and a
// row gives the cells it has: a copy of a whole table written as advised above is its text, byte
// for byte. These two separators are fixed; the block separator of AppendDocumentRangeText() stands
// between the table and the blocks around it as between any two.
//
// LAYOUT is TeX's alignment as far as that goes: every cell is set unbroken,
// a column wants the widest of its cells, and every cell is then boxed to its
// column's width with glue where its alignment leaves room. What makes a
// table fit a column of the page is not TeX's:
//   - a table whose columns want less than the width is scaled UP to fill it,
//     every column in proportion;
//   - one that wants more is squeezed: each column keeps at least its longest
//     unbreakable piece, and what is left is shared by how much each could
//     give up. A cell wider than its column is then broken into lines, ragged
//     on the side its alignment leaves free, and its row is as tall as its
//     tallest cell;
//   - if even the longest pieces do not fit, the table is wider than the
//     document (as a word too long for its line is);
//   - or the host states the widths (ImTypesetTableColumn).
// A row may have fewer cells than the table has columns: the rest are empty.
//
// A TABLE THAT GROWS. Cells are appended to the last block with
// AppendDocumentTableCells(), and text to its last cell with
// AppendDocumentText(). Rows already set are not set again unless a column's
// width changed. Under the rules above every new row can move every column,
// since the table is refitted to the width each time. With
// ImTypesetBlockFlags_TableHeld the columns are held instead: each is the
// widest its cells have wanted at any layout since the block was described
// (never narrower as rows arrive, and no wider than they need), and the
// table is as wide as its columns, not the document. The widths are lengths
// of the type they were measured in: a change of the block's size or face,
// of the fonts or of the cell padding starts them afresh. A column moves only
// when a cell wider than every one before it arrives in it. If the held
// widths stop fitting the width, the table is fitted by the rules above
// until they fit again: while it is, every column can move. A row that
// arrives and moves no column costs that row; no pass is made over the rows
// that stand. The widths held survive AppendDocumentTableCells(),
// AppendDocumentText(), and ReplaceDocumentBlocks() of the one table by one
// table with the flag and as many columns (a host that parses its source
// again as it arrives); they are given up when the flag is. So a host that
// wants the table settled when it is complete replaces it once more without
// the flag, and one that wants it never to jump leaves the flag on.
//
// LIMITS. A table has at most 1,024 columns and 262,144 cells; what is past
// either is left out and said (ImTypesetError_DescriptionCut). A table is laid
// out whole the first time any of it is looked at: it costs what its cells
// cost, once, and nothing while it is out of view.

// One cell of a table: [TextStart, TextEnd) of the BLOCK's text. Cells are
// given in ascending order and do not overlap; one that does is cut where the
// one before it ends, and said (ImTypesetError_DescriptionCut).
struct ImTypesetCell {
    int TextStart;
    int TextEnd;
    ImTypesetCellFlags Flags;

    ImTypesetCell()
        : TextStart(0)
        , TextEnd(0)
        , Flags(0)
    {
    }
};

// One column of a table. Zero everywhere is a left-aligned column whose width
// the library chooses. A table has as many columns as its longest row has
// cells: columns described past that are not read, and ones not described
// are as zero.
struct ImTypesetTableColumn {
    // Where a cell's text stands in the column: Left, Center or Right.
    // ImTypesetAlign_Justify, the zero, is Left: a cell is never justified.
    ImTypesetAlign Align;
    // The column's width in pixels, cell padding included, if above zero.
    // Read only when EVERY column of the table states one: the table is then
    // exactly those widths, whatever the document's width.
    float Width;
    // Otherwise, if above zero in every column: the column's share of the
    // width, in any unit (1, 2, 1 is a quarter, a half, a quarter).
    float Share;

    ImTypesetTableColumn()
        : Align(0)
        , Width(0.0f)
        , Share(0.0f)
    {
    }
};

// Everything one edit hands over, as views: nothing is kept, all of it is
// copied. A null array with a zero count is an empty one.
struct ImTypesetDocumentContent {
    const char* Text; // UTF-8; need not be zero-terminated.
    int TextSize;     // Bytes.
    const ImTypesetBlock* Blocks;
    int BlockCount;
    const ImTypesetRun* Runs;
    int RunCount;
    const ImTypesetBlockStyle* Styles;
    int StyleCount;
    const ImTypesetContainer* Containers;
    int ContainerCount;
    const ImTypesetCell* Cells; // Of every table among the blocks.
    int CellCount;
    const ImTypesetTableColumn* Columns;
    int ColumnCount;

    ImTypesetDocumentContent()
        : Text(NULL)
        , TextSize(0)
        , Blocks(NULL)
        , BlockCount(0)
        , Runs(NULL)
        , RunCount(0)
        , Styles(NULL)
        , StyleCount(0)
        , Containers(NULL)
        , ContainerCount(0)
        , Cells(NULL)
        , CellCount(0)
        , Columns(NULL)
        , ColumnCount(0)
    {
    }
};

// What holds for a whole document. Zero everywhere is the default.
struct ImTypesetDocumentOptions {
    float QuoteIndent;       // Pixels a quote indents what is in it; where a bar stands for a
                             // block that names no container. 0 is one em.
    float QuoteBarThickness; // Pixels. 0 is an eighth of an em, at least one pixel.
    ImU32 QuoteBarColor;     // 0 is ImGuiCol_Separator.
    ImU32 RuleColor;         // A Rule block's. 0 is ImGuiCol_Separator.
    ImTypesetCodeOverflow CodeOverflow;
    // Tables. The space left and right of every cell's text, in pixels: 0 is
    // half an em, below zero is none. The thickness of the rule under a
    // header row: 0 is six hundredths of an em, below zero is no rule. The
    // rule's colour: 0 is ImGuiCol_Separator.
    float TableCellPadding;
    float TableRuleThickness;
    ImU32 TableRuleColor;

    ImTypesetDocumentOptions()
        : QuoteIndent(0.0f)
        , QuoteBarThickness(0.0f)
        , QuoteBarColor(0)
        , RuleColor(0)
        , CodeOverflow(0)
        , TableCellPadding(0.0f)
        , TableRuleThickness(0.0f)
        , TableRuleColor(0)
    {
    }
};

// A place in a document: a block, and a byte offset into the text the host
// gave for that block. It keeps its meaning when the document is laid out at
// another width, when other blocks are laid out or edited, and when blocks
// are appended after it; inserting or removing blocks before it moves Block
// by the difference, which the host knows.
struct ImTypesetPosition {
    int Block;
    int Offset;
};

// [Start, End) in document order. Empty unless Start is before End.
struct ImTypesetDocumentRange {
    ImTypesetPosition Start;
    ImTypesetPosition End;
};

// Something drawn over a range of a document that is no part of its layout
// and can change every frame for nothing: a search hit, a link's colour and
// underline, a syntax token. Marks are drawn in the order given, a later one
// over an earlier one; two Ink marks over one character leave it the later
// one's colour. A formula is marked whole or not at all, by whether the range
// holds its first byte (see ImTypesetTextRange): a mark over the middle of a
// formula's source draws nothing, in any Place. A mark over a rule or a host
// block draws nothing. A mark in a table marks what it covers of each cell.
struct ImTypesetMark {
    ImTypesetDocumentRange Range;
    ImU32 Color;
    ImTypesetMarkPlace Place;
    // Under: the bar's thickness in pixels (0 is a sixteenth of the line's
    // em, at least one pixel) and how far its top is below the baseline
    // (0 is twice that).
    float Thickness;
    float Offset;

    ImTypesetMark()
        : Color(0)
        , Place(0)
        , Thickness(0.0f)
        , Offset(0.0f)
    {
        Range.Start.Block = Range.Start.Offset = Range.End.Block = Range.End.Offset = 0;
    }
};

// What is drawn with a document beside its text.
struct ImTypesetDocumentDraw {
    // Highlighted behind the text in the document style's SelectionColor.
    ImTypesetDocumentRange Selection;
    // Every mark costs a comparison each draw, in view or not: for tens of
    // thousands, hand over the ones near the view.
    const ImTypesetMark* Marks;
    int MarkCount;
    ImTypesetDocumentDrawFlags Flags;

    ImTypesetDocumentDraw()
        : Marks(NULL)
        , MarkCount(0)
        , Flags(0)
    {
        Selection.Start.Block = Selection.Start.Offset = 0;
        Selection.End.Block = Selection.End.Offset = 0;
    }
};

// Where a point falls in a document.
struct ImTypesetDocumentHit {
    // The caret position nearest the point. In a block without text (a rule,
    // a host block) Offset is 0 above its middle and its text's length below.
    ImTypesetPosition Position;
    int Line;     // The line of that block, for GetDocumentBlockLine(); -1 if it has none.
    int Tag;      // ImTypesetRun::Tag of the run the point is on; 0 if on none.
    bool Inside;  // The point is on a line's content, not beside or between.
    bool InBlock; // The point is within the block's rectangle, top to bottom: false in the
                  // space between two blocks and outside the document.
    // In a table: the row and the column of the cell whose rectangle holds
    // the point, which is the cell Position is in (see A POINT IN A CELL'S
    // RECTANGLE for the one exception). -1 in any other block.
    int Row;
    int Column;
};

// Where a block is, relative to the document's top-left corner, in pixels.
struct ImTypesetBlockGeometry {
    // What it occupies: from its left indent to the end of its longest line
    // (the column's right edge for text that is not ragged right), and from
    // its first line's top to its last line's bottom.
    ImVec2 Min;
    ImVec2 Max;
    // The same across the whole column: what a background or a host widget fills.
    ImVec2 FullMin;
    ImVec2 FullMax;
    int LineCount;
    // False if the block has not been laid out at the document's width: the
    // rest is then an estimate, exact only in FullMin.x and FullMax.x.
    bool LaidOut;
};

// A table as it was last laid out, relative to the document's top-left corner.
struct ImTypesetTableGeometry {
    int RowCount;    // The header, if there is one, is row 0.
    int ColumnCount; // The longest row's cells.
    int CellCount;
    // From the table's left edge to the right edge of its last column, and
    // from its first row's top to its last row's bottom.
    ImVec2 Min;
    ImVec2 Max;
    // Whether the columns are the widths held (ImTypesetBlockFlags_TableHeld)
    // or stated (ImTypesetTableColumn::Width), and not fitted to the width.
    bool Stated;
    bool LaidOut; // As ImTypesetBlockGeometry::LaidOut.
};

// One column of a table.
struct ImTypesetTableColumnGeometry {
    float Min; // Its left and right edges, x from the document's left edge.
    float Max; // Padding is inside them.
    // What the column's cells want, whatever the document's width: the widest
    // of them set on one line, and the widest piece of any of them that
    // cannot be broken, each with the padding. What a drag handle is held
    // between.
    float Natural;
    float Floor;
};

// One row of a table.
struct ImTypesetTableRowGeometry {
    float Min;     // Its top and bottom, y from the document's top. Rows do not
    float Max;     // tile the table: the rule under a header lies between two.
    int CellStart; // Its cells: CellCount of the table's, from CellStart.
    int CellCount;
    bool Wrapped; // A cell of it was broken into lines.
};

// The cell a position of a table is in.
struct ImTypesetCellPosition {
    int Row; // -1, with the rest zero, if the position is in no table.
    int Column;
    int Cell;   // The cell's number among the table's cells, in the order given.
    int Offset; // Bytes from the start of the cell's text.
};

// --- Columns --------------------------------------------------------------------------
//
// A document can be SHOWN in columns: the one column it is laid out in is cut
// at good places into pieces that stand side by side and are read down one
// and then down the next. Columns are a property of how a document is shown
// and not of what it is. Nothing of the description changes; LayoutDocument()
// still says how wide ONE column is; and every ImTypesetPosition, range and
// mark means what it meant, so one document can be shown uncut in one frame
// and in columns in the next, at any width, and a selection survives it.
//
// WHERE A COLUMN IS CUT is decided as TeX decides where to end a page (its
// \vsplit): between two lines of a paragraph or of a code block, between two
// rows of a table, and between two blocks; never inside a line, a table row
// (a row whose cells wrapped is whole in its column or not in it), a quote
// or a host block; not after a heading, which stays with what follows it, an
// empty paragraph between them or not; and not between a table's header row,
// the rule under it and the row after. The space between two blocks is
// dropped where a cut falls in it. A column's last line may hang below the
// column's foot by its depth (a descender) and no more.
// What cannot be cut and is taller than a column is put in a column alone and
// reaches past its foot: ImTypesetColumnGeometry::Overfull says so, and
// GetDocumentSize() is then as tall as what that column holds, so that a view
// which scrolls over the page reaches all of it; the host decides whether to
// clip it, scroll to it or give the columns more height.
//
// TWO WAYS OF FILLING.
//
//   IN ORDER   Height above zero, and no ImTypesetColumnFlags_Balance. Every
//              column is Height tall and takes as much as fits; there are as
//              many columns as the document needs, one after another to the
//              right. LAZY, like the uncut document, from the front: a column
//              is cut, and the blocks in it laid out, when a draw, a hit test
//              or a question first reaches it. The first columns of a
//              ten-thousand-block document cost what they show. Column k costs
//              every column before it, once, because where it starts is where
//              they ended: a jump to the end lays everything out. After an
//              edit the columns before the edit stand and the ones from it on
//              are cut again when next reached.
//   BALANCED   ImTypesetColumnFlags_Balance and a Count of two or more. Count
//              columns of one height, the least at which that many hold the
//              document; a block that cannot be cut and is taller than that
//              makes its column, and so the page, as tall as it is, and no
//              column is ever Overfull: nothing reaches past the page. NOT LAZY:
//              the height depends on every block, so all of them are laid out
//              the first time anything is asked, and after any edit the height
//              is searched for again (no block is laid out again for that:
//              ImTypesetDocumentStats::BalanceHeightsTried counts the passes
//              over the document's lines). For a document that streams, or a
//              long one, fill in order.
//
// Anything else is no columns: the document is shown uncut.
//
// THE PAGE is what the columns make side by side. Column k's top-left corner
// is at x = k * (column width + Gutter), y = 0 from the corner DrawDocument()
// is given. In a document shown in columns these speak of the page: the
// points DrawDocument, HitTestDocument and SelectDocumentRange take, the
// rectangles GetDocumentRangeRects and GetDocumentCaretRect answer, and
// GetDocumentSize. These speak of the UNCUT column, as if there were no
// columns: GetDocumentBlockGeometry, GetDocumentBlockLine, the three table
// queries and LayoutDocumentWindow; GetDocumentColumnPos() takes a point from
// there to the page.
//
// READING ORDER is the document's order, down each column and then to the
// next. A drag from a point in one column to a point in a later one selects
// from the first point to the foot of its column, every column between, and
// the later column from its head to the second point; a point below a
// column's last line is taken as a point ON that line, at the same x (the
// caret position of that line nearest it across), one above its first line
// as a point on the first, and a point in a gutter belongs to the column on
// its left. A copy of such a range is the text in the order it
// is read, exactly as from the uncut document: a cut adds nothing to it.
enum ImTypesetColumnFlags_ {
    ImTypesetColumnFlags_None = 0,
    // Count columns of one height, the least that holds the document: see
    // "Columns", BALANCED.
    ImTypesetColumnFlags_Balance = 1 << 0,
};

// How a document is shown in columns. Zero everywhere is no columns.
struct ImTypesetColumns {
    // BALANCED: how many columns. IN ORDER: not read by the layout, which
    // makes as many as it takes; DocumentView() reads it as how many columns
    // to show across its width. Less than 1 is 1.
    int Count;
    // IN ORDER: how tall a column is, in pixels. BALANCED: not read.
    // DocumentView() takes its own height for a Height of 0 when Count is two
    // or more.
    float Height;
    // The space between two columns, in pixels. 0 is one line of the
    // document's style (its EmSize times its LineHeight); below zero is none.
    float Gutter;
    ImTypesetColumnFlags Flags;

    ImTypesetColumns()
        : Count(0)
        , Height(0.0f)
        , Gutter(0.0f)
        , Flags(0)
    {
    }
};

// One column of a document shown in columns, on the page, in pixels.
struct ImTypesetColumnGeometry {
    // The column's own rectangle: as wide as the document's column, as tall
    // as the columns are.
    ImVec2 Min;
    ImVec2 Max;
    // From the top of its first line to the foot of its last, descenders
    // included: what it holds. ContentMax.y is past Max.y when the column is
    // Overfull, and can be by the depth of a line when it is not.
    ImVec2 ContentMin;
    ImVec2 ContentMax;
    // The first position set in it, and the first set in the next column (the
    // document's end for the last): the column holds [Start, End).
    ImTypesetPosition Start;
    ImTypesetPosition End;
    // Where its first line's top is in the uncut column. For everything in
    // this column, y on the page is y there less UncutTop plus ContentMin.y,
    // and x on the page is x there plus Min.x.
    float UncutTop;
    // It holds something taller than a column that cannot be cut, and what
    // it holds reaches past its foot. IN ORDER only: BALANCED columns are as
    // tall as the tallest of them needs.
    bool Overfull;
    // False if there is no such column (the rest is then zero), or under IN
    // ORDER if it is past what GetDocumentColumnCount() counts.
    bool Exists;
};

// A DEBUG FACILITY, like ImTypesetStats, for one document. The first five
// count since the document was made, and so do the two about tables.
struct ImTypesetDocumentStats {
    ImU64 MaterialBuilds; // Blocks whose text was turned into typesetting material.
    ImU64 BlocksLaidOut;  // Blocks broken into lines.
    ImU64 UnitsStacked;   // Quotes stacked from their blocks.
    // Blocks gone over by passes over the whole document, in which no line is
    // broken: every block, once after each edit that adds, removes or
    // re-describes blocks, and once after a change of style.
    ImU64 StructureVisits;
    ImU64 BlocksDrawn;
    int Blocks;               // How many there are,
    int BlocksDeferred;       // and how many of them are not laid out at the present width
                              // (counted when asked: one step a block).
    ImU64 TableCellsMeasured; // Cells whose text was made into material and measured.
    ImU64 TableRowsSet;       // Rows boxed to their columns' widths.
    // Columns (see "Columns"), since the document was made: columns whose cut
    // was found, each one scan down the lines it holds; and, when balancing,
    // heights tried, each one pass over every line of the document.
    ImU64 ColumnsCut;
    ImU64 BalanceHeightsTried;
};

// The sections of the demo window. The values are stable: a new section is
// added before COUNT and none is renumbered.
enum ImTypesetDemoSection_ {
    ImTypesetDemoSection_Paragraph = 0,
    ImTypesetDemoSection_Faces = 1,    // "Faces and sizes"
    ImTypesetDemoSection_Specimen = 2, // Every face at one size, one face up a scale, kerning.
    ImTypesetDemoSection_Math = 3,     // Formulas in text: the subset, refusals, a stream.
    ImTypesetDemoSection_Formulas = 4, // "Formula gallery": plates, each for one rule of math mode.
    ImTypesetDemoSection_Selection = 5,
    ImTypesetDemoSection_Inspector = 6,
    ImTypesetDemoSection_Stress = 7,
    ImTypesetDemoSection_Fonts = 8,
    ImTypesetDemoSection_KnownLimits = 9,
    ImTypesetDemoSection_Document = 10,
    ImTypesetDemoSection_COUNT
};

namespace ImTypeset
{

// --- Context -------------------------------------------------------------------------

// What IMTYPESET_CHECKVERSION() calls. [E]
IMTYPESET_API bool DebugCheckVersionAndDataLayout(const char* version, size_t sz_imwchar,
                                                  size_t sz_imdrawvert, size_t sz_imdrawidx,
                                                  size_t sz_style, size_t sz_run, size_t sz_range,
                                                  size_t sz_line, size_t sz_hit, size_t sz_formula,
                                                  size_t sz_stats, size_t sz_error);
// The third part of IMTYPESET_CHECKVERSION(): the structs of a table. [E]
IMTYPESET_API bool DebugCheckTableDataLayout(size_t sz_cell, size_t sz_column, size_t sz_table,
                                             size_t sz_column_geometry, size_t sz_row_geometry,
                                             size_t sz_cell_position);
// The fourth part of IMTYPESET_CHECKVERSION(): the structs of columns. [E]
IMTYPESET_API bool DebugCheckColumnDataLayout(size_t sz_columns, size_t sz_column_geometry);
// The second part of IMTYPESET_CHECKVERSION(): the structs of a document. [E]
IMTYPESET_API bool DebugCheckDocumentDataLayout(size_t sz_block, size_t sz_block_style,
                                                size_t sz_container, size_t sz_content,
                                                size_t sz_options, size_t sz_position,
                                                size_t sz_range, size_t sz_mark, size_t sz_draw,
                                                size_t sz_hit, size_t sz_geometry, size_t sz_stats);

// Makes a context bound to the current ImGui context: its fonts go into that
// ImGui context's atlas and it draws only while that ImGui context is current.
// As ImGui::CreateContext() does, makes the new context current only if none
// is. Null if there is no ImGui context, if the library was compiled against
// another ImGui than it is linked with, or after a failed
// IMTYPESET_CHECKVERSION(). [E]
//
// LIFETIME. Destroy it before the ImGui context it was made in. If that ImGui
// context goes first, this one is left holding fonts of an atlas that is gone:
//   - with no ImGui context current, or another one, everything that draws,
//     adds or removes a font, or is a widget answers false with
//     ImTypesetError_NoContext, and GetFont() answers null; layout, hit tests
//     and copies still answer, since they need no ImGui;
//   - DestroyContext() is still safe, and is the one thing left to do with it;
//   - an ImGui context made LATER AT THE SAME ADDRESS is taken for the one
//     that was destroyed (an address is all imgui.h gives to know one by, and
//     allocators hand the same block out again), and this context goes on in
//     it. Its fonts are not there: each font added here carries a mark (see
//     AddFontFromFileTTF) and a role's font is used, handed out or removed
//     only while the atlas holds it with that mark, so every role is found
//     empty by the next call that looks, whatever fonts the host has put in
//     the new atlas, and none of those is touched. Text then reports
//     ImTypesetError_NoFont until the roles are given fonts again, after
//     which paragraphs and documents kept from before are laid out and drawn
//     as before. It is the outcome of a host emptying the atlas.
// So whether a context left behind answers NoContext or NoFont is the
// allocator's choice. A host that makes ImGui contexts in turn does not rely
// on either: it calls DestroyContext() before each ImGui::DestroyContext(),
// or, where it cannot know that one happened, DestroyContext() and
// CreateContext() before it adds its fonts.
IMTYPESET_API ImTypesetContext* CreateContext();
// Null destroys the current one. Its paragraphs and documents die with it. The
// fonts it added stay in the ImGui atlas, which owns them. Touches nothing of
// ImGui's: it may be called with no ImGui context, or with the one it was
// made in already destroyed.
IMTYPESET_API void DestroyContext(ImTypesetContext* ctx = NULL);
IMTYPESET_API ImTypesetContext* GetCurrentContext();
IMTYPESET_API void SetCurrentContext(ImTypesetContext* ctx);
// A number no other context of this process has had or will have; never
// zero. Zero with no current context. A pointer cannot tell a context from
// one made later at its address; code that keeps handles and does not own
// the context (ShowDemoWindow() is such code) keeps this beside them.
IMTYPESET_API ImU64 GetContextSerial();

// IMTYPESET_VERSION as compiled into the library.
IMTYPESET_API const char* GetVersion();
// IMGUI_VERSION_NUM as the library saw it when it was compiled.
IMTYPESET_API int GetImGuiVersionNum();

// The outcome of the most recent call of a function marked [E]: Code is None
// if it succeeded. Never null, and valid with no context at all, which is how
// a failed CreateContext() is asked about. See ERRORS at the top.
IMTYPESET_API const ImTypesetErrorInfo* GetLastError();
// A copy; all zero with no context. See ImTypesetStats.
IMTYPESET_API ImTypesetStats GetStats();
// The current context's style. With no context, a scratch style that nothing
// reads.
IMTYPESET_API ImTypesetStyle& GetStyle();

// How much the widgets may keep between frames: so many laid-out texts and so
// many bytes (an estimate), whichever is reached first. Over a limit, texts
// that were not submitted in the current frame or the one before are dropped,
// a few at each call, the longest held first. What those two frames submitted
// is never dropped, so a frame that shows more than the limit still lays each
// text out once and not once a frame. Zero or less leaves a limit as it is.
// The defaults are 2048 texts and 64 MiB.
IMTYPESET_API void SetTextCacheLimits(int max_paragraphs, int max_bytes);

// --- Fonts ---------------------------------------------------------------------------
//
// Gives a role its font: reads its metrics for layout and adds it to the ImGui
// atlas for drawing. Returns the ImFont, which the atlas owns, or null with
// nothing changed. Adding one makes every layout stale and it is redone. [E]
//
//   - `filename` is UTF-8, on Windows too.
//   - `face_index` picks a face of a collection (.ttc); 0 for a single font.
//   - `font_cfg` is a template, as for ImFontAtlas::AddFont*. FontNo is
//     ignored. MergeMode is refused: the returned font's codepoints are laid
//     out for this library and must not be shared. There is no fallback chain
//     a host can set. A character its face lacks is set from the first of
//     the Regular, Math and Code faces that has it, at the same size, and
//     failing those as U+FFFD; either way it copies as what was written. A
//     host that wants another fallback names the face in a run.
//   - DO NOT MERGE ANOTHER FONT INTO ONE RETURNED HERE when ImWchar is 16 bits
//     (the default ImGui build). Glyphs with codepoints above U+FFFF are then
//     drawn through stand-ins in U+E000..U+F8FF, which is also where icon
//     fonts live; the two would shadow each other silently.
//   - A role that already has a font is given the new one and the old ImFont
//     is removed from the atlas. Do not keep the old pointer.
//   - ImTypesetFace_Default is not a role: it is refused.
//   - THE LAST 8 BYTES OF THE FONT'S ImFontConfig::Name ARE THE LIBRARY'S: a
//     mark, past the terminator of the name, by which a font added here is
//     told from any other in the atlas. The name ImGui shows is the first 31
//     bytes of `font_cfg->Name`, or of the file's name when that is empty.
//     Do not write over the mark; a font without it is no longer the role's.
//
// ONE COPY OF A FACE, NOT TWO. The font added here is a copy of the file made
// for typesetting (its character map rewritten so that every codepoint layout
// can name has a glyph), so a font the host added from the same file cannot
// be adopted: the two are different ImFonts. The sharing goes the other way.
// The ImFont returned here is an ordinary one, and a host that wants its own
// widgets in the same face draws them with it (ImGui::PushFont(font, size))
// instead of adding the file a second time. For text the file encodes it
// draws what a copy the host added would draw, with two differences a host
// can see: ImFontConfig::OversampleH is 1 unless `font_cfg` says otherwise,
// and under FreeType a font that asks to be sized by its OS/2 typographic
// metrics is sized by its hhea table, as layout measures it.
IMTYPESET_API ImFont* AddFontFromFileTTF(ImTypesetFace face, const char* filename,
                                         int face_index = 0, const ImFontConfig* font_cfg = NULL);
// The bytes are copied.
IMTYPESET_API ImFont* AddFontFromMemoryTTF(ImTypesetFace face, const void* font_data,
                                           int font_data_size, int face_index = 0,
                                           const ImFontConfig* font_cfg = NULL);
// Takes a role's font away and removes its ImFont from the atlas. False if the
// role had none. Outside a frame, like any change to the atlas. A host that
// removes the ImFont itself (ImFontAtlas::RemoveFont, ImFontAtlas::Clear) need
// not call this: the role is found empty by the next call that looks, whatever
// the host has put in the atlas since (as many fonts as it held, one at the
// same address), and text that names it reports ImTypesetError_NoFont. A host
// that empties the atlas to load its fonts again adds the roles' fonts again
// too. [E]
IMTYPESET_API bool RemoveFont(ImTypesetFace face);
// The ImFont drawing a role, or null. CodeBold answers Bold's until it has
// its own, and Default answers Regular's. Null whenever the ImGui context the
// current context was made in is not the current one: the font is its atlas's.
IMTYPESET_API ImFont* GetFont(ImTypesetFace face);
// Whether the font added for a role has a glyph for a codepoint: the role's
// own font, not the faces a character it lacks is then set from (see
// AddFontFromFileTTF). What a host asks before it hands text over when a
// borrowed glyph would not do: a grid of code in which every character must
// be one cell wide. CodeBold answers for Bold's font until it has its own and
// Default for Regular's, as GetFont does. False for a role with no font, for
// a face that is not one, and with no context. Needs no ImGui context.
// The answer is the font file's, for the codepoint a text would hold, and is
// the same in a build whose ImWchar is 16 bits. For the Math role it is also
// true of the private-use codepoints of plane 15 under which the library
// keeps the glyph variants that have no codepoint of their own; no text a
// host hands over holds those.
IMTYPESET_API bool FaceHasGlyph(ImTypesetFace face, unsigned int codepoint);

// --- Widgets -------------------------------------------------------------------------
//
// One paragraph, set by `style` (null: GetStyle()) and wrapped to `wrap_width`
// pixels (zero or less: the width available, GetContentRegionAvail().x).
// Advances the cursor as any item does and can be asked about afterwards with
// IsItemHovered(), GetItemRectMin() and the rest. Works in windows, child
// windows, tables and under clip rectangles. All are [E], and return false
// when the text could not be set as asked: see ERRORS at the top.
//
// THE ITEM is a whole number of pixels tall and as wide as the text block:
// the wrap width for justified, centred and right-aligned text, whose lines
// are placed across it, and the longest line for left-aligned text. A glyph's
// antialiased edge can reach one pixel past its right side, as an italic's
// overhang can in any text.
//
// THE BASELINE. The item is placed at the cursor like ImGui::Dummy(), with no
// text baseline offset: imgui.h has no way to give one. Beside a framed widget
// do what ImGui's own text needs told too:
//
//     ImGui::Button("Open"); ImGui::SameLine();
//     ImGui::AlignTextToFramePadding();
//     ImTypeset::TextUnformatted("a label");
//
// A line here is also LineHeight ems tall (1.35 by default) where ImGui's is
// the font size, so a one-line label is taller than ImGui::Text()'s.
//
// THE LAYOUT IS KEPT in the context and found again by the ID stack (PushID)
// and the text's CONTENT, never its address: the same words in a new buffer
// each frame are one layout, and so they are from any binding. It is redone
// when the text, the width, the style or the fonts change, and dropped under
// SetTextCacheLimits(). Finding it reads every byte of the text each frame,
// in view or not; a text of megabytes is better kept as an ImTypesetParagraph.
//
// OUT OF VIEW. A text whose rectangle is clipped draws nothing. If the width
// changes while it is out of view it is NOT laid out again: it keeps the
// height it had and is laid out when it next comes into view. So dragging a
// window's edge re-breaks what is visible and not the whole document; the
// price is that the scroll bar's extent is an estimate until the rest has
// been seen at the new width, and a text scrolled into view from above can
// move what is below it by the difference.
//
// With no font added for the style's face, or under a backend without
// ImGuiBackendFlags_RendererHasTextures, the text is drawn by
// ImGui::TextWrapped()'s machinery instead, so a host works before its fonts
// do, and the call returns false with the reason.

// `text_end` null: `text` is zero-terminated. A binding passes one string.
IMTYPESET_API bool TextUnformatted(const char* text, const char* text_end = NULL,
                                   ImTypesetTextFlags flags = 0, float wrap_width = 0.0f,
                                   const ImTypesetStyle* style = NULL);
// Formats, then TextUnformatted(result). No flags: a formatted number should
// not become a formula.
IMTYPESET_API bool Text(const char* fmt, ...) IM_FMTARGS(1);
IMTYPESET_API bool TextV(const char* fmt, va_list args) IM_FMTLIST(1);
// The same, found again by `str_id` (and the ID stack) instead of by content:
// for text that changes every frame (a counter, a log's last line), which by
// content would leave a new layout behind each frame until the cache limit
// took it. The text is still compared, and laid out again when it differs.
IMTYPESET_API bool TextWithID(const char* str_id, const char* text, const char* text_end = NULL,
                              ImTypesetTextFlags flags = 0, float wrap_width = 0.0f,
                              const ImTypesetStyle* style = NULL);
// Text the user can select with the mouse and copy: a drag selects by
// character, a double click and drag by word, a triple click by line;
// Ctrl+C (Cmd+C) while it has focus puts the selection on the clipboard
// through ImGui::SetClipboardText(), and Ctrl+A selects everything. A click
// elsewhere clears the selection. The highlight is drawn behind the text in
// the style's SelectionColor. `selection`, if given, IS the selection, as a
// `bool*` is a checkbox's state: read when the call starts, so a host can set
// it (a search hit, a restored session), and written before it returns. Null
// keeps the selection with the layout, by `str_id`.
IMTYPESET_API bool SelectableText(const char* str_id, const char* text, const char* text_end = NULL,
                                  ImTypesetTextFlags flags = 0, float wrap_width = 0.0f,
                                  const ImTypesetStyle* style = NULL,
                                  ImTypesetTextRange* selection = NULL);

// The size the text would have, without drawing it or keeping anything: the
// analogue of ImGui::CalcTextSize(). `wrap_width` zero or less measures the
// text unwrapped: one line (one per newline with HardBreaks), as wide as it
// comes. Needs a context and its fonts but no window and no frame. Lays the
// text out when it, the width or the style differ from the call before; an
// ImTypesetParagraph keeps an answer for as long as the host wants it.
// (0, 0) when the text cannot be set at all. The answer is in whole pixels,
// rounded up, as GetParagraphSize()'s is: the width a text has to a fraction
// of a pixel is GetParagraphLine()'s (see GetParagraphSize). [E]
IMTYPESET_API ImVec2 CalcTextSize(const char* text, const char* text_end = NULL,
                                  float wrap_width = 0.0f, ImTypesetTextFlags flags = 0,
                                  const ImTypesetStyle* style = NULL);

// --- Paragraphs ----------------------------------------------------------------------
//
// Create, SetParagraphText, then each frame LayoutParagraph (free when nothing
// changed) and DrawParagraph. A paragraph belongs to the context that was
// current when it was created and every function here uses that context,
// whichever is current.

// Null with no current context. [E]
IMTYPESET_API ImTypesetParagraph* CreateParagraph();
// Null is ignored. Constant time.
IMTYPESET_API void DestroyParagraph(ImTypesetParagraph* paragraph);

// Replaces the paragraph's text. The bytes are copied. UTF-8; runs of white
// space are one space wherever they fall, across runs too, and white space at
// either end is dropped. `text_end` null: `text` is zero-terminated. `runs`
// points at `runs_count` runs, or is null. False if something was wrong: the
// text is still set, as far as it could be (ImTypesetError_TextTooLong, or
// ImTypesetError_DescriptionCut for a run out of order). [E]
IMTYPESET_API bool SetParagraphText(ImTypesetParagraph* paragraph, const char* text,
                                    const char* text_end = NULL, const ImTypesetRun* runs = NULL,
                                    int runs_count = 0, ImTypesetTextFlags flags = 0);

// Breaks the paragraph into lines `width` pixels wide. `style` null is
// GetStyle(). Does nothing when the text, the width, the style and the fonts
// are what they were at the last call. False if anything is wrong with the
// paragraph as it now stands, its text included: GetParagraphError(). [E]
IMTYPESET_API bool LayoutParagraph(ImTypesetParagraph* paragraph, float width,
                                   const ImTypesetStyle* style = NULL);

// What the last SetParagraphText and LayoutParagraph found wrong with this
// paragraph. Code is None if nothing. Never null.
IMTYPESET_API const ImTypesetErrorInfo* GetParagraphError(const ImTypesetParagraph* paragraph);
IMTYPESET_API ImTypesetStatusFlags GetParagraphStatus(const ImTypesetParagraph* paragraph);
// The formulas found in the paragraph when it was last laid out, in text
// order: how many, and each. An index that is not one answers an empty
// formula: TextStart and TextEnd both zero, Reason "". None before the first
// LayoutParagraph and without ImTypesetTextFlags_Math.
IMTYPESET_API int GetParagraphFormulaCount(const ImTypesetParagraph* paragraph);
IMTYPESET_API ImTypesetFormula GetParagraphFormula(const ImTypesetParagraph* paragraph, int index);

// As last laid out: whole pixels, rounded up. The width is the block's: see
// THE ITEM above. That is the wrap width for text that is not left-aligned,
// so it is NOT how wide the text itself is. THE NATURAL WIDTH of a text, the
// width it has on one line, is read off a line: lay the paragraph out wider
// than it can be (1e6) and take GetParagraphLine(p, 0).Max.x - Min.x, which
// is where the line's content starts and ends whatever the alignment (the
// widest of the lines, with hard breaks). With ImTypesetAlign_Left
// GetParagraphSize().x is the same number rounded up to a pixel.
IMTYPESET_API ImVec2 GetParagraphSize(const ImTypesetParagraph* paragraph);
IMTYPESET_API int GetParagraphLineCount(const ImTypesetParagraph* paragraph);
// Line `line` of GetParagraphLineCount(), from the top. A line that is not
// one answers all zero, which no line is: a line's Baseline is below its
// Min.y. The first line's Baseline is what aligns a paragraph drawn with
// DrawParagraph() with ImGui text beside it.
IMTYPESET_API ImTypesetLine GetParagraphLine(const ImTypesetParagraph* paragraph, int line);

// Draws the paragraph with its top-left corner at `pos`, clipped by the draw
// list's clip rectangle: lines outside it are skipped without being visited.
// `selection`, if given, is highlighted behind the text in the colour the
// style had at the last LayoutParagraph; a host that wants another draws the
// rectangles of GetParagraphRangeRects() itself. Call inside a frame of the
// ImGui context the paragraph's context was created in, on a draw list whose
// texture is the font atlas (any window's is). False, with nothing drawn, if
// that context is not current, the backend has no texture updates, the
// paragraph has no font, or it has not been laid out since its text or the
// context's fonts last changed. `draw_list` null is the current window's
// (ImGui::GetWindowDrawList()), for a binding that has no way to name one,
// and needs what that needs: a current window, which there is from
// ImGui::NewFrame() to ImGui::EndFrame(). It is refused before the first
// frame and after ImGui::Render(). AFTER AN ImGui::EndFrame() THAT NO
// Render() FOLLOWED it is not: nothing in imgui.h tells that state from the
// inside of a frame, and the call is then ImGui's own failure on a null
// window, as ImGui::GetWindowDrawList() itself is. The same holds for the
// widgets and for DocumentView(). A host that ends frames without rendering
// them (a headless test) draws before EndFrame(), or names a draw list. [E]
IMTYPESET_API bool DrawParagraph(const ImTypesetParagraph* paragraph, ImDrawList* draw_list,
                                 const ImVec2& pos, const ImTypesetTextRange* selection = NULL);

// The same as vertices and indices, appended to the two buffers, for a
// renderer of the host's own. `clip_rect` is (min x, min y, max x, max y) in
// the coordinates of `pos`; lines and glyphs outside it are left out, and
// nothing is cut at its edge, so set the same rectangle as a scissor. Indices
// are absolute positions in `out_vertices`. Everything is textured from the
// ImGui font atlas (ImGui::GetIO().Fonts->TexRef), rectangles from its white
// pixel; the atlas grows as glyphs are first used, so the host must honour
// ImGui's texture updates as an ImGui renderer backend does. False, with
// nothing appended, for what DrawParagraph refuses and if the vertices would
// not fit ImDrawIdx. [E]
IMTYPESET_API bool DrawParagraphToBuffers(const ImTypesetParagraph* paragraph, const ImVec2& pos,
                                          const ImVec4& clip_rect,
                                          ImVector<ImDrawVert>* out_vertices,
                                          ImVector<ImDrawIdx>* out_indices,
                                          const ImTypesetTextRange* selection = NULL);

// --- Hit testing, selection, copy ----------------------------------------------------
//
// Points are relative to the paragraph's top-left corner; positions in the
// text are byte offsets (see POSITIONS at the top). All answer for the
// paragraph as it was last laid out, and none of them fails: a paragraph that
// was not laid out is empty.

// The caret position nearest a point, the line it is on, and whether the
// point is on text. A point above the first line belongs to the first and one
// below the last to the last.
IMTYPESET_API ImTypesetHit HitTestParagraph(const ImTypesetParagraph* paragraph,
                                            const ImVec2& local_pos);
// What a drag from `anchor` to `active` selects. At Word and Line it is the
// union of the units under each end.
IMTYPESET_API ImTypesetTextRange SelectParagraphRange(
    const ImTypesetParagraph* paragraph, const ImVec2& anchor, const ImVec2& active,
    ImTypesetGranularity granularity = ImTypesetGranularity_Character);
// Appends one rectangle per line the range touches, as (min x, min y, max x,
// max y), and returns how many: what to fill to highlight it.
IMTYPESET_API int GetParagraphRangeRects(const ImTypesetParagraph* paragraph,
                                         const ImTypesetTextRange& range,
                                         ImVector<ImVec4>* out_rects);
// Where a caret before the character at `byte_offset` stands, as (min x,
// min y, max x, max y): a rectangle of no width and its line's height. A
// position at which a line was broken answers the start of the later line,
// and one inside a formula the end of the formula. All zero if the paragraph
// has no lines.
IMTYPESET_API ImVec4 GetParagraphCaretRect(const ImTypesetParagraph* paragraph, int byte_offset);
// Appends the text of the range as it was written, not as it was wrapped: a
// line break made by layout is a space, one made by
// ImTypesetTextFlags_HardBreaks is a newline, a character drawn as a stand-in
// is the character that was written, and a formula is its source between its
// dollars. If `out` already holds text and this paragraph adds any, a blank
// line ("\n\n") goes between them: call it for each paragraph of a document
// in turn, with the same buffer, and the result is the document's selection.
// Returns how many bytes it appended. Allocates (a dozen or so blocks): it is
// a copy, not something to call every frame.
IMTYPESET_API int AppendParagraphRangeText(const ImTypesetParagraph* paragraph,
                                           const ImTypesetTextRange& range, ImGuiTextBuffer* out);

// --- Documents -----------------------------------------------------------------------
//
// Styled blocks in; layout, drawing and selection out. The host describes its
// blocks ONCE PER EDIT in one call (SetDocumentBlocks, ReplaceDocumentBlocks,
// AppendDocumentText) and then, each frame, makes a constant number of calls
// whatever the document's length: LayoutDocument, DrawDocument, and a hit test
// when the mouse matters. DocumentView() is all of that in a child window.
//
// LAYOUT IS LAZY. LayoutDocument() only says how wide the column is. A block
// is broken into lines when a draw, a hit test or a question about its
// geometry first reaches it, and again only after it or the width changes; a
// document of ten thousand blocks is set as fast as its text is copied. The
// price is the scroll bar: GetDocumentSize() counts a block that has never
// been laid out as an estimate (its length at its line height), and one laid
// out at another width as the height it then had. When such a block comes
// into view it takes its true height and everything below it moves by the
// difference; nothing above the window ever moves because of what the window
// shows. LayoutDocumentWindow() with the whole height settles all of it, at
// the cost of laying all of it out.
//
// BLOCKS OF NO HEIGHT (a host block of Extent 0, a code block with no lines)
// with no space between them all stand at one height. A window that shows
// that height shows every one of them, and visits each every frame; a window
// that starts just below it lays them out with what it shows, once. A million
// of them in a row is a million steps, where a million paragraphs are a
// screenful.
//
// EDITS COST WHAT CHANGED. A block that an edit does not name keeps its layout
// and the meaning of every position in it. AppendDocumentText() and
// SetDocumentBlockExtent() touch one block and visit no other. An edit that
// adds, removes or re-describes blocks makes the next layout go over every
// block once, with arithmetic and no line breaking (ImTypesetDocumentStats::
// StructureVisits): the widest marker of a list decides the gutter of all of
// its items.
//
// POSITIONS are ImTypesetPosition: (block, byte offset into that block's text
// as the host gave it). For a code block the text is the raw source, tabs and
// all, up to the last character that is set: the line end after it and any
// blank lines that follow are not part of the block, and are not copied. No
// position the library answers is past that character, and one a host gives
// past it is that end. The bytes are kept all the same
// (GetDocumentBlockTextSize() counts them), and they stop being the block's
// end when AppendDocumentText() puts text after them.
//
// WHAT A COPY GIVES (AppendDocumentRangeText): within a block what
// AppendParagraphRangeText gives; a code block's source bytes as they were
// handed in, tabs and "\r\n" included, as far as its text goes (see
// POSITIONS: not the line end after its last character); a host block's text
// whole when the range covers any of it; a table's cells with a tab between
// the cells of a row and a newline between rows (see "Tables"); nothing for a
// rule or a marker.
// `block_separator` goes between two blocks THAT EACH GAVE TEXT, once, however
// many blocks lie between them that gave none: a rule, a host block without
// text, an empty block, a block the range only touches the edge of. Such a
// block leaves no trace in a copy, and a range that starts or ends in one has
// no separator at that end. (Text, a rule, text copies as the two texts with
// one separator between them, which is what the reference copies.)
//
// A document belongs to the context that was current when it was created.

// Null with no current context. [E]
IMTYPESET_API ImTypesetDocument* CreateDocument();
// Null is ignored.
IMTYPESET_API void DestroyDocument(ImTypesetDocument* document);

// Replaces everything the document holds. False, with
// ImTypesetError_DescriptionCut, if anything was wrong with what was handed
// over; the blocks are still set, as far as they could be (an offset outside
// the text is cut to it, a style or container that is not there is none, a
// run out of order is left out), and the message names the first block or
// container it was. ImTypesetError_TextTooLong likewise, for a block whose
// text was cut at the limit. What a block's text holds that cannot be set as
// asked -- a refused formula, a face without a font -- is found when it is
// laid out: GetDocumentBlockError(). Nothing is done, and the document holds
// what it held, only for more than 4,194,304 blocks
// (ImTypesetError_InvalidArgument). [E]
IMTYPESET_API bool SetDocumentBlocks(ImTypesetDocument* document,
                                     const ImTypesetDocumentContent* content);
// Replaces blocks [first, first + count) by content's blocks: an insertion
// when count is 0, a removal when content has none (or is null). content's
// Styles and Containers are APPENDED to the document's and never replace
// them, so the new blocks name old entries by the index they always had and
// new ones by indices that go on from GetDocumentStyleCount() and
// GetDocumentContainerCount(). Entries no block names any more stay until
// SetDocumentBlocks(). False as SetDocumentBlocks is; and with nothing done
// (ImTypesetError_InvalidArgument) if [first, first + count) are not blocks
// of the document or it would hold too many. The selection DocumentView()
// keeps moves with its blocks: see there. [E]
IMTYPESET_API bool ReplaceDocumentBlocks(ImTypesetDocument* document, int first, int count,
                                         const ImTypesetDocumentContent* content);
// Appends bytes to the text of the LAST block, for text that arrives a piece
// at a time. `runs` are further runs of that block, with offsets into its
// whole text, starting at or after the end of the runs it has. Only that
// block is laid out again, and positions in what it already held keep their
// meaning. `text_end` null: `text` is zero-terminated. False, with nothing
// done, without a last block that holds text (ImTypesetError_InvalidArgument).
// False with the text appended if THIS call's text was cut at the block's
// limit (ImTypesetError_TextTooLong) or one of THIS call's runs was out of
// order and left out (ImTypesetError_DescriptionCut). What an earlier call
// found wrong with the block is not said again here: it is
// GetDocumentBlockError()'s. [E]
IMTYPESET_API bool AppendDocumentText(ImTypesetDocument* document, const char* text,
                                      const char* text_end = NULL, const ImTypesetRun* runs = NULL,
                                      int runs_count = 0);
// Appends cells to the LAST block, which must be a table, with the text they
// are stretches of: rows that arrive one at a time. `text` is appended to the
// block's text; `cells` are further cells of the table, with offsets into its
// WHOLE text, at or after the end of the cells it has (a first cell without
// ImTypesetCellFlags_RowStart goes on with the last row); `runs` as for
// AppendDocumentText. Rows the table already had are not set again unless a
// column's width changes. AppendDocumentText() on a table appends to its last
// cell, which must end where the table's text ends. False as
// AppendDocumentText is. [E]
IMTYPESET_API bool AppendDocumentTableCells(ImTypesetDocument* document, const char* text,
                                            const char* text_end, const ImTypesetCell* cells,
                                            int cells_count, const ImTypesetRun* runs = NULL,
                                            int runs_count = 0);
// States a table's columns again: what a drag of a column's edge does each
// frame. `columns` replaces the table's; rows are set again only if a width
// or an alignment changed. False, with nothing done, if `block` is not a
// table. [E]
IMTYPESET_API bool SetDocumentTableColumns(ImTypesetDocument* document, int block,
                                           const ImTypesetTableColumn* columns, int columns_count);
// Changes ImTypesetBlock::Extent of one block: a host widget that grew. [E]
IMTYPESET_API bool SetDocumentBlockExtent(ImTypesetDocument* document, int block, float extent);
// A copy is taken; null is the default. Every block is laid out again. [E]
IMTYPESET_API bool SetDocumentOptions(ImTypesetDocument* document,
                                      const ImTypesetDocumentOptions* options);

IMTYPESET_API int GetDocumentBlockCount(const ImTypesetDocument* document);
IMTYPESET_API int GetDocumentStyleCount(const ImTypesetDocument* document);
IMTYPESET_API int GetDocumentContainerCount(const ImTypesetDocument* document);
// How many bytes of text a block holds, as they were handed in; 0 for a block
// that is not one. For a code block that ends in line ends this is more than
// its last position: see POSITIONS.
IMTYPESET_API int GetDocumentBlockTextSize(const ImTypesetDocument* document, int block);

// Says how wide the column is, in pixels, and how text is set where a block's
// style does not say (null: GetStyle()). Lays nothing out and costs nothing
// when neither has changed. [E]
IMTYPESET_API bool LayoutDocument(ImTypesetDocument* document, float width,
                                  const ImTypesetStyle* style = NULL);
// Lays out the blocks between two heights, in pixels down from the
// document's top (the UNCUT column's, whatever the columns), and cuts the
// columns that show them: what DrawDocument does for what it is about to draw. For a
// host that needs geometry before it draws, or wants the whole document
// settled (0 and a `bottom` at or past GetDocumentSize().y). False if any
// block it laid out or found laid out has something wrong with it; the first
// such block's error is the last error. [E]
IMTYPESET_API bool LayoutDocumentWindow(ImTypesetDocument* document, float top, float bottom);
// The column's width and the document's height, whole pixels rounded up. The
// height is an estimate until everything has been laid out at this width:
// see LAYOUT IS LAZY. In columns, the page's: as wide as
// GetDocumentColumnCount() columns and their gutters, and as tall as a
// column, or as what the tallest Overfull column cut so far holds if that
// is more.
IMTYPESET_API ImVec2 GetDocumentSize(ImTypesetDocument* document);
// What was last found wrong with one block when its text was set and it was
// laid out: what setting it found (a text cut at the limit, a run left out)
// from when it is set, the rest once it has been laid out. Code is None if
// nothing. Never null.
IMTYPESET_API const ImTypesetErrorInfo* GetDocumentBlockError(const ImTypesetDocument* document,
                                                              int block);
IMTYPESET_API ImTypesetDocumentStats GetDocumentStats(const ImTypesetDocument* document);

// Draws the document with its top-left corner at `pos`, laying out first
// whatever the draw list's clip rectangle shows and is not laid out. Blocks
// outside the clip rectangle are not visited: the work is what is in view
// plus a logarithm of the number of blocks. Refuses what DrawParagraph
// refuses; `draw_list` null is the current window's, as there. [E]
IMTYPESET_API bool DrawDocument(ImTypesetDocument* document, ImDrawList* draw_list,
                                const ImVec2& pos, const ImTypesetDocumentDraw* draw = NULL);
// The same as vertices and indices: see DrawParagraphToBuffers. [E]
IMTYPESET_API bool DrawDocumentToBuffers(ImTypesetDocument* document, const ImVec2& pos,
                                         const ImVec4& clip_rect,
                                         ImVector<ImDrawVert>* out_vertices,
                                         ImVector<ImDrawIdx>* out_indices,
                                         const ImTypesetDocumentDraw* draw = NULL);

// Points are relative to the document's top-left corner, in pixels. Each of
// these lays out the blocks it has to look at, and no others.

// A point above the first block belongs to the first and one below the last
// to the last; between two blocks, to the lower one, with InBlock false.
// IN COLUMNS filled in order, a point costs the columns up to the one it is
// in: a point far to the right of what has been shown cuts, and lays out,
// everything before it, and one past the last column all of the document.
IMTYPESET_API ImTypesetDocumentHit HitTestDocument(ImTypesetDocument* document,
                                                   const ImVec2& local_pos);
// What a drag from `anchor` to `active` selects, across blocks. At Word and
// Line it is the union of the units under each end.
IMTYPESET_API ImTypesetDocumentRange
SelectDocumentRange(ImTypesetDocument* document, const ImVec2& anchor, const ImVec2& active,
                    ImTypesetGranularity granularity = ImTypesetGranularity_Character);
// Appends one rectangle per line the range touches (in a table, per cell and
// line), as (min x, min y, max x, max y), and returns how many. Lays out
// every block of the range. In columns the rectangles are the page's, each in
// the column its line is in, and cutting the columns as far as the range
// goes is part of the call: the answer does not depend on what was drawn
// before it.
IMTYPESET_API int GetDocumentRangeRects(ImTypesetDocument* document,
                                        const ImTypesetDocumentRange& range,
                                        ImVector<ImVec4>* out_rects);
// As GetParagraphCaretRect, in the document. In a table, see A POINT IN A
// CELL'S RECTANGLE. In columns it is the page's, as for GetDocumentRangeRects.
IMTYPESET_API ImVec4 GetDocumentCaretRect(ImTypesetDocument* document,
                                          const ImTypesetPosition& position);
// Appends the text of the range: see WHAT A COPY GIVES. `block_separator`
// null is a blank line ("\n\n"). Returns how many bytes it appended. Lays out
// the text blocks of the range.
IMTYPESET_API int AppendDocumentRangeText(ImTypesetDocument* document,
                                          const ImTypesetDocumentRange& range, ImGuiTextBuffer* out,
                                          const char* block_separator = NULL);
// Where a block is. Lays it out, and nothing above it: its top is exact only
// when everything above it has been laid out at this width.
IMTYPESET_API ImTypesetBlockGeometry GetDocumentBlockGeometry(ImTypesetDocument* document,
                                                              int block);
// Line `line` of a block, relative to the BLOCK's top-left corner
// (GetDocumentBlockGeometry().FullMin); all zero if there is none.
IMTYPESET_API ImTypesetLine GetDocumentBlockLine(ImTypesetDocument* document, int block, int line);

// A table, one of its columns and one of its rows, as laid out at the
// document's width: each lays the table out if it is not. All zero (and a
// RowCount of 0) if `block` is not a table, or there is no such column or row.
IMTYPESET_API ImTypesetTableGeometry GetDocumentTableGeometry(ImTypesetDocument* document,
                                                              int block);
IMTYPESET_API ImTypesetTableColumnGeometry GetDocumentTableColumn(ImTypesetDocument* document,
                                                                  int block, int column);
IMTYPESET_API ImTypesetTableRowGeometry GetDocumentTableRow(ImTypesetDocument* document, int block,
                                                            int row);
// The cell a position is in, and the position of byte `offset` of a cell's
// text (clamped to the cell). Neither lays anything out; each is a search
// among the table's cells, a step for each doubling of their number.
IMTYPESET_API ImTypesetCellPosition GetDocumentTableCell(const ImTypesetDocument* document,
                                                         const ImTypesetPosition& position);
IMTYPESET_API ImTypesetPosition GetDocumentTablePosition(const ImTypesetDocument* document,
                                                         int block, int row, int column,
                                                         int offset);

// Says how the document is shown in columns: see "Columns". A copy is taken.
// Null, or columns that say nothing is cut, shows it uncut again. Lays
// nothing out, lays nothing out AGAIN (a block's lines do not depend on the
// columns), and costs nothing when what is said has not changed. Values that
// are not numbers are zero; a Count past 4,096 is 4,096. False only for no
// document. The height a DocumentView() lent (Count of two or more, Height 0)
// stays lent while the same columns are said again, so a host may say them
// every frame and ask its questions before the view is drawn; saying other
// columns gives it up until the view is next drawn. [E]
IMTYPESET_API bool SetDocumentColumns(ImTypesetDocument* document, const ImTypesetColumns* columns);
// How many columns there are: 1 for a document shown uncut. BALANCED: Count,
// and everything is laid out to say it. IN ORDER: the columns cut so far and
// an estimate of the rest, from heights that are themselves estimates until
// their blocks are laid out (see LAYOUT IS LAZY); exact once the last column
// has been reached, which LayoutDocumentWindow() with the whole height
// brings about. Never less than 1.
IMTYPESET_API int GetDocumentColumnCount(ImTypesetDocument* document);
// Column `column` on the page. Cuts every column up to it, laying out what
// they hold. Exists is false, and the rest zero, for a column that is not
// there; a document shown uncut has column 0, which is all of it.
IMTYPESET_API ImTypesetColumnGeometry GetDocumentColumn(ImTypesetDocument* document, int column);
// Where a point of the uncut column is on the page: what takes an answer of
// GetDocumentBlockGeometry, GetDocumentBlockLine or a table query to where it
// is drawn. A point belongs to the column whose first line's top is the last
// at or above it, to within what a float of pixels can say (a top answered by
// one of those calls is its own line's); the space dropped at a cut goes with
// the column above. The same point back for a document shown uncut. Cuts the
// columns up to the one that shows the point.
// To carry a RECTANGLE (a line's, a table row's, a cell's), carry its top-left
// corner and keep its width and height: its bottom edge is the top of the
// line below, which at a column's foot is the head of the next column. A
// block's rectangle is one rectangle only while the block is in one column.
// A float of pixels says a height to about a millionth of itself: at heights
// past a million pixels the column is chosen to within a quarter pixel.
IMTYPESET_API ImVec2 GetDocumentColumnPos(ImTypesetDocument* document, const ImVec2& uncut_pos);

// A document in a child window that scrolls it: lays out and draws what is in
// view, selects with the mouse as SelectableText() does (across blocks), and
// copies with Ctrl+C. `size` is the child's, as for ImGui::BeginChild(): zero
// in a direction takes what is left of the window in it, which is NEXT TO
// NOTHING in a window that sizes itself to its contents (ImGui gives such a
// child a few pixels: give a height there).
// `selection`, if given, is the selection, read and written: the host's, to
// move when it edits blocks before it. Null keeps it with the document, and
// ReplaceDocumentBlocks() then moves it with its blocks: a block replaced one
// for one keeps what was selected in it (cut to its new text), and where the
// number of blocks changes, the part of the selection in the blocks replaced
// is given up. SetDocumentBlocks() clears it. `draw`'s marks and flags are
// drawn; its Selection is not read. Returns false when something in view
// could not be set as asked.
// IN COLUMNS (SetDocumentColumns): the view shows Count columns across its
// width, each as wide as that leaves after the gutters, and scrolls sideways
// through the rest; IN ORDER with a Height of 0 a column is as tall as the
// view. BALANCED columns are scrolled down, like the uncut document. [E]
IMTYPESET_API bool DocumentView(const char* str_id, ImTypesetDocument* document,
                                const ImVec2& size = ImVec2(0.0f, 0.0f),
                                ImTypesetDocumentRange* selection = NULL,
                                const ImTypesetDocumentDraw* draw = NULL,
                                const ImTypesetStyle* style = NULL);

// --- Offsets for bindings ------------------------------------------------------------
//
// For a language whose strings are indexed by codepoint: where codepoint
// number `codepoint_offset` of a UTF-8 text starts, in bytes, and how many
// codepoints start before `byte_offset`. Counted as the library decodes, and
// as ImTypesetErrorInfo::CodepointPos counts: each maximal ill-formed
// sequence of bytes is one codepoint (U+FFFD).
// Clamped to the text at both ends. Linear in the text.
//
// Of any text, before it is given to anything (for ImTypesetRun offsets).
// `text_end` null: `text` is zero-terminated. No context is needed.
IMTYPESET_API int TextCodepointToByteOffset(const char* text, int codepoint_offset,
                                            const char* text_end = NULL);
IMTYPESET_API int TextByteToCodepointOffset(const char* text, int byte_offset,
                                            const char* text_end = NULL);
// Of the text a paragraph holds (for what HitTestParagraph, SelectParagraphRange
// and GetParagraphLine answer), so that a binding's wrapper needs no copy of it.
IMTYPESET_API int ParagraphCodepointToByteOffset(const ImTypesetParagraph* paragraph,
                                                 int codepoint_offset);
IMTYPESET_API int ParagraphByteToCodepointOffset(const ImTypesetParagraph* paragraph,
                                                 int byte_offset);

// --- Demo ----------------------------------------------------------------------------
//
// A window that shows what the library does, section by section, in the fonts
// the host has added; its source, imtypeset_demo.cpp, is where to look up how
// each thing is called. Call it between ImGui::NewFrame() and ImGui::Render(),
// as ImGui::ShowDemoWindow() is. `p_open`, if given, puts a close button on
// it. With no current context it says so. Compiled with
// IMTYPESET_DISABLE_DEMO it is still there to link against and draws nothing.
IMTYPESET_API void ShowDemoWindow(bool* p_open = NULL);
// One section of the demo window without the window: its contents, submitted
// into the current window, for a host that shows it as a page of its own (in a
// tab, a child, a window it made). Text is set at ImGui::GetFontSize(), so
// PushFont() around the call sizes it. What a section remembers between frames
// it remembers per context, and apart from the demo window's own copy of the
// section: a width dragged in one is not dragged in the other. A value that is
// not a section draws nothing; with no current ImTypeset context it says so in
// a line of text.
IMTYPESET_API void ShowDemoSection(ImTypesetDemoSection section);
// The section's title as the demo window's header shows it; "" for a value
// that is not a section, and for every value when compiled with
// IMTYPESET_DISABLE_DEMO. Never null; a string constant.
IMTYPESET_API const char* GetDemoSectionName(ImTypesetDemoSection section);

} // namespace ImTypeset
