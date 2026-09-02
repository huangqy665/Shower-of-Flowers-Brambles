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
    if(DILLEN_BUILD_PRESENTATION)
        add_subdirectory(presentation)
        if(DILLEN_BUILD_MAP_RENDERER)
            add_subdirectory(presentation/gl)
        endif()
    endif()
    add_subdirectory(host)

    # NOTE: dillen::presentation is built but deliberately NOT linked into
    # dillen::standalone.
    #
    # The property this buys, stated exactly: NOTHING AUTHORITATIVE DEPENDS ON
    # PRESENTATION. dillen::standalone does not link it, and
    # architecture_guard_probe fails the build if any module includes one of
    # its headers, so removing it is a mechanical deletion -- the directory,
    # the add_subdirectory line just above, and the probes that test it --
    # rather than a refactor of anything that matters.
    #
    # DILLEN_BUILD_PRESENTATION=OFF is the same statement made at configure
    # time: no presentation targets, no probes that need them, and none of the
    # third-party sources that exist only for them. That option is what makes
    # the deletability claim checkable rather than asserted -- a pure
    # standalone build stopped carrying FreeType the moment it existed.
    #
    # Linking presentation here would lose the real property the moment
    # anything convenient leaked across, and nothing would notice. A probe that
    # needs it links dillen::presentation on its own.
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
