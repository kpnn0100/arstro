/*
 *  cosmo_v2 by arstro — CurvePanel (App.tsx CurvePanel): RGB/R/G/B channel
 *  picker + Reset button above a 232x164 tone-curve plot. Same editing UX as the
 *  mixer's HueCurveEditor: points are CORNERS (straight segments) by default;
 *  Alt-drag a node to pull out symmetric tangent handles (a smooth spline), and
 *  drag a handle to shape it (Alt = break symmetry). Double-click adds a corner
 *  on empty space / removes an interior node. Endpoints are locked in x (0 and 1).
 *
 *  Each channel has its OWN curve: four independent bezier control-point sets
 *  (0=RGB master, 1=R, 2=G, 3=B); the picker switches which is shown/edited. The
 *  RGB master maps EditParams::curve (applied to every channel); R/G/B map
 *  EditParams::curveChannel[0..2]. Edits emit (channel, points).
 *
 *  The effective (group-stacked) curve is drawn behind as a read-only reference
 *  (DR-EDIT-5). It is drawn DASHED, thinner and dimmer than the editable curve, and
 *  captioned, because drawn solid at the same weight it read as a second, equally
 *  editable curve — see D-28. It carries no nodes and is never hit-tested; the plot
 *  edits exactly one curve, the one the channel picker names.
 *
 *  And it LEAVES while you work in the plot (D-31). Looking like a readout was not
 *  enough: double-clicking the green line adds a corner exactly on it, so your curve
 *  snaps to touch it and you have, to all appearances, just grabbed and dragged the
 *  reference. A line that is not on screen during the gesture cannot be aimed at, so
 *  it fades out on press and back in once you have stopped.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../../../core/ImageProcessing/src/base/CurvePoint.h"
#include "../UiInspectable.h"
#include "SegmentedControl.h"
#include "IconButton.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class CurvePanel : public artboard::Segment, public UiInspectable
    {
    public:
        static constexpr double kPlotH = 164.0;  // plot width fills the panel (set in layout)
        using Points = std::vector<CurvePoint>;

        CurvePanel();

        // Restore all four curves: RGB master + the R/G/B per-channel curves.
        void setCurves(const Points &master, const std::array<Points, 3> &channels);
        // The effective (group-stacked) curves, drawn faintly behind as a reference.
        void setReferenceCurves(const Points &master, const std::array<Points, 3> &channels);
        // Select which channel the plot shows/edits (0=RGB,1=R,2=G,3=B).
        void showChannel(int channel);
        // Read a channel's current control points (0=RGB,1=R,2=G,3=B).
        const Points &curveFor(int channel) const { return mCurves[channel]; }
        void layout();
        void advance(double nowMs) override;
        /** The plot box in widget-local coords. Public because a test that drives the ASSEMBLED
         *  app has to aim at the plot through App's pointer entry, and restating kPadX/mPlotY in
         *  the test is how a test comes to aim somewhere the widget no longer draws. */
        artboard::Rect plotBox() const { return plotRect(); }

        /** P0.6 — the plot is one self-drawn leaf, so a tree dump reports a rectangle and
         *  says nothing about which curves are in it. This reports the editable curve, the
         *  faint reference behind it, and whether that reference is actually being drawn. */
        std::string uiDetail() const override;

        // (channel, points): channel 0 = RGB master, 1..3 = R/G/B.
        std::function<void(int, Points)> onCurveChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Rect plotRect() const;
        // Plot-local pixel <-> normalised [0,1] mapping (x = input, y = output).
        double px(double nx) const { return nx * mPlotW; }
        double py(double ny) const { return kPlotH - ny * kPlotH; }
        double nx(double localX) const;   // clamped input in [0,1]
        double ny(double localY) const;   // clamped output in [0,1]
        int pointAt(const artboard::Point &plotLocal) const;             // nearest node within radius, else -1
        bool handleAt(const artboard::Point &plotLocal, int &idx, int &kind) const;  // a smooth node's in/out handle
        Points &active() { return mCurves[mChannel]; }
        const Points &active() const { return mCurves[mChannel]; }
        artboard::Color channelColor() const;  // accent for RGB, red/green/blue for R/G/B
        void emitChange();
        /** True while the effective curve differs from the one being edited — i.e. while an
         *  ancestor group contributes something and there is a second line worth showing. */
        bool referenceWanted() const;
        /** True while the readout should be on screen: wanted, and nothing being aimed inside
         *  the plot (D-31). */
        bool referenceVisible(double nowMs) const;

        std::shared_ptr<SegmentedControl> mChannelPicker;
        std::shared_ptr<IconButton> mResetBtn;
        // 0 = RGB master, 1 = R, 2 = G, 3 = B; each defaults to identity corners.
        std::array<Points, 4> mCurves{{{CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}}}};
        std::array<Points, 4> mReference{};  // effective (group-stacked) curves; empty = none
        // R-G-1: the reference line and its caption fade in and out rather than blinking on
        // the frame a group's curve starts or stops differing. mRefShown holds the last
        // points worth drawing so the fade-out has something to fade.
        artboard::AnimatedProperty mRefFade{0.0};
        double mRefTarget = 0.0;
        Points mRefShown;
        // D-31: the readout is hidden while the pointer is working in the plot, and for a short
        // hold after it stops. The hold is what keeps a DOUBLE-click from flashing the line in
        // the gap between its two presses — the press state alone flickers there.
        bool mPressed = false;
        double mRevealAtMs = 0.0;
        double mNowMs = 0.0;      // last advance()'s clock, so a gesture can schedule the reveal
        static constexpr double kRefHoldMs = 220.0;
        int mChannel = 0;
        int mDragIdx = -1;
        int mDragKind = 0;  // 0 = move node, 1 = in-handle, 2 = out-handle, 3 = symmetric pull (Alt on node)
        // D-32: where the pointer sat WITHIN the thing it grabbed, in curve space, captured on
        // the press and added back on every drag. Without it a drag wrote the pointer's own
        // position straight into the node, so grabbing a node anywhere inside the forgiving
        // 13 px pick radius teleported it up to 13 px on the first pixel of movement.
        double mGrabDX = 0.0, mGrabDY = 0.0;
        /** Record the grab offset so `pointerX/Y` return the grabbed thing's current position
         *  on the frame of the press. `tx`/`ty` are that thing's position in curve space. */
        void beginGrab(const artboard::Point &plotLocal, double tx, double ty)
        { mGrabDX = tx - nx(plotLocal.x); mGrabDY = ty - ny(plotLocal.y); }
        /** The pointer in curve space, corrected for where inside the target it was grabbed. */
        double grabbedX(const artboard::Point &pl) const;
        double grabbedY(const artboard::Point &pl) const;
        double mPlotY = 0.0;
        double mPlotW = 232.0;  // set each layout() to the panel's inner width
    };
}
}
