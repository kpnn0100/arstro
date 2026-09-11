#include "Monitor.h"
#include <algorithm>
#include <cmath>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    Monitor::Monitor() { clipToBounds = true; }

    void Monitor::setFrame(const interstellar::Raster &frame, unsigned seq)
    {
        if (seq == mSeq && !mFrame.empty()) return;
        mFrame = frame;
        mSeq = seq;
        // A setter has no clock, so it records intent and `advance` starts the tween
        // (arstro.design.rule §1). Starting one here would need a `nowMs` this call does not have.
        mFadeFromSeq = seq;
    }

    void Monitor::advance(double nowMs)
    {
        mLastMs = nowMs;
        if (mFadeFromSeq != 0 && mFade.value() >= 1.0)
        {
            // A new frame CROSS-FADES in, over cosmo's photo-dissolve duration and — crucially —
            // LINEARLY: a dissolve eased in time reads as a luminance bump in the middle, because
            // two frames at 50% do not sum to one at 100%.
            mFade.set(0.0);
            mFade.animateTo(1.0, 160.0, Easing::Linear, nowMs);
            mFadeFromSeq = 0;
        }
        mFade.update(nowMs);
        Segment::advance(nowMs);
    }

    Rect Monitor::frameRect() const
    {
        const double w = width.value(), h = height.value();
        const double pad = 9.75;   // 3 rungs of the 3.25 px ladder — cosmo's kPadX
        const double availW = std::max(1.0, w - pad * 2), availH = std::max(1.0, h - pad * 2);
        const double aspect = mFrame.empty() ? 16.0 / 9.0
                                             : (double)mFrame.width / std::max(1, mFrame.height);
        double dw = availW, dh = dw / aspect;
        if (dh > availH) { dh = availH; dw = dh * aspect; }
        return Rect{(w - dw) * 0.5, (h - dh) * 0.5, dw, dh};
    }

    void Monitor::layout() {}

    void Monitor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        t.setFill(surface::monitorBg());
        t.beginPath();
        t.moveTo(0, 0); t.lineTo(w, 0); t.lineTo(w, h); t.lineTo(0, h); t.closePath();
        t.fillPath();

        const Rect r = frameRect();
        if (!mFrame.empty())
        {
            if (mUploadedSeq != (int)mSeq)
            {
                // Register once per frame and RELEASE the previous id, or the target's image
                // table grows for the life of the session.
                if (mImageId > 0) t.releaseImage(mImageId);
                mImageId = t.registerImage(mFrame.rgba.data(), mFrame.width, mFrame.height);
                mUploadedSeq = (int)mSeq;
            }
            if (mImageId > 0)
            {
                t.pushLayer(std::max(0.0, std::min(1.0, mFade.value())));
                t.drawImage(mImageId, r);
                t.popLayer();
            }
        }
        else
        {
            // The empty and loading states, drawn — the two that get skipped and the two that
            // make an app feel broken.
            t.setStroke(palette::border(), 1.0);
            t.beginPath();
            t.moveTo(r.x, r.y); t.lineTo(r.x + r.w, r.y);
            t.lineTo(r.x + r.w, r.y + r.h); t.lineTo(r.x, r.y + r.h);
            t.closePath();
            t.strokePath();
            const std::string msg = mLoading ? "decoding…" : mEmptyReason;
            const double size = 12.0;
            const double tw = t.measureText(msg, size, font::sans(), 0.0);
            t.setFill(palette::mutedForeground());
            t.drawText(msg, r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + size * 0.35, size,
                       font::sans(), 0.0);
        }

        // The timecode is MONO: a proportional face jitters as the number changes, which is the
        // entire reason the mono family is vendored.
        const double tcSize = 10.0;
        t.setFill(palette::secondaryForeground());
        t.drawText(mTimecode, 9.75, h - 6.5, tcSize, font::mono(), 0.0);

        // A coarse proxy level is a READOUT, not a warning: degrading under load is correct
        // behaviour, and a red badge would make it look like a fault (R-NFR-4).
        if (mLevel > 0)
        {
            const std::string lv = "proxy " + std::to_string(mLevelEdge) + "px";
            const double lw = t.measureText(lv, tcSize, font::mono(), 0.0);
            t.setFill(palette::mutedForeground());
            t.drawText(lv, w - lw - 9.75, h - 6.5, tcSize, font::mono(), 0.0);
        }
    }
}
}
