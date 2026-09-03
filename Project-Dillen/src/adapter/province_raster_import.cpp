#include "province_raster_import.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace dillen::adapter {

namespace {

ProvinceRasterImport Fail(
    ProvinceRasterImportStatus status,
    std::string message
)
{
    ProvinceRasterImport result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::uint32_t ReadLittleEndian32(const unsigned char* bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint16_t ReadLittleEndian16(const unsigned char* bytes) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0])
        | (static_cast<std::uint16_t>(bytes[1]) << 8)
    );
}

constexpr std::uint32_t PackColour(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue
) noexcept
{
    return (red << 16) | (green << 8) | blue;
}

// Parses `id;red;green;blue;name;...`, tolerating the trailing rows a corpus
// carries for colours that were never assigned a province.
bool ParseDefinitions(
    const std::filesystem::path& path,
    std::unordered_map<std::uint32_t, std::uint32_t>& colourToSourceId,
    std::unordered_set<std::uint32_t>& unassignedColours,
    std::vector<std::uint32_t>& sourceIds,
    std::string& message
)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        message = "definition table could not be opened";
        return false;
    }
    std::string line;
    bool first = true;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        // Some rows are quoted whole, because the name field contains a
        // character the writer wanted to escape:
        //
        //     "11576;0;15;236;pacific ocean;""x"""
        //
        // Skipping them looks harmless -- twenty-four rows out of
        // fourteen thousand -- and is not. Those rows are the ocean, the
        // raster paints them, and dropping them turns real provinces into
        // 'a colour nothing declared' and their neighbours into isolated
        // islands. The corpus is contiguous 1..14187; it only looks like
        // it has holes if this is missed.
        if (line.size() >= 2 && line.front() == '"'
            && line.back() == '"')
        {
            line = line.substr(1, line.size() - 2);
            std::string collapsed;
            collapsed.reserve(line.size());
            for (std::size_t index = 0; index < line.size(); ++index)
            {
                collapsed.push_back(line[index]);
                if (line[index] == '"' && index + 1 < line.size()
                    && line[index + 1] == '"')
                {
                    ++index;
                }
            }
            line = std::move(collapsed);
        }
        if (first)
        {
            // The header names the columns; its first field is not a number.
            first = false;
            if (!line.empty() && !std::isdigit(static_cast<unsigned char>(
                line.front())))
            {
                continue;
            }
        }
        if (line.empty())
        {
            continue;
        }
        std::uint32_t field[4] = {0, 0, 0, 0};
        std::size_t cursor = 0;
        bool numeric = true;
        for (std::size_t index = 0; index < 4 && numeric; ++index)
        {
            const std::size_t separator = line.find(';', cursor);
            const std::string text = line.substr(
                cursor,
                separator == std::string::npos
                    ? std::string::npos
                    : separator - cursor
            );
            if (text.empty())
            {
                // A row with no id is the corpus declaring that this colour is
                // deliberately not a province. The remaining three fields are
                // still a colour, so they are read: the raster does use these,
                // and knowing they were disclaimed on purpose is the whole
                // difference between a clean import and a broken one.
                if (index == 0)
                {
                    std::uint32_t channel[3] = {0, 0, 0};
                    std::size_t scan = separator + 1;
                    bool parsed = separator != std::string::npos;
                    for (std::size_t part = 0; part < 3 && parsed; ++part)
                    {
                        const std::size_t next = line.find(';', scan);
                        const std::string value = line.substr(
                            scan,
                            next == std::string::npos
                                ? std::string::npos
                                : next - scan
                        );
                        if (value.empty())
                        {
                            parsed = false;
                            break;
                        }
                        std::uint32_t number = 0;
                        for (const char character : value)
                        {
                            if (!std::isdigit(
                                static_cast<unsigned char>(character)))
                            {
                                parsed = false;
                                break;
                            }
                            number = number * 10
                                + static_cast<std::uint32_t>(character - '0');
                        }
                        if (!parsed || number > 255)
                        {
                            parsed = false;
                            break;
                        }
                        channel[part] = number;
                        if (next == std::string::npos)
                        {
                            parsed = part == 2;
                            break;
                        }
                        scan = next + 1;
                    }
                    if (parsed)
                    {
                        unassignedColours.insert(
                            PackColour(channel[0], channel[1], channel[2])
                        );
                    }
                }
                numeric = false;
                break;
            }
            std::uint32_t value = 0;
            for (const char character : text)
            {
                if (!std::isdigit(static_cast<unsigned char>(character)))
                {
                    numeric = false;
                    break;
                }
                value = value * 10 + static_cast<std::uint32_t>(
                    character - '0'
                );
            }
            if (!numeric)
            {
                break;
            }
            field[index] = value;
            if (separator == std::string::npos)
            {
                numeric = index == 3;
                break;
            }
            cursor = separator + 1;
        }
        if (!numeric)
        {
            continue;
        }
        if (field[1] > 255 || field[2] > 255 || field[3] > 255)
        {
            message = "definition table row "
                + std::to_string(lineNumber)
                + " has a colour channel outside 0..255";
            return false;
        }
        const std::uint32_t colour = PackColour(field[1], field[2], field[3]);
        // Two ids claiming one colour makes the raster ambiguous: the pixels
        // cannot say which province they belong to. Refuse rather than pick.
        if (!colourToSourceId.emplace(colour, field[0]).second)
        {
            message = "definition table maps one colour to two province ids "
                      "(row " + std::to_string(lineNumber) + ")";
            return false;
        }
        sourceIds.push_back(field[0]);
    }
    if (sourceIds.empty())
    {
        message = "definition table declares no provinces";
        return false;
    }
    return true;
}

}

