dillen_add_component(
    project_dillen_persistence
    SOURCES
        deterministic_replay.cpp
        deterministic_replay.hpp
        runtime_migration.cpp
        runtime_migration.hpp
        runtime_persistence.cpp
        runtime_persistence.hpp
        runtime_save_codec.cpp
        runtime_save_codec.hpp
        runtime_save_image.cpp
        runtime_save_image.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS dillen::runtime
)

add_library(dillen::persistence ALIAS project_dillen_persistence)
