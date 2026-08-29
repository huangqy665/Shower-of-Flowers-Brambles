#include "native_save_load_barrier.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace core
{
namespace
{

bool SnapshotChanged(
    const NativeSaveLoadBarrierSnapshot& left,
    const NativeSaveLoadBarrierSnapshot& right
)
{
    return left.state != right.state
        || left.reason != right.reason
        || left.runtimeActive != right.runtimeActive
        || left.nativeWritesAllowed != right.nativeWritesAllowed
        || left.loadSuspected != right.loadSuspected
        || left.awaitingExplicitCompletion
            != right.awaitingExplicitCompletion
        || left.playerTag != right.playerTag
        || left.pendingSaveKey != right.pendingSaveKey
        || left.gameStateAddress != right.gameStateAddress
        || left.worldFingerprint != right.worldFingerprint;
}

}

NativeSaveLoadBarrier::NativeSaveLoadBarrier(
    uint32_t requiredStableSamples
)
    : requiredStableSamples_(std::max<uint32_t>(
          1,
          requiredStableSamples
      ))
{
}

NativeSaveLoadBarrierTransition NativeSaveLoadBarrier::Start()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const NativeSaveLoadBarrierSnapshot previous = snapshot_;
    snapshot_ = {};
    snapshot_.runtimeActive = true;
    snapshot_.state = NativeSaveLoadBarrierState::Closed;
    snapshot_.reason = NativeSaveLoadBarrierReason::RuntimeStart;
    snapshot_.generation = previous.generation + 1;
    hasSeenGameplay_ = false;
    completedLoadPending_ = false;
    return FinishTransitionUnlocked(previous);
}

NativeSaveLoadBarrierTransition NativeSaveLoadBarrier::Stop()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const NativeSaveLoadBarrierSnapshot previous = snapshot_;
    snapshot_.runtimeActive = false;
    CloseUnlocked(NativeSaveLoadBarrierReason::RuntimeStop, false);
    snapshot_.pendingSaveKey.clear();
    snapshot_.awaitingExplicitCompletion = false;
    completedLoadPending_ = false;
    hasSeenGameplay_ = false;
    return FinishTransitionUnlocked(previous);
}

NativeSaveLoadBarrierTransition NativeSaveLoadBarrier::Observe(
    const NativeLifecycleSample& sample
)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const NativeSaveLoadBarrierSnapshot previous = snapshot_;
    if (!snapshot_.runtimeActive)
    {
        return FinishTransitionUnlocked(previous);
    }

    if (!sample.available)
    {
        const bool suspect = hasSeenGameplay_
            || snapshot_.state != NativeSaveLoadBarrierState::Closed;
        CloseUnlocked(
            NativeSaveLoadBarrierReason::NativeUnavailable,
            suspect
        );
        return FinishTransitionUnlocked(previous);
    }
    if (!sample.gameplay)
    {
        const bool preserveExplicitLoad =
            snapshot_.awaitingExplicitCompletion
            || completedLoadPending_;
        CloseUnlocked(
            snapshot_.awaitingExplicitCompletion
                ? NativeSaveLoadBarrierReason::ExplicitLoadStarted
                : completedLoadPending_
                    ? NativeSaveLoadBarrierReason::ExplicitLoadCompleted
                    : NativeSaveLoadBarrierReason::Frontend,
            preserveExplicitLoad
        );
        if (!preserveExplicitLoad)
        {
            snapshot_.pendingSaveKey.clear();
            snapshot_.awaitingExplicitCompletion = false;
            completedLoadPending_ = false;
        }
        hasSeenGameplay_ = false;
        return FinishTransitionUnlocked(previous);
    }

    hasSeenGameplay_ = true;
    if (snapshot_.awaitingExplicitCompletion)
    {
        snapshot_.state = NativeSaveLoadBarrierState::Closed;
        snapshot_.nativeWritesAllowed = false;
        snapshot_.reason = NativeSaveLoadBarrierReason::ExplicitLoadStarted;
        snapshot_.stableSamples = 0;
        return FinishTransitionUnlocked(previous);
    }

    if (snapshot_.state == NativeSaveLoadBarrierState::Open)
    {
        NativeSaveLoadBarrierReason reason =
            NativeSaveLoadBarrierReason::StableGameplay;
        bool invalidated = false;
        if (sample.playerTag != snapshot_.playerTag)
        {
            reason = NativeSaveLoadBarrierReason::PlayerChanged;
            invalidated = true;
        }
        else if ((sample.gameStateAddress != 0
                    && snapshot_.gameStateAddress != 0
                    && sample.gameStateAddress
                        != snapshot_.gameStateAddress)
            || (sample.worldFingerprint != 0
                && snapshot_.worldFingerprint != 0
                && sample.worldFingerprint
                    != snapshot_.worldFingerprint))
        {
            reason = NativeSaveLoadBarrierReason::WorldChanged;
            invalidated = true;
        }
        else if (sample.hasTotalDays
            && snapshot_.hasTotalDays
            && sample.totalDays < snapshot_.totalDays)
        {
            reason = NativeSaveLoadBarrierReason::DateRewound;
            invalidated = true;
        }

        if (invalidated)
        {
            BeginStabilizingUnlocked(sample, reason, true);
            return FinishTransitionUnlocked(previous);
        }

        snapshot_.gameStateAddress = sample.gameStateAddress;
        snapshot_.worldFingerprint = sample.worldFingerprint;
        snapshot_.hasTotalDays = sample.hasTotalDays;
        snapshot_.totalDays = sample.totalDays;
        return FinishTransitionUnlocked(previous);
    }

    if (snapshot_.state != NativeSaveLoadBarrierState::Stabilizing
        || !MatchesCandidateUnlocked(sample))
    {
        const bool inferredLoad = snapshot_.loadSuspected
            || completedLoadPending_;
        BeginStabilizingUnlocked(
            sample,
            completedLoadPending_
                ? NativeSaveLoadBarrierReason::ExplicitLoadCompleted
                : NativeSaveLoadBarrierReason::InitialGameplay,
            inferredLoad
        );
    }
    else
    {
        ++snapshot_.stableSamples;
        snapshot_.hasTotalDays = sample.hasTotalDays;
        snapshot_.totalDays = sample.totalDays;
    }

    if (snapshot_.stableSamples < requiredStableSamples_)
    {
        return FinishTransitionUnlocked(previous);
    }

    const bool saveLoaded = snapshot_.loadSuspected
        || completedLoadPending_;
    snapshot_.state = NativeSaveLoadBarrierState::Open;
    snapshot_.reason = NativeSaveLoadBarrierReason::StableGameplay;
    snapshot_.nativeWritesAllowed = true;
    snapshot_.loadSuspected = false;
    snapshot_.awaitingExplicitCompletion = false;
    if (saveLoaded && snapshot_.pendingSaveKey.empty())
    {
        snapshot_.pendingSaveKey = MakeSaveKeyUnlocked();
    }
    completedLoadPending_ = false;
    NativeSaveLoadBarrierTransition transition =
        FinishTransitionUnlocked(previous, saveLoaded);
    transition.saveKey = snapshot_.pendingSaveKey;
    snapshot_.pendingSaveKey.clear();
    return transition;
}

