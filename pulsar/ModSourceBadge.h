/*
 *  Pulsar by arstro — ModSourceBadge: a small draggable "source" chip for a
 *  modulation source (an LFO or a macro). Drag it onto any knob to route that
 *  source to the knob (Serum-style); while dragging it trails a wire to the
 *  cursor. On drop it reports (sourceId, colour, drop-point) so the app can find
 *  the target knob and add the modulation.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace pulsar
{
    class ModSourceBadge : public artboard::Segment
    {
    public:
        ModSourceBadge(int sourceId, const artboard::Color &color, std::string label);

        // (sourceId, colour, world drop point) — the app resolves the target knob.
        std::function<void(int, const artboard::Color &, const artboard::Point &)> onAssign;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int mSourceId;
        artboard::Color mColor;
        std::string mLabel;
        bool mDragging = false;
        artboard::Point mDragLocal{0, 0};
    };
}
}
