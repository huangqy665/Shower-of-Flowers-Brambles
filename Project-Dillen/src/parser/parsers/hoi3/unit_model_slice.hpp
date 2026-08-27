#pragma once

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kUnitModelTemplate =
    0x484F493300001009ULL;
inline constexpr ParserId kUnitModelParser =
    0x484F493300002009ULL;
inline constexpr DefinitionTypeId kUnitModelDocumentType =
    0x484F493300003009ULL;
inline constexpr AnalysisPassId kUnitModelDeclarePass =
    0x484F493300004012ULL;
inline constexpr AnalysisPassId kUnitModelResolvePass =
    0x484F493300004013ULL;

bool RegisterUnitModelSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
);

}
