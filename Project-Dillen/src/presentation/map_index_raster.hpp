#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "presentation_asset.hpp"

namespace dillen::presentation {

// Loads the map index raster a Presentation Package declares.
//
// The raster is what makes a map renderer cheap: one texel per screen pixel
// holds the dense index of the province under it, and the shader turns that
// into a colour with a single palette fetch. Cost per frame is O(pixels), not
// O(provinces), which is the only way 14187 provinces are drawable at all.
//
// It arrives as a Presentation Asset: a text declaration the pipeline parses
// like any other source, plus a binary payload the file catalog deliberately
// never classifies. That split is not a convenience. A 24 MB binary has no
// text form, and putting it through the parser would mean parsing 24 MB of
// noise; leaving it unclassified means it never enters a Package content
// digest either, so `asset_digest` in the declaration is the only thing
// standing between the declaration and whatever bytes happen to sit next to
// it. This loader verifies it, every time, before decoding.
//
// Presentation is outside the determinism closure, so none of this can affect
// a save. That is exactly why the integrity check has to live here: nothing
// upstream is going to do it.

enum class MapIndexRasterStatus
{
    Ok,
    AssetKindMismatch,
    PropertyMissing,
    PropertyInvalid,
    PayloadMissing,
    PayloadDigestMismatch,
    PayloadMalformed,
    DimensionMismatch
};

struct MapIndexRaster
{
    MapIndexRasterStatus status = MapIndexRasterStatus::Ok;
    std::string message;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t provinceCount = 0;
    // width * height dense indices, top-down. 0 means "no province", which is
    // also row 0 of the projection table, so a renderer can index straight
    // across without a branch.
    std::vector<std::uint16_t> indices;

    explicit operator bool() const noexcept
    {
        return status == MapIndexRasterStatus::Ok;
    }
};

// `kind` must be "map_index_raster". The payload path resolves against the
// declaring source's directory, which the pipeline recorded.
MapIndexRaster LoadMapIndexRaster(const kernel::PresentationAsset& asset);

}
