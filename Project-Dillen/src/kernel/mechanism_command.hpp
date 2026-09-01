#pragma once

#include <variant>

#include "algorithm_execution_policy.hpp"
#include "controlled_script.hpp"
#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismSetFieldOperation
{
    MechanismFieldSlotId field;
    MechanismValue value;
};

// Applies a delta to the committed value rather than replacing it.
//
// SetField carries an absolute value computed against the dispatch snapshot.
// When several invocations in one phase target the same field of the same
// instance -- which is exactly what Capability fan-in looks like, N senders
// reporting into one receiver -- they all read the same stale base, all
// compute base+1, and the last commit wins. Eight reports become one.
//
// A delta has no such base. The executor reads whatever is committed when the
// command is applied, so N deltas accumulate to N.
struct MechanismAddFieldOperation
{
    MechanismFieldSlotId field;
    MechanismValue delta;
};

struct MechanismTransitionLifecycleOperation
{
    MechanismLifecycleState target = MechanismLifecycleState::Created;
};

struct MechanismCompleteAlgorithmCreateOperation
{
};

struct MechanismRecordAlgorithmFaultOperation
{
    AlgorithmFaultCode code = AlgorithmFaultCode::ExecutionRejected;
    AlgorithmFaultStage stage = AlgorithmFaultStage::Tick;
};

struct MechanismClearAlgorithmFaultOperation
{
};

struct MechanismDestroyOperation
{
};

struct MechanismReplaceAlgorithmStateOperation
{
    std::vector<MechanismValue> state;
    std::vector<ControlledScriptContinuation> continuations;
};

// FROZEN ORDER (Demo 0.2). Each alternative's position is written straight out
// as the on-disk operation tag by the save codec, so reordering or inserting
// anywhere but the END is a save-format break. runtime_save_codec.cpp pins
// every position with a static_assert -- get it wrong and the build stops
// there, not at a corrupted save.
using MechanismCommandOperation = std::variant<
    MechanismSetFieldOperation,
    MechanismTransitionLifecycleOperation,
    MechanismCompleteAlgorithmCreateOperation,
    MechanismRecordAlgorithmFaultOperation,
    MechanismClearAlgorithmFaultOperation,
    MechanismDestroyOperation,
    MechanismReplaceAlgorithmStateOperation,
    // FROZEN ORDER -- appended 2026-09-01, never inserted. Purely additive
    // under FROZEN_CONTRACTS rule 0.1: old saves contain no tag 7.
    MechanismAddFieldOperation
>;

struct MechanismCommand
{
    MechanismInstanceId target;
    MechanismCommandOperation operation;

    static MechanismCommand AddField(
        MechanismInstanceId target,
        MechanismFieldSlotId field,
        MechanismValue delta
    );
    static MechanismCommand SetField(
        MechanismInstanceId target,
        MechanismFieldSlotId field,
        MechanismValue value
    );
    static MechanismCommand TransitionLifecycle(
        MechanismInstanceId target,
        MechanismLifecycleState state
    );
    static MechanismCommand CompleteAlgorithmCreate(
        MechanismInstanceId target
    );
    static MechanismCommand RecordAlgorithmFault(
        MechanismInstanceId target,
        AlgorithmFaultCode code,
        AlgorithmFaultStage stage
    );
    static MechanismCommand ClearAlgorithmFault(
        MechanismInstanceId target
    );
    static MechanismCommand Destroy(MechanismInstanceId target);
    static MechanismCommand ReplaceAlgorithmState(
        MechanismInstanceId target,
        std::vector<MechanismValue> state,
        std::vector<ControlledScriptContinuation> continuations
    );
};

}
