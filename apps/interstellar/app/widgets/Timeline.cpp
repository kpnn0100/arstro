#include "Timeline.h"
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
        constexpr double kClipInsetY = 3.0;
        constexpr double kFollowStepS = 0.5;   // a playing playhead's per-frame step is followed directly
        Color fade(Color c, double a) { c.a *= a; return c; }
        double clipDur(const interstellar::ClipModel &c)
        {
            if (c.duration > 0.0) return c.duration;
            return c.speed > 0.0 ? (c.out - c.in) / c.speed : (c.out - c.in);
        }
        std::string tickLabel(double t, double step, double fps)
        {
            char buf[32];
            const long long fr = (long long)std::llround(t * fps);
            const long long ifps = std::max(1LL, (long long)std::llround(fps));
            const long long s = fr / ifps, f = fr % ifps;
            if (step >= 1.0)
            {
                if (s >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
                else std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
            }
            else
                std::snprintf(buf, sizeof buf, "%lld:%02lld", s, f);
            return buf;
        }
    }

    Timeline::Timeline() { clipToBounds = true; }

    // ── model in ─────────────────────────────────────────────────────────────────────────

    void Timeline::bind(const interstellar::AppModel &m)
    {
        mFps = m.fps > 0 ? m.fps : 24.0;
        mDuration = std::max(0.0, m.duration);
        mPlayhead = m.playhead;
        mPlaying = m.playing;
        mSelectedClip = m.selectedClip;
        mTransitions = m.transitions;
        mMarkers = m.markers;
        mCacheSegSeconds = m.previewCacheSegmentSeconds > 0 ? m.previewCacheSegmentSeconds : 1.0;
        if (mCacheSegs.size() < m.previewCacheSegments.size()) mCacheSegs.resize(m.previewCacheSegments.size());
        for (size_t n = 0; n < mCacheSegs.size(); ++n)
        {
            const int st = n < m.previewCacheSegments.size() ? m.previewCacheSegments[n] : 0;
            mCacheSegs[n].cachedT = st == 1 ? 1.0 : 0.0;
            mCacheSegs[n].staleT = st == 2 ? 1.0 : 0.0;
            mCacheSegs[n].buildingT = st == 3 ? 1.0 : 0.0;
        }

        // display order: video tracks top-down by descending order (V2 over V1), then audio
        std::vector<interstellar::TrackModel> v, a;
        for (const auto &t : m.tracks) (t.audio ? a : v).push_back(t);
        std::sort(v.begin(), v.end(), [](const auto &x, const auto &y) { return x.order > y.order; });
        std::sort(a.begin(), a.end(), [](const auto &x, const auto &y) { return x.order < y.order; });
        mTracks = v;
        mTracks.insert(mTracks.end(), a.begin(), a.end());

        for (auto &kv : mTrackAnims) { kv.second.gone = true; kv.second.alphaT = 0.0; }
        for (int i = 0; i < (int)mTracks.size(); ++i)
        {
            TrackAnim &ta = mTrackAnims[mTracks[i].id];
            ta.data = mTracks[i];
            ta.gone = false;
            ta.laneT = i;
            ta.alphaT = 1.0;
        }

        mModelClips.clear();
        std::map<std::string, bool> live;
        for (const auto &c : m.clips)
        {
            mModelClips[c.id] = c;
            if (std::find(mClipOrder.begin(), mClipOrder.end(), c.id) == mClipOrder.end()) mClipOrder.push_back(c.id);
            const std::string key = styleKey(c);
            live[key] = true;
            auto found = mAnims.find(key);
            if (found == mAnims.end())
            {
                ClipAnim fresh;
                // the same clip in a different LOOK: start the new look from the old one's live geometry
                for (auto &kv : mAnims)
                    if (!kv.second.gone && kv.second.data.id == c.id && kv.second.placed)
                    {
                        fresh.at.set(kv.second.at.value()); fresh.dur.set(kv.second.dur.value()); fresh.lane.set(kv.second.lane.value());
                        fresh.atL = fresh.at.value(); fresh.durL = fresh.dur.value(); fresh.laneL = fresh.lane.value();
                        fresh.placed = true;
                        fresh.fadeIn = true;
                        break;
                    }
                found = mAnims.emplace(key, std::move(fresh)).first;
            }
            ClipAnim &an = found->second;
            an.data = c;
            an.gone = false;
            an.atT = c.at;
            an.durT = clipDur(c);
            an.laneT = laneOfTrack(c.track);
            an.alphaT = 1.0;
        }
        for (auto &kv : mAnims)
            if (!live.count(kv.first)) { kv.second.gone = true; kv.second.alphaT = 0.0; }
        mEmptyWanted = m.clips.empty();
        mSel.setHovered(clipSlot(mSelectedClip));
        mEverBound = true;
    }

    std::string Timeline::styleKey(const interstellar::ClipModel &c)
    {
        return c.id + "|" + std::to_string((int)c.provenance) + (c.offline ? "|o" : "");
    }

    Timeline::ClipAnim *Timeline::liveAnim(const std::string &id)
    {
        for (auto &kv : mAnims) if (!kv.second.gone && kv.second.data.id == id) return &kv.second;
        return nullptr;
    }

    double Timeline::clipAlpha(const std::string &id) const
    {
        const ClipAnim *a = animFor(id);
        return a ? a->alpha.value() : 0.0;
    }

    int Timeline::clipSlot(const std::string &id) const
    {
        for (int i = 0; i < (int)mClipOrder.size(); ++i) if (mClipOrder[i] == id) return i;
        return -1;
    }

    int Timeline::laneOfTrack(const std::string &trackId) const
    {
        for (int i = 0; i < (int)mTracks.size(); ++i) if (mTracks[i].id == trackId) return i;
        return 0;
    }

    // ── the one origin ───────────────────────────────────────────────────────────────────

    double Timeline::timeToX(double t) const { return shell::headerWidth() + t * mPps.value() - mScroll.value(); }
    double Timeline::xToTime(double x) const
    {
        const double pps = std::max(1e-6, mPps.value());
        return (x - shell::headerWidth() + mScroll.value()) / pps;
    }

    Rect Timeline::rulerRect() const { return Rect{shell::headerWidth(), 0, std::max(0.0, width.value() - shell::headerWidth()), shell::rulerH()}; }
    Rect Timeline::lanesRect() const
    {
        return Rect{shell::headerWidth(), shell::rulerH(), std::max(0.0, width.value() - shell::headerWidth()),
                    std::max(0.0, height.value() - shell::rulerH())};
    }
    double Timeline::laneTop(double laneLive) const { return shell::rulerH() + laneLive * shell::trackH() - mVScroll.value(); }
    Rect Timeline::laneRect(const std::string &trackId) const
    {
        // where the lane is DRAWN now (its eased position), not where it is going
        auto it = mTrackAnims.find(trackId);
        const double lane = (it != mTrackAnims.end() && it->second.placed) ? it->second.lane.value() : laneOfTrack(trackId);
        return Rect{shell::headerWidth(), laneTop(lane), lanesRect().w, shell::trackH()};
    }
    int Timeline::laneAtY(double y) const { return (int)std::floor((y - shell::rulerH() + mVScroll.value()) / shell::trackH()); }

    const Timeline::ClipAnim *Timeline::animFor(const std::string &id) const
    {
        const ClipAnim *ghost = nullptr;
        for (const auto &kv : mAnims)
            if (kv.second.data.id == id)
            {
                if (!kv.second.gone) return &kv.second;
                ghost = &kv.second;
            }
        return ghost;
    }

    Rect Timeline::clipRect(const std::string &id) const
    {
        const ClipAnim *a = animFor(id);
        if (!a) return Rect{0, 0, 0, 0};
        const double x = timeToX(a->at.value());
        return Rect{x, laneTop(a->lane.value()) + kClipInsetY, a->dur.value() * mPps.value(), shell::trackH() - 2 * kClipInsetY};
    }

    Rect Timeline::zoomOutRect() const { return Rect{9.75, (shell::rulerH() - 18.0) * 0.5, 18.0, 18.0}; }
    Rect Timeline::zoomInRect() const { return Rect{9.75 + 20.0, (shell::rulerH() - 18.0) * 0.5, 18.0, 18.0}; }

    double Timeline::fitPps() const
    {
        const double w = std::max(60.0, lanesRect().w - 24.0);
        return std::clamp(w / std::max(1.0, mDuration), 2.0, kMaxPps);
    }

    double Timeline::maxScrollFor(double pps) const { return std::max(0.0, contentSeconds() * pps - lanesRect().w); }

    void Timeline::zoomBy(double factor, double anchorX)
    {
        if (!mPpsInit) return;
        const double minPps = fitPps() * 0.5;
        const double next = std::clamp(mPpsTarget * factor, minPps, kMaxPps);
        if (std::fabs(next - mPpsTarget) < 1e-9) return;
        mAnchorX = std::clamp(anchorX, shell::headerWidth(), width.value());
        mAnchorTime = xToTime(mAnchorX);
        mAnchored = true;
        mPpsTarget = next;
    }

    void Timeline::resetView()
    {
        if (!mPpsInit) return;
        mAnchorX = shell::headerWidth();   // time 0 stays pinned to the left edge as the zoom eases
        mAnchorTime = 0.0;
        mAnchored = true;
        mPpsTarget = fitPps();
    }

    // ── snapping ─────────────────────────────────────────────────────────────────────────

    double Timeline::snap(double start, double dur, const std::string &self, bool twoEdges, bool &snapped, double &snapT) const
    {
        const double thr = kSnapPx / std::max(1e-6, mPps.value());
        double best = thr, delta = 0.0;
        snapped = false;
        auto consider = [&](double c) {
            const double d1 = c - start;
            if (std::fabs(d1) < best) { best = std::fabs(d1); delta = d1; snapped = true; snapT = c; }
            if (twoEdges)
            {
                const double d2 = c - (start + dur);
                if (std::fabs(d2) < best) { best = std::fabs(d2); delta = d2; snapped = true; snapT = c; }
            }
        };
        consider(0.0);
        consider(mPlayhead);
        for (const auto &mk : mMarkers) consider(mk.at);
        for (const auto &kv : mModelClips)
        {
            if (kv.first == self) continue;
            consider(kv.second.at);
            consider(kv.second.at + clipDur(kv.second));
        }
        return start + delta;
    }

    // ── input ────────────────────────────────────────────────────────────────────────────

    std::string Timeline::clipAt(const Point &p, int &edge) const
    {
        edge = 0;
        if (!lanesRect().contains(p)) return std::string();
        // topmost first: the selected clip wins an overlap, then reverse draw order
        std::string hit;
        for (const auto &kv : mAnims)
        {
            if (kv.second.gone) continue;
            const Rect r = clipRect(kv.second.data.id);
            if (!r.contains(p)) continue;
            hit = kv.second.data.id;
            if (hit == mSelectedClip) break;
        }
        if (hit.empty()) return hit;
        const Rect r = clipRect(hit);
        if (r.w >= 3 * kEdgePx)
        {
            if (p.x - r.x <= kEdgePx) edge = -1;
            else if (r.right() - p.x <= kEdgePx) edge = +1;
        }
        return hit;
    }

    void Timeline::resetToModel(const std::string &id)
    {
        ClipAnim *ap = liveAnim(id);
        auto mc = mModelClips.find(id);
        if (!ap || mc == mModelClips.end()) return;
        ClipAnim &an = *ap;
        an.atT = mc->second.at;
        an.durT = clipDur(mc->second);
        an.laneT = laneOfTrack(mc->second.track);
        an.atL = an.durL = an.laneL = -1e9;   // force the ease back on the next advance
    }

    bool Timeline::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int edge = 0;
            const std::string id = clipAt(local, edge);
            mHover.setHovered(id.empty() ? -1 : clipSlot(id));
            mUi.setHovered(zoomOutRect().contains(local) ? 0 : (zoomInRect().contains(local) ? 1 : -1));
            return true;
        }
        case Gesture::Type::Down:
        {
            if (zoomOutRect().contains(local) || zoomInRect().contains(local)) return true;
            if (rulerRect().contains(local) && mDuration > 0.0)
            {
                mDrag = Drag{};
                mDrag.kind = DragKind::Scrub;
                const double t = std::clamp(xToTime(local.x), 0.0, mDuration);
                mShownPlayhead.set(t);
                mPlayheadLast = t;
                mDrag.lastFrame = (long long)std::llround(t * mFps);
                emit("playhead " + cmd::seconds(t, mFps));
                return true;
            }
            int edge = 0;
            const std::string id = clipAt(local, edge);
            if (!id.empty() && liveAnim(id))
            {
                const ClipAnim &an = *liveAnim(id);
                mDrag = Drag{};
                mDrag.clip = id;
                mDrag.origAt = an.data.at;
                mDrag.origDur = clipDur(an.data);
                mDrag.origIn = an.data.in;
                mDrag.origOut = an.data.out;
                mDrag.speed = an.data.speed > 0 ? an.data.speed : 1.0;
                mDrag.origLane = mDrag.lane = laneOfTrack(an.data.track);
                const double pt = xToTime(local.x);
                // Alt on an edge shared with a touching neighbour: ROLL that cut (R-UI-14)
                std::string left = id, right;
                if (g.alt && edge != 0)
                {
                    const double cut = edge > 0 ? an.data.at + mDrag.origDur : an.data.at;
                    for (const auto &kv : mAnims)
                    {
                        const ClipAnim &o = kv.second;
                        if (o.gone || o.data.id == id || o.data.track != an.data.track) continue;
                        if (edge > 0 && std::fabs(o.data.at - cut) < 0.5 / mFps) right = o.data.id;
                        if (edge < 0 && std::fabs(o.data.at + clipDur(o.data) - cut) < 0.5 / mFps) { left = o.data.id; right = id; }
                    }
                }
                if (!right.empty() && liveAnim(left) && liveAnim(right))
                {
                    const ClipAnim &l = *liveAnim(left), &rr = *liveAnim(right);
                    mDrag.kind = DragKind::Roll;
                    mDrag.clip = left;
                    mDrag.origAt = l.data.at;
                    mDrag.origDur = clipDur(l.data);
                    mDrag.other = right;
                    mDrag.otherAt = rr.data.at;
                    mDrag.otherDur = clipDur(rr.data);
                    mDrag.grab = rr.data.at - pt;
                    mDrag.value = rr.data.at;
                }
                else if (g.alt && edge == 0) { mDrag.kind = DragKind::Slip; mDrag.pressT = pt; mDrag.value = 0.0; }
                else if (edge < 0) { mDrag.kind = DragKind::TrimIn; mDrag.grab = an.data.at - pt; mDrag.value = an.data.at; }
                else if (edge > 0) { mDrag.kind = DragKind::TrimOut; mDrag.grab = an.data.at + mDrag.origDur - pt; mDrag.value = an.data.at + mDrag.origDur; }
                else { mDrag.kind = DragKind::Move; mDrag.grab = an.data.at - pt; mDrag.value = an.data.at; }
            }
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
        {
            if (mDrag.kind == DragKind::Scrub)
            {
                const double t = std::clamp(xToTime(local.x), 0.0, mDuration);
                mShownPlayhead.set(t);   // direct manipulation
                mPlayheadLast = t;
                const long long fr = (long long)std::llround(t * mFps);
                if (fr != mDrag.lastFrame) { mDrag.lastFrame = fr; emit("playhead " + cmd::seconds(t, mFps)); }
                return true;
            }
            ClipAnim *ap = liveAnim(mDrag.clip);
            if (mDrag.kind == DragKind::None || !ap) return true;
            ClipAnim &an = *ap;
            const double pt = xToTime(local.x);
            const double frame = 1.0 / mFps;
            if (mDrag.kind == DragKind::Move)
            {
                double start = std::max(0.0, pt + mDrag.grab);   // keep the grab offset: no teleport
                start = std::max(0.0, snap(start, mDrag.origDur, mDrag.clip, true, mDrag.snapped, mDrag.snapTime));
                mDrag.value = start;
                const int lane = laneAtY(local.y);
                if (lane >= 0 && lane < (int)mTracks.size() && mTracks[lane].audio == an.data.audio) mDrag.lane = lane;
                an.at.set(start); an.atT = an.atL = start;
                an.lane.set(mDrag.lane); an.laneT = an.laneL = mDrag.lane;
            }
            else if (mDrag.kind == DragKind::TrimIn)
            {
                const double end = mDrag.origAt + mDrag.origDur;
                double s = snap(pt + mDrag.grab, 0.0, mDrag.clip, false, mDrag.snapped, mDrag.snapTime);
                s = std::clamp(s, std::max(0.0, mDrag.origAt - mDrag.origIn / mDrag.speed), end - frame);
                mDrag.value = s;
                an.at.set(s); an.atT = an.atL = s;
                an.dur.set(end - s); an.durT = an.durL = end - s;
            }
            else if (mDrag.kind == DragKind::TrimOut)
            {
                double e = snap(pt + mDrag.grab, 0.0, mDrag.clip, false, mDrag.snapped, mDrag.snapTime);
                e = std::max(e, mDrag.origAt + frame);
                mDrag.value = e;
                an.dur.set(e - mDrag.origAt); an.durT = an.durL = e - mDrag.origAt;
            }
            else if (mDrag.kind == DragKind::Roll)
            {
                // the shared edge moves: the left clip's end and the right clip's start, together
                double c = snap(pt + mDrag.grab, 0.0, mDrag.clip, false, mDrag.snapped, mDrag.snapTime);
                c = std::clamp(c, mDrag.origAt + frame, mDrag.otherAt + mDrag.otherDur - frame);
                mDrag.value = c;
                an.dur.set(c - mDrag.origAt); an.durT = an.durL = c - mDrag.origAt;
                if (ClipAnim *o = liveAnim(mDrag.other))
                {
                    const double end = mDrag.otherAt + mDrag.otherDur;
                    o->at.set(c); o->atT = o->atL = c;
                    o->dur.set(end - c); o->durT = o->durL = end - c;
                }
            }
            else if (mDrag.kind == DragKind::Slip)
            {
                // dragging right shows EARLIER material: the source slides under a fixed clip
                double by = -(pt - mDrag.pressT) * mDrag.speed;
                by = std::max(by, -mDrag.origIn);                 // never before the source's start
                mDrag.value = std::round(by * mFps) / mFps;      // on the frame grid
            }
            mGuideWanted = mDrag.snapped;
            if (mDrag.snapped) mGuideTime = mDrag.snapTime;
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            const Drag d = mDrag;
            mDrag = Drag{};
            mGuideWanted = false;
            if (d.kind == DragKind::Move)
            {
                const bool moved = std::fabs(d.value - d.origAt) > 1e-9 || d.lane != d.origLane;
                if (moved)
                {
                    std::string line = "clip move " + cmd::quote(d.clip) + " --at " + cmd::seconds(d.value, mFps);
                    if (d.lane != d.origLane && d.lane >= 0 && d.lane < (int)mTracks.size())
                        line += " --track " + cmd::quote(mTracks[d.lane].id);
                    if (!emit(line)) resetToModel(d.clip);
                }
            }
            else if (d.kind == DragKind::TrimIn && std::fabs(d.value - d.origAt) > 1e-9)
            {
                const double newIn = d.origIn + (d.value - d.origAt) * d.speed;
                if (!emit("clip trim " + cmd::quote(d.clip) + " --in " + cmd::seconds(newIn, mFps))) resetToModel(d.clip);
            }
            else if (d.kind == DragKind::TrimOut && std::fabs(d.value - (d.origAt + d.origDur)) > 1e-9)
            {
                const double newOut = d.origIn + (d.value - d.origAt) * d.speed;
                if (!emit("clip trim " + cmd::quote(d.clip) + " --out " + cmd::seconds(newOut, mFps))) resetToModel(d.clip);
            }
            else if (d.kind == DragKind::Roll && std::fabs(d.value - d.otherAt) > 1e-9)
            {
                if (!emit("clip roll " + cmd::quote(d.clip) + " --at " + cmd::seconds(d.value, mFps))) { resetToModel(d.clip); resetToModel(d.other); }
            }
            else if (d.kind == DragKind::Slip && std::fabs(d.value) > 1e-9)
                emit("clip slip " + cmd::quote(d.clip) + " --by " + cmd::num(d.value));
            return true;
        }
        case Gesture::Type::Click:
        {
            if (zoomOutRect().contains(local)) { zoomBy(1.0 / kZoomStep, timeToX(mPlayhead)); return true; }
            if (zoomInRect().contains(local)) { zoomBy(kZoomStep, timeToX(mPlayhead)); return true; }
            if (rulerRect().contains(local)) return true;   // the press already moved the playhead
            int edge = 0;
            const std::string id = clipAt(local, edge);
            if (!id.empty())
            {
                if (id != mSelectedClip) emit("clip select " + cmd::quote(id));
                return true;
            }
            if (lanesRect().contains(local) && mDuration > 0.0)
            {
                emit("playhead " + cmd::seconds(std::clamp(xToTime(local.x), 0.0, mDuration), mFps));
                return true;
            }
            return true;
        }
        case Gesture::Type::RightClick:
        {
            int edge = 0;
            const std::string id = clipAt(local, edge);
            const Point w = worldTransform().apply(local);
            if (!id.empty()) { if (onClipContext) onClipContext(id, w); return true; }
            if (lanesRect().contains(local) || (local.x < shell::headerWidth() && local.y > shell::rulerH()))
            {
                const int lane = laneAtY(local.y);
                const std::string trk = lane >= 0 && lane < (int)mTracks.size() ? mTracks[(size_t)lane].id : std::string();
                if (onLaneContext) onLaneContext(trk, std::clamp(xToTime(local.x), 0.0, std::max(0.0, mDuration + 60.0)), w);
            }
            return true;
        }
        case Gesture::Type::Scroll:
        {
            if (g.ctrl)
            {
                zoomBy(g.delta.y < 0 ? 1.25 : 1.0 / 1.25, local.x);
                return true;
            }
            if (local.x < shell::headerWidth()) return mVScroll.scrollBy(g.delta.y);
            const double maxS = maxScrollFor(mPps.value());
            if (maxS <= 0.5) return mVScroll.scrollBy(g.delta.y);   // nothing to pan: try the tracks, else bubble
            mAnchored = false;
            mScrollTarget = std::clamp(mScrollTarget + g.delta.y, 0.0, maxS);
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    // ── a source dragged in from the bin (R-UI-14) ───────────────────────────────────────

    void Timeline::dropLocate(Point local)
    {
        const double t0 = std::max(0.0, xToTime(local.x));   // the pointer is the clip's head
        bool snapped = false;
        double st = 0.0;
        mDropT = std::max(0.0, snap(std::max(0.0, t0), mDropDur, std::string(), true, snapped, st));
        mGuideWanted = snapped;
        if (snapped) mGuideTime = st;
        int videoLanes = 0;
        for (const auto &tk : mTracks) videoLanes += !tk.audio;
        const int lane = laneAtY(local.y);
        mDropNewTrack = false;
        mDropBad = false;
        if (lane >= 0 && lane < (int)mTracks.size())
        {
            mDropLane = lane;
            mDropTrack = mTracks[(size_t)lane].id;
            if (mTracks[(size_t)lane].audio) { mDropBad = true; mDropTrack = "!"; }
        }
        else if (videoLanes == 0)
        {
            mDropLane = 0;                // the first slot, labelled as the track it will make
            mDropTrack.clear();
            mDropNewTrack = true;
        }
        else
        {
            mDropLane = (double)mTracks.size();   // below the tracks: a new video track there
            mDropTrack.clear();
            mDropNewTrack = true;
        }
    }

    void Timeline::dropHover(const std::string &label, double dur, Point at)
    {
        const Point local = worldTransform().inverse().apply(at);
        mDropLabel = label;
        mDropDur = dur > 0 ? dur : 5.0;
        mDropWanted = lanesRect().contains(local);
        if (mDropWanted) dropLocate(local);
        else mGuideWanted = false;
    }

    bool Timeline::dropAt(Point at, std::string &track, double &t)
    {
        const Point local = worldTransform().inverse().apply(at);
        const bool inside = lanesRect().contains(local);
        if (inside) dropLocate(local);
        mDropWanted = false;
        mGuideWanted = false;
        if (!inside || mDropBad) return false;
        track = mDropTrack;
        t = mDropT;
        return true;
    }

    void Timeline::dropCancel() { mDropWanted = false; mGuideWanted = false; }

    std::string Timeline::dragHint() const
    {
        if (mDrag.kind == DragKind::Roll) return "roll";
        if (mDrag.kind == DragKind::Slip)
        {
            char b[32];
            std::snprintf(b, sizeof b, "slip %+.2f s", mDrag.value);
            return b;
        }
        return std::string();
    }

    // ── time ─────────────────────────────────────────────────────────────────────────────

    void Timeline::advance(double nowMs)
    {
        if (!mPpsInit && mEverBound && lanesRect().w > 0.0)
        {
            const double p = fitPps();
            mPps.set(p);
            mPpsTarget = mPpsLast = p;
            mPpsInit = true;
        }
        if (mPpsTarget != mPpsLast)
        {
            mPps.animateTo(mPpsTarget, motion::kZoomMs, Easing::EaseOutCubic, nowMs);
            mPpsLast = mPpsTarget;
        }
        const bool zooming = mPps.isAnimating();
        mPps.update(nowMs);
        if (mAnchored)
        {
            // derived from the EASED zoom, every frame — the anchored time stays under the anchor
            const double s = std::clamp(mAnchorTime * mPps.value() - (mAnchorX - shell::headerWidth()), 0.0, maxScrollFor(mPps.value()));
            mScroll.set(s);
            mScrollTarget = mScrollLast = s;
            if (!zooming) mAnchored = false;
        }
        else
        {
            mScrollTarget = std::clamp(mScrollTarget, 0.0, maxScrollFor(mPps.value()));
            if (mScrollTarget != mScrollLast)
            {
                mScroll.animateTo(mScrollTarget, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
                mScrollLast = mScrollTarget;
            }
            mScroll.update(nowMs);
        }
        mVScroll.setExtent(shell::rulerH(), std::max(0.0, height.value() - shell::rulerH()), (double)mTracks.size() * shell::trackH());
        mVScroll.advance(nowMs);
        for (auto &cs : mCacheSegs)
        {
            const std::pair<artboard::AnimatedProperty *, std::pair<double *, double *>> amts[] = {
                {&cs.cached, {&cs.cachedT, &cs.cachedL}}, {&cs.stale, {&cs.staleT, &cs.staleL}}, {&cs.building, {&cs.buildingT, &cs.buildingL}}};
            for (const auto &a : amts)
            {
                if (*a.second.first != *a.second.second)
                {
                    a.first->animateTo(*a.second.first, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                    *a.second.second = *a.second.first;
                }
                a.first->update(nowMs);
            }
        }

        for (auto it = mAnims.begin(); it != mAnims.end();)
        {
            ClipAnim &an = it->second;
            const bool dragged = mDrag.kind != DragKind::None && mDrag.kind != DragKind::Scrub && !an.gone && mDrag.clip == an.data.id;
            if (an.fadeIn)
            {
                // a new LOOK of an existing clip: fades in over its old look, which fades out as a ghost
                an.alpha.set(0.0);
                an.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                an.alphaL = 1.0;
                an.fadeIn = false;
            }
            if (!an.placed)
            {
                an.at.set(an.atT); an.dur.set(an.durT); an.lane.set(an.laneT);
                an.atL = an.atT; an.durL = an.durT; an.laneL = an.laneT;
                if (mPopulated && !reducedMotion())
                {
                    an.alpha.set(0.0);
                    an.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                }
                else
                    an.alpha.set(1.0);
                an.alphaL = 1.0;
                an.placed = true;
            }
            else if (!dragged)
            {
                if (an.atT != an.atL) { an.at.animateTo(an.atT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); an.atL = an.atT; }
                if (an.durT != an.durL) { an.dur.animateTo(an.durT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); an.durL = an.durT; }
                if (an.laneT != an.laneL) { an.lane.animateTo(an.laneT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); an.laneL = an.laneT; }
            }
            if (an.alphaT != an.alphaL) { an.alpha.animateTo(an.alphaT, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); an.alphaL = an.alphaT; }
            an.at.update(nowMs); an.dur.update(nowMs); an.lane.update(nowMs); an.alpha.update(nowMs);
            if (an.gone && !an.alpha.isAnimating() && an.alpha.value() <= 0.001) it = mAnims.erase(it);
            else ++it;
        }
        for (auto it = mTrackAnims.begin(); it != mTrackAnims.end();)
        {
            TrackAnim &ta = it->second;
            if (!ta.placed)
            {
                ta.lane.set(ta.laneT);
                ta.laneL = ta.laneT;
                if (mPopulated && !reducedMotion()) { ta.alpha.set(0.0); ta.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); }
                else ta.alpha.set(1.0);
                ta.alphaL = 1.0;
                ta.placed = true;
            }
            if (ta.laneT != ta.laneL) { ta.lane.animateTo(ta.laneT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); ta.laneL = ta.laneT; }
            if (ta.alphaT != ta.alphaL) { ta.alpha.animateTo(ta.alphaT, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); ta.alphaL = ta.alphaT; }
            ta.lane.update(nowMs);
            ta.alpha.update(nowMs);
            if (ta.gone && !ta.alpha.isAnimating() && ta.alpha.value() <= 0.001) it = mTrackAnims.erase(it);
            else ++it;
        }
        if (!mEmptyInit && mEverBound) { mEmptyAmt.set(mEmptyWanted ? 1.0 : 0.0); mEmptyApplied = mEmptyWanted; mEmptyInit = true; }
        if (mEmptyWanted != mEmptyApplied)
        {
            mEmptyAmt.animateTo(mEmptyWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mEmptyApplied = mEmptyWanted;
        }
        mEmptyAmt.update(nowMs);
        if (mEverBound) mPopulated = true;

        if (mDrag.kind != DragKind::Scrub && mPlayhead != mPlayheadLast)
        {
            if (mPlayheadLast < -0.5) mShownPlayhead.set(mPlayhead);
            else if (mPlaying && std::fabs(mPlayhead - mShownPlayhead.value()) < kFollowStepS) mShownPlayhead.set(mPlayhead);
            else mShownPlayhead.animateTo(mPlayhead, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mPlayheadLast = mPlayhead;
        }
        mShownPlayhead.update(nowMs);

        if (mGuideWanted != mGuideApplied)
        {
            mGuideAmt.animateTo(mGuideWanted ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mGuideApplied = mGuideWanted;
        }
        mGuideAmt.update(nowMs);
        if (mDropWanted != mDropApplied)
        {
            mDropAmt.animateTo(mDropWanted ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mDropApplied = mDropWanted;
        }
        mDropAmt.update(nowMs);
        if (!isHovered()) { mHover.clear(); mUi.clear(); }
        mHover.advance(nowMs);
        mUi.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        Segment::advance(nowMs);
    }

    // ── paint ────────────────────────────────────────────────────────────────────────────

    void Timeline::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        const double hw = shell::headerWidth(), rh = shell::rulerH(), th = shell::trackH();
        const double pps = std::max(1e-6, mPps.value());
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::deckBg()));
        const double emptyAmt = mEmptyAmt.value();
        const double laneDim = 1.0 - 0.55 * emptyAmt;   // "tracks greyed" while there is nothing on them

        // ── lanes (clipped to the lanes rect, scrolled vertically) ──
        const Rect lr = lanesRect();
        t.save();
        t.clipRect(lr.x, lr.y, lr.w, lr.h);
        for (const auto &kv : mTrackAnims)
        {
            const TrackAnim &ta = kv.second;
            const double y = laneTop(ta.lane.value());
            const double ta_a = ta.alpha.value();
            if (y + th < lr.y || y > H || ta_a <= 0.001) continue;
            drawRoundedRect(t, Rect{lr.x, y, lr.w, th}, 0.0, Paint::filled(fade(surface::laneBg(), laneDim * ta_a)));
            if (((int)std::lround(ta.laneT)) % 2) drawRoundedRect(t, Rect{lr.x, y, lr.w, th}, 0.0, Paint::filled(fade(surface::laneAltBg(), ta_a)));
            glyph::line(t, lr.x, y + th - 0.5, lr.x + lr.w, y + th - 0.5, fade(palette::border(), ta_a), 1.0);
        }
        // the end of the timeline: everything past `duration` is a shade darker
        {
            const double xe = timeToX(mDuration);
            if (xe < lr.right()) drawRoundedRect(t, Rect{std::max(lr.x, xe), lr.y, lr.right() - std::max(lr.x, xe), lr.h}, 0.0, Paint::filled(surface::scrim(0.35)));
        }
        // markers through the lanes
        for (const auto &mk : mMarkers)
        {
            const double x = timeToX(mk.at);
            if (x < lr.x || x > lr.right()) continue;
            glyph::line(t, x, lr.y, x, H, fade(surface::marker(), 0.35), 1.0);
        }

        // clips, by provenance
        for (const auto &kv : mAnims)
        {
            const ClipAnim &an = kv.second;
            const double a = an.alpha.value();
            if (a <= 0.001) continue;
            const auto &c = an.data;
            const Rect r{timeToX(an.at.value()), laneTop(an.lane.value()) + kClipInsetY, an.dur.value() * pps, th - 2 * kClipInsetY};
            if (r.right() < lr.x || r.x > lr.right() || r.bottom() < lr.y || r.y > H) continue;
            const int slot = clipSlot(c.id);
            const double sel = mSel.amount(slot), hv = mHover.amount(slot);
            const bool dragged = !an.gone && mDrag.clip == c.id && mDrag.kind != DragKind::None && mDrag.kind != DragKind::Scrub;
            double body = a;
            if (c.provenance == interstellar::Provenance::Inherited) body *= 0.55;
            const Color plate = c.audio ? surface::clipAudio() : surface::clipVideo();
            Color border = palette::whiteAlpha(0.10);
            Color fill = plate;
            if (c.provenance == interstellar::Provenance::Dangling)
            {
                fill = lerpColor(plate, palette::destructive(), 0.12);
                border = palette::destructive();
            }
            else if (c.provenance == interstellar::Provenance::Overridden)
                border = palette::primaryAlpha(0.85);
            if (dragged) fill = brighten(fill, 0.08);
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(fill, body), fade(border, a), 1.0));
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            if (c.provenance == interstellar::Provenance::Overridden && r.w > 4.0)
                drawRoundedRect(t, Rect{r.x, r.y, 3.0, r.h}, radius::hairline(), Paint::filled(fade(palette::primary(), a)));
            if (c.provenance == interstellar::Provenance::Dangling || c.offline)
            {
                // hatching: the honest picture of "this is not really here"
                t.save();
                t.clipRect(r.x, r.y, r.w, r.h);
                const Color hc = fade(c.offline && c.provenance != interstellar::Provenance::Dangling ? palette::mutedForeground() : palette::destructive(), 0.22 * a);
                for (double hx = r.x - r.h; hx < r.right(); hx += 7.0) glyph::line(t, hx, r.bottom(), hx + r.h, r.y, hc, 1.0);
                t.restore();
            }
            // label: the source, then (room permitting) what makes this clip itself
            // the label rides the VISIBLE part of the clip, so a clip that starts off-screen keeps its name
            const double tx = std::max(r.x, lr.x) + (c.provenance == interstellar::Provenance::Overridden && r.x >= lr.x ? 8.0 : 6.0);
            const double room = r.right() - 5.0 - tx;
            if (room > 8.0)
            {
                const bool warnText = c.provenance == interstellar::Provenance::Dangling || c.offline;
                const std::string label = c.srcName.empty() ? c.name : c.srcName;
                t.setFill(fade(warnText ? palette::destructive() : (c.provenance == interstellar::Provenance::Inherited ? palette::mutedForeground() : palette::foreground()), a));
                t.drawText(textfit::ellipsize(t, label, room, 10.0, font::sansMedium()), tx, r.y + 11.5, 10.0, font::sansMedium());
                std::string sub;
                if (c.provenance == interstellar::Provenance::Dangling) sub = "base deleted its target";
                else if (c.offline) sub = "offline \xE2\x80\x94 media missing";
                else if (c.provenance == interstellar::Provenance::Inherited) sub = "inherited";
                else if (c.provenance == interstellar::Provenance::Overridden) sub = "overridden here";
                else if (c.audio) sub = cmd::num(c.gain) + " dB";
                else sub = cmd::timecode(clipDur(c), mFps).substr(3);
                if (r.h >= 24.0)
                {
                    t.setFill(fade(warnText ? palette::destructive() : palette::mutedForeground(), a * 0.9));
                    t.drawText(textfit::ellipsize(t, sub, room, 8.5, c.provenance == interstellar::Provenance::Local && !c.audio ? font::mono() : font::sans()),
                               tx, r.y + 22.5, 8.5, c.provenance == interstellar::Provenance::Local && !c.audio ? font::mono() : font::sans());
                }
            }
            if (sel > 0.001)
                drawRoundedRect(t, Rect{r.x - 1.0, r.y - 1.0, r.w + 2.0, r.h + 2.0}, radius::control(), Paint::stroked(palette::primaryAlpha(sel * a), 1.5));
            // a modifier-drag says what it is while it is held (R-UI-14)
            if (dragged && (mDrag.kind == DragKind::Roll || mDrag.kind == DragKind::Slip))
            {
                std::string hint = dragHint();
                if (mDrag.kind == DragKind::Slip) hint += " \xC2\xB7 in " + cmd::timecode(std::max(0.0, mDrag.origIn + mDrag.value), mFps);
                const double tw = t.measureText(hint, 9.0, font::mono()) + 10.0;
                const double hx = mDrag.kind == DragKind::Roll ? timeToX(mDrag.value) - tw * 0.5 : r.x + std::max(0.0, (r.w - tw) * 0.5);
                const Rect chip{hx, r.y - 16.0 < lr.y ? r.bottom() + 2.0 : r.y - 16.0, tw, 14.0};
                drawRoundedRect(t, chip, radius::control(), Paint::filledStroked(palette::popover(), palette::primaryAlpha(0.8), 1.0));
                t.setFill(palette::foreground());
                t.drawText(hint, chip.x + 5.0, textfit::baseline(chip.y + chip.h * 0.5, 9.0), 9.0, font::mono());
                if (mDrag.kind == DragKind::Roll)
                    glyph::line(t, timeToX(mDrag.value), r.y - 2.0, timeToX(mDrag.value), r.bottom() + 2.0, palette::primary(), 2.0);
            }
        }
        // the drop ghost: where a source from the bin would land (R-UI-14)
        if (const double da = mDropAmt.value(); da > 0.001)
        {
            const Rect gr{timeToX(mDropT), laneTop(mDropLane) + kClipInsetY, std::max(8.0, mDropDur * pps), th - 2 * kClipInsetY};
            const Color edge = mDropBad ? palette::destructive() : palette::primary();
            drawRoundedRect(t, gr, radius::control(), Paint::filledStroked(fade(edge, 0.16 * da), fade(edge, 0.9 * da), 1.0));
            const std::string what = mDropBad ? "a video source goes on a video track" : mDropNewTrack ? mDropLabel + " \xC2\xB7 new video track" : mDropLabel;
            const double room = gr.w - 12.0;
            if (room > 8.0)
            {
                t.setFill(fade(mDropBad ? palette::destructive() : palette::foreground(), da));
                t.drawText(textfit::ellipsize(t, what, room, 10.0, font::sansMedium()), gr.x + 6.0, gr.y + 11.5, 10.0, font::sansMedium());
                t.setFill(fade(palette::mutedForeground(), da));
                t.drawText(cmd::timecode(mDropT, mFps), gr.x + 6.0, gr.y + 22.5, 8.5, font::mono());
            }
        }
        // transitions: a bowtie over the cut, on the incoming clip's lane
        for (const auto &tr : mTransitions)
        {
            const ClipAnim *b = animFor(tr.clipB);
            if (!b) continue;
            const double cut = b->at.value();
            const double x0 = timeToX(cut - tr.dur * 0.5), x1 = timeToX(cut + tr.dur * 0.5);
            const double y0 = laneTop(b->lane.value()) + kClipInsetY + 2.0, y1 = y0 + th - 2 * kClipInsetY - 4.0;
            if (x1 < lr.x || x0 > lr.right()) continue;
            const Color c = fade(palette::white(), 0.7 * b->alpha.value());
            t.setFill(fade(palette::white(), 0.10 * b->alpha.value()));
            t.beginPath(); t.moveTo(x0, y0); t.lineTo(x1, y1); t.lineTo(x1, y0); t.lineTo(x0, y1); t.closePath(); t.fillPath();
            glyph::line(t, x0, y0, x1, y1, c, 1.0);
            glyph::line(t, x0, y1, x1, y0, c, 1.0);
        }
        // the snap guide: which edge the drag caught
        if (mGuideAmt.value() > 0.001)
        {
            const double gx = timeToX(mGuideTime);
            glyph::line(t, gx, lr.y, gx, H, palette::primaryAlpha(0.9 * mGuideAmt.value()), 1.0);
        }
        if (emptyAmt > 0.001)
        {
            const std::string s = "drag a source here";
            const double tw = t.measureText(s, 12.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), emptyAmt));
            t.drawText(s, lr.x + (lr.w - tw) * 0.5, lr.y + lr.h * 0.5 + 4.0, 12.0, font::sans());
        }
        t.restore();

        // ── track headers (the column that IS the time origin) ──
        drawRoundedRect(t, Rect{0, rh, hw, H - rh}, 0.0, Paint::filled(surface::trackHeaderBg()));
        t.save();
        t.clipRect(0, rh, hw, H - rh);
        for (const auto &kv : mTrackAnims)
        {
            const auto &tk = kv.second.data;
            const double y = laneTop(kv.second.lane.value());
            const double hA = kv.second.alpha.value();
            if (y + th < rh || y > H || hA <= 0.001) continue;
            t.pushLayer(hA);
            const double cy = y + th * 0.5;
            const bool inherited = tk.provenance == interstellar::Provenance::Inherited;
            const bool dangling = tk.provenance == interstellar::Provenance::Dangling;
            if (tk.provenance == interstellar::Provenance::Overridden)
                drawRoundedRect(t, Rect{0, y + 4, 2.0, th - 8}, 0.0, Paint::filled(palette::primary()));
            const Color gc = dangling ? palette::destructive() : palette::mutedForeground();
            if (tk.audio) glyph::speaker(t, Rect{9.75, cy - 5.5, 11, 11}, gc);
            else glyph::film(t, Rect{9.75, cy - 5.5, 11, 11}, gc);
            double right = hw - 8.0;
            if (tk.mute)
            {
                const Rect chip{right - 14.0, cy - 6.5, 14.0, 13.0};
                drawRoundedRect(t, chip, radius::hairline(), Paint::filledStroked(palette::secondary(), palette::border(), 1.0));
                t.setFill(palette::mutedForeground());
                t.drawText("M", chip.x + (14.0 - t.measureText("M", 8.0, font::sansSemiBold())) * 0.5, textfit::baseline(cy, 8.0), 8.0, font::sansSemiBold());
                right = chip.x - 4.0;
            }
            const double nx = 9.75 + 16.0;
            t.setFill(dangling ? palette::destructive() : (inherited ? palette::mutedForeground() : palette::foreground()));
            t.drawText(textfit::ellipsize(t, tk.name.empty() ? tk.id : tk.name, right - nx, 10.0, font::sansMedium()), nx, cy - 1.0, 10.0, font::sansMedium());
            t.setFill(fade(palette::mutedForeground(), 0.8));
            const std::string sub = inherited ? "inherited" : (dangling ? "dangling" : (tk.audio ? cmd::num(tk.gain) + " dB" : (cmd::num(tk.opacity * 100.0) + "%")));
            t.drawText(textfit::ellipsize(t, sub, right - nx, 8.5, font::sans()), nx, cy + 10.0, 8.5, font::sans());
            glyph::line(t, 0, y + th - 0.5, hw, y + th - 0.5, palette::border(), 1.0);
            t.popLayer();
        }
        t.restore();
        glyph::line(t, hw - 0.5, 0, hw - 0.5, H, palette::border(), 1.0);

        // ── ruler (the same origin) ──
        const Rect rr = rulerRect();
        drawRoundedRect(t, Rect{0, 0, W, rh}, 0.0, Paint::filled(surface::rulerBg()));
        drawRoundedRect(t, Rect{0, 0, hw, rh}, 0.0, Paint::filled(surface::trackHeaderBg()));
        glyph::line(t, 0, rh - 0.5, W, rh - 0.5, palette::border(), 1.0);
        t.save();
        t.clipRect(rr.x, rr.y, rr.w, rr.h);
        {
            static const double kSteps[] = {1.0 / 24, 2.0 / 24, 6.0 / 24, 12.0 / 24, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800};
            double step = kSteps[sizeof kSteps / sizeof kSteps[0] - 1];
            for (double s : kSteps) if (s * pps >= 76.0) { step = s; break; }
            const double minor = step / 4.0;
            const double t0 = std::max(0.0, std::floor(xToTime(rr.x) / step) * step);
            const double t1 = xToTime(rr.right()) + step;
            if (minor * pps >= 6.0)
                for (double tm = t0; tm <= t1; tm += minor)
                {
                    const double x = timeToX(tm);
                    glyph::line(t, x, rh - 5.0, x, rh - 1.0, palette::whiteAlpha(0.12), 1.0);
                }
            for (double tm = t0; tm <= t1 + 1e-9; tm += step)
            {
                const double x = timeToX(tm);
                glyph::line(t, x, rh - 10.0, x, rh - 1.0, palette::whiteAlpha(0.28), 1.0);
                t.setFill(palette::mutedForeground());
                t.drawText(tickLabel(tm, step, mFps), x + 3.0, 10.5, 8.5, font::mono());
            }
        }
        // the preview cache (R-PLAY-1): a 2-px bar along the ruler's top, per second of the timeline
        for (size_t n = 0; n < mCacheSegs.size(); ++n)
        {
            const CacheSegAnim &cs = mCacheSegs[n];
            const double x0 = timeToX(n * mCacheSegSeconds), x1 = timeToX((n + 1) * mCacheSegSeconds);
            if (x1 < rr.x || x0 > rr.right()) continue;
            const Rect bar{x0, 0.0, std::max(0.0, x1 - x0 - 1.0), 2.0};   // a 1-px gap shows the seconds
            if (cs.stale.value() > 0.01) drawRoundedRect(t, bar, 0.0, Paint::filled(palette::whiteAlpha(0.22 * cs.stale.value())));
            if (cs.cached.value() > 0.01) drawRoundedRect(t, bar, 0.0, Paint::filled(palette::successAlpha(0.9 * cs.cached.value())));
            if (cs.building.value() > 0.01) drawRoundedRect(t, bar, 0.0, Paint::filled(palette::primaryAlpha(0.9 * cs.building.value())));
        }
        for (const auto &mk : mMarkers)
        {
            const double x = timeToX(mk.at);
            if (x < rr.x - 60 || x > rr.right()) continue;
            t.setFill(surface::marker());
            t.beginPath(); t.moveTo(x - 4.0, rh - 9.0); t.lineTo(x + 4.0, rh - 9.0); t.lineTo(x, rh - 3.0); t.closePath(); t.fillPath();
            if (!mk.name.empty())
            {
                t.setFill(palette::foreground());
                t.drawText(textfit::ellipsize(t, mk.name, 90.0, 8.5, font::sans()), x + 6.0, rh - 4.0, 8.5, font::sans());
            }
        }
        t.restore();

        // zoom controls in the corner
        for (int k = 0; k < 2; ++k)
        {
            const Rect b = k == 0 ? zoomOutRect() : zoomInRect();
            const double hv = mUi.amount(k);
            if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color c = lerpColor(palette::mutedForeground(), palette::foreground(), 0.7 * hv);
            if (k == 0) glyph::minus(t, Rect{b.x + 4, b.y + 4, 10, 10}, c); else glyph::plus(t, Rect{b.x + 4, b.y + 4, 10, 10}, c);
        }
        {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%.0f px/s", pps);
            t.setFill(fade(palette::mutedForeground(), 0.8));
            const std::string z = textfit::ellipsize(t, buf, hw - 58.0 - 6.0, 8.5, font::mono());
            t.drawText(z, 58.0, textfit::baseline(rh * 0.5, 8.5), 8.5, font::mono());
        }

        // ── the playhead: destructive, a position (ui-brief §1) ──
        const double px = timeToX(mShownPlayhead.value());
        if (px >= hw - 0.5 && px <= W + 0.5)
        {
            t.save();
            t.clipRect(hw, 0, W - hw, H);
            glyph::line(t, px, rh - 2.0, px, H, surface::playhead(), 1.0);
            t.setFill(surface::playhead());
            t.beginPath();
            t.moveTo(px - 5.0, 2.0); t.lineTo(px + 5.0, 2.0); t.lineTo(px + 5.0, rh - 9.0); t.lineTo(px, rh - 3.0); t.lineTo(px - 5.0, rh - 9.0);
            t.closePath(); t.fillPath();
            t.restore();
        }
        mVScroll.drawBar(t, W - 2.0);
        const double maxS = maxScrollFor(pps);
        if (maxS > 0.5)
        {
            const double vw = lr.w, cw = contentSeconds() * pps;
            const double thumbW = std::max(24.0, vw * vw / cw);
            const double x = lr.x + (vw - thumbW) * (mScroll.value() / maxS);
            drawRoundedRect(t, Rect{x, H - 4.0, thumbW, 3.0}, radius::pill(), Paint::filled(palette::whiteAlpha(0.14)));
        }
    }
}
}
