#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "region_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class RegionDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateName,
    IdCollision,
    Frozen
};

enum class RegionResolveResult
{
    Resolved,
    RegionMissing,
    EmptyProvinceSet,
    AlreadyResolved,
    Frozen
};

class RegionDefinitionRegistry
{
public:
    RegionDeclareResult Declare(RegionDefinition definition);
    RegionResolveResult Resolve(
        RegionDefinitionId id,
        std::vector<ProvinceDefinitionId> provinces,
        std::vector<std::string> flags
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const RegionDefinition* Find(RegionDefinitionId id) const;
    const RegionDefinition* Find(std::string_view name) const;
    const std::vector<RegionDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<RegionDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByName_;
    bool frozen_ = false;
};

}
