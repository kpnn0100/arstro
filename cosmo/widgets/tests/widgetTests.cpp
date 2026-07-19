// Unit tests for the cosmo curve/mixer point editors' HIT-TESTING -- the
// forgiving anchor pick radius (metrics::anchorHitRadius) and the
// nearest-within-radius resolution. These are pure interaction geometry, so
// they run device-free: we drive the widgets' gesture entry point directly and
// observe the emitted control points via their change callbacks. Plain
// assert()-based, matching cosmo/core/tests/sessionTests.cpp (no framework).
//
// Coordinates below are computed against each editor's default geometry:
//   HueCurveEditor: width 300, height 150 -> pxh(hue)=12+hue/360*276,
//                   pyv(y)=70-y*60, anchors at y=0 sit on the mid-line (py=70).
//   CurvePanel:     default mPlotW=232, mPlotY=0 (layout() not called, so the
//                   plot is {kPadX=9.75, 0, 232, kPlotH=164}); anchor (nx,ny)
//                   sits at plot-local (nx*232, 164-ny*164).
// The drawn anchor dot is r=4; the OLD pick radii were 7 (curve) / 11 (mixer),
// now unified to 13, so a click 10-12 px from an anchor must now grab it.

#include "../HueCurveEditor.h"
#include "../CurvePanel.h"
#include "../../Theme.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using artboard::Gesture;
using artboard::Point;
using arstro::CurvePoint;
using arstro::cosmo_v2::CurvePanel;
using arstro::cosmo_v2::HueCurveEditor;

namespace
{
    // Expose the protected gesture entry points for direct driving.
    struct TestHue : HueCurveEditor
    {
        using HueCurveEditor::handleGesture;
    };
    struct TestCurve : CurvePanel
    {
        using CurvePanel::handleGesture;
    };

