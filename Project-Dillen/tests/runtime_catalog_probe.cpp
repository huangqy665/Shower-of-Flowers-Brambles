#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "algorithm_registry.hpp"
#include "mechanism_command.hpp"
#include "entity_definition_registry.hpp"
#include "component_schema.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_instance.hpp"
#include "mechanism_instance_store.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"

namespace {

std::string Digest(char value)
{
    return std::string(64, value);
}

dillen::kernel::PackageVersionRange Versions(
    dillen::kernel::PackageVersion minimum,
    dillen::kernel::PackageVersion maximum
)
{
    dillen::kernel::PackageVersionRange range;
    range.minimumInclusive = minimum;
    range.maximumExclusive = maximum;
    return range;
}

dillen::kernel::PackageManifest CoreManifest(
    dillen::kernel::PackageVersion version,
    char digest
)
{
    using namespace dillen::kernel;
    PackageManifest manifest;
    manifest.canonicalName = "dillen.core.rules";
    manifest.id = StablePackageId(manifest.canonicalName);
    manifest.version = version;
    manifest.contentDigest = Digest(digest);
    manifest.providedCapabilities = {{
        StableCapabilityId("dillen.world.command"),
        "dillen.world.command",
        1
    }};
    return manifest;
}

}

// A composed Ruleset may select a Component Type at exactly ONE Schema
// version.
//
// Component field Slots are assigned per (type, version) by sorted field name,
// so two versions give the same slot number two different meanings. An
// instruction that reaches a Component through a role carries the type and the
// slot but no version -- it cannot carry one, because the Entity it will write
// is unknown until run time and Entities declare their own versions. Under two
// versions such an instruction is not merely unchecked, it is unanswerable.
//
// The ambiguity is rejected at compile time rather than resolved by an
// invisible rule, exactly as ambiguous Capability providers are.
bool RejectsTwoComponentVersions()
{
    using namespace dillen::kernel;

    const std::string componentName = "dillen.test.two_version_stock";
    const ComponentTypeId componentType =
        StableComponentTypeId(componentName);

    const auto makeSchema = [&](std::uint32_t version)
    {
        ComponentSchema component;
        component.type = componentType;
        component.canonicalName = componentName;
        component.version = version;
        MechanismFieldSchema amount;
        amount.name = "amount";
        amount.kind = MechanismValueKind::Integer;
        amount.required = true;
        amount.defaultValue = MechanismValue(std::int64_t{0});
        component.fields.push_back(amount);
        // v2 adds a field whose name sorts BEFORE "amount", which is what
        // actually moves the slots: slot 0 is `amount` in v1 and `added` in
        // v2. Same type, same slot number, different field.
        if (version == 2)
        {
            MechanismFieldSchema added;
            added.name = "added";
            added.kind = MechanismValueKind::Integer;
            added.required = true;
            added.defaultValue = MechanismValue(std::int64_t{0});
            component.fields.push_back(added);
        }
        return component;
    };

    ComponentSchemaRegistry componentSchemas;
    if (componentSchemas.Register(makeSchema(1))
            != ComponentSchemaRegisterResult::Added
        || componentSchemas.Register(makeSchema(2))
            != ComponentSchemaRegisterResult::Added)
    {
        std::cerr << "component version probe: registration failed" << '\n';
        return false;
    }
    componentSchemas.Freeze();

    // Two Entity Definitions, each declaring a different version of the same
    // Component Type. That is what pulls both versions into the selection.
    EntityDefinitionRegistry entityDefinitions;
    const EntityTypeId placeType = StableEntityTypeId("dillen.test.place");
    for (std::uint32_t version = 1; version <= 2; ++version)
    {
        EntityDefinition entity;
        entity.canonicalName = version == 1
            ? "dillen.test.first_place"
            : "dillen.test.second_place";
        entity.type = placeType;
        entity.id = StableEntityDefinitionId(placeType, entity.canonicalName);
        entity.source.sourceName = "probe";
        entity.source.virtualPath = "tests/two_versions.txt";
        EntityComponentDefinition component;
        component.type = componentType;
        component.schemaVersion = version;
        component.fields["amount"] = MechanismValue(std::int64_t{1});
        if (version == 2)
        {
            component.fields["added"] = MechanismValue(std::int64_t{2});
        }
        entity.components.push_back(component);
        const EntityDefinitionDeclareResult declared =
            entityDefinitions.Declare(entity, componentSchemas);
        if (declared != EntityDefinitionDeclareResult::Added)
        {
            std::cerr << "component version probe: entity " << version
                      << " was refused (" << static_cast<int>(declared)
                      << ")" << '\n';
            return false;
        }
    }
    entityDefinitions.Freeze();

    MechanismSchemaRegistry schemas;
    schemas.Freeze();
    AlgorithmRegistry algorithms;
    algorithms.Freeze();
    MechanismDefinitionRegistry definitions;
    definitions.Freeze();
    MechanismSpawnDefinitionRegistry spawns;
    spawns.Freeze();
    RuntimeCapabilityContractRegistry capabilityContracts;
    capabilityContracts.Freeze();
    PackageManifestRegistry manifests;
    manifests.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.two_versions";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    // Required directly, so both versions enter the selection regardless of
    // what else happens to be reachable. Content reaches the same state by
    // declaring two Entities on different versions; this states it outright.
    ruleset.requiredComponents.push_back({componentType, 1});
    ruleset.requiredComponents.push_back({componentType, 2});
    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport))
    {
        std::cerr << "component version probe: package lock failed" << '\n';
        return false;
    }

    FrozenRuntimeCatalog catalog;
    RuntimeCompileReport compileReport;
    const bool compiled = RuntimeCompiler{}.Compile(
        ruleset,
        packageLock,
        schemas,
        componentSchemas,
        algorithms,
        definitions,
        entityDefinitions,
        spawns,
        capabilityContracts,
        catalog,
        compileReport
    );
    if (compiled)
    {
        std::cerr << "component version probe: two Schema versions of one "
                     "Component Type were accepted" << '\n';
        return false;
    }
    for (const RuntimeCompileIssue& issue : compileReport.issues)
    {
        if (issue.code
            == RuntimeCompileIssueCode::ComponentSchemaVersionAmbiguous)
        {
            return true;
        }
    }
    std::cerr << "component version probe: rejected for the wrong reason:"
              << '\n';
    for (const RuntimeCompileIssue& issue : compileReport.issues)
    {
        std::cerr << "  " << issue.message << '\n';
    }
    return false;
}

