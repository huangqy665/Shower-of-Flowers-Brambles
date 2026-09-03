#include "map_camera_controller.hpp"

#include <algorithm>
#include <cmath>

namespace dillen::presentation {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Clamp(double value, double low, double high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

double WrapUnit(double value) noexcept
{
    return value - std::floor(value);
}

// Shortest signed distance from `from` to `to` on a circle of circumference 1.
double CircularDelta(double from, double to) noexcept
{
    double delta = WrapUnit(to - from);
    if (delta > 0.5)
    {
        delta -= 1.0;
    }
    return delta;
}

}

MapCameraController::MapCameraController(MapProjection projection) noexcept
    : projection_(projection)
{
}

void MapCameraController::Reset(const MapCamera& camera) noexcept
{
    camera_ = camera;
    camera_.lookAtU = WrapUnit(camera_.lookAtU);
    camera_.lookAtV = Clamp(camera_.lookAtV, 0.0, 1.0);
    camera_.distance =
        Clamp(camera_.distance, limits_.nearDistance, limits_.farDistance);
    camera_.bend = Clamp(camera_.bend, 0.0, 1.0);
    // A reset states the whole camera, so the lock is derived rather than
    // carried in from whatever the caller happened to leave in the struct.
    camera_.orientationV = camera_.lookAtV;
    camera_.orientationLocked = camera_.bend < limits_.globeAboveBend;
}

MapCameraMode MapCameraController::Mode() const noexcept
{
    if (camera_.bend <= limits_.flatBelowBend)
    {
        return MapCameraMode::Flat;
    }
    if (camera_.bend >= limits_.globeAboveBend)
    {
        return MapCameraMode::Globe;
    }
    return MapCameraMode::Transition;
}

double MapCameraController::LookAtLatitudeDegrees() const noexcept
{
    // The same relation the projection uses: latitude spans +-pi*aspect
    // radians, linear in v, with v = 1/2 on the equator.
    const double radians =
        (0.5 - camera_.lookAtV) * kPi * projection_.Aspect();
    return radians * 180.0 / kPi;
}

bool MapCameraController::CurvatureIsUnlocked() const noexcept
{
    // Away from the equator an equirectangular map cannot be unrolled without
    // stretching longitude past any sensible bound, so the wheel is left
    // meaning distance and the globe stays a globe.
    //
    // This gates FLATTENING only. Curving back up is always allowed, or a
    // player who unfolded on the equator and then panned north would be
    // stranded: the map could not flatten further and could not return to a
    // globe either, and the wheel would do nothing at all.
    return std::abs(LookAtLatitudeDegrees()) <= limits_.equatorialBandDegrees;
}

void MapCameraController::ApplyBend(double bend) noexcept
{
    camera_.bend = Clamp(bend, 0.0, 1.0);
    UpdateOrientationLock();
}

// Frozen on the way out of Globe, released on the way back in.
//
// The parallel is latched at the moment the map stops being a globe, so the
// direction the camera ends up holding is the one it had when the player
// started unfolding -- not whichever parallel they happen to reach later.
void MapCameraController::UpdateOrientationLock() noexcept
{
    const bool locked = camera_.bend < limits_.globeAboveBend;
    if (locked && !camera_.orientationLocked)
    {
        camera_.orientationV = camera_.lookAtV;
    }
    camera_.orientationLocked = locked;
    if (!locked)
    {
        camera_.orientationV = camera_.lookAtV;
    }
}

void MapCameraController::Drag(double dx, double dy) noexcept
{
    // Both gestures slow as the camera closes in, so the ground keeps up with
    // the pointer instead of tearing away.
    const double scale = camera_.distance / limits_.farDistance;
    const MapCameraMode mode = Mode();
    const double step = mode == MapCameraMode::Globe
        ? limits_.rotateRadiansPerPixel
        : limits_.panPerPixel;

    camera_.lookAtU = WrapUnit(camera_.lookAtU - dx * step * scale);

    // Up and down only where it is a pure slide.
    //
    // On a globe the camera orbits, so crossing parallels is exactly what the
    // gesture is for. At bend 0 the normal is the same everywhere, so sliding
    // north is a translation and nothing tilts. In between it is neither: the
    // camera direction is frozen, so the surface drags away underneath it and
    // the map appears to skew rather than move. Tested by hand and dropped.
    if (mode != MapCameraMode::Transition)
    {
        camera_.lookAtV =
            Clamp(camera_.lookAtV - dy * step * scale, 0.0, 1.0);
    }
}

void MapCameraController::Pan(double du, double dv) noexcept
{
    camera_.lookAtU = WrapUnit(camera_.lookAtU + du);
    if (Mode() != MapCameraMode::Transition)
    {
        camera_.lookAtV = Clamp(camera_.lookAtV + dv, 0.0, 1.0);
    }
}

void MapCameraController::Zoom(
    double notches,
    bool hasAnchor,
    double anchorU,
    double anchorV
) noexcept
{
    if (notches == 0.0)
    {
        return;
    }
    const double before = camera_.distance;
    const double after = Clamp(
        before * std::pow(limits_.zoomStep, notches),
        limits_.nearDistance,
        limits_.farDistance
    );
    camera_.distance = after;

    // Towards the cursor, not towards the middle of the screen.
    //
    // The exact solve would invert the projection, which is not possible for
    // an arbitrary bend -- the same reason picking reads a buffer instead of
    // doing arithmetic. Moving the look-at towards the anchor in the same
    // proportion as the distance shrank is the approximation every map uses,
    // and it is exact in the limit of small steps.
    if (hasAnchor && before > 0.0)
    {
        const double pull = Clamp(1.0 - after / before, -1.0, 1.0);
        camera_.lookAtU = WrapUnit(
            camera_.lookAtU
                + CircularDelta(camera_.lookAtU, WrapUnit(anchorU)) * pull
        );
        camera_.lookAtV = Clamp(
            camera_.lookAtV + (anchorV - camera_.lookAtV) * pull,
            0.0,
            1.0
        );
    }

    // Curvature follows the wheel, one way freely and the other only inside
    // the equatorial band.
    const double span = std::log(limits_.farDistance / limits_.nearDistance);
    const double target = span > 0.0
        ? std::log(camera_.distance / limits_.nearDistance) / span
        : 1.0;
    if (target >= camera_.bend || CurvatureIsUnlocked())
    {
        ApplyBend(target);
    }
}

bool MapCameraController::NudgeBend(double delta) noexcept
{
    return SetBend(camera_.bend + delta);
}

bool MapCameraController::SetBend(double bend) noexcept
{
    const double target = Clamp(bend, 0.0, 1.0);
    // The same rule the wheel obeys, for the same reason: an equirectangular
    // map cannot be unrolled away from the equator without stretching
    // longitude past any bound, and the geometry does not care which input
    // asked for it. Curving back up is always allowed, so a key cannot strand
    // a player anywhere the wheel could not.
    if (target < camera_.bend && !CurvatureIsUnlocked())
    {
        return false;
    }
    // Nothing else moves. Not the distance, not the look-at, not the aim: a
    // key that slid the view while changing the curvature would make it
    // impossible to see what the curvature alone did.
    ApplyBend(target);
    return true;
}

}
