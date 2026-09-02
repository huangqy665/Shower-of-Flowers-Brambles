#include "province_content_emitter.hpp"

#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "package_content_digest.hpp"

namespace dillen::adapter {

namespace {

ProvinceContentReport Fail(
    ProvinceContentStatus status,
    std::string message
)
{
    ProvinceContentReport report;
    report.status = status;
    report.message = std::move(message);
    return report;
}

// Accumulates one Package: its files on disk and the sources its content
// digest is taken over.
//
// PackageContentSource::bytes is a string_view, so every emitted text has to
// outlive the digest. `retained` is what keeps it alive -- handing the view a
// moved-from temporary compiles, writes correct files, and then reads freed
// memory when the digest is computed.
class PackageWriter
{
public:
    PackageWriter(std::filesystem::path root, std::size_t expectedFiles)
        : root_(std::move(root))
    {
        retained_.reserve(expectedFiles);
    }

    // Every emitted file is written with '\n' regardless of platform. The
    // content digest is a SHA-256 over raw bytes, so CRLF on Windows and LF
    // elsewhere would give the same corpus two different digests and the
    // Package would be rejected as tampered on whichever platform did not
    // generate it. Repository sources are pinned to LF by .gitattributes;
    // generated sources have no such protection and must be right on their own.
    bool Emit(
        const std::string& relative,
        std::string text,
        ProvinceContentReport& report
    )
    {
        if (retained_.size() == retained_.capacity())
        {
            // A reallocation here would dangle every view already handed out.
            return false;
        }
        retained_.push_back(std::move(text));
        const std::string& stored = retained_.back();
        std::ofstream stream(
            root_ / relative,
            std::ios::binary | std::ios::trunc
        );
        if (!stream)
        {
            return false;
        }
        stream.write(
            stored.data(),
            static_cast<std::streamsize>(stored.size())
        );
        if (!stream)
        {
            return false;
        }
        report.bytes += stored.size();
        ++report.files;
        sources_.push_back({relative, stored});
        return true;
    }

