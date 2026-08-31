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

struct ComponentRecord
{
    kernel::EntityId owner;
    kernel::ComponentTypeId type;
    std::uint32_t schemaVersion = 0;
    std::vector<kernel::MechanismValue> values;
};

enum class ComponentAttachResult
{
    Attached,
    OwnerMissing,
    InvalidComponent,
    DuplicateComponent
};

enum class ComponentFieldSetResult
{
    Updated,
    Unchanged,
    OwnerMissing,
    ComponentMissing,
    LayoutMissing,
    UnknownField,
    InvalidValue
};

class ComponentStore
{
public:
    using ComponentKey = std::pair<kernel::EntityId, kernel::ComponentTypeId>;
    using ComponentMap = std::map<ComponentKey, ComponentRecord>;

    ComponentAttachResult Attach(
        kernel::EntityId owner,
        const kernel::CompiledEntityComponentDefinition& component,
        const EntityRegistry& entities
    );
    ComponentFieldSetResult SetField(
        kernel::EntityId owner,
        kernel::ComponentTypeId type,
        kernel::ComponentFieldSlotId field,
        kernel::MechanismValue value,
        const EntityRegistry& entities,
        const kernel::FrozenRuntimeCatalog& catalog,
        kernel::MechanismValue& previousValue
    );
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const ComponentRecord* Find(
        kernel::EntityId owner,
        kernel::ComponentTypeId type
    ) const;
    const std::vector<kernel::EntityId>& FindOwners(
        kernel::ComponentTypeId type
    ) const;
    const std::vector<kernel::ComponentTypeId>& FindTypes(
        kernel::EntityId owner
    ) const;
    const ComponentMap& All() const noexcept;

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
    // Both secondary indexes are kept ascending by the id they hold (see
    // kernel/sorted_id_index.hpp) so the Query Snapshot can share this payload
    // instead of rebuilding them.
    struct Data
    {
        ComponentMap components;
        std::map<kernel::ComponentTypeId, std::vector<kernel::EntityId>>
            ownersByType;
        std::map<kernel::EntityId, std::vector<kernel::ComponentTypeId>>
            typesByOwner;
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
