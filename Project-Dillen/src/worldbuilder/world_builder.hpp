#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "definition_origin.hpp"
#include "definition_registry.hpp"
#include "mechanism_definition_registry.hpp"
#include "world_state.hpp"

namespace dillen::worldbuilder {

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
    content::DefinitionOrigin origin;
};

class WorldBuildReport
{
public:
    void Clear();
    void Warning(
        std::string code,
        std::string message,
        content::DefinitionOrigin origin = {}
    );
    void Error(
        std::string code,
        std::string message,
        content::DefinitionOrigin origin = {}
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
        const content::DefinitionRegistry& definitions,
        content::BookmarkDefinitionId bookmark,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool BuildBookmark(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry& mechanismDefinitions,
        content::BookmarkDefinitionId bookmark,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenario(
        const content::DefinitionRegistry& definitions,
        content::ScenarioDefinitionId scenario,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenario(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry& mechanismDefinitions,
        content::ScenarioDefinitionId scenario,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool Build(
        const content::DefinitionRegistry& definitions,
        content::DefinitionDate date,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool Build(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry& mechanismDefinitions,
        content::DefinitionDate date,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;

private:
    bool BuildInternal(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry* mechanismDefinitions,
        content::DefinitionDate date,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool BuildBookmarkInternal(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry* mechanismDefinitions,
        content::BookmarkDefinitionId bookmark,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    bool BuildScenarioInternal(
        const content::DefinitionRegistry& definitions,
        const kernel::MechanismDefinitionRegistry* mechanismDefinitions,
        content::ScenarioDefinitionId scenario,
        AuthoritativeWorld& output,
        WorldBuildReport& report
    ) const;
    static bool InstantiateMechanisms(
        const kernel::MechanismDefinitionRegistry& definitions,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool BuildCountryRelationGraph(
        const content::DefinitionRegistry& definitions,
        content::DefinitionDate date,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool AddCountryRelation(
        content::DiplomaticRelationKind kind,
        content::CountryDefinitionId first,
        content::CountryDefinitionId second,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool ValidateCountryRelationGraph(
        const AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool BuildRuntimeWarGraph(
        const content::DefinitionRegistry& definitions,
        content::DefinitionDate date,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool ValidateRuntimeWarGraph(
        const AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool BuildTerritorialIndexes(
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool ValidateTerritorialIndexes(
        const AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool InstantiateRuntimeUnits(
        const content::DefinitionRegistry& definitions,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static std::optional<RuntimeUnitId> InstantiateRuntimeUnitNode(
        const content::DefinitionRegistry& definitions,
        const content::OrderOfBattleDefinition& source,
        const content::OrderOfBattleNode& node,
        content::CountryDefinitionId country,
        std::optional<RuntimeUnitId> parent,
        std::optional<content::ProvinceDefinitionId> inheritedLocation,
        std::optional<content::ProvinceDefinitionId> inheritedBase,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool ApplyCountryOperation(
        const content::DefinitionRegistry& definitions,
        const content::CountryHistoryOperation& operation,
        CountryState& country,
        AuthoritativeWorld& world,
        WorldBuildReport& report
    );
    static bool ApplyProvinceOperation(
        const content::DefinitionRegistry& definitions,
        const content::ProvinceHistoryOperation& operation,
        ProvinceState& province,
        WorldBuildReport& report
    );
};

bool IsValidWorldDate(content::DefinitionDate date) noexcept;

}
