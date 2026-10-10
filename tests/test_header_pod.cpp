// The public header on its own: this translation unit includes imtypeset.h and
// nothing else, not even the test harness, and is compiled with the tree's
// flags (-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow
// -Wconversion -Werror). That it compiles is most of the test:
//
//   - the header needs no include before it and no standard library header;
//   - its structs are plain data, which is what a generated binding can hold;
//   - every function can be named, and called with no context, without a crash;
//   - a struct a host fills is, all zero, the default one.
//
// Whether the header's text mentions a std:: type is checked from outside, in
// tests/test_api.cpp.

#include "imtypeset.h"

// imgui.h includes C headers only. A C++ standard library header reached
// through imtypeset.h would define its include guard; these are the guards of
// libstdc++, libc++ and the MSVC library for the headers a layout library is
// most tempted by.
//
// Not when the host's IMGUI_USER_CONFIG includes the C++ library itself, as
// Dear ImGui Bundle's does (<string>, <stdexcept>): imgui.h then brings them
// and there is nothing left to tell apart. Such a build says so with
// IMTYPESET_TEST_IMGUI_CONFIG_INCLUDES_STL (the `bundle-config` preset).
#ifndef IMTYPESET_TEST_IMGUI_CONFIG_INCLUDES_STL
#if defined(_GLIBCXX_STRING) || defined(_GLIBCXX_VECTOR) || defined(_GLIBCXX_MEMORY) || \
    defined(_GLIBCXX_FUNCTIONAL) || defined(_GLIBCXX_ALGORITHM) ||                      \
    defined(_GLIBCXX_STRING_VIEW) || defined(_GLIBCXX_UNORDERED_MAP) ||                 \
    defined(_GLIBCXX_IOSTREAM) || defined(_GLIBCXX_CSTDINT)
#error "imtypeset.h pulled in a libstdc++ header"
#endif
#if defined(_LIBCPP_STRING) || defined(_LIBCPP_VECTOR) || defined(_LIBCPP_MEMORY) ||             \
    defined(_LIBCPP_FUNCTIONAL) || defined(_LIBCPP_ALGORITHM) || defined(_LIBCPP_STRING_VIEW) || \
    defined(_LIBCPP_UNORDERED_MAP) || defined(_LIBCPP_IOSTREAM)
#error "imtypeset.h pulled in a libc++ header"
#endif
#if defined(_STRING_) || defined(_VECTOR_) || defined(_MEMORY_) || defined(_FUNCTIONAL_) || \
    defined(_ALGORITHM_) || defined(_XSTRING_) || defined(_IOSTREAM_)
#error "imtypeset.h pulled in an MSVC standard library header"
#endif
#endif // IMTYPESET_TEST_IMGUI_CONFIG_INCLUDES_STL

// Compiler intrinsics, since <type_traits> is one of the headers this file
// must not need. GCC, Clang and MSVC all spell them this way.
static_assert(__is_standard_layout(ImTypesetStyle), "ImTypesetStyle is plain data");
static_assert(__is_trivially_copyable(ImTypesetStyle), "ImTypesetStyle is plain data");
static_assert(__is_standard_layout(ImTypesetRun), "ImTypesetRun is plain data");
static_assert(__is_trivially_copyable(ImTypesetRun), "ImTypesetRun is plain data");
static_assert(__is_pod(ImTypesetTextRange), "ImTypesetTextRange is plain data");
static_assert(__is_standard_layout(ImTypesetLine) && __is_trivially_copyable(ImTypesetLine),
              "ImTypesetLine is plain data");
static_assert(__is_pod(ImTypesetHit), "ImTypesetHit is plain data");
static_assert(__is_pod(ImTypesetFormula), "ImTypesetFormula is plain data");
static_assert(__is_pod(ImTypesetErrorInfo), "ImTypesetErrorInfo is plain data");
static_assert(__is_pod(ImTypesetStats), "ImTypesetStats is plain data");
static_assert(sizeof(ImTypesetTextRange) == 2 * sizeof(int), "two ints, no padding");
static_assert(ImTypesetFace_Default == 0 && ImTypesetFace_COUNT == 8, "seven roles after zero");
static_assert(IMTYPESET_IMGUI_VERSION_NUM_MIN <= IMGUI_VERSION_NUM, "ImGui is new enough");
// Written out, so that a value inserted in the middle of an enum is a
// compile error here and not a renumbering in every binding.
static_assert(ImTypesetError_NoContext == 1 && ImTypesetError_VersionMismatch == 2 &&
                  ImTypesetError_InvalidArgument == 3 && ImTypesetError_FileNotFound == 4 &&
                  ImTypesetError_FontRefused == 5 && ImTypesetError_NoFont == 6 &&
                  ImTypesetError_TextTooLong == 7 && ImTypesetError_MathRefused == 8 &&
                  ImTypesetError_LayoutFailed == 9 && ImTypesetError_TooManyVertices == 10 &&
                  ImTypesetError_BackendUnsupported == 11,
              "error codes are appended to, never renumbered");
