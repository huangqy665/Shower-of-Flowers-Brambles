#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"

namespace dillen::content {

struct WarHistoryDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept;
bool operator!=(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept;
bool operator<(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept;

std::string NormalizeWarHistoryPath(std::string_view path);
WarHistoryDefinitionId StableWarHistoryDefinitionId(
    std::string_view virtualPath
);

enum class WarParticipantOperationKind
{
    AddAttacker,
    RemoveAttacker,
    AddDefender,
    RemoveDefender
};

struct WarParticipantOperation
{
    WarParticipantOperationKind kind =
        WarParticipantOperationKind::AddAttacker;
    CountryDefinitionId country;
    DefinitionOrigin origin;
};

struct WarGoalDefinition
{
    std::string casusBelli;
    CountryDefinitionId actor;
    CountryDefinitionId receiver;
    DefinitionOrigin origin;
};

struct WarHistoryPatch
{
    DefinitionDate date;
    std::vector<WarParticipantOperation> participantOperations;
    std::vector<WarGoalDefinition> warGoals;
    DefinitionOrigin origin;
    std::uint64_t sequence = 0;
};

struct WarHistoryTimeline
{
    WarHistoryDefinitionId id;
    std::string virtualPath;
    std::string name;
    bool limitedWar = false;
    std::vector<WarHistoryPatch> patches;
    DefinitionOrigin origin;
};

}
