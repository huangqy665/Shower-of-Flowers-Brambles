include(CMakeParseArguments)

function(new_core_define_project_options)
    if(TARGET new_core_project_options)
        return()
    endif()

    add_library(new_core_project_options INTERFACE)
    add_library(new_core::project_options ALIAS new_core_project_options)

    target_compile_features(
        new_core_project_options
        INTERFACE cxx_std_17
    )

    target_compile_definitions(
        new_core_project_options
        INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX
    )

    if(MSVC)
        target_compile_options(
            new_core_project_options
            INTERFACE /utf-8 /Zc:__cplusplus /EHsc
        )
    endif()
endfunction()

function(new_core_configure_target target)
    target_link_libraries(${target} PRIVATE new_core::project_options)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "New Core"
    )
endfunction()

function(new_core_add_component target)
    cmake_parse_arguments(
        ARG
        ""
        ""
        "SOURCES;PUBLIC_LINKS;PRIVATE_LINKS;PUBLIC_INCLUDE_DIRS"
        ${ARGN}
    )

    add_library(${target} STATIC ${ARG_SOURCES})
    new_core_configure_target(${target})
    target_compile_definitions(
        ${target}
        PUBLIC SCRIPTED_GUI_OVERLAY_STATIC
    )
    target_include_directories(
        ${target}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            ${ARG_PUBLIC_INCLUDE_DIRS}
    )
    target_link_libraries(
        ${target}
        PUBLIC ${ARG_PUBLIC_LINKS}
        PRIVATE ${ARG_PRIVATE_LINKS}
    )
endfunction()
