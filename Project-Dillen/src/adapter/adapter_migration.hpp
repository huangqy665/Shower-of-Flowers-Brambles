#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "projection_artifact.hpp"

namespace dillen::adapter {

using AdapterMigrationTransform = std::function<bool(
    const ProjectionArtifact&,
    ProjectionArtifact&,
    std::string&
)>;

struct AdapterMigrationStep
{
    std::string canonicalName;
    ProjectionContract source;
    ProjectionContract target;
    AdapterMigrationTransform transform;
};

enum class AdapterMigrationRegisterResult
{
    Added,
    InvalidStep,
    DuplicateStep,
    Frozen
};

enum class AdapterMigrationStatus
{
    Completed,
    RegistryNotFrozen,
    SourceInvalid,
    TargetInvalid,
    PathMissing,
    PathAmbiguous,
    StepRejected,
    StepOutputInvalid
};

struct AdapterMigrationReport
{
    AdapterMigrationStatus status = AdapterMigrationStatus::Completed;
    std::vector<std::string> appliedSteps;
    std::string message;

    explicit operator bool() const noexcept;
};

class AdapterMigrationRegistry
{
public:
    AdapterMigrationRegisterResult Register(AdapterMigrationStep step);
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;

    AdapterMigrationReport Migrate(
        const ProjectionArtifact& source,
        const ProjectionContract& target,
        ProjectionArtifact& output
    ) const;

private:
    std::vector<AdapterMigrationStep> steps_;
    bool frozen_ = false;
};

}
