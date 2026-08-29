#include "algorithm_inbox.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace dillen::kernel {

namespace {

bool EventOrder(
    const ScheduledAlgorithmEvent& first,
    const ScheduledAlgorithmEvent& second
)
{
    if (first.dueTick != second.dueTick)
    {
        return first.dueTick < second.dueTick;
    }
    if (first.priority != second.priority)
    {
        return first.priority < second.priority;
    }
    return first.sequence < second.sequence;
}

}

AlgorithmInboxScheduleResult AlgorithmInbox::Schedule(
    AlgorithmEventTypeId type,
    MechanismInstanceId target,
    std::uint64_t dueTick,
    std::int32_t priority,
    MechanismValue payload,
    std::uint64_t& outputSequence
)
{
    outputSequence = 0;
    if (!type || dueTick == 0)
    {
        return AlgorithmInboxScheduleResult::InvalidEvent;
    }
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return AlgorithmInboxScheduleResult::SequenceExhausted;
    }
    outputSequence = nextSequence_++;
    pending_.push_back({
        outputSequence,
        type,
        target,
        dueTick,
        priority,
        std::move(payload)
    });
    SortPending();
    return AlgorithmInboxScheduleResult::Scheduled;
}

bool AlgorithmInbox::Cancel(
    std::uint64_t sequence,
    ScheduledAlgorithmEvent& removed
)
{
    removed = {};
    const auto iterator = std::find_if(
        pending_.begin(),
        pending_.end(),
        [sequence](const ScheduledAlgorithmEvent& event)
        {
            return event.sequence == sequence;
        }
    );
    if (iterator == pending_.end())
    {
        return false;
    }
    removed = std::move(*iterator);
    pending_.erase(iterator);
    return true;
}

std::vector<ScheduledAlgorithmEvent> AlgorithmInbox::CancelTarget(
    MechanismInstanceId target
)
{
    std::vector<ScheduledAlgorithmEvent> removed;
    std::vector<ScheduledAlgorithmEvent> retained;
    removed.reserve(pending_.size());
    retained.reserve(pending_.size());
    for (ScheduledAlgorithmEvent& event : pending_)
    {
        if (target && event.target == target)
        {
            removed.push_back(std::move(event));
        }
        else
        {
            retained.push_back(std::move(event));
        }
    }
    pending_ = std::move(retained);
    return removed;
}

std::vector<ScheduledAlgorithmEvent> AlgorithmInbox::TakeReady(
    std::uint64_t tick
)
{
    std::vector<ScheduledAlgorithmEvent> ready;
    std::vector<ScheduledAlgorithmEvent> delayed;
    ready.reserve(pending_.size());
    delayed.reserve(pending_.size());
    for (ScheduledAlgorithmEvent& event : pending_)
    {
        if (event.dueTick <= tick)
        {
            ready.push_back(std::move(event));
        }
        else
        {
            delayed.push_back(std::move(event));
        }
    }
    pending_ = std::move(delayed);
    return ready;
}

void AlgorithmInbox::Clear()
{
    pending_.clear();
    nextSequence_ = 1;
}

bool AlgorithmInbox::Empty() const noexcept
{
    return pending_.empty();
}

std::size_t AlgorithmInbox::Size() const noexcept
{
    return pending_.size();
}

const std::vector<ScheduledAlgorithmEvent>& AlgorithmInbox::Pending()
    const noexcept
{
    return pending_;
}

std::uint64_t AlgorithmInbox::NextSequence() const noexcept
{
    return nextSequence_;
}

void AlgorithmInbox::SortPending()
{
    std::stable_sort(pending_.begin(), pending_.end(), EventOrder);
}

}
