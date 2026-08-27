#include "unit_type_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::content {

UnitTypeDeclareResult UnitTypeDefinitionRegistry::Declare(
    UnitTypeDefinition definition
)
{
    if (frozen_)
    {
        return UnitTypeDeclareResult::Frozen;
    }
    if (definition.name.empty()
        || definition.normalizedName.empty()
        || definition.normalizedName
            != NormalizeUnitTypeName(definition.name)
        || !definition.id
        || definition.id != StableUnitTypeDefinitionId(definition.name)
        || definition.origin.virtualPath.empty())
    {
        return UnitTypeDeclareResult::InvalidDefinition;
    }
    if (indexByName_.find(definition.normalizedName)
        != indexByName_.end())
    {
        return UnitTypeDeclareResult::DuplicateName;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return UnitTypeDeclareResult::IdCollision;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByName_[definition.normalizedName] = index;
    definitions_.push_back(std::move(definition));
    return UnitTypeDeclareResult::Added;
}

UnitTypeResolveResult UnitTypeDefinitionRegistry::ResolveUsableBy(
    UnitTypeDefinitionId id,
    std::vector<CountryDefinitionId> usableBy
)
{
    if (frozen_)
    {
        return UnitTypeResolveResult::Frozen;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return UnitTypeResolveResult::UnitTypeMissing;
    }
    UnitTypeDefinition& definition = definitions_[iterator->second];
    if (definition.usableByResolved)
    {
        return UnitTypeResolveResult::AlreadyResolved;
    }
    definition.usableBy = std::move(usableBy);
    definition.usableByResolved = true;
    return UnitTypeResolveResult::Resolved;
}

void UnitTypeDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByName_.clear();
}

void UnitTypeDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const UnitTypeDefinition& first,
           const UnitTypeDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool UnitTypeDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t UnitTypeDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t UnitTypeDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const UnitTypeDefinition& definition)
        {
            return definition.usableByResolved;
        }
    ));
}

const UnitTypeDefinition* UnitTypeDefinitionRegistry::Find(
    UnitTypeDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const UnitTypeDefinition* UnitTypeDefinitionRegistry::Find(
    std::string_view name
) const
{
    const auto iterator = indexByName_.find(NormalizeUnitTypeName(name));
    return iterator == indexByName_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<UnitTypeDefinition>&
UnitTypeDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void UnitTypeDefinitionRegistry::RebuildIndexes()
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
