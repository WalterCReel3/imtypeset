# A host that has its own Dear ImGui target and adds imtypeset with
# add_subdirectory. HOST_IMGUI says what the host calls its target:
#
#   imgui          a target named `imgui` (hello_imgui, most hand-written hosts)
#   imgui::imgui   a target of another name with the alias imgui::imgui
#   named          a target of another name, handed over in IMTYPESET_IMGUI_TARGET
#   none           no ImGui at all: the configure has to fail, and say why
#
# The host builds with CMAKE_UNITY_BUILD when HOST_UNITY is on, which is also
# the test that src/*.cpp survive being one translation unit in a host's build.
cmake_minimum_required(VERSION 3.21)
project(imtypeset_consumer_subdirectory LANGUAGES CXX)

set(IMGUI_DIR "" CACHE PATH "The host's Dear ImGui checkout")
set(IMTYPESET_DIR "" CACHE PATH "The imtypeset repository")
set(HOST_IMGUI "imgui" CACHE STRING "imgui, imgui::imgui, named or none")
option(HOST_UNITY "Build the host, imtypeset included, as unity translation units" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(HOST_UNITY)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 0)
endif()

if(NOT HOST_IMGUI STREQUAL "none")
    if(HOST_IMGUI STREQUAL "imgui")
        set(_target imgui)
    else()
        set(_target host_dear_imgui)
    endif()
    add_library(${_target} STATIC
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp")
    # A host's own choice, which the library has to follow: stock ImGui, 16-bit
    # ImWchar, stb_truetype.
    target_include_directories(${_target} SYSTEM PUBLIC "${IMGUI_DIR}")
    # ImGui's own sources are not what is under test.
    set_target_properties(${_target} PROPERTIES UNITY_BUILD OFF)
    if(HOST_IMGUI STREQUAL "imgui::imgui")
        add_library(imgui::imgui ALIAS ${_target})
    elseif(HOST_IMGUI STREQUAL "named")
        set(IMTYPESET_IMGUI_TARGET ${_target})
    endif()
endif()

add_subdirectory("${IMTYPESET_DIR}" imtypeset)

if(TARGET imgui AND NOT HOST_IMGUI STREQUAL "imgui")
    message(FATAL_ERROR "imtypeset built an `imgui` target inside a host that has its own")
endif()

add_executable(consumer "${IMTYPESET_DIR}/examples/minimal.cpp")
target_link_libraries(consumer PRIVATE imtypeset::imtypeset)
