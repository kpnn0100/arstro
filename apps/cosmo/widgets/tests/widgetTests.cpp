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
#include "../SectionHeader.h"
#include "../MaskOverlay.h"
#include "../HueCurveEditor.h"
#include "../CropGeometry.h"
#include "../CurvePanel.h"
#include "../XformPanel.h"
#include "../ExportDialog.h"
#include "../../Version.h"
#include "../Filmstrip.h"
#include "../../Theme.h"
#include "../BrandMark.h"
#include "../MenuStrip.h"
#include "../ContextMenu.h"
#include "../Toast.h"
#include "../ConfirmDialog.h"
#include "../Wordmark.h"
#include "../DirtyDot.h"
#include "../SegmentedControl.h"
#include "../HistoryView.h"
#include "../TextMetrics.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../../../core/Artboard/src/render/RecordingTarget.h"
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

    // R-EXPORT-3a (D-72): the preview line is the name that will be WRITTEN. With the defaults
    // (same as source, JPEG) the first photo's wanted name is its own source, so the line must
    // read river_01_edit.jpg; and when that is taken, river_01_edit-1.jpg.
    void test_export_preview_shows_the_real_output_name()
    {
        auto d = makeTree();
        d->setExistsProbe([](const std::string &) { return false; });
        check(d->previewPath() == "/pics/rivers/river_01_edit.jpg",
              "the preview of a JPEG exported beside itself is <stem>_edit.jpg, not the source's name");
        d->setExistsProbe([](const std::string &p) { return p == "/pics/rivers/river_01_edit.jpg"; });
        check(d->previewPath() == "/pics/rivers/river_01_edit-1.jpg", "a taken name counts up in the preview too");
        d->toggleRow(kRivers);   // only loose.jpg is left ticked
        d->setExistsProbe([](const std::string &) { return false; });
        check(d->previewPath() == "/pics/loose_edit.jpg", "the preview follows the first TICKED photo");
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

    // R-UX-5 (amends R-HOME-8): What's New and Help & Documentation hovered, swallowed the
    // click and did nothing. They are gone until they exist - Settings is the only link - and
    // the version under it is the build's, not a literal.
    void homeHasNoDeadLinksAndShowsTheBuildsVersion()
    {
        auto home = makeHome();
        check(arstro::cosmo_v2::HomeScreen::linkCount() == 1, "the sidebar has one link: Settings");
        artboard::RecordingTarget t;
        home->render(t, artboard::Transform::identity());
        bool whatsNew = false, help = false, settings = false, version = false;
        const std::string want = std::string("cosmo v") + COSMO_VERSION;
        for (const auto &op : t.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText)
            {
                whatsNew |= op.text.find("What's New") != std::string::npos;
                help |= op.text.find("Help") != std::string::npos;
                settings |= op.text == "Settings";
                version |= op.text == want;
            }
        check(settings, "Settings is drawn");
        check(!whatsNew && !help, "What's New and Help & Documentation are not drawn (R-UX-5)");
        check(version && arstro::cosmo_v2::HomeScreen::versionText() == want,
              "the version line is the build's COSMO_VERSION");
        // Where the dead links were is now plain sidebar: a click there is not consumed as a link.
        const artboard::Rect r = home->bottomLinkRect(0);
        int opened = 0;
        home->onSettings = [&opened] { ++opened; };
        const bool consumed = home->handleGesture(ev(Gesture::Type::Click), artboard::Point{r.x + 20.0, r.y + r.h * 0.5 + 20.0});
        check(opened == 0 && !consumed, "a click where What's New was is no longer swallowed by a dead link");
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
        const artboard::Rect lastLink = home->bottomLinkRect(HomeScreen::linkCount() - 1);
        check(lastLink.y + lastLink.h < minH, "and the last link fits inside the window");

        // The minimum must be doing real work, not sitting comfortably clear of the problem —
        // a slack minimum hides the NEXT layout change instead of catching it. So find the
        // tallest window that still overlaps, by scanning rather than by hardcoding a delta
        // (the first attempt guessed 40 px and was simply wrong: the built-in air is 32 px plus
        // the link block's own 16 px offset, so 40 px short of the minimum still cleared it).
        //
        // Tightness is a property of the SIDEBAR's own sum, which is one term of the minimum
        // (R-HOME-11 as amended by R-UX-5: the other terms are a card row and the floor held
        // for the Settings modal, D-82). So it is measured from that term, not from the max.
        const double sidebarMin = HomeScreen::minSidebarHeight();
        check(sidebarMin <= minH, "the sidebar's sum is one term of the window minimum");
        double firstOverlap = -1.0;
        for (double h = sidebarMin; h > 200.0; h -= 1.0)
        {
            auto probe = makeHome(HomeScreen::minContentWidth(), h);
            if (probe->bottomLinkRect(0).y < actionsBottom) { firstOverlap = h; break; }
        }
        check(firstOverlap > 0.0, "some window height below the sidebar's sum does overlap");
        check(sidebarMin - firstOverlap < 64.0,
              "and it is close below that sum, so the sum is tight rather than generous");
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
            if (home->headerTitle() == "Recent projects") sawLong = true; else sawShort = true;
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

    // ── R-MASK-6: drawing a closed outline on the photo ──────────────────────────────
    struct TestPathOverlay : MaskOverlay
    {
        TestPathOverlay() : MaskOverlay(arstro::cosmo_v2::palette::primary()) {}
        using MaskOverlay::handleGesture;
        using MaskOverlay::normToLocal;
        using MaskOverlay::pathPointAt;
        using MaskOverlay::pathPointCount;
    };

    std::shared_ptr<TestPathOverlay> makePathOverlay(arstro::MaskParams &out)
    {
        auto ov = std::make_shared<TestPathOverlay>();
        ov->width.set(1000.0);
        ov->height.set(800.0);
        out = arstro::MaskParams{};
        out.type = arstro::MaskParams::Path;      // empty: nothing drawn yet
        ov->setFittedRect(artboard::Rect{200.0, 150.0, 600.0, 500.0});
        ov->setMask(out, true);
        ov->onChange = [&out](const arstro::MaskParams &m) { out = m; };
        return ov;
    }

    /** One click: press and release at the same place, which is what placing a point is. */
    void tapAt(std::shared_ptr<TestPathOverlay> &ov, Point p, bool alt = false)
    {
        Gesture down = ev(Gesture::Type::Down); down.alt = alt;
        ov->handleGesture(down, p);
        ov->handleGesture(ev(Gesture::Type::Up), p);
    }

    void aPathMaskIsDrawnByClickingThePhoto()
    {
        std::printf("Mask: clicking the photo places the points of a closed outline (R-MASK-6)\n");
        arstro::MaskParams m;
        auto ov = makePathOverlay(m);

        // Three clicks inside the photo = a triangle. Each press places its point immediately,
        // so the shape appears as it is drawn rather than on release.
        tapAt(ov, ov->normToLocal(0.5f, 0.2f));
        check(m.path.size() == 1u, "the first click places a point");
        tapAt(ov, ov->normToLocal(0.8f, 0.7f));
        tapAt(ov, ov->normToLocal(0.2f, 0.7f));
        check(m.path.size() == 3u, "and each further click adds one");
        check(!m.path[0].smooth, "a click makes a CORNER point, so a polygon stays a polygon");
        check(near(m.path[1].x, 0.8, 1e-3) && near(m.path[1].y, 0.7, 1e-3),
              "placed where the pointer was, in normalised photo coordinates");

        // The outline the overlay draws comes from the ENGINE's flattener, so asserting the
        // polygon here asserts the rendered shape too — that sharing is the requirement, and it
        // is why this suite can make the claim without linking the engine.
        const auto poly = arstro::maskPathPolygon(m.path);
        check(poly.size() >= 3, "three points flatten to a closed polygon");
        check(near(poly.front().first, 0.5, 1e-3) && near(poly.front().second, 0.2, 1e-3),
              "which starts at the first point the user placed");

        // Dragging a point moves it; it does not add another.
        const Point p1 = ov->pathPointAt(1);
        ov->handleGesture(ev(Gesture::Type::Down), p1);
        ov->handleGesture(ev(Gesture::Type::Drag), ov->normToLocal(0.95f, 0.9f));
        ov->handleGesture(ev(Gesture::Type::Up), ov->normToLocal(0.95f, 0.9f));
        check(m.path.size() == 3u, "grabbing an existing point moves it rather than adding one");
        check(near(m.path[1].x, 0.95, 1e-3), "and it follows the pointer");

        // R-MASK-5 applies here too: a point may sit outside the photo.
        ov->handleGesture(ev(Gesture::Type::Down), ov->pathPointAt(2));
        ov->handleGesture(ev(Gesture::Type::Drag), Point{40.0, 760.0});
        ov->handleGesture(ev(Gesture::Type::Up), Point{40.0, 760.0});
        check(m.path[2].x < 0.0f, "a point may be dragged off the frame (R-MASK-5)");
        check(m.path[2].x > -8.0f, "and stays finite");

        // Double-click removes a point — and only when it lands ON one.
        const int before = ov->pathPointCount();
        ov->handleGesture(ev(Gesture::Type::DoubleClick), ov->pathPointAt(0));
        check(ov->pathPointCount() == before - 1, "double-clicking a point removes it");
        ov->handleGesture(ev(Gesture::Type::DoubleClick), ov->normToLocal(0.5f, 0.99f));
        check(ov->pathPointCount() == before - 1, "and a double-click on empty canvas removes none");
    }

    void altDraggingAPathPointPullsItsHandles()
    {
        std::printf("Mask: Alt-dragging a drawn point bends the outline (R-MASK-6)\n");
        arstro::MaskParams m;
        auto ov = makePathOverlay(m);
        tapAt(ov, ov->normToLocal(0.2f, 0.5f));
        tapAt(ov, ov->normToLocal(0.5f, 0.45f));
        tapAt(ov, ov->normToLocal(0.8f, 0.5f));
        tapAt(ov, ov->normToLocal(0.5f, 0.9f));
        check(m.path.size() == 4u, "four corner points");
        check(!m.path[1].smooth, "and none of them smooth yet");

        // Alt on the press, held for the whole gesture — the same rule the tone curve follows.
        Gesture down = ev(Gesture::Type::Down);
        down.alt = true;
        const Point pt = ov->pathPointAt(1);
        check(ov->handleGesture(down, pt), "Alt+press on a point is claimed");
        Gesture drag = ev(Gesture::Type::Drag);
        drag.alt = true;
        ov->handleGesture(drag, Point{pt.x + 80.0, pt.y - 60.0});
        ov->handleGesture(ev(Gesture::Type::Up), Point{pt.x + 80.0, pt.y - 60.0});

        check(m.path[1].smooth, "Alt+drag makes the point smooth");
        check(m.path[1].ox > 0.0f && m.path[1].oy < 0.0f, "the out-handle follows the drag");
        check(near(m.path[1].ix, -m.path[1].ox, 1e-5) && near(m.path[1].iy, -m.path[1].oy, 1e-5),
              "and the in-handle mirrors it, so the outline stays continuous");
        // The point itself did NOT move: that is the difference between Alt+drag and a drag.
        check(near(m.path[1].x, 0.5, 1e-3) && near(m.path[1].y, 0.45, 1e-3),
              "and the point itself stays where it was placed");

        // The bend is real, and measurable in the shape itself: the bowed outline reaches
        // further up (a smaller y) between the points than the straight one ever could.
        auto topOf = [](const std::vector<arstro::CurvePoint> &pts) {
            double top = 1e9;
            for (const auto &pt : arstro::maskPathPolygon(pts)) top = std::min(top, (double)pt.second);
            return top;
        };
        std::vector<arstro::CurvePoint> cornered = m.path;
        cornered[1].smooth = false;
        check(topOf(m.path) < topOf(cornered) - 1e-3,
              "and the bowed outline reaches past the straight chord");
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

    /** The reported bug: with a fixed ratio, dragging a corner OUT OF THE IMAGE changed the
     *  ratio — "height still expand but width is limited". Every handle, dragged well past
     *  every edge, must keep the shape exactly and stay inside the frame. */
    void cropLockedRatioSurvivesADragOutsideTheImage()
    {
        using namespace arstro::cosmo_v2;
        std::printf("Crop: a locked ratio survives a drag outside the image (D-51)\n");

        const double nr = 16.0 / 9.0;              // normalised, for a square photo
        const artboard::Rect start{0.30, 0.30, 0.40, 0.40 / nr};
        struct Case { const char *name; crop::Part part; double nx, ny; };
        const Case cases[] = {
            {"bottom-right, far past both edges", crop::Part::BottomRight,  3.0,  3.0},
            {"bottom-right, far past the right",  crop::Part::BottomRight,  3.0,  0.6},
            {"bottom-right, far past the bottom", crop::Part::BottomRight,  0.6,  3.0},
            {"top-left, far past both edges",     crop::Part::TopLeft,     -2.0, -2.0},
            {"top-left, far past the left",       crop::Part::TopLeft,     -2.0,  0.1},
            {"top-right, far past both",          crop::Part::TopRight,     3.0, -2.0},
            {"bottom-left, far past both",        crop::Part::BottomLeft,  -2.0,  3.0},
            {"right edge, far past the right",    crop::Part::Right,        4.0,  0.5},
            {"left edge, far past the left",      crop::Part::Left,        -4.0,  0.5},
            {"top edge, far past the top",        crop::Part::Top,          0.5, -4.0},
            {"bottom edge, far past the bottom",  crop::Part::Bottom,       0.5,  4.0},
        };
        for (const Case &c : cases)
        {
            const artboard::Rect out = crop::resizeBy(start, c.part, c.nx, c.ny, nr);
            char msg[224];
            const double got = out.h > 0.0 ? out.w / out.h : 0.0;
            std::snprintf(msg, sizeof(msg), "%s: ratio %.5f (wanted %.5f), box %.4f,%.4f %.4fx%.4f",
                          c.name, got, nr, out.x, out.y, out.w, out.h);
            check(near(got, nr, 1e-6), msg);
            std::snprintf(msg, sizeof(msg), "%s: stays inside the photo", c.name);
            check(out.x >= -1e-9 && out.y >= -1e-9 &&
                      out.x + out.w <= 1.0 + 1e-9 && out.y + out.h <= 1.0 + 1e-9, msg);
            std::snprintf(msg, sizeof(msg), "%s: and is not degenerate", c.name);
            check(out.w >= crop::minSize() - 1e-9 && out.h >= crop::minSize() - 1e-9, msg);
        }

        // ...and the ANCHOR — the corner that was not dragged — must not move. Dragging the
        // bottom-right out of the frame may not drag the top-left with it.
        {
            const artboard::Rect out = crop::resizeBy(start, crop::Part::BottomRight, 3.0, 3.0, nr);
            check(near(out.x, start.x, 1e-9) && near(out.y, start.y, 1e-9),
                  "the un-dragged corner stays put when the drag leaves the frame");
        }
        {
            const artboard::Rect out = crop::resizeBy(start, crop::Part::TopLeft, -2.0, -2.0, nr);
            check(near(out.x + out.w, start.x + start.w, 1e-9) &&
                      near(out.y + out.h, start.y + start.h, 1e-9),
                  "and the opposite corner stays put dragging the other way");
        }

        // A TALL ratio too, so the fix cannot be "whichever axis happens to be the wide one".
        {
            const double tall = 9.0 / 16.0;
            const artboard::Rect s2{0.4, 0.4, 0.2, 0.2 / tall};
            const artboard::Rect out = crop::resizeBy(s2, crop::Part::BottomRight, 5.0, 5.0, tall);
            char msg[160];
            std::snprintf(msg, sizeof(msg), "a tall ratio holds too: %.5f (wanted %.5f)",
                          out.w / out.h, tall);
            check(near(out.w / out.h, tall, 1e-6), msg);
            check(out.y + out.h <= 1.0 + 1e-9, "and it is the HEIGHT that binds for a tall box");
        }
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


// ── R-G-4: a section header is a row, and the rule is its flexible member ─────────────
//
// Reported: the divider in the COLOUR section runs under the eyedropper. It did — the header
// drew a hairline to the FULL panel width while the section's tool button was right-aligned
// inside that same span (D-58). This asserts the arithmetic directly rather than through a
// rendered frame, because "the line ends before the button" is a number, and a number is what
// drifts the next time somebody changes the button's size.
void sectionHeaderRuleStopsBeforeItsTrailingControl()
{
    using arstro::cosmo_v2::drawSectionHeader;
    using arstro::cosmo_v2::kSectionActionSize;
    using artboard::DrawOp;

    // The right end of the one stroked segment the header draws, or -1 if it drew none.
    auto ruleEndOf = [](double x, double w, double trailingW) {
        artboard::RecordingTarget t;
        drawSectionHeader(t, x, 0.0, w, "COLOUR", trailingW);
        double end = -1.0;
        bool sawStroke = false;
        for (const auto &op : t.ops())
        {
            if (op.kind == DrawOp::Kind::LineTo) end = op.args[0];
            if (op.kind == DrawOp::Kind::StrokePath) sawStroke = true;
        }
        return sawStroke ? end : -1.0;
    };

    const double x = 9.75, w = 304.5;   // the ParamPanel's own geometry at the shipping width

    // No trailing control: unchanged behaviour, the rule runs to the end of the row.
    check(near(ruleEndOf(x, w, 0.0), x + w, 1e-6),
          "R-G-4: with no trailing control the rule still reaches the row's end");

    // With one: it stops short of the button's BOX, not merely of its glyph — the button's
    // hover wash is as wide as the box, and a hairline crossing that wash is the same bug one
    // pixel further out.
    const double withBtn = ruleEndOf(x, w, kSectionActionSize);
    check(withBtn < x + w - kSectionActionSize,
          "R-G-4: the rule ends before the section action button's box");
    check(withBtn > x + w - kSectionActionSize - 13.0,
          "R-G-4: ...and only just before it — the gap is a gap, not a second margin");

    // The rule is the FLEXIBLE member: widen the row and only the rule grows.
    const double wide = ruleEndOf(x, w + 100.0, kSectionActionSize);
    check(near(wide - withBtn, 100.0, 1e-6),
          "R-G-4: the rule absorbs the whole width change; the fixed members do not move");

    // And a row too narrow for both fixed members draws no rule at all, rather than one
    // running backwards — the label is what has to survive, not the decoration (R5).
    check(ruleEndOf(x, kSectionActionSize + 4.0, kSectionActionSize) < 0.0,
          "R-G-4: a row with no room left simply has no rule");
}

// R-G-5: the accent is a runtime token. A sibling app (Interstellar) reuses these widgets as libraries
// and must be able to give them its own accent WITHOUT forking one. The default must stay cosmo's blue,
// and every accent-derived token — including the cached sharedTheme, which used to be a `static const`
// frozen at first call — must follow setAccent.
static void test_accent_is_a_runtime_token()
{
    using namespace arstro::cosmo_v2;
    const artboard::Color blue = artboard::Color::hex(0x4F7EF7);
    const artboard::Color pink = artboard::Color::hex(0xCF5AED);
    auto same = [](const artboard::Color &a, const artboard::Color &b) {
        return std::fabs(a.r - b.r) < 1e-6 && std::fabs(a.g - b.g) < 1e-6 && std::fabs(a.b - b.b) < 1e-6;
    };
    check(same(palette::primary(), blue), "R-G-5: the default accent is still cosmo's blue");
    check(same(sharedTheme().tab.activeIndicatorColor, blue), "R-G-5: sharedTheme starts blue");

    palette::setAccent(pink);
    check(same(palette::primary(), pink), "R-G-5: primary() follows setAccent");
    check(same(palette::ring(), pink) && std::fabs(palette::ring().a - 0.5) < 1e-6,
          "R-G-5: ring() follows setAccent and keeps its 0.5 alpha");
    check(same(palette::primaryAlpha(0.22), pink) && std::fabs(palette::primaryAlpha(0.22).a - 0.22) < 1e-6,
          "R-G-5: primaryAlpha() follows setAccent and keeps the requested alpha");
    check(same(sharedTheme().tab.activeIndicatorColor, pink),
          "R-G-5: the CACHED sharedTheme rebuilds when the accent moves");

    palette::setAccent(blue);   // leave the suite exactly as found
    check(same(sharedTheme().tab.activeIndicatorColor, blue), "R-G-5: and rebuilds back");
}

// ── R-BRAND: the logos and their animations ──────────────────────────────────────────────
// The animation law (arstro.rule.animation) is checked by MEASURING poses, never by looking:
// poseAt() is a pure function of time, so a test can stand at any millisecond.
namespace brandtest
{
    using namespace arstro::cosmo_v2::brand;
    const Mark kMarks[3] = {Mark::Cosmo, Mark::Interstellar, Mark::Solaris};
    const char *name(Mark m) { return m == Mark::Cosmo ? "cosmo" : m == Mark::Interstellar ? "interstellar" : "solaris"; }

    /** Every number in a pose, so two poses can be compared whole. */
    std::array<double, 23> flat(const Pose &p)
    {
        return {p.alpha, p.ringScale, p.ringAlpha, p.orbitDeg, p.dotAlpha, p.trailAlpha, p.shipX, p.shipY, p.shipAlpha,
                p.barX[0], p.barX[1], p.barX[2], p.barAlpha[0], p.barAlpha[1], p.barAlpha[2],
                p.passAlpha[0], p.passAlpha[1], p.sunY, p.sunAlpha, p.stripeY[0], p.stripeY[1], p.stripeY[2],
                p.stripeAlpha[0] + p.stripeAlpha[1] + p.stripeAlpha[2]};
    }
    bool samePose(const Pose &a, const Pose &b, double eps = 1e-6)
    {
        const auto x = flat(a), y = flat(b);
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            double d = std::fabs(x[i] - y[i]);
            if (i == 3) d = std::fabs(std::fmod(d, 360.0));   // the moon's angle: 360 is home
            if (d > eps && std::fabs(d - 360.0) > eps) return false;
        }
        return true;
    }
    /** The main body's opacity and position — the part the eye follows out. */
    void body(Mark m, const Pose &p, double &alpha, double &pos)
    {
        if (m == Mark::Cosmo) { alpha = p.dotAlpha; pos = p.orbitDeg; }
        else if (m == Mark::Interstellar) { alpha = p.shipAlpha; pos = p.shipX; }
        else { alpha = p.sunAlpha; pos = p.sunY; }
    }
    /** Everything a mark draws, summed: zero means nothing is on screen. */
    double visible(Mark m, const Pose &p)
    {
        if (m == Mark::Cosmo) return p.ringAlpha + p.dotAlpha + p.trailAlpha;
        if (m == Mark::Interstellar) return p.shipAlpha + p.barAlpha[0] + p.barAlpha[1] + p.barAlpha[2] + p.passAlpha[0] + p.passAlpha[1];
        return p.sunAlpha + p.stripeAlpha[0] + p.stripeAlpha[1] + p.stripeAlpha[2];
    }
}

static void test_brand_marks_obey_the_animation_law()
{
    using namespace brandtest;
    char msg[160];
    for (Mark m : kMarks)
    {
        const Pose rest;
        // Show up ends exactly at the still logo, and it does not start there.
        std::snprintf(msg, sizeof msg, "R-BRAND-3 %s: show up ends at the rest pose", name(m));
        check(samePose(poseAt(m, Phase::In, kInMs), rest), msg);
        std::snprintf(msg, sizeof msg, "R-BRAND-3 %s: show up starts with nothing drawn", name(m));
        check(visible(m, poseAt(m, Phase::In, 0.0)) < 1e-6, msg);
        std::snprintf(msg, sizeof msg, "R-BRAND-3 %s: mid show-up the live pose differs from the target (it eases, it does not snap)", name(m));
        check(!samePose(poseAt(m, Phase::In, 400.0), rest, 1e-3), msg);

        // Rule 3 — never vanish suddenly. The exit ends at nothing ...
        std::snprintf(msg, sizeof msg, "R-BRAND-4 %s: disappear starts at the rest pose", name(m));
        check(samePose(poseAt(m, Phase::Out, 0.0), rest), msg);
        std::snprintf(msg, sizeof msg, "R-BRAND-4 %s: disappear ends with nothing drawn", name(m));
        check(visible(m, poseAt(m, Phase::Out, kOutMs)) < 1e-6, msg);
        // ... and the body is STILL MOVING while it fades: at two late moments it is partly
        // transparent and in two different places. "Move, stop, then fade" fails this.
        double a1, p1, a2, p2;
        body(m, poseAt(m, Phase::Out, kOutMs * 0.70), a1, p1);
        body(m, poseAt(m, Phase::Out, kOutMs * 0.82), a2, p2);
        std::snprintf(msg, sizeof msg, "rule 3 %s: the body fades WHILE it moves (alpha %.2f->%.2f, pos %.1f->%.1f)", name(m), a1, a2, p1, p2);
        check(a1 < 0.999 && a1 > a2 && a2 >= 0.0 && std::fabs(p2 - p1) > 0.5, msg);
        // ... and no frame-to-frame jump in opacity anywhere in the exit (a pop).
        double worst = 0.0, pa, pp;
        body(m, poseAt(m, Phase::Out, 0.0), pa, pp);
        for (double t = 8.0; t <= kOutMs; t += 8.0)
        {
            double a, q; body(m, poseAt(m, Phase::Out, t), a, q);
            worst = std::max(worst, std::fabs(a - pa)); pa = a;
        }
        std::snprintf(msg, sizeof msg, "rule 3 %s: no opacity pop in the exit (largest 8 ms step %.3f)", name(m), worst);
        check(worst < 0.06, msg);

        // Rule 2 — speed has a slope. Leaving starts slowly: a quarter of the way into the
        // flight the body has covered well under a quarter of the distance.
        double a0, x0, aq, xq, ae, xe;
        body(m, poseAt(m, Phase::Out, kOutMs * 0.18), a0, x0);
        body(m, poseAt(m, Phase::Out, kOutMs * 0.36), aq, xq);
        body(m, poseAt(m, Phase::Out, kOutMs * 0.88), ae, xe);
        std::snprintf(msg, sizeof msg, "rule 2 %s: the exit starts slowly (%.0f%% of the way after a quarter of the time)", name(m), 100.0 * std::fabs(xq - x0) / std::fabs(xe - x0));
        check(std::fabs(xq - x0) < 0.2 * std::fabs(xe - x0), msg);
        // Arriving ends slowly: the last fifth of the body's move covers almost nothing.
        double ai, xi, aj, xj;
        body(m, poseAt(m, Phase::In, kInMs * 0.50), ai, xi);
        body(m, poseAt(m, Phase::In, kInMs * 0.60), aj, xj);
        double as, xs; body(m, poseAt(m, Phase::In, 0.0), as, xs);
        std::snprintf(msg, sizeof msg, "rule 2 %s: the arrival slows into rest", name(m));
        check(std::fabs(xj - xi) < 0.12 * std::fabs(xs), msg);

        // Loops: the working loop returns to its start pose, which is the rest pose, so it can
        // be entered and left without a jump — and it actually moves in between.
        std::snprintf(msg, sizeof msg, "loop %s: starts at the rest pose", name(m));
        check(samePose(poseAt(m, Phase::Idle, 0.0), rest), msg);
        std::snprintf(msg, sizeof msg, "loop %s: is back at its start pose after one period (%.0f ms)", name(m), loopMs(m));
        check(samePose(poseAt(m, Phase::Idle, loopMs(m)), poseAt(m, Phase::Idle, 0.0), 1e-4), msg);
        std::snprintf(msg, sizeof msg, "loop %s: moves in between", name(m));
        check(!samePose(poseAt(m, Phase::Idle, loopMs(m) * 0.37), rest, 1e-3), msg);
    }
    // The flight loops are vertical only: the ship and the sun never move sideways in them.
    bool vertical = true;
    for (double t = 0.0; t < 1600.0; t += 20.0)
        if (std::fabs(poseAt(Mark::Interstellar, Phase::Idle, t).shipX) > 1e-9) vertical = false;
    check(vertical, "loop interstellar: the ship shakes up and down only, never sideways");
}

static void test_brand_player_leaves_the_loop_only_at_a_boundary()
{
    using namespace brandtest;
    Player pl(Mark::Interstellar);            // loop = 1200 ms
    check(pl.gone(), "R-BRAND-5: a player starts with nothing drawn");
    pl.showUp(1000.0, true);
    pl.advance(1500.0);
    check(pl.phase() == Phase::In, "R-BRAND-5: showing up");
    pl.advance(2001.0);
    check(pl.phase() == Phase::Idle, "R-BRAND-5: show up hands over to the working loop");
    // Ask to leave 500 ms into a 1200 ms loop: it must keep flying to the boundary at 3200.
    pl.advance(2500.0); pl.requestOut(2500.0); pl.advance(2501.0);
    check(pl.phase() == Phase::Idle && !pl.leaving(), "loop rule: asked to leave mid-loop, it finishes the loop first");
    pl.advance(3199.0);
    check(pl.phase() == Phase::Idle, "loop rule: still in the loop 1 ms before the boundary");
    pl.advance(3210.0);
    check(pl.phase() == Phase::Out, "loop rule: the exit starts at the boundary");
    check(std::fabs(pl.outProgress() - 10.0 / kOutMs) < 1e-6, "loop rule: and is dated from the boundary, not from the frame that noticed");
    pl.advance(3200.0 + kOutMs + 1.0);
    check(pl.gone(), "R-BRAND-5: gone when the exit has played");

    // Reduced motion: nothing travels, but it still fades (rule 3 does not switch off).
    artboard::setReducedMotion(true);
    Player r(Mark::Solaris);
    r.showUp(0.0, true); r.advance(75.0);
    const Pose mid = r.pose();
    check(std::fabs(mid.alpha - 0.5) < 1e-6 && std::fabs(mid.sunY) < 1e-9, "reduced motion: show up is a fade with no travel");
    r.advance(200.0);
    check(r.phase() == Phase::Rest, "reduced motion: no working loop");
    artboard::setReducedMotion(false);
}


// ════════════════════════════════════════════════════════════════════════════════════════════
// R-FAMILY — one family, one behaviour: the shared building blocks (2026-11-17).
// Every block is opt-in, so each test first pins what an app that opts into nothing still gets.
// ════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
    using arstro::cosmo_v2::ConfirmDialog;
    using arstro::cosmo_v2::ContextMenu;
    using arstro::cosmo_v2::MenuStrip;
    using arstro::cosmo_v2::Toast;
    using artboard::DrawOp;
    using artboard::KeyEvent;
    using artboard::Rect;

    struct TestMenuStrip : MenuStrip { using MenuStrip::handleGesture; };
    struct TestContextMenu : ContextMenu { using ContextMenu::handleGesture; };
    struct TestConfirm : ConfirmDialog
    {
        TestConfirm() : ConfirmDialog(arstro::cosmo_v2::palette::primary()) {}
        using ConfirmDialog::advance;
    };

    KeyEvent keyDown(int code) { KeyEvent e; e.type = KeyEvent::Type::Down; e.keyCode = code; return e; }
    bool sameColour(const artboard::Color &a, const artboard::Color &b, double eps = 1e-6)
    {
        return std::fabs(a.r - b.r) < eps && std::fabs(a.g - b.g) < eps && std::fabs(a.b - b.b) < eps && std::fabs(a.a - b.a) < eps;
    }
    /** True if a frame put nothing on screen (a render still records its save / transform). */
    bool drewNothing(const artboard::RecordingTarget &t)
    {
        return t.count(DrawOp::Kind::FillPath) == 0 && t.count(DrawOp::Kind::StrokePath) == 0 && t.count(DrawOp::Kind::DrawText) == 0;
    }
    /** The DrawText op for `text`, and the fill colour in force when it was drawn. */
    struct DrawnText { bool found = false; double x = 0, y = 0, px = 0; std::string family; artboard::Color fill; };
    DrawnText drawnText(const artboard::RecordingTarget &t, const std::string &text)
    {
        DrawnText d;
        artboard::Color fill;
        for (const auto &op : t.ops())
        {
            if (op.kind == DrawOp::Kind::SetFill) fill = op.color;
            if (op.kind == DrawOp::Kind::DrawText && op.text == text)
            {
                d.found = true; d.x = op.args[0]; d.y = op.args[1]; d.px = op.args[2];
                d.family = op.fontFamily; d.fill = fill;
            }
        }
        return d;
    }
    constexpr int kUp = 38, kDown = 40, kLeft = 37, kRight = 39, kEnter = 13, kEsc = 27, kHome = 36, kEnd = 35;
}

// R-FAMILY-1 (MenuStrip): shortcut column, separator, disabled row, keyboard.
static void test_family_menu_strip_rows()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-1: MenuStrip rows\n");
    int ran = 0;
    auto act = [&ran](int id) { return [&ran, id] { ran = id; }; };

    TestMenuStrip ms;
    ms.width.set(600.0);
    // Menu 0 is the OLD shape — plain `{label, action}` initialisers, which must still compile.
    ms.addMenu({"Plain", {{"Open", act(1)}, {"Save a Copy of the Project", act(2)}}});
    // Menu 1 uses every new field. Indexes: 0 New, 1 Open, 2 SEPARATOR, 3 Save (disabled), 4 Export.
    ms.addMenu({"File", {{"New Project", act(10), "Ctrl+N"},
                         {"Open\xE2\x80\xA6", act(11), "Ctrl+O"},
                         MenuStrip::Item::sep(),
                         {"Save", act(13), "Ctrl+S", false},
                         {"Export All Photos\xE2\x80\xA6", act(14), "Ctrl+Shift+E"}}});

    const MenuStrip::Item plain{"x", nullptr};
    check(plain.shortcut.empty() && plain.enabled && !plain.separator && plain.selectable(),
          "a {label, action} item has no shortcut, is enabled, is not a separator");

    // ── the old menu is laid out exactly as before: 24 px rows, width from the estimate ──
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(ms.titleRect(0)));
    ms.advance(0.0); ms.advance(400.0);
    { artboard::RecordingTarget t; ms.renderOverlay(t); }
    const Rect p0 = ms.itemRect(0, 0), p1 = ms.itemRect(0, 1);
    check(near(p0.h, 24.0) && near(p1.y - p0.y, 24.0), "a plain menu keeps its 24 px rows");
    check(near(p0.w, std::max(152.0, estimateTextWidth("Save a Copy of the Project", 11.0) + 22.0)),
          "a plain menu keeps the width it always had (no shortcut: nothing re-measured)");
    check(near(ms.shortcutRect(0, 0).w, 0.0), "...and has no shortcut rect");

    // ── the new menu ──
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(ms.titleRect(1)));
    check(ms.openIndex() == 1, "the File menu opens");
    ms.advance(500.0); ms.advance(900.0);
    artboard::RecordingTarget t;
    ms.renderOverlay(t);

    // Indexes count every row, the separator included; its row is shorter.
    const Rect r0 = ms.itemRect(1, 0), r1 = ms.itemRect(1, 1), rs = ms.itemRect(1, 2), r3 = ms.itemRect(1, 3), r4 = ms.itemRect(1, 4);
    check(near(r1.y - r0.y, 24.0) && near(rs.y - r1.y, 24.0), "rows before the separator are where a plain menu puts them");
    check(rs.h > 0.0 && rs.h < 24.0, "the separator IS item 2, with a rect of its own, shorter than a row");
    check(near(r3.y, rs.y + rs.h) && near(r4.y - r3.y, 24.0), "rows after it start where it ends");
    check(ms.itemIndexAt(centreOf(rs)) == 2 && ms.itemIndexAt(centreOf(r3)) == 3 && ms.itemIndexAt(centreOf(r4)) == 4,
          "hit-testing and itemRect agree, by index, across the separator");

    // The width: widest label + widest shortcut, MEASURED by the target (not chars x 0.6).
    double lw = 0.0, sw = 0.0;
    for (const auto &it : ms.menu(1).items)
    {
        if (it.separator) continue;
        lw = std::max(lw, t.measureText(it.label, 11.0, font::sans()));
        sw = std::max(sw, t.measureText(it.shortcut, 10.0, font::mono()));
    }
    check(near(r0.w, std::max(152.0, lw + sw + 26.0 + 22.0)), "the menu is as wide as its widest label + widest shortcut, measured");
    const double estimated = estimateTextWidth("Export All Photos\xE2\x80\xA6", 11.0) + estimateTextWidth("Ctrl+Shift+E", 10.0) + 26.0 + 22.0;
    check(!near(r0.w, estimated, 0.5), "...which is not what the estimate would have given");

    // The shortcut: mono, muted, right-aligned by its own measured width.
    const DrawnText sc = drawnText(t, "Ctrl+N"), scLong = drawnText(t, "Ctrl+Shift+E");
    check(sc.found && sc.family == font::mono() && near(sc.px, 10.0), "a shortcut is drawn in the mono face");
    check(sameColour(sc.fill, palette::mutedForeground()), "...in the muted colour");
    const double rightEdge = r0.x + r0.w - 11.0;
    check(near(sc.x + t.measureText("Ctrl+N", 10.0, font::mono()), rightEdge) &&
          near(scLong.x + t.measureText("Ctrl+Shift+E", 10.0, font::mono()), rightEdge),
          "every shortcut ends on the same right edge (a right-aligned column)");
    check(near(ms.shortcutRect(1, 0).x, sc.x) && near(ms.shortcutRect(1, 0).w, t.measureText("Ctrl+N", 10.0, font::mono())),
          "shortcutRect publishes where it was drawn");
    const DrawnText lab = drawnText(t, "New Project");
    check(lab.found && lab.x + t.measureText("New Project", 11.0, font::sans()) < sc.x, "a label and its shortcut do not touch");

    // Disabled: dimmed.
    check(sameColour(drawnText(t, "Save").fill, palette::mutedForeground()) && sameColour(lab.fill, palette::foreground()),
          "a disabled row's label is dimmed; an enabled one is not");
    check(drawnText(t, "Ctrl+S").fill.a < sc.fill.a, "...and its shortcut is fainter than an enabled row's");

    // Hover: an enabled row eases in (LIVE differs from the target mid-tween); a separator and a
    // disabled row never take one.
    artboard::Segment::setHovered(&ms);
    ms.advance(1000.0);
    ms.handleGesture(ev(Gesture::Type::Move), centreOf(r1));
    ms.advance(1060.0);
    const double mid = ms.itemHoverAmount(1);
    check(ms.cursorItem() == 1 && mid > 0.02 && mid < 0.98, "an enabled row's wash is EASING 60 ms in (live != target)");
    ms.advance(1400.0);
    check(near(ms.itemHoverAmount(1), 1.0), "...and settles fully on");
    ms.handleGesture(ev(Gesture::Type::Move), centreOf(rs));
    ms.advance(1800.0);
    check(ms.cursorItem() == -1 && near(ms.itemHoverAmount(2), 0.0), "the separator takes no hover");
    ms.handleGesture(ev(Gesture::Type::Move), centreOf(r3));
    ms.advance(2200.0);
    check(ms.cursorItem() == -1 && near(ms.itemHoverAmount(3), 0.0), "a disabled row takes no hover wash");

    // Clicks: the separator and the disabled row do nothing and the menu stays open.
    ran = 0;
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(rs));
    check(ran == 0 && ms.openIndex() == 1, "a click on the separator does nothing; the menu stays open");
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(r3));
    check(ran == 0 && ms.openIndex() == 1, "a click on a disabled row does nothing; the menu stays open");
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(r4));
    check(ran == 14 && ms.openIndex() == -1, "a click on an enabled row runs it and closes");

    // Keyboard: skips the separator and the disabled row, wraps, runs, switches, closes.
    artboard::Segment::setHovered(nullptr);   // the pointer is elsewhere: the keyboard's cursor must survive that
    check(!ms.menuKey(keyDown(kDown)), "menuKey consumes nothing while no menu is open");
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(ms.titleRect(1)));
    ms.advance(3000.0);
    check(ms.cursorItem() == -1, "a menu opens with no row under the cursor");
    ms.menuKey(keyDown(kDown));
    check(ms.cursorItem() == 0, "Down stands on the first row");
    ms.advance(3016.0); ms.advance(3076.0);
    const double kmid = ms.itemHoverAmount(0);
    check(kmid > 0.02 && kmid < 0.98, "the keyboard's row is washed through the same EASED hover (mid-tween)");
    ms.advance(3400.0);
    check(near(ms.itemHoverAmount(0), 1.0), "...and stays lit with the pointer elsewhere");
    ms.menuKey(keyDown(kDown));
    check(ms.cursorItem() == 1, "Down moves to the next row");
    ms.menuKey(keyDown(kDown));
    check(ms.cursorItem() == 4, "Down steps OVER the separator and the disabled row");
    ms.menuKey(keyDown(kDown));
    check(ms.cursorItem() == 0, "...and wraps to the top");
    ms.menuKey(keyDown(kUp));
    check(ms.cursorItem() == 4, "Up wraps to the bottom");
    ms.menuKey(keyDown(kUp));
    check(ms.cursorItem() == 1, "Up steps over them too");
    ms.menuKey(keyDown(kEnd));
    check(ms.cursorItem() == 4, "End is the last selectable row");
    ms.menuKey(keyDown(kHome));
    check(ms.cursorItem() == 0, "Home is the first");
    ms.menuKey(keyDown(kLeft));
    check(ms.openIndex() == 0 && ms.cursorItem() == 0, "Left opens the neighbouring menu, on its first row");
    ms.menuKey(keyDown(kRight));
    check(ms.openIndex() == 1, "Right comes back");
    ms.menuKey(keyDown(kDown));
    ran = 0;
    check(ms.menuKey(keyDown(kEnter)) && ran == 11 && ms.openIndex() == -1, "Enter runs the row under the cursor and closes");
    ms.handleGesture(ev(Gesture::Type::Click), centreOf(ms.titleRect(1)));
    check(ms.menuKey(keyDown(kEsc)) && ms.openIndex() == -1, "Escape closes");
}