NativeSaveLoadBarrierTransition
NativeSaveLoadBarrier::NotifyLoadStarted(std::string_view saveKey)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const NativeSaveLoadBarrierSnapshot previous = snapshot_;
    if (!snapshot_.runtimeActive)
    {
        return FinishTransitionUnlocked(previous);
    }
    CloseUnlocked(
        NativeSaveLoadBarrierReason::ExplicitLoadStarted,
        true
    );
    snapshot_.pendingSaveKey.assign(saveKey);
    snapshot_.awaitingExplicitCompletion = true;
    completedLoadPending_ = false;
    return FinishTransitionUnlocked(previous);
}

NativeSaveLoadBarrierTransition
NativeSaveLoadBarrier::NotifyLoadCompleted(std::string_view saveKey)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const NativeSaveLoadBarrierSnapshot previous = snapshot_;
    if (!snapshot_.runtimeActive)
    {
        return FinishTransitionUnlocked(previous);
    }
    CloseUnlocked(
        NativeSaveLoadBarrierReason::ExplicitLoadCompleted,
        true
    );
    if (!saveKey.empty())
    {
        snapshot_.pendingSaveKey.assign(saveKey);
    }
    snapshot_.awaitingExplicitCompletion = false;
    completedLoadPending_ = true;
    return FinishTransitionUnlocked(previous);
}

NativeSaveLoadBarrierSnapshot NativeSaveLoadBarrier::Snapshot() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return snapshot_;
}

std::optional<NativeSaveLoadWriteLease>
NativeSaveLoadBarrier::TryAcquireWriteLease() const
{
    return TryAcquireStableLease();
}

std::optional<NativeSaveLoadWriteLease>
NativeSaveLoadBarrier::TryAcquireStableLease() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!snapshot_.runtimeActive
        || !snapshot_.nativeWritesAllowed
        || snapshot_.state != NativeSaveLoadBarrierState::Open)
    {
        return std::nullopt;
    }
    return NativeSaveLoadWriteLease(std::move(lock));
}

