/*
 *  interstellar_v1 — NodeGraph: the Grade target's grade as nodes and links (R-CLR-3).
 *
 *  Read left to right, it is the rack: the source's INPUT, the source's own grade, its PARALLEL nodes
 *  (variants grading the same input, their differences added at a "+" mixer), then each group it is in
 *  — the SERIAL nodes — and the OUTPUT. Every node is a Cosmo grade; the wiring is the rack's groups and
 *  `parallelOf`. Clicking a node makes it the Grade target; right-clicking asks for its menu (add a
 *  serial or a parallel node, set a mix, remove one), which the App dispatches as `node …` lines.
 *
 *  Motion: each node keeps its own eased position and opacity, keyed by its rack node — a node added
 *  fades in where it belongs and the ones after it slide along (220 ms), never a one-frame relayout.
 */
#pragma once
#include "../Theme.h"
#include "../../core/service/AppModel.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class NodeGraph : public artboard::Segment
    {
    public:
        enum class Kind { Input, Source, Parallel, Mixer, Group, Output };
        struct Node
        {
            std::string key;          // the #rackobj id, or "in" / "mix" / "out"
            std::string bind, label;
            Kind kind = Kind::Source;
            bool bypassed = false;
            double mix = 1.0;         // a parallel node's share
        };

        NodeGraph();
        void bind(const interstellar::AppModel &m);
        void advance(double nowMs) override;

        const std::vector<Node> &nodes() const { return mNodes; }
        /** Where node `key` is drawn NOW (local; eased), empty when it is not shown. */
        artboard::Rect nodeRect(const std::string &key) const;
        double nodeAlpha(const std::string &key) const;

        std::function<void(const std::string &bind)> onSelect;
        std::function<void(const Node &node, artboard::Point world)> onContext;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Anim
        {
            artboard::AnimatedProperty x, y, w, alpha;
            double tx = 0, ty = 0, tw = 0;
            bool placed = false, gone = false;
        };
        void place(double nowMs);
        int nodeAt(const artboard::Point &local) const;
        std::vector<Node> mNodes;
        std::map<std::string, Anim> mAnims;
        std::string mSelected, mLastKey;
        mutable bool mDirty = true;
        double mLastW = -1, mLastH = -1;
        cosmo_v2::HoverFade mHover;
        mutable std::vector<double> mTextW;   // measured label widths, by node (paint → next layout)
    };
}
}
