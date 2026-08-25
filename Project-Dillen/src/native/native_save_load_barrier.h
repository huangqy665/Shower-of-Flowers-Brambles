#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace core
{

class NativeSaveLoadWriteLease
{
public:
    NativeSaveLoadWriteLease(NativeSaveLoadWriteLease&&) noexcept = default;
    NativeSaveLoadWriteLease& operator=(
        NativeSaveLoadWriteLease&&
    ) noexcept = default;

    NativeSaveLoadWriteLease(const NativeSaveLoadWriteLease&) = delete;
    NativeSaveLoadWriteLease& operator=(
        const NativeSaveLoadWriteLease&
    ) = delete;

private:
    friend class NativeSaveLoadBarrier;
    explicit NativeSaveLoadWriteLease(
        std::shared_lock<std::shared_mutex> lock
    ) : lock_(std::move(lock)) {}

    std::shared_lock<std::shared_mutex> lock_;
};

enum class NativeSaveLoadBarrierState
{
    Closed,
    Stabilizing,
    Open
};

enum class NativeSaveLoadBarrierReason
{
    RuntimeStart,
    RuntimeStop,
    Frontend,
    NativeUnavailable,
    InitialGameplay,
    PlayerChanged,
    WorldChanged,
    DateRewound,
    ExplicitLoadStarted,
    ExplicitLoadCompleted,
    StableGameplay
};

struct NativeLifecycleSample
{
    bool available = false;
    bool gameplay = false;
    std::string playerTag;
    std::uintptr_t gameStateAddress = 0;
    uint64_t worldFingerprint = 0;
    bool hasTotalDays = false;
    int32_t totalDays = 0;
    uint64_t observedAtMilliseconds = 0;
};

struct NativeSaveLoadBarrierSnapshot
{
    NativeSaveLoadBarrierState state =
        NativeSaveLoadBarrierState::Closed;
    NativeSaveLoadBarrierReason reason =
        NativeSaveLoadBarrierReason::RuntimeStart;
    bool runtimeActive = false;
    bool nativeWritesAllowed = false;
    bool loadSuspected = false;
    bool awaitingExplicitCompletion = false;
    uint32_t stableSamples = 0;
    uint64_t generation = 0;
    std::string playerTag;
    std::string pendingSaveKey;
    std::uintptr_t gameStateAddress = 0;
    uint64_t worldFingerprint = 0;
    bool hasTotalDays = false;
    int32_t totalDays = 0;
};

struct NativeSaveLoadBarrierTransition
{
    NativeSaveLoadBarrierSnapshot previous;
    NativeSaveLoadBarrierSnapshot current;
    bool changed = false;
    bool opened = false;
    bool closed = false;
    bool saveLoaded = false;
    std::string saveKey;
};

class NativeSaveLoadBarrier
{
public:
    explicit NativeSaveLoadBarrier(uint32_t requiredStableSamples = 3);

    NativeSaveLoadBarrierTransition Start();
    NativeSaveLoadBarrierTransition Stop();
    NativeSaveLoadBarrierTransition Observe(
        const NativeLifecycleSample& sample
    );
    NativeSaveLoadBarrierTransition NotifyLoadStarted(
        std::string_view saveKey
    );
    NativeSaveLoadBarrierTransition NotifyLoadCompleted(
        std::string_view saveKey
    );

    NativeSaveLoadBarrierSnapshot Snapshot() const;
    std::optional<NativeSaveLoadWriteLease>
    TryAcquireWriteLease() const;
    std::optional<NativeSaveLoadWriteLease>
    TryAcquireStableLease() const;

private:
    NativeSaveLoadBarrierTransition FinishTransitionUnlocked(
        const NativeSaveLoadBarrierSnapshot& previous,
        bool saveLoaded = false
    );
    void CloseUnlocked(
        NativeSaveLoadBarrierReason reason,
        bool loadSuspected
    );
    void BeginStabilizingUnlocked(
        const NativeLifecycleSample& sample,
        NativeSaveLoadBarrierReason reason,
        bool loadSuspected
    );
    bool MatchesCandidateUnlocked(
        const NativeLifecycleSample& sample
    ) const;
    std::string MakeSaveKeyUnlocked() const;

    mutable std::shared_mutex mutex_;
    NativeSaveLoadBarrierSnapshot snapshot_;
    uint32_t requiredStableSamples_ = 3;
    bool hasSeenGameplay_ = false;
    bool completedLoadPending_ = false;
};

const char* NativeSaveLoadBarrierStateName(
    NativeSaveLoadBarrierState value
);
const char* NativeSaveLoadBarrierReasonName(
    NativeSaveLoadBarrierReason value
);

}
