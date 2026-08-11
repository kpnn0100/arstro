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
#include "../ExportDialog.h"
#include "../../Theme.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
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

    // Count the green (#4cb573) reference strokes in a rendered frame — that colour is
    // unique to the "final" reference curve, so its presence proves it was drawn.
    int greenRefStrokes(artboard::Segment &seg)
    {
        artboard::RecordingTarget t; seg.render(t);
        int n = 0;
        for (const auto &op : t.ops())
            if (op.kind == artboard::DrawOp::Kind::SetStroke &&
                near(op.color.r, 0.298, 0.02) && near(op.color.g, 0.710, 0.02) && near(op.color.b, 0.451, 0.02))
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
    void pressExport(std::shared_ptr<ExportDialog> &d, double &now)
    {
        const double bx = (1280.0 + 560.0) * 0.5 - 80.0;
        for (double y = 798.0; y > 400.0; y -= 2.0)
        {
            feedDialog(d, Gesture::Type::Click, bx, y);
            if (d->isExporting()) return;
            if (!d->isOpen()) { d = makeTree(); tickDialog(d, 30, now); }
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
    curveReferenceShownWhenDiffers();

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
    exportProgressIgnoresInputAndReopensClean();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
