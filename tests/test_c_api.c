/* The C ABI, used from C.
 *
 * This file is compiled by a C compiler (C99, -Wall -Wextra -Wpedantic) and
 * includes imtypeset_c.h and nothing of ImGui or of C++: that it compiles is
 * the first thing it tests. It then drives a headless frame and holds what the
 * C ABI answers to what the C++ API answers for the same input, byte for byte.
 * The ImGui context, and the C++ API's side of each comparison, are in
 * tests/c_api_host.cpp, whose functions are declared again below.
 */

#include "imtypeset_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- tests/c_api_host.cpp ------------------------------------------------------------ */

int c_api_host_fact(int which);
int c_api_host_throw_from_next_allocation(int kind);
int c_api_host_throws(void);
const char* c_api_host_data_path(const char* relative);
int c_api_host_create(void);
void c_api_host_destroy(void);
struct ImDrawList* c_api_host_begin_frame(void);
void c_api_host_end_frame(void);
void c_api_host_item_size(float* out_width, float* out_height);
int c_api_host_draw_list_vertex_count(const struct ImDrawList* draw_list);
int c_api_host_draw_list_vertices(const struct ImDrawList* draw_list, int from, void* out,
                                  int capacity);
void* c_api_host_reference_paragraph(const char* text, const char* text_end, const double* runs,
                                     int runs_count, int flags, float width);
void c_api_host_reference_destroy(void* paragraph);
int c_api_host_reference_size(void* paragraph, float* out_width, float* out_height);
void c_api_host_reference_calc(const char* text, const char* text_end, float wrap_width, int flags,
                               float* out_width, float* out_height);
int c_api_host_reference_draw(void* paragraph, struct ImDrawList* draw_list, float x, float y,
                              int has_selection, int selection_start, int selection_end);
int c_api_host_reference_buffers(void* paragraph, float x, float y, const float* clip_rect,
                                 void* vertices, int vertices_capacity, void* indices,
                                 int indices_capacity, int* out_vertices_count,
                                 int* out_indices_count);
int c_api_host_reference_copy(void* paragraph, int start, int end, const char* so_far, char* out,
                              int out_size);

enum {
    Fact_SizeofImWchar = 0,
    Fact_SizeofImDrawVert = 1,
    Fact_SizeofImDrawIdx = 2,
    Fact_FreeType = 3,
    Fact_OffsetofCol = 4,
    Fact_ImGuiVersionNum = 5,
    Fact_Exceptions = 6,
    Fact_AssertThrows = 7,
    Fact_OffsetofPos = 8,
    Fact_OffsetofUV = 9
};

/* --- A harness of three macros ------------------------------------------------------- */

static int g_checks = 0;
static int g_failures = 0;
static const char* g_section = "";

static int check_at(int ok, const char* what, int line)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        fprintf(stderr, "tests/test_c_api.c:%d: [%s] failed: %s\n", line, g_section, what);
    }
    return ok;
}

#define CHECK(condition) check_at((condition) ? 1 : 0, #condition, __LINE__)
#define REQUIRE(condition)     \
    do {                       \
        if (!CHECK(condition)) \
            return;            \
    } while (0)
#define SECTION(name) (g_section = (name))

static int last_error_code(void)
{
    ImTypeset_ErrorInfo info;
    memset(&info, 0, sizeof info);
    if (!ImTypeset_GetLastError(&info))
        return -1;
    return info.Code;
}

/* --- The input ----------------------------------------------------------------------- */

/* Two bytes for the i-diaeresis, so that bytes and codepoints differ. */
static const char TEXT[] =
    "The quick brown fox jumps over the lazy dog, and a na\xc3\xafve typesetter sets the same "
    "words into lines of one measure, stretching and shrinking the spaces between them until "
    "both margins are flush. Then it does so again, at another width, and nothing moves that "
    "did not have to.";
#define TEXT_LENGTH ((int)(sizeof TEXT - 1))

#define RUN_COUNT 2
static const ImTypeset_Run RUNS[RUN_COUNT] = {
    {4, 9, ImTypeset_Face_Bold, 0.0f, 0u, ImTypeset_RunFlags_None, 0},
    {10, 15, ImTypeset_Face_Italic, 1.25f, 0xFF2020FFu, ImTypeset_RunFlags_NoBreak, 0}};
static const double RUNS_AS_NUMBERS[RUN_COUNT * 6] = {4.0,  9.0,  2.0, 0.0,  0.0,          0.0,
                                                      10.0, 15.0, 3.0, 1.25, 4280295679.0, 1.0};

static const float WIDTH = 320.0f;

/* TEXT without its terminator and with other bytes after it: what a language
 * whose strings carry a length hands over. */
static char g_unterminated[sizeof TEXT + 8];

static ImTypeset_BuildInfo g_info;

/* --- Before any context -------------------------------------------------------------- */

static void test_build_info(void)
{
    ImTypeset_BuildInfo partial;
    int size;
    SECTION("build info");

    memset(&g_info, 0xEE, sizeof g_info);
    size = ImTypeset_GetBuildInfo(&g_info, (int)sizeof g_info);
    CHECK(size == (int)sizeof g_info);
    CHECK(g_info.StructSize == size);
    CHECK(g_info.AbiVersion == IMTYPESET_C_ABI_VERSION);
    CHECK(g_info.VersionNum == IMTYPESET_C_VERSION_NUM);
    CHECK(strcmp(g_info.Version, IMTYPESET_C_VERSION) == 0);
    CHECK(strcmp(ImTypeset_GetVersion(), IMTYPESET_C_VERSION) == 0);
    CHECK(g_info.ImGuiVersionNum == c_api_host_fact(Fact_ImGuiVersionNum));
    CHECK(ImTypeset_GetImGuiVersionNum() == g_info.ImGuiVersionNum);
    CHECK(g_info.ImGuiVersion[0] != '\0');
    CHECK(g_info.SizeofImWchar == c_api_host_fact(Fact_SizeofImWchar));
    CHECK(g_info.SizeofImWchar == 2 || g_info.SizeofImWchar == 4);
    CHECK(g_info.SizeofImDrawVert == c_api_host_fact(Fact_SizeofImDrawVert));
    CHECK(g_info.SizeofImDrawIdx == c_api_host_fact(Fact_SizeofImDrawIdx));
    CHECK(g_info.OffsetofImDrawVertPos == c_api_host_fact(Fact_OffsetofPos));
    CHECK(g_info.OffsetofImDrawVertUV == c_api_host_fact(Fact_OffsetofUV));
    CHECK(g_info.OffsetofImDrawVertCol == c_api_host_fact(Fact_OffsetofCol));
    CHECK(g_info.SizeofPointer == (int)sizeof(void*));
    CHECK(g_info.ImGuiEnableFreeType == c_api_host_fact(Fact_FreeType));
    CHECK(g_info.CatchesExceptions == c_api_host_fact(Fact_Exceptions));
    CHECK(g_info.FaceCount == ImTypeset_Face_COUNT);
    CHECK(g_info.SizeofStyle == (int)sizeof(ImTypeset_Style));
    CHECK(g_info.SizeofRun == (int)sizeof(ImTypeset_Run));
    CHECK(g_info.SizeofTextRange == (int)sizeof(ImTypeset_TextRange));
    CHECK(g_info.SizeofLine == (int)sizeof(ImTypeset_Line));
    CHECK(g_info.SizeofHit == (int)sizeof(ImTypeset_Hit));
    CHECK(g_info.SizeofFormula == (int)sizeof(ImTypeset_Formula));
    CHECK(g_info.SizeofStats == (int)sizeof(ImTypeset_Stats));
    CHECK(g_info.SizeofErrorInfo == (int)sizeof(ImTypeset_ErrorInfo));

    /* A caller compiled against a shorter struct gets its prefix and no more. */
    memset(&partial, 0x5A, sizeof partial);
    CHECK(ImTypeset_GetBuildInfo(&partial, (int)(2 * sizeof(int))) == size);
    CHECK(partial.StructSize == size);
    CHECK(partial.AbiVersion == IMTYPESET_C_ABI_VERSION);
    CHECK(partial.VersionNum == 0x5A5A5A5A);
    CHECK(ImTypeset_GetBuildInfo(NULL, 0) == size);

    printf("test_c_api: imtypeset %s, ImGui %s, ImWchar %d, ImDrawVert %d, ImDrawIdx %d, %s, "
           "exceptions %s\n",
           g_info.Version, g_info.ImGuiVersion, g_info.SizeofImWchar, g_info.SizeofImDrawVert,
           g_info.SizeofImDrawIdx, g_info.ImGuiEnableFreeType ? "FreeType" : "stb_truetype",
           g_info.CatchesExceptions ? "caught" : "off");
}

