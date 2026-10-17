#include "SongBar.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

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
        // a name as one token of a command line: quoted when it holds a space (the format has no escape: " becomes ')
        std::string quoted(std::string v)
        {
            for (auto &c : v)
                if (c == '"') c = '\'';
            return v.find_first_of(" \t;=#") == std::string::npos && !v.empty() ? v : "\"" + v + "\"";
        }
        constexpr double kChipPad = 8.0, kChevron = 14.0; // the chip's label inset; the ▾ and its gap
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

    double SongBar::chipX() const { return mMenus->x.value() + mMenus->contentWidth() + 14.0; }

    double SongBar::transportX() const
    {
        // centred when there is room; otherwise right of the menus, the version chip and a readable name (R4: measured,
        // not fixed — the chip's width is eased, so the transport slides with it)
        const double after = chipX() + mChipW.value() + 10.0 + kNameMin + 24.0;
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
        mLearning = m.mapLearning; // R-CTRL-1
        mCpu = m.transport.cpu;    // R-PERF-1
        if (!mLearning.empty()) mLearnShown = mLearning;
        // R-VER: the open version — its name on the chip, " · frozen" when it is
        mVersions = m.versions;
        mVersion = m.version;
        mChipWanted = "main";
        for (const auto &v : mVersions)
            if (v.id == mVersion) mChipWanted = v.name + (v.frozen ? " \xC2\xB7 frozen" : "");
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
        case kVersion: return Rect{chipX(), cy - 9.0, mChipW.value(), 18.0}; // R-VER: after the menus
        case kLearn:
        {
            // R-CTRL-1: after the tempo, up to the CPU readout (u(80) at most)
            const double x = hitRect(kBpm).right() + 12.0, right = cpuRect().x - 12.0;
            return Rect{x, cy - kBtnH * 0.5, std::clamp(right - x, 0.0, 260.0), kBtnH};
        }
        default: return Rect{};
        }
    }

    Rect SongBar::cpuRect() const
    {
        // R-PERF-1: before the master meter — "CPU 12%" over its thin bar; u(18) wide
        const Rect save = hitRect(kSave);
        return Rect{save.x - 14.0 - kMeterW - 12.0 - 58.5, kHeight * 0.5 - 9.0, 58.5, 18.0};
    }

    int SongBar::hitAt(const Point &p) const
    {
        for (int i = kHome; i <= kRecord; ++i)
            if (hitRect(i).contains(p)) return i;
        if (mLearn.value() > 0.5 && hitRect(kLearn).contains(p)) return kLearn; // only while it is there
        if (hitRect(kVersion).contains(p)) return kVersion;
        return kNone;
    }

    void SongBar::advance(double nowMs)
    {
        if (!mInit)
        {
            mPlaying.set(mPlayingWanted ? 1.0 : 0.0);
            mRecLit.set(mRecordingWanted ? 1.0 : 0.0);
            mLearn.set(mLearning.empty() ? 0.0 : 1.0);
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
        toward(mLearn, mLearning.empty() ? 0.0 : 1.0, motion::kCrossFadeMs);
        // R-PERF-1: the load, a value catching up to a source it does not control
        if (mCpu != mCpuTarget)
        {
            mCpuShown.animateTo(mCpu, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); // from where it is drawn
            mCpuTarget = mCpu;
        }
        mCpuShown.update(nowMs);
        toward(mDirtyDot, mDirty ? 1.0 : 0.0, motion::kCrossFadeMs);
        // R-VER: the chip — its label cross-fades to the open version's, its width (measured in the paint) eases after
        // it, its ring eases in while a version and not main is open; the first placement sets
        const bool onVersion = !mVersion.empty() && mVersion != "main";
        if (!mChipPlaced)
        {
            mChipTo = mChipWanted;
            mChipT.set(1.0);
            mOnVersion.set(onVersion ? 1.0 : 0.0);
            mChipPlaced = true;
        }
        else if (mChipWanted != mChipTo)
        {
            mChipFrom = mChipTo;
            mChipTo = mChipWanted;
            mChipT.set(0.0);
            mChipT.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        mChipT.update(nowMs);
        toward(mOnVersion, onVersion ? 1.0 : 0.0, motion::kCrossFadeMs);
        if (mChipTextW > 0.0)
        {
            const double want = kChipPad + mChipTextW + kChevron + kChipPad;
            if (mChipWTarget < 0.0) mChipW.set(want); // measured once: placed there
            else if (std::fabs(want - mChipWTarget) > 0.25) mChipW.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
            mChipWTarget = want;
        }
        mChipW.update(nowMs);
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
            case kLearn: if (onCommand) onCommand("map learn --cancel"); break; // R-CTRL-1
            case kVersion: // R-VER: its menu drops from under the chip, not over it
            {
                const Rect c = hitRect(kVersion);
                const Point at{g.pos.x - local.x + c.x, g.pos.y - local.y + c.bottom() + 2.0};
                if (onMenu) onMenu(versionMenu(at), at);
                break;
            }
            default: break;
            }
            return true;
        default:
            return Segment::handleGesture(g, local);
        }
    }

    std::vector<cosmo_v2::ContextMenu::Item> SongBar::versionMenu(Point world)
    {
        // R-VER: the versions as a tree under main (the open one said), then what a version can do — each ONE line
        std::vector<cosmo_v2::ContextMenu::Item> items;
        auto send = [this](const std::string &line) { if (onCommand) onCommand(line); };
        const bool onMain = mVersion.empty() || mVersion == "main";
        items.push_back({std::string("main") + (onMain ? "  \xC2\xB7 open" : ""), [send] { send("version open main"); }});
        std::function<void(const std::string &, const std::string &)> under = [&](const std::string &base, const std::string &indent) {
            for (const auto &v : mVersions)
            {
                if (v.base != base) continue;
                std::string label = indent + v.name;
                if (v.frozen) label += "  \xC2\xB7 frozen";
                if (!v.problem.empty()) label += "  \xC2\xB7 does not stand";
                if (v.id == mVersion) label += "  \xC2\xB7 open";
                items.push_back({label, [send, id = v.id] { send("version open " + id); }});
                under(v.id, indent + "    ");
            }
        };
        under("main", "    ");
        const std::string base = onMain ? std::string() : " --base " + mVersion;
        items.push_back({"New Version\xE2\x80\xA6", [this, world, base] {
                             if (onEditText)
                                 onEditText("", world, [this, base](const std::string &n) {
                                     if (!n.empty() && onCommand) onCommand("version new " + quoted(n) + base + " --open");
                                 });
                         }});
        if (onMain) return items;
        const solaris::VersionModel *cur = nullptr;
        for (const auto &v : mVersions)
            if (v.id == mVersion) cur = &v;
        if (!cur) return items;
        const std::string id = cur->id, name = cur->name;
        items.push_back({cur->frozen ? "Thaw" : "Freeze", [send, id, frozen = cur->frozen] { send((frozen ? "version thaw " : "version freeze ") + id); }});
        items.push_back({"Rebase", [send, id] { send("version rebase " + id); }});
        items.push_back({"Rename\xE2\x80\xA6", [this, world, id, name] {
                             if (onEditText)
                                 onEditText(name, world, [this, id](const std::string &n) {
                                     if (!n.empty() && onCommand) onCommand("set " + id + ".name=" + quoted(n));
                                 });
                         }});
        items.push_back({"Delete Version", [send, id] { send("version delete " + id); }});
        return items;
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

        // R-VER: the version chip — the label cross-faded, the width eased (measured here, for the next frame's ease)
        {
            mChipTextW = t.measureText(mChipTo, 10.0, font::sans());
            Rect r = hitRect(kVersion);
            if (r.w <= 0.0) r.w = kChipPad + mChipTextW + kChevron + kChipPad; // the first frame: not yet placed
            const double hv = mHover.amount(kVersion), ring = mOnVersion.value(), tt = mChipT.value();
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), palette::primaryAlpha(0.12), ring),
                                                                           lerpColor(palette::border(), palette::primary(), 0.7 * ring), 1.0));
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const double room = std::max(0.0, r.w - kChipPad - kChevron - kChipPad + 4.0);
            const Color ink = lerpColor(palette::mutedForeground(), palette::foreground(), std::max(ring, hv));
            if (tt < 0.999 && !mChipFrom.empty())
            {
                t.setFill(fade(ink, 1.0 - tt));
                t.drawText(textfit::ellipsize(t, mChipFrom, room, 10.0, font::sans()), r.x + kChipPad, textfit::baseline(cy, 10.0), 10.0, font::sans());
            }
            t.setFill(fade(ink, tt));
            t.drawText(textfit::ellipsize(t, mChipTo, room, 10.0, font::sans()), r.x + kChipPad, textfit::baseline(cy, 10.0), 10.0, font::sans());
            const double vx = r.right() - kChipPad - 5.0; // the ▾
            t.setStroke(ink, 1.25);
            t.beginPath(); t.moveTo(vx - 3.0, cy - 1.5); t.lineTo(vx, cy + 1.5); t.lineTo(vx + 3.0, cy - 1.5); t.strokePath();
        }

        // the song's name, ellipsized against the space left before the transport; the unsaved dot after it
        const double nameX = chipX() + std::max(mChipW.value(), 0.0) + 10.0, nameMax = std::max(0.0, hitRect(kRecord).x - 24.0 - nameX);
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

        // R-CTRL-1: a learn waiting — what for; a click cancels it
        if (mLearn.value() > 0.001)
        {
            const double a = mLearn.value();
            const Rect r = hitRect(kLearn);
            const double hv = mHover.amount(kLearn);
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::primaryAlpha((0.14 + 0.08 * hv) * a), palette::primaryAlpha(0.6 * a), 1.0));
            Color c = palette::foreground();
            c.a *= a;
            t.setFill(c);
            t.drawText(textfit::ellipsize(t, "MIDI learn \xC2\xB7 " + mLearnShown + " \xE2\x80\x94 move a control", std::max(0.0, r.w - 16.0), 10.0, font::sans()),
                       r.x + 8.0, textfit::baseline(cy, 10.0), 10.0, font::sans());
        }
        // R-PERF-1: the audio thread's load — muted, the destructive colour from 80 %
        {
            const Rect r = cpuRect();
            const double v = std::max(0.0, mCpuShown.value());
            const Color c = lerpColor(palette::mutedForeground(), palette::destructive(), std::clamp((v - 0.7) / 0.1, 0.0, 1.0));
            t.setFill(palette::mutedForeground());
            t.drawText("CPU", r.x, r.y + 10.0, 9.0, font::sansSemiBold(), 0.13 * 9.0);
            char pct[16];
            std::snprintf(pct, sizeof pct, "%d%%", (int)std::lround(v * 100.0));
            t.setFill(c);
            t.drawText(pct, r.right() - t.measureText(pct, 10.0, font::mono()), r.y + 10.0, 10.0, font::mono());
            const Rect track{r.x, r.bottom() - 3.0, r.w, 2.0};
            drawRoundedRect(t, track, radius::pill(), Paint::filled(palette::whiteAlpha(0.06)));
            if (v > 0.0) drawRoundedRect(t, Rect{track.x, track.y, track.w * std::min(1.0, v), track.h}, radius::pill(), Paint::filled(c));
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
