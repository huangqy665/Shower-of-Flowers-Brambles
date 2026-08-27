#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kUnitTypeTemplate =
    0x484F493300001007ULL;
inline constexpr ParserId kUnitTypeParser =
    0x484F493300002007ULL;
inline constexpr DefinitionTypeId kUnitTypeDocumentType =
    0x484F493300003007ULL;
inline constexpr AnalysisPassId kUnitTypeDeclarePass =
    0x484F493300004008ULL;
inline constexpr AnalysisPassId kUnitTypeResolvePass =
    0x484F493300004009ULL;

bool RegisterUnitTypeSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
