// solaris_ui — Timeline: the song's chords (R-ASSIST-1; docs/discussion.md Round 22). A band of their own at the ruler's
// foot, there only while the song has chords: it grows in and out, and the picture's row and the lanes slide under it
// (`top`). Each chord its name over its span, keyed by id — fading in and out, easing to where the model moves it (a time
// edit, a new detection). Nothing here edits: the ruler's and a clip's menus send `chords detect` / `chords clear`.
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kChordInset = 2.0;  // a chord's span inside the band, top and bottom
        constexpr double kChordPad = 6.5;    // space::u(2): its name inside its span
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    Rect Timeline::chordBandRect() const { return Rect{kHeaderW, kRulerH, std::max(0.0, width.value() - kHeaderW), mChordH.value()}; }

    Rect Timeline::chordRectOf(const ChordLive &l) const
    {
        const double a = l.placed ? l.at.value() : l.c.at, n = l.placed ? l.len.value() : l.c.length;
        const double x0 = beatToX(a), x1 = beatToX(a + n);
        return Rect{x0, kRulerH + kChordInset, std::max(0.0, x1 - x0), kChordH - 2.0 * kChordInset};
    }

    Rect Timeline::chordRect(const std::string &id) const
    {
        for (const auto &l : mChords)
            if (!l.gone && l.c.id == id) return chordRectOf(l);
        return Rect{};
    }

    double Timeline::chordAlpha(const std::string &id) const
    {
        for (const auto &l : mChords)
            if (l.c.id == id) return l.placed ? l.alpha.value() : 0.0;
        return 0.0;
    }

    void Timeline::bindChords(const solaris::AppModel &m)
    {
        for (auto &l : mChords) l.gone = true;
        for (const auto &c : m.songChords)
        {
            ChordLive *l = nullptr;
            for (auto &x : mChords)
                if (x.c.id == c.id) l = &x;
            if (!l) { mChords.emplace_back(); l = &mChords.back(); }
            l->c = c;
            l->gone = false;
        }
        mChordsWanted = !m.songChords.empty();
    }

    void Timeline::advanceChords(double nowMs)
    {
        const bool travel = mEver && !reducedMotion(); // a song opened places its band and its chords where they are
        if (mChordsWanted != mChordApplied)
        {
            if (travel) mChordH.animateTo(mChordsWanted ? kChordH : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
            else mChordH.set(mChordsWanted ? kChordH : 0.0);
            mChordApplied = mChordsWanted;
        }
        mChordH.update(nowMs);
        for (auto &l : mChords)
        {
            if (!l.placed)
            {
                l.at.set(l.c.at);
                l.len.set(l.c.length);
                l.atLast = l.c.at;
                l.lenLast = l.c.length;
                l.alpha.set(travel ? 0.0 : 1.0);
                if (travel) l.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.placed = true;
            }
            if (l.c.at != l.atLast)
            {
                l.at.animateTo(l.c.at, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                l.atLast = l.c.at;
            }
            if (l.c.length != l.lenLast)
            {
                l.len.animateTo(l.c.length, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                l.lenLast = l.c.length;
            }
            if (l.gone && !l.alpha.isAnimating() && l.alpha.value() > 0.0) l.alpha.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            l.at.update(nowMs);
            l.len.update(nowMs);
            l.alpha.update(nowMs);
        }
        mChords.erase(std::remove_if(mChords.begin(), mChords.end(), [](const ChordLive &l) { return l.gone && !l.alpha.isAnimating() && l.alpha.value() <= 0.001; }),
                      mChords.end());
    }

    void Timeline::paintChords(IRenderTarget &t) const
    {
        const double h = mChordH.value();
        if (h <= 0.5) return;
        const double W = width.value(), y0 = kRulerH;
        t.save();
        t.clipRect(0, y0, W, h);
        // the ruler grown into it: its ground, its name in the header as the markers' band says its own
        drawRoundedRect(t, Rect{0, y0, W, h}, 0.0, Paint::filled(surface::rulerBg()));
        t.setFill(palette::mutedForeground());
        t.drawText(textfit::ellipsize(t, solaris_strings::upper(tr("Chords")), kHeaderW - 20.0, 8.0, font::sansSemiBold()), 12.0,
                   textfit::baseline(y0 + kChordH * 0.5, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
        t.clipRect(kHeaderW, y0, W - kHeaderW, h);
        for (const auto &l : mChords)
        {
            const double a = l.placed ? l.alpha.value() : 0.0;
            const Rect r = chordRectOf(l);
            if (a <= 0.001 || r.w <= 0.5 || r.right() < kHeaderW || r.x > W) continue;
            drawRoundedRect(t, r, radius::control(), Paint::filled(fade(palette::secondary(), a)));
            drawRoundedRect(t, Rect{r.x, r.y, 2.0, r.h}, 0.0, Paint::filled(fade(palette::foreground(), 0.5 * a))); // where it begins
            // its name at its start, ellipsized against its span (R5) — a chord is the song's data, never translated
            const double room = r.w - kChordPad - 4.0;
            if (room < 8.0) continue;
            t.setFill(fade(palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, l.c.name, room, 10.0, font::sansMedium()), r.x + kChordPad, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0,
                       font::sansMedium());
        }
        t.restore();
        t.setStroke(palette::border(), 1.0); // its foot, as the ruler's
        t.beginPath(); t.moveTo(0, y0 + h - 0.5); t.lineTo(W, y0 + h - 0.5); t.strokePath();
    }
}
}