// R-FAMILY-1 (ContextMenu): the same three fields.
static void test_family_context_menu_rows()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-1: ContextMenu rows\n");
    int ran = 0;
    auto act = [&ran](int id) { return [&ran, id] { ran = id; }; };
    TestContextMenu cm;
    cm.width.set(800.0); cm.height.set(600.0);

    // The old shape, laid out as before.
    cm.open({{"Rename Group", act(1)}, {"Delete", act(2)}}, 100.0, 100.0);
    cm.advance(0.0); cm.advance(400.0);
    { artboard::RecordingTarget t; cm.renderOverlay(t); }
    check(near(cm.itemRect(1).y - cm.itemRect(0).y, 24.0) && near(cm.itemRect(0).w, 150.0),
          "a plain context menu keeps its rows and its width");
    cm.close();
    cm.advance(800.0);

    // 0 Copy, 1 Paste (disabled), 2 SEPARATOR, 3 Delete.
    cm.open({{"Copy", act(10), "Ctrl+C"}, {"Paste", act(11), "Ctrl+V", false}, ContextMenu::Item::sep(), {"Delete", act(13), "Del"}},
            100.0, 100.0);
    cm.advance(1000.0); cm.advance(1400.0);
    artboard::RecordingTarget t;
    cm.renderOverlay(t);
    const Rect r0 = cm.itemRect(0), r1 = cm.itemRect(1), rs = cm.itemRect(2), r3 = cm.itemRect(3);
    check(cm.itemCount() == 4 && rs.h > 0.0 && rs.h < 24.0 && near(r3.y, rs.y + rs.h), "the separator is item 2; the row after it starts where it ends");
    check(cm.itemIndexAt(centreOf(r3)) == 3 && cm.itemIndexAt(centreOf(rs)) == 2, "hit-testing agrees with itemRect across it");
    const DrawnText sc = drawnText(t, "Ctrl+C"), del = drawnText(t, "Del");
    check(sc.found && sc.family == font::mono() && sameColour(sc.fill, palette::mutedForeground()), "a shortcut is mono and muted");
    check(near(sc.x + t.measureText("Ctrl+C", 10.0, font::mono()), r0.x + r0.w - 11.0) &&
          near(del.x + t.measureText("Del", 10.0, font::mono()), r0.x + r0.w - 11.0), "shortcuts are right-aligned by measured width");
    check(sameColour(drawnText(t, "Paste").fill, palette::mutedForeground()), "a disabled row is dimmed");

    cm.handleGesture(ev(Gesture::Type::Move), centreOf(r1));
    cm.advance(1800.0);
    check(cm.cursorItem() == -1 && near(cm.itemHoverAmount(1), 0.0), "a disabled row takes no hover");
    cm.handleGesture(ev(Gesture::Type::Move), centreOf(r0));
    cm.advance(1816.0); cm.advance(1876.0);
    check(cm.itemHoverAmount(0) > 0.02 && cm.itemHoverAmount(0) < 0.98, "an enabled row's wash is easing mid-tween");
    cm.handleGesture(ev(Gesture::Type::Click), centreOf(r1));
    check(ran == 0 && cm.isOpen(), "a click on a disabled row does nothing; the menu stays open");
    cm.handleGesture(ev(Gesture::Type::Click), centreOf(rs));
    check(ran == 0 && cm.isOpen(), "a click on the separator does nothing; the menu stays open");

    cm.handleGesture(ev(Gesture::Type::Move), Point{700.0, 500.0});   // off the menu
    cm.menuKey(keyDown(kHome));
    check(cm.cursorItem() == 0, "Home stands on the first row");
    cm.menuKey(keyDown(kDown));
    check(cm.cursorItem() == 3, "Down steps over the disabled row and the separator");
    cm.menuKey(keyDown(kDown));
    check(cm.cursorItem() == 0, "...and wraps");
    cm.menuKey(keyDown(kUp));
    check(cm.cursorItem() == 3, "Up wraps back");
    check(cm.menuKey(keyDown(kEnter)) && ran == 13 && !cm.isOpen(), "Enter runs the row and closes");
    check(!cm.menuKey(keyDown(kDown)), "menuKey consumes nothing once closed");
}