NativeSaveLoadBarrierTransition
NativeSaveLoadBarrier::FinishTransitionUnlocked(
    const NativeSaveLoadBarrierSnapshot& previous,
    bool saveLoaded
)
{
    const bool changed = SnapshotChanged(previous, snapshot_);
    if (changed)
    {
        snapshot_.generation = std::max(
            snapshot_.generation,
            previous.generation + 1
        );
    }
    NativeSaveLoadBarrierTransition transition;
    transition.previous = previous;
    transition.current = snapshot_;
    transition.changed = changed;
    transition.opened = !previous.nativeWritesAllowed
        && snapshot_.nativeWritesAllowed;
    transition.closed = previous.nativeWritesAllowed
        && !snapshot_.nativeWritesAllowed;
    transition.saveLoaded = saveLoaded;
    return transition;
}

void NativeSaveLoadBarrier::CloseUnlocked(
    NativeSaveLoadBarrierReason reason,
    bool loadSuspected
)
{
    snapshot_.state = NativeSaveLoadBarrierState::Closed;
    snapshot_.reason = reason;
    snapshot_.nativeWritesAllowed = false;
    snapshot_.loadSuspected = loadSuspected;
    snapshot_.stableSamples = 0;
    snapshot_.playerTag.clear();
    snapshot_.gameStateAddress = 0;
    snapshot_.worldFingerprint = 0;
    snapshot_.hasTotalDays = false;
    snapshot_.totalDays = 0;
}

void NativeSaveLoadBarrier::BeginStabilizingUnlocked(
    const NativeLifecycleSample& sample,
    NativeSaveLoadBarrierReason reason,
    bool loadSuspected
)
{
    snapshot_.state = NativeSaveLoadBarrierState::Stabilizing;
    snapshot_.reason = reason;
    snapshot_.nativeWritesAllowed = false;
    snapshot_.loadSuspected = loadSuspected;
    snapshot_.stableSamples = 1;
    snapshot_.playerTag = sample.playerTag;
    snapshot_.gameStateAddress = sample.gameStateAddress;
    snapshot_.worldFingerprint = sample.worldFingerprint;
    snapshot_.hasTotalDays = sample.hasTotalDays;
    snapshot_.totalDays = sample.totalDays;
}

bool NativeSaveLoadBarrier::MatchesCandidateUnlocked(
    const NativeLifecycleSample& sample
) const
{
    if (sample.playerTag != snapshot_.playerTag)
    {
        return false;
    }
    if (sample.gameStateAddress != 0
        && snapshot_.gameStateAddress != 0
        && sample.gameStateAddress != snapshot_.gameStateAddress)
    {
        return false;
    }
    if (sample.worldFingerprint != 0
        && snapshot_.worldFingerprint != 0
        && sample.worldFingerprint != snapshot_.worldFingerprint)
    {
        return false;
    }
    return !(sample.hasTotalDays
        && snapshot_.hasTotalDays
        && sample.totalDays < snapshot_.totalDays);
}

std::string NativeSaveLoadBarrier::MakeSaveKeyUnlocked() const
{
    std::string key = "native:"
        + std::to_string(snapshot_.generation + 1)
        + ":" + snapshot_.playerTag;
    if (snapshot_.hasTotalDays)
    {
        key += ":" + std::to_string(snapshot_.totalDays);
    }
    return key;
}

const char* NativeSaveLoadBarrierStateName(
    NativeSaveLoadBarrierState value
)
{
    switch (value)
    {
    case NativeSaveLoadBarrierState::Closed:
        return "closed";
    case NativeSaveLoadBarrierState::Stabilizing:
        return "stabilizing";
    case NativeSaveLoadBarrierState::Open:
        return "open";
    default:
        return "unknown";
    }
}

const char* NativeSaveLoadBarrierReasonName(
    NativeSaveLoadBarrierReason value
)
{
    switch (value)
    {
    case NativeSaveLoadBarrierReason::RuntimeStart:
        return "runtime_start";
    case NativeSaveLoadBarrierReason::RuntimeStop:
        return "runtime_stop";
    case NativeSaveLoadBarrierReason::Frontend:
        return "frontend";
    case NativeSaveLoadBarrierReason::NativeUnavailable:
        return "native_unavailable";
    case NativeSaveLoadBarrierReason::InitialGameplay:
        return "initial_gameplay";
    case NativeSaveLoadBarrierReason::PlayerChanged:
        return "player_changed";
    case NativeSaveLoadBarrierReason::WorldChanged:
        return "world_changed";
    case NativeSaveLoadBarrierReason::DateRewound:
        return "date_rewound";
    case NativeSaveLoadBarrierReason::ExplicitLoadStarted:
        return "explicit_load_started";
    case NativeSaveLoadBarrierReason::ExplicitLoadCompleted:
        return "explicit_load_completed";
    case NativeSaveLoadBarrierReason::StableGameplay:
        return "stable_gameplay";
    default:
        return "unknown";
    }
}

}
