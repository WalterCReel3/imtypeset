# The Python host: one shared library holding imtypeset (with its C ABI), the
# Dear ImGui it is built against, ImGui's SDL2 backends, SDL2 and the small
# host surface of python/host/imtypeset_host.h; and the ctest test that drives
# it from Python through the ctypes package in python/imtypeset.
#
# On its own:
#
#   cmake -S python -B build/py-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
#       -DIMTYPESET_SDL2_DIR=../example-project/build/desktop-debug
#   cmake --build build/py-host -j4
#   ctest --test-dir build/py-host --output-on-failure
#
# or from the repository's own build, behind an option, after the `imtypeset`
# target exists:
#
#   option(IMTYPESET_BUILD_PYTHON_HOST "Build python/: the shared library and its test" OFF)
#   if(IMTYPESET_BUILD_PYTHON_HOST)
#       add_subdirectory(python)
#   endif()
#
# What this is not: a packaged, portable distribution. Nothing is installed,
# there is no wheel, the library is found through IMTYPESET_LIBRARY, and the
# link is written for ELF and the GNU-style linkers (a version script decides
# what is exported). The Windows and macOS branches of that have never been
# linked.
cmake_minimum_required(VERSION 3.21)

get_filename_component(_py_repo "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT TARGET imtypeset)
    project(imtypeset_python LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    # Everything below goes into a shared library. Hidden by default: what is
    # exported is what include/imtypeset_c.h and imtypeset_host.h mark.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    # Not top level there: the library and its `imgui` target (ImWchar width
    # and rasteriser by the root's own options), no tests or examples.
    # Asked for: included by another project, the root builds no ImGui of its
    # own unless told to, and this library is the whole program's ImGui.
    set(IMTYPESET_BUILD_IMGUI ON)
    add_subdirectory("${_py_repo}" imtypeset)
    enable_testing()
else()
    # A static library that is linked into a shared one.
    set_target_properties(imtypeset PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(TARGET imgui)
        get_target_property(_py_imgui_aliased imgui ALIASED_TARGET)
        get_target_property(_py_imgui_imported imgui IMPORTED)
        if(NOT _py_imgui_aliased AND NOT _py_imgui_imported)
            set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()
endif()

set(IMTYPESET_SDL2_DIR "" CACHE PATH
    "An SDL2 to use when find_package(SDL2 CONFIG) finds none: an install prefix, or a build tree laid out as example-project's (lib/libSDL2[d].a, _deps/sdl2-build/include*)")
set(IMTYPESET_PYTHON "" CACHE FILEPATH
    "The interpreter the python_binding test runs under (3.11 or later). Empty: ../imgui-adv-text/.venv/bin/python if it exists, else python3 on the PATH.")

# --- SDL2 -----------------------------------------------------------------------
# As examples/demo_sdl2 finds it: an installed SDL2 first, then
# IMTYPESET_SDL2_DIR read as an install prefix and then as a build tree that
# was never installed and so has no SDL2Config.cmake.
if(IMTYPESET_SDL2_DIR AND NOT IS_ABSOLUTE "${IMTYPESET_SDL2_DIR}")
    get_filename_component(_py_abs "${IMTYPESET_SDL2_DIR}" ABSOLUTE BASE_DIR "${_py_repo}")
    set(IMTYPESET_SDL2_DIR "${_py_abs}")
endif()
add_library(imtypeset_pyhost_sdl INTERFACE)
set(_py_sdl_found "")
if(NOT IMTYPESET_SDL2_DIR)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        target_link_libraries(imtypeset_pyhost_sdl INTERFACE SDL2::SDL2)
        set(_py_sdl_found "find_package(SDL2 CONFIG): SDL2::SDL2")
    elseif(TARGET SDL2::SDL2-static)
        target_link_libraries(imtypeset_pyhost_sdl INTERFACE SDL2::SDL2-static)
        set(_py_sdl_found "find_package(SDL2 CONFIG): SDL2::SDL2-static")
    endif()
else()
    find_library(_py_sdl_lib NAMES SDL2 SDL2d SDL2-static
        PATHS "${IMTYPESET_SDL2_DIR}/lib" NO_DEFAULT_PATH)
    find_path(_py_sdl_inc SDL.h PATHS
        "${IMTYPESET_SDL2_DIR}/include/SDL2"
        "${IMTYPESET_SDL2_DIR}/_deps/sdl2-build/include/SDL2"
        NO_DEFAULT_PATH)
    file(GLOB _py_sdl_cfg "${IMTYPESET_SDL2_DIR}/_deps/sdl2-build/include-config-*/SDL2")
    if(_py_sdl_lib AND _py_sdl_inc)
        target_include_directories(imtypeset_pyhost_sdl SYSTEM INTERFACE
            "${_py_sdl_inc}" ${_py_sdl_cfg})
        target_link_libraries(imtypeset_pyhost_sdl INTERFACE "${_py_sdl_lib}")
        if(_py_sdl_lib MATCHES "\\.a$")
            # A static SDL2 on Linux opens X11, Wayland, GL, ALSA and the rest
            # with dlopen at run time, so this is all it needs at link time. It
            # has to have been compiled as position-independent code
            # (SDL_STATIC_PIC) to go into a shared library at all.
            find_package(Threads REQUIRED)
            target_link_libraries(imtypeset_pyhost_sdl INTERFACE
                Threads::Threads ${CMAKE_DL_LIBS} m)
        endif()
        set(_py_sdl_found "${_py_sdl_lib}, headers ${_py_sdl_inc}")
    endif()
endif()
if(NOT _py_sdl_found)
    message(FATAL_ERROR
        "imtypeset python host: no SDL2. Install one that find_package(SDL2 CONFIG) finds, or "
        "pass -DIMTYPESET_SDL2_DIR=<install prefix or build tree>.")
endif()
message(STATUS "imtypeset python host: SDL2 from ${_py_sdl_found}")

# --- ImGui's backends -------------------------------------------------------------
# From the checkout the `imgui` target itself is built from, wherever that is.
get_target_property(_py_imgui_includes imgui INTERFACE_INCLUDE_DIRECTORIES)
set(_py_imgui_dir "")
foreach(_dir IN LISTS _py_imgui_includes)
    if(EXISTS "${_dir}/backends/imgui_impl_sdl2.cpp")
        set(_py_imgui_dir "${_dir}")
    endif()
endforeach()
if(NOT _py_imgui_dir)
    message(FATAL_ERROR
        "imtypeset python host: none of the imgui target's include directories "
        "(${_py_imgui_includes}) has backends/imgui_impl_sdl2.cpp")
endif()

# --- The library ------------------------------------------------------------------
add_library(imtypeset_pyhost SHARED
    host/imtypeset_host.cpp
    "${_py_imgui_dir}/backends/imgui_impl_sdl2.cpp"
    "${_py_imgui_dir}/backends/imgui_impl_sdlrenderer2.cpp"
    "${_py_imgui_dir}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imtypeset_pyhost PRIVATE host)
target_include_directories(imtypeset_pyhost SYSTEM PRIVATE "${_py_imgui_dir}/backends")
set_target_properties(imtypeset_pyhost PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(host/imtypeset_host.cpp PROPERTIES COMPILE_OPTIONS
        "-Wall;-Wextra;-Wpedantic;-Wshadow;-fno-exceptions;-fno-rtti")
endif()

# All of imtypeset, not only what the host's own file happens to call: the C
# ABI is reached from Python by name and by nothing in this link.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(imtypeset_pyhost PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,imtypeset>")
else()
    add_dependencies(imtypeset_pyhost imtypeset)
    target_link_libraries(imtypeset_pyhost PRIVATE
        -Wl,--whole-archive "$<TARGET_FILE:imtypeset>" -Wl,--no-whole-archive)
endif()
target_link_libraries(imtypeset_pyhost PRIVATE imgui imtypeset_pyhost_sdl ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    target_link_options(imtypeset_pyhost PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/host/exports.map")
    # A sanitised shared library leaves the sanitiser's runtime undefined, to
    # be found in the process that loads it (LD_PRELOAD under Python).
    if(NOT "${CMAKE_CXX_FLAGS}" MATCHES "-fsanitize")
        target_link_options(imtypeset_pyhost PRIVATE -Wl,--no-undefined)
    endif()
    set_property(TARGET imtypeset_pyhost APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/host/exports.map")
endif()

# --- The test -----------------------------------------------------------------------
# tests/python/test_binding.py, headless: SDL's dummy video driver and its
# software renderer, so it needs no display. It uses the standard library only
# and installs nothing. Skipped, with the reason printed, when there is no
# interpreter to run it.
set(_py_python "${IMTYPESET_PYTHON}")
if(NOT _py_python)
    if(EXISTS "${_py_repo}/../imgui-adv-text/.venv/bin/python")
        get_filename_component(_py_python "${_py_repo}/../imgui-adv-text/.venv/bin/python" ABSOLUTE)
    else()
        find_program(_py_python_found NAMES python3 python)
        if(_py_python_found)
            set(_py_python "${_py_python_found}")
        endif()
    endif()
endif()
if(_py_python AND EXISTS "${_py_python}")
    message(STATUS "imtypeset python host: test interpreter ${_py_python}")
    add_test(NAME python_binding
        COMMAND "${_py_python}" -P -B "${_py_repo}/tests/python/test_binding.py")
    set_tests_properties(python_binding PROPERTIES
        TIMEOUT 600
        SKIP_RETURN_CODE 77
        ENVIRONMENT "IMTYPESET_LIBRARY=$<TARGET_FILE:imtypeset_pyhost>;PYTHONDONTWRITEBYTECODE=1;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
else()
    message(STATUS "imtypeset python host: no interpreter, python_binding will be skipped")
    add_test(NAME python_binding
        COMMAND "${CMAKE_COMMAND}" -E echo
            "SKIPPED: no Python interpreter ('${_py_python}'); pass -DIMTYPESET_PYTHON=<python 3.11 or later>")
    set_tests_properties(python_binding PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endif()
