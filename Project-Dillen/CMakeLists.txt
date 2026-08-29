cmake_minimum_required(VERSION 3.20)

project(ProjectDillen VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DillenTargets)

option(
    DILLEN_BUILD_STANDALONE_RUNTIME
    "Build the standalone Dillen parser, kernel, world and runtime"
    ON
)
option(
    DILLEN_BUILD_HOI3_COMPATIBILITY
    "Build the HOI3 compatibility content, parser and world builder"
    OFF
)
option(
    DILLEN_BUILD_HOI3_ORACLE
    "Build the independent HOI3 observation and injection platform"
    OFF
)

dillen_define_project_options()

add_subdirectory(src)

if(DILLEN_BUILD_HOI3_ORACLE)
    add_subdirectory(hoi3oracle)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
