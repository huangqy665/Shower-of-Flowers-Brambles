#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "algorithm_program.hpp"
#include "fixed_point.hpp"
#include "mechanism_value.hpp"

namespace dillen::runtime {

struct AlgorithmInvocationContext;
class AlgorithmExecutionBudget;
class WorldQuerySnapshot;

enum class ReadPathStatus
{
    Ok,
    // RequireOne saw zero or more than one value.
    NotExactlyOne,
    // A role slot, relation type, component or field slot the path names is
    // not present on the object it arrived at.
    TargetMissing,
    // Values of different kinds reached a reducer or an operator that cannot
    // combine them.
    KindMismatch,
    // Fixed-point rejection: overflow, division by zero, non-finite input.
    ArithmeticRejected,
    // The fan-out cost more instruction budget than the invocation had left.
    BudgetExceeded
};

// A read result carries its value at the internal fixed-point scale, plus the
// kind it should be written back as. Integers and decimals share one numeric
// pipeline (see kernel/fixed_point.hpp) so a mixed expression has one set of
// rules; `kind` decides only how the result lands in a field.
struct ReadPathResult
{
    ReadPathStatus status = ReadPathStatus::Ok;
    std::string message;
    std::int64_t scaled = 0;
    kernel::MechanismValueKind kind = kernel::MechanismValueKind::Integer;
    // Number of values the path reached before reducing. This is what the
    // instruction budget is charged, so a reduce over a thousand provinces
    // costs a thousand units rather than one -- the budget stays a real bound
    // on the work a Tick can do.
    std::size_t visited = 0;

    explicit operator bool() const noexcept
    {
        return status == ReadPathStatus::Ok;
    }
};

// Evaluates one compiled read path against the immutable snapshots in
// `context`. Reads only; it can never mutate the world.
//
// Determinism note: where a path fans out over a role slot or a relation, the
// traversal order is the stores' secondary index order, which is ascending id
// by construction (kernel/sorted_id_index.hpp). Because the arithmetic is
// fixed point rather than floating point, a Sum is additionally independent of
// that order -- integer addition is associative where float addition is not.
ReadPathResult EvaluateReadPath(
    const kernel::CompiledAlgorithmReadPath& path,
    const AlgorithmInvocationContext& context,
    const std::vector<kernel::MechanismValue>& selfValues
);

// The same evaluation, for a caller that is not an algorithm.
//
// A projection asks one read path of each of many Entities and has no
// instance, no mechanism snapshot and no rng to offer. This assembles the
// context those callers cannot and calls the evaluator above -- one
// implementation of what a read path means, reachable from both sides.
//
// Only AlgorithmReadRoot::SubjectEntity is usable through it; any other root
// reads from an instance that is not there and is rejected rather than read
// as a default.
ReadPathResult EvaluateSubjectReadPath(
    const kernel::CompiledAlgorithmReadPath& path,
    const WorldQuerySnapshot& query,
    kernel::EntityId subject,
    AlgorithmExecutionBudget& budget
);

// Applies a binary operator to two already-evaluated reads.
ReadPathResult ApplyBinaryOperator(
    kernel::AlgorithmBinaryOperator op,
    const ReadPathResult& left,
    const ReadPathResult& right
);

// True/false for a comparison of two already-evaluated reads.
bool CompareReads(
    kernel::AlgorithmCompareOperator op,
    const ReadPathResult& left,
    const ReadPathResult& right
);

}
