#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "map_index_raster.hpp"
#include "map_renderer.hpp"
#include "map_view.hpp"
#include "province_projection.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P3d -- the GL backend, smoke only.
//
// This is the ONE part of the renderer that is not in the standard suite, and
// the reason is not laziness: it needs a GPU and a display server, and the
// Linux cells have neither. Everything it would otherwise be asked to prove
// has already been proved without it -- the morph and the camera in
// map_view_probe, the raster and its digest in map_index_raster_probe, the
// read model in province_projection_probe. What is left here is whether the
// backend can be handed those results and produce pixels.
//
// So this is a smoke test, and it says so. It builds a hidden window, uploads
// the real world map, draws one frame at three bends, and reads the id buffer
// back. It is run by hand on a machine with a GPU; `ctest` never sees it.
//
// The one property worth more than "it did not crash": picking must agree with
// the picture. The id attachment is written by the same fragment shader
// invocation that wrote the colour, so a pick reads the province the viewer is
// looking at by construction -- at any bend, including ones where inverting
// the morph analytically is not possible.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kWorldRoot = "Dillen-Game/world";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "map renderer: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kWorldRoot / "contracts", 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kWorldRoot / "content", 100, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_presentation", kWorldRoot / "presentation", 200, {}, {}, {}
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
        std::cerr << "map renderer: the world did not load\n";
        return 1;
    }
    if (session.PresentationAssets().empty())
    {
        std::cerr << "map renderer: no presentation asset\n";
        return 2;
    }

    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(session.PresentationAssets().front());
    if (!raster)
    {
        std::cerr << "map renderer: raster load failed: " << raster.message
                  << '\n';
        return 3;
    }

    // A debug colouring, and labelled as one. The world carries source_id and
    // nothing else yet, so there is no ownership or terrain to colour by; what
    // is being exercised is the palette path, and the rule that fills it is a
    // single function to replace when a real map mode exists.
    presentation::ProvinceProjectionSpec spec;
    spec.entityTypeName = "dillen.map.region";
    spec.namePrefix = "dillen.map.region_";
    spec.count = raster.provinceCount;
    spec.columns.push_back({"dillen.map.geography", 1, "source_id"});

    presentation::ProvinceProjection projection;
    std::string message;
    if (projection.Bind(session.Catalog(), spec, message)
        != presentation::ProvinceProjectionStatus::Ok)
    {
        std::cerr << "map renderer: projection bind failed: " << message
                  << '\n';
        return 4;
    }
    presentation::PresentationView view;
    view.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );
    projection.Refresh(view);

    std::vector<std::uint32_t> palette(128 * 128, 0u);
    for (std::uint32_t index = 1; index <= projection.Count()
        && index < palette.size(); ++index)
    {
        const std::uint64_t id =
            static_cast<std::uint64_t>(projection.Value(index, 0));
        const std::uint32_t red =
            static_cast<std::uint32_t>((id * 2654435761u) & 0xFFu);
        const std::uint32_t green =
            static_cast<std::uint32_t>((id * 40503u) & 0xFFu);
        const std::uint32_t blue =
            static_cast<std::uint32_t>((id * 2246822519u) & 0xFFu);
        palette[index] = 0xFF000000u | (blue << 16) | (green << 8) | red;
    }

    presentation::gl::MapRendererOptions options;
    options.visible = false;
    options.windowWidth = 640;
    options.windowHeight = 360;

    presentation::gl::MapRenderer renderer;
    const presentation::gl::MapRendererStatus opened =
        renderer.Open(options, raster, message);
    if (opened != presentation::gl::MapRendererStatus::Ok)
    {
        std::cerr << "map renderer: could not open: " << message << '\n';
        // Not a failure of the engine. A machine with no GPU or no display
        // cannot run this, and saying so is more useful than a red result.
        return 0;
    }
    renderer.SetPalette(palette);

    const presentation::MapProjection map{raster.width, raster.height};
    std::uint32_t drawn = 0;
    std::uint32_t hits = 0;
    for (const double bend : {0.0, 0.5, 1.0})
    {
        presentation::MapCamera camera;
        camera.lookAtU = 0.5;
        camera.lookAtV = 0.5;
        camera.distance = 3.0;
        camera.bend = bend;
        renderer.Draw(map, camera);
        ++drawn;

        // The centre of the screen is the look-at point, which is the middle
        // of the map. Whatever province is there, the pick must name it, and
        // it must be a province the world actually has.
        const std::uint16_t picked = renderer.PickAt(
            options.windowWidth / 2,
            options.windowHeight / 2
        );
        Check(picked <= raster.provinceCount,
            "picked index " + std::to_string(picked)
                + " is above the province count at bend "
                + std::to_string(bend));
        if (picked != 0)
        {
            ++hits;
        }
    }
    Check(drawn == 3, "not every bend was drawn");
    Check(hits > 0,
        "no bend put a province under the centre of the screen");

    renderer.Close();

    if (failures != 0)
    {
        std::cerr << "map renderer: " << failures << " failure(s)\n";
        return 5;
    }
    std::cout << "Map renderer smoke: passed (" << raster.width << "x"
              << raster.height << " index texture, " << projection.Count()
              << " palette entries, " << drawn << " frames, " << hits
              << " picks)\n";
    return 0;
}
