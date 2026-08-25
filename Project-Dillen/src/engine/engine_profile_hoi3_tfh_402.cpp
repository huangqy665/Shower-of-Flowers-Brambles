#include "engine_registry.h"

#include <array>
#include <cstddef>
#include <initializer_list>

#include "engine_abi_hoi3_tfh_402.h"

namespace core::engine
{
namespace
{

void ConfigureValidation(std::vector<SymbolDescriptor>& symbols)
{
    const auto setSignature = [&symbols](
        SymbolId id,
        std::initializer_list<uint8_t> bytes
    )
    {
        for (SymbolDescriptor& symbol : symbols)
        {
            if (symbol.id == id)
            {
                symbol.signature.assign(bytes.begin(), bytes.end());
                return;
            }
        }
    };
    const auto setCallTarget = [&symbols](
        SymbolId id,
        SymbolId target
    )
    {
        for (SymbolDescriptor& symbol : symbols)
        {
            if (symbol.id == id)
            {
                symbol.expectedCallTarget = target;
                return;
            }
        }
    };

    setSignature(
        SymbolId::LeaderSharedRemove,
        {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}
    );
    setSignature(
        SymbolId::LeaderManualSite,
        {0x6A, 0x01, 0x6A, 0x01, 0x57, 0xE8, 0x36, 0xAD, 0x0A, 0x00}
    );
    setSignature(
        SymbolId::LeaderCombatASite,
        {0x6A, 0x01, 0x53, 0x57, 0xE8, 0xB4, 0xF1, 0x11, 0x00}
    );
    setSignature(
        SymbolId::LeaderCombatBSite,
        {0x6A, 0x01, 0x53, 0x50, 0x8B, 0xCF, 0xE8, 0x89, 0x2B, 0x00, 0x00}
    );
    setSignature(
        SymbolId::CountryLeaderAdd,
        {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}
    );
    setSignature(
        SymbolId::CountryLeaderRemove,
        {0x56, 0x8D, 0xB0, 0x00, 0x0E, 0x00, 0x00}
    );
    setSignature(
        SymbolId::SaveGameLoadCore,
        {
            0x55, 0x8B, 0xEC, 0x6A, 0xFF,
            0x68, 0x51, 0xF9, 0xC9, 0x00,
            0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
        }
    );
    setSignature(
        SymbolId::SaveGameLoadCallSite,
        {0xE8, 0xF7, 0x13, 0xF7, 0xFF}
    );
    setSignature(
        SymbolId::SaveGameLoadReturn,
        {0x8B, 0xCF, 0xE8, 0x40, 0xBE, 0x00, 0x00}
    );
    setSignature(
        SymbolId::SaveFileLoadWrapper,
        {
            0x55, 0x8B, 0xEC, 0x6A, 0xFF,
            0x68, 0xD0, 0xF9, 0xC9, 0x00,
            0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
        }
    );
    setSignature(
        SymbolId::SaveFileDeserializeCallSite,
        {0xE8, 0x33, 0x01, 0x00, 0x00}
    );
    setSignature(
        SymbolId::SaveFileDeserializeReturn,
        {0x8D, 0x8D, 0xF0, 0xFB, 0xFF, 0xFF}
    );
    setCallTarget(
        SymbolId::SaveGameLoadCallSite,
        SymbolId::SaveGameLoadCore
    );
    setCallTarget(
        SymbolId::SaveFileDeserializeCallSite,
        SymbolId::SaveGameLoadCore
    );
    const std::initializer_list<uint8_t> frameSignature = {
        0x8B, 0x91, 0xA8, 0x00, 0x00, 0x00, 0x50, 0xFF, 0xD2
    };
    setSignature(SymbolId::D3d9FrameCallsite0, frameSignature);
    setSignature(SymbolId::D3d9FrameCallsite1, frameSignature);
    setSignature(SymbolId::D3d9FrameCallsite2, frameSignature);
    setSignature(SymbolId::D3d9FrameCallsite3, frameSignature);

    setCallTarget(
        SymbolId::LeaderOutcomeCallA,
        SymbolId::LeaderBattleOutcome
    );
    setCallTarget(
        SymbolId::LeaderOutcomeCallB,
        SymbolId::LeaderBattleOutcome
    );
    setCallTarget(
        SymbolId::LeaderWithdrawDeferredCall,
        SymbolId::LeaderDeferredEnqueue
    );
    setCallTarget(
        SymbolId::LeaderCombatADirectCall,
        SymbolId::LeaderSharedRemove
    );
}

VersionProfile BuildProfile()
{
    VersionProfile profile;
    profile.version = {
        VersionId::Hoi3Tfh402D328,
        "HOI3 TFH 4.02 (D328)",
        {0x014C, 0x50978B2F, 0x018B0000, 0}
    };

#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence) \
    profile.symbols.push_back({ \
        SymbolId::id, name, rva, SymbolKind::kind, \
        CallingConvention::call, Confidence::confidence, {}, std::nullopt \
    });
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime)
#include "engine_schema_hoi3_tfh_402.inc"

