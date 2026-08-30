#pragma once

#include <cstdint>

namespace dillen::kernel {

enum class AlgorithmFaultStage
{
    Create,
    Tick,
    Event,
    Command,
    Destroy
};

enum class AlgorithmFailurePolicy
{
    IsolateInstance,
    PauseInstance,
    FailInstance
};

enum class AlgorithmFaultCode
{
    None = 0,
    InstructionBudgetExceeded = 1,
    WallClockTimeoutLegacy = 2,
    ContractUnavailable = 3,
    BackendUnavailable = 4,
    ExecutionRejected = 5,
    ExecutorException = 6,
    TransactionRejected = 7,
    ScriptMemoryQuotaExceeded = 8
};

struct AlgorithmExecutionPolicy
{
    std::uint32_t instructionBudget = 4096;
    std::uint32_t wallClockWarningMicroseconds = 50000;
    AlgorithmFailurePolicy failurePolicy =
        AlgorithmFailurePolicy::FailInstance;
    std::uint32_t scriptSliceInstructionBudget = 256;
    std::uint32_t scriptMemoryLimitBytes = 65536;
};

struct AlgorithmFaultState
{
    bool isolated = false;
    std::uint32_t failureCount = 0;
    AlgorithmFaultCode code = AlgorithmFaultCode::None;
    AlgorithmFaultStage stage = AlgorithmFaultStage::Tick;
    std::uint64_t tick = 0;
};

bool IsValidAlgorithmExecutionPolicy(
    const AlgorithmExecutionPolicy& policy
) noexcept;
bool IsAuthoritativeAlgorithmFaultCode(
    AlgorithmFaultCode code
) noexcept;
bool operator==(
    const AlgorithmFaultState& first,
    const AlgorithmFaultState& second
) noexcept;
bool operator!=(
    const AlgorithmFaultState& first,
    const AlgorithmFaultState& second
) noexcept;

}
