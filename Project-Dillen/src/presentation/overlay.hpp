#pragma once

#include <cstdint>
#include <vector>

#include "control_tree.hpp"
#include "mechanism_panel.hpp"
#include "text_atlas.hpp"

namespace dillen::presentation {

// The interface, reduced to a list of rectangles.
//
// This is the whole of what a backend has to draw: solid quads for panels and
// buttons, textured quads for glyphs, in the order they are listed. Deciding
// WHICH rectangles is the interesting part and it is all here, on the CPU,
// where a probe can read it.
//
// The alternative -- a backend that walks the control tree and calls the atlas
// itself -- would put the same decisions behind a GPU and a display server,
// where the Linux cells never run them. That is the same reason the morph is
// computed on the CPU and the read model lives outside the renderer.

struct OverlayQuad
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    // Source in the font atlas, in texels. Meaningless unless `textured`.
    std::uint16_t atlasX = 0;
    std::uint16_t atlasY = 0;
    // A glyph samples the atlas for coverage and multiplies by `colour`; a
    // solid quad is `colour` alone.
    bool textured = false;
    // 0xAABBGGRR, the same byte order the palette uses, so a backend that can
    // upload one can upload the other.
    std::uint32_t colour = 0xFFFFFFFFu;
};

struct OverlayStyle
{
    std::uint32_t panelColour = 0xE0141414u;
    std::uint32_t buttonColour = 0xFF3A3A3Au;
    std::uint32_t hoveredColour = 0xFF5A5A5Au;
    std::uint32_t textColour = 0xFFF0F0F0u;
};

// Panels and buttons first, then their text, so a backend can draw the list in
// order with no sorting and no depth. `hovered` may be null.
// `hovered` is a control index, or UINT32_MAX for none.
std::vector<OverlayQuad> BuildPanelOverlay(
    const ControlTree& tree,
    const TextAtlas& atlas,
    const MechanismPanelReadout& readout,
    std::uint32_t hovered,
    const OverlayStyle& style = {}
);

}