// R-FAMILY-2: the toast.
static void test_family_toast()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-2: Toast\n");
    Toast toast;
    toast.width.set(1000.0);
    toast.advance(0.0);
    { artboard::RecordingTarget t; toast.renderOverlay(t);
      check(drewNothing(t) && !toast.isShowing() && near(toast.amount(), 0.0), "a toast nobody showed draws nothing"); }

    // In: eased (live != target mid-tween), sliding down while it fades.
    toast.show(Toast::Kind::Info, "Copied dv_3.cutoff", 1000.0);
    toast.advance(1000.0);
    toast.advance(1060.0);
    const double inMid = toast.amount();
    check(inMid > 0.05 && inMid < 0.98, "60 ms in, the toast is mid-fade (live != target)");
    { artboard::RecordingTarget t; toast.renderOverlay(t);
      check(toast.rect().y < 0.0 && near(toast.rect().y, -(1.0 - inMid) * Toast::kSlide), "...and still above its resting place: it slides as it fades"); }
    toast.advance(1200.0);
    artboard::RecordingTarget atRest;
    toast.renderOverlay(atRest);
    check(near(toast.amount(), 1.0) && near(toast.rect().y, 0.0) && near(toast.rect().h, Toast::kHeight), "150 ms later it is at rest, at the top of its band");
    check(near(toast.rect().x + toast.rect().w * 0.5, 500.0), "centred in the band the caller gave it");
    check(toast.kind() == Toast::Kind::Info && toast.text() == "Copied dv_3.cutoff", "kind and text are published");

    // Kinds: info draws no bar; warning an amber bar; error a red one.
    auto barColour = [&](Toast::Kind k, artboard::Color &out) {
        Toast tk; tk.width.set(1000.0);
        tk.show(k, "Two files are missing", 0.0);
        tk.advance(0.0); tk.advance(400.0);
        artboard::RecordingTarget t; tk.renderOverlay(t);
        // the bar is the only fill of the kind's colour that is not text: look for the colour at all
        for (const auto &op : t.ops())
            if (op.kind == DrawOp::Kind::SetFill && (sameColour(op.color, palette::warning()) || sameColour(op.color, palette::destructive())))
            { out = op.color; return true; }
        return false;
    };
    artboard::Color c;
    check(!barColour(Toast::Kind::Info, c), "an info toast has no bar and no glyph");
    check(barColour(Toast::Kind::Warning, c) && sameColour(c, palette::warning()), "a warning's bar is palette::warning()");
    check(barColour(Toast::Kind::Error, c) && sameColour(c, palette::destructive()), "an error's bar is palette::destructive()");
    check(sameColour(palette::warning(), artboard::Color::hex(0xE3B341)) && !sameColour(palette::warning(), palette::primary()),
          "the warning amber is #E3B341 and is not the accent");

    // Hold 3200 ms, then out — eased, sliding up while it fades, ending with nothing drawn.
    toast.advance(1000.0 + 3100.0);
    check(near(toast.amount(), 1.0), "it holds: still fully shown 3100 ms after it was said");
    toast.advance(1000.0 + 3216.0);   // the hold is over: this frame starts the fade
    toast.advance(1000.0 + 3216.0 + 150.0);
    const double outMid = toast.amount();
    check(outMid > 0.05 && outMid < 0.95, "half way out it is mid-fade (live != target)");
    { artboard::RecordingTarget t; toast.renderOverlay(t);
      check(toast.rect().y < 0.0, "...and already moving: it fades WHILE it leaves, not after it stops"); }
    toast.advance(1000.0 + 3216.0 + 400.0);
    { artboard::RecordingTarget t; toast.renderOverlay(t);
      check(near(toast.amount(), 0.0) && drewNothing(t) && !toast.isShowing(), "then it is gone, and draws nothing"); }

    // A second toast while one shows: a cross-fade of the words, never a cut.
    Toast sw; sw.width.set(1000.0);
    sw.show(Toast::Kind::Info, "First", 0.0);
    sw.advance(0.0); sw.advance(400.0);
    check(near(sw.swapAmount(), 1.0), "nothing replaced: swap is 1");
    sw.show(Toast::Kind::Error, "Second", 400.0);
    sw.advance(400.0); sw.advance(500.0);
    const double s = sw.swapAmount();
    check(s > 0.05 && s < 0.95, "100 ms after a second toast, the swap is mid-way (live != target)");
    check(near(sw.amount(), 1.0), "...while the toast itself stays up: no fade-out-and-in");
    { artboard::RecordingTarget t; sw.renderOverlay(t);
      const DrawnText a = drawnText(t, "First"), b = drawnText(t, "Second");
      check(a.found && b.found, "both the old and the new words are drawn during the swap");
      check(a.fill.a > 0.02 && b.fill.a > 0.02 && near(a.fill.a + b.fill.a, palette::foreground().a, 1e-6),
            "...the old fading out as the new fades in (their alphas sum to one)"); }
    sw.advance(900.0);
    { artboard::RecordingTarget t; sw.renderOverlay(t);
      check(near(sw.swapAmount(), 1.0) && !drawnText(t, "First").found && drawnText(t, "Second").found && sw.kind() == Toast::Kind::Error,
            "then only the new one is left"); }
    sw.advance(400.0 + 3100.0);
    check(near(sw.amount(), 1.0), "the hold is counted from the LAST thing said");

    // The clockless overload begins on the next advance.
    Toast lazy; lazy.width.set(1000.0);
    lazy.advance(100.0);
    lazy.show(Toast::Kind::Info, "Later");
    check(lazy.isShowing() && near(lazy.amount(), 0.0), "show() with no clock starts nothing yet, and says it is pending");
    lazy.advance(116.0); lazy.advance(180.0);
    check(lazy.amount() > 0.05 && lazy.amount() < 0.99, "...the next advance starts it, eased");

    // Text fits: ellipsized to the band with the target's real metrics.
    Toast narrow; narrow.width.set(300.0);
    const std::string longText = "Rendered the whole song to a file with a very long name indeed and then some more words after it";
    narrow.show(Toast::Kind::Warning, longText, 0.0);
    narrow.advance(0.0); narrow.advance(400.0);
    artboard::RecordingTarget nt; narrow.renderOverlay(nt);
    std::string drawn;
    for (const auto &op : nt.ops()) if (op.kind == DrawOp::Kind::DrawText) drawn = op.text;
    check(drawn != longText && drawn.size() >= 3 && drawn.compare(drawn.size() - 3, 3, "\xE2\x80\xA6") == 0, "a long sentence is cut with U+2026");
    check(nt.measureText(drawn, 12.0, font::sans()) <= 300.0 - 2 * Toast::kSideMargin - (Toast::kGlyph + 8.0) + 1e-6, "...to what the band leaves, by measured width");
    check(narrow.rect().x >= 0.0 && narrow.rect().x + narrow.rect().w <= 300.0, "the toast stays inside its band");

    // translate: a host's words, at paint time.
    Toast tr; tr.width.set(1000.0);
    tr.translate = [](const std::string &s) { return s == "Saved" ? std::string("\xC4\x90\xC3\xA3 l\xC6\xB0u") : s; };
    tr.show(Toast::Kind::Info, "Saved", 0.0);
    tr.advance(0.0); tr.advance(400.0);
    { artboard::RecordingTarget t; tr.renderOverlay(t);
      check(drawnText(t, "\xC4\x90\xC3\xA3 l\xC6\xB0u").found && tr.text() == "\xC4\x90\xC3\xA3 l\xC6\xB0u", "translate turns the stored words into the shown ones"); }
    check(!toast.hitTest(Point{500.0, 10.0}), "a toast never takes input");
}

