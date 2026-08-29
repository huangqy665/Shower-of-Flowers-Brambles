#pragma once

#include "resolver.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kProvinceHistoryTemplate =
    0x484F493300001005ULL;
inline constexpr ParserId kProvinceHistoryParser =
    0x484F493300002005ULL;
inline constexpr DefinitionTypeId kProvinceHistoryDocumentType =
    0x484F493300003005ULL;
inline constexpr ResolutionPassId kProvinceHistoryResolvePass =
    0x484F493300004006ULL;

bool RegisterProvinceHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
);

}
