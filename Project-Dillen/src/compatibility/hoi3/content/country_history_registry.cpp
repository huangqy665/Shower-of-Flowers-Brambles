#include "country_history_registry.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace dillen::compatibility::hoi3::content {

CountryHistoryAppendResult CountryHistoryRegistry::Append(
    CountryDefinitionId country,
    CountryHistorySource source
)
{
    if (frozen_)
    {
        return CountryHistoryAppendResult::Frozen;
    }
    if (!country || source.origin.virtualPath.empty())
    {
        return CountryHistoryAppendResult::InvalidSource;
    }

    auto iterator = indexByCountry_.find(country.value);
    const bool merged = iterator != indexByCountry_.end();
    if (!merged)
    {
        const std::size_t index = timelines_.size();
        indexByCountry_[country.value] = index;
        timelines_.push_back({country, {}, {}, {}});
        iterator = indexByCountry_.find(country.value);
    }
    CountryHistoryTimeline& timeline = timelines_[iterator->second];
    timeline.sources.push_back(source.origin);
    timeline.initialOperations.insert(
        timeline.initialOperations.end(),
        std::make_move_iterator(source.initialOperations.begin()),
        std::make_move_iterator(source.initialOperations.end())
    );
    for (CountryHistoryPatch& patch : source.patches)
    {
        patch.sequence = nextPatchSequence_++;
        timeline.patches.push_back(std::move(patch));
    }
    ++sourceCount_;
    return merged
        ? CountryHistoryAppendResult::Merged
        : CountryHistoryAppendResult::Added;
}

void CountryHistoryRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    timelines_.clear();
    indexByCountry_.clear();
    nextPatchSequence_ = 0;
    sourceCount_ = 0;
}

void CountryHistoryRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    for (CountryHistoryTimeline& timeline : timelines_)
    {
        std::stable_sort(
            timeline.patches.begin(),
            timeline.patches.end(),
            [](const CountryHistoryPatch& first,
               const CountryHistoryPatch& second)
            {
                if (first.date != second.date)
                {
                    return first.date < second.date;
                }
                return first.sequence < second.sequence;
            }
        );
    }
    std::sort(
        timelines_.begin(),
        timelines_.end(),
        [](const CountryHistoryTimeline& first,
           const CountryHistoryTimeline& second)
        {
            return first.country < second.country;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool CountryHistoryRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t CountryHistoryRegistry::Size() const noexcept
{
    return timelines_.size();
}

std::size_t CountryHistoryRegistry::SourceCount() const noexcept
{
    return sourceCount_;
}

const CountryHistoryTimeline* CountryHistoryRegistry::Find(
    CountryDefinitionId country
) const
{
    const auto iterator = indexByCountry_.find(country.value);
    return iterator == indexByCountry_.end()
        ? nullptr
        : &timelines_[iterator->second];
}

const CountryHistoryTimeline* CountryHistoryRegistry::Find(
    const CountryTag& tag
) const
{
    return Find(tag.StableId());
}

const CountryHistoryTimeline* CountryHistoryRegistry::Find(
    std::string_view tag
) const
{
    const auto parsed = CountryTag::Parse(tag);
    return parsed ? Find(*parsed) : nullptr;
}

const std::vector<CountryHistoryTimeline>&
CountryHistoryRegistry::All() const noexcept
{
    return timelines_;
}

void CountryHistoryRegistry::RebuildIndex()
{
    indexByCountry_.clear();
    indexByCountry_.reserve(timelines_.size());
    for (std::size_t index = 0; index < timelines_.size(); ++index)
    {
        indexByCountry_[timelines_[index].country.value] = index;
    }
}

}