    Gesture ev(Gesture::Type t)
    {
        Gesture g;
        g.type = t;
        return g;
    }
    bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (ok) { std::printf("  [ok] %s\n", what); }
        else { std::printf("  [FAIL] %s\n", what); ++failures; }
    }

    CurvePoint hp(float x, float y)
    {
        CurvePoint c;
        c.x = x;
        c.y = y;
        return c;
    }

    // ── The mixer hue-curve editor ────────────────────────────────────────────
    void hueEnlargedTargetIsClickable()
    {
        std::printf("HueCurveEditor: enlarged pick radius grabs a near-but-not-on anchor\n");
        TestHue h;
        std::vector<CurvePoint> last;
        int changes = 0;
        h.onChange = [&](const std::vector<CurvePoint> &p) { last = p; ++changes; };

        // Anchor 1 (hue 120) is at pixel (104,70). Press 12 px below it: outside
        // the OLD radius (11) but inside the new (13) -> must grab it.
        const bool grabbed = h.handleGesture(ev(Gesture::Type::Down), Point{104, 82});
        check(grabbed, "Down 12px from the anchor is caught (was a miss at r=11)");

        // Drag it up to y=0.5 (pixel 40) -> the editor emits the moved point.
        h.handleGesture(ev(Gesture::Type::Drag), Point{104, 40});
        check(changes >= 1 && last.size() == 3, "drag emits the control points");
        check(!last.empty() && near(last[1].x, 120.0) && near(last[1].y, 0.5),
              "the grabbed anchor (idx1) moved to y=0.5");
    }

    void hueFarClickMisses()
    {
        std::printf("HueCurveEditor: a click beyond the radius grabs nothing\n");
        TestHue h;
        int changes = 0;
        h.onChange = [&](const std::vector<CurvePoint> &) { ++changes; };

        const bool grabbed = h.handleGesture(ev(Gesture::Type::Down), Point{104, 90}); // 20px away
        check(!grabbed, "Down 20px from the anchor is not caught");
        h.handleGesture(ev(Gesture::Type::Drag), Point{104, 40});
        check(changes == 0, "no point moves when nothing was grabbed");
    }

    void hueNearestWithinRadiusWins()
    {
        std::printf("HueCurveEditor: overlapping targets resolve to the NEAREST anchor\n");
        TestHue h;
        // Two anchors ~11.5px apart: hue 100 -> px 88.667, hue 115 -> px 100.167.
        h.setPoints({hp(100, 0), hp(115, 0), hp(240, 0)});
        std::vector<CurvePoint> last;
        h.onChange = [&](const std::vector<CurvePoint> &p) { last = p; };

        // Press at px 99: 10.3px from idx0, 1.2px from idx1 -- both inside r=13.
        // The OLD "first within radius" would grab idx0; nearest must grab idx1.
        const bool grabbed = h.handleGesture(ev(Gesture::Type::Down), Point{99, 70});
        check(grabbed, "Down between two anchors is caught");
        h.handleGesture(ev(Gesture::Type::Drag), Point{99, 40}); // to y=0.5
        check(last.size() == 3 && near(last[0].x, 100.0) && near(last[0].y, 0.0),
              "the farther anchor (idx0) was NOT grabbed");
        check(!last.empty() && near(last[1].y, 0.5),
              "the nearer anchor (idx1) was grabbed and moved");
    }

    // ── The tone-curve editor (bezier model, same UX as the mixer) ─────────────
    using P = CurvePanel::Points;
    const P kIdentity{CurvePoint{0.f, 0.f}, CurvePoint{1.f, 1.f}};
    CurvePoint cp(float x, float y) { CurvePoint c; c.x = x; c.y = y; return c; }

    void curveEnlargedTargetIsClickable()
    {
        std::printf("CurvePanel: enlarged pick radius grabs a near-but-not-on anchor\n");
        TestCurve c;
        P last; int changes = 0;
        c.onCurveChange = [&](int, P p) { last = p; ++changes; };

        // Endpoint (1,1) at plot-local (232,0) -> widget-local (241.75,0). Press 10px below.
        const bool grabbed = c.handleGesture(ev(Gesture::Type::Down), Point{241.75, 10});
        check(grabbed, "Down 10px from the endpoint is caught");
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 30}); // y = 1 - 30/164
        check(changes >= 1 && last.size() == 2, "drag emits the curve points");
        check(!last.empty() && near(last[1].x, 1.0, 1e-6) && near(last[1].y, 1.0 - 30.0 / 164.0, 1e-3),
              "the grabbed endpoint moved, x locked to 1");
    }

    void curveFarClickMisses()
    {
        std::printf("CurvePanel: a press beyond the radius grabs nothing\n");
        TestCurve c;
        int changes = 0;
        c.onCurveChange = [&](int, P) { ++changes; };
        c.handleGesture(ev(Gesture::Type::Down), Point{241.75, 20});  // 20px from the endpoint
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 40});  // nothing grabbed -> no emit
        check(changes == 0, "a press 20px away grabs nothing (no drag emit)");
    }

    void curveNearestWithinRadiusWins()
    {
        std::printf("CurvePanel: overlapping targets resolve to the NEAREST anchor\n");
        TestCurve c;
        // Interior nodes at x=0.5 (plot px 116) and x=0.55 (plot px 127.6), 11.6px apart.
        c.setCurves({cp(0, 0), cp(0.5f, 0.5f), cp(0.55f, 0.5f), cp(1, 1)},
                    std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
        P last;
        c.onCurveChange = [&](int, P p) { last = p; };

        // Press at plot px 125 (widget 134.75): 9px from idx1, 2.6px from idx2 -> nearest idx2.
        c.handleGesture(ev(Gesture::Type::Down), Point{134.75, 82});
        c.handleGesture(ev(Gesture::Type::Drag), Point{134.75, 62}); // to y = 1 - 62/164
        check(last.size() == 4 && near(last[1].x, 0.5) && near(last[1].y, 0.5),
              "the farther node (idx1) was NOT grabbed");
        check(!last.empty() && near(last[2].y, 1.0 - 62.0 / 164.0, 1e-3),
              "the nearer node (idx2) was grabbed and moved");
    }

    void curvePerChannelIndependence()
    {
        std::printf("CurvePanel: RGB/R/G/B each edit their own independent curve\n");
        TestCurve c;
        int emittedCh = -99;
        c.onCurveChange = [&](int ch, P) { emittedCh = ch; };

        const P master{cp(0, 0), cp(1, 1)};
        const P rC{cp(0, 0), cp(0.5f, 0.9f), cp(1, 1)};
        const P gC{cp(0, 0), cp(0.5f, 0.1f), cp(1, 1)};
        const P bC{cp(0, 0), cp(1, 1)};
        c.setCurves(master, std::array<P, 3>{{rC, gC, bC}});
        check(c.curveFor(0) == master && c.curveFor(1) == rC && c.curveFor(2) == gC && c.curveFor(3) == bC,
              "setCurves stores four independent curves");

        c.showChannel(0);
        c.handleGesture(ev(Gesture::Type::Down), Point{241.75, 10}); // grab the (1,1) endpoint
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 30});
        check(emittedCh == 0, "editing RGB emits channel 0");
        check(c.curveFor(1) == rC && c.curveFor(2) == gC && c.curveFor(3) == bC,
              "editing RGB left R/G/B untouched");

        c.showChannel(1);  // R's mid (0.5,0.9) -> plot (116,16.4) -> widget (125.75,16.4)
        c.handleGesture(ev(Gesture::Type::Down), Point{125.75, 16.4});
        c.handleGesture(ev(Gesture::Type::Drag), Point{125.75, 40});
        check(emittedCh == 1, "editing R emits channel 1");
        check(c.curveFor(1) != rC, "R's own curve changed");
        check(c.curveFor(2) == gC && c.curveFor(3) == bC, "editing R left G/B untouched");
    }

    void curveAltDragMakesSmoothSpline()
    {
        std::printf("CurvePanel: nodes are corners; Alt-drag pulls tangent handles (spline)\n");
        // A plain drag keeps the mid node a CORNER (straight segments, no auto-ease).
        {
            TestCurve c;
            c.setCurves({cp(0, 0), cp(0.5f, 0.5f), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.handleGesture(ev(Gesture::Type::Down), Point{125.75, 82});  // mid (0.5,0.5) at (125.75,82)
            c.handleGesture(ev(Gesture::Type::Drag), Point{125.75, 60});
            check(!c.curveFor(0)[1].smooth, "a plain drag keeps the node a corner");
        }
        // Alt-drag makes the node SMOOTH with symmetric tangent handles.
        {
            TestCurve c;
            c.setCurves({cp(0, 0), cp(0.5f, 0.5f), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            auto altDown = ev(Gesture::Type::Down); altDown.alt = true;
            c.handleGesture(altDown, Point{125.75, 82});
            c.handleGesture(ev(Gesture::Type::Drag), Point{140.0, 60});  // pull the handles out
            const CurvePoint &m = c.curveFor(0)[1];
            check(m.smooth, "Alt-drag makes the node smooth (a spline)");
            check(!near(m.ox, 0.0) || !near(m.oy, 0.0), "tangent handles were pulled out");
            check(near(m.ix, -m.ox) && near(m.iy, -m.oy), "in/out handles are symmetric");
        }
    }

    void curveDoubleClickAddRemove()
    {
        std::printf("CurvePanel: double-click adds a corner / removes an interior node\n");
        TestCurve c;  // default {(0,0),(1,1)}
        c.handleGesture(ev(Gesture::Type::DoubleClick), Point{125.75, 82});  // empty ~ (0.5,0.5)
        check(c.curveFor(0).size() == 3, "double-click on empty space adds a point");
        c.handleGesture(ev(Gesture::Type::DoubleClick), Point{125.75, 82});  // now on that node
        check(c.curveFor(0).size() == 2, "double-click an interior node removes it");
    }
}

int main()
{
    std::printf("cosmo widget hit-test suite (anchor pick radius = %.0f px)\n\n",
                arstro::cosmo_v2::metrics::anchorHitRadius());

    hueEnlargedTargetIsClickable();
    hueFarClickMisses();
    hueNearestWithinRadiusWins();
    curveEnlargedTargetIsClickable();
    curveFarClickMisses();
    curveNearestWithinRadiusWins();
    curvePerChannelIndependence();
    curveAltDragMakesSmoothSpline();
    curveDoubleClickAddRemove();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
