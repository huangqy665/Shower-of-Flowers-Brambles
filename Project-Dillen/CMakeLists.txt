cmake_minimum_required(VERSION 3.20)

project(hoi3_new_core VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NewCorePlatform)
include(NewCoreTargets)

new_core_require_windows_x86()
new_core_define_project_options()

file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.." NEW_CORE_MOD_ROOT_NATIVE)
file(TO_CMAKE_PATH "${NEW_CORE_MOD_ROOT_NATIVE}" NEW_CORE_MOD_ROOT)
set(SCRIPTED_GUI_HOI3_MOD_ROOT "${NEW_CORE_MOD_ROOT}")
configure_file(
    config/hoi3_scripted_gui_dev.mod.in
    "${CMAKE_BINARY_DIR}/hoi3_scripted_gui_dev.mod"
    @ONLY
)

get_filename_component(
    NEW_CORE_DEFAULT_HOI3_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../hoi3"
    ABSOLUTE
)
set(
    NEW_CORE_HOI3_ROOT
    "${NEW_CORE_DEFAULT_HOI3_ROOT}"
    CACHE PATH
    "HOI3 installation root used by integration probes"
)
set(
    NEW_CORE_LUA51_DLL
    "${NEW_CORE_HOI3_ROOT}/lua51.dll"
    CACHE FILEPATH
    "Lua 5.1 DLL used by gui_lua51_native_probe"
)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
