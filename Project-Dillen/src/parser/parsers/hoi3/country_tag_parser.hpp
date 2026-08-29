#pragma once

#include <string>
#include <vector>

#include "country_tag_definition.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"

namespace dillen::parser::hoi3 {

struct CountryTagDeclaration
{
    dillen::compatibility::hoi3::content::CountryTag tag;
    std::string declaredPath;
    SourceSpan tagSpan;
    SourceSpan pathSpan;
};

struct CountryTagDocument
{
    std::vector<CountryTagDeclaration> declarations;
};

bool ParseCountryTagIndex(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
