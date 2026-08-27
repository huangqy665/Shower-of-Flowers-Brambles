#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr DialectId kHoi3SemicolonCsvDialect =
    0x484F493300000002ULL;
inline constexpr TemplateId kProvinceDefinitionTemplate =
    0x484F493300001003ULL;
inline constexpr ParserId kProvinceDefinitionParser =
    0x484F493300002003ULL;
inline constexpr DefinitionTypeId kProvinceDefinitionDocumentType =
    0x484F493300003003ULL;
inline constexpr AnalysisPassId kProvinceDefinitionDeclarePass =
    0x484F493300004003ULL;

bool RegisterProvinceDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
