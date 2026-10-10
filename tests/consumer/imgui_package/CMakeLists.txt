# A stand-in for an installed Dear ImGui package: the static library, imgui.h
# and its neighbours, and a config that defines imgui::imgui. Dear ImGui ships
# no CMake files of its own; package managers write theirs, and vcpkg's port
# gives the same target name. This one exists so that the install route of
# imtypeset can be tested on a machine with no package manager, and is no model
# of how to package ImGui.
cmake_minimum_required(VERSION 3.21)
project(imgui_package_fixture LANGUAGES CXX)

set(IMGUI_DIR "" CACHE PATH "The Dear ImGui checkout to package")
option(IMGUI_FIXTURE_WCHAR32 "Define IMGUI_USE_WCHAR32 on the packaged target" ON)
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "IMGUI_DIR='${IMGUI_DIR}' has no imgui.h")
endif()

set(CMAKE_CXX_STANDARD 17)
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp")
target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${IMGUI_DIR}>
    $<INSTALL_INTERFACE:include>)
if(IMGUI_FIXTURE_WCHAR32)
    target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
endif()

install(TARGETS imgui EXPORT imguiTargets ARCHIVE DESTINATION lib)
install(FILES
    "${IMGUI_DIR}/imgui.h" "${IMGUI_DIR}/imconfig.h" "${IMGUI_DIR}/imgui_internal.h"
    "${IMGUI_DIR}/imstb_textedit.h" "${IMGUI_DIR}/imstb_rectpack.h" "${IMGUI_DIR}/imstb_truetype.h"
    DESTINATION include)
install(EXPORT imguiTargets NAMESPACE imgui:: DESTINATION lib/cmake/imgui)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/imgui-config.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/imguiTargets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/imgui-config.cmake" DESTINATION lib/cmake/imgui)
