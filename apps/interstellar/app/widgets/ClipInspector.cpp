#include "ClipInspector.h"
#include "Timecode.h"
#include "CommandLine.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/SectionHeader.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kRowH = 22.75;
        constexpr double kBtnH = 26.0;
        constexpr int kIdentityRows = 6;    // name, source, reel, source timecode, track, provenance
        constexpr int kPlacementRows = 5;   // at, in, out, duration, speed
        constexpr int kMixRows = 2;         // opacity, gain
        Color fade(Color c, double a) { c.a *= a; return c; }
        double headerH() { return cosmo_v2::kSectionHeaderHeight; }
        double actionsTop() { return 3 * headerH() + (kIdentityRows + kPlacementRows + kMixRows) * kRowH + headerH(); }
        double contentH() { return actionsTop() + kBtnH + kPadX; }
        const char *provName(interstellar::Provenance p)
        {
            switch (p)
            {
            case interstellar::Provenance::Inherited: return "inherited from base";
            case interstellar::Provenance::Overridden: return "overridden in this version";
            case interstellar::Provenance::Dangling: return "dangling";
            default: return "local";
            }
        }
    }

    ClipInspector::ClipInspector()
    {
        clipToBounds = true;
        auto style = [](cosmo_v2::PillButton &b, bool destructive) {
            b.idleBox = {Paint::filledStroked(Color{0, 0, 0, 0}, destructive ? palette::destructive() : palette::border(), 1.0), radius::control()};
            b.idleText = {destructive ? palette::destructive() : palette::foreground(), 10.0, font::sans()};
            b.activeText = {palette::white(), 10.0, font::sans()};
            b.hoverEmphasis = destructive ? palette::destructive() : palette::primary();
            b.height.set(kBtnH);
        };
        mSplit = std::make_shared<cosmo_v2::PillButton>("Split at playhead");
        style(*mSplit, false);
        mSplit->onClick = [this] {
            if (mHas && onCommand) onCommand("clip split " + cmd::quote(mClip.id) + " --at " + cmd::seconds(mPlayhead, mFps));
        };
        addChild(mSplit);
        mDelete = std::make_shared<cosmo_v2::PillButton>("Delete clip");
        style(*mDelete, true);
        mDelete->onClick = [this] { if (mHas && onCommand) onCommand("clip delete " + cmd::quote(mClip.id)); };
        addChild(mDelete);
    }

    void ClipInspector::bind(const interstellar::AppModel &m)
    {
        mFps = m.fps > 0 ? m.fps : 24.0;
        mPlayhead = m.playhead;
        mHas = false;
        for (const auto &c : m.clips)
            if (c.id == m.selectedClip) { mClip = c; mHas = true; break; }
        mTrackName.clear();
        mReel.clear();
        mSourceTc.clear();
        if (mHas && !mClip.audio)
            for (const auto &r : m.rack)
                if (r.rackObj == mClip.src)
                {
                    // R-XCH-5: the reel, and the source timecode at the clip's in — the file's own clock
                    mReel = r.reel;
                    if (!r.timecode.empty())
                    {
                        const auto rate = interstellar::xch::TcRate::of(r.mediaFps > 0 ? r.mediaFps : mFps);
                        long long f = 0;
                        if (interstellar::xch::framesFromTc(r.timecode, rate, f))
                            mSourceTc = interstellar::xch::tcFromFrames(f + std::llround(mClip.in * rate.fps), rate);
                    }
                }
        if (mHas)
            for (const auto &tk : m.tracks) if (tk.id == mClip.track) { mTrackName = tk.name.empty() ? tk.id : tk.name; break; }
        if (mHas && mClip.id != mLastId)
        {
            if (!mLastId.empty()) mSwapPending = true;
            mLastId = mClip.id;
        }
        mSplit->enabled = mHas;
        mDelete->enabled = mHas;
    }

    void ClipInspector::layout()
    {
        const double w = width.value(), h = height.value();
        mScroll.setExtent(0.0, h, contentH());
        const double y = actionsTop() - mScroll.value();
        const double bw = (w - 2 * kPadX - 6.5) * 0.5;
        mSplit->x.set(kPadX); mSplit->y.set(y); mSplit->width.set(std::max(0.0, bw));
        mDelete->x.set(kPadX + bw + 6.5); mDelete->y.set(y); mDelete->width.set(std::max(0.0, bw));
        // scrolled out of the column: neither drawn nor clickable
        const bool inView = y + kBtnH > 0.0 && y < h;
        mSplit->visible = mDelete->visible = inView;
    }

    void ClipInspector::advance(double nowMs)
    {
        if (!mInit) { mContent.set(mHas ? 1.0 : 0.0); mHasApplied = mHas; mInit = true; }
        if (mHas != mHasApplied)
        {
            mContent.animateTo(mHas ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mHasApplied = mHas;
        }
        if (mSwapPending)
        {
            mSwap.set(0.0);
            mSwap.animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSwapPending = false;
        }
        mContent.update(nowMs);
        mSwap.update(nowMs);
        mScroll.advance(nowMs);
        mSplit->opacity.set(std::max(0.0, mContent.value()));
        mDelete->opacity.set(std::max(0.0, mContent.value()));
        layout();
        Segment::advance(nowMs);
    }

    void ClipInspector::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0.5, 0); t.lineTo(0.5, h); t.strokePath();

        const double ca = mContent.value();
        if (ca < 0.999)
        {
            const double a = 1.0 - ca;
            const std::string s1 = "No clip selected.";
            const std::string s2 = "Click a clip on the timeline to inspect it.";
            t.setFill(fade(palette::foreground(), a));
            t.drawText(s1, (w - t.measureText(s1, 12.0, font::sansMedium())) * 0.5, h * 0.36, 12.0, font::sansMedium());
            const std::string s2e = textfit::ellipsize(t, s2, w - 2 * kPadX, 11.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(s2e, (w - t.measureText(s2e, 11.0, font::sans())) * 0.5, h * 0.36 + 18.0, 11.0, font::sans());
        }
        if (ca <= 0.001) return;
        const double a = ca * (0.35 + 0.65 * mSwap.value());   // a node switch dips, then settles
        t.save();
        t.clipRect(0, 0, w, h);   // the column's own paint stays inside it (D-12's lesson)
        t.pushLayer(a);
        double y = -mScroll.value();
        auto row = [&](const std::string &label, const std::string &value, const char *fam, const Color &vc) {
            const double cy = y + kRowH * 0.5;
            t.setFill(palette::mutedForeground());
            t.drawText(label, kPadX, textfit::baseline(cy, 10.0), 10.0, font::sans());
            const double lw = 86.0;   // cosmo's SliderRow label column
            const std::string v = textfit::ellipsize(t, value, w - kPadX - (kPadX + lw), 10.0, fam);
            t.setFill(vc);
            t.drawText(v, w - kPadX - t.measureText(v, 10.0, fam), textfit::baseline(cy, 10.0), 10.0, fam);
            y += kRowH;
        };
        y += cosmo_v2::drawSectionHeader(t, kPadX, y, w - 2 * kPadX, "CLIP");
        row("Name", mClip.name.empty() ? mClip.id : mClip.name, font::sans(), palette::foreground());
        row("Source", mClip.srcName.empty() ? mClip.src : mClip.srcName, font::mono(), mClip.offline ? palette::destructive() : palette::foreground());
        row("Reel", mReel.empty() ? std::string("\xE2\x80\x94") : mReel, font::mono(), palette::foreground());
        row("Source TC", mSourceTc.empty() ? std::string("\xE2\x80\x94") : mSourceTc, font::mono(), palette::foreground());
        row("Track", mTrackName, font::sans(), palette::foreground());
        const bool dang = mClip.provenance == interstellar::Provenance::Dangling;
        const Color pc = dang ? palette::destructive()
                              : (mClip.provenance == interstellar::Provenance::Overridden ? palette::primary() : palette::foreground());
        row("Provenance", dang ? std::string("dangling \xE2\x80\x94 base deleted its target") : std::string(provName(mClip.provenance)), font::sans(), pc);
        y += cosmo_v2::drawSectionHeader(t, kPadX, y, w - 2 * kPadX, "PLACEMENT");
        row("At", cmd::timecode(mClip.at, mFps), font::mono(), palette::foreground());
        row("In", cmd::timecode(mClip.in, mFps), font::mono(), palette::foreground());
        row("Out", cmd::timecode(mClip.out, mFps), font::mono(), palette::foreground());
        const double dur = mClip.duration > 0 ? mClip.duration : (mClip.out - mClip.in) / std::max(1e-6, mClip.speed);
        row("Duration", cmd::timecode(dur, mFps), font::mono(), palette::foreground());
        row("Speed", cmd::num(mClip.speed * 100.0) + "%", font::mono(), palette::foreground());
        y += cosmo_v2::drawSectionHeader(t, kPadX, y, w - 2 * kPadX, "MIX");
        row("Opacity", cmd::num(mClip.opacity * 100.0) + "%", font::mono(), palette::foreground());
        row("Gain", mClip.audio ? cmd::num(mClip.gain) + " dB" : std::string("\xE2\x80\x94"), font::mono(), palette::foreground());
        cosmo_v2::drawSectionHeader(t, kPadX, y, w - 2 * kPadX, "ACTIONS");
        t.popLayer();
        t.restore();
        mScroll.drawBar(t, w);
    }

    bool ClipInspector::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Scroll) return mScroll.scrollBy(g.delta.y);
        return Segment::handleGesture(g, local);
    }
}
}
