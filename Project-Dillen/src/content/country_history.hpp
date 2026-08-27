#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"
#include "order_of_battle_definition.hpp"
#include "province_definition.hpp"
#include "technology_definition.hpp"

namespace dillen::content {

enum class CountryHistoryField
{
    Capital,
    Government,
    Ideology,
    Minister,
    Alignment,
    Neutrality,
    NationalUnity,
    OrderOfBattle,
    LoadOrderOfBattle,
    OfficersRatio,
    Popularity,
    Organization,
    SetCountryFlag,
    SetGlobalFlag,
    JoinFaction,
    LeaveFaction,
    CreateAlliance,
    Decision,
    Threat,
    SetManpower,
    NamedAssignment,
    TechnologyLevel
};

struct CountryAlignment
{
    double x = 0.0;
    double y = 0.0;
};

struct CountryHistoryNamedNumber
{
    std::string name;
    double value = 0.0;
};

struct CountryHistoryNamedNumberMap
{
    std::vector<CountryHistoryNamedNumber> values;
};

struct CountryHistoryTechnologyLevel
{
    TechnologyDefinitionId technology;
    int level = 0;
};

using CountryHistoryValue = std::variant<
    std::int64_t,
    double,
    bool,
    std::string,
    CountryDefinitionId,
    ProvinceDefinitionId,
    OrderOfBattleDefinitionId,
    CountryHistoryTechnologyLevel,
    CountryAlignment,
    CountryHistoryNamedNumberMap
>;

struct CountryHistoryOperation
{
    CountryHistoryField field = CountryHistoryField::NamedAssignment;
    std::string key;
    CountryHistoryValue value = std::int64_t{0};
    DefinitionOrigin origin;
};

struct CountryHistoryPatch
{
    DefinitionDate date;
    std::vector<CountryHistoryOperation> operations;
    DefinitionOrigin origin;
    std::uint64_t sequence = 0;
};

struct CountryHistorySource
{
    DefinitionOrigin origin;
    std::vector<CountryHistoryOperation> initialOperations;
    std::vector<CountryHistoryPatch> patches;
};

struct CountryHistoryTimeline
{
    CountryDefinitionId country;
    std::vector<DefinitionOrigin> sources;
    std::vector<CountryHistoryOperation> initialOperations;
    std::vector<CountryHistoryPatch> patches;
};

}
