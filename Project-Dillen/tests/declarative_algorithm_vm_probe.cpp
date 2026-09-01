#include <cstdint>
#include <iostream>
#include <limits>
#include <variant>

#include "algorithm_registry.hpp"
#include "declarative_algorithm_vm.hpp"

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    MechanismInstance instance;
    instance.id = MechanismInstanceId{1};
    instance.lifecycle = MechanismLifecycleState::Created;
    instance.values = {
        MechanismValue(std::int64_t{2}),
        MechanismValue(1.5)
    };

    CompiledAlgorithmProgram program;
    program.definition = MechanismDefinitionId{1};
    program.algorithm = AlgorithmId{1};
    program.algorithmVersion = 1;
    program.stages[AlgorithmEntryPoint::Create] = {
        {
            AlgorithmBytecodeOpcode::SetFieldConstant,
            MechanismFieldSlotId{0},
            MechanismValue(std::int64_t{5})
        },
        {
            AlgorithmBytecodeOpcode::AddIntegerConstant,
            MechanismFieldSlotId{0},
            MechanismValue(std::int64_t{2})
        },
        {
            AlgorithmBytecodeOpcode::AddDecimalConstant,
            MechanismFieldSlotId{1},
            MechanismValue(0.5)
        },
        {
            AlgorithmBytecodeOpcode::TransitionLifecycle,
            {},
            {},
            MechanismLifecycleState::Active
        }
    };

    runtime::AlgorithmExecutionBudget executionBudget({16, 1000000});
    const runtime::DeclarativeAlgorithmResult result =
        runtime::DeclarativeAlgorithmVm{}.Execute(
            program,
            AlgorithmEntryPoint::Create,
            instance,
            executionBudget
        );
    if (!result || result.transaction.commands.size() != 4)
    {
        std::cerr << "Declarative bytecode execution failed\n";
        return 1;
    }
    const auto* integerAdd = std::get_if<MechanismCommand>(
        &result.transaction.commands[1].payload
    );
    // `add` commits a DELTA, not the absolute value it computed. The absolute
    // value is still what later instructions in this program see, but sending
    // it to the executor would make N concurrent writers overwrite one another
    // instead of accumulating -- see MechanismAddFieldOperation.
    const auto* integerSet = integerAdd == nullptr
        ? nullptr
        : std::get_if<MechanismAddFieldOperation>(
            &integerAdd->operation
        );
    const auto* decimalAdd = std::get_if<MechanismCommand>(
        &result.transaction.commands[2].payload
    );
    const auto* decimalSet = decimalAdd == nullptr
        ? nullptr
        : std::get_if<MechanismAddFieldOperation>(
            &decimalAdd->operation
        );
    // The deltas are what was authored (+2 and +0.5), not the sums (7, 2.0)
    // the previous absolute form carried.
    if (integerSet == nullptr
        || integerSet->delta != MechanismValue(std::int64_t{2})
        || decimalSet == nullptr
        || decimalSet->delta != MechanismValue(0.5))
    {
        std::cerr << "Declarative bytecode sequencing mismatch\n";
        return 2;
    }

    instance.values[0] = MechanismValue(
        std::numeric_limits<std::int64_t>::max()
    );
    CompiledAlgorithmProgram overflowProgram;
    overflowProgram.stages[AlgorithmEntryPoint::Tick] = {{
        AlgorithmBytecodeOpcode::AddIntegerConstant,
        MechanismFieldSlotId{0},
        MechanismValue(std::int64_t{1})
    }};
    runtime::AlgorithmExecutionBudget overflowBudget({16, 1000000});
    const runtime::DeclarativeAlgorithmResult overflow =
        runtime::DeclarativeAlgorithmVm{}.Execute(
            overflowProgram,
            AlgorithmEntryPoint::Tick,
            instance,
            overflowBudget
        );
    runtime::AlgorithmExecutionBudget missingStageBudget({16, 1000000});
    const runtime::DeclarativeAlgorithmResult missingStage =
        runtime::DeclarativeAlgorithmVm{}.Execute(
            program,
            AlgorithmEntryPoint::Tick,
            instance,
            missingStageBudget
        );
    if (overflow
        || overflow.status
            != runtime::DeclarativeAlgorithmStatus::NumericOverflow
        || missingStage
        || missingStage.status
            != runtime::DeclarativeAlgorithmStatus::StageMissing
        || missingStage.message.empty())
    {
        std::cerr << "Declarative bytecode fault boundary mismatch\n";
        return 3;
    }

    runtime::AlgorithmExecutionBudget exhaustedBudget({1, 1000000});
    const runtime::DeclarativeAlgorithmResult exhausted =
        runtime::DeclarativeAlgorithmVm{}.Execute(
            program,
            AlgorithmEntryPoint::Create,
            instance,
            exhaustedBudget
        );
    if (exhausted
        || exhausted.status
            != runtime::DeclarativeAlgorithmStatus::
                InstructionBudgetExceeded
        || exhaustedBudget.Report().instructionsConsumed != 1)
    {
        std::cerr << "Declarative instruction budget mismatch\n";
        return 4;
    }

    // ---------------------------------------------------------------------
    // Role-addressed Component writes.
    //
    // Three things had no runtime coverage at all: the constant form, the
    // multi-target expansion, and the budget that expansion has to pay. The
    // Demo exercises only the single-target computed form, so a role bound to
    // several Entities could have written just the first one, or written all
    // of them for free, and every gate would still have been green.
    // ---------------------------------------------------------------------
    {
        MechanismInstance owner;
        owner.id = MechanismInstanceId{7};
        owner.lifecycle = MechanismLifecycleState::Active;
        owner.values = {MechanismValue(std::int64_t{4})};
        // Role slot 0 holds three Entities.
        owner.roles = {{
            {MechanismReferenceKind::Entity, 100, 1001},
            {MechanismReferenceKind::Entity, 100, 1002},
            {MechanismReferenceKind::Entity, 100, 1003}
        }};

        AlgorithmBytecodeInstruction write;
        write.opcode =
            AlgorithmBytecodeOpcode::SetComponentFieldByRoleConstant;
        write.targetRoleSlot = MechanismRoleSlotId{0};
        write.component = ComponentTypeId{55};
        write.componentField = ComponentFieldSlotId{2};
        write.operand = MechanismValue(std::int64_t{9});

        CompiledAlgorithmProgram byRole;
        byRole.stages[AlgorithmEntryPoint::Tick] = {write};

        // One instruction plus two extra targets = three units.
        runtime::AlgorithmExecutionBudget roleBudget({8, 1000000});
        const runtime::DeclarativeAlgorithmResult fanOut =
            runtime::DeclarativeAlgorithmVm{}.Execute(
                byRole,
                AlgorithmEntryPoint::Tick,
                owner,
                roleBudget
            );
        if (!fanOut || fanOut.transaction.commands.size() != 3)
        {
            std::cerr << "role-addressed write did not reach every bound "
                         "Entity" << '\n';
            return 5;
        }
        bool addressed = true;
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto* command = std::get_if<ComponentSetFieldCommand>(
                &fanOut.transaction.commands[index].payload
            );
            addressed = addressed
                && command != nullptr
                && command->owner
                    == EntityId{1001 + static_cast<std::uint64_t>(index)}
                && command->component == ComponentTypeId{55}
                && command->field == ComponentFieldSlotId{2}
                && command->value == MechanismValue(std::int64_t{9});
        }
        if (!addressed)
        {
            std::cerr << "role-addressed write produced the wrong commands"
                      << '\n';
            return 5;
        }
        // The instruction itself costs one; the two extra targets cost one
        // each. A fan-out that charged nothing would read 1 here.
        if (roleBudget.Report().instructionsConsumed != 3)
        {
            std::cerr << "role-addressed write consumed "
                      << roleBudget.Report().instructionsConsumed
                      << " units, expected 3" << '\n';
            return 5;
        }

        // A budget that covers the instruction but not the expansion must
        // fail, not silently write fewer Entities.
        runtime::AlgorithmExecutionBudget tightBudget({2, 1000000});
        const runtime::DeclarativeAlgorithmResult tight =
            runtime::DeclarativeAlgorithmVm{}.Execute(
                byRole,
                AlgorithmEntryPoint::Tick,
                owner,
                tightBudget
            );
        if (tight)
        {
            std::cerr << "role-addressed write ignored an exhausted budget"
                      << '\n';
            return 5;
        }

        // An unbound required slot is a Fault, not a no-op write.
        MechanismInstance unbound = owner;
        unbound.roles = {{}};
        runtime::AlgorithmExecutionBudget unboundBudget({8, 1000000});
        const runtime::DeclarativeAlgorithmResult empty =
            runtime::DeclarativeAlgorithmVm{}.Execute(
                byRole,
                AlgorithmEntryPoint::Tick,
                unbound,
                unboundBudget
            );
        if (empty)
        {
            std::cerr << "role-addressed write accepted an unbound slot"
                      << '\n';
            return 5;
        }
    }

    std::cout << "Declarative Algorithm bytecode VM: passed" << '\n';
    return 0;
}
