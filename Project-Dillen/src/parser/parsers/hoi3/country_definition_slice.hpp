#pragma once

#include "resolver.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kCountryDefinitionTemplate =
    0x484F493300001002ULL;
inline constexpr ParserId kCountryDefinitionParser =
    0x484F493300002002ULL;
inline constexpr DefinitionTypeId kCountryDefinitionDocumentType =
    0x484F493300003002ULL;
inline constexpr ResolutionPassId kCountryDefinitionResolvePass =
    0x484F493300004002ULL;

bool RegisterCountryDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
);

}
