#include "native_access_core_module.h"

#include <memory>

#include "capability_registry.h"
#include "engine_registry.h"
#include "native_object_resolver.h"
#include "native_query_service.h"
#include "native_save_load_barrier.h"

namespace core
{

std::string_view NativeAccessCoreModule::Id() const
{
    return "native_access";
}

int NativeAccessCoreModule::Priority() const
{
    return 40;
}

bool NativeAccessCoreModule::Initialize(
    Services& services,
    std::string& error
)
{
    diagnostic_ = services.diagnostic;
    engine_ = services.engine;
    saveLoadBarrier_ = services.saveLoadBarrier;
    queries_ = services.queries
        ? services.queries
        : &GetNativeQueryService();
    resolvers_ = services.objectResolvers
        ? services.objectResolvers
        : &GetNativeObjectResolverService();
    capabilities_ = services.capabilities
        ? services.capabilities
        : &GetCapabilityRegistry();
    if (!engine_ || !queries_ || !resolvers_ || !capabilities_)
    {
        error = "native_access_services_missing";
        return false;
    }
    if (engine_->IsActive())
    {
        if (!capabilities_->SynchronizeEngineProfile(*engine_, error))
        {
            return false;
        }
    }
    resolvers_->Configure(engine_, capabilities_);
    queries_->Configure(capabilities_, engine_);

    const auto gate = [barrier = saveLoadBarrier_]() -> std::shared_ptr<void>
    {
        if (!barrier)
        {
            return {};
        }
        auto lease = barrier->TryAcquireStableLease();
        return lease
            ? std::static_pointer_cast<void>(
                std::make_shared<NativeSaveLoadWriteLease>(
                    std::move(*lease)
                )
            )
            : std::shared_ptr<void>{};
    };
    queries_->SetSafetyGate(gate);
    resolvers_->SetSafetyGate(gate);
    queries_->SetGameplayContext(false, {}, 0);
    if (diagnostic_)
    {
        diagnostic_("Native access services initialized");
    }
    error.clear();
    return true;
}

void NativeAccessCoreModule::OnLifecycleEvent(
    const LifecycleEvent& event
)
{
    if (!queries_)
    {
        return;
    }
    const bool stableGameplay = event.current.runtimeActive
        && event.current.phase == GamePhase::Gameplay
        && event.current.nativeWritesAllowed;
    queries_->SetGameplayContext(
        stableGameplay,
        stableGameplay ? event.current.playerTag : std::string{},
        event.current.generation
    );
    if (event.enteredGameplay
        || event.exitedGameplay
        || event.playerChanged
        || event.reason == LifecycleEventReason::SaveLoaded
        || (event.nativeWriteBarrierChanged
            && !event.current.nativeWritesAllowed))
    {
        queries_->ResetExecutionThread();
    }
}

void NativeAccessCoreModule::Tick(uint64_t)
{
}

void NativeAccessCoreModule::Shutdown()
{
    if (queries_)
    {
        queries_->SetGameplayContext(false, {}, 0);
        queries_->SetSafetyGate({});
        queries_->Configure(nullptr, nullptr);
    }
    if (resolvers_)
    {
        resolvers_->SetSafetyGate({});
        resolvers_->Configure(nullptr, nullptr);
    }
    if (diagnostic_)
    {
        diagnostic_("Native access services stopped");
    }
    diagnostic_ = {};
    engine_ = nullptr;
    saveLoadBarrier_ = nullptr;
    queries_ = nullptr;
    resolvers_ = nullptr;
    capabilities_ = nullptr;
}

}
