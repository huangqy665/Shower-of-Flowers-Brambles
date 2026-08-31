#include "entity_registry.hpp"

#include <utility>

#include "sorted_id_index.hpp"

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
    if (Read().entities.find(entityId) != Read().entities.end())
    {
        return EntityCreateResult::IdCollision;
    }
    Data& data = Mutable();
    data.entities.emplace(entityId, EntityRecord{
        entityId,
        definitionId,
        definition->type
    });
    kernel::InsertSortedId(data.entitiesByDefinition[definitionId], entityId);
    kernel::InsertSortedId(data.entitiesByType[definition->type], entityId);
    outputId = entityId;
    return EntityCreateResult::Created;
}

void EntityRegistry::Clear()
{
    Data& data = Mutable();
    data.entities.clear();
    data.entitiesByDefinition.clear();
    data.entitiesByType.clear();
}

bool EntityRegistry::Empty() const noexcept
{
    return Read().entities.empty();
}

std::size_t EntityRegistry::Size() const noexcept
{
    return Read().entities.size();
}

const EntityRecord* EntityRegistry::Find(kernel::EntityId id) const
{
    const auto iterator = Read().entities.find(id);
    return iterator == Read().entities.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::EntityId>& EntityRegistry::FindByDefinition(
    kernel::EntityDefinitionId definition
) const
{
    const auto iterator = Read().entitiesByDefinition.find(definition);
    return iterator == Read().entitiesByDefinition.end()
        ? EmptyEntityIds()
        : iterator->second;
}

const std::vector<kernel::EntityId>& EntityRegistry::FindByType(
    kernel::EntityTypeId type
) const
{
    const auto iterator = Read().entitiesByType.find(type);
    return iterator == Read().entitiesByType.end()
        ? EmptyEntityIds()
        : iterator->second;
}

const EntityRegistry::EntityMap& EntityRegistry::All() const noexcept
{
    return Read().entities;
}

}
