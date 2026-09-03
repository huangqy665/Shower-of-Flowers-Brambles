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
    DILLEN_BUILD_PRESENTATION
    "Build the presentation layer and the dependencies only it needs"
    ON
)
option(
    DILLEN_BUILD_MAP_RENDERER
    "Build the SDL/OpenGL map renderer and its vendored dependencies"
    OFF
)
option(
    DILLEN_BUILD_HOI3_ORACLE
    "Build the independent HOI3 observation and injection platform"
    OFF
)

# The renderer is a backend for the presentation layer; it cannot exist
# without it.
if(DILLEN_BUILD_MAP_RENDERER AND NOT DILLEN_BUILD_PRESENTATION)
    message(FATAL_ERROR
        "DILLEN_BUILD_MAP_RENDERER requires DILLEN_BUILD_PRESENTATION"
    )
endif()

dillen_define_project_options()

# Third-party sources exist for the presentation layer and nothing else, so a
# build without it configures none of them.
#
# Within that, FreeType is not behind DILLEN_BUILD_MAP_RENDERER and SDL is, and
# the split is not arbitrary. SDL is a PLATFORM backend: it needs a window and
# a display server, so a build that does not want one must not carry it.
# FreeType turns font bytes into coverage bitmaps and touches no display at
# all, which means the text layer it feeds is testable headless -- and a gate
# that only runs on a machine with a GPU is one the Linux cells never run.
if(DILLEN_BUILD_PRESENTATION)
    add_subdirectory(third_party)
endif()

add_subdirectory(src)

# Applications guard themselves.
#
# This used to be behind DILLEN_BUILD_MAP_RENDERER, because the only
# application here needed a window. A headless content generator does not, and
# leaving the whole directory behind the renderer switch would mean one could
# only be configured alongside SDL, a display and a GPU.
add_subdirectory(apps)

if(DILLEN_BUILD_HOI3_ORACLE)
    add_subdirectory(hoi3oracle)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
