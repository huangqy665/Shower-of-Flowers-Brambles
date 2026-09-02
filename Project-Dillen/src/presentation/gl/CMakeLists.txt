# The GL backend, built only when DILLEN_BUILD_MAP_RENDERER is on.
#
# Kept as its own target rather than folded into dillen::presentation so that
# the read model, the morph and the raster loader -- all of which are covered
# by probes in the standard suite -- never acquire a dependency on SDL or on a
# GPU being present. Deleting this directory leaves everything else building.
dillen_add_component(
    project_dillen_presentation_gl
    SOURCES
        map_renderer.cpp
        map_renderer.hpp
    PUBLIC_LINKS
        dillen::presentation
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    project_dillen_presentation_gl
    PRIVATE SDL3::SDL3-static
)

add_library(
    dillen::presentation::gl
    ALIAS project_dillen_presentation_gl
)
