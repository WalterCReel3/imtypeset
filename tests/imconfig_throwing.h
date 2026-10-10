// A test-only IMGUI_USER_CONFIG: IM_ASSERT throws instead of aborting, as it
// does in a build of Dear ImGui made for a scripting host that has to survive
// an assertion (imgui_bundle's Python build defines it so). A build made with
// -DIMTYPESET_ENABLE_EXCEPTIONS=ON -DIMTYPESET_IMGUI_USER_CONFIG=<this file>
// compiles ImGui, the library and every test with it, and
// tests/test_exception_safety.cpp then throws through the library on purpose.
//
// Never a host's configuration: nothing outside tests/ includes this.

#pragma once

// No standard header is included: tests/test_header_pod.cpp holds that
// imtypeset.h brings none in, and this file is in front of it.
namespace imtypeset_test
{

// What a failed IM_ASSERT throws: the expression as written, and where.
struct AssertionFailure {
    const char* expression;
    const char* file;
    int line;
};

} // namespace imtypeset_test

#define IMTYPESET_TEST_IM_ASSERT_THROWS 1
#define IM_ASSERT(_EXPR)                                                          \
    do {                                                                          \
        if (!(_EXPR))                                                             \
            throw ::imtypeset_test::AssertionFailure{#_EXPR, __FILE__, __LINE__}; \
    } while (0)
