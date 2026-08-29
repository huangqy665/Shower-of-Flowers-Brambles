#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "native_effect_bridge.h"

namespace
{

core::NativeEffect MakeEffect(
    std::string operation,
    int64_t amount = 0
)
{
    core::NativeEffect effect;
    effect.operation = std::move(operation);
    effect.arguments["amount"] = amount;
    return effect;
}

}

int main()
{
    core::NativeEffectService service;
    std::string error;
    int64_t total = 0;
    service.SetSafetyGate([]
    {
        return std::static_pointer_cast<void>(std::make_shared<int>(1));
    });

    const auto addHandler = [&total](
        const core::NativeEffect& effect,
        const core::NativeEffectExecutionContext& context,
        core::PreparedNativeEffect& prepared,
        std::string& handlerError
    )
    {
        int64_t amount = 0;
        const core::NativeEffectValue* value = effect.Find("amount");
        if (!value
            || !core::NativeEffectValueToInteger(*value, amount)
            || context.playerTag != "CHI"
            || !context.safetyLease)
        {
            handlerError = "invalid_add_arguments";
            return false;
        }
        prepared.apply = [&total, amount](std::string& applyError)
        {
            total += amount;
            applyError.clear();
            return true;
        };
        prepared.rollback = [&total, amount]
        {
            total -= amount;
        };
        handlerError.clear();
        return true;
    };

    if (!service.RegisterHandler("test.add", addHandler, error)
        || service.RegisterHandler("TEST.ADD", addHandler, error)
        || error != "native_effect_handler_duplicate: test.add")
    {
        std::cerr << "native effect registration failed: "
                  << error << '\n';
        return 1;
    }

    core::NativeEffectBatch inactiveBatch;
    inactiveBatch.effects.push_back(MakeEffect("test.add", 1));
    const core::NativeEffectResult inactive = service.ExecuteImmediate(
        std::move(inactiveBatch),
        1,
        10
    );
    if (inactive.status != core::NativeEffectStatus::GameplayInactive)
    {
        std::cerr << "inactive gameplay request was accepted\n";
        return 2;
    }

    service.SetGameplayContext(true, "CHI", 7);
    core::NativeEffectBatch addBatch;
    addBatch.source = "probe";
    addBatch.effects.push_back(MakeEffect("test.add", 2));
    addBatch.effects.push_back(MakeEffect("test.add", 3));
    const core::NativeEffectResult added = service.ExecuteImmediate(
        std::move(addBatch),
        11,
        10
    );
    if (!added.Succeeded()
        || added.transactionId == 0
        || added.preparedCount != 2
        || added.appliedCount != 2
        || total != 5)
    {
        std::cerr << "atomic native effect batch failed\n";
        return 3;
    }

    core::NativeEffectBatch migratedThreadBatch;
    migratedThreadBatch.effects.push_back(MakeEffect("test.add", 4));
    const core::NativeEffectResult migratedThread = service.ExecuteImmediate(
        std::move(migratedThreadBatch),
        11,
        20
    );
    if (!migratedThread.Succeeded() || total != 9)
    {
        std::cerr << "execution thread migration failed\n";
        return 4;
    }

    if (!service.RegisterHandler(
            "test.fail",
            [](const core::NativeEffect&,
               const core::NativeEffectExecutionContext&,
               core::PreparedNativeEffect& prepared,
               std::string& handlerError)
            {
                prepared.apply = [](std::string& applyError)
                {
                    applyError = "expected_failure";
                    return false;
                };
                prepared.rollback = [] {};
                handlerError.clear();
                return true;
            },
            error
        ))
    {
        std::cerr << error << '\n';
        return 5;
    }

    core::NativeEffectBatch rollbackBatch;
    rollbackBatch.effects.push_back(MakeEffect("test.add", 7));
    rollbackBatch.effects.push_back(MakeEffect("test.fail"));
    const core::NativeEffectResult rolledBack = service.ExecuteImmediate(
        std::move(rollbackBatch),
        11,
        10
    );
    if (rolledBack.status != core::NativeEffectStatus::ApplyFailed
        || total != 9)
    {
        std::cerr << "atomic rollback failed\n";
        return 6;
    }

    if (!service.RegisterHandler(
            "test.no_rollback",
            [](const core::NativeEffect&,
               const core::NativeEffectExecutionContext&,
               core::PreparedNativeEffect& prepared,
               std::string& handlerError)
            {
                prepared.apply = [](std::string& applyError)
                {
                    applyError.clear();
                    return true;
                };
                handlerError.clear();
                return true;
            },
            error
        ))
    {
        std::cerr << error << '\n';
        return 7;
    }
    core::NativeEffectBatch rollbackRequired;
    rollbackRequired.effects.push_back(MakeEffect("test.add", 1));
    rollbackRequired.effects.push_back(
        MakeEffect("test.no_rollback")
    );
    const core::NativeEffectResult noRollback = service.ExecuteImmediate(
        std::move(rollbackRequired),
        11,
        10
    );
    if (noRollback.status
            != core::NativeEffectStatus::AtomicRollbackUnavailable
        || total != 9)
    {
        std::cerr << "rollback capability validation failed\n";
        return 8;
    }

    service.SetGameplayContext(true, "CHI", 8);
    core::NativeEffectBatch nextGeneration;
    nextGeneration.effects.push_back(MakeEffect("test.add", 4));
    const core::NativeEffectResult rebound = service.ExecuteImmediate(
        std::move(nextGeneration),
        12,
        20
    );
    if (!rebound.Succeeded() || total != 13)
    {
        std::cerr << "lifecycle transaction reset failed\n";
        return 9;
    }

    core::NativeEffectValue list = core::NativeEffectList{
        int64_t{1},
        int64_t{2},
        double{3.0}
    };
    std::vector<int64_t> integers;
    if (!core::NativeEffectValueToIntegerList(list, integers)
        || integers != std::vector<int64_t>({1, 2, 3})
        || !service.HasHandler("TEST.ADD")
        || service.Operations().size() != 3
        || !service.UnregisterHandler("test.no_rollback"))
    {
        std::cerr << "native effect utilities failed\n";
        return 10;
    }

    std::cout << "Native effect bridge probe passed\n";
    return 0;
}
