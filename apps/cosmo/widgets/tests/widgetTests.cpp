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

#include "../HomeScreen.h"
#include "../MaskOverlay.h"
#include "../HueCurveEditor.h"
#include "../CropGeometry.h"
#include "../CurvePanel.h"
#include "../XformPanel.h"
#include "../ExportDialog.h"
#include "../Filmstrip.h"
#include "../../Theme.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../core/tests/TestMain.h"

using artboard::Gesture;
using artboard::Point;
using arstro::CurvePoint;
using arstro::cosmo_v2::CurvePanel;
using arstro::cosmo_v2::HomeScreen;
using arstro::cosmo_v2::HueCurveEditor;
using arstro::cosmo_v2::MaskOverlay;

namespace
{
    // Expose the protected gesture entry points for direct driving.
    struct TestHue : HueCurveEditor
    {
        using HueCurveEditor::handleGesture;
        // The plot band, so a test can convert pixels to the -1..1 axis the way the widget does
        // instead of restating the constants and drifting from them.
        double plotTopPx() const { return 10.0; }
        double plotBotPx() const { return height.value() - 20.0; }
    };
    struct TestCurve : CurvePanel
    {
        using CurvePanel::handleGesture;
    };
    struct TestHome : HomeScreen
    {
        using HomeScreen::handleGesture;
        using HomeScreen::bottomLinkRect;
        using HomeScreen::actionRect;   // for the min-height assertion
        using HomeScreen::searchRect;   // for the header-row assertion (R-SCALE-3 / R5)
        using HomeScreen::headerTitle;
        using HomeScreen::titleBlockW;
        /** Where the title block ends, drawn the way onPaint draws it: content edge + pad +
         *  the glyph/title/count block. */
        double titleEndX() const { return contentX() + 32.0 + titleBlockW(headerTitle()); }
        /** R-G-1 needs both halves visible to a test: where a card is being drawn NOW versus
         *  where the layout wants it. If only the target were readable, an implementation that
         *  snapped would be indistinguishable from one that eases. */
        artboard::Rect cardLive(int i) const { return cards()[i].live(); }
        artboard::Rect cardTarget(int i) const { return cards()[i].rect; }
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

        // Drag from 82 to 40: the pointer travelled 42 px UP, so the anchor travels 42 px up
        // from where it was — NOT to the pointer.
        //
        // This assertion used to require the anchor to land exactly at the pointer's y, which
        // is what a press 12 px off the anchor teleporting 12 px looks like from the outside.
        // It encoded D-32 as the expected result: the whole point of a forgiving pick radius is
        // that you can grab a small target without being precise, and it is worthless if the
        // target then jumps to wherever you were imprecise.
        h.handleGesture(ev(Gesture::Type::Drag), Point{104, 40});
        check(changes >= 1 && last.size() == 3, "drag emits the control points");
        // Anchor 1 sat at pixel y=70; 42 px up is y=28, and the editor's y axis spans -1..1
        // over (plotBot - plotTop), so convert through the same helper the widget uses.
        const double travelled = 82.0 - 40.0;
        const double expectedY = 0.0 + travelled * 2.0 / (h.plotBotPx() - h.plotTopPx());
        check(!last.empty() && near(last[1].x, 120.0) && near(last[1].y, expectedY, 1e-3),
              "the grabbed anchor moved BY the drag distance, not TO the pointer (D-32)");
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
        // Pressed 10 px below the endpoint and dragged from y=10 to y=30: 20 px DOWN, so the
        // endpoint travels 20 px down from y=0 — it does not jump to the pointer. (Was
        // `1 - 30/164`, the pointer's own position, which is D-32's teleport written down as
        // the expected answer.)
        c.handleGesture(ev(Gesture::Type::Drag), Point{241.75, 30});
        check(changes >= 1 && last.size() == 2, "drag emits the curve points");
        check(!last.empty() && near(last[1].x, 1.0, 1e-6) &&
              near(last[1].y, 1.0 - 20.0 / CurvePanel::kPlotH, 1e-3),
              "the grabbed endpoint moved BY the drag distance, x still locked to 1 (D-32)");
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

    // Count the green (#4cb573) reference strokes in a rendered frame — that colour is
    // unique to the "final" reference curve, so its presence proves it was drawn.
    int greenRefStrokes(artboard::Segment &seg)
    {
        // advance() twice, far apart: the reference fades in over 180 ms (R-G-1), so a render
        // with no clock would report "not drawn" for every case and the check would pass for
        // the wrong reason.
        seg.advance(0.0);
        seg.advance(400.0);
        artboard::RecordingTarget t; seg.render(t);
        int n = 0;
        for (const auto &op : t.ops())
            if (op.kind == artboard::DrawOp::Kind::SetStroke &&
                near(op.color.r, 0.298, 0.02) && near(op.color.g, 0.710, 0.02) && near(op.color.b, 0.451, 0.02))
                ++n;
        return n;
    }

    // The widest stroke of a given colour in a rendered frame. The reference must be drawn
    // THINNER than the curve being edited: same-weight is what made them indistinguishable.
    double strokeWidthOf(artboard::Segment &seg, double r, double g, double b)
    {
        seg.advance(0.0); seg.advance(400.0);
        artboard::RecordingTarget t; seg.render(t);
        double w = 0.0;
        for (const auto &op : t.ops())
            if (op.kind == artboard::DrawOp::Kind::SetStroke &&
                near(op.color.r, r, 0.02) && near(op.color.g, g, 0.02) && near(op.color.b, b, 0.02))
                w = std::max(w, op.width);
        return w;
    }

    /** Like greenRefStrokes, but advanced to a caller-chosen clock — the D-31 assertions are
     *  about WHEN the readout is on screen, so they cannot use a helper that always settles. */
    int greenRefStrokes2(artboard::Segment &seg, double nowMs)
    {
        seg.advance(nowMs);
        artboard::RecordingTarget t; seg.render(t);
        int n = 0;
        for (const auto &op : t.ops())
            if (op.kind == artboard::DrawOp::Kind::SetStroke &&
                near(op.color.r, 0.298, 0.02) && near(op.color.g, 0.710, 0.02) &&
                near(op.color.b, 0.451, 0.02) && op.color.a > 0.02)
                ++n;
        return n;
    }

