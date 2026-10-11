# The windowed host of the demo: SDL2, Dear ImGui's SDL2 platform backend and
# its SDL_Renderer and OpenGL 3 renderer backends, the library, and
# ImTypeset::ShowDemoWindow(). A project of its own:
#
#   cmake -S examples/demo_sdl2 -B build/demo -G Ninja
#   cmake --build build/demo -j4
#   build/demo/demo_sdl2
#
# SDL2 is an installed one (the distribution's libsdl2-dev or SDL2-devel, found
# by its CMake package or by pkg-config), else this tree's untracked .deps/sdl2
# (tools/vendor_sdl2.sh fills it), and -DIMTYPESET_SDL2_DIR=<prefix or build
# tree> names another over both (cmake/sdl2.cmake). It builds ImGui itself,
# backends and imgui_demo.cpp included, with the two choices the root build
# makes (ImWchar width, rasteriser), and then adds the library on top of that
# same `imgui` target: the root CMakeLists.txt leaves an existing `imgui` alone
# and, not being top level, brings no tests.
#
# It is also reached from the root build, with -DIMTYPESET_BUILD_DEMO_SDL2=ON
# (examples/CMakeLists.txt). There the `imgui` and `imtypeset` targets exist
# already and are used as they are; only the backends and imgui_demo.cpp are
# compiled here.
cmake_minimum_required(VERSION 3.21)
project(imtypeset_demo_sdl2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(_demo_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(IMTYPESET_IMGUI_DIR "" CACHE PATH
    "The Dear ImGui checkout to build, backends included. Empty: .deps/imgui of this tree.")
set(IMTYPESET_SDL2_DIR "" CACHE PATH
    "The SDL2 to use, over an installed one and over .deps/sdl2: an install prefix (lib/, include/SDL2/), or a CMake build tree of SDL2 fetched as a dependency (lib/libSDL2[d].a, _deps/sdl2-build/include*)")
option(IMTYPESET_DEMO_WCHAR32 "Build ImGui with IMGUI_USE_WCHAR32" ON)
find_package(Freetype QUIET)
option(IMTYPESET_DEMO_FREETYPE "Build ImGui with misc/freetype" ${FREETYPE_FOUND})

# A relative path on the command line is relative to where cmake was run.
foreach(_var IMTYPESET_IMGUI_DIR IMTYPESET_SDL2_DIR)
    if(${_var} AND NOT IS_ABSOLUTE "${${_var}}")
        get_filename_component(_abs "${${_var}}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}/../..")
        if(NOT EXISTS "${_abs}")
            get_filename_component(_abs "${${_var}}" ABSOLUTE BASE_DIR "${_demo_repo}")
        endif()
        set(${_var} "${_abs}")
    endif()
endforeach()
# The ImGui sources: the ones named, else the ones the root build chose (it
# leaves them in _imgui_dir), else this tree's copy of the pinned tag.
if(IMTYPESET_IMGUI_DIR)
    set(_demo_imgui "${IMTYPESET_IMGUI_DIR}")
elseif(TARGET imgui AND DEFINED _imgui_dir)
    set(_demo_imgui "${_imgui_dir}")
else()
    set(_demo_imgui "${_demo_repo}/.deps/imgui")
endif()
if(NOT EXISTS "${_demo_imgui}/backends/imgui_impl_sdl2.cpp")
    message(FATAL_ERROR "demo_sdl2: '${_demo_imgui}' has no backends/imgui_impl_sdl2.cpp")
endif()

# --- SDL2 -----------------------------------------------------------------------
# cmake/sdl2.cmake has the order: IMTYPESET_SDL2_DIR when it is given; else an
# installed SDL2, by its CMake package and then by pkg-config; else .deps/sdl2
# of this tree, as .deps/imgui is used for ImGui (cmake/imgui.cmake).
include("${_demo_repo}/cmake/sdl2.cmake")
imtypeset_find_sdl2(demo_sdl2_sdl "demo_sdl2")

# --- Dear ImGui -----------------------------------------------------------------
# Named `imgui`, with the root build's properties, so that the library can sit
# on it. The backends are a separate target: the library must not see them.
set(_demo_fp_targets "")
if(NOT TARGET imgui)
    add_library(imgui STATIC
        "${_demo_imgui}/imgui.cpp"
        "${_demo_imgui}/imgui_demo.cpp"
        "${_demo_imgui}/imgui_draw.cpp"
        "${_demo_imgui}/imgui_tables.cpp"
        "${_demo_imgui}/imgui_widgets.cpp")
    target_include_directories(imgui SYSTEM PUBLIC "${_demo_imgui}")
    if(IMTYPESET_DEMO_WCHAR32)
        target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
    endif()
    if(IMTYPESET_DEMO_FREETYPE)
        find_package(Freetype REQUIRED)
        target_sources(imgui PRIVATE "${_demo_imgui}/misc/freetype/imgui_freetype.cpp")
        target_include_directories(imgui SYSTEM PUBLIC "${_demo_imgui}/misc/freetype")
        target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
        target_link_libraries(imgui PUBLIC Freetype::Freetype)
    endif()
    list(APPEND _demo_fp_targets imgui)
else()
    # The root build's target has everything but ImGui's own demo window.
    set(_demo_imgui_demo "${_demo_imgui}/imgui_demo.cpp")
endif()

add_library(demo_sdl2_backends STATIC
    "${_demo_imgui}/backends/imgui_impl_sdl2.cpp"
    "${_demo_imgui}/backends/imgui_impl_sdlrenderer2.cpp"
    "${_demo_imgui}/backends/imgui_impl_opengl3.cpp"
    ${_demo_imgui_demo})
target_include_directories(demo_sdl2_backends SYSTEM PUBLIC "${_demo_imgui}/backends")
# ImGui's OpenGL backend carries its own loader and finds libGL with dlopen.
target_link_libraries(demo_sdl2_backends PUBLIC imgui demo_sdl2_sdl ${CMAKE_DL_LIBS})

# --- The host -------------------------------------------------------------------
add_executable(demo_sdl2 main.cpp)
target_link_libraries(demo_sdl2 PRIVATE demo_sdl2_backends)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(demo_sdl2 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -fno-exceptions -fno-rtti)
endif()

# Floating point is not contracted in anything compiled here, as in the root
# build (cmake/imgui.cmake, D16): the ImGui made above, its backends, the host.
# Never an `imgui` target that existed before this file was read.
list(APPEND _demo_fp_targets demo_sdl2_backends demo_sdl2)
foreach(_target IN LISTS _demo_fp_targets)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_target} PRIVATE -ffp-contract=off)
    elseif(MSVC)
        target_compile_options(${_target} PRIVATE /fp:precise)
    endif()
endforeach()

if(NOT TARGET imtypeset::imtypeset)
    # Not top level there, so no tests, benchmarks or examples come with it.
    add_subdirectory("${_demo_repo}" imtypeset EXCLUDE_FROM_ALL)
endif()
target_link_libraries(demo_sdl2 PRIVATE imtypeset::imtypeset)
target_compile_definitions(demo_sdl2 PRIVATE
    "IMTYPESET_DEMO_FONT_DIR=\"${_demo_repo}/tests/data/fonts\"")

# In the root build it is also a test: a few frames with no display, through
# SDL's dummy video driver and software renderer.
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_test(NAME demo_sdl2_dummy COMMAND demo_sdl2 --frames 5 --open all --dpi-scale 1)
    set_tests_properties(demo_sdl2_dummy PROPERTIES
        TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
