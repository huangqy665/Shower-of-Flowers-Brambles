# Vendored third-party sources.
#
# Deliberately NOT under src/. architecture_guard_probe walks the source tree
# and resolves every quoted #include to an owning module; third-party code has
# no module and would either fail that check or force a hole in it. Keeping it
# outside means the guard stays exact.
#
# Only pulled in when DILLEN_BUILD_MAP_RENDERER is on. The renderer is optional
# by construction -- src/CMakeLists.txt keeps dillen::presentation out of
# dillen::standalone for the same reason -- so a build that does not want a
# window never configures, compiles or links any of this.

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
