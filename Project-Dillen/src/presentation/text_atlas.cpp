#include "text_atlas.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <fstream>
#include <iterator>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "package_content_digest.hpp"

namespace dillen::presentation {

namespace {

// Padding between glyph boxes in the atlas. One pixel, so a backend sampling
// with any filter cannot pull coverage from the neighbouring glyph.
constexpr std::int32_t kGlyphPadding = 1;

// The atlas is at most 4096 x 4096.
//
// That is one sheet, and it is enough for the case that made the old
// single-row packer unusable: the common CJK block is about twenty thousand
// glyphs, which at a UI size is roughly 5.4 million covered pixels against the
// 16.7 million a 4096 square holds. In one row those glyphs would have been a
// quarter of a million pixels wide and no texture at all.
//
// A declaration that does NOT fit is refused rather than truncated. Silently
// dropping the tail of a range is how a font ends up missing exactly the
// characters nobody tested with.
constexpr std::int32_t kMaxAtlasWidth = 4096;
constexpr std::int32_t kMaxAtlasHeight = 4096;

// A shelf packer.
//
// Glyphs go left to right on a shelf whose height is the tallest glyph placed
// on it; when the next one does not fit, a new shelf starts below. Sorting
// first would pack tighter, but it would also make the atlas layout depend on
// a sort of floating-point-derived heights, and the layout is something probes
// assert about. Insertion order is what the Package declared.
struct Shelf
{
    std::int32_t x = kGlyphPadding;
    std::int32_t y = kGlyphPadding;
    std::int32_t height = 0;
};

// `32-126,0x4E00-0x9FFF`. Decimal or hex, inclusive on both ends.
bool ParseCodepointRanges(
    const std::string& text,
    std::vector<std::pair<char32_t, char32_t>>& output
)
{
    const auto number = [](const std::string& piece, char32_t& value)
    {
        if (piece.empty())
        {
            return false;
        }
        const bool hex = piece.size() > 2 && piece[0] == '0'
            && (piece[1] == 'x' || piece[1] == 'X');
        std::uint64_t parsed = 0;
        for (std::size_t index = hex ? 2u : 0u; index < piece.size(); ++index)
        {
            const char character = piece[index];
            std::uint64_t digit = 0;
            if (character >= '0' && character <= '9')
            {
                digit = static_cast<std::uint64_t>(character - '0');
            }
            else if (hex && character >= 'a' && character <= 'f')
            {
                digit = static_cast<std::uint64_t>(character - 'a') + 10u;
            }
            else if (hex && character >= 'A' && character <= 'F')
            {
                digit = static_cast<std::uint64_t>(character - 'A') + 10u;
            }
            else
            {
                return false;
            }
            parsed = parsed * (hex ? 16u : 10u) + digit;
            if (parsed > 0x10FFFF)
            {
                return false;
            }
        }
        value = static_cast<char32_t>(parsed);
        return true;
    };

    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t comma = text.find(',', cursor);
        const std::string piece = text.substr(
            cursor,
            comma == std::string::npos ? std::string::npos : comma - cursor
        );
        const std::size_t dash = piece.find('-');
        if (dash == std::string::npos)
        {
            return false;
        }
        char32_t low = 0;
        char32_t high = 0;
        if (!number(piece.substr(0, dash), low)
            || !number(piece.substr(dash + 1), high)
            || high < low)
        {
            return false;
        }
        output.push_back({low, high});
        if (comma == std::string::npos)
        {
            break;
        }
        cursor = comma + 1;
    }
    return !output.empty();
}

bool ReadUnsigned(
    const kernel::PresentationAsset& asset,
    const std::string& key,
    std::int32_t& output,
    std::string& message
)
{
    const auto entry = asset.properties.find(key);
    if (entry == asset.properties.end() || entry->second.empty())
    {
        message = "property '" + key + "' is missing";
        return false;
    }
    std::int64_t value = 0;
    for (const char character : entry->second)
    {
        if (character < '0' || character > '9')
        {
            message = "property '" + key + "' is not a number";
            return false;
        }
        value = value * 10 + (character - '0');
        if (value > 0x7FFFFFFF)
        {
            message = "property '" + key + "' is out of range";
            return false;
        }
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

}

TextAtlas::~TextAtlas()
{
    Release();
}

void TextAtlas::Release()
{
    loaded_ = false;
    bitmap_.clear();
    glyphs_.clear();
    width_ = 0;
    height_ = 0;
    pixelSize_ = 0;
    lineHeight_ = 0;
    ascender_ = 0;
    ranges_.clear();
}

TextAtlasStatus TextAtlas::Load(
    const kernel::PresentationAsset& asset,
    std::string& message
)
{
    Release();
    if (asset.kind != "font")
    {
        message = "asset '" + asset.canonicalName + "' is a " + asset.kind;
        return TextAtlasStatus::AssetInvalid;
    }
    const auto format = asset.properties.find("format");
    if (format == asset.properties.end() || format->second != "truetype")
    {
        message = "the font declares no truetype format";
        return TextAtlasStatus::AssetInvalid;
    }
    std::int32_t pixelSize = 0;
    std::int32_t first = 0;
    std::int32_t last = 0;
    if (!ReadUnsigned(asset, "pixel_size", pixelSize, message)
        || !ReadUnsigned(asset, "first_codepoint", first, message)
        || !ReadUnsigned(asset, "last_codepoint", last, message))
    {
        return TextAtlasStatus::AssetInvalid;
    }
    if (pixelSize <= 0 || pixelSize > 256 || first < 0 || last < first
        || last > 0x10FFFF)
    {
        message = "the declared pixel size or codepoint range is not usable";
        return TextAtlasStatus::AssetInvalid;
    }

    // The ranges to rasterise.
    //
    // `codepoints` is the general form -- a comma separated list of `A-B`,
    // decimal or 0x hex -- because the interesting ranges are not contiguous:
    // a Chinese UI wants Latin punctuation and a block starting at U+4E00 and
    // nothing in the twenty thousand codepoints between. `first_codepoint` and
    // `last_codepoint` remain as the one-range shorthand.
    std::vector<std::pair<char32_t, char32_t>> ranges;
    const auto listed = asset.properties.find("codepoints");
    if (listed != asset.properties.end() && !listed->second.empty())
    {
        if (!ParseCodepointRanges(listed->second, ranges))
        {
            message = "codepoints is not a list of ranges";
            return TextAtlasStatus::AssetInvalid;
        }
    }
    else
    {
        ranges.push_back({
            static_cast<char32_t>(first),
            static_cast<char32_t>(last)
        });
    }
    std::size_t declared = 0;
    for (const auto& range : ranges)
    {
        declared += static_cast<std::size_t>(range.second - range.first) + 1;
    }
    if (declared == 0 || declared > 100000)
    {
        message = "the declared codepoint ranges are empty or absurdly large";
        return TextAtlasStatus::AssetInvalid;
    }

    const std::filesystem::path payload =
        std::filesystem::path(asset.source.physicalDirectory)
            / asset.assetPath;
    std::string bytes;
    {
        std::ifstream stream(payload, std::ios::binary);
        if (!stream)
        {
            message = "payload " + payload.string() + " could not be opened";
            return TextAtlasStatus::PayloadMissing;
        }
        bytes.assign(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        );
    }
    if (bytes.empty())
    {
        message = "the font payload is empty";
        return TextAtlasStatus::PayloadMissing;
    }

    // Before FreeType sees a byte. A font file is parsed by a large C library
    // with a long history of being fed hostile input; the digest is what says
    // these are the bytes the Package shipped, and checking it afterwards
    // would be checking it after the risk was taken.
    const std::string digest = kernel::ComputeContentDigest(bytes);
    if (digest != asset.assetDigest)
    {
        message = "payload digest " + digest + " does not match the declared "
            + asset.assetDigest;
        return TextAtlasStatus::DigestMismatch;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
    {
        message = "the font library did not initialise";
        return TextAtlasStatus::FontRejected;
    }
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(
            library,
            reinterpret_cast<const FT_Byte*>(bytes.data()),
            static_cast<FT_Long>(bytes.size()),
            0,
            &face) != 0)
    {
        FT_Done_FreeType(library);
        message = "the font payload is not a face this library can read";
        return TextAtlasStatus::FontRejected;
    }
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        message = "the face does not accept the declared pixel size";
        return TextAtlasStatus::FontRejected;
    }

    const std::size_t count = declared;
    glyphs_.assign(count, GlyphMetrics{});
    ranges_.clear();
    {
        std::uint32_t base = 0;
        for (const auto& range : ranges)
        {
            ranges_.push_back({
                range.first,
                range.second,
                base
            });
            base += static_cast<std::uint32_t>(
                range.second - range.first) + 1u;
        }
    }

    // Two passes. The first measures every glyph so the atlas can be sized
    // exactly; the second blits. Growing a bitmap while blitting into it would
    // make the layout depend on the order glyphs happened to be rasterised in.
    struct Raster
    {
        std::vector<std::uint8_t> coverage;
        std::int32_t width = 0;
        std::int32_t height = 0;
    };
    std::vector<Raster> rasters(count);

    // Shelves, not one row. See kMaxAtlasWidth.
    //
    // The sheet width is declarable so a Package can target a device with a
    // smaller texture limit -- and so the wrapping itself is reachable by a
    // test: the Latin ranges this project ships fit in one shelf at 4096, so
    // a gate that could only use the default would pass whether the packer
    // wrapped or not.
    std::int32_t sheetWidth = kMaxAtlasWidth;
    {
        const auto declared = asset.properties.find("atlas_max_width");
        if (declared != asset.properties.end() && !declared->second.empty())
        {
            std::int32_t value = 0;
            std::string reason;
            if (!ReadUnsigned(asset, "atlas_max_width", value, reason)
                || value < 64 || value > kMaxAtlasWidth)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                message = "atlas_max_width is not between 64 and 4096";
                return TextAtlasStatus::AssetInvalid;
            }
            sheetWidth = value;
        }
    }
    Shelf shelf;
    std::int32_t atlasWidth = kGlyphPadding;
    std::int32_t atlasHeight = 0;
    std::size_t index = 0;
    for (const CodepointRange& range : ranges_)
    for (char32_t codepoint = range.first;
        codepoint <= range.last;
        ++codepoint, ++index)
    {
        // A codepoint the face does not map returns glyph 0 -- .notdef, the
        // hollow box -- and FT_Load_Char reports success for it. Accepting
        // that would mean a Package could declare any range at all and get
        // boxes, which is the substitution this layer refuses everywhere
        // else. A declaration has to be one the font can actually keep.
        if (FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint)) == 0
            || FT_Load_Char(
                face,
                static_cast<FT_ULong>(codepoint),
                FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            message = "the face has no glyph for codepoint "
                + std::to_string(static_cast<std::uint32_t>(codepoint));
            return TextAtlasStatus::FontRejected;
        }
        const FT_GlyphSlot slot = face->glyph;
        Raster& raster = rasters[index];
        raster.width = static_cast<std::int32_t>(slot->bitmap.width);
        raster.height = static_cast<std::int32_t>(slot->bitmap.rows);
        raster.coverage.resize(
            static_cast<std::size_t>(raster.width) * raster.height
        );
        for (std::int32_t row = 0; row < raster.height; ++row)
        {
            const unsigned char* source =
                slot->bitmap.buffer + static_cast<std::ptrdiff_t>(row)
                    * slot->bitmap.pitch;
            std::copy(
                source,
                source + raster.width,
                raster.coverage.begin()
                    + static_cast<std::ptrdiff_t>(row) * raster.width
            );
        }

