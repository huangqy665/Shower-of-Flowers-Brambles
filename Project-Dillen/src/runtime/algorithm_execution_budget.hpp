#pragma once

#include <chrono>
#include <cstdint>

#include "algorithm_execution_policy.hpp"

namespace dillen::runtime {

enum class AlgorithmBudgetStatus
{
    Available,
    InstructionBudgetExceeded
};

struct AlgorithmBudgetReport
{
    AlgorithmBudgetStatus status = AlgorithmBudgetStatus::Available;
    std::uint64_t instructionsConsumed = 0;
    std::uint64_t elapsedMicroseconds = 0;
    bool wallClockWarningExceeded = false;
};

class AlgorithmExecutionBudget
{
public:
    explicit AlgorithmExecutionBudget(
        kernel::AlgorithmExecutionPolicy policy
    );

    bool Consume(std::uint32_t instructions = 1) noexcept;
    bool Checkpoint() noexcept;
    AlgorithmBudgetStatus Status() const noexcept;
    AlgorithmBudgetReport Report() const noexcept;

private:
    void UpdateElapsed() noexcept;

    kernel::AlgorithmExecutionPolicy policy_;
    std::chrono::steady_clock::time_point started_;
    AlgorithmBudgetReport report_;
};

}
