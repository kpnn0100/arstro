#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kMinPpb = 4.0, kMaxPpb = 320.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string beats(double b)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", std::round(b * 960.0) / 960.0);
            return buf;
        }
    }

    Timeline::Timeline() { clipToBounds = true; }

    void Timeline::bind(const solaris::AppModel &m)
    {
        mBeatsPerBar = std::max(1, std::atoi(m.sig.c_str()));
        mLength = m.lengthBeats;
        mPosition = m.transport.position;
        mPlaying = m.transport.playing;
        if (m.projectPath != mSong)
        {
            // another song: nothing of the last one may travel into it
            mSong = m.projectPath;
            mRowMotion = interstellar_v1::AnimatedRows<Row>();
            mLive.clear();
            mStripes.clear();
            mEver = mBound = mEmptyInit = false;
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
                same = l.to[i].at == a.points[i].at && l.to[i].value == a.points[i].value && l.to[i].shape == a.points[i].shape;
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
        if (mPlaying) mPlayhead.set(mPosition); // continuous: it follows the audio, not a tween
        else if (std::fabs(mPlayhead.value() - mPosition) > 1e-6 && !mPlayhead.isAnimating())
            mPlayhead.animateTo(mPosition, 140.0, Easing::EaseOutCubic, nowMs); // a seek eases
        mPlayhead.update(nowMs);
        mPpb.update(nowMs);
        mSelIn.update(nowMs);
        mSelOut.update(nowMs);
        mDropAmt.update(nowMs);
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

    bool Timeline::handleGesture(const Gesture &g, const Point &local)
    {
        if (autoGesture(g, local)) return true; // an automation row's curve and points (R-AUTO-6)
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
                if (onCommand) onCommand("transport seek " + beats(snap(xToBeat(local.x)))); // the grid clips snap to
                return true;
            }
            selectClip(clipAt(local));
            return true;
        case Gesture::Type::Scroll:
            if (g.ctrl)
            {
                // zoom about the pointer: the beat under it stays under it
                const double anchor = xToBeat(local.x);
                const double next = std::clamp(mPpbTarget * (g.delta.y < 0 ? 1.25 : 0.8), kMinPpb, kMaxPpb);
                if (next == mPpbTarget) return true;
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
        t.setFill(fade(palette::foreground(), a));
        t.drawText(textfit::ellipsize(t, v.c.name.empty() ? v.c.id : v.c.name, std::max(0.0, r.right() - lx - 5.0), px, font::sansMedium()), lx,
                   r.y + 12.0, px, font::sansMedium());
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
                    t.setFill(fade(palette::mutedForeground(), a));
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
        const int stride = ppb * mBeatsPerBar < 24.0 ? 4 : 1; // zoomed far out: every 4th bar's line only
        for (double b = b0; b <= b1; b += 1.0)
        {
            const bool bar = std::fmod(b, mBeatsPerBar) == 0.0;
            if (!bar && ppb < 10.0) continue;
            if (bar && std::fmod(b / mBeatsPerBar, stride) != 0.0) continue;
            const double x = std::round(beatToX(b)) + 0.5;
            t.setStroke(palette::whiteAlpha(bar ? 0.06 : 0.025), 1.0);
            t.beginPath(); t.moveTo(x, kRulerH); t.lineTo(x, H); t.strokePath();
        }
        t.restore();

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
                t.setFill(fade(palette::foreground(), a));
                t.drawText(textfit::ellipsize(t, d.label, kHeaderW - 20.0, 11.0, font::sans()), 12.0, textfit::baseline(r.y + 16.0, 11.0), 11.0, font::sans());
                const auto al = mAutos.find(d.automation);
                const std::string sub = al != mAutos.end() && !al->second.model.usedBy.empty() ? al->second.model.usedBy[0] : "moves nothing yet";
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, sub, kHeaderW - 20.0, 9.0, font::mono()), 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::mono());
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(0, r.bottom() - 0.5); t.lineTo(W, r.bottom() - 0.5); t.strokePath();
                continue;
            }
            const bool own = d.lane.empty();
            t.setFill(fade(own ? palette::mutedForeground() : palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, d.label, kHeaderW - 20.0, 11.0, font::sans()), 12.0, textfit::baseline(r.y + 16.0, 11.0), 11.0, font::sans());
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

        // ruler: bars from 1
        drawRoundedRect(t, Rect{0, 0, W, kRulerH}, 0.0, Paint::filled(surface::rulerBg()));
        t.save();
        t.clipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
        const int labelEvery = ppb * mBeatsPerBar < 28.0 ? 8 : (ppb * mBeatsPerBar < 56.0 ? 2 : 1);
        for (double b = std::floor(b0 / mBeatsPerBar) * mBeatsPerBar; b <= b1; b += mBeatsPerBar)
        {
            const int bar = (int)(b / mBeatsPerBar) + 1;
            const double x = std::round(beatToX(b)) + 0.5;
            t.setStroke(palette::whiteAlpha(0.15), 1.0);
            t.beginPath(); t.moveTo(x, kRulerH - 7.0); t.lineTo(x, kRulerH); t.strokePath();
            if ((bar - 1) % labelEvery == 0)
            {
                t.setFill(palette::mutedForeground());
                t.drawText(std::to_string(bar), x + 4.0, 13.0, 9.0, font::mono());
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
