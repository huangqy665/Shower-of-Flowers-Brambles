#include <iostream>
#include <string>

#include "engine_registry.h"

int main()
{
    core::engine::EngineRegistry registry;
    const auto& profile = core::engine::Hoi3Tfh402D328Profile();
    std::string error;
    if (!registry.SelectVersion(
            profile.version.executable,
            0x00400000,
            error
        ))
    {
        std::cerr << "Version selection failed: " << error << '\n';
        return 1;
    }
    if (!registry.IsActive()
        || registry.SymbolRva(
            core::engine::SymbolId::GameStateSingleton
        ) != 0x01689790
        || registry.SymbolRva(
            core::engine::SymbolId::SaveGameLoadCore
        ) != 0x0027CE30
        || registry.SymbolRva(
            core::engine::SymbolId::SaveGameLoadCallSite
        ) != 0x0030BA34
        || registry.SymbolRva(
            core::engine::SymbolId::SaveGameLoadReturn
        ) != 0x0030BA39
        || registry.SymbolRva(
            core::engine::SymbolId::SaveFileLoadWrapper
        ) != 0x0027CAE0
        || registry.SymbolRva(
            core::engine::SymbolId::SaveFileDeserializeCallSite
        ) != 0x0027CCF8
        || registry.SymbolRva(
            core::engine::SymbolId::SaveFileDeserializeReturn
        ) != 0x0027CCFD
        || registry.Resolve(
            core::engine::SymbolId::GameStateSingleton
        ) != 0x01A89790)
    {
        std::cerr << "Symbol registry mismatch\n";
        return 2;
    }
    const auto* saveLoadCandidate = registry.FindSymbol(
        "world_load.frontend_direct_call_site"
    );
    if (!saveLoadCandidate
        || saveLoadCandidate->confidence
            != core::engine::Confidence::Candidate
        || saveLoadCandidate->expectedCallTarget
            != core::engine::SymbolId::SaveGameLoadCore)
    {
        std::cerr << "Save-load candidate registry mismatch\n";
        return 3;
    }
    const auto* fileLoadCandidate = registry.FindSymbol(
        "save_load.file_deserialize_call_site"
    );
    if (!fileLoadCandidate
        || fileLoadCandidate->confidence
            != core::engine::Confidence::Proven
        || fileLoadCandidate->expectedCallTarget
            != core::engine::SymbolId::SaveGameLoadCore)
    {
        std::cerr << "Save-file load candidate registry mismatch\n";
        return 3;
    }
    const auto* countryTag = registry.FindField("country.tag");
    const auto* decisionType = registry.FindType(
        "abi.decision_command"
    );
    if (!countryTag
        || countryTag->owner != core::engine::TypeId::Country
        || countryTag->value != 0xCA4
        || !registry.FindType("province")
        || !decisionType
        || decisionType->size != 0xA0)
    {
        std::cerr << "Type registry mismatch\n";
        return 4;
    }

    registry.ObserveLifecycleGeneration(7);
    const auto handle = registry.MakeHandle(
        core::engine::TypeId::Country,
        9,
        0x1234
    );
    if (!registry.IsHandleCurrent(handle))
    {
        std::cerr << "Fresh handle rejected\n";
        return 5;
    }
    registry.InvalidateSymbol(
        core::engine::SymbolId::GameStateSingleton,
        "probe_symbol_invalidation"
    );
    if (registry.SymbolRva(
            core::engine::SymbolId::GameStateSingleton
        ) != 0
        || registry.SymbolValidation(
            core::engine::SymbolId::GameStateSingleton
        ) != core::engine::SymbolValidationState::Invalid)
    {
        std::cerr << "Symbol invalidation failed\n";
        return 6;
    }
    registry.ObserveLifecycleGeneration(8);
    if (registry.IsHandleCurrent(handle))
    {
        std::cerr << "Stale handle accepted\n";
        return 7;
    }

    registry.Invalidate("probe_invalidation");
    if (registry.IsActive()
        || registry.Resolve(
            core::engine::SymbolId::GameStateSingleton
        ) != 0
        || registry.LastError() != "probe_invalidation")
    {
        std::cerr << "Invalidation failed\n";
        return 8;
    }

    std::cout << "Engine registry probe passed\n";
    return 0;
}
