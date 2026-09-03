dillen_add_component(
    project_dillen_hoi3_worldbuilder
    SOURCES
        world_builder.cpp
        world_builder.hpp
        hoi3_world_state.cpp
        hoi3_world_state.hpp
        political_snapshot.cpp
        political_snapshot.hpp
        hoi3_1936_content_emitter.cpp
        hoi3_1936_content_emitter.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS
        dillen::compatibility::hoi3_content
        dillen::kernel
        dillen::world
)

add_library(
    dillen::compatibility::hoi3_worldbuilder
    ALIAS project_dillen_hoi3_worldbuilder
)
