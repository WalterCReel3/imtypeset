# One executable per examples/*.cpp, found by glob.
#
# Each is also a ctest test named example_<name>, run with no arguments from its
# own scratch directory under the build tree: an example the README quotes has to
# keep running, not only compiling. So an example run with no arguments exits 0
# and writes nothing outside its working directory; one that produces files for
# the repository (docs/images) takes the destination as an argument.
#
# With IMTYPESET_MODULES narrowed the library is partial, so examples are built
# only when IMTYPESET_EXAMPLES names them.
if(IMTYPESET_MODULES AND NOT IMTYPESET_EXAMPLES)
    return()
endif()

file(GLOB _imtypeset_all_examples CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
imtypeset_select(_imtypeset_examples "example" "${IMTYPESET_EXAMPLES}" ${_imtypeset_all_examples})

foreach(_source IN LISTS _imtypeset_examples)
    get_filename_component(_name "${_source}" NAME_WE)
    set(_target example_${_name})
    imtypeset_program(${_target} "${_source}")
    set(_scratch "${CMAKE_CURRENT_BINARY_DIR}/run_${_name}")
    file(MAKE_DIRECTORY "${_scratch}")
    add_test(NAME ${_target} COMMAND ${_target} WORKING_DIRECTORY "${_scratch}")
    set_tests_properties(${_target} PROPERTIES TIMEOUT 600)
endforeach()

# The windowed host of the demo window needs SDL2 and ImGui's backends, which
# nothing else here does, so it is asked for: -DIMTYPESET_BUILD_DEMO_SDL2=ON.
# SDL2 is an installed one (its CMake package, then pkg-config), else .deps/sdl2
# of this tree, and -DIMTYPESET_SDL2_DIR=<prefix or build tree> names another
# over both (cmake/sdl2.cmake).
# examples/demo_sdl2/CMakeLists.txt is also a project of its own,
# which is the shorter way to just run the demo.
option(IMTYPESET_BUILD_DEMO_SDL2 "Build examples/demo_sdl2, the SDL2 host of the demo window" OFF)
if(IMTYPESET_BUILD_DEMO_SDL2)
    add_subdirectory(demo_sdl2)
endif()
