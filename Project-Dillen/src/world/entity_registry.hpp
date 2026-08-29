#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "frozen_runtime_catalog.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::world {

struct EntityRecord
{
    kernel::EntityId id;
    kernel::EntityDefinitionId definition;
    kernel::EntityTypeId type;
};

enum class EntityCreateResult
{
    Created,
    RuntimeCatalogNotFrozen,
    DefinitionMissing,
    IdCollision
};

class EntityRegistry
{
public:
    using EntityMap = std::map<kernel::EntityId, EntityRecord>;

    EntityCreateResult CreateFromDefinition(
        kernel::EntityDefinitionId definition,
        const kernel::FrozenRuntimeCatalog& catalog,
        kernel::EntityId& outputId
    );
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const EntityRecord* Find(kernel::EntityId id) const;
    const std::vector<kernel::EntityId>& FindByType(
        kernel::EntityTypeId type
    ) const;
    const EntityMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    EntityMap entities_;
    std::map<kernel::EntityTypeId, std::vector<kernel::EntityId>>
        entitiesByType_;
};

}