static void test_without_a_context(void)
{
    ImTypeset_Style style;
    ImTypeset_Stats stats;
    ImTypeset_Vec2 size;
    ImTypeset_ErrorInfo info;
    char message[256];
    int length;
    SECTION("no context");

    CHECK(ImTypeset_GetCurrentContext() == NULL);
    CHECK(ImTypeset_GetContextSerial() == 0);
    CHECK(ImTypeset_CreateParagraph() == NULL);
    CHECK(last_error_code() == ImTypeset_Error_NoContext);
    length = ImTypeset_GetLastErrorMessage(NULL, 0);
    CHECK(length > 0);
    CHECK(ImTypeset_GetLastErrorMessage(message, (int)sizeof message) == length);
    CHECK((int)strlen(message) == length);
    CHECK(ImTypeset_GetLastError(&info) == 1);
    CHECK(info.Message != NULL && strcmp(info.Message, message) == 0);
    CHECK(ImTypeset_GetLastError(NULL) == 0);

    /* Everything answers, and nothing crashes, with nothing to answer about. */
    CHECK(ImTypeset_GetStats(&stats) == 1);
    CHECK(stats.Layouts == 0 && stats.CachedBytes == 0);
    CHECK(ImTypeset_GetStyle(&style) == 1);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Regular) == NULL);
    CHECK(ImTypeset_RemoveFont(ImTypeset_Face_Regular) == 0);
    CHECK(ImTypeset_DestroyParagraph(NULL) == 1);
    CHECK(ImTypeset_GetParagraphLineCount(NULL) == 0);
    CHECK(ImTypeset_GetParagraphFormulaCount(NULL) == 0);
    CHECK(ImTypeset_GetParagraphStatus(NULL) == ImTypeset_StatusFlags_None);
    size.x = size.y = 7.0f;
    CHECK(ImTypeset_GetParagraphSize(NULL, &size) == 1);
    CHECK(size.x == 0.0f && size.y == 0.0f);
    CHECK(ImTypeset_GetParagraphError(NULL, &info) == 1);
    CHECK(info.Code != ImTypeset_Error_None && info.Message != NULL);

    /* A null struct is this layer's own failure, said in the same place, and
     * forgotten at the next call. */
    CHECK(ImTypeset_GetStyle(NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
    CHECK(ImTypeset_GetLastErrorMessage(message, (int)sizeof message) > 0);
    CHECK(strstr(message, "ImTypeset_GetStyle") != NULL);
    CHECK(ImTypeset_GetLastError(&info) == 1);
    CHECK(info.BytePos == -1 && info.CodepointPos == -1);
    CHECK(ImTypeset_CreateParagraph() == NULL);
    CHECK(last_error_code() == ImTypeset_Error_NoContext);

    /* Offsets need no context. "naXve" with a two-byte X. */
    CHECK(ImTypeset_TextCodepointToByteOffset("na\xc3\xafve", NULL, 3) == 4);
    CHECK(ImTypeset_TextByteToCodepointOffset("na\xc3\xafve", NULL, 4) == 3);
    CHECK(ImTypeset_TextCodepointToByteOffset(TEXT, TEXT + 2, 10) == 2);
    CHECK(ImTypeset_TextByteToCodepointOffset(g_unterminated, g_unterminated + TEXT_LENGTH,
                                              TEXT_LENGTH + 100) == TEXT_LENGTH - 1);
}

static void test_version_check(void)
{
    const size_t wchar_size = (size_t)g_info.SizeofImWchar;
    const size_t vert_size = (size_t)g_info.SizeofImDrawVert;
    const size_t idx_size = (size_t)g_info.SizeofImDrawIdx;
    static const char version_and_more[] = IMTYPESET_C_VERSION "-and-more";
    SECTION("version check");

    /* A host whose ImWchar is the other width: refused, and so is a context. */
    CHECK(IMTYPESET_C_CHECKVERSION(6 - wchar_size, vert_size, idx_size) == 0);
    CHECK(last_error_code() == ImTypeset_Error_VersionMismatch);
    CHECK(ImTypeset_CreateContext() == NULL);
    CHECK(last_error_code() == ImTypeset_Error_VersionMismatch);
    CHECK(IMTYPESET_C_CHECKVERSION(wchar_size, vert_size + 4, idx_size) == 0);
    CHECK(ImTypeset_DebugCheckVersionAndDataLayout(
              version_and_more, NULL, wchar_size, vert_size, idx_size, sizeof(ImTypeset_Style),
              sizeof(ImTypeset_Run), sizeof(ImTypeset_TextRange), sizeof(ImTypeset_Line),
              sizeof(ImTypeset_Hit), sizeof(ImTypeset_Formula), sizeof(ImTypeset_Stats),
              sizeof(ImTypeset_ErrorInfo)) == 0);
    /* The same bytes with an end: the version, and it passes. */
    CHECK(ImTypeset_DebugCheckVersionAndDataLayout(
              version_and_more, version_and_more + strlen(IMTYPESET_C_VERSION), wchar_size,
              vert_size, idx_size, sizeof(ImTypeset_Style), sizeof(ImTypeset_Run),
              sizeof(ImTypeset_TextRange), sizeof(ImTypeset_Line), sizeof(ImTypeset_Hit),
              sizeof(ImTypeset_Formula), sizeof(ImTypeset_Stats),
              sizeof(ImTypeset_ErrorInfo)) == 1);
    CHECK(IMTYPESET_C_CHECKVERSION(wchar_size, vert_size, idx_size) == 1);
    CHECK(last_error_code() == ImTypeset_Error_None);
}

/* --- Fonts --------------------------------------------------------------------------- */

static unsigned char* read_whole_file(const char* path, int* out_size)
{
    FILE* file = fopen(path, "rb");
    unsigned char* data = NULL;
    long size = 0;
    *out_size = 0;
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0)
        data = (unsigned char*)malloc((size_t)size);
    if (data != NULL && fread(data, 1, (size_t)size, file) == (size_t)size)
        *out_size = (int)size;
    fclose(file);
    return data;
}

static void test_fonts(void)
{
    char padded[1024];
    const char* path;
    size_t length;
    struct ImFont* regular;
    struct ImFont* bold;
    unsigned char* bytes;
    int size = 0;
    SECTION("fonts");

    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Regular, "no/such/font.ttf", NULL, 0, NULL) ==
          NULL);
    CHECK(last_error_code() == ImTypeset_Error_FileNotFound);
    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Default, "no/such/font.ttf", NULL, 0, NULL) ==
          NULL);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
    CHECK(ImTypeset_AddFontFromMemoryTTF(ImTypeset_Face_Regular, NULL, 0, 0, NULL) == NULL);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);

    /* A file name as (begin, end), with bytes after its end that are no part
     * of it. */
    path = c_api_host_data_path("fonts/Roboto-Regular.ttf");
    length = strlen(path);
    REQUIRE(length + 8 < sizeof padded);
    memcpy(padded, path, length);
    memcpy(padded + length, ".nothing", 9);
    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Regular, padded, NULL, 0, NULL) == NULL);
    CHECK(last_error_code() == ImTypeset_Error_FileNotFound);
    regular =
        ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Regular, padded, padded + length, 0, NULL);
    REQUIRE(regular != NULL);
    CHECK(last_error_code() == ImTypeset_Error_None);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Regular) == regular);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Default) == regular);

    /* From memory, and the bytes are the caller's to free at once. */
    bytes = read_whole_file(c_api_host_data_path("fonts/Roboto-Bold.ttf"), &size);
    REQUIRE(bytes != NULL && size > 0);
    bold = ImTypeset_AddFontFromMemoryTTF(ImTypeset_Face_Bold, bytes, size, 0, NULL);
    memset(bytes, 0, (size_t)size);
    free(bytes);
    REQUIRE(bold != NULL);
    CHECK(bold != regular);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Bold) == bold);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_CodeBold) == bold);

    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Italic,
                                       c_api_host_data_path("fonts/Roboto-RegularItalic.ttf"), NULL,
                                       0, NULL) != NULL);
    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Math,
                                       c_api_host_data_path("fonts/latinmodern-math.otf"), NULL, 0,
                                       NULL) != NULL);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Code) == NULL);

    /* A role's font can be taken away and given back. */
    CHECK(ImTypeset_AddFontFromFileTTF(ImTypeset_Face_Code,
                                       c_api_host_data_path("fonts/Inconsolata-Medium.ttf"), NULL,
                                       0, NULL) != NULL);
    CHECK(ImTypeset_RemoveFont(ImTypeset_Face_Code) == 1);
    CHECK(ImTypeset_GetFont(ImTypeset_Face_Code) == NULL);
    CHECK(ImTypeset_RemoveFont(ImTypeset_Face_Code) == 0);
}

