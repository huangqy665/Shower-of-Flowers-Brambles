// apps/map_mode_debug/main.cpp
//
// A window for one question: are the globe's polar caps drawn in the sea
// colour now, and is that colour coming from content rather than the constant
// baked into the renderer?
//
// It is the political viewer with three things bolted on:
//
//   * the camera starts aimed at the north cap, so there is nothing to hunt
//     for;
//   * SPACE cycles renderer.SetPolarFill() through four states -- the colour
//     content declares, the old pale ice-white the renderer ships with, and
//     two loud sentinels -- and prints each to the terminal with the current
//     PolarPad(), so "the cap is not drawn at all" is distinguishable from
//     "the cap is the wrong colour";
//   * hovering prints the palette word for the province under the cursor in
//     the current mode, so a sea province's colour can be read off and
//     compared to the polar colour by eye and by number.
//
// Not registered with CTest: it needs a window. map_mode_probe covers the
// same numbers headless.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "map_camera_controller.hpp"
#include "map_entity_index.hpp"
#include "map_index_raster.hpp"
#include "map_mode.hpp"
#include "map_renderer.hpp"
#include "map_view.hpp"
#include "overlay.hpp"
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
constexpr std::int32_t kStatusWidth = 640;

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

// The four things SPACE cycles renderer.SetPolarFill() through.
struct PolarState
{
    const char* name;
    float r;
    float g;
    float b;
};

}

