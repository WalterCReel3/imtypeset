# imtypeset_find_sdl2(<target> <who>)
#
# Makes the INTERFACE target <target> carry an SDL2: its include directory (the
# one holding SDL.h, since Dear ImGui's backends write <SDL.h>) and what to
# link. For the two things in this tree that open a window, examples/demo_sdl2
# and python/; the library itself never sees SDL. <who> prefixes the messages.
#
# The first of these that answers is used, and the STATUS line says which:
#
#   1. IMTYPESET_SDL2_DIR, when the caller names a directory. It is read as an
#      install prefix (lib/, include/SDL2/) and then as a CMake build tree of
#      SDL2 that was never installed and so has no SDL2Config.cmake: its
#      headers are split between the generated include/ and the
#      per-configuration SDL_config.h, and its static library is linked by
#      path. Naming a directory that holds no SDL2 is an error: nothing below
#      is tried in its place.
#   2. find_package(SDL2 CONFIG), the package configuration SDL2 installs
#      (2.0.12 and later define targets): SDL2::SDL2, else SDL2::SDL2-static.
#   3. The same package when it defines no target, as the configurations
#      older distributions shipped do: SDL2_INCLUDE_DIRS and SDL2_LIBRARIES.
#   4. pkg-config's `sdl2` module.
#   5. .deps/sdl2 of this tree, untracked, laid out as an install prefix
#      (tools/vendor_sdl2.sh fills it), read as in 1.
#
# An installed SDL2 comes before .deps/sdl2 so that a machine with the
# distribution's package (libsdl2-dev, SDL2-devel) needs nothing else.
function(imtypeset_find_sdl2 target who)
    get_filename_component(_repo "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(${target} INTERFACE)
    set(_route "")
    set(_dir "${IMTYPESET_SDL2_DIR}")
    set(_dir_origin "IMTYPESET_SDL2_DIR")

    if(NOT _dir)
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(${target} INTERFACE SDL2::SDL2)
            set(_route "find_package(SDL2 CONFIG) in ${SDL2_DIR}: target SDL2::SDL2")
        elseif(TARGET SDL2::SDL2-static)
            target_link_libraries(${target} INTERFACE SDL2::SDL2-static)
            set(_route "find_package(SDL2 CONFIG) in ${SDL2_DIR}: target SDL2::SDL2-static")
        elseif(SDL2_FOUND AND SDL2_LIBRARIES)
            # Some of those configurations leave a trailing space in
            # SDL2_LIBRARIES, which target_link_libraries refuses (CMP0004).
            string(STRIP "${SDL2_LIBRARIES}" _libs)
            target_include_directories(${target} SYSTEM INTERFACE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(${target} INTERFACE ${_libs})
            set(_route "find_package(SDL2 CONFIG) in ${SDL2_DIR}: no target, SDL2_LIBRARIES [${_libs}], SDL2_INCLUDE_DIRS [${SDL2_INCLUDE_DIRS}]")
        else()
            find_package(PkgConfig QUIET)
            if(PKG_CONFIG_FOUND)
                pkg_check_modules(IMTYPESET_SDL2_PC QUIET IMPORTED_TARGET sdl2)
            endif()
            if(TARGET PkgConfig::IMTYPESET_SDL2_PC)
                target_link_libraries(${target} INTERFACE PkgConfig::IMTYPESET_SDL2_PC)
                set(_route "pkg-config: sdl2 ${IMTYPESET_SDL2_PC_VERSION}, libraries [${IMTYPESET_SDL2_PC_LINK_LIBRARIES}]")
            elseif(EXISTS "${_repo}/.deps/sdl2/lib")
                set(_dir "${_repo}/.deps/sdl2")
                set(_dir_origin ".deps/sdl2")
            endif()
        endif()
    endif()

    if(_dir)
        # Cached per target and per directory: a second configure that names
        # another directory must not be answered from the first one's cache.
        string(MD5 _key "${target};${_dir}")
        find_library(IMTYPESET_SDL2_LIB_${_key} NAMES SDL2 SDL2d SDL2-static
            PATHS "${_dir}/lib" NO_DEFAULT_PATH)
        find_path(IMTYPESET_SDL2_INC_${_key} SDL.h PATHS
            "${_dir}/include/SDL2"
            "${_dir}/_deps/sdl2-build/include/SDL2"
            NO_DEFAULT_PATH)
        mark_as_advanced(IMTYPESET_SDL2_LIB_${_key} IMTYPESET_SDL2_INC_${_key})
        set(_lib "${IMTYPESET_SDL2_LIB_${_key}}")
        set(_inc "${IMTYPESET_SDL2_INC_${_key}}")
        file(GLOB _cfg "${_dir}/_deps/sdl2-build/include-config-*/SDL2")
        if(_lib AND _inc)
            target_include_directories(${target} SYSTEM INTERFACE "${_inc}" ${_cfg})
            target_link_libraries(${target} INTERFACE "${_lib}")
            if(_lib MATCHES "\\.a$")
                # A static SDL2 on Linux opens X11, Wayland, GL, ALSA and the
                # rest with dlopen at run time, so this is all it needs at link
                # time. To go into a shared library (python/) it has to have
                # been compiled as position-independent code (SDL_STATIC_PIC).
                find_package(Threads REQUIRED)
                target_link_libraries(${target} INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
            endif()
            set(_route "${_dir_origin}: ${_lib}, headers ${_inc}")
        elseif(IMTYPESET_SDL2_DIR)
            message(FATAL_ERROR
                "${who}: IMTYPESET_SDL2_DIR='${_dir}' holds no SDL2: looked for libSDL2, libSDL2d "
                "or libSDL2-static in lib/, and for SDL.h in include/SDL2/ or "
                "_deps/sdl2-build/include/SDL2/.")
        endif()
    endif()

    if(NOT _route)
        message(FATAL_ERROR
            "${who}: no SDL2 found. Looked for, in this order: a CMake package "
            "(find_package(SDL2 CONFIG)), pkg-config's `sdl2` module, and "
            "${_repo}/.deps/sdl2. Any one of these supplies it: install the distribution's "
            "development package (libsdl2-dev on Debian and Ubuntu, SDL2-devel on Fedora and "
            "RHEL); or run tools/vendor_sdl2.sh <SDL2 install prefix or build tree> to fill "
            ".deps/sdl2; or pass -DIMTYPESET_SDL2_DIR=<install prefix or build tree>.")
    endif()
    message(STATUS "${who}: SDL2 from ${_route}")
endfunction()
