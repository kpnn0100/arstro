#include "Timeline.h"
#include "ParamMenu.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kZoomRatio = 1.25;   // one Ctrl+wheel notch
        constexpr double kPpq = 960.0;        // a tick (R-TIME-1): what a beat is rounded to
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string beats(double b) { return Timeline::beatText(b); }
        double smooth01(double u)
        {
            u = std::clamp(u, 0.0, 1.0);
            return u * u * (3.0 - 2.0 * u);
        }
        /** How much of a grid level is drawn when its lines are `px` apart (R-UI-10). */
        double room(double px) { return smooth01((px - Timeline::kGridHidePx) / (Timeline::kGridFullPx - Timeline::kGridHidePx)); }
        double deepestPpb() { return Timeline::kZoomPpb * std::pow(kZoomRatio, Timeline::kZoomInSteps); }
        /** Line n of a level is drawn by a COARSER level (a beat on a bar, a half on a beat …). */
        bool coarser(int level, long long n, int beatsPerBar)
        {
            if (level == 1) return n % beatsPerBar == 0;
            return level >= 2 && n % 2 == 0;
        }
        // the lanes' lines: bars strongest, beats next, the divisions faintest (R-UI-10)
        constexpr double kLineA[3] = {0.11, 0.065, 0.04}; // bars · beats · divisions: the subdivisions must READ (R-UI-10: "divide into smaller note")
        // the ruler's ticks: long and bright for a bar, shorter and fainter as the level gets finer
        constexpr double kTickA[3] = {0.15, 0.12, 0.09}, kTickH[3] = {7.0, 4.5, 2.5};
    }

    std::string Timeline::beatText(double beat)
    {
        const double tick = std::round(beat * kPpq);
        if (tick == 0.0) return "0";
        char buf[48];
        for (int d = 0; d <= 6; ++d) // six decimals always suffice: 1e-6 · 960 < ½
        {
            std::snprintf(buf, sizeof buf, "%.*f", d, tick / kPpq);
            if (std::round(std::strtod(buf, nullptr) * kPpq) == tick) break;
        }
        return buf;
    }

    // ---- the grid follows the zoom (R-UI-10, R-TIME-5) --------------------------------------------

    double Timeline::gridSpan(int level) const
    {
        if (level <= 0) return (double)mBeatsPerBar;
        return 1.0 / (double)(1 << std::min(level - 1, kGridLevels - 2)); // 1, 1/2, 1/4 … 1/32
    }

    double Timeline::gridAlpha(int level) const
    {
        if (level < 0 || level >= kGridLevels) return 0.0;
        return room(gridSpan(level) * mPpb.value()); // the EASED zoom: a level fades as the zoom eases
    }

    bool Timeline::atDeepestZoom() const { return mPpb.value() >= deepestPpb() * (1.0 - 1e-9); }

    double Timeline::snapStep() const
    {
        if (atDeepestZoom()) return 0.0; // R-TIME-5: zoomed all the way in, nothing snaps
        for (int l = kGridLevels - 1; l > 0; --l)
            if (gridSpan(l) * mPpb.value() >= kSnapPx) return gridSpan(l);
        return gridSpan(0); // never coarser than a bar
    }

    double Timeline::snap(double beat) const
    {
        const double step = snapStep();
        const double b = step > 0.0 ? std::round(beat / step) * step : beat;
        return std::max(0.0, std::round(b * kPpq) / kPpq);
    }

    std::string Timeline::stepName(double step, int beatsPerBar)
    {
        if (step <= 0.0) return "Off";
        if (beatsPerBar > 1 && step >= beatsPerBar - 1e-9) return "Bar";
        char buf[16];
        std::snprintf(buf, sizeof buf, "1/%lld", (long long)std::llround(4.0 / step)); // the piano roll's note values: a beat = 1/4
        return buf;
    }

    std::string Timeline::snapLabel() const { return stepName(snapStep(), mBeatsPerBar); }

    Timeline::Timeline() { clipToBounds = true; }

    void Timeline::bind(const solaris::AppModel &m)
    {
        mBeatsPerBar = std::max(1, std::atoi(m.sig.c_str()));
        mStrips.clear();
        for (const auto &s : m.strips) mStrips.push_back(StripRef{s.id, s.name, s.kind});
        mLength = m.lengthBeats;
        mPosition = m.transport.position;
        mPlaying = m.transport.playing;
        mLoopFrom = m.transport.loopFrom;
        mLoopTo = m.transport.loopTo;
        if (m.projectPath != mSong)
        {
            // another song: nothing of the last one may travel into it
            mSong = m.projectPath;
            mRowMotion = interstellar_v1::AnimatedRows<Row>();
            mLive.clear();
            mStripes.clear();
            mEver = mBound = mEmptyInit = mLoopInit = false;
        }
        std::map<std::string, int> colourOf; // the service resolves a strip's colour (stable by id)
        for (const auto &st : m.strips) colourOf[st.id] = st.colour;
        mRows.clear();
        std::map<std::string, int> rowOfLane, rowOfStrip;
        for (const auto &l : m.lanes)
        {
            rowOfLane[l.id] = (int)mRows.size();
            mRows.push_back(Row{l.id, l.id, l.name, l.colour});
        }
        for (const auto &c : m.clips)
            if (c.lane.empty() && !rowOfStrip.count(c.track))
            {
                std::string name = c.track;
                for (const auto &st : m.strips)
                    if (st.id == c.track) name = st.name;
                rowOfStrip[c.track] = (int)mRows.size();
                mRows.push_back(Row{"strip:" + c.track, "", name, colourOf[c.track]}); // a clip with no lane: its strip's own row
            }
        mClips.clear();
        for (const auto &c : m.clips)
        {
            ClipView v;
            v.c = c;
            v.row = c.lane.empty() ? rowOfStrip[c.track] : (rowOfLane.count(c.lane) ? rowOfLane[c.lane] : 0);
            v.colour = colourOf.count(c.track) ? colourOf[c.track] : 0;
            for (const auto &p : m.patterns)
                if (p.id == c.pattern) { v.notes = p.notes; v.patternLength = p.length; }
            mClips.push_back(v);
        }
        // a lane with no colour of its own wears its first clip's strip's, so its stripe says something
        for (auto &r : mRows)
            if (r.colour < 0)
                for (const auto &v : mClips)
                    if (v.row == (int)(&r - &mRows[0])) { r.colour = v.colour; break; }

        // what is drawn follows: rows keyed by lane, clips by id; the ones gone stay as ghosts while they fade
        std::vector<std::pair<std::string, Row>> keyed;
        for (const auto &r : mRows)
        {
            keyed.emplace_back(r.key, r);
            mStripes[r.key].want = r.colour;
        }
        // the automation rows, after the lanes, in the same eased list (a lane added slides them down)
        mAutoIds.clear();
        for (const auto &a : m.automations)
        {
            Row ar;
            ar.key = "auto:" + a.id;
            ar.label = a.name;
            ar.automation = a.id;
            keyed.emplace_back(ar.key, ar);
            mAutoIds.push_back(a.id);
            AutoLive &l = mAutos[a.id];
            l.model = a;
            bool same = l.to.size() == a.points.size();
            for (size_t i = 0; same && i < a.points.size(); ++i)
                same = l.to[i].at == a.points[i].at && l.to[i].value == a.points[i].value && l.to[i].shape == a.points[i].shape &&
                       l.to[i].speedIn == a.points[i].speedIn && l.to[i].inflIn == a.points[i].inflIn && l.to[i].speedOut == a.points[i].speedOut &&
                       l.to[i].inflOut == a.points[i].inflOut;
            if (!same)
            {
                l.shownBefore = shownPoints(l); // what is drawn NOW: the next tween starts there
                l.to = a.points;
                l.changed = true;
            }
        }
        for (auto it = mAutos.begin(); it != mAutos.end();)
            it = std::find(mAutoIds.begin(), mAutoIds.end(), it->first) == mAutoIds.end() ? mAutos.erase(it) : std::next(it);
        mRowMotion.sync(keyed, kRowH);
        for (auto &l : mLive) l.gone = true;
        for (const auto &v : mClips)
        {
            ClipLive *l = nullptr;
            for (auto &x : mLive)
                if (x.v.c.id == v.c.id) l = &x;
            if (!l) { mLive.emplace_back(); l = &mLive.back(); }
            l->v = v;
            l->atTarget = v.c.at;
            l->rowTarget = v.row;
            l->gone = false;
        }
        mBound = true;
        if (!mSelected.empty() && clipRect(mSelected).w <= 0) selectClip(""); // the selected clip went away
    }

    const Timeline::ClipLive *Timeline::live(const std::string &id) const
    {
        for (const auto &l : mLive)
            if (!l.gone && l.v.c.id == id) return &l;
        return nullptr;
    }

    double Timeline::clipAlpha(const std::string &id) const
    {
        for (const auto &l : mLive)
            if (l.v.c.id == id) return l.placed ? l.alpha.value() : 0.0;
        return 0.0;
    }

    double Timeline::clipHueAmount(const std::string &id) const
    {
        const ClipLive *l = live(id);
        return l ? l->hueT.value() : 0.0;
    }

    double Timeline::rowY(int i) const
    {
        const auto *r = mRowMotion.byIndex(i);
        if (!r) return i * kRowH;                 // below the last row: where a new lane would go
        return r->placed ? r->liveY() : r->yTarget;
    }

    double Timeline::beatToX(double beat) const { return kHeaderW + beat * mPpb.value() - mScrollX.value(); }
    double Timeline::xToBeat(double x) const { return (x - kHeaderW + mScrollX.value()) / std::max(1e-6, mPpb.value()); }

    int Timeline::rowAt(double y) const
    {
        if (y < kRulerH) return -1;
        for (int i = 0; i < (int)mRows.size(); ++i)   // the rows as drawn — live, mid-tween included
        {
            const Rect r = rowRect(i);
            if (y >= r.y && y < r.bottom()) return i;
        }
        const int r = (int)std::floor((y - kRulerH + mScrollY.value()) / kRowH);
        return std::clamp(r, 0, (int)mRows.size());
    }

    Rect Timeline::rowRect(int i) const { return Rect{0.0, kRulerH + rowY(i) - mScrollY.value(), width.value(), kRowH}; }
    Rect Timeline::rulerRect() const { return Rect{kHeaderW, 0.0, std::max(0.0, width.value() - kHeaderW), kRulerH}; }

    Rect Timeline::clipBox(const ClipView &v, double at, double y) const
    {
        const double top = kRulerH + y - mScrollY.value();
        const double x0 = beatToX(at), x1 = beatToX(at + std::max(0.25, v.c.length));
        return Rect{x0, top + 3.0, std::max(3.0, x1 - x0 - 1.0), kRowH - 6.0};
    }

    Rect Timeline::clipRect(const std::string &id) const
    {
        const ClipLive *l = live(id);
        if (!l) return Rect{};
        if (mDragging && id == mPressClip) return clipBox(l->v, mDragBeat, rowY(mDragRow));
        return l->placed ? clipBox(l->v, l->at.value(), l->row.value() * kRowH) : clipBox(l->v, l->atTarget, l->rowTarget * kRowH);
    }

    std::string Timeline::clipAt(const Point &p) const
    {
        if (p.x < kHeaderW || p.y < kRulerH) return std::string();
        for (auto it = mLive.rbegin(); it != mLive.rend(); ++it) // topmost (drawn last) first; a ghost takes no input
            if (!it->gone && clipRect(it->v.c.id).contains(p)) return it->v.c.id;
        return std::string();
    }

    void Timeline::selectClip(const std::string &id)
    {
        if (id == mSelected) return;
        mPrevSelected = mSelected;
        mSelected = id;
        mSelOut.set(mSelIn.value());   // the old ring fades out from where the new one starts
        mSelIn.set(0.0);
        if (!mPrevSelected.empty()) mSelOut.animateTo(0.0, motion::kSelectMs, Easing::EaseOutCubic, mNowMs);
        if (!mSelected.empty()) mSelIn.animateTo(1.0, motion::kSelectMs, Easing::EaseOutCubic, mNowMs);
        if (onSelect) onSelect(id);
    }

    void Timeline::setDropHint(bool on, double beat, int row, const std::string &label)
    {
        if (on != mDropOn) mDropAmt.animateTo(on ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
        mDropOn = on;
        if (on)
        {
            mDropBeat = beat;
            mDropRow = row;
            mDropLabel = label;
        }
    }

    void Timeline::layout()
    {
        const double viewW = std::max(0.0, width.value() - kHeaderW);
        mScrollX.setExtent(kHeaderW, viewW, std::max(viewW, (mLength + 4.0 * mBeatsPerBar) * mPpbTarget));
        mScrollY.setExtent(kRulerH, std::max(0.0, height.value() - kRulerH), (mRows.size() + mAutoIds.size() + 1) * kRowH);
    }

    void Timeline::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (!mInit) { mPlayhead.set(mPosition); mInit = true; }
        if (mRuler == RulerDrag::Scrub) mPlayhead.set(mScrubBeat); // the pointer is the animation (R-TIME-6)
        else if (mPlaying) mPlayhead.set(mPosition); // continuous: it follows the audio, not a tween
        else if (std::fabs(mPlayhead.value() - mPosition) > 1e-6 && !mPlayhead.isAnimating())
            mPlayhead.animateTo(mPosition, 140.0, Easing::EaseOutCubic, nowMs); // a seek eases
        mPlayhead.update(nowMs);
        mPpb.update(nowMs);
        // the snap step follows the EASED zoom; its name on the ruler cross-fades when it changes (§1)
        if (const std::string name = snapLabel(); mStepNames.empty() || mStepNames.back().text != name)
        {
            const bool first = mStepNames.empty(); // nowhere to travel from: placed
            for (auto &n : mStepNames)
                if (!n.out)
                {
                    n.out = true;
                    n.a.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                }
            mStepNames.push_back(StepName{name});
            if (first) mStepNames.back().a.set(1.0);
            else mStepNames.back().a.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        for (auto &n : mStepNames) n.a.update(nowMs);
        mStepNames.erase(std::remove_if(mStepNames.begin(), mStepNames.end(), [](const StepName &n) { return n.out && !n.a.isAnimating() && n.a.value() <= 0.001; }),
                         mStepNames.end());
        mSelIn.update(nowMs);
        mSelOut.update(nowMs);
        mDropAmt.update(nowMs);
        // the loop region: appears and goes with a fade; moved by anyone it eases there (§1)
        {
            const bool on = mLoopTo > mLoopFrom;
            if (!mLoopInit)
            {
                mLoopA.set(mLoopFrom);
                mLoopB.set(mLoopTo);
                mLoopAmt.set(on ? 1.0 : 0.0);
                mLoopALast = mLoopFrom;
                mLoopBLast = mLoopTo;
                mLoopOnLast = on;
                mLoopInit = true;
            }
            if (on != mLoopOnLast)
            {
                mLoopAmt.animateTo(on ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                mLoopOnLast = on;
            }
            if (on && (mLoopFrom != mLoopALast || mLoopTo != mLoopBLast))
            {
                if (mLoopAmt.value() <= 0.001)
                {
                    // from nothing: it fades in where it is — there is nowhere to travel from
                    mLoopA.set(mLoopFrom);
                    mLoopB.set(mLoopTo);
                }
                else
                {
                    mLoopA.animateTo(mLoopFrom, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                    mLoopB.animateTo(mLoopTo, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                }
                mLoopALast = mLoopFrom;
                mLoopBLast = mLoopTo;
            }
            mLoopA.update(nowMs);
            mLoopB.update(nowMs);
            mLoopAmt.update(nowMs);
        }
        for (auto &kv : mAutos)
        {
            AutoLive &l = kv.second;
            if (!l.placed || !mEver) { l.from = l.to; l.t.set(1.0); l.placed = true; l.changed = false; }
            else if (l.changed)
            {
                // a curve the model changed eases there: point by point, or (a point more or fewer) cross-faded
                l.from = l.shownBefore;
                l.t.set(0.0);
                l.t.animateTo(1.0, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                l.changed = false;
            }
            l.t.update(nowMs);
        }
        advanceAuto(nowMs); // a click's point waiting out the double-click (R-AUTO-11)
        const bool fadeIn = mEver && !reducedMotion();
        mRowMotion.advance(nowMs);
        for (auto &l : mLive)
        {
            const double aWant = l.gone ? 0.0 : 1.0;
            if (!l.placed)
            {
                l.at.set(l.atTarget);
                l.row.set(l.rowTarget);
                l.atLast = l.atTarget;
                l.rowLast = l.rowTarget;
                l.alpha.set(fadeIn ? 0.0 : 1.0);
                l.aLast = fadeIn ? 0.0 : 1.0;
                l.hueFrom = l.hueTo = surface::track(l.v.colour);
                l.hueLast = l.v.colour;
                l.placed = true;
            }
            if (l.v.colour != l.hueLast)
            {
                l.hueFrom = lerpColor(l.hueFrom, l.hueTo, l.hueT.value());
                l.hueTo = surface::track(l.v.colour);
                l.hueT.set(0.0);
                l.hueT.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.hueLast = l.v.colour;
            }
            l.hueT.update(nowMs);
            // the same 200 ms ease as the rows, so a clip and its lane travel together
            if (l.atTarget != l.atLast) { l.at.animateTo(l.atTarget, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.atLast = l.atTarget; }
            if (l.rowTarget != l.rowLast) { l.row.animateTo(l.rowTarget, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.rowLast = l.rowTarget; }
            if (aWant != l.aLast) { l.alpha.animateTo(aWant, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); l.aLast = aWant; }
            l.at.update(nowMs);
            l.row.update(nowMs);
            l.alpha.update(nowMs);
        }
        mLive.erase(std::remove_if(mLive.begin(), mLive.end(), [](const ClipLive &l) { return l.gone && !l.alpha.isAnimating() && l.alpha.value() <= 0.001; }),
                    mLive.end());
        auto stripeColour = [](int c) { return c >= 0 ? surface::track(c) : palette::whiteAlpha(0.12); };
        for (auto &kv : mStripes)
        {
            Stripe &st = kv.second;
            if (!st.placed || !mEver) { st.from = st.to = stripeColour(st.want); st.last = st.want; st.t.set(1.0); st.placed = true; }
            else if (st.want != st.last)
            {
                st.from = lerpColor(st.from, st.to, st.t.value());
                st.to = stripeColour(st.want);
                st.last = st.want;
                st.t.set(0.0);
                st.t.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
            st.t.update(nowMs);
        }
        const double emptyWant = mRows.empty() ? 1.0 : 0.0;
        if (!mEmptyInit) { mEmptyAmt.set(emptyWant); mEmptyLast = emptyWant; mEmptyInit = true; }
        else if (emptyWant != mEmptyLast) { mEmptyAmt.animateTo(emptyWant, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); mEmptyLast = emptyWant; }
        mEmptyAmt.update(nowMs);
        if (mBound) mEver = true;
        mScrollX.advance(nowMs);
        mScrollY.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    void Timeline::loopSpan(double &a, double &b) const
    {
        if (mLoopDragging) { a = std::min(mLoopGrab, mLoopLive); b = std::max(mLoopGrab, mLoopLive); return; }
        if (braceDragging()) { a = mBraceA; b = mBraceB; return; } // the pointer's (R-TIME-6)
        a = mLoopA.value();
        b = mLoopB.value();
    }

    Rect Timeline::loopRect() const
    {
        if (!mLoopDragging && !braceDragging() && mLoopAmt.value() <= 0.001) return Rect{};
        double a, b;
        loopSpan(a, b);
        return Rect{beatToX(a), kRulerH - 7.0, std::max(0.0, beatToX(b) - beatToX(a)), 5.0};
    }

    bool Timeline::loopGesture(const Gesture &g, const Point &local)
    {
        // R-EDM-7: Shift-drag on the ruler sets the loop — the brace is the pointer's while held, ONE
        // `transport loop` on release, where it was let go; a click inside the brace clears it
        switch (g.type)
        {
        case Gesture::Type::Down:
            if (!rulerRect().contains(local) || !g.shift) return false;
            mLoopDragging = true;
            mLoopGrab = mLoopLive = snap(xToBeat(local.x));
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (!mLoopDragging) return false;
            mLoopLive = snap(xToBeat(local.x));
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (!mLoopDragging) return false;
            double a, b;
            loopSpan(a, b);
            mLoopDragging = false;
            if (b - a < 0.5 / kPpq) return true; // a Shift-click (both ends on one line): no region
            mLoopA.set(a);
            mLoopB.set(b);
            mLoopAmt.set(1.0);
            mLoopALast = a;
            mLoopBLast = b;
            mLoopOnLast = true;
            if (onCommand) onCommand("transport loop " + beats(a) + " " + beats(b));
            return true;
        }
        case Gesture::Type::Click:
        {
            if (!rulerRect().contains(local) || g.shift) return g.shift && rulerRect().contains(local);
            const Rect br = loopRect();
            if (br.w > 0 && local.x >= br.x && local.x <= br.right() && mLoopTo > mLoopFrom)
            {
                if (onCommand) onCommand("transport loop off");
                return true;
            }
            return false; // outside the brace: the ruler's own click (a seek)
        }
        default:
            return false;
        }
    }

    bool Timeline::rulerGesture(const Gesture &g, const Point &local)
    {
        // R-TIME-6: a drag on the ruler. A press waits — a click is still the seek (R-TIME-5) or clears the
        // loop (R-EDM-7) — and the drag decides once, by where it began: on the brace's end or body (its
        // lower half) it moves the loop, ONE `transport loop` on release; anywhere else the playhead is the
        // pointer's, on the grid you see, with a seek at each new line (as a fader sends each step) so a
        // playing song is heard from there. A Shift-drag is the loop's own (loopGesture).
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mRuler = RulerDrag::None;
            if (!rulerRect().contains(local) || g.shift) return false;
            mRuler = RulerDrag::Pending;
            mRulerDownBeat = xToBeat(local.x);
            mPressClip.clear();
            mRulerZone = RulerDrag::Scrub;
            const Rect br = loopRect();
            if (br.w > 0 && mLoopTo > mLoopFrom && local.y >= kRulerH * 0.5)
            {
                if (std::fabs(local.x - br.x) <= kBraceGrip) mRulerZone = RulerDrag::LoopFrom;
                else if (std::fabs(local.x - br.right()) <= kBraceGrip) mRulerZone = RulerDrag::LoopTo;
                else if (local.x > br.x && local.x < br.right()) mRulerZone = RulerDrag::LoopBody;
            }
            return true;
        }
        case Gesture::Type::DragStart:
            if (mRuler != RulerDrag::Pending) return mRuler != RulerDrag::None;
            mRuler = mRulerZone;
            if (mRuler == RulerDrag::Scrub) mScrubSent = -1.0;
            else
            {
                mBraceA0 = mBraceA = mLoopFrom;
                mBraceB0 = mBraceB = mLoopTo;
            }
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (mRuler == RulerDrag::None || mRuler == RulerDrag::Pending) return false;
            const double beat = xToBeat(local.x);
            if (mRuler == RulerDrag::Scrub)
            {
                mScrubBeat = snap(beat);
                if (mScrubBeat != mScrubSent && onCommand)
                {
                    mScrubSent = mScrubBeat;
                    onCommand("transport seek " + beats(mScrubBeat));
                }
                return true;
            }
            // the brace: moved by its body (its length kept), resized by an end — never shorter than a step
            const double d = beat - mRulerDownBeat, minLen = std::max(snapStep(), 1.0 / kPpq);
            if (mRuler == RulerDrag::LoopBody)
            {
                mBraceA = snap(mBraceA0 + d);
                mBraceB = mBraceA + (mBraceB0 - mBraceA0);
            }
            else if (mRuler == RulerDrag::LoopFrom) mBraceA = std::min(snap(mBraceA0 + d), mBraceB0 - minLen);
            else mBraceB = std::max(snap(mBraceB0 + d), mBraceA0 + minLen);
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            const RulerDrag was = mRuler;
            mRuler = RulerDrag::None;
            if (was == RulerDrag::None || was == RulerDrag::Pending) return false; // a click: the seek, or clearing the loop
            if (was == RulerDrag::Scrub)
            {
                mPlayhead.set(mScrubBeat); // where it was let go — the pointer was the animation
                if (mScrubBeat != mScrubSent && onCommand) onCommand("transport seek " + beats(mScrubBeat));
                return true;
            }
            // the brace stays where it was let go, then the one line; a refusal eases it home
            mLoopA.set(mBraceA);
            mLoopB.set(mBraceB);
            mLoopAmt.set(1.0);
            mLoopALast = mBraceA;
            mLoopBLast = mBraceB;
            mLoopOnLast = true;
            if ((mBraceA != mBraceA0 || mBraceB != mBraceB0) && onCommand)
                onCommand("transport loop " + beats(mBraceA) + " " + beats(mBraceB));
            return true;
        }
        case Gesture::Type::Click:
            mRuler = RulerDrag::None;
            return false;
        default:
            return false;
        }
    }

    bool Timeline::handleGesture(const Gesture &g, const Point &local)
    {
        if (autoGesture(g, local)) return true; // an automation row's curve and points (R-AUTO-6)
        if (rulerGesture(g, local)) return true; // the ruler dragged: the playhead, the loop's brace (R-TIME-6)
        if (loopGesture(g, local)) return true; // the loop region on the ruler (R-EDM-7)
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            const std::string id = clipAt(local);
            int h = -1;
            for (size_t i = 0; i < mClips.size(); ++i)
                if (mClips[i].c.id == id) h = (int)i;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Down:
            mPressClip = clipAt(local);
            if (const ClipLive *l = live(mPressClip)) mGrab = xToBeat(local.x) - l->at.value(); // where it is DRAWN: a drag never teleports
            return true;
        case Gesture::Type::DragStart:
            if (!mPressClip.empty())
            {
                mDragging = true;
                selectClip(mPressClip);
            }
            [[fallthrough]];
        case Gesture::Type::Drag:
            if (mDragging)
            {
                mDragBeat = snap(xToBeat(local.x) - mGrab);
                int row = rowAt(local.y);
                for (const auto &v : mClips)
                    if (v.c.id == mPressClip && (row < 0 || row >= (int)mRows.size() || mRows[(size_t)row].lane.empty())) row = v.row;
                mDragRow = row; // a clip moves between LANES; a strip's own row is not a lane
            }
            return true;
        case Gesture::Type::Drop:
        case Gesture::Type::Up:
            if (mDragging)
            {
                mDragging = false;
                std::string send;
                for (auto &l : mLive)
                    if (!l.gone && l.v.c.id == mPressClip)
                    {
                        const ClipView &v = l.v;
                        std::string line = "clip move " + v.c.id;
                        bool any = false;
                        if (std::fabs(mDragBeat - v.c.at) > 1e-9) { line += " --at " + beats(mDragBeat); any = true; }
                        if (mDragRow != v.row) { line += " --lane " + q(mRows[(size_t)mDragRow].lane); any = true; }
                        // it is where the pointer left it — the pointer was the animation. The model confirms
                        // it there; a refusal puts the old target back and it eases home.
                        l.at.set(mDragBeat);
                        l.row.set(rowY(mDragRow) / kRowH);
                        l.atTarget = l.atLast = mDragBeat;
                        l.rowTarget = l.rowLast = l.row.value();
                        if (any) send = line;
                        break;
                    }
                if (!send.empty() && onCommand) onCommand(send); // after the loop: the model it brings back re-keys mLive
            }
            mPressClip.clear();
            return true;
        case Gesture::Type::Click:
            if (rulerRect().contains(local))
            {
                if (onCommand) onCommand("transport seek " + beats(snap(xToBeat(local.x)))); // on the grid you see (R-TIME-5)
                return true;
            }
            selectClip(clipAt(local));
            return true;
        case Gesture::Type::DoubleClick:
            for (const auto &v : mClips)
                if (v.c.id == clipAt(local) && v.c.kind == "note" && onOpenPattern) onOpenPattern(v.c.pattern);
            return true;
        case Gesture::Type::RightClick:
        {
            // a clip's menu: what it plays through (R-MIX-14), its notes, a copy, gone — each one line
            const std::string id = clipAt(local);
            const ClipView *v = nullptr;
            for (const auto &x : mClips)
                if (x.c.id == id) v = &x;
            if (!v || !onMenu) return true;
            selectClip(id);
            const solaris::ClipModel c = v->c;
            const std::string want = c.kind == "note" ? "instrument" : "audio";
            std::vector<cosmo_v2::ContextMenu::Item> through, items;
            for (const auto &s : mStrips)
                if (s.kind == want)
                {
                    const std::string sid = s.id;
                    through.push_back({s.name + (sid == c.track ? "  \xC2\xB7 now" : ""),
                                       [this, id, sid] { if (onCommand) onCommand("clip move " + id + " --strip " + sid); }});
                }
            const Point world = g.pos;
            items.push_back({"Play through \xE2\x96\xB8", [this, through, world] { if (onMenu) onMenu(through, world); }});
            if (c.kind == "note")
            {
                const std::string pt = c.pattern;
                items.push_back({"Piano Roll", [this, pt] { if (onOpenPattern) onOpenPattern(pt); }});
            }
            items.push_back({"Duplicate", [this, id] { if (onCommand) onCommand("clip duplicate " + id); }});
            items.push_back({"Copy ID", [this, id] { if (onCopy) onCopy(id); }}); // R-UI-11
            items.push_back({"Delete", [this, id] { if (onCommand) onCommand("clip delete " + id); }});
            onMenu(std::move(items), world);
            return true;
        }
        case Gesture::Type::Scroll:
            if (g.ctrl)
            {
                // zoom about the pointer: the beat under it stays under it
                const double anchor = xToBeat(local.x);
                const int step = std::clamp(mZoomStep + (g.delta.y < 0 ? 1 : -1), -kZoomOutSteps, kZoomInSteps);
                if (step == mZoomStep) return true;
                mZoomStep = step; // a lattice: in and out are exact inverses, and the deepest zoom is one place
                const double next = kZoomPpb * std::pow(kZoomRatio, step);
                mPpbTarget = next;
                mPpb.animateTo(next, motion::kCatchUpMs, Easing::EaseOutCubic, mNowMs);
                layout();
                mScrollX.scrollBy(anchor * next - (local.x - kHeaderW) - mScrollX.target());
                return true;
            }
            if (g.shift || !mScrollY.scrollable()) return mScrollX.scrollBy(g.delta.y);
            return mScrollY.scrollBy(g.delta.y);
        default:
            return Segment::handleGesture(g, local);
        }
    }

    void Timeline::paintClip(IRenderTarget &t, const ClipView &v, const Rect &r, double a, double ring, const Color &hue) const
    {
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(hue, 0.32 * a), fade(hue, 0.85 * a), 1.0));
        t.save();
        t.clipRect(r.x, r.y, r.w, r.h);
        const double px = 10.0;
        const double lx = std::max(r.x, kHeaderW) + 5.0; // a clip begun off-screen keeps its name at the visible edge
        const double ids = idsAmount ? idsAmount() : 0.0; // R-UI-11: its id (and its pattern's) beside its name
        drawNameWithId(t, v.c.name.empty() ? v.c.id : v.c.name, v.c.kind == "note" ? v.c.id + " " + v.c.pattern : v.c.id, lx, r.y + 12.0,
                       std::max(0.0, r.right() - lx - 5.0), px, font::sansMedium(), palette::foreground(), ids, a);
        if (v.c.kind == "note" && v.patternLength > 0)
        {
            // the pattern's notes, repeated where the clip loops it; its seams marked
            int lo = 127, hi = 0;
            for (const auto &n : v.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
            const double top = r.y + 17.0, h = std::max(4.0, r.bottom() - 3.0 - top);
            const double ppb = mPpb.value();
            for (double k = 0; k < v.c.length - 1e-9; k += v.patternLength)
            {
                if (k > 0)
                {
                    t.setStroke(fade(hue, 0.5 * a), 1.0);
                    t.beginPath(); t.moveTo(r.x + k * ppb, r.y + 15.0); t.lineTo(r.x + k * ppb, r.bottom()); t.strokePath();
                }
                for (const auto &n : v.notes)
                {
                    if (k + n.at >= v.c.length) continue;
                    const double y = hi == lo ? top + h * 0.5 : top + h * (1.0 - (double)(n.pitch - lo) / (hi - lo)) - 1.5;
                    const double w = std::max(2.0, std::min(n.length, v.c.length - k - n.at) * ppb - 1.0);
                    drawRoundedRect(t, Rect{r.x + (k + n.at) * ppb, std::clamp(y, top, top + h - 3.0), w, 3.0}, radius::control(),
                                    Paint::filled(fade(palette::foreground(), 0.75 * a)));
                }
            }
            if (v.c.linked > 1)
            {
                const std::string s = "linked \xC3\x97" + std::to_string(v.c.linked);
                const double sw = t.measureText(s, 9.0, font::sans());
                if (r.w > sw + 60.0)
                {
                    t.setFill(fade(palette::mutedForeground(), a * (1.0 - ids))); // the ids take its place
                    t.drawText(s, r.right() - sw - 6.0, r.y + 12.0, 9.0, font::sans());
                }
            }
        }
        else if (v.c.kind == "audio")
        {
            // no peaks yet (the waveform arrives with the peaks hook): the source span and its loop seams
            t.setStroke(fade(hue, 0.6 * a), 1.0);
            const double mid = r.y + r.h * 0.6;
            t.beginPath(); t.moveTo(r.x + 4.0, mid); t.lineTo(r.right() - 4.0, mid); t.strokePath();
            if (v.c.offline)
            {
                t.setFill(fade(palette::destructive(), a));
                t.drawText("offline", r.x + 5.0, r.bottom() - 5.0, 9.0, font::sans());
            }
        }
        t.restore();
        if (ring > 0.001) drawRoundedRect(t, r, radius::control(), Paint::stroked(fade(palette::primary(), ring * a), 1.5));
    }

    void Timeline::paintGrid(IRenderTarget &t, double b0, double b1) const
    {
        // R-UI-10: every level's lines, faded by their room at the EASED zoom; a line is drawn once, by the
        // coarsest level it belongs to; every fourth bar is always drawn (a far-out song keeps its phrases)
        const double H = height.value();
        for (int l = 0; l < kGridLevels; ++l)
        {
            const double a = gridAlpha(l), span = gridSpan(l);
            if (l > 0 && a <= 0.001) continue;
            const double strength = kLineA[std::min(l, 2)];
            for (long long n = (long long)std::ceil(b0 / span - 1e-9); n * span <= b1; ++n)
            {
                if (coarser(l, n, mBeatsPerBar)) continue;
                const double la = l == 0 && n % 4 == 0 ? 1.0 : a;
                if (la <= 0.001) continue;
                const double x = std::round(beatToX(n * span)) + 0.5;
                t.setStroke(palette::whiteAlpha(strength * la), 1.0);
                t.beginPath(); t.moveTo(x, kRulerH); t.lineTo(x, H); t.strokePath();
            }
        }
    }

    void Timeline::paintRuler(IRenderTarget &t, double b0, double b1) const
    {
        const double W = width.value(), ppb = mPpb.value(), barPx = ppb * mBeatsPerBar;
        drawRoundedRect(t, Rect{0, 0, W, kRulerH}, 0.0, Paint::filled(surface::rulerBg()));
        // the corner over the lane headers names the snap step (R-UI-10), cross-faded as it changes
        const double nameX = 12.0 + t.measureText("Snap", 9.0, font::sans()) + 6.0;
        t.setFill(palette::mutedForeground());
        t.drawText("Snap", 12.0, 13.0, 9.0, font::sans());
        for (const auto &n : mStepNames)
            if (n.a.value() > 0.001)
            {
                t.setFill(fade(palette::foreground(), 0.8 * n.a.value()));
                t.drawText(textfit::ellipsize(t, n.text, kHeaderW - 8.0 - nameX, 9.0, font::mono()), nameX, 13.0, 9.0, font::mono());
            }
        t.save();
        t.clipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
        // ticks: the lanes' levels, the same fades — long and bright for a bar, shorter as they get finer
        for (int l = 0; l < kGridLevels; ++l)
        {
            const double a = gridAlpha(l), span = gridSpan(l);
            if (l > 0 && a <= 0.001) continue;
            const int k = std::min(l, 2);
            for (long long n = (long long)std::ceil(b0 / span - 1e-9); n * span <= b1; ++n)
            {
                if (coarser(l, n, mBeatsPerBar)) continue;
                const double la = l == 0 && n % 4 == 0 ? 1.0 : a;
                if (la <= 0.001) continue;
                const double x = std::round(beatToX(n * span)) + 0.5;
                t.setStroke(palette::whiteAlpha(kTickA[k] * la), 1.0);
                t.beginPath(); t.moveTo(x, kRulerH - kTickH[k]); t.lineTo(x, kRulerH); t.strokePath();
            }
        }
        // labels, MEASURED (R5): a bar's number fades in as its every-1/2/4/8… bars get room for the widest;
        // zoomed in, the beats are named too ("2.3": bar 2, beat 3), fading in as a beat gets room for one
        const long long lastBar = (long long)std::floor(b1 / mBeatsPerBar) + 1;
        const double barNeed = t.measureText(std::to_string(lastBar), 9.0, font::mono()) + 8.0;
        const double beatNeed = t.measureText(std::to_string(lastBar) + ".8", 9.0, font::mono()) + 8.0;
        const double beatA = mBeatsPerBar > 1 ? smooth01((ppb - beatNeed) / 10.0) : 0.0;
        for (long long n = std::max(0LL, (long long)std::floor(b0)); n <= (long long)b1; ++n)
        {
            const long long bar = n / mBeatsPerBar;
            const int beat = (int)(n % mBeatsPerBar);
            double a = beatA;
            std::string s = std::to_string(bar + 1);
            if (beat == 0)
            {
                long long every = 64; // the coarsest of 1, 2, 4 … 64 bars this one starts
                while (every > 1 && bar % every != 0) every /= 2;
                a = smooth01((every * barPx - barNeed) / 10.0);
            }
            else s += "." + std::to_string(beat + 1);
            if (a <= 0.001) continue;
            const double x = std::round(beatToX((double)n)) + 0.5;
            t.setFill(fade(palette::mutedForeground(), (beat == 0 ? 1.0 : 0.7) * a));
            t.drawText(s, x + 4.0, 13.0, 9.0, font::mono());
        }
        t.restore();
    }

    void Timeline::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value(), ppb = mPpb.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::stageBg()));
        const double b0 = std::max(0.0, std::floor(xToBeat(kHeaderW))), b1 = xToBeat(W) + 1.0;

        // rows and the grid
        t.save();
        t.clipRect(0, kRulerH, W, H - kRulerH);
        for (const auto &row : mRowMotion.rows())
        {
            const double y = kRulerH + row.liveY() - mScrollY.value(), a = row.liveAlpha();
            if (y + kRowH < kRulerH || y > H || a <= 0.001) continue;
            // the zebra is a function of the LIVE slot, so a row sliding to an odd slot shades as it goes
            const double odd = 0.5 - 0.5 * std::cos(M_PI * row.liveY() / kRowH);
            drawRoundedRect(t, Rect{kHeaderW, y, W - kHeaderW, kRowH}, 0.0, Paint::filled(fade(surface::laneBg(), (1.0 - odd) * a)));
            drawRoundedRect(t, Rect{kHeaderW, y, W - kHeaderW, kRowH}, 0.0, Paint::filled(fade(surface::laneAltBg(), odd * a)));
        }
        paintGrid(t, b0, b1);
        t.restore();

        // the loop region, tinted under the clips (R-EDM-7)
        if (const Rect br = loopRect(); br.w > 0)
        {
            const double la = mLoopDragging ? 1.0 : mLoopAmt.value();
            t.save();
            t.clipRect(kHeaderW, kRulerH, W - kHeaderW, H - kRulerH);
            drawRoundedRect(t, Rect{br.x, kRulerH, br.w, H - kRulerH}, 0.0, Paint::filled(palette::primaryAlpha(0.07 * la)));
            t.restore();
        }

        // clips
        t.save();
        t.clipRect(kHeaderW, kRulerH, W - kHeaderW, H - kRulerH);
        for (const auto &l : mLive)
        {
            const ClipView &v = l.v;
            const double a = l.placed ? l.alpha.value() : 0.0;
            if (a <= 0.001) continue;
            const bool dragged = !l.gone && mDragging && v.c.id == mPressClip;
            const Rect r = clipBox(v, l.at.value(), l.row.value() * kRowH);
            if (r.right() < kHeaderW || r.x > W || r.bottom() < kRulerH || r.y > H) continue;
            const double ring = l.gone ? 0.0 : (v.c.id == mSelected ? mSelIn.value() : (v.c.id == mPrevSelected ? mSelOut.value() : 0.0));
            paintClip(t, v, r, (dragged ? 0.35 : 1.0) * a, dragged ? 0.0 : ring, lerpColor(l.hueFrom, l.hueTo, l.hueT.value()));
            int hi = -1;
            for (size_t i = 0; i < mClips.size() && !l.gone; ++i)
                if (mClips[i].c.id == v.c.id) hi = (int)i;
            const double hv = hi >= 0 ? mHover.amount(hi) : 0.0;
            if (hv > 0.001 && !dragged) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
        }
        if (mDragging)
            if (const ClipLive *l = live(mPressClip)) paintClip(t, l->v, clipBox(l->v, mDragBeat, rowY(mDragRow)), 1.0, 1.0, lerpColor(l->hueFrom, l->hueTo, l->hueT.value()));
        paintAutomation(t); // the curves, in the clips' clip
        // the browser's drop hint: where it would land
        if (mDropAmt.value() > 0.001 && mDropRow >= 0)
        {
            const Rect rr = rowRect(mDropRow);
            const double x = beatToX(mDropBeat);
            const double w = std::max(4.0 * ppb, t.measureText(mDropLabel, 10.0, font::sans()) + 16.0);
            const Rect r{x, rr.y + 3.0, w, rr.h - 6.0};
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::primaryAlpha(0.12 * mDropAmt.value()), palette::primaryAlpha(mDropAmt.value()), 1.5));
            t.setFill(fade(palette::foreground(), mDropAmt.value()));
            t.drawText(textfit::ellipsize(t, mDropLabel, r.w - 10.0, 10.0, font::sans()), r.x + 5.0, r.y + 13.0, 10.0, font::sans());
        }
        t.restore();

        // lane headers
        drawRoundedRect(t, Rect{0, kRulerH, kHeaderW, H - kRulerH}, 0.0, Paint::filled(surface::headerBg()));
        t.save();
        t.clipRect(0, kRulerH, kHeaderW, H - kRulerH);
        auto stripeColour = [](int c) { return c >= 0 ? surface::track(c) : palette::whiteAlpha(0.12); };
        for (const auto &row : mRowMotion.rows())
        {
            const Rect r{0.0, kRulerH + row.liveY() - mScrollY.value(), W, kRowH};
            const double a = row.liveAlpha();
            if (r.bottom() < kRulerH || r.y > H || a <= 0.001) continue;
            const Row &d = row.data;
            Color stripe = stripeColour(d.colour);
            const auto st = mStripes.find(row.key);
            if (st != mStripes.end() && st->second.placed) stripe = lerpColor(st->second.from, st->second.to, st->second.t.value());
            drawRoundedRect(t, Rect{0, r.y + 6.0, 3.0, r.h - 12.0}, radius::control(), Paint::filled(fade(stripe, a)));
            if (!d.automation.empty())
            {
                // an automation row: the accent stripe, its name, what reads it
                drawRoundedRect(t, Rect{0, r.y + 6.0, 3.0, r.h - 12.0}, radius::control(), Paint::filled(fade(palette::primary(), a)));
                drawNameWithId(t, d.label, d.automation, 12.0, textfit::baseline(r.y + 16.0, 11.0), kHeaderW - 20.0, 11.0, font::sans(), palette::foreground(),
                               idsAmount ? idsAmount() : 0.0, a);
                const auto al = mAutos.find(d.automation);
                const std::string sub = al != mAutos.end() && !al->second.model.usedBy.empty() ? al->second.model.usedBy[0] : "moves nothing yet";
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, sub, kHeaderW - 20.0, 9.0, font::mono()), 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::mono());
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(0, r.bottom() - 0.5); t.lineTo(W, r.bottom() - 0.5); t.strokePath();
                continue;
            }
            const bool own = d.lane.empty();
            drawNameWithId(t, d.label, own ? row.key.substr(row.key.find(':') + 1) : d.lane, 12.0, textfit::baseline(r.y + 16.0, 11.0), kHeaderW - 20.0, 11.0,
                           font::sans(), own ? palette::mutedForeground() : palette::foreground(), idsAmount ? idsAmount() : 0.0, a);
            if (own)
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText("its strip's row", 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::sans());
            }
            t.setStroke(fade(palette::border(), a), 1.0);
            t.beginPath(); t.moveTo(0, r.bottom() - 0.5); t.lineTo(W, r.bottom() - 0.5); t.strokePath();
        }
        t.restore();
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kHeaderW - 0.5, 0); t.lineTo(kHeaderW - 0.5, H); t.strokePath();

        // ruler: bars from 1, the grid's ticks, the step named in its corner
        paintRuler(t, b0, b1);
        t.save();
        t.clipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
        // the loop's brace on the ruler, with its ends
        if (const Rect br = loopRect(); br.w > 0)
        {
            const double la = mLoopDragging ? 1.0 : mLoopAmt.value();
            drawRoundedRect(t, br, radius::control(), Paint::filled(palette::primaryAlpha(0.85 * la)));
            t.setStroke(palette::primaryAlpha(la), 1.5);
            for (double x : {br.x, br.right()})
            {
                t.beginPath(); t.moveTo(std::round(x) + 0.5, 3.0); t.lineTo(std::round(x) + 0.5, kRulerH); t.strokePath();
            }
        }
        t.restore();
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kRulerH - 0.5); t.lineTo(W, kRulerH - 0.5); t.strokePath();

        // playhead: a position, so `destructive` (Interstellar's reading)
        const double px = beatToX(mPlayhead.value());
        if (px >= kHeaderW - 1.0 && px <= W)
        {
            t.setStroke(surface::playhead(), 1.0);
            t.beginPath(); t.moveTo(px, 0); t.lineTo(px, H); t.strokePath();
            t.setFill(surface::playhead());
            t.beginPath(); t.moveTo(px - 5.0, 0); t.lineTo(px + 5.0, 0); t.lineTo(px, 7.0); t.closePath(); t.fillPath();
        }

        if (mEmptyAmt.value() > 0.001)
        {
            const double ea = mEmptyAmt.value();
            const std::string a = "Drag a sample or an instrument here from the browser.";
            const std::string b = "Every sample gets its own strip in the mixer; lanes only arrange.";
            const double cx = kHeaderW + (W - kHeaderW) * 0.5;
            t.setFill(fade(palette::mutedForeground(), ea));
            const std::string ae = textfit::ellipsize(t, a, W - kHeaderW - 32.0, 13.0, font::sans());
            t.drawText(ae, cx - t.measureText(ae, 13.0, font::sans()) * 0.5, kRulerH + 90.0, 13.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), 0.75 * ea));
            const std::string be = textfit::ellipsize(t, b, W - kHeaderW - 32.0, 11.0, font::sans());
            t.drawText(be, cx - t.measureText(be, 11.0, font::sans()) * 0.5, kRulerH + 110.0, 11.0, font::sans());
        }
        mScrollY.drawBar(t, W - 5.0);
    }
}
}
