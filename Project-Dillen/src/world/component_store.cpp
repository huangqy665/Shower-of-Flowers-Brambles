#include "component_store.hpp"

#include <utility>

#include "sorted_id_index.hpp"

namespace dillen::world {

namespace {

const std::vector<kernel::EntityId>& EmptyEntityIds()
{
    static const std::vector<kernel::EntityId> empty;
    return empty;
}

const std::vector<kernel::ComponentTypeId>& EmptyComponentTypeIds()
{
    static const std::vector<kernel::ComponentTypeId> empty;
    return empty;
}

}

ComponentAttachResult ComponentStore::Attach(
    kernel::EntityId owner,
    const kernel::CompiledEntityComponentDefinition& component,
    const EntityRegistry& entities
)
{
    if (entities.Find(owner) == nullptr)
    {
        return ComponentAttachResult::OwnerMissing;
    }
    if (!component.type || component.schemaVersion == 0)
    {
        return ComponentAttachResult::InvalidComponent;
    }
    const ComponentKey key{owner, component.type};
    if (Read().components.find(key) != Read().components.end())
    {
        return ComponentAttachResult::DuplicateComponent;
    }
    Data& data = Mutable();
    data.components.emplace(key, ComponentRecord{
        owner,
        component.type,
        component.schemaVersion,
        component.initialValues
    });
    kernel::InsertSortedId(data.ownersByType[component.type], owner);
    kernel::InsertSortedId(data.typesByOwner[owner], component.type);
    return ComponentAttachResult::Attached;
}

ComponentFieldSetResult ComponentStore::SetField(
    kernel::EntityId owner,
    kernel::ComponentTypeId type,
    kernel::ComponentFieldSlotId field,
    kernel::MechanismValue value,
    const EntityRegistry& entities,
    const kernel::FrozenRuntimeCatalog& catalog,
    kernel::MechanismValue& previousValue
)
{
    previousValue = {};
    if (entities.Find(owner) == nullptr)
    {
        return ComponentFieldSetResult::OwnerMissing;
    }
    // Validate against the shared payload and only clone once the write is
    // certain: a rejected or no-op SetField must not cost a store copy, and
    // "set a field to the value it already holds" is common.
    const ComponentMap& readable = Read().components;
    const auto reader = readable.find({owner, type});
    if (reader == readable.end())
    {
        return ComponentFieldSetResult::ComponentMissing;
    }
    const ComponentRecord& component = reader->second;
    const kernel::CompiledComponentLayout* layout =
        catalog.FindComponentLayout(type, component.schemaVersion);
    if (layout == nullptr)
    {
        return ComponentFieldSetResult::LayoutMissing;
    }
    if (!field
        || field.value >= layout->fields.size()
        || field.value >= component.values.size())
    {
        return ComponentFieldSetResult::UnknownField;
    }
    if (!kernel::MechanismValueMatchesSchema(
            layout->fields[field.value],
            value))
    {
        return ComponentFieldSetResult::InvalidValue;
    }
    previousValue = component.values[field.value];
    if (previousValue == value)
    {
        return ComponentFieldSetResult::Unchanged;
    }
    Mutable().components.at({owner, type}).values[field.value] =
        std::move(value);
    return ComponentFieldSetResult::Updated;
}

void ComponentStore::Clear()
{
    Data& data = Mutable();
    data.components.clear();
    data.ownersByType.clear();
    data.typesByOwner.clear();
}

bool ComponentStore::Empty() const noexcept
{
    return Read().components.empty();
}

std::size_t ComponentStore::Size() const noexcept
{
    return Read().components.size();
}

const ComponentRecord* ComponentStore::Find(
    kernel::EntityId owner,
    kernel::ComponentTypeId type
) const
{
    const auto iterator = Read().components.find({owner, type});
    return iterator == Read().components.end()
        ? nullptr
        : &iterator->second;
}

const std::vector<kernel::EntityId>& ComponentStore::FindOwners(
    kernel::ComponentTypeId type
) const
{
    const auto iterator = Read().ownersByType.find(type);
    return iterator == Read().ownersByType.end()
        ? EmptyEntityIds()
        : iterator->second;
}

const std::vector<kernel::ComponentTypeId>& ComponentStore::FindTypes(
    kernel::EntityId owner
) const
{
    const auto iterator = Read().typesByOwner.find(owner);
    return iterator == Read().typesByOwner.end()
        ? EmptyComponentTypeIds()
        : iterator->second;
}

const ComponentStore::ComponentMap& ComponentStore::All() const noexcept
{
    return Read().components;
}

}
