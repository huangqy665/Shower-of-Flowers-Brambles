#pragma once

#include "country_definition.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"

namespace dillen::parser::hoi3 {

struct CountryDefinitionDocument
{
    dillen::compatibility::hoi3::content::CountryDefinition definition;
};

bool ParseCountryDefinition(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
