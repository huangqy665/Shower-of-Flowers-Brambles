#pragma once

#include <string>

#include "algorithm_runtime.hpp"

namespace dillen::runtime {

enum class ControlledScriptStatus
{
    Completed,
    Preempted,
    RuntimeStateInvalid,
    InstructionBudgetExceeded,
    MemoryQuotaExceeded,
    ExecutionRejected
};

struct ControlledScriptResult
{
    ControlledScriptStatus status = ControlledScriptStatus::Completed;
    std::string message;
    kernel::WorldTransaction transaction;

    explicit operator bool() const noexcept;
};

class ControlledScriptVm
{
public:
    ControlledScriptResult Execute(
        const kernel::CompiledControlledScriptProgram& program,
        kernel::AlgorithmEntryPoint entryPoint,
        const AlgorithmInvocationContext& context,
        const kernel::AlgorithmExecutionPolicy& policy
    ) const;
};

}
