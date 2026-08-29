#include "country_definition_registry.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "country_definition.hpp"

namespace dillen::compatibility::hoi3::content {

CountryDeclareResult CountryDefinitionRegistry::Declare(
    CountryTagDefinition definition
)
{
    if (frozen_)
    {
        return CountryDeclareResult::Frozen;
    }
    if (!definition.tag.IsValid()
        || !definition.id
        || definition.id != definition.tag.StableId()
        || definition.declaredPath.empty()
        || definition.definitionPath.empty())
    {
        return CountryDeclareResult::InvalidDefinition;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return CountryDeclareResult::DuplicateTag;
    }
    indexById_[definition.id.value] = definitions_.size();
    definitions_.push_back(std::move(definition));
    return CountryDeclareResult::Added;
}

CountryResolveResult CountryDefinitionRegistry::Resolve(
    CountryDefinitionId id,
    std::shared_ptr<const CountryDefinition> definition
)
{
    if (frozen_)
    {
        return CountryResolveResult::Frozen;
    }
    if (!definition)
    {
        return CountryResolveResult::DefinitionMissing;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return CountryResolveResult::CountryMissing;
    }
    CountryTagDefinition& country = definitions_[iterator->second];
    if (country.definition)
    {
        return CountryResolveResult::AlreadyResolved;
    }
    country.definition = std::move(definition);
    return CountryResolveResult::Resolved;
}

void CountryDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
}

void CountryDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const CountryTagDefinition& first,
           const CountryTagDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool CountryDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t CountryDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t CountryDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const CountryTagDefinition& definition)
        {
            return static_cast<bool>(definition.definition);
        }
    ));
}

const CountryTagDefinition* CountryDefinitionRegistry::Find(
    CountryDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return nullptr;
    }
    return &definitions_[iterator->second];
}

const CountryTagDefinition* CountryDefinitionRegistry::Find(
    const CountryTag& tag
) const
{
    return Find(tag.StableId());
}

const CountryTagDefinition* CountryDefinitionRegistry::Find(
    std::string_view tag
) const
{
    const auto parsed = CountryTag::Parse(tag);
    return parsed ? Find(*parsed) : nullptr;
}

const std::vector<CountryTagDefinition>&
CountryDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void CountryDefinitionRegistry::RebuildIndex()
{
    indexById_.clear();
    indexById_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id.value] = index;
    }
}

}
