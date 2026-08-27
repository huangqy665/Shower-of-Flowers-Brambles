new_core_add_component(
    project_dillen_worldbuilder
    SOURCES
        world_builder.cpp
        world_builder.hpp
        world_state.cpp
        world_state.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS
        dillen::content
        dillen::kernel
)

add_library(dillen::worldbuilder ALIAS project_dillen_worldbuilder)
