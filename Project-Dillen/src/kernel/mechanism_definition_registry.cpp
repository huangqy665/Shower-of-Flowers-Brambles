#include "mechanism_definition_registry.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace dillen::kernel {

namespace {

const MechanismFieldSchema* FindField(
    const MechanismSchema& schema,
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

const MechanismRoleSchema* FindRole(
    const MechanismSchema& schema,
    std::string_view name
)
{
    const auto iterator = std::find_if(
        schema.roles.begin(),
        schema.roles.end(),
        [name](const MechanismRoleSchema& role)
        {
            return role.name == name;
        }
    );
    return iterator == schema.roles.end() ? nullptr : &*iterator;
}

bool RoleBindingsValid(
    const MechanismRoleSchema& schema,
    const std::vector<MechanismReference>& bindings
)
{
    if (bindings.size() < schema.minimumCount
        || (schema.maximumCount
            && bindings.size() > *schema.maximumCount))
    {
        return false;
    }
    std::set<std::tuple<int, std::uint64_t, std::uint64_t>> unique;
    for (const MechanismReference& binding : bindings)
    {
        if (binding.type == 0
            || binding.value == 0
            || binding.kind != schema.referenceKind
            || (schema.referenceType
                && binding.type != *schema.referenceType)
            || !unique.emplace(
                static_cast<int>(binding.kind),
                binding.type,
                binding.value
            ).second)
        {
            return false;
        }
    }
    return true;
}

}

MechanismDefinitionDeclareResult MechanismDefinitionRegistry::Declare(
    MechanismDefinition definition,
    const MechanismSchemaRegistry& schemas,
    const AlgorithmRegistry& algorithms
)
{
    if (frozen_)
    {
        return MechanismDefinitionDeclareResult::Frozen;
    }
    if (!schemas.IsFrozen() || !algorithms.IsFrozen())
    {
        return MechanismDefinitionDeclareResult::DependenciesNotFrozen;
    }
    if (!definition.id
        || !definition.type
        || definition.schemaVersion == 0
        || definition.source.sourceName.empty()
        || !IsValidMechanismSymbol(definition.canonicalName)
        || definition.canonicalName
            != NormalizeMechanismSymbol(definition.canonicalName)
        || definition.id != StableMechanismDefinitionId(
            definition.type,
            definition.canonicalName))
    {
        return MechanismDefinitionDeclareResult::InvalidDefinition;
    }

    const MechanismSchema* schema = schemas.Find(
        definition.type,
        definition.schemaVersion
    );
    if (schema == nullptr)
    {
        return MechanismDefinitionDeclareResult::SchemaMissing;
    }
    if (definition.algorithm)
    {
        if (definition.algorithmVersion == 0
            || algorithms.Find(
                definition.algorithm,
                definition.algorithmVersion) == nullptr)
        {
            return MechanismDefinitionDeclareResult::AlgorithmMissing;
        }
    }
    else if (definition.algorithmVersion != 0)
    {
        return MechanismDefinitionDeclareResult::InvalidDefinition;
    }

    for (const auto& field : definition.fields)
    {
        const MechanismFieldSchema* fieldSchema = FindField(
            *schema,
            field.first
        );
        if (fieldSchema == nullptr)
        {
            return MechanismDefinitionDeclareResult::UnknownField;
        }
        if (!MechanismValueMatchesSchema(*fieldSchema, field.second))
        {
            return MechanismDefinitionDeclareResult::FieldValueInvalid;
        }
    }
    for (const MechanismFieldSchema& fieldSchema : schema->fields)
    {
        if (definition.fields.find(fieldSchema.name)
            != definition.fields.end())
        {
            continue;
        }
        if (fieldSchema.defaultValue)
        {
            definition.fields.emplace(
                fieldSchema.name,
                *fieldSchema.defaultValue
            );
        }
        else if (fieldSchema.required)
        {
            return MechanismDefinitionDeclareResult::RequiredFieldMissing;
        }
    }

    for (const auto& role : definition.roles)
    {
        const MechanismRoleSchema* roleSchema = FindRole(
            *schema,
            role.first
        );
        if (roleSchema == nullptr)
        {
            return MechanismDefinitionDeclareResult::UnknownRole;
        }
        if (!RoleBindingsValid(*roleSchema, role.second))
        {
            return MechanismDefinitionDeclareResult::RoleBindingInvalid;
        }
    }
    for (const MechanismRoleSchema& roleSchema : schema->roles)
    {
        const auto iterator = definition.roles.find(roleSchema.name);
        if (iterator == definition.roles.end())
        {
            if (roleSchema.minimumCount != 0)
            {
                return MechanismDefinitionDeclareResult::RoleBindingInvalid;
            }
            continue;
        }
        if (!RoleBindingsValid(roleSchema, iterator->second))
        {
            return MechanismDefinitionDeclareResult::RoleBindingInvalid;
        }
    }

    const auto existing = indexById_.find(definition.id.value);
    if (existing != indexById_.end())
    {
        const MechanismDefinition& stored = definitions_[existing->second];
        return stored.type == definition.type
            && stored.canonicalName == definition.canonicalName
            ? MechanismDefinitionDeclareResult::DuplicateDefinition
            : MechanismDefinitionDeclareResult::IdCollision;
    }

    const std::size_t index = definitions_.size();
    indexById_[definition.id.value] = index;
    definitions_.push_back(std::move(definition));
    return MechanismDefinitionDeclareResult::Added;
}

void MechanismDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    definitions_.clear();
    indexById_.clear();
}

void MechanismDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const MechanismDefinition& first,
           const MechanismDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool MechanismDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t MechanismDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

const MechanismDefinition* MechanismDefinitionRegistry::Find(
    MechanismDefinitionId id
) const
{
    const auto iterator = indexById_.find(id.value);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const MechanismDefinition* MechanismDefinitionRegistry::Find(
    MechanismTypeId type,
    std::string_view canonicalName
) const
{
    const MechanismDefinitionId id = StableMechanismDefinitionId(
        type,
        canonicalName
    );
    const MechanismDefinition* definition = Find(id);
    return definition != nullptr
        && definition->type == type
        && definition->canonicalName == canonicalName
        ? definition
        : nullptr;
}

const std::vector<MechanismDefinition>&
MechanismDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void MechanismDefinitionRegistry::RebuildIndexes()
{
    indexById_.clear();
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id.value] = index;
    }
}

}