// Creating an instance straight from a Definition must honour required roles.
//
// The Definition registry deliberately lets a Mechanism-Instance role go
// unfilled, because a Definition cannot name an instance that does not exist
// yet; the Spawn registry enforces the minimum instead. That left
// CreateFromDefinition -- a public entry point with no Spawn anywhere near it
// -- as a way to reach a live instance whose required roles are empty, which
// every read path and every directed Capability call would then Fault on.
bool RejectsUnfilledRequiredRole()
{
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.needs_a_role";
    const MechanismTypeId type = StableMechanismTypeId(typeName);

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema counter;
    counter.name = "counter";
    counter.kind = MechanismValueKind::Integer;
    counter.required = true;
    counter.defaultValue = MechanismValue(std::int64_t{0});
    schema.fields.push_back(counter);
    // Required, and of the one reference kind a Definition is structurally
    // unable to fill.
    MechanismRoleSchema partner;
    partner.name = "partner";
    partner.referenceKind = MechanismReferenceKind::MechanismInstance;
    partner.referenceType = type.value;
    partner.minimumCount = 1;
    partner.maximumCount = 1;
    schema.roles.push_back(partner);

    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        return false;
    }
    schemas.Freeze();

    MechanismDefinition definition;
    definition.canonicalName = "dillen.test.unbound_definition";
    definition.type = type;
    definition.schemaVersion = 1;
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.fields["counter"] = MechanismValue(std::int64_t{0});
    definition.source.sourceName = "probe";
    definition.source.virtualPath = "tests/unbound_role.txt";

    MechanismDefinitionRegistry definitions;
    AlgorithmRegistry algorithms;
    algorithms.Freeze();
    // The Definition itself must still be accepted: requiring a Mechanism
    // Instance role here would make every such Schema unregisterable.
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "unfilled role probe: the Definition was refused, so the "
                     "minimum is being enforced in the wrong place"
                  << '\n';
        return false;
    }
    definitions.Freeze();

    ComponentSchemaRegistry componentSchemas;
    componentSchemas.Freeze();
    EntityDefinitionRegistry entityDefinitions;
    entityDefinitions.Freeze();
    MechanismSpawnDefinitionRegistry spawns;
    spawns.Freeze();
    RuntimeCapabilityContractRegistry capabilityContracts;
    capabilityContracts.Freeze();
    PackageManifestRegistry manifests;
    manifests.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.unbound_role";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.requiredSchemas.push_back({type, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    PackageLock packageLock;
    PackageLockReport lockReport;
    FrozenRuntimeCatalog catalog;
    RuntimeCompileReport compileReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport)
        || !RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            spawns,
            capabilityContracts,
            catalog,
            compileReport))
    {
        std::cerr << "unfilled role probe: compile failed" << '\n';
        return false;
    }

    MechanismInstanceStore store;
    MechanismInstanceId created;
    const MechanismInstanceCreateResult result = store.CreateFromDefinition(
        definition.id,
        catalog,
        1,
        created
    );
    if (result != MechanismInstanceCreateResult::RoleBindingMissing
        || store.Size() != 0)
    {
        std::cerr << "unfilled role probe: expected RoleBindingMissing, got "
                  << static_cast<int>(result) << " with " << store.Size()
                  << " instances" << '\n';
        return false;
    }
    return true;
}

