#include "mechanism_scheduler.hpp"

#include <limits>
#include <utility>

namespace dillen::runtime {

MechanismSchedulerTickResult::operator bool() const noexcept
{
    return status == MechanismSchedulerStatus::Completed;
}

MechanismSchedulerTickResult MechanismScheduler::RunTick(
    kernel::WorldCommandQueue& commands,
    kernel::WorldEventQueue& events,
    const kernel::FrozenRuntimeCatalog& catalog,
    std::uint64_t nextTick,
    std::uint64_t& currentTick,
    std::uint64_t& revision,
    const TransactionExecutor& executor
)
{
    MechanismSchedulerTickResult report;
    report.tick = currentTick;
    report.revision = revision;
    if (currentTick == std::numeric_limits<std::uint64_t>::max()
        || nextTick != currentTick + 1)
    {
        report.status = MechanismSchedulerStatus::TickSequenceInvalid;
        return report;
    }
    if (!catalog.IsFrozen())
    {
        report.status = MechanismSchedulerStatus::RuntimeCatalogNotFrozen;
        return report;
    }

    std::vector<kernel::QueuedWorldTransaction> ready =
        commands.TakeReady(nextTick);
    report.transactions.reserve(ready.size());
    for (kernel::QueuedWorldTransaction& queued : ready)
    {
        kernel::WorldTransactionResult result = executor(
            queued.transaction,
            nextTick
        );
        if (result)
        {
            ++report.committedTransactions;
            if (result.Changed())
            {
                ++revision;
            }
        }
        else
        {
            ++report.rejectedTransactions;
        }
        events.PublishTransactionResult(nextTick, queued.sequence, result);
        report.transactions.push_back({
            queued.sequence,
            queued.priority,
            std::move(queued.transaction),
            std::move(result)
        });
    }

    currentTick = nextTick;
    report.status = MechanismSchedulerStatus::Completed;
    report.tick = currentTick;
    report.revision = revision;
    report.processedTransactions = report.transactions.size();
    return report;
}

}
