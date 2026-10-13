# UNTESTED sketch of a vcpkg port for imtypeset. Written on a machine with no
# vcpkg and no network: no line of it has been run. What it is meant to do is
# what tests/consumer/run.cmake does, and that one is run: configure against an
# installed Dear ImGui package, install, be found by find_package.
#
# Not filled in, on purpose:
#   - SHA512: of the archive GitHub serves for the tag, which exists only
#     once a tag is pushed.
# The licence is MIT (LICENSE; docs/decisions.md, D82): "license" in
# vcpkg.json says so, and the copyright file installed below is LICENSE.
#
# Assumed and not checked: that vcpkg's imgui port installs a CMake package
# whose target is imgui::imgui (hello_imgui's build files rely on that name),
# and that its version scheme accepts "1.92.0" as a floor.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY) # as Dear ImGui's own port

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO WalterCReel3/imtypeset
    REF "v${VERSION}"
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DIMTYPESET_USE_IMGUI_PACKAGE=ON
        -DIMTYPESET_INSTALL=ON
        -DIMTYPESET_BUILD_TESTS=OFF
        -DIMTYPESET_BUILD_BENCH=OFF
        -DIMTYPESET_BUILD_EXAMPLES=OFF
        -DIMTYPESET_WARNINGS_AS_ERRORS=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME imtypeset CONFIG_PATH lib/cmake/imtypeset)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
