#include "ScopePanel.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        const char *kModeNames[4] = {"Histogram", "Waveform", "Parade", "Vector"};
        Color fade(Color c, double a) { c.a *= a; return c; }
        constexpr double kPi = 3.14159265358979323846;
    }

    ScopePanel::ScopePanel()
    {
        clipToBounds = true;
        for (int m = 0; m < kModes; ++m) mModeAmt[m].set(m == mMode ? 1.0 : 0.0);
        mSel.setHovered(mMode);
    }

    void ScopePanel::setData(const ScopeData &d)
    {
        mData = d;
        if (!d.valid) return;
        mWave.set(d.waveform);
        mParade.set(d.parade);
        mVector.set(d.vector);
    }

    void ScopePanel::setMode(int m)
    {
        if (m < 0 || m >= kModes || m == mMode) return;
        mMode = m;   // the cross-fade starts in advance
        mSel.setHovered(m);
    }

    void ScopePanel::setClipWarning(bool on)
    {
        if (on == mClip) return;
        mClip = on;
        if (onClipWarning) onClipWarning(on);
    }

    Rect ScopePanel::modeRect(int m) const
    {
        double x = kPadX - 4.0;
        static const double kW[4] = {62.0, 60.0, 46.0, 44.0};   // measured once for 9.5 px sans + padding
        for (int i = 0; i < m; ++i) x += kW[i];
        return Rect{x, 3.0, kW[m], kHeaderH - 6.0};
    }
    Rect ScopePanel::clipRect() const { return Rect{width.value() - kPadX - 44.0, 3.0, 44.0, kHeaderH - 6.0}; }
    Rect ScopePanel::bodyRect() const
    {
        return Rect{kPadX, kHeaderH + 2.0, std::max(0.0, width.value() - 2 * kPadX), std::max(0.0, height.value() - kHeaderH - kReadoutH - 4.0)};
    }

    std::string ScopePanel::readout() const
    {
        if (!mData.valid) return "no frame";
        char b[160];
        std::snprintf(b, sizeof b, "\xE2\x96\xB2 %.1f%%  \xE2\x96\xBC %.1f%%  \xC2\xB7  levels %d/256", mData.clipHiAny(), mData.clipLoAny(), mData.levelsUsed());
        std::string s = b;
        s += mSourceBits > 0 ? "  \xC2\xB7  " + std::to_string(mSourceBits) + "-bit source, 8-bit preview" : "  \xC2\xB7  8-bit preview";
        return s;
    }

    void ScopePanel::layout() {}

    bool ScopePanel::handleGesture(const Gesture &g, const Point &local)
    {
        auto modeAt = [&](const Point &p) {
            for (int m = 0; m < kModes; ++m) if (modeRect(m).contains(p)) return m;
            return -1;
        };
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            const int m = modeAt(local);
            mHover.setHovered(m >= 0 ? m : clipRect().contains(local) ? 10 : -1);
            return true;
        }
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Click:
        {
            const int m = modeAt(local);
            if (m >= 0) setMode(m);
            else if (clipRect().contains(local)) setClipWarning(!mClip);
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void ScopePanel::advance(double nowMs)
    {
        if (mModeApplied < 0) mModeApplied = mMode;
        else if (mModeApplied != mMode)
        {
            for (int m = 0; m < kModes; ++m)
                mModeAmt[m].animateTo(m == mMode ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mModeApplied = mMode;
        }
        for (auto &a : mModeAmt) a.update(nowMs);
        if (mClip != mClipApplied)
        {
            mClipAmt.animateTo(mClip ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mClipApplied = mClip;
        }
        mClipAmt.update(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        Segment::advance(nowMs);
    }

    void ScopePanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::histogramBg()));
        const Rect b = bodyRect();
        auto grid = [&](double a) {
            for (int k = 0; k <= 4; ++k)
            {
                const double y = b.y + b.h * k / 4.0;
                glyph::line(t, b.x, y, b.right(), y, fade(palette::whiteAlpha(k == 0 || k == 4 ? 0.16 : 0.08), a), 1.0);
            }
        };
        // the histogram: R, G, B as translucent areas in cosmo's channel colours, luma as a line
        if (const double a = mModeAmt[Histogram].value(); a > 0.001 && mData.valid && mData.hist.maxCount > 0)
        {
            grid(a);
            const double mx = std::sqrt((double)mData.hist.maxCount);   // sqrt: the tails stay visible
            auto area = [&](const auto &bins, Color c, bool fill) {
                t.beginPath();
                t.moveTo(b.x, b.bottom());
                for (int i = 0; i < HistogramData::kBins; ++i)
                {
                    const double x = b.x + b.w * i / (HistogramData::kBins - 1.0);
                    t.lineTo(x, b.bottom() - b.h * std::sqrt((double)bins[i]) / mx);
                }
                t.lineTo(b.right(), b.bottom());
                t.closePath();
                if (fill) { t.setFill(fade(c, 0.32 * a)); t.fillPath(); }
                else { t.setStroke(fade(c, 0.55 * a), 1.0); t.strokePath(); }
            };
            area(mData.hist.r, Color::rgba(230, 82, 82), true);
            area(mData.hist.g, Color::rgba(97, 204, 107), true);
            area(mData.hist.b, Color::rgba(107, 148, 245), true);
            area(mData.hist.lum, palette::white(), false);
        }
        // waveform and parade: the rasters stretched over the body, graticule under them
        if (const double a = mModeAmt[Waveform].value(); a > 0.001)
        {
            grid(a);
            if (const int id = mWave.ensure(t)) { t.pushLayer(a); t.drawImage(id, b); t.popLayer(); }
        }
        if (const double a = mModeAmt[Parade].value(); a > 0.001)
        {
            grid(a);
            for (int k = 1; k < 3; ++k) glyph::line(t, b.x + b.w * k / 3.0, b.y, b.x + b.w * k / 3.0, b.bottom(), fade(palette::border(), a), 1.0);
            if (const int id = mParade.ensure(t)) { t.pushLayer(a); t.drawImage(id, b); t.popLayer(); }
        }
        if (const double a = mModeAmt[Vector].value(); a > 0.001)
        {
            // a square, centred: the 100 % circle, the 75 % targets, the skin-tone line
            const double side = std::min(b.w, b.h);
            const Rect sq{b.x + (b.w - side) * 0.5, b.y + (b.h - side) * 0.5, side, side};
            const double cx = sq.x + side * 0.5, cy = sq.y + side * 0.5, r = side * 0.5;
            t.setStroke(fade(palette::whiteAlpha(0.14), a), 1.0);
            t.beginPath();
            for (int k = 0; k <= 64; ++k)
            {
                const double th = 2 * kPi * k / 64.0;
                const double x = cx + r * std::cos(th), y = cy + r * std::sin(th);
                if (k == 0) t.moveTo(x, y); else t.lineTo(x, y);
            }
            t.strokePath();
            glyph::line(t, sq.x, cy, sq.right(), cy, fade(palette::whiteAlpha(0.07), a), 1.0);
            glyph::line(t, cx, sq.y, cx, sq.bottom(), fade(palette::whiteAlpha(0.07), a), 1.0);
            if (const int id = mVector.ensure(t)) { t.pushLayer(a); t.drawImage(id, sq); t.popLayer(); }
            // the six 75 % targets, where pure R G B C M Y of 75 % land
            struct T { double r, g, b; const char *n; };
            static const T kT[6] = {{0.75, 0, 0, "R"}, {0.75, 0.75, 0, "Yl"}, {0, 0.75, 0, "G"}, {0, 0.75, 0.75, "Cy"}, {0, 0, 0.75, "B"}, {0.75, 0, 0.75, "Mg"}};
            for (const auto &x : kT)
            {
                const double y = 0.2126 * x.r + 0.7152 * x.g + 0.0722 * x.b;
                const double cb = (x.b - y) / 1.8556, cr = (x.r - y) / 1.5748;
                const double px = cx + cb * 2.0 * r, py = cy - cr * 2.0 * r;
                drawRoundedRect(t, Rect{px - 3.0, py - 3.0, 6.0, 6.0}, radius::hairline(), Paint::stroked(fade(palette::whiteAlpha(0.45), a), 1.0));
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(x.n, px + 5.0, py + 3.0, 7.5, font::sans());
            }
            // the skin-tone ("I") line: about 123° from +Cb, counter-clockwise
            const double th = 123.0 * kPi / 180.0;
            glyph::line(t, cx, cy, cx + r * std::cos(th), cy - r * std::sin(th), fade(palette::primaryAlpha(0.6), a), 1.0);
        }
        if (!mData.valid)
        {
            t.setFill(palette::mutedForeground());
            const std::string s = "no frame to measure";
            t.drawText(s, b.x + (b.w - t.measureText(s, 10.0, font::sans())) * 0.5, b.y + b.h * 0.5 + 3.5, 10.0, font::sans());
        }
    }

    void ScopePanel::onOverlay(IRenderTarget &t) const
    {
        // the header over cosmo's histogram title, and the readout line
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, kHeaderH}, 0.0, Paint::filled(palette::histogramBg()));
        for (int m = 0; m < kModes; ++m)
        {
            const Rect r = modeRect(m);
            const double sel = mSel.amount(m), hv = mHover.amount(m);
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), std::max(sel, 0.6 * hv)));
            const double tw = t.measureText(kModeNames[m], 9.5, font::sansMedium());
            t.drawText(kModeNames[m], r.x + (r.w - tw) * 0.5, textfit::baseline(r.y + r.h * 0.5, 9.5), 9.5, font::sansMedium());
            if (sel > 0.001)
                drawRoundedRect(t, Rect{r.x + (r.w - tw) * 0.5, r.bottom() - 1.0, tw, 2.0}, radius::pill(), Paint::filled(palette::primaryAlpha(sel)));
        }
        {
            const Rect r = clipRect();
            const double on = mClipAmt.value(), hv = mHover.amount(10);
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(lerpColor(palette::hoverWash(hv), palette::primaryAlpha(0.22), on),
                                                                          lerpColor(palette::border(), palette::primary(), on), 1.0));
            glyph::warn(t, Rect{r.x + 5.0, r.y + 4.0, 10.0, 10.0}, lerpColor(palette::mutedForeground(), palette::foreground(), std::max(on, hv)), 1.0);
            t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), std::max(on, hv)));
            t.drawText("Clip", r.x + 18.0, textfit::baseline(r.y + r.h * 0.5, 9.5), 9.5, font::sans());
        }
        glyph::line(t, 0, kHeaderH - 0.5, w, kHeaderH - 0.5, palette::border(), 1.0);
        // the readout: what is wrong, in words (mono numbers), red when something clips
        const double ry = h - kReadoutH;
        drawRoundedRect(t, Rect{0, ry, w, kReadoutH}, 0.0, Paint::filled(palette::histogramBg()));
        const bool bad = mData.valid && (mData.clipHiAny() >= 0.5 || mData.clipLoAny() >= 0.5);
        t.setFill(bad ? palette::destructive() : palette::mutedForeground());
        t.drawText(textfit::ellipsize(t, readout(), w - 2 * kPadX, 9.0, font::mono()), kPadX, textfit::baseline(ry + kReadoutH * 0.5, 9.0), 9.0, font::mono());
        glyph::line(t, 0, h - 0.5, w, h - 0.5, palette::border(), 1.0);
    }
}
}
