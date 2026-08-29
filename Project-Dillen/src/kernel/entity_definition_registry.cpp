#include "entity_definition_registry.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace dillen::kernel {

namespace {

const MechanismFieldSchema* FindField(
    const ComponentSchema& schema,
    std::string_view name
)
{
    const auto iterator = std::find_if(
        schema.fields.begin(),
        schema.fields.end(),
        [name](const MechanismFieldSchema& field)
        {
            return field.name == name;
        }
    );
    return iterator == schema.fields.end() ? nullptr : &*iterator;
}

}

EntityDefinitionDeclareResult EntityDefinitionRegistry::Declare(
    EntityDefinition definition,
    const ComponentSchemaRegistry& schemas
)
{
    if (frozen_)
    {
        return EntityDefinitionDeclareResult::Frozen;
    }
    if (!schemas.IsFrozen())
    {
        return EntityDefinitionDeclareResult::ComponentSchemasNotFrozen;
    }
    if (!definition.id
        || !definition.type
        || definition.source.sourceName.empty()
        || !IsValidMechanismSymbol(definition.canonicalName)
        || definition.canonicalName
            != NormalizeMechanismSymbol(definition.canonicalName)
        || definition.id != StableEntityDefinitionId(
            definition.type,
            definition.canonicalName))
    {
        return EntityDefinitionDeclareResult::InvalidDefinition;
    }

    std::set<ComponentTypeId> componentTypes;
    for (EntityComponentDefinition& component : definition.components)
    {
        if (!component.type
            || component.schemaVersion == 0
            || !componentTypes.emplace(component.type).second)
        {
            return EntityDefinitionDeclareResult::DuplicateComponent;
        }
        const ComponentSchema* schema = schemas.Find(
            component.type,
            component.schemaVersion
        );
        if (schema == nullptr)
        {
            return EntityDefinitionDeclareResult::ComponentSchemaMissing;
        }
        for (const auto& field : component.fields)
        {
            const MechanismFieldSchema* fieldSchema = FindField(
                *schema,
                field.first
            );
            if (fieldSchema == nullptr)
            {
                return EntityDefinitionDeclareResult::UnknownField;
            }
            if (!MechanismValueMatchesSchema(*fieldSchema, field.second))
            {
                return EntityDefinitionDeclareResult::FieldValueInvalid;
            }
        }
        for (const MechanismFieldSchema& fieldSchema : schema->fields)
        {
            if (component.fields.find(fieldSchema.name)
                != component.fields.end())
            {
                continue;
            }
            if (fieldSchema.defaultValue)
            {
                component.fields.emplace(
                    fieldSchema.name,
                    *fieldSchema.defaultValue
                );
            }
            else if (fieldSchema.required)
            {
                return EntityDefinitionDeclareResult::RequiredFieldMissing;
            }
        }
    }

    const auto existing = indexById_.find(definition.id);
    if (existing != indexById_.end())
    {
        const EntityDefinition& stored = definitions_[existing->second];
        return stored.type == definition.type
            && stored.canonicalName == definition.canonicalName
            ? EntityDefinitionDeclareResult::DuplicateDefinition
            : EntityDefinitionDeclareResult::IdCollision;
    }
    indexById_[definition.id] = definitions_.size();
    definitions_.push_back(std::move(definition));
    return EntityDefinitionDeclareResult::Added;
}

void EntityDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
}

void EntityDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const EntityDefinition& first, const EntityDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool EntityDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t EntityDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

const EntityDefinition* EntityDefinitionRegistry::Find(
    EntityDefinitionId id
) const
{
    const auto iterator = indexById_.find(id);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const EntityDefinition* EntityDefinitionRegistry::Find(
    EntityTypeId type,
    std::string_view canonicalName
) const
{
    const EntityDefinitionId id = StableEntityDefinitionId(
        type,
        canonicalName
    );
    const EntityDefinition* definition = Find(id);
    return definition != nullptr
        && definition->type == type
        && definition->canonicalName == canonicalName
        ? definition
        : nullptr;
}

const std::vector<EntityDefinition>&
EntityDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void EntityDefinitionRegistry::RebuildIndex()
{
    indexById_.clear();
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id] = index;
    }
}

}
