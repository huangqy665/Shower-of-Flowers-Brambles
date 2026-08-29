#include "ruleset_fingerprint.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace dillen::kernel {

namespace {

class FingerprintWriter
{
public:
    FingerprintWriter()
        : first_(14695981039346656037ULL),
          second_(1099511628211ULL ^ 0x9E3779B97F4A7C15ULL)
    {
    }

    void Text(std::string_view text)
    {
        Unsigned(text.size());
        for (const unsigned char value : text)
        {
            Byte(value);
        }
    }

    void Unsigned(std::uint64_t value)
    {
        for (std::size_t index = 0; index < sizeof(value); ++index)
        {
            Byte(static_cast<unsigned char>(value & 0xFFU));
            value >>= 8U;
        }
    }

    RulesetFingerprint Finish() const noexcept
    {
        return {
            first_ == 0 ? 1 : first_,
            second_ == 0 ? 1 : second_
        };
    }

private:
    void Byte(unsigned char value)
    {
        first_ ^= value;
        first_ *= 1099511628211ULL;
        second_ ^= static_cast<unsigned char>(value + 0x9DU);
        second_ *= 14029467366897019727ULL;
        second_ ^= second_ >> 29U;
    }

    std::uint64_t first_;
    std::uint64_t second_;
};

}

RulesetFingerprint::operator bool() const noexcept
{
    return high != 0 || low != 0;
}

std::string RulesetFingerprint::ToHex() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
        << std::setw(16) << high
        << std::setw(16) << low;
    return stream.str();
}

bool operator==(
    RulesetFingerprint first,
    RulesetFingerprint second
) noexcept
{
    return first.high == second.high && first.low == second.low;
}

bool operator!=(
    RulesetFingerprint first,
    RulesetFingerprint second
) noexcept
{
    return !(first == second);
}

RulesetFingerprint ComputeRulesetFingerprint(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock
)
{
    SourceLock sourceLock;
    std::string message;
    SourceLockBuilder{}.Build({}, sourceLock, message);
    return ComputeRulesetFingerprint(ruleset, packageLock, sourceLock);
}

