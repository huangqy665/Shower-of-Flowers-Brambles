#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "province_centroids.hpp"

// Where a province sits, as one point.
//
// The cursor names a province, and zooming settles on the province rather than
// on whichever of its pixels the pointer happened to be over. That needs a
// position per province, and the one thing that can go quietly wrong in
// computing it is the map's wrap: a province the cut edge passes through has
// pixels at both ends of the raster, and their arithmetic mean is the middle
// of the map. This probe is mostly about that.

namespace
{
using namespace dillen;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "province centroids: " << what << '\n';
        ++failures;
    }
}

// A raster built by hand, so every expected answer is arithmetic rather than a
// number read off the real corpus and trusted.
presentation::MapIndexRaster Make(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t provinces
)
{
    presentation::MapIndexRaster raster;
    raster.status = presentation::MapIndexRasterStatus::Ok;
    raster.width = width;
    raster.height = height;
    raster.provinceCount = provinces;
    raster.indices.assign(
        static_cast<std::size_t>(width) * height,
        std::uint16_t{0}
    );
    return raster;
}

void Put(
    presentation::MapIndexRaster& raster,
    std::uint32_t x,
    std::uint32_t y,
    std::uint16_t index
)
{
    raster.indices[static_cast<std::size_t>(y) * raster.width + x] = index;
}

double Wrap(double value)
{
    return value - std::floor(value);
}

double Delta(double from, double to)
{
    const double delta = Wrap(to - from);
    return delta > 0.5 ? delta - 1.0 : delta;
}

}

