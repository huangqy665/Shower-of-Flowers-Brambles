#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map_entity_index.hpp"
#include "map_index_raster.hpp"
#include "map_view.hpp"
#include "overlay.hpp"

namespace dillen::presentation::gl {

// The map renderer: a window, an OpenGL 3.3 core context, and one draw call.
//
// Everything that can be got wrong without a GPU has already been got wrong
// somewhere else and fixed there. The morph, the camera, the projection table
// and the index raster are all covered by probes that run headless in the
// standard suite. What is left here is genuinely a backend: create a context,
// upload three buffers, bind two textures, draw a grid, read one pixel back.
//
// HOW IT DRAWS 14187 PROVINCES AT NO PER-PROVINCE COST
//
// It does not draw provinces. It draws one tessellated lat/lon grid, and the
// fragment shader turns (u,v) into a colour with two texture fetches:
//
//   * an R16UI index texture, sampled with texelFetch -- the province index
//     under this pixel;
//   * an RGBA8 palette, 128x128, indexed by that number.
//
// So the per-frame cost is O(screen pixels) and the per-province cost is one
// palette texel, refreshed only when the projection changes. A million-province
// map would cost the same to draw.
//
// The index texture is NEAREST with no mipmaps, and that is not a quality
// setting. Interpolating two province indices produces a third index that
// belongs to an unrelated province on the other side of the map. There is no
// filtering that is meaningful on identity data.

struct MapRendererOptions
{
    std::string title = "Dillen Map";
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;
    // Grid tessellation. 512x256 quads is smooth on a sphere and costs a
    // quarter of a million triangles, which is nothing next to the pixels.
    std::uint32_t gridColumns = 512;
    std::uint32_t gridRows = 256;
    // Off by default so a smoke test can create a context, upload, draw one
    // frame and exit without anyone watching.
    bool visible = true;
    // A visible window that cannot be resized is a window that has decided
    // what size the player's screen is. The offscreen targets are rebuilt to
    // match, which is the part that has to exist for this to be more than a
    // flag.
    bool resizable = true;
    // Whether to keep the (u,v) attachment MapPointAt reads.
    //
    // Off by default, and the default is the honest one: the shipped viewer
    // anchors its zoom on the PROVINCE under the cursor, which comes from the
    // id attachment and a centroid table, so nothing in the product path asks
    // where a pixel is on the map. Writing a third render target every frame
    // for a question nobody asks costs memory and bandwidth for nothing.
    //
    // It stays available because it is the only exact answer to "what map
    // point is this pixel" -- inverting the morph at an arbitrary bend is not
    // possible -- and the renderer's own gates use it to check that the wrap
    // puts the camera's longitude under the middle of the screen. A diagnostic
    // with a stated purpose, rather than a target nobody could account for.
    bool mapPointReadback = false;
};

// One frame of input, already reduced to what a map viewer asks about.
//
// SDL does not leave this module. An application that had to include SDL to
// read a mouse position would be an application that has to be built against
// the platform backend, and then "delete the renderer" stops being a
// mechanical removal.
struct MapInput
{
    bool quit = false;
    // Window coordinates, origin top left -- the same space the control layout
    // works in, so a hit test needs no conversion.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // Went down during this frame, not "is held". A viewer wants edges.
    bool pressed = false;
    // Notches this frame; positive is away from the viewer.
    double wheel = 0.0;
    // Held keys, as a direction rather than a key name.
    std::int32_t panX = 0;
    std::int32_t panY = 0;
    // Held: flattens towards a plane or curves towards a sphere.
    std::int32_t bend = 0;
    // A curvature the viewer asked for outright, or a negative number for "no
    // preference". Held keys move the value; these jump to it, which is what
    // makes the plane reachable without knowing it is 170 frames away.
    double bendPreset = -1.0;
    // Pressed this frame.
    bool step = false;
    // The window changed size this frame; `viewportWidth` and
    // `viewportHeight` are the new one. A caller has to re-lay out anything
    // it positioned against the old size.
    bool resized = false;
    std::uint32_t viewportWidth = 0;
    std::uint32_t viewportHeight = 0;
    // Movement in pixels while the middle (or right) button is held. Turning a
    // globe by dragging it is the only interaction here that a person expects
    // to already work, so it is not left to the arrow keys.
    std::int32_t dragX = 0;
    std::int32_t dragY = 0;
};

enum class MapRendererStatus
{
    Ok,
    SdlInitFailed,
    WindowFailed,
    ContextFailed,
    ExtensionMissing,
    ShaderFailed,
    RasterInvalid
};

class MapRenderer
{
public:
    MapRenderer() = default;
    ~MapRenderer();
    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;

    MapRendererStatus Open(
        const MapRendererOptions& options,
        const MapIndexRaster& raster,
        std::string& message
    );
    void Close();
    bool IsOpen() const noexcept { return window_ != nullptr; }

