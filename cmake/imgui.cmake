# Builds a static `imgui` target: the core only, no platform or renderer backend,
# plus misc/freetype when IMTYPESET_USE_FREETYPE is on.
#
# The tag below is the one the `imgui` target made HERE is built from when
# nothing else is named (.deps/imgui, or the download), and the one every golden
# under tests/golden/draw was dumped against. It is not what the library
# requires of a host: include/imtypeset.h takes any Dear ImGui from
# IMTYPESET_IMGUI_VERSION_NUM_MIN up, no file under src/ includes
# imgui_internal.h, and IMTYPESET_IMGUI_DIR may name any checkout at or above
# that floor (cmake/imgui_version.cmake: an error below it, a warning above the
# tested version). The two numbers are the header's; the root CMakeLists.txt
# reads them from it and checks below that this tag is the tested one.
set(IMTYPESET_IMGUI_TAG         "v1.92.9")
set(IMTYPESET_IMGUI_COMMIT      "01380c579715e62fb9a8d6ec0502c4ea83bfde6e")
set(IMTYPESET_IMGUI_TAG_VERSION_NUM "19290")
if(NOT IMTYPESET_IMGUI_TAG_VERSION_NUM EQUAL IMTYPESET_IMGUI_VERSION_NUM)
    message(FATAL_ERROR
        "imtypeset: cmake/imgui.cmake pins ${IMTYPESET_IMGUI_TAG} (${IMTYPESET_IMGUI_TAG_VERSION_NUM}) and "
        "include/imtypeset.h says the tested version is ${IMTYPESET_IMGUI_VERSION_NUM}. Move them together.")
endif()

# Where the source comes from, first match wins:
#   1. IMTYPESET_IMGUI_DIR, a checkout the caller names;
#   2. .deps/imgui in this repository, an untracked copy of the pinned tag, so
#      that a new or wiped build directory needs no network;
#   3. FetchContent of the pinned commit from GitHub.
set(_imgui_vendored "${CMAKE_CURRENT_LIST_DIR}/../.deps/imgui")
get_filename_component(_imgui_vendored "${_imgui_vendored}" ABSOLUTE)

if(IMTYPESET_IMGUI_DIR)
    set(_imgui_dir "${IMTYPESET_IMGUI_DIR}")
    set(_imgui_origin "IMTYPESET_IMGUI_DIR")
    if(NOT EXISTS "${_imgui_dir}/imgui_internal.h")
        message(FATAL_ERROR "IMTYPESET_IMGUI_DIR='${_imgui_dir}' has no imgui_internal.h")
    endif()
elseif(EXISTS "${_imgui_vendored}/imgui_internal.h")
    set(_imgui_dir "${_imgui_vendored}")
    set(_imgui_origin ".deps/imgui")
else()
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${IMTYPESET_IMGUI_COMMIT}   # ${IMTYPESET_IMGUI_TAG}
        GIT_SHALLOW    FALSE)
    # ImGui ships no CMakeLists.txt, so this only downloads.
    FetchContent_MakeAvailable(imgui)
    set(_imgui_dir "${imgui_SOURCE_DIR}")
    set(_imgui_origin "FetchContent ${IMTYPESET_IMGUI_TAG}")
endif()

# A copy can go stale without anyone noticing, so the version is read out of the
# header that will be compiled and not taken from where the copy came from. A
# copy that claims to be the pinned tag and is not is an error: the draw goldens
# would fail for no visible reason.
include("${CMAKE_CURRENT_LIST_DIR}/imgui_version.cmake")
imtypeset_check_imgui_version("${_imgui_dir}/imgui.h" _imgui_version_num)
if(NOT IMTYPESET_IMGUI_DIR AND NOT _imgui_version_num EQUAL IMTYPESET_IMGUI_TAG_VERSION_NUM)
    message(FATAL_ERROR
        "imtypeset: ${_imgui_dir} (${_imgui_origin}) is Dear ImGui ${_imgui_version_num}, not the "
        "pinned ${IMTYPESET_IMGUI_TAG} (${IMTYPESET_IMGUI_TAG_VERSION_NUM}). Replace the copy, or "
        "name the checkout you mean with IMTYPESET_IMGUI_DIR.")
endif()
set(_imtypeset_imgui_version "${_imgui_version_num}")

add_library(imgui STATIC
    "${_imgui_dir}/imgui.cpp"
    "${_imgui_dir}/imgui_draw.cpp"
    "${_imgui_dir}/imgui_tables.cpp"
    "${_imgui_dir}/imgui_widgets.cpp")
# SYSTEM, so our warning flags are not applied to ImGui's headers.
target_include_directories(imgui SYSTEM PUBLIC "${_imgui_dir}")
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The one flag of ours this ImGui is given (D16, D52). The draw goldens compare
# vertices ImGui computes, and a compiler allowed to contract a*b+c (GCC once
# -march names a CPU with FMA, Clang by default) could move them on that
# machine only. PRIVATE, and only here: an ImGui target a host brings is
# compiled however the host compiles it, and the library's own results do not
# depend on it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imgui PRIVATE -ffp-contract=off)
elseif(MSVC)
    target_compile_options(imgui PRIVATE /fp:precise)
endif()

# ImWchar is 16 bits unless this is defined. ON is what the reference's ImGui
# has, and lets the atlas be asked for the math face's plane-1 and plane-15
# codepoints as they are; OFF is ImGui's default and what many hosts have, so the
# library has to work with both. PUBLIC because it changes the layout of ImGui's
# types: everything that includes imgui.h must agree.
if(IMTYPESET_IMGUI_WCHAR32)
    target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
endif()

# --- FreeType -------------------------------------------------------------------
# With IMGUI_ENABLE_FREETYPE compiled in, ImGui selects the FreeType loader for
# every atlas; without it, stb_truetype. The two rasterise differently, so which
# one a build has is printed below and is visible to code as IMGUI_ENABLE_FREETYPE.
if(IMTYPESET_USE_FREETYPE)
    if(NOT FREETYPE_FOUND)
        find_package(Freetype REQUIRED)
    endif()
    target_sources(imgui PRIVATE "${_imgui_dir}/misc/freetype/imgui_freetype.cpp")
    target_include_directories(imgui SYSTEM PUBLIC "${_imgui_dir}/misc/freetype")
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
    target_link_libraries(imgui PUBLIC Freetype::Freetype)
    set(_imgui_rasteriser "FreeType ${FREETYPE_VERSION_STRING}")
else()
    set(_imgui_rasteriser "stb_truetype")
endif()

message(STATUS "imtypeset: Dear ImGui ${_imgui_version_num} from ${_imgui_dir} (${_imgui_origin})")
message(STATUS "imtypeset: font rasteriser ${_imgui_rasteriser}")
