#include "Monitor.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kInset = 9.75;    // the frame's breathing room inside the monitor
        constexpr double kChipPx = 10.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string proxyLabel(int edge)
        {
            if (edge >= 3840) return "full-res";
            if (edge >= 1920) return "1080p proxy";
            if (edge >= 1280) return "720p proxy";
            if (edge >= 960) return "540p proxy";
            return "360p proxy";
        }
    }

    Monitor::Monitor()
    {
        clipToBounds = true;
        mStateAmt[0].set(0.0);
        mStateAmt[1].set(0.0);
        mStateAmt[2].set(1.0);
    }

    void Monitor::setFrame(const interstellar::Raster &r, bool dissolve)
    {
        if (r.empty()) return;
        mPixels = r.rgba;
        mW = r.width;
        mH = r.height;
        mDirty = true;
        mDissolveWanted = dissolve && mCurId != 0;
    }

    int Monitor::wantedProxyEdge() const
    {
        const double edge = std::max(width.value(), height.value());
        if (edge <= 640.0) return 640;
        if (edge <= 960.0) return 960;
        if (edge <= 1280.0) return 1280;
        if (edge <= 1920.0) return 1920;
        return 3840;
    }

    Rect Monitor::frameRect() const
    {
        const double W = width.value() - 2 * kInset, H = height.value() - 2 * kInset;
        if (W <= 0 || H <= 0) return Rect{0, 0, 0, 0};
        const double aspect = (mW > 0 && mH > 0) ? (double)mW / mH : 16.0 / 9.0;
        double fw = W, fh = W / aspect;
        if (fh > H) { fh = H; fw = H * aspect; }
        return Rect{kInset + (W - fw) * 0.5, kInset + (H - fh) * 0.5, fw, fh};
    }

    void Monitor::advance(double nowMs)
    {
        mPhaseMs = nowMs;
        if (!mStateInit)
        {
            for (int i = 0; i < 3; ++i) mStateAmt[i].set(i == (int)mState ? 1.0 : 0.0);
            mStateApplied = mState;
            mStateInit = true;
        }
        else if (mState != mStateApplied)
        {
            for (int i = 0; i < 3; ++i)
                mStateAmt[i].animateTo(i == (int)mState ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mStateApplied = mState;
        }
        for (auto &a : mStateAmt) a.update(nowMs);
        if (mDissolveWanted)
        {
            mDissolve.set(0.0);
            mDissolve.animateTo(1.0, motion::kDissolveMs, Easing::Linear, nowMs);   // linear: a dissolve
            mDissolveWanted = false;
        }
        mDissolve.update(nowMs);
        Segment::advance(nowMs);
    }

    void Monitor::syncImages(IRenderTarget &t) const
    {
        if (mOwner != &t)
        {
            // A different target: the ids we hold mean nothing there. Start over in this one.
            mCurId = mPrevId = 0;
            mOwner = &t;
            mDirty = !mPixels.empty();
        }
        if (mDirty && !mPixels.empty())
        {
            const int id = t.registerImage(mPixels.data(), mW, mH);
            if (mDissolve.value() < 1.0 && mCurId != 0)
            {
                // keep the outgoing frame for the dissolve; release whatever it replaces
                if (mPrevId) t.releaseImage(mPrevId);
                mPrevId = mCurId;
                mPrevW = mW;
                mPrevH = mH;
            }
            else if (mCurId)
                t.releaseImage(mCurId);   // the id this frame replaces — never leak a frame per step
            mCurId = id;
            mDirty = false;
        }
        if (mPrevId && mDissolve.value() >= 1.0)
        {
            t.releaseImage(mPrevId);
            mPrevId = 0;
        }
    }

    void Monitor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::monitorBg()));
        syncImages(t);

        const Rect fr = frameRect();
        const double frameA = mStateAmt[(int)State::Frame].value();
        if (fr.w > 0 && fr.h > 0)
        {
            // the frame plate (what a letterbox looks like before pixels arrive)
            drawRoundedRect(t, fr, 0.0, Paint::filled(Color::hex(0x050505)));
            if (frameA > 0.001 && mCurId)
            {
                t.pushLayer(frameA);
                if (mPrevId && mDissolve.value() < 1.0)
                {
                    t.drawImage(mPrevId, fr);
                    t.pushLayer(mDissolve.value());
                    t.drawImage(mCurId, fr);
                    t.popLayer();
                }
                else
                    t.drawImage(mCurId, fr);
                t.popLayer();
            }
        }

        const double cy = fr.h > 0 ? fr.y + fr.h * 0.5 : h * 0.5;
        // "decoding", with the proxy level — and a spinner, so it reads as work, not as a stall.
        const double la = mStateAmt[(int)State::Loading].value();
        if (la > 0.001)
        {
            glyph::spinner(t, w * 0.5, cy - 14.0, 9.0, mPhaseMs, la);
            const std::string s = "decoding \xC2\xB7 " + proxyLabel(mProxyEdge);
            const double tw = t.measureText(s, 11.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), la));
            t.drawText(s, (w - tw) * 0.5, cy + 16.0, 11.0, font::sans());
        }
        // "no clip at the playhead" — a sentence, never a black frame that reads as a bug.
        const double ea = mStateAmt[(int)State::Empty].value();
        if (ea > 0.001)
        {
            glyph::film(t, Rect{w * 0.5 - 11.0, cy - 26.0, 22.0, 18.0}, fade(palette::mutedForeground(), 0.7 * ea), 1.2);
            const std::string s = "no clip at the playhead";
            const double tw = t.measureText(s, 12.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), ea));
            t.drawText(s, (w - tw) * 0.5, cy + 10.0, 12.0, font::sans());
        }

        // timecode chip (mono — a number in a proportional face jitters as it changes)
        if (!mTimecode.empty())
        {
            const double tw = t.measureText(mTimecode, kChipPx, font::mono());
            const Rect chip{kInset + 6.0, kInset + 6.0, tw + 12.0, 18.0};
            drawRoundedRect(t, chip, radius::control(), Paint::filled(surface::scrim(0.72)));
            t.setFill(palette::foreground());
            t.drawText(mTimecode, chip.x + 6.0, textfit::baseline(chip.y + chip.h * 0.5, kChipPx), kChipPx, font::mono());
        }
        if (!mCaption.empty())
        {
            const std::string cap = textfit::ellipsize(t, mCaption, std::max(0.0, w * 0.5 - 24.0), kChipPx, font::sans());
            const double tw = t.measureText(cap, kChipPx, font::sans());
            if (!cap.empty())
            {
                const Rect chip{w - kInset - 6.0 - tw - 12.0, kInset + 6.0, tw + 12.0, 18.0};
                drawRoundedRect(t, chip, radius::control(), Paint::filled(surface::scrim(0.72)));
                t.setFill(palette::mutedForeground());
                t.drawText(cap, chip.x + 6.0, textfit::baseline(chip.y + chip.h * 0.5, kChipPx), kChipPx, font::sans());
            }
        }
    }
}
}
