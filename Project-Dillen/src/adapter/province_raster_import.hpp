#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dillen::adapter {

// Imports a province map held as a colour raster plus a colour/id table.
//
// This is the shape every grand-strategy province map ships in: one bitmap
// where each province is a flat colour region, and one table mapping those
// colours to identifiers. Nothing here understands HOI3, or any other game --
// there is no scenario, no country, no terrain semantics. It reads pixels and
// a CSV and produces indices and an adjacency graph, which is why it lives in
// the generic corpus-importer layer rather than the HOI3 compatibility tree.
//
// The adjacency graph is DERIVED, not read. A corpus adjacency file carries
// only the exceptions -- straits, canals, tunnels -- because the ordinary case
// is implied by two provinces sharing a border in the raster. Deriving it is
// the only way to get a complete neighbour graph, and it is a scan of the
// bitmap: for every pixel, compare against the neighbour to its right and the
// one below.

enum class ProvinceRasterImportStatus
{
    Ok,
    RasterMissing,
    RasterUnsupported,
    DefinitionsMissing,
    DefinitionsInvalid,
    ColourNotDefined,
    TooManyProvinces
};

struct ProvinceRasterImportOptions
{
    std::filesystem::path raster;
    std::filesystem::path definitions;
    // The corpus's terrain raster, if it has one.
    //
    // Optional, and the map is complete without it: leaving it out produces
    // the geography that has always been produced. Given, every province is
    // classified land or sea, which is the difference between a world that
    // has 3547 "unowned" regions and one that knows which of those are ocean.
    //
    // It is a path rather than a flag because whether a corpus HAS a terrain
    // raster, and which index in it means water, are facts about that corpus.
    std::filesystem::path terrain;
    // The palette index terrain uses for water. HOI3's is 254; a different
    // corpus is free to differ, and nothing here assumes otherwise.
    std::uint8_t terrainSeaIndex = 254;
    // A world map's left and right edges meet. Off by default only because a
    // regional map's do not.
    bool wrapHorizontally = true;
    // Whether the image is stored with geographic NORTH at its bottom.
    //
    // This is not the BMP row order. BMP row order is a storage detail and is
    // decoded from the header; this is a statement about the image, and the
    // two are independent -- conflating them would silently mirror any corpus
    // whose convention differs.
    //
    // HOI3's own province bitmaps display with north at the bottom, so a
    // corpus taken straight from that game needs this on. The map shipped in
    // Dillen-Game has been turned the right way up already, so it does not.
    //
    // Which way round a given corpus is cannot be inferred -- both
    // orientations decode to a valid raster -- so it is stated rather than
    // guessed, and province_raster_import_probe checks the result against
    // named landmarks in either direction.
    //
    // It belongs here rather than in the renderer for the reason the whole
    // separation exists: the LOGICAL geographic space has to be right, and
    // the renderer is only allowed to bend it.
    bool northAtImageBottom = false;
};

// A raster colour that is not a province, with the pixels it covers.
//
// There are two different situations here and collapsing them loses the
// distinction that matters:
//
//   * UNASSIGNED -- the definition table lists the colour but leaves its id
//     empty. That is the corpus stating outright that the colour is not a
//     province. The reference corpus does this for 57 colours.
//   * UNKNOWN -- the colour is absent from the table entirely. Nothing claimed
//     it and nothing disclaimed it, so it is either an artefact (the reference
//     corpus has a handful of single-pixel ones, plus white) or a sign that
//     the raster and the table have drifted apart.
//
// Both become index 0 in the raster, because neither is a province. They are
// reported separately so a caller can hold the second to a much tighter bound
// than the first -- "the import succeeded" is not a useful check on its own.
struct NonProvinceColour
{
    std::uint32_t packed = 0;
    std::uint64_t pixels = 0;
};

struct ProvinceAdjacency
{
    // Dense indices, always first < second, and the vector is sorted and
    // deduplicated. Both properties are load-bearing: content generated from
    // this must be byte-identical across runs and platforms.
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

struct ProvinceRasterImport
{
    ProvinceRasterImportStatus status = ProvinceRasterImportStatus::Ok;
    std::string message;

    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // Dense index 0 means "no province" -- it is the value the raster carries
    // where a colour has no id, and it is the value a renderer's id texture
    // reads outside the map. Real provinces occupy 1..Count().
    //
    // The dense index is what everything downstream uses: the id texture
    // stores it, the palette is indexed by it, and the Runtime Catalog maps
    // it to an EntityId. sourceIdByIndex is the one place the corpus
    // numbering survives.
    //
    // The reference corpus happens to be contiguous 1..14187, so index and
    // id coincide there. Nothing may rely on that: a corpus with retired
    // province ids is entirely ordinary, and the point of the dense index is
    // that the raster stays 16-bit and the palette gap-free regardless.
    std::vector<std::uint32_t> sourceIdByIndex;
    std::vector<std::uint16_t> indexRaster;

    // 1 where the province is water, 0 where it is land, indexed by the same
    // dense index as sourceIdByIndex. Empty when no terrain raster was given.
    //
    // Decided by majority of the province's own pixels. A coastal province is
    // whichever it mostly is, which is the only answer a single flag can give
    // and the one the corpus itself behaves as if it were giving: HOI3's own
    // sea zones are entirely water and its land provinces almost entirely
    // land, so the majority is not close except in a handful of cases.
    std::vector<std::uint8_t> seaByIndex;
    std::uint32_t seaProvinces = 0;
    std::uint32_t landProvinces = 0;
    std::vector<ProvinceAdjacency> adjacency;

    // Both sorted by packed colour, so they are reproducible like everything
    // else.
    std::vector<NonProvinceColour> unassigned;
    std::vector<NonProvinceColour> unknown;

    std::uint64_t borderPixels = 0;

    std::uint32_t Count() const noexcept
    {
        return sourceIdByIndex.empty()
            ? 0
            : static_cast<std::uint32_t>(sourceIdByIndex.size() - 1);
    }

    explicit operator bool() const noexcept
    {
        return status == ProvinceRasterImportStatus::Ok;
    }
};

ProvinceRasterImport ImportProvinceRaster(
    const ProvinceRasterImportOptions& options
);

}
