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

using MechanismCommandOperation = std::variant<
    MechanismSetFieldOperation,
    MechanismTransitionLifecycleOperation,
    MechanismCompleteAlgorithmCreateOperation,
    MechanismRecordAlgorithmFaultOperation,
    MechanismClearAlgorithmFaultOperation,
    MechanismDestroyOperation,
    MechanismReplaceAlgorithmStateOperation
>;

struct MechanismCommand
{
    MechanismInstanceId target;
    MechanismCommandOperation operation;

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
