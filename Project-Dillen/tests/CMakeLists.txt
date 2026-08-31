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
    dillen_add_probe(parser_syntax_probe dillen::parser)
    dillen_add_probe(parser_pipeline_probe dillen::parser)
    dillen_add_probe(
        authoring_pipeline_probe
        dillen::authoring
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(
        dsl_read_operand_probe
        dillen::authoring
        dillen::world
        dillen::runtime
    )
    dillen_add_probe(
        demo_0_5_vertical_slice_probe
        dillen::authoring
        dillen::world
        dillen::runtime
    )
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
