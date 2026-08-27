#pragma once

#include <cstddef>
#include <vector>

#include "diagnostic.hpp"
#include "parse_result.hpp"
#include "province_definition.hpp"
#include "source_buffer.hpp"

namespace dillen::parser::hoi3 {

struct ProvinceDefinitionDocument
{
    std::vector<content::ProvinceDefinition> definitions;
    std::size_t paletteRowCount = 0;
    std::size_t compatibilityWrappedRowCount = 0;
};

bool ParseProvinceDefinitionCsv(
    const SourceBuffer& source,
    DiagnosticBag& diagnostics,
    ParseArtifact& artifact
);

}
