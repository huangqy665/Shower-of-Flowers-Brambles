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
            INTERFACE
                /utf-8
                /Zc:__cplusplus
                /EHsc
                /W4
                /permissive-
                # C4702 (unreachable code): MSVC /W4 flags the trailing
                # `return` of std::visit + `if constexpr` visitor lambdas as
                # unreachable for the alternatives whose branch always returns,
                # while it is still required for the alternatives that fall
                # through. GCC/Clang do not warn here. Disabled deliberately.
                /wd4702
        )
    else()
        # GCC / Clang. UTF-8 source, correct __cplusplus and the default
        # exception model are already the defaults, so only warnings are set.
        target_compile_options(
            dillen_project_options
            INTERFACE
                -Wall
                -Wextra
                -Wpedantic
                # -Wmissing-field-initializers (in -Wextra) fires on the
                # house style of brace-initialising the leading members of a
                # POD and letting the rest value-initialise -- e.g.
                # `MechanismReference{kind, type, value}` or the WorldCommand
                # factories. The omitted members are zero-initialised by the
                # standard, which is exactly what those call sites want, and
                # spelling every trailing field would make adding a member a
                # repo-wide edit. MSVC does not warn here. Disabled
                # deliberately, in the same spirit as /wd4702 above.
                -Wno-missing-field-initializers
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
