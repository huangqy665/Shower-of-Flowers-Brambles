#include "bytecode_transaction.hpp"

#include "read_path.hpp"

#include "algorithm_runtime.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace dillen::runtime {

namespace {

BytecodeTransactionOutcome Reject(
    BytecodeTransactionStatus status,
    std::string message
)
{
    BytecodeTransactionOutcome outcome;
    outcome.status = status;
    outcome.message = std::move(message);
    return outcome;
}

bool AddWithoutOverflow(
    std::int64_t current,
    std::int64_t operand,
    std::int64_t& output
) noexcept
{
    if ((operand > 0
            && current > std::numeric_limits<std::int64_t>::max() - operand)
        || (operand < 0
            && current < std::numeric_limits<std::int64_t>::min() - operand))
    {
        return false;
    }
    output = current + operand;
    return true;
}

}

bool EvaluateBytecodeConditions(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const std::vector<kernel::MechanismValue>& values
)
{
    using namespace kernel;
    for (const CompiledAlgorithmCondition& condition
        : instruction.conditions)
    {
        switch (condition.kind)
        {
        case AlgorithmConditionKind::SelfFieldEquals:
            if (condition.field.value >= values.size()
                || values[condition.field.value] != condition.value)
            {
                return false;
            }
            break;
        case AlgorithmConditionKind::QueryCountAtLeast:
        {
            std::size_t count = 0;
            switch (condition.queryKind)
            {
            case AlgorithmQueryKind::EntityType:
                count = context.query.Entities().FindByType(
                    EntityTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::ComponentType:
                count = context.query.Components().FindOwners(
                    ComponentTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::RelationType:
                count = context.query.Relations().FindByType(
                    RelationTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::MechanismType:
                count = context.snapshot.FindByType(
                    MechanismTypeId{condition.queryType}
                ).size();
                break;
            }
            if (count < condition.minimumCount)
            {
                return false;
            }
            break;
        }
        case AlgorithmConditionKind::ScheduledEventTypeEquals:
            if (context.scheduledEvent == nullptr
                || context.scheduledEvent->type != condition.eventType)
            {
                return false;
            }
            break;
        case AlgorithmConditionKind::Compare:
        {
            // Both sides are read paths, so this is the general form that
            // SelfFieldEquals was the one hard-coded case of.
            const ReadPathResult left =
                EvaluateReadPath(condition.left, context, values);
            const ReadPathResult right =
                EvaluateReadPath(condition.right, context, values);
            if (!left || !right)
            {
                // A condition that cannot be evaluated is false, not an
                // exception: the instruction it guards simply does not fire.
                // The alternative -- faulting the instance -- would make a
                // temporarily unbound role a fatal error.
                return false;
            }
            if (!CompareReads(condition.compare, left, right))
            {
                return false;
            }
            break;
        }
        case AlgorithmConditionKind::RngModuloEquals:
            if (context.rng.Find(condition.rngStream) == nullptr
                || context.rng.Preview(
                    condition.rngStream,
                    condition.rngOffset) % condition.rngModulo
                    != condition.rngEquals)
            {
                return false;
            }
            break;
        }
    }
    return true;
}

BytecodeTransactionOutcome EmitBytecodeTransaction(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const kernel::MechanismInstance& instance,
    std::vector<kernel::MechanismValue>& values,
    kernel::MechanismLifecycleState& lifecycle
)
{
    using namespace kernel;
    BytecodeTransactionOutcome outcome;
    std::vector<WorldCommand>& resultCommands = outcome.commands;

    if (instruction.opcode == AlgorithmBytecodeOpcode::TransitionLifecycle)
    {
        if (!CanTransitionMechanismLifecycle(
                lifecycle,
                instruction.lifecycle))
        {
            return Reject(
                BytecodeTransactionStatus::LifecycleTransitionRejected,
                "lifecycle transition is not permitted"
            );
        }
        lifecycle = instruction.lifecycle;
        resultCommands.push_back(WorldCommand::Mechanism(
            MechanismCommand::TransitionLifecycle(instance.id, lifecycle)
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CreateEntity)
    {
        resultCommands.push_back(WorldCommand::CreateEntity(
            instruction.entityDefinition
        ));
        return outcome;
    }
    if (instruction.opcode
        == AlgorithmBytecodeOpcode::SetComponentFieldConstant)
    {
        resultCommands.push_back(WorldCommand::SetComponentField(
            instruction.entity,
            instruction.component,
            instruction.componentField,
            instruction.operand
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::AddRelation)
    {
        resultCommands.push_back(WorldCommand::AddRelation(
            instruction.relationType,
            instruction.sourceEntity,
            instruction.targetEntity
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::RemoveRelation)
    {
        resultCommands.push_back(WorldCommand::RemoveRelation(
            instruction.relation
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::SpawnMechanism)
    {
        resultCommands.push_back(WorldCommand::SpawnMechanism(
            instruction.spawn
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::ScheduleEvent)
    {
        resultCommands.push_back(WorldCommand::ScheduleEvent(
            instruction.eventType,
            instance.id,
            context.tick + instruction.dueTickOffset,
            instruction.priority,
            instruction.payload
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CancelEventsByType)
    {
        // Expands into one CancelEvent per match. The Inbox is ordered by
        // (dueTick, priority, sequence), so the commands come out in a
        // deterministic order without sorting anything here.
        //
        // Only this instance's own events are cancellable: an algorithm that
        // could cancel another mechanism's scheduled work would be reaching
        // past every contract the Capability layer exists to provide.
        std::uint32_t matched = 0;
        for (const ScheduledAlgorithmEvent& pending
            : context.query.ScheduledEvents().Pending())
        {
            if (pending.target != instance.id
                || pending.type != instruction.eventType)
            {
                continue;
            }
            resultCommands.push_back(
                WorldCommand::CancelEvent(pending.sequence)
            );
            ++matched;
        }
        // Charged like an aggregation: one instruction, N units of work. The
        // first match is covered by the instruction itself.
        if (matched > 1 && !context.budget.Consume(matched - 1))
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "cancel_events matched " + std::to_string(matched)
                    + " events, exceeding the instruction budget"
            );
        }
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::InvokeCapability)
    {
        MechanismValue payload = instruction.payload;
        if (instruction.payloadComputed)
        {
            const ReadPathResult computed = EvaluateReadPath(
                instruction.payloadSource,
                context,
                values
            );
            if (!computed)
            {
                return Reject(
                    computed.status == ReadPathStatus::ArithmeticRejected
                        ? BytecodeTransactionStatus::NumericOverflow
                        : BytecodeTransactionStatus::OperandTypeMismatch,
                    computed.message
                );
            }
            if (computed.kind == MechanismValueKind::Decimal)
            {
                double stored = 0.0;
                const kernel::FixedPointValue converted =
                    kernel::InternalToStorage(computed.scaled, stored);
                if (!converted)
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed capability payload does not fit decimal "
                        "storage"
                    );
                }
                payload = MechanismValue(stored);
            }
            else
            {
                std::int64_t stored = 0;
                const kernel::FixedPointValue converted =
                    kernel::InternalToInteger(computed.scaled, stored);
                if (!converted)
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed capability payload does not fit an integer"
                    );
                }
                payload = MechanismValue(stored);
            }
        }
        MechanismInstanceId targetInstance;
        if (instruction.targetRoleSlot)
        {
            const std::size_t roleSlot = instruction.targetRoleSlot.value;
            const MechanismReference* bound = nullptr;
            if (roleSlot < instance.roles.size())
            {
                for (const MechanismReference& reference
                    : instance.roles[roleSlot])
                {
                    if (reference.kind
                        == MechanismReferenceKind::MechanismInstance)
                    {
                        bound = &reference;
                        break;
                    }
                }
            }
            if (bound == nullptr)
            {
                return Reject(
                    BytecodeTransactionStatus::OperandTypeMismatch,
                    "invoke_capability target_role is not bound to a "
                    "mechanism instance"
                );
            }
            targetInstance = MechanismInstanceId{bound->value};
        }
        resultCommands.push_back(WorldCommand::InvokeCapability(
            instruction.capability,
            instruction.capabilityDeliveryType,
            context.tick + instruction.dueTickOffset,
            instruction.priority,
            std::move(payload),
            targetInstance,
            instruction.capabilityVersion
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CancelEvent)
    {
        resultCommands.push_back(WorldCommand::CancelEvent(
            instruction.eventSequence
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CreateRngStream)
    {
        resultCommands.push_back(WorldCommand::CreateRngStream(
            instruction.rngStream,
            instruction.rngSeed
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::AdvanceRngStream)
    {
        const DeterministicRngStream* stream = context.rng.Find(
            instruction.rngStream
        );
        if (stream == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "RNG advance references a missing stream"
            );
        }
        resultCommands.push_back(WorldCommand::AdvanceRngStream(
            instruction.rngStream,
            stream->drawCount,
            instruction.rngCount
        ));
        return outcome;
    }
    // Role-addressed Component writes. The Entity is whatever the role slot
    // holds right now, so a reusable Mechanism never names one.
    if (instruction.opcode
            == AlgorithmBytecodeOpcode::SetComponentFieldByRoleConstant
        || instruction.opcode
            == AlgorithmBytecodeOpcode::SetComponentFieldByRoleComputed)
    {
        if (instruction.targetRoleSlot.value >= instance.roles.size())
        {
            return Reject(
                BytecodeTransactionStatus::InvalidFieldSlot,
                "bytecode role Slot is out of range"
            );
        }
        const std::vector<MechanismReference>& bound =
            instance.roles[instruction.targetRoleSlot.value];
        if (bound.empty())
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "set_component_field role slot is unbound"
            );
        }

        MechanismValue written = instruction.operand;
        if (instruction.opcode
            == AlgorithmBytecodeOpcode::SetComponentFieldByRoleComputed)
        {
            ReadPathResult value =
                EvaluateReadPath(instruction.left, context, values);
            if (!value)
            {
                return Reject(
                    value.status == ReadPathStatus::ArithmeticRejected
                        ? BytecodeTransactionStatus::NumericOverflow
                        : BytecodeTransactionStatus::OperandTypeMismatch,
                    value.message
                );
            }
            if (instruction.hasRight)
            {
                const ReadPathResult right =
                    EvaluateReadPath(instruction.right, context, values);
                if (!right)
                {
                    return Reject(
                        right.status == ReadPathStatus::ArithmeticRejected
                            ? BytecodeTransactionStatus::NumericOverflow
                            : BytecodeTransactionStatus::OperandTypeMismatch,
                        right.message
                    );
                }
                value = ApplyBinaryOperator(
                    instruction.binaryOperator,
                    value,
                    right
                );
                if (!value)
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        value.message
                    );
                }
            }
            if (instruction.componentFieldKind == MechanismValueKind::Decimal)
            {
                double stored = 0.0;
                if (!kernel::InternalToStorage(value.scaled, stored))
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed value does not fit the decimal storage scale"
                    );
                }
                written = MechanismValue(stored);
            }
            else if (instruction.componentFieldKind
                == MechanismValueKind::Integer)
            {
                std::int64_t stored = 0;
                if (!kernel::InternalToInteger(value.scaled, stored))
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed value does not fit an integer field"
                    );
                }
                written = MechanismValue(stored);
            }
            else
            {
                return Reject(
                    BytecodeTransactionStatus::OperandTypeMismatch,
                    "computed set_component_field requires a numeric "
                    "Component field"
                );
            }
        }

        // One command per bound Entity. A role slot may legitimately hold
        // several, and writing only the first would be a silent partial write.
        //
        // Charged like cancel_events and like an aggregation: one instruction,
        // N units of work. Without this an author could bind a role to a
        // hundred Entities and have one budgeted instruction emit a hundred
        // commands -- the instruction budget is the only bound on how much
        // work one algorithm invocation can commit, so any construct that
        // expands at run time has to pay for its expansion.
        if (bound.size() > 1
            && !context.budget.Consume(bound.size() - 1))
        {
            return Reject(
                // Same status cancel_events uses for the same situation;
                // BytecodeTransactionStatus has no budget member of its own.
                BytecodeTransactionStatus::OperandTypeMismatch,
                "set_component_field targets " + std::to_string(bound.size())
                    + " Entities, exceeding the instruction budget"
            );
        }
        for (const MechanismReference& target : bound)
        {
            if (target.kind != MechanismReferenceKind::Entity)
            {
                return Reject(
                    BytecodeTransactionStatus::OperandTypeMismatch,
                    "set_component_field role slot holds a non-Entity "
                    "reference"
                );
            }
            resultCommands.push_back(WorldCommand::SetComponentField(
                EntityId{target.value},
                instruction.component,
                instruction.componentField,
                written
            ));
        }
        return outcome;
    }

    // Placed above the field-Slot guard on purpose: this opcode writes an
    // Entity Component, not one of this instance's own fields, so
    // instruction.field is unused and would fail a guard meant for others.
    if (instruction.opcode
        == AlgorithmBytecodeOpcode::SetComponentFieldComputed)
    {
        ReadPathResult value =
            EvaluateReadPath(instruction.left, context, values);
        if (!value)
        {
            return Reject(
                value.status == ReadPathStatus::ArithmeticRejected
                    ? BytecodeTransactionStatus::NumericOverflow
                    : BytecodeTransactionStatus::OperandTypeMismatch,
                value.message
            );
        }
        if (instruction.hasRight)
        {
            const ReadPathResult right =
                EvaluateReadPath(instruction.right, context, values);
            if (!right)
            {
                return Reject(
                    right.status == ReadPathStatus::ArithmeticRejected
                        ? BytecodeTransactionStatus::NumericOverflow
                        : BytecodeTransactionStatus::OperandTypeMismatch,
                    right.message
                );
            }
            value = ApplyBinaryOperator(
                instruction.binaryOperator,
                value,
                right
            );
            if (!value)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    value.message
                );
            }
        }

        // The Component field's declared kind wins, exactly as the destination
        // Mechanism field's does for SetFieldComputed. The compiler recorded
        // it, so an absent Component cannot make the VM guess.
        MechanismValue written;
        if (instruction.componentFieldKind == MechanismValueKind::Decimal)
        {
            double stored = 0.0;
            const kernel::FixedPointValue quantised =
                kernel::InternalToStorage(value.scaled, stored);
            if (!quantised)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "computed value does not fit the decimal storage scale"
                );
            }
            written = MechanismValue(stored);
        }
        else if (instruction.componentFieldKind == MechanismValueKind::Integer)
        {
            std::int64_t stored = 0;
            const kernel::FixedPointValue whole =
                kernel::InternalToInteger(value.scaled, stored);
            if (!whole)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "computed value does not fit an integer field"
                );
            }
            written = MechanismValue(stored);
        }
        else
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "computed set_component_field requires a numeric Component "
                "field"
            );
        }

        // Absolute, not a delta. Unlike a Mechanism field, a Component field
        // has no add form in the DSL yet, so nothing here can fan in from
        // several senders in one phase. When one is added it needs the same
        // delta treatment MechanismAddFieldOperation got, for the same reason.
        resultCommands.push_back(WorldCommand::SetComponentField(
            instruction.entity,
            instruction.component,
            instruction.componentField,
            written
        ));
        return outcome;
    }

    if (instruction.field.value >= values.size())
    {
        return Reject(
            BytecodeTransactionStatus::InvalidFieldSlot,
            "bytecode field Slot is out of range"
        );
    }

    if (instruction.opcode == AlgorithmBytecodeOpcode::SetFieldComputed
        || instruction.opcode == AlgorithmBytecodeOpcode::AddFieldComputed)
    {
        // The whole expression runs on the fixed-point pipeline, so nothing
        // between reading the operands and quantising the result touches a
        // double. Intermediates keep two digits below the storage quantum.
        ReadPathResult value =
            EvaluateReadPath(instruction.left, context, values);
        if (!value)
        {
            return Reject(
                value.status == ReadPathStatus::ArithmeticRejected
                    ? BytecodeTransactionStatus::NumericOverflow
                    : BytecodeTransactionStatus::OperandTypeMismatch,
                value.message
            );
        }
        if (instruction.hasRight)
        {
            const ReadPathResult right =
                EvaluateReadPath(instruction.right, context, values);
            if (!right)
            {
                return Reject(
                    right.status == ReadPathStatus::ArithmeticRejected
                        ? BytecodeTransactionStatus::NumericOverflow
                        : BytecodeTransactionStatus::OperandTypeMismatch,
                    right.message
                );
            }
            value = ApplyBinaryOperator(
                instruction.binaryOperator,
                value,
                right
            );
            if (!value)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    value.message
                );
            }
        }

        // The destination field's declared kind wins: writing a decimal
        // expression into an integer field rounds to the integer, it does not
        // silently change the field's kind out from under the schema.
        const MechanismValue& currentValue = values[instruction.field.value];
        const bool destinationIsDecimal =
            std::get_if<double>(&currentValue.data) != nullptr;
        const bool destinationIsInteger =
            std::get_if<std::int64_t>(&currentValue.data) != nullptr;
        if (!destinationIsDecimal && !destinationIsInteger)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "computed assignment requires a numeric destination field"
            );
        }

        std::int64_t combined = value.scaled;
        if (instruction.opcode == AlgorithmBytecodeOpcode::AddFieldComputed)
        {
            kernel::FixedPointValue base =
                destinationIsDecimal
                    ? kernel::DecimalToInternal(
                        *std::get_if<double>(&currentValue.data))
                    : kernel::IntegerToInternal(
                        *std::get_if<std::int64_t>(&currentValue.data));
            if (!base)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "current field value is outside the fixed-point range"
                );
            }
            const kernel::FixedPointValue sum =
                kernel::FixedAdd(base.scaled, combined);
            if (!sum)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "computed addition overflowed"
                );
            }
            combined = sum.scaled;
        }

        MechanismValue written;
        if (destinationIsDecimal)
        {
            double stored = 0.0;
            const kernel::FixedPointValue quantised =
                kernel::InternalToStorage(combined, stored);
            if (!quantised)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "computed value does not fit the decimal storage scale"
                );
            }
            written = MechanismValue(stored);
        }
        else
        {
            std::int64_t stored = 0;
            const kernel::FixedPointValue whole =
                kernel::InternalToInteger(combined, stored);
            if (!whole)
            {
                return Reject(
                    BytecodeTransactionStatus::NumericOverflow,
                    "computed value does not fit an integer field"
                );
            }
            written = MechanismValue(stored);
        }

        // The local copy takes the absolute result so later instructions in
        // this same program see it, but the COMMAND carries a delta for the
        // add form. An absolute value computed against the dispatch snapshot
        // is only correct while one invocation writes the field; the moment N
        // do -- Capability fan-in is exactly that shape -- they all read the
        // same stale base and the last commit wins. A delta has no base.
        values[instruction.field.value] = written;
        if (instruction.opcode == AlgorithmBytecodeOpcode::AddFieldComputed)
        {
            MechanismValue delta;
            if (destinationIsDecimal)
            {
                double stored = 0.0;
                const kernel::FixedPointValue quantised =
                    kernel::InternalToStorage(value.scaled, stored);
                if (!quantised)
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed delta does not fit the decimal storage scale"
                    );
                }
                delta = MechanismValue(stored);
            }
            else
            {
                std::int64_t stored = 0;
                const kernel::FixedPointValue whole =
                    kernel::InternalToInteger(value.scaled, stored);
                if (!whole)
                {
                    return Reject(
                        BytecodeTransactionStatus::NumericOverflow,
                        "computed delta does not fit an integer field"
                    );
                }
                delta = MechanismValue(stored);
            }
            resultCommands.push_back(WorldCommand::Mechanism(
                MechanismCommand::AddField(
                    instance.id,
                    instruction.field,
                    std::move(delta)
                )
            ));
            return outcome;
        }
        resultCommands.push_back(WorldCommand::Mechanism(
            MechanismCommand::SetField(
                instance.id,
                instruction.field,
                std::move(written)
            )
        ));
        return outcome;
    }

    if (instruction.operandFromPayload && context.scheduledEvent == nullptr)
    {
        return Reject(
            BytecodeTransactionStatus::OperandTypeMismatch,
            "from_payload requires an active scheduled invocation"
        );
    }
    const MechanismValue& operandValue = instruction.operandFromPayload
        ? context.scheduledEvent->payload
        : instruction.operand;

    MechanismValue next;
    if (instruction.opcode == AlgorithmBytecodeOpcode::SetFieldConstant)
    {
        next = operandValue;
    }
    else if (instruction.opcode
        == AlgorithmBytecodeOpcode::AddIntegerConstant)
    {
        const auto* current = std::get_if<std::int64_t>(
            &values[instruction.field.value].data
        );
        const auto* operand = std::get_if<std::int64_t>(&operandValue.data);
        std::int64_t sum = 0;
        if (current == nullptr || operand == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "Integer bytecode requires an integer field and operand"
            );
        }
        if (!AddWithoutOverflow(*current, *operand, sum))
        {
            return Reject(
                BytecodeTransactionStatus::NumericOverflow,
                "Integer bytecode addition overflowed"
            );
        }
        next = MechanismValue(sum);
    }
    else
    {
        const auto* current = std::get_if<double>(
            &values[instruction.field.value].data
        );
        const auto* operand = std::get_if<double>(&operandValue.data);
        if (current == nullptr || operand == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "Decimal bytecode requires a decimal field and operand"
            );
        }
        const double sum = *current + *operand;
        if (!std::isfinite(sum))
        {
            return Reject(
                BytecodeTransactionStatus::NumericOverflow,
                "Decimal bytecode addition produced a non-finite value"
            );
        }
        next = MechanismValue(sum);
    }
    values[instruction.field.value] = next;
    // Same rule as the computed form: `add` commits a delta so concurrent
    // writers accumulate, `set` commits the absolute value it was given.
    if (instruction.opcode == AlgorithmBytecodeOpcode::AddIntegerConstant
        || instruction.opcode == AlgorithmBytecodeOpcode::AddDecimalConstant)
    {
        resultCommands.push_back(WorldCommand::Mechanism(
            MechanismCommand::AddField(
                instance.id,
                instruction.field,
                operandValue
            )
        ));
        return outcome;
    }
    resultCommands.push_back(WorldCommand::Mechanism(
        MechanismCommand::SetField(
            instance.id,
            instruction.field,
            std::move(next)
        )
    ));
    return outcome;
}

}
