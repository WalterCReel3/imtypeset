# The drop-in files, generated and compiled the way a host with no build system
# for them would: one plain compiler line per file, the files and imgui.h, no
# flag of this tree's.
#
#   cmake -DREPO=<repository> -DIMGUI_DIR=<Dear ImGui checkout> -DWORK=<scratch>
#         -DPYTHON=<python3> -DCXX=<compiler> -P tests/amalgamation/compile.cmake
#
# Also checked: `amalgamate.py --check` accepts what it has just written and
# refuses the same directory after one byte of one file has changed.
cmake_minimum_required(VERSION 3.21)

foreach(_required REPO IMGUI_DIR WORK PYTHON CXX)
    if(NOT ${_required})
        message(FATAL_ERROR "compile.cmake: -D${_required}= is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run what expect)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
    if(expect STREQUAL "EXPECT_SUCCESS" AND NOT _result EQUAL 0)
        message(FATAL_ERROR "amalgamation: ${what}: FAILED (${_result})\n${_output}")
    elseif(expect STREQUAL "EXPECT_FAILURE" AND _result EQUAL 0)
        message(FATAL_ERROR "amalgamation: ${what}: succeeded, and it had to fail\n${_output}")
    endif()
    message(STATUS "amalgamation: ${what}: ok")
endfunction()

set(_generator "${PYTHON}" -I -B "${REPO}/tools/amalgamate.py" --out "${WORK}")
run("generate" EXPECT_SUCCESS ${_generator})
run("--check on what was just written" EXPECT_SUCCESS ${_generator} --check)

foreach(_file imtypeset imtypeset_demo imtypeset_c)
    run("c++ -std=c++17 -c ${_file}.cpp -I<imgui>" EXPECT_SUCCESS
        "${CXX}" -std=c++17 -c "${WORK}/${_file}.cpp" "-I${IMGUI_DIR}" -o "${WORK}/${_file}.o")
endforeach()
# The refusal a host must get instead of results nobody specified.
run("-ffast-math is refused" EXPECT_FAILURE
    "${CXX}" -std=c++17 -ffast-math -fsyntax-only "${WORK}/imtypeset.cpp" "-I${IMGUI_DIR}")

file(APPEND "${WORK}/imtypeset_demo.cpp" "\n")
run("--check after an edit" EXPECT_FAILURE ${_generator} --check)
message(STATUS "amalgamation: all steps passed")
