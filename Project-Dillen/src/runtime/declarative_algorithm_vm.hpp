#pragma once

#include <string>

#include "algorithm_execution_budget.hpp"
#include "algorithm_program.hpp"
#include "mechanism_instance.hpp"
#include "world_transaction.hpp"

namespace dillen::runtime {

struct AlgorithmInvocationContext;

enum class DeclarativeAlgorithmStatus
{
    Completed,
    StageMissing,
    InvalidFieldSlot,
    OperandTypeMismatch,
    NumericOverflow,
    LifecycleTransitionRejected,
    InstructionBudgetExceeded
};

struct DeclarativeAlgorithmResult
{
    DeclarativeAlgorithmStatus status =
        DeclarativeAlgorithmStatus::Completed;
    kernel::WorldTransaction transaction;
    std::string message;

    explicit operator bool() const noexcept;
};

class DeclarativeAlgorithmVm
{
public:
    DeclarativeAlgorithmResult Execute(
        const kernel::CompiledAlgorithmProgram& program,
        kernel::AlgorithmEntryPoint entryPoint,
        const AlgorithmInvocationContext& context
    ) const;
    DeclarativeAlgorithmResult Execute(
        const kernel::CompiledAlgorithmProgram& program,
        kernel::AlgorithmEntryPoint entryPoint,
        const kernel::MechanismInstance& instance,
        AlgorithmExecutionBudget& budget
    ) const;
};

}
