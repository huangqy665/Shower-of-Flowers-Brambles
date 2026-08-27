#include "region_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::content {

RegionDeclareResult RegionDefinitionRegistry::Declare(
    RegionDefinition definition
)
{
    if (frozen_)
    {
        return RegionDeclareResult::Frozen;
    }
    if (definition.name.empty()
        || !definition.id
        || definition.id != StableRegionDefinitionId(definition.name)
        || definition.origin.virtualPath.empty())
    {
        return RegionDeclareResult::InvalidDefinition;
    }
    if (indexByName_.find(definition.name) != indexByName_.end())
    {
        return RegionDeclareResult::DuplicateName;
    }
    const auto idIterator = indexById_.find(definition.id.value);
    if (idIterator != indexById_.end())
    {
        return RegionDeclareResult::IdCollision;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByName_[definition.name] = index;
    definitions_.push_back(std::move(definition));
    return RegionDeclareResult::Added;
}

RegionResolveResult RegionDefinitionRegistry::Resolve(
    RegionDefinitionId id,
    std::vector<ProvinceDefinitionId> provinces,
    std::vector<std::string> flags
)
{
    if (frozen_)
    {
        return RegionResolveResult::Frozen;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return RegionResolveResult::RegionMissing;
    }
    if (provinces.empty())
    {
        return RegionResolveResult::EmptyProvinceSet;
    }
    RegionDefinition& definition = definitions_[iterator->second];
    if (definition.resolved)
    {
        return RegionResolveResult::AlreadyResolved;
    }
    definition.provinces = std::move(provinces);
    definition.flags = std::move(flags);
    definition.resolved = true;
    return RegionResolveResult::Resolved;
}

void RegionDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByName_.clear();
}

void RegionDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const RegionDefinition& first,
           const RegionDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool RegionDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RegionDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t RegionDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const RegionDefinition& definition)
        {
            return definition.resolved;
        }
    ));
}

const RegionDefinition* RegionDefinitionRegistry::Find(
    RegionDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const RegionDefinition* RegionDefinitionRegistry::Find(
    std::string_view name
) const
{
    const auto iterator = indexByName_.find(std::string(name));
    return iterator == indexByName_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<RegionDefinition>&
RegionDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void RegionDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByName_.clear();
    indexById_.reserve(definitions_.size());
    indexByName_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id.value] = index;
        indexByName_[definitions_[index].name] = index;
    }
}

}
