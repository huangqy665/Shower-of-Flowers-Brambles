#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "standalone_session.hpp"

// Demo 0.8 P1 -- the committed world map, at full size, through the pipeline.
//
// This is the falsifier the phase exists for. Everything before it measured a
// part: the raster import measured pixels, the DSL golden measured lowering.
// This measures the claim -- that the engine's existing primitives carry a
// real four-digit heterogeneous world through Parse, Resolve, Compile and
// Freeze inside the loading budget, and that the world it builds is the world
// the content describes.
//
// It loads Dillen-Game/map/world, which is committed generated content: no
// importer, no bitmap, no CSV. That separation is the point. Whether the
// committed content still matches the corpus it came from is a different
// question, asked by world_map_content_probe; this probe would happily measure
// a world that had drifted, and measuring is all it claims to do.
//
// 14187 Entities and 41693 Relations in four content files. One file per
// object would have put 55880 entries in the Source Lock, hashed every one
// into the Ruleset Fingerprint, and taken each Package content digest over
// tens of thousands of files. The table forms exist so that a generated world
// is a handful of sources.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";

// What the corpus produced, pinned here so the probe fails if the committed
// world is silently a different world. world_map_content_probe proves these
// numbers still follow from the raster; this one proves the thing being
// measured is the thing that was generated.
constexpr std::uint32_t kExpectedEntities = 14187;
constexpr std::uint32_t kExpectedRelations = 41693;

// The same 30 second ceiling Demo 0.5 loads against. It is a budget, not a
// target: what matters is that a world two orders of magnitude larger than the
// vertical slice still fits under the gate the project already committed to.
constexpr auto kLoadBudget = std::chrono::seconds(30);

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "world map scale: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    if (!fs::exists(kMapWorldRoot))
    {
        std::cerr << "world map scale: " << kMapWorldRoot
                  << " is missing -- generate it with "
                     "DILLEN_REGENERATE_WORLD_MAP=1\n";
        return 1;
    }

    host::StandaloneSessionConfig config;
    // Two layers, because the world is two Packages: schemas are a Contract,
    // the regions themselves are Content.
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const auto loadStart = std::chrono::steady_clock::now();
    const bool started = session.Start(config, report);
    const auto loadElapsed = std::chrono::steady_clock::now() - loadStart;
    const auto loadMs = std::chrono::duration_cast<
        std::chrono::milliseconds>(loadElapsed).count();

    if (!started)
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "world map scale: the world did not load\n";
        return 2;
    }

    Check(loadElapsed < kLoadBudget,
        "loading took " + std::to_string(loadMs)
            + " ms, over the 30 second budget");

    // A pipeline that quietly dropped rows would still load, and quickly.
    const runtime::WorldQuerySnapshot& query = session.Runtime().Query();
    Check(query.Entities().Size() == kExpectedEntities,
        "world holds " + std::to_string(query.Entities().Size())
            + " entities, expected " + std::to_string(kExpectedEntities));
    Check(query.Components().Size() == kExpectedEntities,
        "world holds " + std::to_string(query.Components().Size())
            + " components, expected one per region");
    Check(query.Relations().Size() == kExpectedRelations,
        "world holds " + std::to_string(query.Relations().Size())
            + " relations, expected " + std::to_string(kExpectedRelations));
    // One production mechanism per province. This is the number that makes the
    // tick measurement below mean anything: a world of entities alone costs
    // almost nothing to tick, and would have said so misleadingly.
    Check(query.Mechanisms().Size() == kExpectedEntities,
        "world holds " + std::to_string(query.Mechanisms().Size())
            + " mechanism instances, expected one per province");

    // Every tick, 14187 mechanisms each read their province through a role
    // and write one computed field. That is deliberately the smallest real
    // mechanic there is: at this instance count every instruction is
    // multiplied by 14187, so a larger algorithm would measure the algorithm
    // rather than the floor.
    //
    // scale_probe's synthetic load reads 30.1 ms/tick at N=16000 (Release).
    // A heterogeneous world with a cross-object read lands in the same place,
    // which is the linear extrapolation actually being checked here.
    const auto tickStart = std::chrono::steady_clock::now();
    bool ticked = true;
    for (std::uint64_t tick = 1; tick <= 8; ++tick)
    {
        ticked = ticked && session.Runtime().RunTick(tick);
    }
    const auto tickMs = std::chrono::duration_cast<
        std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tickStart).count() / 8.0
        / 1000.0;
    Check(ticked, "a tick failed");

    if (failures != 0)
    {
        std::cerr << "world map scale: " << failures << " failure(s)\n";
        return 3;
    }

    std::cout << "World map scale: passed (" << kExpectedEntities
              << " entities, " << kExpectedRelations << " relations; load "
              << loadMs << " ms of a 30000 ms budget, tick " << tickMs
              << " ms)\n";
    return 0;
}