// R-FAMILY-3: the destructive button's style.
static void test_family_confirm_destructive_style()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-3: ConfirmDialog destructive style\n");
    using Style = ConfirmDialog::DestructiveStyle;
    auto threeButtons = [] { return std::vector<ConfirmDialog::Button>{{"Cancel"}, {"Don\xE2\x80\x99t save", true}, {"Save", false, true}}; };
    auto twoButtons = [] { return std::vector<ConfirmDialog::Button>{{"Cancel"}, {"Delete", true}}; };
    // How button `i` was painted: a FillPath (filled) or only a StrokePath (outlined) in destructive red.
    auto paintedFilledRed = [](TestConfirm &d) {
        d.advance(0.0); d.advance(400.0);
        artboard::RecordingTarget t; d.renderOverlay(t);
        bool red = false, filled = false;
        for (const auto &op : t.ops())
        {
            if (op.kind == DrawOp::Kind::SetFill) red = sameColour(op.color, palette::destructive());
            if (op.kind == DrawOp::Kind::BeginPath) { /* a new shape: keep `red` — it was set just before */ }
            if (op.kind == DrawOp::Kind::FillPath && red) filled = true;
            if (op.kind == DrawOp::Kind::DrawText) red = false;
        }
        return filled;
    };
    auto labelColour = [](TestConfirm &d, const std::string &label) {
        artboard::RecordingTarget t; d.renderOverlay(t);
        return drawnText(t, label).fill;
    };

    {   // the default: what every existing caller gets
        TestConfirm d; d.width.set(800.0); d.height.set(600.0);
        check(d.destructiveStyle() == Style::Filled && !d.outlinedDestructive(), "the default style is Filled");
        d.show("Save changes to \xE2\x80\x9CTokyo\xE2\x80\x9D?", "Your edits will be lost.", threeButtons());
        check(!d.drawsOutlined(1) && paintedFilledRed(d), "by default the destructive button is filled, even beside a primary");
        check(sameColour(labelColour(d, "Don\xE2\x80\x99t save"), palette::primaryForeground()), "...with white words");
    }
    {   // Auto, with a primary beside it: outlined
        TestConfirm d; d.width.set(800.0); d.height.set(600.0);
        d.setDestructiveStyle(Style::Auto);
        d.show("Save changes to \xE2\x80\x9CTokyo\xE2\x80\x9D?", "Your edits will be lost.", threeButtons());
        check(d.drawsOutlined(1) && !d.drawsOutlined(0) && !d.drawsOutlined(2), "Auto: beside a primary, the destructive button (only it) is outlined");
        check(!paintedFilledRed(d), "...nothing is filled red");
        check(sameColour(labelColour(d, "Don\xE2\x80\x99t save"), palette::destructive()), "...its words are red");
        check(sameColour(labelColour(d, "Save"), palette::primaryForeground()), "...and the primary is still the filled one");
    }
    {   // Auto, alone: filled
        TestConfirm d; d.width.set(800.0); d.height.set(600.0);
        d.setDestructiveStyle(Style::Auto);
        d.show("Delete 3 photos?", "This cannot be undone.", twoButtons());
        check(!d.drawsOutlined(1) && paintedFilledRed(d), "Auto: the only action — the destructive button is FILLED");
        check(sameColour(labelColour(d, "Delete"), palette::primaryForeground()), "...with white words");
    }
    {   // Outlined, always — and the first form of the option still means exactly that
        TestConfirm d; d.width.set(800.0); d.height.set(600.0);
        d.setOutlinedDestructive(true);
        check(d.outlinedDestructive() && d.destructiveStyle() == Style::Outlined, "setOutlinedDestructive(true) is the Outlined style");
        d.show("Delete 3 photos?", "This cannot be undone.", twoButtons());
        check(d.drawsOutlined(1) && !paintedFilledRed(d), "Outlined: outlined even when it is the only action (Solaris's behaviour, kept)");
        d.setOutlinedDestructive(false);
        check(!d.outlinedDestructive() && d.destructiveStyle() == Style::Filled && !d.drawsOutlined(1), "...and (false) is Filled again");
    }
}

