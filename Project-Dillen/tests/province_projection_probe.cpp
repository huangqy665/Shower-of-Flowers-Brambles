#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "map_entity_index.hpp"
#include "province_projection.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P2 -- the presentation read model, on the real world.
//
// A renderer never walks the world. It uploads a table indexed by the same
// dense province index its id raster carries, and the shader does a texel
// fetch. This probe holds that table to the three properties that make it safe
// to sit between an authoritative tick and a frame:
//
//   * it is a pure function of the snapshot -- the same snapshot twice gives
//     byte-identical values;
//   * it says what the world says -- row i really is province i, checked
//     against the corpus ids the content carries;
//   * it never writes -- the world after projecting is the world before, to
//     the byte, which is the claim the const-only PresentationView is supposed
//     to make structural rather than hoped-for.
//
// It also measures. P1 predicted a full rescan of 14187 provinces would cost a
// few milliseconds per publication and concluded incremental update was an
// optimisation rather than a prerequisite. That prediction is worth a number
// rather than a memory.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";
constexpr std::uint32_t kExpectedProvinces = 14187;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "province projection: " << what << '\n';
        ++failures;
    }
}

runtime::WorldQuerySnapshotHandle Publish(host::StandaloneSession& session)
{
    return std::make_shared<const runtime::WorldQuerySnapshot>(
        session.Runtime().Query()
    );
}

}

