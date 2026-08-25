#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core_hook_registry.h"
#include "core_lifecycle.h"
#include "leader_capture_core_module.h"
#include "leader_capture_engine.h"
#include "native_save_load_barrier.h"

namespace
{

bool Dispatch(
    core::LifecycleService& lifecycle,
    LeaderCaptureCoreModule& module
)
{
    for (const core::LifecycleEvent& event : lifecycle.DrainEvents())
    {
        module.OnLifecycleEvent(event);
    }
    return true;
}

}

int main()
{
    core::HookRegistry hooks;
    core::LifecycleService lifecycle;
    core::NativeSaveLoadBarrier saveLoadBarrier(1);
    saveLoadBarrier.Start();
    std::vector<std::string> diagnostics;
    core::Services services{
        hooks,
        lifecycle,
        [&diagnostics](std::string_view message)
        {
            diagnostics.emplace_back(message);
        },
        nullptr,
        nullptr,
        &saveLoadBarrier
    };

    LeaderCaptureCoreModule module;
    std::string error;
    if (module.Id() != "leader_capture"
        || module.Priority() != 200
        || !module.Initialize(services, error))
    {
        std::cerr << "Leader Capture module initialization failed: "
                  << error << '\n';
        return 1;
    }

    const std::vector<core::HookStatus> statuses = hooks.Status();
    if (statuses.size() != 1
        || statuses.front().id != "hoi3.leader_capture"
        || statuses.front().priority != 300
        || statuses.front().installed)
    {
        std::cerr << "Leader Capture hook registration mismatch\n";
        return 2;
    }

    lifecycle.Start();
    Dispatch(lifecycle, module);
    if (leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture activated before gameplay\n";
        return 3;
    }

    lifecycle.Observe(
        core::GamePhase::Gameplay,
        "CHI",
        core::LifecycleEventSource::NativeProbe
    );
    Dispatch(lifecycle, module);
    if (leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture bypassed closed save-load barrier\n";
        return 4;
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
    if (!leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture did not enter safe gameplay\n";
        return 5;
    }

    lifecycle.SetNativeWriteBarrier(
        false,
        2,
        "probe_load_started",
        core::LifecycleEventSource::NativeProbe
    );
    saveLoadBarrier.NotifyLoadStarted("probe_load");
    Dispatch(lifecycle, module);
    if (leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture remained active during save load\n";
        return 6;
    }

    lifecycle.SetNativeWriteBarrier(
        true,
        3,
        "probe_load_stable",
        core::LifecycleEventSource::NativeProbe
    );
    saveLoadBarrier.NotifyLoadCompleted("probe_load");
    saveLoadBarrier.Observe(safeSample);
    Dispatch(lifecycle, module);

    module.Tick(100);
    lifecycle.NotifySaveLoaded(
        "leader_capture_probe_save",
        core::LifecycleEventSource::External
    );
    Dispatch(lifecycle, module);
    if (!leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture did not resume after save load\n";
        return 7;
    }

    lifecycle.Observe(
        core::GamePhase::Frontend,
        "---",
        core::LifecycleEventSource::NativeProbe
    );
    Dispatch(lifecycle, module);
    if (leader_capture::IsGameplayActive())
    {
        std::cerr << "Leader Capture remained active in frontend\n";
        return 8;
    }

    module.Shutdown();
    std::cout << "Leader Capture core module probe: passed\n";
    return 0;
}
