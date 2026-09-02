#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "map_view.hpp"

// Demo 0.8 P3b -- the map's shape, before any of it is drawn.
//
// The requirement is that the logical geography never changes and only the
// rendered curvature does, continuously, with the sphere and the plane as the
// two limits of one family. That is a mathematical claim, and it can be
// falsified here without a window, a GPU or a single line of GL -- which is
// most of why the renderer is split this way. What is left for the backend is
// uploading buffers.
//
// The properties that matter, and why each one is a property rather than a
// number to eyeball:
//
//   * b = 1 really is a sphere. Not "looks round": every point is exactly R
//     from the centre.
//   * b -> 0 really is the plane, and the map is the same SIZE there. Arc
//     length is preserved, so bending must not zoom.
//   * nothing jumps anywhere in between, including at the conditioning cliff
//     around b = 0 where R diverges.
//   * the camera goes through the same embedding, so the point you are looking
//     at stays in the middle of the screen for every b. This is the one that
//     makes the morph usable rather than merely correct.

namespace
{
using namespace dillen::presentation;

constexpr double kPi = 3.14159265358979323846;

// The reference world map.
const MapProjection kMap{5616, 2160};

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "map view: " << what << '\n';
        ++failures;
    }
}

double Length(const MapPoint& point)
{
    return std::sqrt(
        point.x * point.x + point.y * point.y + point.z * point.z
    );
}

double Distance(const MapPoint& first, const MapPoint& second)
{
    return Length({
        first.x - second.x,
        first.y - second.y,
        first.z - second.z
    });
}

bool Finite(const MapPoint& point)
{
    return std::isfinite(point.x)
        && std::isfinite(point.y)
        && std::isfinite(point.z);
}

}

