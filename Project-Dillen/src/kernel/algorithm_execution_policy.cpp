#include "algorithm_execution_policy.hpp"

namespace dillen::kernel {

bool IsValidAlgorithmExecutionPolicy(
    const AlgorithmExecutionPolicy& policy
) noexcept
{
    return policy.instructionBudget > 0
        && policy.scriptSliceInstructionBudget > 0
        && policy.scriptMemoryLimitBytes > 0;
}

bool IsAuthoritativeAlgorithmFaultCode(
    AlgorithmFaultCode code
) noexcept
{
    switch (code)
    {
    case AlgorithmFaultCode::InstructionBudgetExceeded:
    case AlgorithmFaultCode::ContractUnavailable:
    case AlgorithmFaultCode::BackendUnavailable:
    case AlgorithmFaultCode::ExecutionRejected:
    case AlgorithmFaultCode::ExecutorException:
    case AlgorithmFaultCode::TransactionRejected:
    case AlgorithmFaultCode::ScriptMemoryQuotaExceeded:
        return true;
    case AlgorithmFaultCode::None:
    case AlgorithmFaultCode::WallClockTimeoutLegacy:
        return false;
    }
    return false;
}

bool operator==(
    const AlgorithmFaultState& first,
    const AlgorithmFaultState& second
) noexcept
{
    return first.isolated == second.isolated
        && first.failureCount == second.failureCount
        && first.code == second.code
        && first.stage == second.stage
        && first.tick == second.tick;
}

bool operator!=(
    const AlgorithmFaultState& first,
    const AlgorithmFaultState& second
) noexcept
{
    return !(first == second);
}

}
