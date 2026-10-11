// The C ABI covers the public API, declaration for declaration.
//
// src/imtypeset_c.cpp proves at compile time that what imtypeset_c.h mirrors is
// mirrored exactly: sizes, offsets, field types, enumerator values. What it
// cannot see is a declaration that was ADDED to imtypeset.h and never given a C
// form. This test can: it reads both headers as text, lists their functions,
// enumerators, structs and fields, and holds the two lists to the table below.
//
//   - A row names a C++ function and its C counterpart and takes the address
//     of both, so a function removed or renamed in either header does not
//     compile here.
//   - A function, enumerator, struct or field added to imtypeset.h without a
//     counterpart fails the test, by name, when it is run.
//
// The headers are read from the source tree (IMTYPESET_SOURCE_DIR), which is
// where a build's own copies are.

#include "imtypeset.h"
#include "imtypeset_c.h"

#include "golden.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{

using AnyFn = void (*)();

struct Row {
    const char* cpp; // In namespace ImTypeset; null for a function only the C ABI has.
    const char* c;
    AnyFn cpp_fn;
    AnyFn c_fn;
    const char* why_c_only;
};

#define BOTH(Name)                                                              \
    {                                                                           \
#Name, "ImTypeset_" #Name, reinterpret_cast < AnyFn>(&ImTypeset::Name), \
            reinterpret_cast < AnyFn>(&ImTypeset_##Name), nullptr               \
    }
#define C_ONLY(Name, why)                                                                     \
    {                                                                                         \
        nullptr, "ImTypeset_" #Name, nullptr, reinterpret_cast<AnyFn>(&ImTypeset_##Name), why \
    }

const Row TABLE[] = {
    BOTH(DebugCheckVersionAndDataLayout),
    BOTH(CreateContext),
    BOTH(DestroyContext),
    BOTH(GetCurrentContext),
    BOTH(SetCurrentContext),
    BOTH(GetContextSerial),
    BOTH(GetVersion),
    BOTH(GetImGuiVersionNum),
    BOTH(GetLastError),
    BOTH(GetStats),
    BOTH(GetStyle),
    BOTH(SetTextCacheLimits),
    BOTH(AddFontFromFileTTF),
    BOTH(AddFontFromMemoryTTF),
    BOTH(RemoveFont),
    BOTH(GetFont),
    BOTH(TextUnformatted),
    BOTH(Text),
    BOTH(TextV),
    BOTH(TextWithID),
    BOTH(SelectableText),
    BOTH(CalcTextSize),
    BOTH(CreateParagraph),
    BOTH(DestroyParagraph),
    BOTH(SetParagraphText),
    BOTH(LayoutParagraph),
    BOTH(GetParagraphError),
    BOTH(GetParagraphStatus),
    BOTH(GetParagraphFormulaCount),
    BOTH(GetParagraphFormula),
    BOTH(GetParagraphSize),
    BOTH(GetParagraphLineCount),
    BOTH(GetParagraphLine),
    BOTH(DrawParagraph),
    BOTH(DrawParagraphToBuffers),
    BOTH(HitTestParagraph),
    BOTH(SelectParagraphRange),
    BOTH(GetParagraphRangeRects),
    BOTH(GetParagraphCaretRect),
    BOTH(AppendParagraphRangeText),
    BOTH(TextCodepointToByteOffset),
    BOTH(TextByteToCodepointOffset),
    BOTH(ParagraphCodepointToByteOffset),
    BOTH(ParagraphByteToCodepointOffset),
    BOTH(DebugCheckDocumentDataLayout),
    BOTH(CreateDocument),
    BOTH(DestroyDocument),
    BOTH(SetDocumentBlocks),
    BOTH(ReplaceDocumentBlocks),
    BOTH(AppendDocumentText),
    BOTH(SetDocumentBlockExtent),
    BOTH(SetDocumentOptions),
    BOTH(GetDocumentBlockCount),
    BOTH(GetDocumentStyleCount),
    BOTH(GetDocumentContainerCount),
    BOTH(GetDocumentBlockTextSize),
    BOTH(LayoutDocument),
    BOTH(LayoutDocumentWindow),
    BOTH(GetDocumentSize),
    BOTH(GetDocumentBlockError),
    BOTH(GetDocumentStats),
    BOTH(DrawDocument),
    BOTH(DrawDocumentToBuffers),
    BOTH(HitTestDocument),
    BOTH(SelectDocumentRange),
    BOTH(GetDocumentRangeRects),
    BOTH(GetDocumentCaretRect),
    BOTH(AppendDocumentRangeText),
    BOTH(GetDocumentBlockGeometry),
    BOTH(GetDocumentBlockLine),
    BOTH(DocumentView),
    BOTH(ShowDemoWindow),
    C_ONLY(GetBuildInfo, "what a binding compares with its own ImGui before anything else"),
    C_ONLY(SetStyle, "GetStyle() is a reference in C++ and a copy here"),
    C_ONLY(GetLastErrorMessage, "ImTypesetErrorInfo::Message, into the caller's buffer"),
    C_ONLY(GetParagraphErrorMessage, "ImTypesetErrorInfo::Message, into the caller's buffer"),
    C_ONLY(GetParagraphFormulaReason, "ImTypesetFormula::Reason, into the caller's buffer"),
    C_ONLY(GetDocumentBlockErrorMessage, "ImTypesetErrorInfo::Message, into the caller's buffer"),
};

// Enumerators and structs imtypeset_c.h has and imtypeset.h does not.
const char* const C_ONLY_ENUMERATORS[] = {"ImTypeset_Error_Exception"};
const char* const C_ONLY_STRUCTS[] = {"ImTypeset_Vec2", "ImTypeset_Vec4", "ImTypeset_BuildInfo"};

// --- Reading a header ---------------------------------------------------------------

bool is_word(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The text with comments and preprocessor lines (continuations included) made
// blank, so that what is left is declarations.
std::string declarations_of(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    bool line_start = true;
    while (i < text.size()) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n')
                ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            const size_t end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out += ' ';
        } else if (line_start && text[i] == '#') {
            while (i < text.size() && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\n')
                    ++i;
                ++i;
            }
        } else {
            if (text[i] == '\n')
                line_start = true;
            else if (text[i] != ' ' && text[i] != '\t')
                line_start = false;
            out += text[i++];
        }
    }
    return out;
}

