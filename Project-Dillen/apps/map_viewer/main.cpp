#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "client_state.hpp"
#include "control_tree.hpp"
#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "map_index_raster.hpp"
#include "map_renderer.hpp"
#include "map_view.hpp"
#include "mechanism_panel.hpp"
#include "overlay.hpp"
#include "presentation_compiler.hpp"
#include "presentation_schema.hpp"
#include "province_projection.hpp"
#include "standalone_session.hpp"
#include "text_atlas.hpp"

// Demo 0.8 -- the interactive map viewer.
//
// Every piece of this is already gated headless somewhere in the standard
// suite. What this adds is a person: a window that stays open, a mouse that
// selects, buttons that command, and a curvature slider that runs the map
// continuously between a plane and a globe.
//
// It is an APPLICATION, not a module. It lives outside src/ because
// architecture_guard_probe walks src/ and resolves every quoted include to an
// owning module -- and because an executable that wires four libraries
// together has no business being one of them. Nothing links it.
//
// It also does not include SDL. Input arrives through MapRenderer::PollInput,
// so the platform backend stays a single deletable directory.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

constexpr std::int32_t kPanelWidth = 260;
constexpr std::int32_t kStatusHeight = 22;

// The zoom range, measured from the surface along its normal.
//
// At the far end the eye sits one map radius clear of a globe and sees the
// whole of it; at the near end it is a fifth of a radius up and a province
// fills a good part of the window.
constexpr double kZoomNear = 0.05;
constexpr double kZoomFar = 2.7;

// Curvature as a function of zoom.
//
// One wheel, two things, because they are not really two things: a globe seen
// from far away and a plane seen from close up are the same surface at two
// ends of one parameter, and that is the claim this whole map geometry makes.
// Tying them together is what lets a person feel it rather than be told it.
//
// Logarithmic in the distance because the zoom itself is multiplicative -- a
// wheel notch scales rather than adds -- so a linear map would spend almost
// all of the curvature in the last few notches.
double CurvatureForZoom(double distance)
{
    const double span = std::log(kZoomFar / kZoomNear);
    const double at = std::log(distance / kZoomNear) / span;
    return at < 0.0 ? 0.0 : (at > 1.0 ? 1.0 : at);
}

// A line of text the application draws itself, under the panel the Package
// declares.
//
// The Package owns the interface; this is the viewer talking about the viewer
// -- which curvature it is at, where the camera is pointing. Putting it in the
// Package's control tree would make a debug readout part of the content, and
// leaving it out entirely is how a continuous control ends up looking broken
// because nothing on screen says it moved.
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
    const presentation::ControlRect box{
        8, y, kPanelWidth - 16, kStatusHeight
    };
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

std::string Fixed2(double value)
{
    const long scaled = static_cast<long>(value * 100.0 + (value < 0 ? -0.5 : 0.5));
    std::string text = scaled < 0 ? "-" : "";
    const long magnitude = scaled < 0 ? -scaled : scaled;
    text += std::to_string(magnitude / 100);
    text += '.';
    const long fraction = magnitude % 100;
    if (fraction < 10)
    {
        text += '0';
    }
    text += std::to_string(fraction);
    return text;
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

// A debug colouring, and labelled as one. There is no ownership or terrain in
// this world yet, so provinces are coloured by their corpus id, with the
// selected one left to the shader's highlight.
std::uint32_t ProvinceColour(std::int64_t sourceId, std::int64_t level)
{
    const auto id = static_cast<std::uint64_t>(sourceId);
    std::uint32_t red = static_cast<std::uint32_t>((id * 2654435761u) & 0xFFu);
    std::uint32_t green = static_cast<std::uint32_t>((id * 40503u) & 0xFFu);
    std::uint32_t blue = static_cast<std::uint32_t>((id * 2246822519u) & 0xFFu);
    // Developed provinces lighten. This is the one place the picture reacts to
    // the simulation rather than to the corpus, so it is worth being able to
    // see a command land from across the map.
    const std::int64_t lift = level > 24 ? 96 : level * 4;
    red = static_cast<std::uint32_t>(
        std::min<std::int64_t>(255, red + lift));
    green = static_cast<std::uint32_t>(
        std::min<std::int64_t>(255, green + lift));
    blue = static_cast<std::uint32_t>(
        std::min<std::int64_t>(255, blue + lift));
    return 0xFF000000u | (blue << 16) | (green << 8) | red;
}

}

