#include "hoi3_1936_content_emitter.hpp"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

#include "package_content_digest.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

namespace {

struct ManifestDependency
{
    std::string name;
    bool required = true;
};

Hoi31936ContentReport Fail(
    Hoi31936ContentStatus status,
    std::string message
)
{
    Hoi31936ContentReport report;

    report.status = status;
    report.message = std::move(message);

    return report;
}

class PackageWriter
{
public:
    PackageWriter(
        std::filesystem::path root,
        std::size_t expectedFiles
    )
        : root_(std::move(root))
    {
        retained_.reserve(expectedFiles);
    }

    bool Emit(
        const std::string& relative,
        std::string text,
        Hoi31936ContentReport& report
    )
    {
        //
        // PackageContentSource owns only a string_view, so every source string
        // has to remain alive until the digest has been computed.
        //
        if (retained_.size() == retained_.capacity())
        {
            return false;
        }

        retained_.push_back(
            std::move(text)
        );

        const std::string& stored =
            retained_.back();

        std::ofstream stream(
            root_ / relative,
            std::ios::binary
                | std::ios::trunc
        );

        if (!stream)
        {
            return false;
        }

        stream.write(
            stored.data(),
            static_cast<std::streamsize>(
                stored.size()
            )
        );

        if (!stream)
        {
            return false;
        }

        report.bytes +=
            static_cast<std::uint64_t>(
                stored.size()
            );

        ++report.files;

        sources_.push_back({
            relative,
            stored
        });

        return true;
    }

    std::string Digest() const
    {
        return kernel::ComputePackageContentDigest(
            sources_
        );
    }

private:
    std::filesystem::path root_;

    std::vector<std::string>
        retained_;

    std::vector<
        kernel::PackageContentSource
    > sources_;
};

std::string Manifest(
    const std::string& name,
    const std::string& role,
    const std::string& digest,
    std::int32_t priority,
    const std::vector<
        ManifestDependency
    >& dependencies
)
{
    std::string text;

    text += "package_manifest = {\n";

    text += "    name = "
        + name
        + "\n";

    text += "    version_major = 1\n";
    text += "    version_minor = 0\n";
    text += "    version_patch = 0\n";

    text += "    role = "
        + role
        + "\n";

    text += "    content_digest = \""
        + digest
        + "\"\n";

    text += "    load_priority = "
        + std::to_string(priority)
        + "\n";

    if (!dependencies.empty())
    {
        text += "    dependencies = {\n";

        for (const ManifestDependency& dependency
            : dependencies)
        {
            text += "        dependency = {\n";

            text += "            name = "
                + dependency.name
                + "\n";

            text += "            minimum_major = 1\n";
            text += "            minimum_minor = 0\n";
            text += "            minimum_patch = 0\n";

            text += "            maximum_major = 2\n";
            text += "            maximum_minor = 0\n";
            text += "            maximum_patch = 0\n";

            text +=
                std::string(
                    "            required = "
                )
                + (
                    dependency.required
                        ? "yes\n"
                        : "no\n"
                );

            text += "        }\n";
        }

        text += "    }\n";
    }

    text += "}\n";

    return text;
}

bool MakeDirectories(
    const std::filesystem::path& root,
    std::initializer_list<
        const char*
    > directories
)
{
    std::error_code error;

    for (const char* directory
        : directories)
    {
        std::filesystem::create_directories(
            root / directory,
            error
        );

        if (error)
        {
            return false;
        }
    }

    return true;
}

std::string CountrySuffix(
    const content::CountryTag& tag
)
{
    std::string value =
        tag.ToString();

    for (char& character : value)
    {
        if (character >= 'A'
            && character <= 'Z')
        {
            character =
                static_cast<char>(
                    character
                    - 'A'
                    + 'a'
                );
        }
    }

    return value;
}

std::string RelationSchema(
    const std::string& relationName,
    const Hoi31936ContentOptions& options
)
{
    std::string text;

    text += "relation_schema = {\n";

    text += "    name = "
        + relationName
        + "\n";

    text += "    version = 1\n";

    text += "    source_type = "
        + options.countryEntityTypeName
        + "\n";

    text += "    target_type = "
        + options.mapEntityTypeName
        + "\n";

    text += "    allow_self = no\n";

    text += "}\n";

    return text;
}

void AppendRulesetPackageRequirement(
    std::string& text,
    const std::string& packageName
)
{
    text +=
        "        requirement = { name = "
        + packageName
        + "  minimum_major = 1"
          " minimum_minor = 0"
          " minimum_patch = 0"
          "  maximum_major = 2"
          " maximum_minor = 0"
          " maximum_patch = 0 }\n";
}

bool ReadDenseIndex(
    const std::map<
        std::uint32_t,
        std::uint32_t
    >& denseBySource,
    content::ProvinceDefinitionId province,
    std::uint32_t& output
)
{
    const auto found =
        denseBySource.find(
            province.value
        );

    if (found == denseBySource.end())
    {
        return false;
    }

    output = found->second;

    return true;
}

void BeginCountryProvinceRelationTable(
    std::string& text,
    const std::string& relation,
    const std::string& namePrefix,
    const Hoi31936ContentOptions& options
)
{
    text += "relation_table = {\n";

    text += "    relation = "
        + relation
        + "\n";

    text += "    schema_version = 1\n";

    text += "    name_prefix = "
        + namePrefix
        + "\n";

    text += "    source_entity_type = "
        + options.countryEntityTypeName
        + "\n";

    text += "    target_entity_type = "
        + options.mapEntityTypeName
        + "\n";

    text += "    source_prefix = "
        + options.countryEntityNamePrefix
        + "\n";

    text += "    target_prefix = "
        + options.mapEntityNamePrefix
        + "\n";

    text += "    rows = {\n";
}

void EndRelationTable(
    std::string& text
)
{
    text += "    }\n";
    text += "}\n";
}

}

