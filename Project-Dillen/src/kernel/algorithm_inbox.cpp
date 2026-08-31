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
    if (Read().nextSequence == std::numeric_limits<std::uint64_t>::max())
    {
        return AlgorithmInboxScheduleResult::SequenceExhausted;
    }
    Data& data = Mutable();
    outputSequence = data.nextSequence++;
    data.pending.push_back({
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
    const auto& readable = Read().pending;
    const auto reader = std::find_if(
        readable.begin(),
        readable.end(),
        [sequence](const ScheduledAlgorithmEvent& event)
        {
            return event.sequence == sequence;
        }
    );
    if (reader == readable.end())
    {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(reader - readable.begin());
    auto& pending = Mutable().pending;
    removed = std::move(pending[index]);
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::vector<ScheduledAlgorithmEvent> AlgorithmInbox::CancelTarget(
    MechanismInstanceId target
)
{
    std::vector<ScheduledAlgorithmEvent> removed;
    const auto matches = [target](const ScheduledAlgorithmEvent& event)
    {
        return target && event.target == target;
    };
    const auto& readable = Read().pending;
    if (std::none_of(readable.begin(), readable.end(), matches))
    {
        return removed;
    }
    std::vector<ScheduledAlgorithmEvent> retained;
    auto& pending = Mutable().pending;
    removed.reserve(pending.size());
    retained.reserve(pending.size());
    for (ScheduledAlgorithmEvent& event : pending)
    {
        if (matches(event))
        {
            removed.push_back(std::move(event));
        }
        else
        {
            retained.push_back(std::move(event));
        }
    }
    pending = std::move(retained);
    return removed;
}

std::vector<ScheduledAlgorithmEvent> AlgorithmInbox::TakeReady(
    std::uint64_t tick
)
{
    // Called every tick, and in most ticks nothing is due -- taking the
    // mutable payload unconditionally would clone the inbox for nothing.
    std::vector<ScheduledAlgorithmEvent> ready;
    const auto due = [tick](const ScheduledAlgorithmEvent& event)
    {
        return event.dueTick <= tick;
    };
    const auto& readable = Read().pending;
    if (std::none_of(readable.begin(), readable.end(), due))
    {
        return ready;
    }
    std::vector<ScheduledAlgorithmEvent> delayed;
    auto& pending = Mutable().pending;
    ready.reserve(pending.size());
    delayed.reserve(pending.size());
    for (ScheduledAlgorithmEvent& event : pending)
    {
        if (due(event))
        {
            ready.push_back(std::move(event));
        }
        else
        {
            delayed.push_back(std::move(event));
        }
    }
    pending = std::move(delayed);
    return ready;
}

void AlgorithmInbox::Clear()
{
    Data& data = Mutable();
    data.pending.clear();
    data.nextSequence = 1;
}

bool AlgorithmInbox::Empty() const noexcept
{
    return Read().pending.empty();
}

std::size_t AlgorithmInbox::Size() const noexcept
{
    return Read().pending.size();
}

const std::vector<ScheduledAlgorithmEvent>& AlgorithmInbox::Pending()
    const noexcept
{
    return Read().pending;
}

std::uint64_t AlgorithmInbox::NextSequence() const noexcept
{
    return Read().nextSequence;
}

void AlgorithmInbox::SortPending()
{
    auto& pending = Mutable().pending;
    std::stable_sort(pending.begin(), pending.end(), EventOrder);
}

}