int main()
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
    config.sources.push_back({
        "world_map_presentation", kMapPresentationRoot, 200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    std::cout << "Loading the world...\n";
    if (!session.Start(config, report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "map viewer: the world did not load. Run from the "
                     "repository root.\n";
        return 1;
    }

    const kernel::PresentationAsset* rasterAsset =
        FindAsset(session.PresentationAssets(), "map_index_raster");
    const kernel::PresentationAsset* fontAsset =
        FindAsset(session.PresentationAssets(), "font");
    const kernel::PresentationAsset* bindingAsset =
        FindAsset(session.PresentationAssets(), "ui_binding");
    if (rasterAsset == nullptr || bindingAsset == nullptr)
    {
        std::cerr << "map viewer: the Presentation Package is incomplete\n";
        return 2;
    }

    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(*rasterAsset);
    if (!raster)
    {
        std::cerr << "map viewer: " << raster.message << '\n';
        return 3;
    }

    std::string message;
    presentation::ProvinceProjection projection;
    presentation::MapCommandTranslator translator;
    presentation::ControlTree tree;
    presentation::MechanismPanel panel;
    presentation::TextAtlas atlas;

    // Presentation Source -> Schema -> Compiler -> Frozen Catalog, once, here.
    // After this the viewer holds no presentation strings at all: the layout
    // is compiled controls at known slots and the panel binds to the
    // Definition and the field slots the VIEW resolved -- so the host no
    // longer has to know what the mechanism or its fields are called.
    presentation::PresentationSchemaRegistry schema;
    if (!presentation::RegisterBuiltinControls(schema))
    {
        std::cerr << "map viewer: the control vocabulary is inconsistent\n";
        return 4;
    }
    presentation::PresentationCompiler compiler;
    presentation::FrozenPresentationCatalog presentationCatalog;
    const presentation::PresentationCompileStatus compiled = compiler.Compile(
        schema,
        session.PresentationAssets(),
        session.Catalog(),
        presentationCatalog,
        message
    );
    if (compiled != presentation::PresentationCompileStatus::Ok)
    {
        std::cerr << "map viewer: the Presentation Package did not compile: "
                  << message << '\n';
        return 4;
    }
    if (tree.Bind(
               presentationCatalog,
               presentation::StablePresentationViewId(
                   bindingAsset->canonicalName),
               session.Catalog(),
               message)
            != presentation::ControlTreeStatus::Ok
        || panel.Bind(
               session.Catalog(),
               tree.Definition(),
               tree.BoundFields(),
               tree.View().boundFieldSlots,
               message)
            != presentation::MechanismPanelStatus::Ok)
    {
        std::cerr << "map viewer: bind failed: " << message << '\n';
        return 4;
    }

    // The Definition and the role are all the command path needs, and the
    // Definition comes from the compiled view. The mechanism name, the field
    // names, the entity type, the province count and the naming convention
    // that used to be here were every one of them this host asserting
    // something about a Package it should have been reading.
    // Both from the compiled view. The role used to be the string literal
    // "province" here, which meant a Package could only be swapped for one
    // that spelled the same idea the same way.
    presentation::MapCommandSpec commandSpec;
    commandSpec.definition = tree.Definition();
    commandSpec.roleName = tree.SubjectRole();
    if (translator.Bind(session.Catalog(), commandSpec, message)
        != presentation::MapCommandStatus::Ok)
    {
        std::cerr << "map viewer: the command path did not bind: " << message
                  << '\n';
        return 4;
    }
    if (fontAsset != nullptr
        && atlas.Load(*fontAsset, message) != presentation::TextAtlasStatus::Ok)
    {
        // Not fatal. A Package with no usable font still has an interface; it
        // just has no captions, and saying so beats refusing to start.
        std::cerr << "map viewer: no text (" << message << ")\n";
    }

    presentation::PresentationView view;
    view.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );
    // Raster index -> Entity, from the id table the Package ships and the
    // source_id the world carries. A picked pixel names an Entity from here
    // on; nothing reconstructs one from a name.
    presentation::MapEntityIndex entityIndex;
    const kernel::PresentationAsset* idAsset =
        FindAsset(session.PresentationAssets(), "map_province_ids");
    if (idAsset == nullptr
        || entityIndex.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entityIndex.Resolve(view)
            != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "map viewer: the entity index failed: " << message
                  << '\n';
        return 4;
    }
    // The entity type and the component field are the id table's own
    // declaration, read back rather than repeated here. This host names no
    // entity type, component or field of the Package any more.
    presentation::ProvinceProjectionSpec spec;
    spec.entityTypeName = entityIndex.Spec().entityTypeName;
    spec.count = raster.provinceCount;
    spec.columns.push_back({
        entityIndex.Spec().componentName,
        entityIndex.Spec().componentVersion,
        entityIndex.Spec().sourceIdFieldName
    });
    if (projection.Bind(session.Catalog(), spec, entityIndex, message)
        != presentation::ProvinceProjectionStatus::Ok)
    {
        std::cerr << "map viewer: projection bind failed: " << message
                  << '\n';
        return 4;
    }
    projection.Refresh(view);
    translator.Resolve(view);

    presentation::gl::MapRendererOptions options;
    options.resizable = true;
    options.title = "Project Dillen -- world map";
    options.windowWidth = 1280;
    options.windowHeight = 720;
    options.visible = true;

    presentation::gl::MapRenderer renderer;
    if (renderer.Open(options, raster, message)
        != presentation::gl::MapRendererStatus::Ok)
    {
        std::cerr << "map viewer: could not open a window: " << message
                  << '\n';
        return 5;
    }
    if (atlas.IsLoaded())
    {
        renderer.SetFontAtlas(atlas.Bitmap(), atlas.Width(), atlas.Height());
    }
    // The widget resolves picks to Entities itself from here on.
    renderer.SetEntityIndex(&entityIndex);

    // Sized by the renderer from the map's province count. A fixed 128x128
    // was a rendering ceiling of 16383 regions that no content could see.
    std::vector<std::uint32_t> palette(renderer.PaletteSize(), 0u);
    const auto refreshPalette = [&]()
    {
        for (std::uint32_t index = 1;
            index <= projection.Count() && index < palette.size();
            ++index)
        {
            const presentation::MechanismPanelReadout readout =
                panel.Read(translator, view, entityIndex.EntityFor(index));
            const std::int64_t level = readout.valid && !readout.fields.empty()
                ? readout.fields[0].value
                : 0;
            palette[index] = ProvinceColour(projection.Value(index, 0), level);
        }
        renderer.SetPalette(palette);
    };
    refreshPalette();

    presentation::ClientState client;
    client.camera.lookAtU = 0.5;
    client.camera.lookAtV = 0.5;
    client.camera.distance = kZoomFar;
    client.camera.bend = CurvatureForZoom(client.camera.distance);
    client.viewportWidth = options.windowWidth;
    client.viewportHeight = options.windowHeight;

    const presentation::MapProjection map{raster.width, raster.height};
    tree.Layout(kPanelWidth, static_cast<std::int32_t>(options.windowHeight));

    std::cout
        << "\n  wheel         zoom, and the curvature with it:\n"
        << "                zoomed out is a globe, zoomed in is a plane\n"
        << "  middle-drag   turn the map\n"
        << "  1 .. 9, 0     pin the curvature by hand (1 flat, 0 globe);\n"
        << "                [ and ] nudge it; the wheel takes it back\n"
        << "  click a province to select it; +1 / -1 in the panel command it\n"
        << "  arrows/WASD   pan       space  run one tick       esc  quit\n\n";

    std::uint64_t tick = 0;
    // Curvature follows the zoom until someone says otherwise, and a wheel
    // notch takes it back. Keeping the manual override is not indecision: the
    // two limits of this geometry are worth being able to sit at and look at,
    // and the coupling never quite reaches either.
    bool curvatureFollowsZoom = true;
    bool running = true;
    while (running)
    {
        const presentation::gl::MapInput input = renderer.PollInput();
        running = !input.quit;

        // --- camera ---
        if (input.bendPreset >= 0.0)
        {
            client.camera.bend = input.bendPreset;
            curvatureFollowsZoom = false;
        }
        if (input.bend != 0)
        {
            client.camera.bend += input.bend * 0.01;
            curvatureFollowsZoom = false;
        }
        if (client.camera.bend < 0.0)
        {
            client.camera.bend = 0.0;
        }
        if (client.camera.bend > 1.0)
        {
            client.camera.bend = 1.0;
        }
        // A drag turns the map under the cursor. The step shrinks with the
        // camera's distance so the ground keeps up with the pointer at every
        // zoom instead of tearing away when close in.
        const double dragScale = 0.0009 * client.camera.distance;
        client.camera.lookAtU +=
            input.panX * 0.004 - input.dragX * dragScale;
        client.camera.lookAtV +=
            input.panY * 0.004 - input.dragY * dragScale;
        client.camera.lookAtU -= std::floor(client.camera.lookAtU);
        if (client.camera.lookAtV < 0.0)
        {
            client.camera.lookAtV = 0.0;
        }
        if (client.camera.lookAtV > 1.0)
        {
            client.camera.lookAtV = 1.0;
        }
        if (input.wheel != 0.0)
        {
            client.camera.distance *= std::pow(0.9, input.wheel);
            if (client.camera.distance < kZoomNear)
            {
                client.camera.distance = kZoomNear;
            }
            if (client.camera.distance > kZoomFar)
            {
                client.camera.distance = kZoomFar;
            }
            curvatureFollowsZoom = true;
        }
        if (curvatureFollowsZoom)
        {
            client.camera.bend = CurvatureForZoom(client.camera.distance);
        }

        // --- hover and click ---
        //
        // The panel owns its strip; everything else is the map. Two hit tests
        // and no z-order, because the two never overlap.
        // The window may have changed size; the panel is laid out against it.
        if (input.resized)
        {
            client.viewportWidth = input.viewportWidth;
            client.viewportHeight = input.viewportHeight;
            tree.Layout(
                kPanelWidth,
                static_cast<std::int32_t>(client.viewportHeight)
            );
        }

        const bool overPanel = input.mouseX < kPanelWidth;
        const std::uint32_t hovered = overPanel
            ? tree.HitTest(input.mouseX, input.mouseY)
            : UINT32_MAX;
        if (!overPanel)
        {
            // Asked for, not waited for.
            //
            // This used to be a synchronous PickAt every frame: the CPU asked
            // the GPU for a pixel of a frame it had not finished and blocked
            // until it had, which is a full pipeline stall per frame to move
            // a highlight. The request is issued into a pixel pack buffer and
            // the answer collected a frame or two later, which for a cursor
            // is invisible.
            renderer.RequestPick(
                static_cast<std::uint32_t>(std::max(input.mouseX, 0)),
                static_cast<std::uint32_t>(std::max(input.mouseY, 0))
            );
            client.hovered = renderer.LastPickedEntity();
        }

        bool advanced = false;
        if (input.pressed)
        {
            if (overPanel && hovered != UINT32_MAX)
            {
                presentation::MapIntent intent;
                if (tree.IntentFor(hovered, client.selected, intent))
                {
                    // An intent, then a Command, then a Tick. The button knows
                    // what the player asked for and nothing else; the
                    // translator is the only thing that knows how to say it to
                    // the world.
                    kernel::WorldTransaction transaction;
                    if (translator.Translate(intent, transaction)
                        == presentation::MapCommandStatus::Ok)
                    {
                        session.Runtime().Enqueue(
                            std::move(transaction),
                            tick + 1,
                            0
                        );
                        advanced = true;
                    }
                }
            }
            else if (!overPanel)
            {
                // A click is exact. The hover answer may be a frame or two
                // old, which does not matter for a highlight and would matter
                // very much for deciding which province a player just chose.
                client.selected = renderer.PickEntityAt(
                    static_cast<std::uint32_t>(std::max(input.mouseX, 0)),
                    static_cast<std::uint32_t>(std::max(input.mouseY, 0))
                );
                renderer.SetSelection(static_cast<std::uint16_t>(
                    entityIndex.IndexFor(client.selected)
                ));
            }
        }
        if (input.step)
        {
            advanced = true;
        }

        if (advanced)
        {
            ++tick;
            session.Runtime().RunTick(tick);
            view.Advance(
                std::make_shared<const runtime::WorldQuerySnapshot>(
                    session.Runtime().Query()
                )
            );
            projection.Refresh(view);
            refreshPalette();
        }

        const presentation::MechanismPanelReadout readout =
            panel.Read(translator, view, client.selected);

        std::vector<presentation::OverlayQuad> quads =
            presentation::BuildPanelOverlay(tree, atlas, readout, hovered);
        const std::int32_t statusTop =
            static_cast<std::int32_t>(options.windowHeight)
                - 3 * kStatusHeight - 8;
        AppendStatusLine(
            atlas,
            "curvature " + Fixed2(client.camera.bend)
                + (client.camera.bend <= 0.0
                    ? " plane"
                    : (client.camera.bend >= 1.0 ? " globe" : ""))
                + (curvatureFollowsZoom ? "" : " *"),
            statusTop,
            quads
        );
        AppendStatusLine(
            atlas,
            "zoom " + Fixed2(client.camera.distance)
                + "   tick " + std::to_string(tick),
            statusTop + kStatusHeight,
            quads
        );
        AppendStatusLine(
            atlas,
            client.selected
                ? "province "
                    + std::to_string(entityIndex.IndexFor(client.selected))
                : "click the map to select",
            statusTop + 2 * kStatusHeight,
            quads
        );

        renderer.Draw(map, client.camera);
        renderer.DrawOverlay(quads);
        renderer.Present();
    }

    renderer.Close();
    std::cout << "Ran " << tick << " tick(s).\n";
    return 0;
}
