# Vendored third-party sources.
#
# Deliberately NOT under src/. architecture_guard_probe walks the source tree
# and resolves every quoted #include to an owning module; third-party code has
# no module and would either fail that check or force a hole in it. Keeping it
# outside means the guard stays exact.
#
# FreeType is always configured; SDL only when DILLEN_BUILD_MAP_RENDERER is on.
# See the note in the top-level CMakeLists: FreeType has no display dependency,
# so the text layer built on it is gated headless like everything else here,
# while SDL is a platform backend a headless build must not carry.

# FreeType, pinned at VER-2-13-3.
#
# Every optional dependency is off. The compressed-font and colour-font paths
# want zlib, bzip2, PNG, Brotli and HarfBuzz; a UI that draws Latin text from
# one vendored TTF needs none of them, and each would be a further dependency
# to vendor and pin.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory(freetype)

if(NOT DILLEN_BUILD_MAP_RENDERER)
    return()
endif()

# SDL, pinned at release-3.4.14.
#
# Everything below is off. A map renderer needs a window, a GL context, mouse
# and keyboard, and nothing else; leaving audio, haptics, sensors, camera and
# the rest enabled would multiply the build for subsystems no part of this
# project can reach.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)

set(SDL_OPENGL ON CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)

# No EXCLUDE_FROM_ALL: the whole directory is already behind
# DILLEN_BUILD_MAP_RENDERER, and excluding it as well only hides the target
# from the generator without saving anything.
add_subdirectory(SDL)
