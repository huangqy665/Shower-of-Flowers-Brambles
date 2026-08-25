new_core_add_component(
    new_core_launcher_support
    SOURCES new_core_launcher_core.cpp new_core_launcher_core.h
    PUBLIC_LINKS new_core_handshake
)

add_executable(hoi3_new_core_launcher WIN32 new_core_launcher.cpp)
new_core_configure_target(hoi3_new_core_launcher)
target_compile_definitions(
    hoi3_new_core_launcher
    PRIVATE UNICODE _UNICODE
)
target_link_libraries(
    hoi3_new_core_launcher
    PRIVATE
        new_core_launcher_support
        comctl32
        comdlg32
        shell32
        ole32
)

add_executable(scripted_gui_injector scripted_gui_injector.cpp)
new_core_configure_target(scripted_gui_injector)
