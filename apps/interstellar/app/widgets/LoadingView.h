/*
 *  interstellar_v1 — LoadingView: Screen::Loading — a project is opening.
 *
 *  The project's name, what is happening, and an eased progress treatment. The model carries no
 *  load fraction yet (a contract request in NOTES.md), so the bar is INDETERMINATE: a segment that
 *  sweeps the track with an ease-in-out, period 1400 ms — a timing, not a tween, so it is driven
 *  by phase rather than by `animateTo` — which reads as "working" without inventing a number. The
 *  whole view arrives and leaves through the App's screen cross-fade (260 ms).
 */
#pragma once
#include "../Theme.h"
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class LoadingView : public artboard::Segment
    {
    public:
        LoadingView() = default;
        void setProjectName(const std::string &n) { mName = n; }
        const std::string &projectName() const { return mName; }
        /** The sweep's position 0..1 along the track, from the phase clock. */
        double sweep() const;
        void advance(double nowMs) override { mNowMs = nowMs; Segment::advance(nowMs); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::string mName;
        double mNowMs = 0.0;
    };
}
}
