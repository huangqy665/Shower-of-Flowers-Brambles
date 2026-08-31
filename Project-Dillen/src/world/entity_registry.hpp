#pragma once

#include <cstddef>
#include <map>
#include <vector>
#include <memory>

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
    const std::vector<kernel::EntityId>& FindByDefinition(
        kernel::EntityDefinitionId definition
    ) const;
    const std::vector<kernel::EntityId>& FindByType(
        kernel::EntityTypeId type
    ) const;
    const EntityMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    // Copy-on-write. Copying the store shares the payload and costs a
    // refcount bump; the first write through a shared handle clones it. The
    // World Transaction executor copies all six stores to stage a transaction,
    // so this turns "stage a transaction" from O(world) into O(what it
    // touches). Reads go through Read(), writes through Mutable() -- taking a
    // non-const reference out of Read() would mutate a payload someone else
    // may still be holding.
    //
    // use_count() is only meaningful because commit is single-threaded by
    // contract (memo section 3.9): worker threads may run algorithm dispatch,
    // never the store writes below.
    // Both secondary indexes are kept ascending by entity id (see
    // kernel/sorted_id_index.hpp) so the Query Snapshot can share this payload
    // instead of rebuilding them.
    struct Data
    {
        EntityMap entities;
        std::map<kernel::EntityDefinitionId, std::vector<kernel::EntityId>>
            entitiesByDefinition;
        std::map<kernel::EntityTypeId, std::vector<kernel::EntityId>>
            entitiesByType;
    };

    const Data& Read() const noexcept { return *data_; }
    Data& Mutable()
    {
        if (data_.use_count() > 1)
        {
            data_ = std::make_shared<Data>(*data_);
        }
        return *data_;
    }

    std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

}
