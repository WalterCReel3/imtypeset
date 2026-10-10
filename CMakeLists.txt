cmake_minimum_required(VERSION 3.21)

# The version, and the Dear ImGui versions the library is written for and
# tested against, are stated once, in the public header. Everything here reads
# them from it, so the build system cannot come to disagree with it.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/imtypeset.h" _imtypeset_header_defines
    REGEX "^#define[ \t]+IMTYPESET_(VERSION|IMGUI_VERSION_NUM|IMGUI_VERSION_NUM_MIN)[ \t]")
foreach(_line IN LISTS _imtypeset_header_defines)
    if(_line MATCHES "IMTYPESET_VERSION[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
        set(_imtypeset_version "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "IMTYPESET_IMGUI_VERSION_NUM_MIN[ \t]+([0-9]+)")
        set(IMTYPESET_IMGUI_VERSION_NUM_MIN "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "IMTYPESET_IMGUI_VERSION_NUM[ \t]+([0-9]+)")
        set(IMTYPESET_IMGUI_VERSION_NUM "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT _imtypeset_version OR NOT IMTYPESET_IMGUI_VERSION_NUM_MIN OR NOT IMTYPESET_IMGUI_VERSION_NUM)
    message(FATAL_ERROR
        "imtypeset: include/imtypeset.h no longer defines IMTYPESET_VERSION, "
        "IMTYPESET_IMGUI_VERSION_NUM and IMTYPESET_IMGUI_VERSION_NUM_MIN in the form read here")
endif()

project(imtypeset
    VERSION ${_imtypeset_version}
    DESCRIPTION "Typeset text for Dear ImGui: TeX's box-and-glue model over ImGui's font atlas"
    LANGUAGES CXX)

# Top-level only: when another project add_subdirectory()s this one it brings
# its own ImGui target and does not want our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IMTYPESET_TOP_LEVEL ON)
else()
    set(IMTYPESET_TOP_LEVEL OFF)
endif()

option(IMTYPESET_BUILD_TESTS "Build the tests" ${IMTYPESET_TOP_LEVEL})
option(IMTYPESET_BUILD_BENCH "Build the benchmarks" ${IMTYPESET_TOP_LEVEL})
option(IMTYPESET_BUILD_EXAMPLES "Build the examples" ${IMTYPESET_TOP_LEVEL})
option(IMTYPESET_WARNINGS_AS_ERRORS "Treat warnings in imtypeset's own sources as errors" ${IMTYPESET_TOP_LEVEL})
set(IMTYPESET_IMGUI_TARGET "" CACHE STRING
    "The Dear ImGui target to build against and link. Empty: imgui::imgui if there is one, else imgui.")
option(IMTYPESET_USE_IMGUI_PACKAGE
    "find_package(imgui CONFIG) and build against its imgui::imgui (vcpkg, or any installed ImGui package). The one route that can be installed." OFF)
option(IMTYPESET_BUILD_IMGUI
    "With no ImGui target in sight, build one here (cmake/imgui.cmake). OFF when included by another project: a host must not get a second ImGui without asking."
    ${IMTYPESET_TOP_LEVEL})
set(IMTYPESET_IMGUI_DIR "" CACHE PATH
    "A Dear ImGui checkout for the imgui target made here. Empty: .deps/imgui if present, else fetch the tested tag.")
option(IMTYPESET_IMGUI_WCHAR32
    "Build the imgui target made here with IMGUI_USE_WCHAR32, as the reference's ImGui is. OFF: ImGui's default 16-bit ImWchar." ON)
option(IMTYPESET_ENABLE_EXCEPTIONS
    "Compile imtypeset's own sources with exceptions and RTTI enabled, as a host whose IM_ASSERT throws must (imgui_bundle's Python build). OFF: -fno-exceptions -fno-rtti." OFF)
set(IMTYPESET_IMGUI_USER_CONFIG "" CACHE FILEPATH
    "A header given to the imgui target made here, and to everything that links it, as IMGUI_USER_CONFIG. Empty: none.")
set(IMTYPESET_IMGUI_DEFINITIONS "" CACHE STRING
    "Extra compile definitions for the imgui target made here, PUBLIC (a semicolon list): what a host's own ImGui build defines, such as IMGUI_DISABLE_OBSOLETE_FUNCTIONS.")

# Narrowing, for building part of a tree that is being written by several hands
# at once. Each is a semicolon list of basenames without extension; empty means
# everything. See CLAUDE.md, "A private build directory".
set(IMTYPESET_MODULES "" CACHE STRING
    "Library sources to build, as basenames of src/*.cpp. Empty: all.")
set(IMTYPESET_TESTS "" CACHE STRING
    "Tests to build, as basenames of tests/test_*.cpp, or 'smoke'. Empty: all.")
set(IMTYPESET_BENCHES "" CACHE STRING
    "Benchmarks to build, as basenames of bench/bench_*.cpp. Empty: all, unless IMTYPESET_MODULES is narrowed, then none.")
set(IMTYPESET_EXAMPLES "" CACHE STRING
    "Examples to build, as basenames of examples/*.cpp. Empty: all, unless IMTYPESET_MODULES is narrowed, then none.")

option(IMTYPESET_AMALGAMATED
    "Build the library from the generated drop-in files (tools/amalgamate.py) and not from src/*.cpp: what a host that adds those files to its own project compiles." OFF)
set(IMTYPESET_HOST_FLAGS "" CACHE STRING
    "With IMTYPESET_AMALGAMATED: compile the library with exactly these flags (a semicolon list) and none of this tree's own, -ffp-contract=off included, as a host's build would. For the test of what the drop-in file does under a host's floating-point flags; nothing else should set it.")
set(IMTYPESET_AMALGAMATE_ARGS "" CACHE STRING
    "With IMTYPESET_AMALGAMATED: extra arguments for tools/amalgamate.py. For measuring what the file's contraction pragma buys (--no-floating-point-guard); nothing else should set it.")
mark_as_advanced(IMTYPESET_AMALGAMATE_ARGS)
option(IMTYPESET_BUILD_CONSUMER_TESTS
    "Add the ctest test that configures, builds and runs small host projects against this tree: add_subdirectory with the host's ImGui, and install + find_package(imtypeset CONFIG). Minutes, not seconds." OFF)
option(IMTYPESET_BUILD_PYTHON_HOST
    "Build python/: imtypeset, Dear ImGui and SDL2 as one shared library, and the python_binding test. Makes the imtypeset target position-independent: take no timing from such a tree." OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dear ImGui ---------------------------------------------------------------
# The library is built against one ImGui target and links it PUBLIC. Which one,
# first match wins:
#   1. IMTYPESET_IMGUI_TARGET, when the host names it;
#   2. the package's imgui::imgui, when IMTYPESET_USE_IMGUI_PACKAGE asks for it;
#   3. an existing imgui::imgui (a package, vcpkg, hello_imgui's package route);
#   4. an existing imgui (hello_imgui and most hand-written hosts);
#   5. one built here by cmake/imgui.cmake, when IMTYPESET_BUILD_IMGUI is on,
#      which it is by default only when this is the top-level project.
# With none of those the configure fails. Building a private ImGui inside a
# host that has its own under another name gives a program with two, and that
# is not something to do without being asked.
#
# Nothing is ever defined on a host's target. The rasteriser, the ImWchar width
# and the rest of the configuration are whatever that target was built with,
# and reach the library through the target's PUBLIC properties.
set(_imtypeset_built_imgui OFF)
if(IMTYPESET_IMGUI_TARGET)
    if(NOT TARGET ${IMTYPESET_IMGUI_TARGET})
        message(FATAL_ERROR
            "imtypeset: IMTYPESET_IMGUI_TARGET is '${IMTYPESET_IMGUI_TARGET}', and there is no such target")
    endif()
    set(_imtypeset_imgui "${IMTYPESET_IMGUI_TARGET}")
elseif(IMTYPESET_USE_IMGUI_PACKAGE)
    find_package(imgui CONFIG REQUIRED)
    if(NOT TARGET imgui::imgui)
        message(FATAL_ERROR "imtypeset: the imgui package found at ${imgui_DIR} defines no imgui::imgui")
    endif()
    set(_imtypeset_imgui imgui::imgui)
elseif(TARGET imgui::imgui)
    set(_imtypeset_imgui imgui::imgui)
elseif(TARGET imgui)
    set(_imtypeset_imgui imgui)
elseif(IMTYPESET_BUILD_IMGUI)
    # The default follows what the machine has: FreeType where it is installed,
    # stb_truetype where it is not. Set it explicitly to pin either.
    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        set(_imtypeset_freetype_default ON)
    else()
        set(_imtypeset_freetype_default OFF)
    endif()
    option(IMTYPESET_USE_FREETYPE
        "Build the imgui target made here with misc/freetype and IMGUI_ENABLE_FREETYPE"
        ${_imtypeset_freetype_default})
    include(cmake/imgui.cmake)
    if(IMTYPESET_IMGUI_USER_CONFIG)
        # PUBLIC: it can change IM_ASSERT and the layout of ImGui's types, so
        # every file that includes imgui.h has to see the same one.
        target_compile_definitions(imgui PUBLIC
            "IMGUI_USER_CONFIG=\"${IMTYPESET_IMGUI_USER_CONFIG}\"")
    endif()
    if(IMTYPESET_IMGUI_DEFINITIONS)
        target_compile_definitions(imgui PUBLIC ${IMTYPESET_IMGUI_DEFINITIONS})
    endif()
    # Lets a test tell "ImWchar is the width this build asked for" from "ImWchar
    # is whatever it happens to be". Unset when the host supplied the target.
    if(IMTYPESET_IMGUI_WCHAR32)
        set(_imtypeset_expect_wchar32 1)
    else()
        set(_imtypeset_expect_wchar32 0)
    endif()
    set(_imtypeset_imgui imgui)
    set(_imtypeset_built_imgui ON)
else()
    message(FATAL_ERROR
        "imtypeset: no Dear ImGui target. The library is built against the host's ImGui and "
        "found neither `imgui::imgui` nor `imgui`. Define the host's ImGui target before "
        "add_subdirectory(imtypeset); or, if it has another name, set IMTYPESET_IMGUI_TARGET "
        "to it; or set IMTYPESET_USE_IMGUI_PACKAGE=ON to find_package(imgui CONFIG); or set "
        "IMTYPESET_BUILD_IMGUI=ON to have a second, private ImGui built here on purpose.")
endif()

# Whether the target is one somebody installed (IMPORTED, possibly behind an
# alias): the only kind an installed imtypeset can name as its dependency.
set(_imtypeset_imgui_real "${_imtypeset_imgui}")
get_target_property(_imtypeset_aliased ${_imtypeset_imgui} ALIASED_TARGET)
if(_imtypeset_aliased)
    set(_imtypeset_imgui_real "${_imtypeset_aliased}")
endif()
get_target_property(_imtypeset_imgui_imported ${_imtypeset_imgui_real} IMPORTED)

if(NOT _imtypeset_built_imgui)
    # A host's ImGui: the version is looked for in the target's include
    # directories. Not finding the header there proves nothing (a generator
    # expression, a header reached some other way), and then the #error in
    # include/imtypeset.h is what holds the floor, at compile time.
    include(cmake/imgui_version.cmake)
    get_target_property(_imtypeset_imgui_includes ${_imtypeset_imgui_real} INTERFACE_INCLUDE_DIRECTORIES)
    set(_imtypeset_imgui_header "")
    set(_imtypeset_imgui_version "")
    foreach(_dir IN LISTS _imtypeset_imgui_includes)
        string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" _dir "${_dir}")
        if(NOT _imtypeset_imgui_header AND EXISTS "${_dir}/imgui.h")
            set(_imtypeset_imgui_header "${_dir}/imgui.h")
        endif()
    endforeach()
    if(_imtypeset_imgui_header)
        imtypeset_check_imgui_version("${_imtypeset_imgui_header}" _imtypeset_imgui_version)
        message(STATUS "imtypeset: Dear ImGui ${_imtypeset_imgui_version} from the host's target ${_imtypeset_imgui}")
    else()
        message(STATUS "imtypeset: the host's target ${_imtypeset_imgui}; its version is checked when imtypeset.h is compiled")
    endif()
endif()

# --- Flags for our own sources ---------------------------------------------------
# An INTERFACE target so the library, the tests, the benchmarks and the examples
# are compiled the same way and none of it reaches ImGui or a host.
#
# -fno-exceptions -fno-rtti: ImGui hosts commonly build that way, so this tree
# has to, and the way to know it does is to build it so everywhere.
#
# -ffp-contract=off: the layout arithmetic is compared with the reference's to
# the last bit, and a compiler that may fuse a*b+c into one FMA (GCC once -march
# allows it, Clang by default on targets that have one) rounds once where the
# reference rounds twice. For the same reason nothing here is ever built with
# -ffast-math, -Ofast or -funsafe-math-optimizations, benchmarks included.
#
# What the library asks of the compiler about floating point it also asks in its
# sources, for a host that compiles them with its own flags: src/units.h refuses
# fast-math, and the generated drop-in file switches contraction off with a
# pragma (tools/amalgamate.py, docs/distribution.md).
add_library(imtypeset_build_flags INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imtypeset_build_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -ffp-contract=off)
    # The library uses neither exceptions nor RTTI and is built without them
    # everywhere but in the one configuration that shows it also unwinds
    # cleanly when a host's IM_ASSERT throws through it.
    if(NOT IMTYPESET_ENABLE_EXCEPTIONS)
        target_compile_options(imtypeset_build_flags INTERFACE -fno-exceptions -fno-rtti)
    endif()
    if(IMTYPESET_WARNINGS_AS_ERRORS)
        target_compile_options(imtypeset_build_flags INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(imtypeset_build_flags INTERFACE /W4 /fp:precise)
    if(IMTYPESET_ENABLE_EXCEPTIONS)
        target_compile_options(imtypeset_build_flags INTERFACE /EHsc)
    else()
        target_compile_options(imtypeset_build_flags INTERFACE /GR-)
        target_compile_definitions(imtypeset_build_flags INTERFACE _HAS_EXCEPTIONS=0)
    endif()
    if(IMTYPESET_WARNINGS_AS_ERRORS)
        target_compile_options(imtypeset_build_flags INTERFACE /WX)
    endif()
endif()

# imtypeset_select(<out> <kind> <selection> <file>...)
# Filters <file>... down to those whose basename (no extension) is in the list
# <selection>; an empty selection keeps them all. A selected name that matches
# no file is an error unless src/<name>.h exists, so that a header-only module
# can be named in IMTYPESET_MODULES like any other.
function(imtypeset_select out kind selection)
    if("${selection}" STREQUAL "")
        set(${out} ${ARGN} PARENT_SCOPE)
        return()
    endif()
    set(_kept "")
    set(_seen "")
    foreach(_file IN LISTS ARGN)
        get_filename_component(_name "${_file}" NAME_WE)
        if(_name IN_LIST selection)
            list(APPEND _kept "${_file}")
            list(APPEND _seen "${_name}")
        endif()
    endforeach()
    foreach(_name IN LISTS selection)
        if(NOT _name IN_LIST _seen)
            if(kind STREQUAL "module" AND EXISTS "${PROJECT_SOURCE_DIR}/src/${_name}.h")
                continue()
            endif()
            message(FATAL_ERROR "imtypeset: no ${kind} named '${_name}'")
        endif()
    endforeach()
    set(${out} ${_kept} PARENT_SCOPE)
endfunction()

# imtypeset_program(<target> <source>)
# What every test, benchmark and example has in common: our flags, the library,
# src/ and tests/ on the include path (tests exercise internals; examples and
# benchmarks borrow tests/support), and the directories they read data from.
function(imtypeset_program target source)
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE imtypeset::imtypeset imtypeset_build_flags)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(${target} PRIVATE
        "IMTYPESET_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\""
        "IMTYPESET_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/tests/data\""
        "IMTYPESET_GOLDEN_DIR=\"${PROJECT_SOURCE_DIR}/tests/golden\""
        "IMTYPESET_BENCH_DATA_DIR=\"${PROJECT_SOURCE_DIR}/bench/data\"")
    if(DEFINED _imtypeset_expect_wchar32)
        target_compile_definitions(${target} PRIVATE
            "IMTYPESET_EXPECT_WCHAR32=${_imtypeset_expect_wchar32}")
    endif()
endfunction()

# --- The library ---------------------------------------------------------------
if(IMTYPESET_AMALGAMATED)
    # The drop-in files, generated into the build tree and compiled in place of
    # src/*.cpp. Nothing of src/ is on the library's include path here: the
    # files have to build from themselves and imgui.h, as they do for a host.
    if(IMTYPESET_MODULES)
        message(FATAL_ERROR "imtypeset: IMTYPESET_AMALGAMATED builds the whole library; IMTYPESET_MODULES cannot narrow it")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_imtypeset_dist "${CMAKE_CURRENT_BINARY_DIR}/dist")
    file(GLOB _imtypeset_dist_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
    set(_imtypeset_modules
        "${_imtypeset_dist}/imtypeset.cpp"
        "${_imtypeset_dist}/imtypeset_demo.cpp"
        "${_imtypeset_dist}/imtypeset_c.cpp")
    add_custom_command(
        OUTPUT ${_imtypeset_modules} "${_imtypeset_dist}/imtypeset.h" "${_imtypeset_dist}/imtypeset_c.h"
        COMMAND Python3::Interpreter -I -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/amalgamate.py"
                --out "${_imtypeset_dist}" ${IMTYPESET_AMALGAMATE_ARGS}
        DEPENDS ${_imtypeset_dist_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/tools/amalgamate.py"
        COMMENT "Generating the drop-in files"
        VERBATIM)
    message(STATUS "imtypeset: the library is built from the generated drop-in files")
else()
    # Every src/*.cpp, found by glob so that adding a module is adding a file.
    file(GLOB _imtypeset_all_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    imtypeset_select(_imtypeset_modules "module" "${IMTYPESET_MODULES}" ${_imtypeset_all_modules})
    if(NOT _imtypeset_modules)
        # A narrowed build of header-only modules still needs a library to link.
        set(_imtypeset_anchor "${CMAKE_CURRENT_BINARY_DIR}/imtypeset_anchor.cpp")
        if(NOT EXISTS "${_imtypeset_anchor}")
            file(WRITE "${_imtypeset_anchor}"
                "// Generated: IMTYPESET_MODULES selected no src/*.cpp.\nnamespace ImTypeset { namespace detail { int anchor() { return 0; } } }\n")
        endif()
        set(_imtypeset_modules "${_imtypeset_anchor}")
    endif()
endif()

add_library(imtypeset ${_imtypeset_modules})
add_library(imtypeset::imtypeset ALIAS imtypeset)

target_include_directories(imtypeset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
if(NOT IMTYPESET_AMALGAMATED)
    target_include_directories(imtypeset PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
target_link_libraries(imtypeset PUBLIC ${_imtypeset_imgui})
if(IMTYPESET_HOST_FLAGS)
    if(NOT IMTYPESET_AMALGAMATED)
        message(FATAL_ERROR "imtypeset: IMTYPESET_HOST_FLAGS is for IMTYPESET_AMALGAMATED builds")
    endif()
    target_compile_options(imtypeset PRIVATE ${IMTYPESET_HOST_FLAGS})
    message(STATUS "imtypeset: the library is compiled with [${IMTYPESET_HOST_FLAGS}] and none of this tree's own flags")
else()
    # BUILD_INTERFACE: the flags are how this tree compiles, not something an
    # installed library hands to whoever links it.
    target_link_libraries(imtypeset PRIVATE $<BUILD_INTERFACE:imtypeset_build_flags>)
endif()

if(IMTYPESET_MODULES)
    message(STATUS "imtypeset: modules narrowed to [${IMTYPESET_MODULES}]")
endif()

# --- Install and package ---------------------------------------------------------
# find_package(imtypeset CONFIG) gives imtypeset::imtypeset, the same name the
# alias above gives a host that used add_subdirectory.
#
# Only against an ImGui that was itself installed. The library's headers
# include imgui.h and its package has to say where the consumer gets one, and
# the only honest answer is "from the package this was built against". An ImGui
# built here, or a target inside a host's own tree, cannot be named that way,
# and Dear ImGui's headers are never installed from here: they would collide
# with the host's.
if(IMTYPESET_TOP_LEVEL AND _imtypeset_imgui_imported)
    set(_imtypeset_install_default ON)
else()
    set(_imtypeset_install_default OFF)
endif()
option(IMTYPESET_INSTALL
    "Install the library, its headers and a CMake package. Needs an installed (imported) ImGui target."
    ${_imtypeset_install_default})
if(IMTYPESET_INSTALL)
    if(NOT _imtypeset_imgui_imported)
        message(FATAL_ERROR
            "imtypeset: IMTYPESET_INSTALL needs an installed Dear ImGui, and the target here "
            "(${_imtypeset_imgui}) is built in this tree. An installed imtypeset has to tell its "
            "consumer where imgui.h and ImGui's library come from, and it does not install "
            "ImGui itself. Configure with -DIMTYPESET_USE_IMGUI_PACKAGE=ON against an imgui "
            "package (vcpkg's, or your own), or use add_subdirectory or the drop-in files.")
    endif()
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(_imtypeset_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/imtypeset")
    install(TARGETS imtypeset EXPORT imtypesetTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES include/imtypeset.h include/imtypeset_c.h
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT imtypesetTargets NAMESPACE imtypeset:: DESTINATION "${_imtypeset_cmake_dir}")
    configure_package_config_file(cmake/imtypesetConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/imtypesetConfig.cmake"
        INSTALL_DESTINATION "${_imtypeset_cmake_dir}")
    # 0.x: a minor version may change the API.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/imtypesetConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/imtypesetConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/imtypesetConfigVersion.cmake"
        DESTINATION "${_imtypeset_cmake_dir}")
endif()

# --- Tests, benchmarks, examples ---------------------------------------------------
if(IMTYPESET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(IMTYPESET_BUILD_BENCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt")
    add_subdirectory(bench)
endif()
if(IMTYPESET_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    enable_testing()
    add_subdirectory(examples)
endif()
if(IMTYPESET_BUILD_PYTHON_HOST)
    enable_testing()
    add_subdirectory(python)
endif()