    std::string Digest()
    {
        return kernel::ComputePackageContentDigest(sources_);
    }

private:
    std::filesystem::path root_;
    std::vector<std::string> retained_;
    std::vector<kernel::PackageContentSource> sources_;
};

std::string Manifest(
    const std::string& name,
    const std::string& role,
    const std::string& digest,
    int priority,
    const std::string& dependency
)
{
    std::string text;
    text += "package_manifest = {\n";
    text += "    name = " + name + "\n";
    text += "    version_major = 1\n";
    text += "    version_minor = 0\n";
    text += "    version_patch = 0\n";
    text += "    role = " + role + "\n";
    text += "    content_digest = \"" + digest + "\"\n";
    text += "    load_priority = " + std::to_string(priority) + "\n";
    if (!dependency.empty())
    {
        text += "    dependencies = {\n";
        text += "        dependency = {\n";
        text += "            name = " + dependency + "\n";
        text += "            minimum_major = 1\n";
        text += "            minimum_minor = 0\n";
        text += "            minimum_patch = 0\n";
        text += "            maximum_major = 2\n";
        text += "            maximum_minor = 0\n";
        text += "            maximum_patch = 0\n";
        text += "            required = yes\n";
        text += "        }\n";
        text += "    }\n";
    }
    text += "}\n";
    return text;
}

bool MakeDirectories(
    const std::filesystem::path& root,
    std::initializer_list<const char*> directories
)
{
    std::error_code error;
    for (const char* directory : directories)
    {
        std::filesystem::create_directories(root / directory, error);
        if (error)
        {
            return false;
        }
    }
    return true;
}

}

ProvinceContentReport EmitProvinceContent(
    const ProvinceRasterImport& imported,
    const ProvinceContentOptions& options
)
{
    ProvinceContentReport report;
    std::error_code error;
    std::filesystem::remove_all(options.root, error);
    error.clear();

    // Two Packages, because schemas and instances are different kinds of
    // statement.
    //
    // A Component Schema and a Relation Schema are contracts: they say what a
    // region IS, and a Mechanism Package written against them must be able to
    // depend on them. The entity and relation tables are Content: they say
    // which regions exist in this particular world. Putting both in one
    // Package is refused at load time -- PackageRoleAllows draws exactly this
    // line -- and the refusal is right. A generated world that shipped its
    // schemas inside its Content would be unusable by any mechanism that did
    // not also want that specific map.
    const std::filesystem::path contractRoot = options.root / "contracts";
    const std::filesystem::path contentRoot = options.root / "content";
    const std::filesystem::path presentationRoot =
        options.root / "presentation";
    if (!MakeDirectories(
            contractRoot,
            {"packages", "components", "relations/schemas"})
        || !MakeDirectories(
            contentRoot,
            {"packages", "entities", "relations/definitions", "rulesets"})
        || !MakeDirectories(
            presentationRoot,
            {"packages", "assets/rasters"}))
    {
        return Fail(
            ProvinceContentStatus::RootNotWritable,
            "could not create the package directories"
        );
    }

    // --- Contract Package -------------------------------------------------
    PackageWriter contracts(contractRoot, 4);

    std::string component;
    component += "component_schema = {\n";
    component += "    name = " + options.componentName + "\n";
    component += "    version = 1\n";
    component += "    fields = {\n";
    // The province's id in the corpus it came from. Not what the simulation
    // keys on -- Entities have their own stable ids -- but it is what a human
    // matches against the source data, and what the renderer's index texture
    // is built from.
    component += "        field = { name = source_id  kind = integer "
                 " required = yes  default = 0 }\n";
    component += "    }\n";
    component += "}\n";
    if (!contracts.Emit(
            "components/geography.dcomponent",
            std::move(component),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "component schema");
    }

    std::string relationSchema;
    relationSchema += "relation_schema = {\n";
    relationSchema += "    name = " + options.relationName + "\n";
    relationSchema += "    version = 1\n";
    relationSchema += "    source_type = " + options.entityTypeName + "\n";
    relationSchema += "    target_type = " + options.entityTypeName + "\n";
    relationSchema += "    allow_self = no\n";
    relationSchema += "}\n";
    if (!contracts.Emit(
            "relations/schemas/borders.drelation",
            std::move(relationSchema),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "relation schema");
    }

    const std::string contractName = options.packageName + ".contracts";
    if (!contracts.Emit(
            "packages/contracts.dpackage",
            Manifest(contractName, "contract", contracts.Digest(), 0, {}),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "contract manifest");
    }

    // --- Content Package --------------------------------------------------
    PackageWriter content(contentRoot, 6);

    std::string entities;
    entities.reserve(static_cast<std::size_t>(imported.Count()) * 24 + 512);
    entities += "entity_table = {\n";
    entities += "    entity_type = " + options.entityTypeName + "\n";
    entities += "    name_prefix = " + options.namePrefix + "\n";
    entities += "    component = {\n";
    entities += "        type = " + options.componentName + "\n";
    entities += "        schema_version = 1\n";
    entities += "        columns = { source_id }\n";
    entities += "    }\n";
    entities += "    rows = {\n";
    for (std::uint32_t index = 1; index <= imported.Count(); ++index)
    {
        // The row's own suffix is the dense index, not the corpus id: the
        // index is what the raster and the palette carry, so a name built from
        // it is the one a renderer can reach without a second table.
        entities += "        row = { " + std::to_string(index) + "  "
            + std::to_string(imported.sourceIdByIndex[index]) + " }\n";
    }
    entities += "    }\n";
    entities += "}\n";
    report.entities = imported.Count();
    if (!content.Emit(
            "entities/world.dentitytable",
            std::move(entities),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "entity table");
    }

    std::string relations;
    relations.reserve(imported.adjacency.size() * 28 + 512);
    relations += "relation_table = {\n";
    relations += "    relation = " + options.relationName + "\n";
    relations += "    schema_version = 1\n";
    relations += "    name_prefix = " + options.relationNamePrefix + "\n";
    relations += "    source_entity_type = " + options.entityTypeName + "\n";
    relations += "    target_entity_type = " + options.entityTypeName + "\n";
    relations += "    source_prefix = " + options.namePrefix + "\n";
    relations += "    target_prefix = " + options.namePrefix + "\n";
    relations += "    rows = {\n";
    for (const ProvinceAdjacency& edge : imported.adjacency)
    {
        const std::string first = std::to_string(edge.first);
        const std::string second = std::to_string(edge.second);
        relations += "        row = { " + first + "_" + second + "  " + first
            + "  " + second + " }\n";
    }
    relations += "    }\n";
    relations += "}\n";
    report.relations = static_cast<std::uint32_t>(imported.adjacency.size());
    if (!content.Emit(
            "relations/definitions/world.drelationtable",
            std::move(relations),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "relation table");
    }

    std::string ruleset;
    ruleset += "root_ruleset = {\n";
    ruleset += "    name = " + options.rulesetName + "\n";
    ruleset += "    version = 1\n";
    ruleset += "    required_packages = {\n";
    ruleset += "        requirement = { name = " + contractName
        + "  minimum_major = 1 minimum_minor = 0 minimum_patch = 0"
          "  maximum_major = 2 maximum_minor = 0 maximum_patch = 0 }\n";
    ruleset += "        requirement = { name = " + options.packageName
        + "  minimum_major = 1 minimum_minor = 0 minimum_patch = 0"
          "  maximum_major = 2 maximum_minor = 0 maximum_patch = 0 }\n";
    ruleset += "    }\n";
    ruleset += "    required_components = { " + options.componentName
        + " = 1 }\n";
    ruleset += "    required_relations = { " + options.relationName
        + " = 1 }\n";
    // Every region and every border, one line each. Naming 55880
    // requirements would move the enormous file into the Ruleset rather
    // than remove it.
    ruleset += "    required_entity_definitions = { all = yes }\n";
    ruleset += "    required_relation_definitions = { all = yes }\n";
    ruleset += "}\n";
    if (!content.Emit("rulesets/world.druleset", std::move(ruleset), report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "ruleset");
    }

    // --- Presentation Package ---------------------------------------------
    //
    // The index raster: 5616 x 2160 dense province indices, which is 24 MB
    // raw. It is run-length encoded because a province map is enormous flat
    // regions -- the reference corpus averages 27.7 pixels per run, so the
    // encoding is roughly thirteen times smaller and the decode is a memcpy
    // loop.
    //
    // Pairs of (index, count) as little-endian uint16. Runs are taken over the
    // FLAT raster rather than per scanline, so ocean spanning the end of one
    // row and the start of the next costs one run instead of two, and are
    // split at 65535.
    std::string raster;
    raster.reserve(imported.indexRaster.size() / 8);
    {
        const auto append = [&raster](std::uint16_t value)
        {
            raster.push_back(static_cast<char>(value & 0xFFU));
            raster.push_back(static_cast<char>((value >> 8) & 0xFFU));
        };
        std::size_t cursor = 0;
        while (cursor < imported.indexRaster.size())
        {
            const std::uint16_t value = imported.indexRaster[cursor];
            std::size_t run = 1;
            while (cursor + run < imported.indexRaster.size()
                && imported.indexRaster[cursor + run] == value
                && run < 0xFFFFu)
            {
                ++run;
            }
            append(value);
            append(static_cast<std::uint16_t>(run));
            ++report.rasterRuns;
            cursor += run;
        }
    }
    report.rasterBytes = raster.size();
    report.rasterDigest = kernel::ComputeContentDigest(raster);
    {
        std::ofstream stream(
            presentationRoot / "assets/rasters/world.dmapindex",
            std::ios::binary | std::ios::trunc
        );
        if (!stream)
        {
            return Fail(ProvinceContentStatus::WriteFailed, "raster payload");
        }
        stream.write(
            raster.data(),
            static_cast<std::streamsize>(raster.size())
        );
        if (!stream)
        {
            return Fail(ProvinceContentStatus::WriteFailed, "raster payload");
        }
        report.bytes += raster.size();
        ++report.files;
    }

    PackageWriter presentation(presentationRoot, 4);
    std::string asset;
    asset += "presentation_asset = {\n";
    asset += "    name = " + options.rasterAssetName + "\n";
    asset += "    kind = map_index_raster\n";
    asset += "    asset = rasters/world.dmapindex\n";
    asset += "    asset_digest = \"" + report.rasterDigest + "\"\n";
    asset += "    properties = {\n";
    asset += "        format = index16_rle\n";
    asset += "        width = " + std::to_string(imported.width) + "\n";
    asset += "        height = " + std::to_string(imported.height) + "\n";
    asset += "        province_count = "
        + std::to_string(imported.Count()) + "\n";
    asset += "        runs = " + std::to_string(report.rasterRuns) + "\n";
    asset += "    }\n";
    asset += "}\n";
    if (!presentation.Emit("assets/world_raster.dasset", std::move(asset),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "raster asset");
    }
    // No dependency: a Presentation Package that participated in the
    // dependency graph could be pulled into the closure by it, and the whole
    // point is that a skin cannot reach the save identity.
    if (!presentation.Emit(
            "packages/presentation.dpackage",
            Manifest(
                options.presentationPackageName,
                "presentation",
                presentation.Digest(),
                200,
                {}
            ),
            report))
    {
        return Fail(
            ProvinceContentStatus::WriteFailed,
            "presentation manifest"
        );
    }

    // Written last: the manifest carries the digest of every other file in its
    // Package, and is itself excluded from that digest.
    report.contentDigest = content.Digest();
    if (!content.Emit(
            "packages/world.dpackage",
            Manifest(
                options.packageName,
                "content",
                report.contentDigest,
                100,
                contractName
            ),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "content manifest");
    }
    return report;
}

}
