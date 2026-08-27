#pragma once

#include <optional>
#include <string>
#include <vector>

#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"
#include "unit_type_definition.hpp"

namespace dillen::parser::hoi3 {

struct UnresolvedUnitTypeDefinition
{
    std::string name;
    content::UnitDomain domain = content::UnitDomain::Land;
    std::optional<std::string> sprite;
    std::optional<bool> active;
    std::optional<std::string> unitGroup;
    std::vector<std::string> usableBy;
    std::vector<content::UnitScalarProperty> scalarProperties;
    std::vector<content::UnitModifierBlock> modifierBlocks;
    SourceSpan span;
};

struct UnitTypeDocument
{
    std::vector<UnresolvedUnitTypeDefinition> definitions;
};

bool ParseUnitTypes(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
