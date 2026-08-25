function(new_core_require_windows_x86)
    if(NOT WIN32)
        message(FATAL_ERROR "New Core supports only Windows hosts.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
            "New Core must be built as 32-bit x86. "
            "Configure Visual Studio with -A Win32 or use the windows-x86 preset."
        )
    endif()

    if(MSVC
       AND CMAKE_GENERATOR_PLATFORM
       AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
        message(FATAL_ERROR
            "Unsupported generator platform '${CMAKE_GENERATOR_PLATFORM}'; "
            "New Core requires Win32."
        )
    endif()
endfunction()
