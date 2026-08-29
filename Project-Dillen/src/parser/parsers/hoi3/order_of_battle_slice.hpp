#pragma once

#include "resolver.hpp"
#include "definition_registry.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

inline constexpr TemplateId kHistoryOrderOfBattleTemplate =
    0x484F493300001010ULL;
inline constexpr TemplateId kScenarioOrderOfBattleTemplate =
    0x484F493300001011ULL;
inline constexpr TemplateId kScenarioArmyTemplate =
    0x484F493300001012ULL;
inline constexpr ParserId kOrderOfBattleParser =
    0x484F493300002010ULL;
inline constexpr DefinitionTypeId kOrderOfBattleDocumentType =
    0x484F493300003010ULL;
inline constexpr ResolutionPassId kOrderOfBattleDeclarePass =
    0x484F493300004014ULL;
inline constexpr ResolutionPassId kOrderOfBattleResolvePass =
    0x484F493300004015ULL;

bool RegisterOrderOfBattleSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
);

}
