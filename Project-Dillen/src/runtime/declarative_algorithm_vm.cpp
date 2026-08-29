#include "declarative_algorithm_vm.hpp"

#include "algorithm_runtime.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace dillen::runtime {

namespace {

DeclarativeAlgorithmResult Failure(
    DeclarativeAlgorithmStatus status,
    std::string message
)
{
    DeclarativeAlgorithmResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
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

bool ConditionsMatch(
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

}

DeclarativeAlgorithmResult::operator bool() const noexcept
{
    return status == DeclarativeAlgorithmStatus::Completed;
}

DeclarativeAlgorithmResult DeclarativeAlgorithmVm::Execute(
    const kernel::CompiledAlgorithmProgram& program,
    kernel::AlgorithmEntryPoint entryPoint,
    const kernel::MechanismInstance& instance,
    AlgorithmExecutionBudget& budget
) const
{
    WorldQuerySnapshot query;
    kernel::MechanismQuerySnapshot mechanisms;
    kernel::DeterministicRngSnapshot rng;
    kernel::FrozenRuntimeCatalog catalog;
    const std::vector<kernel::CapabilityBindingSlotId> capabilities;
    const AlgorithmInvocationContext context{
        AlgorithmRuntimeStage::Tick,
        0,
        instance,
        query,
        mechanisms,
        rng,
        catalog,
        capabilities,
        nullptr,
        nullptr,
        nullptr,
        budget
    };
    return Execute(program, entryPoint, context);
}

DeclarativeAlgorithmResult DeclarativeAlgorithmVm::Execute(
    const kernel::CompiledAlgorithmProgram& program,
    kernel::AlgorithmEntryPoint entryPoint,
    const AlgorithmInvocationContext& context
) const
{
    using namespace kernel;
    const MechanismInstance& instance = context.instance;
    AlgorithmExecutionBudget& budget = context.budget;
    const std::vector<AlgorithmBytecodeInstruction>* stage =
        program.FindStage(entryPoint);
    if (stage == nullptr)
    {
        return Failure(
            DeclarativeAlgorithmStatus::StageMissing,
            "Compiled declarative stage is missing"
        );
    }

    std::vector<MechanismValue> values = instance.values;
    MechanismLifecycleState lifecycle = instance.lifecycle;
    std::vector<WorldCommand> resultCommands;
    resultCommands.reserve(stage->size());
    for (const AlgorithmBytecodeInstruction& instruction : *stage)
    {
        if (!budget.Consume())
        {
            return Failure(
                DeclarativeAlgorithmStatus::InstructionBudgetExceeded,
                "Declarative instruction budget was exceeded"
            );
        }
        if (!ConditionsMatch(instruction, context, values))
        {
            continue;
        }
        if (instruction.opcode
            == AlgorithmBytecodeOpcode::TransitionLifecycle)
        {
            if (!CanTransitionMechanismLifecycle(
                    lifecycle,
                    instruction.lifecycle))
            {
                return Failure(
                    DeclarativeAlgorithmStatus::
                        LifecycleTransitionRejected,
                    "Declarative lifecycle transition is not permitted"
                );
            }
            lifecycle = instruction.lifecycle;
            resultCommands.push_back(WorldCommand::Mechanism(
                MechanismCommand::TransitionLifecycle(
                    instance.id,
                    lifecycle
                )
            ));
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::CreateEntity)
        {
            resultCommands.push_back(WorldCommand::CreateEntity(
                instruction.entityDefinition
            ));
            continue;
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
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::AddRelation)
        {
            resultCommands.push_back(WorldCommand::AddRelation(
                instruction.relationType,
                instruction.sourceEntity,
                instruction.targetEntity
            ));
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::RemoveRelation)
        {
            resultCommands.push_back(WorldCommand::RemoveRelation(
                instruction.relation
            ));
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::SpawnMechanism)
        {
            resultCommands.push_back(WorldCommand::SpawnMechanism(
                instruction.spawn
            ));
            continue;
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
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::CancelEvent)
        {
            resultCommands.push_back(WorldCommand::CancelEvent(
                instruction.eventSequence
            ));
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::CreateRngStream)
        {
            resultCommands.push_back(WorldCommand::CreateRngStream(
                instruction.rngStream,
                instruction.rngSeed
            ));
            continue;
        }
        if (instruction.opcode == AlgorithmBytecodeOpcode::AdvanceRngStream)
        {
            const DeterministicRngStream* stream = context.rng.Find(
                instruction.rngStream
            );
            if (stream == nullptr)
            {
                return Failure(
                    DeclarativeAlgorithmStatus::OperandTypeMismatch,
                    "Declarative RNG advance references a missing stream"
                );
            }
            resultCommands.push_back(WorldCommand::AdvanceRngStream(
                instruction.rngStream,
                stream->drawCount,
                instruction.rngCount
            ));
            continue;
        }
        if (instruction.field.value >= values.size())
        {
            return Failure(
                DeclarativeAlgorithmStatus::InvalidFieldSlot,
                "Declarative bytecode field Slot is out of range"
            );
        }

        MechanismValue next;
        if (instruction.opcode
            == AlgorithmBytecodeOpcode::SetFieldConstant)
        {
            next = instruction.operand;
        }
        else if (instruction.opcode
            == AlgorithmBytecodeOpcode::AddIntegerConstant)
        {
            const auto* current = std::get_if<std::int64_t>(
                &values[instruction.field.value].data
            );
            const auto* operand = std::get_if<std::int64_t>(
                &instruction.operand.data
            );
            std::int64_t sum = 0;
            if (current == nullptr || operand == nullptr)
            {
                return Failure(
                    DeclarativeAlgorithmStatus::OperandTypeMismatch,
                    "Integer bytecode requires an integer field and operand"
                );
            }
            if (!AddWithoutOverflow(*current, *operand, sum))
            {
                return Failure(
                    DeclarativeAlgorithmStatus::NumericOverflow,
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
            const auto* operand = std::get_if<double>(
                &instruction.operand.data
            );
            if (current == nullptr || operand == nullptr)
            {
                return Failure(
                    DeclarativeAlgorithmStatus::OperandTypeMismatch,
                    "Decimal bytecode requires a decimal field and operand"
                );
            }
            const double sum = *current + *operand;
            if (!std::isfinite(sum))
            {
                return Failure(
                    DeclarativeAlgorithmStatus::NumericOverflow,
                    "Decimal bytecode addition produced a non-finite value"
                );
            }
            next = MechanismValue(sum);
        }
        values[instruction.field.value] = next;
        resultCommands.push_back(WorldCommand::Mechanism(
            MechanismCommand::SetField(
                instance.id,
                instruction.field,
                std::move(next)
            )
        ));
    }

    DeclarativeAlgorithmResult result;
    result.transaction.commands = std::move(resultCommands);
    return result;
}

}
