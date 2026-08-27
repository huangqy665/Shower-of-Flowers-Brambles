#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"

namespace dillen::parser::hoi3 {

struct RegionProvinceReference
{
    std::uint32_t value = 0;
    SourceSpan span;
};

struct RegionFlagDeclaration
{
    std::string name;
    SourceSpan span;
};

struct RegionDefinitionDeclaration
{
    std::string name;
    SourceSpan nameSpan;
    std::vector<RegionProvinceReference> provinces;
    std::vector<RegionFlagDeclaration> flags;
};

struct RegionDefinitionDocument
{
    std::vector<RegionDefinitionDeclaration> declarations;
};

bool ParseRegionDefinitions(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
