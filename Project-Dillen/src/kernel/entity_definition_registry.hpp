#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

#include "component_schema.hpp"
#include "entity_definition.hpp"

namespace dillen::kernel {

enum class EntityDefinitionDeclareResult
{
    Added,
    InvalidDefinition,
    ComponentSchemasNotFrozen,
    ComponentSchemaMissing,
    DuplicateComponent,
    UnknownField,
    FieldValueInvalid,
    RequiredFieldMissing,
    DuplicateDefinition,
    IdCollision,
    Frozen
};

class EntityDefinitionRegistry
{
public:
    EntityDefinitionDeclareResult Declare(
        EntityDefinition definition,
        const ComponentSchemaRegistry& schemas
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const EntityDefinition* Find(EntityDefinitionId id) const;
    const EntityDefinition* Find(
        EntityTypeId type,
        std::string_view canonicalName
    ) const;
    const std::vector<EntityDefinition>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<EntityDefinition> definitions_;
    std::map<EntityDefinitionId, std::size_t> indexById_;
    bool frozen_ = false;
};

}
