#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "unit_model_definition.hpp"

namespace dillen::content {

enum class UnitModelDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateKey,
    IdCollision,
    Frozen
};

enum class UnitModelResolveResult
{
    Resolved,
    UnitModelMissing,
    AlreadyResolved,
    Frozen
};

class UnitModelDefinitionRegistry
{
public:
    UnitModelDeclareResult Declare(UnitModelDefinition definition);
    UnitModelResolveResult ResolveReferences(
        UnitModelDefinitionId id,
        std::optional<UnitTypeDefinitionId> unitType,
        std::vector<UnitModelTechnologyLevel> technologyLevels
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const UnitModelDefinition* Find(UnitModelDefinitionId id) const;
    const UnitModelDefinition* Find(
        CountryDefinitionId country,
        std::string_view unitTypeName,
        int modelIndex
    ) const;
    const std::vector<UnitModelDefinition>& All() const noexcept;

private:
    static std::string CompositeKey(
        CountryDefinitionId country,
        std::string_view unitTypeName,
        int modelIndex
    );
    void RebuildIndexes();

    std::vector<UnitModelDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByKey_;
    bool frozen_ = false;
};

}
