# One executable per bench/bench_*.cpp, found by glob. Not ctest tests: a timing
# is only worth reading from the release preset, and nothing here asserts one.
#
# With IMTYPESET_MODULES narrowed the library is partial, so benchmarks are built
# only when IMTYPESET_BENCHES names them.
if(IMTYPESET_MODULES AND NOT IMTYPESET_BENCHES)
    return()
endif()

file(GLOB _imtypeset_all_benches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bench_*.cpp")
imtypeset_select(_imtypeset_benches "benchmark" "${IMTYPESET_BENCHES}" ${_imtypeset_all_benches})

# What a report has to print next to a number for it to mean anything. The flags
# are the global ones plus the build type's; what imtypeset_build_flags adds
# (warnings, -fno-exceptions -fno-rtti, -ffp-contract=off) is not in the string.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _imtypeset_build_type_upper)
string(STRIP "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_imtypeset_build_type_upper}}" _imtypeset_bench_flags)

foreach(_source IN LISTS _imtypeset_benches)
    get_filename_component(_name "${_source}" NAME_WE)
    imtypeset_program(${_name} "${_source}")
    target_compile_definitions(${_name} PRIVATE
        "IMTYPESET_BENCH_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
        "IMTYPESET_BENCH_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\""
        "IMTYPESET_BENCH_CXX_FLAGS=\"${_imtypeset_bench_flags}\"")
endforeach()
