# apps/map_mode_debug/CMakeLists.txt
#
# A windowed diagnostic for the map-mode polar fill. Same link set as the
# political viewer; not a test.

add_executable(
    dillen_map_mode_debug
    main.cpp
)

target_link_libraries(
    dillen_map_mode_debug
    PRIVATE
        dillen::host
        dillen::presentation
        dillen::presentation::gl
)

dillen_configure_target(dillen_map_mode_debug)
