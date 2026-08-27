#pragma once

#include "country_definition.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"

namespace dillen::parser::hoi3 {

struct CountryDefinitionDocument
{
    content::CountryDefinition definition;
};

bool ParseCountryDefinition(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
