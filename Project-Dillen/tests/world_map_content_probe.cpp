#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "province_content_emitter.hpp"
#include "province_raster_import.hpp"

// Demo 0.8 P1b -- the committed world is exactly what the corpus produces.
//
// Dillen-Game/map/world is generated content: 14187 region Entities and 41693
// border Relations, derived from the raster in Dillen-Game/map/source. It is
// committed rather than generated at load time, because the runtime must be
// able to open a world without an importer, a bitmap, or a CSV anywhere near
// it -- that is the whole point of generating Dillen Content instead of
// reading the corpus directly.
//
// Committed generated content has one failure mode that hand-written content
// does not: it can drift from the thing it was generated from, and nobody
// reads two megabytes of table to notice. So the check is not "does it load"
// but "is it byte-for-byte what the corpus produces today". A single differing
// byte means either the corpus changed, the importer changed, or someone
// edited generated content by hand, and all three need to be seen.
//
// Set DILLEN_REGENERATE_WORLD_MAP=1 to rewrite the committed tree instead of
// checking it. That is the only supported way to change it.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapSourceRoot = kGameRoot / "map/source";

// std::getenv is deprecated by MSVC's secure CRT (C4996), and this suite is
// built warning-clean. _dupenv_s is the sanctioned replacement there; every
// other toolchain keeps std::getenv. Silencing it with _CRT_SECURE_NO_WARNINGS
// would turn the warning off for every file in the target, not this one line.
bool EnvironmentFlagIsSet(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return false;
    }
    const bool set = std::string(value) == "1";
    std::free(value);
    return set;
#else
    const char* const value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
#endif
}

bool ReadFile(const fs::path& path, std::string& output)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    output.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
    return true;
}

// Every file the emitter writes, in a fixed order.
const std::vector<std::string>& EmittedFiles()
{
    static const std::vector<std::string> files = {
        "map/contracts/capabilities/site_development.dcapability",
        "map/contracts/components/geography.dcomponent",
        "map/contracts/relations/schemas/borders.drelation",
        "map/contracts/packages/contracts.dpackage",
        "production/map_world/mechanisms/production_site.dmechanism",
        "production/map_world/algorithms/production.dalgorithm",
        "production/map_world/packages/mechanisms.dpackage",
        "map/world/entities/world.dentitytable",
        "map/world/definitions/site.ddefinition",
        "map/world/spawns/world.dspawntable",
        "map/world/relations/definitions/world.drelationtable",
        "map/world/rulesets/world.druleset",
        "map/world/packages/world.dpackage",
        "presentation/map_world/assets/world_raster.dasset",
        "presentation/map_world/assets/world_province_ids.dasset",
        "presentation/map_world/assets/ui_font.dasset",
        "presentation/map_world/assets/province_panel.dasset",
        "presentation/map_world/packages/presentation.dpackage",
        // The binary payload. Not an authoring source and not in any Package
        // content digest -- its integrity rides on asset_digest in the
        // declaration -- but it is generated, so it is compared like the rest.
        "presentation/map_world/assets/rasters/world.dmapindex",
        "presentation/map_world/assets/rasters/world.dprovinceids",
        "presentation/map_world/assets/fonts/ui.ttf"
    };
    return files;
}

}

int main()
{
    adapter::ProvinceRasterImportOptions importOptions;
    importOptions.raster = kMapSourceRoot / "provinces.bmp";
    importOptions.definitions = kMapSourceRoot / "definition.csv";
    // The terrain raster, which is what decides land from sea. Without
    // it the world knows only that 3547 regions have no owner, and
    // cannot say which of those are ocean.
    importOptions.terrain = kMapSourceRoot / "terrain.bmp";
    // The map in Dillen-Game is already north-up, so no corpus flip. The
    // option stays because HOI3's own bitmaps are not: a corpus imported
    // straight from that game needs it set, and which way round a given
    // corpus is cannot be inferred -- it has to be stated.
    // province_raster_import_probe is the gate either way.
    importOptions.northAtImageBottom = false;

    if (!fs::exists(importOptions.raster))
    {
        std::cerr << "world map content: the map corpus is missing\n";
        return 1;
    }
    const adapter::ProvinceRasterImport imported =
        adapter::ImportProvinceRaster(importOptions);
    if (!imported)
    {
        std::cerr << "world map content: import failed: " << imported.message
                  << '\n';
        return 2;
    }

    const bool rewriting = EnvironmentFlagIsSet("DILLEN_REGENERATE_WORLD_MAP");

    adapter::ProvinceContentOptions options;
    // The demo's gameplay, asked for by name. Leaving it out emits a plain
    // map -- regions, borders, a raster and an id table -- which is what
    // province_map_emitter_probe loads.
    adapter::DemoProductionSlice slice;
    slice.fontPath = fs::path("Dillen-Game/presentation/fonts")
        / "RobotoMono-Regular.ttf";
    options.slice = slice;
    options.root = rewriting
        ? kGameRoot
        : fs::temp_directory_path() / "dillen_world_map_check";
    const adapter::ProvinceContentReport emitted =
        adapter::EmitProvinceContent(imported, options);
    if (!emitted)
    {
        std::cerr << "world map content: emit failed: " << emitted.message
                  << '\n';
        return 3;
    }

    if (rewriting)
    {
        std::cout << "World map content: REGENERATED " << kGameRoot << " ("
                  << emitted.entities << " entities, " << emitted.relations
                  << " relations, digest " << emitted.contentDigest << ")\n";
        return 0;
    }

    int failures = 0;
    for (const std::string& relative : EmittedFiles())
    {
        std::string fresh;
        std::string committed;
        if (!ReadFile(options.root / relative, fresh))
        {
            std::cerr << "world map content: the emitter did not write "
                      << relative << '\n';
            ++failures;
            continue;
        }
        if (!ReadFile(kGameRoot / relative, committed))
        {
            std::cerr << "world map content: " << relative
                      << " is missing from " << kGameRoot
                      << " -- regenerate with "
                         "DILLEN_REGENERATE_WORLD_MAP=1\n";
            ++failures;
            continue;
        }
        if (fresh != committed)
        {
            // Byte counts first, because a line-ending difference gives a size
            // difference and nothing else, and that is the failure this file's
            // .gitattributes entry exists to prevent.
            std::cerr << "world map content: " << relative
                      << " differs from what the corpus produces (committed "
                      << committed.size() << " bytes, fresh " << fresh.size()
                      << ")\n";
            ++failures;
        }
    }

    std::error_code error;
    fs::remove_all(options.root, error);

    if (failures != 0)
    {
        std::cerr << "world map content: " << failures << " failure(s)\n";
        return 4;
    }
    std::cout << "World map content: passed (" << EmittedFiles().size()
              << " generated files match the corpus, " << emitted.entities
              << " entities, " << emitted.relations << " relations)\n";
    return 0;
}
