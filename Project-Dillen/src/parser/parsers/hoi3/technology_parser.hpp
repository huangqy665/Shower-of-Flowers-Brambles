#pragma once

#include <optional>
#include <string>
#include <vector>

#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"
#include "technology_definition.hpp"

namespace dillen::parser::hoi3 {

struct UnresolvedTechnologyDefinition
{
    std::string name;
    double difficulty = 0.0;
    int startYear = 0;
    std::optional<int> firstOffset;
    std::optional<int> additionalOffset;
    std::optional<int> maxLevel;
    std::string folder;
    std::string onCompletion;
    std::optional<bool> change;
    std::optional<dillen::compatibility::hoi3::content::TechnologyRequirement> allow;
    std::vector<dillen::compatibility::hoi3::content::TechnologyResearchBonus> researchBonuses;
    std::vector<dillen::compatibility::hoi3::content::TechnologyUnitReference> activatedUnits;
    std::vector<std::string> activatedBuildings;
    std::vector<dillen::compatibility::hoi3::content::TechnologyScalarEffect> scalarEffects;
    std::vector<dillen::compatibility::hoi3::content::TechnologyEffectBlock> effectBlocks;
    SourceSpan span;
};

struct TechnologyDocument
{
    std::vector<UnresolvedTechnologyDefinition> definitions;
};

bool ParseTechnologies(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
