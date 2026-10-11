# One executable per tests/test_*.cpp, plus smoke.cpp, each a ctest test of the
# same name that fails by returning non-zero. Found by glob: adding a test is
# adding a file. tests/check.h is the harness and supplies main().
file(GLOB _imtypeset_all_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(APPEND _imtypeset_all_tests "${CMAKE_CURRENT_SOURCE_DIR}/smoke.cpp")
# test_c_api is the one test a C compiler builds; it is selected by name like
# the others and built further down.
set(_imtypeset_c_api_test "${CMAKE_CURRENT_SOURCE_DIR}/test_c_api.c")
list(APPEND _imtypeset_all_tests "${_imtypeset_c_api_test}")
imtypeset_select(_imtypeset_tests "test" "${IMTYPESET_TESTS}" ${_imtypeset_all_tests})

# The C ABI, used from C: tests/test_c_api.c is compiled as C99 and sees
# include/imtypeset_c.h and nothing else of this tree or of ImGui. The headless
# ImGui frame it drives, and the C++ API's answers it compares with, are in
# c_api_host.cpp, compiled as every other test is and linked in.
if(_imtypeset_c_api_test IN_LIST _imtypeset_tests)
    list(REMOVE_ITEM _imtypeset_tests "${_imtypeset_c_api_test}")
    enable_language(C)
    add_library(c_api_host STATIC "${CMAKE_CURRENT_SOURCE_DIR}/c_api_host.cpp")
    target_link_libraries(c_api_host PRIVATE imtypeset::imtypeset imtypeset_build_flags)
    target_compile_definitions(c_api_host PRIVATE
        "IMTYPESET_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/tests/data\"")
    add_executable(test_c_api "${_imtypeset_c_api_test}")
    set_target_properties(test_c_api PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
    target_include_directories(test_c_api PRIVATE "${PROJECT_SOURCE_DIR}/include")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # -ffp-contract=off as on every C++ source here (D16): imtypeset_build_flags
        # carries C++-only options and is not linked to a C target.
        target_compile_options(test_c_api PRIVATE -Wall -Wextra -Wpedantic -Werror
            -ffp-contract=off)
    endif()
    target_link_libraries(test_c_api PRIVATE c_api_host)
    add_test(NAME test_c_api COMMAND test_c_api)
    set_tests_properties(test_c_api PROPERTIES TIMEOUT 600)
endif()

foreach(_source IN LISTS _imtypeset_tests)
    get_filename_component(_name "${_source}" NAME_WE)
    imtypeset_program(${_name} "${_source}")
    add_test(NAME ${_name} COMMAND ${_name})
    # Generous: the sanitiser build is several times slower, and a hang should
    # still end.
    set_tests_properties(${_name} PROPERTIES TIMEOUT 600)
endforeach()

if(IMTYPESET_TESTS)
    message(STATUS "imtypeset: tests narrowed to [${IMTYPESET_TESTS}]")
endif()

# --- The drop-in files and the host projects ---------------------------------------
# Neither tests a module, so neither is in IMTYPESET_TESTS: both need the whole
# tree, and are left out of a narrowed build.
#
# amalgamation: tools/amalgamate.py's output compiled by one plain compiler line
# per file (tests/amalgamation/compile.cmake). It needs a Python to generate
# with, a compiler that takes those lines, and to know where the ImGui headers
# are, which it does when the imgui target was made here.
#
# consumer_projects: tests/consumer/run.cmake, behind
# IMTYPESET_BUILD_CONSUMER_TESTS because it builds ImGui and the library three
# times over.
if(NOT IMTYPESET_MODULES AND NOT IMTYPESET_TESTS AND DEFINED _imgui_dir)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_test(NAME amalgamation
            COMMAND "${CMAKE_COMMAND}"
                "-DREPO=${PROJECT_SOURCE_DIR}" "-DIMGUI_DIR=${_imgui_dir}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/amalgamation"
                "-DPYTHON=${Python3_EXECUTABLE}" "-DCXX=${CMAKE_CXX_COMPILER}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/amalgamation/compile.cmake")
        set_tests_properties(amalgamation PROPERTIES TIMEOUT 600)
    else()
        message(STATUS "imtypeset: no test `amalgamation` (it needs python3 and GCC or Clang)")
    endif()
    if(IMTYPESET_BUILD_CONSUMER_TESTS)
        add_test(NAME consumer_projects
            COMMAND "${CMAKE_COMMAND}"
                "-DREPO=${PROJECT_SOURCE_DIR}" "-DIMGUI_DIR=${_imgui_dir}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/consumer" -DJOBS=4
                -P "${CMAKE_CURRENT_SOURCE_DIR}/consumer/run.cmake")
        set_tests_properties(consumer_projects PROPERTIES TIMEOUT 1800 RUN_SERIAL ON)
    endif()
endif()
