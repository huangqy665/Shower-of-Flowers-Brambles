#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "war_history.hpp"

namespace dillen::compatibility::hoi3::content {

enum class WarHistoryDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicatePath,
    IdCollision,
    Frozen
};

class WarHistoryRegistry
{
public:
    WarHistoryDeclareResult Declare(WarHistoryTimeline timeline);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t PatchCount() const noexcept;
    std::size_t ParticipantOperationCount() const noexcept;
    std::size_t WarGoalCount() const noexcept;
    const WarHistoryTimeline* Find(WarHistoryDefinitionId id) const;
    const WarHistoryTimeline* Find(std::string_view virtualPath) const;
    const std::vector<WarHistoryTimeline>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<WarHistoryTimeline> timelines_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByPath_;
    std::size_t patchCount_ = 0;
    std::size_t participantOperationCount_ = 0;
    std::size_t warGoalCount_ = 0;
    bool frozen_ = false;
};

}