RulesetFingerprint ComputeRulesetFingerprint(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const SourceLock& sourceLock
)
{
    if (!packageLock.IsResolved() || !sourceLock.IsResolved())
    {
        return {};
    }
    FingerprintWriter writer;
    writer.Text("dillen.ruleset.fingerprint.v3");
    writer.Unsigned(ruleset.id.value);
    writer.Unsigned(ruleset.version);

    auto extensions = ruleset.appliedExtensions;
    std::sort(
        extensions.begin(),
        extensions.end(),
        [](const AppliedRulesetExtension& first,
           const AppliedRulesetExtension& second)
        {
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    for (const AppliedRulesetExtension& extension : extensions)
    {
        writer.Unsigned(extension.id.value);
        writer.Text(extension.canonicalName);
        writer.Unsigned(extension.version);
        writer.Unsigned(static_cast<std::uint32_t>(extension.priority));
    }

    auto packageRequirements = ruleset.packages;
    std::sort(
        packageRequirements.begin(),
        packageRequirements.end(),
        [](const RulesetPackageRequirement& first,
           const RulesetPackageRequirement& second)
        {
            return first.package < second.package;
        }
    );
    for (const RulesetPackageRequirement& requirement
        : packageRequirements)
    {
        writer.Unsigned(requirement.package.value);
        writer.Text(requirement.canonicalName);
        writer.Unsigned(
            requirement.versions.minimumInclusive.has_value() ? 1 : 0
        );
        if (requirement.versions.minimumInclusive.has_value())
        {
            writer.Unsigned(
                requirement.versions.minimumInclusive->major
            );
            writer.Unsigned(
                requirement.versions.minimumInclusive->minor
            );
            writer.Unsigned(
                requirement.versions.minimumInclusive->patch
            );
        }
        writer.Unsigned(
            requirement.versions.maximumExclusive.has_value() ? 1 : 0
        );
        if (requirement.versions.maximumExclusive.has_value())
        {
            writer.Unsigned(
                requirement.versions.maximumExclusive->major
            );
            writer.Unsigned(
                requirement.versions.maximumExclusive->minor
            );
            writer.Unsigned(
                requirement.versions.maximumExclusive->patch
            );
        }
    }

    for (const PackageLockEntry& entry : packageLock.Entries())
    {
        writer.Unsigned(entry.package.value);
        writer.Unsigned(entry.version.major);
        writer.Unsigned(entry.version.minor);
        writer.Unsigned(entry.version.patch);
        writer.Text(entry.contentDigest);
        writer.Unsigned(entry.loadIndex);
    }

    for (const SourceLockEntry& entry : sourceLock.Entries())
    {
        writer.Text(entry.sourceLayer);
        writer.Text(entry.virtualPath);
        writer.Unsigned(entry.fingerprint);
        writer.Unsigned(entry.size);
    }

    auto schemas = ruleset.requiredSchemas;
    std::sort(
        schemas.begin(),
        schemas.end(),
        [](const RulesetSchemaRequirement& first,
           const RulesetSchemaRequirement& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.version < second.version;
        }
    );
    for (const RulesetSchemaRequirement& schema : schemas)
    {
        writer.Unsigned(schema.type.value);
        writer.Unsigned(schema.version);
    }

    auto components = ruleset.requiredComponents;
    std::sort(
        components.begin(),
        components.end(),
        [](const RulesetComponentRequirement& first,
           const RulesetComponentRequirement& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.version < second.version;
        }
    );
    for (const RulesetComponentRequirement& component : components)
    {
        writer.Unsigned(component.type.value);
        writer.Unsigned(component.version);
    }

    auto relations = ruleset.requiredRelations;
    std::sort(
        relations.begin(),
        relations.end(),
        [](const RulesetRelationRequirement& first,
           const RulesetRelationRequirement& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.version < second.version;
        }
    );
    for (const RulesetRelationRequirement& relation : relations)
    {
        writer.Unsigned(relation.type.value);
        writer.Unsigned(relation.version);
    }

    auto definitions = ruleset.requiredDefinitions;
    std::sort(definitions.begin(), definitions.end());
    for (MechanismDefinitionId definition : definitions)
    {
        writer.Unsigned(definition.value);
    }


    auto entityDefinitions = ruleset.requiredEntityDefinitions;
    std::sort(entityDefinitions.begin(), entityDefinitions.end());
    for (EntityDefinitionId definition : entityDefinitions)
    {
        writer.Unsigned(definition.value);
    }

    auto relationDefinitions = ruleset.requiredRelationDefinitions;
    std::sort(relationDefinitions.begin(), relationDefinitions.end());
    for (RelationDefinitionId definition : relationDefinitions)
    {
        writer.Unsigned(definition.value);
    }

    auto mechanismSpawns = ruleset.requiredMechanismSpawns;
    std::sort(mechanismSpawns.begin(), mechanismSpawns.end());
    for (MechanismSpawnDefinitionId spawn : mechanismSpawns)
    {
        writer.Unsigned(spawn.value);
    }

    auto algorithms = ruleset.requiredAlgorithms;
    std::sort(
        algorithms.begin(),
        algorithms.end(),
        [](const RulesetAlgorithmRequirement& first,
           const RulesetAlgorithmRequirement& second)
        {
            if (first.algorithm != second.algorithm)
            {
                return first.algorithm < second.algorithm;
            }
            return first.version < second.version;
        }
    );
    for (const RulesetAlgorithmRequirement& algorithm : algorithms)
    {
        writer.Unsigned(algorithm.algorithm.value);
        writer.Unsigned(algorithm.version);
    }

    auto capabilities = ruleset.requiredCapabilities;
    std::sort(
        capabilities.begin(),
        capabilities.end(),
        [](const CapabilityRequirement& first,
           const CapabilityRequirement& second)
        {
            return first.capability < second.capability;
        }
    );
    for (const CapabilityRequirement& capability : capabilities)
    {
        writer.Unsigned(capability.capability.value);
        writer.Unsigned(capability.versions.minimumInclusive);
        writer.Unsigned(
            capability.versions.maximumExclusive.value_or(0)
        );
    }
    return writer.Finish();
}

}
