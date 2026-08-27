#include "world_event.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace dillen::kernel {

void WorldEventQueue::PublishTransactionResult(
    std::uint64_t tick,
    std::uint64_t transactionSequence,
    const WorldTransactionResult& result
)
{
    if (result)
    {
        Publish(
            tick,
            transactionSequence,
            WorldTransactionCommittedEvent{
                result.mechanism.changedInstances
            }
        );
        for (const MechanismChange& change : result.mechanism.changes)
        {
            if (const auto* field = std::get_if<MechanismFieldChange>(
                    &change))
            {
                Publish(tick, transactionSequence, *field);
            }
            else
            {
                Publish(
                    tick,
                    transactionSequence,
                    std::get<MechanismLifecycleChange>(change)
                );
            }
        }
        return;
    }
    Publish(
        tick,
        transactionSequence,
        WorldTransactionRejectedEvent{
            result.status,
            result.mechanism.status,
            result.mechanism.commandIndex,
            result.mechanism.target
        }
    );
}

std::vector<WorldEvent> WorldEventQueue::Drain()
{
    std::vector<WorldEvent> drained;
    drained.swap(pending_);
    return drained;
}

void WorldEventQueue::Clear()
{
    pending_.clear();
    nextSequence_ = 1;
}

bool WorldEventQueue::Empty() const noexcept
{
    return pending_.empty();
}

std::size_t WorldEventQueue::Size() const noexcept
{
    return pending_.size();
}

const std::vector<WorldEvent>& WorldEventQueue::Pending() const noexcept
{
    return pending_;
}

void WorldEventQueue::Publish(
    std::uint64_t tick,
    std::uint64_t transactionSequence,
    WorldEventPayload payload
)
{
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("world event sequence exhausted");
    }
    pending_.push_back({
        nextSequence_++,
        tick,
        transactionSequence,
        std::move(payload)
    });
}

}
