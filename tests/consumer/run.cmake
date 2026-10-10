# The consumer-project test: small host projects configured, built and run
# against this tree, the way a host would use it.
#
#   cmake -DREPO=<repository> -DIMGUI_DIR=<Dear ImGui checkout> -DWORK=<scratch>
#         [-DJOBS=4] -P tests/consumer/run.cmake
#
# Everything is written under WORK, which is emptied first. A step that does not
# end as expected stops the script with what it printed.
#
#   1. add_subdirectory, host target `imgui`, the host building unity.
#   2. add_subdirectory, host target under another name with alias imgui::imgui.
#   3. add_subdirectory, host target named in IMTYPESET_IMGUI_TARGET.
#   4. add_subdirectory with no ImGui target: the configure must fail with the
#      message that names the ways out, and must not have built an ImGui.
#   5. Installing against an ImGui built in the tree must be refused.
#   6. An ImGui package (tests/consumer/imgui_package) installed to a prefix;
#      imtypeset configured against it, built, installed to the same prefix; a
#      host that find_package(imtypeset CONFIG)s it, built and run.
#
# The ImGui of hosts 1 to 4 is stock (16-bit ImWchar, stb_truetype); the
# package of 6 defines IMGUI_USE_WCHAR32 and is stb_truetype too. Configure only
# for 2 and 3: what they add over 1 is the target resolution, and a build of
# each would be the same build again.
cmake_minimum_required(VERSION 3.21)

foreach(_required REPO IMGUI_DIR WORK)
    if(NOT ${_required})
        message(FATAL_ERROR "run.cmake: -D${_required}= is required")
    endif()
endforeach()
if(NOT JOBS)
    set(JOBS 4)