int main(int argc, char** argv)
{
    const fs::path root =
        argc > 1 ? fs::path(argv[1]) : fs::path("Dillen-Game-1936");

    host::StandaloneSessionConfig config;
    const auto mount = [&config, &root](
        const char* name, const char* relative, int priority)
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
        return 1;
    }

    const kernel::PresentationAsset* rasterAsset =
        FindAsset(session.PresentationAssets(), "map_index_raster");
    const kernel::PresentationAsset* idAsset =
        FindAsset(session.PresentationAssets(), "map_province_ids");
    const kernel::PresentationAsset* modeAsset =
        FindAsset(session.PresentationAssets(), "map_mode_set");
    const kernel::PresentationAsset* fontAsset =
        FindAsset(session.PresentationAssets(), "font");
    if (rasterAsset == nullptr || idAsset == nullptr || modeAsset == nullptr)
    {
        std::cerr << "map mode debug: presentation packages incomplete\n";
        return 2;
    }

    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(*rasterAsset);
    if (!raster)
    {
        std::cerr << "map mode debug: " << raster.message << '\n';
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
        std::cerr << "map mode debug: entity index failed: " << message << '\n';
        return 4;
    }

    presentation::MapModeSet modes;
    if (modes.Bind(session.Catalog(), *modeAsset, message)
        != presentation::MapModeStatus::Ok)
    {
        std::cerr << "map mode debug: modes failed to bind: " << message
                  << '\n';
        return 5;
    }
    std::size_t mode = 0;
    if (modes.Refresh(view, entityIndex, mode)
        != presentation::MapModeStatus::Ok)
    {
        std::cerr << "map mode debug: mode '" << modes.Mode(mode).id
                  << "' failed to resolve\n";
        return 6;
    }

    // ---- the numbers, before any window opens --------------------------
    std::printf("\n--- polar fill ---\n");
    if (modes.HasPolarColour())
    {
        const std::uint32_t c = modes.PolarColour();
        std::printf(
            "  content polar_colour = %u (0x%06X) -> rgb(%u, %u, %u)\n",
            c, c, (c >> 16) & 0xFFu, (c >> 8) & 0xFFu, c & 0xFFu);
    }
    else
    {
        std::printf("  content declares no polar_colour\n");
    }
    std::printf("  renderer default = rgb(219, 229, 240) (0.86 0.90 0.94)\n");
    for (std::size_t at = 0; at < modes.Count(); ++at)
    {
        const presentation::CompiledMapMode& m = modes.Mode(at);
        std::printf("  mode [%zu] %-14s absent = 0x%08X\n", at, m.id.c_str(),
                    m.absent);
        for (const presentation::MapModeLookupEntry& e : m.lookup)
        {
            std::printf("        lookup %lld -> 0x%08X\n",
                        static_cast<long long>(e.value), e.colour);
        }
    }

    // State 0 is what the viewer actually does: content, or -- if content is
    // silent -- the renderer's own default, left untouched.
    std::array<PolarState, 4> states{{
        {"content", 0.0f, 0.0f, 0.0f},
        {"old ice-white", 0.86f, 0.90f, 0.94f},
        {"MAGENTA sentinel", 1.0f, 0.0f, 1.0f},
        {"GREEN sentinel", 0.0f, 1.0f, 0.0f},
    }};
    bool haveContentColour = modes.HasPolarColour();
    if (haveContentColour)
    {
        const std::uint32_t c = modes.PolarColour();
        states[0].r = static_cast<float>((c >> 16) & 0xFFu) / 255.0f;
        states[0].g = static_cast<float>((c >> 8) & 0xFFu) / 255.0f;
        states[0].b = static_cast<float>(c & 0xFFu) / 255.0f;
    }
    else
    {
        states[0].name = "renderer default (content silent)";
        states[0].r = 0.86f;
        states[0].g = 0.90f;
        states[0].b = 0.94f;
    }

    presentation::TextAtlas atlas;
    if (fontAsset != nullptr)
    {
        atlas.Load(*fontAsset, message);
    }

    presentation::gl::MapRendererOptions options;
    options.resizable = true;
    options.title = "map mode debug -- SPACE cycles the polar fill";
    options.windowWidth = 1280;
    options.windowHeight = 720;
    options.visible = true;

    presentation::gl::MapRenderer renderer;
    if (renderer.Open(options, raster, message)
        != presentation::gl::MapRendererStatus::Ok)
    {
        std::cerr << "map mode debug: no window: " << message << '\n';
        return 7;
    }
    if (atlas.IsLoaded())
    {
        renderer.SetFontAtlas(atlas.Bitmap(), atlas.Width(), atlas.Height());
    }
    renderer.SetEntityIndex(&entityIndex);

    std::size_t polar = 0;
    const auto applyPolar = [&]()
    {
        const PolarState& s = states[polar];
        renderer.SetPolarFill(s.r, s.g, s.b);
        std::printf(
            "[polar] state %zu/%zu  %-32s  SetPolarFill(%.3ff, %.3ff, %.3ff)"
            "   PolarPad=%.5f\n",
            polar, states.size(), s.name, s.r, s.g, s.b, renderer.PolarPad());
        std::fflush(stdout);
    };
    applyPolar();

    const auto uploadPalette = [&]()
    {
        std::vector<std::uint32_t> palette(renderer.PaletteSize(), 0u);
        const std::vector<std::uint32_t>& source = modes.Palette();
        const std::size_t count = std::min(palette.size(), source.size());
        std::copy(source.begin(), source.begin() + count, palette.begin());
        renderer.SetPalette(palette);
    };
    uploadPalette();

    presentation::ProvinceCentroids centroids;
    if (!centroids.Build(raster))
    {
        std::cerr << "map mode debug: centroids failed\n";
        return 8;
    }

    presentation::MapCameraController controller(
        presentation::MapProjection{raster.width, raster.height}
    );
    {
        // Aimed at the north cap: bend 1 so a pole exists at all, look-at
        // high on the map, close enough that the cap fills a good part of the
        // screen.
        presentation::MapCamera start;
        start.lookAtU = 0.5;
        start.lookAtV = 0.04;
        start.distance = 0.7;
        start.bend = 1.0;
        controller.Reset(start);
    }

    presentation::ClientState client;
    client.camera = controller.Camera();
    client.viewportWidth = options.windowWidth;
    client.viewportHeight = options.windowHeight;

    const presentation::MapProjection map{raster.width, raster.height};

    std::cout
        << "\n  SPACE      cycle the polar fill (watch the caps, read the "
           "terminal)\n"
        << "  F1 .. F" << modes.Count()
        << "    switch map mode\n"
        << "  middle-drag turns the globe, wheel moves in and out\n"
        << "  hover a province to read its palette word   esc  quit\n\n";

    bool running = true;
    bool spaceLatch = false;
    while (running)
    {
        const presentation::gl::MapInput input = renderer.PollInput();
        running = !input.quit;

        if (input.step && !spaceLatch)
        {
            polar = (polar + 1) % states.size();
            applyPolar();
        }
        spaceLatch = input.step;

        if (input.modeSelect >= 0
            && static_cast<std::size_t>(input.modeSelect) < modes.Count()
            && static_cast<std::size_t>(input.modeSelect) != mode)
        {
            mode = static_cast<std::size_t>(input.modeSelect);
            if (modes.Refresh(view, entityIndex, mode)
                == presentation::MapModeStatus::Ok)
            {
                uploadPalette();
                std::printf("[mode]  %zu  %s   absent=0x%08X\n", mode,
                            modes.Mode(mode).label.c_str(),
                            modes.Mode(mode).absent);
                std::fflush(stdout);
            }
        }

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
            char word[16] = "--";
            if (hovered != 0 && hovered < modes.Palette().size())
            {
                std::snprintf(word, sizeof(word), "0x%08X",
                              modes.Palette()[hovered]);
            }
            std::string line = modes.Mode(mode).label;
            line += "  |  polar: ";
            line += states[polar].name;
            line += "  |  prov ";
            line += hovered != 0 ? std::to_string(hovered) : std::string("--");
            line += "  col ";
            line += word;
            AppendStatusLine(atlas, line, top, quads);
            renderer.DrawOverlay(quads);
        }

        renderer.Present();
    }

    renderer.Close();
    return 0;
}
