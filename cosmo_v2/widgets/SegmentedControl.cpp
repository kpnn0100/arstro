#include "SegmentedControl.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    SegmentedControl::SegmentedControl(std::vector<std::string> labels)
    {
        for (auto &label : labels)
        {
            auto seg = std::make_shared<PillButton>(label);
            const int idx = (int)mSegs.size();
            seg->onClick = [this, idx] { setSelected(idx); };
            addChild(seg);
            mSegs.push_back(seg);
        }
        if (!mSegs.empty()) mSegs[0]->active = true;
    }

    void SegmentedControl::setSelected(int index)
    {
        if (index < 0 || index >= (int)mSegs.size()) return;
        mSelected = index;
        for (size_t i = 0; i < mSegs.size(); ++i) mSegs[i]->active = ((int)i == index);
        if (onChange) onChange(index);
    }

    void SegmentedControl::layout()
    {
        const double w = width.value(), h = height.value();
        const double innerW = w - 2 * padding, innerH = h - 2 * padding;
        const int n = (int)mSegs.size();
        if (n == 0) return;
        const double segW = (innerW - gap * (n - 1)) / n;
        double cx = padding;
        for (auto &seg : mSegs)
        {
            seg->idleBox = idleSegBox;
            seg->activeBox = activeSegBox;
            seg->idleText = idleText;
            seg->activeText = activeText;
            seg->x.set(cx);
            seg->y.set(padding);
            seg->width.set(segW);
            seg->height.set(innerH);
            cx += segW + gap;
        }
    }

    void SegmentedControl::onPaint(IRenderTarget &t) const
    {
        if (containerBox.paint.hasFill || containerBox.paint.hasStroke)
            drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, containerBox.cornerRadius, containerBox.paint);
    }
}
}
