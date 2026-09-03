// apps/hoi3_1936_import/main.cpp
//
// Reads the HOI3 corpus committed in Dillen-Game and writes the 1936 political
// world as native Dillen Content: country Entities, ownership / control / core
// / capital Relations, and a colour palette Presentation asset.
//
// It does not emit the map unless asked. The map committed in Dillen-Game is
// held byte for byte by world_map_content_probe, and regenerating it as a side
// effect of producing countries would put one responsibility in two places.
// The raster is still imported either way, because the political content needs
// sourceIdByIndex to turn HOI3 province ids into the dense indices the map
// Entities are addressed by.
//
// --with-map emits it as well, into the output root. That is how a
// self-contained political world is produced: Dillen-Game's own map Package
// carries the demo production Definition, so a Ruleset of countries alone
// cannot load it.

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "province_raster_import.hpp"
#include "province_content_emitter.hpp"

#include "political_corpus.hpp"
#include "political_snapshot.hpp"
#include "hoi3_1936_content_emitter.hpp"

namespace fs = std::filesystem;

using namespace dillen;

namespace
{

void PrintUsage()
{
    std::cout
        << "usage:\n"
        << "  dillen_hoi3_1936_import <corpus-root> [output-root] "
           "[--with-map]\n"
        << "\n"
        << "  corpus-root  a Dillen-Game tree holding the HOI3 corpus under\n"
        << "               common/corpus/hoi3, history/corpus/hoi3 and\n"
        << "               map/source\n"
        << "  output-root  where the generated Packages are written\n"
        << "               (default: corpus-root)\n"
        << "  --with-map   also emit the map Packages, so the output root is\n"
        << "               a complete world on its own. Off by default: the\n"
        << "               map committed in Dillen-Game is held byte for byte\n"
        << "               by world_map_content_probe, and regenerating it as\n"
        << "               a side effect of producing countries would put one\n"
        << "               responsibility in two places.\n";
}

// The corpus in this repository is filed BY DOMAIN -- common/corpus/hoi3,
// history/corpus/hoi3, map/source -- while the parser's file templates are
// written against HOI3's own layout, which is filed BY TYPE:
// common/countries/, history/countries/, map/definition.csv. The two nest in
// opposite orders, so no single root satisfies both.
//
// virtualPrefix is what closes that gap: each layer mounts one domain
// directory at the path the templates expect. Pointing a single layer at
// Dillen-Game instead would be the dangerous option, not merely a wrong one:
// common/countries/ exists there but is EMPTY, and common/countries.txt exists
// but is Dillen's own placeholder rather than the corpus tag list -- so two of
// the five templates would match, both with nothing in them, and the import
// would report success over an empty world.
bool MountCorpus(
    const fs::path& corpusRoot,
    parser::hoi3::PoliticalCorpusImportOptions& options,
    std::string& missing
)
{
    struct Mount
    {
        const char* relativeRoot;
        const char* mountedAs;
        const char* probe;
    };

    // Each probe is a file or directory that must exist under the mount. They
    // are checked here rather than left to produce an empty parse, because
    // "the corpus is not where this expects it" and "the corpus says nothing"
    // are different answers and only one of them is this tool's fault.
    static const Mount mounts[] = {
        {"common/corpus/hoi3", "common", "countries.txt"},
        {"history/corpus/hoi3", "history", "countries"},
        {"map/source", "map", "definition.csv"},
    };

    parser::SourceLayerId nextId = 1;
    int priority = 0;
    for (const Mount& mount : mounts)
    {
        const fs::path root = corpusRoot / mount.relativeRoot;
        if (!fs::exists(root / mount.probe))
        {
            missing = (root / mount.probe).string();
            return false;
        }

        parser::SourceLayer layer;
        layer.id = nextId++;
        layer.name = mount.mountedAs;
        layer.root = root;
        layer.priority = priority++;
        layer.virtualPrefix = mount.mountedAs;
        options.layers.push_back(std::move(layer));
    }
    return true;
}

}

