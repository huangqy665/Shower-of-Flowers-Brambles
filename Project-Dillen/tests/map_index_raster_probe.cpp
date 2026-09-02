#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "map_index_raster.hpp"
#include "province_raster_import.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P3a -- the first Presentation Asset, end to end.
//
// A Presentation Package now owns something: the map index raster, declared as
// a `.dasset` and carried as a binary payload the file catalog never
// classifies. This probe loads it the way a renderer will and holds it to the
// properties that make that safe.
//
// The one worth stating plainly: the payload is outside every Package content
// digest, because it is not an authoring source. `asset_digest` in the
// declaration is the ONLY thing tying the declaration to the bytes, so the
// loader verifies it before decoding, and this probe corrupts the payload to
// prove the check is real rather than decorative.
//
// It also re-asserts the P0 boundary against content that finally exists:
// loading a Presentation Package must not move the Ruleset Fingerprint. That
// was easy to satisfy when presentation owned nothing.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapSourceRoot = kGameRoot / "map/source";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "map index raster: " << what << '\n';
        ++failures;
    }
}

host::StandaloneSessionConfig Config(bool withPresentation)
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    if (withPresentation)
    {
        config.sources.push_back({
            "world_map_presentation",
            kMapPresentationRoot,
            200,
            {},
            {},
            {}
        });
    }
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}


// Counts assets of one kind. Asserting on kinds rather than on a total is not
// pedantry: the Package gained a font between two rounds of this work, and a
// probe that counted assets failed for a reason that had nothing to do with
// what it was testing.
std::size_t CountKind(
    const std::vector<kernel::PresentationAsset>& assets,
    const std::string& kind
)
{
    std::size_t total = 0;
    for (const kernel::PresentationAsset& asset : assets)
    {
        if (asset.kind == kind)
        {
            ++total;
        }
    }
    return total;
}

}

