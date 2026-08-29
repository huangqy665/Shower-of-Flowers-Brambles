#pragma once

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

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

    RelationMap relations_;
    std::map<TypedEntityKey, std::vector<kernel::RelationId>> outgoing_;
    std::map<TypedEntityKey, std::vector<kernel::RelationId>> incoming_;
};

}
