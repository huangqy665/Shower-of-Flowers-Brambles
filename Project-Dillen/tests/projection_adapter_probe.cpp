#include <iostream>
#include <string>

#include "adapter_migration.hpp"
#include "mechanism_ids.hpp"
#include "projection_artifact.hpp"

namespace {

dillen::adapter::ProjectionArtifact MakeArtifact()
{
    using namespace dillen;
    adapter::ProjectionArtifact artifact;
    artifact.identity.canonicalName = "dillen.test.synthetic_projection";
    artifact.identity.formatVersion = 1;
    artifact.identity.corpus = {
        "dillen.test.synthetic_corpus",
        "fixture-1",
        std::string(64, 'a')
    };
    artifact.identity.importer = {
        "dillen.test.synthetic_importer",
        1,
        std::string(64, 'b')
    };
    artifact.identity.normalizedCorpus = {
        "dillen.test.synthetic_ir",
        1,
        std::string(64, 'c')
    };
    artifact.identity.mappingProfile = {
        "dillen.test.synthetic_mapping",
        1,
        std::string(64, 'd')
    };
    artifact.identity.targetRuleset = kernel::StableRulesetId(
        "dillen.test.synthetic_ruleset"
    );
    artifact.identity.targetRulesetVersion = 1;
    artifact.sources.push_back({
        "generated/mechanisms/synthetic.dillen",
        "mechanism_definition = { name = synthetic }\n",
        {{1, "corpus/source.txt", 0, 12}}
    });
    adapter::SealProjectionArtifact(artifact);
    return artifact;
}

dillen::adapter::AdapterMigrationStep ImporterMigration(
    const dillen::adapter::ProjectionContract& source,
    const dillen::adapter::ProjectionContract& target
)
{
    return {
        "dillen.test.migration.importer_v1_to_v2",
        source,
        target,
        [](const dillen::adapter::ProjectionArtifact& input,
           dillen::adapter::ProjectionArtifact& output,
           std::string&)
        {
            output = input;
            output.identity.importer.version = 2;
            output.identity.importer.implementationDigest =
                std::string(64, 'e');
            output.sources[0].bytes += "# importer-v2\n";
            return dillen::adapter::SealProjectionArtifact(output);
        }
    };
}

dillen::adapter::AdapterMigrationStep MappingMigration(
    const dillen::adapter::ProjectionContract& source,
    const dillen::adapter::ProjectionContract& target
)
{
    return {
        "dillen.test.migration.mapping_v1_to_v2",
        source,
        target,
        [](const dillen::adapter::ProjectionArtifact& input,
           dillen::adapter::ProjectionArtifact& output,
           std::string&)
        {
            output = input;
            output.identity.mappingProfile.version = 2;
            output.identity.mappingProfile.contentDigest =
                std::string(64, 'f');
            output.sources[0].bytes += "# mapping-v2\n";
            return dillen::adapter::SealProjectionArtifact(output);
        }
    };
}

}

int main()
{
    using namespace dillen::adapter;
    ProjectionArtifact source = MakeArtifact();
    std::string message;
    if (!ValidateProjectionArtifact(source, message)
        || BuildProjectionLockDocument(source.identity).empty())
    {
        std::cerr << "Projection Artifact validation failed\n";
        return 1;
    }
    ProjectionArtifact tampered = source;
    tampered.sources[0].bytes += "tamper";
    if (ValidateProjectionArtifact(tampered, message))
    {
        std::cerr << "Projection Artifact tampering was accepted\n";
        return 2;
    }

    const ProjectionContract v1 = ContractOf(source.identity);
    ProjectionContract importerV2 = v1;
    importerV2.importerVersion = 2;
    ProjectionContract mappingV2 = importerV2;
    mappingV2.mappingProfileVersion = 2;
    AdapterMigrationRegistry migrations;
    if (migrations.Register(ImporterMigration(v1, importerV2))
            != AdapterMigrationRegisterResult::Added
        || migrations.Register(MappingMigration(importerV2, mappingV2))
            != AdapterMigrationRegisterResult::Added)
    {
        return 3;
    }
    migrations.Freeze();
    ProjectionArtifact migrated;
    const AdapterMigrationReport report = migrations.Migrate(
        source,
        mappingV2,
        migrated
    );
    if (!report
        || report.appliedSteps.size() != 2
        || ContractOf(migrated.identity) != mappingV2
        || migrated.identity.corpus != source.identity.corpus
        || !ValidateProjectionArtifact(migrated, message))
    {
        std::cerr << "Projection Adapter migration failed\n";
        return 4;
    }

    AdapterMigrationRegistry ambiguous;
    ambiguous.Register(ImporterMigration(v1, importerV2));
    ambiguous.Register(MappingMigration(importerV2, mappingV2));
    AdapterMigrationStep direct = MappingMigration(v1, mappingV2);
    direct.canonicalName = "dillen.test.migration.direct_v1_to_v2";
    direct.transform = [](const ProjectionArtifact& input,
                          ProjectionArtifact& output,
                          std::string&)
    {
        output = input;
        output.identity.importer.version = 2;
        output.identity.importer.implementationDigest = std::string(64, 'e');
        output.identity.mappingProfile.version = 2;
        output.identity.mappingProfile.contentDigest = std::string(64, 'f');
        output.sources[0].bytes += "# direct\n";
        return SealProjectionArtifact(output);
    };
    ambiguous.Register(std::move(direct));
    ambiguous.Freeze();
    if (ambiguous.Migrate(source, mappingV2, migrated).status
        != AdapterMigrationStatus::PathAmbiguous)
    {
        std::cerr << "Ambiguous Adapter Migration path was accepted\n";
        return 5;
    }

    std::cout << "Projection Artifact identity/migration: passed\n";
    return 0;
}