/* A C++ exception raised inside a call stops at the boundary: the call fails
 * as the header says a call does, and the error says what was thrown. The
 * host's allocator throws it, from the one allocation this layer itself asks
 * ImGui for in ImTypeset_AppendParagraphRangeText(). */
static void test_exception_is_caught(ImTypeset_Paragraph* paragraph)
{
    const ImTypeset_TextRange range = {4, 19};
    ImTypeset_ErrorInfo info;
    char message[256];
    char text[64];
    SECTION("exceptions");

    if (!c_api_host_throw_from_next_allocation(0)) {
        CHECK(!g_info.CatchesExceptions);
        printf("test_c_api: built without exceptions; the exception boundary was not exercised\n");
        return;
    }
    REQUIRE(g_info.CatchesExceptions);
    strcpy(text, "untouched");
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 1, text, (int)sizeof text) == -1);
    CHECK(c_api_host_throws() == 1);
    CHECK(text[0] == '\0');
    CHECK(last_error_code() == ImTypeset_Error_Exception);
    CHECK(ImTypeset_GetLastErrorMessage(message, (int)sizeof message) > 0);
    CHECK(strcmp(message, "c_api_host: the allocator refused") == 0);
    CHECK(ImTypeset_GetLastError(&info) == 1);
    CHECK(info.BytePos == -1 && info.CodepointPos == -1 && info.Message != NULL);

    /* Something that is not a std::exception has no message of its own. */
    REQUIRE(c_api_host_throw_from_next_allocation(1) == 1);
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 1, text, (int)sizeof text) == -1);
    CHECK(c_api_host_throws() == 2);
    CHECK(last_error_code() == ImTypeset_Error_Exception);
    CHECK(ImTypeset_GetLastErrorMessage(message, (int)sizeof message) > 0);

    /* The next call is its own outcome, and the paragraph is as it was. */
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 1, text, (int)sizeof text) == 17);
    CHECK(strcmp(text, "\n\nquick brown fox") == 0);
    CHECK(c_api_host_throws() == 2);
    CHECK(ImTypeset_GetLastError(&info) == 1);
    CHECK(info.Code != ImTypeset_Error_Exception);
    printf("test_c_api: two exceptions stopped at the boundary (%s)\n",
           c_api_host_fact(Fact_AssertThrows) ? "IM_ASSERT throws in this build"
                                              : "IM_ASSERT aborts in this build");
}

/* --- Style --------------------------------------------------------------------------- */

