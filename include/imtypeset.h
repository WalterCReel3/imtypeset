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
#define IMTYPESET_CHECKVERSION()                                                           \
    (ImTypeset::DebugCheckVersionAndDataLayout(                                            \
         IMTYPESET_VERSION, sizeof(ImWchar), sizeof(ImDrawVert), sizeof(ImDrawIdx),        \
         sizeof(ImTypesetStyle), sizeof(ImTypesetRun), sizeof(ImTypesetTextRange),         \
         sizeof(ImTypesetLine), sizeof(ImTypesetHit), sizeof(ImTypesetFormula),            \
         sizeof(ImTypesetStats), sizeof(ImTypesetErrorInfo)) &&                            \
     ImTypeset::DebugCheckDocumentDataLayout(                                              \
         sizeof(ImTypesetBlock), sizeof(ImTypesetBlockStyle), sizeof(ImTypesetContainer),  \
         sizeof(ImTypesetDocumentContent), sizeof(ImTypesetDocumentOptions),               \
         sizeof(ImTypesetPosition), sizeof(ImTypesetDocumentRange), sizeof(ImTypesetMark), \
         sizeof(ImTypesetDocumentDraw), sizeof(ImTypesetDocumentHit),                      \
         sizeof(ImTypesetBlockGeometry), sizeof(ImTypesetDocumentStats)))

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
    // Something in a document's description was wrong and was cut to what is
    // right: an offset outside the text, a style, a container or runs that
    // are not there, a kind that is not one, a table. The blocks are set.
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
// before it ends. A character belongs to the run its first byte is in. Zero
// everywhere but Start and End changes nothing.
struct ImTypesetRun {
    int Start;
    int End;
    ImTypesetFace Face; // Default keeps the paragraph's.
    float Scale;        // Size, as a multiple of the paragraph's. 0 is 1.
    ImU32 Color;        // 0 keeps the paragraph's ink.
    ImTypesetRunFlags Flags;
    // Whatever the host wants back when asked what is at a place: a link's
    // number, say. Not read by layout. 0 is no tag. Answered by
    // HitTestDocument(); a paragraph keeps it and has no question that answers it.
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
// formula whose opening dollar is in it. Nothing is ever half selected.
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
    // formulas. Blank lines at the end are dropped. See ImTypesetCodeOverflow_.
    ImTypesetBlockKind_Code = 3,
    ImTypesetBlockKind_Rule = 4, // A horizontal rule across the column, Extent thick.
    // A rectangle Extent tall that the library leaves empty, for a widget of
    // the host's: GetDocumentBlockGeometry() says where it is. Its text is
    // what a copy that covers it gives.
    ImTypesetBlockKind_Host = 5,
    // RESERVED. Tables are not laid out yet: the block is set as an empty
    // Host block and the call says ImTypesetError_InvalidArgument.
    ImTypesetBlockKind_Table = 6
};

enum ImTypesetBlockFlags_ {
    ImTypesetBlockFlags_None = 0,
    ImTypesetBlockFlags_HardBreaks = 1 << 0, // As ImTypesetTextFlags_HardBreaks.
    ImTypesetBlockFlags_Math = 1 << 2        // As ImTypesetTextFlags_Math: dollars are looked for.
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
    // than this one's. 0 is none.
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
    // text. Empty is none. Not part of the block's text: never selected.
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
    int TableRows;  // RESERVED for ImTypesetBlockKind_Table. Leave zero.
    int TableColumns;

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
        , TableRows(0)
        , TableColumns(0)
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