int main()
{
    if (!fs::exists(kMapWorldRoot))
    {
        std::cerr << "province projection: " << kMapWorldRoot
                  << " is missing -- generate it with "
                     "DILLEN_REGENERATE_WORLD_MAP=1\n";
        return 1;
    }

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
    // The Presentation Package, for the raster's id table. A probe that
    // addresses Entities has to read the same table the viewer does; building
    // the correspondence any other way would be the naming convention again,
    // in a test.
    config.sources.push_back({
        "world_map_presentation",
        kMapPresentationRoot,
        200,
        {},
        {},
        {}
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
        std::cerr << "province projection: the world did not load\n";
        return 2;
    }

    presentation::ProvinceProjectionSpec spec;
    spec.entityTypeName = "dillen.map.region";
    spec.count = kExpectedProvinces;
    spec.columns.push_back({"dillen.map.geography", 1, "source_id"});

    presentation::PresentationView view;
    if (!view.Advance(Publish(session)))
    {
        std::cerr << "province projection: nothing was published\n";
        return 4;
    }

    // The row -> Entity table comes from data now: the id table the raster
    // ships plus the source_id the world carries. It used to be
    // `namePrefix + std::to_string(row)`, which is an assumption about how
    // the content is spelled rather than a lookup.
    std::string message;
    presentation::MapEntityIndex entities;
    const kernel::PresentationAsset* idAsset = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "map_province_ids")
        {
            idAsset = &asset;
            break;
        }
    }
    if (idAsset == nullptr
        || entities.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entities.Resolve(view) != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "province projection: the entity index failed: "
                  << message << '\n';
        return 3;
    }

    presentation::ProvinceProjection projection;
    const presentation::ProvinceProjectionStatus bound =
        projection.Bind(session.Catalog(), spec, entities, message);
    if (bound != presentation::ProvinceProjectionStatus::Ok)
    {
        std::cerr << "province projection: bind failed: " << message << '\n';
        return 3;
    }

    const auto start = std::chrono::steady_clock::now();
    const presentation::ProvinceProjectionStatus refreshed =
        projection.Refresh(view);
    const double refreshMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count()) / 1000.0;
    if (refreshed != presentation::ProvinceProjectionStatus::Ok)
    {
        std::cerr << "province projection: refresh failed\n";
        return 5;
    }

    // --- shape ---
    Check(projection.Count() == kExpectedProvinces,
        "projected " + std::to_string(projection.Count()) + " provinces");
    Check(projection.Columns() == 1, "expected one column");
    Check(projection.Values().size()
            == static_cast<std::size_t>(kExpectedProvinces + 1),
        "value table is the wrong size");
    Check(projection.MissingRows() == 0,
        std::to_string(projection.MissingRows())
            + " provinces are missing from the world");

    // --- row 0 is the reserved hole ---
    //
    // The id raster paints 0 outside the map. A renderer indexes this table
    // with whatever it read, so row 0 has to exist and has to be zero.
    Check(projection.Value(0, 0) == 0, "row 0 is not zero");

    // --- the table says what the world says ---
    //
    // The reference corpus is contiguous, so province index i carries corpus
    // id i. That is a property of this world, not of the projection, which is
    // why it is checked against the content rather than assumed by it: a
    // projection that silently misaligned by one row would still be a valid
    // table of the right size.
    bool aligned = true;
    for (std::uint32_t index = 1; index <= kExpectedProvinces; ++index)
    {
        aligned = aligned
            && projection.Value(index, 0)
                == static_cast<std::int64_t>(index);
    }
    Check(aligned, "row i does not carry province i's source_id");

    // --- purity: the same snapshot gives the same bytes ---
    const std::vector<std::int64_t> first = projection.Values();
    Check(projection.Refresh(view)
            == presentation::ProvinceProjectionStatus::Ok,
        "the second refresh failed");
    Check(projection.Values() == first,
        "refreshing the same snapshot twice produced different values");

    presentation::ProvinceProjection second;
    Check(second.Bind(session.Catalog(), spec, entities, message)
            == presentation::ProvinceProjectionStatus::Ok,
        "the second projection failed to bind");
    Check(second.Refresh(view) == presentation::ProvinceProjectionStatus::Ok,
        "the second projection failed to refresh");
    Check(second.Values() == first,
        "two projections of one snapshot disagree");

    // --- the projection does not write ---
    //
    // PresentationView hands out const access only, so this cannot fail
    // without someone having cast it away. That is exactly why it is worth
    // asserting: the boundary is structural and should be seen to hold.
    const std::uint64_t revisionBefore = view.Stamp().revision;
    const std::size_t entitiesBefore = view.World().Entities().Size();
    const std::size_t componentsBefore = view.World().Components().Size();
    Check(projection.Refresh(view)
            == presentation::ProvinceProjectionStatus::Ok,
        "the third refresh failed");
    Check(view.Stamp().revision == revisionBefore
            && view.World().Entities().Size() == entitiesBefore
            && view.World().Components().Size() == componentsBefore,
        "projecting changed the world it read");

    // --- a tick moves the view forward, and the table follows ---
    Check(static_cast<bool>(session.Runtime().RunTick(1)), "tick 1 failed");
    const runtime::WorldQuerySnapshotHandle next = Publish(session);
    Check(view.Advance(next), "the view refused a newer publication");
    Check(projection.Refresh(view)
            == presentation::ProvinceProjectionStatus::Ok,
        "refresh after a tick failed");
    Check(projection.Stamp().publication == next->Stamp().publication,
        "the projection did not record the snapshot it read");
    // Nothing in this world changes on a tick, so the values must not either.
    Check(projection.Values() == first,
        "an idle tick changed the projected values");

    // --- the palette has no ceiling of its own -------------------------
    //
    // The renderer used to hold a fixed 128x128 palette: 16384 texels, so a
    // map of more than 16383 regions would have wrapped onto other regions'
    // colours with nothing reporting it. The size is derived now, and this is
    // where the derivation is checked -- for counts the shipped map cannot
    // reach, which is exactly why the GPU smoke test cannot check it.
    {
        const std::uint32_t counts[] = {
            0u, 1u, 3u, 4u, 255u, 14187u, 16383u, 16384u, 16385u, 65534u
        };
        for (const std::uint32_t count : counts)
        {
            const std::uint32_t side = presentation::PaletteSideFor(count);
            Check(static_cast<std::uint64_t>(side) * side
                    >= static_cast<std::uint64_t>(count) + 1u,
                "a palette of side " + std::to_string(side)
                    + " cannot hold " + std::to_string(count)
                    + " regions and row 0");
            // A power of two, and no larger than it has to be: halving it
            // must stop being enough.
            Check((side & (side - 1u)) == 0u,
                "the palette side " + std::to_string(side)
                    + " is not a power of two");
            if (side > 1u)
            {
                const std::uint32_t half = side / 2u;
                Check(static_cast<std::uint64_t>(half) * half
                        < static_cast<std::uint64_t>(count) + 1u,
                    "the palette for " + std::to_string(count)
                        + " regions is twice as large as it needs to be");
            }
        }
        // The point of the change, stated as one assertion: past the old
        // ceiling the palette grows.
        Check(presentation::PaletteSideFor(16384u) > 128u,
            "a map larger than 16383 regions still gets a 128 palette");
    }

    if (failures != 0)
    {
        std::cerr << "province projection: " << failures << " failure(s)\n";
        return 6;
    }

    std::cout << "Province projection: passed (" << projection.Count()
              << " provinces x " << projection.Columns()
              << " column, full rescan " << refreshMs << " ms)\n";
    return 0;
}