// R-FAMILY-4: the tokens.
static void test_family_tokens()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-4: tokens\n");
    check(near(space::u(1), 3.25) && near(space::padX(), 9.75) && near(space::gap(), 6.5) && near(space::rowH(), 22.75) && near(space::barH(), 29.25),
          "space:: is the 3.25 px ladder (padX 9.75, gap 6.5, rowH 22.75, barH 29.25)");
    check(near(motion::kHoverMs, 120.0) && near(motion::kModalOpenMs, 150.0) && near(motion::kModalCloseMs, 120.0) &&
          near(motion::kScrollMs, 180.0) && near(motion::kCrossFadeMs, 200.0) && near(motion::kSlideMs, 220.0) &&
          near(motion::kReflowMs, 260.0) && near(motion::kScreenFadeMs, 260.0),
          "motion:: carries the design law's durations");
    check(near(motion::kToastInMs, 150.0) && near(motion::kToastOutMs, 300.0) && near(motion::kToastHoldMs, 3200.0), "the toast: 150 in, 3200 hold, 300 out");
    const artboard::Color w = palette::warning();
    palette::setAccent(artboard::Color::hex(0xDD5A0C));   // Solaris's orange
    check(sameColour(palette::warning(), w), "the warning does not follow the accent");
    palette::setAccent(artboard::Color::hex(0x4F7EF7));
}

