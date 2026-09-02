#include "map_index_raster.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "package_content_digest.hpp"

namespace dillen::presentation {

namespace {

MapIndexRaster Fail(MapIndexRasterStatus status, std::string message)
{
    MapIndexRaster raster;
    raster.status = status;
    raster.message = std::move(message);
    return raster;
}

bool ReadUnsigned(
    const kernel::PresentationAsset& asset,
    const std::string& key,
    std::uint32_t& output,
    std::string& message
)
{
    const auto entry = asset.properties.find(key);
    if (entry == asset.properties.end())
    {
        message = "property '" + key + "' is missing";
        return false;
    }
    if (entry->second.empty())
    {
        message = "property '" + key + "' is empty";
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : entry->second)
    {
        if (character < '0' || character > '9')
        {
            message = "property '" + key + "' is not a number";
            return false;
        }
        value = value * 10
            + static_cast<std::uint64_t>(character - '0');
        if (value > 0xFFFFFFFFull)
        {
            message = "property '" + key + "' is out of range";
            return false;
        }
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

}

MapIndexRaster LoadMapIndexRaster(const kernel::PresentationAsset& asset)
{
    if (asset.kind != "map_index_raster")
    {
        return Fail(
            MapIndexRasterStatus::AssetKindMismatch,
            "asset '" + asset.canonicalName + "' is a " + asset.kind
        );
    }

    const auto format = asset.properties.find("format");
    if (format == asset.properties.end())
    {
        return Fail(
            MapIndexRasterStatus::PropertyMissing,
            "property 'format' is missing"
        );
    }
    if (format->second != "index16_rle")
    {
        return Fail(
            MapIndexRasterStatus::PropertyInvalid,
            "unknown raster format '" + format->second + "'"
        );
    }

    MapIndexRaster raster;
    std::string message;
    if (!ReadUnsigned(asset, "width", raster.width, message)
        || !ReadUnsigned(asset, "height", raster.height, message)
        || !ReadUnsigned(asset, "province_count", raster.provinceCount,
            message))
    {
        return Fail(MapIndexRasterStatus::PropertyInvalid, message);
    }
    if (raster.width == 0 || raster.height == 0)
    {
        return Fail(
            MapIndexRasterStatus::PropertyInvalid,
            "the raster has no area"
        );
    }

    const std::filesystem::path payload =
        std::filesystem::path(asset.source.physicalDirectory)
            / asset.assetPath;
    std::string bytes;
    {
        std::ifstream stream(payload, std::ios::binary);
        if (!stream)
        {
            return Fail(
                MapIndexRasterStatus::PayloadMissing,
                "payload " + payload.string() + " could not be opened"
            );
        }
        bytes.assign(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        );
    }

    // Before decoding, not after. A payload that does not match its
    // declaration is not a raster with a problem, it is a different file, and
    // decoding it first would mean deciding what to do with whatever came out.
    const std::string digest = kernel::ComputeContentDigest(bytes);
    if (digest != asset.assetDigest)
    {
        return Fail(
            MapIndexRasterStatus::PayloadDigestMismatch,
            "payload digest " + digest + " does not match the declared "
                + asset.assetDigest
        );
    }

    if ((bytes.size() % 4) != 0)
    {
        return Fail(
            MapIndexRasterStatus::PayloadMalformed,
            "the payload is not a whole number of (index, count) pairs"
        );
    }

    const std::size_t expected =
        static_cast<std::size_t>(raster.width) * raster.height;
    raster.indices.reserve(expected);
    for (std::size_t cursor = 0; cursor < bytes.size(); cursor += 4)
    {
        const auto byte = [&bytes](std::size_t at)
        {
            return static_cast<std::uint16_t>(
                static_cast<unsigned char>(bytes[at])
            );
        };
        const std::uint16_t value = static_cast<std::uint16_t>(
            byte(cursor) | (byte(cursor + 1) << 8)
        );
        const std::uint16_t count = static_cast<std::uint16_t>(
            byte(cursor + 2) | (byte(cursor + 3) << 8)
        );
        if (count == 0)
        {
            return Fail(
                MapIndexRasterStatus::PayloadMalformed,
                "the payload contains a zero-length run"
            );
        }
        // Checked as it grows rather than at the end. A corrupt payload can
        // describe a raster far larger than the declared one, and reserving
        // for it first is how a decoder turns bad data into an allocation
        // failure instead of an error message.
        if (raster.indices.size() + count > expected)
        {
            return Fail(
                MapIndexRasterStatus::DimensionMismatch,
                "the payload describes more pixels than "
                    + std::to_string(raster.width) + "x"
                    + std::to_string(raster.height)
            );
        }
        if (value > raster.provinceCount)
        {
            return Fail(
                MapIndexRasterStatus::PayloadMalformed,
                "the payload names province index "
                    + std::to_string(value) + ", above the declared count"
            );
        }
        raster.indices.insert(raster.indices.end(), count, value);
    }
    if (raster.indices.size() != expected)
    {
        return Fail(
            MapIndexRasterStatus::DimensionMismatch,
            "the payload describes " + std::to_string(raster.indices.size())
                + " pixels, expected " + std::to_string(expected)
        );
    }
    return raster;
}

}
