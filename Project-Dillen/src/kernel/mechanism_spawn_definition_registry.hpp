#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition.hpp"

namespace dillen::kernel {

enum class MechanismSpawnDeclareResult
{
    Added,
    InvalidSpawn,
    DependenciesNotFrozen,
    DefinitionMissing,
    SchemaMissing,
    UnknownField,
    FieldValueInvalid,
    RequiredFieldMissing,
    UnknownRole,
    RoleBindingInvalid,
    DuplicateSpawn,
    IdCollision,
    Frozen
};

class MechanismSpawnDefinitionRegistry
{
public:
    MechanismSpawnDeclareResult Declare(
        MechanismSpawnDefinition spawn,
        const MechanismDefinitionRegistry& definitions,
        const MechanismSchemaRegistry& schemas
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const MechanismSpawnDefinition* Find(
        MechanismSpawnDefinitionId id
    ) const;
    const std::vector<MechanismSpawnDefinition>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<MechanismSpawnDefinition> spawns_;
    std::map<MechanismSpawnDefinitionId, std::size_t> indexById_;
    bool frozen_ = false;
};

}