// R-FAMILY-5: the wordmark and the unsaved dot.
static void test_family_wordmark_and_dirty_dot()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-5: Wordmark, DirtyDot\n");
    artboard::RecordingTarget t;
    const double w = Wordmark::draw(t, "cosmo", 10.0, 20.0, 13.0);
    const DrawnText name = drawnText(t, "cosmo"), dot = drawnText(t, ".");
    const double sp = -0.03 * 13.0;
    const double nameW = t.measureText("cosmo", 13.0, font::sansSemiBold(), sp);
    check(name.found && dot.found && near(name.x, 10.0) && near(name.y, 20.0) && near(dot.y, 20.0), "the name and the dot share a baseline");
    check(near(dot.x, 10.0 + nameW), "the dot sits at the MEASURED end of the name");
    check(!near(dot.x, 10.0 + estimateTextWidth("cosmo", 13.0), 0.5), "...which is not where the estimate puts it");
    check(sameColour(name.fill, palette::foreground()) && sameColour(dot.fill, palette::primary()), "name in foreground, dot in the accent");
    check(name.family == font::sansSemiBold() && dot.family == font::sansSemiBold(), "both SemiBold");
    check(near(w, nameW + t.measureText(".", 13.0, font::sansSemiBold(), sp)) && near(w, Wordmark::width(t, "cosmo", 13.0)), "it returns its width, and width() agrees");
    artboard::RecordingTarget t2;
    Wordmark::draw(t2, "interstellar", 0.0, 20.0, 13.0, 0.5);
    check(near(drawnText(t2, "interstellar").fill.a, palette::foreground().a * 0.5) && near(drawnText(t2, ".").fill.a, 0.5), "alpha fades both runs");
    check(drawnText(t2, ".").x > dot.x, "a longer name pushes the dot further: it follows the name");

    DirtyDot d;
    d.set(true);
    d.advance(0.0);
    check(near(d.amount(), 1.0), "the first advance PLACES the dot (nowhere to travel from)");
    DirtyDot e;
    e.advance(0.0);
    check(near(e.amount(), 0.0), "a clean project has no dot");
    e.set(true);
    check(near(e.amount(), 0.0) && e.isAnimating(), "a setter has no clock: nothing moved yet, and it says it is pending");
    e.advance(16.0); e.advance(96.0);
    const double mid = e.amount();
    check(mid > 0.05 && mid < 0.99 && e.target(), "80 ms after it became dirty the dot is mid-fade (live != target)");
    { artboard::RecordingTarget r; e.draw(r, Point{50.0, 12.0});
      artboard::Color fill; bool drew = false;
      for (const auto &op : r.ops()) { if (op.kind == DrawOp::Kind::SetFill) fill = op.color; if (op.kind == DrawOp::Kind::FillPath) drew = true; }
      check(drew && near(fill.r, palette::foreground().r) && near(fill.g, palette::foreground().g) && near(fill.a, mid * palette::foreground().a),
            "it is drawn in the FOREGROUND colour, at its live amount"); }
    e.advance(400.0);
    check(near(e.amount(), 1.0) && !e.isAnimating(), "...and settles on");
    e.set(false);
    e.advance(416.0); e.advance(496.0);
    check(e.amount() > 0.01 && e.amount() < 0.95, "saving fades it out, eased");
    e.advance(900.0);
    { artboard::RecordingTarget r; e.draw(r, Point{50.0, 12.0});
      check(near(e.amount(), 0.0) && drewNothing(r), "...to nothing drawn"); }
    check(near(DirtyDot::kRadius, 3.0), "radius 3");
}

