#include <cstdint>
#include <iostream>
#include <string>

#include "algorithm_registry.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_value.hpp"

namespace {

dillen::kernel::MechanismDefinition MakeDefinition(
    dillen::kernel::MechanismTypeId type,
    dillen::kernel::AlgorithmId algorithm,
    std::string name
)
{
    dillen::kernel::MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = std::move(name);
    definition.id = dillen::kernel::StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 2;
    definition.algorithm = algorithm;
    definition.algorithmVersion = 1;
    definition.source.sourceName = "probe";
    definition.source.virtualPath = "tests/mechanisms.txt";
    return definition;
}

}

int main()
{
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.counter";
    const MechanismTypeId type = StableMechanismTypeId(typeName);
    const AlgorithmId algorithm = StableAlgorithmId(
        "dillen.algorithms.counter"
    );
    const std::uint64_t countryReferenceType =
        StableMechanismTypeId("dillen.entity.country").value;
    if (!type
        || !algorithm
        || type != StableMechanismTypeId("DILLEN.TEST.COUNTER")
        || MechanismInstanceId{1} == MechanismInstanceId{2})
    {
        std::cerr << "Mechanism stable ID mismatch\n";
        return 1;
    }

    MechanismValue nested(MechanismValue::Object{
        {"enabled", MechanismValue(true)},
        {"values", MechanismValue(MechanismValue::List{
            MechanismValue(std::int64_t{1}),
            MechanismValue(std::int64_t{2})
        })}
    });
    if (nested.Kind() != MechanismValueKind::Object
        || nested.IsScalar()
        || nested != MechanismValue(std::get<MechanismValue::Object>(
            nested.data)))
    {
        std::cerr << "Mechanism unified value mismatch\n";
        return 2;
    }

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;

    MechanismFieldSchema label;
    label.name = "label";
    label.kind = MechanismValueKind::String;
    label.required = true;
    label.minimumSize = 1;
    schema.fields.push_back(label);

    MechanismFieldSchema amount;
    amount.name = "amount";
    amount.kind = MechanismValueKind::Integer;
    amount.defaultValue = MechanismValue(std::int64_t{0});
    amount.minimumNumber = 0.0;
    amount.maximumNumber = 100.0;
    schema.fields.push_back(amount);

    MechanismFieldSchema weight;
    weight.name = "weight";
    weight.kind = MechanismValueKind::Decimal;
    weight.minimumNumber = 0.0;
    schema.fields.push_back(weight);

    MechanismFieldSchema tags;
    tags.name = "tags";
    tags.kind = MechanismValueKind::List;
    tags.maximumSize = 3;
    tags.listElementKind = MechanismValueKind::String;
    schema.fields.push_back(tags);

    MechanismFieldSchema target;
    target.name = "target";
    target.kind = MechanismValueKind::Reference;
    target.referenceKind = MechanismReferenceKind::Entity;
    target.referenceType = countryReferenceType;
    schema.fields.push_back(target);

    MechanismRoleSchema owner;
    owner.name = "owner";
    owner.referenceKind = MechanismReferenceKind::Entity;
    owner.referenceType = countryReferenceType;
    owner.minimumCount = 1;
    owner.maximumCount = 1;
    schema.roles.push_back(owner);

    MechanismSchemaRegistry schemas;
    MechanismSchema invalidSchema = schema;
    invalidSchema.version = 3;
    invalidSchema.fields.front().minimumNumber = 0.0;
    if (IsValidMechanismSymbol("---")
        || schemas.Register(std::move(invalidSchema))
            != MechanismSchemaRegisterResult::InvalidSchema)
    {
        std::cerr << "Mechanism Schema boundary validation mismatch\n";
        return 3;
    }
    if (schemas.Register(schema) != MechanismSchemaRegisterResult::Added
        || schemas.Register(schema)
            != MechanismSchemaRegisterResult::DuplicateVersion)
    {
        std::cerr << "Mechanism Schema registration mismatch\n";
        return 4;
    }
    MechanismSchema schemaVersionTwo = schema;
    schemaVersionTwo.version = 2;
    if (schemas.Register(std::move(schemaVersionTwo))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism Schema version registration failed\n";
        return 5;
    }

    AlgorithmDescriptor algorithmDescriptor;
    algorithmDescriptor.id = algorithm;
    algorithmDescriptor.canonicalName = "dillen.algorithms.counter";
    algorithmDescriptor.version = 1;
    algorithmDescriptor.backend = AlgorithmBackend::Declarative;
    algorithmDescriptor.entryPoints = AlgorithmEntryPoint::Create
        | AlgorithmEntryPoint::Tick
        | AlgorithmEntryPoint::Command;
    algorithmDescriptor.program.stages[AlgorithmEntryPoint::Create] = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Active
        )
    };
    algorithmDescriptor.program.stages[AlgorithmEntryPoint::Tick] = {
        AlgorithmInstructionDefinition::AddField(
            "amount",
            MechanismValue(std::int64_t{1})
        )
    };
    algorithmDescriptor.program.stages[AlgorithmEntryPoint::Command] = {};
    algorithmDescriptor.requiredCapabilities = {
        {
            StableCapabilityId("dillen.world.read"),
            "dillen.world.read",
            {1, 2}
        },
        {
            StableCapabilityId("dillen.world.command"),
            "dillen.world.command",
            {1, 2}
        }
    };
    AlgorithmRegistry algorithms;
    AlgorithmDescriptor invalidAlgorithm = algorithmDescriptor;
    invalidAlgorithm.version = 2;
    invalidAlgorithm.entryPoints = static_cast<AlgorithmEntryPoint>(1U << 31U);
    if (algorithms.Register(std::move(invalidAlgorithm))
        != AlgorithmRegisterResult::InvalidDescriptor)
    {
        std::cerr << "Algorithm boundary validation mismatch\n";
        return 6;
    }
    if (algorithms.Register(algorithmDescriptor)
        != AlgorithmRegisterResult::Added
        || algorithms.Register(algorithmDescriptor)
            != AlgorithmRegisterResult::DuplicateVersion)
    {
        std::cerr << "Algorithm registration mismatch\n";
        return 7;
    }

    MechanismDefinitionRegistry definitions;
    MechanismDefinition tooEarly = MakeDefinition(
        type,
        algorithm,
        "too_early"
    );
    if (definitions.Declare(tooEarly, schemas, algorithms)
        != MechanismDefinitionDeclareResult::DependenciesNotFrozen)
    {
        std::cerr << "Registry dependency freeze barrier mismatch\n";
        return 8;
    }

    schemas.Freeze();
    algorithms.Freeze();
    if (schemas.Size() != 2
        || schemas.Latest(type) == nullptr
        || schemas.Latest(type)->version != 2
        || schemas.Find(typeName, 1) == nullptr
        || algorithms.Latest(algorithm) == nullptr
        || !HasAlgorithmEntryPoint(
            algorithms.Latest(algorithm)->entryPoints,
            AlgorithmEntryPoint::Tick))
    {
        std::cerr << "Versioned Registry lookup mismatch\n";
        return 9;
    }

    MechanismDefinition missingField = MakeDefinition(
        type,
        algorithm,
        "missing_field"
    );
    missingField.roles["owner"] = {{
        MechanismReferenceKind::Entity,
        countryReferenceType,
        1
    }};
    if (definitions.Declare(missingField, schemas, algorithms)
        != MechanismDefinitionDeclareResult::RequiredFieldMissing)
    {
        std::cerr << "Required field validation mismatch\n";
        return 10;
    }

    MechanismDefinition unknownField = MakeDefinition(
        type,
        algorithm,
        "unknown_field"
    );
    unknownField.fields["label"] = MechanismValue("valid");
    unknownField.fields["undeclared"] = MechanismValue(true);
    unknownField.roles["owner"] = missingField.roles["owner"];
    if (definitions.Declare(unknownField, schemas, algorithms)
        != MechanismDefinitionDeclareResult::UnknownField)
    {
        std::cerr << "Unknown field validation mismatch\n";
        return 11;
    }

    MechanismDefinition invalidValue = MakeDefinition(
        type,
        algorithm,
        "invalid_value"
    );
    invalidValue.fields["label"] = MechanismValue("valid");
    invalidValue.fields["amount"] = MechanismValue(std::int64_t{101});
    invalidValue.roles["owner"] = missingField.roles["owner"];
    if (definitions.Declare(invalidValue, schemas, algorithms)
        != MechanismDefinitionDeclareResult::FieldValueInvalid)
    {
        std::cerr << "Field range validation mismatch\n";
        return 12;
    }

    MechanismDefinition missingRole = MakeDefinition(
        type,
        algorithm,
        "missing_role"
    );
    missingRole.fields["label"] = MechanismValue("valid");
    if (definitions.Declare(missingRole, schemas, algorithms)
        != MechanismDefinitionDeclareResult::RoleBindingInvalid)
    {
        std::cerr << "Role cardinality validation mismatch\n";
        return 13;
    }

    MechanismDefinition valid = MakeDefinition(
        type,
        algorithm,
        "valid_counter"
    );
    valid.fields["label"] = MechanismValue("Counter");
    valid.fields["weight"] = MechanismValue(std::int64_t{2});
    valid.fields["tags"] = MechanismValue(MechanismValue::List{
        MechanismValue("test"),
        MechanismValue("deterministic")
    });
    valid.roles["owner"] = missingField.roles["owner"];
    const MechanismDefinitionId validId = valid.id;
    if (definitions.Declare(valid, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added
        || definitions.Declare(valid, schemas, algorithms)
            != MechanismDefinitionDeclareResult::DuplicateDefinition)
    {
        std::cerr << "Mechanism Definition registration mismatch\n";
        return 14;
    }

    definitions.Freeze();
    const MechanismDefinition* stored = definitions.Find(validId);
    if (definitions.Size() != 1
        || stored == nullptr
        || definitions.Find(type, "valid_counter") != stored
        || stored->fields.find("amount") == stored->fields.end()
        || stored->fields.at("amount")
            != MechanismValue(std::int64_t{0})
        || definitions.Declare(valid, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Frozen)
    {
        std::cerr << "Frozen Mechanism Definition lookup mismatch\n";
        return 15;
    }

    std::cout
        << "Mechanism registries: passed (2 schemas, 1 algorithm, "
        << definitions.Size() << " definition)\n";
    return 0;
}
