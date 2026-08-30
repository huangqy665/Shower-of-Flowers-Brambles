#include "adapter_migration.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "mechanism_ids.hpp"

namespace dillen::adapter {

namespace {

void FindPaths(
    const std::vector<AdapterMigrationStep>& steps,
    const ProjectionContract& current,
    const ProjectionContract& target,
    std::set<ProjectionContract>& visited,
    std::vector<const AdapterMigrationStep*>& path,
    std::vector<std::vector<const AdapterMigrationStep*>>& paths
)
{
    if (paths.size() > 1) return;
    if (current == target)
    {
        paths.push_back(path);
        return;
    }
    if (!visited.insert(current).second) return;
    for (const AdapterMigrationStep& step : steps)
    {
        if (step.source != current) continue;
        path.push_back(&step);
        FindPaths(steps, step.target, target, visited, path, paths);
        path.pop_back();
        if (paths.size() > 1) break;
    }
    visited.erase(current);
}

AdapterMigrationReport Failure(
    AdapterMigrationStatus status,
    std::string message
)
{
    AdapterMigrationReport report;
    report.status = status;
    report.message = std::move(message);
    return report;
}

}

AdapterMigrationReport::operator bool() const noexcept
{
    return status == AdapterMigrationStatus::Completed;
}

AdapterMigrationRegisterResult AdapterMigrationRegistry::Register(
    AdapterMigrationStep step
)
{
    if (frozen_) return AdapterMigrationRegisterResult::Frozen;
    if (!kernel::IsValidMechanismSymbol(step.canonicalName)
        || step.canonicalName
            != kernel::NormalizeMechanismSymbol(step.canonicalName)
        || !IsValidProjectionContract(step.source)
        || !IsValidProjectionContract(step.target)
        || step.source == step.target
        || !step.transform)
    {
        return AdapterMigrationRegisterResult::InvalidStep;
    }
    if (std::any_of(
            steps_.begin(),
            steps_.end(),
            [&step](const AdapterMigrationStep& existing)
            {
                return existing.canonicalName == step.canonicalName
                    || (existing.source == step.source
                        && existing.target == step.target);
            }))
    {
        return AdapterMigrationRegisterResult::DuplicateStep;
    }
    steps_.push_back(std::move(step));
    return AdapterMigrationRegisterResult::Added;
}

void AdapterMigrationRegistry::Freeze()
{
    std::sort(
        steps_.begin(),
        steps_.end(),
        [](const AdapterMigrationStep& first,
           const AdapterMigrationStep& second)
        {
            if (first.source != second.source)
                return first.source < second.source;
            if (first.target != second.target)
                return first.target < second.target;
            return first.canonicalName < second.canonicalName;
        }
    );
    frozen_ = true;
}

bool AdapterMigrationRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t AdapterMigrationRegistry::Size() const noexcept
{
    return steps_.size();
}

AdapterMigrationReport AdapterMigrationRegistry::Migrate(
    const ProjectionArtifact& source,
    const ProjectionContract& target,
    ProjectionArtifact& output
) const
{
    output = {};
    if (!frozen_)
    {
        return Failure(
            AdapterMigrationStatus::RegistryNotFrozen,
            "Adapter Migration Registry must be frozen"
        );
    }
    std::string validation;
    if (!ValidateProjectionArtifact(source, validation))
    {
        return Failure(AdapterMigrationStatus::SourceInvalid, validation);
    }
    if (!IsValidProjectionContract(target))
    {
        return Failure(
            AdapterMigrationStatus::TargetInvalid,
            "Adapter Migration target contract is invalid"
        );
    }

    std::vector<std::vector<const AdapterMigrationStep*>> paths;
    std::vector<const AdapterMigrationStep*> path;
    std::set<ProjectionContract> visited;
    FindPaths(
        steps_,
        ContractOf(source.identity),
        target,
        visited,
        path,
        paths
    );
    if (paths.empty())
    {
        return Failure(
            AdapterMigrationStatus::PathMissing,
            "No Adapter Migration path reaches the requested contract"
        );
    }
    if (paths.size() != 1)
    {
        return Failure(
            AdapterMigrationStatus::PathAmbiguous,
            "More than one Adapter Migration path reaches the target"
        );
    }

    ProjectionArtifact current = source;
    AdapterMigrationReport report;
    for (const AdapterMigrationStep* step : paths.front())
    {
        ProjectionArtifact migrated;
        std::string message;
        if (!step->transform(current, migrated, message))
        {
            report.status = AdapterMigrationStatus::StepRejected;
            report.message = message.empty()
                ? "Adapter Migration transform rejected its input"
                : std::move(message);
            return report;
        }
        if (!(migrated.identity.corpus == source.identity.corpus)
            || ContractOf(migrated.identity) != step->target
            || !ValidateProjectionArtifact(migrated, message))
        {
            report.status = AdapterMigrationStatus::StepOutputInvalid;
            report.message = message.empty()
                ? "Adapter Migration produced an invalid identity transition"
                : std::move(message);
            return report;
        }
        report.appliedSteps.push_back(step->canonicalName);
        current = std::move(migrated);
    }
    output = std::move(current);
    return report;
}

}
