#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "launch_definition.hpp"

namespace dillen::content {

enum class LaunchDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateKey,
    IdCollision,
    Frozen
};

class LaunchDefinitionRegistry
{
public:
    LaunchDeclareResult Declare(BookmarkDefinition definition);
    LaunchDeclareResult Declare(ScenarioDefinition definition);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t BookmarkCount() const noexcept;
    std::size_t ScenarioCount() const noexcept;
    const BookmarkDefinition* Find(BookmarkDefinitionId id) const;
    const BookmarkDefinition* FindBookmark(std::string_view key) const;
    const ScenarioDefinition* Find(ScenarioDefinitionId id) const;
    const ScenarioDefinition* FindScenario(std::string_view key) const;
    const std::vector<BookmarkDefinition>& Bookmarks() const noexcept;
    const std::vector<ScenarioDefinition>& Scenarios() const noexcept;

private:
    void RebuildIndexes();

    std::vector<BookmarkDefinition> bookmarks_;
    std::vector<ScenarioDefinition> scenarios_;
    std::unordered_map<std::uint64_t, std::size_t> bookmarkIndex_;
    std::unordered_map<std::uint64_t, std::size_t> scenarioIndex_;
    std::unordered_map<std::string, std::uint64_t> bookmarkIdsByKey_;
    std::unordered_map<std::string, std::uint64_t> scenarioIdsByKey_;
    bool frozen_ = false;
};

}
