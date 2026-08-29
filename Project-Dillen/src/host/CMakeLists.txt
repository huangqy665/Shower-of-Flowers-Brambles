dillen_add_component(
    project_dillen_host
    SOURCES
        cli_inspector.cpp
        cli_inspector.hpp
        standalone_session.cpp
        standalone_session.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS
        dillen::authoring
        dillen::world
        dillen::runtime
        dillen::persistence
)

add_library(dillen::host ALIAS project_dillen_host)

add_executable(project_dillen_cli standalone_main.cpp)
dillen_configure_target(project_dillen_cli)
target_link_libraries(project_dillen_cli PRIVATE dillen::host)
set_target_properties(
    project_dillen_cli
    PROPERTIES
        OUTPUT_NAME "project-dillen"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
