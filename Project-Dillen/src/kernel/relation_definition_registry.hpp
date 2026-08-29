#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

#include "entity_definition_registry.hpp"
#include "relation_definition.hpp"
#include "relation_schema.hpp"

namespace dillen::kernel {

enum class RelationDefinitionDeclareResult
{
    Added,
    InvalidDefinition,
    RegistriesNotFrozen,
    SchemaMissing,
    SourceDefinitionMissing,
    TargetDefinitionMissing,
    SourceTypeMismatch,
    TargetTypeMismatch,
    SelfRelationRejected,
    DuplicateDefinition,
    IdCollision,
    Frozen
};

class RelationDefinitionRegistry
{
public:
    RelationDefinitionDeclareResult Declare(
        RelationDefinition definition,
        const RelationSchemaRegistry& schemas,
        const EntityDefinitionRegistry& entities
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const RelationDefinition* Find(RelationDefinitionId id) const;
    const RelationDefinition* Find(
        RelationTypeId type,
        std::string_view canonicalName
    ) const;
    const std::vector<RelationDefinition>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<RelationDefinition> definitions_;
    std::map<RelationDefinitionId, std::size_t> indexById_;
    bool frozen_ = false;
};

}
