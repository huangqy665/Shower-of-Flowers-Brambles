#pragma once

#include "resolver.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kDiplomacyHistoryTemplate =
    0x484F493300001013ULL;
inline constexpr ParserId kDiplomacyHistoryParser =
    0x484F493300002013ULL;
inline constexpr DefinitionTypeId kDiplomacyHistoryDocumentType =
    0x484F493300003013ULL;
inline constexpr ResolutionPassId kDiplomacyHistoryResolvePass =
    0x484F493300004016ULL;

bool RegisterDiplomacyHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
);

}
