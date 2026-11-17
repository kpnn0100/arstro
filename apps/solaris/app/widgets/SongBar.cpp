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
        constexpr double kToggleW = 24.0, kBtnGap = 6.5; // Loop, Metronome (R-TIME-4 amended); space::gap() between buttons
        constexpr double kReadGap = 9.75;                // space::padX(): before the position, and before the tempo
        constexpr double kSigW = 4 * 0.6 * 11.0;         // "12/8" in JetBrains Mono at 11 px (its advance is 0.6 em)
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
        mMenus->x.set(hitRect(kHome).right() + 6.0); // R-FAM-1: the menus follow the wordmark — Settings… is File's
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
        return std::max(width.value() * 0.5 - 122.0, after); // Record … the signature is 316 wide; Play begins 36.5 into it
    }

    double SongBar::contentRight(bool withSig) const { return hitRect(kBpm).right() + (withSig ? 6.0 + kSigW : 0.0); }

    double SongBar::rightLimit(double fold) const
    {
        const Rect save = hitRect(kSave);
        const double meterX = save.x - 14.0 - kMeterW;
        return (meterX - 12.0 - 58.5) * (1.0 - fold) + meterX * fold - 12.0; // before the CPU readout — or, it folded, the meter
    }

    Rect SongBar::sigRect() const
    {
        const Rect b = hitRect(kBpm);
        return Rect{b.right() + 6.0, b.y, kSigW, b.h};
    }

    void SongBar::bind(const solaris::AppModel &m)
    {
        mName = m.projectName.empty() ? say("Untitled") : m.projectName; // words: shown in the language drawn
        mDirty = m.dirty;
        mPlayingWanted = m.transport.playing;
        mRecordingWanted = m.transport.recording; // R-MIDI-2
        mPosition = m.transport.position;
        // R-TEMPO-1: the tempo where the time bar is — and what a drag changes: the change in effect there, else the song's
        mBpm = m.tempos.empty() ? m.bpm : m.transport.tempo;
        mTempoId = m.tempos.empty() ? std::string() : m.transport.tempoId;
        mBeatsPerBar = std::max(1, std::atoi(m.sig.c_str()));
        mSig = m.sig;
        mLoopWanted = m.transport.loopTo > m.transport.loopFrom; // R-TIME-4 amended: a region is looped (the song's, or a roll's clip)
        mMetroWanted = m.settings.metronome;
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
            if (v.id == mVersion) mChipWanted = v.name + (v.frozen ? " \xC2\xB7 " + say("frozen") : std::string()); // words: the chip cross-fades a change, the window a language
    }

    void SongBar::accessibles(std::vector<A11yNode> &out)
    {
        auto at = [this](int which) { return worldRect(*this, hitRect(which)); };
        out.push_back(a11yNode("bar.home", "button", tr("Home"), at(kHome)));
        if (mChipW.value() > 1.0) out.push_back(a11yNode("bar.version", "button", tr("Version"), at(kVersion), ::arstro::solaris_ui::shown(mChipTo)));
        A11yNode rec = a11yNode("bar.record", "toggle", tr("Record"), at(kRecord), mRecordingWanted ? tr("on") : tr("off"));
        rec.checked = mRecordingWanted;
        out.push_back(rec);
        out.push_back(a11yNode("bar.play", "button", mPlayingWanted ? tr("Stop") : tr("Play"), at(kPlay)));
        A11yNode loop = a11yNode("bar.loop", "toggle", tr("Loop"), at(kLoop), mLoopWanted ? tr("on") : tr("off"));
        loop.checked = mLoopWanted;
        out.push_back(loop);
        A11yNode metro = a11yNode("bar.metronome", "toggle", tr("Metronome"), at(kMetronome), mMetroWanted ? tr("on") : tr("off"));
        metro.checked = mMetroWanted;
        out.push_back(metro);
        char b[32];
        std::snprintf(b, sizeof b, "%g bpm", std::round(mBpm * 10.0) / 10.0);
        A11yNode bpm = a11yNode("bar.tempo", "slider", tr("Tempo"), at(kBpm), b);
        bpm.step = [this](int dir, bool fine) { // the drag's step, ONE line
            const double step = fine ? 0.1 : 1.0, v = std::clamp(std::round((mBpm + dir * step) / step) * step, 20.0, 999.0);
            char t[32];
            std::snprintf(t, sizeof t, "%g", std::round(v * 10.0) / 10.0);
            if (onCommand) onCommand(tempoLine(t));
        };
        out.push_back(bpm);
        out.push_back(a11yNode("bar.save", "button", tr("Save"), at(kSave), mDirty ? tr("unsaved") : std::string()));
    }

    Rect SongBar::hitRect(int which) const
    {
        const double W = width.value(), cy = kHeight * 0.5;
        switch (which)
        {
        case kHome: return Rect{6.0, cy - 10.0, mWordW + 12.0, 20.0}; // the wordmark (R-UI-3 amended)
        case kPlay: return Rect{transportX(), cy - kBtnH * 0.5, kPlayW, kBtnH};
        case kRecord: return Rect{transportX() - 6.5 - kPlayW, cy - kBtnH * 0.5, kPlayW, kBtnH}; // R-MIDI-2: before Play
        case kSave: return Rect{W - 12.0 - 48.0, cy - kBtnH * 0.5, 48.0, kBtnH};
        case kLoop: return Rect{transportX() + kPlayW + kBtnGap, cy - kBtnH * 0.5, kToggleW, kBtnH};                 // R-TIME-4 amended: after Play
        case kMetronome: return Rect{transportX() + kPlayW + kBtnGap + kToggleW + kBtnGap, cy - kBtnH * 0.5, kToggleW, kBtnH};
        case kBpm:
        {
            // after the position: JetBrains Mono's advance is 0.6 em, so "000.0.000" at 11 px is 59.4 px wide —
            // a fixed width, so the hit rect needs no render target; "999.9 bpm" fits in u(21)
            const Rect p = hitRect(kMetronome);
            return Rect{p.right() + kReadGap + 9 * 0.6 * 11.0 + kReadGap, cy - kBtnH * 0.5, 68.25, kBtnH};
        }
        case kVersion: return Rect{chipX(), cy - 9.0, mChipW.value(), 18.0}; // R-VER: after the menus
        case kLearn:
        {
            // R-CTRL-1: after the tempo and the signature (as much of it as shows), up to the CPU readout — or the meter,
            // the readout folded (R-TIME-4 amended) — u(80) at most; every term the eased one, so nothing here jumps
            const double x = hitRect(kBpm).right() + 12.0 + mSigAmt.value() * (6.0 + kSigW), right = rightLimit(mCpuFold.value());
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
        for (int i : {(int)kLoop, (int)kMetronome})
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
            mLoopLit.set(mLoopWanted ? 1.0 : 0.0);
            mMetroLit.set(mMetroWanted ? 1.0 : 0.0);
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
        toward(mLoopLit, mLoopWanted ? 1.0 : 0.0, motion::kCrossFadeMs);
        toward(mMetroLit, mMetroWanted ? 1.0 : 0.0, motion::kCrossFadeMs);
        // R4 (R-TIME-4 amended): the bar gives way before its content — the signature folds when the transport would
        // reach the CPU readout; while a learn waits and its pill would have under kLearnMin, the readout folds too
        {
            const bool learning = !mLearning.empty();
            const bool cpuFold = learning && rightLimit(0.0) - (contentRight(false) + 12.0) < kLearnMin;
            const double limit = rightLimit(cpuFold ? 1.0 : 0.0);
            const bool sig = contentRight(true) <= limit && (!learning || limit - (contentRight(true) + 12.0) >= kLearnMin);
            if (!mFoldInit) { mSigAmt.set(sig ? 1.0 : 0.0); mCpuFold.set(cpuFold ? 1.0 : 0.0); mFoldInit = width.value() > 1.0; }
            toward(mSigAmt, sig ? 1.0 : 0.0, motion::kCrossFadeMs);
            toward(mCpuFold, cpuFold ? 1.0 : 0.0, motion::kCrossFadeMs);
        }
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
            case kLoop: if (onLoopToggle) onLoopToggle(); break;                 // R-TIME-4 amended: ONE line, the App's
            case kMetronome: if (onMetronomeToggle) onMetronomeToggle(); break;
            case kSave: if (onSave) onSave(); break;
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
        items.push_back({std::string("main") + (onMain ? "  \xC2\xB7 " + say("open") : std::string()), [send] { send("version open main"); }});
        std::function<void(const std::string &, const std::string &)> under = [&](const std::string &base, const std::string &indent) {
            for (const auto &v : mVersions)
            {
                if (v.base != base) continue;
                std::string label = indent + v.name;
                if (v.frozen) label += "  \xC2\xB7 " + say("frozen");
                if (!v.problem.empty()) label += "  \xC2\xB7 " + say("does not stand");
                if (v.id == mVersion) label += "  \xC2\xB7 " + say("open");
                items.push_back({label, [send, id = v.id] { send("version open " + id); }});
                under(v.id, indent + "    ");
            }
        };
        under("main", "    ");
        const std::string base = onMain ? std::string() : " --base " + mVersion;
        items.push_back({say("New Version\xE2\x80\xA6"), [this, world, base] {
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
        items.push_back({cur->frozen ? say("Thaw") : say("Freeze"), [send, id, frozen = cur->frozen] { send((frozen ? "version thaw " : "version freeze ") + id); }});
        items.push_back({say("Rebase"), [send, id] { send("version rebase " + id); }});
        items.push_back({say("Rename\xE2\x80\xA6"), [this, world, id, name] {
                             if (onEditText)
                                 onEditText(name, world, [this, id](const std::string &n) {
                                     if (!n.empty() && onCommand) onCommand("set " + id + ".name=" + quoted(n));
                                 });
                         }});
        items.push_back({say("Delete Version"), [send, id] { send("version delete " + id); }});
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
                const std::string fit = textfit::ellipsize(t, label, r.w - 8.0, 11.0, font::sans()); // a longer language fits its button
                t.drawText(fit, r.x + (r.w - t.measureText(fit, 11.0, font::sans())) * 0.5, textfit::baseline(r.y + r.h * 0.5, 11.0), 11.0, font::sans());
            }
            return r;
        };

        // R-VER: the version chip — the label cross-faded, the width eased (measured here, for the next frame's ease)
        {
            mChipTextW = t.measureText(::arstro::solaris_ui::shown(mChipTo), 10.0, font::sans());
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
                t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(mChipFrom), room, 10.0, font::sans()), r.x + kChipPad, textfit::baseline(cy, 10.0), 10.0, font::sans());
            }
            t.setFill(fade(ink, tt));
            t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(mChipTo), room, 10.0, font::sans()), r.x + kChipPad, textfit::baseline(cy, 10.0), 10.0, font::sans());
            const double vx = r.right() - kChipPad - 5.0; // the ▾
            t.setStroke(ink, 1.25);
            t.beginPath(); t.moveTo(vx - 3.0, cy - 1.5); t.lineTo(vx, cy + 1.5); t.lineTo(vx + 3.0, cy - 1.5); t.strokePath();
        }

        // the song's name, ellipsized against the space left before the transport; the unsaved dot after it
        const double nameX = chipX() + std::max(mChipW.value(), 0.0) + 10.0, nameMax = std::max(0.0, hitRect(kRecord).x - 24.0 - nameX);
        const std::string nm = textfit::ellipsize(t, ::arstro::solaris_ui::shown(mName), nameMax, 12.0, font::sansMedium());
        t.setFill(palette::foreground());
        t.drawText(nm, nameX, textfit::baseline(cy, 12.0), 12.0, font::sansMedium());
        // the unsaved dot: `foreground`, not the accent — an orange dot a few pixels from Record's red one read as a
        // second record light (R-UI-2 amended)
        mDirtyDotAt = Point{nameX + t.measureText(nm, 12.0, font::sansMedium()) + 8.0, cy};
        if (mDirtyDot.value() > 0.001) drawCircle(t, mDirtyDotAt.x, mDirtyDotAt.y, 3.0, Paint::filled(fade(palette::foreground(), mDirtyDot.value())));

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
            // R-TIME-4 amended: Loop and Metronome — toggles, lit in the accent while on (eased), their glyphs drawn
            auto toggle = [&](int id, double lit) {
                const Rect b = hitRect(id);
                const double h2 = mHover.amount(id);
                drawRoundedRect(t, b, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), palette::primaryAlpha(0.25), lit),
                                                                               lerpColor(palette::border(), palette::primary(), 0.4 * h2 + 0.6 * lit), 1.0));
                return lerpColor(palette::mutedForeground(), palette::foreground(), std::max(lit, h2));
            };
            {
                const Color ink = toggle(kLoop, mLoopLit.value());
                const Rect b = hitRect(kLoop);
                const double gx = b.x + b.w * 0.5, gy = b.y + b.h * 0.5 + 1.0;
                drawRoundedRect(t, Rect{gx - 6.0, gy - 3.5, 12.0, 7.0}, 3.5, Paint::stroked(ink, 1.25)); // the loop …
                t.setFill(ink);                                                                           // … and which way round
                t.beginPath(); t.moveTo(gx - 1.0, gy - 6.5); t.lineTo(gx + 2.5, gy - 3.5); t.lineTo(gx - 1.0, gy - 0.5); t.closePath(); t.fillPath();
            }
            {
                const Color ink = toggle(kMetronome, mMetroLit.value());
                const Rect b = hitRect(kMetronome);
                const double gx = b.x + b.w * 0.5, gy = b.y + b.h * 0.5;
                t.setStroke(ink, 1.25); // a metronome: its case, its pendulum
                t.beginPath(); t.moveTo(gx - 4.5, gy + 5.0); t.lineTo(gx + 4.5, gy + 5.0); t.lineTo(gx + 2.0, gy - 5.0); t.lineTo(gx - 2.0, gy - 5.0); t.closePath(); t.strokePath();
                t.beginPath(); t.moveTo(gx, gy + 2.5); t.lineTo(gx + 4.0, gy - 4.0); t.strokePath();
            }
            const Rect last = hitRect(kMetronome);
            // position: bar.beat.tick, 1-based like every DAW
            const double beats = std::max(0.0, mPosition);
            const int bar = (int)(beats / mBeatsPerBar) + 1, beat = (int)std::fmod(beats, (double)mBeatsPerBar) + 1;
            const int tick = (int)((beats - std::floor(beats)) * 960.0);
            char buf[48];
            std::snprintf(buf, sizeof buf, "%3d.%d.%03d", bar, beat, tick);
            t.setFill(palette::foreground());
            t.drawText(buf, last.right() + kReadGap, textfit::baseline(cy, 11.0), 11.0, font::mono());
            // the tempo: a control (R-UI-13) — a wash on hover, the accent while held, eased from elsewhere
            const Rect br = hitRect(kBpm);
            const double bh = std::max(mHover.amount(kBpm), mBpmDrag ? 1.0 : 0.0);
            if (bh > 0.001) drawRoundedRect(t, br, radius::control(), Paint::filled(palette::hoverWash(bh)));
            std::snprintf(buf, sizeof buf, "%g bpm", std::round(bpmShown() * 10.0) / 10.0);
            t.setFill(mBpmDrag ? palette::primary() : lerpColor(palette::mutedForeground(), palette::foreground(), bh));
            t.drawText(buf, br.x + 4.0, textfit::baseline(cy, 11.0), 11.0, font::mono());
            // the time signature: a label, where the bars it counts are shown — folded when the bar has no room (R4)
            if (mSigAmt.value() > 0.001)
            {
                t.setFill(fade(palette::mutedForeground(), mSigAmt.value()));
                t.drawText(textfit::ellipsize(t, mSig, kSigW, 11.0, font::mono()), sigRect().x, textfit::baseline(cy, 11.0), 11.0, font::mono());
            }
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
            t.drawText(textfit::ellipsize(t, fillIn(tr("MIDI learn \xC2\xB7 {1} \xE2\x80\x94 move a control"), mLearnShown), std::max(0.0, r.w - 16.0), 10.0, font::sans()),
                       r.x + 8.0, textfit::baseline(cy, 10.0), 10.0, font::sans());
        }
        // R-PERF-1: the audio thread's load — muted, the destructive colour from 80 %
        if (const double ca = 1.0 - mCpuFold.value(); ca > 0.001) // folded (eased) while a learn's pill needs its room (R4)
        {
            const Rect r = cpuRect();
            const double v = std::max(0.0, mCpuShown.value());
            const Color c = fade(lerpColor(palette::mutedForeground(), palette::destructive(), std::clamp((v - 0.7) / 0.1, 0.0, 1.0)), ca);
            char pct[16];
            std::snprintf(pct, sizeof pct, "%d%%", (int)std::lround(v * 100.0));
            t.setFill(fade(palette::mutedForeground(), ca));
            // the label has the room the readout leaves (a longer language's word ellipsized, never over the number)
            t.drawText(textfit::ellipsize(t, tr("CPU"), std::max(0.0, r.w - t.measureText(pct, 10.0, font::mono()) - 6.0), 9.0, font::sansSemiBold()), r.x, r.y + 10.0, 9.0,
                       font::sansSemiBold(), 0.13 * 9.0);
            t.setFill(c);
            t.drawText(pct, r.right() - t.measureText(pct, 10.0, font::mono()), r.y + 10.0, 10.0, font::mono());
            const Rect track{r.x, r.bottom() - 3.0, r.w, 2.0};
            drawRoundedRect(t, track, radius::pill(), Paint::filled(palette::whiteAlpha(0.06 * ca)));
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
        button(kSave, tr("Save"), false);
    }
}
}