static void test_style(void)
{
    ImTypeset_Style zero;
    ImTypeset_Style style;
    ImTypeset_Style back;
    SECTION("style");

    memset(&zero, 0, sizeof zero);
    memset(&style, 0xEE, sizeof style);
    CHECK(ImTypeset_GetStyle(&style) == 1);
    CHECK(memcmp(&style, &zero, sizeof zero) == 0);

    style.Align = ImTypeset_Align_Right;
    style.EmSize = 19.0f;
    style.Color = 0xFF112233u;
    CHECK(ImTypeset_SetStyle(&style) == 1);
    memset(&back, 0, sizeof back);
    CHECK(ImTypeset_GetStyle(&back) == 1);
    CHECK(memcmp(&back, &style, sizeof style) == 0);
    CHECK(ImTypeset_SetStyle(NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
    CHECK(ImTypeset_SetStyle(&zero) == 1);

    CHECK(ImTypeset_SetTextCacheLimits(64, 1 << 20) == 1);
}

/* --- A paragraph, against the C++ API's ---------------------------------------------- */

static void test_paragraph_geometry(ImTypeset_Paragraph* paragraph, void* reference)
{
    ImTypeset_Vec2 size;
    ImTypeset_Vec2 point;
    ImTypeset_Vec2 far_point;
    ImTypeset_Line line;
    ImTypeset_Line next;
    ImTypeset_Hit hit;
    ImTypeset_TextRange range;
    ImTypeset_Vec4 rects[8];
    ImTypeset_Vec4 caret;
    float reference_width = 0.0f;
    float reference_height = 0.0f;
    int lines;
    int count;
    int i;
    SECTION("geometry");

    lines = ImTypeset_GetParagraphLineCount(paragraph);
    CHECK(lines >= 3);
    CHECK(c_api_host_reference_size(reference, &reference_width, &reference_height) == lines);
    CHECK(ImTypeset_GetParagraphSize(paragraph, &size) == 1);
    CHECK(size.x == reference_width && size.y == reference_height);
    CHECK(size.x > 0.0f && size.y > 0.0f);
    CHECK((ImTypeset_GetParagraphStatus(paragraph) & ImTypeset_StatusFlags_LaidOut) != 0);
    CHECK(ImTypeset_GetParagraphSize(paragraph, NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);

    /* The lines tile the text. */
    for (i = 0; i + 1 < lines; ++i) {
        CHECK(ImTypeset_GetParagraphLine(paragraph, i, &line) == 1);
        CHECK(ImTypeset_GetParagraphLine(paragraph, i + 1, &next) == 1);
        CHECK(line.TextEnd == next.TextStart);
        CHECK(line.Min.y < line.Baseline && line.Baseline <= line.Max.y);
        CHECK(next.Baseline > line.Baseline);
    }
    CHECK(ImTypeset_GetParagraphLine(paragraph, 0, &line) == 1);
    CHECK(line.TextStart == 0);
    CHECK(ImTypeset_GetParagraphLine(paragraph, lines - 1, &next) == 1);
    CHECK(next.TextEnd == TEXT_LENGTH);
    memset(&next, 0xEE, sizeof next);
    CHECK(ImTypeset_GetParagraphLine(paragraph, lines, &next) == 1);
    CHECK(next.TextStart == 0 && next.TextEnd == 0 && next.Baseline == 0.0f);

    /* A point on the second line. */
    CHECK(ImTypeset_GetParagraphLine(paragraph, 1, &line) == 1);
    point.x = line.Min.x + 1.0f;
    point.y = (line.Min.y + line.Max.y) * 0.5f;
    memset(&hit, 0xEE, sizeof hit);
    CHECK(ImTypeset_HitTestParagraph(paragraph, &point, &hit) == 1);
    CHECK(hit.Line == 1);
    CHECK(hit.TextOffset >= line.TextStart && hit.TextOffset <= line.TextEnd);
    CHECK(hit.Inside);
    far_point.x = -50.0f;
    far_point.y = point.y;
    CHECK(ImTypeset_HitTestParagraph(paragraph, &far_point, &hit) == 1);
    CHECK(hit.Line == 1 && !hit.Inside);
    CHECK(ImTypeset_HitTestParagraph(paragraph, NULL, &hit) == 0);
    CHECK(ImTypeset_HitTestParagraph(paragraph, &point, NULL) == 0);

    /* A double click selects a word; a drag from the first line to the second
     * selects across the break. */
    CHECK(ImTypeset_SelectParagraphRange(paragraph, &point, &point, ImTypeset_Granularity_Word,
                                         &range) == 1);
    CHECK(range.Start < range.End);
    CHECK(range.Start >= line.TextStart && range.End <= line.TextEnd);
    far_point.x = 30.0f;
    far_point.y = 2.0f;
    CHECK(ImTypeset_SelectParagraphRange(paragraph, &far_point, &point, ImTypeset_Granularity_Line,
                                         &range) == 1);
    CHECK(range.Start == 0 && range.End == line.TextEnd);

    /* Its rectangles: the count without a buffer, a prefix into a short one. */
    count = ImTypeset_GetParagraphRangeRects(paragraph, &range, NULL, 0);
    CHECK(count == 2);
    memset(rects, 0, sizeof rects);
    rects[1].x = -123.0f;
    CHECK(ImTypeset_GetParagraphRangeRects(paragraph, &range, rects, 1) == count);
    CHECK(rects[0].z > rects[0].x && rects[0].w > rects[0].y);
    CHECK(rects[1].x == -123.0f);
    CHECK(ImTypeset_GetParagraphRangeRects(paragraph, &range, rects, 8) == count);
    CHECK(rects[1].y >= rects[0].y && rects[1].z > rects[1].x);
    CHECK(ImTypeset_GetParagraphRangeRects(paragraph, NULL, rects, 8) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);

    CHECK(ImTypeset_GetParagraphCaretRect(paragraph, line.TextStart, &caret) == 1);
    CHECK(caret.x == caret.z && caret.w > caret.y);
    CHECK(caret.y >= line.Min.y - 1.0f && caret.w <= line.Max.y + 1.0f);

    /* Offsets in the paragraph's text are offsets in the text it was given. */
    CHECK(ImTypeset_ParagraphCodepointToByteOffset(paragraph, 60) ==
          ImTypeset_TextCodepointToByteOffset(TEXT, NULL, 60));
    CHECK(ImTypeset_ParagraphCodepointToByteOffset(paragraph, 60) == 61);
    CHECK(ImTypeset_ParagraphByteToCodepointOffset(paragraph, 61) == 60);
}

static void test_paragraph_copy(ImTypeset_Paragraph* paragraph, void* reference)
{
    char mine[1024];
    char theirs[1024];
    char small_buffer[128];
    ImTypeset_TextRange range;
    const char* cut;
    int length;
    int appended;
    int room;
    SECTION("copy");

    range.Start = 0;
    range.End = TEXT_LENGTH;
    length = ImTypeset_AppendParagraphRangeText(paragraph, &range, 0, NULL, 0);
    CHECK(length == TEXT_LENGTH);
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 0, mine, (int)sizeof mine) ==
          length);
    CHECK(strcmp(mine, TEXT) == 0);
    appended = c_api_host_reference_copy(reference, 0, TEXT_LENGTH, "", theirs, (int)sizeof theirs);
    CHECK(appended == length);
    CHECK(strcmp(mine, theirs) == 0);

    /* After other text: the blank line the C++ function puts between. */
    range.Start = 4;
    range.End = 19;
    length = ImTypeset_AppendParagraphRangeText(paragraph, &range, 1, mine, (int)sizeof mine);
    CHECK(strcmp(mine, "\n\nquick brown fox") == 0);
    CHECK(length == (int)strlen(mine));
    appended = c_api_host_reference_copy(reference, 4, 19, "abc", theirs, (int)sizeof theirs);
    CHECK(appended == length);
    CHECK(strncmp(theirs, "abc", 3) == 0 && strcmp(theirs + 3, mine) == 0);

    /* Nothing selected adds nothing, and no separator. */
    range.Start = 7;
    range.End = 7;
    mine[0] = 'x';
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 1, mine, (int)sizeof mine) == 0);
    CHECK(mine[0] == '\0');

    /* A buffer that ends inside the two-byte character is cut before it. */
    range.Start = 0;
    range.End = TEXT_LENGTH;
    cut = strstr(TEXT, "\xc3\xaf");
    REQUIRE(cut != NULL);
    room = (int)(cut - TEXT) + 2; /* The first of its two bytes, and the terminator. */
    REQUIRE(room < (int)sizeof small_buffer);
    memset(small_buffer, 'Z', sizeof small_buffer);
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, &range, 0, small_buffer, room) ==
          TEXT_LENGTH);
    CHECK((int)strlen(small_buffer) == room - 2);
    CHECK(strncmp(small_buffer, TEXT, (size_t)(room - 2)) == 0);
    CHECK(small_buffer[room] == 'Z');
    CHECK(ImTypeset_AppendParagraphRangeText(paragraph, NULL, 0, small_buffer, room) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
}

