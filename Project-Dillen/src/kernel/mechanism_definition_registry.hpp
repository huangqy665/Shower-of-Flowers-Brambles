#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "algorithm_registry.hpp"
#include "mechanism_definition.hpp"
#include "mechanism_schema_registry.hpp"

namespace dillen::kernel {

enum class MechanismDefinitionDeclareResult
{
    Added,
    InvalidDefinition,
    DependenciesNotFrozen,
    SchemaMissing,
    AlgorithmMissing,
    UnknownField,
    FieldValueInvalid,
    RequiredFieldMissing,
    UnknownRole,
    RoleBindingInvalid,
    DuplicateDefinition,
    IdCollision,
    Frozen
};

class MechanismDefinitionRegistry
{
public:
    MechanismDefinitionDeclareResult Declare(
        MechanismDefinition definition,
        const MechanismSchemaRegistry& schemas,
        const AlgorithmRegistry& algorithms
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const MechanismDefinition* Find(MechanismDefinitionId id) const;
    const MechanismDefinition* Find(
        MechanismTypeId type,
        std::string_view canonicalName
    ) const;
    const std::vector<MechanismDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<MechanismDefinition> definitions_;
    std::map<std::uint64_t, std::size_t> indexById_;
    bool frozen_ = false;
};

}
