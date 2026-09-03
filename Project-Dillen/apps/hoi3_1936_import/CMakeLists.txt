# apps/hoi3_1936_import/CMakeLists.txt
#
# dillen_configure_target carries the project's compile options, including the
# C++ standard. Setting a standard here instead would opt this binary out of
# the warning discipline the rest of the tree is held to, and mixing standards
# across translation units in one binary is an ODR hazard on standard library
# types rather than a formality.

add_executable(
    dillen_hoi3_1936_import
    main.cpp
)

target_link_libraries(
    dillen_hoi3_1936_import
    PRIVATE
        dillen::adapter
        dillen::compatibility::hoi3
)

dillen_configure_target(dillen_hoi3_1936_import)
