/*
 *  interstellar_v1 — ScopePanel: the Grade column's SCOPES (R-UI-15), where cosmo's histogram sat.
 *
 *  A header of modes — Histogram · Waveform · Parade · Vector — and a CLIP switch; the body; and a
 *  readout line that says what is wrong in words: the share of pixels clipped at white (▲) and
 *  crushed at black (▼), the levels used of 256, and the source's bit depth beside the preview's.
 *
 *  The Histogram mode draws the same data cosmo's `HistogramWidget` drew, in its channel colours, but
 *  to fill the scope body: cosmo's widget has a fixed plot height and left half the body empty
 *  (R-UI-5 amended — the histogram is one scope now). The other modes draw the rasters `scopesOf`
 *  made, stretched to the body, with their graticules: 0/25/50/75/100 % lines
 *  for the waveform and parade; for the vectorscope the 75 % colour targets, the 100 % circle and
 *  the skin-tone line. A mode change cross-fades; the CLIP switch's look eases. Which mode, and
 *  whether the monitor shows the clip overlay, are presentation — no command.
 */
#pragma once
#include "../Theme.h"
#include "Scopes.h"
#include "ImageSlot.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class ScopePanel : public artboard::Segment
    {
    public:
        enum Mode { Histogram = 0, Waveform = 1, Parade = 2, Vector = 3, kModes = 4 };
        static constexpr double kHeaderH = 24.0;
        static constexpr double kReadoutH = 20.0;
        static constexpr double kHeight = 168.0;

        ScopePanel();
        void setData(const ScopeData &d);
        /** The source's bit depth, for the readout (0 = unknown). */
        void setSourceBits(int bits) { mSourceBits = bits; }
        void setMode(int m);
        int mode() const { return mMode; }
        bool clipWarning() const { return mClip; }
        void setClipWarning(bool on);
        /** The LIVE eased weight of a mode's body (0..1). */
        double modeAmount(int m) const { return mModeAmt[m].value(); }
        const ScopeData &data() const { return mData; }
        std::string readout() const;
        void layout();

        artboard::Rect modeRect(int m) const;
        artboard::Rect clipRect() const;
        artboard::Rect bodyRect() const;

        std::function<void(bool on)> onClipWarning;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        ScopeData mData;
        ImageSlot mWave, mParade, mVector;
        int mMode = Histogram, mModeApplied = -1;
        artboard::AnimatedProperty mModeAmt[kModes];
        bool mClip = false, mClipApplied = false;
        artboard::AnimatedProperty mClipAmt{0.0};
        cosmo_v2::HoverFade mHover, mSel;
        int mSourceBits = 0;
    };
}
}
