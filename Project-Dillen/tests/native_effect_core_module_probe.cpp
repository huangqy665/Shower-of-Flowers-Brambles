#include <iostream>
#include <string>

#include "core_hook_registry.h"
#include "core_lifecycle.h"
#include "native_effect_bridge.h"
#include "native_effect_core_module.h"
#include "native_save_load_barrier.h"

namespace
{

void Dispatch(
    core::LifecycleService& lifecycle,
    NativeEffectCoreModule& module
)
{
    for (const core::LifecycleEvent& event : lifecycle.DrainEvents())
    {
        module.OnLifecycleEvent(event);
    }
}

core::NativeEffectResult Execute(core::NativeEffectService& service)
{
    core::NativeEffect effect;
    effect.operation = "probe.write";
    core::NativeEffectBatch batch;
    batch.effects.push_back(std::move(effect));
    return service.ExecuteImmediate(std::move(batch), 1, 1);
}

}

int main()
{
    core::HookRegistry hooks;
    core::LifecycleService lifecycle;
    core::NativeEffectService service;
    core::NativeSaveLoadBarrier saveLoadBarrier(1);
    saveLoadBarrier.Start();
    bool applied = false;
    std::string error;
    if (!service.RegisterHandler(
            "probe.write",
            [&applied](
                const core::NativeEffect&,
                const core::NativeEffectExecutionContext&,
                core::PreparedNativeEffect& prepared,
                std::string& prepareError
            )
            {
                prepared.apply = [&applied](std::string& applyError)
                {
                    applied = true;
                    applyError.clear();
                    return true;
                };
                prepareError.clear();
                return true;
            },
            error
        ))
    {
        std::cerr << "Probe handler registration failed\n";
        return 1;
    }

    core::Services services{
        hooks,
        lifecycle,
        [](std::string_view) {},
        &service,
        nullptr,
        &saveLoadBarrier
    };
    NativeEffectCoreModule module;
    if (!module.Initialize(services, error))
    {
        std::cerr << "Native effect module initialization failed\n";
        return 2;
    }

    lifecycle.Start();
    lifecycle.Observe(
        core::GamePhase::Gameplay,
        "CHI",
        core::LifecycleEventSource::NativeProbe
    );
    Dispatch(lifecycle, module);
    if (Execute(service).status
            != core::NativeEffectStatus::GameplayInactive
        || applied)
    {
        std::cerr << "Closed barrier permitted a native write\n";
        return 3;
    }

    lifecycle.SetNativeWriteBarrier(
        true,
        1,
        "probe_open",
        core::LifecycleEventSource::NativeProbe
    );
    core::NativeLifecycleSample safeSample;
    safeSample.available = true;
    safeSample.gameplay = true;
    safeSample.playerTag = "CHI";
    safeSample.gameStateAddress = 0x1000;
    safeSample.worldFingerprint = 1;
    saveLoadBarrier.Observe(safeSample);
    Dispatch(lifecycle, module);
    if (!Execute(service).Succeeded() || !applied)
    {
        std::cerr << "Open barrier rejected a native write\n";
        return 4;
    }

    applied = false;
    lifecycle.SetNativeWriteBarrier(
        false,
        2,
        "probe_load_started",
        core::LifecycleEventSource::NativeProbe
    );
    saveLoadBarrier.NotifyLoadStarted("probe_load");
    Dispatch(lifecycle, module);
    if (Execute(service).status
            != core::NativeEffectStatus::GameplayInactive
        || applied)
    {
        std::cerr << "Load barrier did not revoke native writes\n";
        return 5;
    }

    module.Shutdown();
    std::cout << "Native effect core module barrier: passed\n";
    return 0;
}
