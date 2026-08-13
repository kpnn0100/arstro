/*
 *  Pulsar by arstro — LfoCurve: an editable LFO shape, drawn and edited like an
 *  After-Effects keyframe graph. The shape is a list of nodes (x∈[0,1], y∈[-1,1])
 *  joined by cubic beziers; each node carries a symmetric tangent handle. You drag
 *  nodes to move them, drag a selected node's handle to bend the tangent, and
 *  double-click to add a node on the curve (or remove an interior one). A playhead
 *  dot tracks the LFO phase while a note is gated.
 *
 *  valueAt(phase) evaluates the curve for modulation (reserved for the audio path).
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include <vector>

namespace arstro
{
namespace pulsar
{
    class LfoCurve : public artboard::Segment
    {
    public:
        LfoCurve();

        void setAccent(const artboard::Color &c) { mAccent = c; }
        void setPlayhead(double phase01, bool active) { mPhase = phase01; mActive = active; }
        double valueAt(double phase01) const; // sampled curve value in [-1,1]

        /** Fade the curve content in from zero (used when this tab becomes selected). */
        void triggerReveal() { mReveal.reset(0.0); mReveal.setTarget(1.0); }
        void advance(double nowMs) override; // ticks the reveal fade

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Node { double x, y, hx, hy; }; // pos in [0,1]×[-1,1]; (hx,hy) = out-tangent offset

        // coordinate mapping between normalized curve space and local pixels
        double padX() const { return 10.0; }
        double padY() const { return 12.0; }
        double sx(double nx) const;            // normalized x -> pixel x
        double sy(double ny) const;            // normalized y -> pixel y
        double nx(double px) const;            // pixel x -> normalized x
        double ny(double py) const;            // pixel y -> normalized y
        artboard::Point nodeScreen(int i) const;
        artboard::Point handleScreen(int i, int dir) const; // dir +1 out, -1 in
        double evalSegmentY(int i, double targetX) const;   // bezier y at a given x in segment i

        std::vector<Node> mNodes;
        int mSel = -1;       // selected node
        int mDrag = 0;       // 0 none, 1 node, 2 out-handle, 3 in-handle
        artboard::Color mAccent = artboard::Color::hex(0xb46bff);
        double mPhase = 0.0;
        bool mActive = false;
        artboard::Spring mReveal{1.0}; // content fade-in on tab select (1 = fully shown)
        double mLastMs = -1.0;
    };
}
}