int main()
{
    if (!fs::exists(kMapPresentationRoot))
    {
        std::cerr << "map index raster: the presentation package is missing"
                     " -- regenerate with DILLEN_REGENERATE_WORLD_MAP=1\n";
        return 1;
    }

    host::StandaloneSession bare;
    host::StandaloneSessionReport bareReport;
    host::StandaloneSession skinned;
    host::StandaloneSessionReport skinnedReport;
    if (!bare.Start(Config(false), bareReport)
        || !skinned.Start(Config(true), skinnedReport))
    {
        for (const std::string& diagnostic : skinnedReport.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "map index raster: a session failed to start\n";
        return 2;
    }

    // --- the P0 boundary, now that presentation carries real content ---
    Check(bare.Catalog().Fingerprint() == skinned.Catalog().Fingerprint(),
        "loading a Presentation Package moved the Ruleset Fingerprint");
    Check(bare.Catalog().LockedPackages().Size()
            == skinned.Catalog().LockedPackages().Size(),
        "a Presentation Package entered the Package Lock");
    Check(bare.PresentationAssets().empty(),
        "assets appeared without a Presentation Package");
    Check(CountKind(skinned.PresentationAssets(), "map_index_raster") == 1
            && CountKind(skinned.PresentationAssets(), "ui_binding") == 1,
        "the skin does not carry one raster and one panel binding");
    // Presentation has an identity of its own; it is simply not the one the
    // simulation is sealed with.
    Check(!static_cast<bool>(bare.PresentationFingerprint()),
        "a world with no skin has a presentation fingerprint");
    Check(static_cast<bool>(skinned.PresentationFingerprint()),
        "a world with a skin has no presentation fingerprint");

    if (skinned.PresentationAssets().empty())
    {
        std::cerr << "map index raster: nothing to load\n";
        return 3;
    }
    // Picked by kind rather than by position: the Package declares more than
    // one asset now, and front() would silently become the wrong one the next
    // time an asset is added ahead of it.
    const kernel::PresentationAsset* found = nullptr;
    for (const kernel::PresentationAsset& candidate
        : skinned.PresentationAssets())
    {
        if (candidate.kind == "map_index_raster")
        {
            found = &candidate;
        }
    }
    if (found == nullptr)
    {
        std::cerr << "map index raster: no map_index_raster asset\n";
        return 3;
    }
    const kernel::PresentationAsset& asset = *found;

    const auto start = std::chrono::steady_clock::now();
    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(asset);
    const double decodeMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count()) / 1000.0;
    if (!raster)
    {
        std::cerr << "map index raster: load failed: " << raster.message
                  << '\n';
        return 4;
    }

    Check(raster.width == 5616 && raster.height == 2160,
        "decoded raster is not 5616x2160");
    Check(raster.provinceCount == 14187,
        "decoded raster declares "
            + std::to_string(raster.provinceCount) + " provinces");
    Check(raster.indices.size()
            == static_cast<std::size_t>(raster.width) * raster.height,
        "decoded raster is the wrong size");

    // --- the decoded raster is the corpus raster ---
    //
    // Round-tripping through RLE and a digest proves the payload is intact; it
    // does not prove the payload was ever right. Only the corpus can say that,
    // so the corpus is asked.
    adapter::ProvinceRasterImportOptions importOptions;
    importOptions.raster = kMapSourceRoot / "provinces.bmp";
    importOptions.definitions = kMapSourceRoot / "definition.csv";
    // The map in Dillen-Game is already north-up, so no corpus flip. The
    // option stays because HOI3's own bitmaps are not: a corpus imported
    // straight from that game needs it set, and which way round a given
    // corpus is cannot be inferred -- it has to be stated.
    // province_raster_import_probe is the gate either way.
    importOptions.northAtImageBottom = false;

    const adapter::ProvinceRasterImport imported =
        adapter::ImportProvinceRaster(importOptions);
    if (!imported)
    {
        std::cerr << "map index raster: the corpus could not be re-imported: "
                  << imported.message << '\n';
        return 5;
    }
    Check(raster.indices == imported.indexRaster,
        "the decoded raster differs from the corpus raster");

    // --- the digest check is real ---
    //
    // Flip one byte of the payload and the loader must refuse before it
    // decodes anything. Without this the declaration and the bytes could drift
    // apart silently: nothing else covers this file.
    const fs::path payload =
        fs::path(asset.source.physicalDirectory) / asset.assetPath;
    std::string original;
    {
        std::ifstream stream(payload, std::ios::binary);
        original.assign(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        );
    }
    {
        std::string tampered = original;
        // A byte in the middle of a run count, so the decode would otherwise
        // succeed and quietly produce a slightly different world.
        tampered[tampered.size() / 2] =
            static_cast<char>(tampered[tampered.size() / 2] ^ 0x01);
        std::ofstream stream(payload, std::ios::binary | std::ios::trunc);
        stream.write(
            tampered.data(),
            static_cast<std::streamsize>(tampered.size())
        );
    }
    const presentation::MapIndexRaster tampered =
        presentation::LoadMapIndexRaster(asset);
    Check(!tampered
            && tampered.status
                == presentation::MapIndexRasterStatus::PayloadDigestMismatch,
        "a tampered payload was accepted");
    {
        std::ofstream stream(payload, std::ios::binary | std::ios::trunc);
        stream.write(
            original.data(),
            static_cast<std::streamsize>(original.size())
        );
    }
    // Restored, and proven restored -- a probe that left the repository
    // corrupted on its way out would be worse than no probe.
    const presentation::MapIndexRaster restored =
        presentation::LoadMapIndexRaster(asset);
    Check(static_cast<bool>(restored),
        "the payload was not restored after tampering");

    if (failures != 0)
    {
        std::cerr << "map index raster: " << failures << " failure(s)\n";
        return 6;
    }

    std::cout << "Map index raster: passed (" << raster.width << "x"
              << raster.height << ", " << raster.provinceCount
              << " provinces, " << (original.size() / 1024)
              << " KiB payload for "
              << (raster.indices.size() * 2 / 1024 / 1024)
              << " MiB of indices, decode " << decodeMs
              << " ms, presentation fingerprint "
              << skinned.PresentationFingerprint().ToHex() << ")\n";
    return 0;
}
