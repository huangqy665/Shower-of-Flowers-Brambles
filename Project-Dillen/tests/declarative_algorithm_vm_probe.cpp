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
    const auto* integerSet = integerAdd == nullptr
        ? nullptr
        : std::get_if<MechanismSetFieldOperation>(
            &integerAdd->operation
        );
    const auto* decimalAdd = std::get_if<MechanismCommand>(
        &result.transaction.commands[2].payload
    );
    const auto* decimalSet = decimalAdd == nullptr
        ? nullptr
        : std::get_if<MechanismSetFieldOperation>(
            &decimalAdd->operation
        );
    if (integerSet == nullptr
        || integerSet->value != MechanismValue(std::int64_t{7})
        || decimalSet == nullptr
        || decimalSet->value != MechanismValue(2.0))
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

    std::cout << "Declarative Algorithm bytecode VM: passed\n";
    return 0;
}
