#include "entity_registry.hpp"

#include <utility>

namespace dillen::world {

namespace {

const std::vector<kernel::EntityId>& EmptyEntityIds()
{
    static const std::vector<kernel::EntityId> empty;
    return empty;
}

}

EntityCreateResult EntityRegistry::CreateFromDefinition(
    kernel::EntityDefinitionId definitionId,
    const kernel::FrozenRuntimeCatalog& catalog,
    kernel::EntityId& outputId
)
{
    outputId = {};
    if (!catalog.IsFrozen())
    {
        return EntityCreateResult::RuntimeCatalogNotFrozen;
    }
    const kernel::CompiledEntityDefinition* definition =
        catalog.FindEntityDefinition(definitionId);
    if (definition == nullptr)
    {
        return EntityCreateResult::DefinitionMissing;
    }
    const kernel::EntityId entityId = kernel::StableEntityId(definitionId);
    if (entities_.find(entityId) != entities_.end())
    {
        return EntityCreateResult::IdCollision;
    }
    entities_.emplace(entityId, EntityRecord{
        entityId,
        definitionId,
        definition->type
    });
    entitiesByType_[definition->type].push_back(entityId);
    outputId = entityId;
    return EntityCreateResult::Created;
}

void EntityRegistry::Clear()
{
    entities_.clear();
    entitiesByType_.clear();
}

bool EntityRegistry::Empty() const noexcept
{
    return entities_.empty();
}

std::size_t EntityRegistry::Size() const noexcept
{
    return entities_.size();
}

const EntityRecord* EntityRegistry::Find(kernel::EntityId id) const
{
    const auto iterator = entities_.find(id);
    return iterator == entities_.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::EntityId>& EntityRegistry::FindByType(
    kernel::EntityTypeId type
) const
{
    const auto iterator = entitiesByType_.find(type);
    return iterator == entitiesByType_.end()
        ? EmptyEntityIds()
        : iterator->second;
}

const EntityRegistry::EntityMap& EntityRegistry::All() const noexcept
{
    return entities_;
}

}
