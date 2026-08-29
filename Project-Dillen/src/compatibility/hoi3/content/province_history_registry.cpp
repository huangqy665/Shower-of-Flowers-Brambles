#include "province_history_registry.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace dillen::compatibility::hoi3::content {

ProvinceHistoryAppendResult ProvinceHistoryRegistry::Append(
    ProvinceDefinitionId province,
    ProvinceHistorySource source
)
{
    if (frozen_)
    {
        return ProvinceHistoryAppendResult::Frozen;
    }
    if (!province || source.origin.virtualPath.empty())
    {
        return ProvinceHistoryAppendResult::InvalidSource;
    }

    auto iterator = indexByProvince_.find(province.value);
    const bool merged = iterator != indexByProvince_.end();
    if (!merged)
    {
        const std::size_t index = timelines_.size();
        indexByProvince_[province.value] = index;
        timelines_.push_back({province, {}, {}, {}});
        iterator = indexByProvince_.find(province.value);
    }
    ProvinceHistoryTimeline& timeline = timelines_[iterator->second];
    timeline.sources.push_back(source.origin);
    timeline.initialOperations.insert(
        timeline.initialOperations.end(),
        std::make_move_iterator(source.initialOperations.begin()),
        std::make_move_iterator(source.initialOperations.end())
    );
    for (ProvinceHistoryPatch& patch : source.patches)
    {
        patch.sequence = nextPatchSequence_++;
        timeline.patches.push_back(std::move(patch));
    }
    ++sourceCount_;
    return merged
        ? ProvinceHistoryAppendResult::Merged
        : ProvinceHistoryAppendResult::Added;
}

void ProvinceHistoryRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    timelines_.clear();
    indexByProvince_.clear();
    nextPatchSequence_ = 0;
    sourceCount_ = 0;
}

void ProvinceHistoryRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    for (ProvinceHistoryTimeline& timeline : timelines_)
    {
        std::stable_sort(
            timeline.patches.begin(),
            timeline.patches.end(),
            [](const ProvinceHistoryPatch& first,
               const ProvinceHistoryPatch& second)
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
        [](const ProvinceHistoryTimeline& first,
           const ProvinceHistoryTimeline& second)
        {
            return first.province < second.province;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool ProvinceHistoryRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t ProvinceHistoryRegistry::Size() const noexcept
{
    return timelines_.size();
}

std::size_t ProvinceHistoryRegistry::SourceCount() const noexcept
{
    return sourceCount_;
}

const ProvinceHistoryTimeline* ProvinceHistoryRegistry::Find(
    ProvinceDefinitionId province
) const
{
    return Find(province.value);
}

const ProvinceHistoryTimeline* ProvinceHistoryRegistry::Find(
    std::uint32_t province
) const
{
    const auto iterator = indexByProvince_.find(province);
    return iterator == indexByProvince_.end()
        ? nullptr
        : &timelines_[iterator->second];
}

const std::vector<ProvinceHistoryTimeline>&
ProvinceHistoryRegistry::All() const noexcept
{
    return timelines_;
}

void ProvinceHistoryRegistry::RebuildIndex()
{
    indexByProvince_.clear();
    indexByProvince_.reserve(timelines_.size());
    for (std::size_t index = 0; index < timelines_.size(); ++index)
    {
        indexByProvince_[timelines_[index].province.value] = index;
    }
}

}
