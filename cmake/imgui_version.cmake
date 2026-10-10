# imtypeset_check_imgui_version(<path to imgui.h> <out variable>)
#
# Reads IMGUI_VERSION_NUM out of the header that will be compiled and holds it
# against the two numbers include/imtypeset.h states (the root CMakeLists.txt
# reads them from there):
#
#   below IMTYPESET_IMGUI_VERSION_NUM_MIN   an error: the floor. The header has
#                                           the same check as an #error.
#   above IMTYPESET_IMGUI_VERSION_NUM       a warning: newer than anything the
#                                           suite has been run against.
#   in between                              accepted, and said.
function(imtypeset_check_imgui_version header out)
    file(STRINGS "${header}" _line REGEX "^#define[ \t]+IMGUI_VERSION_NUM[ \t]+[0-9]+")
    string(REGEX MATCH "[0-9]+" _num "${_line}")
    if(NOT _num)
        message(FATAL_ERROR "imtypeset: ${header} does not define IMGUI_VERSION_NUM")
    endif()
    if(_num LESS IMTYPESET_IMGUI_VERSION_NUM_MIN)
        message(FATAL_ERROR
            "imtypeset: ${header} is Dear ImGui ${_num}, and the oldest the library is written "
            "for is ${IMTYPESET_IMGUI_VERSION_NUM_MIN} (include/imtypeset.h, "
            "IMTYPESET_IMGUI_VERSION_NUM_MIN).")
    elseif(_num GREATER IMTYPESET_IMGUI_VERSION_NUM)
        message(WARNING
            "imtypeset: ${header} is Dear ImGui ${_num}, newer than the version the library is "
            "tested against (${IMTYPESET_IMGUI_VERSION_NUM}). It is expected to work; "
            "docs/distribution.md says what has been run against which version.")
    elseif(NOT _num EQUAL IMTYPESET_IMGUI_VERSION_NUM)
        message(STATUS
            "imtypeset: Dear ImGui ${_num} is at or above the floor "
            "(${IMTYPESET_IMGUI_VERSION_NUM_MIN}) and older than the tested version "
            "(${IMTYPESET_IMGUI_VERSION_NUM})")
    endif()
    set(${out} "${_num}" PARENT_SCOPE)
endfunction()