    profile.types = {
        {TypeId::GameState, "game_state", ObjectLifetime::Session},
        {TypeId::CountryDatabase, "country_database", ObjectLifetime::Session},
        {TypeId::Country, "country", ObjectLifetime::Session},
        {TypeId::Province, "province", ObjectLifetime::Session},
        {TypeId::GoodsPool, "goods_pool", ObjectLifetime::Session},
        {TypeId::ResolvedCountry, "resolved_country", ObjectLifetime::Session},
        {TypeId::ModifierCache, "modifier_cache", ObjectLifetime::Session},
        {TypeId::ModifierListNode, "modifier_list_node", ObjectLifetime::Session},
        {TypeId::ModifierRecord, "modifier_record", ObjectLifetime::Session},
        {TypeId::Ideology, "ideology", ObjectLifetime::Process},
        {TypeId::SpyPresence, "spy_presence", ObjectLifetime::Session},
        {TypeId::Relation, "relation", ObjectLifetime::Session},
        {TypeId::OwnedProvinceNode, "owned_province_node", ObjectLifetime::Session},
        {TypeId::ProvinceCoreNode, "province_core_node", ObjectLifetime::Session},
        {TypeId::BuildingDefinition, "building_definition", ObjectLifetime::Process},
        {TypeId::BuildingRecord, "building_record", ObjectLifetime::Session},
        {TypeId::TechnologyDefinition, "technology_definition", ObjectLifetime::Process},
        {TypeId::TechnologyStatus, "technology_status", ObjectLifetime::Session},
        {TypeId::ResearchNode, "research_node", ObjectLifetime::Session},
        {TypeId::Unit, "unit", ObjectLifetime::Session},
        {TypeId::UnitListNode, "unit_list_node", ObjectLifetime::Session},
        {TypeId::Leader, "leader", ObjectLifetime::Session},
        {TypeId::LeaderRegistryNode, "leader_registry_node", ObjectLifetime::Session},
        {TypeId::Combatant, "combatant", ObjectLifetime::Session},
        {TypeId::Combat, "combat", ObjectLifetime::Session},
        {TypeId::CombatSide, "combat_side", ObjectLifetime::Session},
        {TypeId::WinnerCountryNode, "winner_country_node", ObjectLifetime::Session}
        ,{TypeId::EngineRules, "engine_rules", ObjectLifetime::Process}
        ,{TypeId::CountryTagValue, "abi.country_tag", ObjectLifetime::Session}
        ,{TypeId::CountryEffectObject, "abi.country_effect", ObjectLifetime::Session}
        ,{TypeId::CountryEffectScope, "abi.country_effect_scope", ObjectLifetime::Session}
        ,{TypeId::IdeologyValuesEffectObject, "abi.ideology_values_effect", ObjectLifetime::Session}
        ,{TypeId::ThreatEffectObject, "abi.threat_effect", ObjectLifetime::Session}
        ,{TypeId::ProvinceAddEffectObject, "abi.province_add_effect", ObjectLifetime::Session}
        ,{TypeId::ProvinceRemoveEffectObject, "abi.province_remove_effect", ObjectLifetime::Session}
        ,{TypeId::ProvinceEffectScope, "abi.province_effect_scope", ObjectLifetime::Session}
        ,{TypeId::TargetedProvinceEffectScope, "abi.targeted_province_effect_scope", ObjectLifetime::Session}
        ,{TypeId::ProvinceCountryEffectObject, "abi.province_country_effect", ObjectLifetime::Session}
        ,{TypeId::AddCoreEffectObject, "abi.add_core_effect", ObjectLifetime::Session}
        ,{TypeId::RemoveCoreEffectObject, "abi.remove_core_effect", ObjectLifetime::Session}
        ,{TypeId::BuildingLevelCommandObject, "abi.building_level_command", ObjectLifetime::Session}
        ,{TypeId::CountryTechnologyCommandObject, "abi.country_technology_command", ObjectLifetime::Session}
        ,{TypeId::TechnologyLevelCommandObject, "abi.technology_level_command", ObjectLifetime::Session}
        ,{TypeId::ResearchProgressCommandObject, "abi.research_progress_command", ObjectLifetime::Session}
        ,{TypeId::TechnologyInvestmentEffectObject, "abi.technology_investment_effect", ObjectLifetime::Session}
        ,{TypeId::CapitalEffectObject, "abi.capital_effect", ObjectLifetime::Session}
        ,{TypeId::NativeBorrowedString32, "abi.borrowed_string32", ObjectLifetime::Session}
        ,{TypeId::GlobalFlagEffectObject, "abi.global_flag_effect", ObjectLifetime::Session}
        ,{TypeId::NativeEventScopeObject, "abi.event_scope", ObjectLifetime::Session}
        ,{TypeId::NativeDecisionCommandObject, "abi.decision_command", ObjectLifetime::Session}
        ,{TypeId::NativeEventLookupKey, "abi.event_lookup_key", ObjectLifetime::Session}
        ,{TypeId::D3d9FrameContext, "d3d9.frame_context", ObjectLifetime::Session}
    };
    const auto setTypeSize = [&profile](TypeId id, std::size_t size)
    {
        for (TypeDescriptor& type : profile.types)
        {
            if (type.id == id)
            {
                type.size = size;
                return;
            }
        }
    };
    setTypeSize(TypeId::CountryTagValue, sizeof(abi::CountryTagValue));
    setTypeSize(TypeId::CountryEffectObject, sizeof(abi::CountryEffectObject));
    setTypeSize(TypeId::CountryEffectScope, sizeof(abi::CountryEffectScope));
    setTypeSize(TypeId::IdeologyValuesEffectObject, sizeof(abi::IdeologyValuesEffectObject));
    setTypeSize(TypeId::ThreatEffectObject, sizeof(abi::ThreatEffectObject));
    setTypeSize(TypeId::ProvinceAddEffectObject, sizeof(abi::ProvinceAddEffectObject));
    setTypeSize(TypeId::ProvinceRemoveEffectObject, sizeof(abi::ProvinceRemoveEffectObject));
    setTypeSize(TypeId::ProvinceEffectScope, sizeof(abi::ProvinceEffectScope));
    setTypeSize(TypeId::TargetedProvinceEffectScope, sizeof(abi::TargetedProvinceEffectScope));
    setTypeSize(TypeId::ProvinceCountryEffectObject, sizeof(abi::ProvinceCountryEffectObject));
    setTypeSize(TypeId::AddCoreEffectObject, sizeof(abi::AddCoreEffectObject));
    setTypeSize(TypeId::RemoveCoreEffectObject, sizeof(abi::RemoveCoreEffectObject));
    setTypeSize(TypeId::BuildingLevelCommandObject, sizeof(abi::BuildingLevelCommandObject));
    setTypeSize(TypeId::CountryTechnologyCommandObject, sizeof(abi::CountryTechnologyCommandObject));
    setTypeSize(TypeId::TechnologyLevelCommandObject, sizeof(abi::TechnologyLevelCommandObject));
    setTypeSize(TypeId::ResearchProgressCommandObject, sizeof(abi::ResearchProgressCommandObject));
    setTypeSize(TypeId::TechnologyInvestmentEffectObject, sizeof(abi::TechnologyInvestmentEffectObject));
    setTypeSize(TypeId::CapitalEffectObject, sizeof(abi::CapitalEffectObject));
    setTypeSize(TypeId::NativeBorrowedString32, sizeof(abi::NativeBorrowedString32));
    setTypeSize(TypeId::GlobalFlagEffectObject, sizeof(abi::GlobalFlagEffectObject));
    setTypeSize(TypeId::NativeEventScopeObject, sizeof(abi::NativeEventScopeObject));
    setTypeSize(TypeId::NativeDecisionCommandObject, sizeof(abi::NativeDecisionCommandObject));
    setTypeSize(TypeId::NativeEventLookupKey, sizeof(abi::NativeEventLookupKey));

#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence)
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime) \
    profile.fields.push_back({ \
        FieldId::id, name, TypeId::type, value, size, \
        LayoutValueKind::kind, FieldAccess::access, \
        FieldSemantics::semantics, ObjectLifetime::lifetime \
    });
#include "engine_schema_hoi3_tfh_402.inc"

    ConfigureValidation(profile.symbols);
    return profile;
}

}

const VersionProfile& Hoi3Tfh402D328Profile()
{
    static const VersionProfile profile = BuildProfile();
    return profile;
}

}
