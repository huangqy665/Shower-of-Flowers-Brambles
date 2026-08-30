#include "projection_artifact.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <tuple>

#include "mechanism_ids.hpp"
#include "package_content_digest.hpp"
#include "package_manifest.hpp"

namespace dillen::adapter {

namespace {

bool ValidName(const std::string& value) noexcept
{
    return kernel::IsValidMechanismSymbol(value)
        && value == kernel::NormalizeMechanismSymbol(value);
}

bool ValidDigest(const std::string& value) noexcept
{
    return kernel::IsValidPackageContentDigest(value);
}

void Text(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value << ';';
}

void IdentityPayload(
    std::ostringstream& stream,
    const ProjectionArtifactIdentity& identity
)
{
    Text(stream, "dillen.projection.artifact.v1");
    Text(stream, identity.canonicalName);
    stream << identity.formatVersion << ';';
    Text(stream, identity.corpus.canonicalName);
    Text(stream, identity.corpus.revision);
    Text(stream, identity.corpus.contentDigest);
    Text(stream, identity.importer.canonicalName);
    stream << identity.importer.version << ';';
    Text(stream, identity.importer.implementationDigest);
    Text(stream, identity.normalizedCorpus.schema);
    stream << identity.normalizedCorpus.schemaVersion << ';';
    Text(stream, identity.normalizedCorpus.contentDigest);
    Text(stream, identity.mappingProfile.canonicalName);
    stream << identity.mappingProfile.version << ';';
    Text(stream, identity.mappingProfile.contentDigest);
    stream << identity.targetRuleset.value << ';'
        << identity.targetRulesetVersion << ';';
}

}

bool operator==(
    const CorpusSnapshotIdentity& first,
    const CorpusSnapshotIdentity& second
) noexcept
{
    return first.canonicalName == second.canonicalName
        && first.revision == second.revision
        && first.contentDigest == second.contentDigest;
}

bool operator!=(
    const CorpusSnapshotIdentity& first,
    const CorpusSnapshotIdentity& second
) noexcept
{
    return !(first == second);
}

bool operator==(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept
{
    return first.artifact == second.artifact
        && first.artifactFormatVersion == second.artifactFormatVersion
        && first.importer == second.importer
        && first.importerVersion == second.importerVersion
        && first.normalizedSchema == second.normalizedSchema
        && first.normalizedSchemaVersion == second.normalizedSchemaVersion
        && first.mappingProfile == second.mappingProfile
        && first.mappingProfileVersion == second.mappingProfileVersion
        && first.targetRuleset == second.targetRuleset
        && first.targetRulesetVersion == second.targetRulesetVersion;
}

bool operator!=(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept
{
    return !(first == second);
}

bool operator<(
    const ProjectionContract& first,
    const ProjectionContract& second
) noexcept
{
    return std::tie(
        first.artifact,
        first.artifactFormatVersion,
        first.importer,
        first.importerVersion,
        first.normalizedSchema,
        first.normalizedSchemaVersion,
        first.mappingProfile,
        first.mappingProfileVersion,
        first.targetRuleset.value,
        first.targetRulesetVersion
    ) < std::tie(
        second.artifact,
        second.artifactFormatVersion,
        second.importer,
        second.importerVersion,
        second.normalizedSchema,
        second.normalizedSchemaVersion,
        second.mappingProfile,
        second.mappingProfileVersion,
        second.targetRuleset.value,
        second.targetRulesetVersion
    );
}

ProjectionContract ContractOf(const ProjectionArtifactIdentity& identity)
{
    return {
        identity.canonicalName,
        identity.formatVersion,
        identity.importer.canonicalName,
        identity.importer.version,
        identity.normalizedCorpus.schema,
        identity.normalizedCorpus.schemaVersion,
        identity.mappingProfile.canonicalName,
        identity.mappingProfile.version,
        identity.targetRuleset,
        identity.targetRulesetVersion
    };
}

bool IsValidProjectionContract(const ProjectionContract& contract) noexcept
{
    return ValidName(contract.artifact)
        && contract.artifactFormatVersion > 0
        && ValidName(contract.importer)
        && contract.importerVersion > 0
        && ValidName(contract.normalizedSchema)
        && contract.normalizedSchemaVersion > 0
        && ValidName(contract.mappingProfile)
        && contract.mappingProfileVersion > 0
        && contract.targetRuleset
        && contract.targetRulesetVersion > 0;
}

std::string ComputeProjectionArtifactDigest(
    const ProjectionArtifact& artifact
)
{
    std::ostringstream payload;
    IdentityPayload(payload, artifact.identity);
    std::vector<const ProjectionSource*> sources;
    sources.reserve(artifact.sources.size());
    for (const ProjectionSource& source : artifact.sources)
        sources.push_back(&source);
    std::sort(
        sources.begin(),
        sources.end(),
        [](const ProjectionSource* first, const ProjectionSource* second)
        {
            return first->virtualPath < second->virtualPath;
        }
    );
    for (const ProjectionSource* source : sources)
    {
        Text(payload, source->virtualPath);
        Text(payload, source->bytes);
        payload << source->sourceMap.size() << ';';
        for (const ProjectionSourceMapEntry& entry : source->sourceMap)
        {
            payload << entry.generatedLine << ';';
            Text(payload, entry.corpusPath);
            payload << entry.corpusOffset << ';'
                << entry.corpusLength << ';';
        }
    }
    const std::string bytes = payload.str();
    return kernel::ComputePackageContentDigest({
        {".dillen/projection.payload", bytes}
    });
}

bool SealProjectionArtifact(ProjectionArtifact& artifact)
{
    artifact.identity.contentDigest.clear();
    artifact.identity.contentDigest = ComputeProjectionArtifactDigest(
        artifact
    );
    std::string message;
    return ValidateProjectionArtifact(artifact, message);
}

bool ValidateProjectionArtifact(
    const ProjectionArtifact& artifact,
    std::string& message
)
{
    message.clear();
    const ProjectionArtifactIdentity& identity = artifact.identity;
    if (!IsValidProjectionContract(ContractOf(identity))
        || !ValidName(identity.corpus.canonicalName)
        || identity.corpus.revision.empty()
        || !ValidDigest(identity.corpus.contentDigest)
        || !ValidDigest(identity.importer.implementationDigest)
        || !ValidDigest(identity.normalizedCorpus.contentDigest)
        || !ValidDigest(identity.mappingProfile.contentDigest)
        || !ValidDigest(identity.contentDigest))
    {
        message = "Projection Artifact identity is invalid";
        return false;
    }
    if (artifact.sources.empty())
    {
        message = "Projection Artifact contains no generated source";
        return false;
    }
    std::set<std::string> paths;
    for (const ProjectionSource& source : artifact.sources)
    {
        if (source.virtualPath.empty()
            || source.bytes.empty()
            || !paths.insert(source.virtualPath).second)
        {
            message = "Projection Artifact source is invalid or duplicated";
            return false;
        }
        std::uint32_t previousLine = 0;
        for (const ProjectionSourceMapEntry& entry : source.sourceMap)
        {
            if (entry.generatedLine == 0
                || entry.generatedLine < previousLine
                || entry.corpusPath.empty()
                || entry.corpusLength == 0)
            {
                message = "Projection Artifact source map is invalid";
                return false;
            }
            previousLine = entry.generatedLine;
        }
    }
    ProjectionArtifact copy = artifact;
    copy.identity.contentDigest.clear();
    if (ComputeProjectionArtifactDigest(copy) != identity.contentDigest)
    {
        message = "Projection Artifact digest does not match its contents";
        return false;
    }
    return true;
}

std::string BuildProjectionLockDocument(
    const ProjectionArtifactIdentity& identity
)
{
    std::ostringstream stream;
    stream << "projection_lock = {\n"
        << "  artifact = \"" << identity.canonicalName << "\"\n"
        << "  format_version = " << identity.formatVersion << "\n"
        << "  corpus = \"" << identity.corpus.canonicalName << "\"\n"
        << "  corpus_revision = \"" << identity.corpus.revision << "\"\n"
        << "  corpus_digest = \"" << identity.corpus.contentDigest << "\"\n"
        << "  importer = \"" << identity.importer.canonicalName << "\"\n"
        << "  importer_version = " << identity.importer.version << "\n"
        << "  importer_digest = \""
        << identity.importer.implementationDigest << "\"\n"
        << "  normalized_schema = \""
        << identity.normalizedCorpus.schema << "\"\n"
        << "  normalized_schema_version = "
        << identity.normalizedCorpus.schemaVersion << "\n"
        << "  normalized_digest = \""
        << identity.normalizedCorpus.contentDigest << "\"\n"
        << "  mapping_profile = \""
        << identity.mappingProfile.canonicalName << "\"\n"
        << "  mapping_version = " << identity.mappingProfile.version << "\n"
        << "  mapping_digest = \""
        << identity.mappingProfile.contentDigest << "\"\n"
        << "  target_ruleset = " << identity.targetRuleset.value << "\n"
        << "  target_ruleset_version = "
        << identity.targetRulesetVersion << "\n"
        << "  projection_digest = \"" << identity.contentDigest << "\"\n"
        << "}\n";
    return stream.str();
}

}
