add_library(
    scripted_gui_overlay
    SHARED
        scripted_gui_overlay.def
        scripted_gui_overlay_dll.cpp
)
new_core_configure_target(scripted_gui_overlay)
set_target_properties(
    scripted_gui_overlay
    PROPERTIES OUTPUT_NAME hoi3_new_core
)
target_compile_definitions(
    scripted_gui_overlay
    PRIVATE SCRIPTED_GUI_OVERLAY_EXPORTS
)
target_link_libraries(
    scripted_gui_overlay
    PRIVATE
        new_core::api
        new_core_handshake
        new_core_runtime
)
