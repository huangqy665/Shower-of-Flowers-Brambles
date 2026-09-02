dillen_add_component(
    project_dillen_presentation
    SOURCES
        map_index_raster.cpp
        map_index_raster.hpp
        map_view.cpp
        map_view.hpp
        presentation_view.cpp
        presentation_view.hpp
        province_projection.cpp
        province_projection.hpp
    PUBLIC_LINKS
        dillen::kernel
        dillen::world
        dillen::runtime
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(dillen::presentation ALIAS project_dillen_presentation)
