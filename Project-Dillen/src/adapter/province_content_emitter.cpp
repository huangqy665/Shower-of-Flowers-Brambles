#include "province_content_emitter.hpp"

#include <fstream>
#include <iterator>
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
    const std::string& dependency,
    const std::string& providedCapability = {}
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
    if (!providedCapability.empty())
    {
        // A Capability Contract is only usable once its Package says it
        // publishes it. Declaring the contract file alone would leave
        // the Ruleset with a name nothing offers.
        text += "    provides = {\n";
        text += "        capability = {\n";
        text += "            name = " + providedCapability + "\n";
        text += "            version = 1\n";
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
    // Present only for the demo world. Everything gated on it is gameplay
    // rather than geography; see DemoProductionSlice.
    const DemoProductionSlice* const slice =
        options.slice ? &*options.slice : nullptr;
    if (options.root.empty())
    {
        return Fail(
            ProvinceContentStatus::RootNotWritable,
            "the game content root is empty"
        );
    }

    // Domain-first Dillen-Game layout. Each path is still a separate Source
    // Layer with exactly one Manifest; physical co-location by domain must not
    // blur the Package role boundary enforced by AuthoringSession.
    const std::filesystem::path contractRoot =
        options.root / "map/contracts";
    const std::filesystem::path contentRoot = options.root / "map/world";
    const std::filesystem::path presentationRoot =
        options.root / "presentation/map_world";
    const std::filesystem::path mechanismRoot =
        options.root / "production/map_world";

    // Regeneration owns only these four generated Source Layers. Removing the
    // whole Dillen-Game root would also delete hand-authored mechanisms,
    // Rulesets and source corpora merely because they share the same game.
    std::error_code error;
    for (const std::filesystem::path& generatedRoot : {
            contractRoot, contentRoot, presentationRoot, mechanismRoot})
    {
        std::filesystem::remove_all(generatedRoot, error);
        if (error)
        {
            return Fail(
                ProvinceContentStatus::RootNotWritable,
                "could not clear generated package directory: "
                    + generatedRoot.string()
            );
        }
    }

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
    if (!MakeDirectories(
            contractRoot,
            {"packages", "components", "relations/schemas"})
        || !MakeDirectories(
            contentRoot,
            {"packages", "entities", "relations/definitions", "rulesets"})
        || !MakeDirectories(
            presentationRoot,
            {"packages", "assets/rasters"})
        // The demo's directories are the demo's. A plain map that left empty
        // `spawns/` and `mechanisms/` folders behind would still be telling
        // whoever opened it that a mechanism belongs there.
        || (slice != nullptr
            && (!MakeDirectories(contractRoot, {"capabilities"})
                || !MakeDirectories(contentRoot, {"definitions", "spawns"})
                || !MakeDirectories(presentationRoot, {"assets/fonts"})
                || !MakeDirectories(
                    mechanismRoot,
                    {"packages", "mechanisms", "algorithms"}))))
    {
        return Fail(
            ProvinceContentStatus::RootNotWritable,
            "could not create the package directories"
        );
    }

    // --- Contract Package -------------------------------------------------
    PackageWriter contracts(contractRoot, 5);

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
    // Whether the region is water.
    //
    // Declared only when the corpus carried a terrain raster to decide it
    // from. A field that is always present but sometimes meaningless is worse
    // than one that is absent: content downstream can ask whether the Ruleset
    // has this field, and cannot ask whether the zeroes in it mean "land" or
    // "nobody looked".
    if (!imported.seaByIndex.empty())
    {
        component += "        field = { name = is_sea  kind = integer "
                     " required = yes  default = 0 }\n";
    }
    component += "    }\n";
    component += "}\n";
    if (!contracts.Emit(
            "components/geography.dcomponent",
            std::move(component),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "component schema");
    }

    // Gameplay, not geography: only the demo slice publishes a
    // Capability Contract for a UI to act through.
    if (slice != nullptr)
    {
        // The Capability Contract a UI acts through.
        //
        // Before this, a button carried `action = adjust_level` and the generic
        // control compiler compared that against a string literal it held itself:
        // a Demo's vocabulary written into the engine, and the public contract
        // bypassed entirely. The verb now lives here, in content, and the engine
        // knows only that a control names SOME contract and SOME operation the
        // contract declares.
        std::string capability;
        capability += "capability_contract = {\n";
        capability += "    name = " + slice->capabilityName + "\n";
        capability += "    version = 1\n";
        capability += "    deterministic = yes\n";
        capability += "    operations = { " + slice->capabilityOperation
            + " }\n";
        capability += "}\n";
        if (!contracts.Emit(
                "capabilities/site_development.dcapability",
                std::move(capability),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "capability contract");
        }
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
            Manifest(
                contractName,
                "contract",
                contracts.Digest(),
                0,
                {},
                // Only the demo slice publishes a Capability; a plain map's
                // Contract Package provides schemas and nothing else.
                slice != nullptr ? slice->capabilityName : std::string{}
            ),
            report))
    {
        return Fail(ProvinceContentStatus::WriteFailed, "contract manifest");
    }

    // --- Content Package --------------------------------------------------
    PackageWriter content(contentRoot, 8);

    std::string entities;
    entities.reserve(static_cast<std::size_t>(imported.Count()) * 24 + 512);
    entities += "entity_table = {\n";
    entities += "    entity_type = " + options.entityTypeName + "\n";
    entities += "    name_prefix = " + options.namePrefix + "\n";
    entities += "    component = {\n";
    entities += "        type = " + options.componentName + "\n";
    entities += "        schema_version = 1\n";
    entities += imported.seaByIndex.empty()
        ? "        columns = { source_id }\n"
        : "        columns = { source_id is_sea }\n";
    entities += "    }\n";
    entities += "    rows = {\n";
    for (std::uint32_t index = 1; index <= imported.Count(); ++index)
    {
        // The row's own suffix is the dense index, not the corpus id: the
        // index is what the raster and the palette carry, so a name built from
        // it is the one a renderer can reach without a second table.
        entities += "        row = { " + std::to_string(index) + "  "
            + std::to_string(imported.sourceIdByIndex[index]);
        if (!imported.seaByIndex.empty())
        {
            entities += "  ";
            entities += imported.seaByIndex[index] ? "1" : "0";
        }
        entities += " }\n";
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

    // One Definition, shared by every province.
    //
    // It binds no role: the province a given instance belongs to is a Spawn's
    // statement, not a Definition's. That is what lets 14187 instances share
    // one Definition instead of needing 14187 of them.
    // The demo's Definition and one Spawn per region. A plain map has
    // regions and borders and no mechanism to put on them.
    if (slice != nullptr)
    {
        std::string definition;
        definition += "mechanism_definition = {\n";
        definition += "    name = " + slice->mechanismDefinitionName + "\n";
        definition += "    mechanism = " + slice->mechanismName + "\n";
        definition += "    schema_version = 1\n";
        // What this Definition publicly offers. The translator refuses to
        // command an instance whose Definition never declared this, so a UI cannot
        // reach a mechanism that has not offered to be reached.
        definition += "    provides_capabilities = { " + slice->capabilityName
            + " }\n";
        definition += "    algorithm = " + slice->algorithmName + "\n";
        definition += "    algorithm_version = 1\n";
        definition += "}\n";
        if (!content.Emit(
                "definitions/site.ddefinition",
                std::move(definition),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "definition");
        }

        std::string spawns;
        spawns.reserve(static_cast<std::size_t>(imported.Count()) * 32 + 512);
        spawns += "spawn_table = {\n";
        spawns += "    mechanism = " + slice->mechanismName + "\n";
        spawns += "    definition = " + slice->mechanismDefinitionName + "\n";
        spawns += "    name_prefix = " + slice->spawnPrefix + "\n";
        spawns += "    role = province\n";
        spawns += "    target_entity_type = " + options.entityTypeName + "\n";
        spawns += "    target_prefix = " + options.namePrefix + "\n";
        spawns += "    rows = {\n";
        for (std::uint32_t index = 1; index <= imported.Count(); ++index)
        {
            const std::string suffix = std::to_string(index);
            spawns += "        row = { " + suffix + "  " + suffix + " }\n";
        }
        spawns += "    }\n";
        spawns += "}\n";
        report.spawns = imported.Count();
        if (!content.Emit("spawns/world.dspawntable", std::move(spawns), report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "spawn table");
        }
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
    if (slice != nullptr)
    {
        ruleset += "        requirement = { name = "
            + slice->mechanismPackageName
            + "  minimum_major = 1 minimum_minor = 0 minimum_patch = 0"
              "  maximum_major = 2 maximum_minor = 0 maximum_patch = 0 }\n";
    }
    ruleset += "    }\n";
    if (slice != nullptr)
    {
        ruleset += "    required_schemas = { " + slice->mechanismName
            + " = 1 }\n";
        ruleset += "    required_algorithms = { " + slice->algorithmName
            + " = 1 }\n";
        ruleset += "    required_definitions = {\n";
        ruleset += "        requirement = { mechanism = "
            + slice->mechanismName + "  name = "
            + slice->mechanismDefinitionName + " }\n";
        ruleset += "    }\n";
        // Every Spawn, one line. Naming 14187 requirements would move the
        // enormous file into the Ruleset rather than remove it, exactly as
        // it would for the entities and the borders.
        ruleset += "    required_spawns = { all = yes }\n";
    }
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

    // The whole Mechanism Package is the slice's.
    if (slice != nullptr)
    {
        // --- Mechanism Package ------------------------------------------------
        //
        // One production mechanism per province, which is what makes this a test
        // of the engine rather than of a picture. 14187 instances is two orders of
        // magnitude past Demo 0.5 and the first time the tick has been asked to
        // carry a real world.
        //
        // The algorithm is deliberately tiny. At this instance count every
        // instruction is multiplied by 14187, and what is being measured is the
        // floor -- what a world costs per tick before any interesting mechanic
        // exists. A large algorithm here would measure the algorithm.
        PackageWriter mechanisms(mechanismRoot, 4);

        std::string mechanism;
        mechanism += "mechanism_template = {\n";
        mechanism += "    name = " + slice->mechanismName + "\n";
        mechanism += "    version = 1\n";
        mechanism += "    fields = {\n";
        mechanism += "        field = { name = level   kind = integer "
                     " required = yes  default = 1 }\n";
        mechanism += "        field = { name = output  kind = decimal "
                     " required = yes  default = 0.0 }\n";
        mechanism += "    }\n";
        mechanism += "    roles = {\n";
        // minimum_count = 1 and bound by the Spawn, not the Definition. One shared
        // Definition plus one Spawn per province is how 14187 instances are told
        // apart; the Spawn registry is what enforces the minimum over the merged
        // bindings.
        mechanism += "        role = { name = province "
                     " reference_kind = entity "
                     " minimum_count = 1  maximum_count = 1 }\n";
        mechanism += "    }\n";
        mechanism += "}\n";
        if (!mechanisms.Emit(
                "mechanisms/production_site.dmechanism",
                std::move(mechanism),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "mechanism template");
        }

        std::string algorithm;
        algorithm += "algorithm_descriptor = {\n";
        algorithm += "    name = " + slice->algorithmName + "\n";
        algorithm += "    version = 1\n";
        algorithm += "    backend = declarative\n";
        algorithm += "    entry_points = { create tick }\n";
        algorithm += "    deterministic = yes\n";
        algorithm += "    execution_policy = { instruction_budget = 16 "
                     " failure_policy = fail_instance }\n";
        algorithm += "    program = {\n";
        algorithm += "        create = { transition_lifecycle = active }\n";
        algorithm += "        tick = {\n";
        // Reads the province through the role and writes the result to itself.
        // That is the whole mechanic: it exercises a cross-object read and a
        // computed write at map scale, and nothing else.
        algorithm += "            set_field = {\n";
        algorithm += "                field = output\n";
        algorithm += "                op    = mul\n";
        algorithm += "                left  = { role = province "
                     " component = " + options.componentName
                     + "  field = source_id }\n";
        algorithm += "                right = { self_field = level }\n";
        algorithm += "            }\n";
        algorithm += "        }\n";
        algorithm += "    }\n";
        algorithm += "}\n";
        if (!mechanisms.Emit(
                "algorithms/production.dalgorithm",
                std::move(algorithm),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "algorithm");
        }

        if (!mechanisms.Emit(
                "packages/mechanisms.dpackage",
                Manifest(
                    slice->mechanismPackageName,
                    "mechanism",
                    mechanisms.Digest(),
                    50,
                    contractName
                ),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "mechanism manifest");
        }
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

    PackageWriter presentation(presentationRoot, 6);
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
    // The raster's index -> source_id table.
    //
    // This is what makes a picked pixel name an ENTITY rather than a position
    // in a naming convention. The raster stores dense indices; the world knows
    // its regions by their corpus source_id; nothing else connects the two,
    // and before this the connection was
    // `namePrefix + std::to_string(index)` -- a rule about how the content
    // happened to be spelled, which stops being true the moment anyone renames
    // or reorders anything.
    //
    // It ships on the PRESENTATION side because it describes the raster's
    // encoding, not the world. The world already carries source_id, which is
    // the half that legitimately belongs to it.
    {
        std::string table;
        table.reserve(
            static_cast<std::size_t>(imported.Count() + 1) * 4u
        );
        // Little endian, index 0 first and reserved, so the file is a plain
        // array a loader can index without a header.
        for (std::uint32_t index = 0; index <= imported.Count(); ++index)
        {
            const std::uint32_t sourceId =
                index < imported.sourceIdByIndex.size()
                    ? imported.sourceIdByIndex[index]
                    : 0u;
            for (int shift = 0; shift < 32; shift += 8)
            {
                table.push_back(static_cast<char>(
                    static_cast<unsigned char>((sourceId >> shift) & 0xFFu)
                ));
            }
        }
        report.provinceIdBytes = table.size();
        report.provinceIdDigest = kernel::ComputeContentDigest(table);
        {
            std::ofstream stream(
                presentationRoot / "assets/rasters/world.dprovinceids",
                std::ios::binary | std::ios::trunc
            );
            if (!stream)
            {
                return Fail(
                    ProvinceContentStatus::WriteFailed, "province id table");
            }
            stream.write(
                table.data(),
                static_cast<std::streamsize>(table.size())
            );
            if (!stream)
            {
                return Fail(
                    ProvinceContentStatus::WriteFailed, "province id table");
            }
            report.bytes += table.size();
            ++report.files;
        }
        std::string idAsset;
        idAsset += "presentation_asset = {\n";
        idAsset += "    name = " + options.rasterAssetName + "_ids\n";
        idAsset += "    kind = map_province_ids\n";
        idAsset += "    asset = rasters/world.dprovinceids\n";
        idAsset += "    asset_digest = \""
            + report.provinceIdDigest + "\"\n";
        idAsset += "    properties = {\n";
        idAsset += "        format = source_id32\n";
        idAsset += "        count = "
            + std::to_string(imported.Count()) + "\n";
        // The world side of the correspondence, named by the Package
        // rather than assumed by the loader: which entities carry a
        // source_id, and in which component field.
        idAsset += "        entity_type = " + options.entityTypeName
            + "\n";
        idAsset += "        component = " + options.componentName
            + "\n";
        idAsset += "        component_version = 1\n";
        idAsset += "        source_id_field = source_id\n";
        idAsset += "    }\n";
        idAsset += "}\n";
        if (!presentation.Emit(
                "assets/world_province_ids.dasset",
                std::move(idAsset),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "province ids");
        }
    }

    // An interface needs a font; a map does not.
    if (slice != nullptr)
    {
        // The UI font. Same shape as the raster: an opaque payload the file
        // catalog never classifies, carried by its own digest rather than by the
        // Package content digest.
        //
        // The ATLAS is not baked here, and that is the point of taking a real font
        // library instead of a bitmap. An atlas is rasterised at one pixel size;
        // baking it would fix the interface to one size and one display, and the
        // reason FreeType is vendored at all is so the size is a runtime decision.
        // So the Package ships the font, and presentation rasterises from it.
        if (!slice->fontPath.empty())
        {
            std::string font;
            {
                std::ifstream stream(slice->fontPath, std::ios::binary);
                if (!stream)
                {
                    return Fail(ProvinceContentStatus::WriteFailed, "font source");
                }
                font.assign(
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()
                );
            }
            if (font.empty())
            {
                return Fail(ProvinceContentStatus::WriteFailed, "font source");
            }
            report.fontBytes = font.size();
            report.fontDigest = kernel::ComputeContentDigest(font);
            {
                std::ofstream stream(
                    presentationRoot / "assets/fonts/ui.ttf",
                    std::ios::binary | std::ios::trunc
                );
                if (!stream)
                {
                    return Fail(ProvinceContentStatus::WriteFailed, "font payload");
                }
                stream.write(
                    font.data(),
                    static_cast<std::streamsize>(font.size())
                );
                if (!stream)
                {
                    return Fail(ProvinceContentStatus::WriteFailed, "font payload");
                }
                report.bytes += font.size();
                ++report.files;
            }
            std::string fontAsset;
            fontAsset += "presentation_asset = {\n";
            fontAsset += "    name = " + slice->fontAssetName + "\n";
            fontAsset += "    kind = font\n";
            fontAsset += "    asset = fonts/ui.ttf\n";
            fontAsset += "    asset_digest = \"" + report.fontDigest
                + "\"\n";
            fontAsset += "    properties = {\n";
            fontAsset += "        format = truetype\n";
            // The size the interface is authored at. A content decision, so
            // it is declared rather than compiled in.
            fontAsset += "        pixel_size = 14\n";
            fontAsset += "        first_codepoint = 32\n";
            fontAsset += "        last_codepoint = 126\n";
            fontAsset += "    }\n";
            fontAsset += "}\n";
            if (!presentation.Emit(
                    "assets/ui_font.dasset",
                    std::move(fontAsset),
                    report))
            {
                return Fail(ProvinceContentStatus::WriteFailed, "font asset");
            }
        }
    }

    // The panel is the demo's interface to the demo's mechanism.
    if (slice != nullptr)
    {
        // A UI binding, and the reason the Presentation Package is worth having a
        // load-time check at all.
        //
        // It has no payload -- a binding is a declaration, not a file -- and it
        // states what a province panel reads. Those references are typed, so the
        // pipeline can refuse the Package if the Ruleset stops providing them,
        // instead of loading cleanly and showing an empty panel.
        std::string binding;
        binding += "presentation_asset = {\n";
        binding += "    name = " + options.rasterAssetName + "_panel\n";
        binding += "    kind = ui_binding\n";
        binding += "    requires = {\n";
        binding += "        mechanism_field = { mechanism = "
            + slice->mechanismName + "  definition = "
            + slice->mechanismDefinitionName + "  field = level }\n";
        binding += "        mechanism_field = { mechanism = "
            + slice->mechanismName + "  definition = "
            + slice->mechanismDefinitionName + "  field = output }\n";
        binding += "        component_field = { component = "
            + options.componentName + "  version = 1  field = source_id }\n";
        binding += "        capability = { name = " + slice->capabilityName
            + "  version = 1 }\n";
        binding += "    }\n";
        binding += "    properties = {\n";
        binding += "        title = province\n";
        // Which role of the Definition claims the Entity a control acts on.
        // The host used to carry this as a string literal, so a Package could
        // only be replaced by one that used the same word for the same idea.
        binding += "        subject_role = " + slice->subjectRoleName
            + "\n";
        binding += "    }\n";
        // The control tree. The Kernel stores this as an untyped tree of text and
        // assigns it no meaning; presentation::ControlTree is the only thing that
        // knows what a panel or a button is. Editing this block changes the
        // interface with no rebuild, which is the property memo section 4.4.4
        // asks for -- and every `field` here must appear in `requires` above, so a
        // Ruleset that stops providing one fails the load.
        binding += "    content = {\n";
        binding += "        panel = {\n";
        binding += "            id = province_panel\n";
        // Which control paints a surface is DECLARED here rather than inferred
        // from an id. The overlay used to paint whichever panel happened to be
        // called "province_panel", so renaming the root in a Package silently
        // lost the background.
        binding += "            background = yes\n";
        binding += "            axis = vertical\n";
        binding += "            padding = 8\n";
        binding += "            gap = 4\n";
        binding += "            label = { id = title  text = \"Province\""
            "  size = 20 }\n";
        binding += "            value = { id = level  text = \"Level: \""
            "  field = level  size = 20 }\n";
        binding += "            value = { id = output  text = \"Output: \""
            "  field = output  size = 20 }\n";
        binding += "            panel = {\n";
        binding += "                id = actions\n";
        binding += "                axis = horizontal\n";
        binding += "                gap = 4\n";
        binding += "                size = 24\n";
        binding += "                button = { id = raise  text = \"+1\""
            "  capability = " + slice->capabilityName
            + "  capability_version = 1"
            "  operation = " + slice->capabilityOperation
            + "  field = level  amount = 1  size = fill"
            "  background = yes }\n";
        binding += "                button = { id = lower  text = \"-1\""
            "  capability = " + slice->capabilityName
            + "  capability_version = 1"
            "  operation = " + slice->capabilityOperation
            + "  field = level  amount = -1  size = fill"
            "  background = yes }\n";
        binding += "            }\n";
        binding += "            panel = { id = spacer  size = fill }\n";
        binding += "        }\n";
        binding += "    }\n";
        binding += "}\n";
        if (!presentation.Emit(
                "assets/province_panel.dasset",
                std::move(binding),
                report))
        {
            return Fail(ProvinceContentStatus::WriteFailed, "panel binding");
        }
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
