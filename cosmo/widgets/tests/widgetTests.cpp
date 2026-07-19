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

    // ── The tone-curve editor ─────────────────────────────────────────────────
    void curveEnlargedTargetIsClickable()
    {
        std::printf("CurvePanel: enlarged pick radius grabs a near-but-not-on anchor\n");
        TestCurve c;
        std::vector<std::pair<float, float>> last;
        int changes = 0;
        c.onCurveChange = [&](int, std::vector<std::pair<float, float>> p) { last = p; ++changes; };

        // Endpoint (1,1) sits at plot-local (232,0) -> widget-local (241.75,0).
        // Press 10px below it: outside the OLD radius (7), inside the new (13).
        const bool grabbed = c.handleGesture(ev(Gesture::Type::DragStart), Point{241.75, 10});
        check(grabbed, "DragStart 10px from the endpoint is caught (was a miss at r=7)");

        // Drag down; the endpoint's x stays locked to 1, y follows the cursor.
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 30}); // y = 1 - 30/164
        check(changes >= 1 && last.size() == 2, "drag emits the curve points");
        check(!last.empty() && near(last[1].first, 1.0, 1e-6) && near(last[1].second, 1.0 - 30.0 / 164.0, 1e-3),
              "the grabbed endpoint (idx1) moved, x locked to 1");
    }

    void curveFarClickMisses()
    {
        std::printf("CurvePanel: a click beyond the radius grabs nothing\n");
        TestCurve c;
        const bool grabbed = c.handleGesture(ev(Gesture::Type::DragStart), Point{241.75, 20}); // 20px away
        check(!grabbed, "DragStart 20px from the endpoint is not caught");
    }

    void curveNearestWithinRadiusWins()
    {
        std::printf("CurvePanel: overlapping targets resolve to the NEAREST anchor\n");
        TestCurve c;
        // Interior anchors at x=0.5 (plot px 116) and x=0.55 (plot px 127.6), 11.6px apart.
        const CurvePanel::Points idc{{0.f, 0.f}, {1.f, 1.f}};
        c.setCurves({{0.f, 0.f}, {0.5f, 0.5f}, {0.55f, 0.5f}, {1.f, 1.f}},
                    std::array<CurvePanel::Points, 3>{{idc, idc, idc}});
        std::vector<std::pair<float, float>> last;
        c.onCurveChange = [&](int, std::vector<std::pair<float, float>> p) { last = p; };

        // Press at plot px 125 (widget-local 134.75): 9px from idx1, 2.6px from idx2 --
        // both inside r=13. The OLD "first within radius" grabs idx1; nearest grabs idx2.
        const bool grabbed = c.handleGesture(ev(Gesture::Type::DragStart), Point{134.75, 82});
        check(grabbed, "DragStart between two anchors is caught");
        c.handleGesture(ev(Gesture::Type::Drag), Point{134.75, 62}); // to y = 1 - 62/164
        check(last.size() == 4 && near(last[1].first, 0.5) && near(last[1].second, 0.5),
              "the farther anchor (idx1) was NOT grabbed");
        check(!last.empty() && near(last[2].second, 1.0 - 62.0 / 164.0, 1e-3),
              "the nearer anchor (idx2) was grabbed and moved");
    }

    void curvePerChannelIndependence()
    {
        std::printf("CurvePanel: RGB/R/G/B each edit their own independent curve\n");
        using P = CurvePanel::Points;
        TestCurve c;
        int emittedCh = -99;
        c.onCurveChange = [&](int ch, P) { emittedCh = ch; };

        // Four distinct stored curves.
        const P master{{0.f, 0.f}, {1.f, 1.f}};
        const P rC{{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}};
        const P gC{{0.f, 0.f}, {0.5f, 0.1f}, {1.f, 1.f}};
        const P bC{{0.f, 0.f}, {1.f, 1.f}};
        c.setCurves(master, std::array<P, 3>{{rC, gC, bC}});
        check(c.curveFor(0) == master && c.curveFor(1) == rC && c.curveFor(2) == gC && c.curveFor(3) == bC,
              "setCurves stores four independent curves");

        // On RGB (channel 0): dragging the endpoint emits channel 0 and leaves R/G/B alone.
        c.showChannel(0);
        c.handleGesture(ev(Gesture::Type::DragStart), Point{241.75, 10}); // grab the (1,1) endpoint
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 30});
        check(emittedCh == 0, "editing RGB emits channel 0");
        check(c.curveFor(1) == rC && c.curveFor(2) == gC && c.curveFor(3) == bC,
              "editing RGB left R/G/B untouched");

        // Switch to Red: dragging R's mid point (0.5,0.9) emits channel 1 and edits only R.
        c.showChannel(1);
        // (0.5,0.9) -> plot-local (116, 16.4) -> widget-local (125.75, 16.4).
        c.handleGesture(ev(Gesture::Type::DragStart), Point{125.75, 16.4});
        c.handleGesture(ev(Gesture::Type::Drag), Point{125.75, 40});
        check(emittedCh == 1, "editing R emits channel 1");
        check(c.curveFor(1) != rC, "R's own curve changed");
        check(c.curveFor(2) == gC && c.curveFor(3) == bC, "editing R left G/B untouched");
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

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
