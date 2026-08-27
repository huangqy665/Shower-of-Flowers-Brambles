#include "diplomacy_history_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::content {

DiplomacyHistoryAppendResult DiplomacyHistoryRegistry::Append(
    DiplomacyHistoryKey key,
    DiplomaticRelationPeriod period
)
{
    if (frozen_)
    {
        return DiplomacyHistoryAppendResult::Frozen;
    }
    key = CanonicalDiplomacyHistoryKey(key.kind, key.first, key.second);
    if (!key.first
        || !key.second
        || key.first == key.second
        || period.origin.virtualPath.empty()
        || !(period.startDate < period.endDate))
    {
        return DiplomacyHistoryAppendResult::InvalidPeriod;
    }

    auto iterator = std::find_if(
        timelines_.begin(),
        timelines_.end(),
        [&key](const DiplomacyHistoryTimeline& timeline)
        {
            return timeline.key == key;
        }
    );
    const bool added = iterator == timelines_.end();
    if (added)
    {
        timelines_.push_back({key, {}});
        iterator = timelines_.end() - 1;
    }
    period.sequence = nextSequence_++;
    iterator->periods.push_back(std::move(period));
    ++periodCount_;
    return added
        ? DiplomacyHistoryAppendResult::AddedTimeline
        : DiplomacyHistoryAppendResult::AppendedPeriod;
}

void DiplomacyHistoryRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    timelines_.clear();
    nextSequence_ = 0;
    periodCount_ = 0;
}

void DiplomacyHistoryRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    for (DiplomacyHistoryTimeline& timeline : timelines_)
    {
        std::stable_sort(
            timeline.periods.begin(),
            timeline.periods.end(),
            [](const DiplomaticRelationPeriod& first,
               const DiplomaticRelationPeriod& second)
            {
                if (first.startDate != second.startDate)
                {
                    return first.startDate < second.startDate;
                }
                if (first.endDate != second.endDate)
                {
                    return first.endDate < second.endDate;
                }
                return first.sequence < second.sequence;
            }
        );
    }
    std::sort(
        timelines_.begin(),
        timelines_.end(),
        [](const DiplomacyHistoryTimeline& first,
           const DiplomacyHistoryTimeline& second)
        {
            return first.key < second.key;
        }
    );
    frozen_ = true;
}

bool DiplomacyHistoryRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t DiplomacyHistoryRegistry::Size() const noexcept
{
    return timelines_.size();
}

std::size_t DiplomacyHistoryRegistry::PeriodCount() const noexcept
{
    return periodCount_;
}

const DiplomacyHistoryTimeline* DiplomacyHistoryRegistry::Find(
    DiplomacyHistoryKey key
) const
{
    key = CanonicalDiplomacyHistoryKey(key.kind, key.first, key.second);
    if (!frozen_)
    {
        const auto iterator = std::find_if(
            timelines_.begin(),
            timelines_.end(),
            [&key](const DiplomacyHistoryTimeline& timeline)
            {
                return timeline.key == key;
            }
        );
        return iterator == timelines_.end() ? nullptr : &*iterator;
    }
    const auto iterator = std::lower_bound(
        timelines_.begin(),
        timelines_.end(),
        key,
        [](const DiplomacyHistoryTimeline& timeline,
           const DiplomacyHistoryKey& value)
        {
            return timeline.key < value;
        }
    );
    return iterator == timelines_.end() || iterator->key != key
        ? nullptr
        : &*iterator;
}

const std::vector<DiplomacyHistoryTimeline>&
DiplomacyHistoryRegistry::All() const noexcept
{
    return timelines_;
}

}
