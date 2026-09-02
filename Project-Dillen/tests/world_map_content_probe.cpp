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
// Dillen-Game/world is generated content: 14187 region Entities and 41693
// border Relations, derived from the raster in Dillen-Game/content/map. It is
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

const fs::path kMapRoot = "Dillen-Game/content/map";
const fs::path kWorldRoot = "Dillen-Game/world";

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
        "contracts/components/geography.dcomponent",
        "contracts/relations/schemas/borders.drelation",
        "contracts/packages/contracts.dpackage",
        "content/entities/world.dentitytable",
        "content/relations/definitions/world.drelationtable",
        "content/rulesets/world.druleset",
        "content/packages/world.dpackage",
        "presentation/assets/world_raster.dasset",
        "presentation/packages/presentation.dpackage",
        // The binary payload. Not an authoring source and not in any Package
        // content digest -- its integrity rides on asset_digest in the
        // declaration -- but it is generated, so it is compared like the rest.
        "presentation/assets/rasters/world.dmapindex"
    };
    return files;
}

}

int main()
{
    adapter::ProvinceRasterImportOptions importOptions;
    importOptions.raster = kMapRoot / "provinces.bmp";
    importOptions.definitions = kMapRoot / "definition.csv";
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

    const char* regenerate = std::getenv("DILLEN_REGENERATE_WORLD_MAP");
    const bool rewriting = regenerate != nullptr
        && std::string(regenerate) == "1";

    adapter::ProvinceContentOptions options;
    options.root = rewriting
        ? kWorldRoot
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
        std::cout << "World map content: REGENERATED " << kWorldRoot << " ("
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
        if (!ReadFile(kWorldRoot / relative, committed))
        {
            std::cerr << "world map content: " << relative
                      << " is missing from " << kWorldRoot
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
