# Applications.
#
# Deliberately NOT under src/. architecture_guard_probe walks src/ and resolves
# every quoted include to an owning module; an executable that wires several
# libraries together is not one of them, and nothing links it.
#
# Each application is guarded by what it actually needs rather than the
# directory being guarded as a whole -- one of them needs a window and the
# other needs a corpus, and neither implies the other.

# Need a window, a display and a GPU.
if(DILLEN_BUILD_MAP_RENDERER)
    add_executable(dillen_map_viewer map_viewer/main.cpp)
    target_link_libraries(
        dillen_map_viewer
        PRIVATE
            dillen::host
            dillen::presentation
            dillen::presentation::gl
    )
    dillen_configure_target(dillen_map_viewer)

    add_subdirectory(political_viewer)
endif()

# Needs the HOI3 compatibility layer and nothing else -- it reads a corpus and
# writes Dillen Content, with no window anywhere in it.
if(DILLEN_BUILD_HOI3_COMPATIBILITY)
    add_subdirectory(hoi3_1936_import)
endif()
