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
        mModelRef = &m;
        mBeatsPerBar = std::max(1, std::atoi(m.sig.c_str()));
        mArmed = m.armed; // R-MIDI-2
        mStrips.clear();
        for (const auto &s : m.strips) mStrips.push_back(StripRef{s.id, s.name, s.kind});
        mLength = m.lengthBeats;
        mPosition = m.transport.position;
        mCue = m.transport.cue;
        mPlaying = m.transport.playing;
        mLoopFrom = m.transport.loopFrom;
        mLoopTo = m.transport.loopTo;
        mPunchFrom = m.transport.punchFrom; // R-REC-1
        mPunchTo = m.transport.punchTo;
        if (m.projectPath != mSong)
        {
            // another song: nothing of the last one may travel into it
            mSong = m.projectPath;
            mRowMotion = interstellar_v1::AnimatedRows<Row>();
            mLive.clear();
            mStripes.clear();
            mTracks.clear();
            mMarkers.clear(); // R-ARR-1
            mTakesOpen.clear(); // R-REC-2
            mTakeLit.clear();
            mEver = mBound = mEmptyInit = mLoopInit = false;
        }
        bindMarkers(m); // R-ARR-1: after a song change has cleared the last song's
        bindTakes(m);   // R-REC-2
        std::map<std::string, int> colourOf; // the service resolves a strip's colour (stable by id)
        for (const auto &st : m.strips) colourOf[st.id] = st.colour;
        mRows.clear();
        std::map<std::string, int> rowOfLane, rowOfStrip, rowOfGroup;
        std::map<std::string, const solaris::LaneGroupModel *> groupOf; // R-ARR-3
        for (const auto &g : m.groups)
            for (const auto &ln : g.lanes) groupOf[ln] = &g;
        for (const auto &l : m.lanes)
        {
            const solaris::LaneGroupModel *g = groupOf.count(l.id) ? groupOf[l.id] : nullptr;
            if (g && g->folded)
            {
                // R-ARR-3: a folded group is ONE row, its lanes' clips in it
                if (!rowOfGroup.count(g->id))
                {
                    rowOfGroup[g->id] = (int)mRows.size();
                    Row gr{"group:" + g->id, "", g->name, -1};
                    gr.group = g->id;
                    gr.groupName = g->name;
                    gr.folded = true;
                    gr.groupLanes = (int)g->lanes.size();
                    mRows.push_back(gr);
                }
                rowOfLane[l.id] = rowOfGroup[g->id];
                continue;
            }
            rowOfLane[l.id] = (int)mRows.size();
            Row r{l.id, l.id, l.name, l.colour};
            if (g)
            {
                r.group = g->id;
                r.groupName = g->name;
                r.groupHead = !g->lanes.empty() && g->lanes.front() == l.id;
                r.groupLanes = (int)g->lanes.size();
            }
            if (!l.strip.empty())
            {
                // R-LANE-3: an instrument's track — its stripe the strip's colour (unless it has its own), its
                // instrument named under its name
                r.strip = l.strip;
                if (r.colour < 0) r.colour = colourOf[l.strip];
                for (const auto &st : m.strips)
                    if (st.id == l.strip)
                        for (const auto &d : st.devices)
                            if (d.instrument && r.sub.empty()) r.sub = d.label;
            }
            mRows.push_back(r);
            for (const auto &c : m.clips) // R-REC-2: a clip's takes, shown, are rows under its lane
                if (c.lane == l.id && !c.takes.empty() && mTakesOpen.count(c.id))
                    for (int k = 1; k <= (int)c.takes.size(); ++k)
                    {
                        Row tr{"take:" + c.id + ":" + std::to_string(k), "", "Take " + std::to_string(k), r.colour};
                        tr.takeClip = c.id;
                        tr.take = k;
                        tr.strip = "";
                        mRows.push_back(tr);
                    }
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
            if (!c.lane.empty() && groupOf.count(c.lane) && groupOf[c.lane]->folded) // R-ARR-3: its lane's band of the group's row
            {
                const auto &ls = groupOf[c.lane]->lanes;
                const size_t k = (size_t)(std::find(ls.begin(), ls.end(), c.lane) - ls.begin());
                v.bandOff = (double)k / (double)ls.size();
                v.bandFrac = 1.0 / (double)ls.size();
            }
            v.colour = colourOf.count(c.track) ? colourOf[c.track] : 0;
            for (const auto &p : m.patterns)
                if (p.id == c.pattern) { v.notes = p.notes; v.patternLength = p.length; }
            mClips.push_back(v);
        }
        if (mAdoptNew)
        {
            // R-CLIP-9/10: the copy just made is the clip that was not there — it is selected; a refusal brings none
            for (const auto &v : mClips)
                if (!mKnownClips.count(v.c.id))
                {
                    if (mAdoptInstant) mInstantClip = v.c.id;
                    selectClip(v.c.id);
                    break;
                }
            mAdoptNew = false;
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
            l->lenTarget = v.c.length;
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

    double Timeline::trackAmount(const std::string &lane) const
    {
        const auto it = mTracks.find(lane);
        return it == mTracks.end() ? 0.0 : it->second.amt.value();
    }

    std::string Timeline::trackLabel(const std::string &lane) const
    {
        const auto it = mTracks.find(lane);
        return it == mTracks.end() ? std::string() : it->second.sub;
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

    Rect Timeline::armRect(int i) const
    {
        if (i < 0 || i >= (int)mRows.size() || mRows[(size_t)i].strip.empty()) return Rect{};
        const Rect r = rowRect(i);
        return Rect{kHeaderW - 24.0, r.y + 9.75, 13.0, 13.0}; // space::u(4): at the header's right, level with the name
    }

    double Timeline::armAmount(const std::string &strip) const
    {
        const auto it = mArm.find(strip);
        return it == mArm.end() ? 0.0 : it->second.value();
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

    double Timeline::liveLength(const ClipLive &l) const
    {
        if (mResizing && l.v.c.id == mEdgeClip) return mResizeLen;
        return l.placed ? l.len.value() : l.lenTarget;
    }

    double Timeline::clipLengthLive(const std::string &id) const
    {
        const ClipLive *l = live(id);
        return l ? liveLength(*l) : 0.0;
    }

    Rect Timeline::clipBox(const ClipView &, double at, double y, double len, double off, double frac) const
    {
        const double top = kRulerH + y - mScrollY.value(), h = kRowH - 6.0;
        const double x0 = beatToX(at), x1 = beatToX(at + std::max(0.25, len));
        // R-ARR-3: in a folded group's row each lane keeps a band of it, so its clips never lie on one another
        return Rect{x0, top + 3.0 + off * h, std::max(3.0, x1 - x0 - 1.0), std::max(2.0, frac * h - (frac < 0.999 ? 1.0 : 0.0))};
    }

    Rect Timeline::punchRect() const
    {
        if (mPunchAmt.value() <= 0.001 && !(mPunchTo > mPunchFrom)) return Rect{};
        const double a = beatToX(mPunchA.value()), b = beatToX(mPunchB.value());
        return Rect{a, kMarkH + 1.5, std::max(0.0, b - a), 3.0}; // atop the bars, under the markers' band
    }

    // R-ARR-3: a group's chevron and name — on its folded row, or its first lane's header, in the bottom band
    Rect Timeline::groupToggleRect(const std::string &lg) const
    {
        for (const auto &row : mRowMotion.rows())
            if (!row.gone && row.data.group == lg && (row.data.folded || row.data.groupHead))
                return Rect{6.0, kRulerH + row.liveY() - mScrollY.value() + 34.0, kHeaderW - 12.0, 13.0};
        return Rect{};
    }
    double Timeline::groupTurn(const std::string &lg) const
    {
        const auto it = mGroupTurn.find(lg);
        return it == mGroupTurn.end() ? 0.0 : it->second.value();
    }
    int Timeline::groupRow(const std::string &lg) const
    {
        for (size_t i = 0; i < mRows.size(); ++i)
            if (mRows[i].folded && mRows[i].group == lg) return (int)i;
        return -1;
    }
    double Timeline::groupDelta() const
    {
        if (mDragGroup.empty() || !mDragging) return 0.0;
        double d = 0.0, first = 1e300;
        for (const auto &v : mClips)
        {
            if (v.c.id == mPressClip) d = mDragBeat - v.c.at;
            if (std::find(mDragGroupClips.begin(), mDragGroupClips.end(), v.c.id) != mDragGroupClips.end()) first = std::min(first, v.c.at);
        }
        return std::max(d, first < 1e299 ? -first : d); // none of them before beat 0
    }

    Rect Timeline::clipRect(const std::string &id) const
    {
        const ClipLive *l = live(id);
        if (!l) return Rect{};
        if (mDragging && id == mPressClip) return clipBox(l->v, mDragBeat, rowY(mDragRow), liveLength(*l), l->off.value(), l->frac.value());
        if (!mDragGroup.empty() && mDragging && std::find(mDragGroupClips.begin(), mDragGroupClips.end(), id) != mDragGroupClips.end())
            return clipBox(l->v, l->v.c.at + groupDelta(), l->row.value() * kRowH, liveLength(*l), l->off.value(), l->frac.value()); // carried with the group
        return l->placed ? clipBox(l->v, l->at.value(), l->row.value() * kRowH, liveLength(*l), l->off.value(), l->frac.value())
                         : clipBox(l->v, l->atTarget, l->rowTarget * kRowH, liveLength(*l), l->v.bandOff, l->v.bandFrac);
    }

    std::string Timeline::clipAt(const Point &p) const
    {
        if (p.x < kHeaderW || p.y < kRulerH) return std::string();
        for (auto it = mLive.rbegin(); it != mLive.rend(); ++it) // topmost (drawn last) first; a ghost takes no input
            if (!it->gone && clipRect(it->v.c.id).contains(p)) return it->v.c.id;
        return std::string();
    }

    Cursor Timeline::dragCursor() const
    {
        if (mResizing || !mSpanAu.empty()) return Cursor::ResizeH; // a clip's end, an automation span's grip (R-AUTO-12)
        if (mEditZone == 1 || mEditZone == 2) return Cursor::ResizeH; // a fade handle held (R-EDIT-1)
        if (mEditZone == 3) return Cursor::ResizeV;                   // the gain line held
        const RulerDrag held = mRuler == RulerDrag::Pending ? mRulerZone : mRuler; // pressed on an end, not yet moved
        return held == RulerDrag::LoopFrom || held == RulerDrag::LoopTo ? Cursor::ResizeH : Cursor::Default;
    }

    Cursor Timeline::cursorAt(const Point &local) const
    {
        const Cursor held = dragCursor();
        if (held != Cursor::Default || mDragging || mLoopDragging || mRuler != RulerDrag::None) return held;
        // the loop brace's ends — as the press reads them (rulerGesture)
        const Rect br = loopRect();
        if (rulerRect().contains(local) && br.w > 0 && mLoopTo > mLoopFrom && local.y >= kMarkH + (kRulerH - kMarkH) * 0.5 &&
            (std::fabs(local.x - br.x) <= kBraceGrip || std::fabs(local.x - br.right()) <= kBraceGrip))
            return Cursor::ResizeH;
        // an automation span's grip (R-AUTO-12) — as the press reads it
        if (local.x >= kHeaderW && local.y >= kRulerH)
            if (const std::string au = autoAt(local.y); !au.empty() && spanGripAt(au, local) >= 0) return Cursor::ResizeH;
        // an audio clip's fade handles and gain line (R-EDIT-1) — as the press reads them
        {
            std::string eid;
            const int z = clipEditZoneAt(local, eid);
            if (z == 1 || z == 2) return Cursor::ResizeH;
            if (z == 3) return Cursor::ResizeV;
        }
        // a clip's end (R-CLIP-7) — as the press reads it
        const std::string id = clipAt(local);
        if (!id.empty())
        {
            const Rect r = clipRect(id);
            if (local.x >= r.right() - kEdgeGrip && r.w > 3.0 * kEdgeGrip) return Cursor::ResizeH;
        }
        return Cursor::Default;
    }

    void Timeline::expectCopy(bool instant)
    {
        mKnownClips.clear();
        for (const auto &v : mClips) mKnownClips.insert(v.c.id);
        mAdoptNew = true;
        mAdoptInstant = instant;
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
        // R-TIME-7: the cue's mark — where Stop takes the time bar back — fades in with the play and out with it
        if (mPlaying != mCueShown)
        {
            mCueAmt.animateTo(mPlaying ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mCueShown = mPlaying;
        }
        mCueAmt.update(nowMs);
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
        for (const auto &row : mRows)
        {
            if (row.lane.empty()) continue;
            TrackLive &tk = mTracks[row.lane];
            const bool want = !row.strip.empty();
            if (!row.sub.empty()) tk.sub = row.sub;
            if (!tk.placed)
            {
                tk.amt.set(want ? 1.0 : 0.0); // nowhere to travel from: placed
                tk.want = want;
                tk.placed = true;
            }
            else if (want != tk.want)
            {
                tk.want = want;
                tk.amt.animateTo(want ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
            tk.amt.update(nowMs);
        }
        // R-MIDI-2: an instrument track's arm light, eased on and off
        for (const auto &row : mRows)
        {
            if (row.strip.empty()) continue;
            const double want = row.strip == mArmed ? 1.0 : 0.0;
            auto it = mArm.find(row.strip);
            if (it == mArm.end()) { mArm[row.strip].set(want); mArmLast[row.strip] = want; continue; }
            if (mArmLast[row.strip] != want) { it->second.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mArmLast[row.strip] = want; }
            it->second.update(nowMs);
        }
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
        advanceMarkers(nowMs); // R-ARR-1
        advanceTakes(nowMs);   // R-REC-2
        {
            // R-REC-1: the punch — present or not, eased; its ends travel when it moves (from where they ARE)
            const bool on = mPunchTo > mPunchFrom;
            if (!mPunchInit)
            {
                mPunchA.set(mPunchFrom);
                mPunchB.set(mPunchTo);
                mPunchAmt.set(on ? 1.0 : 0.0);
                mPunchALast = mPunchFrom;
                mPunchBLast = mPunchTo;
                mPunchOnLast = on;
                mPunchInit = true;
            }
            if (on != mPunchOnLast) { mPunchAmt.animateTo(on ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); mPunchOnLast = on; }
            if (on && (mPunchFrom != mPunchALast || mPunchTo != mPunchBLast))
            {
                if (mPunchAmt.value() <= 0.001) { mPunchA.set(mPunchFrom); mPunchB.set(mPunchTo); } // appearing: nowhere to travel from
                else { mPunchA.animateTo(mPunchFrom, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); mPunchB.animateTo(mPunchTo, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); }
                mPunchALast = mPunchFrom;
                mPunchBLast = mPunchTo;
            }
            mPunchA.update(nowMs);
            mPunchB.update(nowMs);
            mPunchAmt.update(nowMs);
        }
        for (const auto &r : mRows) // R-ARR-3: a group's chevron turns, eased, as it folds and opens
        {
            if (r.group.empty()) continue;
            const double want = r.folded ? 1.0 : 0.0;
            auto &turn = mGroupTurn[r.group];
            const auto last = mGroupTurnLast.find(r.group);
            if (last == mGroupTurnLast.end()) turn.set(want);
            else if (last->second != want) turn.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mGroupTurnLast[r.group] = want;
        }
        for (auto &g : mGroupTurn) g.second.update(nowMs);
        const bool fadeIn = mEver && !reducedMotion();
        mRowMotion.advance(nowMs);
        {
            // R-CLIP-9: Shift pressed or let go mid-drag eases between a move and a copy
            const double cw = mDragging && mDropCopy ? 1.0 : 0.0;
            if (cw != mCopyLast) { mCopyAmt.animateTo(cw, motion::kHoverMs, Easing::EaseOutCubic, nowMs); mCopyLast = cw; }
            mCopyAmt.update(nowMs);
        }
        for (auto &l : mLive)
        {
            const double aWant = l.gone ? 0.0 : 1.0;
            if (!l.placed)
            {
                l.at.set(l.atTarget);
                l.row.set(l.rowTarget);
                l.len.set(l.lenTarget);
                l.lenLast = l.lenTarget;
                l.atLast = l.atTarget;
                l.rowLast = l.rowTarget;
                l.off.set(l.v.bandOff);
                l.frac.set(l.v.bandFrac);
                l.offLast = l.v.bandOff;
                l.fracLast = l.v.bandFrac;
                placeClipEdit(l);
                const bool instant = l.v.c.id == mInstantClip; // a Shift-drop: the pointer already showed it there
                l.alpha.set(fadeIn && !instant ? 0.0 : 1.0);
                l.aLast = l.alpha.value();
                if (instant) mInstantClip.clear();
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
            if (l.lenTarget != l.lenLast) { l.len.animateTo(l.lenTarget, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.lenLast = l.lenTarget; }
            l.len.update(nowMs);
            if (l.v.bandOff != l.offLast) { l.off.animateTo(l.v.bandOff, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.offLast = l.v.bandOff; }
            if (l.v.bandFrac != l.fracLast) { l.frac.animateTo(l.v.bandFrac, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.fracLast = l.v.bandFrac; }
            l.off.update(nowMs);
            l.frac.update(nowMs);
            advanceClipEdit(l, nowMs); // R-EDIT-1: fades, gain, reversed — eased
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

    bool Timeline::newClipOn(const Point &local)
    {
        // R-CLIP-6: a one-bar MIDI clip on the instrument's track under the pointer, on the snap step — ONE
        // `clip add`, the track's instrument implied, a new empty pattern for it
        if (local.x < kHeaderW || local.y < kRulerH || !onCommand) return false;
        const int ri = rowAt(local.y);
        if (ri < 0 || ri >= (int)mRows.size() || mRows[(size_t)ri].strip.empty()) return false;
        onCommand("clip add --lane " + q(mRows[(size_t)ri].lane) + " --at " + beats(snap(xToBeat(local.x))) + " --length " + beats((double)mBeatsPerBar));
        return true;
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
            if (br.w > 0 && mLoopTo > mLoopFrom && local.y >= kMarkH + (kRulerH - kMarkH) * 0.5) // the bars' band's lower half
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
        if (takeGesture(g, local)) return true; // a take's row: a range of it into the comp (R-REC-2)
        if (clipEditGesture(g, local)) return true; // an audio clip's fade handles and gain line (R-EDIT-1)
        if (markerGesture(g, local)) return true; // a marker's flag on the ruler's band (R-ARR-1)
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
            mEdgeClip.clear();
            if (!mPressClip.empty())
            {
                // R-CLIP-7: on its right edge the press takes the clip's END, not the clip
                const Rect r = clipRect(mPressClip);
                if (local.x >= r.right() - kEdgeGrip && r.w > 3.0 * kEdgeGrip) { mEdgeClip = mPressClip; mPressClip.clear(); }
            }
            if (const ClipLive *l = live(mPressClip)) mGrab = xToBeat(local.x) - l->at.value(); // where it is DRAWN: a drag never teleports
            mDragGroup.clear(); // R-ARR-3: a clip on a folded group's row carries the group's clips
            mDragGroupClips.clear();
            for (const auto &v : mClips)
                if (v.c.id == mPressClip && v.row >= 0 && v.row < (int)mRows.size() && mRows[(size_t)v.row].folded)
                {
                    mDragGroup = mRows[(size_t)v.row].group;
                    for (const auto &o : mClips)
                        if (o.row == v.row) mDragGroupClips.push_back(o.c.id);
                }
            return true;
        case Gesture::Type::DragStart:
            if (!mEdgeClip.empty())
            {
                mResizing = true;
                if (const ClipLive *l = live(mEdgeClip)) mResizeLen = liveLength(*l);
                selectClip(mEdgeClip);
            }
            if (!mPressClip.empty())
            {
                mDragging = true;
                selectClip(mPressClip);
            }
            [[fallthrough]];
        case Gesture::Type::Drag:
            if (mResizing)
            {
                // the end on the grid you see, never shorter than one step of it
                if (const ClipLive *l = live(mEdgeClip))
                    mResizeLen = std::max(std::max(snapStep(), 1.0 / kPpq), snap(xToBeat(local.x)) - l->atTarget);
                return true;
            }
            if (mDragging)
            {
                mDropCopy = g.shift && mDragGroup.empty(); // R-CLIP-9: held, the drop duplicates (a group's drag never copies)
                mDragBeat = std::max(0.0, snap(xToBeat(local.x) - mGrab));
                int row = rowAt(local.y);
                for (const auto &v : mClips)
                    if (v.c.id == mPressClip && (row < 0 || row >= (int)mRows.size() || mRows[(size_t)row].lane.empty())) row = v.row;
                mDragRow = row; // a clip moves between LANES; a strip's own row is not a lane
            }
            return true;
        case Gesture::Type::Drop:
        case Gesture::Type::Up:
            if (mResizing)
            {
                // where it was let go — the pointer was the animation — then ONE line; a refusal eases it back
                mResizing = false;
                std::string send;
                for (auto &l : mLive)
                    if (!l.gone && l.v.c.id == mEdgeClip)
                    {
                        l.len.set(mResizeLen);
                        l.lenTarget = l.lenLast = mResizeLen;
                        if (std::fabs(mResizeLen - l.v.c.length) > 1e-9) send = "set " + l.v.c.id + ".length=" + beats(mResizeLen);
                        break;
                    }
                mEdgeClip.clear();
                if (!send.empty() && onCommand) onCommand(send); // after the loop: the model it brings back re-keys mLive
                return true;
            }
            mEdgeClip.clear();
            if (mDragging && !mDragGroup.empty())
            {
                // R-ARR-3: the group's clips where they were let go — the pointer was the animation — then ONE line
                const double d = groupDelta();
                mDragging = false;
                std::string ids;
                for (auto &l : mLive)
                    if (!l.gone && std::find(mDragGroupClips.begin(), mDragGroupClips.end(), l.v.c.id) != mDragGroupClips.end())
                    {
                        l.at.set(l.v.c.at + d);
                        l.atTarget = l.atLast = l.v.c.at + d;
                        ids += (ids.empty() ? "" : " ") + l.v.c.id;
                    }
                mDragGroup.clear();
                mDragGroupClips.clear();
                mPressClip.clear();
                if (std::fabs(d) > 1e-9 && onCommand) onCommand("clips move \"" + ids + "\" --by " + beats(d));
                return true;
            }
            if (mDragging)
            {
                mDragging = false;
                mDropCopy = g.shift; // as it is when let go
                std::string send;
                bool copy = false;
                for (auto &l : mLive)
                    if (!l.gone && l.v.c.id == mPressClip && mDropCopy)
                    {
                        // R-CLIP-9: Shift held — a copy where it was dropped, ONE line; the original stays where it is
                        const ClipView &v = l.v;
                        send = "clip duplicate " + v.c.id + " --at " + beats(mDragBeat);
                        if (mDragRow != v.row) send += " --lane " + q(mRows[(size_t)mDragRow].lane);
                        copy = true;
                        break;
                    }
                for (auto &l : mLive)
                    if (!copy && !l.gone && l.v.c.id == mPressClip)
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
                if (copy) expectCopy(true); // the copy appears where the pointer left it, selected
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
            for (const auto &r : mRows) // R-ARR-3: a group's chevron and name — folded or opened, ONE line
                if (!r.group.empty() && (r.folded || r.groupHead) && groupToggleRect(r.group).contains(local))
                {
                    if (onCommand) onCommand("set " + r.group + ".folded=" + (r.folded ? "off" : "on"));
                    return true;
                }
            for (int i = 0; i < (int)mRows.size(); ++i)
            {
                // R-MIDI-2: an instrument track's arm button — ONE line, arming it (or disarming it, armed)
                const Rect ar = armRect(i);
                if (ar.w <= 0.0 || !Rect{ar.x - 3.0, ar.y - 3.0, ar.w + 6.0, ar.h + 6.0}.contains(local)) continue;
                const std::string s = mRows[(size_t)i].strip;
                if (onCommand) onCommand(s == mArmed ? std::string("strip arm none") : "strip arm " + s);
                return true;
            }
            selectClip(clipAt(local));
            return true;
        case Gesture::Type::DoubleClick:
            if (clipAt(local).empty() && newClipOn(local)) return true; // R-CLIP-6: on an instrument's track, a MIDI clip there
            for (const auto &v : mClips)
                if (v.c.id == clipAt(local) && v.c.kind == "note" && onOpenPattern) onOpenPattern(v.c.pattern, v.c.id);
            return true;
        case Gesture::Type::RightClick:
        {
            if (local.x < kHeaderW && local.y >= kRulerH)
            {
                // a lane's menu (R-LANE-3): whose track it is — or a plain lane — each one line
                const int ri = rowAt(local.y);
                if (ri >= 0 && ri < (int)mRows.size() && mRows[(size_t)ri].folded && onMenu)
                {
                    // R-ARR-3: a folded group's row — open it, rename it, ungroup it
                    const std::string lg = mRows[(size_t)ri].group, name = mRows[(size_t)ri].groupName;
                    const Point world = g.pos;
                    onMenu({{"Open group", [this, lg] { if (onCommand) onCommand("set " + lg + ".folded=off"); }},
                            {"Rename group\xE2\x80\xA6", [this, lg, name, world] {
                                 if (onRename) onRename(name, world, [this, lg](const std::string &n) { if (onCommand && !n.empty()) onCommand("set " + lg + ".name=" + q(n)); });
                             }},
                            {"Ungroup", [this, lg] { if (onCommand) onCommand("lane ungroup " + lg); }},
                            {"Copy ID", [this, lg] { if (onCopy) onCopy(lg); }}},
                           world);
                    return true;
                }
                if (ri < 0 || ri >= (int)mRows.size() || mRows[(size_t)ri].lane.empty() || !onMenu) return true;
                const std::string ln = mRows[(size_t)ri].lane, owner = mRows[(size_t)ri].strip;
                std::vector<cosmo_v2::ContextMenu::Item> owners, items;
                for (const auto &s : mStrips)
                    if (s.kind == "instrument")
                    {
                        const std::string sid = s.id;
                        owners.push_back({s.name + (sid == owner ? "  \xC2\xB7 now" : ""),
                                          [this, ln, sid] { if (onCommand) onCommand("set " + ln + ".strip=" + sid); }});
                    }
                const Point world = g.pos;
                if (!owners.empty()) items.push_back({"Track of \xE2\x96\xB8", [this, owners, world] { if (onMenu) onMenu(owners, world); }});
                if (!owner.empty()) items.push_back({"Plain Lane", [this, ln] { if (onCommand) onCommand("set " + ln + ".strip=none"); }});
                {
                    // R-ARR-3: its group — a new one, into another, out of its own, folded, ungrouped
                    const std::string lg = mRows[(size_t)ri].group;
                    std::vector<cosmo_v2::ContextMenu::Item> into;
                    std::set<std::string> seen;
                    for (const auto &r : mRows)
                        if (!r.group.empty() && seen.insert(r.group).second && r.group != lg)
                            into.push_back({r.groupName, [this, ln, gid = r.group] { if (onCommand) onCommand("set " + ln + ".group=" + gid); }});
                    if (lg.empty())
                    {
                        const std::string name = "Group " + std::to_string(seen.size() + 1);
                        items.push_back({"New group", [this, ln, name] { if (onCommand) onCommand("lane group " + ln + " --name " + q(name)); }});
                    }
                    if (!into.empty()) items.push_back({"Into group\xE2\x80\xA6", [this, into, world] { if (onMenu) onMenu(into, world); }});
                    if (!lg.empty())
                    {
                        items.push_back({"Leave group", [this, ln] { if (onCommand) onCommand("set " + ln + ".group=none"); }});
                        items.push_back({"Fold group", [this, lg] { if (onCommand) onCommand("set " + lg + ".folded=on"); }});
                        items.push_back({"Ungroup", [this, lg] { if (onCommand) onCommand("lane ungroup " + lg); }});
                    }
                }
                items.push_back({"Copy ID", [this, ln] { if (onCopy) onCopy(ln); }}); // R-UI-11
                onMenu(std::move(items), world);
                return true;
            }
            // a clip's menu: what it plays through (R-MIX-14), its notes, a copy, gone — each one line
            const std::string id = clipAt(local);
            if (id.empty() && onMenu && local.x >= kHeaderW)
            {
                // an instrument's track, where no clip is: New MIDI Clip there (R-CLIP-6)
                const int ri = rowAt(local.y);
                if (ri >= 0 && ri < (int)mRows.size() && !mRows[(size_t)ri].strip.empty())
                {
                    const Point here = local, world = g.pos;
                    onMenu({{"New MIDI Clip", [this, here] { newClipOn(here); }}}, world);
                }
                return true;
            }
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
                items.push_back({"Piano Roll", [this, pt, id] { if (onOpenPattern) onOpenPattern(pt, id); }});
            }
            if (!c.takes.empty()) // R-REC-2: its takes under it — the view's
            {
                const bool shown = mTakesOpen.count(id) > 0;
                items.push_back({shown ? "Hide takes" : "Show takes", [this, id, shown] { showTakes(id, !shown); }});
            }
            clipEditItems(c, items, world); // R-EDIT-1: split, reverse, consolidate, slice
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

    void Timeline::paintClip(IRenderTarget &t, const ClipView &v, const Rect &r, double a, double ring, const Color &hue, double len) const
    {
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(hue, 0.32 * a), fade(hue, 0.85 * a), 1.0));
        t.save();
        t.clipRect(r.x, r.y, r.w, r.h);
        if (!v.c.takes.empty()) // R-REC-2: how many takes it has, at its top right — where there is room beside its name (R5)
        {
            const std::string n = std::to_string(v.c.takes.size()) + " takes";
            const std::string name = v.c.name.empty() ? v.c.id : v.c.name;
            const double nw = t.measureText(n, 9.0, font::sans());
            if (r.w > nw + t.measureText(name, 11.0, font::sans()) + 24.0)
            {
                t.setFill(fade(palette::foreground(), 0.75 * a));
                t.drawText(n, r.right() - nw - 5.0, r.y + 12.0, 9.0, font::sans());
            }
        }
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
            for (double k = 0; k < len - 1e-9; k += v.patternLength)
            {
                if (k > 0)
                {
                    t.setStroke(fade(hue, 0.5 * a), 1.0);
                    t.beginPath(); t.moveTo(r.x + k * ppb, r.y + 15.0); t.lineTo(r.x + k * ppb, r.bottom()); t.strokePath();
                }
                for (const auto &n : v.notes)
                {
                    if (k + n.at >= len) continue;
                    const double y = hi == lo ? top + h * 0.5 : top + h * (1.0 - (double)(n.pitch - lo) / (hi - lo)) - 1.5;
                    const double w = std::max(2.0, std::min(n.length, len - k - n.at) * ppb - 1.0);
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
            // no peaks yet (the waveform arrives with the peaks hook): its gain line, its fades as heard (R-EDIT-1)
            paintClipEdit(t, v, r, a, hue);
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
        t.drawText("Snap", 12.0, kMarkH + 13.0, 9.0, font::sans());
        for (const auto &n : mStepNames)
            if (n.a.value() > 0.001)
            {
                t.setFill(fade(palette::foreground(), 0.8 * n.a.value()));
                t.drawText(textfit::ellipsize(t, n.text, kHeaderW - 8.0 - nameX, 9.0, font::mono()), nameX, kMarkH + 13.0, 9.0, font::mono());
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
            t.drawText(s, x + 4.0, kMarkH + 13.0, 9.0, font::mono());
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
            const bool carried = !l.gone && mDragging && !mDragGroup.empty() &&
                                 std::find(mDragGroupClips.begin(), mDragGroupClips.end(), v.c.id) != mDragGroupClips.end(); // R-ARR-3
            const bool dragged = !l.gone && mDragging && (v.c.id == mPressClip || carried);
            const Rect r = clipBox(v, l.at.value(), l.row.value() * kRowH, liveLength(l), l.off.value(), l.frac.value());
            if (r.right() < kHeaderW || r.x > W || r.bottom() < kRulerH || r.y > H) continue;
            const double ring = l.gone ? 0.0 : (v.c.id == mSelected ? mSelIn.value() : (v.c.id == mPrevSelected ? mSelOut.value() : 0.0));
            // dragged: the original dims where it was — unless Shift makes it a copy, when it stays at full strength
            paintClip(t, v, r, (dragged ? 0.35 + 0.65 * mCopyAmt.value() : 1.0) * a, dragged ? 0.0 : ring, lerpColor(l.hueFrom, l.hueTo, l.hueT.value()), liveLength(l));
            int hi = -1;
            for (size_t i = 0; i < mClips.size() && !l.gone; ++i)
                if (mClips[i].c.id == v.c.id) hi = (int)i;
            const double hv = hi >= 0 ? mHover.amount(hi) : 0.0;
            if (hv > 0.001 && !dragged) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
        }
        if (mDragging && !mDragGroup.empty())
            for (const auto &id : mDragGroupClips) // R-ARR-3: the group's clips carried with the one in hand
                if (const ClipLive *l = live(id); l && id != mPressClip)
                    paintClip(t, l->v, clipRect(id), 1.0, 0.0, lerpColor(l->hueFrom, l->hueTo, l->hueT.value()), liveLength(*l));
        if (mDragging)
            if (const ClipLive *l = live(mPressClip))
            {
                const Rect dr = clipBox(l->v, mDragBeat, rowY(mDragRow), liveLength(*l), l->off.value(), l->frac.value());
                paintClip(t, l->v, dr, 1.0, 1.0, lerpColor(l->hueFrom, l->hueTo, l->hueT.value()), liveLength(*l));
                const double k = mCopyAmt.value();
                if (k > 0.001)
                {
                    // R-CLIP-9: a "+" on the copy in hand
                    const Rect b{dr.right() - 18.0, dr.y + 3.0, 15.0, 15.0};
                    Color bg = palette::primary(), fg = palette::primaryForeground();
                    bg.a *= k;
                    fg.a *= k;
                    drawRoundedRect(t, b, radius::pill(), Paint::filled(bg));
                    t.setStroke(fg, 1.4);
                    t.beginPath();
                    t.moveTo(b.x + 4.0, b.y + b.h * 0.5); t.lineTo(b.right() - 4.0, b.y + b.h * 0.5);
                    t.moveTo(b.x + b.w * 0.5, b.y + 4.0); t.lineTo(b.x + b.w * 0.5, b.bottom() - 4.0);
                    t.strokePath();
                }
            }
        paintAutomation(t); // the curves, in the clips' clip
        paintTakes(t);      // R-REC-2: a clip's takes, under its lane
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
            if (!d.takeClip.empty())
            {
                // R-REC-2: a take's row — its number, indented under its clip's lane
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(d.label, 22.0, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(0, r.bottom() - 0.5); t.lineTo(W, r.bottom() - 0.5); t.strokePath();
                continue;
            }
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
            const bool own = d.lane.empty() && !d.folded;
            const double nameW = d.strip.empty() ? kHeaderW - 20.0 : kHeaderW - 40.0; // a track keeps room for its arm button
            drawNameWithId(t, d.label, d.folded ? d.group : own ? row.key.substr(row.key.find(':') + 1) : d.lane, 12.0, textfit::baseline(r.y + 16.0, 11.0), nameW,
                           11.0, font::sans(), own ? palette::mutedForeground() : palette::foreground(), idsAmount ? idsAmount() : 0.0, a);
            if (d.folded || d.groupHead)
            {
                // R-ARR-3: the chevron (turning as it folds) and the group's name — or, folded, how many lanes it holds
                const double turn = groupTurn(d.group), cx = 15.0, cy = r.y + 40.5, ang = -1.5707963 * turn;
                auto pt = [&](double x, double y) { return Point{cx + x * std::cos(ang) - y * std::sin(ang), cy + x * std::sin(ang) + y * std::cos(ang)}; };
                const Point p0 = pt(-3.5, -1.75), p1 = pt(3.5, -1.75), p2 = pt(0.0, 2.25);
                t.setFill(fade(palette::secondaryForeground(), a));
                t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.lineTo(p2.x, p2.y); t.closePath(); t.fillPath();
                std::string band = d.folded ? std::to_string(d.groupLanes) + (d.groupLanes == 1 ? " LANE" : " LANES") : d.groupName;
                if (!d.folded)
                    for (auto &ch : band) ch = (char)std::toupper((unsigned char)ch);
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, band, kHeaderW - 34.0, 8.0, font::sansSemiBold()), 23.0, textfit::baseline(cy, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
            }
            if (!d.strip.empty())
            {
                // R-MIDI-2: the arm button — a ring, filled red as it is armed
                const Rect ar{kHeaderW - 24.0, r.y + 9.75, 13.0, 13.0};
                const double lit = armAmount(d.strip);
                drawRoundedRect(t, ar, radius::pill(), Paint::filledStroked(fade(lerpColor(palette::input(), palette::destructive(), lit), a),
                                                                           fade(lerpColor(palette::mutedForeground(), palette::destructive(), lit), a), 1.0));
            }
            if (own)
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText("its strip's row", 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::sans());
            }
            else if (d.folded)
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText("a group, folded", 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::sans());
            }
            else if (const auto tk = mTracks.find(d.lane); tk != mTracks.end() && tk->second.amt.value() > 0.001 && !tk->second.sub.empty())
            {
                // R-LANE-3: an instrument's track names its instrument
                t.setFill(fade(palette::mutedForeground(), a * tk->second.amt.value()));
                t.drawText(textfit::ellipsize(t, tk->second.sub, kHeaderW - 20.0, 9.0, font::sans()), 12.0, textfit::baseline(r.y + 31.0, 9.0), 9.0, font::sans());
            }
            t.setStroke(fade(palette::border(), a), 1.0);
            t.beginPath(); t.moveTo(0, r.bottom() - 0.5); t.lineTo(W, r.bottom() - 0.5); t.strokePath();
        }
        {
            // R-ARR-3: an open group's bar down its lanes' headers — from where its rows ARE (eased), as faint as they are
            std::map<std::string, std::pair<double, double>> span;
            std::map<std::string, double> alpha;
            for (const auto &row : mRowMotion.rows())
            {
                if (row.data.group.empty() || row.data.folded || row.liveAlpha() <= 0.001) continue;
                const double y = kRulerH + row.liveY() - mScrollY.value();
                auto it = span.find(row.data.group);
                if (it == span.end()) span[row.data.group] = {y, y + kRowH};
                else { it->second.first = std::min(it->second.first, y); it->second.second = std::max(it->second.second, y + kRowH); }
                alpha[row.data.group] = std::max(alpha[row.data.group], row.liveAlpha());
            }
            for (const auto &g : span)
                drawRoundedRect(t, Rect{5.5, g.second.first + 6.0, 2.0, g.second.second - g.second.first - 12.0}, radius::pill(),
                                Paint::filled(palette::whiteAlpha(0.22 * alpha[g.first])));
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
        paintMarkers(t); // R-ARR-1: the ruler's band
        if (const Rect pr = punchRect(); pr.w > 0 && mPunchAmt.value() > 0.001) // R-REC-1: where a recording keeps — red
        {
            t.save();
            t.clipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
            drawRoundedRect(t, pr, radius::control(), Paint::filled(fade(palette::destructive(), 0.9 * mPunchAmt.value())));
            t.restore();
        }
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kRulerH - 0.5); t.lineTo(W, kRulerH - 0.5); t.strokePath();

        // the cue (R-TIME-7): an outlined mark on the ruler where Stop takes the time bar back, while playing
        if (mCueAmt.value() > 0.001)
        {
            const double cx = std::round(beatToX(mCue)) + 0.5;
            if (cx >= kHeaderW - 1.0 && cx <= W)
            {
                const double ca = mCueAmt.value();
                Color pc = surface::playhead();
                pc.a *= 0.85 * ca;
                t.setStroke(pc, 1.25);
                t.beginPath(); t.moveTo(cx - 5.0, 0.75); t.lineTo(cx + 5.0, 0.75); t.lineTo(cx, 7.5); t.closePath(); t.strokePath();
                pc.a *= 0.35;
                t.setStroke(pc, 1.0);
                t.beginPath(); t.moveTo(cx, 7.5); t.lineTo(cx, rulerRect().bottom()); t.strokePath();
            }
        }
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