endif()
set(_fonts "${REPO}/tests/data/fonts")
set(_here "${REPO}/tests/consumer")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# step(<what> EXPECT_FAILURE|EXPECT_SUCCESS [MATCH <regex>] COMMAND ...)
function(step what expect)
    cmake_parse_arguments(PARSE_ARGV 2 _s "" "MATCH" "COMMAND")
    execute_process(COMMAND ${_s_COMMAND}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
    if(expect STREQUAL "EXPECT_SUCCESS" AND NOT _result EQUAL 0)
        message(FATAL_ERROR "consumer: ${what}: FAILED (${_result})\n${_output}")
    elseif(expect STREQUAL "EXPECT_FAILURE" AND _result EQUAL 0)
        message(FATAL_ERROR "consumer: ${what}: succeeded, and it had to fail\n${_output}")
    endif()
    if(_s_MATCH AND NOT _output MATCHES "${_s_MATCH}")
        message(FATAL_ERROR "consumer: ${what}: the output does not match '${_s_MATCH}'\n${_output}")
    endif()
    message(STATUS "consumer: ${what}: ok")
endfunction()

set(_common -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DIMGUI_DIR=${IMGUI_DIR}" "-DIMTYPESET_DIR=${REPO}")

# 1
step("add_subdirectory, host `imgui`, unity: configure" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/subdirectory" -B "${WORK}/sub-imgui" ${_common}
            -DHOST_IMGUI=imgui -DHOST_UNITY=ON)
step("add_subdirectory, host `imgui`, unity: build" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" --build "${WORK}/sub-imgui" -j ${JOBS})
step("add_subdirectory, host `imgui`, unity: run" EXPECT_SUCCESS MATCH "minimal: 3 frames, 1 layout"
    COMMAND "${WORK}/sub-imgui/consumer" "${_fonts}")
if(EXISTS "${WORK}/sub-imgui/imtypeset/tests" OR EXISTS "${WORK}/sub-imgui/imtypeset/examples")
    message(FATAL_ERROR "consumer: a host that add_subdirectory()s the library got its tests or examples")
endif()

# 2, 3
step("add_subdirectory, host alias imgui::imgui: configure" EXPECT_SUCCESS
    MATCH "host's target imgui::imgui"
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/subdirectory" -B "${WORK}/sub-alias" ${_common}
            -DHOST_IMGUI=imgui::imgui)
step("add_subdirectory, IMTYPESET_IMGUI_TARGET: configure" EXPECT_SUCCESS
    MATCH "host's target host_dear_imgui"
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/subdirectory" -B "${WORK}/sub-named" ${_common}
            -DHOST_IMGUI=named)

# 4
step("add_subdirectory with no ImGui target is refused" EXPECT_FAILURE
    MATCH "no Dear ImGui target.*IMTYPESET_IMGUI_TARGET.*IMTYPESET_BUILD_IMGUI"
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/subdirectory" -B "${WORK}/sub-none" ${_common}
            -DHOST_IMGUI=none)

# 5
step("install against an ImGui built in the tree is refused" EXPECT_FAILURE
    MATCH "IMTYPESET_INSTALL needs an installed Dear ImGui"
    COMMAND "${CMAKE_COMMAND}" -S "${REPO}" -B "${WORK}/install-refused" -G Ninja
            "-DIMTYPESET_IMGUI_DIR=${IMGUI_DIR}" -DIMTYPESET_INSTALL=ON
            -DIMTYPESET_BUILD_TESTS=OFF -DIMTYPESET_BUILD_BENCH=OFF -DIMTYPESET_BUILD_EXAMPLES=OFF)

# 6
set(_prefix "${WORK}/prefix")
step("imgui package: configure" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/imgui_package" -B "${WORK}/imgui-package" -G Ninja
            -DCMAKE_BUILD_TYPE=Debug "-DIMGUI_DIR=${IMGUI_DIR}" "-DCMAKE_INSTALL_PREFIX=${_prefix}")
step("imgui package: build and install" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" --build "${WORK}/imgui-package" -j ${JOBS} --target install)
step("imtypeset against the package: configure" EXPECT_SUCCESS
    MATCH "host's target imgui::imgui"
    COMMAND "${CMAKE_COMMAND}" -S "${REPO}" -B "${WORK}/imtypeset" -G Ninja
            -DCMAKE_BUILD_TYPE=Debug -DIMTYPESET_USE_IMGUI_PACKAGE=ON
            "-DCMAKE_PREFIX_PATH=${_prefix}" "-DCMAKE_INSTALL_PREFIX=${_prefix}"
            -DIMTYPESET_BUILD_TESTS=OFF -DIMTYPESET_BUILD_BENCH=OFF -DIMTYPESET_BUILD_EXAMPLES=OFF)
step("imtypeset against the package: build and install" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" --build "${WORK}/imtypeset" -j ${JOBS} --target install)
foreach(_file include/imtypeset.h include/imtypeset_c.h lib/libimtypeset.a
        lib/cmake/imtypeset/imtypesetConfig.cmake lib/cmake/imtypeset/imtypesetConfigVersion.cmake
        lib/cmake/imtypeset/imtypesetTargets.cmake)
    if(NOT EXISTS "${_prefix}/${_file}")
        message(FATAL_ERROR "consumer: the install did not write ${_file}")
    endif()
endforeach()
# Dear ImGui's headers in the prefix are the fixture's. imtypeset installs two
# headers and no others.
file(STRINGS "${WORK}/imtypeset/install_manifest.txt" _manifest)
foreach(_line IN LISTS _manifest)
    if(_line MATCHES "imgui|imstb|imconfig")
        message(FATAL_ERROR "consumer: imtypeset's install wrote a Dear ImGui file: ${_line}")
    endif()
endforeach()
step("find_package(imtypeset CONFIG): configure" EXPECT_SUCCESS
    MATCH "consumer: imtypeset [0-9]+\\.[0-9]+\\.[0-9]+, built against Dear ImGui [0-9]+"
    COMMAND "${CMAKE_COMMAND}" -S "${_here}/package" -B "${WORK}/package" -G Ninja
            -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=${_prefix}" "-DIMTYPESET_DIR=${REPO}")
step("find_package(imtypeset CONFIG): build" EXPECT_SUCCESS
    COMMAND "${CMAKE_COMMAND}" --build "${WORK}/package" -j ${JOBS})
step("find_package(imtypeset CONFIG): run" EXPECT_SUCCESS MATCH "minimal: 3 frames, 1 layout"
    COMMAND "${WORK}/package/consumer" "${_fonts}")
# A consumer asking for a version with another minor is turned away.
file(WRITE "${WORK}/wrong-version/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.21)\nproject(wrong LANGUAGES CXX)\nfind_package(imtypeset 0.99 CONFIG REQUIRED)\n")
step("find_package(imtypeset 0.99) is refused" EXPECT_FAILURE
    COMMAND "${CMAKE_COMMAND}" -S "${WORK}/wrong-version" -B "${WORK}/wrong-version/build" -G Ninja
            "-DCMAKE_PREFIX_PATH=${_prefix}")

message(STATUS "consumer: all steps passed")
