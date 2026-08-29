dillen_add_component(
    project_dillen_authoring
    SOURCES
        authoring_parser.cpp
        authoring_parser.hpp
        authoring_pipeline.cpp
        authoring_pipeline.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS
        dillen::parser
        dillen::kernel
)

add_library(dillen::authoring ALIAS project_dillen_authoring)