Hoi31936ContentReport
EmitHoi31936PoliticalContent(
    const PoliticalSnapshot& snapshot,
    const std::vector<std::uint32_t>&
        provinceSourceIdByDenseIndex,
    const Hoi31936ContentOptions& options
)
{
    //
    // ---------------------------------------------------------------------
    // Input validation
    // ---------------------------------------------------------------------
    //
    if (options.root.empty())
    {
        return Fail(
            Hoi31936ContentStatus::RootNotWritable,
            "the Dillen game root is empty"
        );
    }

    //
    // This first emitter intentionally represents exactly one explicit
    // historical bookmark.
    //
    if (snapshot.date.year != 1936
        || snapshot.date.month != 1
        || snapshot.date.day != 1)
    {
        return Fail(
            Hoi31936ContentStatus::InvalidInput,
            "the first political-world emitter is intentionally pinned "
            "to 1936.1.1"
        );
    }

    if (provinceSourceIdByDenseIndex.size()
        < 2)
    {
        return Fail(
            Hoi31936ContentStatus::InvalidInput,
            "the province dense-index table is empty"
        );
    }

    Hoi31936ContentReport report;

    //
    // ---------------------------------------------------------------------
    // HOI3 Province ID -> Dillen map dense index
    // ---------------------------------------------------------------------
    //
    // Never assume HOI3 Province ID == raster index.
    //
    std::map<
        std::uint32_t,
        std::uint32_t
    > denseBySource;

    for (std::uint32_t dense = 1;
         dense
            < provinceSourceIdByDenseIndex.size();
         ++dense)
    {
        const std::uint32_t source =
            provinceSourceIdByDenseIndex[dense];

        if (source == 0)
        {
            continue;
        }

        if (!denseBySource.emplace(
                source,
                dense).second)
        {
            return Fail(
                Hoi31936ContentStatus::InvalidInput,
                "two dense province indices carry "
                "the same source Province ID "
                    + std::to_string(source)
            );
        }
    }

    //
    // ---------------------------------------------------------------------
    // Country ID -> canonical Dillen suffix
    // ---------------------------------------------------------------------
    //
    std::map<
        content::CountryDefinitionId,
        std::string
    > countrySuffixById;

    for (const PoliticalCountryState& country
        : snapshot.countries)
    {
        const std::string suffix =
            CountrySuffix(country.tag);

        if (!countrySuffixById.emplace(
                country.id,
                suffix).second)
        {
            return Fail(
                Hoi31936ContentStatus::InvalidInput,
                "the political snapshot contains "
                "a duplicate Country ID"
            );
        }
    }

    //
    // ---------------------------------------------------------------------
    // Generated Source Layers
    // ---------------------------------------------------------------------
    //
    const std::filesystem::path contractRoot =
        options.root
        / "country/contracts";

    const std::filesystem::path contentRoot =
        options.root
        / "country/hoi3_1936";

    const std::filesystem::path presentationRoot =
        options.root
        / "presentation/hoi3_1936";

    std::error_code error;

    for (const std::filesystem::path& generatedRoot
        : {
            contractRoot,
            contentRoot,
            presentationRoot
        })
    {
        std::filesystem::remove_all(
            generatedRoot,
            error
        );

        if (error)
        {
            return Fail(
                Hoi31936ContentStatus::RootNotWritable,
                "could not clear generated package directory: "
                    + generatedRoot.string()
            );
        }
    }

    if (!MakeDirectories(
            contractRoot,
            {
                "packages",
                "components",
                "relations/schemas"
            })
        || !MakeDirectories(
            contentRoot,
            {
                "packages",
                "entities",
                "relations/definitions",
                "rulesets"
            })
        || !MakeDirectories(
            presentationRoot,
            {
                "packages",
                "assets"
            }))
    {
        return Fail(
            Hoi31936ContentStatus::RootNotWritable,
            "could not create the 1936 political-world "
            "package directories"
        );
    }

    //
    // =====================================================================
    // Country Contract Package
    // =====================================================================
    //
    PackageWriter contracts(
        contractRoot,
        6
    );

    //
    // Country identity.
    //
    // source_tag is deliberately an import identity, not the Entity identity.
    //
    std::string identity;

    identity += "component_schema = {\n";

    identity += "    name = "
        + options.countryIdentityComponentName
        + "\n";

    identity += "    version = 1\n";

    identity += "    fields = {\n";

    identity +=
        "        field = {"
        " name = source_tag"
        "  kind = string"
        "  required = yes"
        "  default = \"---\""
        " }\n";

    // The country's colour, as a fact about the country.
    //
    // It used to live in the palette asset, which made it a fact about one
    // presentation of the world -- so anything else that wanted to draw a
    // country had to read that asset, and a second view would have had to
    // agree with it by hand. Packed 0x00RRGGBB in one integer: a Component
    // field is numeric, and three fields would be three chances to set two of
    // them.
    identity +=
        "        field = {"
        " name = colour"
        "  kind = integer"
        "  required = yes"
        "  default = 0"
        " }\n";

    identity += "    }\n";
    identity += "}\n";

    if (!contracts.Emit(
            "components/identity.dcomponent",
            std::move(identity),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "country identity schema"
        );
    }

    //
    // Territory ownership.
    //
    if (!contracts.Emit(
            "relations/schemas/owns_region.drelation",
            RelationSchema(
                options.ownershipRelationName,
                options
            ),
            report)
        || !contracts.Emit(
            "relations/schemas/controls_region.drelation",
            RelationSchema(
                options.controlRelationName,
                options
            ),
            report)
        || !contracts.Emit(
            "relations/schemas/core_on_region.drelation",
            RelationSchema(
                options.coreRelationName,
                options
            ),
            report)
        || !contracts.Emit(
            "relations/schemas/capital_region.drelation",
            RelationSchema(
                options.capitalRelationName,
                options
            ),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "country relation schema"
        );
    }

    report.contractDigest =
        contracts.Digest();

    if (!contracts.Emit(
            "packages/country_contracts.dpackage",
            Manifest(
                options.countryContractPackageName,
                "contract",
                report.contractDigest,
                10,
                {}
            ),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "country contract manifest"
        );
    }

    //
    // =====================================================================
    // 1936 Content Package
    // =====================================================================
    //
    PackageWriter content(
        contentRoot,
        7
    );

    //
    // ---------------------------------------------------------------------
    // Country Entities
    // ---------------------------------------------------------------------
    //
    std::string countries;

    countries.reserve(
        snapshot.countries.size()
            * 36
        + 512
    );

    countries += "entity_table = {\n";

    countries += "    entity_type = "
        + options.countryEntityTypeName
        + "\n";

    countries += "    name_prefix = "
        + options.countryEntityNamePrefix
        + "\n";

    countries += "    component = {\n";

    countries += "        type = "
        + options.countryIdentityComponentName
        + "\n";

    countries +=
        "        schema_version = 1\n";

    countries +=
        "        columns = { source_tag colour }\n";

    countries += "    }\n";

    countries += "    rows = {\n";

    for (const PoliticalCountryState& country
        : snapshot.countries)
    {
        // A country the corpus gave no colour is written as zero, which
        // the mode's `absent` colour then covers. Skipping the row instead
        // would leave an Entity with no Component and a hole in the table.
        std::uint32_t packed = 0;
        if (country.color)
        {
            packed = (static_cast<std::uint32_t>(country.color->red) << 16)
                | (static_cast<std::uint32_t>(country.color->green) << 8)
                | static_cast<std::uint32_t>(country.color->blue);
        }
        else
        {
            ++report.countriesWithoutColour;
        }
        countries +=
            "        row = { "
            + countrySuffixById.at(country.id)
            + "  \""
            + country.tag.ToString()
            + "\"  "
            + std::to_string(packed)
            + " }\n";
    }

    countries += "    }\n";
    countries += "}\n";

    report.countries =
        static_cast<std::uint32_t>(
            snapshot.countries.size()
        );

    if (!content.Emit(
            "entities/countries.dentitytable",
            std::move(countries),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "country entity table"
        );
    }

    //
    // ---------------------------------------------------------------------
    // Ownership
    // ---------------------------------------------------------------------
    //
    std::string ownerships;

    ownerships.reserve(
        snapshot.provinces.size()
            * 34
        + 512
    );

    BeginCountryProvinceRelationTable(
        ownerships,
        options.ownershipRelationName,
        "dillen.hoi3.1936.owner_",
        options
    );

    for (const PoliticalProvinceState& province
        : snapshot.provinces)
    {
        if (!province.owner)
        {
            continue;
        }

        std::uint32_t dense = 0;

        if (!ReadDenseIndex(
                denseBySource,
                province.id,
                dense))
        {
            ++report.unmappedProvinces;
            continue;
        }

        const auto country =
            countrySuffixById.find(
                *province.owner
            );

        if (country
            == countrySuffixById.end())
        {
            return Fail(
                Hoi31936ContentStatus::InvalidInput,
                "province owner is absent from "
                "the political snapshot"
            );
        }

        const std::string denseText =
            std::to_string(dense);

        //
        // name suffix / source suffix / target suffix
        //
        ownerships +=
            "        row = { "
            + denseText
            + "  "
            + country->second
            + "  "
            + denseText
            + " }\n";

        ++report.ownerships;
    }

    EndRelationTable(ownerships);

    if (!content.Emit(
            "relations/definitions/ownership.drelationtable",
            std::move(ownerships),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "ownership relation table"
        );
    }

    //
    // ---------------------------------------------------------------------
    // Controller
    // ---------------------------------------------------------------------
    //
    std::string controls;

    controls.reserve(
        snapshot.provinces.size()
            * 38
        + 512
    );

    BeginCountryProvinceRelationTable(
        controls,
        options.controlRelationName,
        "dillen.hoi3.1936.controller_",
        options
    );

    for (const PoliticalProvinceState& province
        : snapshot.provinces)
    {
        if (!province.controller)
        {
            continue;
        }

        std::uint32_t dense = 0;

        if (!ReadDenseIndex(
                denseBySource,
                province.id,
                dense))
        {
            continue;
        }

        const auto country =
            countrySuffixById.find(
                *province.controller
            );

        if (country
            == countrySuffixById.end())
        {
            return Fail(
                Hoi31936ContentStatus::InvalidInput,
                "province controller is absent "
                "from the political snapshot"
            );
        }

        const std::string denseText =
            std::to_string(dense);

        controls +=
            "        row = { "
            + denseText
            + "  "
            + country->second
            + "  "
            + denseText
            + " }\n";

        ++report.controls;
    }

    EndRelationTable(controls);

    if (!content.Emit(
            "relations/definitions/control.drelationtable",
            std::move(controls),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "control relation table"
        );
    }

    //
    // ---------------------------------------------------------------------
    // Core
    // ---------------------------------------------------------------------
    //
    std::string cores;

    cores.reserve(
        snapshot.provinces.size()
            * 48
        + 512
    );

    BeginCountryProvinceRelationTable(
        cores,
        options.coreRelationName,
        "dillen.hoi3.1936.core_",
        options
    );

    for (const PoliticalProvinceState& province
        : snapshot.provinces)
    {
        std::uint32_t dense = 0;

        if (!ReadDenseIndex(
                denseBySource,
                province.id,
                dense))
        {
            continue;
        }

        const std::string denseText =
            std::to_string(dense);

        for (const content::CountryDefinitionId core
            : province.cores)
        {
            const auto country =
                countrySuffixById.find(core);

            if (country
                == countrySuffixById.end())
            {
                return Fail(
                    Hoi31936ContentStatus::InvalidInput,
                    "province core is absent from "
                    "the political snapshot"
                );
            }

            cores +=
                "        row = { "
                + country->second
                + "_"
                + denseText
                + "  "
                + country->second
                + "  "
                + denseText
                + " }\n";

            ++report.cores;
        }
    }

    EndRelationTable(cores);

    if (!content.Emit(
            "relations/definitions/cores.drelationtable",
            std::move(cores),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "core relation table"
        );
    }

    //
    // ---------------------------------------------------------------------
    // Capital
    // ---------------------------------------------------------------------
    //
    std::string capitals;

    capitals.reserve(
        snapshot.countries.size()
            * 48
        + 512
    );

    BeginCountryProvinceRelationTable(
        capitals,
        options.capitalRelationName,
        "dillen.hoi3.1936.capital_",
        options
    );

    for (const PoliticalCountryState& country
        : snapshot.countries)
    {
        if (!country.capital)
        {
            continue;
        }

        std::uint32_t dense = 0;

        if (!ReadDenseIndex(
                denseBySource,
                *country.capital,
                dense))
        {
            continue;
        }

        const std::string suffix =
            countrySuffixById.at(
                country.id
            );

        capitals +=
            "        row = { "
            + suffix
            + "  "
            + suffix
            + "  "
            + std::to_string(dense)
            + " }\n";

        ++report.capitals;
    }

    EndRelationTable(capitals);

    if (!content.Emit(
            "relations/definitions/capitals.drelationtable",
            std::move(capitals),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "capital relation table"
        );
    }

    //
    // ---------------------------------------------------------------------
    // 1936 Root Ruleset
    // ---------------------------------------------------------------------
    //
    // This is deliberately a new Root Ruleset.
    //
    // The generic map's own Root stays untouched. A 1936 political world is
    // one composition of that map plus country semantics, not a modification
    // of the map Package itself.
    //
    std::string ruleset;

    ruleset += "root_ruleset = {\n";

    ruleset += "    name = "
        + options.rulesetName
        + "\n";

    ruleset += "    version = 1\n";

    ruleset += "    required_packages = {\n";

    AppendRulesetPackageRequirement(
        ruleset,
        options.mapContractPackageName
    );

    AppendRulesetPackageRequirement(
        ruleset,
        options.mapContentPackageName
    );

    AppendRulesetPackageRequirement(
        ruleset,
        options.countryContractPackageName
    );

    AppendRulesetPackageRequirement(
        ruleset,
        options.worldPackageName
    );

    ruleset += "    }\n";

    ruleset +=
        "    required_components = {\n";

    ruleset += "        "
        + options.mapComponentName
        + " = 1\n";

    ruleset += "        "
        + options.countryIdentityComponentName
        + " = 1\n";

    ruleset += "    }\n";

    ruleset +=
        "    required_relations = {\n";

    ruleset += "        "
        + options.mapRelationName
        + " = 1\n";

    ruleset += "        "
        + options.ownershipRelationName
        + " = 1\n";

    ruleset += "        "
        + options.controlRelationName
        + " = 1\n";

    ruleset += "        "
        + options.coreRelationName
        + " = 1\n";

    ruleset += "        "
        + options.capitalRelationName
        + " = 1\n";

    ruleset += "    }\n";

    //
    // Pull the complete selected world into the Runtime Catalog.
    //
    ruleset +=
        "    required_entity_definitions"
        " = { all = yes }\n";

    ruleset +=
        "    required_relation_definitions"
        " = { all = yes }\n";

    ruleset += "}\n";

    if (!content.Emit(
            "rulesets/political_world.druleset",
            std::move(ruleset),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "1936 root ruleset"
        );
    }

    //
    // Content Package depends on the map world because the territorial
    // Relation Definitions target Entity Definitions owned by that Package.
    //
    report.contentDigest =
        content.Digest();

    if (!content.Emit(
            "packages/political_world.dpackage",
            Manifest(
                options.worldPackageName,
                "content",
                report.contentDigest,
                110,
                {
                    {
                        options.mapContractPackageName,
                        true
                    },
                    {
                        options.mapContentPackageName,
                        true
                    },
                    {
                        options.countryContractPackageName,
                        true
                    }
                }
            ),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "1936 content manifest"
        );
    }

    //
    // =====================================================================
    // Country-colour Presentation Package
    // =====================================================================
    //
    // Country colour NEVER enters Authoritative World.
    //
    PackageWriter presentation(
        presentationRoot,
        2
    );

    std::string palette;

    palette += "presentation_asset = {\n";

    palette += "    name = "
        + options.modeSetAssetName
        + "\n";

    palette += "    kind = map_mode_set\n";

    palette += "    properties = {\n";

    palette += "        ownership_relation = "
        + options.ownershipRelationName
        + "\n";

    palette +=
        "        ownership_relation_version = 1\n";

    // Water, so the political map can tell an ocean from a region nobody
    // has claimed. Named here rather than known by the renderer: which
    // Two modes, declared rather than written.
    //
    // Political is a read path: from the province, one hop inward along
    // owns_region, then the owner's colour -- and the value read IS the
    // colour, so there is no table between the world and the picture. The
    // renderer never learns what "political" means; it uploads whichever
    // palette the selected mode produced.
    //
    // Adding a third mode is content from here on, not C++.
    palette += "        modes = 2\n";

    // The corpus is an equirectangular band and stops short of the poles, so
    // wrapping it onto a globe leaves a hole at each end that the renderer
    // fills flat. That fill is open ocean -- the Arctic and the Southern
    // Ocean both are -- so its colour is named here, the same 0x365A98 the
    // sea reads as, rather than left to a constant baked into the renderer.
    palette += "        polar_colour = 3564696\n";
    palette += "    }\n";
    palette += "    content = {\n";

    palette += "        mode = {\n";
    palette += "            id = political\n";
    palette += "            label = \"Political\"\n";
    palette += "            relation = " + options.ownershipRelationName
        + "\n";
    palette += "            relation_direction = incoming\n";
    palette += "            component = "
        + options.countryIdentityComponentName + "\n";
    palette += "            component_version = 1\n";
    palette += "            component_field = colour\n";
    palette += "            mapping = { kind = value }\n";
    // Water and unclaimed land both read nothing here -- neither is owned --
    // so political draws them alike and the terrain mode is what tells them
    // apart. Saying so is the point of `absent` being required.
    palette += "            absent = 3564696\n";
    palette += "        }\n";

    palette += "        mode = {\n";
    palette += "            id = terrain\n";
    palette += "            label = \"Land and sea\"\n";
    palette += "            component = " + options.mapComponentName + "\n";
    palette += "            component_version = 1\n";
    palette += "            component_field = is_sea\n";
    palette += "            mapping = {\n";
    palette += "                kind = lookup\n";
    palette += "                entry = { value = 0  colour = 7902066 }\n";
    palette += "                entry = { value = 1  colour = 3564696 }\n";
    palette += "            }\n";
    palette += "            absent = 2236962\n";
    palette += "        }\n";

    palette += "    }\n";
    palette += "}\n";

    if (!presentation.Emit(
            "assets/map_modes.dasset",
            std::move(palette),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "map mode set asset"
        );
    }

    report.presentationDigest =
        presentation.Digest();

    if (!presentation.Emit(
            "packages/political_presentation.dpackage",
            Manifest(
                options.presentationPackageName,
                "presentation",
                report.presentationDigest,
                210,
                {}
            ),
            report))
    {
        return Fail(
            Hoi31936ContentStatus::WriteFailed,
            "country presentation manifest"
        );
    }

    return report;
}

}