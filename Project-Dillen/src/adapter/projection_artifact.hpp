#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"

namespace dillen::adapter {

struct CorpusSnapshotIdentity
{
    std::string canonicalName;
    std::string revision;
    std::string contentDigest;
};

struct ImporterIdentity
{
    std::string canonicalName;
    std::uint32_t version = 0;
    std::string implementationDigest;
};

struct NormalizedCorpusIdentity
{
    std::string schema;
    std::uint32_t schemaVersion = 0;
    std::string contentDigest;
};

struct MappingProfileIdentity
{
    std::string canonicalName;
    std::uint32_t version = 0;
    std::string contentDigest;
};

struct ProjectionArtifactIdentity
{
    std::string canonicalName;
    std::uint32_t formatVersion = 1;
    CorpusSnapshotIdentity corpus;
    ImporterIdentity importer;
    NormalizedCorpusIdentity normalizedCorpus;
    MappingProfileIdentity mappingProfile;
    kernel::RulesetId targetRuleset;
    std::uint32_t targetRulesetVersion = 0;
    std::string contentDigest;
};

struct ProjectionSourceMapEntry
{
    std::uint32_t generatedLine = 0;
    std::string corpusPath;
    std::uint64_t corpusOffset = 0;
    std::uint64_t corpusLength = 0;
};

struct ProjectionSource
{
    std::string virtualPath;
    std::string bytes;
    std::vector<ProjectionSourceMapEntry> sourceMap;
};

struct ProjectionArtifact
{
    ProjectionArtifactIdentity identity;
    std::vector<ProjectionSource> sources;
};

struct ProjectionContract
{
    std::string artifact;
    std::uint32_t artifactFormatVersion = 0;
    std::string importer;
    std::uint32_t importerVersion = 0;
    std::string normalizedSchema;
    std::uint32_t normalizedSchemaVersion = 0;
    std::string mappingProfile;
    std::uint32_t mappingProfileVersion = 0;
    kernel::RulesetId targetRuleset;
    std::uint32_t targetRulesetVersion = 0;
};

bool operator==(
    const CorpusSnapshotIdentity& first,
    const CorpusSnapshotIdentity& second
) noexcept;
bool operator!=(
    const CorpusSnapshotIdentity& first,
    const CorpusSnapshotIdentity& second
) noexcept;
bool operator==(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept;
bool operator!=(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept;
bool operator<(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept;

ProjectionContract ContractOf(const ProjectionArtifactIdentity& identity);
bool IsValidProjectionContract(const ProjectionContract& contract) noexcept;
std::string ComputeProjectionArtifactDigest(
    const ProjectionArtifact& artifact
);
bool SealProjectionArtifact(ProjectionArtifact& artifact);
bool ValidateProjectionArtifact(
    const ProjectionArtifact& artifact,
    std::string& message
);
std::string BuildProjectionLockDocument(
    const ProjectionArtifactIdentity& identity
);

}
