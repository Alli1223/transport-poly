cmake_minimum_required(VERSION 3.25)

project(transport_poly
    VERSION 0.1.0
    DESCRIPTION "Low poly transport simulation game"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(TP_BUILD_CLIENT "Build the graphical client" ON)
option(TP_BUILD_SERVER "Build the dedicated server" ON)
option(TP_BUILD_TESTS "Build the unit tests" ON)
option(TP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(TP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
