#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kRegionDefinitionTemplate =
    0x484F493300001004ULL;
inline constexpr ParserId kRegionDefinitionParser =
    0x484F493300002004ULL;
inline constexpr DefinitionTypeId kRegionDefinitionDocumentType =
    0x484F493300003004ULL;
inline constexpr AnalysisPassId kRegionDefinitionDeclarePass =
    0x484F493300004004ULL;
inline constexpr AnalysisPassId kRegionDefinitionResolvePass =
    0x484F493300004005ULL;

bool RegisterRegionDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
