#pragma once

#include <cstdint>
#include <vector>

#include "map_index_raster.hpp"

namespace dillen::presentation {

// Where each province sits on the map, as one point.
//
// The cursor names a PROVINCE, and a strategy map that zooms should settle on
// that province rather than on whichever pixel of it happened to be under the
// pointer. The difference shows on a large region: zooming into a corner of
// one and zooming into its middle are not the same gesture, and only the
// second one settles.
//
// Computed from the index raster rather than shipped, because it is a fact
// ABOUT the raster: any content that changes it changes the raster too, and a
// table that could disagree with the picture would be a second copy of the
// same statement.
//
// LONGITUDE IS CIRCULAR
//
// A province the map's cut edge passes through has pixels at both u = 0 and
// u = 1, and their arithmetic mean is the middle of the map -- somewhere in
// the Pacific, for a province in the Bering Strait. So longitude is averaged
// as a direction: each pixel contributes a unit vector at angle 2*pi*u, and
// the centroid is the angle of their sum. Latitude has no such wrap and is a
// plain mean.
class ProvinceCentroids
{
public:
    // Builds from a decoded raster. One pass over the pixels; at the reference
    // corpus that is 12 million of them, once, at load.
    bool Build(const MapIndexRaster& raster);

    bool IsBuilt() const noexcept { return built_; }
    std::uint32_t Count() const noexcept { return count_; }

    // False for index 0 and for any province with no pixels.
    bool Find(std::uint32_t provinceIndex, double& u, double& v) const;

    // How many pixels the province covers. A caller that wants to ignore
    // slivers has the number rather than a guess.
    std::uint64_t Pixels(std::uint32_t provinceIndex) const;

private:
    struct Centroid
    {
        double u = 0.0;
        double v = 0.0;
        std::uint64_t pixels = 0;
    };

    bool built_ = false;
    std::uint32_t count_ = 0;
    std::vector<Centroid> centroids_;
};

}
