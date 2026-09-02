#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "control_tree.hpp"
#include "presentation_asset.hpp"

namespace dillen::presentation {

// A rasterised font, and the arithmetic that places its glyphs.
//
// The Presentation Package ships the FONT, not an atlas. An atlas is
// rasterised at one pixel size, so baking one would fix the interface to a
// single size and display -- and being able to choose the size at runtime is
// the entire reason a real font library is vendored instead of a bitmap strip.
// So the Package carries the TTF with a digest, and this rasterises from it.
//
// Everything here is CPU work: a coverage bitmap, a metrics table, and integer
// pen arithmetic. A backend uploads the bitmap and draws the quads; it decides
// nothing. That keeps the text layer under the same headless gates as the
// layout it sits inside, which matters because a wrong advance is invisible in
// a screenshot and obvious in an assertion.

struct GlyphMetrics
{
    // Position of the glyph's box inside the atlas bitmap.
    std::uint16_t atlasX = 0;
    std::uint16_t atlasY = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    // Offset from the pen position to the top-left of the box, and how far the
    // pen moves afterwards. Integers: the atlas is rasterised at a fixed pixel
    // size, so there is no reason to carry a fraction that two machines could
    // round apart.
    std::int16_t bearingX = 0;
    std::int16_t bearingY = 0;
    std::int16_t advance = 0;
};

// One glyph, placed. `atlas*` addresses the bitmap; the rest is where it goes
// on screen.
struct TextQuad
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint16_t atlasX = 0;
    std::uint16_t atlasY = 0;
};

enum class TextAtlasStatus
{
    Ok,
    NotLoaded,
    // The asset is not a font, or its properties are malformed.
    AssetInvalid,
    // The payload does not match the digest the Package declared. Checked
    // BEFORE the font library is given a byte: handing a mutated font to a
    // font parser is precisely the input nobody wants it to see.
    DigestMismatch,
    PayloadMissing,
    // FreeType refused the file, or a glyph in the declared range.
    FontRejected
};

class TextAtlas
{
public:
    TextAtlas() = default;
    ~TextAtlas();
    TextAtlas(const TextAtlas&) = delete;
    TextAtlas& operator=(const TextAtlas&) = delete;

    // Reads the payload named by the asset, verifies its digest, and
    // rasterises the codepoint range the asset declares.
    TextAtlasStatus Load(
        const kernel::PresentationAsset& asset,
        std::string& message
    );

    bool IsLoaded() const noexcept { return loaded_; }

    // Single channel, 8-bit coverage, `width * height` bytes, row major.
    const std::vector<std::uint8_t>& Bitmap() const noexcept { return bitmap_; }
    std::uint16_t Width() const noexcept { return width_; }
    std::uint16_t Height() const noexcept { return height_; }
    std::int32_t PixelSize() const noexcept { return pixelSize_; }
    // Baseline-to-baseline distance for stacked lines.
    std::int32_t LineHeight() const noexcept { return lineHeight_; }
    // Baseline offset from the top of a line box.
    std::int32_t Ascender() const noexcept { return ascender_; }
    std::size_t GlyphCount() const noexcept { return glyphs_.size(); }

    // By codepoint, not by `char`.
    //
    // The atlas used to be ASCII 32-126 and addressed by a signed char, which
    // is not a limit of anything here -- FreeType will rasterise any codepoint
    // the face has -- but of the storage and the addressing. A Package now
    // declares the ranges it wants and text arrives as UTF-8.
    const GlyphMetrics* Glyph(char32_t codepoint) const;

    // Decodes one UTF-8 sequence at `cursor`, advancing it. Malformed input
    // yields U+FFFD and advances by one byte, so a bad string costs a wrong
    // glyph rather than a loop that never terminates.
    static char32_t DecodeUtf8(const std::string& text, std::size_t& cursor);

    // Sum of advances. Codepoints outside the declared range are skipped
    // rather than substituted: a box glyph would be a lie about what the
    // Package can draw.
    std::int32_t Measure(const std::string& text) const;

    // Places `text` inside `rect`, left aligned and vertically centred, and
    // clips whole glyphs at the right edge. Clipping by glyph rather than by
    // pixel keeps the result the same whatever the backend does with a partial
    // quad.
    std::vector<TextQuad> LayoutText(
        const std::string& text,
        const ControlRect& rect
    ) const;

private:
    void Release();

    bool loaded_ = false;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::int32_t pixelSize_ = 0;
    std::int32_t lineHeight_ = 0;
    std::int32_t ascender_ = 0;
    // The declared ranges, in order, and where each one starts in `glyphs_`.
    // Ranges need not be contiguous -- a CJK font wants Latin and a block far
    // above it and nothing in between -- so a single first/last cannot
    // describe them and a flat array cannot store them.
    struct CodepointRange
    {
        char32_t first = 0;
        char32_t last = 0;
        std::uint32_t firstGlyph = 0;
    };
    std::vector<CodepointRange> ranges_;
    std::vector<std::uint8_t> bitmap_;
    std::vector<GlyphMetrics> glyphs_;
};

}
