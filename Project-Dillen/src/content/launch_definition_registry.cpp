#include "launch_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::content {

LaunchDeclareResult LaunchDefinitionRegistry::Declare(
    BookmarkDefinition definition
)
{
    if (frozen_)
    {
        return LaunchDeclareResult::Frozen;
    }
    const std::string normalized = NormalizeBookmarkKey(definition.key);
    if (normalized.empty()
        || normalized != definition.key
        || !definition.id
        || definition.id != StableBookmarkDefinitionId(normalized)
        || definition.name.empty()
        || definition.origin.virtualPath.empty())
    {
        return LaunchDeclareResult::InvalidDefinition;
    }
    if (bookmarkIdsByKey_.find(normalized) != bookmarkIdsByKey_.end())
    {
        return LaunchDeclareResult::DuplicateKey;
    }
    if (bookmarkIndex_.find(definition.id.value) != bookmarkIndex_.end())
    {
        return LaunchDeclareResult::IdCollision;
    }
    const std::size_t index = bookmarks_.size();
    bookmarkIndex_[definition.id.value] = index;
    bookmarkIdsByKey_[normalized] = definition.id.value;
    bookmarks_.push_back(std::move(definition));
    return LaunchDeclareResult::Added;
}

LaunchDeclareResult LaunchDefinitionRegistry::Declare(
    ScenarioDefinition definition
)
{
    if (frozen_)
    {
        return LaunchDeclareResult::Frozen;
    }
    const std::string normalized = NormalizeScenarioKey(definition.key);
    if (normalized.empty()
        || normalized != definition.key
        || !definition.id
        || definition.id != StableScenarioDefinitionId(normalized)
        || definition.name.empty()
        || definition.origin.virtualPath.empty())
    {
        return LaunchDeclareResult::InvalidDefinition;
    }
    if (scenarioIdsByKey_.find(normalized) != scenarioIdsByKey_.end())
    {
        return LaunchDeclareResult::DuplicateKey;
    }
    if (scenarioIndex_.find(definition.id.value) != scenarioIndex_.end())
    {
        return LaunchDeclareResult::IdCollision;
    }
    const std::size_t index = scenarios_.size();
    scenarioIndex_[definition.id.value] = index;
    scenarioIdsByKey_[normalized] = definition.id.value;
    scenarios_.push_back(std::move(definition));
    return LaunchDeclareResult::Added;
}

void LaunchDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    bookmarks_.clear();
    scenarios_.clear();
    bookmarkIndex_.clear();
    scenarioIndex_.clear();
    bookmarkIdsByKey_.clear();
    scenarioIdsByKey_.clear();
}

void LaunchDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        bookmarks_.begin(),
        bookmarks_.end(),
        [](const BookmarkDefinition& first, const BookmarkDefinition& second)
        {
            if (first.date != second.date)
            {
                return first.date < second.date;
            }
            return first.id < second.id;
        }
    );
    std::sort(
        scenarios_.begin(),
        scenarios_.end(),
        [](const ScenarioDefinition& first, const ScenarioDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool LaunchDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t LaunchDefinitionRegistry::BookmarkCount() const noexcept
{
    return bookmarks_.size();
}

std::size_t LaunchDefinitionRegistry::ScenarioCount() const noexcept
{
    return scenarios_.size();
}

const BookmarkDefinition* LaunchDefinitionRegistry::Find(
    BookmarkDefinitionId id
) const
{
    const auto iterator = bookmarkIndex_.find(id.value);
    return iterator == bookmarkIndex_.end()
        ? nullptr
        : &bookmarks_[iterator->second];
}

const BookmarkDefinition* LaunchDefinitionRegistry::FindBookmark(
    std::string_view key
) const
{
    const auto iterator = bookmarkIdsByKey_.find(NormalizeBookmarkKey(key));
    return iterator == bookmarkIdsByKey_.end()
        ? nullptr
        : Find(BookmarkDefinitionId{iterator->second});
}

const ScenarioDefinition* LaunchDefinitionRegistry::Find(
    ScenarioDefinitionId id
) const
{
    const auto iterator = scenarioIndex_.find(id.value);
    return iterator == scenarioIndex_.end()
        ? nullptr
        : &scenarios_[iterator->second];
}

const ScenarioDefinition* LaunchDefinitionRegistry::FindScenario(
    std::string_view key
) const
{
    const auto iterator = scenarioIdsByKey_.find(NormalizeScenarioKey(key));
    return iterator == scenarioIdsByKey_.end()
        ? nullptr
        : Find(ScenarioDefinitionId{iterator->second});
}

const std::vector<BookmarkDefinition>&
LaunchDefinitionRegistry::Bookmarks() const noexcept
{
    return bookmarks_;
}

const std::vector<ScenarioDefinition>&
LaunchDefinitionRegistry::Scenarios() const noexcept
{
    return scenarios_;
}

void LaunchDefinitionRegistry::RebuildIndexes()
{
    bookmarkIndex_.clear();
    scenarioIndex_.clear();
    bookmarkIdsByKey_.clear();
    scenarioIdsByKey_.clear();
    for (std::size_t index = 0; index < bookmarks_.size(); ++index)
    {
        const BookmarkDefinition& definition = bookmarks_[index];
        bookmarkIndex_[definition.id.value] = index;
        bookmarkIdsByKey_[definition.key] = definition.id.value;
    }
    for (std::size_t index = 0; index < scenarios_.size(); ++index)
    {
        const ScenarioDefinition& definition = scenarios_[index];
        scenarioIndex_[definition.id.value] = index;
        scenarioIdsByKey_[definition.key] = definition.id.value;
    }
}

}