int main()
{
    // Held outside the block so its buffers are not counted against the
    // timing, and so a debugger can look at it afterwards.
    presentation::ProvinceCentroids centroids_full;

    // --- a block in the middle -------------------------------------------
    {
        presentation::MapIndexRaster raster = Make(100, 100, 2);
        // Province 1: columns 40..59, rows 30..49. Centre (0.5, 0.4).
        for (std::uint32_t y = 30; y < 50; ++y)
        {
            for (std::uint32_t x = 40; x < 60; ++x)
            {
                Put(raster, x, y, 1);
            }
        }
        presentation::ProvinceCentroids centroids;
        Check(centroids.Build(raster), "a simple raster did not build");
        double u = 0.0;
        double v = 0.0;
        Check(centroids.Find(1, u, v), "province 1 has no centroid");
        Check(std::abs(u - 0.5) < 1e-9 && std::abs(v - 0.4) < 1e-9,
            "the centroid is " + std::to_string(u) + ", " + std::to_string(v)
                + " rather than 0.5, 0.4");
        Check(centroids.Pixels(1) == 400,
            "the pixel count is " + std::to_string(centroids.Pixels(1)));

        // The ocean and a province with no pixels are both "no centroid", not
        // a default one somebody could zoom to.
        Check(!centroids.Find(0, u, v), "the ocean has a centroid");
        Check(!centroids.Find(2, u, v),
            "a province with no pixels has a centroid");
        Check(!centroids.Find(99, u, v),
            "a province outside the count has a centroid");
    }

    // --- a province the cut edge passes through --------------------------
    //
    // The whole reason longitude is averaged as a direction. Ten columns at
    // each end of the raster: the true centre is u = 0, and the arithmetic
    // mean of the same pixels is 0.5 -- the far side of the world.
    {
        presentation::MapIndexRaster raster = Make(100, 100, 1);
        for (std::uint32_t y = 40; y < 60; ++y)
        {
            for (std::uint32_t x = 0; x < 10; ++x)
            {
                Put(raster, x, y, 1);
            }
            for (std::uint32_t x = 90; x < 100; ++x)
            {
                Put(raster, x, y, 1);
            }
        }
        presentation::ProvinceCentroids centroids;
        Check(centroids.Build(raster), "the wrapping raster did not build");
        double u = 0.0;
        double v = 0.0;
        Check(centroids.Find(1, u, v), "the wrapping province has no centroid");
        Check(std::abs(Delta(0.0, u)) < 1e-9,
            "a province spanning the cut has its centroid at "
                + std::to_string(u) + " instead of 0; longitude was averaged "
                "as a number rather than as a direction");
        Check(std::abs(v - 0.5) < 1e-9,
            "the latitude of the wrapping province is " + std::to_string(v));
    }

    // --- moving a province round the map moves its centroid with it ------
    //
    // The same shape at every longitude: the centroid has to follow, and it
    // has to keep following across the cut rather than jumping.
    {
        for (std::uint32_t shift = 0; shift < 100; shift += 7)
        {
            presentation::MapIndexRaster raster = Make(100, 100, 1);
            for (std::uint32_t y = 45; y < 55; ++y)
            {
                for (std::uint32_t step = 0; step < 10; ++step)
                {
                    Put(raster, (shift + step) % 100, y, 1);
                }
            }
            presentation::ProvinceCentroids centroids;
            centroids.Build(raster);
            double u = 0.0;
            double v = 0.0;
            Check(centroids.Find(1, u, v),
                "the shifted province has no centroid");
            const double expected = Wrap(
                (static_cast<double>(shift) + 5.0) / 100.0
            );
            Check(std::abs(Delta(expected, u)) < 1e-9,
                "at shift " + std::to_string(shift) + " the centroid is "
                    + std::to_string(u) + " rather than "
                    + std::to_string(expected));
        }
    }

    // --- a one pixel province lands in the middle of its pixel -----------
    {
        presentation::MapIndexRaster raster = Make(10, 10, 1);
        Put(raster, 3, 7, 1);
        presentation::ProvinceCentroids centroids;
        centroids.Build(raster);
        double u = 0.0;
        double v = 0.0;
        Check(centroids.Find(1, u, v), "the single pixel has no centroid");
        Check(std::abs(Delta(0.35, u)) < 1e-9 && std::abs(v - 0.75) < 1e-9,
            "a one pixel province is at " + std::to_string(u) + ", "
                + std::to_string(v) + " rather than 0.35, 0.75");
    }

    // --- a full sized raster, in a budget --------------------------------
    //
    // The reference corpus is 5616 x 2160: twelve million pixels, walked once
    // at load. The first version called std::sin and std::cos per PIXEL to
    // compute 5616 distinct answers, which is the kind of cost that never
    // shows up on a hand-built raster and is the whole reason this case exists.
    {
        presentation::MapIndexRaster raster = Make(5616, 2160, 14187);
        // Blocked rather than random, so provinces are contiguous the way real
        // ones are and the accumulation is not measuring cache misses that a
        // real map would not have.
        for (std::uint32_t y = 0; y < raster.height; ++y)
        {
            for (std::uint32_t x = 0; x < raster.width; ++x)
            {
                const std::uint32_t index =
                    1u + ((x / 48u) + (y / 48u) * 117u) % 14187u;
                Put(raster, x, y, static_cast<std::uint16_t>(index));
            }
        }
        const auto start = std::chrono::steady_clock::now();
        const bool built = centroids_full.Build(raster);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
        Check(built, "the full sized raster did not build");
        // Generous, because this runs in Debug too, and a budget that has to
        // be tuned per configuration is a budget nobody trusts. What it is
        // guarding against is an order of magnitude, not a percent.
        Check(elapsed < 2000,
            "building centroids for a full sized raster took "
                + std::to_string(elapsed) + " ms");
        std::cout << "  full sized build: " << elapsed << " ms"
                  << std::endl;
    }

    // --- a raster that does not add up is refused ------------------------
    //
    // A declared size that disagrees with the decoded pixels would index past
    // the end of the vector. "The loader would have caught it" is not the same
    // as catching it.
    {
        presentation::MapIndexRaster short_ = Make(100, 100, 1);
        short_.indices.resize(short_.indices.size() - 1);
        presentation::ProvinceCentroids centroids;
        Check(!centroids.Build(short_),
            "a raster with fewer pixels than it declares built centroids");

        presentation::MapIndexRaster wide = Make(100, 100, 1);
        wide.width = 101;
        Check(!centroids.Build(wide),
            "a raster whose width does not match its pixels built centroids");
    }

    // --- an empty raster is refused rather than half built ---------------
    {
        presentation::ProvinceCentroids centroids;
        presentation::MapIndexRaster empty;
        Check(!centroids.Build(empty), "an unloaded raster built centroids");
        Check(!centroids.IsBuilt(), "a refused build left the table usable");
        double u = 0.0;
        double v = 0.0;
        Check(!centroids.Find(1, u, v), "an unbuilt table answered a query");
    }

    if (failures != 0)
    {
        std::cerr << "province centroids: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "Province centroids: passed (longitude averaged as a "
              << "direction, so a province spanning the cut keeps its "
              << "position)\n";
    return 0;
}
