#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "map_entity_index.hpp"
#include "map_mode.hpp"
#include "presentation_view.hpp"
#include "standalone_session.hpp"

// The map modes, held to the world they colour.
//
// map_mode.cpp turns world state into a palette through a read path declared
// in content -- the same lowering an algorithm's reads go through. Nothing
// else in the suite exercises it, so a mode that quietly coloured the wrong
// provinces would reach a frame with the build still green.
//
// This does not compare the palette against itself. It recomputes the two
// facts a political map has to get right, each from a different corner of the
// world:
//
//   * which provinces have no owner   -- from the ownership Relation
//   * which provinces are open water  -- from dillen.map.geography
//
// A century of hand-authored province history produced the first; a terrain
// raster produced the second; the two were never reconciled. The political
// mode draws a province "absent" exactly when it has no owner, and the claim
// pinned here is that that set IS the water: every land province owned, every
// unowned one sea, the counts equal.
//
// Teeth: a mode naming a field the Ruleset does not have, and a malformed
// polar colour, are both refused at Bind. Without that, nothing above would
// show the binding validates anything at all.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kRoot = "Dillen-Game-1936";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "map mode: " << what << '\n';
        ++failures;
    }
}

void Mount(
    host::StandaloneSessionConfig& config,
    const char* name,
    const char* relative,
    int priority
)
{
    host::StandaloneSourceLayerConfig layer;
    layer.name = name;
    layer.root = kRoot / relative;
    layer.priority = priority;
    config.sources.push_back(std::move(layer));
}

const kernel::PresentationAsset* FindAsset(
    const std::vector<kernel::PresentationAsset>& assets,
    const std::string& kind
)
{
    for (const kernel::PresentationAsset& asset : assets)
    {
        if (asset.kind == kind)
        {
            return &asset;
        }
    }
    return nullptr;
}

// The `field` child of whichever `mode` block carries `id = <id>`.
kernel::PresentationAssetNode* FindModeChild(
    kernel::PresentationAsset& asset,
    const std::string& id,
    const std::string& field
)
{
    for (kernel::PresentationAssetNode& node : asset.content)
    {
        if (node.key != "mode" || !node.block)
        {
            continue;
        }
        bool isTarget = false;
        kernel::PresentationAssetNode* found = nullptr;
        for (kernel::PresentationAssetNode& child : node.children)
        {
            if (child.key == "id" && child.value == id)
            {
                isTarget = true;
            }
            if (child.key == field)
            {
                found = &child;
            }
        }
        if (isTarget)
        {
            return found;
        }
    }
    return nullptr;
}

}

