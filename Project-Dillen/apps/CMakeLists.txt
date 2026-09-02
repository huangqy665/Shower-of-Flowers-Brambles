# Applications.
#
# Deliberately NOT under src/. architecture_guard_probe walks src/ and resolves
# every quoted include to an owning module; an executable that wires four
# libraries together is not one of them, and nothing links it.
#
# Only configured with DILLEN_BUILD_MAP_RENDERER, because the only application
# here needs a window.

add_executable(dillen_map_viewer map_viewer/main.cpp)
target_link_libraries(
    dillen_map_viewer
    PRIVATE
        dillen::host
        dillen::presentation
        dillen::presentation::gl
)
dillen_configure_target(dillen_map_viewer)
