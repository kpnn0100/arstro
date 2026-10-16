#include "SongBar.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        constexpr double kPlayW = 30.0, kBtnH = 21.0;
        constexpr double kMeterW = 60.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        double dbOf(float lin) { return lin > 0 ? 20.0 * std::log10(lin) : -90.0; }
    }

    SongBar::SongBar()
    {
        height.set(kHeight);
        mMenus = std::make_shared<cosmo_v2::MenuStrip>();
        addChild(mMenus);
    }

    void SongBar::layout()
    {
        const Rect s = hitRect(kSettings);
        mMenus->x.set(s.right() + 6.0);
        mMenus->y.set(0.0);
        mMenus->width.set(mMenus->contentWidth());
        mMenus->height.set(kHeight);
    }

    double SongBar::transportX() const
    {
        // centred when there is room; otherwise right of the menus and a readable name (R4: measured, not fixed)
        const double after = mMenus->x.value() + mMenus->contentWidth() + 14.0 + kNameMin + 24.0;
        return std::max(width.value() * 0.5 - 110.0, after);
    }

    void SongBar::bind(const solaris::AppModel &m)
    {
        mName = m.projectName.empty() ? "Untitled" : m.projectName;
        mDirty = m.dirty;
        mPlayingWanted = m.transport.playing;
        mRecordingWanted = m.transport.recording; // R-MIDI-2
        mPosition = m.transport.position;
        // R-TEMPO-1: the tempo where the time bar is — and what a drag changes: the change in effect there, else the song's
        mBpm = m.tempos.empty() ? m.bpm : m.transport.tempo;
        mTempoId = m.tempos.empty() ? std::string() : m.transport.tempoId;
        mBeatsPerBar = std::max(1, std::atoi(m.sig.c_str()));
        mPeak[0] = m.transport.masterPeak[0];
        mPeak[1] = m.transport.masterPeak[1];
    }

    Rect SongBar::hitRect(int which) const
    {
        const double W = width.value(), cy = kHeight * 0.5;
        switch (which)
        {
        case kHome: return Rect{6.0, cy - 10.0, mWordW + 12.0, 20.0}; // the wordmark (R-UI-3 amended)
        case kSettings: return Rect{12.0 + mWordW + 12.0, cy - kBtnH * 0.5, 66.0, kBtnH};
        case kPlay: return Rect{transportX(), cy - kBtnH * 0.5, kPlayW, kBtnH};
        case kRecord: return Rect{transportX() - 6.5 - kPlayW, cy - kBtnH * 0.5, kPlayW, kBtnH}; // R-MIDI-2: before Play
        case kSave: return Rect{W - 12.0 - 48.0, cy - kBtnH * 0.5, 48.0, kBtnH};
        case kBpm:
        {
            // after the position: JetBrains Mono's advance is 0.6 em, so "000.0.000" at 11 px is 59.4 px wide —
            // a fixed width, so the hit rect needs no render target; "999.9 bpm" fits in u(21)
            const Rect p = hitRect(kPlay);
            return Rect{p.right() + 12.0 + 9 * 0.6 * 11.0 + 12.0, cy - kBtnH * 0.5, 68.25, kBtnH};
        }
        default: return Rect{};
        }
    }

    int SongBar::hitAt(const Point &p) const
    {
        for (int i = kHome; i <= kRecord; ++i)
            if (hitRect(i).contains(p)) return i;
        return kNone;
    }

    void SongBar::advance(double nowMs)
    {
        if (!mInit)
        {
            mPlaying.set(mPlayingWanted ? 1.0 : 0.0);
            mRecLit.set(mRecordingWanted ? 1.0 : 0.0);
            mDirtyDot.set(mDirty ? 1.0 : 0.0);
            mInit = true;
        }
        auto toward = [&](AnimatedProperty &p, double want, double ms) {
            if (std::fabs(p.value() - want) > 1e-4 && (!p.isAnimating())) p.animateTo(want, ms, Easing::EaseOutCubic, nowMs);
            p.update(nowMs);
        };
        mNowMs = nowMs;
        if (mBpmLast < 0) { mBpmShown.set(mBpm); mBpmLast = mBpm; } // first placement: nowhere to travel from
        else if (mBpm != mBpmLast)
        {
            if (mBpmDrag) mBpmShown.set(mBpm); // the pointer's own steps: already where it is drawn
            else mBpmShown.animateTo(mBpm, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mBpmLast = mBpm;
        }
        mBpmShown.update(nowMs);
        toward(mPlaying, mPlayingWanted ? 1.0 : 0.0, motion::kCrossFadeMs);
        toward(mRecLit, mRecordingWanted ? 1.0 : 0.0, motion::kCrossFadeMs);
        toward(mDirtyDot, mDirty ? 1.0 : 0.0, motion::kCrossFadeMs);
        for (int c = 0; c < 2; ++c)
        {
            // −60…0 dBFS → 0…1; up in 40 ms (a meter's attack), down in 300 ms (its release)
            const double want = std::clamp((dbOf(mPeak[c]) + 60.0) / 60.0, 0.0, 1.0);
            AnimatedProperty &p = mMeter[c];
            if (std::fabs(p.value() - want) > 1e-3)
            {
                const bool rising = want > p.value();
                p.animateTo(want, rising ? 40.0 : motion::kMeterFallMs, rising ? Easing::EaseOutQuad : Easing::EaseOutCubic, nowMs);
            }
            p.update(nowMs);
        }
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool SongBar::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hitAt(local));
            return true;
        case Gesture::Type::Down:
            if (hitAt(local) == kBpm)
            {
                mBpmDrag = true; // the tempo, taken: the pointer's until let go
                mBpmPress = mBpmLive = mBpm;
                mBpmPressY = local.y;
                mBpmSent.clear();
            }
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mBpmDrag)
            {
                const double step = g.shift ? 0.1 : 1.0;
                const double raw = mBpmPress + (mBpmPressY - local.y) / kBpmStepPx * step;
                const double v = std::clamp(std::round(raw / step) * step, 20.0, 999.0);
                char buf[32];
                std::snprintf(buf, sizeof buf, "%g", std::round(v * 10.0) / 10.0);
                mBpmLive = std::round(v * 10.0) / 10.0;
                if (buf != mBpmSent && mBpmLive != mBpm)
                {
                    mBpmSent = buf;
                    if (onCommand) onCommand(tempoLine(buf)); // one line per step that moved it
                }
                return true;
            }
            return Segment::handleGesture(g, local);
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            if (mBpmDrag) { mBpmDrag = false; return true; }
            return Segment::handleGesture(g, local);
        case Gesture::Type::DoubleClick:
            if (hitAt(local) == kBpm && onEditText)
            {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%g", mBpm);
                onEditText(buf, g.pos, [this](const std::string &text) {
                    if (onCommand && !text.empty()) onCommand(tempoLine(text)); // the service refuses what is not a tempo
                });
                return true;
            }
            return Segment::handleGesture(g, local);
        case Gesture::Type::Click:
            switch (hitAt(local))
            {
            case kHome: if (onHome) onHome(); break;
            case kPlay: if (onPlayToggle) onPlayToggle(); break;
            case kRecord: if (onRecordToggle) onRecordToggle(); break;
            case kSave: if (onSave) onSave(); break;
            case kSettings: if (onSettings) onSettings(); break;
            default: break;
            }
            return true;
        default:
            return Segment::handleGesture(g, local);
        }
    }

    void SongBar::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = kHeight, cy = H * 0.5;
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(palette::leftRailBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, H - 0.5); t.lineTo(W, H - 0.5); t.strokePath();

        // the wordmark — Home (R-UI-3 amended): washed on hover, as cosmo's and Interstellar's
        const double wordW = t.measureText("solaris", 13.0, font::sansSemiBold(), -0.03 * 13.0);
        mWordW = wordW + t.measureText(".", 13.0, font::sansSemiBold());
        const double homeHv = mHover.amount(kHome);
        if (homeHv > 0.001) drawRoundedRect(t, hitRect(kHome), radius::control(), Paint::filled(palette::hoverWash(homeHv)));
        t.setFill(palette::foreground());
        t.drawText("solaris", 12.0, textfit::baseline(cy, 13.0), 13.0, font::sansSemiBold(), -0.03 * 13.0);
        t.setFill(palette::primary());
        t.drawText(".", 12.0 + wordW, textfit::baseline(cy, 13.0), 13.0, font::sansSemiBold());

        auto button = [&](int id, const std::string &label, bool primary) {
            const Rect r = hitRect(id);
            const double hv = mHover.amount(id);
            if (primary) drawRoundedRect(t, r, radius::control(), Paint::filled(brighten(palette::primary(), interaction::kHoverFillLift * hv)));
            else if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            if (!label.empty())
            {
                t.setFill(primary ? palette::primaryForeground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.5 + 0.5 * hv));
                t.drawText(label, r.x + (r.w - t.measureText(label, 11.0, font::sans())) * 0.5, textfit::baseline(r.y + r.h * 0.5, 11.0), 11.0, font::sans());
            }
            return r;
        };
        button(kSettings, "Settings", false);

        // the song's name, ellipsized against the space left before the transport; the unsaved dot after it
        const double nameX = mMenus->x.value() + mMenus->contentWidth() + 14.0, nameMax = std::max(0.0, hitRect(kRecord).x - 24.0 - nameX);
        const std::string nm = textfit::ellipsize(t, mName, nameMax, 12.0, font::sansMedium());
        t.setFill(palette::foreground());
        t.drawText(nm, nameX, textfit::baseline(cy, 12.0), 12.0, font::sansMedium());
        if (mDirtyDot.value() > 0.001)
            drawCircle(t, nameX + t.measureText(nm, 12.0, font::sansMedium()) + 8.0, cy, 3.0, Paint::filled(fade(palette::primary(), mDirtyDot.value())));

        // R-MIDI-2: record — a red dot; lit while recording, eased
        {
            const Rect r = hitRect(kRecord);
            const double hv = mHover.amount(kRecord), lit = mRecLit.value();
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), fade(palette::destructive(), 0.3), lit),
                                                                           lerpColor(palette::border(), palette::destructive(), 0.4 * hv + 0.6 * lit), 1.0));
            drawCircle(t, r.x + r.w * 0.5, r.y + r.h * 0.5, 4.0, Paint::filled(lerpColor(lerpColor(palette::mutedForeground(), palette::destructive(), 0.6), palette::destructive(), lit)));
        }
        // transport: play/stop cross-fade
        {
            const Rect r = hitRect(kPlay);
            const double hv = mHover.amount(kPlay), p = mPlaying.value();
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), palette::primaryAlpha(0.25), p),
                                                                           lerpColor(palette::border(), palette::primary(), 0.4 * hv + 0.6 * p), 1.0));
            const Rect g{r.x + (r.w - 10.0) * 0.5, r.y + (r.h - 10.0) * 0.5, 10.0, 10.0};
            if (p < 0.999) glyph::play(t, g, fade(palette::foreground(), 1.0 - p));
            if (p > 0.001) drawRoundedRect(t, Rect{g.x + 1.0, g.y + 1.0, 8.0, 8.0}, radius::control(), Paint::filled(fade(palette::foreground(), p)));
            // position: bar.beat.tick, 1-based like every DAW
            const double beats = std::max(0.0, mPosition);
            const int bar = (int)(beats / mBeatsPerBar) + 1, beat = (int)std::fmod(beats, (double)mBeatsPerBar) + 1;
            const int tick = (int)((beats - std::floor(beats)) * 960.0);
            char buf[48];
            std::snprintf(buf, sizeof buf, "%3d.%d.%03d", bar, beat, tick);
            t.setFill(palette::foreground());
            t.drawText(buf, r.right() + 12.0, textfit::baseline(cy, 11.0), 11.0, font::mono());
            // the tempo: a control (R-UI-13) — a wash on hover, the accent while held, eased from elsewhere
            const Rect br = hitRect(kBpm);
            const double bh = std::max(mHover.amount(kBpm), mBpmDrag ? 1.0 : 0.0);
            if (bh > 0.001) drawRoundedRect(t, br, radius::control(), Paint::filled(palette::hoverWash(bh)));
            std::snprintf(buf, sizeof buf, "%g bpm", std::round(bpmShown() * 10.0) / 10.0);
            t.setFill(mBpmDrag ? palette::primary() : lerpColor(palette::mutedForeground(), palette::foreground(), bh));
            t.drawText(buf, br.x + 4.0, textfit::baseline(cy, 11.0), 11.0, font::mono());
        }

        // master meter, L over R
        {
            const Rect save = hitRect(kSave);
            const Rect m{save.x - 14.0 - kMeterW, cy - 5.0, kMeterW, 10.0};
            for (int c = 0; c < 2; ++c)
            {
                const Rect track{m.x, m.y + c * 6.0, m.w, 4.0};
                drawRoundedRect(t, track, radius::control(), Paint::filled(palette::whiteAlpha(0.06)));
                const double v = mMeter[c].value();
                if (track.w * v >= 2 * radius::control())
                {
                    const Color col = v > 0.95 ? surface::meterHigh() : v > 0.8 ? surface::meterMid() : surface::meterLow();
                    drawRoundedRect(t, Rect{track.x, track.y, track.w * v, track.h}, radius::control(), Paint::filled(col));
                }
            }
        }
        button(kSave, "Save", false);
    }
}
}
