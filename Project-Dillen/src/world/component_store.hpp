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
    const ComponentMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    ComponentMap components_;
    std::map<kernel::ComponentTypeId, std::vector<kernel::EntityId>>
        ownersByType_;
};

}
