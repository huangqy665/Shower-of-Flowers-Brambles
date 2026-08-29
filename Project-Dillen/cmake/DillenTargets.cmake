include(CMakeParseArguments)

function(dillen_define_project_options)
    if(TARGET dillen_project_options)
        return()
    endif()

    add_library(dillen_project_options INTERFACE)
    add_library(dillen::project_options ALIAS dillen_project_options)
    target_compile_features(dillen_project_options INTERFACE cxx_std_17)

    if(WIN32)
        target_compile_definitions(
            dillen_project_options
            INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX
        )
    endif()

    if(MSVC)
        target_compile_options(
            dillen_project_options
            INTERFACE /utf-8 /Zc:__cplusplus /EHsc
        )
    endif()
endfunction()

function(dillen_configure_target target)
    target_link_libraries(${target} PRIVATE dillen::project_options)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Project Dillen"
    )
endfunction()

function(dillen_add_component target)
    cmake_parse_arguments(
        ARG
        ""
        ""
        "SOURCES;PUBLIC_LINKS;PRIVATE_LINKS;PUBLIC_INCLUDE_DIRS"
        ${ARGN}
    )

    add_library(${target} STATIC ${ARG_SOURCES})
    dillen_configure_target(${target})
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