// R-FAMILY-6: the segmented control's live highlight.
static void test_family_segmented_highlight_pos()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-6: SegmentedControl::highlightPos\n");
    SegmentedControl seg({"Before", "Split", "After"});
    seg.width.set(300.0); seg.height.set(24.0);
    seg.layout();
    seg.advance(0.0);
    check(near(seg.highlightPos(), 0.0), "at rest the highlight is on the selected slot");
    seg.setSelected(2);
    check(seg.selected() == 2 && near(seg.highlightPos(), 0.0), "selecting moves the TARGET at once; the highlight has not moved yet");
    seg.advance(16.0); seg.advance(96.0);
    const double mid = seg.highlightPos();
    check(mid > 0.05 && mid < 1.95, "80 ms into the slide the highlight is between slots (live != target)");
    seg.advance(400.0);
    check(near(seg.highlightPos(), 2.0), "...and arrives");
    seg.setSelectedImmediate(1);
    check(near(seg.highlightPos(), 1.0), "setSelectedImmediate snaps it");
}

// R-FAMILY-7: the History card's title and radius.
static void test_family_history_title()
{
    using namespace arstro::cosmo_v2;
    std::printf("\nR-FAMILY-7: HistoryView title\n");
    HistoryView hv;
    hv.width.set(1200.0); hv.height.set(800.0);
    check(hv.title == "History", "the default title is \"History\", not uppercase");
    hv.show({HistoryView::Node{-1, "Open"}, HistoryView::Node{0, "Exposure"}}, 1);
    hv.advance(0.0); hv.advance(600.0);
    artboard::RecordingTarget t;
    hv.renderOverlay(t);
    const DrawnText title = drawnText(t, "History");
    check(title.found && near(title.px, 14.0) && title.family == font::sansSemiBold(), "drawn at 14 px SemiBold, as the other modal titles");
    double tracking = -1.0;
    for (const auto &op : t.ops()) if (op.kind == DrawOp::Kind::DrawText && op.text == "History") tracking = op.letterSpacingPx;
    check(near(tracking, 0.0), "...and not tracked out");
    // The card's corner. The scrim is the first path (radius 0), the card the second; a rounded
    // rect's path starts at (x + r, y), so r is that MoveTo's x minus the path's leftmost x.
    int paths = 0; double firstX = 0.0, minX = 1e9;
    for (const auto &op : t.ops())
    {
        if (op.kind == DrawOp::Kind::BeginPath) { ++paths; continue; }
        if (paths != 2) { if (paths > 2) break; continue; }
        if (op.kind == DrawOp::Kind::MoveTo) firstX = op.args[0];
        if (op.kind == DrawOp::Kind::MoveTo || op.kind == DrawOp::Kind::LineTo) minX = std::min(minX, op.args[0]);
        if (op.kind == DrawOp::Kind::QuadTo) minX = std::min(minX, op.args[2]);
        if (op.kind == DrawOp::Kind::CubicTo) minX = std::min(minX, op.args[4]);
    }
    check(near(firstX - minX, radius::control(), 0.01), "the card's corner radius is radius::control(), not a literal 6");
}

int main()
{
    arstro::cosmo::testMainInit();   // D-10: a failing assert must exit, not hang
    test_accent_is_a_runtime_token();
    test_brand_marks_obey_the_animation_law();
    test_brand_player_leaves_the_loop_only_at_a_boundary();
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
    cropLockedRatioSurvivesADragOutsideTheImage();
    cropPartHitTestPrefersCornersOverEdges();

    exportTreeStartsFullySelected();
    test_export_preview_shows_the_real_output_name();
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
    aPathMaskIsDrawnByClickingThePhoto();
    altDraggingAPathPointPullsItsHandles();
    homeGridReflowIsAnimated();
    homeMinimumHeightClearsBothAnchoredBlocks();
    homeHeaderTitleNeverRunsUnderTheSearchField();
    homeHasNoDeadLinksAndShowsTheBuildsVersion();
    homeSettingsLinkIsReachableAtASmallWindow();
    sectionHeaderRuleStopsBeforeItsTrailingControl();

    test_family_menu_strip_rows();
    test_family_context_menu_rows();
    test_family_toast();
    test_family_confirm_destructive_style();
    test_family_tokens();
    test_family_wordmark_and_dirty_dot();
    test_family_segmented_highlight_pos();
    test_family_history_title();


    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
