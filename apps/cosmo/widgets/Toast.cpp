#include "Toast.h"
#include "TextFit.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kFontPx = 12.0;       // the ramp's dialog-body size
        constexpr double kPadX = 16.0;         // a modal's 8/16 rhythm
        constexpr double kGlyphGap = 8.0;      // glyph -> words
    }

    Toast::Toast()
    {
        height.set(kHeight);
        inputTransparent = true;
    }

    void Toast::show(Kind kind, const std::string &text)
    {
        // One that replaces a toast still showing cross-fades with it. A setter has no clock: the
        // next advance (or the clocked overload) starts it.
        if (!mSwapWanted && mAmount.value() > 0.001 && !mText.empty() && (text != mText || kind != mKind))
        {
            mPrevText = mText;
            mPrevKind = mKind;
            mSwapWanted = true;
        }
        mText = text;
        mKind = kind;
        mWanted = true;
    }

    void Toast::show(Kind kind, const std::string &text, double nowMs)
    {
        show(kind, text);
        begin(nowMs);
    }

    void Toast::begin(double nowMs)
    {
        if (mSwapWanted)   // it replaced one still showing: the old words fade out as these fade in
        {
            mSwap.set(0.0);
            mSwap.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseInOutCubic, nowMs);
            mSwapWanted = false;
        }
        if (mWanted)
        {
            mAmount.animateTo(1.0, motion::kToastInMs, Easing::EaseOutCubic, nowMs);
            mShownAt = nowMs;   // the hold is counted from the last thing said
            mWanted = false;
        }
    }

    void Toast::advance(double nowMs)
    {
        begin(nowMs);
        mSwap.update(nowMs);
        // Fades in, HOLDS, fades out slowly. The fade eases in AND out: a fade that only eased in
        // would end at its steepest, which is a pop with a curve on it (arstro.rule.animation rule 3).
        if (mAmount.value() > 0.0 && !mAmount.isAnimating() && nowMs - mShownAt > motion::kToastHoldMs)
            mAmount.animateTo(0.0, motion::kToastOutMs, Easing::EaseInOutCubic, nowMs);
        mAmount.update(nowMs);
        Segment::advance(nowMs);
    }

    void Toast::onOverlay(IRenderTarget &t) const
    {
        const double ta = mAmount.value();
        if (ta <= 0.001 || mText.empty()) return;
        // It slides down into place as it fades in, and on up as it fades out — the travel and the
        // fade are one eased value, so it is never seen standing still waiting to fade.
        const double sw = mSwap.value(), slide = (1.0 - ta) * kSlide;
        if (sw < 0.999 && !mPrevText.empty()) paintOne(t, words(mPrevText), mPrevKind, ta * (1.0 - sw), slide, false);
        paintOne(t, words(mText), mKind, ta * sw, slide, true);
    }

    void Toast::paintOne(IRenderTarget &t, const std::string &words, Kind kind, double alpha, double slide,
                         bool keepRect) const
    {
        const double band = width.value();
        const bool marked = kind != Kind::Info;
        const double glyphW = marked ? kGlyph + kGlyphGap : 0.0;
        // R5: the words are fitted to what the band leaves, with the target's real metrics.
        const std::string s = fitTextEnd(t, words, std::max(0.0, std::min(kMaxWidth, band - 2.0 * kSideMargin) - glyphW),
                                         kFontPx, font::sans());
        const double w = t.measureText(s, kFontPx, font::sans()) + 2.0 * kPadX + glyphW;
        const Rect r{(band - w) * 0.5, -slide, w, kHeight};
        if (keepRect) mRect = r;
        auto faded = [alpha](Color c) { c.a *= alpha; return c; };

        // Every kind: the popover's ground and a NEUTRAL border — the accent is not a toast colour.
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(faded(palette::popover()), faded(palette::border()), 1.0));
        if (marked)
        {
            // A warning: amber, a triangle. An error: the destructive red, a crossed circle.
            const Color c = faded(kind == Kind::Error ? palette::destructive() : palette::warning());
            drawRoundedRect(t, Rect{r.x + 1.0, r.y + 1.0, kBarW, r.h - 2.0}, 0.0, Paint::filled(c));
            const double gx = r.x + kPadX + kGlyph * 0.5, gy = r.y + kHeight * 0.5, k = kGlyph * 0.5;
            t.setStroke(c, 1.5);
            if (kind == Kind::Error)
            {
                drawCircle(t, gx, gy, k, Paint::stroked(c, 1.5));
                t.setStroke(c, 1.5);
                t.beginPath(); t.moveTo(gx - 2.25, gy - 2.25); t.lineTo(gx + 2.25, gy + 2.25); t.strokePath();
                t.beginPath(); t.moveTo(gx + 2.25, gy - 2.25); t.lineTo(gx - 2.25, gy + 2.25); t.strokePath();
            }
            else
            {
                t.beginPath(); t.moveTo(gx, gy - k); t.lineTo(gx + k, gy + k - 1.0); t.lineTo(gx - k, gy + k - 1.0); t.closePath(); t.strokePath();
                t.beginPath(); t.moveTo(gx, gy - 1.5); t.lineTo(gx, gy + 1.0); t.strokePath();
                drawCircle(t, gx, gy + 3.0, 0.75, Paint::filled(c));
            }
        }
        t.setFill(faded(palette::foreground()));
        t.drawText(s, r.x + kPadX + glyphW, r.y + kHeight * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
    }
}
}
