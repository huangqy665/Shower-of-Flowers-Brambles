#include "component_store.hpp"

#include <utility>

namespace dillen::world {

namespace {

const std::vector<kernel::EntityId>& EmptyEntityIds()
{
    static const std::vector<kernel::EntityId> empty;
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
    if (components_.find(key) != components_.end())
    {
        return ComponentAttachResult::DuplicateComponent;
    }
    components_.emplace(key, ComponentRecord{
        owner,
        component.type,
        component.schemaVersion,
        component.initialValues
    });
    ownersByType_[component.type].push_back(owner);
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
    const auto iterator = components_.find({owner, type});
    if (iterator == components_.end())
    {
        return ComponentFieldSetResult::ComponentMissing;
    }
    ComponentRecord& component = iterator->second;
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
    component.values[field.value] = std::move(value);
    return ComponentFieldSetResult::Updated;
}

void ComponentStore::Clear()
{
    components_.clear();
    ownersByType_.clear();
}

bool ComponentStore::Empty() const noexcept
{
    return components_.empty();
}

std::size_t ComponentStore::Size() const noexcept
{
    return components_.size();
}

const ComponentRecord* ComponentStore::Find(
    kernel::EntityId owner,
    kernel::ComponentTypeId type
) const
{
    const auto iterator = components_.find({owner, type});
    return iterator == components_.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::EntityId>& ComponentStore::FindOwners(
    kernel::ComponentTypeId type
) const
{
    const auto iterator = ownersByType_.find(type);
    return iterator == ownersByType_.end()
        ? EmptyEntityIds()
        : iterator->second;
}

const ComponentStore::ComponentMap& ComponentStore::All() const noexcept
{
    return components_;
}

}
