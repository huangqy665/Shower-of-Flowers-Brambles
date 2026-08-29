#include "initial_world_builder.hpp"

#include <utility>

namespace dillen::world {

namespace {

void AddIssue(
    InitialWorldBuildReport& report,
    InitialWorldBuildIssueCode code,
    std::uint64_t subject,
    std::string message
)
{
    report.issues.push_back({
        code,
        std::to_string(subject),
        std::move(message)
    });
}

}

bool InitialWorldBuildReport::Success() const noexcept
{
    return issues.empty();
}

bool InitialWorldBuilder::Build(
    const kernel::FrozenRuntimeCatalog& catalog,
    AuthoritativeWorld& output,
    InitialWorldBuildReport& report
) const
{
    report = {};
    if (!catalog.IsFrozen())
    {
        AddIssue(
            report,
            InitialWorldBuildIssueCode::RuntimeCatalogNotFrozen,
            0,
            "Frozen Runtime Catalog is required"
        );
        return false;
    }

    EntityRegistry entities;
    ComponentStore components;
    RelationIndex relations;
    kernel::MechanismInstanceStore mechanisms;
    for (const kernel::CompiledEntityDefinition& definition
        : catalog.EntityDefinitions())
    {
        kernel::EntityId entity;
        if (entities.CreateFromDefinition(
                definition.id,
                catalog,
                entity) != EntityCreateResult::Created)
        {
            AddIssue(
                report,
                InitialWorldBuildIssueCode::EntityCreationFailed,
                definition.id.value,
                "Compiled Entity Definition could not be instantiated"
            );
            return false;
        }
        for (const kernel::CompiledEntityComponentDefinition& component
            : definition.components)
        {
            if (components.Attach(entity, component, entities)
                != ComponentAttachResult::Attached)
            {
                AddIssue(
                    report,
                    InitialWorldBuildIssueCode::ComponentAttachmentFailed,
                    component.type.value,
                    "Compiled Component could not be attached"
                );
                return false;
            }
        }
    }
    for (const kernel::CompiledRelationDefinition& definition
        : catalog.RelationDefinitions())
    {
        kernel::RelationId relation;
        if (relations.Add(
                definition.type,
                kernel::StableEntityId(definition.source),
                kernel::StableEntityId(definition.target),
                entities,
                catalog,
                relation) != RelationAddResult::Added)
        {
            AddIssue(
                report,
                InitialWorldBuildIssueCode::RelationCreationFailed,
                definition.id.value,
                "Compiled Relation Definition could not be instantiated"
            );
            return false;
        }
    }
    for (const kernel::CompiledMechanismSpawnDefinition& spawn
        : catalog.SpawnDefinitions())
    {
        for (std::uint32_t index = 0; index < spawn.count; ++index)
        {
            kernel::MechanismInstanceId instance;
            if (mechanisms.CreateFromSpawn(
                    spawn.id,
                    catalog,
                    0,
                    instance)
                != kernel::MechanismInstanceCreateResult::Created)
            {
                AddIssue(
                    report,
                    InitialWorldBuildIssueCode::MechanismSpawnFailed,
                    spawn.id.value,
                    "Compiled Mechanism Spawn could not be instantiated"
                );
                return false;
            }
        }
    }
    output = AuthoritativeWorld(
        std::move(entities),
        std::move(components),
        std::move(relations),
        std::move(mechanisms)
    );
    return true;
}

}
