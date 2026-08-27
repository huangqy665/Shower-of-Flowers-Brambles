#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "definition_origin.hpp"
#include "province_definition.hpp"

namespace dillen::content {

struct RegionDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept;
bool operator!=(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept;
bool operator<(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept;

RegionDefinitionId StableRegionDefinitionId(
    std::string_view name
) noexcept;

struct RegionDefinition
{
    RegionDefinitionId id;
    std::string name;
    DefinitionOrigin origin;
    std::vector<ProvinceDefinitionId> provinces;
    std::vector<std::string> flags;
    bool resolved = false;

    bool HasFlag(std::string_view flag) const noexcept;
};

}
