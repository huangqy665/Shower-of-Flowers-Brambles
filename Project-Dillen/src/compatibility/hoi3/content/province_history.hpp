#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"
#include "province_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class ProvinceHistoryField
{
    Owner,
    Controller,
    AddCore,
    RemoveCore,
    Terrain,
    StrategicResource,
    Infrastructure,
    Industry,
    VictoryPoints,
    NavalBase,
    AirBase,
    AntiAir,
    LandFort,
    CoastalFort,
    RadarStation,
    RocketTest,
    Manpower,
    Leadership,
    Energy,
    Metal,
    RareMaterials,
    CrudeOil,
    Fuel,
    Supplies
};

using ProvinceHistoryValue = std::variant<
    CountryDefinitionId,
    std::int64_t,
    double,
    std::string
>;

struct ProvinceHistoryOperation
{
    ProvinceHistoryField field = ProvinceHistoryField::Owner;
    ProvinceHistoryValue value = CountryDefinitionId{};
    DefinitionOrigin origin;
};

struct ProvinceHistoryPatch
{
    DefinitionDate date;
    std::vector<ProvinceHistoryOperation> operations;
    DefinitionOrigin origin;
    std::uint64_t sequence = 0;
};

struct ProvinceHistorySource
{
    DefinitionOrigin origin;
    std::vector<ProvinceHistoryOperation> initialOperations;
    std::vector<ProvinceHistoryPatch> patches;
};

struct ProvinceHistoryTimeline
{
    ProvinceDefinitionId province;
    std::vector<DefinitionOrigin> sources;
    std::vector<ProvinceHistoryOperation> initialOperations;
    std::vector<ProvinceHistoryPatch> patches;
};

}
