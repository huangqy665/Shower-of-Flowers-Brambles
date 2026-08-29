#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "definition_origin.hpp"
#include "definition_registry.hpp"
#include "frozen_runtime_catalog.hpp"
#include "hoi3_world_state.hpp"
#include "initial_world_builder.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

enum class WorldBuildIssueSeverity
{
    Warning,
    Error
};

struct WorldBuildIssue
{
    WorldBuildIssueSeverity severity = WorldBuildIssueSeverity::Error;
    std::string code;
    std::string message;
    dillen::compatibility::hoi3::content::DefinitionOrigin origin;
};

class WorldBuildReport
{
public:
    void Clear();
    void Warning(
        std::string code,
        std::string message,
        dillen::compatibility::hoi3::content::DefinitionOrigin origin = {}
    );
    void Error(
        std::string code,
        std::string message,
        dillen::compatibility::hoi3::content::DefinitionOrigin origin = {}
    );
    bool HasErrors() const noexcept;
    std::size_t WarningCount() const noexcept;
    std::size_t ErrorCount() const noexcept;
    const std::vector<WorldBuildIssue>& All() const noexcept;

private:
    std::vector<WorldBuildIssue> issues_;
};

class WorldBuilder
{
public:
    bool BuildBookmark(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool BuildBookmark(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog& runtimeCatalog,
        dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenario(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenario(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog& runtimeCatalog,
        dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool Build(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        dillen::compatibility::hoi3::content::DefinitionDate date,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool Build(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog& runtimeCatalog,
        dillen::compatibility::hoi3::content::DefinitionDate date,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;

private:
    bool BuildInternal(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog* runtimeCatalog,
        dillen::compatibility::hoi3::content::DefinitionDate date,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool BuildBookmarkInternal(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog* runtimeCatalog,
        dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenarioInternal(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const kernel::FrozenRuntimeCatalog* runtimeCatalog,
        dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
        Hoi3WorldState& output,
        WorldBuildReport& report
    ) const;
    static bool BuildKernelWorld(
        const kernel::FrozenRuntimeCatalog& runtimeCatalog,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool BuildCountryRelationGraph(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        dillen::compatibility::hoi3::content::DefinitionDate date,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool AddCountryRelation(
        dillen::compatibility::hoi3::content::DiplomaticRelationKind kind,
        dillen::compatibility::hoi3::content::CountryDefinitionId first,
        dillen::compatibility::hoi3::content::CountryDefinitionId second,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool ValidateCountryRelationGraph(
        const Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool BuildRuntimeWarGraph(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        dillen::compatibility::hoi3::content::DefinitionDate date,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool ValidateRuntimeWarGraph(
        const Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool BuildTerritorialIndexes(
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool ValidateTerritorialIndexes(
        const Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool InstantiateRuntimeUnits(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static std::optional<RuntimeUnitId> InstantiateRuntimeUnitNode(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const dillen::compatibility::hoi3::content::OrderOfBattleDefinition& source,
        const dillen::compatibility::hoi3::content::OrderOfBattleNode& node,
        dillen::compatibility::hoi3::content::CountryDefinitionId country,
        std::optional<RuntimeUnitId> parent,
        std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> inheritedLocation,
        std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> inheritedBase,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool ApplyCountryOperation(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation,
        CountryState& country,
        Hoi3WorldState& world,
        WorldBuildReport& report
    );
    static bool ApplyProvinceOperation(
        const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
        const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation,
        ProvinceState& province,
        WorldBuildReport& report
    );
};

bool IsValidWorldDate(dillen::compatibility::hoi3::content::DefinitionDate date) noexcept;

}
