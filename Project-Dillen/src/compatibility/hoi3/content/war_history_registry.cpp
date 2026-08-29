#include "war_history_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::compatibility::hoi3::content {

WarHistoryDeclareResult WarHistoryRegistry::Declare(
    WarHistoryTimeline timeline
)
{
    if (frozen_)
    {
        return WarHistoryDeclareResult::Frozen;
    }
    const std::string normalized = NormalizeWarHistoryPath(
        timeline.virtualPath
    );
    if (normalized.empty()
        || timeline.virtualPath != normalized
        || !timeline.id
        || timeline.id != StableWarHistoryDefinitionId(normalized)
        || timeline.name.empty()
        || timeline.origin.virtualPath.empty())
    {
        return WarHistoryDeclareResult::InvalidDefinition;
    }
    if (indexByPath_.find(normalized) != indexByPath_.end())
    {
        return WarHistoryDeclareResult::DuplicatePath;
    }
    if (indexById_.find(timeline.id.value) != indexById_.end())
    {
        return WarHistoryDeclareResult::IdCollision;
    }

    std::uint64_t sequence = 0;
    std::size_t participantOperationCount = 0;
    std::size_t warGoalCount = 0;
    for (WarHistoryPatch& patch : timeline.patches)
    {
        if (patch.date.year < 1
            || patch.date.month < 1
            || patch.date.month > 12
            || patch.date.day < 1
            || patch.date.day > 31
            || patch.origin.virtualPath.empty())
        {
            return WarHistoryDeclareResult::InvalidDefinition;
        }
        patch.sequence = sequence++;
        participantOperationCount += patch.participantOperations.size();
        warGoalCount += patch.warGoals.size();
    }
    patchCount_ += timeline.patches.size();
    participantOperationCount_ += participantOperationCount;
    warGoalCount_ += warGoalCount;

    const std::size_t index = timelines_.size();
    indexById_[timeline.id.value] = index;
    indexByPath_[normalized] = index;
    timelines_.push_back(std::move(timeline));
    return WarHistoryDeclareResult::Added;
}

void WarHistoryRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    timelines_.clear();
    indexById_.clear();
    indexByPath_.clear();
    patchCount_ = 0;
    participantOperationCount_ = 0;
    warGoalCount_ = 0;
}

void WarHistoryRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    for (WarHistoryTimeline& timeline : timelines_)
    {
        std::stable_sort(
            timeline.patches.begin(),
            timeline.patches.end(),
            [](const WarHistoryPatch& first, const WarHistoryPatch& second)
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
        [](const WarHistoryTimeline& first,
           const WarHistoryTimeline& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool WarHistoryRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t WarHistoryRegistry::Size() const noexcept
{
    return timelines_.size();
}

std::size_t WarHistoryRegistry::PatchCount() const noexcept
{
    return patchCount_;
}

std::size_t WarHistoryRegistry::ParticipantOperationCount() const noexcept
{
    return participantOperationCount_;
}

std::size_t WarHistoryRegistry::WarGoalCount() const noexcept
{
    return warGoalCount_;
}

const WarHistoryTimeline* WarHistoryRegistry::Find(
    WarHistoryDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &timelines_[iterator->second];
}

const WarHistoryTimeline* WarHistoryRegistry::Find(
    std::string_view virtualPath
) const
{
    const auto iterator = indexByPath_.find(
        NormalizeWarHistoryPath(virtualPath)
    );
    return iterator == indexByPath_.end()
        ? nullptr
        : &timelines_[iterator->second];
}

const std::vector<WarHistoryTimeline>&
WarHistoryRegistry::All() const noexcept
{
    return timelines_;
}

void WarHistoryRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByPath_.clear();
    indexById_.reserve(timelines_.size());
    indexByPath_.reserve(timelines_.size());
    for (std::size_t index = 0; index < timelines_.size(); ++index)
    {
        const WarHistoryTimeline& timeline = timelines_[index];
        indexById_[timeline.id.value] = index;
        indexByPath_[timeline.virtualPath] = index;
    }
}

}
