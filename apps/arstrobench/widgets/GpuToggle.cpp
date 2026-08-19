#include "GpuToggle.h"
#include "../Theme.h"

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        constexpr double kSwitchW = 34.0;
        constexpr double kSwitchH = 18.0;
        constexpr double kCaptionSize = 10.0;
        constexpr double kGap = 9.0;
    }

    GpuToggle::GpuToggle()
    {
        mSwitch = std::make_shared<ToggleSwitch>(sharedTheme().toggle);
        mSwitch->width.set(kSwitchW);
        mSwitch->height.set(kSwitchH);
        // Snapped to this group's right edge, so the caption can change width (it does,
        // when the machine has no GPU) without the switch drifting.
        mSwitch->x.set(0.0);
        mSwitch->y.set(0.0);
        mSwitch->onChange = [this](bool on) {
            mOnAmount.animate(Tween::range(mOnAmount.value(), on ? 1.0 : 0.0,
                                           motion::kDurationShort3).withEasing(Easing::EaseOutCubic),
                              mNowMs);
            if (onChange) onChange(on);
        };
        addChild(mSwitch);
    }

    bool GpuToggle::on() const { return mSwitch->on(); }

    void GpuToggle::advance(double nowMs)
    {
        mNowMs = nowMs;
        // Layout here, not in onPaint: the switch is snapped to this group's right edge,
        // and a const paint pass is the wrong place to move a child.
        mSwitch->x.set(width.value() - kSwitchW);
        mSwitch->y.set((height.value() - kSwitchH) * 0.5);
        Segment::advance(nowMs);
        mOnAmount.update(nowMs);
    }

    void GpuToggle::setUnavailable(bool unavailable)
    {
        mUnavailable = unavailable;
        // `enabled` drives the framework's animated disabledAmount(), so this fades
        // rather than flipping (R-G-1).
        enabled = !unavailable;
        mSwitch->enabled = !unavailable;
        if (unavailable) { mSwitch->setOn(false); mOnAmount.set(0.0); }
    }

    void GpuToggle::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const char *caption = mUnavailable ? "GPU unavailable" : "GPU";
        const double tw = t.measureText(caption, kCaptionSize, font::sansMedium(), 0.4);
        // The caption brightens as the switch comes on, eased by mOnAmount (R-G-1).
        const Color ink = dimColor(lerpColor(palette::mutedForeground(), palette::foreground(),
                                             mOnAmount.value()),
                                   disabledAmount());
        t.setFill(ink);
        t.drawText(caption, w - kSwitchW - kGap - tw, h * 0.5 + kCaptionSize * 0.36,
                   kCaptionSize, font::sansMedium(), 0.4);
    }
}
}