/* ImTypeset_DrawParagraphToBuffers() against ImTypeset::DrawParagraphToBuffers(). */
static void test_paragraph_buffers(ImTypeset_Paragraph* paragraph, void* reference)
{
    const size_t vert_size = (size_t)g_info.SizeofImDrawVert;
    const size_t idx_size = (size_t)g_info.SizeofImDrawIdx;
    const ImTypeset_Vec2 pos = {40.0f, 60.0f};
    const ImTypeset_Vec4 clip = {-10000.0f, -10000.0f, 10000.0f, 10000.0f};
    const float clip_numbers[4] = {-10000.0f, -10000.0f, 10000.0f, 10000.0f};
    const ImTypeset_TextRange selection = {4, 60}; /* On the first two lines. */
    unsigned char* mine_v;
    unsigned char* mine_i;
    unsigned char* theirs_v;
    unsigned char* theirs_i;
    int vertices = -1;
    int indices = -1;
    int their_vertices = -1;
    int their_indices = -1;
    int again_vertices = -1;
    int again_indices = -1;
    int half;
    SECTION("buffers");

    /* How much there is, with nowhere to put it. */
    CHECK(ImTypeset_DrawParagraphToBuffers(paragraph, &pos, &clip, NULL, 0, NULL, 0, NULL,
                                           &vertices, &indices) == 1);
    CHECK(last_error_code() == ImTypeset_Error_None);
    REQUIRE(vertices > 0 && indices > 0);
    CHECK(vertices % 4 == 0 && indices == vertices / 4 * 6);

    mine_v = (unsigned char*)malloc((size_t)vertices * vert_size + 16);
    mine_i = (unsigned char*)malloc((size_t)indices * idx_size + 16);
    theirs_v = (unsigned char*)malloc((size_t)vertices * vert_size + 16);
    theirs_i = (unsigned char*)malloc((size_t)indices * idx_size + 16);
    if (mine_v != NULL && mine_i != NULL && theirs_v != NULL && theirs_i != NULL) {
        memset(mine_v, 0xA5, (size_t)vertices * vert_size + 16);
        memset(mine_i, 0xA5, (size_t)indices * idx_size + 16);
        CHECK(ImTypeset_DrawParagraphToBuffers(paragraph, &pos, &clip, mine_v, vertices, mine_i,
                                               indices, NULL, &again_vertices,
                                               &again_indices) == 1);
        CHECK(again_vertices == vertices && again_indices == indices);
        CHECK(mine_v[(size_t)vertices * vert_size] == 0xA5);
        CHECK(mine_i[(size_t)indices * idx_size] == 0xA5);

        CHECK(c_api_host_reference_buffers(reference, pos.x, pos.y, clip_numbers, theirs_v,
                                           vertices, theirs_i, indices, &their_vertices,
                                           &their_indices) == 1);
        CHECK(their_vertices == vertices && their_indices == indices);
        CHECK(memcmp(mine_v, theirs_v, (size_t)vertices * vert_size) == 0);
        CHECK(memcmp(mine_i, theirs_i, (size_t)indices * idx_size) == 0);

        /* Room for half: the first half, the whole count, and not a failure. */
        half = vertices / 2;
        memset(mine_v, 0xA5, (size_t)vertices * vert_size);
        memset(mine_i, 0xA5, (size_t)indices * idx_size);
        CHECK(ImTypeset_DrawParagraphToBuffers(paragraph, &pos, &clip, mine_v, half, mine_i, 6,
                                               NULL, &again_vertices, &again_indices) == 1);
        CHECK(again_vertices == vertices && again_indices == indices);
        CHECK(memcmp(mine_v, theirs_v, (size_t)half * vert_size) == 0);
        CHECK(mine_v[(size_t)half * vert_size] == 0xA5);
        CHECK(memcmp(mine_i, theirs_i, 6 * idx_size) == 0);
        CHECK(mine_i[6 * idx_size] == 0xA5);
    } else {
        CHECK(!"malloc");
    }
    free(mine_v);
    free(mine_i);
    free(theirs_v);
    free(theirs_i);

    /* A selection is drawn into the buffers too: a rectangle for each line. */
    CHECK(ImTypeset_DrawParagraphToBuffers(paragraph, &pos, &clip, NULL, 0, NULL, 0, &selection,
                                           &again_vertices, &again_indices) == 1);
    CHECK(again_vertices == vertices + 8 && again_indices == indices + 12);

    /* Refused by the C++ function: false, and both counts zero. */
    again_vertices = again_indices = 99;
    CHECK(ImTypeset_DrawParagraphToBuffers(NULL, &pos, &clip, NULL, 0, NULL, 0, NULL,
                                           &again_vertices, &again_indices) == 0);
    CHECK(again_vertices == 0 && again_indices == 0);
    CHECK(last_error_code() != ImTypeset_Error_None);
    CHECK(ImTypeset_DrawParagraphToBuffers(paragraph, &pos, &clip, NULL, 0, NULL, 0, NULL, NULL,
                                           &again_indices) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
}

/* ImTypeset_DrawParagraph() against ImTypeset::DrawParagraph(), into one draw
 * list at one place: the same vertices, twice. */
static void test_paragraph_draw(ImTypeset_Paragraph* paragraph, void* reference,
                                struct ImDrawList* draw_list, int with_selection)
{
    const size_t vert_size = (size_t)g_info.SizeofImDrawVert;
    const ImTypeset_Vec2 pos = {30.0f, 50.0f};
    const ImTypeset_TextRange selection = {4, 120};
    unsigned char* mine;
    unsigned char* theirs;
    int before;
    int between;
    int after;
    int count;
    SECTION(with_selection ? "draw, selected" : "draw");

    before = c_api_host_draw_list_vertex_count(draw_list);
    CHECK(ImTypeset_DrawParagraph(paragraph, draw_list, &pos, with_selection ? &selection : NULL) ==
          1);
    CHECK(last_error_code() == ImTypeset_Error_None);
    between = c_api_host_draw_list_vertex_count(draw_list);
    CHECK(c_api_host_reference_draw(reference, draw_list, pos.x, pos.y, with_selection,
                                    selection.Start, selection.End) == 1);
    after = c_api_host_draw_list_vertex_count(draw_list);
    count = between - before;
    REQUIRE(count > 0);
    CHECK(after - between == count);

    mine = (unsigned char*)malloc((size_t)count * vert_size);
    theirs = (unsigned char*)malloc((size_t)count * vert_size);
    if (mine != NULL && theirs != NULL) {
        CHECK(c_api_host_draw_list_vertices(draw_list, before, mine, count) == after - before);
        CHECK(c_api_host_draw_list_vertices(draw_list, between, theirs, count) == count);
        CHECK(memcmp(mine, theirs, (size_t)count * vert_size) == 0);
    } else {
        CHECK(!"malloc");
    }
    free(mine);
    free(theirs);

    CHECK(ImTypeset_DrawParagraph(paragraph, draw_list, NULL, NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
    CHECK(c_api_host_draw_list_vertex_count(draw_list) == after);
}

static void test_layout_style(void)
{
    ImTypeset_Paragraph* paragraph;
    ImTypeset_Style style;
    int lines;
    SECTION("layout style");

    paragraph = ImTypeset_CreateParagraph();
    REQUIRE(paragraph != NULL);
    CHECK(ImTypeset_SetParagraphText(paragraph, TEXT, NULL, NULL, 0, 0) == 1);
    CHECK(ImTypeset_LayoutParagraph(paragraph, WIDTH, NULL) == 1);
    lines = ImTypeset_GetParagraphLineCount(paragraph);
    memset(&style, 0, sizeof style);
    style.EmSize = 30.0f;
    CHECK(ImTypeset_LayoutParagraph(paragraph, WIDTH, &style) == 1);
    CHECK(ImTypeset_GetParagraphLineCount(paragraph) > lines);
    /* Runs that are not there: a count with no pointer is no runs. */
    CHECK(ImTypeset_SetParagraphText(paragraph, TEXT, TEXT + 9, NULL, 5, 0) == 1);
    CHECK(ImTypeset_LayoutParagraph(paragraph, WIDTH, NULL) == 1);
    CHECK(ImTypeset_GetParagraphLineCount(paragraph) == 1);
    CHECK(ImTypeset_ParagraphCodepointToByteOffset(paragraph, 1000) == 9);
    CHECK(ImTypeset_DestroyParagraph(paragraph) == 1);
}

/* --- Formulas ------------------------------------------------------------------------ */

static void test_formulas(void)
{
    static const char source[] = "Set $x^2$ and not $\\begin{matrix}a\\end{matrix}$ here.";
    ImTypeset_Paragraph* paragraph;
    ImTypeset_Formula formula;
    ImTypeset_ErrorInfo error;
    char reason[512];
    char message[512];
    int length;
    SECTION("formulas");

    paragraph = ImTypeset_CreateParagraph();
    REQUIRE(paragraph != NULL);
    ImTypeset_SetParagraphText(paragraph, source, NULL, NULL, 0, ImTypeset_TextFlags_Math);
    /* One formula is refused: laid out, and false. */
    CHECK(ImTypeset_LayoutParagraph(paragraph, 400.0f, NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_MathRefused);
    CHECK(ImTypeset_GetParagraphLineCount(paragraph) >= 1);
    CHECK(ImTypeset_GetParagraphFormulaCount(paragraph) == 2);

    memset(&formula, 0xEE, sizeof formula);
    CHECK(ImTypeset_GetParagraphFormula(paragraph, 0, &formula) == 1);
    CHECK(formula.TextStart == 4 && formula.TextEnd == 9);
    CHECK(!formula.Display && !formula.Refused);
    CHECK(formula.Reason != NULL && formula.Reason[0] == '\0');
    CHECK(ImTypeset_GetParagraphFormulaReason(paragraph, 0, reason, (int)sizeof reason) == 0);

    CHECK(ImTypeset_GetParagraphFormula(paragraph, 1, &formula) == 1);
    CHECK(formula.TextStart == 18 && source[formula.TextEnd - 1] == '$');
    CHECK(formula.Refused);
    CHECK(formula.BytePos >= formula.TextStart && formula.BytePos < formula.TextEnd);
    CHECK(formula.Reason != NULL && formula.Reason[0] != '\0');
    length = ImTypeset_GetParagraphFormulaReason(paragraph, 1, reason, (int)sizeof reason);
    CHECK(length > 0 && length == (int)strlen(reason));
    CHECK(formula.Reason != NULL && strcmp(formula.Reason, reason) == 0);

    CHECK(ImTypeset_GetParagraphFormula(paragraph, 2, &formula) == 1);
    CHECK(formula.TextStart == 0 && formula.TextEnd == 0 && formula.Reason != NULL);

    CHECK(ImTypeset_GetParagraphError(paragraph, &error) == 1);
    CHECK(error.Code == ImTypeset_Error_MathRefused);
    CHECK(error.BytePos >= 18);
    length = ImTypeset_GetParagraphErrorMessage(paragraph, message, (int)sizeof message);
    CHECK(length > 0 && error.Message != NULL && strcmp(error.Message, message) == 0);
    /* Cut to fit, terminated, and the whole length answered. */
    CHECK(ImTypeset_GetParagraphErrorMessage(paragraph, message, 5) == length);
    CHECK(strlen(message) == 4 && strncmp(message, error.Message, 4) == 0);

    CHECK(ImTypeset_DestroyParagraph(paragraph) == 1);
}

/* --- Widgets ------------------------------------------------------------------------- */

static void test_widgets(int frame, struct ImDrawList* draw_list)
{
    static const char id_and_more[] = "status-line-and-more";
    ImTypeset_Style style;
    ImTypeset_TextRange selection;
    ImTypeset_Vec2 size;
    ImTypeset_Vec2 styled_size;
    float width = 0.0f;
    float height = 0.0f;
    int before;
    int selected;
    int unselected;
    SECTION("widgets");

    CHECK(ImTypeset_TextUnformatted(g_unterminated, g_unterminated + TEXT_LENGTH, 0, 0.0f, NULL) ==
          1);
    CHECK(last_error_code() == ImTypeset_Error_None);
    c_api_host_item_size(&width, &height);
    CHECK(width > 100.0f && height > 20.0f);

    memset(&style, 0, sizeof style);
    style.Align = ImTypeset_Align_Left;
    style.EmSize = 15.0f;
    CHECK(ImTypeset_TextUnformatted(TEXT, NULL, ImTypeset_TextFlags_None, 300.0f, &style) == 1);
    c_api_host_item_size(&width, &height);
    CHECK(width > 100.0f && width < 300.0f); /* Ragged right: the longest line. */
    style.Align = ImTypeset_Align_Justify;
    CHECK(ImTypeset_TextWithID("justified", NULL, TEXT, NULL, 0, 300.0f, &style) == 1);
    c_api_host_item_size(&width, &height);
    CHECK(width == 300.0f);

    CHECK(ImTypeset_Text("%d of %s", frame, "three") == 1);
    CHECK(ImTypeset_TextWithID(id_and_more, id_and_more + 11, frame % 2 ? "odd" : "even", NULL, 0,
                               0.0f, NULL) == 1);

    /* The selection is the caller's. Read at entry: the same text with a
     * selection draws one rectangle more than without. */
    before = c_api_host_draw_list_vertex_count(draw_list);
    selection.Start = 4;
    selection.End = 9;
    CHECK(ImTypeset_SelectableText("selectable", NULL, g_unterminated, g_unterminated + TEXT_LENGTH,
                                   0, 0.0f, NULL, &selection) == 1);
    CHECK(selection.Start == 4 && selection.End == 9);
    selected = c_api_host_draw_list_vertex_count(draw_list) - before;
    before += selected;
    selection.Start = 0;
    selection.End = 0;
    CHECK(ImTypeset_SelectableText("selectable-too", NULL, TEXT, NULL, 0, 0.0f, NULL, &selection) ==
          1);
    unselected = c_api_host_draw_list_vertex_count(draw_list) - before;
    if (frame == 2) {
        CHECK(unselected > 0);
        CHECK(selected == unselected + 4);
    }
    CHECK(ImTypeset_SelectableText("selectable-three", NULL, TEXT, NULL, 0, 0.0f, NULL, NULL) == 1);
    /* Written before return: a text whose face has no font is drawn by ImGui,
     * selects nothing, and says so. */
    memset(&style, 0, sizeof style);
    style.Face = ImTypeset_Face_Code;
    selection.Start = 4;
    selection.End = 9;
    CHECK(ImTypeset_SelectableText("selectable-four", NULL, TEXT, NULL, 0, 0.0f, &style,
                                   &selection) == 0);
    CHECK(last_error_code() == ImTypeset_Error_NoFont);
    CHECK(selection.Start == 0 && selection.End == 0);

    /* The size a text would have, against ImTypeset::CalcTextSize(). */
    size.x = size.y = -1.0f;
    CHECK(ImTypeset_CalcTextSize(g_unterminated, g_unterminated + TEXT_LENGTH, WIDTH, 0, NULL,
                                 &size) == 1);
    c_api_host_reference_calc(TEXT, NULL, WIDTH, 0, &width, &height);
    CHECK(size.x == width && size.y == height);
    CHECK(size.x > 0.0f && size.y > 0.0f);
    /* The style given with a call is the style of that call. */
    memset(&style, 0, sizeof style);
    style.EmSize = 30.0f;
    CHECK(ImTypeset_CalcTextSize(TEXT, NULL, WIDTH, 0, &style, &styled_size) == 1);
    CHECK(styled_size.y > 1.5f * size.y);
    /* An [E] function that answers a value: the status is its outcome. */
    style.Face = ImTypeset_Face_Code;
    styled_size.x = styled_size.y = -1.0f;
    CHECK(ImTypeset_CalcTextSize(TEXT, NULL, WIDTH, 0, &style, &styled_size) == 0);
    CHECK(last_error_code() == ImTypeset_Error_NoFont);
    CHECK(styled_size.x == 0.0f && styled_size.y == 0.0f);
    CHECK(ImTypeset_CalcTextSize(TEXT, NULL, WIDTH, 0, NULL, NULL) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
}

/* --- The whole of it ----------------------------------------------------------------- */

/* A document through the C ABI: blocks described in C structs, laid out
 * lazily, asked, copied across blocks (a formula and code among them) and
 * drawn both ways. The struct sizes were compared by the version check. */
static void test_document(struct ImDrawList* draw_list)
{
    static const char BODY[] = "A heading"
                               "Words, then \\frac{1}{2} as a formula, then a link."
                               "x = 1\n\ty = 2\n"
                               "widget"
                               "1."
                               "An item.";
    ImTypeset_Block blocks[5];
    ImTypeset_Run runs[2];
    ImTypeset_BlockStyle styles[2];
    ImTypeset_Container containers[1];
    ImTypeset_DocumentContent content;
    ImTypeset_DocumentOptions options;
    ImTypeset_Document* document;
    ImTypeset_DocumentStats stats;
    ImTypeset_BlockGeometry geometry;
    ImTypeset_BlockGeometry above;
    ImTypeset_DocumentHit hit;
    ImTypeset_DocumentRange range;
    ImTypeset_DocumentDraw draw;
    ImTypeset_Mark mark;
    ImTypeset_Position position;
    ImTypeset_ErrorInfo error;
    ImTypeset_Vec2 size;
    ImTypeset_Vec2 point;
    ImTypeset_Vec2 origin;
    ImTypeset_Vec4 caret;
    ImTypeset_Vec4 clip;
    ImTypeset_Vec4 rects[16];
    ImTypeset_Line line;
    ImTypeset_Run more;
    char text[512];
    char message[128];
    static unsigned char vertices[20 * 4000];
    static unsigned char indices[4 * 6000];
    int vertex_count = 0;
    int index_count = 0;
    int listed;
    int length;
    int block;

    SECTION("document");
    memset(blocks, 0, sizeof blocks);
    memset(runs, 0, sizeof runs);
    memset(styles, 0, sizeof styles);
    memset(containers, 0, sizeof containers);
    memset(&content, 0, sizeof content);
    memset(&options, 0, sizeof options);
    memset(&draw, 0, sizeof draw);
    memset(&mark, 0, sizeof mark);
    memset(&more, 0, sizeof more);

    styles[0].SpaceBelow = 8.0f;
    styles[0].Align = ImTypeset_Align_Left;
    styles[1] = styles[0];
    styles[1].Face = ImTypeset_Face_Bold;
    styles[1].Scale = 1.5f;
    containers[0].Kind = ImTypeset_ContainerKind_List;
    containers[0].Indent = 24.0f;

    blocks[0].Kind = ImTypeset_BlockKind_Heading;
    blocks[0].Style = 1;
    blocks[0].TextStart = 0;
    blocks[0].TextEnd = 9;
    blocks[1].Kind = ImTypeset_BlockKind_Paragraph;
    blocks[1].TextStart = 9;
    blocks[1].TextEnd = 9 + 50;
    blocks[1].RunStart = 0;
    blocks[1].RunCount = 2;
    runs[0].Start = 12;
    runs[0].End = 23;
    runs[0].Flags = ImTypeset_RunFlags_Formula;
    runs[1].Start = 45;
    runs[1].End = 49;
    runs[1].Tag = 42;
    blocks[2].Kind = ImTypeset_BlockKind_Code;
    blocks[2].TextStart = 59;
    blocks[2].TextEnd = 59 + 13;
    blocks[3].Kind = ImTypeset_BlockKind_Host;
    blocks[3].TextStart = 72;
    blocks[3].TextEnd = 78;
    blocks[3].Extent = 30.0f;
    blocks[4].Kind = ImTypeset_BlockKind_ListItem;
    blocks[4].MarkerStart = 78;
    blocks[4].MarkerEnd = 80;
    blocks[4].TextStart = 80;
    blocks[4].TextEnd = 88;
    blocks[4].LeftIndent = 24.0f;
    blocks[4].Container = 1;

    content.Text = BODY;
    content.TextSize = (int)(sizeof BODY - 1);
    content.Blocks = blocks;
    content.BlockCount = 5;
    content.Runs = runs;
    content.RunCount = 2;
    content.Styles = styles;
    content.StyleCount = 2;
    content.Containers = containers;
    content.ContainerCount = 1;

    CHECK(ImTypeset_SetDocumentBlocks(NULL, &content) == 0);
    CHECK(last_error_code() == ImTypeset_Error_InvalidArgument);
    document = ImTypeset_CreateDocument();
    REQUIRE(document != NULL);
    CHECK(ImTypeset_SetDocumentBlocks(document, &content) == 1);
    CHECK(ImTypeset_SetDocumentOptions(document, &options) == 1);
    CHECK(ImTypeset_GetDocumentBlockCount(document) == 5);
    CHECK(ImTypeset_GetDocumentStyleCount(document) == 2);
    CHECK(ImTypeset_GetDocumentContainerCount(document) == 1);
    CHECK(ImTypeset_GetDocumentBlockTextSize(document, 1) == 50);
    CHECK(ImTypeset_LayoutDocument(document, 400.0f, NULL) == 1);

    /* Nothing is laid out until something looks. */
    memset(&stats, 0xFF, sizeof stats);
    CHECK(ImTypeset_GetDocumentStats(document, &stats) == 1);
    CHECK(stats.Blocks == 5 && stats.BlocksDeferred == 5 && stats.BlocksLaidOut == 0);
    CHECK(ImTypeset_LayoutDocumentWindow(document, 0.0f, 100000.0f) == 1);
    CHECK(ImTypeset_GetDocumentStats(document, &stats) == 1);
    CHECK(stats.BlocksDeferred == 0 && stats.BlocksLaidOut == 5 && stats.MaterialBuilds == 4);
    CHECK(ImTypeset_GetDocumentStats(document, NULL) == 0);
    CHECK(ImTypeset_GetDocumentSize(document, &size) == 1);
    CHECK(size.x == 400.0f && size.y > 100.0f);
    CHECK(ImTypeset_GetDocumentBlockError(document, 1, &error) == 1);
    CHECK(error.Code == ImTypeset_Error_None);
    CHECK(ImTypeset_GetDocumentBlockErrorMessage(document, 1, message, (int)sizeof message) == 0);

    /* The blocks stack, and the host block is as tall as it said. */
    CHECK(ImTypeset_GetDocumentBlockGeometry(document, 0, &above) == 1);
    CHECK(above.LaidOut && above.FullMin.y == 0.0f);
    for (block = 1; block < 5; ++block) {
        CHECK(ImTypeset_GetDocumentBlockGeometry(document, block, &geometry) == 1);
        CHECK(geometry.LaidOut && geometry.FullMin.y >= above.FullMax.y - 0.01f);
        if (block == 3)
            CHECK(geometry.FullMax.y - geometry.FullMin.y > 29.99f &&
                  geometry.FullMax.y - geometry.FullMin.y < 30.01f);
        if (block == 4)
            CHECK(geometry.FullMin.x > 23.99f && geometry.FullMin.x < 24.01f);
        above = geometry;
    }
    CHECK(ImTypeset_GetDocumentBlockGeometry(document, 9, &geometry) == 1);
    CHECK(!geometry.LaidOut);
    CHECK(ImTypeset_GetDocumentBlockLine(document, 2, 1, &line) == 1);
    CHECK(line.TextStart == 6 && line.TextEnd == 12);

    /* A point on the tagged run answers the block, a byte in the run, the tag. */
    position.Block = 1;
    position.Offset = 46;
    CHECK(ImTypeset_GetDocumentCaretRect(document, &position, &caret) == 1);
    point.x = caret.x + 1.0f;
    point.y = (caret.y + caret.w) / 2.0f;
    CHECK(ImTypeset_HitTestDocument(document, &point, &hit) == 1);
    CHECK(hit.Position.Block == 1 && hit.Position.Offset >= 46 && hit.Position.Offset <= 47);
    CHECK(hit.Inside && hit.InBlock && hit.Tag == 42);
    CHECK(ImTypeset_HitTestDocument(document, NULL, &hit) == 0);

    /* A copy across blocks: the formula as its source, the code as its bytes,
     * the host block's text, the marker never, the separator as named. */
    range.Start.Block = 0;
    range.Start.Offset = 2;
    range.End.Block = 4;
    range.End.Offset = 8;
    length =
        ImTypeset_AppendDocumentRangeText(document, &range, NULL, NULL, text, (int)sizeof text);
    CHECK(length == (int)strlen(text));
    CHECK(strcmp(text, "heading\n\nWords, then \\frac{1}{2} as a formula, then a link.\n\n"
                       "x = 1\n\ty = 2\n\nwidget\n\nAn item.") == 0);
    length = ImTypeset_AppendDocumentRangeText(document, &range, " | ", NULL, NULL, 0);
    CHECK(length == (int)strlen(text) - 4 * 2 + 4 * 3);
    CHECK(ImTypeset_AppendDocumentRangeText(document, NULL, NULL, NULL, text, (int)sizeof text) ==
          0);
    listed = ImTypeset_GetDocumentRangeRects(document, &range, rects, 16);
    CHECK(listed >= 5 && listed <= 16);
    CHECK(ImTypeset_GetDocumentRangeRects(document, &range, NULL, 0) == listed);

    /* A drag from the heading into the paragraph. */
    origin.x = 5.0f;
    origin.y = 5.0f;
    CHECK(ImTypeset_SelectDocumentRange(document, &origin, &point, ImTypeset_Granularity_Character,
                                        &range) == 1);
    CHECK(range.Start.Block == 0 && range.End.Block == 1 && range.End.Offset >= 46);

    /* Drawn to buffers and to a draw list: the same number of vertices. */
    mark.Range.Start.Block = 1;
    mark.Range.Start.Offset = 45;
    mark.Range.End.Block = 1;
    mark.Range.End.Offset = 49;
    mark.Place = ImTypeset_MarkPlace_Under;
    mark.Color = 0xFFFF0000u;
    draw.Selection = range;
    draw.Marks = &mark;
    draw.MarkCount = 1;
    origin.x = 20.0f;
    origin.y = 40.0f;
    clip.x = 0.0f;
    clip.y = 0.0f;
    clip.z = 2000.0f;
    clip.w = 2000.0f;
    CHECK(ImTypeset_DrawDocumentToBuffers(document, &origin, &clip, vertices, 4000, indices, 6000,
                                          &draw, &vertex_count, &index_count) == 1);
    CHECK(vertex_count > 200 && vertex_count <= 4000);
    CHECK(index_count == vertex_count / 4 * 6);
    {
        int counted = 0;
        int counted_indices = 0;
        CHECK(ImTypeset_DrawDocumentToBuffers(document, &origin, &clip, NULL, 0, NULL, 0, &draw,
                                              &counted, &counted_indices) == 1);
        CHECK(counted == vertex_count && counted_indices == index_count);
    }
    {
        const int before = c_api_host_draw_list_vertex_count(draw_list);
        CHECK(ImTypeset_DrawDocument(document, draw_list, &origin, &draw) == 1);
        CHECK(c_api_host_draw_list_vertex_count(draw_list) - before == vertex_count);
        CHECK(ImTypeset_DrawDocument(document, draw_list, NULL, &draw) == 0);
    }

    /* Streaming: text appended to the last block lays out that block alone. */
    CHECK(ImTypeset_GetDocumentStats(document, &stats) == 1);
    {
        const ImTypeset_U64 laid_out = stats.BlocksLaidOut;
        const ImTypeset_U64 visits = stats.StructureVisits;
        more.Start = 9;
        more.End = 13;
        more.Face = ImTypeset_Face_Bold;
        CHECK(ImTypeset_AppendDocumentText(document, " More.", NULL, &more, 1) == 1);
        CHECK(ImTypeset_LayoutDocumentWindow(document, 0.0f, 100000.0f) == 1);
        CHECK(ImTypeset_GetDocumentStats(document, &stats) == 1);
        CHECK(stats.BlocksLaidOut == laid_out + 1);
        CHECK(stats.StructureVisits == visits);
        CHECK(ImTypeset_GetDocumentBlockTextSize(document, 4) == 14);
    }
    /* Replacing: the host block grows, a block is removed, one is added. */
    CHECK(ImTypeset_SetDocumentBlockExtent(document, 3, 60.0f) == 1);
    CHECK(ImTypeset_SetDocumentBlockExtent(document, 1, 60.0f) == 0);
    CHECK(ImTypeset_ReplaceDocumentBlocks(document, 0, 1, NULL) == 1);
    CHECK(ImTypeset_GetDocumentBlockCount(document) == 4);
    content.BlockCount = 1;
    content.StyleCount = 0;
    content.ContainerCount = 0;
    CHECK(ImTypeset_ReplaceDocumentBlocks(document, 4, 0, &content) == 1);
    CHECK(ImTypeset_GetDocumentBlockCount(document) == 5);
    CHECK(ImTypeset_ReplaceDocumentBlocks(document, 9, 0, &content) == 0);
    range.Start.Block = 4;
    range.Start.Offset = 0;
    range.End.Block = 4;
    range.End.Offset = 100;
    ImTypeset_AppendDocumentRangeText(document, &range, NULL, NULL, text, (int)sizeof text);
    CHECK(strcmp(text, "A heading") == 0);

    /* The widget, inside the frame's window. */
    CHECK(ImTypeset_DocumentView("doc", NULL, document, NULL, NULL, NULL, NULL) == 1);
    CHECK(ImTypeset_DocumentView("doc", NULL, NULL, NULL, NULL, NULL, NULL) == 0);

    /* An append that is applied says so, whatever an earlier one found: a run
     * that is not after the block's runs is left out and said by the call that
     * handed it over, as something done differently, and by no later call. */
    more.Start = 9;
    more.End = 10;
    CHECK(ImTypeset_AppendDocumentText(document, "x", NULL, &more, 1) == 1);
    more.Start = 2;
    more.End = 4;
    CHECK(ImTypeset_AppendDocumentText(document, "y", NULL, &more, 1) == 0);
    CHECK(ImTypeset_GetLastError(&error) == 1);
    CHECK(error.Code == ImTypeset_Error_DescriptionCut);
    CHECK(ImTypeset_GetDocumentBlockTextSize(document, 4) == 11);
    CHECK(ImTypeset_AppendDocumentText(document, "z", NULL, NULL, 0) == 1);
    CHECK(ImTypeset_GetLastError(&error) == 1);
    CHECK(error.Code == ImTypeset_Error_None);
    CHECK(ImTypeset_GetDocumentBlockTextSize(document, 4) == 12);
    CHECK(ImTypeset_GetDocumentBlockError(document, 4, &error) == 1);
    CHECK(error.Code == ImTypeset_Error_DescriptionCut);

    CHECK(ImTypeset_DestroyDocument(document) == 1);
    CHECK(ImTypeset_DestroyDocument(NULL) == 1);
}

static void test_with_a_context(void)
{
    ImTypeset_Context* context;
    ImTypeset_Paragraph* paragraph;
    ImTypeset_Stats stats;
    void* reference;
    int frame;

    SECTION("context");
    REQUIRE(c_api_host_create() == 1);
    context = ImTypeset_CreateContext();
    REQUIRE(context != NULL);
    CHECK(last_error_code() == ImTypeset_Error_None);
    CHECK(ImTypeset_GetCurrentContext() == context);
    CHECK(ImTypeset_SetCurrentContext(NULL) == 1);
    CHECK(ImTypeset_GetCurrentContext() == NULL);
    CHECK(ImTypeset_GetContextSerial() == 0);
    CHECK(ImTypeset_SetCurrentContext(context) == 1);
    /* A context's serial: its own, never zero, and not one a later context
     * has, wherever that one is made. */
    {
        const ImTypeset_U64 serial = ImTypeset_GetContextSerial();
        ImTypeset_Context* other = ImTypeset_CreateContext();
        ImTypeset_U64 others;
        CHECK(serial != 0);
        REQUIRE(other != NULL);
        CHECK(ImTypeset_GetContextSerial() == serial);
        CHECK(ImTypeset_SetCurrentContext(other) == 1);
        others = ImTypeset_GetContextSerial();
        CHECK(others != 0 && others != serial);
        CHECK(ImTypeset_DestroyContext(other) == 1);
        CHECK(ImTypeset_GetContextSerial() == 0);
        other = ImTypeset_CreateContext();
        REQUIRE(other != NULL);
        CHECK(ImTypeset_GetContextSerial() != serial);
        CHECK(ImTypeset_GetContextSerial() != others);
        CHECK(ImTypeset_GetContextSerial() != 0);
        CHECK(ImTypeset_DestroyContext(other) == 1);
        CHECK(ImTypeset_SetCurrentContext(context) == 1);
        CHECK(ImTypeset_GetContextSerial() == serial);
    }

    test_style();
    test_fonts();

    /* The paragraph through the C ABI, from text that is not terminated, and
     * the same through the C++ API. */
    SECTION("paragraph");
    paragraph = ImTypeset_CreateParagraph();
    REQUIRE(paragraph != NULL);
    CHECK(ImTypeset_SetParagraphText(paragraph, g_unterminated, g_unterminated + TEXT_LENGTH, RUNS,
                                     RUN_COUNT, ImTypeset_TextFlags_None) == 1);
    CHECK(ImTypeset_GetParagraphStatus(paragraph) == ImTypeset_StatusFlags_None);
    CHECK(ImTypeset_LayoutParagraph(paragraph, WIDTH, NULL) == 1);
    CHECK(last_error_code() == ImTypeset_Error_None);
    reference = c_api_host_reference_paragraph(TEXT, NULL, RUNS_AS_NUMBERS, RUN_COUNT, 0, WIDTH);
    REQUIRE(reference != NULL);

    test_paragraph_geometry(paragraph, reference);
    test_paragraph_copy(paragraph, reference);
    test_exception_is_caught(paragraph);
    test_layout_style();
    test_formulas();

    /* A window does not draw in the frame it first appears in. */
    for (frame = 0; frame < 3; ++frame) {
        struct ImDrawList* draw_list = c_api_host_begin_frame();
        test_widgets(frame, draw_list);
        if (frame == 2) {
            test_paragraph_draw(paragraph, reference, draw_list, 0);
            test_paragraph_draw(paragraph, reference, draw_list, 1);
            test_paragraph_buffers(paragraph, reference);
            test_document(draw_list);
        }
        c_api_host_end_frame();
    }

    SECTION("stats");
    memset(&stats, 0, sizeof stats);
    CHECK(ImTypeset_GetStats(&stats) == 1);
    CHECK(stats.Layouts > 0 && stats.TextBuilds > 0);
    CHECK(stats.DrawCalls > 0 && stats.VerticesEmitted > 0);
    CHECK(stats.CachedParagraphs > 0);
    CHECK(ImTypeset_GetStats(NULL) == 0);

    SECTION("teardown");
    c_api_host_reference_destroy(reference);
    CHECK(ImTypeset_DestroyParagraph(paragraph) == 1);
    CHECK(ImTypeset_DestroyContext(NULL) == 1);
    CHECK(ImTypeset_GetCurrentContext() == NULL);
    c_api_host_destroy();
}

int main(void)
{
    memcpy(g_unterminated, TEXT, (size_t)TEXT_LENGTH);
    memcpy(g_unterminated + TEXT_LENGTH, "$x$ junk", 8);

    test_build_info();
    test_without_a_context();
    test_version_check();
    test_with_a_context();

    printf("test_c_api: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
