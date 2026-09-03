#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "province_content_emitter.hpp"
#include "province_raster_import.hpp"
#include "standalone_session.hpp"

// A map, and nothing but a map.
//
// province_content_emitter used to emit four Packages in one call: the
// geography, a production mechanism, its algorithm, a Capability Contract and
// a two-button panel. That is a demo generator, and shipping it under the name
// of a map Adapter would mean every future map arrived with one demo's
// gameplay welded on -- discoverable only by reading nine hundred lines.
//
// The gameplay is now an optional DemoProductionSlice, and this probe is what
// makes "optional" a fact. It emits WITHOUT the slice, loads the result, and
// checks the world that comes up is geography: regions, borders, a raster and
// an id table, a Ruleset that loads them, and no mechanism, no spawn and no
// interface anywhere.
//
// The assertion that matters is the negative one. "The map loads" would pass
// for an emitter that quietly produced the mechanism anyway; "the world has
// zero mechanism instances" is the one that cannot.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kMapSourceRoot = fs::path("Dillen-Game") / "map/source";

constexpr std::uint32_t kExpectedEntities = 14187;
constexpr std::uint32_t kExpectedRelations = 41693;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "province map emitter: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    adapter::ProvinceRasterImportOptions importOptions;
    importOptions.raster = kMapSourceRoot / "provinces.bmp";
    importOptions.definitions = kMapSourceRoot / "definition.csv";
    importOptions.northAtImageBottom = false;
    if (!fs::exists(importOptions.raster))
    {
        std::cerr << "province map emitter: the map corpus is missing from "
                  << kMapSourceRoot << '\n';
        return 1;
    }
    const adapter::ProvinceRasterImport imported =
        adapter::ImportProvinceRaster(importOptions);
    if (!imported)
    {
        std::cerr << "province map emitter: import failed: "
                  << imported.message << '\n';
        return 2;
    }

    const fs::path root =
        fs::temp_directory_path() / "dillen_plain_map_check";
    std::error_code error;
    fs::remove_all(root, error);

    // No slice. This is the whole point of the probe.
    adapter::ProvinceContentOptions options;
    options.root = root;
    const adapter::ProvinceContentReport emitted =
        adapter::EmitProvinceContent(imported, options);
    if (!emitted)
    {
        std::cerr << "province map emitter: emit failed: " << emitted.message
                  << '\n';
        return 3;
    }

    // Nothing of the demo's was written at all -- not an empty Package, not a
    // Package with no definitions in it. The files are absent.
    Check(!fs::exists(root / "production/map_world"),
        "a map without a slice emitted a Mechanism Package");
    Check(!fs::exists(root / "map/world/definitions"),
        "a map without a slice emitted a Mechanism Definition");
    Check(!fs::exists(root / "map/world/spawns"),
        "a map without a slice emitted Spawns");
    Check(!fs::exists(root / "map/contracts/capabilities"),
        "a map without a slice published a Capability Contract");
    Check(!fs::exists(
            root / "presentation/map_world/assets/province_panel.dasset"),
        "a map without a slice emitted a UI binding");
    Check(!fs::exists(root / "presentation/map_world/assets/fonts"),
        "a map without a slice emitted a font");

    // The geography is all there.
    Check(fs::exists(root / "map/contracts/components/geography.dcomponent"),
        "the map has no geography component");
    Check(fs::exists(root / "map/world/entities/world.dentitytable"),
        "the map has no entity table");
    Check(fs::exists(
            root / "presentation/map_world/assets/rasters/world.dmapindex"),
        "the map has no index raster");
    Check(fs::exists(
            root / "presentation/map_world/assets/rasters/world.dprovinceids"),
        "the map has no province id table");

    // --- and it loads --------------------------------------------------
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "plain_map_contracts", root / "map/contracts", 0, {}, {}, {}
    });
    config.sources.push_back({
        "plain_map_world", root / "map/world", 100, {}, {}, {}
    });
    config.sources.push_back({
        "plain_map_presentation", root / "presentation/map_world",
        200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(config, report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "province map emitter: a map without gameplay would not "
                     "load\n";
        return 4;
    }

    const runtime::WorldQuerySnapshot query = session.Runtime().Query();
    Check(query.Entities().Size() == kExpectedEntities,
        "the plain map holds " + std::to_string(query.Entities().Size())
            + " entities, expected " + std::to_string(kExpectedEntities));
    Check(query.Relations().Size() == kExpectedRelations,
        "the plain map holds " + std::to_string(query.Relations().Size())
            + " relations, expected " + std::to_string(kExpectedRelations));
    // The negative assertion, and the reason this probe exists.
    Check(query.Mechanisms().Size() == 0,
        "a map emitted without a gameplay slice came up with "
            + std::to_string(query.Mechanisms().Size())
            + " mechanism instances");

    // Presentation carries the raster and the id table, and nothing that
    // describes an interface.
    std::size_t bindings = 0;
    std::size_t rasters = 0;
    std::size_t idTables = 0;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "ui_binding")
        {
            ++bindings;
        }
        else if (asset.kind == "map_index_raster")
        {
            ++rasters;
        }
        else if (asset.kind == "map_province_ids")
        {
            ++idTables;
        }
    }
    Check(rasters == 1 && idTables == 1,
        "the plain map's Presentation Package is not one raster and one id "
        "table");
    Check(bindings == 0,
        "a map emitted without a gameplay slice declared a UI binding");

    // The world still ticks. A map with no mechanisms is a world with nothing
    // to do, and it has to do nothing successfully rather than fail.
    bool ticked = true;
    for (std::uint64_t tick = 1; tick <= 4; ++tick)
    {
        ticked = ticked && session.Runtime().RunTick(tick);
    }
    Check(ticked, "a map with no mechanisms could not tick");

    fs::remove_all(root, error);

    if (failures != 0)
    {
        std::cerr << "province map emitter: " << failures << " failure(s)\n";
        return 5;
    }
    std::cout << "Province map emitter: passed (" << kExpectedEntities
              << " regions and " << kExpectedRelations
              << " borders load with no mechanism, no spawn and no interface)"
              << std::endl;
    return 0;
}
