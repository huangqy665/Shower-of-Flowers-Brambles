dillen_add_component(
    project_dillen_world
    SOURCES
        authoritative_world.cpp
        authoritative_world.hpp
        component_store.cpp
        component_store.hpp
        entity_registry.cpp
        entity_registry.hpp
        initial_world_builder.cpp
        initial_world_builder.hpp
        relation_index.cpp
        relation_index.hpp
        world_transaction_executor.cpp
        world_transaction_executor.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS dillen::kernel
)

add_library(dillen::world ALIAS project_dillen_world)
