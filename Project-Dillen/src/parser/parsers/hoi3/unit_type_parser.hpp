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
    dillen::compatibility::hoi3::content::UnitDomain domain = dillen::compatibility::hoi3::content::UnitDomain::Land;
    std::optional<std::string> sprite;
    std::optional<bool> active;
    std::optional<std::string> unitGroup;
    std::vector<std::string> usableBy;
    std::vector<dillen::compatibility::hoi3::content::UnitScalarProperty> scalarProperties;
    std::vector<dillen::compatibility::hoi3::content::UnitModifierBlock> modifierBlocks;
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
