#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "authoring_pipeline.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "runtime_save_codec.hpp"
#include "template_registry.hpp"

// Compile golden for the Authoring DSL.
//
// The save format is frozen down to the byte -- 36 variant-tag static_asserts
// and two sets of golden bytes. The language authors actually write had no
// guard at all. That asymmetry is backwards for an engine whose whole premise
// is that mechanisms are defined externally: a save-format break has a
// migration path, and a DSL break does not. Content written against a drifting
// DSL can only be fixed by hand.
//
// This probe pins the third of the four observable results (Parse, Resolve,
// Compile, Diagnostic): given a fixture that exercises every construct the DSL
// can express, the Frozen Runtime Catalog's compiled bytecode and slot layout
// must encode to exactly these bytes.
//
// It is also the machine proof of additivity for the read-operand work that
// comes next. New opcodes, operand sources and reducers must not change how
// any existing construct lowers -- and if they do, this golden moves. The
// encoder below is written per-opcode, emitting only the fields a given opcode
// actually uses, precisely so that adding a field to the instruction struct
// cannot shift the encoding of instructions that do not use it.

namespace
{
using namespace dillen;

// ---------------------------------------------------------------------------
// Canonical encoder
//
// Little-endian, length-prefixed, no padding, no pointers, no addresses.
// Deliberately independent of RuntimeSaveCodec: that codec encodes save
// images, and reusing it here would make a save-format change silently move a
// DSL golden that has nothing to do with it.
// ---------------------------------------------------------------------------
class Encoder
{
public:
    void U8(std::uint8_t value) { bytes_.push_back(value); }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void I32(std::int32_t value)
    {
        U32(static_cast<std::uint32_t>(value));
    }

    void Text(const std::string& value)
    {
        U32(static_cast<std::uint32_t>(value.size()));
        for (const char character : value)
        {
            bytes_.push_back(static_cast<std::uint8_t>(character));
        }
    }

    void Value(const kernel::MechanismValue& value)
    {
        // Tag first, then only the payload that tag carries.
        U8(static_cast<std::uint8_t>(value.data.index()));
        if (const auto* flag = std::get_if<bool>(&value.data))
        {
            U8(*flag ? 1U : 0U);
        }
        else if (const auto* integer = std::get_if<std::int64_t>(&value.data))
        {
            U64(static_cast<std::uint64_t>(*integer));
        }
        else if (const auto* decimal = std::get_if<double>(&value.data))
        {
            // Bit pattern, not a formatted number: the golden must fail on a
            // representation change, not paper over it with rounding.
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(*decimal), "double is 64-bit");
            std::memcpy(&bits, decimal, sizeof(bits));
            U64(bits);
        }
        else if (const auto* text = std::get_if<std::string>(&value.data))
        {
            Text(*text);
        }
        else if (const auto* reference =
            std::get_if<kernel::MechanismReference>(&value.data))
        {
            U8(static_cast<std::uint8_t>(reference->kind));
            U64(reference->type);
            U64(reference->value);
        }
        // Null / List / Object carry nothing the fixture uses; the tag alone
        // still distinguishes them, and a fixture that starts using one will
        // move the golden and force this branch to be written.
    }

