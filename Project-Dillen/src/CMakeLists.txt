if(DILLEN_BUILD_HOI3_COMPATIBILITY
   AND NOT DILLEN_BUILD_STANDALONE_RUNTIME)
    message(FATAL_ERROR
        "DILLEN_BUILD_HOI3_COMPATIBILITY requires the standalone runtime"
    )
endif()

if(DILLEN_BUILD_STANDALONE_RUNTIME)
    add_subdirectory(kernel)
    add_subdirectory(world)
    add_subdirectory(runtime)
    add_subdirectory(persistence)
    add_subdirectory(parser)
    add_subdirectory(parser/parsers/dillen)
    add_subdirectory(adapter)
    add_subdirectory(host)

    add_library(project_dillen_standalone INTERFACE)
    add_library(dillen::standalone ALIAS project_dillen_standalone)
    target_link_libraries(
        project_dillen_standalone
        INTERFACE
            dillen::kernel
            dillen::world
            dillen::runtime
            dillen::persistence
            dillen::parser
            dillen::authoring
            dillen::adapter
    )

    if(DILLEN_BUILD_HOI3_COMPATIBILITY)
        add_subdirectory(compatibility/hoi3/content)
        add_subdirectory(parser/parsers/hoi3)
        add_subdirectory(compatibility/hoi3/worldbuilder)

        add_library(project_dillen_hoi3_compatibility INTERFACE)
        add_library(
            dillen::compatibility::hoi3
            ALIAS project_dillen_hoi3_compatibility
        )
        target_link_libraries(
            project_dillen_hoi3_compatibility
            INTERFACE
                dillen::standalone
                dillen::compatibility::hoi3_content
                dillen::compatibility::hoi3_parser
                dillen::compatibility::hoi3_worldbuilder
        )
    endif()
endif()
