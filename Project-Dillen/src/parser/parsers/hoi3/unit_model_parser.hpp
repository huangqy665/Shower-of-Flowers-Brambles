#pragma once

#include <string>
#include <vector>

#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"
#include "unit_model_definition.hpp"

namespace dillen::parser::hoi3 {

struct UnresolvedUnitModelDefinition
{
    std::string unitTypeName;
    int modelIndex = 0;
    std::vector<dillen::compatibility::hoi3::content::UnitModelTechnologyLevel> technologyLevels;
    SourceSpan span;
};

struct UnitModelDocument
{
    std::vector<UnresolvedUnitModelDefinition> definitions;
};

bool ParseUnitModels(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
