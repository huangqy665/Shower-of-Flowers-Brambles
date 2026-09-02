#include "map_view.hpp"

#include <cmath>

namespace dillen::presentation {

namespace {

constexpr double kPi = 3.14159265358979323846;
// The flat map is 2*pi wide, so R = W / (2*pi*b) = 1/b and b = 1 is the unit
// sphere. Everything downstream -- camera distances, near and far planes -- is
// stated against that.
constexpr double kWidth = 2.0 * kPi;

double Clamp01(double value) noexcept
{
    if (!(value > 0.0))
    {
        // Also catches NaN, which must not be allowed to reach the shape.
        return 0.0;
    }
    return value < 1.0 ? value : 1.0;
}

// sin(x)/x, exact at zero.
//
// This is what removes the 0 * infinity in R * sin(lambda): R diverges as
// b -> 0 and sin(lambda) vanishes, but their product is finite and equals the
// flat coordinate. Writing it as a sinc means the flat limit falls out of the
// same expression instead of needing a branch.
double Sinc(double x) noexcept
{
    const double magnitude = x < 0.0 ? -x : x;
    if (magnitude < 1e-8)
    {
        // Two terms are plenty: the next is x^4/120, below 1e-34 here.
        return 1.0 - (x * x) / 6.0;
    }
    return std::sin(x) / x;
}

// 2*sin^2(x/2), which is 1 - cos(x) without the cancellation.
double Versine(double x) noexcept
{
    const double half = std::sin(x * 0.5);
    return 2.0 * half * half;
}

struct Angles
{
    double lambda = 0.0;
    double phi = 0.0;
    double s = 0.0;
    double t = 0.0;
    double a = 0.0;
};

Angles AnglesFor(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept
{
    Angles angles;
    angles.a = 2.0 * kPi * Clamp01(bend);
    angles.s = u - 0.5;
    angles.t = (0.5 - v) * projection.Aspect();
    angles.lambda = angles.a * angles.s;
    angles.phi = angles.a * angles.t;
    return angles;
}

MapPoint Normalise(MapPoint point) noexcept
{
    const double length = std::sqrt(
        point.x * point.x + point.y * point.y + point.z * point.z
    );
    if (!(length > 0.0))
    {
        return {0.0, 0.0, 1.0};
    }
    return {point.x / length, point.y / length, point.z / length};
}

MapPoint Cross(const MapPoint& first, const MapPoint& second) noexcept
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x
    };
}

double Dot(const MapPoint& first, const MapPoint& second) noexcept
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

}

double MapProjection::Aspect() const noexcept
{
    return width == 0
        ? 1.0
        : static_cast<double>(height) / static_cast<double>(width);
}

double MapProjection::LatitudeExtent() const noexcept
{
    return kPi * Aspect();
}

MapPoint ProjectMapPoint(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept
{
    const Angles angles = AnglesFor(projection, u, v, bend);
    const double cosPhi = std::cos(angles.phi);

    MapPoint point;
    point.x = kWidth * cosPhi * angles.s * Sinc(angles.lambda);
    point.y = kWidth * angles.t * Sinc(angles.phi);
    if (angles.a < 1e-12)
    {
        // Exactly flat. Not an approximation: at b = 0 the surface is the
        // plane and its z is zero everywhere.
        point.z = 0.0;
        return point;
    }
    point.z = -kWidth
        * (Versine(angles.phi) + cosPhi * Versine(angles.lambda))
        / angles.a;
    return point;
}

MapPoint MapNormal(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept
{
    const Angles angles = AnglesFor(projection, u, v, bend);
    const double cosPhi = std::cos(angles.phi);
    return {
        cosPhi * std::sin(angles.lambda),
        std::sin(angles.phi),
        cosPhi * std::cos(angles.lambda)
    };
}

MapPoint MapNorth(
    const MapProjection& projection,
    double u,
    double v,
    double bend
) noexcept
{
    const Angles angles = AnglesFor(projection, u, v, bend);
    const double sinPhi = std::sin(angles.phi);
    return {
        -sinPhi * std::sin(angles.lambda),
        std::cos(angles.phi),
        -sinPhi * std::cos(angles.lambda)
    };
}

MapViewMatrix BuildMapViewMatrix(
    const MapProjection& projection,
    const MapCamera& camera
) noexcept
{
    const MapPoint target = ProjectMapPoint(
        projection,
        camera.lookAtU,
        camera.lookAtV,
        camera.bend
    );
    const MapPoint normal = MapNormal(
        projection,
        camera.lookAtU,
        camera.lookAtV,
        camera.bend
    );
    const MapPoint north = MapNorth(
        projection,
        camera.lookAtU,
        camera.lookAtV,
        camera.bend
    );

    const double distance = camera.distance > 1e-6
        ? camera.distance
        : 1e-6;
    const MapPoint eye = {
        target.x + normal.x * distance,
        target.y + normal.y * distance,
        target.z + normal.z * distance
    };

    // Right-handed: forward is -z in view space, so the basis is built from
    // the direction the camera looks (target - eye), which is exactly -normal.
    const MapPoint forward = {-normal.x, -normal.y, -normal.z};
    // North is already perpendicular to the normal analytically; the
    // re-orthogonalisation is defensive rather than corrective.
    const MapPoint right = Normalise(Cross(forward, north));
    const MapPoint up = Cross(right, forward);

    MapViewMatrix matrix{};
    matrix[0] = right.x;
    matrix[1] = right.y;
    matrix[2] = right.z;
    matrix[3] = -Dot(right, eye);
    matrix[4] = up.x;
    matrix[5] = up.y;
    matrix[6] = up.z;
    matrix[7] = -Dot(up, eye);
    matrix[8] = -forward.x;
    matrix[9] = -forward.y;
    matrix[10] = -forward.z;
    matrix[11] = Dot(forward, eye);
    matrix[12] = 0.0;
    matrix[13] = 0.0;
    matrix[14] = 0.0;
    matrix[15] = 1.0;
    return matrix;
}

MapPoint TransformMapPoint(
    const MapViewMatrix& matrix,
    const MapPoint& point
) noexcept
{
    return {
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z
            + matrix[3],
        matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z
            + matrix[7],
        matrix[8] * point.x + matrix[9] * point.y + matrix[10] * point.z
            + matrix[11]
    };
}

}
