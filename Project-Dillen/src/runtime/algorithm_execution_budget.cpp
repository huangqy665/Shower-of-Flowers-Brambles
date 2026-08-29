#include "algorithm_execution_budget.hpp"

#include <limits>

namespace dillen::runtime {

AlgorithmExecutionBudget::AlgorithmExecutionBudget(
    kernel::AlgorithmExecutionPolicy policy
)
    : policy_(policy),
      started_(std::chrono::steady_clock::now())
{
}

bool AlgorithmExecutionBudget::Consume(
    std::uint32_t instructions
) noexcept
{
    if (!Checkpoint())
    {
        return false;
    }
    if (instructions > policy_.instructionBudget
        || report_.instructionsConsumed
            > policy_.instructionBudget - instructions)
    {
        report_.status = AlgorithmBudgetStatus::InstructionBudgetExceeded;
        return false;
    }
    report_.instructionsConsumed += instructions;
    return Checkpoint();
}

bool AlgorithmExecutionBudget::Checkpoint() noexcept
{
    if (report_.status != AlgorithmBudgetStatus::Available)
    {
        return false;
    }
    UpdateElapsed();
    if (policy_.wallClockWarningMicroseconds > 0
        && report_.elapsedMicroseconds
            >= policy_.wallClockWarningMicroseconds)
    {
        report_.wallClockWarningExceeded = true;
    }
    return true;
}

AlgorithmBudgetStatus AlgorithmExecutionBudget::Status() const noexcept
{
    return report_.status;
}

AlgorithmBudgetReport AlgorithmExecutionBudget::Report() const noexcept
{
    return report_;
}

void AlgorithmExecutionBudget::UpdateElapsed() noexcept
{
    const auto elapsed = std::chrono::duration_cast<
        std::chrono::microseconds
    >(std::chrono::steady_clock::now() - started_).count();
    report_.elapsedMicroseconds = elapsed <= 0
        ? 0
        : static_cast<std::uint64_t>(elapsed);
}

}