// Read an 8-bit indexed BMP into a top-down buffer of palette indices.
//
// Separate from the province reader rather than folded into it: that one is
// 24-bit by contract, because a province raster's colours ARE its identity
// and a palette between them and the id would be one more place for two
// provinces to collide. A terrain raster has no such requirement.
bool ReadIndexedBitmap(
    const std::filesystem::path& path,
    bool northAtImageBottom,
    std::uint32_t expectedWidth,
    std::uint32_t expectedHeight,
    std::vector<std::uint8_t>& output,
    std::string& message
)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        message = "terrain raster could not be opened";
        return false;
    }
    unsigned char header[54] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header))
        || header[0] != 'B' || header[1] != 'M')
    {
        message = "terrain raster is not a BMP";
        return false;
    }
    const std::uint32_t dataOffset = ReadLittleEndian32(header + 10);
    const std::uint32_t width = ReadLittleEndian32(header + 18);
    const std::int32_t signedHeight =
        static_cast<std::int32_t>(ReadLittleEndian32(header + 22));
    const std::uint16_t bitsPerPixel = ReadLittleEndian16(header + 28);
    const std::uint32_t compression = ReadLittleEndian32(header + 30);
    if (bitsPerPixel != 8 || compression != 0)
    {
        message = "terrain raster must be an uncompressed 8-bit BMP";
        return false;
    }
    const bool bottomUp = signedHeight > 0;
    const std::uint32_t height = static_cast<std::uint32_t>(
        bottomUp ? signedHeight : -signedHeight
    );
    // The province raster decides the geometry. A terrain raster of a
    // different size is not a terrain raster for THIS map, and silently
    // sampling it anyway would misclassify a band of provinces whose width
    // depends on how far the two disagree.
    if (width != expectedWidth || height != expectedHeight)
    {
        message = "terrain raster is " + std::to_string(width) + "x"
            + std::to_string(height) + ", the province raster is "
            + std::to_string(expectedWidth) + "x"
            + std::to_string(expectedHeight);
        return false;
    }
    const std::size_t rowStride =
        ((static_cast<std::size_t>(width) + 3u) / 4u) * 4u;
    output.assign(static_cast<std::size_t>(width) * height, 0u);
    file.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
    std::vector<unsigned char> row(rowStride);
    for (std::uint32_t scan = 0; scan < height; ++scan)
    {
        file.read(reinterpret_cast<char*>(row.data()),
            static_cast<std::streamsize>(rowStride));
        if (file.gcount() != static_cast<std::streamsize>(rowStride))
        {
            message = "terrain raster ends before its declared height";
            return false;
        }
        std::uint32_t y = bottomUp ? (height - 1 - scan) : scan;
        if (northAtImageBottom)
        {
            y = height - 1 - y;
        }
        std::copy(
            row.begin(),
            row.begin() + static_cast<std::ptrdiff_t>(width),
            output.begin() + static_cast<std::ptrdiff_t>(y) * width
        );
    }
    return true;
}

