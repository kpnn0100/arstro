#include "ModSourceBadge.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    ModSourceBadge::ModSourceBadge(int sourceId, const Color &color, std::string label)
        : mSourceId(sourceId), mColor(color), mLabel(std::move(label))
    {
        width.set(46.0);
        height.set(18.0);
    }

    void ModSourceBadge::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        // while dragging, trail a wire from the chip to the cursor
        if (mDragging)
        {
            t.beginPath(); t.moveTo(w * 0.5, h * 0.5); t.lineTo(mDragLocal.x, mDragLocal.y);
            t.setStroke(Color{mColor.r, mColor.g, mColor.b, 0.7}, 2.0); t.strokePath();
            drawCircle(t, mDragLocal.x, mDragLocal.y, 4.0, Paint::filled(mColor));
        }
        drawRoundedRect(t, Rect{0, 0, w, h}, 5.0,
                        Paint::filledStroked(Color{mColor.r, mColor.g, mColor.b, 0.22}, mColor, 1.0));
        drawCircle(t, 9.0, h * 0.5, 3.0, Paint::filled(mColor));
        t.setFill(mColor);
        t.drawText(mLabel, 16.0, h * 0.5 + 3.0, 9.0);
    }

    bool ModSourceBadge::handleGesture(const Gesture &g, const Point &lp)
    {
        using T = Gesture::Type;
        if (g.type == T::Down) { mDragLocal = lp; return true; }
        if (g.type == T::DragStart) { mDragging = true; mDragLocal = lp; return true; }
        if (g.type == T::Drag) { mDragLocal = lp; return true; }
        if (g.type == T::Up || g.type == T::Drop)
        {
            if (mDragging && onAssign) onAssign(mSourceId, mColor, g.pos);
            mDragging = false;
            return true;
        }
        return Segment::handleGesture(g, lp);
    }
}
}
