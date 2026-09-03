function(dillen_add_probe target)
    add_executable(${target} "${target}.cpp")
    dillen_configure_target(${target})
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(
        ${target}
        PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
    )
endfunction()

if(DILLEN_BUILD_STANDALONE_RUNTIME)
    dillen_add_probe(architecture_guard_probe)
    dillen_add_probe(mechanism_ids_probe dillen::kernel)
    dillen_add_probe(mechanism_registry_probe dillen::kernel)
    dillen_add_probe(ruleset_composition_probe dillen::kernel)
    dillen_add_probe(runtime_catalog_probe dillen::kernel)
    dillen_add_probe(
        algorithm_runtime_probe
        dillen::kernel
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(fixed_point_probe dillen::kernel)
    dillen_add_probe(algorithm_lifecycle_fault_probe dillen::runtime)
    dillen_add_probe(persistence_replay_probe dillen::persistence)
    dillen_add_probe(
        entity_world_slice_probe
        dillen::kernel
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(declarative_algorithm_vm_probe dillen::runtime)
    dillen_add_probe(
        capability_invocation_probe
        dillen::kernel
        dillen::world
        dillen::runtime
        dillen::persistence
    )
    dillen_add_probe(
        scale_probe
        dillen::kernel
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(
        thread_contract_probe
        dillen::kernel
        dillen::world
        dillen::runtime
        dillen::persistence
    )
    dillen_add_probe(
        controlled_script_probe
        dillen::runtime
        dillen::persistence
        dillen::authoring
    )
    dillen_add_probe(projection_adapter_probe dillen::adapter)
    dillen_add_probe(province_raster_import_probe dillen::adapter)
    dillen_add_probe(parser_syntax_probe dillen::parser)
    dillen_add_probe(parser_pipeline_probe dillen::parser)
    dillen_add_probe(
        authoring_pipeline_probe
        dillen::authoring
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(
        authoring_compile_golden_probe
        dillen::authoring
        dillen::kernel
        dillen::persistence
    )
    dillen_add_probe(
        dsl_read_operand_probe
        dillen::authoring
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(
        demo_0_5_vertical_slice_probe
        dillen::host
    )
    dillen_add_probe(
        authoring_frontend_golden_probe
        dillen::authoring
        dillen::kernel
        dillen::persistence
    )
    dillen_add_probe(
        authoring_diagnostic_contract_probe
        dillen::authoring
        dillen::parser
    )
    # Everything from here to world_map_scale_probe needs the
    # presentation layer, so none of it is registered without it. That is
    # what makes DILLEN_BUILD_PRESENTATION=OFF a real configuration
    # rather than a switch that breaks the build: a pure standalone tree
    # carries no presentation targets, no probes for them and none of the
    # third-party sources that exist only to serve them.
    if(DILLEN_BUILD_PRESENTATION)
        # Links dillen::presentation explicitly. dillen::standalone deliberately
        # does not, so this is the only target in the tree that pulls it in.
        dillen_add_probe(
            presentation_boundary_probe
            dillen::host
            dillen::presentation
        )
        dillen_add_probe(world_map_content_probe dillen::adapter)
    dillen_add_probe(
        province_map_emitter_probe
        dillen::adapter
        dillen::host
    )
        dillen_add_probe(
            text_atlas_probe
            dillen::host
            dillen::presentation
        )
        dillen_add_probe(
            control_tree_probe
            dillen::host
            dillen::presentation
        )
        dillen_add_probe(
            client_state_probe
            dillen::host
            dillen::presentation
            dillen::persistence
        )
        dillen_add_probe(
            map_command_probe
            dillen::host
            dillen::presentation
            dillen::persistence
        )
        dillen_add_probe(presentation_binding_probe dillen::host)
        dillen_add_probe(map_view_probe dillen::presentation)
        dillen_add_probe(
            map_camera_controller_probe
            dillen::presentation
        )
        dillen_add_probe(
            province_centroids_probe
            dillen::presentation
        )
        dillen_add_probe(
            presentation_lifecycle_probe
            dillen::host
            dillen::presentation
            dillen::persistence
        )
        dillen_add_probe(
            world_map_durability_probe
            dillen::host
            dillen::presentation
            dillen::persistence
        )
        # The GL smoke target.
        #
        # It IS registered with CTest, but with a skip code rather than a pass: on
        # a machine with no GPU or no display it exits 77 and CTest reports
        # "Skipped", which is the honest answer. It used to exit 0 there and to be
        # left out of the listing entirely -- so a machine that COULD have run it
        # never did, and a renderer regression could reach a green board.
        #
        # Everything it could otherwise prove is proved headless by map_view_probe,
        # map_index_raster_probe and province_projection_probe; what only it can
        # say is that the backend turns those results into the right pixels.
        if(DILLEN_BUILD_MAP_RENDERER)
            add_executable(map_renderer_smoke "map_renderer_smoke.cpp")
            dillen_configure_target(map_renderer_smoke)
            target_link_libraries(
                map_renderer_smoke
                PRIVATE
                    dillen::host
                    dillen::presentation
                    dillen::presentation::gl
                    dillen::persistence
            )
            add_test(NAME map_renderer_smoke COMMAND map_renderer_smoke)
            set_tests_properties(
                map_renderer_smoke
                PROPERTIES
                    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
                    SKIP_RETURN_CODE 77
            )
        endif()
        dillen_add_probe(
            map_index_raster_probe
            dillen::host
            dillen::presentation
            dillen::adapter
        )
        dillen_add_probe(
            province_projection_probe
            dillen::host
            dillen::presentation
        )
    endif()
    dillen_add_probe(world_map_scale_probe dillen::host)
    dillen_add_probe(standalone_host_probe dillen::host)
    dillen_add_probe(dillen_demo_1_0_probe dillen::host)
endif()

if(DILLEN_BUILD_HOI3_COMPATIBILITY)
    dillen_add_probe(
        mechanism_instance_store_probe
        dillen::kernel
        dillen::compatibility::hoi3_worldbuilder
    )
    dillen_add_probe(
        mechanism_transaction_probe
        dillen::kernel
        dillen::runtime
        dillen::compatibility::hoi3_worldbuilder
    )
    dillen_add_probe(
        mechanism_runtime_pipeline_probe
        dillen::kernel
        dillen::runtime
        dillen::compatibility::hoi3_worldbuilder
    )

    set(DILLEN_HOI3_PARSER_PROBES
        country_tag_slice_probe
        country_definition_slice_probe
        country_history_slice_probe
        province_region_slice_probe
        province_history_slice_probe
        unit_type_slice_probe
        technology_slice_probe
        unit_model_slice_probe
        order_of_battle_slice_probe
    )
    foreach(probe IN LISTS DILLEN_HOI3_PARSER_PROBES)
        dillen_add_probe(${probe} dillen::compatibility::hoi3_parser)
    endforeach()

    dillen_add_probe(
        diplomacy_history_slice_probe
        dillen::compatibility::hoi3_parser
        dillen::compatibility::hoi3_worldbuilder
    )
    dillen_add_probe(
        war_history_slice_probe
        dillen::compatibility::hoi3_parser
        dillen::compatibility::hoi3_worldbuilder
    )
    dillen_add_probe(
        world_builder_probe
        dillen::compatibility::hoi3_worldbuilder
    )
    dillen_add_probe(
        scenario_launch_pipeline_probe
        dillen::compatibility::hoi3_parser
        dillen::compatibility::hoi3_worldbuilder
    )
endif()
