dillen_add_component(
    project_dillen_adapter
    SOURCES
        adapter_migration.cpp
        adapter_migration.hpp
        projection_artifact.cpp
        projection_artifact.hpp
        province_content_emitter.cpp
        province_content_emitter.hpp
        province_raster_import.cpp
        province_raster_import.hpp
    PUBLIC_LINKS
        dillen::kernel
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(dillen::adapter ALIAS project_dillen_adapter)
