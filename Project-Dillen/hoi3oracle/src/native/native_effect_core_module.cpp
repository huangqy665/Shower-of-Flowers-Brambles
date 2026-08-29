#include "native_effect_core_module.h"

#include <optional>
#include <utility>

#include "engine_registry.h"
#include "native_effect_bridge.h"
#include "native_save_load_barrier.h"

std::string_view NativeEffectCoreModule::Id() const
{
    return "native_effects";
}

int NativeEffectCoreModule::Priority() const
{
    return 50;
}

bool NativeEffectCoreModule::Initialize(
    core::Services& services,
    std::string& error
)
{
    diagnostic_ = services.diagnostic;
    service_ = services.effects
        ? services.effects
        : &core::GetNativeEffectService();
    saveLoadBarrier_ = services.saveLoadBarrier;
    capabilities_ = services.capabilities;
    service_->SetCapabilityRegistry(capabilities_);
    service_->SetCapabilityVersion(
        services.engine && services.engine->ActiveVersion()
            ? std::optional<core::engine::VersionId>(
                services.engine->ActiveVersion()->id
            )
            : std::nullopt
    );
    service_->SetSafetyGate(
        saveLoadBarrier_
            ? core::NativeEffectSafetyGate([barrier = saveLoadBarrier_]
              {
                  auto lease = barrier->TryAcquireWriteLease();
                  return lease
                      ? std::static_pointer_cast<void>(
                          std::make_shared<
                              core::NativeSaveLoadWriteLease
                          >(std::move(*lease))
                      )
                      : std::shared_ptr<void>{};
              })
            : core::NativeEffectSafetyGate{}
    );
    service_->SetGameplayContext(false, {}, 0);
    initialized_ = true;
    error.clear();
    return true;
}

void NativeEffectCoreModule::OnLifecycleEvent(
    const core::LifecycleEvent& event
)
{
    const bool gameplay = event.current.runtimeActive
        && event.current.phase == core::GamePhase::Gameplay
        && event.current.nativeWritesAllowed;
    if (!service_)
    {
        return;
    }
    service_->SetGameplayContext(
        gameplay,
        gameplay ? event.current.playerTag : std::string{},
        event.current.generation
    );
    if (event.reason == core::LifecycleEventReason::SaveLoaded
        || (event.nativeWriteBarrierChanged
            && !event.current.nativeWritesAllowed))
    {
        service_->ResetExecutionThread();
    }
}

void NativeEffectCoreModule::Tick(uint64_t)
{
}

void NativeEffectCoreModule::Shutdown()
{
    if (service_)
    {
        service_->SetGameplayContext(false, {}, 0);
        service_->SetSafetyGate({});
        service_->SetCapabilityVersion(std::nullopt);
        service_->SetCapabilityRegistry(nullptr);
    }
    if (diagnostic_)
    {
        diagnostic_("Native effect bridge stopped");
    }
    diagnostic_ = {};
    service_ = nullptr;
    saveLoadBarrier_ = nullptr;
    capabilities_ = nullptr;
    initialized_ = false;
}
