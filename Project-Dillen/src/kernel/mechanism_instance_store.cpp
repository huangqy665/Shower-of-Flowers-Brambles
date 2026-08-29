#include "mechanism_instance_store.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

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
        nextOrdinalByDefinition_[definitionId];
    const MechanismInstanceId instanceId = StableMechanismInstanceId(
        definitionId,
        creationOrdinal
    );
    if (instances_.find(instanceId) != instances_.end())
    {
        return MechanismInstanceCreateResult::IdCollision;
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
    instance.createdTick = currentTick;
    instance.updatedTick = currentTick;

    instances_.emplace(instanceId, std::move(instance));
    instancesByDefinition_[definitionId].push_back(instanceId);
    instancesByType_[definition->type].push_back(instanceId);
    nextOrdinalByDefinition_[definitionId] = creationOrdinal + 1;
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
        nextOrdinalByDefinition_[spawn->definition];
    const MechanismInstanceId instanceId = StableMechanismInstanceId(
        spawn->definition,
        creationOrdinal
    );
    if (instances_.find(instanceId) != instances_.end())
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
    instance.createdTick = currentTick;
    instance.updatedTick = currentTick;

    instances_.emplace(instanceId, std::move(instance));
    instancesByDefinition_[spawn->definition].push_back(instanceId);
    instancesByType_[definition->type].push_back(instanceId);
    nextOrdinalByDefinition_[spawn->definition] = creationOrdinal + 1;
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
            const auto storedIterator = instances_.find(command.target);
            if (storedIterator == instances_.end())
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

    for (MechanismInstanceId id : changed)
    {
        MechanismInstance& instance = staged.at(id);
        if (destroyed.find(id) != destroyed.end())
        {
            auto& byDefinition = instancesByDefinition_.at(
                instance.definition
            );
            byDefinition.erase(
                std::remove(byDefinition.begin(), byDefinition.end(), id),
                byDefinition.end()
            );
            auto& byType = instancesByType_.at(instance.type);
            byType.erase(
                std::remove(byType.begin(), byType.end(), id),
                byType.end()
            );
            instances_.erase(id);
            continue;
        }
        instance.updatedTick = currentTick;
        instances_.at(id) = std::move(instance);
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
    instances_.clear();
    instancesByDefinition_.clear();
    instancesByType_.clear();
    nextOrdinalByDefinition_.clear();
}

bool MechanismInstanceStore::Empty() const noexcept
{
    return instances_.empty();
}

std::size_t MechanismInstanceStore::Size() const noexcept
{
    return instances_.size();
}

const MechanismInstance* MechanismInstanceStore::Find(
    MechanismInstanceId id
) const
{
    const auto iterator = instances_.find(id);
    return iterator == instances_.end() ? nullptr : &iterator->second;
}

const std::vector<MechanismInstanceId>&
MechanismInstanceStore::FindByDefinition(
    MechanismDefinitionId definition
) const
{
    const auto iterator = instancesByDefinition_.find(definition);
    return iterator == instancesByDefinition_.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const std::vector<MechanismInstanceId>& MechanismInstanceStore::FindByType(
    MechanismTypeId type
) const
{
    const auto iterator = instancesByType_.find(type);
    return iterator == instancesByType_.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const MechanismInstanceStore::InstanceMap&
MechanismInstanceStore::All() const noexcept
{
    return instances_;
}

}
