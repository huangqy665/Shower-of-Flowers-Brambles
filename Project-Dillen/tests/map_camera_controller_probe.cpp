#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "map_camera_controller.hpp"

// Demo 0.8 -- the camera's input policy, headless.
//
// One surface held three ways. The geometry is a single family parameterised
// by `bend` and there is no second map system anywhere; what changes across
// the family is what a gesture MEANS, because a globe and a plane are not held
// the same way.
//
// Every rule below is arithmetic on doubles, which is why it is tested here
// rather than by looking at a window. A camera policy nobody can assert about
// is a camera policy nobody can change safely.

namespace
{
using namespace dillen;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "map camera: " << what << '\n';
        ++failures;
    }
}

// The reference corpus: 5616 x 2160, so the map covers +-69.2 degrees.
presentation::MapProjection Projection()
{
    return presentation::MapProjection{5616, 2160};
}

presentation::MapCameraController AtGlobe()
{
    presentation::MapCameraController controller(Projection());
    presentation::MapCamera start;
    start.lookAtU = 0.5;
    start.lookAtV = 0.5;
    start.distance = controller.Limits().farDistance;
    start.bend = 1.0;
    controller.Reset(start);
    return controller;
}

double Wrap(double value)
{
    return value - std::floor(value);
}

// Shortest signed distance on a circle of circumference 1.
double Delta(double from, double to)
{
    double delta = Wrap(to - from);
    return delta > 0.5 ? delta - 1.0 : delta;
}

}

