new_core_add_component(
    new_core_core
    SOURCES
        core_hook_registry.cpp
        core_lifecycle.cpp
        core_module_registry.cpp
        core_hook_registry.h
        core_lifecycle.h
        core_module.h
        core_module_registry.h
)

new_core_add_component(
    new_core_capabilities
    SOURCES capability_registry.cpp capability_registry.h
    PUBLIC_LINKS new_core_engine
)

new_core_add_component(
    new_core_handshake
    SOURCES new_core_handshake.cpp new_core_handshake.h
)
