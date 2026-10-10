# A host that uses an installed imtypeset: find_package, one target, nothing
# else. The ImGui it gets is the one the package was built against, found
# through the package's own find_dependency.
cmake_minimum_required(VERSION 3.21)
project(imtypeset_consumer_package LANGUAGES CXX)

set(IMTYPESET_DIR "" CACHE PATH "The imtypeset repository, for the example's source only")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imtypeset CONFIG REQUIRED)
message(STATUS "consumer: imtypeset ${imtypeset_VERSION}, built against Dear ImGui ${imtypeset_IMGUI_VERSION_NUM}")

add_executable(consumer "${IMTYPESET_DIR}/examples/minimal.cpp")
target_link_libraries(consumer PRIVATE imtypeset::imtypeset)
