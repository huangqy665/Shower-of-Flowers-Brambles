#include "mechanism_scheduler.hpp"

#include <limits>
#include <utility>

namespace dillen::kernel {

MechanismSchedulerTickResult::operator bool() const noexcept
{
    return status == MechanismSchedulerStatus::Completed;
}

MechanismSchedulerTickResult MechanismScheduler::RunTick(
    MechanismInstanceStore& mechanisms,
    WorldCommandQueue& commands,
    WorldEventQueue& events,
    MechanismQuerySnapshot& snapshot,
    const MechanismDefinitionRegistry& definitions,
    const MechanismSchemaRegistry& schemas,
    std::uint64_t nextTick,
    std::uint64_t& revision
)
{
    MechanismSchedulerTickResult report;
    report.tick = currentTick_;
    report.revision = revision;
    if (currentTick_ == std::numeric_limits<std::uint64_t>::max()
        || nextTick != currentTick_ + 1)
    {
        report.status = MechanismSchedulerStatus::TickSequenceInvalid;
        return report;
    }
    if (!definitions.IsFrozen())
    {
        report.status =
            MechanismSchedulerStatus::DefinitionRegistryNotFrozen;
        return report;
    }
    if (!schemas.IsFrozen())
    {
        report.status = MechanismSchedulerStatus::SchemaRegistryNotFrozen;
        return report;
    }

    std::vector<QueuedWorldTransaction> ready = commands.TakeReady(nextTick);
    report.transactions.reserve(ready.size());
    for (QueuedWorldTransaction& queued : ready)
    {
        WorldTransactionResult result = ApplyWorldTransaction(
            mechanisms,
            queued.transaction,
            definitions,
            schemas,
            nextTick
        );
        if (result)
        {
            ++report.committedTransactions;
            if (result.mechanism.changedInstances != 0)
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
            std::move(result)
        });
    }

    currentTick_ = nextTick;
    snapshot.Publish(mechanisms, currentTick_, revision);
    report.status = MechanismSchedulerStatus::Completed;
    report.tick = currentTick_;
    report.revision = revision;
    report.processedTransactions = report.transactions.size();
    return report;
}

void MechanismScheduler::Reset(std::uint64_t tick) noexcept
{
    currentTick_ = tick;
}

std::uint64_t MechanismScheduler::CurrentTick() const noexcept
{
    return currentTick_;
}

}
