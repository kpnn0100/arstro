#include "TextToggle.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    TextToggle::TextToggle(const std::string &text, const Color &accent)
        : mText(text), mAccent(accent)
    {
        focusable = true;
        width.set(36.0);
        height.set(16.0);
    }

    void TextToggle::setOn(bool on)
    {
        mOn = on;
        mAnim.animateTo(on ? 1.0 : 0.0, 160.0, Easing::EaseOutCubic, mNowMs);
    }

    void TextToggle::advance(double nowMs)
    {
        mNowMs = nowMs;
        mAnim.update(nowMs);
        Segment::advance(nowMs);
    }

    void TextToggle::onPaint(IRenderTarget &t) const
    {
        const double a = mAnim.value();  // 0..1
        const Color off = palette::muted();
        const Color col{off.r + (mAccent.r - off.r) * a,
                        off.g + (mAccent.g - off.g) * a,
                        off.b + (mAccent.b - off.b) * a, 1.0};
        const double size = 11.0;
        const double y = height.value() * 0.5 + size * 0.35;
        t.setFill(col);
        t.drawText(mText, 0.0, y, size);
        // faux-bold: overdraw intensity grows with the on-animation
        if (a > 0.01)
        {
            t.setFill(Color{col.r, col.g, col.b, a});
            for (double ox : {0.4, 0.8})
                t.drawText(mText, ox, y, size);
        }
    }

    bool TextToggle::handleGesture(const Gesture &g, const Point &localPoint)
    {
        if (g.type == Gesture::Type::Click)
        {
            mOn = !mOn;
            mAnim.animateTo(mOn ? 1.0 : 0.0, 160.0, Easing::EaseOutCubic, mNowMs);
            if (onChange)
                onChange(mOn);
            return true;
        }
        return Segment::handleGesture(g, localPoint);
    }
}
}
