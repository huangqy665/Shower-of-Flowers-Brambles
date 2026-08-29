#include "technology_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::compatibility::hoi3::content {

TechnologyDeclareResult TechnologyDefinitionRegistry::Declare(
    TechnologyDefinition definition
)
{
    if (frozen_)
    {
        return TechnologyDeclareResult::Frozen;
    }
    if (definition.name.empty()
        || definition.normalizedName.empty()
        || definition.normalizedName
            != NormalizeTechnologyName(definition.name)
        || !definition.id
        || definition.id != StableTechnologyDefinitionId(definition.name)
        || definition.folder.empty()
        || definition.onCompletion.empty()
        || definition.startYear <= 0
        || definition.origin.virtualPath.empty())
    {
        return TechnologyDeclareResult::InvalidDefinition;
    }
    if (indexByName_.find(definition.normalizedName)
        != indexByName_.end())
    {
        return TechnologyDeclareResult::DuplicateName;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return TechnologyDeclareResult::IdCollision;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByName_[definition.normalizedName] = index;
    definitions_.push_back(std::move(definition));
    return TechnologyDeclareResult::Added;
}

TechnologyResolveResult TechnologyDefinitionRegistry::ResolveReferences(
    TechnologyDefinitionId id,
    std::optional<TechnologyRequirement> allow,
    std::vector<TechnologyUnitReference> activatedUnits,
    std::vector<TechnologyEffectBlock> effectBlocks
)
{
    if (frozen_)
    {
        return TechnologyResolveResult::Frozen;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return TechnologyResolveResult::TechnologyMissing;
    }
    TechnologyDefinition& definition = definitions_[iterator->second];
    if (definition.referencesResolved)
    {
        return TechnologyResolveResult::AlreadyResolved;
    }
    definition.allow = std::move(allow);
    definition.activatedUnits = std::move(activatedUnits);
    definition.effectBlocks = std::move(effectBlocks);
    definition.referencesResolved = true;
    return TechnologyResolveResult::Resolved;
}

void TechnologyDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByName_.clear();
}

void TechnologyDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const TechnologyDefinition& first,
           const TechnologyDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool TechnologyDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t TechnologyDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t TechnologyDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const TechnologyDefinition& definition)
        {
            return definition.referencesResolved;
        }
    ));
}

const TechnologyDefinition* TechnologyDefinitionRegistry::Find(
    TechnologyDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const TechnologyDefinition* TechnologyDefinitionRegistry::Find(
    std::string_view name
) const
{
    const auto iterator = indexByName_.find(NormalizeTechnologyName(name));
    return iterator == indexByName_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<TechnologyDefinition>&
TechnologyDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void TechnologyDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByName_.clear();
    indexById_.reserve(definitions_.size());
    indexByName_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id.value] = index;
        indexByName_[definitions_[index].normalizedName] = index;
    }
}

}
