#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_origin.hpp"
#include "technology_definition.hpp"
#include "unit_type_definition.hpp"

namespace dillen::compatibility::hoi3::content {

struct UnitModelDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept;
bool operator!=(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept;
bool operator<(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept;

UnitModelDefinitionId StableUnitModelDefinitionId(
    CountryDefinitionId country,
    std::string_view unitTypeName,
    int modelIndex
) noexcept;

struct UnitModelTechnologyLevel
{
    std::string name;
    int level = 0;
    std::optional<TechnologyDefinitionId> technology;
};

struct UnitModelDefinition
{
    UnitModelDefinitionId id;
    CountryDefinitionId country;
    std::string unitTypeName;
    std::string normalizedUnitTypeName;
    int modelIndex = 0;
    std::optional<UnitTypeDefinitionId> unitType;
    std::vector<UnitModelTechnologyLevel> technologyLevels;
    DefinitionOrigin origin;
    bool referencesResolved = false;
};

}
