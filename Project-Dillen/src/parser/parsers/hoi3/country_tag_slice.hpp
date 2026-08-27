#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr DialectId kHoi3ClausewitzDialect =
    0x484F493300000001ULL;
inline constexpr TemplateId kCountryTagTemplate =
    0x484F493300001001ULL;
inline constexpr ParserId kCountryTagParser =
    0x484F493300002001ULL;
inline constexpr DefinitionTypeId kCountryTagDocumentType =
    0x484F493300003001ULL;
inline constexpr AnalysisPassId kCountryTagDeclarePass =
    0x484F493300004001ULL;

bool RegisterCountryTagSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
