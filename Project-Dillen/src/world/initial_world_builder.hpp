#pragma once

#include <string>
#include <vector>

#include "authoritative_world.hpp"
#include "frozen_runtime_catalog.hpp"

namespace dillen::world {

enum class InitialWorldBuildIssueCode
{
    RuntimeCatalogNotFrozen,
    EntityCreationFailed,
    ComponentAttachmentFailed,
    RelationCreationFailed,
    MechanismSpawnFailed,
    ReferenceTargetMissing,
    ReferenceTypeMismatch
};

struct InitialWorldBuildIssue
{
    InitialWorldBuildIssueCode code =
        InitialWorldBuildIssueCode::RuntimeCatalogNotFrozen;
    std::string subject;
    std::string message;
};

struct InitialWorldBuildReport
{
    std::vector<InitialWorldBuildIssue> issues;

    bool Success() const noexcept;
};

class InitialWorldBuilder
{
public:
    bool Build(
        const kernel::FrozenRuntimeCatalog& catalog,
        AuthoritativeWorld& output,
        InitialWorldBuildReport& report
    ) const;
};

}
