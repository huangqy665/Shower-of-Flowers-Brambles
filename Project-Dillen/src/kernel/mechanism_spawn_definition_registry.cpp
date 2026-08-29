#include "mechanism_spawn_definition_registry.hpp"

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
                binding.value).second)
        {
            return false;
        }
    }
    return true;
}

}

MechanismSpawnDeclareResult MechanismSpawnDefinitionRegistry::Declare(
    MechanismSpawnDefinition spawn,
    const MechanismDefinitionRegistry& definitions,
    const MechanismSchemaRegistry& schemas
)
{
    if (frozen_)
    {
        return MechanismSpawnDeclareResult::Frozen;
    }
    if (!definitions.IsFrozen() || !schemas.IsFrozen())
    {
        return MechanismSpawnDeclareResult::DependenciesNotFrozen;
    }
    if (!spawn.id
        || !spawn.definition
        || spawn.count == 0
        || spawn.source.sourceName.empty()
        || !IsValidMechanismSymbol(spawn.canonicalName)
        || spawn.canonicalName
            != NormalizeMechanismSymbol(spawn.canonicalName)
        || spawn.id != StableMechanismSpawnDefinitionId(
            spawn.definition,
            spawn.canonicalName))
    {
        return MechanismSpawnDeclareResult::InvalidSpawn;
    }
    const MechanismDefinition* definition = definitions.Find(
        spawn.definition
    );
    if (definition == nullptr)
    {
        return MechanismSpawnDeclareResult::DefinitionMissing;
    }
    const MechanismSchema* schema = schemas.Find(
        definition->type,
        definition->schemaVersion
    );
    if (schema == nullptr)
    {
        return MechanismSpawnDeclareResult::SchemaMissing;
    }

    std::map<std::string, MechanismValue> fields = definition->fields;
    for (const auto& field : spawn.initialFields)
    {
        const MechanismFieldSchema* fieldSchema = FindField(
            *schema,
            field.first
        );
        if (fieldSchema == nullptr)
        {
            return MechanismSpawnDeclareResult::UnknownField;
        }
        if (!MechanismValueMatchesSchema(*fieldSchema, field.second))
        {
            return MechanismSpawnDeclareResult::FieldValueInvalid;
        }
        fields[field.first] = field.second;
    }
    for (const MechanismFieldSchema& fieldSchema : schema->fields)
    {
        if (fields.find(fieldSchema.name) == fields.end()
            && fieldSchema.required)
        {
            return MechanismSpawnDeclareResult::RequiredFieldMissing;
        }
    }

    std::map<std::string, std::vector<MechanismReference>> roles =
        definition->roles;
    for (const auto& role : spawn.initialRoles)
    {
        const MechanismRoleSchema* roleSchema = FindRole(
            *schema,
            role.first
        );
        if (roleSchema == nullptr)
        {
            return MechanismSpawnDeclareResult::UnknownRole;
        }
        if (!RoleBindingsValid(*roleSchema, role.second))
        {
            return MechanismSpawnDeclareResult::RoleBindingInvalid;
        }
        roles[role.first] = role.second;
    }
    for (const MechanismRoleSchema& roleSchema : schema->roles)
    {
        const auto role = roles.find(roleSchema.name);
        if (role == roles.end())
        {
            if (roleSchema.minimumCount != 0)
            {
                return MechanismSpawnDeclareResult::RoleBindingInvalid;
            }
            continue;
        }
        if (!RoleBindingsValid(roleSchema, role->second))
        {
            return MechanismSpawnDeclareResult::RoleBindingInvalid;
        }
    }

    const auto existing = indexById_.find(spawn.id);
    if (existing != indexById_.end())
    {
        const MechanismSpawnDefinition& stored = spawns_[existing->second];
        return stored.definition == spawn.definition
            && stored.canonicalName == spawn.canonicalName
            ? MechanismSpawnDeclareResult::DuplicateSpawn
            : MechanismSpawnDeclareResult::IdCollision;
    }
    spawn.initialFields = std::move(fields);
    spawn.initialRoles = std::move(roles);
    indexById_[spawn.id] = spawns_.size();
    spawns_.push_back(std::move(spawn));
    return MechanismSpawnDeclareResult::Added;
}

void MechanismSpawnDefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    spawns_.clear();
    indexById_.clear();
}

void MechanismSpawnDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        spawns_.begin(),
        spawns_.end(),
        [](const MechanismSpawnDefinition& first,
           const MechanismSpawnDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool MechanismSpawnDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t MechanismSpawnDefinitionRegistry::Size() const noexcept
{
    return spawns_.size();
}

const MechanismSpawnDefinition* MechanismSpawnDefinitionRegistry::Find(
    MechanismSpawnDefinitionId id
) const
{
    const auto iterator = indexById_.find(id);
    return iterator == indexById_.end()
        ? nullptr
        : &spawns_[iterator->second];
}

const std::vector<MechanismSpawnDefinition>&
MechanismSpawnDefinitionRegistry::All() const noexcept
{
    return spawns_;
}

void MechanismSpawnDefinitionRegistry::RebuildIndex()
{
    indexById_.clear();
    for (std::size_t index = 0; index < spawns_.size(); ++index)
    {
        indexById_[spawns_[index].id] = index;
    }
}

}
