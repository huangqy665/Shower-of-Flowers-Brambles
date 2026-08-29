#include "unit_model_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::compatibility::hoi3::content {

UnitModelDeclareResult UnitModelDefinitionRegistry::Declare(
    UnitModelDefinition definition
)
{
    if (frozen_)
    {
        return UnitModelDeclareResult::Frozen;
    }
    if (!definition.country
        || definition.unitTypeName.empty()
        || definition.normalizedUnitTypeName.empty()
        || definition.normalizedUnitTypeName
            != NormalizeUnitTypeName(definition.unitTypeName)
        || definition.modelIndex < 0
        || !definition.id
        || definition.id != StableUnitModelDefinitionId(
            definition.country,
            definition.unitTypeName,
            definition.modelIndex)
        || definition.origin.virtualPath.empty())
    {
        return UnitModelDeclareResult::InvalidDefinition;
    }
    const std::string key = CompositeKey(
        definition.country,
        definition.normalizedUnitTypeName,
        definition.modelIndex
    );
    if (indexByKey_.find(key) != indexByKey_.end())
    {
        return UnitModelDeclareResult::DuplicateKey;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return UnitModelDeclareResult::IdCollision;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByKey_[key] = index;
    definitions_.push_back(std::move(definition));
    return UnitModelDeclareResult::Added;
}

UnitModelResolveResult UnitModelDefinitionRegistry::ResolveReferences(
    UnitModelDefinitionId id,
    std::optional<UnitTypeDefinitionId> unitType,
    std::vector<UnitModelTechnologyLevel> technologyLevels
)
{
    if (frozen_)
    {
        return UnitModelResolveResult::Frozen;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return UnitModelResolveResult::UnitModelMissing;
    }
    UnitModelDefinition& definition = definitions_[iterator->second];
    if (definition.referencesResolved)
    {
        return UnitModelResolveResult::AlreadyResolved;
    }
    definition.unitType = unitType;
    definition.technologyLevels = std::move(technologyLevels);
    definition.referencesResolved = true;
    return UnitModelResolveResult::Resolved;
}

void UnitModelDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByKey_.clear();
}

void UnitModelDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const UnitModelDefinition& first,
           const UnitModelDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool UnitModelDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t UnitModelDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t UnitModelDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const UnitModelDefinition& definition)
        {
            return definition.referencesResolved;
        }
    ));
}

const UnitModelDefinition* UnitModelDefinitionRegistry::Find(
    UnitModelDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const UnitModelDefinition* UnitModelDefinitionRegistry::Find(
    CountryDefinitionId country,
    std::string_view unitTypeName,
    int modelIndex
) const
{
    const auto iterator = indexByKey_.find(
        CompositeKey(country, unitTypeName, modelIndex)
    );
    return iterator == indexByKey_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<UnitModelDefinition>&
UnitModelDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

std::string UnitModelDefinitionRegistry::CompositeKey(
    CountryDefinitionId country,
    std::string_view unitTypeName,
    int modelIndex
)
{
    return std::to_string(country.value) + '|'
        + NormalizeUnitTypeName(unitTypeName) + '|'
        + std::to_string(modelIndex);
}

void UnitModelDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByKey_.clear();
    indexById_.reserve(definitions_.size());
    indexByKey_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        const UnitModelDefinition& definition = definitions_[index];
        indexById_[definition.id.value] = index;
        indexByKey_[CompositeKey(
            definition.country,
            definition.normalizedUnitTypeName,
            definition.modelIndex)] = index;
    }
}

}