int main()
{
    // --- the modes are read off the bend, not stored beside it -----------
    {
        presentation::MapCameraController controller = AtGlobe();
        Check(controller.Mode() == presentation::MapCameraMode::Globe,
            "a fully curved map is not in Globe");
        controller.SetBend(0.5);
        Check(controller.Mode() == presentation::MapCameraMode::Transition,
            "a half curved map is not in Transition");
        controller.SetBend(0.0);
        Check(controller.Mode() == presentation::MapCameraMode::Flat,
            "a flat map is not in Flat");
        controller.SetBend(1.0);
        Check(controller.Mode() == presentation::MapCameraMode::Globe,
            "curving back up did not return to Globe");
    }

    // --- Globe: the wheel does not flatten away from the equator ---------
    //
    // Flattening an equirectangular map at high latitude stretches longitude
    // without bound, so the map declines to start there rather than doing it
    // badly. The rule is the reviewer's, and this is the whole of it.
    {
        presentation::MapCameraController controller = AtGlobe();
        // Far north: about 55 degrees on this corpus.
        presentation::MapCamera high = controller.Camera();
        high.lookAtV = 0.1;
        controller.Reset(high);
        Check(std::abs(controller.LookAtLatitudeDegrees()) > 15.0,
            "the test is not actually at a high latitude: "
                + std::to_string(controller.LookAtLatitudeDegrees()));
        Check(!controller.CurvatureIsUnlocked(),
            "curvature is unlocked at high latitude");

        const double before = controller.Camera().distance;
        for (int step = 0; step < 40; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().distance < before,
            "the wheel did not move the camera closer at high latitude");
        Check(controller.Camera().bend == 1.0,
            "zooming in at high latitude flattened the map to "
                + std::to_string(controller.Camera().bend));
        Check(controller.Mode() == presentation::MapCameraMode::Globe,
            "a high-latitude zoom left Globe");
    }

    // --- near the equator the same wheel drives curvature ----------------
    {
        presentation::MapCameraController controller = AtGlobe();
        Check(controller.CurvatureIsUnlocked(),
            "curvature is locked on the equator");
        double last = controller.Camera().bend;
        bool monotonic = true;
        for (int step = 0; step < 40; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
            if (controller.Camera().bend > last + 1e-12)
            {
                monotonic = false;
            }
            last = controller.Camera().bend;
        }
        Check(monotonic, "zooming in did not flatten the map monotonically");
        Check(controller.Camera().bend == 0.0,
            "zooming all the way in did not reach a plane, got "
                + std::to_string(controller.Camera().bend));
        Check(controller.Mode() == presentation::MapCameraMode::Flat,
            "the fully zoomed map is not Flat");
        // And back out.
        for (int step = 0; step < 40; ++step)
        {
            controller.Zoom(-1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().bend == 1.0
                && controller.Mode() == presentation::MapCameraMode::Globe,
            "zooming back out did not return to a globe");
    }

    // --- no longitude is out of reach, at any curvature -----------------
    //
    // An earlier version kept the map's cut edge off screen by refusing to pan
    // more than half a turn from wherever unfolding began. It worked and it
    // was the wrong fix: it bought the property by making longitudes
    // unreachable. The backend now builds its grid centred on the camera, so
    // the cut is behind the viewer by construction and this class clamps
    // nothing -- which is what these assertions are about.
    {
        presentation::MapCameraController controller = AtGlobe();
        presentation::MapCamera start = controller.Camera();
        start.lookAtU = 0.3;
        controller.Reset(start);
        for (int step = 0; step < 12; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Mode() != presentation::MapCameraMode::Globe,
            "the map did not start unfolding");

        // Turn a long way and confirm the camera really does travel: every
        // longitude has to be reachable while the map is partly unfolded.
        double lowest = 1.0;
        double highest = 0.0;
        for (int step = 0; step < 400; ++step)
        {
            controller.Drag(-40.0, 0.0);
            lowest = std::min(lowest, controller.Camera().lookAtU);
            highest = std::max(highest, controller.Camera().lookAtU);
        }
        Check(lowest < 0.05 && highest > 0.95,
            "the camera could not reach every longitude while unfolded: "
                + std::to_string(lowest) + " .. " + std::to_string(highest));

        // Sideways only. Checked again here because the roaming test above
        // is what would notice a vertical axis quietly coming back.
        const double heldV = controller.Camera().lookAtV;
        controller.Drag(0.0, -60.0);
        Check(controller.Camera().lookAtV == heldV,
            "a vertical drag moved an unfolding map");
    }

    // --- once it unfolds, the view stops turning --------------------------
    //
    // A globe is orbited; an unrolling map is slid about. The property is that
    // the camera's DIRECTION is held while the look-at moves, which in the
    // view matrix means the three basis rows stay put and only the translation
    // column changes. Asserting "the camera moved" would be satisfied by an
    // orbit, which is exactly the thing this rule forbids.
    {
        presentation::MapCameraController controller = AtGlobe();
        const presentation::MapProjection projection = Projection();

        // While a globe, dragging DOES turn the camera. Stated first so the
        // assertion below is known to be capable of failing.
        const presentation::MapViewMatrix globeBefore =
            presentation::BuildMapViewMatrix(
                projection,
                presentation::CameraInGridSpace(controller.Camera()));
        controller.Drag(0.0, -80.0);
        const presentation::MapViewMatrix globeAfter =
            presentation::BuildMapViewMatrix(
                projection,
                presentation::CameraInGridSpace(controller.Camera()));
        bool globeTurned = false;
        for (const std::size_t row : {0u, 4u, 8u})
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                if (std::abs(globeBefore[row + column]
                        - globeAfter[row + column]) > 1e-9)
                {
                    globeTurned = true;
                }
            }
        }
        Check(globeTurned, "dragging a globe did not turn the camera");

        // Now unfold, and the same drag must slide instead.
        controller = AtGlobe();
        for (int step = 0; step < 12; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Mode() != presentation::MapCameraMode::Globe,
            "the map did not unfold");
        Check(controller.Camera().orientationLocked,
            "the camera direction was not frozen when unfolding began");
        const double frozen = controller.Camera().orientationV;

        // The strongest form of "the camera is fixed": while unfolding, a
        // drag leaves the view matrix ENTIRELY alone -- basis and translation
        // both. The map moves because the backend slides the raster under a
        // grid it rebuilds around the camera, so there is nothing left for the
        // camera itself to do.
        const presentation::MapViewMatrix before =
            presentation::BuildMapViewMatrix(
                projection,
                presentation::CameraInGridSpace(controller.Camera()));
        std::size_t changed = 0;
        double travelled = 0.0;
        double startU = controller.Camera().lookAtU;
        for (const double dx : {-60.0, 60.0, 0.0, 0.0})
        {
            const double dy = dx == 0.0 ? 60.0 : 0.0;
            controller.Drag(dx, dy);
            travelled += std::abs(Delta(startU, controller.Camera().lookAtU));
            startU = controller.Camera().lookAtU;
            const presentation::MapViewMatrix now =
                presentation::BuildMapViewMatrix(
                    projection,
                    presentation::CameraInGridSpace(controller.Camera()));
            for (std::size_t element = 0; element < 16; ++element)
            {
                if (std::abs(before[element] - now[element]) > 1e-9)
                {
                    ++changed;
                }
            }
        }
        Check(changed == 0,
            "the view matrix changed in " + std::to_string(changed)
                + " places while the map was unfolding; the camera should be "
                  "completely still");
        Check(travelled > 0.0,
            "dragging an unfolding map did not move it at all");
        Check(controller.Camera().orientationV == frozen,
            "the frozen parallel drifted while dragging");

        // Sideways only while unfolding.
        //
        // Tested by hand and dropped: with the camera direction frozen,
        // crossing parallels drags the surface away underneath it and the map
        // skews rather than moves. Along a parallel it is a pure slide.
        presentation::MapCameraController axes = AtGlobe();
        for (int step = 0; step < 12; ++step)
        {
            axes.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(axes.Mode() == presentation::MapCameraMode::Transition,
            "the map is not in Transition after twelve notches");
        const presentation::MapCamera start = axes.Camera();
        axes.Drag(50.0, 0.0);
        Check(axes.Camera().lookAtU != start.lookAtU,
            "an unfolding map would not pan sideways");
        Check(axes.Camera().lookAtV == start.lookAtV,
            "an unfolding map moved vertically on a sideways drag");
        axes.Drag(0.0, 200.0);
        Check(axes.Camera().lookAtV == start.lookAtV,
            "an unfolding map panned up and down");

        // A flat map does pan in both axes: at bend 0 the normal is the same
        // everywhere, so up and down is a pure slide too. Stated here so the
        // assertion above is known to be about Transition rather than about
        // vertical panning being broken everywhere.
        presentation::MapCameraController flat = AtGlobe();
        for (int step = 0; step < 60; ++step)
        {
            flat.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(flat.Mode() == presentation::MapCameraMode::Flat,
            "the map is not Flat after sixty notches");
        const double flatV = flat.Camera().lookAtV;
        flat.Drag(0.0, 200.0);
        Check(flat.Camera().lookAtV != flatV,
            "a flat map would not pan up and down");

        // Curving back up hands the globe its orbit again.
        for (int step = 0; step < 60; ++step)
        {
            axes.Zoom(-1.0, false, 0.0, 0.0);
        }
        Check(!axes.Camera().orientationLocked,
            "the camera direction stayed frozen after returning to a globe");
    }

    // --- the curvature keys obey the same band, and move nothing else ----
    //
    // The geometry does not care which input asked to flatten it, so a key is
    // refused exactly where the wheel is. And a key that changed the curvature
    // AND slid the view would make it impossible to see what the curvature
    // alone did, so nothing but the bend moves.
    {
        presentation::MapCameraController controller = AtGlobe();
        presentation::MapCamera north = controller.Camera();
        north.lookAtV = 0.1;
        controller.Reset(north);
        Check(!controller.CurvatureIsUnlocked(),
            "the camera is not out of the equatorial band");

        const presentation::MapCamera before = controller.Camera();
        Check(!controller.NudgeBend(-0.2),
            "a curvature key flattened the map at high latitude");
        Check(!controller.SetBend(0.0),
            "a curvature preset flattened the map at high latitude");
        Check(controller.Camera().bend == before.bend,
            "a refused curvature key moved the curvature anyway");

        // Curving back up is never refused, from anywhere.
        presentation::MapCameraController stuck = AtGlobe();
        for (int step = 0; step < 12; ++step)
        {
            stuck.Zoom(1.0, false, 0.0, 0.0);
        }
        presentation::MapCamera moved = stuck.Camera();
        moved.lookAtV = 0.1;
        stuck.Reset(moved);
        Check(stuck.SetBend(1.0),
            "a curvature key could not return the map to a globe");

        // On the equator the key works, and moves NOTHING else.
        presentation::MapCameraController equator = AtGlobe();
        const presentation::MapCamera held = equator.Camera();
        Check(equator.NudgeBend(-0.2), "a curvature key was refused on the "
            "equator");
        Check(equator.Camera().bend < held.bend,
            "the curvature key did not flatten the map");
        Check(equator.Camera().lookAtU == held.lookAtU
                && equator.Camera().lookAtV == held.lookAtV
                && equator.Camera().distance == held.distance,
            "a curvature key moved the camera as well as the curvature");
    }

    // --- the latitude rule is one way ------------------------------------
    //
    // Flattening away from the equator is refused; curving back up never is.
    // Gating both would strand a player who unfolded on the equator and then
    // panned north: the wheel would do nothing in either direction.
    {
        presentation::MapCameraController controller = AtGlobe();
        for (int step = 0; step < 12; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        const double unfolded = controller.Camera().bend;
        Check(unfolded < 1.0, "the map did not unfold on the equator");

        // North, out of the band.
        presentation::MapCamera north = controller.Camera();
        north.lookAtV = 0.1;
        controller.Reset(north);
        Check(!controller.CurvatureIsUnlocked(),
            "the camera is not actually out of the equatorial band");

        const double held = controller.Camera().bend;
        for (int step = 0; step < 20; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().bend >= held - 1e-12,
            "the map flattened further at high latitude");

        for (int step = 0; step < 60; ++step)
        {
            controller.Zoom(-1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().bend == 1.0
                && controller.Mode() == presentation::MapCameraMode::Globe,
            "a player who unfolded and then panned north was stranded at bend "
                + std::to_string(controller.Camera().bend));
    }

    // --- a globe turns in both axes --------------------------------------
    {
        presentation::MapCameraController controller = AtGlobe();
        const presentation::MapCamera before = controller.Camera();
        controller.Drag(60.0, 40.0);
        Check(controller.Camera().lookAtU != before.lookAtU
                && controller.Camera().lookAtV != before.lookAtV,
            "dragging a globe did not turn it in both axes");
        // Dragging right turns the map left under the cursor, so the ground
        // follows the pointer rather than running from it.
        Check(Delta(before.lookAtU, controller.Camera().lookAtU) < 0.0,
            "dragging right moved the ground the wrong way");
    }

    // --- zooming goes towards the cursor ---------------------------------
    //
    // Without this a map is something you fight: you zoom in, the thing you
    // were looking at leaves the screen, and you pan it back.
    {
        presentation::MapCameraController controller = AtGlobe();
        // Where the PROVINCE is, not where the pixel was. The caller reads
        // the province under the cursor and passes its position; the camera
        // settles on the region rather than drifting towards a corner of it.
        const double anchorU = 0.62;
        const double anchorV = 0.44;
        const double startU = controller.Camera().lookAtU;
        const double startV = controller.Camera().lookAtV;
        Check(std::abs(Delta(startU, anchorU)) > 0.05,
            "the anchor is not off centre to begin with");

        double previous = std::abs(Delta(startU, anchorU));
        for (int step = 0; step < 10; ++step)
        {
            controller.Zoom(1.0, true, anchorU, anchorV);
            const double now =
                std::abs(Delta(controller.Camera().lookAtU, anchorU));
            Check(now <= previous + 1e-12,
                "zooming moved the camera away from the cursor");
            previous = now;
        }
        Check(previous < std::abs(Delta(startU, anchorU)) * 0.9,
            "ten notches towards the cursor barely moved the camera");
        Check(std::abs(controller.Camera().lookAtV - anchorV)
                < std::abs(startV - anchorV) + 1e-12,
            "zooming did not track the cursor vertically while a globe");

        // Zooming out moves away from the anchor again, which is what makes
        // the gesture reversible rather than a one-way funnel.
        presentation::MapCameraController out = AtGlobe();
        presentation::MapCamera close = out.Camera();
        close.distance = out.Limits().nearDistance;
        out.Reset(close);
        const double near = std::abs(Delta(out.Camera().lookAtU, anchorU));
        out.Zoom(-1.0, true, anchorU, anchorV);
        Check(std::abs(Delta(out.Camera().lookAtU, anchorU)) >= near - 1e-12,
            "zooming out pulled the camera towards the cursor");
    }

    // --- panning has its own sensitivity ---------------------------------
    //
    // Rotating a globe and dragging a map are different gestures with
    // different right answers, and one constant for both means one of them is
    // wrong. panPerPixel is derived so the ground keeps up with the pointer on
    // a flat map; this checks the two constants are actually distinct paths
    // rather than one applied twice.
    {
        presentation::MapCameraController globe = AtGlobe();
        const double globeBefore = globe.Camera().lookAtU;
        globe.Drag(100.0, 0.0);
        const double globeMoved =
            std::abs(Delta(globeBefore, globe.Camera().lookAtU));

        presentation::MapCameraController flat = AtGlobe();
        for (int step = 0; step < 60; ++step)
        {
            flat.Zoom(1.0, false, 0.0, 0.0);
        }
        // Same distance in both, so the comparison is about the constants
        // rather than about the zoom scaling.
        presentation::MapCamera matched = flat.Camera();
        matched.distance = globe.Camera().distance;
        flat.Reset(matched);
        const double flatBefore = flat.Camera().lookAtU;
        flat.Drag(100.0, 0.0);
        const double flatMoved =
            std::abs(Delta(flatBefore, flat.Camera().lookAtU));

        Check(globeMoved > 0.0 && flatMoved > 0.0,
            "one of the two drags did nothing");
        Check(std::abs(globeMoved - flatMoved) > 1e-9,
            "rotating and panning move the camera by the same amount, so "
            "panPerPixel is not being used");
    }

    // --- Flat: drag pans, and the wheel is an ordinary zoom ---------------
    {
        presentation::MapCameraController controller = AtGlobe();
        for (int step = 0; step < 60; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Mode() == presentation::MapCameraMode::Flat,
            "the map is not Flat after zooming all the way in");
        Check(controller.Camera().bend == 0.0, "Flat is not bend 0");

        const double before = controller.Camera().lookAtU;
        controller.Drag(-30.0, 0.0);
        Check(controller.Camera().lookAtU != before,
            "dragging a flat map did not pan it");
    }

    // --- the limits hold at both ends ------------------------------------
    {
        presentation::MapCameraController controller = AtGlobe();
        for (int step = 0; step < 200; ++step)
        {
            controller.Zoom(1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().distance
                >= controller.Limits().nearDistance - 1e-12,
            "the camera zoomed past the near limit");
        for (int step = 0; step < 400; ++step)
        {
            controller.Zoom(-1.0, false, 0.0, 0.0);
        }
        Check(controller.Camera().distance
                <= controller.Limits().farDistance + 1e-12,
            "the camera zoomed past the far limit");
        Check(controller.Camera().bend >= 0.0
                && controller.Camera().bend <= 1.0,
            "the curvature left [0, 1]");
    }

    if (failures != 0)
    {
        std::cerr << "map camera: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "Map camera: passed (three regimes off one continuous bend, "
              << "curvature pinned outside the equatorial band, the seam kept "
              << "off screen, zoom towards the cursor)\n";
    return 0;
}
