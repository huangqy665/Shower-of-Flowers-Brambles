#include "world_command_queue.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dillen::kernel {

std::uint64_t WorldCommandQueue::Enqueue(
    WorldTransaction transaction,
    std::uint64_t notBeforeTick,
    std::int32_t priority
)
{
    const std::uint64_t sequence = ReserveSequence();
    pending_.push_back({
        sequence,
        notBeforeTick,
        priority,
        std::move(transaction)
    });
    return sequence;
}

std::uint64_t WorldCommandQueue::ReserveSequence()
{
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("world command sequence exhausted");
    }
    return nextSequence_++;
}

std::vector<QueuedWorldTransaction> WorldCommandQueue::TakeReady(
    std::uint64_t tick
)
{
    std::vector<QueuedWorldTransaction> ready;
    std::vector<QueuedWorldTransaction> delayed;
    ready.reserve(pending_.size());
    delayed.reserve(pending_.size());
    for (QueuedWorldTransaction& queued : pending_)
    {
        if (queued.notBeforeTick <= tick)
        {
            ready.push_back(std::move(queued));
        }
        else
        {
            delayed.push_back(std::move(queued));
        }
    }
    std::stable_sort(
        ready.begin(),
        ready.end(),
        [](const QueuedWorldTransaction& first,
           const QueuedWorldTransaction& second)
        {
            if (first.notBeforeTick != second.notBeforeTick)
            {
                return first.notBeforeTick < second.notBeforeTick;
            }
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            return first.sequence < second.sequence;
        }
    );
    pending_ = std::move(delayed);
    return ready;
}

void WorldCommandQueue::Clear()
{
    pending_.clear();
    nextSequence_ = 1;
}

bool WorldCommandQueue::Empty() const noexcept
{
    return pending_.empty();
}

std::size_t WorldCommandQueue::Size() const noexcept
{
    return pending_.size();
}

const std::vector<QueuedWorldTransaction>&
WorldCommandQueue::Pending() const noexcept
{
    return pending_;
}

}
