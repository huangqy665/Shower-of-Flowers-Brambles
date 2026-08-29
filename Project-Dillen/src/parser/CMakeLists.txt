dillen_add_component(
    project_dillen_parser
    SOURCES
        diagnostic.cpp
        diagnostic.hpp
        file_catalog.cpp
        file_catalog.hpp
        lexer.cpp
        lexer.hpp
        parse_result.hpp
        parser_cursor.cpp
        parser_cursor.hpp
        path_pattern.cpp
        path_pattern.hpp
        parser_registry.cpp
        parser_registry.hpp
        resolver.cpp
        resolver.hpp
        source_buffer.cpp
        source_buffer.hpp
        template.hpp
        template_registry.cpp
        template_registry.hpp
        token.cpp
        token.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(dillen::parser ALIAS project_dillen_parser)
