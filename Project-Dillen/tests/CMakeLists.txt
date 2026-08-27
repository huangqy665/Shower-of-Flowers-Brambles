function(new_core_add_probe target)
    add_executable(${target} "${target}.cpp")
    new_core_configure_target(${target})
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(
        ${target}
        PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
    )
endfunction()

new_core_add_probe(core_infrastructure_probe new_core_core)
new_core_add_probe(engine_registry_probe new_core_engine)
new_core_add_probe(capability_registry_probe new_core_capabilities)
new_core_add_probe(native_save_load_barrier_probe new_core_native)
new_core_add_probe(native_effect_bridge_probe new_core_native)
new_core_add_probe(native_effect_core_module_probe new_core_native)
new_core_add_probe(native_object_resolver_probe new_core_native)
new_core_add_probe(native_query_service_probe new_core_native)
new_core_add_probe(reverse_probe_framework_probe new_core_reverse)
new_core_add_probe(hoi3_lifecycle_probe new_core_hoi3)
new_core_add_probe(hoi3_native_object_keys_probe new_core_hoi3)
new_core_add_probe(hoi3_gameplay_effects_probe new_core_hoi3)
new_core_add_probe(hoi3_native_queries_module_probe new_core_hoi3)
new_core_add_probe(leader_capture_core_module_probe new_core_leader_capture)
new_core_add_probe(mechanism_registry_probe dillen::kernel)
new_core_add_probe(
    mechanism_instance_store_probe
    dillen::kernel
    dillen::worldbuilder
)
new_core_add_probe(
    mechanism_transaction_probe
    dillen::kernel
    dillen::worldbuilder
)
new_core_add_probe(
    mechanism_runtime_pipeline_probe
    dillen::kernel
    dillen::worldbuilder
)
new_core_add_probe(parser_syntax_probe dillen::parser)
new_core_add_probe(parser_pipeline_probe dillen::parser)
new_core_add_probe(country_tag_slice_probe dillen::parser_hoi3)
new_core_add_probe(country_definition_slice_probe dillen::parser_hoi3)
new_core_add_probe(country_history_slice_probe dillen::parser_hoi3)
new_core_add_probe(province_region_slice_probe dillen::parser_hoi3)
new_core_add_probe(province_history_slice_probe dillen::parser_hoi3)
new_core_add_probe(unit_type_slice_probe dillen::parser_hoi3)
new_core_add_probe(technology_slice_probe dillen::parser_hoi3)
new_core_add_probe(unit_model_slice_probe dillen::parser_hoi3)
new_core_add_probe(order_of_battle_slice_probe dillen::parser_hoi3)
new_core_add_probe(
    diplomacy_history_slice_probe
    dillen::parser_hoi3
    dillen::worldbuilder
)
new_core_add_probe(
    war_history_slice_probe
    dillen::parser_hoi3
    dillen::worldbuilder
)
new_core_add_probe(world_builder_probe dillen::worldbuilder)
new_core_add_probe(
    scenario_launch_pipeline_probe
    dillen::parser_hoi3
    dillen::worldbuilder
)

set(NEW_CORE_GUI_PROBES
    declarative_gui_plugin_probe
    gui_application_bus_probe
    gui_custom_widget_probe
    gui_data_bridge_codec_probe
    gui_data_bridge_probe
    gui_data_provider_probe
    gui_declarative_data_probe
    gui_inprocess_application_probe
    gui_interpreter_probe
    gui_localization_probe
    gui_lua_bridge_probe
    gui_persistence_probe
    gui_plugin_manifest_probe
    gui_render_queue_probe
    gui_sequence_data_provider_probe
    gui_warmap_appointment_probe
    gui_window_manager_probe
    gui_window_session_probe
)
foreach(probe IN LISTS NEW_CORE_GUI_PROBES)
    new_core_add_probe(${probe} new_core::gui)
endforeach()

new_core_add_probe(gui_host_d3d9_probe new_core::gui)
set_tests_properties(gui_host_d3d9_probe PROPERTIES SKIP_RETURN_CODE 77)

add_executable(gui_lua51_native_probe gui_lua51_native_probe.cpp)
new_core_configure_target(gui_lua51_native_probe)
target_link_libraries(gui_lua51_native_probe PRIVATE new_core::gui)
add_test(
    NAME gui_lua51_native_probe
    COMMAND gui_lua51_native_probe "${NEW_CORE_LUA51_DLL}"
)
set_tests_properties(
    gui_lua51_native_probe
    PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
        SKIP_RETURN_CODE 77
)

add_executable(new_core_launcher_probe new_core_launcher_probe.cpp)
new_core_configure_target(new_core_launcher_probe)
target_link_libraries(
    new_core_launcher_probe
    PRIVATE new_core_launcher_support
)
add_test(
    NAME new_core_launcher_probe
    COMMAND new_core_launcher_probe
        "${PROJECT_SOURCE_DIR}/.."
        "$<TARGET_FILE:hoi3_new_core_launcher>"
        "$<TARGET_FILE:scripted_gui_overlay>"
        "${CMAKE_CURRENT_BINARY_DIR}/new_core_launcher_probe.ini"
)
set_tests_properties(
    new_core_launcher_probe
    PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
)
