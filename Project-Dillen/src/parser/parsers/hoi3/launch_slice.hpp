#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kBookmarkTemplate =
    0x484F493300001009ULL;
inline constexpr TemplateId kScenarioTemplate =
    0x484F49330000100AULL;
inline constexpr ParserId kBookmarkParser =
    0x484F493300002009ULL;
inline constexpr ParserId kScenarioParser =
    0x484F49330000200AULL;
inline constexpr DefinitionTypeId kBookmarkDocumentType =
    0x484F493300003009ULL;
inline constexpr DefinitionTypeId kScenarioDocumentType =
    0x484F49330000300AULL;
inline constexpr AnalysisPassId kLaunchDefinitionDeclarePass =
    0x484F493300004009ULL;

bool RegisterLaunchDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
