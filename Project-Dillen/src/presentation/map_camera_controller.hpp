#pragma once

#include <cstdint>

#include "map_view.hpp"

namespace dillen::presentation {

// Input policy for the map camera.
//
// One surface, three ways of holding it. The geometry stays a single family
// parameterised by `bend` -- there is no second map system anywhere below --
// but what the mouse MEANS changes across that family, because a globe and a
// plane are not held the same way. Trying to make one set of gestures cover
// both is what makes a map feel wrong at the ends: an orbit that becomes a
// pan by degrees is neither.
//
//   Globe       drag rotates, the wheel changes distance only, and latitude
//               above the equatorial band pins the curvature. Flattening an
//               equirectangular map away from the equator is geometrically
//               ugly -- longitude stretches without bound towards the poles --
//               so the map declines to start there rather than doing it badly.
//   Transition  the wheel drives curvature. Dragging slides the map SIDEWAYS
//               only: the camera direction is frozen, so moving along a
//               parallel is a pure slide, while moving across parallels drags
//               the surface away under a camera that is no longer following
//               it. Reading a map is a sideways gesture anyway.
//   Flat        drag pans in both axes -- at bend 0 the normal is the same
//               everywhere, so up and down is a pure slide too -- and the
//               wheel is an ordinary map zoom.
//
// The modes are READ OFF the continuous bend, not stored as state that could
// disagree with it. Nothing else is stored: this is a pure function of the
// gestures so far.
//
// ONCE IT STARTS UNFOLDING, THE VIEW STOPS TURNING
//
// A globe is orbited and a map is slid about, and the change happens the
// moment the surface stops being a globe. So leaving Globe freezes the
// camera's direction: `MapCamera::orientationLocked` goes up, the parallel it
// was looking at is latched, and from then on a drag moves the look-at without
// re-aiming the camera. Up, down, left and right, and nothing else.
//
// THE SEAM IS NOT THIS CLASS'S PROBLEM
//
// Below a full sphere the surface has a cut edge, and an earlier version of
// this held an "unfolding centre" and refused to pan more than half a turn
// from it so the cut stayed off screen. That bought the property at the cost
// of making longitudes unreachable, and it was solving the wrong problem: the
// backend now builds its grid CENTRED ON THE CAMERA, so the cut is at the far
// end of the strip -- behind the viewer -- whatever the camera does. A
// province the cut passes through is drawn at both ends of the grid, twice,
// both copies carrying the same index and so the same Entity.
//
// So the camera is free. Nothing here clamps a longitude.
//
// All of it is arithmetic on doubles, so all of it is gated headless. A camera
// policy tested by looking at it is a camera policy nobody can change safely.

enum class MapCameraMode
{
    Globe,
    Transition,
    Flat
};

struct MapCameraLimits
{
    // Measured from the surface along its normal.
    double nearDistance = 0.2;
    double farDistance = 2.0;
    // Curvature is only allowed to leave 1.0 within this many degrees of the
    // equator. 15 degrees at the reference corpus is about a fifth of the
    // latitude the map covers.
    double equatorialBandDegrees = 15.0;
    // Where Globe ends and Flat begins. Between them the wheel drives
    // curvature; outside them it drives distance.
    double flatBelowBend = 0.02;
    double globeAboveBend = 0.98;
    // Radians of rotation per pixel of drag, at the far end of the zoom range.
    double rotateRadiansPerPixel = 0.0035;
    // Fraction of the map per pixel of drag, at the far end of the zoom range,
    // used once the map has stopped being a globe.
    //
    // Derived rather than tuned by eye, so that the ground keeps up with the
    // pointer. At bend 0 the plane is 2*pi world units wide, which is the
    // whole of u. A camera at distance d with a 45 degree field of view sees
    // 2*d*tan(22.5) = 0.828*d units of height, and 1.472*d of width at 16:9 --
    // 0.234*d of u. Across a 1280 pixel window that is 0.000183*d per pixel,
    // and this constant is quoted at the far distance, so 0.000183 * 2.0.
    //
    // It is a constant rather than a live solve because the controller does
    // not know the viewport or the field of view, and giving it both to chase
    // the last few percent would couple it to the backend it is deliberately
    // independent of. A window far from 16:9 at 1280 drags slightly fast or
    // slow, and the number is here to be changed.
    double panPerPixel = 0.00037;
    // One wheel notch scales the distance by this.
    double zoomStep = 0.9;
};

class MapCameraController
{
public:
    explicit MapCameraController(MapProjection projection) noexcept;

    void Reset(const MapCamera& camera) noexcept;
    const MapCamera& Camera() const noexcept { return camera_; }
    MapCameraMode Mode() const noexcept;
    MapCameraLimits& Limits() noexcept { return limits_; }
    const MapCameraLimits& Limits() const noexcept { return limits_; }

    // Latitude of the look-at point, in degrees. Positive is north.
    double LookAtLatitudeDegrees() const noexcept;
    // Whether the wheel would currently change curvature.
    bool CurvatureIsUnlocked() const noexcept;

    // Drag, in pixels. Both axes, in every mode: with the seam handled by
    // the grid there is no longer a direction that has to be forbidden.
    void Drag(double dx, double dy) noexcept;

    // Wheel notches, positive away from the viewer.
    //
    // `anchorU`/`anchorV` is where the camera should settle -- the PROVINCE
    // under the cursor, not the pixel under it. A strategy map zooms onto a
    // region, not onto an arbitrary point inside one, and the difference is
    // the difference between a map that settles and a map that drifts.
    // Passing `hasAnchor = false` zooms about the centre of the screen.
    void Zoom(
        double notches,
        bool hasAnchor,
        double anchorU,
        double anchorV
    ) noexcept;

    // Held-key nudges, kept so the keyboard can still reach everything the
    // mouse can.
    void Pan(double du, double dv) noexcept;

    // Curvature by hand.
    //
    // Gated the same way the wheel is -- flattening only inside the equatorial
    // band, curving back up always -- because the geometry does not care which
    // input asked. And the camera does not move at all: no distance, no
    // look-at, no re-aim. A key that changed the curvature AND slid the view
    // would make it impossible to see what the curvature alone did.
    //
    // Returns false when the band refused it, so a caller can say so.
    bool NudgeBend(double delta) noexcept;
    bool SetBend(double bend) noexcept;

private:
    void ApplyBend(double bend) noexcept;
    void UpdateOrientationLock() noexcept;

    MapProjection projection_;
    MapCameraLimits limits_;
    MapCamera camera_;
};

}
