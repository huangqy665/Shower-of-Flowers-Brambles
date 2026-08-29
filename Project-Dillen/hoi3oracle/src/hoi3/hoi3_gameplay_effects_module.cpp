#include "hoi3_gameplay_effects_module.h"

#include <utility>

#include "hoi3_gameplay_effects.h"
#include "native_effect_bridge.h"

namespace core
{

Hoi3GameplayEffectsModule::Hoi3GameplayEffectsModule() = default;
Hoi3GameplayEffectsModule::~Hoi3GameplayEffectsModule() = default;

std::string_view Hoi3GameplayEffectsModule::Id() const
{
    return "hoi3_gameplay_effects";
}

int Hoi3GameplayEffectsModule::Priority() const
{
    return 60;
}

bool Hoi3GameplayEffectsModule::Initialize(
    Services& services,
    std::string& error
)
{
    diagnostic_ = services.diagnostic;
    service_ = services.effects
        ? services.effects
        : &GetNativeEffectService();
    effects_ = std::make_unique<Hoi3GameplayEffects>();
    if (!effects_->RegisterHandlers(*service_, error))
    {
        effects_.reset();
        service_ = nullptr;
        diagnostic_ = {};
        return false;
    }
    if (diagnostic_)
    {
        diagnostic_(
            effects_->IsSupportedExecutable()
                ? "HOI3 gameplay native effects registered"
                : "HOI3 gameplay native effects registered for an unsupported host"
        );
    }
    error.clear();
    return true;
}

void Hoi3GameplayEffectsModule::OnLifecycleEvent(
    const LifecycleEvent& event
)
{
    writesAllowed_ = event.current.runtimeActive
        && event.current.phase == GamePhase::Gameplay
        && event.current.nativeWritesAllowed;
    if (!effects_)
    {
        return;
    }
    const bool resetQueue = event.enteredGameplay
        || event.exitedGameplay
        || event.playerChanged
        || (event.nativeWriteBarrierChanged
            && !event.current.nativeWritesAllowed)
        || event.reason == LifecycleEventReason::SaveLoaded
        || event.reason == LifecycleEventReason::RuntimeStopping;
    if (!resetQueue)
    {
        return;
    }
    const std::size_t cleared = effects_->ClearQueuedActions();
    if (cleared > 0 && diagnostic_)
    {
        diagnostic_(
            "HOI3 native dispatch queue cleared: "
            + std::to_string(cleared)
        );
    }
}

void Hoi3GameplayEffectsModule::Tick(uint64_t)
{
    if (!effects_ || !writesAllowed_)
    {
        return;
    }
    for (const std::string& message : effects_->Tick())
    {
        if (diagnostic_)
        {
            diagnostic_(message);
        }
    }
}

void Hoi3GameplayEffectsModule::Shutdown()
{
    if (effects_)
    {
        effects_->UnregisterHandlers();
        effects_.reset();
    }
    service_ = nullptr;
    diagnostic_ = {};
    writesAllowed_ = false;
}

}
