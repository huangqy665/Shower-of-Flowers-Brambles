#include "read_path.hpp"

#include <algorithm>
#include <limits>
#include <string>

#include "algorithm_runtime.hpp"

namespace dillen::runtime {

namespace {

using kernel::MechanismValueKind;

ReadPathResult Reject(ReadPathStatus status, std::string message)
{
    ReadPathResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

ReadPathResult FromFixedPoint(
    const kernel::FixedPointValue& value,
    MechanismValueKind kind,
    std::size_t visited
)
{
    if (!value)
    {
        switch (value.status)
        {
        case kernel::FixedPointStatus::DivideByZero:
            return Reject(
                ReadPathStatus::ArithmeticRejected,
                "division by zero"
            );
        case kernel::FixedPointStatus::NotFinite:
            return Reject(
                ReadPathStatus::ArithmeticRejected,
                "value is not finite"
            );
        default:
            return Reject(
                ReadPathStatus::ArithmeticRejected,
                "fixed-point overflow"
            );
        }
    }
    ReadPathResult result;
    result.scaled = value.scaled;
    result.kind = kind;
    result.visited = visited;
    return result;
}

// One MechanismValue -> the shared numeric pipeline. Non-numeric kinds have no
// image here, and saying so is better than coercing them to zero.
bool ToScaled(
    const kernel::MechanismValue& value,
    std::int64_t& scaled,
    MechanismValueKind& kind
)
{
    if (const auto* integer = std::get_if<std::int64_t>(&value.data))
    {
        const kernel::FixedPointValue converted =
            kernel::IntegerToInternal(*integer);
        if (!converted) return false;
        scaled = converted.scaled;
        kind = MechanismValueKind::Integer;
        return true;
    }
    if (const auto* decimal = std::get_if<double>(&value.data))
    {
        const kernel::FixedPointValue converted =
            kernel::DecimalToInternal(*decimal);
        if (!converted) return false;
        scaled = converted.scaled;
        kind = MechanismValueKind::Decimal;
        return true;
    }
    return false;
}

// Widening rule: any decimal in the expression makes the result a decimal.
// Integers are exact at the internal scale, so this never loses information --
// it only decides how the result is written back.
MechanismValueKind WidenKind(MechanismValueKind first, MechanismValueKind second)
{
    return first == MechanismValueKind::Decimal
            || second == MechanismValueKind::Decimal
        ? MechanismValueKind::Decimal
        : MechanismValueKind::Integer;
}

// Collects every value a path reaches, before reduction.
struct Collected
{
    std::vector<std::int64_t> scaled;
    MechanismValueKind kind = MechanismValueKind::Integer;
    bool sawDecimal = false;
    bool failed = false;
    std::string message;
};

void Take(Collected& out, const kernel::MechanismValue& value)
{
    std::int64_t scaled = 0;
    MechanismValueKind kind = MechanismValueKind::Integer;
    if (!ToScaled(value, scaled, kind))
    {
        out.failed = true;
        out.message = "read path reached a value that is not numeric";
        return;
    }
    out.sawDecimal = out.sawDecimal || kind == MechanismValueKind::Decimal;
    out.scaled.push_back(scaled);
}

// Reads the terminal off one reference, which is either a Mechanism Instance
// field or an Entity's Component field. Which one is fixed at compile time by
// the path's terminal, so a role bound to the wrong kind of thing is a Fault
// rather than a silent miss.
void TakeFromReference(
    Collected& out,
    const kernel::CompiledAlgorithmReadPath& path,
    const AlgorithmInvocationContext& context,
    const kernel::MechanismReference& reference
)
{
    if (path.terminal == kernel::AlgorithmReadTerminal::MechanismField)
    {
        if (reference.kind != kernel::MechanismReferenceKind::MechanismInstance)
        {
            out.failed = true;
            out.message = "read path expected a Mechanism Instance reference";
            return;
        }
        const kernel::MechanismValue* value = context.snapshot.FindField(
            kernel::MechanismInstanceId{reference.value},
            path.targetField
        );
        if (value == nullptr)
        {
            out.failed = true;
            out.message = "read path target Mechanism field is missing";
            return;
        }
        Take(out, *value);
        return;
    }
    if (reference.kind != kernel::MechanismReferenceKind::Entity)
    {
        out.failed = true;
        out.message = "read path expected an Entity reference";
        return;
    }
    const kernel::EntityId entity{reference.value};
    if (!path.traverseRelation)
    {
        const kernel::MechanismValue* value =
            context.query.Components().FindField(
                entity,
                path.component,
                path.componentField
            );
        if (value == nullptr)
        {
            out.failed = true;
            out.message = "read path target Component field is missing";
            return;
        }
        Take(out, *value);
        return;
    }
    // Relation hop. Both index lookups return ids in ascending order, so the
    // fan-out is enumerated deterministically.
    const std::vector<kernel::RelationId>& edges =
        path.direction == kernel::AlgorithmRelationDirection::Outgoing
            ? context.query.Relations().Outgoing(path.relationType, entity)
            : context.query.Relations().Incoming(path.relationType, entity);
    for (const kernel::RelationId edge : edges)
    {
        const world::RelationRecord* record =
            context.query.Relations().Find(edge);
        if (record == nullptr)
        {
            continue;
        }
        const kernel::EntityId other =
            path.direction == kernel::AlgorithmRelationDirection::Outgoing
                ? record->target
                : record->source;
        const kernel::MechanismValue* value =
            context.query.Components().FindField(
                other,
                path.component,
                path.componentField
            );
        if (value == nullptr)
        {
            out.failed = true;
            out.message =
                "read path target Component field is missing across a Relation";
            return;
        }
        Take(out, *value);
        if (out.failed) return;
    }
}

}

ReadPathResult EvaluateSubjectReadPath(
    const kernel::CompiledAlgorithmReadPath& path,
    const WorldQuerySnapshot& query,
    kernel::EntityId subject,
    AlgorithmExecutionBudget& budget
)
{
    // A second entry, not a second evaluator.
    //
    // AlgorithmInvocationContext is built for an algorithm: it carries an
    // instance, a mechanism snapshot, an rng and a capability list, because an
    // algorithm has all of those. A projection has none, and a subject-rooted
    // path reaching a Component field touches none either -- only the world
    // query and the budget. The empties below exist to satisfy the references,
    // and a path that reached for one of them would have to be rooted
    // somewhere a projection cannot root, which the compiler refuses.
    static const kernel::MechanismInstance kNoInstance{};
    static const kernel::MechanismQuerySnapshot kNoMechanisms{};
    static const kernel::DeterministicRngSnapshot kNoRng{};
    static const std::vector<kernel::CapabilityBindingSlotId> kNoCapabilities;
    static const kernel::FrozenRuntimeCatalog kNoCatalog{};

    const AlgorithmInvocationContext context{
        AlgorithmRuntimeStage::Tick,
        0,
        kNoInstance,
        query,
        kNoMechanisms,
        kNoRng,
        kNoCatalog,
        kNoCapabilities,
        nullptr,
        nullptr,
        nullptr,
        budget,
        &subject
    };
    static const std::vector<kernel::MechanismValue> kNoSelfValues;
    return EvaluateReadPath(path, context, kNoSelfValues);
}

ReadPathResult EvaluateReadPath(
    const kernel::CompiledAlgorithmReadPath& path,
    const AlgorithmInvocationContext& context,
    const std::vector<kernel::MechanismValue>& selfValues
)
{
    // One aggregation is one instruction in the program but N units of work,
    // and the instruction budget is supposed to bound what a Tick can do. If a
    // reduce over a thousand provinces cost the same as `add 1`, the budget
    // would stop being an upper bound on anything -- one instruction could
    // walk the whole world. So the visited count is charged, not just
    // reported.
    //
    // Charged after the walk rather than before it, because the size of the
    // fan-out is not known until the path has been followed; the walk itself
    // is bounded by the world, and it is the *repeat* cost across instructions
    // that the budget has to contain.
    Collected collected;
    switch (path.root)
    {
    case kernel::AlgorithmReadRoot::Constant:
        Take(collected, path.constant);
        break;
    case kernel::AlgorithmReadRoot::EventPayload:
        if (context.scheduledEvent == nullptr)
        {
            return Reject(
                ReadPathStatus::TargetMissing,
                "event_payload requires an active scheduled invocation"
            );
        }
        Take(collected, context.scheduledEvent->payload);
        break;
    case kernel::AlgorithmReadRoot::SelfField:
        if (path.selfField.value >= selfValues.size())
        {
            return Reject(
                ReadPathStatus::TargetMissing,
                "read path names a field slot this Mechanism does not have"
            );
        }
        Take(collected, selfValues[path.selfField.value]);
        break;
    case kernel::AlgorithmReadRoot::RoleTarget:
    {
        const std::vector<kernel::MechanismReference>* role =
            context.snapshot.FindRole(context.instance.id, path.role);
        if (role == nullptr)
        {
            return Reject(
                ReadPathStatus::TargetMissing,
                "read path names a role slot this Mechanism does not have"
            );
        }
        for (const kernel::MechanismReference& reference : *role)
        {
            TakeFromReference(collected, path, context, reference);
            if (collected.failed) break;
        }
        break;
    }
    case kernel::AlgorithmReadRoot::SubjectEntity:
    {
        if (context.subject == nullptr)
        {
            return Reject(
                ReadPathStatus::TargetMissing,
                "read path starts at a subject Entity and none was given"
            );
        }
        // Handed to the SAME traversal every other root uses. The relation
        // hop, the terminals and the reduce are not reimplemented here; the
        // root only decides where the walk begins.
        kernel::MechanismReference reference;
        reference.kind = kernel::MechanismReferenceKind::Entity;
        reference.value = context.subject->value;
        TakeFromReference(collected, path, context, reference);
        break;
    }
    }

    if (collected.failed)
    {
        return Reject(ReadPathStatus::KindMismatch, collected.message);
    }

    const std::size_t visited = collected.scaled.size();
    if (visited > 1)
    {
        // The first value is already paid for by the instruction itself; only
        // the fan-out beyond it is extra.
        const std::uint64_t extra = static_cast<std::uint64_t>(visited - 1);
        const std::uint32_t charge = extra
                > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(extra);
        if (!context.budget.Consume(charge))
        {
            return Reject(
                ReadPathStatus::BudgetExceeded,
                "read path fan-out of " + std::to_string(visited)
                    + " exceeded the instruction budget"
            );
        }
    }
    const MechanismValueKind kind = collected.sawDecimal
        ? MechanismValueKind::Decimal
        : MechanismValueKind::Integer;

    switch (path.reduce)
    {
    case kernel::AlgorithmReduce::Count:
    {
        // Count is about how many values were reached, so it is an integer
        // whatever the values themselves were.
        ReadPathResult result = FromFixedPoint(
            kernel::IntegerToInternal(static_cast<std::int64_t>(visited)),
            MechanismValueKind::Integer,
            visited
        );
        return result;
    }
    case kernel::AlgorithmReduce::RequireOne:
        if (visited != 1)
        {
            return Reject(
                ReadPathStatus::NotExactlyOne,
                "read path reached " + std::to_string(visited)
                    + " values where exactly one is required; name a reducer "
                      "if more than one is intended"
            );
        }
        {
            ReadPathResult result;
            result.scaled = collected.scaled.front();
            result.kind = kind;
            result.visited = visited;
            return result;
        }
    case kernel::AlgorithmReduce::Sum:
    {
        std::int64_t total = 0;
        for (const std::int64_t term : collected.scaled)
        {
            const kernel::FixedPointValue next =
                kernel::FixedAdd(total, term);
            if (!next)
            {
                return FromFixedPoint(next, kind, visited);
            }
            total = next.scaled;
        }
        ReadPathResult result;
        result.scaled = total;
        result.kind = kind;
        result.visited = visited;
        return result;
    }
    case kernel::AlgorithmReduce::Minimum:
    case kernel::AlgorithmReduce::Maximum:
    {
        if (visited == 0)
        {
            return Reject(
                ReadPathStatus::NotExactlyOne,
                "min/max over an empty set has no value"
            );
        }
        const bool wantMinimum =
            path.reduce == kernel::AlgorithmReduce::Minimum;
        std::int64_t best = collected.scaled.front();
        for (const std::int64_t candidate : collected.scaled)
        {
            best = wantMinimum
                ? std::min(best, candidate)
                : std::max(best, candidate);
        }
        ReadPathResult result;
        result.scaled = best;
        result.kind = kind;
        result.visited = visited;
        return result;
    }
    }
    return Reject(ReadPathStatus::TargetMissing, "unknown reducer");
}

ReadPathResult ApplyBinaryOperator(
    kernel::AlgorithmBinaryOperator op,
    const ReadPathResult& left,
    const ReadPathResult& right
)
{
    const MechanismValueKind kind = WidenKind(left.kind, right.kind);
    const std::size_t visited = left.visited + right.visited;
    switch (op)
    {
    case kernel::AlgorithmBinaryOperator::Add:
        return FromFixedPoint(
            kernel::FixedAdd(left.scaled, right.scaled), kind, visited);
    case kernel::AlgorithmBinaryOperator::Subtract:
        return FromFixedPoint(
            kernel::FixedSubtract(left.scaled, right.scaled), kind, visited);
    case kernel::AlgorithmBinaryOperator::Multiply:
        return FromFixedPoint(
            kernel::FixedMultiply(left.scaled, right.scaled), kind, visited);
    case kernel::AlgorithmBinaryOperator::Divide:
        return FromFixedPoint(
            kernel::FixedDivide(left.scaled, right.scaled), kind, visited);
    case kernel::AlgorithmBinaryOperator::Minimum:
    case kernel::AlgorithmBinaryOperator::Maximum:
    {
        ReadPathResult result;
        result.scaled = op == kernel::AlgorithmBinaryOperator::Minimum
            ? std::min(left.scaled, right.scaled)
            : std::max(left.scaled, right.scaled);
        result.kind = kind;
        result.visited = visited;
        return result;
    }
    }
    return Reject(ReadPathStatus::KindMismatch, "unknown binary operator");
}

bool CompareReads(
    kernel::AlgorithmCompareOperator op,
    const ReadPathResult& left,
    const ReadPathResult& right
)
{
    // Both sides are already at the same internal scale, so an ordered
    // comparison is an ordinary integer comparison -- no epsilon, no
    // representation surprises.
    switch (op)
    {
    case kernel::AlgorithmCompareOperator::Equal:
        return left.scaled == right.scaled;
    case kernel::AlgorithmCompareOperator::NotEqual:
        return left.scaled != right.scaled;
    case kernel::AlgorithmCompareOperator::Less:
        return left.scaled < right.scaled;
    case kernel::AlgorithmCompareOperator::LessOrEqual:
        return left.scaled <= right.scaled;
    case kernel::AlgorithmCompareOperator::Greater:
        return left.scaled > right.scaled;
    case kernel::AlgorithmCompareOperator::GreaterOrEqual:
        return left.scaled >= right.scaled;
    }
    return false;
}

}
