#include <array>
#include <iostream>
#include <string>

#include "hoi3_gameplay_effects.h"
#include "native_effect_bridge.h"

int main()
{
    constexpr std::array<const char*, 59> ExpectedOperations = {
        "global.set_flag",
        "global.clear_flag",
        "event.fire",
        "event.execute",
        "event.enqueue",
        "event.cancel",
        "decision.execute",
        "decision.enqueue",
        "decision.cancel",
        "queue.cancel",
        "province.set_owner",
        "province.set_controller",
        "province.add_core",
        "province.remove_core",
        "province.set_building_level",
        "technology.set_level",
        "research.set_progress",
        "research.complete",
        "research.cancel",
        "country.set_capital",
        "country.set_acting_capital",
        "country.add_manpower",
        "country.set_manpower",
        "country.add_goods",
        "country.set_goods",
        "country.add_national_unity",
        "country.set_national_unity",
        "country.add_dissent",
        "country.set_dissent",
        "country.add_neutrality",
        "country.set_neutrality",
        "country.add_officers",
        "country.set_officers",
        "country.add_diplomatic_influence",
        "country.set_diplomatic_influence",
        "country.add_leadership",
        "country.set_leadership",
        "country.add_convoys",
        "country.set_convoys",
        "country.add_escorts",
        "country.set_escorts",
        "country.add_free_spies",
        "country.set_free_spies",
        "country.set_government",
        "country.set_ruling_ideology",
        "country.add_ideology_popularity",
        "country.set_ideology_popularity",
        "country.add_ideology_organization",
        "country.set_ideology_organization",
        "diplomacy.add_relation",
        "diplomacy.set_relation",
        "diplomacy.add_threat",
        "diplomacy.set_threat",
        "espionage.set_presence_level",
        "intelligence.set_province_level",
        "country.add_modifier",
        "country.remove_modifier",
        "province.add_modifier",
        "province.remove_modifier"
    };
    core::NativeEffectService service;
    core::Hoi3GameplayEffects effects;
    std::string error;
    if (!effects.RegisterHandlers(service, error))
    {
        std::cerr << "Native gameplay effect registration failed: "
                  << error << '\n';
        return 1;
    }
    for (const char* operation : ExpectedOperations)
    {
        if (!service.HasHandler(operation))
        {
            std::cerr << "Missing native gameplay effect handler: "
                      << operation << '\n';
            return 1;
        }
    }

    service.SetGameplayContext(true, "CHI", 1);
    core::NativeEffectBatch batch;
    core::NativeEffect effect;
    effect.operation = "country.add_manpower";
    effect.arguments["tag"] = std::string("CHI");
    effect.arguments["amount"] = -1.0;
    batch.effects.push_back(std::move(effect));
    const core::NativeEffectResult result = service.ExecuteImmediate(
        std::move(batch),
        1,
        1
    );
    if (effects.IsSupportedExecutable()
        || result.status != core::NativeEffectStatus::PreparationFailed
        || result.message != "hoi3_native_effects_unsupported_executable")
    {
        std::cerr << "Unsupported-host guard failed\n";
        return 2;
    }

    effects.UnregisterHandlers();
    for (const char* operation : ExpectedOperations)
    {
        if (service.HasHandler(operation))
        {
            std::cerr << "Native gameplay effect unregister failed: "
                      << operation << '\n';
            return 3;
        }
    }

    std::cout << "HOI3 gameplay effects probe passed\n";
    return 0;
}
