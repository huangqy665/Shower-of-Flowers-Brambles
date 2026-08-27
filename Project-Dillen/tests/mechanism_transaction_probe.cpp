#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "definition_registry.hpp"
#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "world_builder.hpp"

namespace {

dillen::kernel::MechanismDefinition MakeDefinition(
    dillen::kernel::MechanismTypeId type,
    std::string name
)
{
    using namespace dillen::kernel;

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = std::move(name);
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.fields["label"] = MechanismValue(
        definition.canonicalName
    );
    definition.source.sourceName = "probe";
    definition.source.virtualPath = "tests/mechanism_commands.txt";
    return definition;
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.transaction";
    const MechanismTypeId type = StableMechanismTypeId(typeName);

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;

    MechanismFieldSchema label;
    label.name = "label";
    label.kind = MechanismValueKind::String;
    label.required = true;
    schema.fields.push_back(label);

    MechanismFieldSchema counter;
    counter.name = "counter";
    counter.kind = MechanismValueKind::Integer;
    counter.defaultValue = MechanismValue(std::int64_t{0});
    counter.minimumNumber = 0.0;
    counter.maximumNumber = 10.0;
    schema.fields.push_back(counter);

    MechanismSchemaRegistry schemas;
    AlgorithmRegistry algorithms;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism transaction Schema registration failed\n";
        return 1;
    }
    schemas.Freeze();
    algorithms.Freeze();

    MechanismDefinition alpha = MakeDefinition(type, "alpha");
    MechanismDefinition beta = MakeDefinition(type, "beta");
    const MechanismDefinitionId alphaDefinition = alpha.id;
    const MechanismDefinitionId betaDefinition = beta.id;
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(alpha, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added
        || definitions.Declare(beta, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Mechanism transaction Definition registration failed\n";
        return 2;
    }
    definitions.Freeze();

    content::DefinitionRegistry contentDefinitions;
    contentDefinitions.Freeze();
    worldbuilder::WorldBuilder builder;
    worldbuilder::WorldBuildReport report;
    worldbuilder::AuthoritativeWorld world;
    if (!builder.Build(
            contentDefinitions,
            definitions,
            {1936, 1, 1},
            world,
            report))
    {
        std::cerr << "Mechanism transaction world construction failed\n";
        return 3;
    }

    const MechanismInstanceId alphaId = StableMechanismInstanceId(
        alphaDefinition,
        0
    );
    const MechanismInstanceId betaId = StableMechanismInstanceId(
        betaDefinition,
        0
    );
    const std::vector<MechanismCommand> initialCommands = {
        MechanismCommand::SetField(
            alphaId,
            "counter",
            MechanismValue(std::int64_t{5})
        ),
        MechanismCommand::TransitionLifecycle(
            alphaId,
            MechanismLifecycleState::Active
        ),
        MechanismCommand::SetField(
            betaId,
            "label",
            MechanismValue("beta_updated")
        )
    };
    const MechanismTransactionResult initial =
        world.ApplyMechanismTransaction(
            initialCommands,
            definitions,
            schemas,
            10
        );
    const MechanismInstance* alphaState = world.Mechanisms().Find(alphaId);
    const MechanismInstance* betaState = world.Mechanisms().Find(betaId);
    if (!initial
        || initial.changedInstances != 2
        || initial.commandIndex != initialCommands.size()
        || alphaState == nullptr
        || betaState == nullptr
        || alphaState->lifecycle != MechanismLifecycleState::Active
        || alphaState->values.at("counter")
            != MechanismValue(std::int64_t{5})
        || alphaState->updatedTick != 10
        || betaState->values.at("label")
            != MechanismValue("beta_updated")
        || betaState->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction commit mismatch\n";
        return 4;
    }

    const MechanismTransactionResult noChange =
        world.ApplyMechanismTransaction(
            {
                MechanismCommand::SetField(
                    alphaId,
                    "counter",
                    MechanismValue(std::int64_t{5})
                ),
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Active
                )
            },
            definitions,
            schemas,
            11
        );
    if (!noChange
        || noChange.changedInstances != 0
        || world.Mechanisms().Find(alphaId)->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction no-op mismatch\n";
        return 5;
    }