static_assert(ImTypesetTextFlags_HardBreaks == 1 && ImTypesetTextFlags_Math == 4, "text flags");
static_assert(ImTypesetLineBreak_Auto == 0 && ImTypesetLineBreak_FirstFit == 1 &&
                  ImTypesetLineBreak_TotalFit == 2,
              "one choice, three values");

namespace
{

int failures = 0;

void expect(bool condition, const char* what)
{
    if (!condition) {
        ++failures;
        // imgui.h includes <string.h> and <stdarg.h> but not <stdio.h>; the
        // exit status is the report, and `what` is here for a debugger.
        (void)what;
    }
}

bool same_text(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

bool all_zero(const void* object, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(object);
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

} // namespace

int main()
{
    // Every public function, with no ImTypeset context and no ImGui context:
    // each must answer something defined.
    expect(same_text(ImTypeset::GetVersion(), IMTYPESET_VERSION), "version");
    expect(ImTypeset::GetImGuiVersionNum() == IMGUI_VERSION_NUM, "imgui version");
    expect(ImTypeset::GetCurrentContext() == nullptr, "no context yet");
    expect(ImTypeset::GetLastError() != nullptr, "an outcome is never null");
    expect(ImTypeset::GetLastError()->Code == ImTypesetError_None, "nothing has failed yet");

    expect(IMTYPESET_CHECKVERSION(), "this file and the library agree");
    expect(ImTypeset::CreateContext() == nullptr, "no ImGui context, so no context");
    expect(ImTypeset::GetLastError()->Code == ImTypesetError_NoContext, "and it says why");
    expect(ImTypeset::GetLastError()->Message != nullptr, "a message is never null");

    // A struct a host fills: default-constructed and zeroed memory are the
    // same bytes, which is what a C caller or a binding has.
    ImTypesetStyle defaults;
    expect(all_zero(&defaults, sizeof defaults), "the default style is all zero");
    expect(defaults.Face == ImTypesetFace_Default, "default face");
    expect(defaults.Align == ImTypesetAlign_Justify, "default alignment");
    expect(defaults.LineBreak == ImTypesetLineBreak_Auto, "default line breaking");
    ImTypesetRun run;
    expect(all_zero(&run, sizeof run), "the default run is all zero");
    expect(run.Face == ImTypesetFace_Default, "default run");

    ImTypeset::GetStyle().LineHeight = 2.0f; // writable, read by nobody
    expect(ImTypeset::GetStats().Layouts == 0, "stats with no context");
    ImTypeset::SetTextCacheLimits(10, 1 << 20);
    expect(ImTypeset::AddFontFromFileTTF(ImTypesetFace_Regular, "nowhere.ttf") == nullptr, "font");
    expect(ImTypeset::GetLastError()->Code == ImTypesetError_NoContext, "font: why");
    expect(ImTypeset::AddFontFromMemoryTTF(ImTypesetFace_Regular, nullptr, 0) == nullptr, "font");
    expect(!ImTypeset::RemoveFont(ImTypesetFace_Regular), "remove");
    expect(ImTypeset::GetFont(ImTypesetFace_Regular) == nullptr, "no font");
    expect(!ImTypeset::TextUnformatted("no context"), "widget");
    expect(ImTypeset::GetLastError()->Code == ImTypesetError_NoContext, "widget: why");
    expect(!ImTypeset::Text("no %s", "context"), "formatted widget");
    expect(!ImTypeset::TextWithID("id", "no context"), "keyed widget");
    ImTypesetTextRange selected = {5, 9};
    expect(!ImTypeset::SelectableText("id", "no context", nullptr, 0, 0.0f, nullptr, &selected),
           "selectable widget");
    const ImVec2 measured = ImTypeset::CalcTextSize("no context", nullptr, 100.0f);
    expect(measured.x == 0.0f && measured.y == 0.0f, "measure");

    ImTypesetParagraph* paragraph = ImTypeset::CreateParagraph();
    expect(paragraph == nullptr, "no context, so no paragraph");
    expect(!ImTypeset::SetParagraphText(paragraph, "text"), "null paragraph");
    expect(ImTypeset::GetLastError()->Code == ImTypesetError_InvalidArgument, "and why");
    expect(ImTypeset::GetParagraphError(paragraph)->Code == ImTypesetError_InvalidArgument,
           "null paragraph has an error");
    expect(ImTypeset::GetParagraphStatus(paragraph) == ImTypesetStatusFlags_None, "status");
    expect(!ImTypeset::LayoutParagraph(paragraph, 100.0f), "null paragraph is not laid out");
    expect(ImTypeset::GetParagraphSize(paragraph).y == 0.0f, "size");
    expect(ImTypeset::GetParagraphLineCount(paragraph) == 0, "lines");
    const ImTypesetLine line = ImTypeset::GetParagraphLine(paragraph, 0);
    expect(line.Baseline == 0.0f && line.TextEnd == 0, "line");
    expect(ImTypeset::GetParagraphFormulaCount(paragraph) == 0, "formulas");
    const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(paragraph, 0);
    expect(formula.TextStart == 0 && formula.TextEnd == 0, "formula");
    expect(formula.Reason != nullptr && formula.Reason[0] == '\0', "a reason is never null");
    expect(!ImTypeset::DrawParagraph(paragraph, nullptr, ImVec2(0.0f, 0.0f)), "draw");
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    expect(!ImTypeset::DrawParagraphToBuffers(paragraph, ImVec2(0.0f, 0.0f),
                                              ImVec4(0.0f, 0.0f, 1.0f, 1.0f), &vertices, &indices),
           "buffers");
    expect(vertices.Size == 0 && indices.Size == 0, "nothing appended");
    const ImTypesetHit hit = ImTypeset::HitTestParagraph(paragraph, ImVec2(1.0f, 1.0f));
    expect(hit.TextOffset == 0 && hit.Line == -1 && !hit.Inside, "hit");
    const ImTypesetTextRange range =
        ImTypeset::SelectParagraphRange(paragraph, ImVec2(0.0f, 0.0f), ImVec2(9.0f, 9.0f));
    expect(range.Start == 0 && range.End == 0, "range");
    ImVector<ImVec4> rects;
    expect(ImTypeset::GetParagraphRangeRects(paragraph, range, &rects) == 0, "rects");
    const ImVec4 caret = ImTypeset::GetParagraphCaretRect(paragraph, 0);
    expect(caret.x == 0.0f && caret.w == 0.0f, "caret");
    ImGuiTextBuffer copied;
    expect(ImTypeset::AppendParagraphRangeText(paragraph, range, &copied) == 0, "copy");
    expect(copied.size() == 0, "nothing copied");
    ImTypeset::DestroyParagraph(paragraph);

    // Offsets need no context at all. U+00E9 is two bytes, U+1D465 four.
    const char* const mixed = "a\xC3\xA9\xF0\x9D\x91\xA5z";
    expect(ImTypeset::TextCodepointToByteOffset(mixed, 0) == 0, "codepoint 0");
    expect(ImTypeset::TextCodepointToByteOffset(mixed, 2) == 3, "codepoint 2");
    expect(ImTypeset::TextCodepointToByteOffset(mixed, 3) == 7, "codepoint 3");
    expect(ImTypeset::TextCodepointToByteOffset(mixed, 99) == 8, "past the end");
    expect(ImTypeset::TextByteToCodepointOffset(mixed, 3) == 2, "byte 3");
    expect(ImTypeset::TextByteToCodepointOffset(mixed, 7) == 3, "byte 7");
    expect(ImTypeset::TextByteToCodepointOffset(mixed, 99) == 4, "byte past the end");
    expect(ImTypeset::TextByteToCodepointOffset(nullptr, 3) == 0, "no text");
    expect(ImTypeset::ParagraphByteToCodepointOffset(nullptr, 3) == 0, "no paragraph");
    expect(ImTypeset::ParagraphCodepointToByteOffset(nullptr, 3) == 0, "no paragraph");

    ImTypeset::SetCurrentContext(nullptr);
    ImTypeset::DestroyContext();
    return failures == 0 ? 0 : 1;
}
