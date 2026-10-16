/*
 *  solaris_ui — LoudnessPanel: the master as measured — the content of the Loudness window (R-METER-1, R-WIN).
 *
 *  Opened from the master's LUFS chip in the mixer. Every number is the MODEL's (`loudness`, which the service
 *  reads off the player's meters — the DSP library's BS.1770-4 loudness, true peak, correlation and spectrum):
 *  momentary, short-term and integrated LUFS, true peak in dBTP, the correlation as a number and a needle on
 *  −1…+1, and 61 bands of 1/6 octave. Reset is ONE `meters clear` line.
 *
 *  Nothing snaps (§1): each reading catches up to the model over 220 ms (a value the user does not control);
 *  a reading with nothing measured cross-fades to "—"; a band rises in 40 ms and falls in 300, as a meter does.
 *  `readingLive` / `bandLive` are what is drawn, `readingTarget` the model's.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <array>
#include <functional>
#include <string>

namespace arstro
{
namespace solaris_ui
{
    class LoudnessPanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 35.75;  // space::u(11), as the automation window's
        static constexpr double kCellH = 42.25;    // space::u(13): a label over a value
        static constexpr int kBands = 61;
        enum Reading { Momentary, ShortTerm, Integrated, TruePeak, Correlation, kReadings };

        LoudnessPanel();
        void bind(const solaris::AppModel &m);
        void layout() {}
        void advance(double nowMs) override;

        // what a test reads (LIVE, as drawn)
        double readingLive(int r) const { return mLive[(size_t)r].value(); }
        double readingTarget(int r) const { return mTarget[(size_t)r]; }
        double readingShown(int r) const { return mOn[(size_t)r].value(); } // 1: the number; 0: "—"
        double bandLive(int b) const { return mBand[(size_t)b].value(); } // dB, floored at −72
        artboard::Rect resetRect() const;
        artboard::Rect spectrumRect() const;

        std::function<bool(const std::string &line)> onCommand;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::array<double, kReadings> mTarget{}, mLast{};
        std::array<bool, kReadings> mHas{}, mHasLast{};
        std::array<artboard::AnimatedProperty, kReadings> mLive{}, mOn{};
        std::array<double, kBands> mBandTarget{}, mBandLast{};
        std::array<artboard::AnimatedProperty, kBands> mBand{};
        bool mPlaced = false;
        cosmo_v2::HoverFade mHover;
    };
}
}