    const MechanismTransactionResult rejected =
        world.ApplyMechanismTransaction(
            {
                MechanismCommand::SetField(
                    alphaId,
                    "counter",
                    MechanismValue(std::int64_t{6})
                ),
                MechanismCommand::SetField(
                    betaId,
                    "counter",
                    MechanismValue("invalid")
                )
            },
            definitions,
            schemas,
            12
        );
    if (rejected
        || rejected.status
            != MechanismTransactionStatus::FieldValueInvalid
        || rejected.commandIndex != 1
        || rejected.target != betaId
        || world.Mechanisms().Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{5})
        || world.Mechanisms().Find(alphaId)->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction rollback mismatch\n";
        return 6;
    }

    const MechanismTransactionResult lifecycleBatch =
        world.ApplyMechanismTransaction(
            {
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Paused
                ),
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Active
                )
            },
            definitions,
            schemas,
            12
        );
    if (!lifecycleBatch
        || lifecycleBatch.changedInstances != 1
        || world.Mechanisms().Find(alphaId)->lifecycle
            != MechanismLifecycleState::Active
        || world.Mechanisms().Find(alphaId)->updatedTick != 12)
    {
        std::cerr << "Mechanism lifecycle batch mismatch\n";
        return 7;
    }

    const MechanismTransactionResult completed =
        world.ApplyMechanismTransaction(
            {MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Completed)},
            definitions,
            schemas,
            13
        );
    const MechanismTransactionResult terminalRejected =
        world.ApplyMechanismTransaction(
            {MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Active)},
            definitions,
            schemas,
            14
        );
    if (!completed
        || terminalRejected
        || terminalRejected.status
            != MechanismTransactionStatus::LifecycleTransitionInvalid
        || !IsTerminalMechanismLifecycleState(
            world.Mechanisms().Find(alphaId)->lifecycle)
        || world.Mechanisms().Find(alphaId)->updatedTick != 13)
    {
        std::cerr << "Mechanism terminal lifecycle mismatch\n";
        return 8;
    }

    const MechanismTransactionResult tickRejected =
        world.ApplyMechanismTransaction(
            {MechanismCommand::SetField(
                betaId,
                "counter",
                MechanismValue(std::int64_t{1}))},
            definitions,
            schemas,
            9
        );
    const MechanismTransactionResult targetRejected =
        world.ApplyMechanismTransaction(
            {MechanismCommand::SetField(
                MechanismInstanceId{1},
                "counter",
                MechanismValue(std::int64_t{1}))},
            definitions,
            schemas,
            20
        );
    if (tickRejected.status != MechanismTransactionStatus::TickRegression
        || targetRejected.status
            != MechanismTransactionStatus::TargetMissing)
    {
        std::cerr << "Mechanism transaction safety barrier mismatch\n";
        return 9;
    }

    MechanismDefinitionRegistry unfrozenDefinitions;
    MechanismSchemaRegistry unfrozenSchemas;
    const MechanismTransactionResult definitionBarrier =
        world.ApplyMechanismTransaction(
            {},
            unfrozenDefinitions,
            schemas,
            20
        );
    const MechanismTransactionResult schemaBarrier =
        world.ApplyMechanismTransaction(
            {},
            definitions,
            unfrozenSchemas,
            20
        );
    if (definitionBarrier.status
            != MechanismTransactionStatus::DefinitionRegistryNotFrozen
        || schemaBarrier.status
            != MechanismTransactionStatus::SchemaRegistryNotFrozen)
    {
        std::cerr << "Mechanism transaction Registry barrier mismatch\n";
        return 10;
    }

    std::cout
        << "Mechanism lifecycle and transactions: passed ("
        << world.Mechanisms().Size()
        << " instances)\n";
    return 0;
}
