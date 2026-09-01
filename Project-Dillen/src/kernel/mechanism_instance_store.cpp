#include "mechanism_instance_store.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

#include "fixed_point.hpp"
#include "sorted_id_index.hpp"

namespace dillen::kernel {

namespace {

const std::vector<MechanismInstanceId>& EmptyInstanceIds()
{
    static const std::vector<MechanismInstanceId> empty;
    return empty;
}

MechanismTransactionResult TransactionFailure(
    MechanismTransactionStatus status,
    std::size_t commandIndex,
    MechanismInstanceId target
)
{
    return {status, commandIndex, target, 0, {}};
}

}

MechanismInstanceCreateResult MechanismInstanceStore::CreateFromDefinition(
    MechanismDefinitionId definitionId,
    const FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick,
    MechanismInstanceId& outputId
)
{
    outputId = {};
    if (!catalog.IsFrozen())
    {
        return MechanismInstanceCreateResult::RuntimeCatalogNotFrozen;
    }
    const CompiledMechanismDefinition* definition =
        catalog.FindDefinition(definitionId);
    if (definition == nullptr)
    {
        return MechanismInstanceCreateResult::DefinitionMissing;
    }

    const std::uint64_t creationOrdinal =
        Mutable().nextOrdinalByDefinition[definitionId];
    const MechanismInstanceId instanceId = StableMechanismInstanceId(
        definitionId,
        creationOrdinal
    );
    if (Read().instances.find(instanceId) != Read().instances.end())
    {
        return MechanismInstanceCreateResult::IdCollision;
    }

    const CompiledMechanismLayout* layout = catalog.FindLayout(
        definition->type,
        definition->schemaVersion
    );
    if (layout == nullptr)
    {
        return MechanismInstanceCreateResult::LayoutMissing;
    }

    // Every required role slot must be filled.
    //
    // The Definition registry deliberately lets a Mechanism-Instance role go
    // unfilled, because a Definition cannot name an instance; the Spawn
    // registry enforces the minimum instead. That leaves this entry point --
    // creating straight from a Definition, with no Spawn anywhere -- as a way
    // to reach a live instance whose required roles are empty, which every
    // read path and directed Capability call would then Fault on.
    //
    // So the same rule is applied here. It is the last door into instance
    // creation that was not checking it.
    for (std::size_t slot = 0; slot < layout->roles.size(); ++slot)
    {
        const MechanismRoleSchema& roleSchema = layout->roles[slot];
        const std::size_t bound = slot < definition->initialRoles.size()
            ? definition->initialRoles[slot].size()
            : 0;
        if (bound < roleSchema.minimumCount)
        {
            return MechanismInstanceCreateResult::RoleBindingMissing;
        }
    }

    MechanismInstance instance;
    instance.id = instanceId;
    instance.definition = definitionId;
    instance.type = definition->type;
    instance.schemaVersion = definition->schemaVersion;
    instance.algorithm = definition->algorithm;
    instance.algorithmVersion = definition->algorithmVersion;
    instance.creationOrdinal = creationOrdinal;
    instance.values = definition->initialValues;
    instance.roles = definition->initialRoles;
    const AlgorithmDescriptor* algorithm = definition->algorithm
        ? catalog.FindAlgorithm(
            definition->algorithm,
            definition->algorithmVersion)
        : nullptr;
    instance.algorithmInitialized = algorithm == nullptr
        || !HasAlgorithmEntryPoint(
            algorithm->entryPoints,
            AlgorithmEntryPoint::Create
        );
    if (algorithm != nullptr && algorithm->backend == AlgorithmBackend::Script)
    {
        const CompiledControlledScriptProgram* script =
            catalog.FindControlledScriptProgram(definitionId);
        if (script == nullptr)
        {
            return MechanismInstanceCreateResult::DefinitionMissing;
        }
        instance.algorithmState = script->initialState;
    }
    instance.createdTick = currentTick;
    instance.updatedTick = currentTick;

    Data& data = Mutable();
    data.instances.emplace(instanceId, std::move(instance));
    InsertSortedId(data.instancesByDefinition[definitionId], instanceId);
    InsertSortedId(data.instancesByType[definition->type], instanceId);
    data.nextOrdinalByDefinition[definitionId] = creationOrdinal + 1;
    outputId = instanceId;
    return MechanismInstanceCreateResult::Created;
}

MechanismInstanceCreateResult MechanismInstanceStore::CreateFromSpawn(
    MechanismSpawnDefinitionId spawnId,
    const FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick,
    MechanismInstanceId& outputId
)
{
    outputId = {};
    if (!catalog.IsFrozen())
    {
        return MechanismInstanceCreateResult::RuntimeCatalogNotFrozen;
    }
    const CompiledMechanismSpawnDefinition* spawn =
        catalog.FindSpawnDefinition(spawnId);
    if (spawn == nullptr)
    {
        return MechanismInstanceCreateResult::SpawnMissing;
    }
    const CompiledMechanismDefinition* definition =
        catalog.FindDefinition(spawn->definition);
    if (definition == nullptr)
    {
        return MechanismInstanceCreateResult::DefinitionMissing;
    }

    const std::uint64_t creationOrdinal =
        Mutable().nextOrdinalByDefinition[spawn->definition];
    const MechanismInstanceId instanceId = StableMechanismInstanceId(
        spawn->definition,
        creationOrdinal
    );
    if (Read().instances.find(instanceId) != Read().instances.end())
    {
        return MechanismInstanceCreateResult::IdCollision;
    }

    MechanismInstance instance;
    instance.id = instanceId;
    instance.definition = spawn->definition;
    instance.type = definition->type;
    instance.schemaVersion = definition->schemaVersion;
    instance.algorithm = definition->algorithm;
    instance.algorithmVersion = definition->algorithmVersion;
    instance.creationOrdinal = creationOrdinal;
    instance.values = spawn->initialValues;
    instance.roles = spawn->initialRoles;
    const AlgorithmDescriptor* algorithm = definition->algorithm
        ? catalog.FindAlgorithm(
            definition->algorithm,
            definition->algorithmVersion)
        : nullptr;
    instance.algorithmInitialized = algorithm == nullptr
        || !HasAlgorithmEntryPoint(
            algorithm->entryPoints,
            AlgorithmEntryPoint::Create
        );
    if (algorithm != nullptr && algorithm->backend == AlgorithmBackend::Script)
    {
        const CompiledControlledScriptProgram* script =
            catalog.FindControlledScriptProgram(spawn->definition);
        if (script == nullptr)
        {
            return MechanismInstanceCreateResult::DefinitionMissing;
        }
        instance.algorithmState = script->initialState;
    }
    instance.createdTick = currentTick;
    instance.updatedTick = currentTick;

    Data& data = Mutable();
    data.instances.emplace(instanceId, std::move(instance));
    InsertSortedId(
        data.instancesByDefinition[spawn->definition],
        instanceId
    );
    InsertSortedId(data.instancesByType[definition->type], instanceId);
    data.nextOrdinalByDefinition[spawn->definition] = creationOrdinal + 1;
    outputId = instanceId;
    return MechanismInstanceCreateResult::Created;
}

MechanismTransactionResult MechanismInstanceStore::ApplyTransaction(
    const std::vector<MechanismCommand>& commands,
    const FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick
)
{
    if (!catalog.IsFrozen())
    {
        return TransactionFailure(
            MechanismTransactionStatus::RuntimeCatalogNotFrozen,
            0,
            {}
        );
    }

    std::map<MechanismInstanceId, MechanismInstance> staged;
    std::set<MechanismInstanceId> changed;
    std::set<MechanismInstanceId> destroyed;
    std::vector<MechanismChange> changes;
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
        const MechanismCommand& command = commands[index];
        if (destroyed.find(command.target) != destroyed.end())
        {
            return TransactionFailure(
                MechanismTransactionStatus::TargetDestroyed,
                index,
                command.target
            );
        }
        auto stagedIterator = staged.find(command.target);
        if (stagedIterator == staged.end())
        {
            const auto storedIterator =
                Read().instances.find(command.target);
            if (storedIterator == Read().instances.end())
            {
                return TransactionFailure(
                    MechanismTransactionStatus::TargetMissing,
                    index,
                    command.target
                );
            }
            stagedIterator = staged.emplace(
                command.target,
                storedIterator->second
            ).first;
        }

        MechanismInstance& instance = stagedIterator->second;
        if (currentTick < instance.updatedTick)
        {
            return TransactionFailure(
                MechanismTransactionStatus::TickRegression,
                index,
                command.target
            );
        }
        const CompiledMechanismDefinition* definition =
            catalog.FindDefinition(instance.definition);
        if (definition == nullptr)
        {
            return TransactionFailure(
                MechanismTransactionStatus::DefinitionMissing,
                index,
                command.target
            );
        }
        if (definition->type != instance.type
            || definition->schemaVersion != instance.schemaVersion
            || definition->algorithm != instance.algorithm
            || definition->algorithmVersion != instance.algorithmVersion)
        {
            return TransactionFailure(
                MechanismTransactionStatus::InstanceDefinitionMismatch,
                index,
                command.target
            );
        }
        const CompiledMechanismLayout* layout = catalog.FindLayout(
            instance.type,
            instance.schemaVersion
        );
        if (layout == nullptr)
        {
            return TransactionFailure(
                MechanismTransactionStatus::LayoutMissing,
                index,
                command.target
            );
        }

        if (const auto* operation =
            std::get_if<MechanismSetFieldOperation>(&command.operation))
        {
            if (!operation->field
                || operation->field.value >= layout->fields.size()
                || operation->field.value >= instance.values.size())
            {
                return TransactionFailure(
                    MechanismTransactionStatus::UnknownField,
                    index,
                    command.target
                );
            }
            const MechanismFieldSchema& field =
                layout->fields[operation->field.value];
            if (!MechanismValueMatchesSchema(field, operation->value))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::FieldValueInvalid,
                    index,
                    command.target
                );
            }
            MechanismValue& storedValue =
                instance.values[operation->field.value];
            if (storedValue != operation->value)
            {
                changes.emplace_back(MechanismFieldChange{
                    command.target,
                    operation->field,
                    storedValue,
                    operation->value
                });
                storedValue = operation->value;
                changed.insert(command.target);
            }
            continue;
        }

        if (const auto* operation =
            std::get_if<MechanismAddFieldOperation>(&command.operation))
        {
            // Read-modify-write against the COMMITTED value, which is the
            // whole point: several deltas landing in one phase accumulate
            // instead of overwriting one another.
            if (!operation->field
                || operation->field.value >= layout->fields.size()
                || operation->field.value >= instance.values.size())
            {
                return TransactionFailure(
                    MechanismTransactionStatus::UnknownField,
                    index,
                    command.target
                );
            }
            MechanismValue& storedValue =
                instance.values[operation->field.value];
            const auto* storedInteger =
                std::get_if<std::int64_t>(&storedValue.data);
            const auto* deltaInteger =
                std::get_if<std::int64_t>(&operation->delta.data);
            const auto* storedDecimal =
                std::get_if<double>(&storedValue.data);
            const auto* deltaDecimal =
                std::get_if<double>(&operation->delta.data);
            MechanismValue next;
            if (storedInteger != nullptr && deltaInteger != nullptr)
            {
                // Integer + integer is a checked int64 addition, NOT a trip
                // through the fixed-point pipeline.
                //
                // The internal fixed-point scale is 10^4, so IntegerToInternal
                // rejects anything past +-9.2e14 -- it has to, or the scaled
                // form overflows. Routing an integer field's delta through it
                // therefore shrank a full int64 field to about one ten-
                // thousandth of its range and rejected perfectly legal
                // additions as overflow. Decimals need the scale; integers
                // never did.
                const std::int64_t base = *storedInteger;
                const std::int64_t addend = *deltaInteger;
                if ((addend > 0
                        && base
                            > std::numeric_limits<std::int64_t>::max()
                                - addend)
                    || (addend < 0
                        && base
                            < std::numeric_limits<std::int64_t>::min()
                                - addend))
                {
                    return TransactionFailure(
                        MechanismTransactionStatus::FieldValueInvalid,
                        index,
                        command.target
                    );
                }
                next = MechanismValue(base + addend);
            }
            else if (storedDecimal != nullptr && deltaDecimal != nullptr)
            {
                const FixedPointValue base =
                    DecimalToInternal(*storedDecimal);
                const FixedPointValue addend =
                    DecimalToInternal(*deltaDecimal);
                if (!base || !addend)
                {
                    return TransactionFailure(
                        MechanismTransactionStatus::FieldValueInvalid,
                        index,
                        command.target
                    );
                }
                const FixedPointValue sum =
                    FixedAdd(base.scaled, addend.scaled);
                double stored = 0.0;
                if (!sum || !InternalToStorage(sum.scaled, stored))
                {
                    return TransactionFailure(
                        MechanismTransactionStatus::FieldValueInvalid,
                        index,
                        command.target
                    );
                }
                next = MechanismValue(stored);
            }
            else
            {
                return TransactionFailure(
                    MechanismTransactionStatus::FieldValueInvalid,
                    index,
                    command.target
                );
            }
            const MechanismFieldSchema& field =
                layout->fields[operation->field.value];
            if (!MechanismValueMatchesSchema(field, next))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::FieldValueInvalid,
                    index,
                    command.target
                );
            }
            if (storedValue != next)
            {
                changes.emplace_back(MechanismFieldChange{
                    command.target,
                    operation->field,
                    storedValue,
                    next
                });
                storedValue = next;
                changed.insert(command.target);
            }
            continue;
        }

        if (std::holds_alternative<
                MechanismCompleteAlgorithmCreateOperation>(
                command.operation))
        {
            if (!instance.algorithmInitialized)
            {
                instance.algorithmInitialized = true;
                changes.emplace_back(
                    MechanismAlgorithmInitializedChange{command.target}
                );
                changed.insert(command.target);
            }
            continue;
        }

        if (const auto* operation = std::get_if<
                MechanismReplaceAlgorithmStateOperation>(
                &command.operation))
        {
            const AlgorithmDescriptor* algorithm = instance.algorithm
                ? catalog.FindAlgorithm(
                    instance.algorithm,
                    instance.algorithmVersion)
                : nullptr;
            const CompiledControlledScriptProgram* script =
                catalog.FindControlledScriptProgram(instance.definition);
            if (algorithm == nullptr
                || algorithm->backend != AlgorithmBackend::Script
                || script == nullptr
                || !IsValidControlledScriptRuntimeState(
                    *script,
                    operation->state,
                    operation->continuations,
                    algorithm->executionPolicy.scriptMemoryLimitBytes))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::AlgorithmStateInvalid,
                    index,
                    command.target
                );
            }
            if (instance.algorithmState != operation->state
                || instance.algorithmContinuations
                    != operation->continuations)
            {
                changes.emplace_back(MechanismAlgorithmStateChange{
                    command.target,
                    instance.algorithmState,
                    operation->state,
                    instance.algorithmContinuations,
                    operation->continuations
                });
                instance.algorithmState = operation->state;
                instance.algorithmContinuations = operation->continuations;
                changed.insert(command.target);
            }
            continue;
        }

        if (const auto* operation = std::get_if<
                MechanismRecordAlgorithmFaultOperation>(
                &command.operation))
        {
            if (!IsAuthoritativeAlgorithmFaultCode(operation->code))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::FaultCodeInvalid,
                    index,
                    command.target
                );
            }
            const AlgorithmFaultState previous = instance.algorithmFault;
            instance.algorithmFault.isolated = true;
            if (instance.algorithmFault.failureCount
                != std::numeric_limits<std::uint32_t>::max())
            {
                ++instance.algorithmFault.failureCount;
            }
            instance.algorithmFault.code = operation->code;
            instance.algorithmFault.stage = operation->stage;
            instance.algorithmFault.tick = currentTick;
            if (instance.algorithmFault != previous)
            {
                changes.emplace_back(MechanismAlgorithmFaultChange{
                    command.target,
                    previous,
                    instance.algorithmFault
                });
                changed.insert(command.target);
            }
            continue;
        }

        if (std::holds_alternative<MechanismClearAlgorithmFaultOperation>(
                command.operation))
        {
            const AlgorithmFaultState previous = instance.algorithmFault;
            instance.algorithmFault = {};
            if (instance.algorithmFault != previous)
            {
                changes.emplace_back(MechanismAlgorithmFaultChange{
                    command.target,
                    previous,
                    instance.algorithmFault
                });
                changed.insert(command.target);
            }
            continue;
        }

        if (std::holds_alternative<MechanismDestroyOperation>(
                command.operation))
        {
            if (!IsTerminalMechanismLifecycleState(instance.lifecycle))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::DestroyRequiresTerminalState,
                    index,
                    command.target
                );
            }
            changes.emplace_back(MechanismDestroyedChange{
                command.target,
                instance.definition,
                instance.type
            });
            changed.insert(command.target);
            destroyed.insert(command.target);
            continue;
        }

        const auto& operation = std::get<
            MechanismTransitionLifecycleOperation>(command.operation);
        if (!CanTransitionMechanismLifecycle(
                instance.lifecycle,
                operation.target))
        {
            return TransactionFailure(
                MechanismTransactionStatus::LifecycleTransitionInvalid,
                index,
                command.target
            );
        }
        if (instance.lifecycle != operation.target)
        {
            changes.emplace_back(MechanismLifecycleChange{
                command.target,
                instance.lifecycle,
                operation.target
            });
            instance.lifecycle = operation.target;
            changed.insert(command.target);
        }
    }

    if (changed.empty())
    {
        return {
            MechanismTransactionStatus::Committed,
            commands.size(),
            {},
            0,
            std::move(changes)
        };
    }
    Data& data = Mutable();
    for (MechanismInstanceId id : changed)
    {
        MechanismInstance& instance = staged.at(id);
        if (destroyed.find(id) != destroyed.end())
        {
            auto& byDefinition = data.instancesByDefinition.at(
                instance.definition
            );
            byDefinition.erase(
                std::remove(byDefinition.begin(), byDefinition.end(), id),
                byDefinition.end()
            );
            auto& byType = data.instancesByType.at(instance.type);
            byType.erase(
                std::remove(byType.begin(), byType.end(), id),
                byType.end()
            );
            data.instances.erase(id);
            continue;
        }
        instance.updatedTick = currentTick;
        data.instances.at(id) = std::move(instance);
    }
    return {
        MechanismTransactionStatus::Committed,
        commands.size(),
        {},
        changed.size(),
        std::move(changes)
    };
}

void MechanismInstanceStore::Clear()
{
    Data& data = Mutable();
    data.instances.clear();
    data.instancesByDefinition.clear();
    data.instancesByType.clear();
    data.nextOrdinalByDefinition.clear();
}

bool MechanismInstanceStore::Empty() const noexcept
{
    return Read().instances.empty();
}

std::size_t MechanismInstanceStore::Size() const noexcept
{
    return Read().instances.size();
}

const MechanismInstance* MechanismInstanceStore::Find(
    MechanismInstanceId id
) const
{
    const auto iterator = Read().instances.find(id);
    return iterator == Read().instances.end() ? nullptr : &iterator->second;
}

const std::vector<MechanismInstanceId>&
MechanismInstanceStore::FindByDefinition(
    MechanismDefinitionId definition
) const
{
    const auto iterator = Read().instancesByDefinition.find(definition);
    return iterator == Read().instancesByDefinition.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const std::vector<MechanismInstanceId>& MechanismInstanceStore::FindByType(
    MechanismTypeId type
) const
{
    const auto iterator = Read().instancesByType.find(type);
    return iterator == Read().instancesByType.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const MechanismInstanceStore::InstanceMap&
MechanismInstanceStore::All() const noexcept
{
    return Read().instances;
}

}
