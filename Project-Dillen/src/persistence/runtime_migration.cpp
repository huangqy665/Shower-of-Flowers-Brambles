#include "runtime_migration.hpp"

#include <set>
#include <utility>

namespace dillen::persistence {

namespace {

bool SameVersion(
    kernel::PackageVersion first,
    kernel::PackageVersion second
) noexcept
{
    return first == second;
}

bool SameCapabilityProvision(
    const kernel::CapabilityProvision& first,
    const kernel::CapabilityProvision& second
) noexcept
{
    return first.capability == second.capability
        && first.canonicalName == second.canonicalName
        && first.version == second.version;
}

bool SamePackageLockEntry(
    const kernel::PackageLockEntry& first,
    const kernel::PackageLockEntry& second
) noexcept
{
    if (first.package != second.package
        || first.canonicalName != second.canonicalName
        || !SameVersion(first.version, second.version)
        || first.contentDigest != second.contentDigest
        || first.loadIndex != second.loadIndex
        || first.dependencies.size() != second.dependencies.size()
        || first.providedCapabilities.size()
            != second.providedCapabilities.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < first.dependencies.size(); ++index)
    {
        if (first.dependencies[index].package
                != second.dependencies[index].package
            || !SameVersion(
                first.dependencies[index].version,
                second.dependencies[index].version))
        {
            return false;
        }
    }
    for (std::size_t index = 0;
        index < first.providedCapabilities.size();
        ++index)
    {
        if (!SameCapabilityProvision(
                first.providedCapabilities[index],
                second.providedCapabilities[index]))
        {
            return false;
        }
    }
    return true;
}

bool SameExtension(
    const kernel::AppliedRulesetExtension& first,
    const kernel::AppliedRulesetExtension& second
) noexcept
{
    return first.id == second.id
        && first.canonicalName == second.canonicalName
        && first.version == second.version
        && first.priority == second.priority;
}

}

RuntimeMigrationReport::operator bool() const noexcept
{
    return status == RuntimeMigrationStatus::NotRequired
        || status == RuntimeMigrationStatus::Migrated;
}

bool SameRuntimeSaveIdentity(
    const RuntimeSaveIdentity& first,
    const RuntimeSaveIdentity& second
) noexcept
{
    if (first.formatVersion != second.formatVersion
        || first.ruleset != second.ruleset
        || first.rulesetVersion != second.rulesetVersion
        || first.rulesetFingerprint != second.rulesetFingerprint
        || first.rulesetExtensions.size()
            != second.rulesetExtensions.size()
        || first.packageLock.size() != second.packageLock.size()
        || first.sourceLock != second.sourceLock)
    {
        return false;
    }
    for (std::size_t index = 0;
        index < first.rulesetExtensions.size();
        ++index)
    {
        if (!SameExtension(
                first.rulesetExtensions[index],
                second.rulesetExtensions[index]))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < first.packageLock.size(); ++index)
    {
        if (!SamePackageLockEntry(
                first.packageLock[index],
                second.packageLock[index]))
        {
            return false;
        }
    }
    return true;
}

RuntimeMigrationRegistry::SourceKey RuntimeMigrationRegistry::Key(
    const RuntimeSaveIdentity& source
) noexcept
{
    return {
        source.formatVersion,
        {
            source.rulesetFingerprint.high,
            source.rulesetFingerprint.low
        }
    };
}

RuntimeMigrationRegisterResult RuntimeMigrationRegistry::Register(
    RuntimeMigrationStep step
)
{
    if (frozen_)
    {
        return RuntimeMigrationRegisterResult::Frozen;
    }
    if (step.canonicalName.empty()
        || !step.source.rulesetFingerprint
        || !step.target.rulesetFingerprint
        || !step.target.ruleset
        || step.target.rulesetVersion == 0
        || step.target.formatVersion < step.source.formatVersion
        || !step.migrate
        || (step.source.formatVersion == step.target.formatVersion
            && step.source.rulesetFingerprint
                == step.target.rulesetFingerprint))
    {
        return RuntimeMigrationRegisterResult::InvalidStep;
    }
    const SourceKey key = Key(step.source);
    if (indexBySource_.find(key) != indexBySource_.end())
    {
        return RuntimeMigrationRegisterResult::DuplicateSource;
    }
    indexBySource_[key] = steps_.size();
    steps_.push_back(std::move(step));
    return RuntimeMigrationRegisterResult::Added;
}

void RuntimeMigrationRegistry::Freeze()
{
    frozen_ = true;
}

bool RuntimeMigrationRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RuntimeMigrationRegistry::Size() const noexcept
{
    return steps_.size();
}

RuntimeMigrationReport RuntimeMigrationRegistry::Migrate(
    RuntimeSaveImage& image,
    const RuntimeSaveIdentity& target
) const
{
    RuntimeMigrationReport report;
    if (SameRuntimeSaveIdentity(image.identity, target))
    {
        return report;
    }
    if (!frozen_)
    {
        report.status = RuntimeMigrationStatus::RegistryNotFrozen;
        report.message = "Runtime Migration Registry is not frozen";
        return report;
    }

    std::set<SourceKey> visited;
    while (!SameRuntimeSaveIdentity(image.identity, target))
    {
        const SourceKey source = Key(image.identity);
        if (!visited.insert(source).second)
        {
            report.status = RuntimeMigrationStatus::CycleDetected;
            report.message = "Runtime migration path contains a cycle";
            return report;
        }
        const auto iterator = indexBySource_.find(source);
        if (iterator == indexBySource_.end())
        {
            report.status = RuntimeMigrationStatus::PathMissing;
            report.message = "No migration step accepts the save identity";
            return report;
        }
        const RuntimeMigrationStep& step = steps_[iterator->second];
        if (!SameRuntimeSaveIdentity(image.identity, step.source))
        {
            report.status = RuntimeMigrationStatus::PathMissing;
            report.message =
                "Migration source Package Lock or Source Lock does not match";
            return report;
        }
        RuntimeSaveImage candidate = image;
        std::string message;
        if (!step.migrate(candidate, message))
        {
            report.status = RuntimeMigrationStatus::StepRejected;
            report.message = step.canonicalName + ": " + message;
            return report;
        }
        candidate.identity = step.target;
        image = std::move(candidate);
        report.appliedSteps.push_back(step.canonicalName);
        if (report.appliedSteps.size() > steps_.size())
        {
            report.status = RuntimeMigrationStatus::CycleDetected;
            report.message = "Runtime migration path exceeded registry size";
            return report;
        }
    }
    report.status = RuntimeMigrationStatus::Migrated;
    return report;
}

}
