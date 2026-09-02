#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map_index_raster.hpp"
#include "map_view.hpp"

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
    void SetPalette(const std::vector<std::uint32_t>& palette);

    void Draw(const MapProjection& projection, const MapCamera& camera);

    // The province index under a window pixel, or 0 for none.
    //
    // Read back from an integer render target rather than derived from the
    // morph. Inverting the bend analytically is not possible for arbitrary b,
    // and a renderer that picks what it drew cannot disagree with what the
    // viewer sees.
    std::uint16_t PickAt(std::uint32_t x, std::uint32_t y);

    const std::string& Diagnostics() const noexcept { return diagnostics_; }

private:
    struct Impl;

    void* window_ = nullptr;
    void* context_ = nullptr;
    Impl* impl_ = nullptr;
    std::string diagnostics_;
};

}