std::string trimmed(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0)
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0)
        --b;
    return s.substr(a, b - a);
}

// The last identifier of `s`, before any array bound.
std::string last_word(std::string s)
{
    const size_t bracket = s.find('[');
    if (bracket != std::string::npos)
        s.resize(bracket);
    s = trimmed(s);
    size_t a = s.size();
    while (a > 0 && is_word(s[a - 1]))
        --a;
    return s.substr(a);
}

// Every function declared after `marker`: the identifier before its
// parameter list.
std::vector<std::string> functions_of(const std::string& decls, const std::string& marker)
{
    std::vector<std::string> out;
    size_t at = 0;
    while ((at = decls.find(marker, at)) != std::string::npos) {
        const bool whole =
            (at == 0 || !is_word(decls[at - 1])) &&
            (at + marker.size() >= decls.size() || !is_word(decls[at + marker.size()]));
        at += marker.size();
        if (!whole)
            continue;
        const size_t paren = decls.find('(', at);
        if (paren == std::string::npos)
            break;
        out.push_back(last_word(decls.substr(at, paren - at)));
    }
    return out;
}

// "3", "1 << 2".
bool value_of(const std::string& expression, long* out)
{
    const size_t shift = expression.find("<<");
    char* end = nullptr;
    if (shift == std::string::npos) {
        const std::string text = trimmed(expression);
        *out = std::strtol(text.c_str(), &end, 0);
        return !text.empty() && *end == '\0';
    }
    const std::string left = trimmed(expression.substr(0, shift));
    const std::string right = trimmed(expression.substr(shift + 2));
    const long a = std::strtol(left.c_str(), &end, 0);
    if (left.empty() || *end != '\0')
        return false;
    const long b = std::strtol(right.c_str(), &end, 0);
    if (right.empty() || *end != '\0' || b < 0 || b > 30)
        return false;
    *out = a << b;
    return true;
}

