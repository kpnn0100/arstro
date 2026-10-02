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
        // the TARGET zoom, so a zoom asks once for the level it is going to, not every frame
        const double edge = std::max(width.value(), height.value()) * std::max(1.0, mZoomTarget);
        if (edge <= 640.0) return 640;
        if (edge <= 960.0) return 960;
        if (edge <= 1280.0) return 1280;
        if (edge <= 1920.0) return 1920;
        if (edge <= 3840.0) return 3840;
        return 7680;
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

    Point Monitor::centreFor(double z) const
    {
        const Rect fr = frameRect();
        Point c = mCentre;
        if (mAnchored && fr.w > 0 && fr.h > 0)
        {
            // keep picture point mAnchorU under mAnchorAt at THIS zoom
            c.x = (fr.x + fr.w * 0.5 - mAnchorAt.x) / (fr.w * z) + mAnchorU.x;
            c.y = (fr.y + fr.h * 0.5 - mAnchorAt.y) / (fr.h * z) + mAnchorU.y;
        }
        // the picture always covers the frame: the centre stays half a view from each edge
        const double half = 0.5 / std::max(1.0, z);
        c.x = std::clamp(c.x, half, 1.0 - half);
        c.y = std::clamp(c.y, half, 1.0 - half);
        return c;
    }

    Rect Monitor::imageRect() const
    {
        const Rect fr = frameRect();
        const double z = std::max(1.0, mZoom.value());
        const Point c = centreFor(z);
        return Rect{fr.x + fr.w * 0.5 - c.x * fr.w * z, fr.y + fr.h * 0.5 - c.y * fr.h * z, fr.w * z, fr.h * z};
    }

    void Monitor::zoomAbout(double factor, Point at)
    {
        const Rect ir = imageRect();
        if (ir.w <= 0 || ir.h <= 0) return;
        // the picture point under the pointer NOW (the live picture is what the user aims at)
        mAnchorU = Point{std::clamp((at.x - ir.x) / ir.w, 0.0, 1.0), std::clamp((at.y - ir.y) / ir.h, 0.0, 1.0)};
        mAnchorAt = at;
        mAnchored = true;
        mZoomTarget = std::clamp(mZoomTarget * factor, 1.0, kMaxZoom);
    }

    void Monitor::resetZoom()
    {
        const Rect fr = frameRect();
        const Point c = centreFor(std::max(1.0, mZoom.value()));
        mAnchorAt = Point{fr.x + fr.w * 0.5, fr.y + fr.h * 0.5};   // zoom out about the view's centre
        mAnchorU = c;
        mAnchored = true;
        mZoomTarget = 1.0;
    }

    bool Monitor::hitTestSelf(const Point &p) const { return localBounds().contains(p); }   // the wheel lands here

    bool Monitor::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mCaptureHover.setHovered(mCaptureRect.contains(local) ? 0 : -1);
            return true;
        case Gesture::Type::Click:
            if (mCaptureRect.contains(local))
            {
                const Point o = worldTransform().apply(Point{mCaptureRect.x, mCaptureRect.y});
                if (onCapture) onCapture(Rect{o.x, o.y, mCaptureRect.w, mCaptureRect.h});
            }
            return true;
        case Gesture::Type::Scroll:
            // cosmo's R-ZOOM-1: Ctrl + wheel zooms about the pointer; a plain wheel does nothing here
            if (!g.ctrl) return false;
            zoomAbout(std::pow(kZoomNotch, -g.delta.y / shell::wheelNotchPx()), local);
            return true;
        case Gesture::Type::DoubleClick:
            if (frameRect().contains(local) && !mCaptureRect.contains(local)) resetZoom();
            return true;
        case Gesture::Type::Down:
            if (mZoomTarget > 1.0 + 1e-9 && frameRect().contains(local) && !mCaptureRect.contains(local))
            {
                // a pan takes the view from where it is drawn now; the anchor lets go
                mCentre = centreFor(std::max(1.0, mZoom.value()));
                mAnchored = false;
                mPanning = true;
                mPanLast = local;
            }
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mPanning)
            {
                // direct manipulation: the picture follows the pointer exactly
                const Rect ir = imageRect();
                if (ir.w > 0 && ir.h > 0)
                {
                    mCentre.x -= (local.x - mPanLast.x) / ir.w;
                    mCentre.y -= (local.y - mPanLast.y) / ir.h;
                    mCentre = centreFor(std::max(1.0, mZoom.value()));   // clamped
                }
                mPanLast = local;
            }
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mPanning = false;
            return true;
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void Monitor::advance(double nowMs)
    {
        if (!mCaptureInit) { mCaptureAmt.set(mCaptureWanted ? 1.0 : 0.0); mCaptureApplied = mCaptureWanted; mCaptureInit = true; }
        if (mCaptureWanted != mCaptureApplied)
        {
            mCaptureAmt.animateTo(mCaptureWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mCaptureApplied = mCaptureWanted;
        }
        mCaptureAmt.update(nowMs);
        if (!isHovered()) mCaptureHover.clear();
        mCaptureHover.advance(nowMs);
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
        if (mZoomTarget != mZoomLast)
        {
            mZoom.animateTo(mZoomTarget, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mZoomLast = mZoomTarget;
        }
        mZoom.update(nowMs);
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
                // zoomed: the picture overhangs the frame, which clips it (R-UI-13)
                const Rect ir = imageRect();
                t.save();
                t.clipRect(fr.x, fr.y, fr.w, fr.h);
                t.pushLayer(frameA);
                if (mPrevId && mDissolve.value() < 1.0)
                {
                    t.drawImage(mPrevId, ir);
                    t.pushLayer(mDissolve.value());
                    t.drawImage(mCurId, ir);
                    t.popLayer();
                }
                else
                    t.drawImage(mCurId, ir);
                t.popLayer();
                t.restore();
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
        double capLeft = w - kInset - 6.0;
        if (!mCaption.empty())
        {
            const std::string cap = textfit::ellipsize(t, mCaption, std::max(0.0, w * 0.5 - 48.0), kChipPx, font::sans());
            const double tw = t.measureText(cap, kChipPx, font::sans());
            if (!cap.empty())
            {
                const Rect chip{w - kInset - 6.0 - tw - 12.0, kInset + 6.0, tw + 12.0, 18.0};
                drawRoundedRect(t, chip, radius::control(), Paint::filled(surface::scrim(0.72)));
                t.setFill(palette::mutedForeground());
                t.drawText(cap, chip.x + 6.0, textfit::baseline(chip.y + chip.h * 0.5, kChipPx), kChipPx, font::sans());
                capLeft = chip.x - 4.0;
            }
        }
        // The magnification, while zoomed — it fades with the LIVE zoom, so it leaves as the view
        // eases back to fit (R-UI-13)
        const double zl = mZoom.value();
        const double za = std::clamp((zl - 1.0) * 5.0, 0.0, 1.0);
        if (za > 0.001 && fr.w > 0)
        {
            const std::string z = std::to_string((int)std::lround(zl * 100.0)) + "%";
            const double tw = t.measureText(z, kChipPx, font::mono());
            const Rect chip{fr.right() - 8.0 - tw - 12.0, fr.bottom() - 8.0 - 18.0, tw + 12.0, 18.0};
            drawRoundedRect(t, chip, radius::control(), Paint::filled(fade(surface::scrim(0.72), za)));
            t.setFill(fade(palette::foreground(), za));
            t.drawText(z, chip.x + 6.0, textfit::baseline(chip.y + chip.h * 0.5, kChipPx), kChipPx, font::mono());
        }
        // The capture button, left of the caption (Grade — R-UI-11). Fades with its intent.
        const double ca = mCaptureAmt.value();
        mCaptureRect = Rect{0, 0, 0, 0};
        if (ca > 0.001)
        {
            const Rect b{capLeft - 18.0, kInset + 6.0, 18.0, 18.0};
            const double hv = mCaptureHover.amount(0);
            drawRoundedRect(t, b, radius::control(), Paint::filled(lerpColor(surface::scrim(0.72), palette::whiteAlpha(0.18), hv * 0.6)));
            glyph::camera(t, Rect{b.x + 3.5, b.y + 3.5, b.w - 7.0, b.h - 7.0}, fade(lerpColor(palette::mutedForeground(), palette::white(), hv), ca));
            if (ca > 0.5) mCaptureRect = b;
        }
    }
}
}
