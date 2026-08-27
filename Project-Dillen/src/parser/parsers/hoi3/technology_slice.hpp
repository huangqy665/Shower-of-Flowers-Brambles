#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kTechnologyTemplate =
    0x484F493300001008ULL;
inline constexpr ParserId kTechnologyParser =
    0x484F493300002008ULL;
inline constexpr DefinitionTypeId kTechnologyDocumentType =
    0x484F493300003008ULL;
inline constexpr AnalysisPassId kTechnologyDeclarePass =
    0x484F493300004010ULL;
inline constexpr AnalysisPassId kTechnologyResolvePass =
    0x484F493300004011ULL;

bool RegisterTechnologySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
