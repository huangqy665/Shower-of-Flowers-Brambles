#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "unit_type_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class UnitTypeDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateName,
    IdCollision,
    Frozen
};

enum class UnitTypeResolveResult
{
    Resolved,
    UnitTypeMissing,
    AlreadyResolved,
    Frozen
};

class UnitTypeDefinitionRegistry
{
public:
    UnitTypeDeclareResult Declare(UnitTypeDefinition definition);
    UnitTypeResolveResult ResolveUsableBy(
        UnitTypeDefinitionId id,
        std::vector<CountryDefinitionId> usableBy
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const UnitTypeDefinition* Find(UnitTypeDefinitionId id) const;
    const UnitTypeDefinition* Find(std::string_view name) const;
    const std::vector<UnitTypeDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<UnitTypeDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByName_;
    bool frozen_ = false;
};

}
