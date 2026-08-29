#include "relation_definition_registry.hpp"

#include <algorithm>
#include <utility>

namespace dillen::kernel {

RelationDefinitionDeclareResult RelationDefinitionRegistry::Declare(
    RelationDefinition definition,
    const RelationSchemaRegistry& schemas,
    const EntityDefinitionRegistry& entities
)
{
    if (frozen_)
    {
        return RelationDefinitionDeclareResult::Frozen;
    }
    if (!schemas.IsFrozen() || !entities.IsFrozen())
    {
        return RelationDefinitionDeclareResult::RegistriesNotFrozen;
    }
    if (!definition.id
        || !definition.type
        || definition.schemaVersion == 0
        || !definition.source
        || !definition.target
        || !IsValidMechanismSymbol(definition.canonicalName)
        || definition.canonicalName
            != NormalizeMechanismSymbol(definition.canonicalName)
        || definition.id != StableRelationDefinitionId(
            definition.type,
            definition.canonicalName))
    {
        return RelationDefinitionDeclareResult::InvalidDefinition;
    }
    const RelationSchema* schema = schemas.Find(
        definition.type,
        definition.schemaVersion
    );
    if (schema == nullptr)
    {
        return RelationDefinitionDeclareResult::SchemaMissing;
    }
    const EntityDefinition* source = entities.Find(definition.source);
    if (source == nullptr)
    {
        return RelationDefinitionDeclareResult::SourceDefinitionMissing;
    }
    const EntityDefinition* target = entities.Find(definition.target);
    if (target == nullptr)
    {
        return RelationDefinitionDeclareResult::TargetDefinitionMissing;
    }
    if (schema->sourceType && source->type != *schema->sourceType)
    {
        return RelationDefinitionDeclareResult::SourceTypeMismatch;
    }
    if (schema->targetType && target->type != *schema->targetType)
    {
        return RelationDefinitionDeclareResult::TargetTypeMismatch;
    }
    if (!schema->allowSelf && definition.source == definition.target)
    {
        return RelationDefinitionDeclareResult::SelfRelationRejected;
    }
    const auto existing = indexById_.find(definition.id);
    if (existing != indexById_.end())
    {
        return definitions_[existing->second].canonicalName
                == definition.canonicalName
            ? RelationDefinitionDeclareResult::DuplicateDefinition
            : RelationDefinitionDeclareResult::IdCollision;
    }
    indexById_[definition.id] = definitions_.size();
    definitions_.push_back(std::move(definition));
    return RelationDefinitionDeclareResult::Added;
}

void RelationDefinitionRegistry::Clear()
{
    if (!frozen_)
    {
        definitions_.clear();
        indexById_.clear();
    }
}

void RelationDefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        definitions_.begin(),
        definitions_.end(),
        [](const RelationDefinition& first,
           const RelationDefinition& second)
        {
            return first.id < second.id;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool RelationDefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RelationDefinitionRegistry::Size() const noexcept
{
    return definitions_.size();
}

const RelationDefinition* RelationDefinitionRegistry::Find(
    RelationDefinitionId id
) const
{
    const auto iterator = indexById_.find(id);
    return iterator == indexById_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const RelationDefinition* RelationDefinitionRegistry::Find(
    RelationTypeId type,
    std::string_view canonicalName
) const
{
    const RelationDefinition* definition = Find(
        StableRelationDefinitionId(type, canonicalName)
    );
    return definition != nullptr
            && definition->canonicalName == canonicalName
        ? definition
        : nullptr;
}

const std::vector<RelationDefinition>&
RelationDefinitionRegistry::All() const noexcept
{
    return definitions_;
}

void RelationDefinitionRegistry::RebuildIndex()
{
    indexById_.clear();
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        indexById_[definitions_[index].id] = index;
    }
}

}
