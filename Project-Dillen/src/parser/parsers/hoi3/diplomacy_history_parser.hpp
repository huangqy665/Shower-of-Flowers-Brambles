#pragma once

#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "diplomacy_history.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"

namespace dillen::parser::hoi3 {

struct ParsedDiplomaticRelation
{
    dillen::compatibility::hoi3::content::DiplomaticRelationKind kind =
        dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance;
    dillen::compatibility::hoi3::content::CountryTag first;
    dillen::compatibility::hoi3::content::CountryTag second;
    dillen::compatibility::hoi3::content::DefinitionDate startDate;
    dillen::compatibility::hoi3::content::DefinitionDate endDate;
    SourceSpan span;
};

struct DiplomacyHistoryDocument
{
    std::vector<ParsedDiplomaticRelation> relations;
};

bool ParseDiplomacyHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
