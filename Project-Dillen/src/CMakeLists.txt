add_library(new_core_api INTERFACE)
add_library(new_core::api ALIAS new_core_api)
target_include_directories(
    new_core_api
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dll"
)

add_subdirectory(engine)
add_subdirectory(core)
add_subdirectory(native)
add_subdirectory(kernel)
add_subdirectory(content)
add_subdirectory(parser)
add_subdirectory(worldbuilder)

new_core_add_component(
    new_core_reverse
    SOURCES
        core/reverse_probe_framework.cpp
        core/reverse_probe_framework.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/core"
    PUBLIC_LINKS
        new_core_core
        new_core_capabilities
        new_core_engine
        new_core_native
)

add_subdirectory(hoi3)
add_subdirectory(leader_capture)
add_subdirectory(gui)

new_core_add_component(
    new_core_runtime
    SOURCES
        core/core_runtime.cpp
        core/core_runtime.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/core"
    PUBLIC_LINKS
        new_core::api
        new_core_core
        new_core_capabilities
        new_core_engine
        new_core_native
        new_core_reverse
        new_core_hoi3
        new_core_leader_capture
        new_core_script_gui
)

add_subdirectory(dll)
add_subdirectory(launcher)
add_subdirectory(tools)
