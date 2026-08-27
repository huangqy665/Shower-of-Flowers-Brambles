#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kWarHistoryTemplate =
    0x484F493300001014ULL;
inline constexpr ParserId kWarHistoryParser =
    0x484F493300002014ULL;
inline constexpr DefinitionTypeId kWarHistoryDocumentType =
    0x484F493300003014ULL;
inline constexpr AnalysisPassId kWarHistoryResolvePass =
    0x484F493300004017ULL;

bool RegisterWarHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
