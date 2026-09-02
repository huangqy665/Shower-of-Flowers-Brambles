#pragma once

#include <array>
#include <cstdint>

namespace dillen::presentation {

// The map's geometry, and the one family of shapes it is drawn as.
//
// Three spaces, and keeping them apart is the whole design:
//
//   * SIMULATION space is the graph -- Entities and Relations. It has no
//     coordinates at all and nothing here touches it.
//   * MAP space is this file's input: the fixed (u,v) domain of the province
//     raster, u east, v south, both in [0,1]. It never changes.
//   * VIEW space is the output: an embedding of map space into three
//     dimensions, parameterised by a single bend `b`.
//
// A sphere and a plane are two embeddings of ONE map space. b = 1 is the
// globe, b -> 0 is the flat map, and every value between is a real shape
// rather than a blend of two pictures. Nothing about the map, the world or the
// save changes when b does; only where the vertices go.
//
// The family, with s = u - 1/2 and t = (1/2 - v) * aspect:
//
//     a = 2*pi*b            R = W / a            W = 2*pi
//     lambda = a * s        phi = a * t
//     p = R * (cos(phi)sin(lambda), sin(phi), cos(phi)cos(lambda) - 1)
//
// R = 1/b, so b = 1 is the unit sphere and the -1 keeps the map's centre at
// the origin for every b.
//
// ARC LENGTH IS PRESERVED at both ends, which is what stops the map changing
// size as it bends -- and it settles the latitude span rather than leaving it
// to taste. A raster of aspect H/W covers +-pi*H/W radians of latitude: for
// 5616x2160 that is +-69.2 degrees. The globe therefore has open caps at the
// poles, and that is correct. The map has no data there; closing it would mean
// inventing some.
//
// CONDITIONING is the one real trap. R = W/a diverges as b -> 0 while
// sin(lambda) -> 0, so the naive product is 0 * infinity. Every term below is
// written in a form that is exact at b = 0 instead:
//
//     R*cos(phi)*sin(lambda) = W*cos(phi)*s*sinc(a*s)
//     R*sin(phi)             = W*t*sinc(a*t)
//     R*(cos(phi)cos(lambda) - 1)
//         = -W * (2sin^2(phi/2) + cos(phi)*2sin^2(lambda/2)) / a
//
// The last one uses 2sin^2(x/2) rather than 1 - cos(x) because the latter
// cancels catastrophically for small x -- which is precisely the regime the
// flat map lives in.

struct MapPoint
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct MapProjection
{
    // Raster dimensions in texels. Only their ratio matters here; they are
    // carried whole so a caller never has to compute the aspect itself and get
    // it upside down.
    std::uint32_t width = 1;
    std::uint32_t height = 1;

    double Aspect() const noexcept;
    // Half the latitude span, in radians. pi * height / width.
    double LatitudeExtent() const noexcept;
};

// Bend is clamped to [0, 1]. b = 0 is exactly the plane, not an approximation
// of it.
MapPoint ProjectMapPoint(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept;

// The outward unit normal at (u,v).
//
// Computed analytically as (cos(phi)sin(lambda), sin(phi), cos(phi)cos(lambda))
// rather than by normalising p minus the sphere centre. The centre sits at
// z = -R, and R diverges as b -> 0, so the subtraction would lose every
// significant digit of p exactly where the answer matters most. This form is
// (0, 0, 1) at b = 0 with no special case.
MapPoint MapNormal(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept;

// The local "north" tangent, unit length. (0, 1, 0) at b = 0.
MapPoint MapNorth(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept;

// A camera stated in MAP space, not in view space.
//
// This is what makes the morph continuous to look at rather than merely
// continuous on paper. If the camera were defined in view space it would have
// to be re-derived for each shape -- pan and zoom for a plane, orbit for a
// globe -- and the two would not meet in the middle. Defining it over (u,v)
// and pushing it through the SAME embedding means the view cannot jump,
// because there is nothing to jump between.
struct MapCamera
{
    double lookAtU = 0.5;
    double lookAtV = 0.5;
    // Along the local outward normal, in the same world units the projection
    // uses (the unit sphere has radius 1 at b = 1).
    double distance = 3.0;
    double bend = 1.0;
};

// Row-major 4x4, the usual right-handed look-at: the camera sits at
// lookAt + distance * normal and stares back down it.
using MapViewMatrix = std::array<double, 16>;

MapViewMatrix BuildMapViewMatrix(
    const MapProjection& projection,
    const MapCamera& camera
) noexcept;

// Applies a row-major 4x4 to a point, dropping the w component.
MapPoint TransformMapPoint(
    const MapViewMatrix& matrix,
    const MapPoint& point
) noexcept;

}