int main()
{
    using namespace dillen::kernel;

    static_assert(std::is_same_v<
        decltype(MechanismSetFieldOperation{}.field),
        MechanismFieldSlotId
    >);
    static_assert(std::is_same_v<
        decltype(MechanismInstance{}.values),
        std::vector<MechanismValue>
    >);

    PackageManifestRegistry manifests;
    PackageManifest coreOne = CoreManifest({1, 0, 0}, '1');
    PackageManifest coreTwo = CoreManifest({2, 0, 0}, '2');
    PackageManifest addon;
    addon.canonicalName = "dillen.test.addon";
    addon.id = StablePackageId(addon.canonicalName);
    addon.version = {1, 1, 0};
    addon.contentDigest = Digest('a');
    addon.loadPriority = -10;
    addon.dependencies.push_back({
        coreOne.id,
        coreOne.canonicalName,
        Versions({1, 0, 0}, {3, 0, 0}),
        true
    });
    if (manifests.Register(coreOne)
            != PackageManifestRegisterResult::Added
        || manifests.Register(coreTwo)
            != PackageManifestRegisterResult::Added
        || manifests.Register(addon)
            != PackageManifestRegisterResult::Added
        || manifests.Register(addon)
            != PackageManifestRegisterResult::DuplicateVersion)
    {
        std::cerr << "Package Manifest registration mismatch\n";
        return 1;
    }
    manifests.Freeze();

    const std::string typeName = "dillen.test.compiled_counter";
    const MechanismTypeId type = StableMechanismTypeId(typeName);
    const std::string algorithmName = "dillen.algorithm.compiled_counter";
    const AlgorithmId algorithmId = StableAlgorithmId(algorithmName);
    const std::string capabilityName = "dillen.world.command";
    const CapabilityId capabilityId = StableCapabilityId(capabilityName);

    RuntimeCapabilityContract capability;
    capability.id = capabilityId;
    capability.canonicalName = capabilityName;
    capability.version = 1;
    capability.operations = {"transaction.enqueue"};
    RuntimeCapabilityContractRegistry capabilityContracts;
    if (capabilityContracts.Register(std::move(capability))
        != CapabilityContractRegisterResult::Added)
    {
        std::cerr << "Capability Contract registration failed\n";
        return 2;
    }
    capabilityContracts.Freeze();

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema zeta;
    zeta.name = "zeta";
    zeta.kind = MechanismValueKind::Integer;
    zeta.defaultValue = MechanismValue(std::int64_t{9});
    schema.fields.push_back(zeta);
    MechanismFieldSchema alpha;
    alpha.name = "alpha";
    alpha.kind = MechanismValueKind::Integer;
    alpha.defaultValue = MechanismValue(std::int64_t{1});
    alpha.minimumNumber = 0;
    alpha.maximumNumber = 10;
    schema.fields.push_back(alpha);

    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Schema registration failed\n";
        return 2;
    }
    schemas.Freeze();

    AlgorithmDescriptor algorithm;
    algorithm.id = algorithmId;
    algorithm.canonicalName = algorithmName;
    algorithm.version = 1;
    algorithm.entryPoints = AlgorithmEntryPoint::Tick
        | AlgorithmEntryPoint::Command;
    algorithm.program.stages[AlgorithmEntryPoint::Tick] = {
        AlgorithmInstructionDefinition::AddField(
            "alpha",
            MechanismValue(std::int64_t{1})
        )
    };
    algorithm.program.stages[AlgorithmEntryPoint::Command] = {
        AlgorithmInstructionDefinition::SetField(
            "zeta",
            MechanismValue(std::int64_t{10})
        )
    };
    algorithm.requiredCapabilities = {{
        capabilityId,
        capabilityName,
        {1, 2}
    }};
    AlgorithmRegistry algorithms;
    if (algorithms.Register(std::move(algorithm))
        != AlgorithmRegisterResult::Added)
    {
        std::cerr << "Algorithm registration failed\n";
        return 3;
    }
    algorithms.Freeze();

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = "compiled_counter";
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.algorithm = algorithmId;
    definition.algorithmVersion = 1;
    definition.source.sourceName = "runtime_catalog_probe";
    definition.source.virtualPath = "tests/runtime_catalog.txt";
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Definition registration failed\n";
        return 4;
    }
    definitions.Freeze();

    ComponentSchemaRegistry componentSchemas;
    EntityDefinitionRegistry entityDefinitions;
    componentSchemas.Freeze();
    entityDefinitions.Freeze();
    MechanismSpawnDefinition spawn;
    spawn.canonicalName = "compiled_counter_initial";
    spawn.definition = definition.id;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.initialFields["alpha"] = MechanismValue(std::int64_t{4});
    spawn.source.sourceName = "runtime_catalog_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added)
    {
        std::cerr << "Spawn registration failed\n";
        return 5;
    }
    spawns.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.runtime_catalog";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.packages.push_back({
        addon.id,
        addon.canonicalName,
        Versions({1, 0, 0}, {2, 0, 0})
    });
    ruleset.requiredSchemas.push_back({type, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    ruleset.requiredMechanismSpawns.push_back(spawn.id);
    ruleset.requiredAlgorithms.push_back({algorithmId, 1});
    ruleset.requiredCapabilities.push_back({
        capabilityId,
        capabilityName,
        {1, 2}
    });

    RulesetRegistry rulesets;
    if (rulesets.Register(ruleset) != RulesetRegisterResult::Added
        || rulesets.Register(ruleset)
            != RulesetRegisterResult::DuplicateVersion)
    {
        std::cerr << "Ruleset registration mismatch\n";
        return 5;
    }
    rulesets.Freeze();

    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport)
        || packageLock.Size() != 2
        || packageLock.Entries()[0].package != coreOne.id
        || packageLock.Entries()[0].version != PackageVersion{2, 0, 0}
        || packageLock.Entries()[1].package != addon.id)
    {
        std::cerr << "Package Lock resolution mismatch\n";
        return 6;
    }

    RulesetDefinition impossible = ruleset;
    impossible.packages.push_back({
        coreOne.id,
        coreOne.canonicalName,
        Versions({3, 0, 0}, {4, 0, 0})
    });
    PackageLock rejectedLock;
    PackageLockReport rejectedReport;
    if (PackageLockBuilder{}.Resolve(
            manifests,
            impossible,
            rejectedLock,
            rejectedReport)
        || rejectedReport.issues.empty()
        || rejectedLock.IsResolved())
    {
        std::cerr << "Package version conflict was not rejected\n";
        return 7;
    }

    RuntimeCompileReport compileReport;
    FrozenRuntimeCatalog catalog;
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            spawns,
            capabilityContracts,
            catalog,
            compileReport)
        || !catalog.IsFrozen()
        || catalog.DefinitionCount() != 1
        || catalog.SpawnDefinitionCount() != 1
        || catalog.LayoutCount() != 1
        || catalog.CapabilityCount() != 1
        || catalog.AlgorithmProgramCount() != 1
        || catalog.AlgorithmCapabilities(algorithmId, 1).size() != 1
        || catalog.FindCapability(
            catalog.AlgorithmCapabilities(algorithmId, 1).front()) == nullptr
        || !catalog.Fingerprint()
        || catalog.Fingerprint()
            != ComputeRulesetFingerprint(ruleset, packageLock))
    {
        std::cerr << "Frozen Runtime Catalog compilation failed\n";
        return 8;
    }

    const auto alphaSlot = catalog.ResolveDefinitionFieldSlot(
        definition.id,
        "alpha"
    );
    const auto zetaSlot = catalog.ResolveDefinitionFieldSlot(
        definition.id,
        "zeta"
    );
    const CompiledMechanismDefinition* compiled =
        catalog.FindDefinition(definition.id);
    const CompiledAlgorithmProgram* compiledProgram =
        catalog.FindAlgorithmProgram(definition.id);
    const std::vector<AlgorithmBytecodeInstruction>* tickProgram =
        compiledProgram == nullptr
            ? nullptr
            : compiledProgram->FindStage(AlgorithmEntryPoint::Tick);
    const std::vector<AlgorithmBytecodeInstruction>* commandProgram =
        compiledProgram == nullptr
            ? nullptr
            : compiledProgram->FindStage(AlgorithmEntryPoint::Command);
    if (!alphaSlot
        || !zetaSlot
        || alphaSlot->value != 0
        || zetaSlot->value != 1
        || compiled == nullptr
        || compiled->initialValues[alphaSlot->value]
            != MechanismValue(std::int64_t{1})
        || compiled->initialValues[zetaSlot->value]
            != MechanismValue(std::int64_t{9})
        || tickProgram == nullptr
        || tickProgram->size() != 1
        || tickProgram->front().opcode
            != AlgorithmBytecodeOpcode::AddIntegerConstant
        || tickProgram->front().field != *alphaSlot
        || commandProgram == nullptr
        || commandProgram->size() != 1
        || commandProgram->front().opcode
            != AlgorithmBytecodeOpcode::SetFieldConstant
        || commandProgram->front().field != *zetaSlot)
    {
        std::cerr << "Stable Field Slot compilation mismatch\n";
        return 9;
    }

    MechanismInstanceStore store;
    MechanismInstanceId instance;
    const CompiledMechanismSpawnDefinition* compiledSpawn =
        catalog.FindSpawnDefinition(spawn.id);
    if (compiledSpawn == nullptr
        || store.CreateFromSpawn(
            compiledSpawn->id,
            catalog,
            0,
            instance) != MechanismInstanceCreateResult::Created)
    {
        std::cerr << "Compiled instance creation failed\n";
        return 10;
    }
    const MechanismTransactionResult updated = store.ApplyTransaction(
        {MechanismCommand::SetField(
            instance,
            *alphaSlot,
            MechanismValue(std::int64_t{7}))},
        catalog,
        1
    );
    if (!updated
        || store.Find(instance)->values[alphaSlot->value]
            != MechanismValue(std::int64_t{7})
        || !std::holds_alternative<MechanismFieldChange>(
            updated.changes.front())
        || std::get<MechanismFieldChange>(updated.changes.front()).field
            != *alphaSlot)
    {
        std::cerr << "Slot-only Runtime transaction mismatch\n";
        return 11;
    }

    RulesetDefinition missingCapability = ruleset;
    missingCapability.requiredCapabilities.push_back({
        StableCapabilityId("dillen.missing.capability"),
        "dillen.missing.capability",
        {1, 2}
    });
    FrozenRuntimeCatalog rejectedCatalog;
    RuntimeCompileReport integrityFailure;
    if (RuntimeCompiler{}.Compile(
            missingCapability,
            packageLock,
            schemas,
            componentSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            spawns,
            capabilityContracts,
            rejectedCatalog,
            integrityFailure)
        || integrityFailure.integrity.Success()
        || rejectedCatalog.IsFrozen())
    {
        std::cerr << "Ruleset integrity barrier mismatch\n";
        return 12;
    }

    if (!RejectsTwoComponentVersions())
    {
        return 13;
    }
    if (!RejectsUnfilledRequiredRole())
    {
        return 14;
    }

    std::cout
        << "Ruleset lock and Runtime Catalog: passed (fingerprint "
        << catalog.Fingerprint().ToHex()
        << ", Component version ambiguity and unfilled required roles both "
           "rejected)" << '\n';
    return 0;
}
