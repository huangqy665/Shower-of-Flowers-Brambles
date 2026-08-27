#include "order_of_battle_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::content {

OrderOfBattleDeclareResult OrderOfBattleDefinitionRegistry::Declare(
    OrderOfBattleDefinition definition
)
{
    if (frozen_)
    {
        return OrderOfBattleDeclareResult::Frozen;
    }
    const std::string normalized = NormalizeOrderOfBattlePath(
        definition.virtualPath
    );
    if (normalized.empty()
        || definition.virtualPath != normalized
        || !definition.id
        || definition.id != StableOrderOfBattleDefinitionId(normalized)
        || definition.origin.virtualPath.empty())
    {
        return OrderOfBattleDeclareResult::InvalidDefinition;
    }
    if (indexByPath_.find(normalized) != indexByPath_.end())
    {
        return OrderOfBattleDeclareResult::DuplicatePath;
    }
    if (indexById_.find(definition.id.value) != indexById_.end())
    {
        return OrderOfBattleDeclareResult::IdCollision;
    }
    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    indexByPath_[normalized] = index;
    definitions_.push_back(std::move(definition));
    return OrderOfBattleDeclareResult::Added;
}

OrderOfBattleResolveResult
OrderOfBattleDefinitionRegistry::ResolveReferences(
    OrderOfBattleDefinitionId id,
    std::vector<OrderOfBattleNode> roots,
    std::vector<OrderOfBattleMilitaryAccess> militaryAccess,
    std::vector<OrderOfBattleConstruction> constructions,
    std::vector<OrderOfBattleMetadata> metadata
)
{
    if (frozen_)
    {
        return OrderOfBattleResolveResult::Frozen;
    }
    const auto iterator = indexById_.find(id.value);
    if (iterator == indexById_.end())
    {
        return OrderOfBattleResolveResult::DefinitionMissing;
    }
    OrderOfBattleDefinition& definition = definitions_[iterator->second];
    if (definition.referencesResolved)
    {
        return OrderOfBattleResolveResult::AlreadyResolved;
    }
    definition.roots = std::move(roots);
    definition.militaryAccess = std::move(militaryAccess);
    definition.constructions = std::move(constructions);
    definition.metadata = std::move(metadata);
    definition.referencesResolved = true;
    return OrderOfBattleResolveResult::Resolved;
}

void OrderOfBattleDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
    indexByPath_.clear();
}

void OrderOfBattleDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const OrderOfBattleDefinition& first,
           const OrderOfBattleDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool OrderOfBattleDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t OrderOfBattleDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

std::size_t OrderOfBattleDefinitionRegistry::ResolvedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(),
        definitions_.end(),
        [](const OrderOfBattleDefinition& definition)
        {
            return definition.referencesResolved;
        }
    ));
}

const OrderOfBattleDefinition* OrderOfBattleDefinitionRegistry::Find(
    OrderOfBattleDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const OrderOfBattleDefinition* OrderOfBattleDefinitionRegistry::Find(
    std::string_view virtualPath
) const
{
    const auto iterator = indexByPath_.find(
        NormalizeOrderOfBattlePath(virtualPath)
    );
    return iterator == indexByPath_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const std::vector<OrderOfBattleDefinition>&
OrderOfBattleDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void OrderOfBattleDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    indexByPath_.clear();
    indexById_.reserve(definitions_.size());
    indexByPath_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        const OrderOfBattleDefinition& definition = definitions_[index];
        indexById_[definition.id.value] = index;
        indexByPath_[definition.virtualPath] = index;
    }
}

}
