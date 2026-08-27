#pragma once

#include <string>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "war_history.hpp"

namespace dillen::parser::hoi3 {

struct UnresolvedWarParticipantOperation
{
    content::WarParticipantOperationKind kind =
        content::WarParticipantOperationKind::AddAttacker;
    content::CountryTag country;
    SourceSpan span;
};

struct UnresolvedWarGoal
{
    std::string casusBelli;
    content::CountryTag actor;
    content::CountryTag receiver;
    SourceSpan span;
};

struct UnresolvedWarHistoryPatch
{
    content::DefinitionDate date;
    std::vector<UnresolvedWarParticipantOperation> participantOperations;
    std::vector<UnresolvedWarGoal> warGoals;
    SourceSpan span;
};

struct WarHistoryDocument
{
    std::string name;
    bool limitedWar = false;
    std::vector<UnresolvedWarHistoryPatch> patches;
    SourceSpan span;
};

bool ParseWarHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