int main()
{
    // --- the latitude span follows from the aspect, and should ---
    //
    // A 5616x2160 raster covers +-pi*H/W = +-69.2 degrees. The globe is
    // therefore open at the poles, which is correct: the corpus has no data
    // there and closing it would mean inventing some.
    const double extentDegrees = kMap.LatitudeExtent() * 180.0 / kPi;
    Check(std::abs(extentDegrees - 69.23) < 0.05,
        "latitude extent is " + std::to_string(extentDegrees)
            + " degrees, expected about 69.23");

    // --- b = 1 is exactly a sphere ---
    //
    // R = 1 at b = 1, and the centre sits at (0, 0, -1) because the map's
    // centre is held at the origin.
    double worstRadius = 0.0;
    for (std::uint32_t row = 0; row <= 32; ++row)
    {
        for (std::uint32_t column = 0; column <= 32; ++column)
        {
            const double u = static_cast<double>(column) / 32.0;
            const double v = static_cast<double>(row) / 32.0;
            const MapPoint point = ProjectMapPoint(kMap, u, v, 1.0);
            const double radius = Length({point.x, point.y, point.z + 1.0});
            worstRadius = std::max(worstRadius, std::abs(radius - 1.0));
        }
    }
    Check(worstRadius < 1e-12,
        "at b = 1 the surface is off the unit sphere by "
            + std::to_string(worstRadius));

    // --- the globe closes in longitude ---
    //
    // u = 0 and u = 1 are the same meridian. If they do not meet, the sphere
    // has a seam and the adjacency that crosses the date line is a lie.
    double worstSeam = 0.0;
    for (std::uint32_t row = 0; row <= 32; ++row)
    {
        const double v = static_cast<double>(row) / 32.0;
        worstSeam = std::max(
            worstSeam,
            Distance(
                ProjectMapPoint(kMap, 0.0, v, 1.0),
                ProjectMapPoint(kMap, 1.0, v, 1.0)
            )
        );
    }
    Check(worstSeam < 1e-12,
        "at b = 1 the date line does not close, gap "
            + std::to_string(worstSeam));

    // --- b -> 0 is the plane, at the same size ---
    //
    // Arc length is preserved, so the flat map must be exactly 2*pi wide and
    // 2*pi*aspect tall. A morph that quietly rescaled would still look
    // plausible in motion.
    double worstFlat = 0.0;
    for (std::uint32_t row = 0; row <= 32; ++row)
    {
        for (std::uint32_t column = 0; column <= 32; ++column)
        {
            const double u = static_cast<double>(column) / 32.0;
            const double v = static_cast<double>(row) / 32.0;
            const MapPoint point = ProjectMapPoint(kMap, u, v, 0.0);
            const MapPoint expected{
                2.0 * kPi * (u - 0.5),
                2.0 * kPi * (0.5 - v) * kMap.Aspect(),
                0.0
            };
            worstFlat = std::max(worstFlat, Distance(point, expected));
        }
    }
    Check(worstFlat < 1e-12,
        "at b = 0 the surface is not the flat map, off by "
            + std::to_string(worstFlat));

    // --- the approach to the plane is smooth, not merely correct at 0 ---
    //
    // This is the conditioning cliff: R = W/(2*pi*b) diverges while
    // sin(lambda) vanishes. A naive implementation is finite at b = 0 by
    // special case and garbage just above it.
    double worstNearFlat = 0.0;
    for (const double bend : {1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3})
    {
        for (std::uint32_t column = 0; column <= 16; ++column)
        {
            const double u = static_cast<double>(column) / 16.0;
            const MapPoint point = ProjectMapPoint(kMap, u, 0.25, bend);
            const MapPoint flat = ProjectMapPoint(kMap, u, 0.25, 0.0);
            Check(Finite(point),
                "a non-finite point appeared at b = "
                    + std::to_string(bend));
            worstNearFlat = std::max(worstNearFlat, Distance(point, flat));
        }
    }
    // At b = 1e-3 the surface has genuinely started to bend, so this is a
    // bound on "still essentially flat", not on "identical".
    Check(worstNearFlat < 1e-2,
        "the surface diverges from the plane too fast near b = 0, by "
            + std::to_string(worstNearFlat));

    // --- nothing jumps anywhere ---
    //
    // Sweeping b and bounding the step size is what "continuous" means for
    // something a viewer watches. A discontinuity of a tenth of a unit would
    // be invisible in a still and unmistakable in motion.
    double worstStep = 0.0;
    double worstStepBend = 0.0;
    for (std::uint32_t step = 0; step < 2000; ++step)
    {
        const double first = static_cast<double>(step) / 2000.0;
        const double second = static_cast<double>(step + 1) / 2000.0;
        for (std::uint32_t column = 0; column <= 8; ++column)
        {
            const double u = static_cast<double>(column) / 8.0;
            for (const double v : {0.0, 0.5, 1.0})
            {
                const double moved = Distance(
                    ProjectMapPoint(kMap, u, v, first),
                    ProjectMapPoint(kMap, u, v, second)
                );
                if (moved > worstStep)
                {
                    worstStep = moved;
                    worstStepBend = first;
                }
            }
        }
    }
    Check(worstStep < 0.05,
        "the shape jumps by " + std::to_string(worstStep)
            + " at b = " + std::to_string(worstStepBend));

    // --- the camera goes through the same embedding ---
    //
    // Two properties, and only the second one has teeth.
    //
    // That the look-at point lands on the view axis is true BY CONSTRUCTION:
    // the eye is placed at target + normal*distance and the basis is built
    // from that same normal, so the point is on the axis no matter which bend
    // the normal was computed at. Asserting it catches a transposed matrix and
    // nothing else. It is kept for that, and labelled, because a test that
    // cannot fail is worse than no test when it is mistaken for one.
    //
    // What actually pins the requirement is that the camera itself is
    // CONTINUOUS in b. A camera derived separately for the plane and the globe
    // -- pan and zoom below some threshold, orbit above it -- satisfies the
    // first property perfectly and lurches at the seam. Bounding the step of
    // every matrix entry across a sweep is what sees that.
    double worstOffAxis = 0.0;
    double worstDepth = 0.0;
    for (std::uint32_t step = 0; step <= 400; ++step)
    {
        const double bend = static_cast<double>(step) / 400.0;
        MapCamera camera;
        camera.lookAtU = 0.62;
        camera.lookAtV = 0.38;
        camera.distance = 3.0;
        camera.bend = bend;

        const MapViewMatrix view = BuildMapViewMatrix(kMap, camera);
        const MapPoint target = ProjectMapPoint(
            kMap,
            camera.lookAtU,
            camera.lookAtV,
            bend
        );
        const MapPoint inView = TransformMapPoint(view, target);
        Check(Finite(inView),
            "the view matrix produced a non-finite point at b = "
                + std::to_string(bend));
        worstOffAxis = std::max(
            worstOffAxis,
            std::max(std::abs(inView.x), std::abs(inView.y))
        );
        worstDepth = std::max(
            worstDepth,
            std::abs(inView.z + camera.distance)
        );
    }
    Check(worstOffAxis < 1e-9,
        "the look-at point drifts off screen centre by "
            + std::to_string(worstOffAxis));
    Check(worstDepth < 1e-9,
        "the look-at point drifts in depth by " + std::to_string(worstDepth));

    // The one with teeth: no entry of the view matrix may jump.
    double worstCameraStep = 0.0;
    double worstCameraBend = 0.0;
    for (std::uint32_t step = 0; step < 2000; ++step)
    {
        MapCamera before;
        before.lookAtU = 0.62;
        before.lookAtV = 0.38;
        before.distance = 3.0;
        before.bend = static_cast<double>(step) / 2000.0;
        MapCamera after = before;
        after.bend = static_cast<double>(step + 1) / 2000.0;

        const MapViewMatrix first = BuildMapViewMatrix(kMap, before);
        const MapViewMatrix second = BuildMapViewMatrix(kMap, after);
        for (std::size_t entry = 0; entry < first.size(); ++entry)
        {
            const double moved = std::abs(second[entry] - first[entry]);
            if (moved > worstCameraStep)
            {
                worstCameraStep = moved;
                worstCameraBend = before.bend;
            }
        }
    }
    Check(worstCameraStep < 0.05,
        "the camera jumps by " + std::to_string(worstCameraStep)
            + " at b = " + std::to_string(worstCameraBend));

    // --- the normal and the north tangent are a frame, everywhere ---
    for (std::uint32_t step = 0; step <= 64; ++step)
    {
        const double bend = static_cast<double>(step) / 64.0;
        for (const double v : {0.05, 0.5, 0.95})
        {
            const MapPoint normal = MapNormal(kMap, 0.3, v, bend);
            const MapPoint north = MapNorth(kMap, 0.3, v, bend);
            Check(std::abs(Length(normal) - 1.0) < 1e-12,
                "the normal is not unit length");
            Check(std::abs(Length(north) - 1.0) < 1e-12,
                "the north tangent is not unit length");
            const double dot = normal.x * north.x + normal.y * north.y
                + normal.z * north.z;
            Check(std::abs(dot) < 1e-12,
                "the normal and the north tangent are not perpendicular, dot "
                    + std::to_string(dot));
        }
    }
    // At b = 0 the frame must be the plane's, exactly.
    const MapPoint flatNormal = MapNormal(kMap, 0.7, 0.2, 0.0);
    Check(Distance(flatNormal, {0.0, 0.0, 1.0}) < 1e-12,
        "the flat map's normal is not +z");

    if (failures != 0)
    {
        std::cerr << "map view: " << failures << " failure(s)\n";
        return 1;
    }

    std::cout << "Map view: passed (latitude extent " << extentDegrees
              << " degrees, sphere error " << worstRadius
              << ", flat error " << worstFlat << "; over 2000 bend samples "
              << "the shape steps at most " << worstStep
              << " and the camera " << worstCameraStep << ")\n";
    return 0;
}
