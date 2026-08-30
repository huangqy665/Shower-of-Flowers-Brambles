#pragma once

#include <string>
#include <vector>

#include "algorithm_program.hpp"
#include "mechanism_instance.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"
#include "world_transaction.hpp"

namespace dillen::runtime {

struct AlgorithmInvocationContext;

// Outcome of lowering one compiled bytecode instruction to World commands.
enum class BytecodeTransactionStatus
{
    Ok,
    InvalidFieldSlot,
    OperandTypeMismatch,
    NumericOverflow,
    LifecycleTransitionRejected
};

struct BytecodeTransactionOutcome
{
    BytecodeTransactionStatus status = BytecodeTransactionStatus::Ok;
    std::string message;
    std::vector<kernel::WorldCommand> commands;

    explicit operator bool() const noexcept
    {
        return status == BytecodeTransactionStatus::Ok;
    }
};

// True when every compiled condition on `instruction` holds against the given
// invocation context and the mechanism's current field values.
bool EvaluateBytecodeConditions(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const std::vector<kernel::MechanismValue>& values
);

// Lowers one non-control bytecode instruction (field mutation, entity /
// component / relation / spawn / scheduled event / RNG / capability) to the
// World commands it produces. Updates `values` for read-modify-write field
// math and `lifecycle` for the lifecycle gate. Conditions are NOT checked here
// -- call EvaluateBytecodeConditions first. Shared verbatim by the declarative
// VM and the controlled-script VM so the two backends cannot drift.
BytecodeTransactionOutcome EmitBytecodeTransaction(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const kernel::MechanismInstance& instance,
    std::vector<kernel::MechanismValue>& values,
    kernel::MechanismLifecycleState& lifecycle
);

}
