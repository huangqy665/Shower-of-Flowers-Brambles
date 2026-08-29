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
    dillen::compatibility::hoi3::content::DefinitionDate date;
    std::vector<dillen::compatibility::hoi3::content::CountryTag> countries;
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
    dillen::compatibility::hoi3::content::DefinitionDate startDate;
    dillen::compatibility::hoi3::content::DefinitionDate endDate;
    std::vector<dillen::compatibility::hoi3::content::CountryTag> selectableCountries;
    std::vector<dillen::compatibility::hoi3::content::CountryTag> additionalCountries;
    SourceSpan span;
};

bool ParseBookmarks(ParserCursor& cursor, ParseArtifact& artifact);
bool ParseScenario(ParserCursor& cursor, ParseArtifact& artifact);

}