int main()
{
    if (!fs::exists(kRoot))
    {
        std::cerr << "map mode: " << kRoot << " is missing\n";
        return 1;
    }

    host::StandaloneSessionConfig config;
    Mount(config, "world_map_contracts", "map/contracts", 0);
    Mount(config, "country_contracts", "country/contracts", 10);
    Mount(config, "world_map_content", "map/world", 100);
    Mount(config, "country_content", "country/hoi3_1936", 110);
    Mount(config, "world_map_presentation", "presentation/map_world", 200);
    Mount(config, "country_presentation", "presentation/hoi3_1936", 210);
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.hoi3.1936_world_root"),
        "dillen.hoi3.1936_world_root",
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
        std::cerr << "map mode: the 1936 world did not load\n";
        return 2;
    }

    const kernel::PresentationAsset* idAsset =
        FindAsset(session.PresentationAssets(), "map_province_ids");
    const kernel::PresentationAsset* modeAsset =
        FindAsset(session.PresentationAssets(), "map_mode_set");
    if (idAsset == nullptr || modeAsset == nullptr)
    {
        std::cerr << "map mode: the presentation packages are incomplete\n";
        return 2;
    }

    presentation::PresentationView view;
    if (!view.Advance(std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query())))
    {
        std::cerr << "map mode: nothing was published\n";
        return 2;
    }

    std::string message;
    presentation::MapEntityIndex entities;
    if (entities.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entities.Resolve(view) != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "map mode: the entity index failed: " << message << '\n';
        return 2;
    }

    presentation::MapModeSet modes;
    if (modes.Bind(session.Catalog(), *modeAsset, message)
        != presentation::MapModeStatus::Ok)
    {
        std::cerr << "map mode: bind failed: " << message << '\n';
        return 3;
    }

    // ---- terminal debug dump -------------------------------------------
    //
    // What the political viewer would feed renderer.SetPolarFill(), against
    // the constant the renderer ships with when nobody feeds it.
    std::printf("\n--- polar fill (the globe's caps) ---\n");
    if (modes.HasPolarColour())
    {
        const std::uint32_t c = modes.PolarColour();
        const unsigned r = (c >> 16) & 0xFFu;
        const unsigned g = (c >> 8) & 0xFFu;
        const unsigned b = c & 0xFFu;
        std::printf(
            "  declared    polar_colour = %u  (0x%06X)  ->  rgb(%u, %u, %u)\n",
            c, c, r, g, b);
        std::printf(
            "  viewer call SetPolarFill(%.3ff, %.3ff, %.3ff)\n",
            r / 255.0, g / 255.0, b / 255.0);
    }
    else
    {
        std::printf("  declared    <none> -- viewer keeps the default\n");
    }
    std::printf(
        "  renderer    default (map_renderer.cpp) = "
        "(0.860f, 0.900f, 0.940f)  the pale ice-white\n");

    std::printf("\n--- modes ---\n");
    for (std::size_t at = 0; at < modes.Count(); ++at)
    {
        const presentation::CompiledMapMode& m = modes.Mode(at);
        const std::uint32_t a = m.absent;
        std::printf(
            "  [%zu] %-14s absent = 0x%08X  (a=%u b=%u g=%u r=%u, the "
            "swizzled palette form)\n",
            at, m.id.c_str(), a, (a >> 24) & 0xFFu, (a >> 16) & 0xFFu,
            (a >> 8) & 0xFFu, a & 0xFFu);
        for (const presentation::MapModeLookupEntry& e : m.lookup)
        {
            std::printf(
                "        lookup %lld -> 0x%08X\n",
                static_cast<long long>(e.value), e.colour);
        }
    }
    std::printf("\n");
    std::fflush(stdout);
    // -------------------------------------------------------------------

    Check(modes.HasPolarColour(), "the asset declares no polar colour");
    Check(
        modes.PolarColour() == 3564696u,
        "the polar colour is not the 0x365A98 the sea reads as"
    );

    const std::size_t political = modes.Find("political");
    const std::size_t terrain = modes.Find("terrain");
    Check(political != modes.Count(), "there is no political mode");
    Check(terrain != modes.Count(), "there is no terrain mode");
    if (political == modes.Count() || terrain == modes.Count())
    {
        std::cerr << "map mode: " << failures << " failure(s)\n";
        return 4;
    }

    // --- the world, read without reference to any mode --------------------
    const kernel::RelationTypeId ownership =
        kernel::StableRelationTypeId("dillen.country.owns_region");
    const kernel::ComponentTypeId geography =
        kernel::StableComponentTypeId("dillen.map.geography");
    const std::optional<kernel::ComponentFieldSlotId> isSea =
        session.Catalog().ResolveComponentFieldSlot(geography, 1, "is_sea");
    Check(isSea.has_value(), "the geography component has no is_sea field");
    if (!isSea)
    {
        std::cerr << "map mode: " << failures << " failure(s)\n";
        return 4;
    }

    const runtime::WorldQuerySnapshot& world = view.World();
    const std::uint32_t count = entities.Count();

    std::vector<char> unowned(static_cast<std::size_t>(count) + 1u, 0);
    std::vector<char> sea(static_cast<std::size_t>(count) + 1u, 0);
    std::uint32_t unownedCount = 0;
    std::uint32_t seaCount = 0;
    std::uint32_t noGeography = 0;
    for (std::uint32_t index = 1; index <= count; ++index)
    {
        const kernel::EntityId province = entities.EntityFor(index);
        if (!province)
        {
            continue;
        }
        if (world.Relations().Incoming(ownership, province).empty())
        {
            unowned[index] = 1;
            ++unownedCount;
        }
        const kernel::MechanismValue* value =
            world.Components().FindField(province, geography, *isSea);
        if (value == nullptr)
        {
            ++noGeography;
            continue;
        }
        const auto* number = std::get_if<std::int64_t>(&value->data);
        if (number != nullptr && *number != 0)
        {
            sea[index] = 1;
            ++seaCount;
        }
    }
    Check(
        noGeography == 0,
        "some province carries no geography, so the terrain mode cannot be "
        "total"
    );

    // --- political: absent iff unowned, and the unowned are the sea ------
    Check(
        modes.Refresh(view, entities, political)
            == presentation::MapModeStatus::Ok,
        "political refresh failed"
    );
    const std::uint32_t politicalAbsent = modes.Mode(political).absent;
    {
        const std::vector<std::uint32_t>& palette = modes.Palette();
        std::uint32_t mismatched = 0;
        for (std::uint32_t index = 1; index <= count; ++index)
        {
            if (!entities.EntityFor(index))
            {
                continue;
            }
            const bool painted = palette[index] == politicalAbsent;
            if (painted != (unowned[index] != 0))
            {
                ++mismatched;
            }
        }
        Check(
            mismatched == 0,
            std::to_string(mismatched)
                + " provinces are drawn absent by the political mode without "
                  "being unowned, or the other way round"
        );
    }
    Check(
        modes.Absent() == unownedCount,
        "the political mode read no value for " + std::to_string(modes.Absent())
            + " provinces; " + std::to_string(unownedCount) + " have no owner"
    );
    Check(
        unownedCount == seaCount,
        std::to_string(unownedCount) + " provinces have no owner but "
            + std::to_string(seaCount)
            + " are sea -- some land is unclaimed, or some sea is owned"
    );

    // --- terrain: the lookup paints land as land and sea as sea ---------
    Check(
        modes.Refresh(view, entities, terrain)
            == presentation::MapModeStatus::Ok,
        "terrain refresh failed"
    );
    Check(
        modes.Absent() == 0,
        "the terrain mode left " + std::to_string(modes.Absent())
            + " provinces with no value, but every province has geography"
    );
    std::uint32_t seaColour = 0;
    std::uint32_t landColour = 0;
    for (const presentation::MapModeLookupEntry& entry
        : modes.Mode(terrain).lookup)
    {
        if (entry.value == 1)
        {
            seaColour = entry.colour;
        }
        if (entry.value == 0)
        {
            landColour = entry.colour;
        }
    }
    Check(
        seaColour != 0 && landColour != 0 && seaColour != landColour,
        "the terrain lookup does not give land and sea distinct colours"
    );
    {
        const std::vector<std::uint32_t>& palette = modes.Palette();
        std::uint32_t wrong = 0;
        for (std::uint32_t index = 1; index <= count; ++index)
        {
            if (!entities.EntityFor(index))
            {
                continue;
            }
            const std::uint32_t want = sea[index] ? seaColour : landColour;
            if (palette[index] != want)
            {
                ++wrong;
            }
        }
        Check(
            wrong == 0,
            std::to_string(wrong)
                + " provinces are the wrong land/sea colour in the terrain mode"
        );
    }

    // --- teeth: Bind refuses a mode it cannot resolve -------------------
    {
        kernel::PresentationAsset broken = *modeAsset;
        kernel::PresentationAssetNode* field =
            FindModeChild(broken, "political", "component_field");
        Check(field != nullptr, "could not reach political's component_field");
        if (field != nullptr)
        {
            field->value = "not_a_real_field";
            presentation::MapModeSet rejected;
            std::string why;
            Check(
                rejected.Bind(session.Catalog(), broken, why)
                    != presentation::MapModeStatus::Ok,
                "a mode naming a field the Ruleset lacks was accepted"
            );
        }
    }
    {
        kernel::PresentationAsset broken = *modeAsset;
        broken.properties["polar_colour"] = "not a number";
        presentation::MapModeSet rejected;
        std::string why;
        Check(
            rejected.Bind(session.Catalog(), broken, why)
                != presentation::MapModeStatus::Ok,
            "a malformed polar_colour was accepted"
        );
    }

    if (failures != 0)
    {
        std::cerr << "map mode: " << failures << " failure(s)\n";
        return 5;
    }
    std::cout << "map mode: political draws " << unownedCount
              << " provinces absent, every one of them sea; terrain splits "
              << seaCount << " sea / " << (count - seaCount) << " land\n";
    return 0;
}