// Every enumerator of every `enum Name { ... }`, with its value.
bool enumerators_of(const std::string& decls, std::vector<std::pair<std::string, long>>* out)
{
    size_t at = 0;
    while ((at = decls.find("enum ", at)) != std::string::npos) {
        const size_t open = decls.find('{', at);
        const size_t semi = decls.find(';', at);
        at += 5;
        if (open == std::string::npos || (semi != std::string::npos && semi < open))
            continue;
        const size_t close = decls.find('}', open);
        if (close == std::string::npos)
            return false;
        long next = 0;
        size_t item = open + 1;
        while (item < close) {
            size_t comma = decls.find(',', item);
            if (comma == std::string::npos || comma > close)
                comma = close;
            const std::string entry = trimmed(decls.substr(item, comma - item));
            item = comma + 1;
            if (entry.empty())
                continue;
            const size_t equals = entry.find('=');
            long value = next;
            if (equals != std::string::npos && !value_of(entry.substr(equals + 1), &value))
                return false;
            out->emplace_back(trimmed(entry.substr(0, equals)), value);
            next = value + 1;
        }
        at = close;
    }
    return true;
}

struct StructDecl {
    std::string name;
    std::vector<std::string> fields;
};

// Every `struct Name { ... }` with its data members in order. A member
// function's declarator and body hold no ';' at the struct's own depth, which
// is how a constructor is told from a field.
std::vector<StructDecl> structs_of(const std::string& decls)
{
    std::vector<StructDecl> out;
    size_t at = 0;
    while ((at = decls.find("struct ", at)) != std::string::npos) {
        const bool whole = at == 0 || !is_word(decls[at - 1]);
        at += 7;
        if (!whole)
            continue;
        size_t end = at;
        while (end < decls.size() && is_word(decls[end]))
            ++end;
        size_t open = end;
        while (open < decls.size() && std::isspace(static_cast<unsigned char>(decls[open])) != 0)
            ++open;
        if (end == at || open >= decls.size() || decls[open] != '{')
            continue;
        StructDecl decl;
        decl.name = decls.substr(at, end - at);
        std::string statement;
        int depth = 1;
        size_t i = open + 1;
        for (; i < decls.size() && depth > 0; ++i) {
            const char c = decls[i];
            if (c == '{') {
                ++depth;
                statement.clear();
            } else if (c == '}') {
                --depth;
                statement.clear();
            } else if (depth == 1 && c == ';') {
                decl.fields.push_back(last_word(statement));
                statement.clear();
            } else if (depth == 1) {
                statement += c;
            }
        }
        out.push_back(decl);
        at = i;
    }
    return out;
}

// ImTypesetStyle -> ImTypeset_Style, ImTypesetFace_Bold -> ImTypeset_Face_Bold.
std::string c_name_of(const std::string& cpp)
{
    const std::string prefix = "ImTypeset";
    if (cpp.compare(0, prefix.size(), prefix) != 0)
        return std::string();
    return prefix + "_" + cpp.substr(prefix.size());
}

struct Headers {
    std::string cpp_text;
    std::string c_text;
    std::string cpp;
    std::string c;
    bool ok = false;
};

const Headers& headers()
{
    static Headers h;
    if (!h.ok) {
        const std::string root = IMTYPESET_SOURCE_DIR;
        h.ok = golden::read_file(root + "/include/imtypeset.h", h.cpp_text) &&
               golden::read_file(root + "/include/imtypeset_c.h", h.c_text);
        h.cpp = declarations_of(h.cpp_text);
        h.c = declarations_of(h.c_text);
    }
    return h;
}

template<typename List>
bool contains(const List& list, const std::string& name)
{
    for (const auto& entry : list)
        if (name == entry)
            return true;
    return false;
}

} // namespace

