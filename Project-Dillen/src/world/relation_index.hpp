#pragma once

#include <cstddef>
#include <map>
#include <utility>
#include <vector>
#include <memory>

#include "entity_registry.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::world {

struct RelationRecord
{
    kernel::RelationId id;
    kernel::RelationTypeId type;
    kernel::EntityId source;
    kernel::EntityId target;
};

enum class RelationAddResult
{
    Added,
    InvalidRelation,
    SourceMissing,
    TargetMissing,
    SchemaMissing,
    SourceTypeMismatch,
    TargetTypeMismatch,
    SelfRelationRejected,
    DuplicateRelation,
    IdCollision
};

enum class RelationRemoveResult
{
    Removed,
    RelationMissing
};

class RelationIndex
{
public:
    using RelationMap = std::map<kernel::RelationId, RelationRecord>;

    RelationAddResult Add(
        kernel::RelationTypeId type,
        kernel::EntityId source,
        kernel::EntityId target,
        const EntityRegistry& entities,
        kernel::RelationId& outputId
    );
    RelationAddResult Add(
        kernel::RelationTypeId type,
        kernel::EntityId source,
        kernel::EntityId target,
        const EntityRegistry& entities,
        const kernel::FrozenRuntimeCatalog& catalog,
        kernel::RelationId& outputId
    );
    RelationRemoveResult Remove(
        kernel::RelationId relation,
        RelationRecord& removed
    );
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const RelationRecord* Find(kernel::RelationId id) const;
    const std::vector<kernel::RelationId>& FindByType(
        kernel::RelationTypeId type
    ) const;
    const std::vector<kernel::RelationId>& Outgoing(
        kernel::RelationTypeId type,
        kernel::EntityId source
    ) const;
    const std::vector<kernel::RelationId>& Incoming(
        kernel::RelationTypeId type,
        kernel::EntityId target
    ) const;
    const RelationMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    using TypedEntityKey = std::pair<kernel::RelationTypeId, kernel::EntityId>;

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
    // All three secondary indexes are kept ascending by relation id (see
    // kernel/sorted_id_index.hpp) so the Query Snapshot can share this payload
    // instead of rebuilding them.
    struct Data
    {
        RelationMap relations;
        std::map<kernel::RelationTypeId, std::vector<kernel::RelationId>>
            relationsByType;
        std::map<TypedEntityKey, std::vector<kernel::RelationId>> outgoing;
        std::map<TypedEntityKey, std::vector<kernel::RelationId>> incoming;
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
