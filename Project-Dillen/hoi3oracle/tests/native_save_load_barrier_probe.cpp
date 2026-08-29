#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "native_save_load_barrier.h"

namespace
{

core::NativeLifecycleSample Gameplay(
    int32_t days,
    uint64_t fingerprint = 100
)
{
    core::NativeLifecycleSample sample;
    sample.available = true;
    sample.gameplay = true;
    sample.playerTag = "CHI";
    sample.gameStateAddress = 0x1000;
    sample.worldFingerprint = fingerprint;
    sample.hasTotalDays = true;
    sample.totalDays = days;
    return sample;
}

core::NativeLifecycleSample Frontend()
{
    core::NativeLifecycleSample sample;
    sample.available = true;
    sample.gameplay = false;
    return sample;
}

bool Stabilize(
    core::NativeSaveLoadBarrier& barrier,
    int32_t days,
    bool expectSaveLoaded
)
{
    barrier.Observe(Gameplay(days));
    barrier.Observe(Gameplay(days));
    const core::NativeSaveLoadBarrierTransition transition =
        barrier.Observe(Gameplay(days));
    return transition.opened
        && transition.saveLoaded == expectSaveLoaded
        && transition.current.nativeWritesAllowed;
}

}

int main()
{
    core::NativeSaveLoadBarrier barrier(3);
    barrier.Start();
    if (!Stabilize(barrier, 100, false))
    {
        std::cerr << "Initial gameplay did not stabilize safely\n";
        return 1;
    }

    core::NativeLifecycleSample unavailable;
    const auto closed = barrier.Observe(unavailable);
    if (!closed.closed
        || closed.current.nativeWritesAllowed
        || !closed.current.loadSuspected)
    {
        std::cerr << "Unavailable native state did not close barrier\n";
        return 2;
    }
    if (!Stabilize(barrier, 90, true))
    {
        std::cerr << "Inferred save load did not reopen safely\n";
        return 3;
    }

    const auto rewound = barrier.Observe(Gameplay(80));
    if (!rewound.closed
        || rewound.current.reason
            != core::NativeSaveLoadBarrierReason::DateRewound)
    {
        std::cerr << "Date rewind did not close barrier\n";
        return 4;
    }
    barrier.Observe(Gameplay(80));
    const auto reopened = barrier.Observe(Gameplay(80));
    if (!reopened.opened || !reopened.saveLoaded)
    {
        std::cerr << "Date rewind did not publish save load\n";
        return 5;
    }

    const auto started = barrier.NotifyLoadStarted("explicit_save");
    if (!started.closed
        || !started.current.awaitingExplicitCompletion)
    {
        std::cerr << "Explicit load start did not close barrier\n";
        return 6;
    }
    barrier.Observe(Gameplay(80));
    if (barrier.Snapshot().state
        != core::NativeSaveLoadBarrierState::Closed)
    {
        std::cerr << "Barrier reopened before explicit completion\n";
        return 7;
    }
    barrier.NotifyLoadCompleted("explicit_save");
    const auto explicitFrontend = barrier.Observe(Frontend());
    if (explicitFrontend.current.reason
            != core::NativeSaveLoadBarrierReason::ExplicitLoadCompleted
        || !explicitFrontend.current.loadSuspected
        || explicitFrontend.current.pendingSaveKey != "explicit_save")
    {
        std::cerr << "Frontend transition discarded explicit load state\n";
        return 8;
    }
    barrier.Observe(Gameplay(80));
    barrier.Observe(Gameplay(80));
    const auto explicitReopened = barrier.Observe(Gameplay(80));
    if (!explicitReopened.opened
        || !explicitReopened.saveLoaded
        || explicitReopened.saveKey != "explicit_save")
    {
        std::cerr << "Explicit load completion was not stabilized\n";
        return 9;
    }

    auto writeLease = barrier.TryAcquireWriteLease();
    if (!writeLease)
    {
        std::cerr << "Open barrier did not issue a write lease\n";
        return 10;
    }
    std::atomic<bool> closeFinished{false};
    std::thread closer([&barrier, &closeFinished]
    {
        barrier.NotifyLoadStarted("lease_probe");
        closeFinished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    if (closeFinished.load(std::memory_order_acquire))
    {
        writeLease.reset();
        closer.join();
        std::cerr << "Load barrier crossed an active write lease\n";
        return 11;
    }
    writeLease.reset();
    closer.join();
    if (!closeFinished.load(std::memory_order_acquire)
        || barrier.Snapshot().nativeWritesAllowed)
    {
        std::cerr << "Barrier did not close after lease release\n";
        return 12;
    }

    const auto stopped = barrier.Stop();
    if (stopped.current.nativeWritesAllowed
        || stopped.current.runtimeActive)
    {
        std::cerr << "Barrier stop failed\n";
        return 13;
    }

    std::cout << "Native save-load barrier: passed\n";
    return 0;
}
