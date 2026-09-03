# apps/political_viewer/CMakeLists.txt

add_executable(
    dillen_political_viewer
    main.cpp
)

target_link_libraries(
    dillen_political_viewer
    PRIVATE
        dillen::host
        dillen::presentation
        dillen::presentation::gl
)

dillen_configure_target(dillen_political_viewer)