        GlyphMetrics& metrics = glyphs_[index];
        metrics.width = static_cast<std::uint16_t>(raster.width);
        metrics.height = static_cast<std::uint16_t>(raster.height);
        metrics.bearingX = static_cast<std::int16_t>(slot->bitmap_left);
        metrics.bearingY = static_cast<std::int16_t>(slot->bitmap_top);
        // 26.6 fixed point, rounded to whole pixels once and stored. Carrying
        // the fraction would put a rounding rule between the measurement a
        // probe asserts and the pen a backend advances.
        metrics.advance =
            static_cast<std::int16_t>((slot->advance.x + 32) >> 6);
        if (shelf.x + raster.width + kGlyphPadding > sheetWidth
            && shelf.x > kGlyphPadding)
        {
            shelf.y += shelf.height + kGlyphPadding;
            shelf.x = kGlyphPadding;
            shelf.height = 0;
        }
        metrics.atlasX = static_cast<std::uint16_t>(shelf.x);
        metrics.atlasY = static_cast<std::uint16_t>(shelf.y);
        shelf.x += raster.width + kGlyphPadding;
        shelf.height = std::max(shelf.height, raster.height);
        atlasWidth = std::max(atlasWidth, shelf.x);
        atlasHeight = std::max(atlasHeight, shelf.y + shelf.height);
    }
    atlasHeight += kGlyphPadding;
    if (atlasWidth > kMaxAtlasWidth || atlasHeight > kMaxAtlasHeight)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        message = "the declared codepoints need a "
            + std::to_string(atlasWidth) + "x" + std::to_string(atlasHeight)
            + " atlas, which is larger than 4096x4096";
        return TextAtlasStatus::AssetInvalid;
    }
    // Rounded up to a multiple of four bytes. GL's default unpack alignment is
    // four, and a row that is not a multiple of it is read at a drifting
    // offset -- a font that shears further with every row. The upload sets the
    // alignment to one as well, but this is what makes the hazard impossible
    // rather than merely handled: the smoke test cannot tell a sheared atlas
    // from a correct one by sampling glyph centres, so the guard it cannot
    // check must not be the only thing standing there.
    atlasWidth = (atlasWidth + 3) & ~3;

    pixelSize_ = pixelSize;
    lineHeight_ = static_cast<std::int32_t>(face->size->metrics.height >> 6);
    ascender_ = static_cast<std::int32_t>(face->size->metrics.ascender >> 6);
    FT_Done_Face(face);
    FT_Done_FreeType(library);

    width_ = static_cast<std::uint16_t>(atlasWidth);
    height_ = static_cast<std::uint16_t>(atlasHeight);
    bitmap_.assign(
        static_cast<std::size_t>(width_) * height_,
        0
    );
    for (std::size_t index = 0; index < count; ++index)
    {
        const Raster& raster = rasters[index];
        const GlyphMetrics& metrics = glyphs_[index];
        for (std::int32_t row = 0; row < raster.height; ++row)
        {
            const std::size_t target =
                static_cast<std::size_t>(metrics.atlasY + row) * width_
                    + metrics.atlasX;
            std::copy(
                raster.coverage.begin()
                    + static_cast<std::ptrdiff_t>(row) * raster.width,
                raster.coverage.begin()
                    + static_cast<std::ptrdiff_t>(row + 1) * raster.width,
                bitmap_.begin() + static_cast<std::ptrdiff_t>(target)
            );
        }
    }

    loaded_ = true;
    return TextAtlasStatus::Ok;
}

