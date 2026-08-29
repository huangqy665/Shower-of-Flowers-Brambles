#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "country_history.hpp"
#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "diplomacy_history.hpp"
#include "launch_definition.hpp"
#include "authoritative_world.hpp"
#include "order_of_battle_definition.hpp"
#include "province_history.hpp"
#include "technology_definition.hpp"
#include "war_history.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

using CountryScalarValue = std::variant<
    std::int64_t,
    double,
    bool,
    std::string
>;

struct RuntimeUnitId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(RuntimeUnitId first, RuntimeUnitId second) noexcept;
bool operator!=(RuntimeUnitId first, RuntimeUnitId second) noexcept;
bool operator<(RuntimeUnitId first, RuntimeUnitId second) noexcept;

struct OrderOfBattleSelection
{
    std::optional<dillen::compatibility::hoi3::content::OrderOfBattleDefinitionId> definition;
    std::string unresolvedPath;
    bool additive = false;
};

struct RuntimeUnitState
{
    RuntimeUnitId id;
    dillen::compatibility::hoi3::content::OrderOfBattleDefinitionId source;
    dillen::compatibility::hoi3::content::OrderOfBattleNodeKind kind =
        dillen::compatibility::hoi3::content::OrderOfBattleNodeKind::Division;
    std::string name;
    std::optional<dillen::compatibility::hoi3::content::UnitTypeDefinitionId> unitType;
    dillen::compatibility::hoi3::content::CountryDefinitionId country;
    std::optional<dillen::compatibility::hoi3::content::CountryDefinitionId> expeditionaryOwner;
    std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> location;
    std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> base;
    std::optional<std::int64_t> leader;
    std::optional<RuntimeUnitId> parent;
    std::vector<RuntimeUnitId> children;
};

struct CountryRelationState
{
    dillen::compatibility::hoi3::content::DiplomaticRelationKind kind =
        dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance;
    dillen::compatibility::hoi3::content::CountryDefinitionId first;
    dillen::compatibility::hoi3::content::CountryDefinitionId second;
};

struct RuntimeWarState
{
    dillen::compatibility::hoi3::content::WarHistoryDefinitionId id;
    std::string name;
    bool limitedWar = false;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> attackers;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> defenders;
    std::vector<dillen::compatibility::hoi3::content::WarGoalDefinition> warGoals;
};

struct CountryState
{
    dillen::compatibility::hoi3::content::CountryDefinitionId id;
    dillen::compatibility::hoi3::content::CountryTag tag;
    std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> capital;
    std::string government;
    std::string ideology;
    std::map<std::string, std::uint64_t> ministers;
    std::optional<dillen::compatibility::hoi3::content::CountryAlignment> alignment;
    std::optional<double> neutrality;
    std::optional<double> nationalUnity;
    std::optional<double> officersRatio;
    std::optional<double> threat;
    std::optional<double> manpower;
    std::map<std::string, double> popularity;
    std::map<std::string, double> organization;
    std::map<std::string, CountryScalarValue> namedAssignments;
    std::map<dillen::compatibility::hoi3::content::TechnologyDefinitionId, int> technologies;
    std::set<std::string> flags;
    std::optional<std::string> faction;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> alliances;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> guarantees;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> guaranteedBy;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> subjects;
    std::optional<dillen::compatibility::hoi3::content::CountryDefinitionId> overlord;
    std::set<dillen::compatibility::hoi3::content::WarHistoryDefinitionId> offensiveWars;
    std::set<dillen::compatibility::hoi3::content::WarHistoryDefinitionId> defensiveWars;
    std::set<std::string> decisions;
    std::vector<OrderOfBattleSelection> ordersOfBattle;
    std::vector<RuntimeUnitId> unitRoots;
    std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId> ownedProvinces;
    std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId> controlledProvinces;
    std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId> coreProvinces;
};

struct ProvinceState
{
    dillen::compatibility::hoi3::content::ProvinceDefinitionId id;
    std::optional<dillen::compatibility::hoi3::content::CountryDefinitionId> owner;
    std::optional<dillen::compatibility::hoi3::content::CountryDefinitionId> controller;
    std::set<dillen::compatibility::hoi3::content::CountryDefinitionId> cores;
    std::string terrain;
    std::string strategicResource;
    std::map<dillen::compatibility::hoi3::content::ProvinceHistoryField, double> numericValues;
    std::vector<RuntimeUnitId> locatedUnits;
    std::vector<RuntimeUnitId> basedUnits;

    std::optional<double> Numeric(
        dillen::compatibility::hoi3::content::ProvinceHistoryField field
    ) const;
};

class Hoi3WorldState
{
public:
    const dillen::compatibility::hoi3::content::DefinitionDate& Date() const noexcept;
    const std::vector<CountryState>& Countries() const noexcept;
    const std::vector<ProvinceState>& Provinces() const noexcept;
    const std::vector<RuntimeUnitState>& Units() const noexcept;
    const std::vector<CountryRelationState>& Relations() const noexcept;
    const std::vector<RuntimeWarState>& Wars() const noexcept;
    const kernel::MechanismInstanceStore& Mechanisms() const noexcept;
    const world::AuthoritativeWorld& World() const noexcept;
    world::AuthoritativeWorld& World() noexcept;
    const std::set<std::string>& GlobalFlags() const noexcept;
    std::optional<dillen::compatibility::hoi3::content::BookmarkDefinitionId> Bookmark() const noexcept;
    std::optional<dillen::compatibility::hoi3::content::ScenarioDefinitionId> Scenario() const noexcept;
    const CountryState* FindCountry(
        dillen::compatibility::hoi3::content::CountryDefinitionId id
    ) const;
    const CountryState* FindCountry(std::string_view tag) const;
    const ProvinceState* FindProvince(
        dillen::compatibility::hoi3::content::ProvinceDefinitionId id
    ) const;
    const ProvinceState* FindProvince(std::uint32_t id) const;
    const RuntimeUnitState* FindUnit(RuntimeUnitId id) const;
    const RuntimeWarState* FindWar(
        dillen::compatibility::hoi3::content::WarHistoryDefinitionId id
    ) const;

private:
    friend class WorldBuilder;

    CountryState* FindCountryMutable(dillen::compatibility::hoi3::content::CountryDefinitionId id);
    ProvinceState* FindProvinceMutable(dillen::compatibility::hoi3::content::ProvinceDefinitionId id);
    RuntimeUnitState* FindUnitMutable(RuntimeUnitId id);

    dillen::compatibility::hoi3::content::DefinitionDate date_;
    std::vector<CountryState> countries_;
    std::vector<ProvinceState> provinces_;
    std::vector<RuntimeUnitState> units_;
    std::vector<CountryRelationState> relations_;
    std::vector<RuntimeWarState> wars_;
    world::AuthoritativeWorld world_;
    std::set<std::string> globalFlags_;
    std::optional<dillen::compatibility::hoi3::content::BookmarkDefinitionId> bookmark_;
    std::optional<dillen::compatibility::hoi3::content::ScenarioDefinitionId> scenario_;
};

}
