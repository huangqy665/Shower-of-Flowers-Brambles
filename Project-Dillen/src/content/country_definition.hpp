#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "definition_date.hpp"
#include "definition_origin.hpp"

namespace dillen::content {

struct CountryColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

struct DivisionTemplateDefinition
{
    std::string name;
    std::vector<std::string> brigadeTypes;
};

struct UnitNamePoolDefinition
{
    std::string unitType;
    std::vector<std::string> names;
};

struct MinisterPositionDefinition
{
    std::string position;
    std::string trait;
};

struct MinisterDefinition
{
    std::uint64_t id = 0;
    std::string name;
    std::string ideology;
    double loyalty = 0.0;
    std::string picture;
    DefinitionDate startDate;
    std::optional<DefinitionDate> deathDate;
    std::vector<MinisterPositionDefinition> positions;
};

struct CountryDefinition
{
    std::string virtualPath;
    DefinitionOrigin origin;
    std::optional<CountryColor> color;
    std::string graphicalCulture;
    bool major = false;
    std::optional<DefinitionDate> lastElection;
    std::optional<std::uint32_t> electionDurationMonths;
    std::vector<DivisionTemplateDefinition> defaultTemplates;
    std::vector<UnitNamePoolDefinition> unitNamePools;
    std::vector<MinisterDefinition> ministers;
};

}
