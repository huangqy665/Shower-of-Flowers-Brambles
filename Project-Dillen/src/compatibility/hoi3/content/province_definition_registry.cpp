#include "province_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::compatibility::hoi3::content {

ProvinceDeclareResult ProvinceDefinitionRegistry::Declare(
    ProvinceDefinition definition
)
{
    if (frozen_)
    {
        return ProvinceDeclareResult::Frozen;
    }
    if (!definition.id || definition.origin.virtualPath.empty())
    {
        return ProvinceDeclareResult::InvalidDefinition;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return ProvinceDeclareResult::DuplicateId;
    }
    const std::uint32_t packedRgb = definition.color.PackedRgb();
    if (indexByColor_.find(packedRgb) != indexByColor_.end())
    {
        return ProvinceDeclareResult::DuplicateColor;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByColor_[packedRgb] = index;
    definitions_.push_back(std::move(definition));
    return ProvinceDeclareResult::Added;
}

void ProvinceDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByColor_.clear();
}

void ProvinceDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const ProvinceDefinition& first,
           const ProvinceDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool ProvinceDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t ProvinceDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

const ProvinceDefinition* ProvinceDefinitionRegistry::Find(
    ProvinceDefinitionId id
) const
{
    return Find(id.value);
}

const ProvinceDefinition* ProvinceDefinitionRegistry::Find(
    std::uint32_t id
) const
{
    const auto iterator = indexById_.find(id);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const ProvinceDefinition* ProvinceDefinitionRegistry::FindByColor(
    ProvinceColor color
) const
{
    return FindByPackedRgb(color.PackedRgb());
}

const ProvinceDefinition* ProvinceDefinitionRegistry::FindByPackedRgb(
    std::uint32_t packedRgb
) const
{
    const auto iterator = indexByColor_.find(packedRgb);
    return iterator == indexByColor_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<ProvinceDefinition>&
ProvinceDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void ProvinceDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByColor_.clear();
    indexById_.reserve(definitions_.size());
    indexByColor_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id.value] = index;
        indexByColor_[definitions_[index].color.PackedRgb()] = index;
    }
}

}