TEST(every_public_function_has_a_c_counterpart)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    const std::vector<std::string> cpp = functions_of(h.cpp, "IMTYPESET_API");
    // A reader that found nothing must not read as a pass: the header declares
    // forty-three functions as this is written, and only ever more.
    REQUIRE(cpp.size() >= 43u);
    size_t rows = 0;
    for (const std::string& name : cpp) {
        CHECK_CONTEXT("ImTypeset::%s", name.c_str());
        bool found = false;
        for (const Row& row : TABLE)
            found = found || (row.cpp != nullptr && name == row.cpp);
        if (!found)
            FAIL("ImTypeset::%s is declared in imtypeset.h and has no row in tests/"
                 "test_c_api_map.cpp: give it a C form in imtypeset_c.h and a row",
                 name.c_str());
    }
    for (const Row& row : TABLE) {
        if (row.cpp == nullptr)
            continue;
        ++rows;
        CHECK_CONTEXT("row %s", row.cpp);
        CHECK(contains(cpp, row.cpp));
        CHECK(row.cpp_fn != nullptr);
        CHECK(row.c_fn != nullptr);
        CHECK_EQ(std::string(row.c), std::string("ImTypeset_") + row.cpp);
    }
    CHECK_EQ(rows, cpp.size());
    std::printf("test_c_api_map: %zu C++ functions, each with a C counterpart\n", cpp.size());
}

TEST(every_c_function_is_accounted_for)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    const std::vector<std::string> c = functions_of(h.c, "IMTYPESET_C_API");
    REQUIRE(c.size() >= 43u);
    size_t c_only = 0;
    for (const std::string& name : c) {
        CHECK_CONTEXT("%s", name.c_str());
        bool found = false;
        for (const Row& row : TABLE)
            found = found || name == row.c;
        if (!found)
            FAIL("%s is declared in imtypeset_c.h and has no row", name.c_str());
    }
    for (const Row& row : TABLE) {
        CHECK_CONTEXT("row %s", row.c);
        CHECK(contains(c, row.c));
        if (row.cpp == nullptr) {
            ++c_only;
            CHECK(row.why_c_only != nullptr && row.why_c_only[0] != '\0');
        }
    }
    CHECK_EQ(c.size(), sizeof(TABLE) / sizeof(TABLE[0]));
    std::printf("test_c_api_map: %zu C functions, %zu of them the C ABI's own\n", c.size(), c_only);
}

TEST(every_enumerator_has_its_c_twin_with_the_same_value)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    std::vector<std::pair<std::string, long>> cpp;
    std::vector<std::pair<std::string, long>> c;
    REQUIRE(enumerators_of(h.cpp, &cpp));
    REQUIRE(enumerators_of(h.c, &c));
    REQUIRE(cpp.size() >= 40u);
    std::map<std::string, long> c_values;
    for (const auto& entry : c)
        c_values[entry.first] = entry.second;
    CHECK_EQ(c_values.size(), c.size());
    for (const auto& entry : cpp) {
        CHECK_CONTEXT("%s", entry.first.c_str());
        const auto twin = c_values.find(c_name_of(entry.first));
        if (twin == c_values.end()) {
            FAIL("%s has no %s in imtypeset_c.h", entry.first.c_str(),
                 c_name_of(entry.first).c_str());
            continue;
        }
        CHECK_EQ(twin->second, entry.second);
        c_values.erase(twin);
    }
    // What is left is the C ABI's own, and each is listed above.
    for (const auto& entry : c_values) {
        CHECK_CONTEXT("%s", entry.first.c_str());
        CHECK(contains(C_ONLY_ENUMERATORS, entry.first));
    }
    CHECK_EQ(c_values.size(), sizeof(C_ONLY_ENUMERATORS) / sizeof(C_ONLY_ENUMERATORS[0]));
    // The parser's reading of the values, against the compiler's.
    for (const auto& entry : cpp) {
        if (entry.first == "ImTypesetFace_COUNT")
            CHECK_EQ(entry.second, static_cast<long>(ImTypesetFace_COUNT));
        if (entry.first == "ImTypesetTextFlags_Math")
            CHECK_EQ(entry.second, static_cast<long>(ImTypesetTextFlags_Math));
        if (entry.first == "ImTypesetError_BackendUnsupported")
            CHECK_EQ(entry.second, static_cast<long>(ImTypesetError_BackendUnsupported));
    }
    std::printf("test_c_api_map: %zu enumerators\n", cpp.size());
}