    const std::vector<std::uint8_t>& Bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

void EncodeReadPath(
    Encoder& out,
    const kernel::CompiledAlgorithmReadPath& path
)
{
    out.U8(static_cast<std::uint8_t>(path.root));
    out.U8(static_cast<std::uint8_t>(path.reduce));
    out.U8(static_cast<std::uint8_t>(path.terminal));
    switch (path.root)
    {
    case kernel::AlgorithmReadRoot::Constant:
        out.Value(path.constant);
        return;
    case kernel::AlgorithmReadRoot::EventPayload:
        return;
    case kernel::AlgorithmReadRoot::SelfField:
        out.U32(path.selfField.value);
        return;
    case kernel::AlgorithmReadRoot::RoleTarget:
        break;
    }
    out.U32(path.role.value);
    out.U8(path.traverseRelation ? 1U : 0U);
    if (path.traverseRelation)
    {
        out.U64(path.relationType.value);
        out.U8(static_cast<std::uint8_t>(path.direction));
    }
    if (path.terminal == kernel::AlgorithmReadTerminal::ComponentField)
    {
        out.U64(path.component.value);
        out.U32(path.componentField.value);
    }
    else
    {
        out.U32(path.targetField.value);
    }
}

void EncodeCondition(
    Encoder& out,
    const kernel::CompiledAlgorithmCondition& condition
)
{
    out.U8(static_cast<std::uint8_t>(condition.kind));
    switch (condition.kind)
    {
    case kernel::AlgorithmConditionKind::SelfFieldEquals:
        out.U32(condition.field.value);
        out.Value(condition.value);
        break;
    case kernel::AlgorithmConditionKind::QueryCountAtLeast:
        out.U8(static_cast<std::uint8_t>(condition.queryKind));
        out.U64(condition.queryType);
        out.U64(condition.minimumCount);
        break;
    case kernel::AlgorithmConditionKind::ScheduledEventTypeEquals:
        out.U64(condition.eventType.value);
        break;
    case kernel::AlgorithmConditionKind::RngModuloEquals:
        out.U64(condition.rngStream.value);
        out.U64(condition.rngOffset);
        out.U64(condition.rngModulo);
        out.U64(condition.rngEquals);
        break;
    case kernel::AlgorithmConditionKind::Compare:
        out.U8(static_cast<std::uint8_t>(condition.compare));
        EncodeReadPath(out, condition.left);
        EncodeReadPath(out, condition.right);
        break;
    }
}

// Per-opcode: emit only what this opcode reads. A new field on the instruction
// struct therefore cannot shift the bytes of an opcode that ignores it, which
// is what makes this golden a real additivity test rather than a tripwire that
// fires on every struct edit.
void EncodeInstruction(
    Encoder& out,
    const kernel::AlgorithmBytecodeInstruction& instruction
)
{
    using Opcode = kernel::AlgorithmBytecodeOpcode;
    out.U8(static_cast<std::uint8_t>(instruction.opcode));
    out.U32(static_cast<std::uint32_t>(instruction.conditions.size()));
    for (const kernel::CompiledAlgorithmCondition& condition
        : instruction.conditions)
    {
        EncodeCondition(out, condition);
    }
    switch (instruction.opcode)
    {
    case Opcode::SetFieldConstant:
    case Opcode::AddIntegerConstant:
    case Opcode::AddDecimalConstant:
        out.U32(instruction.field.value);
        out.U8(instruction.operandFromPayload ? 1U : 0U);
        if (!instruction.operandFromPayload)
        {
            out.Value(instruction.operand);
        }
        break;
    case Opcode::TransitionLifecycle:
        out.U8(static_cast<std::uint8_t>(instruction.lifecycle));
        break;
    case Opcode::CreateEntity:
        out.U64(instruction.entityDefinition.value);
        break;
    case Opcode::SetComponentFieldConstant:
        out.U64(instruction.entity.value);
        out.U64(instruction.component.value);
        out.U32(instruction.componentField.value);
        out.Value(instruction.operand);
        break;
    case Opcode::AddRelation:
        out.U64(instruction.relationType.value);
        out.U64(instruction.sourceEntity.value);
        out.U64(instruction.targetEntity.value);
        break;
    case Opcode::RemoveRelation:
        out.U64(instruction.relation.value);
        break;
    case Opcode::SpawnMechanism:
        out.U64(instruction.spawn.value);
        break;
    case Opcode::ScheduleEvent:
        out.U64(instruction.eventType.value);
        out.U64(instruction.dueTickOffset);
        out.I32(instruction.priority);
        out.Value(instruction.payload);
        break;
    case Opcode::CancelEvent:
        out.U64(instruction.eventSequence);
        break;
    case Opcode::CreateRngStream:
        out.U64(instruction.rngStream.value);
        out.U64(instruction.rngSeed);
        break;
    case Opcode::AdvanceRngStream:
        out.U64(instruction.rngStream.value);
        out.U64(instruction.rngCount);
        break;
    case Opcode::SetFieldComputed:
    case Opcode::AddFieldComputed:
        out.U32(instruction.field.value);
        EncodeReadPath(out, instruction.left);
        out.U8(instruction.hasRight ? 1U : 0U);
        if (instruction.hasRight)
        {
            out.U8(static_cast<std::uint8_t>(instruction.binaryOperator));
            EncodeReadPath(out, instruction.right);
        }
        break;
    case Opcode::InvokeCapability:
        out.U64(instruction.capability.value);
        out.U64(instruction.capabilityDeliveryType.value);
        out.U64(instruction.dueTickOffset);
        out.I32(instruction.priority);
        out.U32(instruction.targetRoleSlot.value);
        out.U32(instruction.capabilityVersion);
        out.U8(instruction.operandFromPayload ? 1U : 0U);
        out.Value(instruction.payload);
        break;
    }
}

// Optionals are encoded presence-first so that "absent" and "present with a
// default-looking value" never collide.
void EncodeFieldSchema(Encoder& out, const kernel::MechanismFieldSchema& field)
{
    out.Text(field.name);
    out.U8(static_cast<std::uint8_t>(field.kind));
    out.U8(field.required ? 1U : 0U);
    out.U8(field.defaultValue.has_value() ? 1U : 0U);
    if (field.defaultValue.has_value())
    {
        out.Value(*field.defaultValue);
    }
}

// Walks the catalog in its own stable order: Definitions() is the compiler's
// ordered vector, and each program is reached through it, so nothing here
// depends on map iteration of unordered keys.
void EncodeCatalog(Encoder& out, const kernel::FrozenRuntimeCatalog& catalog)
{
    out.Text("dillen.dsl.compile.v1");
    out.U32(static_cast<std::uint32_t>(catalog.LayoutCount()));
    out.U32(static_cast<std::uint32_t>(catalog.DefinitionCount()));
    out.U32(static_cast<std::uint32_t>(catalog.ComponentLayoutCount()));
    out.U32(static_cast<std::uint32_t>(catalog.EntityDefinitionCount()));
    out.U32(static_cast<std::uint32_t>(catalog.RelationLayoutCount()));
    out.U32(static_cast<std::uint32_t>(catalog.RelationDefinitionCount()));
    out.U32(static_cast<std::uint32_t>(catalog.SpawnDefinitionCount()));
    out.U32(static_cast<std::uint32_t>(catalog.AlgorithmProgramCount()));
    out.U32(static_cast<std::uint32_t>(
        catalog.ControlledScriptProgramCount()));

    for (const kernel::CompiledMechanismDefinition& definition
        : catalog.Definitions())
    {
        out.U64(definition.id.value);
        out.U64(definition.type.value);
        out.U32(definition.schemaVersion);
        out.U64(definition.algorithm.value);
        out.U32(definition.algorithmVersion);

        // Slot layout: the order of these is the slot assignment, which is
        // exactly what must not drift under the author's feet.
        const kernel::CompiledMechanismLayout* layout = catalog.FindLayout(
            definition.type,
            definition.schemaVersion
        );
        out.U8(layout == nullptr ? 0U : 1U);
        if (layout != nullptr)
        {
            out.U32(static_cast<std::uint32_t>(layout->fields.size()));
            for (const kernel::MechanismFieldSchema& field : layout->fields)
            {
                EncodeFieldSchema(out, field);
            }
            out.U32(static_cast<std::uint32_t>(layout->roles.size()));
            for (const kernel::MechanismRoleSchema& role : layout->roles)
            {
                out.Text(role.name);
                out.U8(static_cast<std::uint8_t>(role.referenceKind));
                out.U64(static_cast<std::uint64_t>(role.minimumCount));
                out.U8(role.maximumCount.has_value() ? 1U : 0U);
                if (role.maximumCount.has_value())
                {
                    out.U64(static_cast<std::uint64_t>(*role.maximumCount));
                }
            }
            out.U32(static_cast<std::uint32_t>(
                layout->fieldSlotsByName.size()));
            for (const auto& entry : layout->fieldSlotsByName)
            {
                out.Text(entry.first);
                out.U32(entry.second.value);
            }
            out.U32(static_cast<std::uint32_t>(
                layout->roleSlotsByName.size()));
            for (const auto& entry : layout->roleSlotsByName)
            {
                out.Text(entry.first);
                out.U32(entry.second.value);
            }
        }

        out.U32(static_cast<std::uint32_t>(definition.initialValues.size()));
        for (const kernel::MechanismValue& value : definition.initialValues)
        {
            out.Value(value);
        }
        out.U32(static_cast<std::uint32_t>(
            definition.providedCapabilities.size()));
        for (const kernel::CapabilityProvision& provision
            : definition.providedCapabilities)
        {
            out.U64(provision.capability.value);
            out.U32(provision.version);
        }

        const kernel::CompiledAlgorithmProgram* program =
            catalog.FindAlgorithmProgram(definition.id);
        out.U8(program == nullptr ? 0U : 1U);
        if (program != nullptr)
        {
            out.U32(static_cast<std::uint32_t>(program->stages.size()));
            for (const auto& stage : program->stages)
            {
                out.U32(static_cast<std::uint32_t>(stage.first));
                out.U32(static_cast<std::uint32_t>(stage.second.size()));
                for (const kernel::AlgorithmBytecodeInstruction& instruction
                    : stage.second)
                {
                    EncodeInstruction(out, instruction);
                }
            }
        }
    }

    for (const kernel::CompiledEntityDefinition& entity
        : catalog.EntityDefinitions())
    {
        out.U64(entity.id.value);
        out.U64(entity.type.value);
        out.U32(static_cast<std::uint32_t>(entity.components.size()));
        for (const kernel::CompiledEntityComponentDefinition& component
            : entity.components)
        {
            out.U64(component.type.value);
            out.U32(component.schemaVersion);
            out.U32(static_cast<std::uint32_t>(
                component.initialValues.size()));
            for (const kernel::MechanismValue& value
                : component.initialValues)
            {
                out.Value(value);
            }
        }
    }

    for (const kernel::CompiledRelationDefinition& relation
        : catalog.RelationDefinitions())
    {
        out.U64(relation.id.value);
        out.U64(relation.type.value);
        out.U32(relation.schemaVersion);
        out.U64(relation.source.value);
        out.U64(relation.target.value);
    }

    for (const kernel::CompiledMechanismSpawnDefinition& spawn
        : catalog.SpawnDefinitions())
    {
        out.U64(spawn.id.value);
        out.U64(spawn.definition.value);
        out.U32(spawn.count);
        out.U32(static_cast<std::uint32_t>(spawn.initialValues.size()));
        for (const kernel::MechanismValue& value : spawn.initialValues)
        {
            out.Value(value);
        }
    }
}

}

