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
#include "mechanism_query_snapshot.hpp"
#include "mechanism_scheduler.hpp"
#include "mechanism_instance_store.hpp"
#include "order_of_battle_definition.hpp"
#include "province_history.hpp"
#include "technology_definition.hpp"
#include "war_history.hpp"

namespace dillen::worldbuilder {

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
    std::optional<content::OrderOfBattleDefinitionId> definition;
    std::string unresolvedPath;
    bool additive = false;
};

struct RuntimeUnitState
{
    RuntimeUnitId id;
    content::OrderOfBattleDefinitionId source;
    content::OrderOfBattleNodeKind kind =
        content::OrderOfBattleNodeKind::Division;
    std::string name;
    std::optional<content::UnitTypeDefinitionId> unitType;
    content::CountryDefinitionId country;
    std::optional<content::CountryDefinitionId> expeditionaryOwner;
    std::optional<content::ProvinceDefinitionId> location;
    std::optional<content::ProvinceDefinitionId> base;
    std::optional<std::int64_t> leader;
    std::optional<RuntimeUnitId> parent;
    std::vector<RuntimeUnitId> children;
};

struct CountryRelationState
{
    content::DiplomaticRelationKind kind =
        content::DiplomaticRelationKind::Alliance;
    content::CountryDefinitionId first;
    content::CountryDefinitionId second;
};

struct RuntimeWarState
{
    content::WarHistoryDefinitionId id;
    std::string name;
    bool limitedWar = false;
    std::set<content::CountryDefinitionId> attackers;
    std::set<content::CountryDefinitionId> defenders;
    std::vector<content::WarGoalDefinition> warGoals;
};

struct CountryState
{
    content::CountryDefinitionId id;
    content::CountryTag tag;
    std::optional<content::ProvinceDefinitionId> capital;
    std::string government;
    std::string ideology;
    std::map<std::string, std::uint64_t> ministers;
    std::optional<content::CountryAlignment> alignment;
    std::optional<double> neutrality;
    std::optional<double> nationalUnity;
    std::optional<double> officersRatio;
    std::optional<double> threat;
    std::optional<double> manpower;
    std::map<std::string, double> popularity;
    std::map<std::string, double> organization;
    std::map<std::string, CountryScalarValue> namedAssignments;
    std::map<content::TechnologyDefinitionId, int> technologies;
    std::set<std::string> flags;
    std::optional<std::string> faction;
    std::set<content::CountryDefinitionId> alliances;
    std::set<content::CountryDefinitionId> guarantees;
    std::set<content::CountryDefinitionId> guaranteedBy;
    std::set<content::CountryDefinitionId> subjects;
    std::optional<content::CountryDefinitionId> overlord;
    std::set<content::WarHistoryDefinitionId> offensiveWars;
    std::set<content::WarHistoryDefinitionId> defensiveWars;
    std::set<std::string> decisions;
    std::vector<OrderOfBattleSelection> ordersOfBattle;
    std::vector<RuntimeUnitId> unitRoots;
    std::vector<content::ProvinceDefinitionId> ownedProvinces;
    std::vector<content::ProvinceDefinitionId> controlledProvinces;
    std::vector<content::ProvinceDefinitionId> coreProvinces;
};

struct ProvinceState
{
    content::ProvinceDefinitionId id;
    std::optional<content::CountryDefinitionId> owner;
    std::optional<content::CountryDefinitionId> controller;
    std::set<content::CountryDefinitionId> cores;
    std::string terrain;
    std::string strategicResource;
    std::map<content::ProvinceHistoryField, double> numericValues;
    std::vector<RuntimeUnitId> locatedUnits;
    std::vector<RuntimeUnitId> basedUnits;

    std::optional<double> Numeric(
        content::ProvinceHistoryField field
    ) const;
};

class AuthoritativeWorld
{
public:
    const content::DefinitionDate& Date() const noexcept;
    const std::vector<CountryState>& Countries() const noexcept;
    const std::vector<ProvinceState>& Provinces() const noexcept;
    const std::vector<RuntimeUnitState>& Units() const noexcept;
    const std::vector<CountryRelationState>& Relations() const noexcept;
    const std::vector<RuntimeWarState>& Wars() const noexcept;
    const kernel::MechanismInstanceStore& Mechanisms() const noexcept;
    const kernel::MechanismQuerySnapshot& MechanismSnapshot() const noexcept;
    const kernel::WorldCommandQueue& WorldCommands() const noexcept;
    const kernel::WorldEventQueue& WorldEvents() const noexcept;
    std::uint64_t Tick() const noexcept;
    std::uint64_t Revision() const noexcept;
    std::uint64_t EnqueueWorldTransaction(
        kernel::WorldTransaction transaction,
        std::uint64_t notBeforeTick
    );
    kernel::WorldTransactionResult ApplyWorldTransaction(
        const kernel::WorldTransaction& transaction,
        const kernel::MechanismDefinitionRegistry& definitions,
        const kernel::MechanismSchemaRegistry& schemas,
        std::uint64_t currentTick
    );
    kernel::MechanismTransactionResult ApplyMechanismTransaction(
        const std::vector<kernel::MechanismCommand>& commands,
        const kernel::MechanismDefinitionRegistry& definitions,
        const kernel::MechanismSchemaRegistry& schemas,
        std::uint64_t currentTick
    );
    kernel::MechanismSchedulerTickResult RunMechanismSchedulerTick(
        const kernel::MechanismDefinitionRegistry& definitions,
        const kernel::MechanismSchemaRegistry& schemas,
        std::uint64_t nextTick
    );
    std::vector<kernel::WorldEvent> DrainWorldEvents();
    const std::set<std::string>& GlobalFlags() const noexcept;
    std::optional<content::BookmarkDefinitionId> Bookmark() const noexcept;
    std::optional<content::ScenarioDefinitionId> Scenario() const noexcept;
    const CountryState* FindCountry(
        content::CountryDefinitionId id
    ) const;
    const CountryState* FindCountry(std::string_view tag) const;
    const ProvinceState* FindProvince(
        content::ProvinceDefinitionId id
    ) const;
    const ProvinceState* FindProvince(std::uint32_t id) const;
    const RuntimeUnitState* FindUnit(RuntimeUnitId id) const;
    const RuntimeWarState* FindWar(
        content::WarHistoryDefinitionId id
    ) const;

private:
    friend class WorldBuilder;

    CountryState* FindCountryMutable(content::CountryDefinitionId id);
    ProvinceState* FindProvinceMutable(content::ProvinceDefinitionId id);
    RuntimeUnitState* FindUnitMutable(RuntimeUnitId id);

    content::DefinitionDate date_;
    std::vector<CountryState> countries_;
    std::vector<ProvinceState> provinces_;
    std::vector<RuntimeUnitState> units_;
    std::vector<CountryRelationState> relations_;
    std::vector<RuntimeWarState> wars_;
    kernel::MechanismInstanceStore mechanisms_;
    kernel::MechanismQuerySnapshot mechanismSnapshot_;
    kernel::WorldCommandQueue worldCommands_;
    kernel::WorldEventQueue worldEvents_;
    kernel::MechanismScheduler mechanismScheduler_;
    std::uint64_t revision_ = 0;
    std::set<std::string> globalFlags_;
    std::optional<content::BookmarkDefinitionId> bookmark_;
    std::optional<content::ScenarioDefinitionId> scenario_;
};

}
