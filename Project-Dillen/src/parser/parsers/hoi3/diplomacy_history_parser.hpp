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
    content::DiplomaticRelationKind kind =
        content::DiplomaticRelationKind::Alliance;
    content::CountryTag first;
    content::CountryTag second;
    content::DefinitionDate startDate;
    content::DefinitionDate endDate;
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
