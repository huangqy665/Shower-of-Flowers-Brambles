#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "algorithm_registry.hpp"
#include "definition_registry.hpp"
#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_query_snapshot.hpp"
#include "mechanism_schema_registry.hpp"
#include "world_builder.hpp"
#include "world_event.hpp"
#include "world_transaction.hpp"

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
    definition.source.virtualPath = "tests/mechanism_runtime.txt";
    return definition;
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.runtime_pipeline";
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
        std::cerr << "Runtime pipeline Schema registration failed\n";
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
        std::cerr << "Runtime pipeline Definition registration failed\n";
        return 2;
    }
    definitions.Freeze();

    content::DefinitionRegistry contentDefinitions;
    contentDefinitions.Freeze();
    worldbuilder::WorldBuilder builder;
    worldbuilder::WorldBuildReport buildReport;
    worldbuilder::AuthoritativeWorld world;
    if (!builder.Build(
            contentDefinitions,
            definitions,
            {1936, 1, 1},
            world,
            buildReport))
    {
        std::cerr << "Runtime pipeline world construction failed\n";
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
    const MechanismQuerySnapshot initialSnapshot =
        world.MechanismSnapshot();
    if (!initialSnapshot.IsPublished()
        || initialSnapshot.Tick() != 0
        || initialSnapshot.Revision() != 0
        || initialSnapshot.Size() != 2
        || initialSnapshot.Find(alphaId) == nullptr
        || initialSnapshot.Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{0}))
    {
        std::cerr << "Initial Query Snapshot mismatch\n";
        return 4;
    }

    const std::uint64_t firstSequence = world.EnqueueWorldTransaction(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                alphaId,
                "counter",
                MechanismValue(std::int64_t{5})
            ),
            MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Active
            )
        }),
        1
    );
    const std::uint64_t rejectedSequence = world.EnqueueWorldTransaction(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                "counter",
                MechanismValue("invalid")
            )
        }),
        1
    );
    const std::uint64_t delayedSequence = world.EnqueueWorldTransaction(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                "counter",
                MechanismValue(std::int64_t{3})
            )
        }),
        2
    );
    if (firstSequence != 1
        || rejectedSequence != 2
        || delayedSequence != 3
        || world.WorldCommands().Size() != 3)
    {
        std::cerr << "World Command Queue sequence mismatch\n";
        return 5;
    }

    const MechanismSchedulerTickResult firstTick =
        world.RunMechanismSchedulerTick(definitions, schemas, 1);
    if (!firstTick
        || firstTick.processedTransactions != 2
        || firstTick.committedTransactions != 1
        || firstTick.rejectedTransactions != 1
        || firstTick.transactions.size() != 2
        || firstTick.transactions[0].sequence != firstSequence
        || firstTick.transactions[1].sequence != rejectedSequence
        || world.Tick() != 1
        || world.Revision() != 1
        || world.WorldCommands().Size() != 1
        || world.Mechanisms().Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{5})
        || world.MechanismSnapshot().Tick() != 1
        || world.MechanismSnapshot().Revision() != 1
        || world.MechanismSnapshot().Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{5})
        || initialSnapshot.Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{0}))
    {
        std::cerr << "First Scheduler Tick mismatch\n";
        return 6;
    }

    const std::vector<WorldEvent>& pendingEvents =
        world.WorldEvents().Pending();
    if (pendingEvents.size() != 4
        || pendingEvents[0].sequence != 1
        || pendingEvents[0].transactionSequence != firstSequence
        || !std::holds_alternative<WorldTransactionCommittedEvent>(
            pendingEvents[0].payload)
        || !std::holds_alternative<MechanismFieldChange>(
            pendingEvents[1].payload)
        || !std::holds_alternative<MechanismLifecycleChange>(
            pendingEvents[2].payload)
        || pendingEvents[3].transactionSequence != rejectedSequence
        || !std::holds_alternative<WorldTransactionRejectedEvent>(
            pendingEvents[3].payload))
    {
        std::cerr << "World Event publication mismatch\n";
        return 7;
    }
    if (world.DrainWorldEvents().size() != 4
        || !world.WorldEvents().Empty())
    {
        std::cerr << "World Event drain mismatch\n";
        return 8;
    }

    const MechanismSchedulerTickResult duplicateTick =
        world.RunMechanismSchedulerTick(definitions, schemas, 1);
    if (duplicateTick.status
            != MechanismSchedulerStatus::TickSequenceInvalid
        || world.WorldCommands().Size() != 1
        || world.Tick() != 1)
    {
        std::cerr << "Scheduler Tick sequence barrier mismatch\n";
        return 9;
    }

    const MechanismSchedulerTickResult secondTick =
        world.RunMechanismSchedulerTick(definitions, schemas, 2);
    if (!secondTick
        || secondTick.processedTransactions != 1
        || secondTick.transactions.front().sequence != delayedSequence
        || world.Revision() != 2
        || !world.WorldCommands().Empty()
        || world.MechanismSnapshot().Find(betaId)->values.at("counter")
            != MechanismValue(std::int64_t{3})
        || world.MechanismSnapshot().FindByDefinition(betaDefinition).size()
            != 1
        || world.MechanismSnapshot().FindByType(type).size() != 2)
    {
        std::cerr << "Delayed Scheduler transaction mismatch\n";
        return 10;
    }
    world.DrainWorldEvents();

    const WorldTransactionResult worldRejected = world.ApplyWorldTransaction(
        WorldTransaction::FromMechanismCommands({
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
        }),
        definitions,
        schemas,
        2
    );
    if (worldRejected
        || worldRejected.status != WorldTransactionStatus::MechanismRejected
        || world.Mechanisms().Find(alphaId)->values.at("counter")
            != MechanismValue(std::int64_t{5})
        || world.Revision() != 2
        || world.MechanismSnapshot().Revision() != 2)
    {
        std::cerr << "World transaction rollback mismatch\n";
        return 11;
    }
    world.DrainWorldEvents();

    const WorldTransactionResult worldCommitted =
        world.ApplyWorldTransaction(
            WorldTransaction::FromMechanismCommands({
                MechanismCommand::SetField(
                    alphaId,
                    "label",
                    MechanismValue("alpha_committed")
                ),
                MechanismCommand::SetField(
                    betaId,
                    "label",
                    MechanismValue("beta_committed")
                )
            }),
            definitions,
            schemas,
            2
        );
    if (!worldCommitted
        || worldCommitted.mechanism.changedInstances != 2
        || world.Revision() != 3
        || world.MechanismSnapshot().Revision() != 3
        || world.MechanismSnapshot().Find(alphaId)->values.at("label")
            != MechanismValue("alpha_committed")
        || world.MechanismSnapshot().Find(betaId)->values.at("label")
            != MechanismValue("beta_committed"))
    {
        std::cerr << "World transaction commit mismatch\n";
        return 12;
    }
    world.DrainWorldEvents();

    const WorldTransactionResult tickRejected = world.ApplyWorldTransaction(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                "counter",
                MechanismValue(std::int64_t{4})
            )
        }),
        definitions,
        schemas,
        1
    );
    if (tickRejected.status != WorldTransactionStatus::TickRegression
        || world.Tick() != 2
        || world.Revision() != 3)
    {
        std::cerr << "World transaction Tick barrier mismatch\n";
        return 13;
    }
    world.DrainWorldEvents();

    world.EnqueueWorldTransaction(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                "counter",
                MechanismValue(std::int64_t{4})
            )
        }),
        3
    );
    MechanismDefinitionRegistry unfrozenDefinitions;
    const MechanismSchedulerTickResult frozenBarrier =
        world.RunMechanismSchedulerTick(unfrozenDefinitions, schemas, 3);
    if (frozenBarrier.status
            != MechanismSchedulerStatus::DefinitionRegistryNotFrozen
        || world.WorldCommands().Size() != 1
        || world.Tick() != 2)
    {
        std::cerr << "Scheduler Registry barrier mismatch\n";
        return 14;
    }

    const MechanismSchedulerTickResult thirdTick =
        world.RunMechanismSchedulerTick(definitions, schemas, 3);
    if (!thirdTick
        || thirdTick.processedTransactions != 1
        || world.Tick() != 3
        || world.Revision() != 4
        || world.MechanismSnapshot().Tick() != 3
        || world.MechanismSnapshot().Revision() != 4
        || world.MechanismSnapshot().Find(betaId)->values.at("counter")
            != MechanismValue(std::int64_t{4}))
    {
        std::cerr << "Final Scheduler Tick mismatch\n";
        return 15;
    }

    std::cout
        << "Mechanism runtime pipeline: passed (tick "
        << world.Tick()
        << ", revision "
        << world.Revision()
        << ")\n";
    return 0;
}
