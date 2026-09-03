#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "client_state.hpp"
#include "fixed_point.hpp"
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
#include "runtime_persistence.hpp"
#include "text_atlas.hpp"
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

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

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
    if (!session.Start(config, report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "map renderer: the world did not load\n";
        return 1;
    }
    // The Presentation Package owns more than one asset now -- the raster and
    // the UI binding -- so it is found by kind, not by position.
    const kernel::PresentationAsset* rasterAsset = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "map_index_raster")
        {
            rasterAsset = &asset;
            break;
        }
    }
    if (rasterAsset == nullptr)
    {
        std::cerr << "map renderer: no map_index_raster asset\n";
        return 2;
    }

    const presentation::MapIndexRaster raster =
        presentation::LoadMapIndexRaster(*rasterAsset);
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
    spec.count = raster.provinceCount;
    spec.columns.push_back({"dillen.map.geography", 1, "source_id"});

    presentation::ProvinceProjection projection;
    std::string message;
    presentation::PresentationView view;
    view.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );

    // Raster index -> Entity, from the id table the Package ships and the
    // source_id the world carries.
    presentation::MapEntityIndex entityIndex;
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
        || entityIndex.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entityIndex.Resolve(view)
            != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "map renderer: the entity index failed: " << message
                  << '\n';
        return 4;
    }
    if (projection.Bind(session.Catalog(), spec, entityIndex, message)
        != presentation::ProvinceProjectionStatus::Ok)
    {
        std::cerr << "map renderer: projection bind failed: " << message
                  << '\n';
        return 4;
    }
    projection.Refresh(view);

    presentation::gl::MapRendererOptions options;
    options.visible = false;
    options.windowWidth = 640;
    options.windowHeight = 360;
    // The (u,v) attachment is off in the product path; this probe is the one
    // thing that asks for it, to check that the wrap puts the camera's
    // longitude under the middle of the screen.
    options.mapPointReadback = true;

    presentation::gl::MapRenderer renderer;
    const presentation::gl::MapRendererStatus opened =
        renderer.Open(options, raster, message);
    if (opened != presentation::gl::MapRendererStatus::Ok)
    {
        std::cerr << "map renderer: could not open: " << message << '\n';
        // Not a failure of the engine, and not a pass either. 77 is the code
        // CTest is told to read as "skipped": a machine with no GPU or no
        // display reports that it did not run this, which is the only honest
        // answer. Returning 0 here -- which this did -- made a machine that
        // could not run the test indistinguishable from one where it worked.
        return 77;
    }
    // Filled after Open, because the renderer decides the size from the
    // map's province count. It used to be a fixed 128x128 here and in the
    // shader -- a rendering ceiling of 16383 regions that no content could
    // see and nothing reported.
    std::vector<std::uint32_t> palette;
    const auto fillPalette = [&]()
    {
    palette.assign(renderer.PaletteSize(), 0u);
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
    };

    // --- the palette is sized by the map, not by the renderer ---------
    Check(renderer.PaletteSide() >= 128,
        "the palette is smaller than the province count needs");
    Check(static_cast<std::uint64_t>(renderer.PaletteSide())
                * renderer.PaletteSide()
            > raster.provinceCount,
        "the palette has fewer texels than the map has provinces: side "
            + std::to_string(renderer.PaletteSide()) + " for "
            + std::to_string(raster.provinceCount) + " provinces");
    fillPalette();
    renderer.SetPalette(palette);
    renderer.SetEntityIndex(&entityIndex);

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
        renderer.Present();
        ++drawn;

        // The centre of the screen is the look-at point, which is the middle
        // of the map. Whatever province is there, the pick must name it, and
        // it must be a province the world actually has.
        const std::uint16_t atCentre = renderer.PickAt(
            options.windowWidth / 2,
            options.windowHeight / 2
        );
        Check(atCentre <= raster.provinceCount,
            "atCentre index " + std::to_string(atCentre)
                + " is above the province count at bend "
                + std::to_string(bend));
        if (atCentre != 0)
        {
            ++hits;
        }
    }
    Check(drawn == 3, "not every bend was drawn");
    Check(hits > 0,
        "no bend put a province under the centre of the screen");

    // --- the closed loop -----------------------------------------------
    //
    // Pick a province off the screen, read its panel, command it, tick, and
    // read it again. This is the whole of Demo 0.8's interactive claim in one
    // sequence, and the only part of it that needs a window is the pick.
    //
    // Everything it relies on is already gated headless: the panel and the
    // translator agree about which instance a province owns
    // (map_command_probe), client state cannot reach the world
    // (client_state_probe), and the binding that names these fields is
    // refused at load if the Ruleset stops providing them
    // (presentation_binding_probe). What is added here is that a pick really
    // does name the province under the cursor.
    // The Presentation Package is compiled once, here, and the panel binds to
    // what the compiled view resolved -- no mechanism, Definition or field
    // name appears in this file any more.
    presentation::PresentationSchemaRegistry schema;
    presentation::RegisterBuiltinControls(schema);
    presentation::PresentationCompiler compiler;
    presentation::FrozenPresentationCatalog presentationCatalog;
    presentation::ControlTree tree;
    const kernel::PresentationAsset* bindingAsset = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "ui_binding")
        {
            bindingAsset = &asset;
            break;
        }
    }

    presentation::MapCommandTranslator translator;
    presentation::MechanismPanel panel;
    if (bindingAsset == nullptr
        || compiler.Compile(
               schema,
               session.PresentationAssets(),
               session.Catalog(),
               presentationCatalog,
               message)
            != presentation::PresentationCompileStatus::Ok
        || tree.Bind(
               presentationCatalog,
               presentation::StablePresentationViewId(
                   bindingAsset->canonicalName),
               session.Catalog(),
               message)
            != presentation::ControlTreeStatus::Ok
        || translator.Bind(
               session.Catalog(),
               presentation::MapCommandSpec{tree.Definition(), "province"},
               message)
            != presentation::MapCommandStatus::Ok
        || panel.Bind(
               session.Catalog(),
               tree.Definition(),
               tree.BoundFields(),
               tree.View().boundFieldSlots,
               message)
            != presentation::MechanismPanelStatus::Ok)
    {
        std::cerr << "map renderer: interactive bind failed: " << message
                  << '\n';
        renderer.Close();
        return 6;
    }
    translator.Resolve(view);

    // The camera is aimed until something is actually under the middle of the
    // screen. A pick of 0 is the ocean, which is a legitimate thing to click
    // and a useless thing to command.
    presentation::ClientState client;
    client.camera.distance = 3.0;
    client.camera.bend = 1.0;
    std::uint16_t picked = 0;
    for (const double u : {0.5, 0.52, 0.48, 0.55})
    {
        client.camera.lookAtU = u;
        client.camera.lookAtV = 0.45;
        renderer.Draw(map, client.camera);
        renderer.Present();
        picked = renderer.PickAt(
            options.windowWidth / 2,
            options.windowHeight / 2
        );
        if (picked != 0)
        {
            break;
        }
    }
    Check(picked != 0, "no aim put a province under the cursor");

    if (picked != 0)
    {
        client.selected = entityIndex.EntityFor(picked);
        Check(static_cast<bool>(client.selected),
            "the picked province resolved to no Entity");
        const presentation::MechanismPanelReadout before =
            panel.Read(translator, view, client.selected);
        Check(before.valid, "the picked province has no panel");

        // The intent comes from the Package's own button, so it carries the
        // Capability Contract the Package declared rather than a verb this
        // file made up.
        kernel::WorldTransaction transaction;
        presentation::MapIntent intent;
        const std::uint32_t raiseButton = tree.Find("raise");
        Check(raiseButton != UINT32_MAX, "the layout has no raise button");
        Check(raiseButton != UINT32_MAX
                && tree.IntentFor(raiseButton, client.selected, intent),
            "the raise button produced no intent");
        Check(translator.Translate(intent, transaction)
                == presentation::MapCommandStatus::Ok,
            "the picked province produced no command");
        session.Runtime().Enqueue(std::move(transaction), 1, 0);
        Check(static_cast<bool>(session.Runtime().RunTick(1)),
            "the tick after the command failed");

        view.Advance(
            std::make_shared<const runtime::WorldQuerySnapshot>(
                session.Runtime().Query()
            )
        );
        const presentation::MechanismPanelReadout after =
            panel.Read(translator, view, client.selected);
        Check(after.valid, "the panel went blank after a tick");
        if (before.valid && after.valid
            && before.fields.size() == 2 && after.fields.size() == 2)
        {
            Check(after.fields[0].value == before.fields[0].value + 1,
                "the command did not move the level the panel shows");
            // output = source_id * level, recomputed by the algorithm on the
            // tick that followed. A panel that only echoed the command would
            // have the level right and this wrong. The source_id comes from
            // the read model rather than the panel, so this also says the
            // projection row and the mechanism instance the translator found
            // belong to the same province.
            const std::int64_t sourceId = projection.Value(picked, 0);
            Check(after.fields[1].isDecimal
                    && after.fields[1].value
                        == after.fields[0].value * sourceId
                            * kernel::kDecimalInternalScale,
                "output is not source_id x level for the picked province");
            std::cout << "  province " << picked
                      << ": level " << before.fields[0].value << " -> "
                      << after.fields[0].value << ", output "
                      << before.fields[1].value << " -> "
                      << after.fields[1].value
                      << " (fixed-point internal scale)" << '\n';
        }

        // Recolour from the new state and draw again: the picture follows the
        // world without anything being told to invalidate it.
        projection.Refresh(view);
        fillPalette();
        renderer.SetPalette(palette);
        renderer.Draw(map, client.camera);
        ++drawn;

        // --- P5c: the selection highlight ------------------------------
        //
        // The tint is decided by the same fragment shader invocation that
        // writes the id attachment, so it cannot name a different province
        // than a pick does. What is checked here is that it happens at all,
        // and that it is confined to the selected province: a highlight that
        // repainted the whole map would satisfy any assertion about the
        // selected pixel alone.
        const std::uint32_t plainSelected = renderer.ColourAt(
            options.windowWidth / 2,
            options.windowHeight / 2
        );
        std::uint32_t otherX = 0;
        std::uint32_t otherY = 0;
        std::uint32_t plainOther = 0;
        for (std::uint32_t step = 1; step < 40 && plainOther == 0; ++step)
        {
            const std::uint32_t x = options.windowWidth / 2 + step * 4;
            const std::uint32_t y = options.windowHeight / 2;
            if (x < options.windowWidth
                && renderer.PickAt(x, y) != 0
                && renderer.PickAt(x, y) != picked)
            {
                otherX = x;
                otherY = y;
                plainOther = renderer.ColourAt(x, y);
            }
        }

        renderer.SetSelection(static_cast<std::uint16_t>(picked));
        renderer.Draw(map, client.camera);
        ++drawn;
        Check(renderer.ColourAt(
                  options.windowWidth / 2,
                  options.windowHeight / 2) != plainSelected,
            "selecting a province did not change its colour");
        if (plainOther != 0)
        {
            Check(renderer.ColourAt(otherX, otherY) == plainOther,
                "the highlight bled onto a province that is not selected");
        }
        Check(renderer.PickAt(
                  options.windowWidth / 2,
                  options.windowHeight / 2) == picked,
            "the highlight changed which province a pick returns");

        // --- P5c: the panel, drawn --------------------------------------
        //
        // Every decision here was made on the CPU and gated in
        // text_atlas_probe: which quads, where, and what they say. The window
        // is asked one question -- did the overlay land on the pixels the
        // layout named.
        presentation::TextAtlas atlas;
        const kernel::PresentationAsset* fontAsset =
            presentationCatalog.FindAsset("font");
        Check(fontAsset != nullptr, "the Package is missing the font");
        if (fontAsset != nullptr
            && atlas.Load(*fontAsset, message)
                == presentation::TextAtlasStatus::Ok)
        {
            renderer.SetFontAtlas(
                atlas.Bitmap(),
                atlas.Width(),
                atlas.Height()
            );
            // The panel occupies a strip down the left of the window.
            tree.Layout(220, static_cast<std::int32_t>(options.windowHeight));
            const std::vector<presentation::OverlayQuad> quads =
                presentation::BuildPanelOverlay(
                    tree, atlas, after, UINT32_MAX);
            Check(!quads.empty(), "the panel produced nothing to draw");

            std::vector<std::uint32_t> beforeOverlay;
            beforeOverlay.reserve(quads.size());
            for (const presentation::OverlayQuad& quad : quads)
            {
                beforeOverlay.push_back(renderer.ColourAt(
                    static_cast<std::uint32_t>(quad.x + quad.width / 2),
                    static_cast<std::uint32_t>(quad.y + quad.height / 2)
                ));
            }

            renderer.DrawOverlay(quads);
            ++drawn;

            // Read BEFORE presenting. A swap leaves the back buffer
            // undefined, so sampling after it measures nothing -- which is
            // exactly what this probe did on its first run, and it reported
            // zero of thirty-nine quads landed.
            std::size_t changed = 0;
            for (std::size_t index = 0; index < quads.size(); ++index)
            {
                const presentation::OverlayQuad& quad = quads[index];
                if (renderer.ColourAt(
                        static_cast<std::uint32_t>(quad.x + quad.width / 2),
                        static_cast<std::uint32_t>(quad.y + quad.height / 2))
                    != beforeOverlay[index])
                {
                    ++changed;
                }
            }
            // Not every glyph centre is inked -- the middle of an 'o' is a
            // hole -- so this is a majority, not all of them. A backend that
            // drew nothing, or drew somewhere else, fails it outright.
            Check(changed * 2 > quads.size(),
                "the overlay did not land where the layout put it ("
                    + std::to_string(changed) + " of "
                    + std::to_string(quads.size()) + " quads)");
            renderer.Present();
            std::cout << "  panel: " << quads.size() << " quads, "
                      << changed << " landed" << '\n';
        }
    }

    // --- the widget answers in Entities -------------------------------
    //
    // The conversion from a raster index to a stable Entity used to live in
    // every host that drew a map. It is the widget's own answer now, and the
    // gate is that it agrees with the index the id attachment actually holds.
    {
        const std::uint32_t x = options.windowWidth / 2;
        const std::uint32_t y = options.windowHeight / 2;
        const std::uint16_t index = renderer.PickAt(x, y);
        Check(renderer.PickEntityAt(x, y) == entityIndex.EntityFor(index),
            "the widget's Entity pick disagrees with its own raster index");
        renderer.RequestPick(x, y);
        renderer.Draw(map, client.camera);
        ++drawn;
        renderer.RequestPick(x, y);
        Check(renderer.LastPickedEntity()
                == entityIndex.EntityFor(renderer.LastPick()),
            "the widget's asynchronous Entity disagrees with its own index");
    }

    // --- the map point under a pixel ----------------------------------
    //
    // A third attachment carries (u,v), because inverting the morph at an
    // arbitrary bend is not possible and the fragment shader already had the
    // value. It is what lets the wheel zoom towards the cursor.
    {
        const std::uint32_t x = options.windowWidth / 2;
        const std::uint32_t y = options.windowHeight / 2;
        double u = -1.0;
        double v = -1.0;
        Check(renderer.MapPointAt(x, y, u, v),
            "the centre of the screen has no map point");
        Check(u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0,
            "the map point is outside the map: " + std::to_string(u) + ", "
                + std::to_string(v));
        // It has to be the point that was DRAWN there, not just any point.
        // The province the id attachment names for the same pixel is the one
        // the raster holds at that (u,v), and the two are written by the same
        // fragment invocation.
        const std::uint16_t atPixel = renderer.PickAt(x, y);
        const std::uint32_t column = std::min(
            static_cast<std::uint32_t>(u * (raster.width - 1)),
            raster.width - 1
        );
        const std::uint32_t rasterRow = std::min(
            static_cast<std::uint32_t>(v * (raster.height - 1)),
            raster.height - 1
        );
        Check(raster.indices[
                static_cast<std::size_t>(rasterRow) * raster.width + column]
                == atPixel,
            "the map point and the picked province disagree about the pixel");
        // A pixel off the map has no map point rather than a default one.
        Check(!renderer.MapPointAt(0, 0, u, v)
                || renderer.PickAt(0, 0) != 0,
            "a pixel off the map returned a map point anyway");
    }

    // --- the cut edge is behind the camera, and costs nothing but pixels
    //
    // The grid is built centred on the camera, so the cylinder's cut is always
    // at the far end of the strip. Two things follow and both are checked:
    // every longitude is reachable while partly unfolded, and a province the
    // cut passes through is drawn TWICE without becoming two provinces -- both
    // copies carry the same index, so a pick on either resolves to one Entity.
    {
        presentation::MapCamera roaming;
        roaming.lookAtV = 0.5;
        roaming.distance = 0.8;
        roaming.bend = 0.5;
        const std::uint32_t midX = options.windowWidth / 2;
        const std::uint32_t midY = options.windowHeight / 2;

        std::size_t onMap = 0;
        std::size_t agreed = 0;
        for (int step = 0; step < 16; ++step)
        {
            roaming.lookAtU = static_cast<double>(step) / 16.0;
            renderer.Draw(map, roaming);
            ++drawn;
            const std::uint16_t centre = renderer.PickAt(midX, midY);
            double u = 0.0;
            double v = 0.0;
            if (centre == 0)
            {
                continue;
            }
            ++onMap;
            // The map point under the middle of the screen has to be the
            // longitude the camera is looking at, whatever the offset -- that
            // is the wrap working. A cut left at a fixed longitude would put a
            // hole here for some of these.
            Check(renderer.MapPointAt(midX, midY, u, v),
                "no map point at the centre while turned to "
                    + std::to_string(roaming.lookAtU));
            const double delta = std::abs(u - roaming.lookAtU);
            if (std::min(delta, 1.0 - delta) < 0.02)
            {
                ++agreed;
            }
            // Whatever is drawn at the far edges of the screen is still a
            // province of this world rather than a wrapped index of its own.
            for (const std::uint32_t x : {0u, options.windowWidth - 1u})
            {
                const std::uint16_t edge = renderer.PickAt(x, midY);
                Check(edge <= raster.provinceCount,
                    "the edge of a turned map picked index "
                        + std::to_string(edge));
                Check(edge == 0
                        || static_cast<bool>(entityIndex.EntityFor(edge)),
                    "a province drawn at the wrapped edge resolves to no "
                    "Entity");
            }
        }
        Check(onMap >= 12,
            "only " + std::to_string(onMap)
                + " of 16 longitudes put the map under the centre of the "
                  "screen; the cut is not staying behind the camera");
        Check(agreed == onMap,
            "the centre of the screen showed the camera's longitude on only "
                + std::to_string(agreed) + " of " + std::to_string(onMap)
                + " turns");
    }

    // --- resizing rebuilds the offscreen targets ----------------------
    //
    // The window used to be fixed size, and the attachments were created once
    // at the size it opened with. A resizable window without this is worse
    // than a fixed one: the map would be drawn into a buffer of the old size
    // and picking would read a pixel that is no longer where the cursor is.
    {
        const std::uint32_t widths[] = {800, 641, 640};
        for (const std::uint32_t width : widths)
        {
            const std::uint32_t height = width * 9u / 16u;
            renderer.Resize(width, height);
            renderer.Draw(map, client.camera);
            ++drawn;
            // The centre of a resized window still has to name a province,
            // and picking has to agree with the new geometry rather than
            // reading outside the old attachment.
            const std::uint16_t centre =
                renderer.PickAt(width / 2, height / 2);
            Check(centre <= raster.provinceCount,
                "picking after a resize to " + std::to_string(width) + "x"
                    + std::to_string(height) + " returned "
                    + std::to_string(centre));
            // A pixel that only exists at the new size.
            Check(renderer.ColourAt(width - 1, height - 1) != 0
                    || renderer.PickAt(width - 1, height - 1)
                        <= raster.provinceCount,
                "the corner of the resized window is not addressable");
        }
        renderer.Resize(options.windowWidth, options.windowHeight);
        renderer.Draw(map, client.camera);
        ++drawn;
    }

    // --- the asynchronous pick agrees with the synchronous one ---------
    //
    // RequestPick exists so hover does not stall the pipeline every frame. It
    // is only worth having if it returns the same answer a frame later, so
    // that is what is checked: issue requests for a pixel, keep drawing, and
    // the collected value must equal what PickAt says about the same pixel.
    {
        const std::uint32_t x = options.windowWidth / 2;
        const std::uint32_t y = options.windowHeight / 2;
        const std::uint16_t exact = renderer.PickAt(x, y);
        // Two requests: the first has nothing to collect, the second collects
        // the first. That two-step is the whole mechanism.
        renderer.RequestPick(x, y);
        renderer.Draw(map, client.camera);
        ++drawn;
        renderer.RequestPick(x, y);
        Check(renderer.LastPick() == exact,
            "the asynchronous pick returned "
                + std::to_string(renderer.LastPick())
                + " where the synchronous one returned "
                + std::to_string(exact));
    }

    // --- the windowed Host and a headless run land on the same save ------
    //
    // The last gate section 4.4.4 asks for, and the one that can only be
    // checked here: everything else in this suite compares two HEADLESS paths,
    // which says nothing about whether drawing changes a world.
    //
    // It could. A backend reads a snapshot every frame, and a renderer that
    // held a mutable reference, or that ticked to get one, or that picked up a
    // command of its own, would produce a world that depends on how often
    // somebody looked at it. The claim is that it does not, and the way to
    // check it is to run the same commands twice -- once with a window drawing
    // every tick, once with nothing looking -- and compare the bytes.
    {
        host::StandaloneSession headless;
        host::StandaloneSessionReport headlessReport;
        if (!headless.Start(config, headlessReport))
        {
            Check(false, "the headless comparison world did not load");
        }
        else
        {
            host::StandaloneSession windowed;
            host::StandaloneSessionReport windowedReport;
            Check(windowed.Start(config, windowedReport),
                "the windowed comparison world did not load");

            // One command log, authored once from the compiled view so both
            // runs submit exactly the same transactions.
            presentation::MapCommandTranslator parityTranslator;
            presentation::PresentationView parityView;
            parityView.Advance(
                std::make_shared<const runtime::WorldQuerySnapshot>(
                    windowed.Runtime().Query()
                )
            );
            Check(parityTranslator.Bind(
                      windowed.Catalog(),
                      presentation::MapCommandSpec{
                          tree.Definition(), tree.SubjectRole()},
                      message)
                    == presentation::MapCommandStatus::Ok,
                "the parity translator did not bind");
            parityTranslator.Resolve(parityView);

            std::vector<kernel::WorldTransaction> log;
            for (const std::uint32_t index : {1u, 4096u, 9000u, 14187u})
            {
                presentation::MapIntent intent;
                intent.entity = entityIndex.EntityFor(index);
                intent.capability = kernel::StableCapabilityId(
                    "dillen.map.site_development");
                intent.capabilityVersion = 1;
                intent.field = tree.View().boundFieldSlots.front();
                intent.delta = 1 + static_cast<std::int64_t>(index % 7);
                kernel::WorldTransaction transaction;
                Check(parityTranslator.Translate(intent, transaction)
                        == presentation::MapCommandStatus::Ok,
                    "a parity intent did not translate");
                log.push_back(std::move(transaction));
            }

            presentation::MapCamera parityCamera;
            parityCamera.distance = 1.2;
            for (std::uint64_t tick = 1; tick <= 10; ++tick)
            {
                if (tick == 3 || tick == 6)
                {
                    for (const kernel::WorldTransaction& transaction : log)
                    {
                        kernel::WorldTransaction a = transaction;
                        kernel::WorldTransaction b = transaction;
                        headless.Runtime().Enqueue(std::move(a), tick, 0);
                        windowed.Runtime().Enqueue(std::move(b), tick, 0);
                    }
                }
                Check(static_cast<bool>(headless.Runtime().RunTick(tick)),
                    "a headless tick failed");
                Check(static_cast<bool>(windowed.Runtime().RunTick(tick)),
                    "a windowed tick failed");

                // The window does what a window does, every tick: read the
                // new snapshot, recolour, draw, pick, present.
                parityView.Advance(
                    std::make_shared<const runtime::WorldQuerySnapshot>(
                        windowed.Runtime().Query()
                    )
                );
                parityCamera.lookAtU = static_cast<double>(tick) / 10.0;
                parityCamera.bend = 1.0 - static_cast<double>(tick) / 20.0;
                renderer.Draw(map, parityCamera);
                renderer.PickAt(
                    options.windowWidth / 2, options.windowHeight / 2);
                renderer.Present();
                ++drawn;
            }

            const persistence::RuntimePersistenceService persistence;
            std::vector<std::uint8_t> headlessSave;
            std::vector<std::uint8_t> windowedSave;
            Check(static_cast<bool>(
                    persistence.Save(headless.Runtime(), headlessSave)),
                "the headless world could not be saved");
            Check(static_cast<bool>(
                    persistence.Save(windowed.Runtime(), windowedSave)),
                "the windowed world could not be saved");
            Check(!headlessSave.empty() && headlessSave == windowedSave,
                "a world that was drawn ten times differs from one that was "
                "not: " + std::to_string(windowedSave.size())
                    + " bytes against "
                    + std::to_string(headlessSave.size()));
            std::cout << "  host parity: " << windowedSave.size()
                      << " identical save bytes with and without a window"
                      << std::endl;
        }
    }

    // --- the GL half of close and reopen ---------------------------------
    //
    // presentation_lifecycle_probe destroys and rebuilds the CPU presentation
    // stack, which is the half about the world not depending on a viewer. It
    // runs headless, so it never touches a GL object -- and the resources that
    // are actually hard to tear down and rebuild inside one process are the GL
    // ones: the context, the shader program, the index texture, the palette,
    // the render targets, the pixel pack buffers. Leak or reuse any of them
    // across a Close and the second Open either fails or draws the first map.
    //
    // So: record what the renderer draws and picks, close it, open it again in
    // the same process, and require the same answers. Comparing a pick alone
    // would pass for a renderer that reopened with a black screen, so the
    // colour is checked too, and required to be non-zero.
    {
        const std::uint32_t x = options.windowWidth / 2;
        const std::uint32_t y = options.windowHeight / 2;
        const std::uint32_t paletteSideBefore = renderer.PaletteSide();
        presentation::MapCamera settled;
        settled.distance = 1.1;
        settled.lookAtU = 0.42;
        settled.lookAtV = 0.55;
        settled.bend = 1.0;

        renderer.Draw(map, settled);
        ++drawn;
        const std::uint16_t pickedBefore = renderer.PickAt(x, y);
        const std::uint32_t colourBefore = renderer.ColourAt(x, y);
        Check(colourBefore != 0,
            "the reference frame for the reopen check is blank");

        renderer.Close();

        std::string reopenMessage;
        const presentation::gl::MapRendererStatus reopened =
            renderer.Open(options, raster, reopenMessage);
        Check(reopened == presentation::gl::MapRendererStatus::Ok,
            "the renderer would not reopen in the same process: "
                + reopenMessage);
        if (reopened == presentation::gl::MapRendererStatus::Ok)
        {
            // Everything Open decides has to be decided again, not inherited.
            Check(renderer.PaletteSide() == paletteSideBefore,
                "the reopened renderer sized its palette differently: "
                    + std::to_string(renderer.PaletteSide()) + " against "
                    + std::to_string(paletteSideBefore));
            renderer.SetEntityIndex(&entityIndex);
            fillPalette();
            renderer.SetPalette(palette);

            renderer.Draw(map, settled);
            ++drawn;
            Check(renderer.PickAt(x, y) == pickedBefore,
                "the reopened renderer picks "
                    + std::to_string(renderer.PickAt(x, y))
                    + " where it picked " + std::to_string(pickedBefore));
            Check(renderer.ColourAt(x, y) == colourBefore,
                "the reopened renderer draws a different colour at the same "
                "camera");
            // And the Entity mapping survives, which is the part a renderer
            // that reopened with a stale index would get wrong.
            Check(renderer.PickEntityAt(x, y)
                    == entityIndex.EntityFor(pickedBefore),
                "the reopened renderer resolves the picked province to a "
                "different Entity");
        }
    }

    renderer.Close();

    if (failures != 0)
    {
        std::cerr << "map renderer: " << failures << " failure(s)\n";
        return 5;
    }
    std::cout << "Map renderer smoke: passed (" << raster.width << "x"
              << raster.height << " index texture, " << projection.Count()
              << " palette entries, " << drawn << " frames, " << hits
              << " picks, one pick-command-tick-redraw loop, "
              << "a highlight, a drawn panel, three resizes, an async pick and a windowed/headless save comparison)\n";
    return 0;
}
