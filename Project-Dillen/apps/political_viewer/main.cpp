// apps/political_viewer/main.cpp
//
// The 1936 political world, on screen.
//
// It loads the committed map together with the country Packages the importer
// wrote, and colours every province by whoever owns it. Nothing here knows the
// word "HOI3": the ownership Relation, the country Entity type and the colours
// all come out of the country_palette Presentation asset, which is why the
// same viewer would show any political world a Package cared to declare.
//
// The map viewer next door shows the same geography coloured by corpus id and
// drives a mechanism. This one has no mechanism to drive -- a 1936 snapshot is
// state, not gameplay -- so it does not tick, and there is no panel.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "map_camera_controller.hpp"
#include "map_entity_index.hpp"
#include "map_index_raster.hpp"
#include "map_renderer.hpp"
#include "map_view.hpp"
#include "overlay.hpp"
#include "political_map_projection.hpp"
#include "client_state.hpp"
#include "presentation_view.hpp"
#include "province_centroids.hpp"
#include "standalone_session.hpp"
#include "text_atlas.hpp"

namespace fs = std::filesystem;

using namespace dillen;

namespace
{


constexpr std::int32_t kStatusHeight = 18;
constexpr std::int32_t kStatusWidth = 320;

// The viewer talking about the viewer. The Package owns the interface; where
// the cursor is and who owns it is this program's own readout, so it draws it
// rather than putting a debug line into somebody's content.
void AppendStatusLine(
    const presentation::TextAtlas& atlas,
    const std::string& text,
    std::int32_t y,
    std::vector<presentation::OverlayQuad>& quads
)
{
    if (!atlas.IsLoaded())
    {
        return;
    }
    const presentation::ControlRect box{8, y, kStatusWidth, kStatusHeight};
    for (const presentation::TextQuad& glyph : atlas.LayoutText(text, box))
    {
        presentation::OverlayQuad quad;
        quad.x = glyph.x;
        quad.y = glyph.y;
        quad.width = glyph.width;
        quad.height = glyph.height;
        quad.atlasX = glyph.atlasX;
        quad.atlasY = glyph.atlasY;
        quad.textured = true;
        quad.colour = 0xFF9CC8FFu;
        quads.push_back(quad);
    }
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

}

int main(int argc, char** argv)
{
    // The world root, because the political world lives wherever the importer
    // was told to write it. Dillen-Game's own map Package carries the demo
    // production Definition, so a Ruleset of countries alone cannot load that
    // tree -- point this at a root produced with --with-map.
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path("Dillen-Game");

    // Six layers, and their file names have to differ.
    //
    // A layer's virtual paths are relative to its own root, so a map holding
    // packages/contracts.dpackage and a country Package holding
    // packages/contracts.dpackage occupy THE SAME virtual path: the
    // higher-priority layer wins and the loader reports the other as having no
    // Package Manifest -- true, and silent about why. virtualPrefix looks like
    // the answer and is not: the authoring file templates are anchored at the
    // layer root (rulesets/**/*.druleset), so a prefix stops them matching at
    // all. The emitter names its four files distinctly instead.
    host::StandaloneSessionConfig config;
    const auto mount = [&config, &root](
        const char* name,
        const char* relative,
        int priority)
    {
        host::StandaloneSourceLayerConfig layer;
        layer.name = name;
        layer.root = root / relative;
        layer.priority = priority;
        config.sources.push_back(std::move(layer));
    };
    mount("world_map_contracts", "map/contracts", 0);
    mount("country_contracts", "country/contracts", 10);
    mount("world_map_content", "map/world", 100);
    mount("country_content", "country/hoi3_1936", 110);
    mount("world_map_presentation", "presentation/map_world", 200);
    mount("country_presentation", "presentation/hoi3_1936", 210);
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.hoi3.1936_world_root"),
        "dillen.hoi3.1936_world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    std::cout << "Loading the 1936 world...\n";
    if (!session.Start(config, report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "political viewer: " << root.string()
                  << " did not load. Produce it with\n"
                     "  dillen_hoi3_1936_import Dillen-Game <root> "
                     "--with-map\n";
        return 1;
    }

    const kernel::PresentationAsset* rasterAsset =
        FindAsset(session.PresentationAssets(), "map_index_raster");
    const kernel::PresentationAsset* idAsset =
        FindAsset(session.PresentationAssets(), "map_province_ids");
    const kernel::PresentationAsset* paletteAsset =
        FindAsset(session.PresentationAssets(), "country_palette");
    const kernel::PresentationAsset* fontAsset =
        FindAsset(session.PresentationAssets(), "font");
    if (rasterAsset == nullptr || idAsset == nullptr || paletteAsset == nullptr)
    {
        std::cerr << "political viewer: the Presentation Packages are "
                     "incomplete (raster, id table and country palette are "
                     "all required)\n";
        return 2;
    }

    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(*rasterAsset);
    if (!raster)
    {
        std::cerr << "political viewer: " << raster.message << '\n';
        return 3;
    }

    std::string message;

    presentation::PresentationView view;
    view.Advance(std::make_shared<const runtime::WorldQuerySnapshot>(
        session.Runtime().Query()
    ));

    presentation::MapEntityIndex entityIndex;
    if (entityIndex.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entityIndex.Resolve(view)
            != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "political viewer: the entity index failed: " << message
                  << '\n';
        return 4;
    }

    presentation::PoliticalMapProjection political;
    if (political.Bind(session.Catalog(), *paletteAsset, message)
        != presentation::PoliticalMapProjectionStatus::Ok)
    {
        std::cerr << "political viewer: the country palette failed to bind: "
                  << message << '\n';
        return 5;
    }
    if (political.Refresh(view, entityIndex)
        != presentation::PoliticalMapProjectionStatus::Ok)
    {
        std::cerr << "political viewer: the country palette failed to "
                     "resolve against the world\n";
        return 6;
    }

    presentation::TextAtlas atlas;
    if (fontAsset != nullptr)
    {
        atlas.Load(*fontAsset, message);
    }

    // Owner lookup, for the status line.
    //
    // The projection turns ownership into colours; naming the owner needs the
    // same walk one province at a time, so it is done on demand rather than
    // cached for 14187.
    const kernel::RelationTypeId ownership =
        kernel::StableRelationTypeId("dillen.country.owns_region");
    const kernel::ComponentTypeId identity =
        kernel::StableComponentTypeId("dillen.country.identity");
    const auto identitySlot =
        session.Catalog().ResolveComponentFieldSlot(identity, 1, "source_tag");

    const auto ownerOf = [&](std::uint32_t index) -> std::string
    {
        if (index == 0 || !identitySlot)
        {
            return "--";
        }
        const kernel::EntityId province = entityIndex.EntityFor(index);
        if (!province)
        {
            return "--";
        }
        const runtime::WorldQuerySnapshot& world = view.World();
        const std::vector<kernel::RelationId>& owners =
            world.Relations().Incoming(ownership, province);
        if (owners.empty())
        {
            return "unowned";
        }
        const world::RelationRecord* record =
            world.Relations().Find(owners.front());
        if (record == nullptr)
        {
            return "--";
        }
        const kernel::MechanismValue* tag = world.Components().FindField(
            record->source,
            identity,
            *identitySlot
        );
        if (tag == nullptr)
        {
            return "--";
        }
        const auto* text = std::get_if<std::string>(&tag->data);
        return text != nullptr ? *text : std::string("--");
    };

    presentation::gl::MapRendererOptions options;
    options.resizable = true;
    options.title = "Project Dillen -- the world in 1936";
    options.windowWidth = 1280;
    options.windowHeight = 720;
    options.visible = true;

    presentation::gl::MapRenderer renderer;
    if (renderer.Open(options, raster, message)
        != presentation::gl::MapRendererStatus::Ok)
    {
        std::cerr << "political viewer: could not open a window: " << message
                  << '\n';
        return 7;
    }
    if (atlas.IsLoaded())
    {
        renderer.SetFontAtlas(atlas.Bitmap(), atlas.Width(), atlas.Height());
    }
    renderer.SetEntityIndex(&entityIndex);

    // The projection's palette is indexed by dense raster index and is exactly
    // as long as the map has provinces; the renderer's texture is a square, so
    // the answer is copied into it rather than handed over directly.
    {
        std::vector<std::uint32_t> palette(renderer.PaletteSize(), 0u);
        const std::vector<std::uint32_t>& source = political.Palette();
        const std::size_t count = std::min(palette.size(), source.size());
        std::copy(source.begin(), source.begin() + count, palette.begin());
        renderer.SetPalette(palette);
    }

    presentation::ProvinceCentroids centroids;
    if (!centroids.Build(raster))
    {
        std::cerr << "political viewer: province centroids could not be built\n";
        return 8;
    }

    presentation::MapCameraController controller(
        presentation::MapProjection{raster.width, raster.height}
    );
    {
        presentation::MapCamera start;
        start.lookAtU = 0.5;
        start.lookAtV = 0.5;
        start.distance = controller.Limits().farDistance;
        start.bend = 1.0;
        controller.Reset(start);
    }

    presentation::ClientState client;
    client.camera = controller.Camera();
    client.viewportWidth = options.windowWidth;
    client.viewportHeight = options.windowHeight;

    const presentation::MapProjection map{raster.width, raster.height};

    std::cout
        << "\n  " << political.Palette().size() - 1 << " provinces: "
        << (political.Palette().size() - 1 - political.Unowned()
            - political.Sea())
        << " owned, " << political.Sea() << " sea, "
        << political.Unowned() << " unclaimed land.\n";
    if (political.AmbiguousOwners() != 0)
    {
        std::cout << "  " << political.AmbiguousOwners()
                  << " provinces have more than one owner.\n";
    }
    if (political.MissingColours() != 0)
    {
        std::cout << "  " << political.MissingColours()
                  << " owners have no colour in the palette.\n";
    }
    std::cout
        << "\n  middle-drag turns the globe, the wheel moves in and out.\n"
        << "  Near the equator the wheel unrolls it into a plane.\n"
        << "  hover a province to read its owner        esc  quit\n\n";

    bool running = true;
    while (running)
    {
        const presentation::gl::MapInput input = renderer.PollInput();
        running = !input.quit;

        if (input.bendPreset >= 0.0)
        {
            controller.SetBend(input.bendPreset);
        }
        if (input.bend != 0)
        {
            controller.NudgeBend(input.bend * 0.01);
        }
        if (input.dragX != 0 || input.dragY != 0)
        {
            controller.Drag(
                static_cast<double>(input.dragX),
                static_cast<double>(input.dragY)
            );
        }
        if (input.panX != 0 || input.panY != 0)
        {
            controller.Pan(input.panX * 0.004, input.panY * 0.004);
        }
        if (input.wheel != 0.0)
        {
            double anchorU = 0.0;
            double anchorV = 0.0;
            bool anchored = false;
            const std::uint16_t under = renderer.PickAt(
                static_cast<std::uint32_t>(std::max(input.mouseX, 0)),
                static_cast<std::uint32_t>(std::max(input.mouseY, 0))
            );
            if (under != 0)
            {
                anchored = centroids.Find(under, anchorU, anchorV);
            }
            controller.Zoom(input.wheel, anchored, anchorU, anchorV);
        }
        client.camera = controller.Camera();

        if (input.resized)
        {
            client.viewportWidth = input.viewportWidth;
            client.viewportHeight = input.viewportHeight;
            renderer.Resize(input.viewportWidth, input.viewportHeight);
        }

        // Asked for, not waited for: the answer arrives a frame later rather
        // than stalling the pipeline to move a highlight.
        renderer.RequestPick(
            static_cast<std::uint32_t>(std::max(input.mouseX, 0)),
            static_cast<std::uint32_t>(std::max(input.mouseY, 0))
        );
        const std::uint16_t hovered = renderer.LastPick();
        client.selected = entityIndex.EntityFor(hovered);

        renderer.Draw(map, client.camera);

        if (atlas.IsLoaded())
        {
            std::vector<presentation::OverlayQuad> quads;
            const std::int32_t top =
                static_cast<std::int32_t>(client.viewportHeight)
                    - kStatusHeight - 8;
            std::string line = "province ";
            line += hovered != 0 ? std::to_string(hovered) : std::string("--");
            line += "   owner ";
            line += ownerOf(hovered);
            AppendStatusLine(atlas, line, top, quads);
            renderer.DrawOverlay(quads);
        }

        renderer.Present();
    }

    renderer.Close();
    return 0;
}
