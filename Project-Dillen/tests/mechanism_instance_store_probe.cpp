#include <cstdint>
#include <iostream>
#include <string>

#include "algorithm_registry.hpp"
#include "definition_registry.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_instance_store.hpp"
#include "mechanism_schema_registry.hpp"
#include "world_builder.hpp"

namespace {

dillen::kernel::MechanismDefinition MakeDefinition(
    dillen::kernel::MechanismTypeId type,
    std::uint64_t ownerType,
    std::string name,
    std::uint64_t owner
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
    definition.roles["owner"] = {{
        MechanismReferenceKind::Entity,
        ownerType,
        owner
    }};
    definition.source.sourceName = "probe";
    definition.source.virtualPath = "tests/mechanism_instances.txt";
    return definition;
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.runtime_counter";
    const MechanismTypeId type = StableMechanismTypeId(typeName);
    const std::uint64_t ownerType =
        StableMechanismTypeId("dillen.entity.country").value;

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
    schema.fields.push_back(counter);

    MechanismRoleSchema owner;
    owner.name = "owner";
    owner.referenceKind = MechanismReferenceKind::Entity;
    owner.referenceType = ownerType;
    owner.minimumCount = 1;
    owner.maximumCount = 1;
    schema.roles.push_back(owner);

    MechanismSchemaRegistry schemas;
    AlgorithmRegistry algorithms;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism instance Schema registration failed\n";
        return 1;
    }
    schemas.Freeze();
    algorithms.Freeze();

    MechanismDefinition alpha = MakeDefinition(type, ownerType, "alpha", 1);
    MechanismDefinition beta = MakeDefinition(type, ownerType, "beta", 2);
    const MechanismDefinitionId alphaDefinition = alpha.id;
    const MechanismDefinitionId betaDefinition = beta.id;
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(alpha, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added
        || definitions.Declare(beta, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Mechanism instance Definition registration failed\n";
        return 2;
    }
    definitions.Freeze();

    MechanismInstanceStore store;
    MechanismInstanceId alphaFirst;
    MechanismInstanceId betaFirst;
    MechanismInstanceId alphaSecond;
    if (store.CreateFromDefinition(
            alphaDefinition,
            definitions,
            7,
            alphaFirst) != MechanismInstanceCreateResult::Created
        || store.CreateFromDefinition(
            betaDefinition,
            definitions,
            8,
            betaFirst) != MechanismInstanceCreateResult::Created
        || store.CreateFromDefinition(
            alphaDefinition,
            definitions,
            9,
            alphaSecond) != MechanismInstanceCreateResult::Created)
    {
        std::cerr << "Mechanism instance creation failed\n";
        return 3;
    }

    const MechanismInstance* first = store.Find(alphaFirst);
    if (store.Size() != 3
        || first == nullptr
        || first->definition != alphaDefinition
        || first->type != type
        || first->creationOrdinal != 0
        || first->lifecycle != MechanismLifecycleState::Created
        || first->createdTick != 7
        || first->updatedTick != 7
        || first->values.at("counter")
            != MechanismValue(std::int64_t{0})
        || first->roles.at("owner").front().value != 1
        || alphaFirst != StableMechanismInstanceId(alphaDefinition, 0)
        || alphaSecond != StableMechanismInstanceId(alphaDefinition, 1)
        || betaFirst != StableMechanismInstanceId(betaDefinition, 0)
        || store.FindByDefinition(alphaDefinition).size() != 2
        || store.FindByDefinition(betaDefinition).size() != 1
        || store.FindByType(type).size() != 3)
    {
        std::cerr << "Mechanism instance state or index mismatch\n";
        return 4;
    }

    MechanismInstanceId rejected;
    MechanismDefinitionRegistry unfrozenDefinitions;
    if (store.CreateFromDefinition(
            alphaDefinition,
            unfrozenDefinitions,
            10,
            rejected)
            != MechanismInstanceCreateResult::DefinitionRegistryNotFrozen
        || rejected
        || store.CreateFromDefinition(
            StableMechanismDefinitionId(type, "missing"),
            definitions,
            10,
            rejected) != MechanismInstanceCreateResult::DefinitionMissing)
    {
        std::cerr << "Mechanism instance creation barrier mismatch\n";
        return 5;
    }

    store.Clear();
    MechanismInstanceId recreated;
    if (!store.Empty()
        || store.CreateFromDefinition(
            alphaDefinition,
            definitions,
            11,
            recreated) != MechanismInstanceCreateResult::Created
        || recreated != alphaFirst)
    {
        std::cerr << "Mechanism instance deterministic reset mismatch\n";
        return 6;
    }

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
            report)
        || report.HasErrors()
        || world.Mechanisms().Size() != definitions.Size()
        || world.Mechanisms().FindByDefinition(alphaDefinition).size() != 1
        || world.Mechanisms().FindByDefinition(betaDefinition).size() != 1)
    {
        std::cerr << "WorldBuilder mechanism instantiation failed\n";
        return 7;
    }

    const std::size_t committedSize = world.Mechanisms().Size();
    if (builder.Build(
            contentDefinitions,
            unfrozenDefinitions,
            {1936, 1, 2},
            world,
            report)
        || !report.HasErrors()
        || world.Mechanisms().Size() != committedSize)
    {
        std::cerr << "WorldBuilder mechanism transaction barrier failed\n";
        return 8;
    }

    worldbuilder::AuthoritativeWorld compatibilityWorld;
    if (!builder.Build(
            contentDefinitions,
            {1936, 1, 1},
            compatibilityWorld,
            report)
        || !compatibilityWorld.Mechanisms().Empty())
    {
        std::cerr << "WorldBuilder compatibility overload failed\n";
        return 9;
    }

    std::cout
        << "Mechanism Instance Store: passed ("
        << committedSize
        << " authoritative instances)\n";
    return 0;
}