    // RGBA8 per province index, row 0 reserved. Uploaded as a 128x128 texture;
    // the shader reconstructs the coordinate as (i % 128, i / 128).
    // Texels a palette upload must contain: PaletteSide() squared. Sized from
    // the map's province count at Open, so a caller fills what the map needs
    // rather than a constant that used to cap the world at 16383 regions.
    std::uint32_t PaletteSide() const noexcept;
    std::size_t PaletteSize() const noexcept;

    void SetPalette(const std::vector<std::uint32_t>& palette);

    // The province the map shader tints. 0 is none, which is what an
    // unselected client state means. The tint is applied by the SAME fragment
    // shader invocation that writes the id attachment, so the province that
    // lights up and the province a pick returns cannot disagree.
    void SetSelection(std::uint16_t provinceIndex);

    // Single channel coverage, `width * height` bytes. Uploaded as an R8
    // texture for the overlay pass to sample.
    void SetFontAtlas(
        const std::vector<std::uint8_t>& coverage,
        std::uint32_t width,
        std::uint32_t height
    );

    // Draws the map into the offscreen targets and blits the colour to the
    // window. It does NOT present: an overlay has to land on top of it first.
    void Draw(const MapProjection& projection, const MapCamera& camera);

    // Screen space quads, in the order given, alpha blended over whatever the
    // map left. Solid quads are their colour; textured quads multiply it by
    // the atlas coverage.
    void DrawOverlay(const std::vector<OverlayQuad>& quads);

    void Present();

    // Rebuilds the offscreen attachments at a new drawable size. Called for
    // you when PollInput sees the window change; public so a host that drives
    // its own event loop can too.
    void Resize(std::uint32_t width, std::uint32_t height);

    // Drains the platform event queue and returns the frame's input. Also the
    // only place the window can ask to be closed.
    MapInput PollInput();

    // The province index under a window pixel, or 0 for none.
    //
    // Read back from an integer render target rather than derived from the
    // morph. Inverting the bend analytically is not possible for arbitrary b,
    // and a renderer that picks what it drew cannot disagree with what the
    // viewer sees.
    // A synchronous readback: it stalls until the GPU has finished the frame.
    // Correct, exact, and the right thing for a click or a probe -- and the
    // wrong thing to call every frame, which is what RequestPick is for.
    std::uint16_t PickAt(std::uint32_t x, std::uint32_t y);

    // Asks for the province under a pixel WITHOUT waiting for it.
    //
    // Hover has to be sampled continuously, and doing that with PickAt put a
    // full pipeline stall in every frame: the CPU asked the GPU for a pixel it
    // had not drawn yet and blocked until it had. The read is issued into a
    // pixel pack buffer instead and collected on a later frame, so nothing
    // waits. The answer is one or two frames old, which for a cursor is
    // invisible and for a click would not be -- hence two entry points rather
    // than one.
    void RequestPick(std::uint32_t x, std::uint32_t y);
    // The most recent completed request, or 0 before any has completed.
    std::uint16_t LastPick() const noexcept;

    // The index -> Entity table this widget resolves picks through.
    //
    // Held here so the widget's own API answers in stable identities. Before
    // this every caller took a raster index out of PickAt and converted it
    // itself, which put the one piece of the map that is not a position -- the
    // identity -- outside the widget that produced it, and made every host
    // repeat the conversion.
    void SetEntityIndex(const MapEntityIndex* entities) noexcept;

    // The Entity under a window pixel, or empty.
    //
    // This is the entry point a host should use. PickAt and LastPick remain
    // for the backend's own gates, which have to assert about raster indices
    // because that is what the id attachment holds.
    kernel::EntityId PickEntityAt(std::uint32_t x, std::uint32_t y);
    kernel::EntityId LastPickedEntity() const noexcept;

    // The map point under a window pixel. False when the pixel is off the map.
    //
    // Read out of a render target rather than computed, for the same reason
    // picking is: inverting the morph analytically is not possible at an
    // arbitrary bend, and a value the fragment shader already had is exact by
    // construction. It is what lets a wheel zoom towards the cursor instead of
    // towards the middle of the screen.
    bool MapPointAt(
        std::uint32_t x,
        std::uint32_t y,
        double& u,
        double& v
    );

    // The colour at a window pixel, 0xAABBGGRR, read from the default
    // framebuffer -- so before DrawOverlay it is the map and after it is what
    // the viewer sees. It exists because the two things this backend adds that
    // are not decided on the CPU -- the selection tint and whether the overlay
    // landed at all -- can only be checked by looking at pixels.
    std::uint32_t ColourAt(std::uint32_t x, std::uint32_t y);

    const std::string& Diagnostics() const noexcept { return diagnostics_; }

private:
    struct Impl;

    bool BuildTargets();

    void* window_ = nullptr;
    void* context_ = nullptr;
    Impl* impl_ = nullptr;
    std::string diagnostics_;
};

}
