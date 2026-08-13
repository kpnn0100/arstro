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
 *  EditParams::curveChannel[0..2]. Edits emit (channel, points); the effective
 *  (group-stacked) curve is drawn faintly behind as a reference.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../../../core/ImageProcessing/src/base/CurvePoint.h"
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
    class CurvePanel : public artboard::Segment
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

        std::shared_ptr<SegmentedControl> mChannelPicker;
        std::shared_ptr<IconButton> mResetBtn;
        // 0 = RGB master, 1 = R, 2 = G, 3 = B; each defaults to identity corners.
        std::array<Points, 4> mCurves{{{CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}},
                                       {CurvePoint{0, 0}, CurvePoint{1, 1}}}};
        std::array<Points, 4> mReference{};  // effective (group-stacked) curves; empty = none
        int mChannel = 0;
        int mDragIdx = -1;
        int mDragKind = 0;  // 0 = move node, 1 = in-handle, 2 = out-handle, 3 = symmetric pull (Alt on node)
        double mPlotY = 0.0;
        double mPlotW = 232.0;  // set each layout() to the panel's inner width
    };
}
}
