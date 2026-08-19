/*
 *  Arstrobench by arstro — GpuToggle: opt the image workload into the GPU (R-UI-9).
 *
 *  A caption plus Artboard's own ToggleSwitch, snapped to the caption's right edge. The
 *  switch is the framework's — its track/thumb styling comes straight from cosmo's theme,
 *  and its animated thumb and hover treatment come for free, so this widget owns only the
 *  caption and the unavailable state.
 *
 *  When the machine has no GPU backend at all the whole group is disabled and says so,
 *  rather than offering a switch that cannot change anything.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace arstrobench
{
    class GpuToggle : public artboard::Segment
    {
    public:
        GpuToggle();

        /** Fires on user interaction only (the framework's ToggleSwitch contract). */
        std::function<void(bool)> onChange;

        bool on() const;
        /** Grey the group out and swap the caption — for a machine with no accelerator. */
        void setUnavailable(bool unavailable);
        bool unavailable() const { return mUnavailable; }

        artboard::ToggleSwitch &toggle() { return *mSwitch; }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &) const override { return false; }

    private:
        std::shared_ptr<artboard::ToggleSwitch> mSwitch;
        bool mUnavailable = false;
        /** Eased 0..1 "switched on", so the caption brightens rather than flipping
         *  colour in one frame (R-G-1). */
        artboard::Property mOnAmount{0.0};
        double mNowMs = 0.0;  ///< last frame time, so the switch callback can start a tween
    };
}
}
