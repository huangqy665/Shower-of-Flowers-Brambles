#include "overlay.hpp"

namespace dillen::presentation {

std::vector<OverlayQuad> BuildPanelOverlay(
    const ControlTree& tree,
    const TextAtlas& atlas,
    const MechanismPanelReadout& readout,
    std::uint32_t hovered,
    const OverlayStyle& style
)
{
    std::vector<OverlayQuad> quads;
    if (!tree.IsBound())
    {
        return quads;
    }

    // Two passes over the same draw list: surfaces, then text. A backend can
    // then draw the list in order with no sorting and no depth buffer, and
    // text is never painted over by the panel behind it.
    const std::vector<ControlDraw> draws = tree.Draw(readout);

    // Which control paints is DECLARED, by the `background` property in the
    // layout. It used to be inferred from the control's id being
    // "province_panel", so renaming the root in a Package silently lost the
    // background -- a Package that loaded cleanly and drew wrongly, which is
    // exactly the failure the "refuse, do not ignore" rule exists to prevent.
    for (const ControlDraw& draw : draws)
    {
        if (!draw.background
            || draw.rect.width <= 0
            || draw.rect.height <= 0)
        {
            continue;
        }
        OverlayQuad quad;
        quad.x = draw.rect.x;
        quad.y = draw.rect.y;
        quad.width = draw.rect.width;
        quad.height = draw.rect.height;
        quad.colour = draw.control == hovered
            ? style.hoveredColour
            : (draw.actionable ? style.buttonColour : style.panelColour);
        quads.push_back(quad);
    }

    if (!atlas.IsLoaded())
    {
        // A Package with no font still has an interface, it just has no
        // captions. Returning the surfaces rather than nothing is the honest
        // answer: the panel is there, the text is missing, and a blank panel
        // says so.
        return quads;
    }

    for (const ControlDraw& draw : draws)
    {
        if (draw.text.empty() || draw.rect.width <= 0)
        {
            continue;
        }
        ControlRect box = draw.rect;
        if (draw.actionable)
        {
            // A control that can act centres its caption; a row is left
            // aligned against the panel's padding.
            const std::int32_t width = atlas.Measure(draw.text);
            const std::int32_t inset = (box.width - width) / 2;
            if (inset > 0)
            {
                box.x += inset;
                box.width -= inset;
            }
        }
        for (const TextQuad& glyph : atlas.LayoutText(draw.text, box))
        {
            OverlayQuad quad;
            quad.x = glyph.x;
            quad.y = glyph.y;
            quad.width = glyph.width;
            quad.height = glyph.height;
            quad.atlasX = glyph.atlasX;
            quad.atlasY = glyph.atlasY;
            quad.textured = true;
            quad.colour = style.textColour;
            quads.push_back(quad);
        }
    }
    return quads;
}

}
