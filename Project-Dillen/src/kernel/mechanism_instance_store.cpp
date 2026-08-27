#include "mechanism_instance_store.hpp"

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

namespace dillen::kernel {

namespace {

const std::vector<MechanismInstanceId>& EmptyInstanceIds()
{
    static const std::vector<MechanismInstanceId> empty;
    return empty;
}

const MechanismFieldSchema* FindFieldSchema(
    const MechanismSchema& schema,
    std::string_view name
)
{
    const auto iterator = std::find_if(
        schema.fields.begin(),
        schema.fields.end(),
        [name](const MechanismFieldSchema& field)
        {
            return field.name == name;
        }
    );
    return iterator == schema.fields.end() ? nullptr : &*iterator;
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
    const MechanismDefinitionRegistry& definitions,
    std::uint64_t currentTick,
    MechanismInstanceId& outputId
)
{
    outputId = {};
    if (!definitions.IsFrozen())
    {
        return MechanismInstanceCreateResult::DefinitionRegistryNotFrozen;
    }
    const MechanismDefinition* definition = definitions.Find(definitionId);
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
    instance.values = definition->fields;
    instance.roles = definition->roles;
    instance.createdTick = currentTick;
    instance.updatedTick = currentTick;

    instances_.emplace(instanceId, std::move(instance));
    instancesByDefinition_[definitionId].push_back(instanceId);
    instancesByType_[definition->type].push_back(instanceId);
    nextOrdinalByDefinition_[definitionId] = creationOrdinal + 1;
    outputId = instanceId;
    return MechanismInstanceCreateResult::Created;
}

MechanismTransactionResult MechanismInstanceStore::ApplyTransaction(
    const std::vector<MechanismCommand>& commands,
    const MechanismDefinitionRegistry& definitions,
    const MechanismSchemaRegistry& schemas,
    std::uint64_t currentTick
)
{
    if (!definitions.IsFrozen())
    {
        return TransactionFailure(
            MechanismTransactionStatus::DefinitionRegistryNotFrozen,
            0,
            {}
        );
    }
    if (!schemas.IsFrozen())
    {
        return TransactionFailure(
            MechanismTransactionStatus::SchemaRegistryNotFrozen,
            0,
            {}
        );
    }

    std::map<MechanismInstanceId, MechanismInstance> staged;
    std::set<MechanismInstanceId> changed;
    std::vector<MechanismChange> changes;
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
        const MechanismCommand& command = commands[index];
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
        const MechanismDefinition* definition = definitions.Find(
            instance.definition
        );
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
        const MechanismSchema* schema = schemas.Find(
            instance.type,
            instance.schemaVersion
        );
        if (schema == nullptr)
        {
            return TransactionFailure(
                MechanismTransactionStatus::SchemaMissing,
                index,
                command.target
            );
        }

        if (const auto* operation =
            std::get_if<MechanismSetFieldOperation>(&command.operation))
        {
            const MechanismFieldSchema* field = FindFieldSchema(
                *schema,
                operation->field
            );
            if (field == nullptr)
            {
                return TransactionFailure(
                    MechanismTransactionStatus::UnknownField,
                    index,
                    command.target
                );
            }
            if (!MechanismValueMatchesSchema(*field, operation->value))
            {
                return TransactionFailure(
                    MechanismTransactionStatus::FieldValueInvalid,
                    index,
                    command.target
                );
            }
            const auto valueIterator = instance.values.find(
                operation->field
            );
            if (valueIterator == instance.values.end()
                || valueIterator->second != operation->value)
            {
                std::optional<MechanismValue> previousValue;
                if (valueIterator != instance.values.end())
                {
                    previousValue = valueIterator->second;
                }
                changes.emplace_back(MechanismFieldChange{
                    command.target,
                    operation->field,
                    std::move(previousValue),
                    operation->value
                });
                instance.values[operation->field] = operation->value;
                changed.insert(command.target);
            }
            continue;
        }

        const auto& operation =
            std::get<MechanismTransitionLifecycleOperation>(
                command.operation
            );
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