    ImTypesetDocumentOptions()
        : QuoteIndent(0.0f)
        , QuoteBarThickness(0.0f)
        , QuoteBarColor(0)
        , RuleColor(0)
        , CodeOverflow(0)
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
// one's colour.
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

// A DEBUG FACILITY, like ImTypesetStats, for one document. The first five
// count since the document was made.
struct ImTypesetDocumentStats {
    ImU64 MaterialBuilds; // Blocks whose text was turned into typesetting material.
    ImU64 BlocksLaidOut;  // Blocks broken into lines.
    ImU64 UnitsStacked;   // Quotes stacked from their blocks.
    // Blocks gone over by passes over the whole document, in which no line is
    // broken: every block, once after each edit that adds, removes or
    // re-describes blocks, and once after a change of style.
    ImU64 StructureVisits;
    ImU64 BlocksDrawn;
    int Blocks;         // How many there are,
    int BlocksDeferred; // and how many of them are not laid out at the present width
                        // (counted when asked: one step a block).
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
// The second half of IMTYPESET_CHECKVERSION(): the structs of a document. [E]
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
IMTYPESET_API ImTypesetContext* CreateContext();
// Null destroys the current one. Its paragraphs die with it. The fonts it
// added stay in the ImGui atlas, which owns them.
IMTYPESET_API void DestroyContext(ImTypesetContext* ctx = NULL);
IMTYPESET_API ImTypesetContext* GetCurrentContext();
IMTYPESET_API void SetCurrentContext(ImTypesetContext* ctx);

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
IMTYPESET_API ImFont* AddFontFromFileTTF(ImTypesetFace face, const char* filename,
                                         int face_index = 0, const ImFontConfig* font_cfg = NULL);
// The bytes are copied.
IMTYPESET_API ImFont* AddFontFromMemoryTTF(ImTypesetFace face, const void* font_data,
                                           int font_data_size, int face_index = 0,
                                           const ImFontConfig* font_cfg = NULL);
// Takes a role's font away and removes its ImFont from the atlas. False if the
// role had none. Outside a frame, like any change to the atlas. A host that
// removes the ImFont itself (ImFontAtlas::RemoveFont, ImFontAtlas::Clear) need
// not call this: the role is found empty at the next frame and text that names
// it reports ImTypesetError_NoFont. [E]
IMTYPESET_API bool RemoveFont(ImTypesetFace face);
// The ImFont drawing a role, or null. CodeBold answers Bold's until it has
// its own, and Default answers Regular's.
IMTYPESET_API ImFont* GetFont(ImTypesetFace face);

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
// (0, 0) when the text cannot be set at all. [E]
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
// text is still set, as far as it could be. [E]
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
// THE ITEM above.
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
// context's fonts last changed. [E]
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
// blank lines that follow are not part of the block, and are not copied.
//
// WHAT A COPY GIVES (AppendDocumentRangeText): within a block what
// AppendParagraphRangeText gives; a code block's source bytes as they were
// handed in; a host block's text whole when the range covers any of it;
// nothing for a rule or a marker; and between two blocks `block_separator`.
//
// A document belongs to the context that was current when it was created.

// Null with no current context. [E]
IMTYPESET_API ImTypesetDocument* CreateDocument();
// Null is ignored.
IMTYPESET_API void DestroyDocument(ImTypesetDocument* document);

// Replaces everything the document holds. False, with
// ImTypesetError_DescriptionCut, if anything was wrong with what was handed
// over; the blocks are still set, as far as they could be (an offset outside
// the text is cut to it, a style or container that is not there is none). A block's own trouble --
// a refused formula, a face without a font -- is found when it is laid out:
// GetDocumentBlockError(). [E]
IMTYPESET_API bool SetDocumentBlocks(ImTypesetDocument* document,
                                     const ImTypesetDocumentContent* content);
// Replaces blocks [first, first + count) by content's blocks: an insertion
// when count is 0, a removal when content has none (or is null). content's
// Styles and Containers are APPENDED to the document's and never replace
// them, so the new blocks name old entries by the index they always had and
// new ones by indices that go on from GetDocumentStyleCount() and
// GetDocumentContainerCount(). Entries no block names any more stay until
// SetDocumentBlocks(). [E]
IMTYPESET_API bool ReplaceDocumentBlocks(ImTypesetDocument* document, int first, int count,
                                         const ImTypesetDocumentContent* content);
// Appends bytes to the text of the LAST block, for text that arrives a piece
// at a time. `runs` are further runs of that block, with offsets into its
// whole text, starting at or after the end of the runs it has. Only that
// block is laid out again, and positions in what it already held keep their
// meaning. False without a last block that holds text. `text_end` null:
// `text` is zero-terminated. [E]
IMTYPESET_API bool AppendDocumentText(ImTypesetDocument* document, const char* text,
                                      const char* text_end = NULL, const ImTypesetRun* runs = NULL,
                                      int runs_count = 0);
// Changes ImTypesetBlock::Extent of one block: a host widget that grew. [E]
IMTYPESET_API bool SetDocumentBlockExtent(ImTypesetDocument* document, int block, float extent);
// A copy is taken; null is the default. Every block is laid out again. [E]
IMTYPESET_API bool SetDocumentOptions(ImTypesetDocument* document,
                                      const ImTypesetDocumentOptions* options);

IMTYPESET_API int GetDocumentBlockCount(const ImTypesetDocument* document);
IMTYPESET_API int GetDocumentStyleCount(const ImTypesetDocument* document);
IMTYPESET_API int GetDocumentContainerCount(const ImTypesetDocument* document);
// How many bytes of text a block holds; 0 for a block that is not one.
IMTYPESET_API int GetDocumentBlockTextSize(const ImTypesetDocument* document, int block);

// Says how wide the column is, in pixels, and how text is set where a block's
// style does not say (null: GetStyle()). Lays nothing out and costs nothing
// when neither has changed. [E]
IMTYPESET_API bool LayoutDocument(ImTypesetDocument* document, float width,
                                  const ImTypesetStyle* style = NULL);
// Lays out the blocks between two heights, in pixels down from the
// document's top: what DrawDocument does for what it is about to draw. For a
// host that needs geometry before it draws, or wants the whole document
// settled (0 and a `bottom` at or past GetDocumentSize().y). False if any
// block it laid out or found laid out has something wrong with it; the first
// such block's error is the last error. [E]
IMTYPESET_API bool LayoutDocumentWindow(ImTypesetDocument* document, float top, float bottom);
// The column's width and the document's height, whole pixels rounded up. The
// height is an estimate until everything has been laid out at this width:
// see LAYOUT IS LAZY.
IMTYPESET_API ImVec2 GetDocumentSize(ImTypesetDocument* document);
// What was last found wrong with one block when its text was set and it was
// laid out. Code is None if nothing, or if it has not been laid out. Never null.
IMTYPESET_API const ImTypesetErrorInfo* GetDocumentBlockError(const ImTypesetDocument* document,
                                                              int block);
IMTYPESET_API ImTypesetDocumentStats GetDocumentStats(const ImTypesetDocument* document);

// Draws the document with its top-left corner at `pos`, laying out first
// whatever the draw list's clip rectangle shows and is not laid out. Blocks
// outside the clip rectangle are not visited: the work is what is in view
// plus a logarithm of the number of blocks. Refuses what DrawParagraph
// refuses. [E]
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
IMTYPESET_API ImTypesetDocumentHit HitTestDocument(ImTypesetDocument* document,
                                                   const ImVec2& local_pos);
// What a drag from `anchor` to `active` selects, across blocks. At Word and
// Line it is the union of the units under each end.
IMTYPESET_API ImTypesetDocumentRange
SelectDocumentRange(ImTypesetDocument* document, const ImVec2& anchor, const ImVec2& active,
                    ImTypesetGranularity granularity = ImTypesetGranularity_Character);
// Appends one rectangle per line the range touches, as (min x, min y, max x,
// max y), and returns how many. Lays out every block of the range.
IMTYPESET_API int GetDocumentRangeRects(ImTypesetDocument* document,
                                        const ImTypesetDocumentRange& range,
                                        ImVector<ImVec4>* out_rects);
// As GetParagraphCaretRect, in the document.
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

// A document in a child window that scrolls it: lays out and draws what is in
// view, selects with the mouse as SelectableText() does (across blocks), and
// copies with Ctrl+C. `size` is the child's, as for ImGui::BeginChild(): zero
// in a direction takes what is left of the window in it, which is NOTHING in a
// window that sizes itself to its contents (give a height there).
// `selection`, if given, is the selection, read and written; null keeps it
// with the document. `draw`'s marks and flags are drawn; its Selection is not
// read. Returns false when something in view could not be set as asked. [E]
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

} // namespace ImTypeset
