new_core_add_component(
    new_core_gui_model
    SOURCES
        model/gui_action_bridge.cpp
        model/gui_behavior.cpp
        model/gui_data.cpp
        model/gui_declarative_data.cpp
        model/gui_interpreter.cpp
        model/gui_runtime.cpp
        model/gui_action_bridge.h
        model/gui_behavior.h
        model/gui_data.h
        model/gui_data_value.h
        model/gui_declarative_data.h
        model/gui_interpreter.h
        model/gui_list_model.h
        model/gui_runtime.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/model"
)

new_core_add_component(
    new_core_gui_data
    SOURCES
        data/gui_data_bridge.cpp
        data/gui_data_bridge_codec.cpp
        data/gui_data_provider.cpp
        data/gui_file_data_provider.cpp
        data/gui_memory_data_bridge.cpp
        data/gui_sequence_data_provider.cpp
        data/gui_data_bridge.h
        data/gui_data_bridge_codec.h
        data/gui_data_provider.h
        data/gui_file_data_provider.h
        data/gui_memory_data_bridge.h
        data/gui_sequence_data_provider.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/data"
    PUBLIC_LINKS new_core_gui_model
)

new_core_add_component(
    new_core_gui_runtime
    SOURCES
        runtime/declarative_gui_plugin.cpp
        runtime/gui_application_bus.cpp
        runtime/gui_builtin_plugins.cpp
        runtime/gui_custom_widget.cpp
        runtime/gui_diagnostics.cpp
        runtime/gui_indexed_map_core.cpp
        runtime/gui_localization.cpp
        runtime/gui_persistence.cpp
        runtime/gui_plugin_manifest.cpp
        runtime/gui_plugin_registry.cpp
        runtime/gui_render_queue.cpp
        runtime/gui_tick.cpp
        runtime/gui_window_manager.cpp
        runtime/gui_window_session.cpp
        runtime/declarative_gui_plugin.h
        runtime/gui_application_bus.h
        runtime/gui_builtin_plugins.h
        runtime/gui_custom_widget.h
        runtime/gui_diagnostics.h
        runtime/gui_indexed_map_core.h
        runtime/gui_localization.h
        runtime/gui_persistence.h
        runtime/gui_plugin.h
        runtime/gui_plugin_manifest.h
        runtime/gui_plugin_registry.h
        runtime/gui_render_queue.h
        runtime/gui_tick.h
        runtime/gui_window_manager.h
        runtime/gui_window_session.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
    PUBLIC_LINKS new_core_gui_data new_core_gui_model
)

new_core_add_component(
    new_core_gui_lua
    SOURCES
        lua/gui_lua_bridge.cpp
        lua/gui_lua_native_binding.cpp
        lua/gui_lua51_hook.cpp
        lua/gui_inprocess_application.cpp
        lua/gui_lua_bridge.h
        lua/gui_lua_native_binding.h
        lua/gui_lua51_hook.h
        lua/gui_inprocess_application.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/lua"
    PUBLIC_LINKS
        new_core::api
        new_core_capabilities
        new_core_engine
        new_core_gui_data
        new_core_gui_runtime
        new_core_native
        new_core_reverse
)

new_core_add_component(
    new_core_gui_d3d9
    SOURCES
        d3d9/gui_custom_widget_d3d9.cpp
        d3d9/gui_d3d9_hook.cpp
        d3d9/gui_host_d3d9.cpp
        d3d9/gui_indexed_map_d3d9.cpp
        d3d9/gui_marker_layer_d3d9.cpp
        d3d9/gui_text_renderer_d3d9.cpp
        d3d9/gui_texture_loader_d3d9.cpp
        d3d9/gui_custom_widget_d3d9.h
        d3d9/gui_d3d9_hook.h
        d3d9/gui_host_d3d9.h
        d3d9/gui_indexed_map_d3d9.h
        d3d9/gui_marker_layer_d3d9.h
        d3d9/gui_text_renderer_d3d9.h
        d3d9/gui_texture_loader_d3d9.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/d3d9"
    PUBLIC_LINKS
        new_core::api
        new_core_engine
        new_core_gui_lua
        new_core_gui_runtime
        d3d9
        gdiplus
        user32
)

new_core_add_component(
    new_core_script_gui
    SOURCES
        module/script_gui_core_module.cpp
        module/script_gui_core_module.h
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/module"
    PUBLIC_LINKS
        new_core::api
        new_core_core
        new_core_gui_d3d9
        new_core_gui_lua
        new_core_native
        new_core_reverse
)

add_library(new_core_gui INTERFACE)
add_library(new_core::gui ALIAS new_core_gui)
target_link_libraries(
    new_core_gui
    INTERFACE
        new_core_gui_model
        new_core_gui_data
        new_core_gui_runtime
        new_core_gui_lua
        new_core_gui_d3d9
)
