#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kCountryHistoryTemplate =
    0x484F493300001006ULL;
inline constexpr ParserId kCountryHistoryParser =
    0x484F493300002006ULL;
inline constexpr DefinitionTypeId kCountryHistoryDocumentType =
    0x484F493300003006ULL;
inline constexpr AnalysisPassId kCountryHistoryResolvePass =
    0x484F493300004007ULL;

bool RegisterCountryHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