ProvinceRasterImport ImportProvinceRaster(
    const ProvinceRasterImportOptions& options
)
{
    std::unordered_map<std::uint32_t, std::uint32_t> colourToSourceId;
    std::unordered_set<std::uint32_t> unassignedColours;
    std::vector<std::uint32_t> sourceIds;
    std::string message;
    if (!std::filesystem::exists(options.definitions))
    {
        return Fail(
            ProvinceRasterImportStatus::DefinitionsMissing,
            "definition table is missing"
        );
    }
    if (!ParseDefinitions(
            options.definitions,
            colourToSourceId,
            unassignedColours,
            sourceIds,
            message))
    {
        return Fail(
            ProvinceRasterImportStatus::DefinitionsInvalid,
            std::move(message)
        );
    }

    // Dense indices are assigned in ascending corpus id, not in file order, so
    // the same corpus always produces the same indices no matter how the table
    // happens to be sorted. Everything downstream -- the id raster, generated
    // content, the palette -- is keyed on this index, so its stability is the
    // stability of the whole map pipeline.
    std::sort(sourceIds.begin(), sourceIds.end());
    sourceIds.erase(
        std::unique(sourceIds.begin(), sourceIds.end()),
        sourceIds.end()
    );
    if (sourceIds.size() >= 0xFFFFu)
    {
        return Fail(
            ProvinceRasterImportStatus::TooManyProvinces,
            "more provinces than a 16-bit index raster can carry"
        );
    }

    std::unordered_map<std::uint32_t, std::uint16_t> colourToIndex;
    colourToIndex.reserve(colourToSourceId.size() * 2);
    {
        std::unordered_map<std::uint32_t, std::uint16_t> indexBySourceId;
        indexBySourceId.reserve(sourceIds.size() * 2);
        for (std::size_t index = 0; index < sourceIds.size(); ++index)
        {
            indexBySourceId.emplace(
                sourceIds[index],
                static_cast<std::uint16_t>(index + 1)
            );
        }
        for (const auto& entry : colourToSourceId)
        {
            const auto dense = indexBySourceId.find(entry.second);
            if (dense != indexBySourceId.end())
            {
                colourToIndex.emplace(entry.first, dense->second);
            }
        }
    }

    std::ifstream raster(options.raster, std::ios::binary);
    if (!raster)
    {
        return Fail(
            ProvinceRasterImportStatus::RasterMissing,
            "province raster could not be opened"
        );
    }
    unsigned char header[54] = {};
    raster.read(reinterpret_cast<char*>(header), sizeof(header));
    if (raster.gcount() != static_cast<std::streamsize>(sizeof(header))
        || header[0] != 'B'
        || header[1] != 'M')
    {
        return Fail(
            ProvinceRasterImportStatus::RasterUnsupported,
            "province raster is not a BMP"
        );
    }
    const std::uint32_t dataOffset = ReadLittleEndian32(header + 10);
    const std::uint32_t width = ReadLittleEndian32(header + 18);
    const std::int32_t signedHeight =
        static_cast<std::int32_t>(ReadLittleEndian32(header + 22));
    const std::uint16_t bitsPerPixel = ReadLittleEndian16(header + 28);
    const std::uint32_t compression = ReadLittleEndian32(header + 30);
    if (bitsPerPixel != 24 || compression != 0 || width == 0
        || signedHeight == 0)
    {
        return Fail(
            ProvinceRasterImportStatus::RasterUnsupported,
            "province raster must be an uncompressed 24-bit BMP"
        );
    }
    // A positive height means the rows are stored bottom-up, which is the
    // usual case. Either way the import produces a top-down index raster, so
    // downstream code never has to know.
    const bool bottomUp = signedHeight > 0;
    const std::uint32_t height = static_cast<std::uint32_t>(
        bottomUp ? signedHeight : -signedHeight
    );
    const std::size_t rowStride = ((static_cast<std::size_t>(width) * 3u + 3u)
        / 4u) * 4u;

    ProvinceRasterImport result;
    result.width = width;
    result.height = height;
    result.sourceIdByIndex.resize(sourceIds.size() + 1, 0);
    for (std::size_t index = 0; index < sourceIds.size(); ++index)
    {
        result.sourceIdByIndex[index + 1] = sourceIds[index];
    }
    result.indexRaster.assign(
        static_cast<std::size_t>(width) * height,
        std::uint16_t{0}
    );

    raster.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
    std::vector<unsigned char> row(rowStride);
    std::unordered_map<std::uint32_t, std::uint64_t> unassignedPixels;
    std::unordered_map<std::uint32_t, std::uint64_t> unknownPixels;
    for (std::uint32_t scan = 0; scan < height; ++scan)
    {
        raster.read(reinterpret_cast<char*>(row.data()),
            static_cast<std::streamsize>(rowStride));
        if (raster.gcount() != static_cast<std::streamsize>(rowStride))
        {
            return Fail(
                ProvinceRasterImportStatus::RasterUnsupported,
                "province raster ends before its declared height"
            );
        }
        // Two independent flips. `bottomUp` undoes BMP's storage order;
        // `northAtImageBottom` undoes the corpus's own orientation. Folding
        // them into one flag would make a corpus that differs in either one
        // impossible to express.
        std::uint32_t y = bottomUp ? (height - 1 - scan) : scan;
        if (options.northAtImageBottom)
        {
            y = height - 1 - y;
        }
        std::uint16_t* target =
            result.indexRaster.data() + static_cast<std::size_t>(y) * width;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const unsigned char* pixel = row.data()
                + static_cast<std::size_t>(x) * 3u;
            // BMP stores BGR.
            const std::uint32_t colour = PackColour(pixel[2], pixel[1],
                pixel[0]);
            const auto dense = colourToIndex.find(colour);
            if (dense == colourToIndex.end())
            {
                // Not a province either way, but which kind of not-a-province
                // is the interesting part.
                if (unassignedColours.count(colour) != 0)
                {
                    ++unassignedPixels[colour];
                }
                else
                {
                    ++unknownPixels[colour];
                }
                target[x] = 0;
                continue;
            }
            target[x] = dense->second;
        }
    }

    const auto collect = [](
        const std::unordered_map<std::uint32_t, std::uint64_t>& counted,
        std::vector<NonProvinceColour>& output)
    {
        output.reserve(counted.size());
        for (const auto& entry : counted)
        {
            output.push_back({entry.first, entry.second});
        }
        std::sort(
            output.begin(),
            output.end(),
            [](const NonProvinceColour& first, const NonProvinceColour& second)
            {
                return first.packed < second.packed;
            }
        );
    };
    collect(unassignedPixels, result.unassigned);
    collect(unknownPixels, result.unknown);

    // Adjacency by border scan.
    //
    // Every pixel is compared with the neighbour to its right and the one
    // below; a differing pair is a shared border. Pairs are packed into one
    // 32-bit key so the deduplication is a sort over a flat vector instead of
    // a set insert per border pixel -- at twelve million pixels the difference
    // is minutes.
    std::vector<std::uint32_t> pairs;
    pairs.reserve(1u << 20);
    const auto record = [&pairs](std::uint16_t left, std::uint16_t right)
    {
        if (left == right || left == 0 || right == 0)
        {
            return false;
        }
        const std::uint32_t low = left < right ? left : right;
        const std::uint32_t high = left < right ? right : left;
        pairs.push_back((low << 16) | high);
        return true;
    };
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint16_t* scan =
            result.indexRaster.data() + static_cast<std::size_t>(y) * width;
        const std::uint16_t* below = y + 1 < height
            ? scan + width
            : nullptr;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            if (x + 1 < width && record(scan[x], scan[x + 1]))
            {
                ++result.borderPixels;
            }
            if (below != nullptr && record(scan[x], below[x]))
            {
                ++result.borderPixels;
            }
        }
        // A world map's east and west edges are the same meridian. Without
        // this the provinces either side of the date line are unreachable from
        // one another, which is a hole in the simulation graph, not just in
        // the picture.
        if (options.wrapHorizontally && width > 1
            && record(scan[width - 1], scan[0]))
        {
            ++result.borderPixels;
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    result.adjacency.reserve(pairs.size());
    for (const std::uint32_t key : pairs)
    {
        result.adjacency.push_back({key >> 16, key & 0xFFFFu});
    }

    // --- land or sea, if the corpus said ----------------------------------
    if (!options.terrain.empty())
    {
        std::vector<std::uint8_t> terrain;
        std::string terrainMessage;
        if (!ReadIndexedBitmap(
                options.terrain,
                options.northAtImageBottom,
                result.width,
                result.height,
                terrain,
                terrainMessage))
        {
            return Fail(
                ProvinceRasterImportStatus::RasterUnsupported,
                terrainMessage
            );
        }
        std::vector<std::uint64_t> sea(result.sourceIdByIndex.size(), 0);
        std::vector<std::uint64_t> land(result.sourceIdByIndex.size(), 0);
        for (std::size_t at = 0; at < result.indexRaster.size(); ++at)
        {
            const std::uint16_t index = result.indexRaster[at];
            if (index == 0 || index >= sea.size())
            {
                continue;
            }
            if (terrain[at] == options.terrainSeaIndex)
            {
                ++sea[index];
            }
            else
            {
                ++land[index];
            }
        }
        result.seaByIndex.assign(result.sourceIdByIndex.size(), 0u);
        for (std::size_t index = 1; index < sea.size(); ++index)
        {
            const bool isSea = sea[index] > land[index];
            result.seaByIndex[index] = isSea ? 1u : 0u;
            if (isSea)
            {
                ++result.seaProvinces;
            }
            else
            {
                ++result.landProvinces;
            }
        }
    }
    return result;
}

}
