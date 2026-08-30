dillen_add_component(
    project_dillen_runtime
    SOURCES
        algorithm_execution_budget.cpp
        algorithm_execution_budget.hpp
        algorithm_runtime.cpp
        algorithm_runtime.hpp
        bytecode_transaction.cpp
        bytecode_transaction.hpp
        controlled_script_vm.cpp
        controlled_script_vm.hpp
        declarative_algorithm_vm.cpp
        declarative_algorithm_vm.hpp
        kernel_runtime.cpp
        kernel_runtime.hpp
        mechanism_scheduler.cpp
        mechanism_scheduler.hpp
        world_query_snapshot.cpp
        world_query_snapshot.hpp
    PUBLIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC_LINKS
        dillen::kernel
        dillen::world
)

add_library(dillen::runtime ALIAS project_dillen_runtime)