TEST(every_struct_has_its_c_twin_with_the_same_fields)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    const std::vector<StructDecl> cpp = structs_of(h.cpp);
    const std::vector<StructDecl> c = structs_of(h.c);
    REQUIRE(cpp.size() >= 8u);
    size_t fields = 0;
    for (const StructDecl& decl : cpp) {
        CHECK_CONTEXT("struct %s", decl.name.c_str());
        REQUIRE(!decl.fields.empty());
        const StructDecl* twin = nullptr;
        for (const StructDecl& candidate : c)
            if (candidate.name == c_name_of(decl.name))
                twin = &candidate;
        if (twin == nullptr) {
            FAIL("struct %s has no %s in imtypeset_c.h", decl.name.c_str(),
                 c_name_of(decl.name).c_str());
            continue;
        }
        CHECK_EQ(twin->fields.size(), decl.fields.size());
        for (size_t i = 0; i < decl.fields.size() && i < twin->fields.size(); ++i) {
            CHECK_CONTEXT("field %zu", i);
            CHECK_EQ(twin->fields[i], decl.fields[i]);
            ++fields;
        }
    }
    for (const StructDecl& decl : c) {
        CHECK_CONTEXT("struct %s", decl.name.c_str());
        bool mirrored = false;
        for (const StructDecl& candidate : cpp)
            mirrored = mirrored || c_name_of(candidate.name) == decl.name;
        CHECK(mirrored || contains(C_ONLY_STRUCTS, decl.name));
    }
    // The parser's reading against the compiler's, for the struct with a
    // constructor in it.
    for (const StructDecl& decl : cpp)
        if (decl.name == "ImTypesetStyle") {
            CHECK_EQ(decl.fields.size(), 12u);
            CHECK_EQ(decl.fields.front(), std::string("Face"));
            CHECK_EQ(decl.fields.back(), std::string("SelectionColor"));
        }
    std::printf("test_c_api_map: %zu structs, %zu fields\n", cpp.size(), fields);
}

TEST(every_enum_type_has_its_c_typedef)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    int found = 0;
    size_t at = 0;
    const std::string marker = "typedef int ImTypeset";
    while ((at = h.cpp.find(marker, at)) != std::string::npos) {
        const size_t semi = h.cpp.find(';', at);
        REQUIRE(semi != std::string::npos);
        const std::string name = last_word(h.cpp.substr(at, semi - at));
        CHECK_CONTEXT("%s", name.c_str());
        CHECK(h.c.find("typedef int " + c_name_of(name) + ";") != std::string::npos);
        ++found;
        at = semi;
    }
    CHECK_EQ(found, 16);
}

TEST(the_two_headers_are_one_version)
{
    CHECK_EQ(std::string(IMTYPESET_C_VERSION), std::string(IMTYPESET_VERSION));
    CHECK_EQ(IMTYPESET_C_VERSION_NUM, IMTYPESET_VERSION_NUM);
    CHECK_EQ(std::string(ImTypeset_GetVersion()), std::string(ImTypeset::GetVersion()));
    CHECK_EQ(ImTypeset_GetImGuiVersionNum(), ImTypeset::GetImGuiVersionNum());
}

// Nothing of C++ or of ImGui: three C headers, and stdbool.h for C alone.
TEST(the_c_header_includes_c_headers_only)
{
    const Headers& h = headers();
    REQUIRE(h.ok);
    std::vector<std::string> includes;
    size_t at = 0;
    while ((at = h.c_text.find("#include", at)) != std::string::npos) {
        const size_t end = h.c_text.find('\n', at);
        includes.push_back(trimmed(h.c_text.substr(at + 8, end - at - 8)));
        at = end == std::string::npos ? h.c_text.size() : end;
    }
    REQUIRE_EQ(includes.size(), 3u);
    CHECK_EQ(includes[0], std::string("<stdarg.h>"));
    CHECK_EQ(includes[1], std::string("<stddef.h>"));
    CHECK_EQ(includes[2], std::string("<stdbool.h>"));
    CHECK(h.c.find("ImVector") == std::string::npos);
    CHECK(h.c.find("namespace") == std::string::npos);
    CHECK(h.c.find('&') == std::string::npos);
}
