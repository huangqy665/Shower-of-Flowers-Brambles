#pragma once

#include <string>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"

namespace dillen::parser::hoi3 {

struct ParsedBookmark
{
    std::string name;
    std::string description;
    std::string icon;
    content::DefinitionDate date;
    std::vector<content::CountryTag> countries;
    SourceSpan span;
};

struct BookmarkDocument
{
    std::vector<ParsedBookmark> bookmarks;
};

struct ScenarioDocument
{
    std::string name;
    std::string description;
    std::string icon;
    content::DefinitionDate startDate;
    content::DefinitionDate endDate;
    std::vector<content::CountryTag> selectableCountries;
    std::vector<content::CountryTag> additionalCountries;
    SourceSpan span;
};

bool ParseBookmarks(ParserCursor& cursor, ParseArtifact& artifact);
bool ParseScenario(ParserCursor& cursor, ParseArtifact& artifact);

}
