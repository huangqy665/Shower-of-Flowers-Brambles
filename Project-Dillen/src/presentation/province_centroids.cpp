#include "province_centroids.hpp"

#include <cmath>

namespace dillen::presentation {

namespace {

constexpr double kPi = 3.14159265358979323846;

}

bool ProvinceCentroids::Build(const MapIndexRaster& raster)
{
    built_ = false;
    count_ = 0;
    centroids_.clear();
    // The raster has to be internally consistent before it is walked. A
    // declared size that does not match the decoded pixels would index past
    // the end of the vector, and "the loader would have caught it" is not the
    // same as catching it.
    if (!raster
        || raster.provinceCount == 0
        || raster.width == 0
        || raster.height == 0
        || raster.indices.size()
            != static_cast<std::size_t>(raster.width) * raster.height)
    {
        return false;
    }

    count_ = raster.provinceCount;
    centroids_.assign(static_cast<std::size_t>(count_) + 1, Centroid{});

    // Accumulated as a direction for longitude and a plain sum for latitude.
    // One pass, and the sums are doubles because 12 million pixels of a single
    // province would lose the low bits of a float.
    std::vector<double> cosine(centroids_.size(), 0.0);
    std::vector<double> sine(centroids_.size(), 0.0);
    std::vector<double> latitude(centroids_.size(), 0.0);

    // One sine and one cosine per COLUMN, not per pixel.
    //
    // Longitude depends only on x, so the reference corpus was calling
    // std::sin and std::cos 12.1 million times to compute 5616 distinct
    // answers. The table is 90 KB and turns the pass into two multiply-adds
    // per pixel.
    const double width = static_cast<double>(raster.width);
    const double height = static_cast<double>(raster.height);
    std::vector<double> columnCos(raster.width, 0.0);
    std::vector<double> columnSin(raster.width, 0.0);
    for (std::uint32_t x = 0; x < raster.width; ++x)
    {
        const double angle =
            2.0 * kPi * ((static_cast<double>(x) + 0.5) / width);
        columnCos[x] = std::cos(angle);
        columnSin[x] = std::sin(angle);
    }
    for (std::uint32_t y = 0; y < raster.height; ++y)
    {
        const std::uint16_t* row =
            raster.indices.data() + static_cast<std::size_t>(y) * raster.width;
        // The pixel centre, so a one-pixel province lands in the middle of its
        // pixel rather than on its corner.
        const double v = (static_cast<double>(y) + 0.5) / height;
        for (std::uint32_t x = 0; x < raster.width; ++x)
        {
            const std::uint16_t index = row[x];
            if (index == 0 || index > count_)
            {
                continue;
            }
            cosine[index] += columnCos[x];
            sine[index] += columnSin[x];
            latitude[index] += v;
            ++centroids_[index].pixels;
        }
    }

    for (std::size_t index = 1; index < centroids_.size(); ++index)
    {
        Centroid& centroid = centroids_[index];
        if (centroid.pixels == 0)
        {
            continue;
        }
        const double count = static_cast<double>(centroid.pixels);
        centroid.v = latitude[index] / count;
        // A province spread evenly around the whole map has a zero direction
        // sum and no meaningful longitude. It cannot happen with real content
        // and is handled rather than assumed away.
        if (std::abs(cosine[index]) < 1e-12 && std::abs(sine[index]) < 1e-12)
        {
            centroid.u = 0.5;
            continue;
        }
        double turns = std::atan2(sine[index], cosine[index]) / (2.0 * kPi);
        turns -= std::floor(turns);
        centroid.u = turns;
    }
    built_ = true;
    return true;
}

bool ProvinceCentroids::Find(
    std::uint32_t provinceIndex,
    double& u,
    double& v
) const
{
    if (!built_
        || provinceIndex == 0
        || provinceIndex >= centroids_.size()
        || centroids_[provinceIndex].pixels == 0)
    {
        return false;
    }
    u = centroids_[provinceIndex].u;
    v = centroids_[provinceIndex].v;
    return true;
}

std::uint64_t ProvinceCentroids::Pixels(std::uint32_t provinceIndex) const
{
    if (!built_ || provinceIndex >= centroids_.size())
    {
        return 0;
    }
    return centroids_[provinceIndex].pixels;
}

}