int main(
    int argc,
    char** argv
)
{
    std::vector<std::string> positional;
    bool withMap = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument(argv[index]);
        if (argument == "--with-map")
        {
            withMap = true;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.empty() || positional.size() > 2)
    {
        PrintUsage();
        return 1;
    }

    const fs::path corpusRoot = fs::path(positional[0]);
    const fs::path outputRoot =
        positional.size() == 2 ? fs::path(positional[1]) : corpusRoot;

    //
    // ------------------------------------------------------------
    // 1. Import the province raster -- for its id mapping only.
    // ------------------------------------------------------------
    //
    adapter::ProvinceRasterImportOptions mapOptions;

    mapOptions.raster =
        corpusRoot / "map" / "source" / "provinces.bmp";

    mapOptions.definitions =
        corpusRoot / "map" / "source" / "definition.csv";

    mapOptions.terrain = corpusRoot / "map" / "source" / "terrain.bmp";
    mapOptions.wrapHorizontally = true;

    // The raster committed in Dillen-Game has already been turned the right
    // way up; only a corpus taken straight out of HOI3 needs the flip. Getting
    // this wrong mirrors the world without failing, so it follows where the
    // file came from rather than which game wrote it.
    mapOptions.northAtImageBottom = false;

    std::cout << "Importing province raster (for the id mapping)...\n";

    const adapter::ProvinceRasterImport imported =
        adapter::ImportProvinceRaster(mapOptions);

    if (!imported)
    {
        std::cerr
            << "province raster import failed: "
            << imported.message
            << '\n';
        return 2;
    }

    std::cout << "  provinces: " << imported.Count() << '\n';

    if (withMap)
    {
        // No DemoProductionSlice: geography, borders, the raster and the id
        // table, and nothing that plays.
        //
        // This matters more than it looks. The map committed in Dillen-Game
        // was generated WITH the demo slice, so its geography Package carries
        // a Mechanism Definition that needs a Package the political Ruleset
        // does not require -- a world of countries cannot load it without
        // also spawning 14187 production sites. Emitted here, the output root
        // is a political world and nothing else.
        adapter::ProvinceContentOptions mapContent;
        mapContent.root = outputRoot;
        mapContent.slice.reset();

        std::cout << "Emitting the map Packages...\n";
        const adapter::ProvinceContentReport mapReport =
            adapter::EmitProvinceContent(imported, mapContent);
        if (!mapReport)
        {
            std::cerr << "map content emission failed: " << mapReport.message
                      << '\n';
            return 7;
        }
        std::cout
            << "  regions: " << mapReport.entities
            << "   borders: " << mapReport.relations << '\n';
    }

    //
    // ------------------------------------------------------------
    // 2. Parse the HOI3 political corpus.
    // ------------------------------------------------------------
    //
    compatibility::hoi3::content::DefinitionRegistry definitions;

    parser::DiagnosticBag diagnostics;

    parser::hoi3::PoliticalCorpusImportOptions corpus;

    std::string missing;
    if (!MountCorpus(corpusRoot, corpus, missing))
    {
        std::cerr
            << "the corpus is not where this expects it: "
            << missing << " is absent\n";
        PrintUsage();
        return 3;
    }

    std::cout << "Importing HOI3 country/history corpus...\n";

    if (!parser::hoi3::ImportPoliticalCorpus(
            corpus,
            definitions,
            diagnostics))
    {
        std::cerr << "HOI3 political corpus import failed\n";
        for (const parser::Diagnostic& issue : diagnostics.All())
        {
            std::cerr << "  " << parser::FormatDiagnostic(issue) << '\n';
        }
        return 4;
    }

    //
    // ------------------------------------------------------------
    // 3. Evaluate history at 1936.1.1.
    // ------------------------------------------------------------
    //
    compatibility::hoi3::worldbuilder::PoliticalSnapshot snapshot;
    compatibility::hoi3::worldbuilder::PoliticalSnapshotReport snapshotReport;

    std::cout << "Building political state for 1936.1.1...\n";

    if (!compatibility::hoi3::worldbuilder::BuildPoliticalSnapshot(
            definitions,
            {1936, 1, 1},
            snapshot,
            snapshotReport))
    {
        std::cerr << "political snapshot failed\n";
        for (const auto& issue : snapshotReport.issues)
        {
            std::cerr << "  " << issue.code << ": " << issue.message << '\n';
        }
        return 5;
    }

    std::cout
        << "  countries: " << snapshot.countries.size() << '\n'
        << "  provinces: " << snapshot.provinces.size() << '\n';

    //
    // ------------------------------------------------------------
    // 4. Project that source state into native Dillen Content.
    // ------------------------------------------------------------
    //
    compatibility::hoi3::worldbuilder::Hoi31936ContentOptions politicalContent;

    politicalContent.root = outputRoot;

    std::cout << "Emitting Dillen 1936 political content...\n";

    const auto politicalReport =
        compatibility::hoi3::worldbuilder::EmitHoi31936PoliticalContent(
            snapshot,
            // HOI3 Province ID -> Dillen raster dense index, which is how the
            // map's region Entities are addressed.
            imported.sourceIdByIndex,
            politicalContent
        );

    if (!politicalReport)
    {
        std::cerr
            << "1936 political content emission failed: "
            << politicalReport.message
            << '\n';
        return 6;
    }

    std::cout
        << '\n'
        << "1936 world generated successfully.\n"
        << '\n'
        << "Countries:    " << politicalReport.countries << '\n'
        << "Ownerships:   " << politicalReport.ownerships << '\n'
        << "Controls:     " << politicalReport.controls << '\n'
        << "Cores:        " << politicalReport.cores << '\n'
        << "Capitals:     " << politicalReport.capitals << '\n'
        << "Unmapped:     " << politicalReport.unmappedProvinces << '\n';

    return 0;
}