int main()
{
    const std::string rootName = "dillen.dsl.root";
    authoring::AuthoringLaunchSelection selection;
    selection.root = {kernel::StableRulesetId(rootName), rootName, 1};
    authoring::AuthoringSession session(std::move(selection));

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!session.Register(templates, parsers, resolver))
    {
        std::cerr << "DSL compile golden: frontend registration failed\n";
        return 1;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    if (!fileCatalog.AddLayer({
            1,
            "dillen_dsl_v1",
            std::filesystem::path("Project-Dillen/tests/fixtures/dillen_dsl_v1"),
            0,
            {}
        })
        || !fileCatalog.Build(templates, diagnostics))
    {
        std::cerr << "DSL compile golden: source catalog failed\n";
        return 2;
    }

    parser::ParseWorkspace workspace;
    if (!fileCatalog.Parse(parsers, workspace, diagnostics)
        || !resolver.Resolve(workspace, diagnostics))
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "DSL compile golden: parse/resolve failed\n";
        return 3;
    }

    const kernel::FrozenRuntimeCatalog& catalog = session.RuntimeCatalog();
    if (!catalog.IsFrozen())
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "DSL compile golden: catalog is not frozen\n";
        return 4;
    }

    Encoder encoder;
    EncodeCatalog(encoder, catalog);
    const std::vector<std::uint8_t>& bytes = encoder.Bytes();
    const std::uint64_t checksum = persistence::StableRuntimeChecksum(bytes);

    // Both metrics are asserted. Twice now in this codebase an injected defect
    // has kept the byte count and moved only the checksum.
    constexpr std::size_t kGoldenBytes = 2377;
    constexpr std::uint64_t kGoldenChecksum = 13272956740390339094ULL;
    if (bytes.size() != kGoldenBytes || checksum != kGoldenChecksum)
    {
        std::cerr << "DSL compile output drifted:\n"
                  << "  bytes    : " << bytes.size()
                  << " (expected " << kGoldenBytes << ")\n"
                  << "  checksum : " << checksum
                  << " (expected " << kGoldenChecksum << ")\n"
                  << "An accidental change is a bug to fix. A deliberate one\n"
                  << "is a change to the language external Packages are\n"
                  << "written against: it needs a DSL version bump and a\n"
                  << "documented migration for existing content, not a\n"
                  << "re-baselined number here.\n";
        return 5;
    }

    std::cout << "Authoring DSL compile golden: passed ("
              << catalog.DefinitionCount() << " definitions, "
              << catalog.AlgorithmProgramCount() << " declarative programs, "
              << catalog.ControlledScriptProgramCount() << " script programs, "
              << bytes.size() << " canonical bytes)\n";
    return 0;
}