    void curveReferenceShownWhenDiffers()
    {
        std::printf("CurvePanel: the green 'final' curve is drawn only when it differs from own\n");
        {   // reference (group-stacked final) differs from the own curve -> green drawn
            TestCurve c;
            c.setCurves({cp(0, 0), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves({cp(0, 0), cp(0.5f, 0.8f), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            check(greenRefStrokes(c) >= 1, "green 'final' curve drawn when it differs from own");
        }
        {   // reference equals own (no group contribution) -> no green
            TestCurve c;
            c.setCurves({cp(0, 0), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves({cp(0, 0), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            check(greenRefStrokes(c) == 0, "no green when the final equals the own curve");
        }
    }

    // ── D-32: a grab moves the node BY the drag, never TO the pointer ────────────────────
    //
    // Reported as "when i click slightly off the main curve it will select the green curve and
    // it switch to that curve", with a screenshot of an edited curve zig-zagging through the
    // green readout's inflections. Both halves of that sentence come from one line of code:
    // `mDragKind == 0` wrote `nx(pl.x)` / `ny(pl.y)` — the pointer's own position — into the
    // node. The pick radius is a forgiving 13 px, so pressing anywhere within 13 px of a node
    // grabbed it and the first pixel of movement teleported it up to 13 px, straight to where
    // the user had clicked. If they were aiming near the readout, the node landed ON the
    // readout and their curve visibly snapped onto it.
    //
    // Swept over the whole radius, because the size of the jump IS the bug: a spot check at
    // offset 0 passes on the broken code.
    void curveGrabMovesByTheDragNotToThePointer()
    {
        std::printf("CurvePanel: a grab moves the node by the drag, not to the pointer (D-32)\n");
        const double W = 232.0, H = CurvePanel::kPlotH, padX = 9.75;   // no layout(): see the header note
        const P own{cp(0, 0), cp(0.5f, 0.55f), cp(1, 1)};
        const double nodeX = padX + 0.5 * W, nodeY = H - 0.55 * H;
        const double kDrag = 6.0;    // pixels the pointer travels, upward

        int teleported = 0, missed = 0;
        for (double off = 0.0; off <= 12.0; off += 1.0)
        {
            TestCurve c;
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            const Point press{nodeX, nodeY - off};
            if (!c.handleGesture(ev(Gesture::Type::Down), press)) { ++missed; continue; }
            c.handleGesture(ev(Gesture::Type::Drag), Point{press.x, press.y - kDrag});
            const double afterY = H - c.curveFor(0)[1].y * H;
            // The node started at nodeY and the pointer moved kDrag up, so it must be exactly
            // kDrag up. On the broken code it lands at press.y - kDrag, i.e. off px further.
            if (std::fabs(afterY - (nodeY - kDrag)) > 0.51) ++teleported;
        }
        check(missed == 0, "every press inside the 13 px radius grabs the node");
        check(teleported == 0,
              "and the node travels exactly the drag distance from EVERY grab offset — it is "
              "never yanked to the pointer");

        // The forgiving radius still has an edge: beyond it, nothing is grabbed and nothing moves.
        {
            TestCurve c;
            int changes = 0;
            c.onCurveChange = [&](int, P) { ++changes; };
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.handleGesture(ev(Gesture::Type::Down), Point{nodeX, nodeY - 20.0});
            c.handleGesture(ev(Gesture::Type::Drag), Point{nodeX, nodeY - 26.0});
            check(changes == 0 && near(c.curveFor(0)[1].y, 0.55f, 1e-4),
                  "a press beyond the radius still grabs and moves nothing");
        }
    }

    // D-28 / D-31: the "final, with group" line is a READOUT — shown, never interacted with.
    //
    // D-28 asserted this at ONE point and passed, and the line was still reachable: the point I
    // picked happened to be one of the three (of 21) along the reference where nothing happens.
    // That is the same mistake the home header row's test was written to avoid — a threshold
    // proves nothing about a spot check — so this SWEEPS the reference and reports the count.
    //
    // What the sweep found, and what the fix therefore had to be: a double-click on the green
    // line adds a corner exactly ON it at 18 of 21 points, so the edited curve snaps to touch
    // the reference and the user has, to all appearances, grabbed and dragged it. Nothing about
    // the reference's own geometry was wrong — it owns no nodes and is not hit-tested — the
    // problem was that it was on screen to be aimed at. So it leaves while the plot is being
    // worked in (D-31), and THAT is what these assertions check: not "the gesture did nothing"
    // (a double-click adding a corner is the documented editing model) but "the reference was
    // not on screen to be aimed at while the gesture happened".
    void curveReferenceIsNotEditable()
    {
        std::printf("CurvePanel: the green 'final' curve leaves while the plot is worked in (D-31)\n");
        const P own{cp(0, 0), cp(0.35f, 0.55f), cp(0.7f, 0.75f), cp(1, 1)};
        const P ref{cp(0, 0), cp(0.25f, 0.10f), cp(0.5f, 0.30f), cp(0.75f, 0.62f), cp(1, 1)};

        // 1) At rest, with a group contributing, the readout is drawn.
        {
            TestCurve c;
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves(ref, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            check(greenRefStrokes(c) > 5, "at rest the dashed readout is on screen");
        }

        // 2) Sweep the reference: after a press anywhere in the plot, it is gone — so there is
        //    nothing to aim at, at ANY point along it, for the whole gesture.
        int visibleDuringPress = 0;
        const int kSamples = 21;
        for (int i = 0; i < kSamples; ++i)
        {
            const double tx = (double)i / (kSamples - 1);
            const auto dense = arstro::curve::sample(ref, false, 0.f);
            double ry = 0.0, best = 1e9;
            for (const auto &d : dense)
                if (std::fabs(d.first - tx) < best) { best = std::fabs(d.first - tx); ry = d.second; }
            // plot-local -> segment-local: mPlotW is 232 and mPlotY 0 with no layout() (see the
            // note at the top of this file), and kPadX is 9.75.
            const Point onGreen{9.75 + tx * 232.0, CurvePanel::kPlotH - ry * CurvePanel::kPlotH};

            TestCurve c;
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves(ref, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.advance(0.0); c.advance(400.0);              // let it fade in
            c.handleGesture(ev(Gesture::Type::Down), onGreen);
            c.advance(500.0); c.advance(700.0);            // and out again, on the press
            if (greenRefStrokes2(c, 700.0) > 0) ++visibleDuringPress;
        }
        check(visibleDuringPress == 0,
              "and after a press it is gone at EVERY point along it, not just the one I checked first");

        // 3) It comes back after the gesture — a readout that hides for good is not a readout.
        {
            TestCurve c;
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves(ref, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.advance(0.0); c.advance(400.0);
            c.handleGesture(ev(Gesture::Type::Down), Point{125.75, 82.0});
            c.advance(500.0);
            c.handleGesture(ev(Gesture::Type::Up), Point{125.75, 82.0});
            // 700 ms, not 560: the fade-OUT is 110 ms from the press at 500, so a query at 560
            // catches it mid-fade and still sees green. The claim is about the HOLD (reveal at
            // 500 + 220 = 720), so ask after the fade has finished and before the reveal is due.
            check(greenRefStrokes2(c, 700.0) == 0,
                  "it stays away through the hold, so a double-click cannot flash it");
            // Two clocks: the frame the reveal becomes due only STARTS the 180 ms fade, so its
            // value is still 0 on that frame. Asking once at 1200 measured the frame the
            // animation began, which is a fade-in working correctly and reads as a failure.
            greenRefStrokes2(c, 1200.0);
            check(greenRefStrokes2(c, 1500.0) > 5, "and it is back once the gesture is over");
        }

        // 4) The press itself still edits only the user's own curve. Swept for the same reason:
        //    the reference and the edited curve SHARE their endpoints, so a press at the green
        //    line's ends legitimately grabs the user's own endpoint node — that is their node,
        //    and the sweep records where it happens instead of pretending it does not.
        int emittedTotal = 0, grabbedOwnNode = 0, addedNodes = 0;
        for (int i = 0; i < kSamples; ++i)
        {
            const double tx = (double)i / (kSamples - 1);
            const auto dense = arstro::curve::sample(ref, false, 0.f);
            double ry = 0.0, best = 1e9;
            for (const auto &d : dense)
                if (std::fabs(d.first - tx) < best) { best = std::fabs(d.first - tx); ry = d.second; }
            const Point onGreen{9.75 + tx * 232.0, CurvePanel::kPlotH - ry * CurvePanel::kPlotH};

            TestCurve c;
            int emitted = 0;
            c.onCurveChange = [&](int, P) { ++emitted; };
            c.setCurves(own, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.setReferenceCurves(ref, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
            c.handleGesture(ev(Gesture::Type::Down), onGreen);
            c.handleGesture(ev(Gesture::Type::Drag), Point{onGreen.x, onGreen.y - 25.0});
            c.handleGesture(ev(Gesture::Type::Up), Point{onGreen.x, onGreen.y - 25.0});
            emittedTotal += emitted;
            if (emitted > 0) ++grabbedOwnNode;
            if (c.curveFor(0).size() != own.size()) ++addedNodes;   // one verdict for the sweep
        }
        check(addedNodes == 0, "no press on the reference ADDS a node to the edited curve");
        check(grabbedOwnNode <= 4,
              "and only the few samples within 13 px of an OWN node move anything — the two "
              "curves share their endpoints, so those presses grab the user's own node");
        (void)emittedTotal;
    }

    void curveReferenceLooksLikeAReadout()
    {
        std::printf("CurvePanel: the 'final' line is dashed and thinner than the edited curve (D-28)\n");
        TestCurve c;
        c.setCurves({cp(0, 0), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});
        c.setReferenceCurves({cp(0, 0), cp(0.5f, 0.8f), cp(1, 1)}, std::array<P, 3>{{kIdentity, kIdentity, kIdentity}});

        const double refW = strokeWidthOf(c, 0.298, 0.710, 0.451);   // #4cb573, the reference
        const artboard::Color accent = arstro::cosmo_v2::palette::primary();  // the edited RGB curve
        const double curveW = strokeWidthOf(c, accent.r, accent.g, accent.b);
        check(refW > 0.0 && curveW > 0.0, "both lines are drawn");
        check(refW < curveW, "the reference is drawn thinner than the curve being edited");
        // Dashed: one solid polyline is a single stroked path, so many green strokes in one
        // frame is what dashing looks like from the outside.
        check(greenRefStrokes(c) > 5, "the reference is dashed, not a solid line");
    }

    // ── ExportDialog tree propagation (R-EXPORT-2) ────────────────────────────
    //
    // The four rules the requirement spells out, driven through the widget's own
    // model surface (the same calls handleGesture makes for a row click):
    //   select parent   -> select all children
    //   deselect parent -> deselect all children
    //   deselect child  -> untick parent
    //   select all children -> tick parent
    // Group state is DERIVED (0 = none, 1 = all, 2 = indeterminate), so a stored
    // flag can never drift out of step with the leaves.

    using arstro::cosmo_v2::ExportDialog;

    // Rows for the fixture, in the order show() flattens them (pre-order):
    //   0 Rivers        (group)
    //   1   river_01    slot 0
    //   2   river_02    slot 1
    //   3   Dusk        (group, nested)
    //   4     dusk_a    slot 2
    //   5     dusk_b    slot 3
    //   6 loose.jpg     slot 4
    enum { kRivers = 0, kR1, kR2, kDusk, kD1, kD2, kLoose };

    std::shared_ptr<ExportDialog> makeTree()
    {
        std::vector<ExportDialog::Node> n;
        auto add = [&](int parent, bool group, int slot, const char *name, const char *path) {
            ExportDialog::Node e;
            e.parent = parent; e.group = group; e.slot = slot; e.name = name; e.sourcePath = path;
            n.push_back(e);
            return (int)n.size() - 1;
        };
        const int rivers = add(-1, true, -1, "Rivers", "");
        add(rivers, false, 0, "river_01.jpg", "/pics/rivers/river_01.jpg");
        add(rivers, false, 1, "river_02.jpg", "/pics/rivers/river_02.jpg");
        const int dusk = add(rivers, true, -1, "Dusk", "");
        add(dusk, false, 2, "dusk_a.jpg", "/pics/rivers/dusk_a.jpg");
        add(dusk, false, 3, "dusk_b.jpg", "/pics/rivers/dusk_b.jpg");
        add(-1, false, 4, "loose.jpg", "/pics/loose.jpg");

        auto d = std::make_shared<ExportDialog>(arstro::cosmo_v2::palette::primary());
        d->width.set(1280); d->height.set(800);
        d->show(std::move(n), {});          // empty preselect == everything ticked
        return d;
    }

    void exportTreeStartsFullySelected()
    {
        auto d = makeTree();
        check(d->rowCount() == 7, "every node is a visible row (groups start expanded)");
        check(d->rowState(kRivers) == 1, "a fully-ticked group reads ticked");
        check(d->rowState(kDusk) == 1, "a fully-ticked nested group reads ticked");
        check(d->selectedSlots().size() == 5, "all five image leaves are selected");
    }

    void exportDeselectParentClearsChildren()
    {
        auto d = makeTree();
        d->toggleRow(kRivers);   // ticked -> untick the whole subtree
        check(d->rowState(kRivers) == 0, "deselecting a parent leaves it unticked");
        check(d->rowState(kR1) == 0 && d->rowState(kR2) == 0, "its direct children are unticked");
        check(d->rowState(kDusk) == 0, "its nested group is unticked");
        check(d->rowState(kD1) == 0 && d->rowState(kD2) == 0, "the nested group's leaves too");
        check(d->rowState(kLoose) == 1, "a sibling outside the subtree is untouched");
        check(d->selectedSlots().size() == 1, "only the loose image is left selected");
    }

    void exportSelectParentSelectsChildren()
    {
        auto d = makeTree();
        d->toggleRow(kRivers);   // clear it first
        d->toggleRow(kRivers);   // ...then select the parent again
        check(d->rowState(kRivers) == 1, "selecting a parent ticks it");
        check(d->rowState(kR1) == 1 && d->rowState(kR2) == 1, "every direct child is ticked");
        check(d->rowState(kD1) == 1 && d->rowState(kD2) == 1, "every nested descendant is ticked");
        check(d->selectedSlots().size() == 5, "the whole tree is selected again");
    }

    void exportDeselectChildUnticksParent()
    {
        auto d = makeTree();
        d->toggleRow(kD2);       // untick ONE leaf deep in the tree
        check(d->rowState(kD2) == 0, "the clicked leaf is unticked");
        check(d->rowState(kDusk) == 2, "its parent group goes indeterminate, not ticked");
        check(d->rowState(kRivers) == 2, "the grandparent goes indeterminate too");
        check(d->rowState(kRivers) != 1, "an ancestor is never ticked while a descendant is not");
        check(d->selectedSlots().size() == 4, "one image dropped out of the selection");
    }

    void exportSelectingLastChildTicksParent()
    {
        auto d = makeTree();
        d->toggleRow(kD1);
        d->toggleRow(kD2);       // both nested leaves off -> the group is empty
        check(d->rowState(kDusk) == 0, "a group with no ticked descendants reads unticked");
        check(d->rowState(kRivers) == 2, "the grandparent still has river_01/02 -> indeterminate");
        d->toggleRow(kD1);
        check(d->rowState(kDusk) == 2, "one of two ticked -> indeterminate");
        d->toggleRow(kD2);       // ...and now the LAST child
        check(d->rowState(kDusk) == 1, "ticking the last child ticks the parent");
        check(d->rowState(kRivers) == 1, "and the ancestor above it, once it is complete");
    }

    void exportMasterToggleAndCollapse()
    {
        auto d = makeTree();
        d->setAllChecked(false);
        check(d->selectedSlots().empty(), "Select none clears every leaf");
        check(d->rowState(kRivers) == 0, "groups follow the leaves down");
        d->setAllChecked(true);
        check(d->selectedSlots().size() == 5, "Select all ticks every leaf");

        d->toggleExpand(kRivers);
        check(d->rowCount() == 2, "collapsing a group hides its whole subtree (Rivers + loose)");
        check(d->selectedSlots().size() == 5, "collapsing changes visibility, never selection");
        d->toggleExpand(kRivers);
        check(d->rowCount() == 7, "re-expanding restores every row");
    }

    void exportSelectedSlotsAreInTreeOrder()
    {
        auto d = makeTree();
        const std::vector<int> slots = d->selectedSlots();
        check(slots.size() == 5, "five slots");
        for (size_t i = 1; i < slots.size(); ++i)
            check(slots[i - 1] < slots[i], "slots come back in tree (pre-order) order");
    }

    // ── ExportDialog: drag (R-EXPORT-8) and the export beats (R-EXPORT-6) ───────

    void feedDialog(std::shared_ptr<ExportDialog> d, Gesture::Type type, double x, double y)
    {
        Gesture g; g.type = type; g.pos = Point{x, y}; g.start = Point{x, y};
        d->onGesture(g);
    }
    void tickDialog(std::shared_ptr<ExportDialog> d, int frames, double &now)
    {
        std::shared_ptr<artboard::Segment> seg = d;
        for (int i = 0; i < frames; ++i) { seg->advance(now); now += 16.0; }
    }

    /** Press the real Export button through the gesture path. Its y depends on the card
     *  height, so sweep UPWARD from the bottom of the window — the footer is the card's
     *  lowest band, so the first click that lands inside it is the button. A miss lands
     *  outside and closes the dialog, so rebuild and carry on. */
    void pressExport(std::shared_ptr<ExportDialog> &d, double &now,
                     std::function<void(ExportDialog::Request)> onExport = {},
                     std::function<void(std::shared_ptr<ExportDialog> &)> reseed = {})
    {
        const double bx = (1280.0 + 560.0) * 0.5 - 80.0;
        for (double y = 798.0; y > 400.0; y -= 2.0)
        {
            feedDialog(d, Gesture::Type::Click, bx, y);
            if (d->isExporting()) return;
            if (!d->isOpen())
            {
                // A probe miss dismissed it; rebuild AND restore whatever the caller set
                // up on the dialog, or the assertions would run against a bare one.
                d = makeTree();
                if (onExport) d->onExport = onExport;
                if (reseed) reseed(d);
                tickDialog(d, 30, now);
            }
        }
    }

    void exportDragMovesTheCard()
    {
        double now = 0.0;
        auto d = makeTree();
        tickDialog(d, 30, now);
        // The window centre starts INSIDE the card, so clicking there does not dismiss.
        feedDialog(d, Gesture::Type::Click, 640.0, 400.0);
        check(d->isOpen(), "a click inside the centred card does not close it");

        // Drag the card far to the right by its header, then the same point is outside.
        feedDialog(d, Gesture::Type::Down, 640.0, 30.0);   // header band
        feedDialog(d, Gesture::Type::Move, 900.0, 30.0);
        feedDialog(d, Gesture::Type::Up,   900.0, 30.0);
        feedDialog(d, Gesture::Type::Click, 900.0, 30.0);  // the click a drag ends with
        check(d->isOpen(), "the click that terminates a drag is swallowed, not acted on");
        tickDialog(d, 4, now);
        feedDialog(d, Gesture::Type::Click, 400.0, 400.0);
        check(!d->isOpen(), "after the drag, a point the card used to cover is outside it");
    }

    void exportDragIsClamped()
    {
        double now = 0.0;
        auto d = makeTree();
        tickDialog(d, 30, now);
        // Fling it far past the window edge; the header must still be grabbable, which
        // means a drag from the clamped position still works.
        feedDialog(d, Gesture::Type::Down, 640.0, 30.0);
        feedDialog(d, Gesture::Type::Move, 9000.0, 9000.0);
        feedDialog(d, Gesture::Type::Up, 9000.0, 9000.0);
        tickDialog(d, 4, now);
        check(d->isOpen(), "a wild drag never dismisses the dialog");
        // Grab the header at its clamped resting place and pull it back to centre.
        feedDialog(d, Gesture::Type::Down, 1200.0, 780.0);
        feedDialog(d, Gesture::Type::Move, 640.0, 30.0);
        feedDialog(d, Gesture::Type::Up, 640.0, 30.0);
        tickDialog(d, 4, now);
        feedDialog(d, Gesture::Type::Click, 640.0, 400.0);
        check(d->isOpen(), "the card can be dragged back into view after being flung");
    }

    void exportBeatsRunToCompletionAndClose()
    {
        double now = 0.0;
        auto d = makeTree();
        tickDialog(d, 30, now);
        check(!d->isExporting(), "not exporting until the button is pressed");
        pressExport(d, now);
        check(d->isExporting(), "pressing Export enters the progress face");
        tickDialog(d, 30, now);                     // let the collapse settle

        // Partial progress keeps the dialog up and writing.
        d->setExportProgress(2, 5, "river_02.jpg");
        tickDialog(d, 20, now);
        check(d->isOpen() && d->isExporting(), "mid-batch the dialog stays open");

        // The last file switches to the confirmation, which holds before dismissing.
        d->setExportProgress(5, 5, "");
        tickDialog(d, 20, now);
        check(d->isOpen(), "the confirmation is shown, not skipped");
        tickDialog(d, 80, now);                     // > the ~950ms hold + the close fade
        check(!d->isOpen(), "the dialog closes itself once the confirmation has been read");
    }

    void exportAnimationPlaysBeforeAnyExportWork()
    {
        // The crux of "animate first, export later" (R-EXPORT-6 beat 1): pressing
        // Export must start the collapse and hand the host NOTHING until that tween has
        // finished, so the first full-resolution render can never stall the animation.
        double now = 0.0;
        auto d = makeTree();
        int fired = 0;
        std::vector<int> firedSlots;
        auto cb = [&](ExportDialog::Request r) { ++fired; firedSlots = r.slots; };
        d->onExport = cb;
        tickDialog(d, 30, now);
        pressExport(d, now, cb);
        check(d->isExporting(), "the collapse starts immediately");
        check(fired == 0, "no export work is handed over on the click itself");

        tickDialog(d, 6, now);            // ~96ms into the 260ms collapse
        check(fired == 0, "still nothing while the collapse is mid-flight");

        tickDialog(d, 20, now);           // past the end of the collapse
        check(fired == 1, "the batch is handed over exactly once, after the collapse");
        check(firedSlots.size() == 5, "the request carries the ticked images");

        tickDialog(d, 30, now);
        check(fired == 1, "and it is never handed over a second time");
    }

    void exportRequestIsSnapshotAtPressTime()
    {
        // The request is captured when Export is pressed, so the collapse (which stops
        // drawing the picker) cannot change what gets written.
        double now = 0.0;
        auto d = makeTree();
        ExportDialog::Request got;
        int fired = 0;
        auto cb = [&](ExportDialog::Request r) { ++fired; got = std::move(r); };
        d->onExport = cb;
        tickDialog(d, 30, now);
        d->toggleRow(kD2);                // untick one leaf -> 4 of 5
        pressExport(d, now, cb, [](std::shared_ptr<ExportDialog> &nd) { nd->toggleRow(kD2); });
        tickDialog(d, 40, now);
        check(fired == 1, "handed over once");
        check(got.slots.size() == 4, "the snapshot honours the ticks at press time");
    }

    void exportProgressIgnoresInputAndReopensClean()
    {
        double now = 0.0;
        auto d = makeTree();
        tickDialog(d, 30, now);
        pressExport(d, now);
        tickDialog(d, 30, now);
        // Modal + non-cancellable: neither a click outside nor Escape abandons a batch.
        feedDialog(d, Gesture::Type::Click, 20.0, 20.0);
        check(d->isOpen(), "a click outside cannot abandon a running export");
        artboard::KeyEvent esc; esc.type = artboard::KeyEvent::Type::Down; esc.keyCode = 27;
        d->dispatchKey(esc);
        check(d->isOpen(), "Escape cannot abandon a running export");

        // Re-opening returns a clean, centred picker.
        d->cancelExport();
        tickDialog(d, 20, now);
        check(!d->isExporting(), "cancelExport returns to the form");
    }

    // ── Filmstrip: the rack as a scroll view (R-BROWSE-1/2) ───────────────────

    using arstro::cosmo_v2::Filmstrip;

    std::shared_ptr<Filmstrip> makeStrip(int photos, double viewW)
    {
        auto f = std::make_shared<Filmstrip>();
        f->width.set(viewW);
        f->height.set(Filmstrip::kHeight);
        std::vector<Filmstrip::Cell> cells;
        for (int i = 0; i < photos; ++i)
            cells.push_back({false, i + 1, -1, "p" + std::to_string(i) + ".jpg", 0, false, false});
        f->setCells(std::move(cells));
        return f;
    }
    // The eased scroll settles over a few frames; read it after it has.
    double settledScroll(std::shared_ptr<Filmstrip> f, double &now)
    {
        std::shared_ptr<artboard::Segment> seg = f;
        for (int i = 0; i < 40; ++i) { seg->advance(now); now += 16.0; }
        // cellX(0) is kPadX minus the scroll offset, so the offset is recoverable.
        return 9.75 - f->cellXForTest(0);
    }

    void filmstripScrollsIntoViewBothWays()
    {
        double now = 0.0;
        auto f = makeStrip(20, 400.0);          // ~4 cells visible of 20
        settledScroll(f, now);
        check(settledScroll(f, now) < 1.0, "a fresh rack starts at the left edge");

        f->scrollCellIntoView(19);              // the last photo
        const double atEnd = settledScroll(f, now);
        check(atEnd > 1.0, "walking to the end scrolls the rack");

        f->scrollCellIntoView(0);               // back to the first
        check(settledScroll(f, now) < 1.0, "and walking back scrolls it home again");
    }

    void filmstripScrollIsClampedAtBothEnds()
    {
        double now = 0.0;
        auto f = makeStrip(20, 400.0);
        f->scrollBy(-100000.0);                 // fling right, far past the end
        const double maxed = settledScroll(f, now);
        f->scrollBy(-100000.0);
        check(std::fabs(settledScroll(f, now) - maxed) < 0.5, "cannot scroll past the last photo");
        f->scrollBy(100000.0);                  // fling back left
        check(settledScroll(f, now) < 1.0, "cannot scroll before the first photo");
    }

    void filmstripKeepsScrollWhenTheSameListIsRepushed()
    {
        // The regression that broke arrow navigation outright: the same cell list gets
        // re-pushed on EVERY selection change (and once per photo while a project
        // streams in), and setCells was resetting the scroll each time — so the rack
        // snapped back to the start on every arrow press.
        double now = 0.0;
        auto f = makeStrip(20, 400.0);
        f->scrollCellIntoView(19);
        const double scrolled = settledScroll(f, now);
        check(scrolled > 1.0, "walked to the end");

        std::vector<Filmstrip::Cell> same;
        for (int i = 0; i < 20; ++i)
            same.push_back({false, i + 1, -1, "p" + std::to_string(i) + ".jpg", 0, false, false});
        f->setCells(same);
        check(std::fabs(settledScroll(f, now) - scrolled) < 0.5,
              "re-pushing the same list keeps the scroll where it was");

        // A genuinely different list (drilling into a group) IS a fresh view.
        std::vector<Filmstrip::Cell> other;
        for (int i = 0; i < 5; ++i)
            other.push_back({false, 100 + i, -1, "q.jpg", 0, false, false});
        f->setCells(other);
        check(settledScroll(f, now) < 1.0, "a different list starts at the left edge again");
    }

    void filmstripLandsTheCellAtTheEdgeItCameFrom()
    {
        // R-BROWSE-2: an off-screen cell is brought flush to the edge it entered from —
        // not centred, and not over-scrolled.
        double now = 0.0;
        auto f = makeStrip(20, 400.0);
        settledScroll(f, now);
        const int firstHidden = [&] {
            for (int i = 0; i < 20; ++i) if (!f->cellFullyVisible(i)) return i;
            return -1;
        }();
        check(firstHidden > 0, "with 20 photos in a 400px rack something is off-screen");

        f->scrollCellIntoView(firstHidden);
        settledScroll(f, now);
        check(f->cellFullyVisible(firstHidden), "it is now fully shown");
        // Flush against the right edge: its right side sits at the viewport edge (minus
        // the strip's own padding), so nothing beyond it is revealed.
        const double right = f->cellXForTest(firstHidden) + 86.0;
        check(std::fabs(right - (400.0 - 9.75)) < 1.5, "and it landed AT the edge, not in the middle");

        // A cell already fully in view must not move the rack at all.
        const double before = settledScroll(f, now);
        f->scrollCellIntoView(firstHidden);
        check(std::fabs(settledScroll(f, now) - before) < 0.5,
              "an already-visible cell scrolls nothing");
    }

    void filmstripShortRackNeverScrolls()
    {
        double now = 0.0;
        auto f = makeStrip(2, 800.0);           // everything already fits
        f->scrollCellIntoView(1);
        check(settledScroll(f, now) < 1.0, "a rack that fits its viewport stays put");
    }

    // ── home sidebar links (R-SETTINGS-5, amending R-HOME-8) ──
    // Settings became live so the preferences that decide how a project LOADS can be
    // set before one is open. The other two stay reserved, and BOTH facts matter: a
    // link that silently did nothing is what R-HOME-8 originally specified, and a
    // reserved link must still swallow its click rather than let it reach the grid.
    std::shared_ptr<TestHome> makeHome(double w = 1440.0, double h = 900.0)
    {
        auto home = std::make_shared<TestHome>();
        home->width.set(w);
        home->height.set(h);
        home->layout();
        return home;
    }
    Point centreOf(const artboard::Rect &r) { return Point{r.x + r.w * 0.5, r.y + r.h * 0.5}; }

    void homeSettingsLinkOpensSettings()
    {
        auto home = makeHome();
        int opened = 0;
        home->onSettings = [&opened] { ++opened; };
        const bool consumed = home->handleGesture(ev(Gesture::Type::Click),
                                                  centreOf(home->bottomLinkRect(0)));
        check(consumed, "the Settings link consumes its click");
        check(opened == 1, "clicking Settings on the home screen opens the settings surface");
    }

    void homeReservedLinksStaySilentButSwallowTheClick()
    {
        auto home = makeHome();
        int opened = 0;
        home->onSettings = [&opened] { ++opened; };
        for (int i = 1; i <= 2; ++i)   // What's New, Help & Documentation
        {
            const bool consumed = home->handleGesture(ev(Gesture::Type::Click),
                                                      centreOf(home->bottomLinkRect(i)));
            check(consumed, "a reserved link still swallows its click");
        }
        check(opened == 0, "only Settings is live; the reserved links do nothing (R-HOME-8)");
    }

    void homeSettingsLinkIsReachableAtASmallWindow()
    {
        // R4: the link block is pinned to the sidebar bottom, so a short window is where
        // it would collide with the version line or run off the bottom edge.
        auto home = makeHome(1024.0, 640.0);
        const artboard::Rect r = home->bottomLinkRect(0);
        check(r.y >= 0.0 && r.y + r.h <= 640.0, "the Settings link is inside a 1024x640 window");
        int opened = 0;
        home->onSettings = [&opened] { ++opened; };
        home->handleGesture(ev(Gesture::Type::Click), centreOf(r));
        check(opened == 1, "and it is still clickable there");
    }

    // ── R-G-1: the grid reflow travels, it does not snap ──────────────────────────────
    // The column count changes DISCRETELY as the window resizes — 4 across becomes 3 at one
    // particular width — so before this every card's size and position jumped at that instant.
    // R-G-1 admits no exception: "no component may suddenly change size, appear, disappear,
    // move, recolor, or reflow in a single frame."
    void homeGridReflowIsAnimated()
    {
        auto home = makeHome(1440.0, 900.0);
        std::vector<HomeScreen::CardInfo> cards(6);
        for (int i = 0; i < 6; ++i) { cards[i].name = "p" + std::to_string(i); cards[i].recentIndex = i; }
        home->setRecents(cards);
        home->advance(0.0);
        home->layout();
        home->advance(16.0);

        const artboard::Rect before = home->cardLive(0);
        check(before.w > 0.0, "the first layout places a card at once (no fly-in on open)");
        check(std::fabs(before.w - home->cardTarget(0).w) < 0.01,
              "and it starts AT its target, not eased toward it");

        // Squeeze the window so the grid must drop a column.
        home->width.set(760.0);
        home->layout();
        home->advance(32.0);

        const artboard::Rect target = home->cardTarget(0);
        const artboard::Rect live = home->cardLive(0);
        check(std::fabs(target.w - before.w) > 1.0, "the narrower window really did change the card size");
        check(std::fabs(live.w - target.w) > 0.5,
              "R-G-1: one frame after the reflow the card is still on its way, not already there");
        check(std::fabs(live.w - before.w) < std::fabs(target.w - before.w),
              "and it has started moving rather than sitting still");

        // Let the ease finish; it must ARRIVE, not stall short of the target.
        home->advance(32.0 + 400.0);
        const artboard::Rect settled = home->cardLive(0);
        check(std::fabs(settled.w - target.w) < 0.01, "and it arrives exactly at the target");
        check(std::fabs(settled.x - target.x) < 0.01 && std::fabs(settled.y - target.y) < 0.01,
              "in both axes");
    }

    // ── the window minimum is summed from the layout, not guessed ─────────────────────
    // The action buttons are anchored to the sidebar's TOP and the Settings / What's New /
    // Help & Documentation links to its BOTTOM, so a window shorter than their sum overlaps
    // them. A hardcoded 400 px minimum did exactly that and could never notice the layout
    // changing underneath it.
    void homeMinimumHeightClearsBothAnchoredBlocks()
    {
        const double minH = HomeScreen::minContentHeight();
        auto home = makeHome(HomeScreen::minContentWidth(), minH);

        // The lowest action button must finish above the highest bottom link, with air.
        const artboard::Rect lastAction = home->actionRect(2);
        const artboard::Rect firstLink = home->bottomLinkRect(0);
        const double actionsBottom = lastAction.y + lastAction.h;
        check(firstLink.y > actionsBottom,
              "at the minimum height the links start BELOW the action buttons");
        check(firstLink.y - actionsBottom > 8.0, "with visible air, not merely non-overlapping");

        // The version line is the lowest thing in the sidebar: it must still be on screen.
        const artboard::Rect lastLink = home->bottomLinkRect(2);
        check(lastLink.y + lastLink.h < minH, "and the last link fits inside the window");

        // The minimum must be doing real work, not sitting comfortably clear of the problem —
        // a slack minimum hides the NEXT layout change instead of catching it. So find the
        // tallest window that still overlaps, by scanning rather than by hardcoding a delta
        // (the first attempt guessed 40 px and was simply wrong: the built-in air is 32 px plus
        // the link block's own 16 px offset, so 40 px short of the minimum still cleared it).
        double firstOverlap = -1.0;
        for (double h = minH; h > 200.0; h -= 1.0)
        {
            auto probe = makeHome(HomeScreen::minContentWidth(), h);
            if (probe->bottomLinkRect(0).y < actionsBottom) { firstOverlap = h; break; }
        }
        check(firstOverlap > 0.0, "some window height below the minimum does overlap");
        check(minH - firstOverlap < 64.0,
              "and it is close below the minimum, so the minimum is tight rather than generous");
    }

    // R-SCALE-3 / R5: the header row shares ONE line between the "Recent Projects" title and
    // the search field. Both used to be positioned independently — the field pinned right at a
    // fixed 168 px, the title drawn from the left knowing nothing about it — so at the minimum
    // window width the title ran straight under the box. Found by rendering the launcher at
    // App::minPhysical* for each screen scale, which is the smallest window the shell now
    // permits and therefore the frame where a fixed-plus-fixed row shows itself.
    //
    // Swept rather than spot-checked at the minimum: the failure is a threshold, and the whole
    // point is that no width between the minimum and a large window has an overlap.
    void homeHeaderTitleNeverRunsUnderTheSearchField()
    {
        std::printf("HomeScreen: the header title and the search field never overlap (R5)\n");
        // One `check` per property for the WHOLE sweep, not per width: 1600 identical [ok]
        // lines bury every other test in the suite, and the interesting number is the width
        // that failed, which a counter can carry out of the loop.
        bool sawLong = false, sawShort = false, sawShrunkField = false;
        double overlapAt = -1.0, spillAt = -1.0, squeezeAt = -1.0;
        for (double w = HomeScreen::minContentWidth(); w <= 2200.0; w += 1.0)
        {
            auto home = makeHome(w, 900.0);
            const artboard::Rect sr = home->searchRect();
            const double titleEnd = home->titleEndX();
            if (titleEnd > sr.x && overlapAt < 0) overlapAt = w;
            if (sr.x + sr.w > w - 32.0 + 0.01 && spillAt < 0) spillAt = w;
            if (sr.w < 72.0 && squeezeAt < 0) squeezeAt = w;
            if (home->headerTitle() == "Recent Projects") sawLong = true; else sawShort = true;
            if (sr.w < 168.0) sawShrunkField = true;
        }
        check(overlapAt < 0, "the title block ends before the search field at every width");
        check(spillAt < 0, "and the field stays inside the right pad at every width");
        check(squeezeAt < 0, "and is never squeezed below its readable floor");
        // All three responses must actually happen somewhere in the range, or the test is
        // passing because one of them is unreachable rather than because it works.
        check(sawLong, "a wide window shows the full title");
        check(sawShort, "and the narrowest one falls back to the short title");
        check(sawShrunkField, "and somewhere between, the field gives up width rather than overlap");

        // The minimum width must be big enough for the header's own floor, not only for one
        // card — the two constraints are summed independently in minContentWidth so that a
        // future sidebar or title change moves the minimum instead of breaking the row.
        auto atMin = makeHome(HomeScreen::minContentWidth(), 900.0);
        check(atMin->searchRect().x >= atMin->titleEndX(),
              "the published minimum width fits the header row");
    }

    // ── R-MASK-5: a mask may be sized and moved OUTSIDE the framed image ──────────────
    // Reported as "when I edit the width and height it limits at the image border". The cause
    // was a clamp to 0..1 in the overlay's localToNorm, which turned normalised framed-image
    // coordinates — a coordinate SPACE — into a boundary. The engine never had that limit.
    struct TestOverlay : MaskOverlay
    {
        TestOverlay() : MaskOverlay(arstro::cosmo_v2::palette::primary()) {}
        using MaskOverlay::handleGesture;
        using MaskOverlay::normToLocal;   // the test needs to grab the handle where it is drawn
    };

    std::shared_ptr<TestOverlay> makeOverlay(arstro::MaskParams &out)
    {
        auto ov = std::make_shared<TestOverlay>();
        // A canvas larger than the photo, which is the real arrangement: the image is fitted
        // inside the stage and letterboxed, so there IS room to drag past its edge.
        ov->width.set(1000.0);
        ov->height.set(800.0);
        out = arstro::MaskParams{};
        out.type = arstro::MaskParams::Radial;
        out.cx = 0.5f; out.cy = 0.5f; out.rx = 0.2f; out.ry = 0.2f;
        ov->setFittedRect(artboard::Rect{200.0, 150.0, 600.0, 500.0});   // the photo, inset
        ov->setMask(out, true);
        ov->onChange = [&out](const arstro::MaskParams &m) { out = m; };
        return ov;
    }

    void maskRadiusCanGrowPastTheImageEdge()
    {
        arstro::MaskParams m;
        auto ov = makeOverlay(m);

        // Grab the +X edge handle and drag it well beyond the photo's right edge (x=800) but
        // still on the canvas (x=1000) — the gesture the user performed.
        const Point handle = ov->normToLocal(m.cx + m.rx, m.cy);
        check(ov->handleGesture(ev(Gesture::Type::Down), handle), "the edge handle is grabbed");
        ov->handleGesture(ev(Gesture::Type::Drag), Point{980.0, handle.y});
        ov->handleGesture(ev(Gesture::Type::Up), Point{980.0, handle.y});

        // The photo's half-width in normalised units is 0.5; anything <= that is the old clamp.
        check(m.rx > 0.5f, "R-MASK-5: the radius grew past the image edge, not up to it");
        check(m.rx < 8.0f, "and stayed finite (the sanity bound, not a design limit)");

        // Vertically too, so this is not an accident of one axis.
        arstro::MaskParams m2;
        auto ov2 = makeOverlay(m2);
        const Point hy = ov2->normToLocal(m2.cx, m2.cy + m2.ry);
        ov2->handleGesture(ev(Gesture::Type::Down), hy);
        ov2->handleGesture(ev(Gesture::Type::Drag), Point{hy.x, 780.0});
        ov2->handleGesture(ev(Gesture::Type::Up), Point{hy.x, 780.0});
        check(m2.ry > 0.5f, "and the same on the Y edge");
    }

    void maskCentreCanLeaveTheImage()
    {
        arstro::MaskParams m;
        auto ov = makeOverlay(m);
        // Drag the centre off the left edge of the photo (x=200) into the letterbox.
        const Point c = ov->normToLocal(m.cx, m.cy);
        ov->handleGesture(ev(Gesture::Type::Down), c);
        ov->handleGesture(ev(Gesture::Type::Drag), Point{40.0, c.y});
        ov->handleGesture(ev(Gesture::Type::Up), Point{40.0, c.y});
        check(m.cx < 0.0f, "R-MASK-5: a gradient/vignette origin may sit off-frame");
        check(m.cx > -8.0f, "and remains finite");
    }

    // A radius must never reach zero: that is not a small mask, it is a division the engine
    // guards with an epsilon, and persisting a 0 would make the project file the problem.
    void maskRadiusStaysPositive()
    {
        arstro::MaskParams m;
        auto ov = makeOverlay(m);
        const Point handle = ov->normToLocal(m.cx + m.rx, m.cy);
        ov->handleGesture(ev(Gesture::Type::Down), handle);
        ov->handleGesture(ev(Gesture::Type::Drag), ov->normToLocal(m.cx, m.cy));  // onto the centre
        ov->handleGesture(ev(Gesture::Type::Up), ov->normToLocal(m.cx, m.cy));
        check(m.rx > 0.0f, "a radius dragged onto the centre stays strictly positive");
    }
}

namespace
{
    // ── R-CROP-1/2/3/6: the crop arithmetic ─────────────────────────────────────────────
    //
    // Pure functions, so they are tested directly rather than through a widget. Each of these
    // was a reported bug or the thing that made one possible.
    void cropRatioUsesThePhotosShapeNotASquare()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: a ratio is against the PHOTO's shape (R-CROP-1)\n");

        // The reported case: 16:9 on a 3:2 photo. Crop is normalised per axis, so the
        // normalised w/h is NOT 16/9 — it is 16/9 * (h/w) of the source.
        const double nr = crop::normalisedRatio(16.0 / 9.0, 3000, 2000);
        check(near(nr, (16.0 / 9.0) * (2000.0 / 3000.0)), "16:9 on a 3:2 photo is normalised");
        // ...and the box that comes out really is 16:9 in PIXELS, which is the whole point.
        const artboard::Rect box = crop::applyRatio(artboard::Rect{0, 0, 1, 1}, nr);
        const double pxW = box.w * 3000.0, pxH = box.h * 2000.0;
        char msg[160];
        std::snprintf(msg, sizeof(msg), "and the resulting box is %.0fx%.0f px = %.4f (16:9 = %.4f)",
                      pxW, pxH, pxW / pxH, 16.0 / 9.0);
        check(near(pxW / pxH, 16.0 / 9.0, 1e-3), msg);

        // A square photo is the case the old code was accidentally right for; it must stay right.
        check(near(crop::normalisedRatio(1.0, 500, 500), 1.0), "1:1 on a square photo is 1.0");
        // Unknown dimensions mean "nothing honest to compute" — 0, which callers read as free.
        check(near(crop::normalisedRatio(1.5, 0, 0), 0.0), "unknown dimensions report 0, not a guess");
    }

    void cropRatioFitsTheExistingCropRatherThanResetting()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: picking a ratio keeps YOUR framing (R-CROP-3)\n");
        // A crop the photographer has already placed, off-centre and small.
        const artboard::Rect framed{0.10, 0.55, 0.30, 0.30};
        const double cx = framed.x + framed.w * 0.5, cy = framed.y + framed.h * 0.5;
        const artboard::Rect out = crop::applyRatio(framed, 16.0 / 9.0);
        check(near(out.x + out.w * 0.5, cx, 1e-6), "the centre is preserved in x");
        check(near(out.y + out.h * 0.5, cy, 1e-6), "and in y");
        check(near(out.w / out.h, 16.0 / 9.0, 1e-6), "and the shape is the requested one");
        check(out.w <= 1.0 && out.h <= 1.0 && out.x >= 0.0 && out.y >= 0.0,
               "and it is still inside the photo");
        // The bug this replaces: a centred full-width box, i.e. the framing thrown away.
        check(!near(out.x, (1.0 - out.w) * 0.5, 1e-3), "NOT reset to a centred box");
    }

    void cropNeverGoesDegenerateOrOffFrame()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: clamped inside the frame, never degenerate (R-CROP-6)\n");
        const artboard::Rect tiny = crop::clampToFrame(artboard::Rect{0.5, 0.5, 0.0, 0.0});
        check(tiny.w >= crop::minSize() && tiny.h >= crop::minSize(), "a zero-size crop is grown to the minimum");
        const artboard::Rect off = crop::clampToFrame(artboard::Rect{1.5, -0.4, 0.3, 0.3});
        check(off.x >= 0.0 && off.y >= 0.0 && off.x + off.w <= 1.0 + 1e-9 && off.y + off.h <= 1.0 + 1e-9,
               "a box dragged off the photo slides back in");
        check(near(off.w, 0.3) && near(off.h, 0.3), "...and slides rather than shrinking");
        const artboard::Rect huge = crop::clampToFrame(artboard::Rect{-1.0, -1.0, 5.0, 5.0});
        check(near(huge.w, 1.0) && near(huge.h, 1.0) && near(huge.x, 0.0) && near(huge.y, 0.0),
               "an oversized crop becomes the whole photo");
    }

    void cropMoveAndResizeRespectTheLock()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: a locked ratio constrains the shape, not the position (R-CROP-3)\n");
        const double nr = 1.0;   // square, in normalised terms
        const artboard::Rect start{0.2, 0.2, 0.4, 0.4};

        // MOVE: the gesture the user reported missing. A locked ratio must not prevent it, and
        // must not change the shape either.
        const artboard::Rect moved = crop::moveTo(start, 0.55, 0.10);
        check(near(moved.w, start.w) && near(moved.h, start.h), "moving does not change the shape");
        check(near(moved.x, 0.55) && near(moved.y, 0.10), "and it goes where it was put");

        // RESIZE by a corner, locked: the shape survives.
        const artboard::Rect corner = crop::resizeBy(start, crop::Part::BottomRight, 0.8, 0.5, nr);
        check(near(corner.w / corner.h, nr, 1e-6), "a corner drag keeps the locked ratio");
        check(corner.w > start.w, "and actually resizes");

        // RESIZE by an EDGE, locked: the other axis follows rather than the drag being refused.
        const artboard::Rect edge = crop::resizeBy(start, crop::Part::Right, 0.9, 0.4, nr);
        check(near(edge.w / edge.h, nr, 1e-6), "an edge drag keeps it too, by moving the other axis");
        check(edge.w > start.w, "and is not simply ignored");

        // FREE: an edge drag changes ONE axis, which is what free means.
        const artboard::Rect free = crop::resizeBy(start, crop::Part::Right, 0.9, 0.4, 0.0);
        check(near(free.h, start.h), "free: the other axis is untouched");
        check(free.w > start.w, "and the dragged one moves");
    }

    void cropPartHitTestPrefersCornersOverEdges()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: corners beat edges, the interior is a MOVE (R-CROP-5)\n");
        const artboard::Rect box{100, 100, 200, 150};
        const double grab = 10.0;
        check(crop::partAt(box, artboard::Point{100, 100}, grab) == crop::Part::TopLeft,
               "the top-left corner is a corner, not an edge");
        check(crop::partAt(box, artboard::Point{300, 250}, grab) == crop::Part::BottomRight,
               "and so is the bottom-right");
        check(crop::partAt(box, artboard::Point{200, 100}, grab) == crop::Part::Top, "mid-top is an edge");
        check(crop::partAt(box, artboard::Point{100, 175}, grab) == crop::Part::Left, "mid-left is an edge");
        check(crop::partAt(box, artboard::Point{200, 175}, grab) == crop::Part::Move,
               "the interior is a MOVE — the gesture that was missing");
        check(crop::partAt(box, artboard::Point{500, 500}, grab) == crop::Part::None,
               "and well outside is nothing, so the press falls through to the pan");
    }
}

int main()
{
    arstro::cosmo::testMainInit();   // D-10: a failing assert must exit, not hang
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
    curveReferenceShownWhenDiffers();
    curveGrabMovesByTheDragNotToThePointer();
    curveReferenceIsNotEditable();
    curveReferenceLooksLikeAReadout();

    cropRatioUsesThePhotosShapeNotASquare();
    cropRatioFitsTheExistingCropRatherThanResetting();
    cropNeverGoesDegenerateOrOffFrame();
    cropMoveAndResizeRespectTheLock();
    cropPartHitTestPrefersCornersOverEdges();

    exportTreeStartsFullySelected();
    exportDeselectParentClearsChildren();
    exportSelectParentSelectsChildren();
    exportDeselectChildUnticksParent();
    exportSelectingLastChildTicksParent();
    exportMasterToggleAndCollapse();
    exportSelectedSlotsAreInTreeOrder();
    exportDragMovesTheCard();
    exportDragIsClamped();
    exportBeatsRunToCompletionAndClose();
    exportAnimationPlaysBeforeAnyExportWork();
    exportRequestIsSnapshotAtPressTime();
    filmstripScrollsIntoViewBothWays();
    filmstripScrollIsClampedAtBothEnds();
    filmstripKeepsScrollWhenTheSameListIsRepushed();
    filmstripLandsTheCellAtTheEdgeItCameFrom();
    filmstripShortRackNeverScrolls();
    exportProgressIgnoresInputAndReopensClean();

    homeSettingsLinkOpensSettings();
    maskRadiusCanGrowPastTheImageEdge();
    maskCentreCanLeaveTheImage();
    maskRadiusStaysPositive();
    homeGridReflowIsAnimated();
    homeMinimumHeightClearsBothAnchoredBlocks();
    homeHeaderTitleNeverRunsUnderTheSearchField();
    homeReservedLinksStaySilentButSwallowTheClick();
    homeSettingsLinkIsReachableAtASmallWindow();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