const GlyphMetrics* TextAtlas::Glyph(char32_t codepoint) const
{
    if (!loaded_)
    {
        return nullptr;
    }
    for (const CodepointRange& range : ranges_)
    {
        if (codepoint >= range.first && codepoint <= range.last)
        {
            return &glyphs_[
                range.firstGlyph + (codepoint - range.first)
            ];
        }
    }
    return nullptr;
}

char32_t TextAtlas::DecodeUtf8(const std::string& text, std::size_t& cursor)
{
    constexpr char32_t kReplacement = 0xFFFD;
    if (cursor >= text.size())
    {
        return kReplacement;
    }
    const auto lead = static_cast<unsigned char>(text[cursor]);
    std::size_t extra = 0;
    char32_t value = 0;
    if (lead < 0x80)
    {
        ++cursor;
        return lead;
    }
    if ((lead & 0xE0u) == 0xC0u)
    {
        extra = 1;
        value = lead & 0x1Fu;
    }
    else if ((lead & 0xF0u) == 0xE0u)
    {
        extra = 2;
        value = lead & 0x0Fu;
    }
    else if ((lead & 0xF8u) == 0xF0u)
    {
        extra = 3;
        value = lead & 0x07u;
    }
    else
    {
        // A continuation byte or an illegal lead. Advancing by one is what
        // keeps a malformed string from becoming a loop that never ends.
        ++cursor;
        return kReplacement;
    }
    if (cursor + extra >= text.size())
    {
        ++cursor;
        return kReplacement;
    }
    for (std::size_t step = 1; step <= extra; ++step)
    {
        const auto byte = static_cast<unsigned char>(text[cursor + step]);
        if ((byte & 0xC0u) != 0x80u)
        {
            ++cursor;
            return kReplacement;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    cursor += extra + 1;
    return value;
}

std::int32_t TextAtlas::Measure(const std::string& text) const
{
    std::int32_t width = 0;
    std::size_t cursor = 0;
    while (cursor < text.size())
    {
        const char32_t codepoint = DecodeUtf8(text, cursor);
        if (const GlyphMetrics* metrics = Glyph(codepoint))
        {
            width += metrics->advance;
        }
    }
    return width;
}

std::vector<TextQuad> TextAtlas::LayoutText(
    const std::string& text,
    const ControlRect& rect
) const
{
    std::vector<TextQuad> quads;
    if (!loaded_ || rect.width <= 0 || rect.height <= 0)
    {
        return quads;
    }
    quads.reserve(text.size());
    std::int32_t pen = rect.x;
    // The baseline, from centring the line box in the rect. Integer division
    // truncates the same way everywhere, which is the only property this needs.
    const std::int32_t baseline =
        rect.y + (rect.height - lineHeight_) / 2 + ascender_;
    std::size_t cursor = 0;
    while (cursor < text.size())
    {
        const GlyphMetrics* metrics = Glyph(DecodeUtf8(text, cursor));
        if (metrics == nullptr)
        {
            continue;
        }
        if (metrics->width != 0 && metrics->height != 0)
        {
            const std::int32_t left = pen + metrics->bearingX;
            const std::int32_t top = baseline - metrics->bearingY;
            // Whole glyphs only. A glyph clipped down the middle would need
            // the backend to agree about which pixel column the cut falls in,
            // and that agreement is exactly what is not testable here.
            if (left + metrics->width > rect.x + rect.width)
            {
                break;
            }
            quads.push_back({
                left,
                top,
                metrics->width,
                metrics->height,
                metrics->atlasX,
                metrics->atlasY
            });
        }
        pen += metrics->advance;
    }
    return quads;
}

}
